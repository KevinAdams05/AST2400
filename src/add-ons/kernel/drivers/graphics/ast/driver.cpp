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
 * Phase 1 scope: probe and inventory.
 *   - Claim PCI vendor 0x1a03, device 0x2000.
 *   - On init_driver, log chip generation, revision, BAR layout.
 *   - On open, map MMIO + framebuffer BARs and create the accelerant's
 *     shared area; the actual accelerant doesn't exist yet, so this
 *     codepath won't run until Phase 2, but the wiring is here.
 *   - AST_DUMP_REGISTERS ioctl logs the first 64 bytes of MMIO for
 *     post-bring-up sanity checking once the accelerant is in place.
 *
 * Linux reference: drivers/gpu/drm/ast/ast_drv.c (ast_pci_probe, ast_detect_chip).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Drivers.h>
#include <KernelExport.h>
#include <OS.h>
#include <PCI.h>
#include <graphic_driver.h>

#include "DriverInterface.h"


#define TRACE_AST_DRIVER		1

#if TRACE_AST_DRIVER
#	define TRACE(x...)		dprintf("ast: " x)
#else
#	define TRACE(x...)		do {} while (0)
#endif

#define TRACE_ERROR(x...)	dprintf("ast: ERROR: " x)


#define MAX_AST_DEVICES		4
#define DEVICE_PATH_FORMAT	"graphics/" AST_DRIVER_NAME "_%02x%02x%02x"

#define ROUND_TO_PAGE_SIZE(x) \
	(((x) + (B_PAGE_SIZE) - 1) & ~((B_PAGE_SIZE) - 1))


struct ast_device_info {
	int32				openCount;
	pci_info			pciInfo;
	uint8				chipRevision;
	ast_chip_generation	chipGeneration;

	area_id				sharedArea;
	ast_shared_info*	sharedInfo;

	area_id				registersArea;
	volatile uint8*		registers;

	area_id				framebufferArea;
	volatile uint8*		framebuffer;

	char				devicePath[B_OS_NAME_LENGTH];
};


int32 api_version = B_CUR_DRIVER_API_VERSION;


static pci_module_info*	sPciModule = NULL;
static ast_device_info*	sDevices[MAX_AST_DEVICES + 1];
static const char*		sDevicePaths[MAX_AST_DEVICES + 1];
static int32			sDeviceCount = 0;


static status_t ast_open(const char* name, uint32 flags, void** cookie);
static status_t ast_close(void* cookie);
static status_t ast_free(void* cookie);
static status_t ast_control(void* cookie, uint32 op, void* arg, size_t length);
static status_t ast_read(void* cookie, off_t pos, void* buffer, size_t* length);
static status_t ast_write(void* cookie, off_t pos, const void* buffer,
	size_t* length);


static device_hooks sDeviceHooks = {
	ast_open,
	ast_close,
	ast_free,
	ast_control,
	ast_read,
	ast_write,
	NULL,
	NULL,
	NULL,
	NULL
};


/*! Decode a PCI revision-register value into a chip generation.
 *
 *  ASPEED encodes both major silicon generation and stepping in the
 *  revision byte. The major-number ranges are documented in Linux
 *  drivers/gpu/drm/ast/ast_drv.c ast_detect_chip(). Anything we don't
 *  recognize falls through to AST_GEN_UNKNOWN — the driver still
 *  binds, since the PCI bus interface is stable across the family,
 *  but later phases that depend on silicon-specific register
 *  sequences will need to refuse to drive it.
 */
static ast_chip_generation
revision_to_generation(uint8 revision)
{
	if (revision >= 0x50)
		return AST_GEN_AST2600;
	if (revision >= 0x40)
		return AST_GEN_AST2500;
	if (revision >= 0x30)
		return AST_GEN_AST2400;
	if (revision >= 0x20)
		return AST_GEN_AST2300;
	if (revision >= 0x10)
		return AST_GEN_AST2200;
	if (revision >= 0x01)
		return AST_GEN_AST2100;
	return AST_GEN_UNKNOWN;
}


