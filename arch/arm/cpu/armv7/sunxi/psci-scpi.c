// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016 Chen-Yu Tsai <wens@csie.org>
 * Copyright (C) 2018-2021 Samuel Holland <samuel@sholland.org>
 */

#include <common.h>
#include <asm/arch/cpu.h>
#include <asm/arch/cpucfg.h>
#include <asm/armv7.h>
#include <asm/gic.h>
#include <asm/io.h>
#include <asm/psci.h>
#include <asm/secure.h>
#include <asm/system.h>

#define	GICD_BASE		(SUNXI_GIC400_BASE + GIC_DIST_OFFSET)
#define	GICC_BASE		(SUNXI_GIC400_BASE + GIC_CPU_OFFSET_A15)

#define HW_ON			0
#define HW_OFF			1
#define HW_STANDBY		2

#define MPIDR_AFFLVL0(mpidr)	(mpidr & 0xf)
#define MPIDR_AFFLVL1(mpidr)	(mpidr >> 8 & 0xf)

#if defined(CONFIG_MACH_SUN8I_H3)
#define SCPI_SHMEM_BASE		0x0004be00
#else
#define SCPI_SHMEM_BASE		0x00053e00
#endif
#define SCPI_SHMEM		((struct scpi_shmem *)SCPI_SHMEM_BASE)

#define SCPI_RX_CHANNEL		1
#define SCPI_TX_CHANNEL		0
#define SCPI_VIRTUAL_CHANNEL	BIT(0)

#define SCPI_MESSAGE_SIZE	0x100
#define SCPI_PAYLOAD_SIZE	(SCPI_MESSAGE_SIZE - sizeof(struct scpi_header))

#define SUNXI_MSGBOX_BASE	0x01c17000
#define REMOTE_IRQ_STAT_REG	(SUNXI_MSGBOX_BASE + 0x0050)
#define LOCAL_IRQ_STAT_REG	(SUNXI_MSGBOX_BASE + 0x0070)
#define MSG_STAT_REG(n)		(SUNXI_MSGBOX_BASE + 0x0140 + 0x4 * (n))
#define MSG_DATA_REG(n)		(SUNXI_MSGBOX_BASE + 0x0180 + 0x4 * (n))

#define RX_IRQ(n)		BIT(0 + 2 * (n))
#define TX_IRQ(n)		BIT(1 + 2 * (n))

enum {
	CORE_POWER_LEVEL		= 0,
	CLUSTER_POWER_LEVEL		= 1,
	CSS_POWER_LEVEL			= 2,
};

enum {
	SCPI_CMD_SCP_READY		= 0x01,
	SCPI_CMD_SET_CSS_POWER_STATE	= 0x03,
	SCPI_CMD_GET_CSS_POWER_STATE	= 0x04,
	SCPI_CMD_SET_SYS_POWER_STATE	= 0x05,
};

enum {
	SCPI_E_OK			= 0,
	SCPI_E_PARAM			= 1,
	SCPI_E_ALIGN			= 2,
	SCPI_E_SIZE			= 3,
	SCPI_E_HANDLER			= 4,
	SCPI_E_ACCESS			= 5,
	SCPI_E_RANGE			= 6,
	SCPI_E_TIMEOUT			= 7,
	SCPI_E_NOMEM			= 8,
	SCPI_E_PWRSTATE			= 9,
	SCPI_E_SUPPORT			= 10,
	SCPI_E_DEVICE			= 11,
	SCPI_E_BUSY			= 12,
	SCPI_E_OS			= 13,
	SCPI_E_DATA			= 14,
	SCPI_E_STATE			= 15,
};

enum {
	SCPI_POWER_ON			= 0x00,
	SCPI_POWER_RETENTION		= 0x01,
	SCPI_POWER_OFF			= 0x03,
};

enum {
	SCPI_SYSTEM_SHUTDOWN		= 0x00,
	SCPI_SYSTEM_REBOOT		= 0x01,
	SCPI_SYSTEM_RESET		= 0x02,
};

struct scpi_header {
	u8			command;
	u8			sender;
	u16			size;
	u32			status;
};

