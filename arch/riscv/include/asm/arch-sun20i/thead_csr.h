/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2026, Indrek Kruusa <indrek.kruusa@gmail.com>
 *
 * T-Head C906 CSR definitions for the Allwinner D1 (sun20i).
 */

#ifndef _ASM_RISCV_ARCH_THEAD_CSR_H
#define _ASM_RISCV_ARCH_THEAD_CSR_H

#define CSR_MXSTATUS            0x7c0
#define CSR_MHCR                0x7c1
#define CSR_MHINT               0x7c5

#define MXSTATUS_THEADISAEE     BIT(22) /* T-HEAD ISA extensions enable */
#define MXSTATUS_MAEE           BIT(21) /* MMU honours the PTE memory type */
#define MXSTATUS_MM             BIT(15) /* misaligned access enable */

#define MHCR_IE                 BIT(0)  /* icache enable */
#define MHCR_DE                 BIT(1)  /* dcache enable */
#define MHCR_WA                 BIT(2)  /* dcache write allocate */
#define MHCR_RS                 BIT(4)  /* return stack enable */
#define MHCR_BPE                BIT(5)  /* branch prediction enable */
#define MHCR_BTB_C906           BIT(6)  /* branch target prediction enable */

#define MHINT_IPLD              BIT(8)  /* icache prefetch enable */
#define MHINT_IWPE              BIT(9)  /* icache way prediction enable */

#endif /* _ASM_RISCV_ARCH_THEAD_CSR_H */
