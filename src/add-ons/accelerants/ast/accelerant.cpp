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
 *
 * Phase 2 scope: bare-minimum accelerant.
 *   - Init: clone the kernel driver's shared_info area into our user
 *     address space; clone the MMIO and framebuffer areas too so we
 *     have register access ready for Phase 3.
 *   - Uninit: release everything.
 *   - Mode list: a single hardcoded 1024x768@60 mode. Real EDID-driven
 *     mode generation lands in Phase 3.
 *   - SetDisplayMode: stub — returns B_OK only if the requested mode
 *     matches our hardcoded one (no chip programming yet).
 *   - Framebuffer config: returns the cloned framebuffer base. Note
 *     this still relies on whatever VESA programmed during boot — the
 *     CRTC isn't being driven by us yet.
 *
 * Linux reference: drivers/gpu/drm/ast/ast_mode.c — the place to look
 * for actual CRTC/PLL programming once we get to Phase 3.
 */

#include "accelerant.h"
#include "ast_regs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <Debug.h>


#define TRACE_AST_ACCEL		1

// <Debug.h> defines TRACE() as a no-op in release builds; override it
// so accelerant logs always land in syslog via _sPrintf().
#undef TRACE

#if TRACE_AST_ACCEL
#	define TRACE(x...) _sPrintf("ast.accel: " x)
#else
#	define TRACE(x...) ((void)0)
#endif

#define ERROR(x...) _sPrintf("ast.accel: ERROR: " x)


ast_accelerant_info* gInfo = NULL;


/*! Defined in mode.cpp. */
extern "C" status_t ast_program_mode(const ast_mode_info* mode);

/*! Defined in ddc.cpp. */
extern "C" status_t ast_read_edid_block(uint8 buffer[128]);
extern "C" void ast_log_edid(const uint8 buffer[128]);


/*! Convenience: clone an area from the kernel driver's address space into
 *  ours and return the cloned id + a pointer to the mapping. */
static status_t
clone_kernel_area(const char* nameHint, area_id sourceArea,
	area_id* outClone, void** outBase, uint32 protection)
{
	char cloneName[B_OS_NAME_LENGTH];
	snprintf(cloneName, sizeof(cloneName), "%s clone", nameHint);

	*outClone = clone_area(cloneName, outBase, B_ANY_ADDRESS, protection,
		sourceArea);
	if (*outClone < B_OK) {
		TRACE("clone_area(%s) failed: %s\n", nameHint, strerror(*outClone));
		return *outClone;
	}
	return B_OK;
}


// ===== B_INIT_ACCELERANT / B_UNINIT_ACCELERANT =====

