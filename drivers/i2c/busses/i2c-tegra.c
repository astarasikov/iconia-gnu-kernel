/*
 * drivers/i2c/busses/i2c-tegra.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Colin Cross <ccross@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/i2c-tegra.h>

#include <asm/unaligned.h>

#include <mach/clk.h>
#include <mach/pinmux.h>

#define TEGRA_I2C_TIMEOUT (msecs_to_jiffies(1000))
#define TEGRA_I2C_RETRIES 3
#define BYTES_PER_FIFO_WORD 4

#define I2C_CNFG				0x000
#define I2C_CNFG_DEBOUNCE_CNT_SHIFT		12
#define I2C_CNFG_PACKET_MODE_EN			(1<<10)
#define I2C_CNFG_NEW_MASTER_FSM			(1<<11)
#define I2C_STATUS				0x01C
#define I2C_STATUS_BUSY				(1<<8)
#define I2C_SL_CNFG				0x020
#define I2C_SL_CNFG_NACK			(1<<1)
#define I2C_SL_CNFG_NEWSL			(1<<2)
#define I2C_SL_ADDR1				0x02c
#define I2C_SL_ADDR2				0x030
#define I2C_TX_FIFO				0x050
#define I2C_RX_FIFO				0x054
#define I2C_PACKET_TRANSFER_STATUS		0x058
#define I2C_FIFO_CONTROL			0x05c
#define I2C_FIFO_CONTROL_TX_FLUSH		(1<<1)
#define I2C_FIFO_CONTROL_RX_FLUSH		(1<<0)
#define I2C_FIFO_CONTROL_TX_TRIG_SHIFT		5
#define I2C_FIFO_CONTROL_RX_TRIG_SHIFT		2
#define I2C_FIFO_STATUS				0x060
#define I2C_FIFO_STATUS_TX_MASK			0xF0
#define I2C_FIFO_STATUS_TX_SHIFT		4
#define I2C_FIFO_STATUS_RX_MASK			0x0F
#define I2C_FIFO_STATUS_RX_SHIFT		0
#define I2C_INT_MASK				0x064
#define I2C_INT_STATUS				0x068
#define I2C_INT_PACKET_XFER_COMPLETE		(1<<7)
#define I2C_INT_ALL_PACKETS_XFER_COMPLETE	(1<<6)
#define I2C_INT_TX_FIFO_OVERFLOW		(1<<5)
#define I2C_INT_RX_FIFO_UNDERFLOW		(1<<4)
#define I2C_INT_NO_ACK				(1<<3)
#define I2C_INT_ARBITRATION_LOST		(1<<2)
#define I2C_INT_TX_FIFO_DATA_REQ		(1<<1)
#define I2C_INT_RX_FIFO_DATA_REQ		(1<<0)
#define I2C_CLK_DIVISOR				0x06c

#define DVC_CTRL_REG1				0x000
#define DVC_CTRL_REG1_INTR_EN			(1<<10)
#define DVC_CTRL_REG2				0x004
#define DVC_CTRL_REG3				0x008
#define DVC_CTRL_REG3_SW_PROG			(1<<26)
#define DVC_CTRL_REG3_I2C_DONE_INTR_EN		(1<<30)
#define DVC_STATUS				0x00c
#define DVC_STATUS_I2C_DONE_INTR		(1<<30)

#define I2C_ERR_NONE				0x00
#define I2C_ERR_NO_ACK				0x01
#define I2C_ERR_ARBITRATION_LOST		0x02
#define I2C_ERR_UNKNOWN_INTERRUPT		0x04
#define I2C_ERR_UNEXPECTED_STATUS		0x08

#define PACKET_HEADER0_HEADER_SIZE_SHIFT	28
#define PACKET_HEADER0_PACKET_ID_SHIFT		16
#define PACKET_HEADER0_CONT_ID_SHIFT		12
#define PACKET_HEADER0_PROTOCOL_I2C		(1<<4)

#define I2C_HEADER_HIGHSPEED_MODE		(1<<22)
#define I2C_HEADER_CONT_ON_NAK			(1<<21)
#define I2C_HEADER_SEND_START_BYTE		(1<<20)
#define I2C_HEADER_READ				(1<<19)
#define I2C_HEADER_10BIT_ADDR			(1<<18)
#define I2C_HEADER_IE_ENABLE			(1<<17)
#define I2C_HEADER_REPEAT_START			(1<<16)
#define I2C_HEADER_MASTER_ADDR_SHIFT		12
#define I2C_HEADER_SLAVE_ADDR_SHIFT		1

#define SL_ADDR1(addr) (addr & 0xff)
#define SL_ADDR2(addr) ((addr >> 8) & 0xff)

struct tegra_i2c_dev;

struct tegra_i2c_bus {
	struct tegra_i2c_dev *dev;
	const struct tegra_pingroup_config *mux;
	int mux_len;
	unsigned long bus_clk_rate;
	struct i2c_adapter adapter;
};

/**
 * struct tegra_i2c_dev	- per device i2c context
 * @dev: device reference for power management
 * @adapter: core i2c layer adapter information
 * @clk: clock reference for i2c controller
 * @i2c_clk: clock reference for i2c bus
 * @iomem: memory resource for registers
 * @base: ioremapped registers cookie
 * @cont_id: i2c controller id, used for for packet header
 * @irq: irq number of transfer complete interrupt
 * @is_dvc: identifies the DVC i2c controller, has a different register layout
 * @msg_complete: transfer completion notifier
 * @msg_err: error code for completed message
 * @msg_buf: pointer to current message data
 * @msg_buf_remaining: size of unsent data in the message buffer
 * @msg_read: identifies read transfers
 * @bus_clk_rate: current i2c bus clock rate
 * @is_suspended: prevents i2c controller accesses after suspend is called
 */