struct scpi_message {
	struct scpi_header	header;
	u8			payload[SCPI_PAYLOAD_SIZE];
};

struct scpi_shmem {
	struct scpi_message	rx;
	struct scpi_message	tx;
};

static bool __secure_data gic_dist_init;

static u32 __secure_data lock;

static inline u32 __secure read_mpidr(void)
{
	u32 val;

	asm volatile ("mrc p15, 0, %0, c0, c0, 5" : "=r" (val));

	return val;
}

static void __secure cp15_write_cntp_tval(u32 tval)
{
	asm volatile ("mcr p15, 0, %0, c14, c2, 0" : : "r" (tval));
}

static void __secure cp15_write_cntp_ctl(u32 val)
{
	asm volatile ("mcr p15, 0, %0, c14, c2, 1" : : "r" (val));
}

static u32 __secure cp15_read_cntp_ctl(void)
{
	u32 val;

	asm volatile ("mrc p15, 0, %0, c14, c2, 1" : "=r" (val));

	return val;
}

#define ONE_US (CONFIG_COUNTER_FREQUENCY / 1000000)

static void __secure mdfs_udelay(u32 us)
{
	u32 reg = ONE_US * us;

	cp15_write_cntp_tval(reg);
	isb();
	cp15_write_cntp_ctl(3);

	do {
		isb();
		reg = cp15_read_cntp_ctl();
	} while (!(reg & BIT(2)));

	cp15_write_cntp_ctl(0);
	isb();
}

static void __secure scpi_begin_command(void)
{
	u32 mpidr = read_mpidr();

	do {
		while (readl(&lock));
		writel(mpidr, &lock);
		dsb();
	} while (readl(&lock) != mpidr);
	while (readl(REMOTE_IRQ_STAT_REG) & RX_IRQ(SCPI_TX_CHANNEL));
}

static void __secure scpi_send_command(void)
{
	writel(SCPI_VIRTUAL_CHANNEL, MSG_DATA_REG(SCPI_TX_CHANNEL));
}

static void __secure scpi_wait_response(void)
{
	while (!readl(MSG_STAT_REG(SCPI_RX_CHANNEL)));
}

static void __secure scpi_end_command(void)
{
	while (readl(MSG_STAT_REG(SCPI_RX_CHANNEL)))
		readl(MSG_DATA_REG(SCPI_RX_CHANNEL));
	writel(RX_IRQ(SCPI_RX_CHANNEL), LOCAL_IRQ_STAT_REG);
	writel(0, &lock);
}

static void __secure scpi_set_css_power_state(u32 target_cpu, u32 core_state,
					      u32 cluster_state, u32 css_state)
{
	struct scpi_shmem *shmem = SCPI_SHMEM;

	scpi_begin_command();

	shmem->tx.header.command = SCPI_CMD_SET_CSS_POWER_STATE;
	shmem->tx.header.size = 4;

	shmem->tx.payload[0] = target_cpu    >> 4 | target_cpu;
	shmem->tx.payload[1] = cluster_state << 4 | core_state;
	shmem->tx.payload[2] = css_state;
	shmem->tx.payload[3] = 0;

	scpi_send_command();
	scpi_end_command();
}

static s32 __secure scpi_get_css_power_state(u32 target_cpu, u8 *core_states,
					     u8 *cluster_state)
{
	struct scpi_shmem *shmem = SCPI_SHMEM;
	u32 cluster = MPIDR_AFFLVL1(target_cpu);
	u32 offset;
	s32 ret;

	scpi_begin_command();

	shmem->tx.header.command = SCPI_CMD_GET_CSS_POWER_STATE;
	shmem->tx.header.size = 0;

	scpi_send_command();
	scpi_wait_response();

	for (offset = 0; offset < shmem->rx.header.size; offset += 2) {
		if ((shmem->rx.payload[offset] & 0xf) == cluster) {
			*cluster_state = shmem->rx.payload[offset+0] >> 4;
			*core_states   = shmem->rx.payload[offset+1];

			break;
		}
	}

	ret = shmem->rx.header.status;

	scpi_end_command();

	return ret;
}

