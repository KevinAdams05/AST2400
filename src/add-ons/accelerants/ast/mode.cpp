/*
 * AST2400: Haiku graphics driver for ASPEED AST2400/2500/2600 BMC GPUs.
 *
 * Copyright 2012 Red Hat Inc.
 *   Authors:
 *     Dave Airlie <airlied@redhat.com>
 *   Original Linux drivers/gpu/drm/ast/ast_mode.c (ast_set_std_reg,
 *   ast_set_crtc_reg, ast_set_dclk_reg, ast_set_color_reg,
 *   ast_set_sync_reg, ast_set_offset_reg, ast_set_start_address_crt1,
 *   ast_set_vbios_mode_reg, ast_wait_for_vretrace).
 *
 * Copyright (c) 2005 ASPEED Technology Inc.
 *   Original xf86-video-ast tables (vbios_stdtable, ast_2000_dclk_table).
 *
 *   Copyright (C) 2026 Kevin Adams <kevinadams05@gmail.com>.
 *   (Haiku port — accelerant API, Haiku types, _sPrintf logging, no
 *   per-silicon-rev quirks yet, AST2400 path only.)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *  [...standard GPL v2 boilerplate...]
 *
 * Phase 3 scope:
 *   - One hardcoded mode: 1024x768@60 @ 32 bpp.
 *   - Programs sequencer, CRTC, attribute, graphics, DAC, and PLL.
 *   - No EDID readback yet (Phase 4).
 *   - No per-generation quirks (AST2500/2600 use the same code path
 *     pending hardware testing).
 */

#include "accelerant.h"
#include "ast_regs.h"

#include <Debug.h>


#undef TRACE
#define TRACE_AST_MODE	1
#if TRACE_AST_MODE
#	define TRACE(x...) _sPrintf("ast.mode: " x)
#else
#	define TRACE(x...) ((void)0)
#endif


// === Standard VGA register-default tables, ported verbatim from Linux's
// === ast_tables.h. Index by AST_STD_*_MODE. ===

const struct ast_std_table kStdTables[5] = {
	/* AST_STD_TEXT_MODE — MD_2_3_400 */
	{
		0x67,
		{0x00, 0x03, 0x00, 0x02},
		{0x5f, 0x4f, 0x50, 0x82, 0x55, 0x81, 0xbf, 0x1f,
		 0x00, 0x4f, 0x0d, 0x0e, 0x00, 0x00, 0x00, 0x00,
		 0x9c, 0x8e, 0x8f, 0x28, 0x1f, 0x96, 0xb9, 0xa3,
		 0xff},
		{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
		 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
		 0x0c, 0x00, 0x0f, 0x08},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0e, 0x00,
		 0xff}
	},
	/* AST_STD_EGA_MODE — Mode12/ExtEGATable */
	{
		0xe3,
		{0x01, 0x0f, 0x00, 0x06},
		{0x5f, 0x4f, 0x50, 0x82, 0x55, 0x81, 0x0b, 0x3e,
		 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		 0xe9, 0x8b, 0xdf, 0x28, 0x00, 0xe7, 0x04, 0xe3,
		 0xff},
		{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
		 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
		 0x01, 0x00, 0x0f, 0x00},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0f,
		 0xff}
	},
	/* AST_STD_VGA_MODE — ExtVGATable (8 bpp) */
	{
		0x2f,
		{0x01, 0x0f, 0x00, 0x0e},
		{0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0x0b, 0x3e,
		 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		 0xea, 0x8c, 0xdf, 0x28, 0x40, 0xe7, 0x04, 0xa3,
		 0xff},
		{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		 0x01, 0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0f,
		 0xff}
	},
	/* AST_STD_HIC_MODE — ExtHiCTable (16 bpp) */
	{
		0x2f,
		{0x01, 0x0f, 0x00, 0x0e},
		{0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0x0b, 0x3e,
		 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		 0xea, 0x8c, 0xdf, 0x28, 0x40, 0xe7, 0x04, 0xa3,
		 0xff},
		{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		 0x01, 0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0f,
		 0xff}
	},
	/* AST_STD_TRUEC_MODE — ExtTrueCTable (32 bpp) */
	{
		0x2f,
		{0x01, 0x0f, 0x00, 0x0e},
		{0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0x0b, 0x3e,
		 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		 0xea, 0x8c, 0xdf, 0x28, 0x40, 0xe7, 0x04, 0xa3,
		 0xff},
		{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		 0x01, 0x00, 0x00, 0x00},
		{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0f,
		 0xff}
	},
};


