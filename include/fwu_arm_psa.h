/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
 *
 * Authors:
 *   Abdellatif El Khlifi <abdellatif.elkhlifi@arm.com>
 *   Davidson kumaresan <davidson.kumaresan@arm.com>
 */

#ifndef __FWU_ARM_PSA_H
#define __FWU_ARM_PSA_H

#include <linux/bitfield.h>
#include <u-boot/uuid.h>

#define FWU_BUFFER_PAGES		(1024)

/* 4 MB buffer shared with secure world */
#define FWU_BUFFER_SIZE			(FWU_BUFFER_PAGES * EFI_PAGE_SIZE)

/* TS UUID string for detecting all SPs  (in big-endian format) */
#define ALL_TS_SP_UUID			"d776cdbd-5e82-5147-3b96-ac4349f8d486"
/* In little-endian equivalent to: bdcd76d7-825e-4751-963b-86d4f84943ac */

/* TS FWU service UUID string (in big-endian format) */
#define TS_FWU_SERVICE_UUID		"38a82368-061b-0e47-7497-fd53fb8bce0c"
/* In little-endian equivalent to:  6823a838-1b06-470e-9774-0cce8bfb53fd */

#define TS_RPC_MEM_RETRIEVE		(0xff0001)
#define TS_RPC_SERVICE_INFO_GET		(0xff0003)
#define RPC_SUCCESS			(0)

#define SVC_IFACE_ID_GET_MASK		GENMASK(7, 0)
#define GET_SVC_IFACE_ID(x)		\
			 ((u8)(FIELD_GET(SVC_IFACE_ID_GET_MASK, (x))))

#define HANDLE_MSW_MASK			GENMASK(63, 32)
#define HANDLE_LSW_MASK			GENMASK(31, 0)
#define GET_FWU_BUF_MSW(x)		\
				((u32)(FIELD_GET(HANDLE_MSW_MASK, (x))))
#define GET_FWU_BUF_LSW(x)		\
				((u32)(FIELD_GET(HANDLE_LSW_MASK, (x))))

/**
 * fwu_agent_init() - Setup the FWU agent
 * Perform the initializations required to communicate
 * and use the FWU agent in secure world.
 * The frontend of the FWU agent is the Trusted Services (aka TS)
 * FWU SP (aka Secure Partition).
 *
 * Return: 0 on success. Otherwise, failure
 */
int fwu_agent_init(void);

#endif
