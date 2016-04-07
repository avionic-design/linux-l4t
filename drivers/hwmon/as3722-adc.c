/*
 * AMS 3722 ADC driver
 *
 * Copyright (C) 2016 Avionic Design GmbH
 * Copyright (C) 2016 Nikolaus Schulz <nikolaus.schulz@avionic-design.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 *
 * Based on revision [0-33] of the datasheet for the AS3722 from
 * 2014-02-20.
 */

#include <linux/platform_device.h>
#include <linux/mfd/as3722-plat.h>
#include <linux/mfd/as3722.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon.h>
#include <linux/module.h>
#include <linux/delay.h>

struct as3722_adc_chaninfo {
	struct device_attribute attr_input;
	struct device_attribute attr_label;
	const char              *label;
	enum as3722_adc_source  src;
	int offset;
	int scale;
	int sd;
};
#define to_as3722_adc_chaninfo(attr, name) \
	container_of(attr, struct as3722_adc_chaninfo, name)

struct as3722_adc {
	struct as3722 *as3722;
	struct device *dev;
	struct mutex  lock;
};

static ssize_t show_input(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t show_label(struct device *dev,
		struct device_attribute *attr, char *buf);

/*
 * Constants for the conversion of result register values to
 * millivolt/millidegrees. Taken from figure 69 (ADC input sources) in
 * the datasheet, p.65.
 */
#define AS3722_ADC_VOLT_SCALE_LOW  156
#define AS3722_ADC_VOLT_SCALE_HIGH 625
#define AS3722_ADC_VOLT_VBAT_SCALE 2344

#define AS3722_ADC_TEMP_SD_SCALE   (-37340)
#define AS3722_ADC_TEMP_SD_OFF     32650000
#define AS3722_ADC_TEMP_DIE_SCALE  76980
#define AS3722_ADC_TEMP_DIE_OFF    (-27400000)

/* Scaling factor relative to sysfs units */
#define AS3722_ADC_PRECISION       100

#define AS3722_ADC_SENSOR_ATTR(name, item) \
	__ATTR(name##_##item, S_IRUGO,  show_##item, NULL)

#define AS3722_ADC_CHANINFO(_label, _src, _off, _scale, _sd, _name) \
	{ \
		.attr_input = AS3722_ADC_SENSOR_ATTR(_name, input), \
		.attr_label = AS3722_ADC_SENSOR_ATTR(_name, label), \
		.label      = _label, \
		.src        = _src, \
		.offset     = _off, \
		.scale      = _scale, \
		.sd         = _sd, \
	}

#define AS3722_ADC_CHANINFO_VOLT(label, src, scale, index) \
	AS3722_ADC_CHANINFO(label, src, 0, scale, -1, in##index)

#define AS3722_ADC_CHANINFO_TEMP_SD(label, src, sd, index) \
	AS3722_ADC_CHANINFO(label, src, AS3722_ADC_TEMP_SD_OFF, \
			AS3722_ADC_TEMP_SD_SCALE, sd, temp##index)

static struct as3722_adc_chaninfo as3722_adc_chaninfo[] = {
	AS3722_ADC_CHANINFO("DIE temperature", AS3722_ADC_TEMP_SENSOR,
			 AS3722_ADC_TEMP_DIE_OFF, AS3722_ADC_TEMP_DIE_SCALE,
			-1, temp1),
	AS3722_ADC_CHANINFO_VOLT("VSUP", AS3722_ADC_VSUP,
			AS3722_ADC_VOLT_SCALE_HIGH, 1),
	AS3722_ADC_CHANINFO_VOLT("VBAT", AS3722_ADC_VBAT,
			AS3722_ADC_VOLT_VBAT_SCALE, 2),
	AS3722_ADC_CHANINFO_TEMP_SD("TEMP1_SD0", AS3722_ADC_TEMP1_SD0, 0, 2),
	AS3722_ADC_CHANINFO_TEMP_SD("TEMP2_SD0", AS3722_ADC_TEMP2_SD0, 0, 3),
	AS3722_ADC_CHANINFO_TEMP_SD("TEMP3_SD0", AS3722_ADC_TEMP3_SD0, 0, 4),
	AS3722_ADC_CHANINFO_TEMP_SD("TEMP4_SD0", AS3722_ADC_TEMP4_SD0, 0, 5),
	AS3722_ADC_CHANINFO_TEMP_SD("TEMP_SD1",  AS3722_ADC_TEMP_SD1,  1, 6),
	AS3722_ADC_CHANINFO_TEMP_SD("TEMP1_SD6", AS3722_ADC_TEMP1_SD6, 6, 7),
	AS3722_ADC_CHANINFO_TEMP_SD("TEMP2_SD6", AS3722_ADC_TEMP2_SD6, 6, 8),
};

static struct attribute
	*as3722_adc_attrs[ARRAY_SIZE(as3722_adc_chaninfo) * 2 + 1];
ATTRIBUTE_GROUPS(as3722_adc);

static int read_adc_channel(const struct device *dev,
		struct as3722_adc_chaninfo *info)
{
	struct as3722_adc *as3722_adc = dev_get_drvdata(dev);
	struct as3722 *as3722 = as3722_adc->as3722;
	int ret, try, result;
	u32 val;

	mutex_lock(&as3722_adc->lock);

	/* If we're reading the temp from an sd, check if the sd is enabled. */
	if (info->sd >= 0) {
		ret = as3722_read(as3722, AS3722_SD_CONTROL_REG, &val);
		if (ret < 0) {
			dev_err(dev, "error checking sd enable: %d\n", ret);
			goto out;
		}

		if ((val & AS3722_SDn_CTRL(info->sd)) == 0) {
			dev_dbg(dev, "can't read temp: sd%d disabled\n",
					info->sd);
			ret = -EAGAIN;
			goto out;
		}
	}

	/* Initiate conversion */
	ret = as3722_write(as3722, AS3722_ADC0_CONTROL_REG,
			AS3722_ADC0_CONV_START |
			info->src);
	if (ret < 0) {
		dev_err(dev, "error initiating conversion: %d\n", ret);
		goto out;
	}

	/* Wait for conversion to start */
	for (try = 2; ; --try) {
		ret = as3722_read(as3722, AS3722_ADC0_CONTROL_REG, &val);
		if (ret < 0) {
			dev_err(dev, "error checking adc0 cfg: %d\n", ret);
			goto out;
		}

		if ((val & AS3722_ADC0_CONV_START) == 0)
			break;

		if (!try) {
			dev_err(dev, "timeout waiting for conversion start\n");
			ret = -EBUSY;
			goto out;
		}

		/*
		 * adc1 might block the conversion unit.  According to
		 * the ADC timing diagram in the datasheet (figure 71,
		 * p.68), sampling for adc0 then starts when the
		 * conversion for adc1 is complete.
		 * adc0_start_conversion is set till the conversion
		 * starts for adc0.  Sampling time is either 32 or 64us,
		 * and typical conversion time at 25°C is 40 us.  So,
		 * wait twice the sampling time plus once the conversion
		 * time, adding a safety margin of 5us to the latter.
		 */
		usleep_range(173, 200);
	}

	/* Wait for conversion to complete */
	for (try = 2; ; --try) {
		ret = as3722_read(as3722, AS3722_ADC0_MSB_RESULT_REG, &val);
		if (ret < 0) {
			dev_err(dev, "error reading adc0 result msb: %d\n",
				ret);
			goto out;
		}

		if ((val & AS3722_ADC0_CONV_NOTREADY) == 0)
			break;

		if (!try) {
			dev_err(dev, "timeout waiting for adc0 result\n");
			ret = -EBUSY;
			goto out;
		}

		/* Conversion time should be max. 45us */
		usleep_range(45, 70);
	}

	result = (val & AS3722_ADC_MASK_MSB_VAL) << 3;

	ret = as3722_read(as3722, AS3722_ADC0_LSB_RESULT_REG, &val);
	if (ret < 0) {
		dev_err(dev, "error reading adc0 result lsb: %d\n", ret);
		goto out;
	}

	result |= val & AS3722_ADC_MASK_LSB_VAL;

	ret = result;
out:
	mutex_unlock(&as3722_adc->lock);
	return ret;
}

static ssize_t show_input(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct as3722_adc_chaninfo *info =
		to_as3722_adc_chaninfo(attr, attr_input);
	int val;

	val = read_adc_channel(dev, info);
	if (val < 0)
		return val;

	val = (info->scale * val + info->offset) / AS3722_ADC_PRECISION;

	return sprintf(buf, "%d\n", val);
}

static ssize_t show_label(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct as3722_adc_chaninfo *info =
		to_as3722_adc_chaninfo(attr, attr_label);

	return sprintf(buf, "%s\n", info->label);
}

static int as3722_adc_probe(struct platform_device *pdev)
{
	struct as3722_adc *as3722_adc;
	struct device *hwmon;
	unsigned i, j;

	as3722_adc = devm_kzalloc(&pdev->dev, sizeof(*as3722_adc),
			GFP_KERNEL);
	if (!as3722_adc)
		return -ENOMEM;

	mutex_init(&as3722_adc->lock);

	as3722_adc->as3722 = dev_get_drvdata(pdev->dev.parent);

	platform_set_drvdata(pdev, as3722_adc);

	for (i = 0, j = 0; i < ARRAY_SIZE(as3722_adc_chaninfo); ++i) {
		as3722_adc_attrs[j++] =
			&as3722_adc_chaninfo[i].attr_input.attr;
		as3722_adc_attrs[j++] =
			&as3722_adc_chaninfo[i].attr_label.attr;
	}
	as3722_adc_attrs[j] = NULL;

	hwmon = devm_hwmon_device_register_with_groups(&pdev->dev, "as3722",
			as3722_adc, as3722_adc_groups);
	if (IS_ERR(hwmon))
		return PTR_ERR(hwmon);

	return 0;
}

static int as3722_adc_remove(struct platform_device *pdev)
{
	struct as3722_adc *as3722_adc = platform_get_drvdata(pdev);

	mutex_destroy(&as3722_adc->lock);

	return 0;
}

static struct platform_driver as3722_adc_driver = {
	.probe  = as3722_adc_probe,
	.remove = as3722_adc_remove,
	.driver = {
		.name = "as3722-adc",
	},
};
module_platform_driver(as3722_adc_driver);

MODULE_AUTHOR("Nikolaus Schulz <nikolaus.schulz@avionic-design.de>");
MODULE_DESCRIPTION("hwmon interface for the AS3722 PMU ADC");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:as3722-adc");
