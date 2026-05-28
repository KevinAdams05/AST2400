/*
 * AST2400: Haiku graphics driver for ASPEED AST2400/2500/2600 BMC GPUs.
 *   Copyright (C) 2026 Kevin Adams <kevinadams05@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */
#ifndef AST_DRIVER_INTERFACE_H
#define AST_DRIVER_INTERFACE_H


#include <Accelerant.h>
#include <Drivers.h>
#include <PCI.h>


#define AST_DRIVER_NAME			"ast"
#define AST_ACCELERANT_NAME		"ast.accelerant"

/*! Magic number written into shared_info to let the accelerant validate that
 *  it's talking to a matching driver build. Bump when the shared struct layout
 *  changes in an ABI-incompatible way. */
#define AST_PRIVATE_DATA_MAGIC	'astV'


/*! ASPEED PCI identifiers. The vendor is constant across the family; all
 *  AST2300 / AST2400 / AST2500 / AST2600 BMC GPUs share the same device ID
 *  and are distinguished at runtime via the PCI revision register. */
#define VENDOR_ID_ASPEED		0x1a03
#define DEVICE_ID_AST_VGA		0x2000


/*! Custom ioctl opcodes. B_DEVICE_OP_CODES_END is the last reserved id in
 *  Drivers.h; everything beyond it is driver-private. */
enum {
	AST_GET_PRIVATE_DATA = B_DEVICE_OP_CODES_END + 1,
	AST_DUMP_REGISTERS,
};


/*! Logical chip generations. The PCI revision register encodes more than just
 *  a generation (process steppings, etc.), but the major-number runs are
 *  documented in Linux drivers/gpu/drm/ast/ast_drv.c ast_detect_chip(). */
enum ast_chip_generation {
	AST_GEN_UNKNOWN	= 0,
	AST_GEN_AST2100,
	AST_GEN_AST2200,
	AST_GEN_AST2300,
	AST_GEN_AST2400,
	AST_GEN_AST2500,
	AST_GEN_AST2600,
};


/*! State shared between the kernel driver and the accelerant. Mapped into the
 *  accelerant's address space via clone_area on accelerant init.
 *
 *  Phase 1 fields only — this struct will grow as later phases add EDID
 *  state, current display mode, encoder state, etc. */
struct ast_shared_info {
	uint32				magic;
	pci_info			pciInfo;
	uint8				chipRevision;
	ast_chip_generation	chipGeneration;

	/* MMIO register window — typically BAR1 on AST silicon. */
	area_id				registersArea;
	phys_addr_t			registersPhys;
	uint32				registersSize;

	/* Framebuffer — typically BAR0. */
	area_id				framebufferArea;
	phys_addr_t			framebufferPhys;
	uint32				framebufferSize;
};


/*! Argument to AST_GET_PRIVATE_DATA. Used by the accelerant to discover the
 *  driver's shared_area id so it can clone_area() the shared state. */
struct ast_get_private_data {
	uint32	magic;
	area_id	sharedArea;
};


#endif	// AST_DRIVER_INTERFACE_H