extern "C" status_t
ast_init_accelerant(int deviceFd)
{
	TRACE("init_accelerant(fd=%d)\n", deviceFd);

	gInfo = (ast_accelerant_info*)malloc(sizeof(*gInfo));
	if (gInfo == NULL)
		return B_NO_MEMORY;
	memset(gInfo, 0, sizeof(*gInfo));
	gInfo->deviceFd = deviceFd;
	gInfo->isClone = false;
	gInfo->sharedInfoClone = -1;
	gInfo->registersClone = -1;
	gInfo->framebufferClone = -1;

	// Ask the kernel driver which shared_area it created on its side, then
	// clone it into our user address space.
	ast_get_private_data privateData;
	privateData.magic = AST_PRIVATE_DATA_MAGIC;
	if (ioctl(deviceFd, AST_GET_PRIVATE_DATA, &privateData,
			sizeof(privateData)) != 0) {
		TRACE("AST_GET_PRIVATE_DATA ioctl failed: %s\n", strerror(errno));
		free(gInfo);
		gInfo = NULL;
		return B_ERROR;
	}

	status_t status = clone_kernel_area("ast shared info clone",
		privateData.sharedArea, &gInfo->sharedInfoClone,
		(void**)&gInfo->sharedInfo, B_READ_AREA | B_WRITE_AREA);
	if (status != B_OK) {
		free(gInfo);
		gInfo = NULL;
		return status;
	}

	if (gInfo->sharedInfo->magic != AST_PRIVATE_DATA_MAGIC) {
		TRACE("shared_info magic mismatch: got 0x%" B_PRIx32
			" want 0x%" B_PRIx32 "\n",
			gInfo->sharedInfo->magic, (uint32)AST_PRIVATE_DATA_MAGIC);
		delete_area(gInfo->sharedInfoClone);
		free(gInfo);
		gInfo = NULL;
		return B_ERROR;
	}

	// Clone MMIO + framebuffer too so Phase 3 has them available without
	// another handshake. Framebuffer needs to be writable so app_server
	// can draw into it; MMIO is also r/w because we'll be programming
	// CRTC registers via it.
	status = clone_kernel_area("ast regs clone",
		gInfo->sharedInfo->registersArea,
		&gInfo->registersClone, (void**)&gInfo->registers,
		B_READ_AREA | B_WRITE_AREA);
	if (status != B_OK) {
		delete_area(gInfo->sharedInfoClone);
		free(gInfo);
		gInfo = NULL;
		return status;
	}

	status = clone_kernel_area("ast fb clone",
		gInfo->sharedInfo->framebufferArea,
		&gInfo->framebufferClone, (void**)&gInfo->framebuffer,
		B_READ_AREA | B_WRITE_AREA);
	if (status != B_OK) {
		delete_area(gInfo->registersClone);
		delete_area(gInfo->sharedInfoClone);
		free(gInfo);
		gInfo = NULL;
		return status;
	}

	TRACE("init_accelerant: AST chip gen %d rev 0x%02x, fb %" B_PRIu32
		" MB, MMIO %" B_PRIu32 " B\n",
		(int)gInfo->sharedInfo->chipGeneration,
		gInfo->sharedInfo->chipRevision,
		gInfo->sharedInfo->framebufferSize / (1024 * 1024),
		gInfo->sharedInfo->registersSize);

	// Phase 4.1: try to read the monitor's EDID via the AST DDC bus and
	// log it. Doesn't yet feed back into the mode list — that's Phase 4.2.
	uint8 edid[128];
	if (ast_read_edid_block(edid) == B_OK) {
		ast_log_edid(edid);
	} else {
		TRACE("init_accelerant: no usable EDID; keeping hardcoded mode list\n");
	}

	return B_OK;
}


extern "C" void
ast_uninit_accelerant()
{
	TRACE("uninit_accelerant()\n");
	if (gInfo == NULL)
		return;

	if (gInfo->framebufferClone >= 0)
		delete_area(gInfo->framebufferClone);
	if (gInfo->registersClone >= 0)
		delete_area(gInfo->registersClone);
	if (gInfo->sharedInfoClone >= 0)
		delete_area(gInfo->sharedInfoClone);

	free(gInfo);
	gInfo = NULL;
}


// ===== B_ACCELERANT_CLONE_INFO_SIZE / B_GET_ACCELERANT_CLONE_INFO /
// ===== B_CLONE_ACCELERANT
//
// app_server clones the accelerant for each workspace. CLONE_INFO_SIZE
// returns how much state to save; GET_CLONE_INFO fills it (we just stash
// the device path); CLONE_ACCELERANT uses it to recreate the state.

extern "C" ssize_t
ast_accelerant_clone_info_size()
{
	return B_OS_NAME_LENGTH;
}


extern "C" void
ast_get_accelerant_clone_info(void* data)
{
	if (gInfo == NULL || data == NULL)
		return;
	strlcpy((char*)data, gInfo->devicePath, B_OS_NAME_LENGTH);
}


