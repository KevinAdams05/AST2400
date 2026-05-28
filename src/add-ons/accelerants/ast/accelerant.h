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
#ifndef AST_ACCELERANT_H
#define AST_ACCELERANT_H


#include <Accelerant.h>

#include "DriverInterface.h"


/*! Per-accelerant-instance state. app_server creates one of these on init
 *  and passes it back to every hook via a thread-local pointer (gInfo).
 *  Phase 2 stores the cloned shared_info, the device fd, and the cloned
 *  framebuffer/MMIO mappings; later phases will grow this with mode
 *  state and EDID. */
struct ast_accelerant_info {
	int					deviceFd;
	bool				isClone;

	/* Cloned from the kernel driver. */
	area_id				sharedInfoClone;
	ast_shared_info*	sharedInfo;

	/* Userspace clones of the kernel's BAR mappings. */
	area_id				registersClone;
	volatile uint8*		registers;

	area_id				framebufferClone;
	volatile uint8*		framebuffer;

	/* Identifier app_server hands us at clone time (the /dev/graphics/...
	 *  path the kernel driver published). */
	char				devicePath[B_OS_NAME_LENGTH];
};


/*! Single thread-local instance pointer. Each open accelerant context has
 *  its own. */
extern ast_accelerant_info* gInfo;


#endif	// AST_ACCELERANT_H