static const char*
generation_name(ast_chip_generation generation)
{
	switch (generation) {
		case AST_GEN_AST2600:
			return "AST2600";

		case AST_GEN_AST2500:
			return "AST2500";

		case AST_GEN_AST2400:
			return "AST2400";

		case AST_GEN_AST2300:
			return "AST2300";

		case AST_GEN_AST2200:
			return "AST2200";

		case AST_GEN_AST2100:
			return "AST2100";

		default:
			return "unknown ASPEED";
	}
}


/*! Validate that BAR0 (framebuffer) and BAR1 (MMIO) have been assigned
 *  by the BIOS / Haiku PCI bus manager. Haiku ticket #3 (open since
 *  2005) means the bus manager does not allocate BARs that the BIOS
 *  left unprogrammed; on affected boards we get base=0 / size=0 and
 *  any attempt to map those BARs will fail in confusing ways
 *  (map_physical_memory(0, ...) usually returns garbage or panics).
 *
 *  Returns B_OK if both BARs look usable, an error otherwise. Logs a
 *  clear diagnostic with a pointer to Haiku #3 on failure so users
 *  can find the upstream root cause. */
static status_t
validate_bars(const pci_info& info)
{
	phys_addr_t fbPhys = (phys_addr_t)info.u.h0.base_registers[0];
	uint32 fbSize = info.u.h0.base_register_sizes[0];
	phys_addr_t mmioPhys = (phys_addr_t)info.u.h0.base_registers[1];
	uint32 mmioSize = info.u.h0.base_register_sizes[1];

	if (fbPhys == 0 || fbSize == 0) {
		TRACE_ERROR("PCI BAR0 (framebuffer) unassigned at "
			"[bus %u device %u function %u]: base=0x%" B_PRIxPHYSADDR
			" size=%" B_PRIu32 ". This is Haiku ticket #3 "
			"(PCI bus_manager does no memory resource assignment). "
			"Refusing to bind; VESA fallback will take over.\n",
			info.bus, info.device, info.function, fbPhys, fbSize);
		return B_DEV_RESOURCE_CONFLICT;
	}
	if (mmioPhys == 0 || mmioSize == 0) {
		TRACE_ERROR("PCI BAR1 (MMIO regs) unassigned at "
			"[bus %u device %u function %u]: base=0x%" B_PRIxPHYSADDR
			" size=%" B_PRIu32 ". This is Haiku ticket #3 "
			"(PCI bus_manager does no memory resource assignment). "
			"Refusing to bind; VESA fallback will take over.\n",
			info.bus, info.device, info.function, mmioPhys, mmioSize);
		return B_DEV_RESOURCE_CONFLICT;
	}

	// Sanity: BARs below 1 MB physical are almost certainly system DRAM,
	// not a real device window. Catches BIOS-left-stale-default cases
	// where the value is e.g. 0x10 (default reset value of some chipsets).
	if (fbPhys < 0x100000 || mmioPhys < 0x100000) {
		TRACE_ERROR("PCI BARs at suspiciously low addresses "
			"[bus %u device %u function %u]: BAR0=0x%" B_PRIxPHYSADDR
			" BAR1=0x%" B_PRIxPHYSADDR ". Refusing to bind.\n",
			info.bus, info.device, info.function, fbPhys, mmioPhys);
		return B_DEV_RESOURCE_CONFLICT;
	}

	return B_OK;
}


/*! Walk PCI for matching devices, allocate device_info entries, and log a
 *  short inventory line for each. Returns the number of devices accepted. */