extern "C" status_t
ast_clone_accelerant(void* data)
{
	TRACE("clone_accelerant(%s)\n", (char*)data);

	// Re-open the same /dev/graphics/... path the original accelerant
	// used. The kernel driver counts opens and shares per-device state.
	char devicePath[MAXPATHLEN];
	snprintf(devicePath, sizeof(devicePath), "/dev/%s", (char*)data);

	int fd = open(devicePath, B_READ_WRITE);
	if (fd < 0)
		return errno;

	status_t status = ast_init_accelerant(fd);
	if (status != B_OK) {
		close(fd);
		return status;
	}
	gInfo->isClone = true;
	strlcpy(gInfo->devicePath, (char*)data, sizeof(gInfo->devicePath));
	return B_OK;
}


// ===== B_GET_ACCELERANT_DEVICE_INFO =====

extern "C" status_t
ast_get_accelerant_device_info(accelerant_device_info* info)
{
	if (info == NULL || gInfo == NULL)
		return B_BAD_VALUE;

	info->version = B_ACCELERANT_VERSION;
	strlcpy(info->name, "ASPEED Graphics", sizeof(info->name));
	switch (gInfo->sharedInfo->chipGeneration) {
		case AST_GEN_AST2600:
			strlcpy(info->chipset, "AST2600", sizeof(info->chipset));
			break;
		case AST_GEN_AST2500:
			strlcpy(info->chipset, "AST2500", sizeof(info->chipset));
			break;
		case AST_GEN_AST2400:
			strlcpy(info->chipset, "AST2400", sizeof(info->chipset));
			break;
		case AST_GEN_AST2300:
			strlcpy(info->chipset, "AST2300", sizeof(info->chipset));
			break;
		case AST_GEN_AST2200:
			strlcpy(info->chipset, "AST2200", sizeof(info->chipset));
			break;
		case AST_GEN_AST2100:
			strlcpy(info->chipset, "AST2100", sizeof(info->chipset));
			break;
		default:
			strlcpy(info->chipset, "Unknown ASPEED", sizeof(info->chipset));
			break;
	}
	strlcpy(info->serial_no, "unknown", sizeof(info->serial_no));
	info->memory = gInfo->sharedInfo->framebufferSize;
	info->dac_speed = 165000;	// MHz * 1000; AST2400 DAC tops at ~165
	return B_OK;
}


// ===== Mode list — Phase 4.0: 5 hardcoded 4:3 VESA modes =====
//
// One Haiku display_mode entry per kModeList[] entry in mode.cpp. They
// share array index, so accelerant slot N programs mode.cpp slot N.
// All currently at 32 bpp.

#define AST_HAIKU_MODE(pixelClockKhz, hAct, hSyncStart, hSyncEnd, hTotal, \
		vAct, vSyncStart, vSyncEnd, vTotal, syncFlags) \
	{ \
		{ \
			pixelClockKhz, \
			hAct, hSyncStart, hSyncEnd, hTotal, \
			vAct, vSyncStart, vSyncEnd, vTotal, \
			syncFlags \
		}, \
		B_RGB32_LITTLE, \
		hAct, vAct, \
		0, 0, 0 \
	}


