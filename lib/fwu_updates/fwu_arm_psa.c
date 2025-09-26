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
#include <fwu.h>
#include <fwu_arm_psa.h>
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
static u64 g_max_payload_size;
static u8 g_fwu_version_major;
static u8 g_fwu_version_minor;
static bool g_fwu_initialized;
struct fwu_image_directory g_fwu_cached_directory;
efi_guid_t g_update_guid[CONFIG_FWU_NUM_IMAGES_PER_BANK];

/* Error mapping declarations */

int fwu_to_std_errmap[MAX_NUMBER_FWU_ERR] = {
	[FWU_UNKNOWN] = -EOPNOTSUPP,
	[FWU_OUT_OF_BOUNDS] = -ERANGE,
	[FWU_AUTH_FAIL] = -EFAULT,
	[FWU_BUSY] = -EBUSY,
	[FWU_NO_PERMISSION] = -EPERM,
	[FWU_DENIED] = -EACCES,
	[FWU_RESUME] = -EAGAIN,
	[FWU_NOT_AVAILABLE] = -ENAVAIL,
};

static struct fwu_abi_errmap err_msg_map[FWU_ERRMAP_COUNT] = {
	[FWU_ID_TO_ERRMAP_ID(FWU_OPEN)] = {
		{
			[FWU_UNKNOWN] =
			"FWU_UNKNOWN: Image type with GUID=image_type_guid does not exist",
			[FWU_DENIED] =
			"FWU_DENIED: An image cannot be opened for writing outside of this Staging state",
			[FWU_NOT_AVAILABLE] =
			"FWU_NOT_AVAILABLE: The Update Agent does not support the op_type for this image",
		},
	},
	[FWU_ID_TO_ERRMAP_ID(FWU_READ_STREAM)] = {
		{
			[FWU_UNKNOWN] =
			"FWU_UNKNOWN: Handle is not recognized",
			[FWU_DENIED] =
			"FWU_DENIED: The image cannot be temporarily read from",
			[FWU_NO_PERMISSION] =
			"FWU_NO_PERMISSION: The image cannot be read from",
		},
	},
	[FWU_ID_TO_ERRMAP_ID(FWU_WRITE_STREAM)] = {
		{
			[FWU_UNKNOWN] =
			"FWU_UNKNOWN: Unrecognized handle",
			[FWU_DENIED] =
			"FWU_DENIED: The system is not in a Staging state",
			[FWU_NO_PERMISSION] =
			"FWU_NO_PERMISSION: The image cannot be written to",
			[FWU_OUT_OF_BOUNDS] =
			"FWU_OUT_OF_BOUNDS: less than data_len bytes available in the image",
		},
	},
	[FWU_ID_TO_ERRMAP_ID(FWU_COMMIT)] = {
		{
			[FWU_UNKNOWN] =
			"FWU_UNKNOWN: Handle is not recognized",
			[FWU_DENIED] =
			"FWU_DENIED: The image can only be accepted after activation",
			[FWU_AUTH_FAIL] =
			"FWU_AUTH_FAIL: Image closed, authentication failed",
			[FWU_RESUME] =
			"FWU_RESUME: The Update Agent yielded",
		},
	},
	[FWU_ID_TO_ERRMAP_ID(FWU_BEGIN_STAGING)] = {
		{
			[FWU_UNKNOWN] =
			"FWU_UNKNOWN: One of more GUIDs in the update_guid field are unknown to the Update Agent",
			[FWU_DENIED] =
			"FWU_DENIED: The Firmware Store is in the Trial state or the platform did not boot correctly",
			[FWU_BUSY] =
			"FWU_BUSY: The Client is temporarily prevented from entering the Staging state",
		},
	},
	[FWU_ID_TO_ERRMAP_ID(FWU_END_STAGING)] = {
		{
			[FWU_BUSY] =
			"FWU_BUSY: There are open image handles",
			[FWU_DENIED] =
			"FWU_DENIED: The system is not in a Staging state",
			[FWU_AUTH_FAIL] =
			"FWU_AUTH_FAIL: At least one of the updated images fails to authenticate",
			[FWU_NOT_AVAILABLE] =
			"FWU_NOT_AVAILABLE: The Update Agent does not support partial updates",
		},
	},
	[FWU_ID_TO_ERRMAP_ID(FWU_CANCEL_STAGING)] = {
		{
			[FWU_DENIED] =
			"FWU_DENIED: The system is not in a Staging state",
		},
	},
	[FWU_ID_TO_ERRMAP_ID(FWU_ACCEPT_IMAGE)] = {
		{
			[FWU_UNKNOWN] =
			"FWU_UNKNOWN: Image with type=image_type_guid is not managed by the Update Agent",
			[FWU_DENIED] =
			"FWU_DENIED: The system has not booted with the active bank, or the image cannot be accepted before being activated",
		},
	},
};