static s32 __secure scpi_set_sys_power_state(u32 sys_state)
{
	struct scpi_shmem *shmem = SCPI_SHMEM;
	s32 ret;

	scpi_begin_command();

	shmem->tx.header.command = SCPI_CMD_SET_SYS_POWER_STATE;
	shmem->tx.header.size = 1;

	shmem->tx.payload[0] = sys_state;

	scpi_send_command();
	scpi_wait_response();

	ret = shmem->rx.header.status;

	scpi_end_command();

	return ret;
}

void psci_enable_smp(void);

static s32 __secure psci_suspend_common(u32 pc, u32 context_id, u32 core_state,
					u32 cluster_state, u32 css_state)

{
	u32 target_cpu = read_mpidr();

	if (core_state == SCPI_POWER_OFF)
		psci_save(MPIDR_AFFLVL0(target_cpu), pc, context_id);
	if (css_state == SCPI_POWER_OFF)
		gic_dist_init = true;

	scpi_set_css_power_state(target_cpu, core_state,
				 cluster_state, css_state);

	psci_cpu_off_common();

	wfi();

	psci_enable_smp();

	return ARM_PSCI_RET_SUCCESS;
}

u32 __secure psci_version(void)
{
	return ARM_PSCI_VER_1_1;
}

s32 __secure psci_cpu_suspend(u32 __always_unused function_id,
			      u32 power_state, u32 pc, u32 context_id)
{
	return psci_suspend_common(pc, context_id,
				   power_state >> 0 & 0xf,
				   power_state >> 4 & 0xf,
				   power_state >> 8 & 0xf);
}

s32 __secure psci_cpu_off(void)
{
	u32 pc = 0, context_id = 0;

	return psci_suspend_common(pc, context_id, SCPI_POWER_OFF,
				   SCPI_POWER_OFF, SCPI_POWER_ON);
}

s32 __secure psci_cpu_on(u32 __always_unused function_id,
			 u32 target_cpu, u32 pc, u32 context_id)
{
	psci_save(MPIDR_AFFLVL0(target_cpu), pc, context_id);

	scpi_set_css_power_state(target_cpu, SCPI_POWER_ON,
				 SCPI_POWER_ON, SCPI_POWER_ON);

	return ARM_PSCI_RET_SUCCESS;
}

s32 __secure psci_affinity_info(u32 function_id,
				u32 target_cpu, u32 power_level)
{
	if (power_level != CORE_POWER_LEVEL)
		return ARM_PSCI_RET_INVAL;

	/* This happens to have the same HW_ON/HW_OFF encoding. */
	return psci_node_hw_state(function_id, target_cpu, power_level);
}

void __secure psci_system_off(void)
{
	scpi_set_sys_power_state(SCPI_SYSTEM_SHUTDOWN);

	/* Wait to be turned off. */
	for (;;) wfi();
}

void __secure psci_system_reset(void)
{
	scpi_set_sys_power_state(SCPI_SYSTEM_REBOOT);

	/* Wait to be turned off. */
	for (;;) wfi();
}

s32 __secure psci_features(u32 __always_unused function_id,
			   u32 psci_fid)
{
	switch (psci_fid) {
	case ARM_PSCI_0_2_FN_PSCI_VERSION:
	case ARM_PSCI_0_2_FN_CPU_SUSPEND:
	case ARM_PSCI_0_2_FN_CPU_OFF:
	case ARM_PSCI_0_2_FN_CPU_ON:
	case ARM_PSCI_0_2_FN_AFFINITY_INFO:
	case ARM_PSCI_0_2_FN_SYSTEM_OFF:
	case ARM_PSCI_0_2_FN_SYSTEM_RESET:
	case ARM_PSCI_1_0_FN_PSCI_FEATURES:
	case ARM_PSCI_1_0_FN_CPU_DEFAULT_SUSPEND:
	case ARM_PSCI_1_0_FN_NODE_HW_STATE:
	case ARM_PSCI_1_0_FN_SYSTEM_SUSPEND:
	case ARM_PSCI_1_1_FN_SYSTEM_RESET2:
		return ARM_PSCI_RET_SUCCESS;
	default:
		return ARM_PSCI_RET_NI;
	}
}

