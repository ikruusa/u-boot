// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Tom Cubie <tangliang@allwinnertech.com>
 * (C) Copyright 2012-2013 Henrik Nordstrom <henrik@henriknordstrom.net>
 * (C) Copyright 2013 Luke Kenneth Casson Leighton <lkcl@lkcl.net>
 *
 * (C) Copyright 2026 Indrek Kruusa <indrek.kruusa@gmail.com>
 *
 * SPL board initialization for Allwinner D1(s) RISC-V SoC.
 */

#include <fdt_support.h>
#include <image.h>
#include <ram.h>
#include <spl.h>
#include <sunxi_image.h>
#include <asm/cache.h>
#include <asm/csr.h>
#include <asm/arch/thead_csr.h>
#include <cpu.h>
#include <cpu_func.h>
#include <dm.h>
#include <init.h>

extern const char *get_spl_dt_name(void);

int spl_board_init_f(void)
{
	int ret;
	struct udevice *dev;

	ret = cpu_probe_all();
	if (ret)
		debug("CPU init failed: %d\n", ret);

	/* DDR init */
	ret = uclass_get_device(UCLASS_RAM, 0, &dev);
	if (ret) {
		debug("DRAM init failed: %d\n", ret);
		return ret;
	}

	/*
	 * The C906 leaves reset with both caches disabled (MHCR.IE and
	 * MHCR.DE are 0) and nothing else in the boot chain turns them on.
	 * Enable them here, i.e. only once DRAM is usable: with the D-cache
	 * on, the DRAM controller's training tests observe their own cached
	 * writes and fail.
	 *
	 * The SYS_{I,D}CACHE_OFF options still win: the T-Head cache
	 * maintenance library (SYS_CACHE_THEAD_CMO) is only built when the
	 * D-cache may be on, so enabling the D-cache here regardless would
	 * leave such a build without working range flushes.
	 */
	if (!CONFIG_IS_ENABLED(SYS_ICACHE_OFF))
		icache_enable();
	if (!CONFIG_IS_ENABLED(SYS_DCACHE_OFF))
		dcache_enable();

	/*
	 * MXSTATUS.MAEE makes the MMU take the memory type from PTE[63:59]
	 * instead of ignoring those bits.  With it clear every mapping is
	 * cached, so the OS cannot mark its DMA memory non-cacheable, and this
	 * SoC has no Zicbom to fall back on: drivers then read stale
	 * descriptors (OHCI reports "frame counter not updating").  The C906
	 * does not leave the bit set, so set it once DRAM is up, as the vendor
	 * SPL does.
	 */
	csr_set(CSR_MXSTATUS, MXSTATUS_MAEE);

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

	/*
	 * The D-cache is enabled by now, so the image we just loaded and the
	 * DTB we just patched may still be dirty in it. The next stage is
	 * fetched through the I-cache, which does not see dirty D-cache
	 * lines, so push everything out before handing over.
	 */
	flush_dcache_all();
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
	if (!best_dt_name)
		best_dt_name = CONFIG_DEFAULT_DEVICE_TREE;
#endif

	if (!best_dt_name) {
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
