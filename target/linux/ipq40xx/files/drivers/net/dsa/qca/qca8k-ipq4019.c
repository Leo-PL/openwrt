// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2009 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2011-2012, 2020-2021 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (c) 2015, 2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2016 John Crispin <john@phrozen.org>
 * Copyright (c) 2021 Robert Marko <robert.marko@sartura.hr>
 */

#include <linux/clk.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/mdio.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_platform.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <linux/reset.h>
#include <net/dsa.h>

#include "qca8k-ipq4019.h"

#define MIB_DESC(_s, _o, _n)	\
	{			\
		.size = (_s),	\
		.offset = (_o),	\
		.name = (_n),	\
	}

static const struct qca8k_mib_desc ar8327_mib[] = {
	MIB_DESC(1, 0x00, "RxBroad"),
	MIB_DESC(1, 0x04, "RxPause"),
	MIB_DESC(1, 0x08, "RxMulti"),
	MIB_DESC(1, 0x0c, "RxFcsErr"),
	MIB_DESC(1, 0x10, "RxAlignErr"),
	MIB_DESC(1, 0x14, "RxRunt"),
	MIB_DESC(1, 0x18, "RxFragment"),
	MIB_DESC(1, 0x1c, "Rx64Byte"),
	MIB_DESC(1, 0x20, "Rx128Byte"),
	MIB_DESC(1, 0x24, "Rx256Byte"),
	MIB_DESC(1, 0x28, "Rx512Byte"),
	MIB_DESC(1, 0x2c, "Rx1024Byte"),
	MIB_DESC(1, 0x30, "Rx1518Byte"),
	MIB_DESC(1, 0x34, "RxMaxByte"),
	MIB_DESC(1, 0x38, "RxTooLong"),
	MIB_DESC(2, 0x3c, "RxGoodByte"),
	MIB_DESC(2, 0x44, "RxBadByte"),
	MIB_DESC(1, 0x4c, "RxOverFlow"),
	MIB_DESC(1, 0x50, "Filtered"),
	MIB_DESC(1, 0x54, "TxBroad"),
	MIB_DESC(1, 0x58, "TxPause"),
	MIB_DESC(1, 0x5c, "TxMulti"),
	MIB_DESC(1, 0x60, "TxUnderRun"),
	MIB_DESC(1, 0x64, "Tx64Byte"),
	MIB_DESC(1, 0x68, "Tx128Byte"),
	MIB_DESC(1, 0x6c, "Tx256Byte"),
	MIB_DESC(1, 0x70, "Tx512Byte"),
	MIB_DESC(1, 0x74, "Tx1024Byte"),
	MIB_DESC(1, 0x78, "Tx1518Byte"),
	MIB_DESC(1, 0x7c, "TxMaxByte"),
	MIB_DESC(1, 0x80, "TxOverSize"),
	MIB_DESC(2, 0x84, "TxByte"),
	MIB_DESC(1, 0x8c, "TxCollision"),
	MIB_DESC(1, 0x90, "TxAbortCol"),
	MIB_DESC(1, 0x94, "TxMultiCol"),
	MIB_DESC(1, 0x98, "TxSingleCol"),
	MIB_DESC(1, 0x9c, "TxExcDefer"),
	MIB_DESC(1, 0xa0, "TxDefer"),
	MIB_DESC(1, 0xa4, "TxLateCol"),
	MIB_DESC(1, 0xa8, "RXUnicast"),
	MIB_DESC(1, 0xac, "TXunicast"),
};

static int
qca8k_read(struct qca8k_priv *priv, u32 reg, u32 *val)
{
	return regmap_read(priv->regmap, reg, val);
}

static int
qca8k_write(struct qca8k_priv *priv, u32 reg, u32 val)
{
	return regmap_write(priv->regmap, reg, val);
}

static int
qca8k_rmw(struct qca8k_priv *priv, u32 reg, u32 mask, u32 write_val)
{
	return regmap_update_bits(priv->regmap, reg, mask, write_val);
}

static int
qca8k_reg_set(struct qca8k_priv *priv, u32 reg, u32 val)
{
	return regmap_set_bits(priv->regmap, reg, val);
}

static int
qca8k_reg_clear(struct qca8k_priv *priv, u32 reg, u32 val)
{
	return regmap_clear_bits(priv->regmap, reg, val);
}

static const struct regmap_range qca8k_readable_ranges[] = {
	regmap_reg_range(0x0000, 0x00e4), /* Global control */
	regmap_reg_range(0x0100, 0x0168), /* EEE control */
	regmap_reg_range(0x0200, 0x0270), /* Parser control */
	regmap_reg_range(0x0400, 0x0454), /* ACL */
	regmap_reg_range(0x0600, 0x0718), /* Lookup */
	regmap_reg_range(0x0800, 0x0b70), /* QM */
	regmap_reg_range(0x0c00, 0x0c80), /* PKT */
	regmap_reg_range(0x0e00, 0x0e98), /* L3 */
	regmap_reg_range(0x1000, 0x10ac), /* MIB - Port0 */
	regmap_reg_range(0x1100, 0x11ac), /* MIB - Port1 */
	regmap_reg_range(0x1200, 0x12ac), /* MIB - Port2 */
	regmap_reg_range(0x1300, 0x13ac), /* MIB - Port3 */
	regmap_reg_range(0x1400, 0x14ac), /* MIB - Port4 */
	regmap_reg_range(0x1500, 0x15ac), /* MIB - Port5 */
	regmap_reg_range(0x1600, 0x16ac), /* MIB - Port6 */

};

static const struct regmap_access_table qca8k_readable_table = {
	.yes_ranges = qca8k_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(qca8k_readable_ranges),
};

static struct regmap_config qca8k_ipq4019_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x16ac, /* end MIB - Port6 range */
	.rd_table = &qca8k_readable_table,
};

static struct regmap_config qca8k_ipq4019_psgmii_phy_regmap_config = {
	.name = "psgmii-phy",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x7fc,
};

static int
qca8k_busy_wait(struct qca8k_priv *priv, u32 reg, u32 mask)
{
	u32 val;

	return regmap_read_poll_timeout(priv->regmap, reg, val,
					!(val & mask),
					0,
					QCA8K_BUSY_WAIT_TIMEOUT);
}

static int
qca8k_fdb_read(struct qca8k_priv *priv, struct qca8k_fdb *fdb)
{
	u32 reg[4], val;
	int i, ret;

	/* load the ARL table into an array */
	for (i = 0; i < 4; i++) {
		ret = qca8k_read(priv, QCA8K_REG_ATU_DATA0 + (i * 4), &val);
		if (ret < 0)
			return ret;

		reg[i] = val;
	}

	/* vid - 83:72 */
	fdb->vid = (reg[2] >> QCA8K_ATU_VID_S) & QCA8K_ATU_VID_M;
	/* aging - 67:64 */
	fdb->aging = reg[2] & QCA8K_ATU_STATUS_M;
	/* portmask - 54:48 */
	fdb->port_mask = (reg[1] >> QCA8K_ATU_PORT_S) & QCA8K_ATU_PORT_M;
	/* mac - 47:0 */
	fdb->mac[0] = (reg[1] >> QCA8K_ATU_ADDR0_S) & 0xff;
	fdb->mac[1] = reg[1] & 0xff;
	fdb->mac[2] = (reg[0] >> QCA8K_ATU_ADDR2_S) & 0xff;
	fdb->mac[3] = (reg[0] >> QCA8K_ATU_ADDR3_S) & 0xff;
	fdb->mac[4] = (reg[0] >> QCA8K_ATU_ADDR4_S) & 0xff;
	fdb->mac[5] = reg[0] & 0xff;

	return 0;
}