static int32
probe_devices()
{
	pci_info info;
	int32 found = 0;
	for (int32 index = 0;
			sPciModule->get_nth_pci_info(index, &info) == B_OK; index++) {
		if (info.vendor_id != VENDOR_ID_ASPEED)
			continue;
		if (info.device_id != DEVICE_ID_AST_VGA)
			continue;
		if (found >= MAX_AST_DEVICES) {
			TRACE_ERROR("more than %d ASPEED devices found; skipping rest\n",
				MAX_AST_DEVICES);
			break;
		}

		if (validate_bars(info) != B_OK)
			continue;

		ast_device_info* device
			= (ast_device_info*)malloc(sizeof(ast_device_info));
		if (device == NULL) {
			TRACE_ERROR("out of memory probing device %" B_PRId32 "\n", index);
			break;
		}
		memset(device, 0, sizeof(*device));
		device->pciInfo = info;
		device->chipRevision = info.revision;
		device->chipGeneration = revision_to_generation(info.revision);
		device->sharedArea = -1;
		device->registersArea = -1;
		device->framebufferArea = -1;

		snprintf(device->devicePath, sizeof(device->devicePath),
			DEVICE_PATH_FORMAT, info.bus, info.device, info.function);

		TRACE("probed %s: %s rev 0x%02x at [bus %u device %u function %u], "
			"BAR0 %" B_PRIu32 " MB, BAR1 0x%" B_PRIxPHYSADDR " (%" B_PRIu32 " B)\n",
			device->devicePath,
			generation_name(device->chipGeneration), info.revision,
			info.bus, info.device, info.function,
			info.u.h0.base_register_sizes[0] / (1024 * 1024),
			(phys_addr_t)info.u.h0.base_registers[1],
			info.u.h0.base_register_sizes[1]);

		sDevices[found] = device;
		sDevicePaths[found] = device->devicePath;
		found++;
	}
	sDevices[found] = NULL;
	sDevicePaths[found] = NULL;
	return found;
}


extern "C" status_t
init_hardware()
{
	TRACE("init_hardware()\n");

	pci_module_info* pci = NULL;
	if (get_module(B_PCI_MODULE_NAME, (module_info**)&pci) != B_OK)
		return B_ERROR;

	bool found = false;
	pci_info info;
	for (int32 index = 0;
			pci->get_nth_pci_info(index, &info) == B_OK; index++) {
		if (info.vendor_id == VENDOR_ID_ASPEED
				&& info.device_id == DEVICE_ID_AST_VGA) {
			found = true;
			break;
		}
	}

	put_module(B_PCI_MODULE_NAME);

	return found ? B_OK : B_ERROR;
}


extern "C" status_t
init_driver()
{
	TRACE("init_driver()\n");

	if (get_module(B_PCI_MODULE_NAME, (module_info**)&sPciModule) != B_OK)
		return B_ERROR;

	sDeviceCount = probe_devices();
	if (sDeviceCount == 0) {
		put_module(B_PCI_MODULE_NAME);
		sPciModule = NULL;
		return B_ERROR;
	}

	TRACE("init_driver(): %" B_PRId32 " device(s) ready\n", sDeviceCount);
	return B_OK;
}


extern "C" void
uninit_driver()
{
	TRACE("uninit_driver()\n");

	for (int32 index = 0; index < sDeviceCount; index++) {
		free(sDevices[index]);
		sDevices[index] = NULL;
		sDevicePaths[index] = NULL;
	}
	sDeviceCount = 0;

	if (sPciModule != NULL) {
		put_module(B_PCI_MODULE_NAME);
		sPciModule = NULL;
	}
}


extern "C" const char**
publish_devices()
{
	TRACE("publish_devices()\n");
	return sDevicePaths;
}


extern "C" device_hooks*
find_device(const char* name)
{
	TRACE("find_device(%s)\n", name);

	for (int32 index = 0; index < sDeviceCount; index++) {
		if (strcmp(name, sDevices[index]->devicePath) == 0)
			return &sDeviceHooks;
	}
	return NULL;
}