// === DCLK PLL parameter table, ported from Linux ast_2000.c
// === ast_2000_dclk_table[]. AST2400 inherits the AST2000 PLL math
// === per ast_2400_init() in Linux.

const struct ast_dclk_info kDclkTable[] = {
	{0x2c, 0xe7, 0x03},		/* 00: VCLK25_175  */
	{0x95, 0x62, 0x03},		/* 01: VCLK28_322  */
	{0x67, 0x63, 0x01},		/* 02: VCLK31_5    */
	{0x76, 0x63, 0x01},		/* 03: VCLK36      */
	{0xee, 0x67, 0x01},		/* 04: VCLK40      */
	{0x82, 0x62, 0x01},		/* 05: VCLK49_5    */
	{0xc6, 0x64, 0x01},		/* 06: VCLK50      */
	{0x94, 0x62, 0x01},		/* 07: VCLK56_25   */
	{0x80, 0x64, 0x00},		/* 08: VCLK65 — 1024x768@60 */
	{0x7b, 0x63, 0x00},		/* 09: VCLK75      */
	{0x67, 0x62, 0x00},		/* 0a: VCLK78_75   */
	{0x7c, 0x62, 0x00},		/* 0b: VCLK94_5    */
	{0x8e, 0x62, 0x00},		/* 0c: VCLK108     */
	{0x85, 0x24, 0x00},		/* 0d: VCLK135     */
	{0x67, 0x22, 0x00},		/* 0e: VCLK157_5   */
	{0x6a, 0x22, 0x00},		/* 0f: VCLK162     */
	{0x4d, 0x4c, 0x80},		/* 10: VCLK154     */
	{0x68, 0x6f, 0x80},		/* 11: VCLK83_5    */
	{0x28, 0x49, 0x80},		/* 12: VCLK106_5   */
	{0x37, 0x49, 0x80},		/* 13: VCLK146_25  */
	{0x1f, 0x45, 0x80},		/* 14: VCLK148_5 — 1920x1080@60 */
};


// === Mode table for Phase 4.0. All 4:3 VESA-standard 60Hz modes whose
// === pixel clocks fall within our existing DCLK table (VCLK25_175 –
// === VCLK162). Ported from Linux ast_vbios.c res_640x480 / res_800x600
// === / res_1024x768 / res_1280x1024 / res_1600x1200. ===

