/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2026, Indrek Kruusa <indrek.kruusa@gmail.com>
 *
 * Allwinner D1 (sun20i) memory map.
 */
#ifndef _ASM_RISCV_ARCH_SUN20I_CPU_H
#define _ASM_RISCV_ARCH_SUN20I_CPU_H

#define SUNXI_CCM_BASE			0x02001000

#define SUNXI_SID_BASE			0x03006200

#define SUNXI_DRAM_COM_BASE		0x03102000
#define SUNXI_DRAM_COM_BASE2		0x03202000
#define SUNXI_DRAM_PHY_BASE		0x03103000

#define SUNXI_MMC0_BASE			0x04020000
#define SUNXI_MMC1_BASE			0x04021000
#define SUNXI_MMC2_BASE			0x04022000

#endif /* _ASM_RISCV_ARCH_SUN20I_CPU_H */
