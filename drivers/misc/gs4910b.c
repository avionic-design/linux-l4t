/*
 * Copyright 2018 Alban Bedel <alban.bedel@avionic-design.de>
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
#include <linux/of_irq.h>

#define GENLOCK_CONTROL		0x16
#define VIDEO_CONTROL		0x4C

static const struct regmap_range gs4910b_regmap_rw_ranges[] = {
	regmap_reg_range(0x0A, 0x6A),
	regmap_reg_range(0x81, 0x83),
};

static const struct regmap_access_table gs4910b_regmap_access = {
	.yes_ranges = gs4910b_regmap_rw_ranges,
	.n_yes_ranges = ARRAY_SIZE(gs4910b_regmap_rw_ranges),
};

static const struct regmap_config gs4910b_regmap_config = {
	.name = "gs4910b",
	.reg_bits = 16,
	.reg_stride = 1,
	.val_bits = 16,
	.max_register = 0x83,
	/* Set bit 15 for read and 12 for extended addresses.
	 * The value set here apply to the top byte, so bit 7 and 4. */
	.read_flag_mask = BIT(7) | BIT(4),
	.write_flag_mask = BIT(4),
	.rd_table = &gs4910b_regmap_access,
	.wr_table = &gs4910b_regmap_access,
};

struct gs4910b {
	struct regmap *regmap;
	struct gpio_desc *reset_gpio;
	struct mutex lock;

	struct work_struct ref_lost_work;
	struct work_struct lock_lost_work;
};

struct gs4910b_attribute {
	struct device_attribute attr;
	unsigned int reg;
	unsigned int shift;
	unsigned int mask;
};

#define GS4910B_ATTR(_name, _mode, _reg, _shift, _mask)		\
	struct gs4910b_attribute dev_attr_##_name = {		\
		.attr = __ATTR(_name, _mode,			\
			gs4910b_show_reg, gs4910b_store_reg),	\
		.reg = _reg,					\
		.shift = _shift,				\
		.mask = _mask,					\
	}

static ssize_t gs4910b_show_reg(
	struct device *device, struct device_attribute *mattr, char *buf)
{
	struct gs4910b_attribute *attr =
		container_of(mattr, struct gs4910b_attribute, attr);
	struct gs4910b *gs = dev_get_drvdata(device);
	unsigned int val;
	int err;

	err = regmap_read(gs->regmap, attr->reg, &val);
	if (err)
		return err;

	return sprintf(buf, "%u\n", (val >> attr->shift) & attr->mask);
}

static ssize_t gs4910b_store_reg(
	struct device *device, struct device_attribute *mattr,
	const char *buf, size_t len)
{
	struct gs4910b_attribute *attr =
		container_of(mattr, struct gs4910b_attribute, attr);
	struct gs4910b *gs = dev_get_drvdata(device);
	unsigned int val;
	int err;

	err = sscanf(buf, "%u", &val);
	if (err != 1)
		return -EINVAL;

	err = regmap_update_bits(gs->regmap, attr->reg,
				attr->mask << attr->shift,
				(val & attr->mask) << attr->shift);

	return err ? err : len;
}

