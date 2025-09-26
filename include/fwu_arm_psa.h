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

#include <efi_loader.h>
#include <linux/bitfield.h>
#include <u-boot/uuid.h>

#define DEFAULT_HW_INSTANCE		(1)

/* The minimum supported ESRT version */
#define EFI_SYSTEM_RESOURCE_TABLE_FIRMWARE_RESOURCE_VERSION		(1)

/* Default values of the ESRT fields which are not supported at this stage */
#define PACKAGE_VERSION_NOT_SUP		(0xffffffff)
#define LAST_ATTEMPT_NOT_SUP		(0)

#define FWU_BUFFER_PAGES		(1024)

/* 4 MB buffer shared with secure world */
#define FWU_BUFFER_SIZE			(FWU_BUFFER_PAGES * EFI_PAGE_SIZE)

/* TS UUID string for detecting all SPs  (in big-endian format) */
#define ALL_TS_SP_UUID			"d776cdbd-5e82-5147-3b96-ac4349f8d486"
/* In little-endian equivalent to: bdcd76d7-825e-4751-963b-86d4f84943ac */

/* TS FWU service UUID string (in big-endian format) */
#define TS_FWU_SERVICE_UUID		"38a82368-061b-0e47-7497-fd53fb8bce0c"
/* In little-endian equivalent to:  6823a838-1b06-470e-9774-0cce8bfb53fd */

/* TS FWU directory UUID string (in big-endian format) */
#define FWU_DIRECTORY_CANONICAL_UUID	"d958eede-4751-d34a-90a2-a541236e6677"
/* In little-endian equivalent to:  deee58d9-5147-4ad3-a290-77666e2341a5 */

/* The entries correspond to the payloads in the storage device and the fake ESRT image */
#define FWU_DIRECTORY_IMAGE_ENTRIES_COUNT (CONFIG_FWU_NUM_IMAGES_PER_BANK + 1)

/*
 * GUIDs for dummy payloads
 *
 * The GUIDs are generated with the UUIDv5 format.
 * Namespace: 7b5c472e-5671-4fb7-a824-36a8e86f05c1
 * Names: DUMMY_START, DUMMY_END
 */
#define FWU_DUMMY_START_IMAGE_GUID \
	EFI_GUID(0x6f784cbf, 0x7938, 0x5c23, 0x8d, 0x6e, \
		0x24, 0xd2, 0xf1, 0x41, 0x0f, 0xa9)

#define FWU_DUMMY_END_IMAGE_GUID \
	EFI_GUID(0xb57e432b, 0xa250, 0x5c73, 0x93, 0xe3, \
		0x90, 0x20, 0x5e, 0x64, 0xba, 0xba)

#define TS_RPC_MEM_RETRIEVE		(0xff0001)
#define TS_RPC_SERVICE_INFO_GET		(0xff0003)
#define RPC_SUCCESS			(0)

#define SVC_IFACE_ID_GET_MASK		GENMASK(7, 0)
#define GET_SVC_IFACE_ID(x)		\
			 ((u8)(FIELD_GET(SVC_IFACE_ID_GET_MASK, (x))))

#define SVC_ID_MASK			GENMASK(15, 0)
#define SVC_IFACE_ID_SET_MASK		GENMASK(23, 16)
#define PACK_SVC_IFACE_ID(svc, iface)	(FIELD_PREP(SVC_ID_MASK, (svc)) | \
					FIELD_PREP(SVC_IFACE_ID_SET_MASK, (iface)))

#define HANDLE_MSW_MASK			GENMASK(63, 32)
#define HANDLE_LSW_MASK			GENMASK(31, 0)
#define GET_FWU_BUF_MSW(x)		\
				((u32)(FIELD_GET(HANDLE_MSW_MASK, (x))))
#define GET_FWU_BUF_LSW(x)		\
				((u32)(FIELD_GET(HANDLE_LSW_MASK, (x))))

enum fwu_abis {
	FWU_DISCOVER = 0,
	FWU_BEGIN_STAGING = 16,
	FWU_END_STAGING = 17,
	FWU_CANCEL_STAGING = 18,
	FWU_OPEN = 19,
	FWU_WRITE_STREAM = 20,
	FWU_READ_STREAM = 21,
	FWU_COMMIT = 22,
	FWU_ACCEPT_IMAGE = 23,
	/* To be updated when adding new FWU IDs */
	FWU_FIRST_ID = FWU_DISCOVER, /* Lowest number ID */
	FWU_LAST_ID = FWU_ACCEPT_IMAGE, /* Highest number ID */
};

