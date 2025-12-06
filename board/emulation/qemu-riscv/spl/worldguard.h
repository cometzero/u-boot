// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2025 RISC-V QEMU Virt Boot Chain Project
 * WorldGuard initialization for U-Boot SPL
 */

#ifndef _WORLDGUARD_H_
#define _WORLDGUARD_H_

#include <asm/csr.h>

/* WorldGuard CSR addresses (RISC-V WorldGuard spec v0.4) */
#define CSR_MLWID       0x390   /* Machine Local World ID */
#define CSR_MWIDDELEG   0x748   /* Machine WID Delegation */

/* wgChecker MMIO base address (from DT reg property) */
#define WGCHECKER_BASE  0x6000000

/* wgChecker MMIO register offsets (per slot) */
#define WGC_SLOT_SIZE   0x20
#define WGC_SLOT_ADDR_OFFSET   0x0
#define WGC_SLOT_PERM_OFFSET   0x8
#define WGC_SLOT_CFG_OFFSET    0x10

/* Helper macros for wgChecker slot registers */
#define WGC_SLOT_ADDR(n)    (WGCHECKER_BASE + ((n) * WGC_SLOT_SIZE) + WGC_SLOT_ADDR_OFFSET)
#define WGC_SLOT_PERM(n)    (WGCHECKER_BASE + ((n) * WGC_SLOT_SIZE) + WGC_SLOT_PERM_OFFSET)
#define WGC_SLOT_CFG(n)     (WGCHECKER_BASE + ((n) * WGC_SLOT_SIZE) + WGC_SLOT_CFG_OFFSET)

/* wgChecker configuration bits */
#define WGC_CFG_LOCK    (1 << 0)  /* Lock bit */
#define WGC_CFG_TOR     (1 << 1)  /* Top-of-Range mode */
#define WGC_CFG_NAPOT   (0 << 1)  /* Naturally Aligned Power-of-Two */

/* Default WorldGuard configuration values */
#define WG_DEFAULT_NWORLDS      4
#define WG_DEFAULT_TRUSTEDWID   3
#define WG_DEFAULT_MWIDDELEG    0x6  /* Delegate WID 1,2 to S-mode */

/* Device Tree compatible strings */
#define WG_DT_COMPAT    "riscv,worldguard"
#define WGC_DT_COMPAT   "riscv,wgchecker"

/**
 * spl_worldguard_init() - Initialize WorldGuard in SPL
 * @fdt: Pointer to device tree blob
 *
 * Returns: 0 on success, negative on error, 1 if WorldGuard not present
 */
int spl_worldguard_init(void *fdt);

/**
 * spl_worldguard_add_dt_props() - Add WorldGuard properties to DTB
 * @fdt: Pointer to device tree blob to modify
 * @mlwid: Machine Local World ID value
 * @mwiddeleg: Machine WID Delegation value
 *
 * Returns: 0 on success, negative on error
 */
int spl_worldguard_add_dt_props(void *fdt, u32 mlwid, u32 mwiddeleg);

/**
 * spl_create_merged_dtb() - Create new DTB with WorldGuard properties
 * @orig_fdt: Original device tree
 * @mlwid: Machine Local World ID value
 * @mwiddeleg: Machine WID Delegation value
 *
 * Returns: Pointer to new DTB on success, NULL on error
 */
void *spl_create_merged_dtb(const void *orig_fdt, u32 mlwid, u32 mwiddeleg);

#endif /* _WORLDGUARD_H_ */
