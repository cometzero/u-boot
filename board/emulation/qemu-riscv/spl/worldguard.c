// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2025 RISC-V QEMU Virt Boot Chain Project
 * WorldGuard initialization for U-Boot SPL
 */

#include <common.h>
#include <fdt_support.h>
#include <log.h>
#include <asm/io.h>
#include "worldguard.h"

/**
 * spl_worldguard_detect() - Detect WorldGuard hardware via Device Tree
 * @fdt: Pointer to device tree blob
 *
 * This implements DT-first detection (per clarification #4) to avoid
 * illegal instruction exceptions on non-WorldGuard systems.
 *
 * Returns: node offset if WorldGuard present, negative (FDT_ERR_*) if not
 */
static int spl_worldguard_detect(void *fdt)
{
	int node;

	if (!fdt) {
		debug("WorldGuard: No FDT provided\n");
		return -FDT_ERR_BADSTRUCTURE;
	}

	/* Check for riscv,worldguard compatible node in Device Tree */
	node = fdt_node_offset_by_compatible(fdt, -1, WG_DT_COMPAT);
	if (node < 0) {
		/* No WorldGuard node - this is expected on non-WG systems */
		debug("WorldGuard: No DT node found (not present)\n");
		return node;
	}

	debug("WorldGuard: DT node found at offset %d\n", node);
	return node;
}

/**
 * spl_worldguard_init() - Initialize WorldGuard in SPL
 * @fdt: Pointer to device tree blob
 *
 * Main entry point for WorldGuard initialization.
 * Uses DT-first detection to safely skip initialization if WorldGuard
 * is not present, avoiding illegal instruction exceptions.
 *
 * Returns: 0 on success, 1 if WorldGuard not present (silent skip),
 *          negative on error
 */
int spl_worldguard_init(void *fdt)
{
	int node;

	/* DT-first detection: Only access CSRs if DT node present */
	node = spl_worldguard_detect(fdt);
	if (node < 0) {
		/*
		 * WorldGuard DT node not found.
		 * This is the normal case for wg=off or non-WorldGuard systems.
		 * Return 1 to indicate "not present" (not an error).
		 */
		return 1;
	}

	/*
	 * WorldGuard DT node found.
	 * CSR initialization and wgChecker programming will be added
	 * in Phase 4 (US2) and Phase 5 (US3).
	 */
	debug("WorldGuard: Detected (initialization pending)\n");

	return 0;
}
