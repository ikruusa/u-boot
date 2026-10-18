// SPDX-License-Identifier: GPL-2.0+
/*
 * Allwinner sun20i (D1) HiFi4 DSP remoteproc driver
 *
 * The D1 contains a Cadence Tensilica HiFi4 audio DSP whose control block
 * lives at 0x0170_0000.  U-Boot copies an ELF image into the DSP's local
 * SRAM / DRAM window, points the core at the image entry point and releases
 * it from run-stall.
 *
 * The DSP_CFG control registers are not described in the public D1-H user
 * manual.  The bit definitions below are taken from Allwinner's own BSP
 * driver (sun8iw20/dsp_reg.h in the FreeRTOS-HIFI4-DSP project).
 */

#define LOG_CATEGORY UCLASS_REMOTEPROC

#include <dm.h>
#include <elf.h>
#include <log.h>
#include <remoteproc.h>
#include <asm/io.h>
#include <dm/device_compat.h>
#include <linux/delay.h>
#include <linux/printk.h>

/* DSP_CFG registers */
#define DSP_ALT_RESET_VEC_REG	0x0000	/* alternative reset vector */
#define DSP_CTRL_REG0		0x0004	/* control register 0 */
#define DSP_STAT_REG		0x0010	/* status register */
#define DSP_VER_REG		0x0020	/* version register */

#define DSP_CTRL_RUN_STALL	BIT(0)
#define DSP_CTRL_START_VEC_SEL	BIT(1)
#define DSP_CTRL_CLKEN		BIT(2)

#define DSP_STAT_PFAULT		BIT(1)
#define DSP_STAT_DOUBLE_EXC	BIT(2)

/* CCU registers */
#define CCU_DSP_CLK_REG		0x0c70
#define CCU_DSP_BGR_REG		0x0c7c

#define DSP_CLK_SCLK_GATING	BIT(31)
#define DSP_CLK_SRC_PERI2X	(0x3 << 24)
#define DSP_CLK_FACTOR_M(x)	(((x) - 1) << 0)

#define DSP_BGR_CFG_GATING	BIT(1)
#define DSP_BGR_RST		BIT(16)
#define DSP_BGR_CFG_RST		BIT(17)
#define DSP_BGR_DBG_RST		BIT(18)

/* SYS_CFG registers */
#define SYS_CFG_DSP_RAMMAP_REG	0x0008
#define DSP_RAMMAP_SYSTEM_BOOT	BIT(0)

/* The DSP sees the first 256 MiB of DRAM at 0x1000_0000 */
#define DSP_DDR_VA_START	0x10000000UL
#define DSP_DDR_VA_END		0x1fffffffUL
#define DSP_DDR_PA_OFFSET	0x30000000UL

enum sunxi_dsp_reg_index {
	DSP_REG_CFG,
	DSP_REG_CCU,
	DSP_REG_SYS_CFG,
};

/**
 * struct sunxi_dsp_priv - driver private data
 * @cfg:	DSP_CFG register block
 * @ccu:	CCU register block
 * @sys_cfg:	SYS_CFG register block
 * @entry:	ELF entry point captured at load time
 */
struct sunxi_dsp_priv {
	void __iomem *cfg;
	void __iomem *ccu;
	void __iomem *sys_cfg;
	ulong entry;
};

static void sunxi_dsp_runstall(struct sunxi_dsp_priv *priv, bool stall)
{
	u32 val = readl(priv->cfg + DSP_CTRL_REG0);

	if (stall)
		val |= DSP_CTRL_RUN_STALL;
	else
		val &= ~DSP_CTRL_RUN_STALL;

	writel(val, priv->cfg + DSP_CTRL_REG0);
}

/*
 * Hand the DSP's 128 KiB local SRAM either to the system (so U-Boot can
 * write the image) or back to the DSP before the core is released.
 * Per the D1-H manual this bit must be 0 before the DSP is used.
 */
static void sunxi_dsp_sram_remap(struct sunxi_dsp_priv *priv, bool system)
{
	u32 val = readl(priv->sys_cfg + SYS_CFG_DSP_RAMMAP_REG);

	if (system)
		val |= DSP_RAMMAP_SYSTEM_BOOT;
	else
		val &= ~DSP_RAMMAP_SYSTEM_BOOT;

	writel(val, priv->sys_cfg + SYS_CFG_DSP_RAMMAP_REG);
}

static void sunxi_dsp_power_on(struct sunxi_dsp_priv *priv)
{
	u32 val;

	/* Clock the DSP from PERI2X / 2 and ungate the DSP clock */
	writel(DSP_CLK_SRC_PERI2X | DSP_CLK_FACTOR_M(2) | DSP_CLK_SCLK_GATING,
	       priv->ccu + CCU_DSP_CLK_REG);

	/* Pass the DSP_CFG bus clock and release all three DSP resets
	 * (each reset bit is 0 = assert, 1 = de-assert)
	 */
	val = readl(priv->ccu + CCU_DSP_BGR_REG);
	val |= DSP_BGR_CFG_GATING | DSP_BGR_RST |
	       DSP_BGR_CFG_RST | DSP_BGR_DBG_RST;
	writel(val, priv->ccu + CCU_DSP_BGR_REG);

	/* Keep the core halted until ->start() is called */
	sunxi_dsp_runstall(priv, true);

	val = readl(priv->cfg + DSP_CTRL_REG0);
	val |= DSP_CTRL_CLKEN;
	writel(val, priv->cfg + DSP_CTRL_REG0);
}

