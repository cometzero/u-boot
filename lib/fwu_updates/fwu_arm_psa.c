// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
 *
 * Authors:
 *   Abdellatif El Khlifi <abdellatif.elkhlifi@arm.com>
 *   Davidson kumaresan <davidson.kumaresan@arm.com>
 */
#include <arm_ffa.h>
#include <dm.h>
#include <fwu_arm_psa.h>
#include <efi_loader.h>
#include <fwu.h>
#include <log.h>
#include <malloc.h>
#include <mapmem.h>
#include <linux/errno.h>

static void *g_fwu_buf;
static u64 g_fwu_buf_handle;
static u16 g_fwu_sp_id;
static struct udevice *g_dev;
static u16 g_svc_interface_id;
static bool g_fwu_initialized;

/**
 * fwu_discover_ts_sp_id() - Query the FWU partition ID
 *
 * Description: Use the FF-A driver to get the FWU partition ID.
 * If multiple partitions are found, use the first one.
 *
 * Return: 0 on success. Otherwise, failure.
 */
static int fwu_discover_ts_sp_id(void)
{
	u32 count = 0;
	int ret;
	u32 i;
	struct ffa_partition_desc *descs = NULL;
	struct ffa_send_direct_data msg;
	struct ffa_partition_uuid fwu_service_uuid = {0};

	/* Ask the driver to fill the buffer with the SPs info */

	ret = ffa_partition_info_get(g_dev, ALL_TS_SP_UUID, &count, &descs);
	if (ret) {
		log_err("FWU: Failure in querying partitions (err: %d)\n", ret);
		return ret;
	}

	if (!count) {
		log_err("FWU: No Trusted Service partition found\n");
		return -ENODATA;
	}

	if (!descs) {
		log_err("FWU: No partitions descriptors filled\n");
		return -EINVAL;
	}

	if (uuid_str_to_le_bin(TS_FWU_SERVICE_UUID,
			       (unsigned char *)&fwu_service_uuid)) {
		log_err("FWU: invalid FWU SP  UUID\n");
		return -EINVAL;
	}

	for (i = 0; i < count ; i++) {
		log_debug("FWU: Enquiring service from SP 0x%x\n",
			  descs[i].info.id);

		msg.data0 = TS_RPC_SERVICE_INFO_GET;
		msg.data1 = fwu_service_uuid.a1;
		msg.data2 = fwu_service_uuid.a2;
		msg.data3 = fwu_service_uuid.a3;
		msg.data4 = fwu_service_uuid.a4;

		ret = ffa_sync_send_receive(g_dev, descs[i].info.id, &msg, 0);
		if (ret) {
			log_err("FWU: FF-A error for SERVICE_INFO_GET (err: %d)\n",
				ret);
			return ret;
		}

		if (msg.data0 != TS_RPC_SERVICE_INFO_GET) {
			log_err("FWU: Wrong SERVICE_INFO_GET return: (%lx)\n",
				msg.data0);
			continue;
		}

		if (msg.data1 == RPC_SUCCESS) {
			g_svc_interface_id = GET_SVC_IFACE_ID(msg.data2);
			g_fwu_sp_id = descs[i].info.id;
			log_debug("FWU: Service interface ID 0x%x found\n",
				  g_svc_interface_id);
			return 0;
		}

		log_debug("FWU: service not found\n");
	}

	log_err("FWU: No SP provides the service\n");

	return -ENOENT;
}

/**
 * fwu_shared_buf_reclaim() - Reclaim the shared communication buffer
 *
 * Description: In case of errors, this function can be called to retrieve
 * the FWU shared buffer.
 *
 * Return: 0 on success. Otherwise, failure.
 */
static int fwu_shared_buf_reclaim(void)
{
	int reclaim_ret;

	reclaim_ret = ffa_memory_reclaim(g_dev, g_fwu_buf_handle, 0);
	if (reclaim_ret)
		log_err("FWU: FF-A memory reclaim failure (err: %d)\n",
			reclaim_ret);
	else
		log_debug("FWU: Shared buffer reclaimed\n");

	free(g_fwu_buf);
	g_fwu_buf = NULL;

	return reclaim_ret;
}