/*! Locate the device_info matching a path string. */
static ast_device_info*
device_for_path(const char* name)
{
	for (int32 index = 0; index < sDeviceCount; index++) {
		if (strcmp(name, sDevices[index]->devicePath) == 0)
			return sDevices[index];
	}
	return NULL;
}


static status_t
ast_open(const char* name, uint32 /*flags*/, void** cookie)
{
	TRACE("ast_open(%s)\n", name);

	ast_device_info* device = device_for_path(name);
	if (device == NULL)
		return B_NAME_NOT_FOUND;

	if (atomic_add(&device->openCount, 1) > 0) {
		// Already open — share the existing mappings and shared_area.
		*cookie = device;
		return B_OK;
	}

	pci_info& info = device->pciInfo;

	phys_addr_t framebufferPhys
		= (phys_addr_t)info.u.h0.base_registers[0];
	uint32 framebufferSize = info.u.h0.base_register_sizes[0];
	phys_addr_t registersPhys
		= (phys_addr_t)info.u.h0.base_registers[1];
	uint32 registersSize = info.u.h0.base_register_sizes[1];

	TRACE("  BAR0 (framebuffer): phys 0x%" B_PRIxPHYSADDR
		" size %" B_PRIu32 " bytes (%" B_PRIu32 " MB)\n",
		framebufferPhys, framebufferSize, framebufferSize / (1024 * 1024));
	TRACE("  BAR1 (MMIO regs):   phys 0x%" B_PRIxPHYSADDR
		" size %" B_PRIu32 " bytes\n",
		registersPhys, registersSize);

	char areaName[B_OS_NAME_LENGTH];
	snprintf(areaName, sizeof(areaName), "%s regs %02x%02x%02x",
		AST_DRIVER_NAME, info.bus, info.device, info.function);
	device->registersArea = map_physical_memory(areaName, registersPhys,
		registersSize, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA,
		(void**)&device->registers);
	if (device->registersArea < B_OK) {
		TRACE_ERROR("failed to map MMIO BAR: %s\n",
			strerror(device->registersArea));
		atomic_add(&device->openCount, -1);
		return device->registersArea;
	}

	snprintf(areaName, sizeof(areaName), "%s fb %02x%02x%02x",
		AST_DRIVER_NAME, info.bus, info.device, info.function);
	device->framebufferArea = map_physical_memory(areaName, framebufferPhys,
		framebufferSize, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA,
		(void**)&device->framebuffer);
	if (device->framebufferArea < B_OK) {
		TRACE_ERROR("failed to map framebuffer BAR: %s\n",
			strerror(device->framebufferArea));
		delete_area(device->registersArea);
		device->registersArea = -1;
		atomic_add(&device->openCount, -1);
		return device->framebufferArea;
	}

	snprintf(areaName, sizeof(areaName), "%s shared %02x%02x%02x",
		AST_DRIVER_NAME, info.bus, info.device, info.function);
	device->sharedArea = create_area(areaName, (void**)&device->sharedInfo,
		B_ANY_KERNEL_ADDRESS,
		ROUND_TO_PAGE_SIZE(sizeof(ast_shared_info)), B_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);
	if (device->sharedArea < B_OK) {
		TRACE_ERROR("failed to create shared area: %s\n",
			strerror(device->sharedArea));
		delete_area(device->framebufferArea);
		delete_area(device->registersArea);
		device->framebufferArea = -1;
		device->registersArea = -1;
		atomic_add(&device->openCount, -1);
		return device->sharedArea;
	}

	memset(device->sharedInfo, 0, sizeof(*device->sharedInfo));
	device->sharedInfo->magic = AST_PRIVATE_DATA_MAGIC;
	device->sharedInfo->pciInfo = info;
	device->sharedInfo->chipRevision = device->chipRevision;
	device->sharedInfo->chipGeneration = device->chipGeneration;
	device->sharedInfo->registersArea = device->registersArea;
	device->sharedInfo->registersPhys = registersPhys;
	device->sharedInfo->registersSize = registersSize;
	device->sharedInfo->framebufferArea = device->framebufferArea;
	device->sharedInfo->framebufferPhys = framebufferPhys;
	device->sharedInfo->framebufferSize = framebufferSize;

	*cookie = device;
	TRACE("ast_open: device opened — %s rev 0x%02x\n",
		generation_name(device->chipGeneration), device->chipRevision);
	return B_OK;
}