static void
qca8k_fdb_write(struct qca8k_priv *priv, u16 vid, u8 port_mask, const u8 *mac,
		u8 aging)
{
	u32 reg[3] = { 0 };
	int i;

	/* vid - 83:72 */
	reg[2] = (vid & QCA8K_ATU_VID_M) << QCA8K_ATU_VID_S;
	/* aging - 67:64 */
	reg[2] |= aging & QCA8K_ATU_STATUS_M;
	/* portmask - 54:48 */
	reg[1] = (port_mask & QCA8K_ATU_PORT_M) << QCA8K_ATU_PORT_S;
	/* mac - 47:0 */
	reg[1] |= mac[0] << QCA8K_ATU_ADDR0_S;
	reg[1] |= mac[1];
	reg[0] |= mac[2] << QCA8K_ATU_ADDR2_S;
	reg[0] |= mac[3] << QCA8K_ATU_ADDR3_S;
	reg[0] |= mac[4] << QCA8K_ATU_ADDR4_S;
	reg[0] |= mac[5];

	/* load the array into the ARL table */
	for (i = 0; i < 3; i++)
		qca8k_write(priv, QCA8K_REG_ATU_DATA0 + (i * 4), reg[i]);
}

static int
qca8k_fdb_access(struct qca8k_priv *priv, enum qca8k_fdb_cmd cmd, int port)
{
	u32 reg;
	int ret;

	/* Set the command and FDB index */
	reg = QCA8K_ATU_FUNC_BUSY;
	reg |= cmd;
	if (port >= 0) {
		reg |= QCA8K_ATU_FUNC_PORT_EN;
		reg |= (port & QCA8K_ATU_FUNC_PORT_M) << QCA8K_ATU_FUNC_PORT_S;
	}

	/* Write the function register triggering the table access */
	ret = qca8k_write(priv, QCA8K_REG_ATU_FUNC, reg);
	if (ret)
		return ret;

	/* wait for completion */
	ret = qca8k_busy_wait(priv, QCA8K_REG_ATU_FUNC, QCA8K_ATU_FUNC_BUSY);
	if (ret)
		return ret;

	/* Check for table full violation when adding an entry */
	if (cmd == QCA8K_FDB_LOAD) {
		ret = qca8k_read(priv, QCA8K_REG_ATU_FUNC, &reg);
		if (ret < 0)
			return ret;
		if (reg & QCA8K_ATU_FUNC_FULL)
			return -1;
	}

	return 0;
}

static int
qca8k_fdb_next(struct qca8k_priv *priv, struct qca8k_fdb *fdb, int port)
{
	int ret;

	qca8k_fdb_write(priv, fdb->vid, fdb->port_mask, fdb->mac, fdb->aging);
	ret = qca8k_fdb_access(priv, QCA8K_FDB_NEXT, port);
	if (ret < 0)
		return ret;

	return qca8k_fdb_read(priv, fdb);
}

static int
qca8k_fdb_add(struct qca8k_priv *priv, const u8 *mac, u16 port_mask,
	      u16 vid, u8 aging)
{
	int ret;

	mutex_lock(&priv->reg_mutex);
	qca8k_fdb_write(priv, vid, port_mask, mac, aging);
	ret = qca8k_fdb_access(priv, QCA8K_FDB_LOAD, -1);
	mutex_unlock(&priv->reg_mutex);

	return ret;
}

static int
qca8k_fdb_del(struct qca8k_priv *priv, const u8 *mac, u16 port_mask, u16 vid)
{
	int ret;

	mutex_lock(&priv->reg_mutex);
	qca8k_fdb_write(priv, vid, port_mask, mac, 0);
	ret = qca8k_fdb_access(priv, QCA8K_FDB_PURGE, -1);
	mutex_unlock(&priv->reg_mutex);

	return ret;
}

static void
qca8k_fdb_flush(struct qca8k_priv *priv)
{
	mutex_lock(&priv->reg_mutex);
	qca8k_fdb_access(priv, QCA8K_FDB_FLUSH, -1);
	mutex_unlock(&priv->reg_mutex);
}

static int
qca8k_vlan_access(struct qca8k_priv *priv, enum qca8k_vlan_cmd cmd, u16 vid)
{
	u32 reg;
	int ret;

	/* Set the command and VLAN index */
	reg = QCA8K_VTU_FUNC1_BUSY;
	reg |= cmd;
	reg |= vid << QCA8K_VTU_FUNC1_VID_S;

	/* Write the function register triggering the table access */
	ret = qca8k_write(priv, QCA8K_REG_VTU_FUNC1, reg);
	if (ret)
		return ret;

	/* wait for completion */
	ret = qca8k_busy_wait(priv, QCA8K_REG_VTU_FUNC1, QCA8K_VTU_FUNC1_BUSY);
	if (ret)
		return ret;

	/* Check for table full violation when adding an entry */
	if (cmd == QCA8K_VLAN_LOAD) {
		ret = qca8k_read(priv, QCA8K_REG_VTU_FUNC1, &reg);
		if (ret < 0)
			return ret;
		if (reg & QCA8K_VTU_FUNC1_FULL)
			return -ENOMEM;
	}

	return 0;
}

static int
qca8k_vlan_add(struct qca8k_priv *priv, u8 port, u16 vid, bool untagged)
{
	u32 reg;
	int ret;

	/*
	   We do the right thing with VLAN 0 and treat it as untagged while
	   preserving the tag on egress.
	 */
	if (vid == 0)
		return 0;

	mutex_lock(&priv->reg_mutex);
	ret = qca8k_vlan_access(priv, QCA8K_VLAN_READ, vid);
	if (ret < 0)
		goto out;

	ret = qca8k_read(priv, QCA8K_REG_VTU_FUNC0, &reg);
	if (ret < 0)
		goto out;
	reg |= QCA8K_VTU_FUNC0_VALID | QCA8K_VTU_FUNC0_IVL_EN;
	reg &= ~(QCA8K_VTU_FUNC0_EG_MODE_MASK << QCA8K_VTU_FUNC0_EG_MODE_S(port));
	if (untagged)
		reg |= QCA8K_VTU_FUNC0_EG_MODE_UNTAG <<
				QCA8K_VTU_FUNC0_EG_MODE_S(port);
	else
		reg |= QCA8K_VTU_FUNC0_EG_MODE_TAG <<
				QCA8K_VTU_FUNC0_EG_MODE_S(port);

	ret = qca8k_write(priv, QCA8K_REG_VTU_FUNC0, reg);
	if (ret)
		goto out;
	ret = qca8k_vlan_access(priv, QCA8K_VLAN_LOAD, vid);

out:
	mutex_unlock(&priv->reg_mutex);

	return ret;
}

static int
qca8k_vlan_del(struct qca8k_priv *priv, u8 port, u16 vid)
{
	u32 reg, mask;
	int ret, i;
	bool del;

	mutex_lock(&priv->reg_mutex);
	ret = qca8k_vlan_access(priv, QCA8K_VLAN_READ, vid);
	if (ret < 0)
		goto out;

	ret = qca8k_read(priv, QCA8K_REG_VTU_FUNC0, &reg);
	if (ret < 0)
		goto out;
	reg &= ~(3 << QCA8K_VTU_FUNC0_EG_MODE_S(port));
	reg |= QCA8K_VTU_FUNC0_EG_MODE_NOT <<
			QCA8K_VTU_FUNC0_EG_MODE_S(port);

	/* Check if we're the last member to be removed */
	del = true;
	for (i = 0; i < QCA8K_NUM_PORTS; i++) {
		mask = QCA8K_VTU_FUNC0_EG_MODE_NOT;
		mask <<= QCA8K_VTU_FUNC0_EG_MODE_S(i);

		if ((reg & mask) != mask) {
			del = false;
			break;
		}
	}

	if (del) {
		ret = qca8k_vlan_access(priv, QCA8K_VLAN_PURGE, vid);
	} else {
		ret = qca8k_write(priv, QCA8K_REG_VTU_FUNC0, reg);
		if (ret)
			goto out;
		ret = qca8k_vlan_access(priv, QCA8K_VLAN_LOAD, vid);
	}

out:
	mutex_unlock(&priv->reg_mutex);

	return ret;
}