const struct ast_mode_info kModeList[] = {
	/* 640x480@60 — VESA */
	{ 800, 640, 16, 96, 525, 480, 10, 2, AST_VCLK25_175,
	  AST_FLAG_NHSYNC | AST_FLAG_NVSYNC | AST_FLAG_CHARX8DOT,
	  60, 1, 0x2e },

	/* 800x600@60 — VESA */
	{ 1056, 800, 40, 128, 628, 600, 1, 4, AST_VCLK40,
	  AST_FLAG_PHSYNC | AST_FLAG_PVSYNC | AST_FLAG_CHARX8DOT,
	  60, 2, 0x30 },

	/* 1024x768@60 — VESA. Originally Phase 3 single mode. */
	{ 1344, 1024, 24, 136, 806, 768, 3, 6, AST_VCLK65,
	  AST_FLAG_NHSYNC | AST_FLAG_NVSYNC | AST_FLAG_CHARX8DOT,
	  60, 1, 0x31 },

	/* 1280x1024@60 — VESA */
	{ 1688, 1280, 48, 112, 1066, 1024, 1, 3, AST_VCLK108,
	  AST_FLAG_PHSYNC | AST_FLAG_PVSYNC | AST_FLAG_CHARX8DOT,
	  60, 1, 0x32 },

	/* 1600x1200@60 — VESA */
	{ 2160, 1600, 64, 192, 1250, 1200, 1, 3, AST_VCLK162,
	  AST_FLAG_PHSYNC | AST_FLAG_PVSYNC | AST_FLAG_CHARX8DOT,
	  60, 1, 0x33 },

	/* 1920x1080@60 — CEA / EDID preferred timing for most modern
	 * desktop monitors. AST2500PreCatchCRT flag from Linux only fires
	 * on AST2500/2600 (quirks->crtc_hsync_precatch_needed), so it's a
	 * no-op for our AST2400 path and we don't set it. */
	{ 2200, 1920, 88, 44, 1125, 1080, 4, 5, AST_VCLK148_5,
	  AST_FLAG_PHSYNC | AST_FLAG_PVSYNC | AST_FLAG_CHARX8DOT
		| AST_FLAG_WIDESCREEN | AST_FLAG_NEWMODEINFO,
	  60, 1, 0x38 },

	/* === 16:10 widescreen modes. Pixel clocks chosen to fit the AST2400's
	 * ~165 MHz DAC ceiling: the three smaller modes use their standard
	 * (non-reduced-blanking) VESA DMT timings, while 1920x1200 uses CVT
	 * reduced-blanking (154 MHz) because its non-RB clock (193 MHz) would
	 * overrun the DAC. Ported from Linux ast_vbios.c res_1280x800 /
	 * res_1440x900 / res_1680x1050 / res_1920x1200. The AST2500PreCatchCRT
	 * flag Linux sets on the reduced-blanking variants is AST2500/2600-only
	 * (quirks->crtc_hsync_precatch_needed), so we omit it on the AST2400
	 * path — same reasoning as the 1920x1080 entry above. === */

	/* 1280x800@60 — WXGA, VESA DMT. SyncPN: H negative, V positive. */
	{ 1680, 1280, 72, 128, 831, 800, 3, 6, AST_VCLK83_5,
	  AST_FLAG_NHSYNC | AST_FLAG_PVSYNC | AST_FLAG_CHARX8DOT
		| AST_FLAG_WIDESCREEN | AST_FLAG_NEWMODEINFO,
	  60, 2, 0x35 },

	/* 1440x900@60 — WXGA+, VESA DMT. SyncPN: H negative, V positive. */
	{ 1904, 1440, 80, 152, 934, 900, 3, 6, AST_VCLK106_5,
	  AST_FLAG_NHSYNC | AST_FLAG_PVSYNC | AST_FLAG_CHARX8DOT
		| AST_FLAG_WIDESCREEN | AST_FLAG_NEWMODEINFO,
	  60, 2, 0x36 },

	/* 1680x1050@60 — WSXGA+, VESA DMT. SyncPN: H negative, V positive. */
	{ 2240, 1680, 104, 176, 1089, 1050, 3, 6, AST_VCLK146_25,
	  AST_FLAG_NHSYNC | AST_FLAG_PVSYNC | AST_FLAG_CHARX8DOT
		| AST_FLAG_WIDESCREEN | AST_FLAG_NEWMODEINFO,
	  60, 2, 0x37 },

	/* 1920x1200@60 — WUXGA, CVT reduced-blanking (154 MHz fits the DAC).
	 * SyncNP: H positive, V negative. */
	{ 2080, 1920, 48, 32, 1235, 1200, 3, 6, AST_VCLK154,
	  AST_FLAG_PHSYNC | AST_FLAG_NVSYNC | AST_FLAG_CHARX8DOT
		| AST_FLAG_WIDESCREEN | AST_FLAG_NEWMODEINFO,
	  60, 1, 0x34 },
};

const uint32 kModeCount = sizeof(kModeList) / sizeof(kModeList[0]);


/*! Look up an entry in kModeList by (width, height, refresh). Returns
 *  NULL if no match. Used by SET_DISPLAY_MODE to find which mode to
 *  program. */