/**
 * fwu_shared_buf_init() - Setup the FWU shared communication buffer
 *
 * Description: The communication with the TS FWU SP is based on a buffer shared
 * between U-Boot and TS FWU SP allocated in normal world and accessed
 * by both sides. The buffer contains the data exchanged between both sides
 * such as the payloads data.
 *
 * Return: 0 on success. Otherwise, failure.
 */
static int fwu_shared_buf_init(void)
{
	struct ffa_mem_ops_args args = {0};
	struct ffa_mem_region_attributes attrs = {0};
	struct ffa_send_direct_data msg;
	int ret;

	g_fwu_buf = memalign(EFI_PAGE_SIZE, FWU_BUFFER_SIZE);
	if (!g_fwu_buf) {
		log_err("FWU: Failure to allocate the shared buffer\n");
		return -ENOMEM;
	}

	/* Setting up user arguments */
	args.use_txbuf = true;
	args.address = g_fwu_buf;
	args.pg_cnt = FWU_BUFFER_PAGES;
	args.nattrs = 1;
	attrs.receiver = g_fwu_sp_id;
	attrs.attrs = FFA_MEM_RW;
	args.attrs = &attrs;

	/* Registering the shared buffer with secure world (Trusted Services) */
	ret = ffa_memory_share(g_dev, &args);
	if (ret) {
		free(g_fwu_buf);
		g_fwu_buf = NULL;
		log_err("FWU: Failure setting up the shared buffer (err: %d)\n",
			ret);
		return ret;
	}

	g_fwu_buf_handle = args.g_handle;

	log_debug("FWU: shared buffer handle 0x%llx\n", g_fwu_buf_handle);

	/* Inform the FWU SP know about the shared buffer */

	msg.data0 = TS_RPC_MEM_RETRIEVE;
	msg.data1 = GET_FWU_BUF_LSW(g_fwu_buf_handle);
	msg.data2 = GET_FWU_BUF_MSW(g_fwu_buf_handle);

	ret = ffa_sync_send_receive(g_dev, g_fwu_sp_id, &msg, 0);
	if (ret) {
		log_err("FWU: FF-A message error for MEM_RETRIEVE (err: %d)\n",
			ret);
		goto failure;
	}

	if (msg.data0 != TS_RPC_MEM_RETRIEVE) {
		log_err("FWU: Unexpected MEM_RETRIEVE return: (%lx)\n",
			msg.data0);
		ret = -EINVAL;
		goto failure;
	}

	if (msg.data1 != RPC_SUCCESS) {
		log_err("FWU: MEM_RETRIEVE failed\n");
		ret = -EOPNOTSUPP;
		goto failure;
	}

	log_debug("FWU: MEM_RETRIEVE success for SP 0x%x\n", g_fwu_sp_id);

	return 0;

failure:
	fwu_shared_buf_reclaim();
	return ret;
}

/**
 * fwu_agent_init() - Setup the FWU agent
 *
 * Description: Perform the initializations required to communicate
 * and use the FWU agent in secure world.
 * The frontend of the FWU agent is the Trusted Services (aka TS)
 * FWU SP (aka Secure Partition).
 *
 * Return: 0 on success. Otherwise, failure.
 */
int fwu_agent_init(void)
{
	int ret;
	struct fwu_data *fwu_data;
	u32 active_idx;

	fwu_data = fwu_get_data();
	if (!fwu_data) {
		log_err("FWU: Cannot get FWU data\n");
		return -EINVAL;
	}

	ret = fwu_get_active_index(&active_idx);
	if (ret) {
		log_err("FWU: Failed to read boot index, err (%d)\n",
			ret);
		return ret;
	}

	if (fwu_data->trial_state)
		log_info("FWU: System booting in Trial State\n");
	else
		log_info("FWU: System booting in Regular State\n");

	ret = uclass_first_device_err(UCLASS_FFA, &g_dev);
	if (ret) {
		log_err("FWU: Cannot find FF-A bus device, err (%d)\n", ret);
		return ret;
	}

	ret = fwu_discover_ts_sp_id();
	if (ret)
		return ret;

	ret = fwu_shared_buf_init();
	if (ret)
		return ret;

	g_fwu_initialized = true;

	return 0;
}
