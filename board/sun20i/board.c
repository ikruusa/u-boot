// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2012-2013 Henrik Nordstrom <henrik@henriknordstrom.net>
 * (C) Copyright 2013 Luke Kenneth Casson Leighton <lkcl@lkcl.net>
 *
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Tom Cubie <tangliang@allwinnertech.com>
 *
 * Board initialization, environment and boot source detection for
 * Allwinner D1(s) RISC-V SoC.
 */

#include <asm/csr.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/arch/thead_csr.h>
#include <cpu.h>
#include <dm.h>
#include <env.h>
#include <env_internal.h>
#include <init.h>
#include <spl.h>
#include <sunxi_image.h>

int board_init(void)
{
	/* https://lore.kernel.org/u-boot/31587574-4cd1-02da-9761-0134ac82b94b@sholland.org/ */
	return cpu_probe_all();
}

#define SPL_ADDR		CONFIG_SUNXI_SRAM_ADDRESS

/* The low 8-bits of the 'boot_media' field in the SPL header */
#define SUNXI_BOOTED_FROM_MMC0	0
#define SUNXI_BOOTED_FROM_MMC2	2
#define SUNXI_BOOTED_FROM_SPI	3
#define SUNXI_BOOTED_FROM_MMC0_HIGH	0x10
#define SUNXI_BOOTED_FROM_MMC2_HIGH	0x12

#define SUNXI_INVALID_BOOT_SOURCE	-1

static int sunxi_egon_valid(struct boot_file_head *egon_head)
{
	return !memcmp(egon_head->magic, BOOT0_MAGIC, 8); /* eGON.BT0 */
}

static int sunxi_toc0_valid(struct toc0_main_info *toc0_info)
{
	return !memcmp(toc0_info->name, TOC0_MAIN_INFO_NAME, 8); /* TOC0.GLH */
}

static int sunxi_get_boot_source(void)
{
	struct boot_file_head *egon_head = (void *)SPL_ADDR;
	struct toc0_main_info *toc0_info = (void *)SPL_ADDR;

	if (sunxi_egon_valid(egon_head))
		return readb(&egon_head->boot_media);
	if (sunxi_toc0_valid(toc0_info))
		return readb(&toc0_info->platform[0]);

	/* Not a valid image, so we must have been booted via FEL. */
	return SUNXI_INVALID_BOOT_SOURCE;
}

/* The sunxi internal BROM will try to load an external bootloader
 * from mmc0, mmc2, or SPI flash.
 */
uint32_t sunxi_get_boot_device(void)
{
	int boot_source = sunxi_get_boot_source();

	/*
	 * When booting from the SD card or NAND memory, the "eGON.BT0"
	 * signature is expected to be found in memory at the address 0x0004
	 * (see the "mksunxiboot" tool, which generates this header).
	 *
	 * When booting in the FEL mode over USB, this signature is patched in
	 * memory and replaced with something else by the 'fel' tool. This other
	 * signature is selected in such a way, that it can't be present in a
	 * valid bootable SD card image (because the BROM would refuse to
	 * execute the SPL in this case).
	 *
	 * This checks for the signature and if it is not found returns to
	 * the FEL code in the BROM to wait and receive the main u-boot
	 * binary over USB. If it is found, it determines where SPL was
	 * read from.
	 */
	switch (boot_source) {
	case SUNXI_INVALID_BOOT_SOURCE:
		return BOOT_DEVICE_BOARD;
	case SUNXI_BOOTED_FROM_MMC0:
	case SUNXI_BOOTED_FROM_MMC0_HIGH:
		return BOOT_DEVICE_MMC1;
	case SUNXI_BOOTED_FROM_MMC2:
	case SUNXI_BOOTED_FROM_MMC2_HIGH:
		return BOOT_DEVICE_MMC2;
	case SUNXI_BOOTED_FROM_SPI:
		return BOOT_DEVICE_SPI;
	}

	panic("Unknown boot source %d\n", boot_source);
	return -1;		/* Never reached */
}

uint32_t sunxi_get_spl_size(void)
{
	struct boot_file_head *egon_head = (void *)SPL_ADDR;
	struct toc0_main_info *toc0_info = (void *)SPL_ADDR;

	if (sunxi_egon_valid(egon_head))
		return readl(&egon_head->length);
	if (sunxi_toc0_valid(toc0_info))
		return readl(&toc0_info->length);

	/* Not a valid image, so use the default U-Boot offset. */
	return 0;
}

/*
 * The eGON SPL image can be located at 8KB or at 128KB into an SD card or
 * an eMMC device. The boot source has bit 4 set in the latter case.
 * By adding 120KB to the normal offset when booting from a "high" location
 * we can support both cases.
 * Also U-Boot proper is located at least 32KB after the SPL, but will
 * immediately follow the SPL if that is bigger than that.
 */
unsigned long spl_mmc_get_uboot_raw_sector(struct mmc *mmc,
					   unsigned long raw_sect)
{
	unsigned long spl_size = sunxi_get_spl_size();
	unsigned long sector;

	sector = max(raw_sect, spl_size / 512);

	switch (sunxi_get_boot_source()) {
	case SUNXI_BOOTED_FROM_MMC0_HIGH:
	case SUNXI_BOOTED_FROM_MMC2_HIGH:
		sector += (128 - 8) * 2;
		break;
	}

	printf("SPL size = %lu, sector = %lu\n", spl_size, sector);

	return sector;
}