static void sunxi_dsp_power_off(struct sunxi_dsp_priv *priv)
{
	u32 val;

	sunxi_dsp_runstall(priv, true);

	val = readl(priv->ccu + CCU_DSP_BGR_REG);
	val &= ~(DSP_BGR_RST | DSP_BGR_CFG_RST | DSP_BGR_DBG_RST);
	writel(val, priv->ccu + CCU_DSP_BGR_REG);
}

static int sunxi_dsp_probe(struct udevice *dev)
{
	struct sunxi_dsp_priv *priv = dev_get_priv(dev);

	priv->cfg = dev_read_addr_index_ptr(dev, DSP_REG_CFG);
	priv->ccu = dev_read_addr_index_ptr(dev, DSP_REG_CCU);
	priv->sys_cfg = dev_read_addr_index_ptr(dev, DSP_REG_SYS_CFG);

	if (!priv->cfg || !priv->ccu || !priv->sys_cfg) {
		dev_err(dev, "missing reg entries\n");
		return -EINVAL;
	}

	return 0;
}

static void *sunxi_dsp_device_to_virt(struct udevice *dev, ulong da,
				      ulong len, bool *is_iomem)
{
	/* Translate the DSP's DRAM window to a physical DRAM address */
	if (da >= DSP_DDR_VA_START && da <= DSP_DDR_VA_END)
		da += DSP_DDR_PA_OFFSET;

	*is_iomem = false;

	return (void *)da;
}

static int sunxi_dsp_load(struct udevice *dev, ulong addr, ulong size)
{
	struct sunxi_dsp_priv *priv = dev_get_priv(dev);
	const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)addr;
	int ret;

	if (size < sizeof(*ehdr))
		return -EINVAL;

	ret = rproc_elf32_sanity_check(addr, size);
	if (ret)
		return ret;

	priv->entry = ehdr->e_entry;

	/* Let the system write the local SRAM, then power the core up
	 * halted so the image can be copied in
	 */
	sunxi_dsp_sram_remap(priv, true);
	sunxi_dsp_power_on(priv);

	ret = rproc_elf32_load_image(dev, addr, size);

	/* Hand the local SRAM back to the DSP */
	sunxi_dsp_sram_remap(priv, false);

	if (ret)
		dev_err(dev, "failed to load image: %d\n", ret);

	return ret;
}

static int sunxi_dsp_start(struct udevice *dev)
{
	struct sunxi_dsp_priv *priv = dev_get_priv(dev);
	u32 val, stat;

	/* Boot the core from the ELF entry point instead of the DSP's
	 * default reset vector
	 */
	writel(priv->entry, priv->cfg + DSP_ALT_RESET_VEC_REG);

	val = readl(priv->cfg + DSP_CTRL_REG0);
	val |= DSP_CTRL_START_VEC_SEL | DSP_CTRL_CLKEN;
	writel(val, priv->cfg + DSP_CTRL_REG0);

	sunxi_dsp_runstall(priv, false);

	mdelay(1);

	stat = readl(priv->cfg + DSP_STAT_REG);
	if (stat & (DSP_STAT_PFAULT | DSP_STAT_DOUBLE_EXC)) {
		dev_err(dev, "DSP faulted: STAT=0x%08x VER=0x%08x\n",
			stat, readl(priv->cfg + DSP_VER_REG));
		return -EIO;
	}

	dev_dbg(dev, "DSP started at 0x%08lx, STAT=0x%08x\n",
		priv->entry, stat);

	return 0;
}

static int sunxi_dsp_stop(struct udevice *dev)
{
	struct sunxi_dsp_priv *priv = dev_get_priv(dev);

	sunxi_dsp_runstall(priv, true);

	return 0;
}

static int sunxi_dsp_reset(struct udevice *dev)
{
	struct sunxi_dsp_priv *priv = dev_get_priv(dev);

	sunxi_dsp_power_off(priv);

	return 0;
}

static int sunxi_dsp_is_running(struct udevice *dev)
{
	struct sunxi_dsp_priv *priv = dev_get_priv(dev);

	return !(readl(priv->cfg + DSP_CTRL_REG0) & DSP_CTRL_RUN_STALL);
}

static const struct dm_rproc_ops sunxi_dsp_ops = {
	.load		= sunxi_dsp_load,
	.start		= sunxi_dsp_start,
	.stop		= sunxi_dsp_stop,
	.reset		= sunxi_dsp_reset,
	.is_running	= sunxi_dsp_is_running,
	.device_to_virt	= sunxi_dsp_device_to_virt,
};

static const struct udevice_id sunxi_dsp_ids[] = {
	{ .compatible = "allwinner,sun20i-d1-dsp" },
	{ }
};

U_BOOT_DRIVER(sunxi_dsp_rproc) = {
	.name		= "sunxi_dsp_rproc",
	.id		= UCLASS_REMOTEPROC,
	.of_match	= sunxi_dsp_ids,
	.ops		= &sunxi_dsp_ops,
	.probe		= sunxi_dsp_probe,
	.priv_auto	= sizeof(struct sunxi_dsp_priv),
};