static GS4910B_ATTR(input_standard, S_IRUGO, 0x0F, 0, 0x3F);
static GS4910B_ATTR(reference_present, S_IRUGO, 0x15, 0, 1);
static GS4910B_ATTR(clock_lock, S_IRUGO, 0x15, 1, 1);
static GS4910B_ATTR(h_lock, S_IRUGO, 0x15, 2, 1);
static GS4910B_ATTR(v_lock, S_IRUGO, 0x15, 3, 1);
static GS4910B_ATTR(f_lock, S_IRUGO, 0x15, 4, 1);
static GS4910B_ATTR(reference_lock, S_IRUGO, 0x15, 5, 1);
static GS4910B_ATTR(n_genlock, S_IRUGO | S_IWUSR, 0x16, 0, 1);
static GS4910B_ATTR(drift, S_IRUGO | S_IWUSR, 0x16, 1, 1);
static GS4910B_ATTR(h_offset, S_IRUGO | S_IWUSR, 0x1B, 0, 0xFFFF);
static GS4910B_ATTR(v_offset, S_IRUGO | S_IWUSR, 0x1C, 0, 0xFFFF);
static GS4910B_ATTR(vid_std, S_IRUGO | S_IWUSR, 0x4D, 0, 0x3F);
static GS4910B_ATTR(clocks_per_line, S_IRUGO | S_IWUSR, 0x4E, 0, 0xFFFF);
static GS4910B_ATTR(clocks_per_hsync, S_IRUGO | S_IWUSR, 0x4F, 0, 0xFFFF);
static GS4910B_ATTR(hsync_to_sav, S_IRUGO | S_IWUSR, 0x50, 0, 0xFFFF);
static GS4910B_ATTR(hsync_to_eav, S_IRUGO | S_IWUSR, 0x51, 0, 0xFFFF);
static GS4910B_ATTR(lines_per_field, S_IRUGO | S_IWUSR, 0x52, 0, 0xFFFF);
static GS4910B_ATTR(lines_per_vsync, S_IRUGO | S_IWUSR, 0x53, 0, 0xFFFF);
static GS4910B_ATTR(vsync_to_first_active_line, S_IRUGO | S_IWUSR,
		    0x54, 0, 0xFFFF);
static GS4910B_ATTR(vsync_to_last_active_line, S_IRUGO | S_IWUSR,
		    0x55, 0, 0xFFFF);
static GS4910B_ATTR(hsync_polarity, S_IRUGO | S_IWUSR, 0x56, 0, 1);
static GS4910B_ATTR(vsync_polarity, S_IRUGO | S_IWUSR, 0x56, 2, 1);

static struct attribute *gs4910b_attrs[] = {
	&dev_attr_input_standard.attr.attr,
	&dev_attr_reference_present.attr.attr,
	&dev_attr_clock_lock.attr.attr,
	&dev_attr_h_lock.attr.attr,
	&dev_attr_v_lock.attr.attr,
	&dev_attr_f_lock.attr.attr,
	&dev_attr_reference_lock.attr.attr,
	&dev_attr_n_genlock.attr.attr,
	&dev_attr_drift.attr.attr,
	&dev_attr_h_offset.attr.attr,
	&dev_attr_v_offset.attr.attr,
	&dev_attr_vid_std.attr.attr,
	&dev_attr_clocks_per_line.attr.attr,
	&dev_attr_clocks_per_hsync.attr.attr,
	&dev_attr_hsync_to_sav.attr.attr,
	&dev_attr_hsync_to_eav.attr.attr,
	&dev_attr_lines_per_field.attr.attr,
	&dev_attr_lines_per_vsync.attr.attr,
	&dev_attr_vsync_to_first_active_line.attr.attr,
	&dev_attr_vsync_to_last_active_line.attr.attr,
	&dev_attr_hsync_polarity.attr.attr,
	&dev_attr_vsync_polarity.attr.attr,
	NULL,
};

static const struct attribute_group gs4910b_attr_grp = {
	.attrs = gs4910b_attrs,
};

static void gs4910b_ref_lost_notify(struct work_struct *work)
{
	struct gs4910b *gs = container_of(
		work, struct gs4910b, ref_lost_work);
	struct device *dev = regmap_get_device(gs->regmap);

	sysfs_notify(&dev->kobj, NULL, "reference_present");
	kobject_uevent(&dev->kobj, KOBJ_CHANGE);
}

static irqreturn_t gs4910b_ref_lost_irq_handler(int irq, void *ctx)
{
	struct gs4910b *gs = ctx;

	schedule_work(&gs->ref_lost_work);

	return IRQ_HANDLED;
}

static void gs4910b_lock_lost_notify(struct work_struct *work)
{
	struct gs4910b *gs = container_of(
		work, struct gs4910b, ref_lost_work);
	struct device *dev = regmap_get_device(gs->regmap);

	sysfs_notify(&dev->kobj, NULL, "reference_lock");
	kobject_uevent(&dev->kobj, KOBJ_CHANGE);
}