enum fwu_abi_errcode {
	FWU_UNKNOWN = 1,
	FWU_BUSY,
	FWU_OUT_OF_BOUNDS,
	FWU_AUTH_FAIL,
	FWU_NO_PERMISSION,
	FWU_DENIED,
	FWU_RESUME,
	FWU_NOT_AVAILABLE,
	MAX_NUMBER_FWU_ERR
};

/* Enum to classify the possible type of payloads */
enum fwu_payload_type {
	FWU_PAYLOAD_TYPE_REAL = 1, /* Real payload */
	FWU_PAYLOAD_TYPE_DUMMY_START, /* The start dummy payload */
	FWU_PAYLOAD_TYPE_DUMMY_END, /* The end dummy payload */
	FWU_PAYLOAD_TYPE_INVALID, /* Invalid image_index */
};

/* Container structure and helper macros to map between an FF-A error and relevant error log */
struct fwu_abi_errmap {
	char *err_str[MAX_NUMBER_FWU_ERR];
};

#define FWU_ERRMAP_COUNT (FWU_LAST_ID - FWU_FIRST_ID + 1)
#define FWU_ID_TO_ERRMAP_ID(fwu_id) ((fwu_id) - FWU_FIRST_ID)

/**
 * struct fwu_open_args - fwu_open ABI arguments
 * @function_id: fwu_open service ID
 * @image_type_guid: GUID of the image to be opened
 * @op_type: The operation that the Client will perform on the image
 */
struct __packed fwu_open_args {
	u32 function_id;
	efi_guid_t image_type_guid;
#define FWU_OP_TYPE_READ 0
#define FWU_OP_TYPE_WRITE 1
	u8  op_type;
};

/**
 * struct fwu_open_resp - fwu_open ABI returns
 * @status: The ABI return status
 * @handle: Staging context identifier
 */
struct __packed fwu_open_resp {
	int status;
	u32  handle;
};

/**
 * struct fwu_discover_args - fwu_discover ABI arguments
 * @function_id: fwu_discover service ID
 */
struct __packed fwu_discover_args {
	u32 function_id;
};

/**
 * struct fwu_discover_resp - fwu_discover ABI returns
 * @status: The ABI return status
 * @service_status: the status of the service provider
 * @version_major: the ABI major version
 * @version_minor: the ABI minor version
 * @off_function_presence: the offset (in bytes) of the function_presence array
 *                         relative to the start of this data structure
 * @num_func: the number of entries in the function_presence array
 * @max_payload_size: the maximum number of bytes that a payload can contain
 * @flags: flags listing the update capabilities
 * @vendor_specific_flags: Vendor specific update capabilities flags
 * @function_presence: array of bytes indicating functions that are implemented
 */
struct __packed fwu_discover_resp {
	int status;
	u16 service_status;
	u8 version_major;
	u8 version_minor;
	u16 off_function_presence;
	u16 num_func;
	u64 max_payload_size;
	u32 flags;
	u32 vendor_specific_flags;
	void *function_presence;
};

/**
 * struct fwu_read_stream_args - fwu_read_stream ABI arguments
 * @function_id: fwu_read_stream service ID
 * @handle: The handle of the context being read from
 */
struct __packed fwu_read_stream_args {
	u32 function_id;
	u32  handle;
};

/**
 * struct fwu_read_stream_resp - fwu_read_stream ABI returns
 * @status: The ABI return status
 * @read_bytes: Number of bytes read by the current ABI call
 * @total_bytes: Total number of bytes that can be read
 * @payload: The read data by the current ABI call
 */
struct __packed fwu_read_stream_resp {
	int status;
	u32  read_bytes;
	u32  total_bytes;
	u8  payload[];
};

/**
 * struct fwu_begin_staging_args - fwu_begin_staging ABI arguments
 * @function_id: fwu_begin_staging service ID
 * @reserved: Reserved, must be zero
 * @vendor_flags: Vendor specific staging flags
 * @partial_update_count: The number of elements in the update_guid
 * @update_guid: An array of image type GUIDs that the update Client will update
 *               during the Staging state
 */
struct __packed fwu_begin_staging_args {
	u32 function_id;
	u32 reserved;
	u32 vendor_flags;
	u32 partial_update_count;
	efi_guid_t update_guid[];
};

/**
 * struct fwu_begin_staging_resp - fwu_begin_staging ABI returns
 * @status: The ABI return status
 */
struct __packed fwu_begin_staging_resp {
	int status;
};

