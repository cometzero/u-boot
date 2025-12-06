// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2025 RISC-V QEMU Virt Boot Chain Project
 * SPL board initialization for QEMU RISC-V
 */

#include <common.h>
#include <cpu_func.h>
#include <debug_uart.h>
#include <dm.h>
#include <hang.h>
#include <init.h>
#include <log.h>
#include <ram.h>
#include <spl.h>
#include <asm/arch/spl.h>
#include <asm/io.h>

DECLARE_GLOBAL_DATA_PTR;

/*
 * board_init_f() - SPL early initialization
 *
 * This function is called very early in the boot process, before DRAM
 * is initialized. It sets up the debug UART and prepares for DRAM init.
 */
void board_init_f(ulong dummy)
{
	int ret;

	/* Enable debug UART if configured */
#ifdef CONFIG_DEBUG_UART
	debug_uart_init();
#endif

	debug("SPL: Board init (M-mode)\n");

	/* Initialize architecture-specific components */
	ret = spl_early_init();
	if (ret) {
		debug("SPL: spl_early_init() failed: %d\n", ret);
		hang();
	}

	/* Initialize DRAM */
	ret = spl_init();
	if (ret) {
		debug("SPL: spl_init() failed: %d\n", ret);
		hang();
	}

	/* Initialize device model */
	ret = uclass_get_device(UCLASS_RAM, 0, &gd->ram);
	if (ret) {
		debug("SPL: DRAM init failed: %d\n", ret);
		hang();
	}

	debug("SPL: Board init complete\n");
}

/*
 * spl_board_init() - SPL board-specific initialization
 *
 * Called after SPL framework init and DRAM is available.
 * This is where we would initialize WorldGuard (in later phases).
 */
void spl_board_init(void)
{
	debug("SPL: Board-specific init\n");

	/*
	 * WorldGuard initialization will be added here in Phase 4
	 * For now, this is a placeholder for the boot chain test
	 */

	debug("SPL: Ready to load next stage\n");
}

/*
 * board_boot_order() - Define boot order for SPL
 *
 * SPL will try boot methods in this order.
 * For QEMU virt, we load OpenSBI from FIT image.
 */
void board_boot_order(u32 *spl_boot_list)
{
	/* Try loading from FIT image first */
	spl_boot_list[0] = BOOT_DEVICE_BOARD;
	spl_boot_list[1] = BOOT_DEVICE_NONE;
}