static status_t
ast_close(void* /*cookie*/)
{
	TRACE("ast_close()\n");
	return B_OK;
}


static status_t
ast_free(void* cookie)
{
	ast_device_info* device = (ast_device_info*)cookie;
	TRACE("ast_free()\n");

	if (atomic_add(&device->openCount, -1) > 1)
		return B_OK;

	if (device->sharedArea >= 0) {
		delete_area(device->sharedArea);
		device->sharedArea = -1;
		device->sharedInfo = NULL;
	}
	if (device->framebufferArea >= 0) {
		delete_area(device->framebufferArea);
		device->framebufferArea = -1;
		device->framebuffer = NULL;
	}
	if (device->registersArea >= 0) {
		delete_area(device->registersArea);
		device->registersArea = -1;
		device->registers = NULL;
	}

	return B_OK;
}


static status_t
ast_control(void* cookie, uint32 op, void* arg, size_t length)
{
	// `arg` is a USERSPACE pointer when this is called via the ioctl()
	// syscall. Modern x86_64 enforces SMAP, so the kernel must transfer
	// data through user_memcpy / user_strlcpy rather than dereferencing
	// `arg` directly. (Bare strcpy / struct access here panics with
	// "SMAP violation user-mapped address" on first ioctl.)
	ast_device_info* device = (ast_device_info*)cookie;

	switch (op) {
		case B_GET_ACCELERANT_SIGNATURE:
			if (user_strlcpy((char*)arg, AST_ACCELERANT_NAME, length) < B_OK)
				return B_BAD_ADDRESS;
			return B_OK;

		case AST_GET_PRIVATE_DATA:
		{
			ast_get_private_data data;
			if (user_memcpy(&data, arg, sizeof(data)) < B_OK)
				return B_BAD_ADDRESS;
			if (data.magic != AST_PRIVATE_DATA_MAGIC)
				return B_BAD_VALUE;
			data.sharedArea = device->sharedArea;
			if (user_memcpy(arg, &data, sizeof(data)) < B_OK)
				return B_BAD_ADDRESS;
			return B_OK;
		}

		case AST_DUMP_REGISTERS:
		{
			// Phase 1 diagnostic: dump the first 64 MMIO bytes. Real register
			// decode lands once we know which regions of the BAR1 map to
			// what hardware blocks — see AST2500 SPG §1.2 onwards.
			// No userspace pointer involved here, so direct MMIO access
			// is fine — `device->registers` is a kernel-mapped pointer.
			for (uint32 offset = 0; offset < 0x40; offset += 16) {
				TRACE("regs 0x%04" B_PRIx32 ":  %08" B_PRIx32 "  %08" B_PRIx32
					"  %08" B_PRIx32 "  %08" B_PRIx32 "\n", offset,
					*(volatile uint32*)(device->registers + offset),
					*(volatile uint32*)(device->registers + offset + 4),
					*(volatile uint32*)(device->registers + offset + 8),
					*(volatile uint32*)(device->registers + offset + 12));
			}
			return B_OK;
		}

		default:
			return B_BAD_VALUE;
	}
}


static status_t
ast_read(void* /*cookie*/, off_t /*pos*/, void* /*buffer*/, size_t* length)
{
	*length = 0;
	return B_NOT_ALLOWED;
}


static status_t
ast_write(void* /*cookie*/, off_t /*pos*/, const void* /*buffer*/,
	size_t* length)
{
	*length = 0;
	return B_NOT_ALLOWED;
}