/**
 * fwu_to_std_errno() - convert FWU error code to standard error code
 * @fwu_errno:	Error code returned by the FWU ABI
 *
 * Description: Map the given FWU error code as specified
 * by the spec to a U-Boot standard error code.
 *
 * Return: The standard error code on success. . Otherwise, failure.
 */
static int fwu_to_std_errno(int fwu_errno)
{
	int err_idx = -fwu_errno;

	/* Map the FWU error code to the standard u-boot error code */
	if (err_idx > 0 && err_idx < MAX_NUMBER_FWU_ERR)
		return fwu_to_std_errmap[err_idx];
	return -EINVAL;
}

/**
 * fwu_print_error_log() - print the error log of the selected FWU ABI
 * @fwu_id:	FWU ABI ID
 * @fwu_errno:	Error code returned by the FWU ABI
 *
 * Description: Map the FWU error code to the error log relevant to the
 * selected FWU ABI. Then the error log is printed.
 *
 * Return: 0 on success. Otherwise, failure.
 */
static int fwu_print_error_log(u32 fwu_id, int fwu_errno)
{
	int err_idx = -fwu_errno, abi_idx = 0;

	/* Map the FWU error code to the corresponding error log */

	if (err_idx <= 0 || err_idx >= MAX_NUMBER_FWU_ERR)
		return -EINVAL;

	if (fwu_id < FWU_FIRST_ID || fwu_id > FWU_LAST_ID)
		return -EINVAL;

	abi_idx = FWU_ID_TO_ERRMAP_ID(fwu_id);

	if (!err_msg_map[abi_idx].err_str[err_idx])
		return -EINVAL;

	log_err("%s\n", err_msg_map[abi_idx].err_str[err_idx]);

	return 0;
}

/**
 * fwu_get_payload_type() - Identifies the payload type
 * @image_index:	The payload index
 *
 * Description: Identifies the FWU payload type based on the image index.
 *
 * Return: See @fwu_payload_type for details
 */
enum fwu_payload_type fwu_get_payload_type(u32 image_index)
{
	efi_guid_t *image_guid = NULL;
	int i;
	struct efi_fw_image *fw_array;

	fw_array = update_info.images;
	for (i = 0; i < update_info.num_images; i++) {
		if (fw_array[i].image_index == image_index) {
			image_guid = &fw_array[i].image_type_id;
			break;
		}
	}

	if (!image_guid)
		return FWU_PAYLOAD_TYPE_INVALID;

	if (!guidcmp(image_guid,
		     &((efi_guid_t)FWU_DUMMY_START_IMAGE_GUID)))
		return FWU_PAYLOAD_TYPE_DUMMY_START;

	if (!guidcmp(image_guid,
		     &((efi_guid_t)FWU_DUMMY_END_IMAGE_GUID)))
		return FWU_PAYLOAD_TYPE_DUMMY_END;

	return FWU_PAYLOAD_TYPE_REAL;
}

/**
 * fwu_get_capsule_guids() - Detect the payloads GUIDs in the caspsule
 *
 * @partial_update_count:	A pointer to the number of payloads to update
 * @saved_guids:	A pointer to a GUIDs array for the payloads GUIDs
 *
 * Description: Parse the current capsule and detect the payloads GUIDs.
 *
 * Return: EFI_SUCCESS on success. Otherwise, failure.
 */
static efi_status_t fwu_get_capsule_guids(u32 *partial_update_count,
					  efi_guid_t saved_guids[])
{
	struct efi_firmware_management_capsule_header *capsule;
	struct efi_firmware_management_capsule_image_header *image;
	int item;
	size_t capsule_size;
	efi_status_t ret = EFI_SUCCESS;

	if (!saved_guids || !partial_update_count)
		return EFI_INVALID_PARAMETER;

	*partial_update_count = 0;
	capsule = (void *)g_capsule_data + g_capsule_data->header_size;
	capsule_size = g_capsule_data->capsule_image_size
		- g_capsule_data->header_size;

	/* Payload */
	for (item = capsule->embedded_driver_count;
	     item < capsule->embedded_driver_count
		     + capsule->payload_item_count; item++) {
		/* sanity check */
		if ((capsule->item_offset_list[item] + sizeof(*image)
				>= capsule_size)) {
			ret = EFI_INVALID_PARAMETER;
			log_err("FWU: Insufficient data, err (0x%lx)\n", ret);
			break;
		}

		image = (void *)capsule + capsule->item_offset_list[item];

		if (image->version !=
			EFI_FIRMWARE_MANAGEMENT_CAPSULE_IMAGE_HEADER_VERSION) {
			ret = EFI_UNSUPPORTED;
			log_err("FWU: Version check failed, err (0x%lx)\n",
				ret);
			break;
		}

		if (fwu_get_payload_type(image->update_image_index) !=
					 FWU_PAYLOAD_TYPE_REAL)
			continue;

		guidcpy(&saved_guids[*partial_update_count],
			&image->update_image_type_id);

		(*partial_update_count)++;
	}

	return ret;
}

