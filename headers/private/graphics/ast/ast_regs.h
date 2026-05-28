/*
 * AST2400: Haiku graphics driver for ASPEED AST2400/2500/2600 BMC GPUs.
 *
 * Copyright 2012 Red Hat Inc.
 *   Authors:
 *     Dave Airlie <airlied@redhat.com>
 *   Original Linux drivers/gpu/drm/ast/ast_reg.h
 *
 * Copyright (c) 2005 ASPEED Technology Inc.
 *   Original xf86-video-ast / ast_tables.h flag definitions
 *
 *   Copyright (C) 2026 Kevin Adams <kevinadams05@gmail.com>.
 *   (Haiku port — accelerant API, RAII, Haiku types, TRACE logging.)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *  [...standard GPL v2 boilerplate...]
 */
#ifndef AST_REGS_H
#define AST_REGS_H


#include <SupportDefs.h>


/*! MMIO offset where the legacy VGA I/O register window begins inside the
 *  AST's BAR1. VGA port `0x3C0` maps to BAR1 + 0x380 + 0x40, port `0x3D5`
 *  to BAR1 + 0x380 + 0x55, etc. — i.e. the AST_IO_xxx register-offset
 *  constants below are pre-subtracted (offset - 0x380). */
#define AST_IO_MM_OFFSET		0x380


/* === Standard VGA I/O port mapping (within the MMIO window) === */
#define AST_IO_VGAARI_W			0x40	/* attribute index/write   (0x3C0) */
#define AST_IO_VGAMR_W			0x42	/* miscellaneous out write (0x3C2) */
#define AST_IO_VGAER			0x43	/* feature control write   (0x3C3) */
#define AST_IO_VGASRI			0x44	/* sequencer index         (0x3C4) */
#define AST_IO_VGADRR			0x47	/* DAC read address        (0x3C7) */
#define AST_IO_VGADWR			0x48	/* DAC write address       (0x3C8) */
#define AST_IO_VGAPDR			0x49	/* DAC data port           (0x3C9) */
#define AST_IO_VGAMR_R			0x4c	/* misc out read           (0x3CC) */
#define AST_IO_VGAGRI			0x4E	/* graphics index          (0x3CE) */
#define AST_IO_VGACRI			0x54	/* CRTC index              (0x3D4) */
#define AST_IO_VGAIR1_R			0x5A	/* input status register 1 (0x3DA) */


/* === Bits === */
#define AST_IO_VGAMR_IOSEL		(1 << 0)
#define AST_IO_VGAER_VGA_ENABLE	(1 << 0)
#define AST_IO_VGASR1_SD		(1 << 5)
#define AST_IO_VGAIR1_VREFRESH	(1 << 3)

#define AST_IO_VGACR17_SYNC_ENABLE		(1 << 7)
#define AST_IO_VGACR80_PASSWORD			0xa8	/* writes to extension CRs gated by this */
#define AST_IO_VGACRA1_VGAIO_DISABLED	(1 << 1)
#define AST_IO_VGACRA1_MMIO_ENABLED		(1 << 2)


/* === ast_vbios_enhtable flag bits (from Linux ast_vbios.h) === */
#define AST_FLAG_CHARX8DOT			0x00000001
#define AST_FLAG_HALFDCLK			0x00000002
#define AST_FLAG_DOUBLESCAN			0x00000004
#define AST_FLAG_LINECOMPAREOFF		0x00000008
#define AST_FLAG_VBORDER			0x00000010
#define AST_FLAG_HBORDER			0x00000020
#define AST_FLAG_WIDESCREEN			0x00000100
#define AST_FLAG_NEWMODEINFO		0x00000200
#define AST_FLAG_NHSYNC				0x00000400
#define AST_FLAG_PHSYNC				0x00000800
#define AST_FLAG_NVSYNC				0x00001000
#define AST_FLAG_PVSYNC				0x00002000
#define AST_FLAG_AST2500PRECATCH	0x00004000


/* === DCLK table indices, for use as ast_vbios_enhtable.dclk_index === */
#define AST_VCLK25_175				0x00
#define AST_VCLK28_322				0x01
#define AST_VCLK31_5				0x02
#define AST_VCLK36					0x03
#define AST_VCLK40					0x04
#define AST_VCLK49_5				0x05
#define AST_VCLK50					0x06
#define AST_VCLK56_25				0x07
#define AST_VCLK65					0x08	/* 1024x768@60 */
#define AST_VCLK75					0x09
#define AST_VCLK78_75				0x0a
#define AST_VCLK94_5				0x0b
#define AST_VCLK108					0x0c
#define AST_VCLK135					0x0d
#define AST_VCLK157_5				0x0e
#define AST_VCLK162					0x0f
#define AST_VCLK154					0x10
#define AST_VCLK83_5				0x11
#define AST_VCLK106_5				0x12
#define AST_VCLK146_25				0x13
#define AST_VCLK148_5				0x14	/* 1920x1080@60 */


/* === VBIOS "std table" index — controls color depth via stdtable selection === */
#define AST_STD_TEXT_MODE			0
#define AST_STD_EGA_MODE			1
#define AST_STD_VGA_MODE			2	/* 8 bpp */
#define AST_STD_HIC_MODE			3	/* 16 bpp */
#define AST_STD_TRUEC_MODE			4	/* 32 bpp */


/* === Data structure for per-mode VBIOS info, ported from Linux ast_vbios.h
 *     ast_vbios_enhtable. Used by ast_set_crtc_reg, ast_set_dclk_reg,
 *     ast_set_sync_reg, ast_set_vbios_mode_reg. === */
struct ast_mode_info {
	uint32	hTotal;			/* total horizontal pixels (incl. blanking) */
	uint32	hActive;		/* horizontal active pixels */
	uint32	hFrontPorch;	/* horizontal front porch (pixels) */
	uint32	hSync;			/* horizontal sync pulse width (pixels) */
	uint32	vTotal;			/* total vertical lines (incl. blanking) */
	uint32	vActive;		/* vertical active lines */
	uint32	vFrontPorch;	/* vertical front porch (lines) */
	uint32	vSync;			/* vertical sync pulse width (lines) */
	uint32	dclkIndex;		/* index into AST DCLK table (AST_VCLK*) */
	uint32	flags;			/* AST_FLAG_* */
	uint32	refreshRate;	/* Hz */
	uint32	refreshRateIndex;
	uint32	modeId;			/* VBIOS mode id (0x31 for 1024x768) */
};


/* === ast_vbios_stdtable, ported. Holds the "standard VGA register"
 *     defaults to write for a given color depth. === */
struct ast_std_table {
	uint8	misc;
	uint8	seq[4];		/* sequencer regs 1..4 (index 0 is special) */
	uint8	crtc[25];	/* CRTC regs 0..24 */
	uint8	ar[20];		/* attribute regs 0..19 */
	uint8	gr[9];		/* graphics regs 0..8 */
};


/* === ast_vbios_dclk_info, ported. PLL parameters for a target pixel clock. === */
struct ast_dclk_info {
	uint8	param1;		/* CR0xc0 value */
	uint8	param2;		/* CR0xc1 value */
	uint8	param3;		/* CR0xbb high-nibble contribution */
};


/* Exported tables (defined in mode.cpp). */
extern const struct ast_std_table kStdTables[5];
extern const struct ast_dclk_info kDclkTable[];
extern const struct ast_mode_info kModeList[];
extern const uint32 kModeCount;


#endif	// AST_REGS_H