static int
qca8k_mib_init(struct qca8k_priv *priv)
{
	int ret;

	mutex_lock(&priv->reg_mutex);
	ret = qca8k_reg_set(priv, QCA8K_REG_MIB, QCA8K_MIB_FLUSH | QCA8K_MIB_BUSY);
	if (ret)
		goto exit;

	ret = qca8k_busy_wait(priv, QCA8K_REG_MIB, QCA8K_MIB_BUSY);
	if (ret)
		goto exit;

	ret = qca8k_reg_set(priv, QCA8K_REG_MIB, QCA8K_MIB_CPU_KEEP);
	if (ret)
		goto exit;

	ret = qca8k_write(priv, QCA8K_REG_MODULE_EN, QCA8K_MODULE_EN_MIB);

exit:
	mutex_unlock(&priv->reg_mutex);
	return ret;
}

static void
qca8k_port_set_status(struct qca8k_priv *priv, int port, int enable)
{
	u32 mask = QCA8K_PORT_STATUS_TXMAC | QCA8K_PORT_STATUS_RXMAC;

	/* Port 0 is internally connected to the CPU
	 * TODO: Probably check for RGMII as well if it doesnt work
	 * in RGMII mode.
	 */
	if (port > QCA8K_CPU_PORT)
		mask |= QCA8K_PORT_STATUS_LINK_AUTO;

	if (enable)
		qca8k_reg_set(priv, QCA8K_REG_PORT_STATUS(port), mask);
	else
		qca8k_reg_clear(priv, QCA8K_REG_PORT_STATUS(port), mask);
}

static int
qca8k_setup_port(struct dsa_switch *ds, int port)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	int ret;

	/* CPU port gets connected to all user ports of the switch */
	if (dsa_is_cpu_port(ds, port)) {
		ret = qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(QCA8K_CPU_PORT),
				QCA8K_PORT_LOOKUP_MEMBER, dsa_user_ports(ds));
		if (ret)
			return ret;
	}

	/* Individual user ports get connected to CPU port only */
	if (dsa_is_user_port(ds, port)) {
		int shift = 16 * (port % 2);

		ret = qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(port),
				QCA8K_PORT_LOOKUP_MEMBER,
				BIT(QCA8K_CPU_PORT));
		if (ret)
			return ret;

		/* Enable ARP Auto-learning by default */
		ret = qca8k_reg_set(priv, QCA8K_PORT_LOOKUP_CTRL(port),
				    QCA8K_PORT_LOOKUP_LEARN);
		if (ret)
			return ret;

		/* For port based vlans to work we need to set the
		 * default egress vid
		 */
		ret = qca8k_rmw(priv, QCA8K_EGRESS_VLAN(port),
				0xfff << shift,
				QCA8K_PORT_VID_DEF << shift);
		if (ret)
			return ret;

		ret = qca8k_write(priv, QCA8K_REG_PORT_VLAN_CTRL0(port),
				  QCA8K_PORT_VLAN_CVID(QCA8K_PORT_VID_DEF) |
				  QCA8K_PORT_VLAN_SVID(QCA8K_PORT_VID_DEF));
		if (ret)
			return ret;
	}

	return 0;
}

static int
qca8k_setup(struct dsa_switch *ds)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	int ret, i;

	/* Make sure that port 0 is the cpu port */
	if (!dsa_is_cpu_port(ds, 0)) {
		dev_err(priv->dev, "port 0 is not the CPU port");
		return -EINVAL;
	}

	/* Enable CPU Port */
	ret = qca8k_reg_set(priv, QCA8K_REG_GLOBAL_FW_CTRL0,
			    QCA8K_GLOBAL_FW_CTRL0_CPU_PORT_EN);
	if (ret) {
		dev_err(priv->dev, "failed enabling CPU port");
		return ret;
	}

	/* Enable MIB counters */
	ret = qca8k_mib_init(priv);
	if (ret)
		dev_warn(priv->dev, "MIB init failed");

	/* Enable QCA header mode on the cpu port */
	ret = qca8k_write(priv, QCA8K_REG_PORT_HDR_CTRL(QCA8K_CPU_PORT),
			  QCA8K_PORT_HDR_CTRL_ALL << QCA8K_PORT_HDR_CTRL_TX_S |
			  QCA8K_PORT_HDR_CTRL_ALL << QCA8K_PORT_HDR_CTRL_RX_S);
	if (ret) {
		dev_err(priv->dev, "failed enabling QCA header mode");
		return ret;
	}

	/* Disable forwarding by default on all ports */
	for (i = 0; i < QCA8K_NUM_PORTS; i++) {
		ret = qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(i),
				QCA8K_PORT_LOOKUP_MEMBER, 0);
		if (ret)
			return ret;
	}

	/* Disable MAC by default on all ports */
	for (i = 1; i < QCA8K_NUM_PORTS; i++)
		qca8k_port_set_status(priv, i, 0);

	/* Forward all unknown frames to CPU port for Linux processing */
	ret = qca8k_write(priv, QCA8K_REG_GLOBAL_FW_CTRL1,
			  BIT(QCA8K_CPU_PORT) << QCA8K_GLOBAL_FW_CTRL1_IGMP_DP_S |
			  BIT(QCA8K_CPU_PORT) << QCA8K_GLOBAL_FW_CTRL1_BC_DP_S |
			  BIT(QCA8K_CPU_PORT) << QCA8K_GLOBAL_FW_CTRL1_MC_DP_S |
			  BIT(QCA8K_CPU_PORT) << QCA8K_GLOBAL_FW_CTRL1_UC_DP_S);
	if (ret)
		return ret;

	/* Setup connection between CPU port & user ports */
	for (i = 0; i < QCA8K_NUM_PORTS; i++) {
		ret = qca8k_setup_port(ds, i);
		if (ret)
			return ret;
	}

	/* Setup our port MTUs to match power on defaults */
	for (i = 0; i < QCA8K_NUM_PORTS; i++)
		priv->port_mtu[i] = ETH_FRAME_LEN + ETH_FCS_LEN;
	ret = qca8k_write(priv, QCA8K_MAX_FRAME_SIZE, ETH_FRAME_LEN + ETH_FCS_LEN);
	if (ret)
		dev_warn(priv->dev, "failed setting MTU settings");

	/* Flush the FDB table */
	qca8k_fdb_flush(priv);

	/* We don't have interrupts for link changes, so we need to poll */
	ds->pcs_poll = true;

	return 0;
}

static void
qca8k_phylink_mac_config(struct dsa_switch *ds, int port, unsigned int mode,
			 const struct phylink_link_state *state)
{
	struct qca8k_priv *priv = ds->priv;

	/* Only RGMII configuration here.
	 * TODO: Look into moving PHY calibration here
	 */
	switch (port) {
	case 0:
	case 1:
	case 2:
	case 3:
		/* TODO: Move PSGMII config and calibration here */
		return;
	case 4:
	case 5:
		if (state->interface == PHY_INTERFACE_MODE_RGMII) {
			qca8k_reg_set(priv, QCA8K_REG_RGMII_CTRL, QCA8K_RGMII_CTRL_CLK);
		}
		return;
	default:
		dev_err(ds->dev, "%s: unsupported port: %i\n", __func__, port);
		return;
	}
}

