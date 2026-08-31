// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018, Bin Meng <bmeng.cn@gmail.com>
 * Copyright (C) 2026, Indrek Kruusa <indrek.kruusa@gmail.com>
 *
 * Allwinner D1 (sun20i) SoC support, based around the T-Head C906 CPU core.
 */

#include <irq_func.h>
#include <asm/io.h>
#include <asm/arch/clock.h>
#include <asm/arch/thead_csr.h>
#include <asm/cache.h>
#include <asm/csr.h>
#include <linux/bitops.h>

/*
 * cleanup_before_linux() is called just before we call linux
 * it prepares the processor for linux
 *
 * we disable interrupt and caches.
 */
int cleanup_before_linux(void)
{
	disable_interrupts();

	cache_flush();

	return 0;
}

void harts_early_init(void)
{
	if (!CONFIG_IS_ENABLED(RISCV_MMODE))
		return;

	csr_set(CSR_MXSTATUS, MXSTATUS_THEADISAEE | MXSTATUS_MM);
	csr_set(CSR_MHCR,
		MHCR_BTB_C906 | MHCR_BPE | MHCR_RS | MHCR_WB | MHCR_WA);
	csr_set(CSR_MHINT, MHINT_IWPE | MHINT_IPLD);
}

void sun20i_uart_init(void)
{
	void __iomem *ccm = (void __iomem *)SUNXI_CCM_BASE;
	int uart = CONFIG_CONS_INDEX - 1;

	/* enable UART bus clock gate */
	setbits_le32(ccm + CCU_UART_GATE_RESET, BIT(uart));
	/* deassert UART reset */
	setbits_le32(ccm + CCU_UART_GATE_RESET, BIT(RESET_SHIFT + uart));
}