const struct ast_mode_info*
ast_find_mode(uint32 width, uint32 height, uint32 refreshHz)
{
	for (uint32 i = 0; i < kModeCount; i++) {
		const ast_mode_info& m = kModeList[i];
		if (m.hActive == width && m.vActive == height
				&& m.refreshRate == refreshHz)
			return &kModeList[i];
	}
	return NULL;
}


// === MMIO register access ==================================================

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


/*! Indexed VGA register write: write `index` to base register, then `value`
 *  to (base+1). Used for sequencer (SR), CRTC (CR), and graphics (GR). */
static inline void
set_index_reg(uint32 base, uint8 index, uint8 value)
{
	vga_io_write8(base, index);
	vga_io_write8(base + 1, value);
}


static inline uint8
get_index_reg(uint32 base, uint8 index)
{
	vga_io_write8(base, index);
	return vga_io_read8(base + 1);
}


/*! Read-modify-write indexed register. `preserveMask` is the bits to keep
 *  from the current register value; cleared bits get the new value. */
static inline void
set_index_reg_mask(uint32 base, uint8 index, uint8 preserveMask, uint8 value)
{
	uint8 current = get_index_reg(base, index);
	uint8 newValue = (current & preserveMask) | (value & ~preserveMask);
	set_index_reg(base, index, newValue);
}


// === Helpers ==============================================================

static void
ast_open_key()
{
	// Writing the magic password to CR80 unlocks the extension CRs (0x80+).
	set_index_reg(AST_IO_VGACRI, 0x80, AST_IO_VGACR80_PASSWORD);
}


/*! Spin until the chip enters vertical retrace, so we don't reprogram CRTC
 *  registers while a scanout is in progress. Bounded — we give up after
 *  ~100ms rather than hang the accelerant. */
static void
ast_wait_for_vretrace()
{
	bigtime_t deadline = system_time() + 100000;	// 100 ms
	while (system_time() < deadline) {
		if ((vga_io_read8(AST_IO_VGAIR1_R) & AST_IO_VGAIR1_VREFRESH) != 0)
			return;
	}
	TRACE("ast_wait_for_vretrace: timeout\n");
}


// === Mode-set steps =======================================================

static void
ast_set_vbios_mode_reg(const ast_mode_info& mode)
{
	// VBIOS mode metadata, written into the AST's "scratch" extension CRs.
	// Used by IPMI/iKVM clients to know what mode the host is in.
	set_index_reg(AST_IO_VGACRI, 0x8d, mode.refreshRateIndex & 0xff);
	set_index_reg(AST_IO_VGACRI, 0x8e, mode.modeId & 0xff);
	set_index_reg(AST_IO_VGACRI, 0x91, 0xa8);	// "new mode info" marker
	// Pixel clock in MHz, dimensions, color depth — for the iKVM viewer.
	uint32 pixelClockKhz =
		mode.hTotal * mode.vTotal * mode.refreshRate / 1000;
	set_index_reg(AST_IO_VGACRI, 0x92, 32);		// bpp
	set_index_reg(AST_IO_VGACRI, 0x93, pixelClockKhz / 1000);
	set_index_reg(AST_IO_VGACRI, 0x94, mode.hActive & 0xff);
	set_index_reg(AST_IO_VGACRI, 0x95, (mode.hActive >> 8) & 0xff);
	set_index_reg(AST_IO_VGACRI, 0x96, mode.vActive & 0xff);
	set_index_reg(AST_IO_VGACRI, 0x97, (mode.vActive >> 8) & 0xff);
}


/*! Write the "standard" VGA register defaults from a stdtable. Ported from
 *  Linux ast_set_std_reg. */
