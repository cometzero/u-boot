// SPDX-License-Identifier: GPL-2.0+
#include <blk.h>
#include <command.h>
#include <env.h>
#include <malloc.h>
#include <part.h>
#include <u-boot/crc.h>
#include <asm/cache.h>
#include <linux/errno.h>
#include <linux/types.h>

#define AANX_DEV_IFACE		"virtio"
#define AANX_DEV_NUM		0
#define AANX_MISC_PART		"misc"
#define AANX_BOOT_A_PART	"boot_a"
#define AANX_BOOT_B_PART	"boot_b"
#define AANX_MAGIC		"AANXBOOT"
#define AANX_MAGIC_LEN		8
#define AANX_VERSION		1
#define AANX_HEADER_SIZE	64
#define AANX_CRC_OFFSET		0x18
#define AANX_SLOT_A		0
#define AANX_SLOT_B		1

struct aanx_misc_state {
	u8 magic[AANX_MAGIC_LEN];
	__le32 version;
	__le32 header_size;
	u8 active_slot;
	u8 attempts;
	__le16 flags;
	__le32 generation;
	__le32 crc32;
} __packed;

struct aanx_slot_info {
	int value;
	const char *upper;
	const char *lower;
	const char *part_name;
	const char *uki;
};

static const struct aanx_slot_info aanx_slots[] = {
	{
		.value = AANX_SLOT_A,
		.upper = "A",
		.lower = "a",
		.part_name = AANX_BOOT_A_PART,
		.uki = "EFI/Linux/auto-ad-nexios-a.efi",
	},
	{
		.value = AANX_SLOT_B,
		.upper = "B",
		.lower = "b",
		.part_name = AANX_BOOT_B_PART,
		.uki = "EFI/Linux/auto-ad-nexios-b.efi",
	},
};

static const struct aanx_slot_info *aanx_slot_by_value(int slot)
{
	if (slot == AANX_SLOT_B)
		return &aanx_slots[1];

	return &aanx_slots[0];
}

static int aanx_set_env(const char *name, const char *value)
{
	int ret = env_set(name, value);

	if (ret)
		printf("auto-ad-nexios: failed to set %s\n", name);

	return ret;
}

static int aanx_set_part_env(const char *name, int part)
{
	char value[12];

	snprintf(value, sizeof(value), "%d", part);
	return aanx_set_env(name, value);
}

static int aanx_validate_misc(const u8 *buf)
{
	const struct aanx_misc_state *state =
		(const struct aanx_misc_state *)buf;
	u32 expected_crc;
	u32 stored_crc;

	if (memcmp(state->magic, AANX_MAGIC, AANX_MAGIC_LEN))
		return -EINVAL;

	if (le32_to_cpu(state->version) != AANX_VERSION)
		return -EINVAL;

	if (le32_to_cpu(state->header_size) != AANX_HEADER_SIZE)
		return -EINVAL;

	if (state->active_slot > AANX_SLOT_B)
		return -EINVAL;

	stored_crc = le32_to_cpu(state->crc32);
	expected_crc = crc32(0, buf, AANX_CRC_OFFSET);
	if (stored_crc != expected_crc)
		return -EINVAL;

	return state->active_slot;
}

static int aanx_read_misc_slot(struct blk_desc *desc)
{
	struct disk_partition misc;
	lbaint_t blkcnt;
	void *buf;
	int ret;

	ret = part_get_info_by_name(desc, AANX_MISC_PART, &misc);
	if (ret < 0)
		return ret;

	blkcnt = (AANX_HEADER_SIZE + desc->blksz - 1) / desc->blksz;
	buf = memalign(ARCH_DMA_MINALIGN, blkcnt * desc->blksz);
	if (!buf)
		return -ENOMEM;

	if (blk_dread(desc, misc.start, blkcnt, buf) != blkcnt) {
		free(buf);
		return -EIO;
	}

	ret = aanx_validate_misc(buf);
	free(buf);

	return ret;
}

static int aanx_get_partnum(struct blk_desc *desc, const char *name)
{
	struct disk_partition part;

	return part_get_info_by_name(desc, name, &part);
}

static int do_aanxbootselect(struct cmd_tbl *cmdtp, int flag, int argc,
			     char *const argv[])
{
	const struct aanx_slot_info *fallback;
	const struct aanx_slot_info *selected;
	struct blk_desc *desc;
	int selected_part;
	int fallback_part;
	int slot;

	if (argc != 1)
		return CMD_RET_USAGE;

	desc = blk_get_dev(AANX_DEV_IFACE, AANX_DEV_NUM);
	if (!desc)
		return CMD_RET_FAILURE;

	slot = aanx_read_misc_slot(desc);
	if (slot < 0) {
		printf("auto-ad-nexios: invalid misc, defaulting slot A\n");
		slot = AANX_SLOT_A;
	}

	selected = aanx_slot_by_value(slot);
	fallback = aanx_slot_by_value(slot == AANX_SLOT_A ? AANX_SLOT_B :
				      AANX_SLOT_A);

	selected_part = aanx_get_partnum(desc, selected->part_name);
	fallback_part = aanx_get_partnum(desc, fallback->part_name);
	if (selected_part < 0 || fallback_part < 0)
		return CMD_RET_FAILURE;

	if (aanx_set_env("aanx_slot", selected->upper) ||
	    aanx_set_env("aanx_slot_lower", selected->lower) ||
	    aanx_set_env("aanx_uki", selected->uki) ||
	    aanx_set_part_env("aanx_boot_part", selected_part) ||
	    aanx_set_env("aanx_fallback_slot", fallback->upper) ||
	    aanx_set_env("aanx_fallback_slot_lower", fallback->lower) ||
	    aanx_set_env("aanx_fallback_uki", fallback->uki) ||
	    aanx_set_part_env("aanx_fallback_boot_part", fallback_part))
		return CMD_RET_FAILURE;

	printf("auto-ad-nexios: selected slot %s\n", selected->upper);
	printf("auto-ad-nexios: selected UKI %s\n", selected->uki);

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
	aanxbootselect, 1, 0, do_aanxbootselect,
	"select the auto-ad-nexios boot slot",
	""
);