static void
qca8k_phylink_validate(struct dsa_switch *ds, int port,
		       unsigned long *supported,
		       struct phylink_link_state *state)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };

	switch (port) {
	case 0: /* CPU port */
		if (state->interface != PHY_INTERFACE_MODE_INTERNAL)
			goto unsupported;
		break;
	case 1:
	case 2:
	case 3:
		/* Only PSGMII mode is supported */
		if (state->interface != PHY_INTERFACE_MODE_PSGMII)
			goto unsupported;
		break;
	case 4:
	case 5:
		/* PSGMII and RGMII modes are supported */
		if (state->interface != PHY_INTERFACE_MODE_PSGMII &&
		    state->interface != PHY_INTERFACE_MODE_RGMII)
			goto unsupported;
		break;
	default:
unsupported:
		dev_warn(ds->dev, "interface '%s' (%d) on port %d is not supported\n",
			 phy_modes(state->interface), state->interface, port);
		linkmode_zero(supported);
		return;
	}

	if (port == 0) {
		phylink_set_port_modes(mask);

		phylink_set(mask, 1000baseT_Full);

		phylink_set(mask, Pause);
		phylink_set(mask, Asym_Pause);

		linkmode_and(supported, supported, mask);
		linkmode_and(state->advertising, state->advertising, mask);
	} else {
		/* Simply copy what PHYs tell us */
		linkmode_copy(state->advertising, supported);
	}
}

static int
qca8k_phylink_mac_link_state(struct dsa_switch *ds, int port,
			     struct phylink_link_state *state)
{
	struct qca8k_priv *priv = ds->priv;
	u32 reg;
	int ret;

	ret = qca8k_read(priv, QCA8K_REG_PORT_STATUS(port), &reg);
	if (ret < 0)
		return ret;

	state->link = !!(reg & QCA8K_PORT_STATUS_LINK_UP);
	state->an_complete = state->link;
	state->an_enabled = !!(reg & QCA8K_PORT_STATUS_LINK_AUTO);
	state->duplex = (reg & QCA8K_PORT_STATUS_DUPLEX) ? DUPLEX_FULL :
							   DUPLEX_HALF;

	switch (reg & QCA8K_PORT_STATUS_SPEED) {
	case QCA8K_PORT_STATUS_SPEED_10:
		state->speed = SPEED_10;
		break;
	case QCA8K_PORT_STATUS_SPEED_100:
		state->speed = SPEED_100;
		break;
	case QCA8K_PORT_STATUS_SPEED_1000:
		state->speed = SPEED_1000;
		break;
	default:
		state->speed = SPEED_UNKNOWN;
		break;
	}

	state->pause = MLO_PAUSE_NONE;
	if (reg & QCA8K_PORT_STATUS_RXFLOW)
		state->pause |= MLO_PAUSE_RX;
	if (reg & QCA8K_PORT_STATUS_TXFLOW)
		state->pause |= MLO_PAUSE_TX;

	return 1;
}

static void
qca8k_phylink_mac_link_down(struct dsa_switch *ds, int port, unsigned int mode,
			    phy_interface_t interface)
{
	struct qca8k_priv *priv = ds->priv;

	qca8k_port_set_status(priv, port, 0);
}

static void
qca8k_phylink_mac_link_up(struct dsa_switch *ds, int port, unsigned int mode,
			  phy_interface_t interface, struct phy_device *phydev,
			  int speed, int duplex, bool tx_pause, bool rx_pause)
{
	struct qca8k_priv *priv = ds->priv;
	u32 reg;

	if (phylink_autoneg_inband(mode)) {
		reg = QCA8K_PORT_STATUS_LINK_AUTO;
	} else {
		switch (speed) {
		case SPEED_10:
			reg = QCA8K_PORT_STATUS_SPEED_10;
			break;
		case SPEED_100:
			reg = QCA8K_PORT_STATUS_SPEED_100;
			break;
		case SPEED_1000:
			reg = QCA8K_PORT_STATUS_SPEED_1000;
			break;
		default:
			reg = QCA8K_PORT_STATUS_LINK_AUTO;
			break;
		}

		if (duplex == DUPLEX_FULL)
			reg |= QCA8K_PORT_STATUS_DUPLEX;

		if (rx_pause || dsa_is_cpu_port(ds, port))
			reg |= QCA8K_PORT_STATUS_RXFLOW;

		if (tx_pause || dsa_is_cpu_port(ds, port))
			reg |= QCA8K_PORT_STATUS_TXFLOW;
	}

	reg |= QCA8K_PORT_STATUS_TXMAC | QCA8K_PORT_STATUS_RXMAC;

	qca8k_write(priv, QCA8K_REG_PORT_STATUS(port), reg);
}

static void
qca8k_get_strings(struct dsa_switch *ds, int port, u32 stringset, uint8_t *data)
{
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < ARRAY_SIZE(ar8327_mib); i++)
		strncpy(data + i * ETH_GSTRING_LEN, ar8327_mib[i].name,
			ETH_GSTRING_LEN);
}

static void
qca8k_get_ethtool_stats(struct dsa_switch *ds, int port,
			uint64_t *data)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	const struct qca8k_mib_desc *mib;
	u32 reg, i, val;
	u32 hi = 0;
	int ret;

	for (i = 0; i < ARRAY_SIZE(ar8327_mib); i++) {
		mib = &ar8327_mib[i];
		reg = QCA8K_PORT_MIB_COUNTER(port) + mib->offset;

		ret = qca8k_read(priv, reg, &val);
		if (ret < 0)
			continue;

		if (mib->size == 2) {
			ret = qca8k_read(priv, reg + 4, &hi);
			if (ret < 0)
				continue;
		}

		data[i] = val;
		if (mib->size == 2)
			data[i] |= (u64)hi << 32;
	}
}

static int
qca8k_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	if (sset != ETH_SS_STATS)
		return 0;

	return ARRAY_SIZE(ar8327_mib);
}

static int
qca8k_set_mac_eee(struct dsa_switch *ds, int port, struct ethtool_eee *eee)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	u32 lpi_en = QCA8K_REG_EEE_CTRL_LPI_EN(port);
	u32 reg;
	int ret;

	mutex_lock(&priv->reg_mutex);
	ret = qca8k_read(priv, QCA8K_REG_EEE_CTRL, &reg);
	if (ret < 0)
		goto exit;

	if (eee->eee_enabled)
		reg |= lpi_en;
	else
		reg &= ~lpi_en;
	ret = qca8k_write(priv, QCA8K_REG_EEE_CTRL, reg);

exit:
	mutex_unlock(&priv->reg_mutex);
	return ret;
}

static int
qca8k_get_mac_eee(struct dsa_switch *ds, int port, struct ethtool_eee *e)
{
	/* Nothing to do on the port's MAC */
	return 0;
}

static void
qca8k_port_stp_state_set(struct dsa_switch *ds, int port, u8 state)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	u32 stp_state;

	switch (state) {
	case BR_STATE_DISABLED:
		stp_state = QCA8K_PORT_LOOKUP_STATE_DISABLED;
		break;
	case BR_STATE_BLOCKING:
		stp_state = QCA8K_PORT_LOOKUP_STATE_BLOCKING;
		break;
	case BR_STATE_LISTENING:
		stp_state = QCA8K_PORT_LOOKUP_STATE_LISTENING;
		break;
	case BR_STATE_LEARNING:
		stp_state = QCA8K_PORT_LOOKUP_STATE_LEARNING;
		break;
	case BR_STATE_FORWARDING:
	default:
		stp_state = QCA8K_PORT_LOOKUP_STATE_FORWARD;
		break;
	}

	qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(port),
		  QCA8K_PORT_LOOKUP_STATE_MASK, stp_state);
}

static int
qca8k_port_bridge_join(struct dsa_switch *ds, int port, struct net_device *br)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	int port_mask, cpu_port;
	int i, ret;

	cpu_port = dsa_to_port(ds, port)->cpu_dp->index;
	port_mask = BIT(cpu_port);

	for (i = 0; i < QCA8K_NUM_PORTS; i++) {
		if (dsa_is_cpu_port(ds, i))
			continue;
		if (dsa_to_port(ds, i)->bridge_dev != br)
			continue;
		/* Add this port to the portvlan mask of the other ports
		 * in the bridge
		 */
		ret = qca8k_reg_set(priv,
				    QCA8K_PORT_LOOKUP_CTRL(i),
				    BIT(port));
		if (ret)
			return ret;
		if (i != port)
			port_mask |= BIT(i);
	}

	/* Add all other ports to this ports portvlan mask */
	ret = qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(port),
			QCA8K_PORT_LOOKUP_MEMBER, port_mask);

	return ret;
}