static void
ast_set_std_reg(const ast_std_table& stdTable)
{
	// Miscellaneous output register.
	vga_io_write8(AST_IO_VGAMR_W, stdTable.misc);

	// Sequencer registers (index 0 is "reset", set to 3 to begin).
	set_index_reg(AST_IO_VGASRI, 0x00, 0x03);
	set_index_reg_mask(AST_IO_VGASRI, 0x01, 0xdf, stdTable.seq[0]);
	for (uint32 i = 1; i < 4; i++)
		set_index_reg(AST_IO_VGASRI, i + 1, stdTable.seq[i]);

	// CRTC registers — except the timing slots that ast_set_crtc_reg will
	// overwrite next, and CR13/CRb0 (stride) and CR0c/CR0d (start address).
	// Match Linux's masking: 0x11 has bit 7 = "protect" (clear before writes).
	set_index_reg_mask(AST_IO_VGACRI, 0x11, 0x7f, 0x00);
	for (uint32 i = 0; i < 12; i++)
		set_index_reg(AST_IO_VGACRI, i, stdTable.crtc[i]);
	for (uint32 i = 14; i < 19; i++)
		set_index_reg(AST_IO_VGACRI, i, stdTable.crtc[i]);
	for (uint32 i = 20; i < 25; i++)
		set_index_reg(AST_IO_VGACRI, i, stdTable.crtc[i]);

	// Attribute registers — write index, then value, alternating through
	// the same write port. Read VGAIR1 first to reset the AR flip-flop.
	(void)vga_io_read8(AST_IO_VGAIR1_R);
	for (uint32 i = 0; i < 20; i++) {
		vga_io_write8(AST_IO_VGAARI_W, (uint8)i);
		vga_io_write8(AST_IO_VGAARI_W, stdTable.ar[i]);
	}
	vga_io_write8(AST_IO_VGAARI_W, 0x14);	// PCS register
	vga_io_write8(AST_IO_VGAARI_W, 0x00);
	(void)vga_io_read8(AST_IO_VGAIR1_R);
	vga_io_write8(AST_IO_VGAARI_W, 0x20);	// re-enable video output

	// Graphics registers.
	for (uint32 i = 0; i < 9; i++)
		set_index_reg(AST_IO_VGAGRI, i, stdTable.gr[i]);
}


/*! Program CRTC timing registers from the mode info. Ported from Linux
 *  ast_set_crtc_reg. The horizontal-timing values are divided by 8 because
 *  the CRTC counts in character cells of 8 pixels (Charx8Dot). */
