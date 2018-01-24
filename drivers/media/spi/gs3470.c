/*
 * Copyright 2017-2018 Alban Bedel <alban.bedel@avionic-design.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 *
 */

#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/of.h>

#include <media/v4l2-subdev.h>
#include <media/v4l2-async.h>
#include <media/v4l2-of.h>

#define HOST_CONF_REG_0		0x000
#define IOPROC_1		0x800
#define POWER_DOWN		0x811
#define IO_CONFIG		0x812
#define IO_CONFIG_2		0x813
#define RASTER_STRUC_1_DS1	0x830
#define TIM_861_CFG		0x837
#define INPUT_CONFIG		0x84D
#define LOS_CONTROL		0x86F
#define DELAY_LINE_CTRL_2	0x871
#define PIN_CSR_SELECT		0x877

#define PORT_SDI0		0
#define PORT_SDI1		1
#define PORT_DESER		2
#define PORT_SDO		3

#define RATE_HD 0
#define RATE_SD 1
#define RATE_3G 2

static const struct regmap_range gs3470_regmap_rw_ranges[] = {
	regmap_reg_range(0x000, 0x000),
	regmap_reg_range(0x800, 0x8D3),
	regmap_reg_range(0x989, 0x989),
	regmap_reg_range(0xA01, 0xA96),
	regmap_reg_range(0xB01, 0xB96),
	regmap_reg_range(0xC00, 0xFFF),
};

static const struct regmap_access_table gs3470_regmap_access = {
	.yes_ranges = gs3470_regmap_rw_ranges,
	.n_yes_ranges = ARRAY_SIZE(gs3470_regmap_rw_ranges),
};

static const struct regmap_config gs3470_regmap_config = {
	.name = "gs3470",
	.reg_bits = 32,
	.reg_stride = 1,
	.val_bits = 16,
	.max_register = 0xFFF,
	/* Set bit 31 for read and 29 for extended addresses.
	 * The value set here apply to the top byte, so bit 7 and 5. */
	.read_flag_mask = BIT(7) | BIT(5),
	.write_flag_mask = BIT(5),
	.rd_table = &gs3470_regmap_access,
	.wr_table = &gs3470_regmap_access,
};

struct gs3470 {
	struct v4l2_subdev sd;

	struct regmap *regmap;
	struct gpio_desc *reset_gpio;
	struct mutex lock;

	enum v4l2_mbus_pixelcode pixelcode;
	struct v4l2_mbus_framefmt framefmt;
	struct v4l2_dv_timings timings;
	unsigned ports;
};

static ssize_t gs3470_show_routing(
	struct device *device, char *buf, unsigned int port)
{
	struct spi_device *spi = container_of(device, struct spi_device, dev);
	struct v4l2_subdev *sd = spi_get_drvdata(spi);
	struct gs3470 *gs = container_of(sd, struct gs3470, sd);
	unsigned int input_cfg;
	int err;

	/* Check that this port is connected to something */
	if (!(gs->ports & BIT(port)))
		return -ENOLINK;

	mutex_lock(&gs->lock);

	err = regmap_read(gs->regmap, INPUT_CONFIG, &input_cfg);

	mutex_unlock(&gs->lock);

	if (err)
		return err;

	return sprintf(buf, "%u\n", (input_cfg >> port) & 1);
}

static ssize_t gs3470_store_routing(
	struct device *device, const char *buf, size_t len, unsigned int port)
{
	struct spi_device *spi = container_of(device, struct spi_device, dev);
	struct v4l2_subdev *sd = spi_get_drvdata(spi);
	struct gs3470 *gs = container_of(sd, struct gs3470, sd);
	unsigned int input_cfg;
	int err;

	/* Check that this port is connected to something */
	if (!(gs->ports & BIT(port)))
		return -ENOLINK;

	err = sscanf(buf, "%u", &input_cfg);
	if (err != 1)
		return -EINVAL;

	if (input_cfg > 1)
		return -ERANGE;

	/* Only allow selecting inputs connected to something */
	if (!(gs->ports & BIT(input_cfg)))
		return -ENOLINK;

	mutex_lock(&gs->lock);