static void
qca8k_port_bridge_leave(struct dsa_switch *ds, int port, struct net_device *br)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	int cpu_port, i;

	cpu_port = dsa_to_port(ds, port)->cpu_dp->index;

	for (i = 0; i < QCA8K_NUM_PORTS; i++) {
		if (dsa_is_cpu_port(ds, i))
			continue;
		if (dsa_to_port(ds, i)->bridge_dev != br)
			continue;
		/* Remove this port to the portvlan mask of the other ports
		 * in the bridge
		 */
		qca8k_reg_clear(priv,
				QCA8K_PORT_LOOKUP_CTRL(i),
				BIT(port));
	}

	/* Set the cpu port to be the only one in the portvlan mask of
	 * this port
	 */
	qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(port),
		  QCA8K_PORT_LOOKUP_MEMBER, BIT(cpu_port));
}

static int
qca8k_port_enable(struct dsa_switch *ds, int port,
		  struct phy_device *phy)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;

	qca8k_port_set_status(priv, port, 1);
	priv->port_sts[port].enabled = 1;

	if (dsa_is_user_port(ds, port))
		phy_support_asym_pause(phy);

	return 0;
}

static void
qca8k_port_disable(struct dsa_switch *ds, int port)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;

	qca8k_port_set_status(priv, port, 0);
	priv->port_sts[port].enabled = 0;
}

static int
qca8k_port_change_mtu(struct dsa_switch *ds, int port, int new_mtu)
{
	struct qca8k_priv *priv = ds->priv;
	int i, mtu = 0;

	priv->port_mtu[port] = new_mtu;

	for (i = 0; i < QCA8K_NUM_PORTS; i++)
		if (priv->port_mtu[i] > mtu)
			mtu = priv->port_mtu[i];

	/* Include L2 header / FCS length */
	return qca8k_write(priv, QCA8K_MAX_FRAME_SIZE, mtu + ETH_HLEN + ETH_FCS_LEN);
}

static int
qca8k_port_max_mtu(struct dsa_switch *ds, int port)
{
	return QCA8K_MAX_MTU;
}

static int
qca8k_port_fdb_insert(struct qca8k_priv *priv, const u8 *addr,
		      u16 port_mask, u16 vid)
{
	/* Set the vid to the port vlan id if no vid is set */
	if (!vid)
		vid = QCA8K_PORT_VID_DEF;

	return qca8k_fdb_add(priv, addr, port_mask, vid,
			     QCA8K_ATU_STATUS_STATIC);
}

static int
qca8k_port_fdb_add(struct dsa_switch *ds, int port,
		   const unsigned char *addr, u16 vid)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	u16 port_mask = BIT(port);

	return qca8k_port_fdb_insert(priv, addr, port_mask, vid);
}

static int
qca8k_port_fdb_del(struct dsa_switch *ds, int port,
		   const unsigned char *addr, u16 vid)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	u16 port_mask = BIT(port);

	if (!vid)
		vid = QCA8K_PORT_VID_DEF;

	return qca8k_fdb_del(priv, addr, port_mask, vid);
}

static int
qca8k_port_fdb_dump(struct dsa_switch *ds, int port,
		    dsa_fdb_dump_cb_t *cb, void *data)
{
	struct qca8k_priv *priv = (struct qca8k_priv *)ds->priv;
	struct qca8k_fdb _fdb = { 0 };
	int cnt = QCA8K_NUM_FDB_RECORDS;
	bool is_static;
	int ret = 0;

	mutex_lock(&priv->reg_mutex);
	while (cnt-- && !qca8k_fdb_next(priv, &_fdb, port)) {
		if (!_fdb.aging)
			break;
		is_static = (_fdb.aging == QCA8K_ATU_STATUS_STATIC);
		ret = cb(_fdb.mac, _fdb.vid, is_static, data);
		if (ret)
			break;
	}
	mutex_unlock(&priv->reg_mutex);

	return 0;
}

static int
qca8k_port_vlan_filtering(struct dsa_switch *ds, int port, bool vlan_filtering,
			  struct switchdev_trans *trans)
{
	struct qca8k_priv *priv = ds->priv;

	if (switchdev_trans_ph_prepare(trans))
		return 0;

	if (vlan_filtering) {
		qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(port),
			  QCA8K_PORT_LOOKUP_VLAN_MODE,
			  QCA8K_PORT_LOOKUP_VLAN_MODE_SECURE);
	} else {
		qca8k_rmw(priv, QCA8K_PORT_LOOKUP_CTRL(port),
			  QCA8K_PORT_LOOKUP_VLAN_MODE,
			  QCA8K_PORT_LOOKUP_VLAN_MODE_NONE);
	}

	return 0;
}

static int
qca8k_port_vlan_prepare(struct dsa_switch *ds, int port,
			const struct switchdev_obj_port_vlan *vlan)
{
	return 0;
}

static void
qca8k_port_vlan_add(struct dsa_switch *ds, int port,
		    const struct switchdev_obj_port_vlan *vlan)
{
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	struct qca8k_priv *priv = ds->priv;
	int ret = 0;
	u16 vid;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end && !ret; ++vid)
		ret = qca8k_vlan_add(priv, port, vid, untagged);

	if (ret)
		dev_err(priv->dev, "Failed to add VLAN to port %d (%d)", port, ret);

	if (pvid) {
		int shift = 16 * (port % 2);

		qca8k_rmw(priv, QCA8K_EGRESS_VLAN(port),
			  0xfff << shift,
			  vlan->vid_end << shift);
		qca8k_write(priv, QCA8K_REG_PORT_VLAN_CTRL0(port),
			    QCA8K_PORT_VLAN_CVID(vlan->vid_end) |
			    QCA8K_PORT_VLAN_SVID(vlan->vid_end));
	}
}

static int
qca8k_port_vlan_del(struct dsa_switch *ds, int port,
		    const struct switchdev_obj_port_vlan *vlan)
{
	struct qca8k_priv *priv = ds->priv;
	int ret = 0;
	u16 vid;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end && !ret; ++vid)
		ret = qca8k_vlan_del(priv, port, vid);

	if (ret)
		dev_err(priv->dev, "Failed to delete VLAN from port %d (%d)", port, ret);

	return ret;
}

static enum dsa_tag_protocol
qca8k_get_tag_protocol(struct dsa_switch *ds, int port,
		       enum dsa_tag_protocol mp)
{
	return DSA_TAG_PROTO_IPQ4019;
}

static const struct dsa_switch_ops qca8k_switch_ops = {
	.get_tag_protocol	= qca8k_get_tag_protocol,
	.setup			= qca8k_setup,
	.get_strings		= qca8k_get_strings,
	.get_ethtool_stats	= qca8k_get_ethtool_stats,
	.get_sset_count		= qca8k_get_sset_count,
	.get_mac_eee		= qca8k_get_mac_eee,
	.set_mac_eee		= qca8k_set_mac_eee,
	.port_enable		= qca8k_port_enable,
	.port_disable		= qca8k_port_disable,
	.port_change_mtu	= qca8k_port_change_mtu,
	.port_max_mtu		= qca8k_port_max_mtu,
	.port_stp_state_set	= qca8k_port_stp_state_set,
	.port_bridge_join	= qca8k_port_bridge_join,
	.port_bridge_leave	= qca8k_port_bridge_leave,
	.port_fdb_add		= qca8k_port_fdb_add,
	.port_fdb_del		= qca8k_port_fdb_del,
	.port_fdb_dump		= qca8k_port_fdb_dump,
	.port_vlan_filtering	= qca8k_port_vlan_filtering,
	.port_vlan_prepare	= qca8k_port_vlan_prepare,
	.port_vlan_add		= qca8k_port_vlan_add,
	.port_vlan_del		= qca8k_port_vlan_del,
	.phylink_validate	= qca8k_phylink_validate,
	.phylink_mac_link_state	= qca8k_phylink_mac_link_state,
	.phylink_mac_config	= qca8k_phylink_mac_config,
	.phylink_mac_link_down	= qca8k_phylink_mac_link_down,
	.phylink_mac_link_up	= qca8k_phylink_mac_link_up,
};