static void
ast_set_crtc_reg(const ast_mode_info& mode)
{
	uint8 jreg05 = 0, jreg07 = 0, jreg09 = 0;
	uint8 jregAC = 0, jregAD = 0, jregAE = 0;

	// Active horizontal timing values (Linux's crtc_htotal etc.).
	uint32 htotal = mode.hTotal;
	uint32 hdisplay = mode.hActive;
	uint32 hblank_start = mode.hActive;
	uint32 hblank_end = mode.hTotal;
	uint32 hsync_start = mode.hActive + mode.hFrontPorch;
	uint32 hsync_end = hsync_start + mode.hSync;
	uint32 vtotal = mode.vTotal;
	uint32 vdisplay = mode.vActive;
	uint32 vblank_start = mode.vActive;
	uint32 vblank_end = mode.vTotal;
	uint32 vsync_start = mode.vActive + mode.vFrontPorch;
	uint32 vsync_end = vsync_start + mode.vSync;

	// Unlock CRTC protect.
	set_index_reg_mask(AST_IO_VGACRI, 0x11, 0x7f, 0x00);

	uint32 temp;

	// CR00 Horizontal Total.
	temp = (htotal >> 3) - 5;
	if (temp & 0x100) jregAC |= 0x01;
	set_index_reg_mask(AST_IO_VGACRI, 0x00, 0x00, temp);

	// CR01 Horizontal Display End.
	temp = (hdisplay >> 3) - 1;
	if (temp & 0x100) jregAC |= 0x04;
	set_index_reg_mask(AST_IO_VGACRI, 0x01, 0x00, temp);

	// CR02 Horizontal Blank Start.
	temp = (hblank_start >> 3) - 1;
	if (temp & 0x100) jregAC |= 0x10;
	set_index_reg_mask(AST_IO_VGACRI, 0x02, 0x00, temp);

	// CR03 Horizontal Blank End (low bits).
	temp = ((hblank_end >> 3) - 1) & 0x7f;
	if (temp & 0x20) jreg05 |= 0x80;
	if (temp & 0x40) jregAD |= 0x01;
	set_index_reg_mask(AST_IO_VGACRI, 0x03, 0xE0, temp & 0x1f);

	// CR04 Horizontal Sync Start.
	temp = (hsync_start >> 3) - 1;
	if (temp & 0x100) jregAC |= 0x40;
	set_index_reg_mask(AST_IO_VGACRI, 0x04, 0x00, temp);

	// CR05 Horizontal Sync End.
	temp = ((hsync_end >> 3) - 1) & 0x3f;
	if (temp & 0x20) jregAD |= 0x04;
	set_index_reg_mask(AST_IO_VGACRI, 0x05, 0x60,
		(uint8)((temp & 0x1f) | jreg05));

	// Extended H timing high-bits in CR_AC and CR_AD.
	set_index_reg_mask(AST_IO_VGACRI, 0xAC, 0x00, jregAC);
	set_index_reg_mask(AST_IO_VGACRI, 0xAD, 0x00, jregAD);

	// CR_FC: no special quirks for our 1024x768 mode.
	set_index_reg_mask(AST_IO_VGACRI, 0xFC, 0xFD, 0x00);

	// CR06 Vertical Total.
	temp = vtotal - 2;
	if (temp & 0x100) jreg07 |= 0x01;
	if (temp & 0x200) jreg07 |= 0x20;
	if (temp & 0x400) jregAE |= 0x01;
	set_index_reg_mask(AST_IO_VGACRI, 0x06, 0x00, temp);

	// CR10 Vertical Sync Start.
	temp = vsync_start - 1;
	if (temp & 0x100) jreg07 |= 0x04;
	if (temp & 0x200) jreg07 |= 0x80;
	if (temp & 0x400) jregAE |= 0x08;
	set_index_reg_mask(AST_IO_VGACRI, 0x10, 0x00, temp);

	// CR11 Vertical Sync End (low bits).
	temp = (vsync_end - 1) & 0x3f;
	if (temp & 0x10) jregAE |= 0x20;
	if (temp & 0x20) jregAE |= 0x40;
	set_index_reg_mask(AST_IO_VGACRI, 0x11, 0x70, temp & 0xf);

	// CR12 Vertical Display End.
	temp = vdisplay - 1;
	if (temp & 0x100) jreg07 |= 0x02;
	if (temp & 0x200) jreg07 |= 0x40;
	if (temp & 0x400) jregAE |= 0x02;
	set_index_reg_mask(AST_IO_VGACRI, 0x12, 0x00, temp);

	// CR15 Vertical Blank Start.
	temp = vblank_start - 1;
	if (temp & 0x100) jreg07 |= 0x08;
	if (temp & 0x200) jreg09 |= 0x20;
	if (temp & 0x400) jregAE |= 0x04;
	set_index_reg_mask(AST_IO_VGACRI, 0x15, 0x00, temp);

	// CR16 Vertical Blank End.
	temp = vblank_end - 1;
	if (temp & 0x100) jregAE |= 0x10;
	set_index_reg_mask(AST_IO_VGACRI, 0x16, 0x00, temp);

	// Combine V-timing high bits into CR07, CR09, CR_AE.
	set_index_reg_mask(AST_IO_VGACRI, 0x07, 0x00, jreg07);
	set_index_reg_mask(AST_IO_VGACRI, 0x09, 0xdf, jreg09);
	set_index_reg_mask(AST_IO_VGACRI, 0xAE, 0x00, jregAE | 0x80);

	// CR_B6: H/V sync off bits cleared (we want sync on).
	set_index_reg_mask(AST_IO_VGACRI, 0xb6, 0x3f, 0x00);

	// Re-protect CRTC registers (and assert "sync enable" in CR17).
	set_index_reg_mask(AST_IO_VGACRI, 0x11, 0x7f, 0x80);
}