struct tegra_i2c_dev {
	struct device *dev;
	struct clk *clk;
	struct clk *i2c_clk;
	struct resource *iomem;
	struct rt_mutex dev_lock;
	void __iomem *base;
	int cont_id;
	int irq;
	bool irq_disabled;
	int is_dvc;
	struct completion msg_complete;
	int msg_err;
	u8 *msg_buf;
	size_t msg_buf_remaining;
	int msg_read;
	int msg_transfer_complete;
	bool is_suspended;
	int bus_count;
	const struct tegra_pingroup_config *last_mux;
	int last_mux_len;
	unsigned long last_bus_clk;
	u16 slave_addr;
	struct tegra_i2c_bus busses[1];
};

static void dvc_writel(struct tegra_i2c_dev *i2c_dev, u32 val, unsigned long reg)
{
	writel(val, i2c_dev->base + reg);
}

static u32 dvc_readl(struct tegra_i2c_dev *i2c_dev, unsigned long reg)
{
	return readl(i2c_dev->base + reg);
}

/*
 * i2c_writel and i2c_readl will offset the register if necessary to talk
 * to the I2C block inside the DVC block
 */
static unsigned long tegra_i2c_reg_addr(struct tegra_i2c_dev *i2c_dev,
	unsigned long reg)
{
	if (i2c_dev->is_dvc)
		reg += (reg >= I2C_TX_FIFO) ? 0x10 : 0x40;
	return reg;
}

static void i2c_writel(struct tegra_i2c_dev *i2c_dev, u32 val,
	unsigned long reg)
{
	writel(val, i2c_dev->base + tegra_i2c_reg_addr(i2c_dev, reg));
}

static u32 i2c_readl(struct tegra_i2c_dev *i2c_dev, unsigned long reg)
{
	return readl(i2c_dev->base + tegra_i2c_reg_addr(i2c_dev, reg));
}

static void i2c_writesl(struct tegra_i2c_dev *i2c_dev, void *data,
	unsigned long reg, int len)
{
	writesl(i2c_dev->base + tegra_i2c_reg_addr(i2c_dev, reg), data, len);
}

static void i2c_readsl(struct tegra_i2c_dev *i2c_dev, void *data,
	unsigned long reg, int len)
{
	readsl(i2c_dev->base + tegra_i2c_reg_addr(i2c_dev, reg), data, len);
}

static void tegra_i2c_mask_irq(struct tegra_i2c_dev *i2c_dev, u32 mask)
{
	u32 int_mask = i2c_readl(i2c_dev, I2C_INT_MASK);
	int_mask &= ~mask;
	i2c_writel(i2c_dev, int_mask, I2C_INT_MASK);
}