static irqreturn_t gs4910b_lock_lost_irq_handler(int irq, void *ctx)
{
	struct gs4910b *gs = ctx;

	schedule_work(&gs->lock_lost_work);

	return IRQ_HANDLED;
}

static int gs4910b_probe(struct spi_device *spi)
{
	int lock_lost_irq = 0;
	struct gs4910b *gs;
	int err;

	gs = devm_kzalloc(&spi->dev, sizeof(*gs), GFP_KERNEL);
	if (!gs)
		return -ENOMEM;

	mutex_init(&gs->lock);
	INIT_WORK(&gs->lock_lost_work, gs4910b_lock_lost_notify);
	INIT_WORK(&gs->ref_lost_work, gs4910b_ref_lost_notify);

	gs->regmap = devm_regmap_init_spi(spi, &gs4910b_regmap_config);
	if (IS_ERR(gs->regmap)) {
		dev_err(&spi->dev, "regmap init failed: %ld\n",
			PTR_ERR(gs->regmap));
		return PTR_ERR(gs->regmap);
	}

	gs->reset_gpio = devm_gpiod_get_optional(
		&spi->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(gs->reset_gpio))
		return PTR_ERR(gs->reset_gpio);

	if (gs->reset_gpio) {
		usleep_range(500, 10000);
		gpiod_set_value_cansleep(gs->reset_gpio, 0);
	};

	spi_set_drvdata(spi, gs);

	/* Set VID_STD from host */
	err = regmap_update_bits(gs->regmap, VIDEO_CONTROL, BIT(1), BIT(1));

	/* Set GENLOCK from host */
	if (!err)
		err = regmap_update_bits(gs->regmap, GENLOCK_CONTROL,
					BIT(5), BIT(5));

	if (err) {
		dev_err(&spi->dev, "Failed to set initial config\n");
		return err;
	}

	err = sysfs_create_group(&spi->dev.kobj, &gs4910b_attr_grp);
	if (err) {
		dev_err(&spi->dev, "Failed to create sysfs attributes\n");
		return err;
	}

	if (spi->irq) {
		err = devm_request_threaded_irq(
			&spi->dev, spi->irq,
			NULL, gs4910b_ref_lost_irq_handler,
			IRQF_ONESHOT, dev_name(&spi->dev), gs);
		if (err)
			dev_warn(&spi->dev,
				"Failed to request REF_LOST irq\n");
	}

	if (spi->dev.of_node)
		lock_lost_irq = irq_of_parse_and_map(spi->dev.of_node, 1);

	if (lock_lost_irq > 0) {
		err = devm_request_threaded_irq(
			&spi->dev, lock_lost_irq,
			NULL, gs4910b_lock_lost_irq_handler,
			IRQF_ONESHOT, dev_name(&spi->dev), gs);
		if (err)
			dev_warn(&spi->dev,
				"Failed to request LOCK_LOST irq\n");
	}

	return 0;
}

static int gs4910b_remove(struct spi_device *spi)
{
	sysfs_remove_group(&spi->dev.kobj, &gs4910b_attr_grp);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id gs4910b_of_table[] = {
	{ .compatible = "semtech,gs4910b" },
	{ .compatible = "semtech,gs4911b" },
	{ }
};
MODULE_DEVICE_TABLE(of, gs4910b_of_table);
#endif

static const struct spi_device_id gs4910b_id[] = {
	{ "gs4910b", 0 },
	{ "gs4911b", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, gs4910b_id);

static struct spi_driver gs4910b_driver = {
	.driver = {
		.of_match_table = of_match_ptr(gs4910b_of_table),
		.name		= "gs4910b",
		.owner		= THIS_MODULE,
	},

	.probe		= gs4910b_probe,
	.remove		= gs4910b_remove,
	.id_table	= gs4910b_id,
};

module_spi_driver(gs4910b_driver);

MODULE_DESCRIPTION("Driver for Gennum GS4910B/11B Graphics Clock and Timing Generator");
MODULE_AUTHOR("Alban Bedel <alban.bedel@avionic-design.de>");
MODULE_LICENSE("GPL");
