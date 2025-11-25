// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2025 ARM Limited
 */

#include <generated/dt.h>
#include <efi_loader.h>
#include <fwu_arm_psa.h>

/*
 * GUIDs for capsule updatable firmware images
 *
 * The GUIDs are generating with the UUIDv5 format.
 * Namespace used for FVP GUIDs: f7dfa2d4-8970-4aa8-9373-12d7f6f920b9
 * Names: the image names stated in the fw_name field
 */

#define RDASPEN_TFM_BL2_IMAGE_GUID \
	EFI_GUID(0x4b312051, 0x850a, 0x5b17, 0xa3, 0xcf, \
		 0x29, 0x95, 0xba, 0xa4, 0xbe, 0xd4)

#define RDASPEN_TFM_RUNTIME_IMAGE_GUID \
	EFI_GUID(0xb181e748, 0xc362, 0x55e6, 0x85, 0x2c, \
		 0x66, 0x2d, 0x15, 0x44, 0xf4, 0x14)

#define RDASPEN_SCP_FIRMWARE_IMAGE_GUID \
	EFI_GUID(0x771ceff3, 0xf186, 0x5d56, 0x80, 0xcb, \
		 0x15, 0xa2, 0xa0, 0x6d, 0xfe, 0x81)

#define RDASPEN_AP_FIP_IMAGE_GUID \
	EFI_GUID(0x5d904717, 0x0904, 0x53cd, 0xb2, 0x40, \
		 0xdf, 0x7c, 0x91, 0xef, 0x49, 0x18)

#ifdef RD_ASPEN_VARIANT_CFG2
#define RDASPEN_SI_CL1_IMAGE_GUID \
	EFI_GUID(0x46083fc9, 0x3d43, 0x5766, 0xa5, 0x83, \
		 0xae, 0x8e, 0x0a, 0x19, 0x9a, 0x85)
#endif

enum fw_image_index {
	FW_IMAGE_INDEX_BL2 = 1,
	FW_IMAGE_INDEX_TFM_S,
	FW_IMAGE_INDEX_SCP,
	FW_IMAGE_INDEX_AP_FIP,
#ifdef RD_ASPEN_VARIANT_CFG2
	FW_IMAGE_INDEX_SI_CL1,
#endif
	FW_IMAGE_INDEX_DUMMY_START,
	FW_IMAGE_INDEX_DUMMY_END
};

struct efi_fw_image rdaspen_fw_images[] = {
	{
		.image_type_id = RDASPEN_TFM_BL2_IMAGE_GUID,
		.fw_name = u"TFM_BL2",
		.image_index = FW_IMAGE_INDEX_BL2,
	},
	{
		.image_type_id = RDASPEN_TFM_RUNTIME_IMAGE_GUID,
		.fw_name = u"TFM_S",
		.image_index = FW_IMAGE_INDEX_TFM_S,
	},
	{
		.image_type_id = RDASPEN_SCP_FIRMWARE_IMAGE_GUID,
		.fw_name = u"SCP",
		.image_index = FW_IMAGE_INDEX_SCP,
	},
	{
		.image_type_id = RDASPEN_AP_FIP_IMAGE_GUID,
		.fw_name = u"TFA_FIP",
		.image_index = FW_IMAGE_INDEX_AP_FIP,
	},
#ifdef RD_ASPEN_VARIANT_CFG2
	{
		.image_type_id = RDASPEN_SI_CL1_IMAGE_GUID,
		.fw_name = u"SI_CL1",
		.image_index = FW_IMAGE_INDEX_SI_CL1,
	},
#endif
	{
		.image_type_id = FWU_DUMMY_START_IMAGE_GUID,
		.fw_name = u"DUMMY_START",
		.image_index = FW_IMAGE_INDEX_DUMMY_START,
	},
	{
		.image_type_id = FWU_DUMMY_END_IMAGE_GUID,
		.fw_name = u"DUMMY_END",
		.image_index = FW_IMAGE_INDEX_DUMMY_END,
	},
};

struct efi_capsule_update_info update_info = {
	.dfu_string = NULL,
	.num_images = ARRAY_SIZE(rdaspen_fw_images),
	.images = rdaspen_fw_images
};
