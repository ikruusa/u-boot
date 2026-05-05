/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2024 Allwinner D1 RISC-V clock definitions
 */

#ifndef _ASM_RISCV_ARCH_CLOCK_H
#define _ASM_RISCV_ARCH_CLOCK_H

#include <linux/bitops.h>

/* MMC clock bit field (common with H6 generation) */
#define CCM_MMC_CTRL_M(x)		((x) - 1)
#define CCM_MMC_CTRL_N(x)		((x) << 8)
#define CCM_MMC_CTRL_OSCM24		(0x0 << 24)
#define CCM_MMC_CTRL_PLL6		(0x1 << 24)
#define CCM_MMC_CTRL_PLL_PERIPH2X2	(0x2 << 24)
#define CCM_MMC_CTRL_ENABLE		(0x1 << 31)
/* H6+ doesn't have these delays */
#define CCM_MMC_CTRL_OCLK_DLY(a)	((void) (a), 0)
#define CCM_MMC_CTRL_SCLK_DLY(a)	((void) (a), 0)

/* MMC module clock config register offsets */
#define CCU_MMC0_CLK_CFG		0x830
#define CCU_MMC1_CLK_CFG		0x834
#define CCU_MMC2_CLK_CFG		0x838

/* MMC gate/reset register and bit definitions */
#define CCU_H6_MMC_GATE_RESET		0x84c
#define RESET_SHIFT			16

/* D1 CCU base address */
#define SUNXI_CCM_BASE			0x02001000

/* PLL6 (PERIPH0) register offset and bitfields */
#define SUNXI_CCU_PLL6_CFG_OFF		0x020
#define SUNXI_CCU_PLL6_CTRL_N_SHIFT	8
#define SUNXI_CCU_PLL6_CTRL_N_MASK	GENMASK(15, 8)

#ifndef __ASSEMBLY__
#include <asm/io.h>

/**
 * clock_get_pll6() - read the PLL6 (PERIPH0) clock rate
 *
 * Returns the PLL6 output frequency in Hz.
 */
static inline unsigned int clock_get_pll6(void)
{
	uint32_t rval = readl((void *)(SUNXI_CCM_BASE + SUNXI_CCU_PLL6_CFG_OFF));

	int n = ((rval & SUNXI_CCU_PLL6_CTRL_N_MASK) >>
		 SUNXI_CCU_PLL6_CTRL_N_SHIFT) + 1;
	int m = ((rval >> 1) & 0x1) + 1;
	int p0 = ((rval >> 16) & 0x7) + 1;
	/* The register defines PLL6-2X, not plain PLL6 */
	return 24000000UL * n / m / p0;
}
#endif /* __ASSEMBLY__ */

#endif /* _ASM_RISCV_ARCH_CLOCK_H */