/**
 * fwu_invoke_svc() - FWU service call request
 * @svc_id: FWU ABI function ID
 * @svc_name: FWU ABI function name
 * @req_args_sz: Size in bytes of the arguments of the FWU ABI function
 * @expect_resp_sz: Size in bytes of the response of the FWU ABI function
 * @extra_resp_bytes: Size in bytes of the extra response data
 *
 * Description: Invoke a FWU ABI by issuing a TS service call request.
 *
 * Return: 0 on success. Otherwise, failure.
 */
static int fwu_invoke_svc(u32 svc_id, const char *svc_name,
			  u32 req_args_sz, u32 expect_resp_sz,
			  u32 *extra_resp_bytes)
{
	struct ffa_send_direct_data msg;
	int ret;
	int *svc_status_in_buf = g_fwu_buf, svc_status_in_reg;
	u32 expect_total_resp_sz;

	log_debug("FWU: Invoking %s\n", svc_name);

	if (!expect_resp_sz || !svc_name || !req_args_sz) {
		log_err("%s: Invalid invoke arguments\n", svc_name);
		return -EINVAL;
	}

	msg.data0 = PACK_SVC_IFACE_ID(svc_id, g_svc_interface_id);
	msg.data1 = GET_FWU_BUF_LSW(g_fwu_buf_handle);
	msg.data2 = GET_FWU_BUF_MSW(g_fwu_buf_handle);
	msg.data3 = req_args_sz;
	msg.data4 = 0;

	ret = ffa_sync_send_receive(g_dev, g_fwu_sp_id, &msg, 0);
	if (ret) {
		log_err("%s: FF-A error %d\n", svc_name, ret);
		return ret;
	}

	if (msg.data0 != PACK_SVC_IFACE_ID(svc_id, g_svc_interface_id)) {
		log_err("%s: Unexpected service/interface ID 0x%lx\n",
			svc_name, msg.data0);
		return -EINVAL;
	}

	if (msg.data1 != RPC_SUCCESS) {
		log_err("%s: TS RPC error 0x%lx\n", svc_name, msg.data1);
		return -ECOMM;
	}

	svc_status_in_reg = (int)msg.data2;
	if (*svc_status_in_buf != svc_status_in_reg) {
		log_err("%s: Status mismatch (reg %d , buf %d)\n",
			svc_name, svc_status_in_reg, *svc_status_in_buf);
		return -EINVAL;
	}

	if (svc_status_in_reg < 0) {
		fwu_print_error_log(svc_id, svc_status_in_reg);
		if (svc_status_in_reg != -FWU_RESUME)
			return fwu_to_std_errno(svc_status_in_reg);
	}

	if (!extra_resp_bytes)
		expect_total_resp_sz = expect_resp_sz;
	else
		expect_total_resp_sz = expect_resp_sz + *extra_resp_bytes;

	if (msg.data3 != expect_total_resp_sz) {
		log_err("%s: Unexpected response size (%ld , %d)\n",
			svc_name, msg.data3, expect_total_resp_sz);
		return -EINVAL;
	}

	if (svc_status_in_reg == -FWU_RESUME)
		return fwu_to_std_errno(svc_status_in_reg);

	return 0;
}

/**
 * fwu_discover() -  fwu_discover ABI
 *
 * Description: This call indicates the version of the ABI alongside a list of
 * the implemented functions (aka services).
 * Only max_payload_size is saved for future use.
 *
 * Return: 0 on success. Otherwise, failure.
 */
static int fwu_discover(void)
{
	int ret;
	struct fwu_discover_args *args = g_fwu_buf;
	struct fwu_discover_resp *resp = g_fwu_buf;
	char *svc_name = "FWU_DISCOVER";

	/* Filling the arguments in the shared buffer */
	args->function_id = FWU_DISCOVER;

	/* Executing the FWU ABI through the FF-A bus */
	ret = fwu_invoke_svc(args->function_id, svc_name, sizeof(*args),
			     sizeof(int), NULL);
	if (ret) {
		log_debug("FWU_DISCOVER: error %d\n", ret);
		return ret;
	}

	g_max_payload_size = resp->max_payload_size;
	g_fwu_version_major = resp->version_major;
	g_fwu_version_minor = resp->version_minor;

	log_debug("FWU: max_payload_size %llu\n", g_max_payload_size);
	log_info("FWU: ABI version %d.%d detected\n", g_fwu_version_major,
		 g_fwu_version_minor);

	return 0;
}

/**
 * fwu_open() -  fwu_open ABI
 * @guid: GUID of the image to be opened
 * @op_type: The operation that the Client will perform on the image
 * @handle: Staging context identifier
 *
 * Description: Returns a handle to the image with a given GUID.
 *
 * Return: 0 on success. Otherwise, failure.
 */
