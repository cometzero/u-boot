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
 * spl_worldguard_read_dt_config() - Read WorldGuard config from Device Tree
 * @fdt: Pointer to device tree blob
 * @node: WorldGuard DT node offset
 * @nworlds: Output pointer for nworlds value
 * @trustedwid: Output pointer for trustedwid value
 * @mwiddeleg: Output pointer for mwiddeleg value
 *
 * Reads WorldGuard configuration from DT properties.
 * Falls back to defaults if properties not found.
 */
static void spl_worldguard_read_dt_config(void *fdt, int node,
					  u32 *nworlds, u32 *trustedwid,
					  u32 *mwiddeleg)
{
	const fdt32_t *prop;
	int len;

	/* Read nworlds property (default: 4) */
	prop = fdt_getprop(fdt, node, "nworlds", &len);
	if (prop && len == sizeof(fdt32_t))
		*nworlds = fdt32_to_cpu(*prop);
	else
		*nworlds = WG_DEFAULT_NWORLDS;

	/* Read trustedwid property (default: 3) */
	prop = fdt_getprop(fdt, node, "trustedwid", &len);
	if (prop && len == sizeof(fdt32_t))
		*trustedwid = fdt32_to_cpu(*prop);
	else
		*trustedwid = WG_DEFAULT_TRUSTEDWID;

	/* Read mwiddeleg property (default: 0x6) */
	prop = fdt_getprop(fdt, node, "mwiddeleg", &len);
	if (prop && len == sizeof(fdt32_t))
		*mwiddeleg = fdt32_to_cpu(*prop);
	else
		*mwiddeleg = WG_DEFAULT_MWIDDELEG;

	debug("WorldGuard: DT config - nworlds=%u, trustedwid=%u, mwiddeleg=0x%x\n",
	      *nworlds, *trustedwid, *mwiddeleg);
}

/**
 * spl_worldguard_init_csrs() - Initialize WorldGuard CSRs
 * @mlwid: Machine Local World ID value
 * @mwiddeleg: Machine WID Delegation value
 *
 * Writes WorldGuard CSRs in M-mode.
 * This must be called before transitioning to S-mode.
 */
static void spl_worldguard_init_csrs(u32 mlwid, u32 mwiddeleg)
{
	/* Set Machine Local World ID (mlwid) */
	csr_write(CSR_MLWID, mlwid);

	/* Set Machine WID Delegation (mwiddeleg) */
	csr_write(CSR_MWIDDELEG, mwiddeleg);

	debug("WorldGuard: CSRs initialized - mlwid=%u, mwiddeleg=0x%x\n",
	      mlwid, mwiddeleg);
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
	u32 nworlds, trustedwid, mwiddeleg;

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

	/* Read WorldGuard configuration from Device Tree */
	spl_worldguard_read_dt_config(fdt, node, &nworlds, &trustedwid, &mwiddeleg);

	/* Initialize WorldGuard CSRs */
	spl_worldguard_init_csrs(trustedwid, mwiddeleg);

	printf("WorldGuard: enabled, mlwid=%u, mwiddeleg=0x%x\n",
	       trustedwid, mwiddeleg);

	return 0;
}