	err = regmap_update_bits(gs->regmap, INPUT_CONFIG,
				 BIT(port), input_cfg << port);
	/* When changing the deserializer input we must also change
	 * the lock detector input. */
	if (!err && port == PORT_DESER)
		err = regmap_update_bits(gs->regmap, LOS_CONTROL,
					 BIT(8), input_cfg << 8);

	mutex_unlock(&gs->lock);

	return err ? err : len;
}

static ssize_t gs3470_show_deserializer(
	struct device *device, struct device_attribute *mattr, char *buf)
{
	return gs3470_show_routing(device, buf, PORT_DESER);
}

static ssize_t gs3470_store_deserializer(
	struct device *device, struct device_attribute *mattr,
	const char *buf, size_t len)
{
	return gs3470_store_routing(device, buf, len, PORT_DESER);
}

static ssize_t gs3470_show_loopback(
	struct device *device, struct device_attribute *mattr, char *buf)
{
	return gs3470_show_routing(device, buf, PORT_SDO);
}

static ssize_t gs3470_store_loopback(
	struct device *device, struct device_attribute *mattr,
	const char *buf, size_t len)
{
	return gs3470_store_routing(device, buf, len, PORT_SDO);
}

static DEVICE_ATTR(deserializer, S_IRUGO | S_IWUSR,
		   gs3470_show_deserializer, gs3470_store_deserializer);

static DEVICE_ATTR(loopback, S_IRUGO | S_IWUSR,
		   gs3470_show_loopback, gs3470_store_loopback);

static struct attribute *gs3470_attrs[] = {
	&dev_attr_deserializer.attr,
	&dev_attr_loopback.attr,
	NULL,
};

static const struct attribute_group gs3470_attr_grp = {
	.attrs = gs3470_attrs,
};

static int gs3470_read_input_format(struct v4l2_subdev *sd)
{
	struct gs3470 *gs = container_of(sd, struct gs3470, sd);
	struct spi_device *spi = v4l2_get_subdevdata(sd);
	struct v4l2_bt_timings bt = {};
	unsigned int raster_struc[4];
	unsigned int i, rate;
	int err;

	/* No need to always re-read the data if we already have it */
	if (spi->irq >= 0 && gs->framefmt.width && gs->framefmt.height)
		return 0;

	/* Read the raster struct */
	for (i = 0; i < ARRAY_SIZE(raster_struc); i++) {
		err = regmap_read(gs->regmap, RASTER_STRUC_1_DS1 + i,
				  &raster_struc[i]);
		if (err)
			return err;
	}

	/* Check that a mode has been detected */
	if (!(raster_struc[3] & BIT(12))) {
		dev_dbg(sd->dev, "No mode detected: raster4 = %x\n",
			 raster_struc[3]);
		return -ENODATA;
	}

	/* Get the rate and normalize as both 1 and 3 mean SD */
	rate = (raster_struc[3] >> 14) & 3;
	if (rate & RATE_SD)
		rate = RATE_SD;

	/* Read the picture size */
	bt.width = raster_struc[0] & 0x3FFF;
	bt.height = raster_struc[3] & 0x7FF;
	bt.hsync = (raster_struc[1] & 0x3FFF) - bt.width;
	bt.vsync = (raster_struc[2] & 0x7FF) - bt.height;
	bt.interlaced = !!(raster_struc[3] & BIT(11));

	if (bt.interlaced) {
		dev_err(sd->dev,
			 "Interlaced formats are not supported for now!\n");
		return -EINVAL;
	}

	switch (rate) {
	case RATE_3G:
		bt.pixelclock = 148500000;
		if (gs->pixelcode != V4L2_MBUS_FMT_UYVY10_1X20) {
			dev_err(sd->dev,
				"3G formats with 10 bits bus need DDR\n");
			return -EINVAL;
		}
		break;
	case RATE_HD:
		bt.pixelclock = 74250000;
		break;
	case RATE_SD:
		bt.pixelclock = 13500000;
		break;
	default:
		/* Should not happen */
		return -EINVAL;
	}

	/* Apply M = 1.001 if needed */
	if (raster_struc[3] & BIT(13)) {
		bt.pixelclock *= 1000;
		do_div(bt.pixelclock, 1001);
	}

	gs->timings.type = V4L2_DV_BT_656_1120;
	gs->timings.bt = bt;

	/* Fill the framefmt */
	gs->framefmt.width = bt.width;
	gs->framefmt.height = bt.height;
	gs->framefmt.code = gs->pixelcode;
	gs->framefmt.field = V4L2_FIELD_NONE;

	dev_dbg(&spi->dev, "Got mode: %u(+%u)x%u(+%u)%c @ %dx%uHz\n",
		bt.width, bt.hsync, bt.height, bt.vsync,
		bt.interlaced ? 'i' : 'p',
		gs->pixelcode == V4L2_MBUS_FMT_UYVY10_1X20 ? 1 : 2,
		(unsigned int)bt.pixelclock);
	return 0;
}

