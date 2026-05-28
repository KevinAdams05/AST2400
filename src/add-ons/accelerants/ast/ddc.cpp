/*
 * AST2400: Haiku graphics driver for ASPEED AST2400/2500/2600 BMC GPUs.
 *
 * Copyright 2012 Red Hat Inc.
 *   Authors:
 *     Dave Airlie <airlied@redhat.com>
 *   Original Linux drivers/gpu/drm/ast/ast_ddc.c (SDA/SCL bit-bang
 *   pattern via CR_B7).
 *
 *   Copyright (C) 2026 Kevin Adams <kevinadams05@gmail.com>.
 *   (Haiku port — no Linux i2c_algo_bit_data dependency, hand-rolled
 *   I2C state machine, Haiku snooze() for timing, _sPrintf logging.)
 *
 * The upstream `drivers/gpu/drm/ast/ast_ddc.c` is MIT-licensed, the
 * Haiku port + combined AST2400 driver are GPL v2 per the project
 * licensing rules (see docs/STYLE_GUIDE.md §16.3).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *  [...standard GPL v2 boilerplate...]
 *
 * Phase 4.1 scope:
 *   - I2C bit-bang primitives via AST CR_B7.
 *   - Read 128-byte EDID base block from monitor at I2C address 0x50.
 *   - Log first part of the EDID to syslog (raw hex + key fields).
 *   - Does NOT yet filter the mode list — Phase 4.2.
 */

#include "accelerant.h"
#include "ast_regs.h"

#include <stdio.h>
#include <string.h>

#include <Debug.h>
#include <OS.h>


#undef TRACE
#define TRACE_AST_DDC	1
#if TRACE_AST_DDC
#	define TRACE(x...) _sPrintf("ast.ddc: " x)
#else
#	define TRACE(x...) ((void)0)
#endif


// === BAR1 MMIO helpers (duplicated from mode.cpp for translation-unit
// === independence — keeps ddc.cpp standalone without pulling all of
// === mode.cpp's static helpers in via a shared header). =================

static inline volatile uint8*
vga_io(uint32 offset)
{
	return (volatile uint8*)(gInfo->registers + AST_IO_MM_OFFSET + offset);
}


static inline uint8
vga_io_read8(uint32 reg)
{
	return *vga_io(reg);
}


static inline void
vga_io_write8(uint32 reg, uint8 value)
{
	*vga_io(reg) = value;
}


static inline uint8
get_index_reg(uint32 base, uint8 index)
{
	vga_io_write8(base, index);
	return vga_io_read8(base + 1);
}


static inline void
set_index_reg(uint32 base, uint8 index, uint8 value)
{
	vga_io_write8(base, index);
	vga_io_write8(base + 1, value);
}


static inline void
set_index_reg_mask(uint32 base, uint8 index, uint8 preserveMask, uint8 value)
{
	uint8 current = get_index_reg(base, index);
	set_index_reg(base, index, (current & preserveMask)
		| (value & ~preserveMask));
}


// === I2C line control via CR_B7 ==========================================
//
// AST_IO_VGACRI 0xb7 layout (from Linux ast_ddc.c):
//   bit 0: SCL out (inverted — set this bit to drive SCL low,
//          clear to release for pull-up to drive high)
//   bit 2: SDA out (inverted — same)
//   bit 4: SCL in  (1 = line is high, 0 = low)
//   bit 5: SDA in  (1 = line is high, 0 = low)
//
// "Inverted" means writing the inverse of the desired line state. To
// pull a line low we WRITE 1 (drive); to release (allow pull-up to
// drive high) we WRITE 0.

static const uint32 kI2cUdelay = 20;	// microseconds, matches Linux


static void
i2c_set_scl(bool high)
{
	uint8 value = high ? 0 : 1;
	// Mask 0xf4 preserves bits 7,6,5,4,2 — writes bits 0,1,3.
	set_index_reg_mask(AST_IO_VGACRI, 0xb7, 0xf4, value);
}


static void
i2c_set_sda(bool high)
{
	uint8 value = high ? 0 : (1 << 2);
	// Mask 0xf1 preserves bits 7,6,5,4,0 — writes bits 1,2,3.
	set_index_reg_mask(AST_IO_VGACRI, 0xb7, 0xf1, value);
}


static bool
i2c_get_sda()
{
	return (get_index_reg(AST_IO_VGACRI, 0xb7) & (1 << 5)) != 0;
}


// === I2C protocol (one master, no arbitration, no clock stretching) =====

static void
i2c_start()
{
	// Both lines high → SDA falls while SCL stays high → START.
	i2c_set_sda(true);
	i2c_set_scl(true);
	snooze(kI2cUdelay);
	i2c_set_sda(false);
	snooze(kI2cUdelay);
	i2c_set_scl(false);
	snooze(kI2cUdelay);
}


static void
i2c_stop()
{
	// SCL high while SDA rises from low → STOP.
	i2c_set_sda(false);
	snooze(kI2cUdelay);
	i2c_set_scl(true);
	snooze(kI2cUdelay);
	i2c_set_sda(true);
	snooze(kI2cUdelay);
}


/*! Write one byte to the bus. Returns true on ACK (slave pulled SDA low). */
static bool
i2c_write_byte(uint8 byte)
{
	for (int bit = 7; bit >= 0; bit--) {
		i2c_set_sda(((byte >> bit) & 1) != 0);
		snooze(kI2cUdelay);
		i2c_set_scl(true);
		snooze(kI2cUdelay);
		i2c_set_scl(false);
		snooze(kI2cUdelay);
	}

	// ACK bit: release SDA, clock high, sample SDA, clock low.
	i2c_set_sda(true);
	snooze(kI2cUdelay);
	i2c_set_scl(true);
	snooze(kI2cUdelay);
	bool ack = !i2c_get_sda();
	i2c_set_scl(false);
	snooze(kI2cUdelay);
	return ack;
}