static int fwu_open(const efi_guid_t *guid, u8  op_type,
		    u32 *handle)
{
	int ret;
	struct fwu_open_args *args = g_fwu_buf;
	struct fwu_open_resp *resp = g_fwu_buf;
	char *svc_name = "FWU_OPEN";

	if (!guid || !handle)
		return -EINVAL;

	/* Filling the arguments in the shared buffer */
	args->function_id = FWU_OPEN;

	guidcpy(&args->image_type_guid, guid);
	args->op_type = op_type;

	/* Executing the FWU ABI through the FF-A bus */
	ret = fwu_invoke_svc(args->function_id, svc_name,
			     sizeof(*args), sizeof(*resp), NULL);
	if (ret)
		return ret;

	*handle = resp->handle;

	return 0;
}

/**
 * fwu_read_stream() -  fwu_read_stream ABI
 * @handle: The handle of the context being read from
 * @buffer: The destination buffer where the data will be copied to
 * @buffer_size: The size of the destination buffer
 *
 * Description: The call reads at most g_max_payload_size bytes from the Update
 * Agent context pointed to by handle.
 *
 * Return: 0 on success. Otherwise, failure.
 */
static int fwu_read_stream(u32 handle, u8 *buffer, u32 buffer_size)
{
	int ret;
	u32 curr_read_offset = 0, new_read_offset, fwu_buf_bytes_left;
	struct fwu_read_stream_args *args = g_fwu_buf;
	struct fwu_read_stream_resp *resp = g_fwu_buf;
	char *svc_name = "FWU_READ_STREAM";

	if (!buffer || !buffer_size)
		return -EINVAL;

	do {
		/* Filling the arguments in the shared buffer */
		args->function_id = FWU_READ_STREAM;
		args->handle = handle;

		/* Executing the FWU ABI through the FF-A bus */
		ret = fwu_invoke_svc(args->function_id, svc_name, sizeof(*args),
				     sizeof(*resp), &resp->read_bytes);
		if (ret)
			return ret;

		if (resp->read_bytes > g_max_payload_size) {
			log_err("%s: Bytes read > max_payload_size (%d , %llu)\n",
				svc_name, resp->read_bytes,
				g_max_payload_size);
			return -EINVAL;
		}

		fwu_buf_bytes_left = FWU_BUFFER_SIZE - sizeof(*resp);

		if (resp->read_bytes > fwu_buf_bytes_left) {
			log_err("%s: Bytes read > shared buffer (%d , %d)\n",
				svc_name, resp->read_bytes, fwu_buf_bytes_left);
			return -EINVAL;
		}

		if (resp->total_bytes > buffer_size) {
			log_err("%s: Total bytes > dest buffer (%d , %d)\n",
				svc_name, resp->total_bytes, buffer_size);
			return -EINVAL;
		}

		new_read_offset = resp->read_bytes + curr_read_offset;

		if (new_read_offset > buffer_size) {
			log_err("%s: Bytes read > dest buffer (%d , %d)\n",
				svc_name, new_read_offset, buffer_size);
			return -EINVAL;
		}

		memcpy(buffer + curr_read_offset, resp->payload,
		       resp->read_bytes);

		curr_read_offset = new_read_offset;

		if (curr_read_offset > resp->total_bytes) {
			log_err("%s: Offset bypassed total bytes (%d , %d)\n",
				svc_name, curr_read_offset, resp->total_bytes);
			return -EINVAL;
		}

	} while (curr_read_offset != resp->total_bytes);

	return ret;
}

/**
 * fwu_begin_staging() -  fwu_begin_staging ABI
 *
 * Description: This call indicates to the Update Agent that a new staging
 *  process will commence.
 *
 * Return: 0 on success. Otherwise, failure.
 */
static int fwu_begin_staging(void)
{
	struct fwu_begin_staging_args *args = g_fwu_buf;
	struct fwu_begin_staging_resp *resp = g_fwu_buf;
	char *svc_name = "FWU_BEGIN_STAGING";
	efi_status_t ret;

	/* Filling the arguments in the shared buffer */
	args->function_id = FWU_BEGIN_STAGING;

	args->reserved = 0;
	args->vendor_flags = 0;

	ret = fwu_get_capsule_guids(&args->partial_update_count,
				    args->update_guid);
	if (ret) {
		log_err("FWU: Failure to get the payloads GUIDs\n");
		return -ENODATA;
	}

	log_info("FWU: Updating %d payload(s)\n", args->partial_update_count);

	/* Executing the FWU ABI through the FF-A bus */
	return fwu_invoke_svc(args->function_id, svc_name,
			      sizeof(*args), sizeof(*resp), NULL);
}

/**
 * fwu_end_staging() -  fwu_end_staging ABI
 *
 * Description: The Client informs the Update Agent that all the images, meant
 * to be updated, have been transferred to the Update Agent and that the staging
 * has terminated.
 *
 * Return: 0 on success. Otherwise, failure.
 */
