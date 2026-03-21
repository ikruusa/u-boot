// SPDX-License-Identifier: GPL-2.0+

#include <asm/io.h>
#include <cpu_func.h>
#include <dm.h>
#include <linux/sizes.h>
#include <log.h>
#include <init.h>

DECLARE_GLOBAL_DATA_PTR;

#define CSR_MXSTATUS		0x7c0
#define CSR_MHCR		0x7c1
#define CSR_MHINT		0x7c5

#define MXSTATUS_THEADISAEE	BIT(22) /* T-HEAD ISA extensions enable */
#define MXSTATUS_MM		BIT(15) /* misaligned access enable */

#define MHCR_IE			BIT(0)	/* icache enable */
#define MHCR_DE			BIT(1)	/* dcache enable */
#define MHCR_WA			BIT(2)	/* dcache write allocate */
#define MHCR_WB			BIT(3)	/* dcache write back */
#define MHCR_RS			BIT(4)	/* return stack enable */
#define MHCR_BPE		BIT(5)	/* branch prediction enable */
#define MHCR_BTB_C906		BIT(6)	/* branch target prediction enable */
#define MHCR_WBR		BIT(8)	/* write burst enable */
#define MHCR_BTB_E906		BIT(12) /* branch target prediction enable */

#define MHINT_DPLD		BIT(2)	/* dcache prefetch enable */
#define MHINT_AMR_PAGE		(0x0 << 3)
#define MHINT_AMR_LIMIT_3	(0x1 << 3)
#define MHINT_AMR_LIMIT_64	(0x2 << 3)
#define MHINT_AMR_LIMIT_128	(0x3 << 3)
#define MHINT_IPLD		BIT(8)	/* icache prefetch enable */
#define MHINT_IWPE		BIT(9)	/* icache way prediction enable */
#define MHINT_D_DIS_PREFETCH_2	(0x0 << 13)
#define MHINT_D_DIS_PREFETCH_4	(0x1 << 13)
#define MHINT_D_DIS_PREFETCH_8	(0x2 << 13)
#define MHINT_D_DIS_PREFETCH_16	(0x3 << 13)
#define MHINT_AEE		BIT(20) /* accurate exception enable */

int spl_dram_init(void)
{
	int ret;
	struct udevice *dev;

	ret = fdtdec_setup_mem_size_base();
	if (ret) {
		printf("failed to setup memory size and base: %d\n", ret);
		return ret;
	}

	/* DDR init */
	ret = uclass_get_device(UCLASS_RAM, 0, &dev);
	if (ret) {
		printf("DRAM init failed: %d\n", ret);
		return ret;
	}

	return 0;
}

void harts_early_init(void)
{
	if (!CONFIG_IS_ENABLED(RISCV_MMODE))
		return;

	csr_set(CSR_MXSTATUS, MXSTATUS_THEADISAEE | MXSTATUS_MM);
	csr_set(CSR_MHCR,
		MHCR_BTB_C906 | MHCR_BPE | MHCR_RS | MHCR_WB | MHCR_WA);
	csr_set(CSR_MHINT, MHINT_IWPE | MHINT_IPLD | MHINT_IPLD);
}