static void tegra_i2c_unmask_irq(struct tegra_i2c_dev *i2c_dev, u32 mask)
{
	u32 int_mask = i2c_readl(i2c_dev, I2C_INT_MASK);
	int_mask |= mask;
	i2c_writel(i2c_dev, int_mask, I2C_INT_MASK);
}

static void tegra_i2c_set_clk(struct tegra_i2c_dev *i2c_dev, unsigned int freq)
{
	clk_set_rate(i2c_dev->clk, freq * 8);
}

static int tegra_i2c_flush_fifos(struct tegra_i2c_dev *i2c_dev)
{
	unsigned long timeout = jiffies + HZ;
	u32 val = i2c_readl(i2c_dev, I2C_FIFO_CONTROL);
	val |= I2C_FIFO_CONTROL_TX_FLUSH | I2C_FIFO_CONTROL_RX_FLUSH;
	i2c_writel(i2c_dev, val, I2C_FIFO_CONTROL);

	while (i2c_readl(i2c_dev, I2C_FIFO_CONTROL) &
		(I2C_FIFO_CONTROL_TX_FLUSH | I2C_FIFO_CONTROL_RX_FLUSH)) {
		if (time_after(jiffies, timeout)) {
			dev_warn(i2c_dev->dev, "timeout waiting for fifo flush\n");
			return -ETIMEDOUT;
		}
		msleep(1);
	}
	return 0;
}

static int tegra_i2c_empty_rx_fifo(struct tegra_i2c_dev *i2c_dev)
{
	u32 val;
	int rx_fifo_avail;
	u8 *buf = i2c_dev->msg_buf;
	size_t buf_remaining = i2c_dev->msg_buf_remaining;
	int words_to_transfer;

	val = i2c_readl(i2c_dev, I2C_FIFO_STATUS);
	rx_fifo_avail = (val & I2C_FIFO_STATUS_RX_MASK) >>
		I2C_FIFO_STATUS_RX_SHIFT;

	/* Rounds down to not include partial word at the end of buf */
	words_to_transfer = buf_remaining / BYTES_PER_FIFO_WORD;
	if (words_to_transfer > rx_fifo_avail)
		words_to_transfer = rx_fifo_avail;

	i2c_readsl(i2c_dev, buf, I2C_RX_FIFO, words_to_transfer);

	buf += words_to_transfer * BYTES_PER_FIFO_WORD;
	buf_remaining -= words_to_transfer * BYTES_PER_FIFO_WORD;
	rx_fifo_avail -= words_to_transfer;

	/*
	 * If there is a partial word at the end of buf, handle it manually to
	 * prevent overwriting past the end of buf
	 */
	if (rx_fifo_avail > 0 && buf_remaining > 0) {
		BUG_ON(buf_remaining > 3);
		val = i2c_readl(i2c_dev, I2C_RX_FIFO);
		memcpy(buf, &val, buf_remaining);
		buf_remaining = 0;
		rx_fifo_avail--;
	}

	BUG_ON(rx_fifo_avail > 0 && buf_remaining > 0);
	i2c_dev->msg_buf_remaining = buf_remaining;
	i2c_dev->msg_buf = buf;
	return 0;
}