static int fwu_end_staging(void)
{
	struct fwu_end_staging_args *args = g_fwu_buf;
	struct fwu_end_staging_resp *resp = g_fwu_buf;
	char *svc_name = "FWU_END_STAGING";

	/* Filling the arguments in the shared buffer */
	args->function_id = FWU_END_STAGING;

	/* Executing the FWU ABI through the FF-A bus */
	return fwu_invoke_svc(args->function_id, svc_name,
			      sizeof(*args), sizeof(*resp), NULL);
}

/**
 * fwu_cancel_staging() -  fwu_cancel_staging ABI
 *
 * Description: The Client cancels the staging procedure and the system
 * transitions back to the Regular state.
 *
 * Return: 0 on success. Otherwise, failure.
 */
static int fwu_cancel_staging(void)
{
	struct fwu_cancel_staging_args *args = g_fwu_buf;
	struct fwu_cancel_staging_resp *resp = g_fwu_buf;
	char *svc_name = "FWU_CANCEL_STAGING";

	/* Filling the arguments in the shared buffer */
	args->function_id = FWU_CANCEL_STAGING;

	/* Executing the FWU ABI through the FF-A bus */
	return fwu_invoke_svc(args->function_id, svc_name,
			      sizeof(*args), sizeof(*resp), NULL);
}

/**
 * fwu_commit() -  fwu_commit ABI
 * @handle: The handle of the context being closed
 * @acceptance_req: Acceptance status set by the Client
 * @max_atomic_len: Hint, maximum time (in ns) that the Update Agent can execute
 *                  continuously without yielding back to the Client state
 *
 * Description: The call closes the image pointed to by handle. The image can be
 * any entity opened with fwu_open().
 *
 * Return: 0 on success. Otherwise, failure.
 */
static int fwu_commit(u32 handle, u32 acceptance_req, u32 max_atomic_len)
{
	struct fwu_commit_args *args = g_fwu_buf;
	struct fwu_commit_resp *resp = g_fwu_buf;
	char *svc_name = "FWU_COMMIT";
	int ret;

	/* Filling the arguments in the shared buffer */
	args->function_id = FWU_COMMIT;
	args->handle = handle;
	args->acceptance_req = acceptance_req;
	args->max_atomic_len = max_atomic_len;

	/* Executing the FWU ABI through the FF-A bus */
	ret = fwu_invoke_svc(args->function_id, svc_name,
			     sizeof(*args), sizeof(*resp), NULL);

	while (resp->status == -FWU_RESUME)
		ret = fwu_invoke_svc(args->function_id, svc_name, sizeof(*args),
				     sizeof(*resp), NULL);

	if (ret)
		return ret;

	log_debug("%s:  Progress %d/%d\n", svc_name, resp->progress,
		  resp->total_work);

	return 0;
}

/**
 * fwu_write_stream() -  fwu_write_stream ABI
 * @handle: The handle of the context being writen to
 * @payload: The data to be transferred
 * @payload_size: Size of the data present in the payload
 *
 *  Description: The call writes at most max_payload_size bytes to the Update
 *  Agent context pointed to by handle.
 *
 * Return: 0 on success. Otherwise, failure
 */
static int fwu_write_stream(u32 handle, const u8 *payload, u32 payload_size)
{
	int ret;
	u32 write_size, max_write_size, curr_write_offset = 0;
	u32 payload_bytes_left = payload_size, fwu_buf_bytes_left;
	struct fwu_write_stream_args *args = g_fwu_buf;
	struct fwu_write_stream_resp *resp = g_fwu_buf;
	char *svc_name = "FWU_WRITE_STREAM";

	if (!payload || !payload_size)
		return -EINVAL;

	fwu_buf_bytes_left = FWU_BUFFER_SIZE - sizeof(*args);

	if (g_max_payload_size <= fwu_buf_bytes_left)
		max_write_size = g_max_payload_size;
	else
		max_write_size = fwu_buf_bytes_left;

	while (curr_write_offset < payload_size) {
		if (payload_bytes_left <= max_write_size)
			write_size = payload_bytes_left;
		else
			write_size = max_write_size;

		/* Filling the arguments in the shared buffer */
		args->function_id = FWU_WRITE_STREAM;
		args->handle = handle;
		args->data_len = write_size;
		memcpy(args->payload, payload + curr_write_offset, write_size);

		/* Executing the FWU ABI through the FF-A bus */
		ret = fwu_invoke_svc(args->function_id, svc_name, sizeof(*args),
				     sizeof(*resp), NULL);
		if (ret)
			return ret;

		curr_write_offset += write_size;
		payload_bytes_left -= write_size;

		log_debug("%s:  %d bytes written, remaining %d bytes\n",
			  svc_name, write_size, payload_bytes_left);
	}

	return ret;
}