u32 spl_boot_device(void)
{
	return sunxi_get_boot_device();
}

DECLARE_GLOBAL_DATA_PTR;

/*
 * Try to use the environment from the boot source first.
 * For MMC, this means a FAT partition on the boot device (SD or eMMC).
 * If the raw MMC environment is also enabled, this is tried next.
 * SPI flash falls back to FAT (on SD card).
 */
enum env_location env_get_location(enum env_operation op, int prio)
{
	if (prio > 1)
		return ENVL_UNKNOWN;

	/* NOWHERE is exclusive, no other option can be defined. */
	if (IS_ENABLED(CONFIG_ENV_IS_NOWHERE))
		return ENVL_NOWHERE;

	switch (sunxi_get_boot_device()) {
	case BOOT_DEVICE_MMC1:
	case BOOT_DEVICE_MMC2:
		if (prio == 0 && IS_ENABLED(CONFIG_ENV_IS_IN_FAT))
			return ENVL_FAT;
		if (IS_ENABLED(CONFIG_ENV_IS_IN_MMC))
			return ENVL_MMC;
		break;
	case BOOT_DEVICE_SPI:
		if (prio == 0 && IS_ENABLED(CONFIG_ENV_IS_IN_SPI_FLASH))
			return ENVL_SPI_FLASH;
		if (IS_ENABLED(CONFIG_ENV_IS_IN_FAT))
			return ENVL_FAT;
		break;
	case BOOT_DEVICE_BOARD:
		break;
	default:
		break;
	}

	/*
	 * If we come here for the first time, we *must* return a valid
	 * environment location other than ENVL_UNKNOWN, or the setup sequence
	 * in board_f() will silently hang. This is arguably a bug in
	 * env_init(), but for now pick one environment for which we know for
	 * sure to have a driver for. For all defconfigs this is either FAT
	 * or UBI, or NOWHERE, which is already handled above.
	 */
	if (prio == 0) {
		if (IS_ENABLED(CONFIG_ENV_IS_IN_FAT))
			return ENVL_FAT;
		if (IS_ENABLED(CONFIG_ENV_IS_IN_UBI))
			return ENVL_UBI;
	}

	return ENVL_UNKNOWN;
}

static struct boot_file_head *get_spl_header(uint8_t req_version)
{
	struct boot_file_head *spl = (void *)(ulong)SPL_ADDR;
	uint8_t spl_header_version = spl->spl_signature[3];

	/* Is there really the SPL header (still) there? */
	if (memcmp(spl->spl_signature, SPL_SIGNATURE, 3) != 0)
		return NULL;

	if (spl_header_version < req_version) {
		printf("sunxi SPL version mismatch: expected %u, got %u\n",
		       req_version, spl_header_version);
		return NULL;
	}

	return spl;
}

const char *get_spl_dt_name(void)
{
	struct boot_file_head *spl = get_spl_header(SPL_DT_HEADER_VERSION);

	/* Check if there is a DT name stored in the SPL header. */
	if (spl && spl->dt_name_offset)
		return (char *)spl + spl->dt_name_offset;

	return NULL;
}

/*
 * Check the SPL header for the "sunxi" variant. If found: parse values
 * that might have been passed by the loader ("fel" utility), and update
 * the environment accordingly.
 */
static void parse_spl_header(const uint32_t spl_addr)
{
	struct boot_file_head *spl = get_spl_header(SPL_ENV_HEADER_VERSION);

	if (!spl)
		return;

	if (!spl->fel_script_address)
		return;

	if (spl->fel_uEnv_length != 0) {
		/*
		 * data is expected in uEnv.txt compatible format, so "env
		 * import -t" the string(s) at fel_script_address right away.
		 */
		himport_r(&env_htab, (char *)(uintptr_t)spl->fel_script_address,
			  spl->fel_uEnv_length, '\n', H_NOCLEAR, 0, 0, NULL);
		return;
	}
	/* otherwise assume .scr format (mkimage-type script) */
	env_set_hex("fel_scriptaddr", spl->fel_script_address);
}

int misc_init_r(void)
{
	const char *spl_dt_name;
	uint boot;

	env_set("fel_booted", NULL);
	env_set("fel_scriptaddr", NULL);
	env_set("mmc_bootdev", NULL);

	boot = sunxi_get_boot_device();
	/* determine if we are running in FEL mode */
	if (boot == BOOT_DEVICE_BOARD) {
		env_set("fel_booted", "1");
		parse_spl_header(SPL_ADDR);
	} else if (boot == BOOT_DEVICE_MMC1)
		env_set("mmc_bootdev", "0");


	/* Set fdtfile to match the FIT configuration chosen in SPL. */
	spl_dt_name = get_spl_dt_name();
	if (spl_dt_name) {
		char str[64];
		snprintf(str, sizeof(str), "%s.dtb", spl_dt_name);
		env_set("fdtfile", str);
	}

	return 0;
}


int ft_board_setup(void *blob, struct bd_info *bd)
{
	return 0;
}