enum ar40xx_port_wrapper_cfg {
	PORT_WRAPPER_PSGMII = 0,
	PORT_WRAPPER_RGMII = 3,
};

#define AR40XX_PSGMII_MODE_CONTROL			0x1b4
#define   AR40XX_PSGMII_ATHR_CSCO_MODE_25M		BIT(0)

#define AR40XX_PSGMIIPHY_TX_CONTROL			0x288

#define AR40XX_REG_RGMII_CTRL				0x0004
#define AR40XX_REG_PORT_LOOKUP(_i)			(0x660 + (_i) * 0xc)
#define   AR40XX_PORT_LOOKUP_LOOPBACK			BIT(21)

#define AR40XX_PHY_SPEC_STATUS				0x11
#define   AR40XX_PHY_SPEC_STATUS_LINK			BIT(10)
#define   AR40XX_PHY_SPEC_STATUS_DUPLEX			BIT(13)
#define   AR40XX_PHY_SPEC_STATUS_SPEED			GENMASK(16, 14)

#define AR40XX_PSGMII_ID				5
#define AR40XX_PSGMII_CALB_NUM				100
#define AR40XX_MALIBU_PSGMII_MODE_CTRL			0x6d
#define AR40XX_MALIBU_PHY_PSGMII_MODE_CTRL_ADJUST_VAL	0x220c
#define AR40XX_MALIBU_PHY_MMD7_DAC_CTRL			0x801a
#define AR40XX_MALIBU_DAC_CTRL_MASK			0x380
#define AR40XX_MALIBU_DAC_CTRL_VALUE			0x280
#define AR40XX_MALIBU_PHY_RLP_CTRL			0x805a
#define AR40XX_PSGMII_TX_DRIVER_1_CTRL			0xb
#define AR40XX_MALIBU_PHY_PSGMII_REDUCE_SERDES_TX_AMP	0x8a
#define AR40XX_MALIBU_PHY_LAST_ADDR			4

static u32
psgmii_read(struct qca8k_priv *priv, int reg)
{
	u32 val;

	regmap_read(priv->psgmii, reg, &val);
	return val;
}

static void
psgmii_write(struct qca8k_priv *priv, int reg, u32 val)
{
	regmap_write(priv->psgmii, reg, val);
}

static void
qca8k_phy_mmd_write(struct qca8k_priv *priv, u32 phy_id,
		     u16 mmd_num, u16 reg_id, u16 reg_val)
{
	struct mii_bus *bus = priv->bus;

	mutex_lock(&bus->mdio_lock);
	__mdiobus_write(bus, phy_id, MII_MMD_CTRL, mmd_num);
	__mdiobus_write(bus, phy_id, MII_MMD_DATA, reg_id);
	__mdiobus_write(bus, phy_id, MII_MMD_CTRL, MII_MMD_CTRL_NOINCR | mmd_num);
	__mdiobus_write(bus, phy_id, MII_MMD_DATA, reg_val);
	mutex_unlock(&bus->mdio_lock);
}

static u16
qca8k_phy_mmd_read(struct qca8k_priv *priv, u32 phy_id,
		    u16 mmd_num, u16 reg_id)
{
	struct mii_bus *bus = priv->bus;
	u16 value;

	mutex_lock(&bus->mdio_lock);
	__mdiobus_write(bus, phy_id, MII_MMD_CTRL, mmd_num);
	__mdiobus_write(bus, phy_id, MII_MMD_DATA, reg_id);
	__mdiobus_write(bus, phy_id, MII_MMD_CTRL, MII_MMD_CTRL_NOINCR | mmd_num);
	value = __mdiobus_read(bus, phy_id, MII_MMD_DATA);
	mutex_unlock(&bus->mdio_lock);

	return value;
}

static void
ess_reset(struct qca8k_priv *priv)
{
	reset_control_assert(priv->ess_rst);

	mdelay(10);

	reset_control_deassert(priv->ess_rst);

	/* Waiting for all inner tables to be flushed and reinitialized.
	 * This takes between 5 and 10ms.
	 */
	mdelay(10);
}

static void
ar40xx_malibu_psgmii_ess_reset(struct qca8k_priv *priv)
{
	struct mii_bus *bus = priv->bus;
	u32 n;

	/* Reset phy psgmii */
	/* fix phy psgmii RX 20bit */
	mdiobus_write(bus, AR40XX_PSGMII_ID, 0x0, 0x005b);
	/* reset phy psgmii */
	mdiobus_write(bus, AR40XX_PSGMII_ID, 0x0, 0x001b);
	/* release reset phy psgmii */
	mdiobus_write(bus, AR40XX_PSGMII_ID, 0x0, 0x005b);

	for (n = 0; n < AR40XX_PSGMII_CALB_NUM; n++) {
		u16 status;

		status = qca8k_phy_mmd_read(priv, AR40XX_PSGMII_ID,
					     MDIO_MMD_PMAPMD, 0x28);
		if (status & BIT(0))
			break;

		/* Polling interval to check PSGMII PLL in malibu is ready
		 * the worst time is 8.67ms
		 * for 25MHz reference clock
		 * [512+(128+2048)*49]*80ns+100us
		 */
		mdelay(2);
	}

	/* check malibu psgmii calibration done end... */

	/* freeze phy psgmii RX CDR */
	mdiobus_write(bus, AR40XX_PSGMII_ID, 0x1a, 0x2230);

	ess_reset(priv);

	/* wait for the psgmii calibration to complete */
	for (n = 0; n < AR40XX_PSGMII_CALB_NUM; n++) {
		u32 status;

		status = psgmii_read(priv, 0xa0);
		if (status & BIT(0))
			break;

		/* Polling interval to check PSGMII PLL in ESS is ready */
		mdelay(2);
	}

	/* release phy psgmii RX CDR */
	mdiobus_write(bus, AR40XX_PSGMII_ID, 0x1a, 0x3230);
	/* release phy psgmii RX 20bit */
	mdiobus_write(bus, AR40XX_PSGMII_ID, 0x0, 0x005f);
}

static void
ar40xx_phytest_run(struct qca8k_priv *priv, int phy)
{
	/* enable check */
	qca8k_phy_mmd_write(priv, phy, 7, 0x8029, 0x0000);
	qca8k_phy_mmd_write(priv, phy, 7, 0x8029, 0x0003);

	/* start traffic */
	qca8k_phy_mmd_write(priv, phy, 7, 0x8020, 0xa000);

	/* wait precisely for all traffic end
	 * 4096(pkt num) * 1524(size) * 8ns (125MHz) = 49.9ms
	 */
	mdelay(50);
}

static bool
ar40xx_phytest_check_counters(struct qca8k_priv *priv, int phy, u32 count)
{
	u32 tx_ok, tx_error;
	u32 rx_ok, rx_error;
	u32 tx_ok_high16;
	u32 rx_ok_high16;
	u32 tx_all_ok, rx_all_ok;

	/* read counters */
	tx_ok = qca8k_phy_mmd_read(priv, phy, 7, 0x802e);
	tx_ok_high16 = qca8k_phy_mmd_read(priv, phy, 7, 0x802d);
	tx_error = qca8k_phy_mmd_read(priv, phy, 7, 0x802f);
	rx_ok = qca8k_phy_mmd_read(priv, phy, 7, 0x802b);
	rx_ok_high16 = qca8k_phy_mmd_read(priv, phy, 7, 0x802a);
	rx_error = qca8k_phy_mmd_read(priv, phy, 7, 0x802c);
	tx_all_ok = tx_ok + (tx_ok_high16 << 16);
	rx_all_ok = rx_ok + (rx_ok_high16 << 16);

	if (tx_all_ok != count || tx_error != 0) {
		dev_dbg(priv->dev,
			"PHY%d tx_ok:%08x tx_err:%08x rx_ok:%08x rx_err:%08x\n",
			phy, tx_all_ok, tx_error, rx_all_ok, rx_error);
		return false;
	}

	return true;
}