static int tegra_i2c_fill_tx_fifo(struct tegra_i2c_dev *i2c_dev)
{
	u32 val;
	int tx_fifo_avail;
	u8 *buf = i2c_dev->msg_buf;
	size_t buf_remaining = i2c_dev->msg_buf_remaining;
	int words_to_transfer;

	val = i2c_readl(i2c_dev, I2C_FIFO_STATUS);
	tx_fifo_avail = (val & I2C_FIFO_STATUS_TX_MASK) >>
		I2C_FIFO_STATUS_TX_SHIFT;

	/* Rounds down to not include partial word at the end of buf */
	words_to_transfer = buf_remaining / BYTES_PER_FIFO_WORD;

	/* It's very common to have < 4 bytes, so optimize that case. */
	if (words_to_transfer) {
		if (words_to_transfer > tx_fifo_avail)
			words_to_transfer = tx_fifo_avail;

		/*
		 * Update state before writing to FIFO.  If this casues us
		 * to finish writing all bytes (AKA buf_remaining goes to 0) we
		 * have a potential for an interrupt (PACKET_XFER_COMPLETE is
		 * not maskable).  We need to make sure that the isr sees
		 * buf_remaining as 0 and doesn't call us back re-entrantly.
		 */
		buf_remaining -= words_to_transfer * BYTES_PER_FIFO_WORD;
		tx_fifo_avail -= words_to_transfer;
		i2c_dev->msg_buf_remaining = buf_remaining;
		i2c_dev->msg_buf = buf +
			words_to_transfer * BYTES_PER_FIFO_WORD;
		barrier();

		i2c_writesl(i2c_dev, buf, I2C_TX_FIFO, words_to_transfer);

		buf += words_to_transfer * BYTES_PER_FIFO_WORD;
	}

	/*
	 * If there is a partial word at the end of buf, handle it manually to
	 * prevent reading past the end of buf, which could cross a page
	 * boundary and fault.
	 */
	if (tx_fifo_avail > 0 && buf_remaining > 0) {
		BUG_ON(buf_remaining > 3);
		memcpy(&val, buf, buf_remaining);

		/* Again update before writing to FIFO to make sure isr sees. */
		i2c_dev->msg_buf_remaining = 0;
		i2c_dev->msg_buf = NULL;
		barrier();

		i2c_writel(i2c_dev, val, I2C_TX_FIFO);
	}

	return 0;
}

/*
 * One of the Tegra I2C blocks is inside the DVC (Digital Voltage Controller)
 * block.  This block is identical to the rest of the I2C blocks, except that
 * it only supports master mode, it has registers moved around, and it needs
 * some extra init to get it into I2C mode.  The register moves are handled
 * by i2c_readl and i2c_writel
 */
static void tegra_dvc_init(struct tegra_i2c_dev *i2c_dev)
{
	u32 val = 0;
	val = dvc_readl(i2c_dev, DVC_CTRL_REG3);
	val |= DVC_CTRL_REG3_SW_PROG;
	val |= DVC_CTRL_REG3_I2C_DONE_INTR_EN;
	dvc_writel(i2c_dev, val, DVC_CTRL_REG3);

	val = dvc_readl(i2c_dev, DVC_CTRL_REG1);
	val |= DVC_CTRL_REG1_INTR_EN;
	dvc_writel(i2c_dev, val, DVC_CTRL_REG1);
}

static void tegra_i2c_slave_init(struct tegra_i2c_dev *i2c_dev)
{
	u32 val = i2c_readl(i2c_dev, I2C_SL_CNFG);

	val |= I2C_SL_CNFG_NEWSL | I2C_SL_CNFG_NACK;
	i2c_writel(i2c_dev, val, I2C_SL_CNFG);

	if (i2c_dev->slave_addr) {
		u16 addr = i2c_dev->slave_addr;

		i2c_writel(i2c_dev, SL_ADDR1(addr), I2C_SL_ADDR1);
		i2c_writel(i2c_dev, SL_ADDR2(addr), I2C_SL_ADDR2);
	}
}

static int tegra_i2c_init(struct tegra_i2c_dev *i2c_dev)
{
	u32 val;
	int err = 0;

	clk_enable(i2c_dev->clk);

	tegra_periph_reset_assert(i2c_dev->clk);
	udelay(2);
	tegra_periph_reset_deassert(i2c_dev->clk);

	if (i2c_dev->is_dvc)
		tegra_dvc_init(i2c_dev);

	val = I2C_CNFG_NEW_MASTER_FSM | I2C_CNFG_PACKET_MODE_EN | (0x2 << I2C_CNFG_DEBOUNCE_CNT_SHIFT);
	i2c_writel(i2c_dev, val, I2C_CNFG);
	i2c_writel(i2c_dev, 0, I2C_INT_MASK);
	tegra_i2c_set_clk(i2c_dev, i2c_dev->last_bus_clk);

	if (!i2c_dev->is_dvc)
		tegra_i2c_slave_init(i2c_dev);

	val = 7 << I2C_FIFO_CONTROL_TX_TRIG_SHIFT |
		0 << I2C_FIFO_CONTROL_RX_TRIG_SHIFT;
	i2c_writel(i2c_dev, val, I2C_FIFO_CONTROL);

	if (tegra_i2c_flush_fifos(i2c_dev))
		err = -ETIMEDOUT;

	clk_disable(i2c_dev->clk);

	if (i2c_dev->irq_disabled) {
		i2c_dev->irq_disabled = 0;
		enable_irq(i2c_dev->irq);
	}

	return err;
}

