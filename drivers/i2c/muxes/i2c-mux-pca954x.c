/*
 * I2C multiplexer
 *
 * Copyright (c) 2008-2009 Rodolfo Giometti <giometti@linux.it>
 * Copyright (c) 2008-2009 Eurotech S.p.A. <info@eurotech.it>
 *
 * This module supports the PCA954x series of I2C multiplexer/switch chips
 * made by Philips Semiconductors.
 * This includes the:
 *	 PCA9540, PCA9542, PCA9543, PCA9544, PCA9545, PCA9546, PCA9547
 *	 and PCA9548.
 *
 * These chips are all controlled via the I2C bus itself, and all have a
 * single 8-bit register. The upstream "parent" bus fans out to two,
 * four, or eight downstream busses or channels; which of these
 * are selected is determined by the chip type and register contents. A
 * mux can select only one sub-bus at a time; a switch can select any
 * combination simultaneously.
 *
 * Based on:
 *	pca954x.c from Kumar Gala <galak@kernel.crashing.org>
 * Copyright (C) 2006
 *
 * Based on:
 *	pca954x.c from Ken Harrenstien
 * Copyright (C) 2004 Google, Inc. (Ken Harrenstien)
 *
 * Based on:
 *	i2c-virtual_cb.c from Brian Kuschak <bkuschak@yahoo.com>
 * and
 *	pca9540.c from Jean Delvare <jdelvare@suse.de>.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/delay.h>

#include <linux/i2c/pca954x.h>

#define PCA954X_MAX_NCHANS 8

enum pca_type {
	pca_9540,
	pca_9542,
	pca_9543,
	pca_9544,
	pca_9545,
	pca_9546,
	pca_9547,
	pca_9548,
};

struct pca954x {
	enum pca_type type;
	struct i2c_adapter *virt_adaps[PCA954X_MAX_NCHANS];

	u8 last_chan;		/* last register value */
	struct regulator *vcc_reg;
	struct regulator *pullup_reg;
};

struct chip_desc {
	u8 nchans;
	u8 enable;	/* used for muxes only */
	enum muxtype {
		pca954x_ismux = 0,
		pca954x_isswi
	} muxtype;
};

/* Provide specs for the PCA954x types we know about */
static const struct chip_desc chips[] = {
	[pca_9540] = {
		.nchans = 2,
		.enable = 0x4,
		.muxtype = pca954x_ismux,
	},
	[pca_9543] = {
		.nchans = 2,
		.muxtype = pca954x_isswi,
	},
	[pca_9544] = {
		.nchans = 4,
		.enable = 0x4,
		.muxtype = pca954x_ismux,
	},
	[pca_9545] = {
		.nchans = 4,
		.muxtype = pca954x_isswi,
	},
	[pca_9547] = {
		.nchans = 8,
		.enable = 0x8,
		.muxtype = pca954x_ismux,
	},
	[pca_9548] = {
		.nchans = 8,
		.muxtype = pca954x_isswi,
	},
};