/**
 * fwu_accept() -  fwu_accept_image ABI
 *
 * @guid: GUID of the image to be accepted
 *
 * Description: Sets the status of the firmware image, with a given GUID
 * to "accepted" in the active firmware bank.
 *
 * Return: 0 on success. Otherwise, failure.
 */
static int fwu_accept(const efi_guid_t *guid)
{
	struct fwu_accept_image_args *args = g_fwu_buf;
	struct fwu_accept_image_resp *resp = g_fwu_buf;
	char *svc_name = "FWU_ACCEPT_IMAGE";

	if (!guid)
		return -EINVAL;

	/* Filling the arguments in the shared buffer */
	args->function_id = FWU_ACCEPT_IMAGE;

	guidcpy(&args->image_type_guid, guid);

	/* Executing the FWU ABI through the FF-A bus */
	return fwu_invoke_svc(args->function_id, svc_name,
			     sizeof(*args), sizeof(*resp), NULL);
}

/**
 * fwu_update_image() - Update an image
 *
 * @image: Pointer to the payload to write
 * @image_index: The payload index
 * @image_size: The payload size
 *
 * Description: Perform staging with multiple payloads support.
 * The capsule is expected to:
 *     - Start with a dummy payload to mark the start of the payloads sequence
 *     - One or more payloads to be written to the storage device
 *     - End with a dummy payload to mark the end of the payloads sequence
 *
 * The possible payloads in the capsule are described in the board file
 * through struct efi_fw_image. This includes the dummy payloads.
 * The dummy payloads image indexes must be >= CONFIG_FWU_NUM_IMAGES_PER_BANK
 * The dummy payloads are not sent to the Secure world and are not written to
 * the storage device.
 *
 * Return: 0 on success. Otherwise, failure.
 */
int fwu_update_image(const void *image, u8 image_index, u32 image_size)
{
	int ret;
	u32 handle;

	if (!image)
		return -EINVAL;

	/* Only image indexes starting from 1 are supported */
	if (!image_index || image_index > update_info.num_images)
		return -EINVAL;

	if (fwu_get_payload_type(image_index) ==
		FWU_PAYLOAD_TYPE_DUMMY_START) {
		return fwu_begin_staging();
	}

	if (fwu_get_payload_type(image_index) ==
		FWU_PAYLOAD_TYPE_DUMMY_END) {
		ret = fwu_end_staging();
		if (ret)
			goto cancel_staging;
		return 0;
	}

	ret = fwu_open(&g_fwu_cached_directory.entries[image_index - 1].image_guid,
		       FWU_OP_TYPE_WRITE, &handle);
	if (ret)
		goto cancel_staging;

	ret = fwu_write_stream(handle, image, image_size);
	if (ret)
		goto cancel_staging;

	/*
	 * The Update Agent can execute for an unbounded time.
	 * The image should be tried before being accepted.
	 * So, we put the acceptance request as 'not accepted'.
	 */
	ret = fwu_commit(handle, FWU_IMG_NOT_ACCEPTED, 0);
	if (ret)
		goto cancel_staging;

	log_debug("FWU: Image at index %d updated\n", image_index);

	return 0;

cancel_staging:

	return fwu_cancel_staging();
}

/**
 * fwu_read_directory() - Read FWU directory information
 *
 * Description: Read FWU directory information.
 * For more details see fwu_image_directory structure.
 *
 * Return: 0 on success. Otherwise, failure.
 */
static int fwu_read_directory(void)
{
	int ret, close_ret;
	u32 handle = 0;
	efi_guid_t dir_guid = {0};
	char *uuid_str = FWU_DIRECTORY_CANONICAL_UUID;

	if (!uuid_str) {
		log_err("FWU: No directory UUID provided\n");
		return -EINVAL;
	}

	if (uuid_str_to_le_bin(uuid_str, dir_guid.b)) {
		log_err("FWU: Invalid directory UUID\n");
		return -EINVAL;
	}

	ret = fwu_open(&dir_guid, FWU_OP_TYPE_READ, &handle);
	if (ret) {
		log_err("FWU: Open image directory failed (err: %d)\n",
			ret);
		return ret;
	}

	log_debug("FWU: Image directory handle (0x%x)\n", handle);

	ret = fwu_read_stream(handle, (u8 *)&g_fwu_cached_directory,
			      sizeof(g_fwu_cached_directory));
	if (ret) {
		log_err("FWU: Read image directory failed (err: %d)\n",
			ret);
		goto close_handle;
	}

	log_debug("FWU: directory_version (%d)\n",
		  g_fwu_cached_directory.directory_version);

	/*
	 * Note: The expected images in the directory are:
	 * - The images to be updated
	 * - The ESRT image (an image containing ESRT data)
	 * The ESRT image is not involved in the FWU.
	 * It should be removed from the count.
	 */
	g_fwu_cached_directory.num_images -= 1;

	if (g_fwu_cached_directory.num_images !=
	    CONFIG_FWU_NUM_IMAGES_PER_BANK) {
		log_err("FWU: Unexpected image count (%d , %d)\n",
			g_fwu_cached_directory.num_images,
			CONFIG_FWU_NUM_IMAGES_PER_BANK);
		ret = -EINVAL;
		goto close_handle;
	}

	log_debug("FWU: images to be updated (%d)\n",
		  g_fwu_cached_directory.num_images);
	log_debug("FWU: img_info_size (%d)\n",
		  g_fwu_cached_directory.img_info_size);

close_handle:
	/* The Update Agent can execute for an unbounded time */
	close_ret = fwu_commit(handle, FWU_IMG_NOT_ACCEPTED, 0);
	if (close_ret)
		log_err("FWU: Close image directory handle failed (err: %d)\n",
			close_ret);

	return ret;
}

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
 * fwu_one_image_accepted() - Accept one image in trial state
 *
 * @img_entry: Pointer to the image entry.
 * @active_idx: Active bank index.
 * @image_number: Image number for logging purposes.
 *
 * Description: Invoke FWU accept image ABI to accept the image.
 *
 * Return: true on success, false on failure.
 */
