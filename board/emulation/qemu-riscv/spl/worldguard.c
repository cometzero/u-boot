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
 * spl_worldguard_program_wgchecker() - Program wgChecker memory protection slots
 * @fdt: Pointer to device tree blob
 *
 * Parses wgChecker slot configuration from Device Tree and programs
 * MMIO registers for each slot.
 *
 * Returns: 0 on success, negative on error, 1 if wgChecker not present
 */
static int spl_worldguard_program_wgchecker(void *fdt)
{
	int node, len, i;
	const fdt32_t *prop;
	const fdt32_t *lock_prop;
	u32 addr_hi, addr_lo, size_hi, size_lo, perm, cfg;
	u64 addr;
	bool lock_slots = true;
	int num_slots;

	/* Find wgChecker node */
	node = fdt_node_offset_by_compatible(fdt, -1, WGC_DT_COMPAT);
	if (node < 0) {
		debug("WorldGuard: No wgChecker DT node found\n");
		return 1;  /* Not present, not an error */
	}

	/* Check lock-slots property (default: true) */
	lock_prop = fdt_getprop(fdt, node, "worldguard,lock-slots", &len);
	if (lock_prop && len == sizeof(fdt32_t)) {
		lock_slots = (fdt32_to_cpu(*lock_prop) != 0);
	}

	/* Get slots property */
	prop = fdt_getprop(fdt, node, "slots", &len);
	if (!prop || len == 0) {
		debug("WorldGuard: No slots property in wgChecker node\n");
		return 0;  /* No slots to program */
	}

	/* Each slot is 6 u32 values: addr_hi, addr_lo, size_hi, size_lo, perm, cfg */
	num_slots = len / (6 * sizeof(fdt32_t));
	if (num_slots == 0) {
		debug("WorldGuard: No valid slots found\n");
		return 0;
	}

	printf("WorldGuard: Programming %d wgChecker slots\n", num_slots);

	/* Program each slot */
	for (i = 0; i < num_slots; i++) {
		/* Parse slot configuration (6 values per slot) */
		addr_hi = fdt32_to_cpu(prop[i * 6 + 0]);
		addr_lo = fdt32_to_cpu(prop[i * 6 + 1]);
		size_hi = fdt32_to_cpu(prop[i * 6 + 2]);
		size_lo = fdt32_to_cpu(prop[i * 6 + 3]);
		perm = fdt32_to_cpu(prop[i * 6 + 4]);
		cfg = fdt32_to_cpu(prop[i * 6 + 5]);

		/* Combine hi/lo for 64-bit address */
		addr = ((u64)addr_hi << 32) | addr_lo;

		/* Program MMIO registers for this slot (1-indexed) */
		writel(addr_lo, WGC_SLOT_ADDR(i + 1));
		writel(perm, WGC_SLOT_PERM(i + 1));
		
		/* Set lock bit if enabled */
		if (lock_slots)
			cfg |= WGC_CFG_LOCK;
		
		writel(cfg, WGC_SLOT_CFG(i + 1));

		printf("  slot[%d]: addr=0x%lx perm=0x%x cfg=0x%x%s\n",
		       i + 1, (unsigned long)addr, perm, cfg,
		       lock_slots ? " (locked)" : "");
	}

	if (!lock_slots) {
		debug("WorldGuard: Slots not locked (configurable)\n");
	}

	return 0;
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
	int node, ret;
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

	/* Program wgChecker memory protection slots */
	ret = spl_worldguard_program_wgchecker(fdt);
	if (ret < 0) {
		debug("WorldGuard: wgChecker programming failed: %d\n", ret);
		/* Non-fatal, continue */
	}

	printf("WorldGuard: enabled, mlwid=%u, mwiddeleg=0x%x\n",
	       trustedwid, mwiddeleg);

	return 0;
}
