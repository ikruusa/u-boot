// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Tom Cubie <tangliang@allwinnertech.com>
 * (C) Copyright 2012-2013 Henrik Nordstrom <henrik@henriknordstrom.net>
 * (C) Copyright 2013 Luke Kenneth Casson Leighton <lkcl@lkcl.net>
 *
 * SPL board initialization for Allwinner D1(s) RISC-V SoC.
 */

#include <fdt_support.h>
#include <image.h>
#include <ram.h>
#include <spl.h>
#include <sunxi_image.h>
#include <asm/csr.h>
#include <asm/arch/thead_csr.h>
#include <cpu.h>
#include <dm.h>
#include <init.h>

extern const char *get_spl_dt_name(void);

int spl_board_init_f(void)
{
	int ret;
	struct udevice *dev;

	ret = cpu_probe_all();
	if (ret) {
		debug("CPU init failed: %d\n", ret);
	}

	/* DDR init */
	ret = uclass_get_device(UCLASS_RAM, 0, &dev);
	if (ret) {
		debug("DRAM init failed: %d\n", ret);
		return ret;
	}

	printf("mxstatus=0x%08lx mhcr=0x%08lx mcor=0x%08lx mhint=0x%08lx\n",
	       csr_read(CSR_MXSTATUS),
	       csr_read(CSR_MHCR),
	       csr_read(CSR_MCOR),
	       csr_read(CSR_MHINT));

	csr_set(CSR_MXSTATUS, 0x638000);
	csr_write(CSR_MCOR, 0x70013);
	csr_write(CSR_MHCR, 0x11ff);
	csr_write(CSR_MHINT, 0x16e30c);

	return 0;
}

void spl_perform_board_fixups(struct spl_image_info *spl_image)
{
	struct ram_info info;
	struct udevice *dev;
	int ret;

	ret = uclass_get_device(UCLASS_RAM, 0, &dev);
	if (ret)
		panic("No RAM device");

	ret = ram_get_info(dev, &info);
	if (ret)
		panic("No RAM info");

	ret = fdt_fixup_memory(spl_image->fdt_addr, info.base, info.size);
	if (ret)
		panic("Failed to update DTB");
}

#ifdef CONFIG_SPL_LOAD_FIT
static void set_spl_dt_name(const char *name)
{
	struct boot_file_head *spl = (void *)(ulong)CONFIG_SUNXI_SRAM_ADDRESS;

	/* Promote the header version for U-Boot proper, if needed. */
	if (spl->spl_signature[3] < SPL_DT_HEADER_VERSION)
		spl->spl_signature[3] = SPL_DT_HEADER_VERSION;

	strcpy((char *)&spl->string_pool, name);
	spl->dt_name_offset = offsetof(struct boot_file_head, string_pool);
}

int board_fit_config_name_match(const char *name)
{
	const char *best_dt_name = get_spl_dt_name();
	int ret;

#ifdef CONFIG_DEFAULT_DEVICE_TREE
	if (best_dt_name == NULL)
		best_dt_name = CONFIG_DEFAULT_DEVICE_TREE;
#endif

	if (best_dt_name == NULL) {
		/* No DT name was provided, so accept the first config. */
		return 0;
	}

	ret = strcmp(name, best_dt_name);

	/*
	 * If one of the FIT configurations matches the most accurate DT name,
	 * update the SPL header to provide that DT name to U-Boot proper.
	 */
	if (ret == 0)
		set_spl_dt_name(best_dt_name);

	return ret;
}
#endif