s32 __secure psci_cpu_default_suspend(u32 __always_unused function_id,
				      u32 pc, u32 context_id)
{
	return psci_suspend_common(pc, context_id, SCPI_POWER_OFF,
				   SCPI_POWER_OFF, SCPI_POWER_RETENTION);
}

s32 __secure psci_node_hw_state(u32 __always_unused function_id,
				u32 target_cpu, u32 power_level)
{
	u32 core = MPIDR_AFFLVL0(target_cpu);
	u8 core_states, cluster_state;

	if (power_level >= CSS_POWER_LEVEL)
		return HW_ON;
	if (scpi_get_css_power_state(target_cpu, &core_states, &cluster_state))
		return ARM_PSCI_RET_NI;
	if (power_level == CLUSTER_POWER_LEVEL) {
		if (cluster_state == SCPI_POWER_ON)
			return HW_ON;
		if (cluster_state < SCPI_POWER_OFF)
			return HW_STANDBY;
		return HW_OFF;
	}

	return (core_states & BIT(core)) ? HW_ON : HW_OFF;
}

s32 __secure psci_system_suspend(u32 __always_unused function_id,
				 u32 pc, u32 context_id)
{
	return psci_suspend_common(pc, context_id, SCPI_POWER_OFF,
				   SCPI_POWER_OFF, SCPI_POWER_OFF);
}

s32 __secure psci_system_reset2(u32 __always_unused function_id,
				u32 reset_type, u32 cookie)
{
	s32 ret;

	if (reset_type)
		return ARM_PSCI_RET_INVAL;

	ret = scpi_set_sys_power_state(SCPI_SYSTEM_RESET);
	if (ret)
		return ARM_PSCI_RET_INVAL;

	/* Wait to be turned off. */
	for (;;) wfi();
}

#define MCTL_COM_BASE               ((void *)(0x1c62000))
#define MCTL_CTL_BASE               ((void *)(0x1c63000))

#define _CCM_PLL_DDR_REG            (CCM_PLL_BASE  +  0x20)
#define MC_WORK_MODE                (MCTL_COM_BASE +  0x00)
#define PIR                         (MCTL_CTL_BASE +  0x00)
#define PWRCTL                      (MCTL_CTL_BASE +  0x04)
#define PGSR0                       (MCTL_CTL_BASE +  0x10)
#define STATR                       (MCTL_CTL_BASE +  0x18)
#define DTCR                        (MCTL_CTL_BASE +  0xc0)
#define ODTMAP                      (MCTL_CTL_BASE + 0x120)
#define DXnGCR0(x)                  (MCTL_CTL_BASE + 0x344 + 0x80*(x))