/*! Set the scanout pitch (stride) in CR13 + CR_B0. Stride is divided by 8
 *  because it's measured in character cells. */
static void
ast_set_offset_reg(uint32 bytesPerRow)
{
	uint16 offset = bytesPerRow >> 3;
	set_index_reg(AST_IO_VGACRI, 0x13, offset & 0xff);
	set_index_reg(AST_IO_VGACRI, 0xb0, (offset >> 8) & 0x3f);
}


/*! Program the pixel-clock PLL. Ported from Linux ast_set_dclk_reg. */
static void
ast_set_dclk_reg(uint32 dclkIndex)
{
	const ast_dclk_info& info = kDclkTable[dclkIndex];
	set_index_reg_mask(AST_IO_VGACRI, 0xc0, 0x00, info.param1);
	set_index_reg_mask(AST_IO_VGACRI, 0xc1, 0x00, info.param2);
	// CR_BB: high bits 6-7 are param3 bits 6-7; bits 4-5 are param3 bits 0-1.
	// Low nibble preserved.
	set_index_reg_mask(AST_IO_VGACRI, 0xbb, 0x0f,
		(uint8)((info.param3 & 0xc0) | ((info.param3 & 0x3) << 4)));
}


/*! Set the color depth via the ASPEED-extension color regs. Ported from
 *  Linux ast_set_color_reg. */
static void
ast_set_color_reg(uint32 bitsPerPixel)
{
	uint8 jregA0 = 0, jregA3 = 0, jregA8 = 0;
	switch (bitsPerPixel) {
		case 8:
			jregA0 = 0x70;
			jregA3 = 0x01;
			jregA8 = 0x00;
			break;
		case 15:
		case 16:
			jregA0 = 0x70;
			jregA3 = 0x04;
			jregA8 = 0x02;
			break;
		case 32:
		default:
			jregA0 = 0x70;
			jregA3 = 0x08;
			// Linux sets bit 1 of CR_A8 for 32bpp; on our AST2400 with
			// Haiku's BGR- byte order in memory this produces R↔B
			// swapped colors. Per-boot register dump (0.0.8) showed the
			// VBIOS leaves bit 1 clear in 32bpp mode and that
			// configuration scans out the same memory with correct
			// colors. Linux must compensate for this in ast_post.c
			// silicon-init code we haven't ported yet.
			jregA8 = 0x00;
			break;
	}
	set_index_reg_mask(AST_IO_VGACRI, 0xa0, 0x8f, jregA0);
	set_index_reg_mask(AST_IO_VGACRI, 0xa3, 0xf0, jregA3);
	set_index_reg_mask(AST_IO_VGACRI, 0xa8, 0xfd, jregA8);
}


/*! Sync polarity for H and V. Ported from Linux ast_set_sync_reg. */
static void
ast_set_sync_reg(const ast_mode_info& mode)
{
	uint8 jreg = vga_io_read8(AST_IO_VGAMR_R);
	jreg &= ~0xc0;
	if ((mode.flags & AST_FLAG_NVSYNC) != 0) jreg |= 0x80;
	if ((mode.flags & AST_FLAG_NHSYNC) != 0) jreg |= 0x40;
	vga_io_write8(AST_IO_VGAMR_W, jreg);
}


/*! Program the scanout base address (offset within BAR0 framebuffer).
 *  For our use it's always 0. Ported from Linux ast_set_start_address_crt1. */
static void
ast_set_start_address(uint32 offset)
{
	uint32 addr = offset >> 2;
	set_index_reg(AST_IO_VGACRI, 0x0d, (uint8)(addr & 0xff));
	set_index_reg(AST_IO_VGACRI, 0x0c, (uint8)((addr >> 8) & 0xff));
	set_index_reg(AST_IO_VGACRI, 0xaf, (uint8)((addr >> 16) & 0xff));
}