static int gs3470_query_dv_timings(
	struct v4l2_subdev *sd, struct v4l2_dv_timings *timings)
{
	struct gs3470 *gs = container_of(sd, struct gs3470, sd);
	int err;

	mutex_lock(&gs->lock);

	err = gs3470_read_input_format(sd);
	if (!err)
		*timings = gs->timings;

	mutex_unlock(&gs->lock);

	return err;
}

static int gs3470_g_mbus_fmt(
	struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *fmt)
{
	struct gs3470 *gs = container_of(sd, struct gs3470, sd);
	int err;

	mutex_lock(&gs->lock);

	err = gs3470_read_input_format(sd);
	if (!err)
		*fmt = gs->framefmt;

	mutex_unlock(&gs->lock);

	return err;
}

static int gs3470_enum_mbus_fmt(
	struct v4l2_subdev *sd, unsigned int index,
	enum v4l2_mbus_pixelcode *code)
{
	struct gs3470 *gs = container_of(sd, struct gs3470, sd);

	if (index > 0)
		return -EINVAL;

	*code = gs->pixelcode;

	return 0;
}

static int gs3470_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct gs3470 *gs = container_of(sd, struct gs3470, sd);

	int err;

	mutex_lock(&gs->lock);

	err = gs3470_read_input_format(sd);

	mutex_unlock(&gs->lock);

	return err;
}

static const struct v4l2_subdev_video_ops gs3470_video_ops = {
	.query_dv_timings = gs3470_query_dv_timings,
	.g_mbus_fmt = gs3470_g_mbus_fmt,
	.try_mbus_fmt = gs3470_g_mbus_fmt,
	.s_mbus_fmt = gs3470_g_mbus_fmt,
	.enum_mbus_fmt = gs3470_enum_mbus_fmt,
	.g_input_status = gs3470_g_input_status,
};

static const struct v4l2_subdev_ops gs3470_ops = {
	.video = &gs3470_video_ops,
};

static irqreturn_t gs3470_locked_irq_handler(int irq, void *ctx)
{
	struct gs3470 *gs = ctx;

	/* Clear the stored mode to force a reload */
	mutex_lock(&gs->lock);

	memset(&gs->framefmt, 0, sizeof(gs->framefmt));
	memset(&gs->timings, 0, sizeof(gs->timings));

	mutex_unlock(&gs->lock);

	return IRQ_HANDLED;
}