static irqreturn_t tegra_i2c_isr(int irq, void *dev_id)
{
	u32 status;
	const u32 status_err = I2C_INT_NO_ACK | I2C_INT_ARBITRATION_LOST;
	struct tegra_i2c_dev *i2c_dev = dev_id;

	status = i2c_readl(i2c_dev, I2C_INT_STATUS);

	if (status == 0) {
		dev_warn(i2c_dev->dev, "irq status 0 %08x %08x %08x\n",
			 i2c_readl(i2c_dev, I2C_PACKET_TRANSFER_STATUS),
			 i2c_readl(i2c_dev, I2C_STATUS),
			 i2c_readl(i2c_dev, I2C_CNFG));
		i2c_dev->msg_err |= I2C_ERR_UNKNOWN_INTERRUPT;

		if (!i2c_dev->irq_disabled) {
			disable_irq_nosync(i2c_dev->irq);
			i2c_dev->irq_disabled = 1;
		}

		complete(&i2c_dev->msg_complete);
		goto err;
	}

	if (unlikely(status & status_err)) {
		if (status & I2C_INT_NO_ACK)
			i2c_dev->msg_err |= I2C_ERR_NO_ACK;
		if (status & I2C_INT_ARBITRATION_LOST)
			i2c_dev->msg_err |= I2C_ERR_ARBITRATION_LOST;
		complete(&i2c_dev->msg_complete);
		goto err;
	}

	if ((i2c_readl(i2c_dev, I2C_STATUS) & I2C_STATUS_BUSY)
	    && (status == I2C_INT_TX_FIFO_DATA_REQ)
	    && i2c_dev->msg_read
	    && i2c_dev->msg_buf_remaining) {
		i2c_dev->msg_err |= I2C_ERR_UNEXPECTED_STATUS;

		if (!i2c_dev->irq_disabled) {
			disable_irq_nosync(i2c_dev->irq);
			i2c_dev->irq_disabled = 1;
		}

		complete(&i2c_dev->msg_complete);
		goto err;
	}

	if (i2c_dev->msg_read && (status & I2C_INT_RX_FIFO_DATA_REQ)) {
		if (i2c_dev->msg_buf_remaining)
			tegra_i2c_empty_rx_fifo(i2c_dev);
		else
			BUG();
	}

	if (!i2c_dev->msg_read && (status & I2C_INT_TX_FIFO_DATA_REQ)) {
		if (i2c_dev->msg_buf_remaining)
			tegra_i2c_fill_tx_fifo(i2c_dev);
		else
			tegra_i2c_mask_irq(i2c_dev, I2C_INT_TX_FIFO_DATA_REQ);
	}

	if (status & I2C_INT_PACKET_XFER_COMPLETE) {
		BUG_ON(i2c_dev->msg_buf_remaining);
		complete(&i2c_dev->msg_complete);
	}

	i2c_writel(i2c_dev, status, I2C_INT_STATUS);
	if (i2c_dev->is_dvc)
		dvc_writel(i2c_dev, DVC_STATUS_I2C_DONE_INTR, DVC_STATUS);
	return IRQ_HANDLED;
err:
	/* An error occured, mask all interrupts */
	tegra_i2c_mask_irq(i2c_dev, I2C_INT_NO_ACK | I2C_INT_ARBITRATION_LOST |
		I2C_INT_PACKET_XFER_COMPLETE | I2C_INT_TX_FIFO_DATA_REQ |
		I2C_INT_RX_FIFO_DATA_REQ);
	i2c_writel(i2c_dev, status, I2C_INT_STATUS);
	if (i2c_dev->is_dvc)
		dvc_writel(i2c_dev, DVC_STATUS_I2C_DONE_INTR, DVC_STATUS);
	return IRQ_HANDLED;
}

static int tegra_i2c_xfer_msg(struct tegra_i2c_bus *i2c_bus,
	struct i2c_msg *msg, int stop)
{
	struct tegra_i2c_dev *i2c_dev = i2c_bus->dev;
	u32 packet_header;
	u32 int_mask;
	int ret;

	tegra_i2c_flush_fifos(i2c_dev);
	i2c_writel(i2c_dev, 0xFF, I2C_INT_STATUS);

