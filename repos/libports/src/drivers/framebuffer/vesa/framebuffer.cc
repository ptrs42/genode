/*
 * \brief  VESA frame buffer driver back end
 * \author Sebastian Sumpf
 * \author Christian Helmuth
 * \date   2007-09-11
 */

/*
 * Copyright (C) 2007-2013 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

#include <base/env.h>
#include <base/sleep.h>
#include <base/stdint.h>
#include <base/printf.h>
#include <base/snprintf.h>
#include <io_mem_session/connection.h>

#include "framebuffer.h"
#include "ifx86emu.h"
#include "vesa.h"
#include "vbe.h"

using namespace Genode;
using namespace Vesa;

/**
 * Frame buffer I/O memory dataspace
 */
static Dataspace_capability io_mem_cap;

static const bool verbose = false;

/***************
 ** Utilities **
 ***************/


static inline uint32_t to_phys(uint32_t addr)
{
	return (addr & 0xFFFF) + ((addr >> 12) & 0xFFFF0);
}


static uint16_t get_vesa_mode(mb_vbe_ctrl_t *ctrl_info, mb_vbe_mode_t *mode_info,
                              unsigned long width, unsigned long height,
                              unsigned long depth, bool verbose)
{
	uint16_t ret = 0;

	if (verbose)
		printf("Supported mode list\n");

	/*
	 * The virtual address of the ctrl_info mapping may change on x86_cmd
	 * execution. Therefore, we resolve the address on each iteration.
	 */
#define MODE_PTR(off) (X86emu::virt_addr<uint16_t>(to_phys(ctrl_info->video_mode)) + off)
	for (unsigned off = 0; *MODE_PTR(off) != 0xFFFF; ++off) {
		if (X86emu::x86emu_cmd(VBE_INFO_FUNC, 0, *MODE_PTR(off), VESA_MODE_OFFS) != VBE_SUPPORTED)
			continue;

		enum { DIRECT_COLOR = 0x06 };
		if (mode_info->memory_model != DIRECT_COLOR)
			continue;

		if (verbose)
			printf("    0x%03x %ux%u@%u\n", *MODE_PTR(off), mode_info->x_resolution,
			                                 mode_info->y_resolution,
			                                 mode_info->bits_per_pixel);

		if (mode_info->x_resolution == width &&
		    mode_info->y_resolution == height &&
		    mode_info->bits_per_pixel == depth)
			ret = *MODE_PTR(off);
	}
#undef MODE_PTR

	if (ret)
		return ret;

	if (verbose)
		PWRN("Searching in default vesa modes");

	return get_default_vesa_mode(width, height, depth);
}


/****************
 ** Driver API **
 ****************/

Dataspace_capability Framebuffer_drv::hw_framebuffer()
{
	return io_mem_cap;
}


int Framebuffer_drv::map_io_mem(addr_t base, size_t size, bool write_combined,
                                void **out_addr, addr_t addr,
                                Dataspace_capability *out_io_ds)
{
	Io_mem_connection io_mem(base, size, write_combined);
	io_mem.on_destruction(Io_mem_connection::KEEP_OPEN);
	Io_mem_dataspace_capability io_ds = io_mem.dataspace();
	if (!io_ds.valid())
		return -2;

	try {
		*out_addr = env()->rm_session()->attach(io_ds, size, 0, addr != 0, addr);
	} catch (Rm_session::Attach_failed) {
		return -3;
	}

	PDBG("fb mapped to %p", *out_addr);

	if (out_io_ds)
		*out_io_ds = io_ds;

	return 0;
}


int Framebuffer_drv::use_current_mode()
{
	mb_vbe_ctrl_t *ctrl_info;
	mb_vbe_mode_t *mode_info;

	/* set location of data types */
	ctrl_info = reinterpret_cast<mb_vbe_ctrl_t*>(X86emu::x86_mem.data_addr()
	                                             + VESA_CTRL_OFFS);
	mode_info = reinterpret_cast<mb_vbe_mode_t*>(X86emu::x86_mem.data_addr()
	                                             + VESA_MODE_OFFS);

	/* retrieve controller information */
	if (X86emu::x86emu_cmd(VBE_CONTROL_FUNC, 0, 0, VESA_CTRL_OFFS) != VBE_SUPPORTED) {
		PWRN("VBE Bios not present");
		return -1;
	}

	/* retrieve current video mode */
	uint16_t vesa_mode = 0;
	if (X86emu::x86emu_cmd(VBE_GMODE_FUNC, 0, 0, 0, &vesa_mode) != VBE_SUPPORTED) {
		PWRN("VBE Bios not present");
		return -1;
	}
	vesa_mode &= 0x3fff;

	/* retrieve framebuffer info for current mode */
	if (X86emu::x86emu_cmd(VBE_INFO_FUNC, 0, vesa_mode, VESA_MODE_OFFS) != VBE_SUPPORTED) {
		PWRN("VBE Info function failed");
		return -2;
	}

	if (!io_mem_cap.valid()) {
		printf("Found: physical frame buffer at 0x%08x size: 0x%08x\n",
		       mode_info->phys_base,
		       ctrl_info->total_memory << 16);

		void *fb;
		map_io_mem(mode_info->phys_base, ctrl_info->total_memory << 16, true,
		           &fb, 0, &io_mem_cap);
	}

	return 0;
}