/**
 * struct fwu_end_staging_args - fwu_end_staging ABI arguments
 * @function_id: fwu_end_staging service ID state
 */
struct __packed fwu_end_staging_args {
	u32 function_id;
};

/**
 * struct fwu_end_staging_resp - fwu_end_staging ABI returns
 * @status: The ABI return status
 */
struct __packed fwu_end_staging_resp {
	int status;
};

/**
 * struct fwu_cancel_staging_args - fwu_cancel_staging ABI arguments
 * @function_id: fwu_cancel_staging service ID state
 */
struct __packed fwu_cancel_staging_args {
	u32 function_id;
};

/**
 * struct fwu_cancel_staging_resp - fwu_cancel_staging ABI returns
 * @status: The ABI return status
 */
struct __packed fwu_cancel_staging_resp {
	int status;
};

/**
 * struct fwu_commit_args - fwu_commit ABI arguments
 * @function_id: fwu_commit service ID
 * @handle: The handle of the context being closed
 * @acceptance_req: If positive, the Client requests the image to be marked as
 *                  unaccepted
 * @max_atomic_len: Hint, maximum time (in ns) that the Update Agent can execute
 *                  continuously without yielding back to the Client state
 */
struct __packed fwu_commit_args {
	u32 function_id;
	u32 handle;
#define FWU_IMG_ACCEPTED 0
#define FWU_IMG_NOT_ACCEPTED 1
	u32 acceptance_req;
	u32 max_atomic_len;
};

/**
 * struct fwu_commit_resp - fwu_commit ABI returns
 * @status: The ABI return status
 * @progress: Units of work already completed by the Update Agent
 * @total_work: Units of work the Update Agent must perform until fwu_commit
 *              returns successfully
 */
struct __packed fwu_commit_resp {
	int status;
	u32 progress;
	u32 total_work;
};

/**
 * struct fwu_write_stream_args - fwu_write_stream ABI arguments
 * @function_id: fwu_write_stream service ID
 * @handle: The handle of the context being written to
 * @data_len: Size of the data present in the payload
 * @payload: The data to be transferred
 */
struct __packed fwu_write_stream_args {
	u32 function_id;
	u32  handle;
	u32 data_len;
	u8 payload[];
};

/**
 * struct fwu_write_stream_resp - fwu_write_stream ABI returns
 * @status: The ABI return status
 */
struct __packed fwu_write_stream_resp {
	int status;
};

/**
 * struct fwu_accept_image_args - fwu_accept_image ABI arguments
 * @function_id: fwu_accept_image service ID
 * @image_type_guid: GUID of the image to be accepted
 */
struct __packed fwu_accept_image_args {
	u32 function_id;
	u32 reserved;
	efi_guid_t image_type_guid;
};

/**
 * struct fwu_accept_image_resp - fwu_accept_image ABI returns
 * @status: The ABI return status
 */
struct __packed fwu_accept_image_resp {
	int status;
};

/*
 * FWU directory information structures
 */

struct __packed fwu_image_info_entry {
	efi_guid_t image_guid;
	u32 client_permissions;
	u32 img_max_size;
	u32 lowest_acceptable_version;
	u32 img_version;
	u32 accepted;
	u32 reserved;
};

struct __packed fwu_image_directory {
	u32 directory_version;
	u32 img_info_offset;
	u32 num_images;
	u32 correct_boot;
	u32 img_info_size;
	u32 reserved;
	struct fwu_image_info_entry entries[FWU_DIRECTORY_IMAGE_ENTRIES_COUNT];
};

/**
 * struct fwu_esrt_data_wrapper - Wrapper for the ESRT data
 * @data: The ESRT data read from secure world
 * @entries: The ESRT entries
 */
struct __packed fwu_esrt_data_wrapper {
	struct efi_system_resource_table data;
	struct efi_system_resource_entry entries[CONFIG_FWU_NUM_IMAGES_PER_BANK];
};

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

/**
 * fwu_update_image() - Update an image
 * @image: Pointer to the payload
 * @image_index: Payload index
 * @image_size: Payload size in bytes
 *
 * Perform staging with multiple payloads support.
 *
 * Return: 0 on success
 */
int fwu_update_image(const void *image, u8 image_index, u32 image_size);

/**
 * fwu_get_payload_type() - Identifies the payload type
 * @image_index:	The payload index
 *
 * Identifies the FWU payload type based on the image index.
 *
 * Return: See @fwu_payload_type for details
 */
enum fwu_payload_type fwu_get_payload_type(u32 image_index);

#endif