static const struct i2c_device_id pca954x_id[] = {
	{ "pca9540", pca_9540 },
	{ "pca9542", pca_9540 },
	{ "pca9543", pca_9543 },
	{ "pca9544", pca_9544 },
	{ "pca9545", pca_9545 },
	{ "pca9546", pca_9545 },
	{ "pca9547", pca_9547 },
	{ "pca9548", pca_9548 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pca954x_id);

#ifdef CONFIG_OF
static const struct of_device_id pca954x_of_match[] = {
	{ .compatible = "nxp,pca9540", .data = (void *)pca_9540, },
	{ .compatible = "nxp,pca9542", .data = (void *)pca_9540, },
	{ .compatible = "nxp,pca9543", .data = (void *)pca_9543, },
	{ .compatible = "nxp,pca9544", .data = (void *)pca_9544, },
	{ .compatible = "nxp,pca9545", .data = (void *)pca_9545, },
	{ .compatible = "nxp,pca9546", .data = (void *)pca_9545, },
	{ .compatible = "nxp,pca9547", .data = (void *)pca_9547, },
	{ .compatible = "nxp,pca9548", .data = (void *)pca_9548, },
	{ },
};
MODULE_DEVICE_TABLE(of, pca954x_of_match);

static int pca954x_get_device_type(struct device *dev,
				const struct i2c_device_id *id)
{
	if (dev->of_node) {
		const struct of_device_id *of_id;

		of_id = of_match_node(pca954x_of_match, dev->of_node);
		if (!of_id) {
			dev_err(dev, "could not match dt node\n");
			return -ENODEV;
		}
		return (int)of_id->data;
	}

	return id->driver_data;
}
#else
static int pca954x_get_device_type(struct device *dev,
				const struct i2c_device_id *id)
{
	return id->driver_data;
}
#endif

/* Write to mux register. Don't use i2c_transfer()/i2c_smbus_xfer()
   for this as they will try to lock adapter a second time */
static int pca954x_reg_write(struct i2c_adapter *adap,
			     struct i2c_client *client, u8 val)
{
	int ret = -ENODEV;
	struct pca954x *data = i2c_get_clientdata(client);

	/* Increase ref count for pca954x vcc */
	if (data->vcc_reg) {
		ret = regulator_enable(data->vcc_reg);
		if (ret) {
			dev_err(&client->dev, "%s: failed to enable vcc\n",
				__func__);
			goto vcc_regulator_failed;
		}
	}
	/* Increase ref count for pca954x vcc-pullup */
	if (data->pullup_reg) {
		ret = regulator_enable(data->pullup_reg);
		if (ret) {
			dev_err(&client->dev, "%s: failed to enable vcc-pullup\n",
				__func__);
			goto pullup_regulator_failed;
		}
	}

	if (adap->algo->master_xfer) {
		struct i2c_msg msg;
		char buf[1];

		msg.addr = client->addr;
		msg.flags = 0;
		msg.len = 1;
		buf[0] = val;
		msg.buf = buf;
		ret = adap->algo->master_xfer(adap, &msg, 1);
	} else {
		union i2c_smbus_data data;
		ret = adap->algo->smbus_xfer(adap, client->addr,
					     client->flags,
					     I2C_SMBUS_WRITE,
					     val, I2C_SMBUS_BYTE, &data);
	}

	/* Decrease ref count for pca954x vcc-pullup */
	if (data->pullup_reg)
		regulator_disable(data->pullup_reg);

pullup_regulator_failed:
	/* Decrease ref count for pca954x vcc */
	if (data->vcc_reg)
		regulator_disable(data->vcc_reg);
vcc_regulator_failed:
	return ret;
}

static int pca954x_select_chan(struct i2c_adapter *adap,
			       void *client, u32 chan)
{
	struct pca954x *data = i2c_get_clientdata(client);
	const struct chip_desc *chip = &chips[data->type];
	u8 regval;
	int ret = 0;

	/* we make switches look like muxes, not sure how to be smarter */
	if (chip->muxtype == pca954x_ismux)
		regval = chan | chip->enable;
	else
		regval = 1 << chan;

	/* Only select the channel if its different from the last channel */
	if (data->last_chan != regval) {
		ret = pca954x_reg_write(adap, client, regval);
		data->last_chan = regval;
	}

	return ret;
}

static int pca954x_deselect_mux(struct i2c_adapter *adap,
				void *client, u32 chan)
{
	struct pca954x *data = i2c_get_clientdata(client);

	/* Deselect active channel */
	data->last_chan = 0;
	return pca954x_reg_write(adap, client, data->last_chan);
}

/*
 * I2C init/probing/exit functions
 */
static int pca954x_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct i2c_adapter *adap = to_i2c_adapter(client->dev.parent);
	struct pca954x_platform_data *pdata = client->dev.platform_data;
	int num, force, class;
	struct pca954x *data;
	int ret;

	if (!i2c_check_functionality(adap, I2C_FUNC_SMBUS_BYTE))
		return -ENODEV;

	data = devm_kzalloc(&client->dev, sizeof(struct pca954x), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);

	data->type = pca954x_get_device_type(&client->dev, id);
	if (data->type < 0)
		return data->type;

	/* Get regulator pointer for pca954x vcc */
	data->vcc_reg = devm_regulator_get(&client->dev, "vcc");
	if (PTR_ERR(data->vcc_reg) == -EPROBE_DEFER)
		data->vcc_reg = NULL;
	else if (IS_ERR(data->vcc_reg)) {
		ret = PTR_ERR(data->vcc_reg);
		dev_err(&client->dev, "vcc regualtor get failed, %d\n", ret);
		return ret;
	}

	/* Get regulator pointer for pca954x vcc-pullup */
	data->pullup_reg = devm_regulator_get(&client->dev, "vcc-pullup");
	if (IS_ERR(data->pullup_reg)) {
		dev_info(&client->dev, "vcc-pullup regulator not found\n");
		data->pullup_reg = NULL;
	}

	/* Increase ref count for pca954x vcc */
	if (data->vcc_reg) {
		ret = regulator_enable(data->vcc_reg);
		if (ret < 0) {
			dev_err(&client->dev, "failed to enable vcc\n");
			return ret;
		}
	}
	/* Increase ref count for pca954x vcc-pullup */
	if (data->pullup_reg) {
		ret = regulator_enable(data->pullup_reg);
		if (ret < 0) {
			dev_err(&client->dev, "failed to enable vcc-pullup\n");
			return ret;
		}
	}

	/*
	 * Power-On Reset takes time.
	 * I2C is ready after Power-On Reset.
	 */
	msleep(1);

	/* Write the mux register at addr to verify
	 * that the mux is in fact present. This also
	 * initializes the mux to disconnected state.
	 */
	ret = i2c_smbus_write_byte(client, 0);
	if (ret < 0) {
		dev_err(&client->dev, "Write to  device failed: %d\n", ret);
		goto exit_regulator_disable;
	}

	/* Decrease ref count for pca954x vcc */
	if (data->vcc_reg)
		regulator_disable(data->vcc_reg);
	/* Decrease ref count for pca954x vcc-pullup */
	if (data->pullup_reg)
		regulator_disable(data->pullup_reg);

	data->last_chan = 0;		   /* force the first selection */

	/* Now create an adapter for each channel */
	for (num = 0; num < chips[data->type].nchans; num++) {
		bool deselect_on_exit = false;

		force = 0;			  /* dynamic adap number */
		class = 0;			  /* no class by default */
		if (pdata) {
			if (num < pdata->num_modes) {
				/* force static number */
				force = pdata->modes[num].adap_id;
				class = pdata->modes[num].class;
				deselect_on_exit =
					pdata->modes[num].deselect_on_exit;
			} else
				/* discard unconfigured channels */
				break;
		}
		if (client->dev.of_node)
			deselect_on_exit = true;

		data->virt_adaps[num] =
			i2c_add_mux_adapter(adap, &client->dev, client,
				force, num, class, pca954x_select_chan,
				(deselect_on_exit)
					? pca954x_deselect_mux : NULL);

		if (IS_ERR(data->virt_adaps[num])) {
			ret = PTR_ERR(data->virt_adaps[num]);
			dev_err(&client->dev,
				"failed to register multiplexed adapter"
				" %d as bus %d\n", num, force);
			goto virt_reg_failed;
		}
	}

	dev_info(&client->dev,
		 "registered %d multiplexed busses for I2C %s %s\n",
		 num, chips[data->type].muxtype == pca954x_ismux
				? "mux" : "switch", client->name);

	return 0;

virt_reg_failed:
	for (num--; num >= 0; num--)
		i2c_del_mux_adapter(data->virt_adaps[num]);
exit_regulator_disable:
	if (data->pullup_reg)
		regulator_disable(data->pullup_reg);
	if (data->vcc_reg)
		regulator_disable(data->vcc_reg);
	return ret;
}

static int pca954x_remove(struct i2c_client *client)
{
	struct pca954x *data = i2c_get_clientdata(client);
	const struct chip_desc *chip = &chips[data->type];
	int i;

	for (i = 0; i < chip->nchans; ++i)
		if (data->virt_adaps[i]) {
			i2c_del_mux_adapter(data->virt_adaps[i]);
			data->virt_adaps[i] = NULL;
		}

	return 0;
}

static struct i2c_driver pca954x_driver = {
	.driver		= {
		.name	= "pca954x",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(pca954x_of_match),
	},
	.probe		= pca954x_probe,
	.remove		= pca954x_remove,
	.id_table	= pca954x_id,
};

module_i2c_driver(pca954x_driver);

MODULE_AUTHOR("Rodolfo Giometti <giometti@linux.it>");
MODULE_DESCRIPTION("PCA954x I2C mux/switch driver");
MODULE_LICENSE("GPL v2");