static void
ar40xx_check_phy_reset_status(struct qca8k_priv *priv, int phy)
{
	u16 bmcr;

	bmcr = mdiobus_read(priv->bus, phy, MII_BMCR);
	if (bmcr & BMCR_RESET)
		dev_warn_once(priv->dev, "PHY %d reset is pending\n", phy);
}

static void
ar40xx_psgmii_single_phy_testing(struct qca8k_priv *priv, int phy)
{
	struct mii_bus *bus = priv->bus;
	int j;

	mdiobus_write(bus, phy, MII_BMCR, BMCR_RESET | BMCR_ANENABLE);
	ar40xx_check_phy_reset_status(priv, phy);

	mdiobus_write(bus, phy, MII_BMCR, BMCR_LOOPBACK | BMCR_FULLDPLX |
					  BMCR_SPEED1000);

	for (j = 0; j < AR40XX_PSGMII_CALB_NUM; j++) {
		u16 status;

		status = mdiobus_read(bus, phy, AR40XX_PHY_SPEC_STATUS);
		if (status & AR40XX_PHY_SPEC_STATUS_LINK)
			break;

		/* the polling interval to check if the PHY link up or not
		  * maxwait_timer: 750 ms +/-10 ms
		  * minwait_timer : 1 us +/- 0.1us
		  * time resides in minwait_timer ~ maxwait_timer
		  * see IEEE 802.3 section 40.4.5.2
		  */
		mdelay(8);
	}

	ar40xx_phytest_run(priv, phy);

	/* check counter */
	if (ar40xx_phytest_check_counters(priv, phy, 0x1000)) {
		priv->phy_t_status &= (~BIT(phy));
	} else {
		dev_info(priv->dev, "PHY %d single test PSGMII issue happen!\n", phy);
		priv->phy_t_status |= BIT(phy);
	}

	mdiobus_write(bus, phy, MII_BMCR, BMCR_ANENABLE | BMCR_PDOWN |
					  BMCR_SPEED1000);
}

static void
ar40xx_psgmii_all_phy_testing(struct qca8k_priv *priv)
{
	struct mii_bus *bus = priv->bus;
	int phy, j;

	mdiobus_write(bus, 0x1f, MII_BMCR, BMCR_RESET | BMCR_ANENABLE);
	for (phy = 0; phy < QCA8K_NUM_PORTS - 1; phy++)
		ar40xx_check_phy_reset_status(priv, phy);

	mdiobus_write(bus, 0x1f, MII_BMCR, BMCR_LOOPBACK | BMCR_FULLDPLX |
					   BMCR_SPEED1000);

	for (j = 0; j < AR40XX_PSGMII_CALB_NUM; j++) {
		for (phy = 0; phy < QCA8K_NUM_PORTS - 1; phy++) {
			u16 status;

			status = mdiobus_read(bus, phy, AR40XX_PHY_SPEC_STATUS);
			if (!(status & AR40XX_PHY_SPEC_STATUS_LINK))
				break;
		}

		if (phy >= (QCA8K_NUM_PORTS - 1))
			break;
		/* The polling interva to check if the PHY link up or not */
		mdelay(8);
	}

	ar40xx_phytest_run(priv, 0x1f);

	for (phy = 0; phy < QCA8K_NUM_PORTS - 1; phy++) {
		if (ar40xx_phytest_check_counters(priv, phy, 4096)) {
			/* success */
			priv->phy_t_status &= ~BIT(phy + 8);
		} else {
			dev_info(priv->dev, "PHY%d test see issue!\n", phy);
			priv->phy_t_status |= BIT(phy + 8);
		}
	}

	dev_dbg(priv->dev, "PHY all test 0x%x \r\n", priv->phy_t_status);
}

static void
ar40xx_psgmii_self_test(struct qca8k_priv *priv)
{
	struct mii_bus *bus = priv->bus;
	u32 i, phy;

	ar40xx_malibu_psgmii_ess_reset(priv);

	/* switch to access MII reg for copper */
	mdiobus_write(bus, 4, 0x1f, 0x8500);

	for (phy = 0; phy < QCA8K_NUM_PORTS - 1; phy++) {
		/*enable phy mdio broadcast write*/
		qca8k_phy_mmd_write(priv, phy, 7, 0x8028, 0x801f);
	}

	/* force no link by power down */
	mdiobus_write(bus, 0x1f, MII_BMCR, BMCR_ANENABLE | BMCR_PDOWN |
					   BMCR_SPEED1000);

	/* Setup packet generator for loopback calibration */
	qca8k_phy_mmd_write(priv, 0x1f, 7, 0x8021, 0x1000); /* 4096 Packets */
	qca8k_phy_mmd_write(priv, 0x1f, 7, 0x8062, 0x05e0); /* 1524 Bytes */

	/* fix mdi status */
	mdiobus_write(bus, 0x1f, 0x10, 0x6800);
	for (i = 0; i < AR40XX_PSGMII_CALB_NUM; i++) {
		priv->phy_t_status = 0;

		for (phy = 0; phy < QCA8K_NUM_PORTS - 1; phy++) {
			qca8k_rmw(priv, AR40XX_REG_PORT_LOOKUP(phy + 1),
				AR40XX_PORT_LOOKUP_LOOPBACK,
				AR40XX_PORT_LOOKUP_LOOPBACK);
		}

		for (phy = 0; phy < QCA8K_NUM_PORTS - 1; phy++)
			ar40xx_psgmii_single_phy_testing(priv, phy);

		ar40xx_psgmii_all_phy_testing(priv);

		if (priv->phy_t_status)
			ar40xx_malibu_psgmii_ess_reset(priv);
		else
			break;
	}

	if (i >= AR40XX_PSGMII_CALB_NUM)
		dev_info(priv->dev, "PSGMII cannot recover\n");
	else
		dev_dbg(priv->dev, "PSGMII recovered after %d times reset\n", i);

	/* configuration recover */
	/* packet number */
	qca8k_phy_mmd_write(priv, 0x1f, 7, 0x8021, 0x0);
	/* disable check */
	qca8k_phy_mmd_write(priv, 0x1f, 7, 0x8029, 0x0);
	/* disable traffic */
	qca8k_phy_mmd_write(priv, 0x1f, 7, 0x8020, 0x0);
}

static void
ar40xx_psgmii_self_test_clean(struct qca8k_priv *priv)
{
	struct mii_bus *bus = priv->bus;
	int phy;

	/* disable phy internal loopback */
	mdiobus_write(bus, 0x1f, 0x10, 0x6860);
	mdiobus_write(bus, 0x1f, MII_BMCR, BMCR_ANENABLE | BMCR_RESET |
					   BMCR_SPEED1000);

	for (phy = 0; phy < QCA8K_NUM_PORTS - 1; phy++) {
		/* disable mac loop back */
		qca8k_rmw(priv, AR40XX_REG_PORT_LOOKUP(phy + 1),
				AR40XX_PORT_LOOKUP_LOOPBACK, 0);

		/* disable phy mdio broadcast write */
		qca8k_phy_mmd_write(priv, phy, 7, 0x8028, 0x001f);
	}
}