static const display_mode kHaikuModes[] = {
	/* 640x480@60   — 25.175 MHz, neg-H neg-V */
	AST_HAIKU_MODE( 25175,  640,  656,  752,  800,
		480, 490, 492, 525, 0),
	/* 800x600@60   — 40 MHz, pos-H pos-V */
	AST_HAIKU_MODE( 40000,  800,  840,  968, 1056,
		600, 601, 605, 628,
		B_POSITIVE_HSYNC | B_POSITIVE_VSYNC),
	/* 1024x768@60  — 65 MHz, neg-H neg-V */
	AST_HAIKU_MODE( 65000, 1024, 1048, 1184, 1344,
		768, 771, 777, 806, 0),
	/* 1280x1024@60 — 108 MHz, pos-H pos-V */
	AST_HAIKU_MODE(108000, 1280, 1328, 1440, 1688,
		1024, 1025, 1028, 1066,
		B_POSITIVE_HSYNC | B_POSITIVE_VSYNC),
	/* 1600x1200@60 — 162 MHz, pos-H pos-V */
	AST_HAIKU_MODE(162000, 1600, 1664, 1856, 2160,
		1200, 1201, 1204, 1250,
		B_POSITIVE_HSYNC | B_POSITIVE_VSYNC),
	/* 1920x1080@60 — 148.5 MHz, pos-H pos-V — CEA standard */
	AST_HAIKU_MODE(148500, 1920, 2008, 2052, 2200,
		1080, 1084, 1089, 1125,
		B_POSITIVE_HSYNC | B_POSITIVE_VSYNC),

	/* --- 16:10 widescreen --- */
	/* 1280x800@60   — 83.5 MHz, neg-H pos-V — WXGA, DMT */
	AST_HAIKU_MODE( 83500, 1280, 1352, 1480, 1680,
		800, 803, 809, 831,
		B_POSITIVE_VSYNC),
	/* 1440x900@60   — 106.5 MHz, neg-H pos-V — WXGA+, DMT */
	AST_HAIKU_MODE(106500, 1440, 1520, 1672, 1904,
		900, 903, 909, 934,
		B_POSITIVE_VSYNC),
	/* 1680x1050@60  — 146.25 MHz, neg-H pos-V — WSXGA+, DMT */
	AST_HAIKU_MODE(146250, 1680, 1784, 1960, 2240,
		1050, 1053, 1059, 1089,
		B_POSITIVE_VSYNC),
	/* 1920x1200@60  — 154 MHz reduced-blanking, pos-H neg-V — WUXGA */
	AST_HAIKU_MODE(154000, 1920, 1968, 2000, 2080,
		1200, 1203, 1209, 1235,
		B_POSITIVE_HSYNC),
};

static const uint32 kHaikuModeCount
	= sizeof(kHaikuModes) / sizeof(kHaikuModes[0]);


/*! Lookup helper: find the kHaikuModes index whose width/height matches
 *  the requested mode. Returns -1 if no match. */
static int32
find_mode_index(uint32 width, uint32 height)
{
	for (uint32 i = 0; i < kHaikuModeCount; i++) {
		if (kHaikuModes[i].virtual_width == width
				&& kHaikuModes[i].virtual_height == height)
			return (int32)i;
	}
	return -1;
}


/*! Track the currently-active mode so GET_DISPLAY_MODE +
 *  GET_FRAME_BUFFER_CONFIG agree with reality. */
static uint32 sCurrentModeIndex = 2;	// default to 1024x768@60


extern "C" uint32
ast_accelerant_mode_count()
{
	return kHaikuModeCount;
}


extern "C" status_t
ast_get_mode_list(display_mode* list)
{
	if (list == NULL)
		return B_BAD_VALUE;
	for (uint32 i = 0; i < kHaikuModeCount; i++)
		list[i] = kHaikuModes[i];
	return B_OK;
}


extern "C" status_t
ast_propose_display_mode(display_mode* target, const display_mode* /*low*/,
	const display_mode* /*high*/)
{
	if (target == NULL)
		return B_BAD_VALUE;
	int32 index = find_mode_index(target->virtual_width,
		target->virtual_height);
	if (index < 0) {
		// Fall back to our default mode (1024×768) and signal that the
		// target was rewritten so app_server can decide what to do.
		*target = kHaikuModes[2];
		return B_BAD_VALUE;
	}
	*target = kHaikuModes[index];
	return B_OK;
}


extern "C" status_t
ast_set_display_mode(display_mode* mode)
{
	if (mode == NULL)
		return B_BAD_VALUE;
	TRACE("set_display_mode(%u x %u)\n",
		mode->virtual_width, mode->virtual_height);

	int32 index = find_mode_index(mode->virtual_width,
		mode->virtual_height);
	if (index < 0)
		return B_NOT_SUPPORTED;

	// Validate the framebuffer is large enough. (Phase 4.0 always uses
	// 32 bpp + width-stride; the 16 MB BAR0 on AST2400 covers everything
	// up through 2048×2048 single-buffer, so this check exists mostly
	// to catch future format/depth changes.)
	uint32 needed = mode->virtual_width * mode->virtual_height * 4;
	if (needed > gInfo->sharedInfo->framebufferSize) {
		TRACE("set_display_mode: %u x %u (%u bytes) exceeds framebuffer"
			" (%" B_PRIu32 " bytes)\n",
			mode->virtual_width, mode->virtual_height, needed,
			gInfo->sharedInfo->framebufferSize);
		return B_NO_MEMORY;
	}

	status_t status = ast_program_mode(&kModeList[index]);
	if (status != B_OK)
		return status;
	sCurrentModeIndex = (uint32)index;
	return B_OK;
}