	if (msg->len == 0)
		return -EINVAL;

	i2c_dev->msg_buf = msg->buf;
	i2c_dev->msg_buf_remaining = msg->len;
	i2c_dev->msg_err = I2C_ERR_NONE;
	i2c_dev->msg_transfer_complete = 0;
	i2c_dev->msg_read = (msg->flags & I2C_M_RD);
	INIT_COMPLETION(i2c_dev->msg_complete);

	packet_header = (0 << PACKET_HEADER0_HEADER_SIZE_SHIFT) |
			PACKET_HEADER0_PROTOCOL_I2C |
			(i2c_dev->cont_id << PACKET_HEADER0_CONT_ID_SHIFT) |
			(1 << PACKET_HEADER0_PACKET_ID_SHIFT);
	i2c_writel(i2c_dev, packet_header, I2C_TX_FIFO);

	packet_header = msg->len - 1;
	i2c_writel(i2c_dev, packet_header, I2C_TX_FIFO);

	packet_header = msg->addr << I2C_HEADER_SLAVE_ADDR_SHIFT;
	packet_header |= I2C_HEADER_IE_ENABLE;
	if (!stop)
		packet_header |= I2C_HEADER_REPEAT_START;
	if (msg->flags & I2C_M_TEN)
		packet_header |= I2C_HEADER_10BIT_ADDR;
	if (msg->flags & I2C_M_IGNORE_NAK)
		packet_header |= I2C_HEADER_CONT_ON_NAK;
	if (msg->flags & I2C_M_RD)
		packet_header |= I2C_HEADER_READ;
	i2c_writel(i2c_dev, packet_header, I2C_TX_FIFO);

	if (!(msg->flags & I2C_M_RD))
		tegra_i2c_fill_tx_fifo(i2c_dev);

	int_mask = I2C_INT_NO_ACK | I2C_INT_ARBITRATION_LOST;
	if (msg->flags & I2C_M_RD)
		int_mask |= I2C_INT_RX_FIFO_DATA_REQ;
	else if (i2c_dev->msg_buf_remaining)
		int_mask |= I2C_INT_TX_FIFO_DATA_REQ;
	tegra_i2c_unmask_irq(i2c_dev, int_mask);
	dev_dbg(i2c_dev->dev, "unmasked irq: %02x\n",
		i2c_readl(i2c_dev, I2C_INT_MASK));

	ret = wait_for_completion_timeout(&i2c_dev->msg_complete, TEGRA_I2C_TIMEOUT);
	tegra_i2c_mask_irq(i2c_dev, int_mask);

	if (WARN_ON(ret == 0)) {
		dev_err(i2c_dev->dev, "i2c transfer timed out\n");

		tegra_i2c_init(i2c_dev);
		return -ETIMEDOUT;
	}

	dev_dbg(i2c_dev->dev, "transfer complete: %d %d %d\n",
		ret, completion_done(&i2c_dev->msg_complete), i2c_dev->msg_err);

	if (likely(i2c_dev->msg_err == I2C_ERR_NONE))
		return 0;

	tegra_i2c_init(i2c_dev);
	if (i2c_dev->msg_err == I2C_ERR_NO_ACK) {
		if (msg->flags & I2C_M_IGNORE_NAK)
			return 0;
		return -EAGAIN;
	}

	if (i2c_dev->msg_err & I2C_ERR_UNEXPECTED_STATUS)
		return -EAGAIN;

	return -EIO;
}

static int tegra_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[],
	int num)
{
	struct tegra_i2c_bus *i2c_bus = i2c_get_adapdata(adap);
	struct tegra_i2c_dev *i2c_dev = i2c_bus->dev;
	int i;
	int ret = 0;

	if (i2c_dev->is_suspended)
		return -EBUSY;

	rt_mutex_lock(&i2c_dev->dev_lock);

	if (i2c_dev->last_mux != i2c_bus->mux) {
		tegra_pinmux_set_safe_pinmux_table(i2c_dev->last_mux,
						   i2c_dev->last_mux_len);
		tegra_pinmux_config_pinmux_table(i2c_bus->mux,
						 i2c_bus->mux_len);
		i2c_dev->last_mux = i2c_bus->mux;
		i2c_dev->last_mux_len = i2c_bus->mux_len;
	}

	if (i2c_dev->last_bus_clk != i2c_bus->bus_clk_rate) {
		tegra_i2c_set_clk(i2c_dev, i2c_bus->bus_clk_rate);
		i2c_dev->last_bus_clk = i2c_bus->bus_clk_rate;
	}

	clk_enable(i2c_dev->clk);
	for (i = 0; i < num; i++) {
		int stop = (i == (num - 1)) ? 1  : 0;
		ret = tegra_i2c_xfer_msg(i2c_bus, &msgs[i], stop);
		if (ret)
			break;
	}
	clk_disable(i2c_dev->clk);

	rt_mutex_unlock(&i2c_dev->dev_lock);

	return ret ?: i;
}

