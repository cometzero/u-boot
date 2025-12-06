// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2025 RISC-V QEMU Virt Boot Chain Project
 * SPL Device Tree merging for WorldGuard property handoff
 */

#include <common.h>
#include <fdt_support.h>
#include <log.h>
#include <linux/libfdt.h>
#include "worldguard.h"

/* 128KB DTB buffer (per clarification #5) */
static u8 spl_merged_dtb_buf[128 * 1024] __aligned(8);

/**
 * spl_worldguard_add_dt_props() - Add WorldGuard properties to DTB
 * @fdt: Pointer to device tree blob to modify
 * @mlwid: Machine Local World ID value
 * @mwiddeleg: Machine WID Delegation value
 *
 * Adds SPL initialization marker and CSR values to WorldGuard DT node.
 * Creates properties for OpenSBI to detect SPL has already initialized.
 *
 * Returns: 0 on success, negative on error
 */
int spl_worldguard_add_dt_props(void *fdt, u32 mlwid, u32 mwiddeleg)
{
	int node, ret;

	/* Find WorldGuard node */
	node = fdt_path_offset(fdt, "/worldguard");
	if (node < 0) {
		debug("WorldGuard: Cannot find /worldguard node for property addition\n");
		return node;
	}

	/* Add spl-initialized marker */
	ret = fdt_setprop_u32(fdt, node, "spl-initialized", 1);
	if (ret < 0) {
		debug("WorldGuard: Failed to set spl-initialized: %d\n", ret);
		return ret;
	}

	/* Add mlwid value */
	ret = fdt_setprop_u32(fdt, node, "mlwid", mlwid);
	if (ret < 0) {
		debug("WorldGuard: Failed to set mlwid: %d\n", ret);
		return ret;
	}

	/* Add mwiddeleg value */
	ret = fdt_setprop_u32(fdt, node, "mwiddeleg", mwiddeleg);
	if (ret < 0) {
		debug("WorldGuard: Failed to set mwiddeleg: %d\n", ret);
		return ret;
	}

	debug("WorldGuard: Added DT properties - spl-initialized=1, mlwid=%u, mwiddeleg=0x%x\n",
	      mlwid, mwiddeleg);

	return 0;
}

/**
 * spl_create_merged_dtb() - Create new DTB with WorldGuard properties
 * @orig_fdt: Original device tree (from QEMU)
 * @mlwid: Machine Local World ID value
 * @mwiddeleg: Machine WID Delegation value
 *
 * Creates a new DTB in the 128KB buffer, copying the original DTB
 * and adding WorldGuard properties for OpenSBI handoff.
 *
 * Returns: Pointer to new DTB on success, NULL on error
 */
void *spl_create_merged_dtb(const void *orig_fdt, u32 mlwid, u32 mwiddeleg)
{
	int ret;
	int dtb_size;

	if (!orig_fdt) {
		debug("WorldGuard: No original FDT provided\n");
		return NULL;
	}

	/* Get original DTB size */
	dtb_size = fdt_totalsize(orig_fdt);
	if (dtb_size <= 0 || dtb_size > sizeof(spl_merged_dtb_buf)) {
		debug("WorldGuard: Invalid DTB size: %d\n", dtb_size);
		return NULL;
	}

	/* Copy original DTB to our buffer */
	memcpy(spl_merged_dtb_buf, orig_fdt, dtb_size);

	/* Open DTB for modification */
	ret = fdt_open_into(spl_merged_dtb_buf, spl_merged_dtb_buf, 
			    sizeof(spl_merged_dtb_buf));
	if (ret < 0) {
		debug("WorldGuard: Failed to open DTB for modification: %d\n", ret);
		return NULL;
	}

	/* Add WorldGuard properties */
	ret = spl_worldguard_add_dt_props(spl_merged_dtb_buf, mlwid, mwiddeleg);
	if (ret < 0) {
		debug("WorldGuard: Failed to add properties: %d\n", ret);
		return NULL;
	}

	/* Pack DTB (finalize modifications) */
	ret = fdt_pack(spl_merged_dtb_buf);
	if (ret < 0) {
		debug("WorldGuard: Failed to pack DTB: %d\n", ret);
		return NULL;
	}

	debug("WorldGuard: Created merged DTB (%d bytes)\n",
	      fdt_totalsize(spl_merged_dtb_buf));

	return spl_merged_dtb_buf;
}
