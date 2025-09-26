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
	FWU_OPEN = 19,
	FWU_READ_STREAM = 21,
	FWU_COMMIT = 22,
	/* To be updated when adding new FWU IDs */
	FWU_FIRST_ID = FWU_DISCOVER, /* Lowest number ID */
	FWU_LAST_ID = FWU_COMMIT, /* Highest number ID */
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