static bool fwu_one_image_accepted(const struct fwu_image_entry *img_entry,
				   u32 active_idx,
				   u32 image_number)
{
	const struct fwu_image_bank_info *bank_info =
		&img_entry->img_bank_info[active_idx];
	int fwu_ret;

	if (!bank_info->accepted) {
		fwu_ret = fwu_accept(&bank_info->image_guid);
		if (fwu_ret) {
			log_err("FWU: Failed to accept image #%d\n",
				image_number + 1);
			return false;
		}
		log_debug("FWU: Image #%d accepted\n", image_number + 1);
	}

	return true;
}

/**
 * fwu_all_images_accepted() - Accept any pending firmware update images
 *
 * @fwu_data: Pointer to FWU data structure
 *
 * Description: Read from the metadata the acceptance state of each image.
 * Then, accept the images which are not accepted yet.
 *
 * Return: true on success, false on failure.
 */
static bool fwu_all_images_accepted(const struct fwu_data *fwu_data)
{
	int fwu_ret;
	u32 active_idx;
	u32 i;
	bool accepted;

	fwu_ret = fwu_get_active_index(&active_idx);
	if (fwu_ret) {
		log_err("FWU: Failed to read boot index, err (%d)\n",
			fwu_ret);
		return false;
	}

	for (i = 0 ; i < CONFIG_FWU_NUM_IMAGES_PER_BANK ; i++) {
		accepted = fwu_one_image_accepted(&fwu_data->fwu_images[i], active_idx, i);
		if (!accepted)
			return false;
	}

	return true;
}

/**
 * fwu_notify_exit_boot_services() - FWU notification handler
 *
 * Description: Some boards need to perform custom actions on ExitBootService()
 * related to FWU. This function can be overridden by the board.
 *
 * Return: EFI_SUCCESS on success. Otherwise, failure.
 */
efi_status_t __weak fwu_notify_exit_boot_services(void)
{
	return EFI_SUCCESS;
}

/**
 * fwu_accept_notify_exit_boot_services() - ExitBootServices callback
 *
 * @event:	callback event
 * @context:	callback context
 *
 * Description: Reaching ExitBootServices() level means the boot succeeded.
 * So, accept all the images.
 *
 * Return: EFI_SUCCESS on success. Otherwise, failure.
 */
static void EFIAPI fwu_accept_notify_exit_boot_services(struct efi_event *event,
							void *context)
{
	efi_status_t efi_ret = EFI_SUCCESS;
	bool all_accepted;
	struct fwu_data *fwu_data;

	EFI_ENTRY("%p, %p", event, context);

	fwu_data = fwu_get_data();
	if (!fwu_data) {
		log_err("FWU: Cannot get FWU data\n");
		efi_ret = EFI_INVALID_PARAMETER;
		goto out;
	}

	if (fwu_data->trial_state) {
		all_accepted = fwu_all_images_accepted(fwu_data);
		if (!all_accepted) {
			efi_ret = EFI_ACCESS_DENIED;
			goto out;
		}

	} else {
		log_info("FWU: ExitBootServices: Booting in regular state\n");
	}

out:
	fwu_notify_exit_boot_services();

	EFI_EXIT(efi_ret);
}

/**
 * fwu_setup_accept_event() - Setup the FWU accept event
 *
 * Description: Create a FWU accept event triggered on ExitBootServices().
 *
 * Return: 0 on success. Otherwise, failure.
 */