s32 __secure sunxi_dram_dvfs_req(u32 __always_unused function_id,
				 u32 freq, u32 flags)
{
	u32 rank_num, reg_val;
	unsigned int i = 0;

	rank_num = readl(MC_WORK_MODE) & 0x1;

	/* 1. enter self-refresh and disable all master access */
	reg_val = readl(PWRCTL);
	reg_val |= (0x1<<0);
	reg_val |= (0x1<<8);
	writel(reg_val, PWRCTL);
	mdfs_udelay(1);

	/* make sure enter self-refresh */
	while ((readl(STATR) & 0x7) != 0x3)
		;

	/* 2.Update PLL setting and wait 1ms */
	reg_val = readl(SUNXI_CCM_BASE + 0x20);
	reg_val |= (1U << 20);
	writel(reg_val, SUNXI_CCM_BASE + 0x20);
	mdfs_udelay(1000);

	/* 3.set PIR register issue phy reset and DDL calibration */
	if (rank_num) {
		reg_val = readl(DTCR);
		reg_val &= ~(0x3<<24);
		reg_val |= (0x3<<24);
		writel(reg_val, DTCR);
	} else {
		reg_val = readl(DTCR);
		reg_val &= ~(0x3<<24);
		reg_val |= (0x1<<24);
		writel(reg_val, DTCR);
	}

	/* trigger phy reset and DDL calibration */
	writel(0x40000061, PIR);
	/* add 1us delay here */
	mdfs_udelay(1);

	/* wait for DLL Lock */
	while ((readl(PGSR0) & 0x1) != 0x1)
		;

	/*4.setting ODT configure */
	if (!(flags & BIT(0))) {
		/* turn off DRAMC ODT */
		for (i = 0; i < 4; i++) {
			reg_val = readl(DXnGCR0(i));
			reg_val &= ~(0x3U<<4);
			reg_val |= (0x2<<4);
			writel(reg_val, DXnGCR0(i));
		}
	} else {
		/* turn on DRAMC dynamic ODT */
		for (i = 0; i < 4; i++) {
			reg_val = readl(DXnGCR0(i));
			reg_val &= ~(0x3U<<4);
			writel(reg_val, DXnGCR0(i));
		}
	}

	/* 5.exit self-refresh and enable all master access */
	reg_val = readl(PWRCTL);
	reg_val &= ~(0x1<<0);
	reg_val &= ~(0x1<<8);
	writel(reg_val, PWRCTL);
	mdfs_udelay(1);

	/* make sure exit self-refresh */
	while ((readl(STATR) & 0x7) != 0x1)
		;
	return 0;
}

/*
 * R40 is different from other single cluster SoCs. The secondary core
 * entry address register is in the SRAM controller address range.
 */
#define SUN8I_R40_SRAMC_SOFT_ENTRY_REG0		(0xbc)

#ifdef CONFIG_MACH_SUN8I_R40
/* secondary core entry address is programmed differently on R40 */
static void __secure sunxi_set_entry_address(void *entry)
{
	writel((u32)entry,
	       SUNXI_SRAMC_BASE + SUN8I_R40_SRAMC_SOFT_ENTRY_REG0);
}
#else
static void __secure sunxi_set_entry_address(void *entry)
{
	struct sunxi_cpucfg_reg *cpucfg =
		(struct sunxi_cpucfg_reg *)SUNXI_CPUCFG_BASE;

	writel((u32)entry, &cpucfg->priv0);

#ifdef CONFIG_MACH_SUN8I_H3
	/* Redirect CPU 0 to the secure monitor via the resume shim. */
	writel(0x16aaefe8, &cpucfg->super_standy_flag);
	writel(0xaa16efe8, &cpucfg->super_standy_flag);
	writel(CONFIG_SUNXI_RESUME_BASE, &cpucfg->priv1);
#endif
}
#endif

void __secure psci_arch_init(void)
{
	static bool __secure_data one_time_init = true;

	if (one_time_init) {
		/* Set secondary core power-on PC. */
		sunxi_set_entry_address(psci_cpu_entry);

		/* Wait for the SCP firmware to boot. */
		scpi_begin_command();
		scpi_wait_response();
		scpi_end_command();

		one_time_init = false;
	}

	/*
	 * Copied from arch/arm/cpu/armv7/virt-v7.c
	 * See also gic_resume() in arch/arm/mach-imx/mx7/psci-mx7.c
	 */
	if (gic_dist_init) {
		u32 i, itlinesnr;

		/* enable the GIC distributor */
		writel(readl(GICD_BASE + GICD_CTLR) | 0x03, GICD_BASE + GICD_CTLR);

		/* TYPER[4:0] contains an encoded number of available interrupts */
		itlinesnr = readl(GICD_BASE + GICD_TYPER) & 0x1f;

		/* set all bits in the GIC group registers to one to allow access
		 * from non-secure state. The first 32 interrupts are private per
		 * CPU and will be set later when enabling the GIC for each core
		 */
		for (i = 1; i <= itlinesnr; i++)
			writel((unsigned)-1, GICD_BASE + GICD_IGROUPRn + 4 * i);

		gic_dist_init = false;
	}

	/* Be cool with non-secure. */
	writel(0xff, GICC_BASE + GICC_PMR);
}
