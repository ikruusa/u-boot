// SPDX-License-Identifier: GPL-2.0+

#include <asm/io.h>
#include <cpu_func.h>
#include <dm.h>
#include <linux/sizes.h>
#include <log.h>
#include <init.h>

DECLARE_GLOBAL_DATA_PTR;

#define CSR_MXSTATUS			0x7c0
#define  CSR_MXSTATUS_THEADISAEE	BIT(22)
#define  CSR_MXSTATUS_MAEE		BIT(21)
#define  CSR_MXSTATUS_CLINTEE		BIT(17)
#define  CSR_MXSTATUS_UCME		BIT(16)
#define  CSR_MXSTATUS_MM		BIT(15)
#define CSR_MHCR			0x7c1
#define  CSR_MHCR_WBR			BIT(8)
#define  CSR_MHCR_BTB			BIT(6)
#define  CSR_MHCR_BPE			BIT(5)
#define  CSR_MHCR_RS			BIT(4)
#define  CSR_MHCR_WB			BIT(3)
#define  CSR_MHCR_WA			BIT(2)
#define  CSR_MHCR_DE			BIT(1)
#define  CSR_MHCR_IE			BIT(0)
#define CSR_MCOR			0x7c2
#define  CSR_MCOR_IBP_INV		BIT(18)
#define  CSR_MCOR_BTB_INV		BIT(17)
#define  CSR_MCOR_BHT_INV		BIT(16)
#define  CSR_MCOR_CACHE_INV		BIT(4)
#define CSR_MCCR2			0x7c3
#define  CSR_MCCR2_TPRF			BIT(31)
#define  CSR_MCCR2_IPRF(n)		((n) << 29)
#define  CSR_MCCR2_TSETUP		BIT(25)
#define  CSR_MCCR2_TLNTCY(n)		((n) << 22)
#define  CSR_MCCR2_DSETUP		BIT(19)
#define  CSR_MCCR2_DLNTCY(n)		((n) << 16)
#define  CSR_MCCR2_L2EN			BIT(3)
#define  CSR_MCCR2_RFE			BIT(0)
#define CSR_MHINT			0x7c5
#define  CSR_MHINT_FENCERW_BROAD_DIS	BIT(22)
#define  CSR_MHINT_TLB_BRAOD_DIS	BIT(21)
#define  CSR_MHINT_NSFE			BIT(18)
#define  CSR_MHINT_L2_PREF_DIST(n)	((n) << 16)
#define  CSR_MHINT_L2PLD		BIT(15)
#define  CSR_MHINT_DCACHE_PREF_DIST(n)	((n) << 13)
#define  CSR_MHINT_LPE			BIT(9)
#define  CSR_MHINT_ICACHE_PREF		BIT(8)
#define  CSR_MHINT_AMR			BIT(3)
#define  CSR_MHINT_DCACHE_PREF		BIT(2)
#define CSR_MHINT2			0x7cc
#define  CSR_MHINT2_LOCAL_ICG_EN(n)	BIT((n) + 14)
#define CSR_MHINT4			0x7ce
#define CSR_MSMPR			0x7f3
#define  CSR_MSMPR_SMPEN		BIT(0)

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