static int gs3470_probe(struct spi_device *spi)
{
	const struct device_node *of_node = spi->dev.of_node;
	unsigned int pin_csr_select = BIT(1);
	unsigned int delay_line_ctrl_2 = 0;
	struct device_node *np = NULL;
	unsigned int tim_861_cfg = 0;
	bool gspi_link_disable;
	bool gspi_bus_through;
	struct gs3470 *gs;
	bool timing_861;
	u32 stat_mux[6];
	int i, err;

	gs = devm_kzalloc(&spi->dev, sizeof(*gs), GFP_KERNEL);
	if (!gs)
		return -ENOMEM;

	/* Read the ports config from OF */
	while ((np = v4l2_of_get_next_endpoint(of_node, np))) {
		struct v4l2_of_endpoint ep;

		if (!of_device_is_available(np)) {
			of_node_put(np);
			continue;
		}

		v4l2_of_parse_endpoint(np, &ep);
		of_node_put(np);

		if (ep.port == PORT_DESER) {
			struct v4l2_of_bus_parallel *bus = &ep.bus.parallel;

			if (ep.id > 0) {
				dev_err(&spi->dev,
					"Too many output endpoints\n");
				return -EINVAL;
			}

			if (ep.bus_type != V4L2_MBUS_PARALLEL) {
				dev_err(&spi->dev, "The output port 2 "
					"should be a parallel bus\n");
				return -EINVAL;
			}

			if (bus->bus_width == 20 && bus->data_shift == 0) {
				gs->pixelcode = V4L2_MBUS_FMT_UYVY10_1X20;
				pin_csr_select |= BIT(2);
			} else if (bus->bus_width == 10 &&
				   bus->data_shift == 10) {
				gs->pixelcode = V4L2_MBUS_FMT_UYVY10_2X10;
			} else if (bus->bus_width == 8 &&
				   bus->data_shift == 12) {
				gs->pixelcode = V4L2_MBUS_FMT_UYVY8_2X8;
			}

			if (bus->flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)
				delay_line_ctrl_2 |= BIT(0) | BIT(1) | BIT(2);
			if (bus->flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)
				tim_861_cfg |= BIT(1);
			if (bus->flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)
				tim_861_cfg |= BIT(2);
		}

		gs->ports |= BIT(ep.port);
	}

	/* If no input have been defined assume both are in use. */
	if ((gs->ports & (PORT_SDI0 | PORT_SDI1)) == 0)
		gs->ports |= PORT_SDI0 | PORT_SDI1;

	if (!gs->pixelcode) {
		dev_err(&spi->dev, "Unsupported parallel bus configuration\n");
		return -EINVAL;
	}

	/* Initialize the stat mux array with the chip defaults */
	for (i = 0; i < ARRAY_SIZE(stat_mux); i++)
		stat_mux[i] = i < 5 ? i : 6;

	/* Get the values from OF, if there is any */
	of_property_read_u32_array(
		of_node, "stat-mux", stat_mux, ARRAY_SIZE(stat_mux));

	/* Get the timing type we need */
	timing_861 = of_property_read_bool(of_node, "timing-861");

	/* Get the GSPI bus mode */
	gspi_link_disable = of_property_read_bool(of_node, "gspi-link-disable");
	gspi_bus_through = of_property_read_bool(of_node, "gspi-bus-through");

	mutex_init(&gs->lock);

	gs->regmap = devm_regmap_init_spi(spi, &gs3470_regmap_config);
	if (IS_ERR(gs->regmap)) {
		dev_err(&spi->dev, "regmap init failed: %ld\n",
			PTR_ERR(gs->regmap));
		return PTR_ERR(gs->regmap);
	}

	gs->reset_gpio = devm_gpiod_get_optional(
		&spi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gs->reset_gpio))
		return PTR_ERR(gs->reset_gpio);

	/* Setup the GSPI bus mode */
	err = regmap_update_bits(gs->regmap, HOST_CONF_REG_0,
				BIT(13) | BIT(14),
				(gspi_bus_through << 13) |
				(gspi_link_disable << 14));
	if (err) {
		dev_err(&spi->dev, "Failed to set GSPI mode.\n");
		return err;
	}

	/* Setup the timing type */
	err = regmap_update_bits(gs->regmap, IOPROC_1,
				BIT(6), timing_861 << 6);
	if (err) {
		dev_err(&spi->dev, "Failed to set the timing type\n");
		return err;
	}

	/* Setup the STAT pins mux */
	err = regmap_write(gs->regmap, IO_CONFIG,
			((stat_mux[0] & 0x1F) << 0) |
			((stat_mux[1] & 0x1F) << 5) |
			((stat_mux[2] & 0x1F) << 10));
	if (err) {
		dev_err(&spi->dev, "Failed to set STAT0-2 mux\n");
		return err;
	}

	err = regmap_write(gs->regmap, IO_CONFIG_2,
			((stat_mux[3] & 0x1F) << 0) |
			((stat_mux[4] & 0x1F) << 5) |
			((stat_mux[5] & 0x1F) << 10));
	if (err) {
		dev_err(&spi->dev, "Failed to set STAT3-5 mux\n");
		return err;
	}

	/* Setup the bus width */
	err = regmap_update_bits(gs->regmap, PIN_CSR_SELECT,
				 BIT(1) | BIT(2), pin_csr_select);
	if (err) {
		dev_err(&spi->dev, "Failed to set the bus width\n");
		return err;
	}

	/* Setup the pixel clock polarity */
	err = regmap_update_bits(gs->regmap, DELAY_LINE_CTRL_2,
				 BIT(0) | BIT(1) | BIT(2), delay_line_ctrl_2);
	if (err) {
		dev_err(&spi->dev, "Failed to set the pixel clock polarity\n");
		return err;
	}

	/* Setup the v/hsync polarity */
	err = regmap_update_bits(gs->regmap, TIM_861_CFG,
				 BIT(1) | BIT(2), tim_861_cfg);
	if (err) {
		dev_err(&spi->dev, "Failed to set the v/hsync polarity\n");
		return err;
	}

	/* Enable the DDO port in loopback mode */
	if (gs->ports & PORT_SDO) {
		err = regmap_update_bits(gs->regmap, POWER_DOWN,
					 BIT(1) | BIT(2), BIT(1) | BIT(2));
		if (err) {
			dev_err(&spi->dev, "Failed to setup serial loopback\n");
			return err;
		}
	}

	/* If SDI1 is not in use switch to SDI0 */
	if (!(gs->ports & PORT_SDI1)) {
		err = regmap_update_bits(gs->regmap, INPUT_CONFIG, BIT(2), 0);
		if (!err)
			err = regmap_update_bits(gs->regmap, LOS_CONTROL,
						 BIT(8), 0);
		if (err) {
			dev_err(&spi->dev, "Failed to switch input port\n");
			return err;
		}
	}

	v4l2_spi_subdev_init(&gs->sd, spi, &gs3470_ops);

	if (spi->irq >= 0) {
		err = devm_request_threaded_irq(&spi->dev, spi->irq,
						NULL, gs3470_locked_irq_handler,
						IRQF_ONESHOT, dev_name(&spi->dev), gs);
		if (err) {
			dev_warn(&spi->dev, "Failed to request IRQ %d\n", spi->irq);
			spi->irq = -1;
		}
	}

	err = sysfs_create_group(&spi->dev.kobj, &gs3470_attr_grp);
	if (err) {
		dev_err(&spi->dev, "Failed to create sysfs attributes\n");
		return err;
	}

	err = v4l2_async_register_subdev(&gs->sd);
	if (err) {
		sysfs_remove_group(&spi->dev.kobj, &gs3470_attr_grp);
		return err;
	}

	return 0;
}

static int gs3470_remove(struct spi_device *spi)
{
	struct v4l2_subdev *sd = spi_get_drvdata(spi);
	struct gs3470 *gs = container_of(sd, struct gs3470, sd);

	v4l2_async_unregister_subdev(&gs->sd);
	sysfs_remove_group(&sd->dev->kobj, &gs3470_attr_grp);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id gs3470_of_table[] = {
	{ .compatible = "semtech,gs3470" },
	{ }
};
MODULE_DEVICE_TABLE(of, gs3470_of_table);
#endif

static const struct spi_device_id gs3470_id[] = {
	{ "gs3470", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, gs3470_id);

static struct spi_driver gs3470_driver = {
	.driver = {
		.of_match_table = of_match_ptr(gs3470_of_table),
		.name		= "gs3470",
		.owner		= THIS_MODULE,
	},

	.probe		= gs3470_probe,
	.remove		= gs3470_remove,
	.id_table	= gs3470_id,
};

module_spi_driver(gs3470_driver);

MODULE_DESCRIPTION("Driver for Semtech GS3470 3G/HD/SD-SDI Receiver");
MODULE_AUTHOR("Alban Bedel <alban.bedel@avionic-design.de>");
MODULE_LICENSE("GPL");