static int fwu_setup_accept_event(void)
{
	efi_status_t efi_ret;
	struct efi_event *evt = NULL;

	efi_ret = efi_create_event(EVT_SIGNAL_EXIT_BOOT_SERVICES, TPL_CALLBACK,
				   fwu_accept_notify_exit_boot_services, NULL,
				   &efi_guid_event_group_exit_boot_services,
				   &evt);
	if (efi_ret != EFI_SUCCESS) {
		log_err("FWU: Cannot install accept event %p, err (%lu)\n", evt,
			efi_ret);
		return -EPERM;
	}

	return 0;
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

	ret = fwu_discover();
	if (ret)
		goto failure;

	if (IS_ENABLED(CONFIG_FWU_ARM_PSA_ACCEPT_IMAGES)) {
		ret = fwu_setup_accept_event();
		if (ret)
			goto failure;
	}

	g_fwu_initialized = true;

	return 0;

failure:
	fwu_shared_buf_reclaim();
	return ret;
}

/**
 * efi_fill_image_desc_array - PSA implementation for populating image descriptors
 * @image_info_size:		Size of @image_info
 * @image_info:			Image information
 * @descriptor_version:		Pointer to version number
 * @descriptor_count:		Image count
 * @descriptor_size:		Pointer to descriptor size
 * @package_version:		Package version
 * @package_version_name:	Package version's name
 *
 * Initialize the update agent in secure world if not initialized.
 * Then, read the FWU directory information including the current
 * images information. For more details refer to fwu_image_directory structure.
 *
 * Return information about the current firmware image in @image_info.
 * @image_info will consist of a number of descriptors.
 * Each descriptor will be created based on efi_fw_image array.
 *
 * Return: EFI_SUCCESS on success. Otherwise, failure
 */
efi_status_t efi_fill_image_desc_array(efi_uintn_t *image_info_size,
				       struct efi_firmware_image_descriptor *image_info,
				       u32 *descriptor_version,
				       u8 *descriptor_count,
				       efi_uintn_t *descriptor_size,
				       u32 *package_version,
				       u16 **package_version_name)
{
	int ret;
	int required_image_info_size;
	size_t image_info_desc_size = sizeof(*image_info);

	if (!g_fwu_initialized) {
		ret = fwu_agent_init();
		if (ret) {
			log_err("FWU: Update agent init failed, ret = %d\n",
				ret);
			return EFI_EXIT(EFI_DEVICE_ERROR);
		}
	}

	ret = fwu_read_directory();
	if (ret)
		return EFI_NOT_READY;

	required_image_info_size = g_fwu_cached_directory.num_images *
		image_info_desc_size;

	if (*image_info_size < required_image_info_size) {
		*image_info_size = required_image_info_size;
		return EFI_BUFFER_TOO_SMALL;
	}

	*descriptor_version = EFI_FIRMWARE_IMAGE_DESCRIPTOR_VERSION;
	*descriptor_count = g_fwu_cached_directory.num_images;
	*descriptor_size = image_info_desc_size;
	*package_version = PACKAGE_VERSION_NOT_SUP; /* Not supported */
	*package_version_name = NULL; /* Not supported */

	for (int i = 0; i < g_fwu_cached_directory.num_images; i++) {
		/* Only image indexes starting from 1 are supported */
		image_info[i].image_index = i + 1;

		/* Corresponding ESRT field: FwClass */
		guidcpy(&image_info[i].image_type_id,
			&g_fwu_cached_directory.entries[i].image_guid);

		image_info[i].image_id = image_info[i].image_index;
		image_info[i].image_id_name = NULL; /* Not supported */

		/* Corresponding ESRT field: FwVersion */
		image_info[i].version =
			g_fwu_cached_directory.entries[i].img_version;

		image_info[i].version_name = NULL; /* Not supported */
		image_info[i].size =
			g_fwu_cached_directory.entries[i].img_max_size;

		image_info[i].attributes_supported =
			IMAGE_ATTRIBUTE_IMAGE_UPDATABLE |
			IMAGE_ATTRIBUTE_AUTHENTICATION_REQUIRED;
		image_info[i].attributes_setting =
				IMAGE_ATTRIBUTE_IMAGE_UPDATABLE;

		/* Check if the capsule authentication is enabled */
		if (IS_ENABLED(CONFIG_EFI_CAPSULE_AUTHENTICATE))
			image_info[i].attributes_setting |=
				IMAGE_ATTRIBUTE_AUTHENTICATION_REQUIRED;

		/* Corresponding ESRT field: LowestSupportedFwVersion */
		image_info[i].lowest_supported_image_version =
			g_fwu_cached_directory.entries[i].lowest_acceptable_version;

		/* Corresponding ESRT field: LastAttemptVersion (not supported) */
		image_info[i].last_attempt_version = LAST_ATTEMPT_NOT_SUP;

		/* Corresponding ESRT field: LastAttemptStatus (not supported) */
		image_info[i].last_attempt_status = LAST_ATTEMPT_NOT_SUP;

		image_info[i].hardware_instance = DEFAULT_HW_INSTANCE;
		image_info[i].dependencies = NULL; /* Not supported */
	}

	return EFI_SUCCESS;
}