static void
ar40xx_malibu_init(struct qca8k_priv *priv)
{
	int i;
	u16 val;

	/* war to enable AZ transmitting ability */
	qca8k_phy_mmd_write(priv, AR40XX_PSGMII_ID, 1,
		      AR40XX_MALIBU_PSGMII_MODE_CTRL,
		      AR40XX_MALIBU_PHY_PSGMII_MODE_CTRL_ADJUST_VAL);

	for (i = 0; i < QCA8K_NUM_PORTS - 1; i++) {

		/* change malibu control_dac */
		val = qca8k_phy_mmd_read(priv, i, 7, AR40XX_MALIBU_PHY_MMD7_DAC_CTRL);
		val &= ~AR40XX_MALIBU_DAC_CTRL_MASK;
		val |= AR40XX_MALIBU_DAC_CTRL_VALUE;
		qca8k_phy_mmd_write(priv, i, 7, AR40XX_MALIBU_PHY_MMD7_DAC_CTRL, val);

		if (i == AR40XX_MALIBU_PHY_LAST_ADDR) {
			/* avoid PHY to get into hibernation */
			val = qca8k_phy_mmd_read(priv, i, 3,
						  AR40XX_MALIBU_PHY_RLP_CTRL);
			val &= (~(1<<1));
			qca8k_phy_mmd_write(priv, i, 3,
					     AR40XX_MALIBU_PHY_RLP_CTRL, val);
		}
	}

	/* adjust psgmii serdes tx amp */
	mdiobus_write(priv->bus, AR40XX_PSGMII_ID,
		      AR40XX_PSGMII_TX_DRIVER_1_CTRL,
		      AR40XX_MALIBU_PHY_PSGMII_REDUCE_SERDES_TX_AMP);
}

static void
ar40xx_mac_mode_init(struct qca8k_priv *priv)
{
	switch (priv->mac_mode) {
	case PORT_WRAPPER_PSGMII:
		ar40xx_malibu_init(priv);
		ar40xx_psgmii_self_test(priv);
		ar40xx_psgmii_self_test_clean(priv);

		psgmii_write(priv, AR40XX_PSGMII_MODE_CONTROL, 0x2200);
		psgmii_write(priv, AR40XX_PSGMIIPHY_TX_CONTROL, 0x8380);
		break;
	}
}

static void
qca8k_dsa_init_work(struct work_struct *work)
{
	struct qca8k_priv *priv = container_of(work, struct qca8k_priv, dsa_init.work);
	struct device *parent = priv->dev->parent;
	int ret;

	ret = dsa_register_switch(priv->ds);

	switch (ret) {
	case 0:
		return;

	case -EPROBE_DEFER:
		dev_dbg(priv->dev, "dsa_register_switch defered.\n");
		schedule_delayed_work(&priv->dsa_init, msecs_to_jiffies(200));
		return;

	default:
		dev_err(priv->dev, "dsa_register_switch failed with (%d).\n", ret);
		/* unbind anything failed */
		if (parent)
			device_lock(parent);

		device_release_driver(priv->dev);
		if (parent)
			device_unlock(parent);
		return;
	}
}

static int
qca8k_ipq4019_probe(struct platform_device *pdev)
{
	struct qca8k_priv *priv;
	void __iomem *base, *psgmii;
	struct device_node *np = pdev->dev.of_node, *mdio_np;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;

	base = devm_platform_ioremap_resource_byname(pdev, "base");
	if (IS_ERR(base))
		return PTR_ERR(base);

	priv->regmap = devm_regmap_init_mmio(priv->dev, base,
					     &qca8k_ipq4019_regmap_config);
	if (IS_ERR(priv->regmap)) {
		ret = PTR_ERR(priv->regmap);
		dev_err(priv->dev, "base regmap initialization failed, %d\n", ret);
		return ret;
	}

	psgmii = devm_platform_ioremap_resource_byname(pdev, "psgmii_phy");
	if (IS_ERR(psgmii))
		return PTR_ERR(psgmii);

	priv->psgmii = devm_regmap_init_mmio(priv->dev, psgmii,
					     &qca8k_ipq4019_psgmii_phy_regmap_config);
	if (IS_ERR(priv->psgmii)) {
		ret = PTR_ERR(priv->psgmii);
		dev_err(priv->dev, "PSGMII regmap initialization failed, %d\n", ret);
		return ret;
	}

	priv->ess_clk = of_clk_get_by_name(np, "ess_clk");
	if (IS_ERR(priv->ess_clk)) {
		dev_err(&pdev->dev, "Failed to get ess_clk\n");
		return PTR_ERR(priv->ess_clk);
	}

	priv->ess_rst = devm_reset_control_get(&pdev->dev, "ess_rst");
	if (IS_ERR(priv->ess_rst)) {
		dev_err(&pdev->dev, "Failed to get ess_rst control!\n");
		return PTR_ERR(priv->ess_rst);
	}

	ret = of_property_read_u32(np, "mac-mode", &priv->mac_mode);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to get 'mac-mode' property\n");
		return -EINVAL;
	}

	mdio_np = of_parse_phandle(np, "mdio", 0);
	if (!mdio_np) {
		dev_err(&pdev->dev, "unable to get MDIO bus phandle\n");
		return -EINVAL;
	}

	priv->bus = of_mdio_find_bus(mdio_np);
	of_node_put(mdio_np);
	if (!priv->bus) {
		dev_err(&pdev->dev, "unable to find MDIO bus\n");
		return -EPROBE_DEFER;
	}

	priv->ds = devm_kzalloc(priv->dev, sizeof(*priv->ds), GFP_KERNEL);
	if (!priv->ds)
		return -ENOMEM;

	priv->ds->dev = priv->dev;
	priv->ds->num_ports = QCA8K_NUM_PORTS;
	priv->ds->priv = priv;
	priv->ops = qca8k_switch_ops;
	priv->ds->ops = &priv->ops;

	mutex_init(&priv->reg_mutex);
	platform_set_drvdata(pdev, priv);

	clk_prepare_enable(priv->ess_clk);

	ess_reset(priv);

	ar40xx_mac_mode_init(priv);

	reset_control_put(priv->ess_rst);

	/* Ok. What's going on with the delayed dsa_switch_register?!
	 *
	 * On Bootup, this switch driver loads before the ethernet
	 * driver. This causes a problem in dsa_register_switch when
	 * it parses the tree and encounters the not-yet-ready
	 * 	"ethernet = <&gmac>;" property.
	 *
	 * Which will err with -EPROBE_DEFER. Normally this should be
	 * OK and the driver will just get loaded at a later time.
	 * However, the EthernetSubSystem (ESS for short) really doesn't
	 * like being resetted more than once in this fashion and will
	 * "lock it up for good"... like "real good".
	 *
	 * So far, only a reboot can "unwedge" it, which is not what
	 * we want.
	 *
	 * So this workaround (running dsa_register_switch in a
	 * workqueue task) is employed to fix this unknown issue within
	 * the SoC for now.
	 */

	INIT_DELAYED_WORK(&priv->dsa_init, qca8k_dsa_init_work);
	schedule_delayed_work(&priv->dsa_init, msecs_to_jiffies(1000));

	return 0;
}

static const struct of_device_id qca8k_ipq4019_of_match[] = {
	{ .compatible = "qca,ipq4019-qca8337n" },
	{ /* sentinel */ },
};

static struct platform_driver qca8k_ipq4019_driver = {
	.driver = {
		.name = "qca8k-ipq4019",
		.of_match_table = qca8k_ipq4019_of_match,
	},
};

module_platform_driver_probe(qca8k_ipq4019_driver, qca8k_ipq4019_probe);

MODULE_AUTHOR("Mathieu Olivari, John Crispin <john@phrozen.org>");
MODULE_AUTHOR("Gabor Juhos <j4g8y7@gmail.com>, Robert Marko <robert.marko@sartura.hr>");
MODULE_DESCRIPTION("Qualcomm IPQ4019 built-in switch driver");
MODULE_LICENSE("GPL v2");