static uint8
i2c_read_byte(bool ackToSlave)
{
	uint8 byte = 0;
	i2c_set_sda(true);	// release SDA so slave can drive it

	for (int bit = 7; bit >= 0; bit--) {
		snooze(kI2cUdelay);
		i2c_set_scl(true);
		snooze(kI2cUdelay);
		if (i2c_get_sda())
			byte |= (1 << bit);
		i2c_set_scl(false);
	}

	// ACK or NACK: drive SDA accordingly, clock high then low.
	i2c_set_sda(!ackToSlave);
	snooze(kI2cUdelay);
	i2c_set_scl(true);
	snooze(kI2cUdelay);
	i2c_set_scl(false);
	snooze(kI2cUdelay);

	return byte;
}


// === EDID readback =======================================================

static const uint8 kEdidI2cAddr = 0x50;
static const uint8 kEdidSize = 128;


extern "C" status_t
ast_read_edid_block(uint8 buffer[128])
{
	if (buffer == NULL || gInfo == NULL)
		return B_BAD_VALUE;
	memset(buffer, 0, kEdidSize);

	// Release both lines to begin in known idle state.
	i2c_set_scl(true);
	i2c_set_sda(true);
	snooze(kI2cUdelay * 2);

	// START + 0x50<<1 (write) + register offset 0
	i2c_start();
	if (!i2c_write_byte((uint8)(kEdidI2cAddr << 1))) {
		TRACE("no ACK for write-address 0x%02x — monitor not present?\n",
			kEdidI2cAddr);
		i2c_stop();
		return B_ENTRY_NOT_FOUND;
	}
	if (!i2c_write_byte(0x00)) {
		TRACE("no ACK for EDID offset write\n");
		i2c_stop();
		return B_IO_ERROR;
	}

	// Repeated START + 0x50<<1 | 1 (read)
	i2c_start();
	if (!i2c_write_byte((uint8)((kEdidI2cAddr << 1) | 1))) {
		TRACE("no ACK for read-address 0x%02x\n", kEdidI2cAddr);
		i2c_stop();
		return B_IO_ERROR;
	}

	for (uint32 i = 0; i < kEdidSize; i++) {
		bool sendAck = (i != kEdidSize - 1);	// NACK on last byte
		buffer[i] = i2c_read_byte(sendAck);
	}

	i2c_stop();

	// Quick sanity check: EDID 1.x blocks start with 00 FF FF FF FF FF FF 00.
	static const uint8 kEdidHeader[8]
		= { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };
	if (memcmp(buffer, kEdidHeader, 8) != 0) {
		TRACE("EDID header mismatch — got %02x %02x %02x %02x %02x %02x %02x %02x\n",
			buffer[0], buffer[1], buffer[2], buffer[3],
			buffer[4], buffer[5], buffer[6], buffer[7]);
		return B_BAD_DATA;
	}

	return B_OK;
}


/*! Dump EDID header + manufacturer + serial + a sample of detailed
 *  timings to syslog. For Phase 4.1 diagnostic only — Phase 4.2 will
 *  parse the detailed-timing descriptors into our mode list. */
extern "C" void
ast_log_edid(const uint8 buffer[128])
{
	TRACE("EDID dump:\n");
	for (uint32 row = 0; row < kEdidSize; row += 16) {
		TRACE("  %02x: %02x %02x %02x %02x %02x %02x %02x %02x  "
			"%02x %02x %02x %02x %02x %02x %02x %02x\n", row,
			buffer[row+0], buffer[row+1], buffer[row+2], buffer[row+3],
			buffer[row+4], buffer[row+5], buffer[row+6], buffer[row+7],
			buffer[row+8], buffer[row+9], buffer[row+10], buffer[row+11],
			buffer[row+12], buffer[row+13], buffer[row+14], buffer[row+15]);
	}

	// Manufacturer ID (bytes 8-9): two big-endian bytes containing a
	// 5+5+5-bit packed three-letter code, with 'A' = 1.
	uint16 mfgId = ((uint16)buffer[8] << 8) | buffer[9];
	char mfg[4];
	mfg[0] = 'A' - 1 + ((mfgId >> 10) & 0x1f);
	mfg[1] = 'A' - 1 + ((mfgId >> 5) & 0x1f);
	mfg[2] = 'A' - 1 + (mfgId & 0x1f);
	mfg[3] = '\0';
	TRACE("  manufacturer: %s  product: 0x%02x%02x  serial: 0x%02x%02x%02x%02x\n",
		mfg, buffer[11], buffer[10], buffer[15], buffer[14], buffer[13], buffer[12]);

	TRACE("  week %u  year %u  EDID ver %u.%u\n",
		buffer[16], 1990 + buffer[17], buffer[18], buffer[19]);

	// First detailed timing descriptor starts at byte 54.
	uint32 pixelClock10kHz
		= ((uint32)buffer[55] << 8) | buffer[54];
	uint32 hActive
		= (((uint32)(buffer[58] & 0xf0)) << 4) | buffer[56];
	uint32 vActive
		= (((uint32)(buffer[61] & 0xf0)) << 4) | buffer[59];
	TRACE("  preferred timing: %u x %u @ %u kHz pixel clock\n",
		hActive, vActive, pixelClock10kHz * 10);
}
