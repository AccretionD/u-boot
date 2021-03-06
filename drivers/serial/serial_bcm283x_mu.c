/*
 * (C) Copyright 2016 Stephen Warren <swarren@wwwdotorg.org>
 *
 * Derived from pl01x code:
 *
 * (C) Copyright 2000
 * Rob Taylor, Flying Pig Systems. robt@flyingpig.com.
 *
 * (C) Copyright 2004
 * ARM Ltd.
 * Philippe Robin, <philippe.robin@arm.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

/* Simple U-Boot driver for the BCM283x mini UART */

#include <common.h>
#include <dm.h>
#include <errno.h>
#include <watchdog.h>
#include <asm/io.h>
#include <serial.h>
#include <dm/platform_data/serial_bcm283x_mu.h>
#include <linux/compiler.h>
#include <fdtdec.h>

struct bcm283x_mu_regs {
	u32 io;
	u32 iir;
	u32 ier;
	u32 lcr;
	u32 mcr;
	u32 lsr;
	u32 msr;
	u32 scratch;
	u32 cntl;
	u32 stat;
	u32 baud;
};

#define BCM283X_MU_LCR_DATA_SIZE_8	3

#define BCM283X_MU_LSR_TX_IDLE		BIT(6)
/* This actually means not full, but is named not empty in the docs */
#define BCM283X_MU_LSR_TX_EMPTY		BIT(5)
#define BCM283X_MU_LSR_RX_READY		BIT(0)

struct bcm283x_mu_priv {
	struct bcm283x_mu_regs *regs;
};

static int bcm283x_mu_serial_setbrg(struct udevice *dev, int baudrate)
{
	struct bcm283x_mu_serial_platdata *plat = dev_get_platdata(dev);
	struct bcm283x_mu_priv *priv = dev_get_priv(dev);
	struct bcm283x_mu_regs *regs = priv->regs;
	u32 divider;

	if (plat->skip_init)
		return 0;

	divider = plat->clock / (baudrate * 8);

	writel(BCM283X_MU_LCR_DATA_SIZE_8, &regs->lcr);
	writel(divider - 1, &regs->baud);

	return 0;
}

static int bcm283x_mu_serial_probe(struct udevice *dev)
{
	struct bcm283x_mu_serial_platdata *plat = dev_get_platdata(dev);
	struct bcm283x_mu_priv *priv = dev_get_priv(dev);

	priv->regs = (struct bcm283x_mu_regs *)plat->base;

	return 0;
}

static int bcm283x_mu_serial_getc(struct udevice *dev)
{
	struct bcm283x_mu_priv *priv = dev_get_priv(dev);
	struct bcm283x_mu_regs *regs = priv->regs;
	u32 data;

	/* Wait until there is data in the FIFO */
	if (!(readl(&regs->lsr) & BCM283X_MU_LSR_RX_READY))
		return -EAGAIN;

	data = readl(&regs->io);

	return (int)data;
}

static int bcm283x_mu_serial_putc(struct udevice *dev, const char data)
{
	struct bcm283x_mu_priv *priv = dev_get_priv(dev);
	struct bcm283x_mu_regs *regs = priv->regs;

	/* Wait until there is space in the FIFO */
	if (!(readl(&regs->lsr) & BCM283X_MU_LSR_TX_EMPTY))
		return -EAGAIN;

	/* Send the character */
	writel(data, &regs->io);

	return 0;
}

static int bcm283x_mu_serial_pending(struct udevice *dev, bool input)
{
	struct bcm283x_mu_priv *priv = dev_get_priv(dev);
	struct bcm283x_mu_regs *regs = priv->regs;
	unsigned int lsr = readl(&regs->lsr);

	if (input) {
		WATCHDOG_RESET();
		return lsr & BCM283X_MU_LSR_RX_READY;
	} else {
		return !(lsr & BCM283X_MU_LSR_TX_IDLE);
	}
}

static const struct dm_serial_ops bcm283x_mu_serial_ops = {
	.putc = bcm283x_mu_serial_putc,
	.pending = bcm283x_mu_serial_pending,
	.getc = bcm283x_mu_serial_getc,
	.setbrg = bcm283x_mu_serial_setbrg,
};

U_BOOT_DRIVER(serial_bcm283x_mu) = {
	.name = "serial_bcm283x_mu",
	.id = UCLASS_SERIAL,
	.platdata_auto_alloc_size = sizeof(struct bcm283x_mu_serial_platdata),
	.probe = bcm283x_mu_serial_probe,
	.ops = &bcm283x_mu_serial_ops,
	.flags = DM_FLAG_PRE_RELOC,
	.priv_auto_alloc_size = sizeof(struct bcm283x_mu_priv),
};