int Framebuffer_drv::set_mode(unsigned long width, unsigned long height,
                              unsigned long mode)
{
	mb_vbe_ctrl_t *ctrl_info;
	mb_vbe_mode_t *mode_info;
	char * oem_string;
	uint16_t vesa_mode;

	/* set location of data types */
	ctrl_info = reinterpret_cast<mb_vbe_ctrl_t*>(X86emu::x86_mem.data_addr()
	                                             + VESA_CTRL_OFFS);
	mode_info = reinterpret_cast<mb_vbe_mode_t*>(X86emu::x86_mem.data_addr()
	                                             + VESA_MODE_OFFS);

	/* request VBE 2.0 information */
	memcpy(ctrl_info->signature, "VBE2", 4);

	/* retrieve controller information */
	if (X86emu::x86emu_cmd(VBE_CONTROL_FUNC, 0, 0, VESA_CTRL_OFFS) != VBE_SUPPORTED) {
		PWRN("VBE Bios not present");
		return -1;
	}

	/* retrieve vesa mode hex value */
	if (!(vesa_mode = get_vesa_mode(ctrl_info, mode_info, width, height, mode, verbose))) {
		PWRN("graphics mode %lux%lu@%lu not found", width, height, mode);
		get_vesa_mode(ctrl_info, mode_info, 0, 0, 0, true);
		return -2;
	}

	/* use current refresh rate, set flat framebuffer model */
	vesa_mode = (vesa_mode & VBE_CUR_REFRESH_MASK) | VBE_SET_FLAT_FB;

	/* determine VBE version and OEM string */
	oem_string = X86emu::virt_addr<char>(to_phys(ctrl_info->oem_string));

	printf("Found: VESA BIOS version %d.%d\nOEM: %s\n",
	       ctrl_info->version >> 8, ctrl_info->version & 0xFF,
	       ctrl_info->oem_string ? oem_string : "[unknown]");

	if (ctrl_info->version < 0x200) {
		PWRN("VESA Bios version 2.0 or later required");
		return -3;
	}

	/* get mode info */
	/* 0x91 tests MODE SUPPORTED (0x1) | GRAPHICS  MODE (0x10) | LINEAR
	 * FRAME BUFFER (0x80) bits */
	if (X86emu::x86emu_cmd(VBE_INFO_FUNC, 0, vesa_mode, VESA_MODE_OFFS) != VBE_SUPPORTED
	   || (mode_info->mode_attributes & 0x91) != 0x91) {
		PWRN("graphics mode %lux%lu@%lu not supported", width, height, mode);
		get_vesa_mode(ctrl_info, mode_info, 0, 0, 0, true);
		return -4;
	}

	/* set mode */
	if ((X86emu::x86emu_cmd(VBE_MODE_FUNC, vesa_mode) & 0xFF00) != VBE_SUCCESS) {
		PDBG("VBE SET error");
		return -5;
	}

	/* map framebuffer */
	void *fb;
	if (!io_mem_cap.valid()) {
		X86emu::x86emu_cmd(VBE_INFO_FUNC, 0, vesa_mode, VESA_MODE_OFFS);

		printf("Found: physical frame buffer at 0x%08x size: 0x%08x\n",
		       mode_info->phys_base,
		       ctrl_info->total_memory << 16);
		map_io_mem(mode_info->phys_base, ctrl_info->total_memory << 16, true,
		           &fb, 0, &io_mem_cap);
	}

	if (verbose)
		X86emu::print_regions();

	return 0;
}


/********************
 ** Driver startup **
 ********************/

int Framebuffer_drv::init()
{
	if (X86emu::init())
		return -1;

	return 0;
}