extern "C" status_t
ast_get_display_mode(display_mode* currentMode)
{
	if (currentMode == NULL)
		return B_BAD_VALUE;
	*currentMode = kHaikuModes[sCurrentModeIndex];
	return B_OK;
}


extern "C" status_t
ast_get_frame_buffer_config(frame_buffer_config* config)
{
	if (config == NULL || gInfo == NULL)
		return B_BAD_VALUE;

	config->frame_buffer = (void*)gInfo->framebuffer;
	config->frame_buffer_dma = (void*)gInfo->sharedInfo->framebufferPhys;
	config->bytes_per_row
		= kHaikuModes[sCurrentModeIndex].virtual_width * 4;
	return B_OK;
}


extern "C" status_t
ast_get_pixel_clock_limits(display_mode* /*mode*/, uint32* low, uint32* high)
{
	if (low != NULL)
		*low = 25000;	// VGA minimum
	if (high != NULL)
		*high = 165000;	// AST2400 single-link DAC ceiling
	return B_OK;
}


extern "C" status_t
ast_get_timing_constraints(display_timing_constraints* constraints)
{
	if (constraints == NULL)
		return B_BAD_VALUE;
	// VESA-ish defaults; tightened in Phase 3 once we have real EDID.
	constraints->h_res = 8;
	constraints->h_sync_min = 1;
	constraints->h_sync_max = 0x7ff;
	constraints->h_blank_min = 1;
	constraints->h_blank_max = 0x7ff;
	constraints->v_res = 1;
	constraints->v_sync_min = 1;
	constraints->v_sync_max = 0x7ff;
	constraints->v_blank_min = 1;
	constraints->v_blank_max = 0x7ff;
	return B_OK;
}


// ===== Hook dispatch =====

extern "C" void*
get_accelerant_hook(uint32 feature, void* /*data*/)
{
	switch (feature) {
		case B_INIT_ACCELERANT:
			return (void*)ast_init_accelerant;
		case B_UNINIT_ACCELERANT:
			return (void*)ast_uninit_accelerant;
		case B_ACCELERANT_CLONE_INFO_SIZE:
			return (void*)ast_accelerant_clone_info_size;
		case B_GET_ACCELERANT_CLONE_INFO:
			return (void*)ast_get_accelerant_clone_info;
		case B_CLONE_ACCELERANT:
			return (void*)ast_clone_accelerant;
		case B_GET_ACCELERANT_DEVICE_INFO:
			return (void*)ast_get_accelerant_device_info;

		case B_ACCELERANT_MODE_COUNT:
			return (void*)ast_accelerant_mode_count;
		case B_GET_MODE_LIST:
			return (void*)ast_get_mode_list;
		case B_PROPOSE_DISPLAY_MODE:
			return (void*)ast_propose_display_mode;
		case B_SET_DISPLAY_MODE:
			return (void*)ast_set_display_mode;
		case B_GET_DISPLAY_MODE:
			return (void*)ast_get_display_mode;
		case B_GET_FRAME_BUFFER_CONFIG:
			return (void*)ast_get_frame_buffer_config;
		case B_GET_PIXEL_CLOCK_LIMITS:
			return (void*)ast_get_pixel_clock_limits;
		case B_GET_TIMING_CONSTRAINTS:
			return (void*)ast_get_timing_constraints;

		default:
			return NULL;
	}
}