static u32 tegra_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm tegra_i2c_algo = {
	.master_xfer	= tegra_i2c_xfer,
	.functionality	= tegra_i2c_func,
};

static int tegra_i2c_probe(struct platform_device *pdev)
{
	struct tegra_i2c_dev *i2c_dev;
	struct tegra_i2c_platform_data *pdata = pdev->dev.platform_data;
	struct resource *res;
	struct resource *iomem;
	struct clk *clk;
	struct clk *i2c_clk;
	void *base;
	int irq;
	int nbus;
	int i = 0;
	int ret = 0;

	if (!pdata) {
		dev_err(&pdev->dev, "no platform data?\n");
		return -ENODEV;
	}


	if (pdata->bus_count <= 0 || pdata->adapter_nr < 0) {
		dev_err(&pdev->dev, "invalid platform data?\n");
		return -ENODEV;
	}

	WARN_ON(pdata->bus_count > TEGRA_I2C_MAX_BUS);
	nbus = min(TEGRA_I2C_MAX_BUS, pdata->bus_count);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no mem resource\n");
		return -EINVAL;
	}
	iomem = request_mem_region(res->start, resource_size(res), pdev->name);
	if (!iomem) {
		dev_err(&pdev->dev, "I2C region already claimed\n");
		return -EBUSY;
	}

	base = ioremap(iomem->start, resource_size(iomem));
	if (!base) {
		dev_err(&pdev->dev, "Cannot ioremap I2C region\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "no irq resource\n");
		ret = -EINVAL;
		goto err_iounmap;
	}
	irq = res->start;

	clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "missing controller clock");
		ret = PTR_ERR(clk);
		goto err_release_region;
	}

	i2c_clk = clk_get(&pdev->dev, "i2c");
	if (IS_ERR(i2c_clk)) {
		dev_err(&pdev->dev, "missing bus clock");
		ret = PTR_ERR(i2c_clk);
		goto err_clk_put;
	}

	i2c_dev = kzalloc(sizeof(struct tegra_i2c_dev) +
			  (nbus-1) * sizeof(struct tegra_i2c_bus), GFP_KERNEL);

	if (!i2c_dev) {
		ret = -ENOMEM;
		goto err_i2c_clk_put;
	}

	i2c_dev->base = base;
	i2c_dev->clk = clk;
	i2c_dev->i2c_clk = i2c_clk;
	i2c_dev->iomem = iomem;
	i2c_dev->irq = irq;
	i2c_dev->cont_id = pdev->id;
	i2c_dev->dev = &pdev->dev;
	i2c_dev->last_bus_clk = pdata->bus_clk_rate[0] ?: 100000;
	rt_mutex_init(&i2c_dev->dev_lock);

	i2c_dev->slave_addr = pdata->slave_addr;
	i2c_dev->is_dvc = pdata->is_dvc;
	init_completion(&i2c_dev->msg_complete);

	platform_set_drvdata(pdev, i2c_dev);

	ret = tegra_i2c_init(i2c_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize i2c controller");
		goto err_free;
	}

	ret = request_irq(i2c_dev->irq, tegra_i2c_isr, 0, pdev->name, i2c_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq %i\n", i2c_dev->irq);
		goto err_free;
	}

	clk_enable(i2c_dev->i2c_clk);

	for (i = 0; i < nbus; i++) {
		struct tegra_i2c_bus *i2c_bus = &i2c_dev->busses[i];

		i2c_bus->dev = i2c_dev;
		i2c_bus->mux = pdata->bus_mux[i];
		i2c_bus->mux_len = pdata->bus_mux_len[i];
		i2c_bus->bus_clk_rate = pdata->bus_clk_rate[i] ?: 100000;

		i2c_bus->adapter.algo = &tegra_i2c_algo;
		i2c_set_adapdata(&i2c_bus->adapter, i2c_bus);
		i2c_bus->adapter.owner = THIS_MODULE;
		i2c_bus->adapter.class = I2C_CLASS_HWMON;
		strlcpy(i2c_bus->adapter.name, "Tegra I2C adapter",
			sizeof(i2c_bus->adapter.name));
		i2c_bus->adapter.dev.parent = &pdev->dev;
		i2c_bus->adapter.nr = pdata->adapter_nr + i;
		i2c_bus->adapter.retries = TEGRA_I2C_RETRIES;

		ret = i2c_add_numbered_adapter(&i2c_bus->adapter);
		if (ret) {
			dev_err(&pdev->dev, "Failed to add I2C adapter\n");
			goto err_del_bus;
		}
		i2c_dev->bus_count++;
	}

	return 0;