// === Color/format register diagnostic dump ==============================
//
// Diagnostic helper to print the chip's color / format register state
// before and after our mode-set sequence. Used to track down the R↔B
// swap seen in 0.0.5 — we can't fix what we can't see, and the AST
// register docs are sparse, so the empirical-comparison approach is
// faster than guessing.

static void
dump_color_regs(const char* when)
{
	TRACE("== color/format regs %s ==\n", when);
	TRACE("  VGAMR (misc out) = 0x%02x\n", vga_io_read8(AST_IO_VGAMR_R));
	TRACE("  SR01 (clocking)  = 0x%02x\n", get_index_reg(AST_IO_VGASRI, 0x01));
	TRACE("  SR02 (planemask) = 0x%02x\n", get_index_reg(AST_IO_VGASRI, 0x02));
	TRACE("  SR03 (charmap)   = 0x%02x\n", get_index_reg(AST_IO_VGASRI, 0x03));
	TRACE("  SR04 (memmode)   = 0x%02x\n", get_index_reg(AST_IO_VGASRI, 0x04));
	TRACE("  GR05 (gfx mode)  = 0x%02x\n", get_index_reg(AST_IO_VGAGRI, 0x05));
	TRACE("  GR06 (misc)      = 0x%02x\n", get_index_reg(AST_IO_VGAGRI, 0x06));
	TRACE("  CR13 (pitch lo)  = 0x%02x\n", get_index_reg(AST_IO_VGACRI, 0x13));
	TRACE("  CR17 (mode ctl)  = 0x%02x\n", get_index_reg(AST_IO_VGACRI, 0x17));
	TRACE("  CR_A0 (ext ctl)  = 0x%02x\n", get_index_reg(AST_IO_VGACRI, 0xa0));
	TRACE("  CR_A3 (depth)    = 0x%02x\n", get_index_reg(AST_IO_VGACRI, 0xa3));
	TRACE("  CR_A8 (format)   = 0x%02x\n", get_index_reg(AST_IO_VGACRI, 0xa8));
	TRACE("  CR_B0 (pitch hi) = 0x%02x\n", get_index_reg(AST_IO_VGACRI, 0xb0));
	TRACE("  DAC PEL mask     = 0x%02x\n", vga_io_read8(0x46));	// 0x3C6
}


// === Public entry point ==================================================

extern "C" status_t
ast_program_mode(const ast_mode_info* mode)
{
	if (mode == NULL)
		return B_BAD_VALUE;

	TRACE("program_mode: %ux%u@%uHz 32bpp on AST chip gen %d\n",
		mode->hActive, mode->vActive, mode->refreshRate,
		(int)gInfo->sharedInfo->chipGeneration);

	dump_color_regs("BEFORE");

	// Wait for vertical retrace so we don't reprogram mid-scanout.
	ast_wait_for_vretrace();

	// Unlock extension registers.
	ast_open_key();

	// Tell the IPMI/iKVM viewer what mode we're going to.
	ast_set_vbios_mode_reg(*mode);

	// Mystery sequence from Linux's ast_crtc_helper_mode_set_nofb — CRA1
	// gets 0x06 right before std_reg programming. Bit 1 disables legacy
	// VGAIO (the chip should only respond to MMIO from this point).
	// Preserving for parity with upstream until we understand it better.
	set_index_reg(AST_IO_VGACRI, 0xa1, 0x06);

	// Program sequencer/CRTC/attribute/graphics defaults for 32 bpp.
	ast_set_std_reg(kStdTables[AST_STD_TRUEC_MODE]);

	// Program CRTC timing from our mode info.
	ast_set_crtc_reg(*mode);

	// PLL for the pixel clock.
	ast_set_dclk_reg(mode->dclkIndex);

	// Sync polarity.
	ast_set_sync_reg(*mode);

	// Color depth (32 bpp).
	ast_set_color_reg(32);

	// Framebuffer pitch (width * 4 bytes for 32bpp) and scanout base.
	ast_set_offset_reg(mode->hActive * 4);
	ast_set_start_address(0);

	dump_color_regs("AFTER");

	TRACE("program_mode: done\n");
	return B_OK;
}