err_del_bus:
	while (i2c_dev->bus_count--)
		i2c_del_adapter(&i2c_dev->busses[i2c_dev->bus_count].adapter);
	free_irq(i2c_dev->irq, i2c_dev);
err_free:
	kfree(i2c_dev);
err_i2c_clk_put:
	clk_put(i2c_clk);
err_clk_put:
	clk_put(clk);
err_release_region:
	release_mem_region(iomem->start, resource_size(iomem));
err_iounmap:
	iounmap(base);
	return ret;
}

static int tegra_i2c_remove(struct platform_device *pdev)
{
	struct tegra_i2c_dev *i2c_dev = platform_get_drvdata(pdev);
	while (i2c_dev->bus_count--)
		i2c_del_adapter(&i2c_dev->busses[i2c_dev->bus_count].adapter);

	free_irq(i2c_dev->irq, i2c_dev);
	clk_put(i2c_dev->i2c_clk);
	clk_put(i2c_dev->clk);
	release_mem_region(i2c_dev->iomem->start,
		resource_size(i2c_dev->iomem));
	iounmap(i2c_dev->base);
	kfree(i2c_dev);
	return 0;
}

#ifdef CONFIG_PM
static int tegra_i2c_suspend_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct tegra_i2c_dev *i2c_dev = platform_get_drvdata(pdev);

	rt_mutex_lock(&i2c_dev->dev_lock);
	i2c_dev->is_suspended = true;
	rt_mutex_unlock(&i2c_dev->dev_lock);

	return 0;
}

static int tegra_i2c_resume_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct tegra_i2c_dev *i2c_dev = platform_get_drvdata(pdev);
	int ret;

	rt_mutex_lock(&i2c_dev->dev_lock);

	ret = tegra_i2c_init(i2c_dev);

	if (ret) {
		rt_mutex_unlock(&i2c_dev->dev_lock);
		return ret;
	}

	i2c_dev->is_suspended = false;

	rt_mutex_unlock(&i2c_dev->dev_lock);

	return 0;
}

static const struct dev_pm_ops tegra_i2c_dev_pm_ops = {
	.suspend_noirq = tegra_i2c_suspend_noirq,
	.resume_noirq = tegra_i2c_resume_noirq,
};
#define TEGRA_I2C_DEV_PM_OPS (&tegra_i2c_dev_pm_ops)
#else
#define TEGRA_I2C_DEV_PM_OPS NULL
#endif

static struct platform_driver tegra_i2c_driver = {
	.probe   = tegra_i2c_probe,
	.remove  = tegra_i2c_remove,
	.driver  = {
		.name  = "tegra-i2c",
		.owner = THIS_MODULE,
		.pm    = TEGRA_I2C_DEV_PM_OPS,
	},
};

static int __init tegra_i2c_init_driver(void)
{
	return platform_driver_register(&tegra_i2c_driver);
}

static void __exit tegra_i2c_exit_driver(void)
{
	platform_driver_unregister(&tegra_i2c_driver);
}

subsys_initcall(tegra_i2c_init_driver);
module_exit(tegra_i2c_exit_driver);

MODULE_DESCRIPTION("nVidia Tegra2 I2C Bus Controller driver");
MODULE_AUTHOR("Colin Cross");
MODULE_LICENSE("GPL v2");
