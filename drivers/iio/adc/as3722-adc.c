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
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/delay.h>

struct as3722_adc_chaninfo {
	int offset;
	int scale;
	int sd;
};

/*
 * Constants for the conversion of result register values to
 * millivolt/millidegrees. Taken from figure 69 (ADC input sources) in
 * the datasheet, p.65.
 */
#define AS3722_ADC_VOLT_SCALE_LOW  156
#define AS3722_ADC_VOLT_SCALE_HIGH 625
#define AS3722_ADC_VOLT_VBAT_SCALE 2344

#define AS3722_ADC_TEMP_SD_SCALE   (-37340)
#define AS3722_ADC_TEMP_SD_OFF     (32650000/(AS3722_ADC_TEMP_SD_SCALE))
#define AS3722_ADC_TEMP_DIE_SCALE  76980
#define AS3722_ADC_TEMP_DIE_OFF    (-27400000/(AS3722_ADC_TEMP_DIE_SCALE))

#define AS3722_ADC_SCALE_DENOMINATOR 100

#define AS3722_ADC_CHANINFO(_off, _scale, _sd) \
	{ \
		.offset     = _off, \
		.scale      = _scale, \
		.sd         = _sd, \
	}

#define AS3722_ADC_CHANINFO_VOLT(scale) \
	AS3722_ADC_CHANINFO(0, scale, -1)

#define AS3722_ADC_CHANINFO_TEMP_SD(sd) \
	AS3722_ADC_CHANINFO(AS3722_ADC_TEMP_SD_OFF, \
			AS3722_ADC_TEMP_SD_SCALE, sd)

static const struct as3722_adc_chaninfo as3722_adc_chaninfo[] = {
	[AS3722_ADC_TEMP_SENSOR] = AS3722_ADC_CHANINFO(
			AS3722_ADC_TEMP_DIE_OFF,
			AS3722_ADC_TEMP_DIE_SCALE,
			-1),
	[AS3722_ADC_VSUP] = AS3722_ADC_CHANINFO_VOLT(
			AS3722_ADC_VOLT_SCALE_HIGH),
	[AS3722_ADC_GPIO1] = AS3722_ADC_CHANINFO_VOLT(
			AS3722_ADC_VOLT_SCALE_LOW),
	[AS3722_ADC_GPIO2] = AS3722_ADC_CHANINFO_VOLT(
			AS3722_ADC_VOLT_SCALE_LOW),
	[AS3722_ADC_GPIO3] = AS3722_ADC_CHANINFO_VOLT(
			AS3722_ADC_VOLT_SCALE_LOW),
	[AS3722_ADC_GPIO4] = AS3722_ADC_CHANINFO_VOLT(
			AS3722_ADC_VOLT_SCALE_LOW),
	[AS3722_ADC_GPIO6] = AS3722_ADC_CHANINFO_VOLT(
			AS3722_ADC_VOLT_SCALE_LOW),
	[AS3722_ADC_GPIO7] = AS3722_ADC_CHANINFO_VOLT(
			AS3722_ADC_VOLT_SCALE_LOW),
	[AS3722_ADC_VBAT] = AS3722_ADC_CHANINFO_VOLT(
			AS3722_ADC_VOLT_VBAT_SCALE),
	[AS3722_ADC_TEMP1_SD0] = AS3722_ADC_CHANINFO_TEMP_SD(0),
	[AS3722_ADC_TEMP2_SD0] = AS3722_ADC_CHANINFO_TEMP_SD(0),
	[AS3722_ADC_TEMP3_SD0] = AS3722_ADC_CHANINFO_TEMP_SD(0),
	[AS3722_ADC_TEMP4_SD0] = AS3722_ADC_CHANINFO_TEMP_SD(0),
	[AS3722_ADC_TEMP_SD1]  = AS3722_ADC_CHANINFO_TEMP_SD(1),
	[AS3722_ADC_TEMP1_SD6] = AS3722_ADC_CHANINFO_TEMP_SD(6),
	[AS3722_ADC_TEMP2_SD6] = AS3722_ADC_CHANINFO_TEMP_SD(6),
};

#define ADC_CHANNEL(_index, _type, _addr, _name) \
{ \
	.channel = _index, \
	.type = _type, \
	.address = _addr, \
	.indexed = 1, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_SCALE) | \
		((_type) == IIO_TEMP ? BIT(IIO_CHAN_INFO_OFFSET) : 0), \
	.datasheet_name = _name, \
}

static bool as3722_adc_has_channel(const struct iio_chan_spec *channel)
{
	return (long)channel->address >= 0;
}

static bool as3722_adc_channel_supported(const struct iio_chan_spec *channel)
{
	const struct as3722_adc_chaninfo *chaninfo =
		&as3722_adc_chaninfo[channel->address];

	return chaninfo->scale != 0;
}

static struct iio_chan_spec as3722_adc_channels[] = {
	ADC_CHANNEL(0, IIO_CURRENT, AS3722_ADC_SD0, "SD0_current"),
	ADC_CHANNEL(1, IIO_CURRENT, AS3722_ADC_SD1, "SD1_current"),
	ADC_CHANNEL(2, IIO_CURRENT, AS3722_ADC_SD6, "SD6_current"),
	ADC_CHANNEL(3, IIO_TEMP, AS3722_ADC_TEMP_SENSOR, "DIE"),
	ADC_CHANNEL(4, IIO_VOLTAGE, AS3722_ADC_VSUP, "VSUP"),
	ADC_CHANNEL(5, IIO_VOLTAGE, AS3722_ADC_GPIO1, "GPIO1"),
	ADC_CHANNEL(6, IIO_VOLTAGE, AS3722_ADC_GPIO2, "GPIO2"),
	ADC_CHANNEL(7, IIO_VOLTAGE, AS3722_ADC_GPIO3, "GPIO3"),
	ADC_CHANNEL(8, IIO_VOLTAGE, AS3722_ADC_GPIO4, "GPIO4"),
	ADC_CHANNEL(9, IIO_VOLTAGE, AS3722_ADC_GPIO6, "GPIO6"),
	ADC_CHANNEL(10, IIO_VOLTAGE, AS3722_ADC_GPIO7, "GPIO7"),
	ADC_CHANNEL(11, IIO_VOLTAGE, AS3722_ADC_VBAT, "VBAT"),
	ADC_CHANNEL(12, IIO_VOLTAGE, AS3722_ADC_PWM_CLK2, "PWM_CLK2"),
	ADC_CHANNEL(13, IIO_VOLTAGE, AS3722_ADC_PWM_DAT2, "PWM_DAT2"),
	ADC_CHANNEL(14, IIO_VOLTAGE, -1, "dummy0"),
	ADC_CHANNEL(15, IIO_VOLTAGE, -1, "dummy1"),
	ADC_CHANNEL(16, IIO_TEMP, AS3722_ADC_TEMP1_SD0, "TEMP1_SD0"),
	ADC_CHANNEL(17, IIO_TEMP, AS3722_ADC_TEMP2_SD0, "TEMP2_SD0"),
	ADC_CHANNEL(18, IIO_TEMP, AS3722_ADC_TEMP3_SD0, "TEMP3_SD0"),
	ADC_CHANNEL(19, IIO_TEMP, AS3722_ADC_TEMP4_SD0, "TEMP4_SD0"),
	ADC_CHANNEL(20, IIO_TEMP, AS3722_ADC_TEMP_SD1, "TEMP_SD1"),
	ADC_CHANNEL(21, IIO_TEMP, AS3722_ADC_TEMP1_SD6, "TEMP1_SD6"),
	ADC_CHANNEL(22, IIO_TEMP, AS3722_ADC_TEMP2_SD6, "TEMP2_SD6"),
};

static int read_adc_channel(struct iio_dev *iio,
		const struct iio_chan_spec *channel)
{
	struct as3722 *as3722 = iio_device_get_drvdata(iio);
	const struct as3722_adc_chaninfo *chaninfo;
	int ret, try, result;
	u32 val;

	if (!as3722_adc_has_channel(channel))
		return -ENXIO;

	if (!as3722_adc_channel_supported(channel))
		return -EOPNOTSUPP;

	chaninfo = &as3722_adc_chaninfo[channel->address];

	mutex_lock(&iio->mlock);

	/* If we're reading the temp from an sd, check if the sd is enabled. */
	if (chaninfo->sd >= 0) {
		ret = as3722_read(as3722, AS3722_SD_CONTROL_REG, &val);
		if (ret < 0) {
			dev_err(&iio->dev, "error checking sd enable: %d\n",
					ret);
			goto out;
		}

		if ((val & AS3722_SDn_CTRL(chaninfo->sd)) == 0) {
			dev_dbg(&iio->dev, "can't read temp: sd%d disabled\n",
					chaninfo->sd);
			ret = -EAGAIN;
			goto out;
		}
	}

	/* Initiate conversion */
	ret = as3722_write(as3722, AS3722_ADC0_CONTROL_REG,
			AS3722_ADC0_CONV_START |
			AS3722_ADC0_LOW_VOLTAGE_RANGE |
			channel->address);
	if (ret < 0) {
		dev_err(&iio->dev, "error initiating conversion: %d\n", ret);
		goto out;
	}

	/* Wait for conversion to start */
	for (try = 2; ; --try) {
		ret = as3722_read(as3722, AS3722_ADC0_CONTROL_REG, &val);
		if (ret < 0) {
			dev_err(&iio->dev, "error checking adc0 cfg: %d\n",
					ret);
			goto out;
		}

		if ((val & AS3722_ADC0_CONV_START) == 0)
			break;

		if (!try) {
			dev_err(&iio->dev,
				"timeout waiting for conversion start\n");
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
			dev_err(&iio->dev, "error reading adc0 result "
					"msb: %d\n", ret);
			goto out;
		}

		if ((val & AS3722_ADC0_CONV_NOTREADY) == 0)
			break;

		if (!try) {
			dev_err(&iio->dev, "timeout waiting for adc0 "
					"result\n");
			ret = -EBUSY;
			goto out;
		}

		/* Conversion time should be max. 45us */
		usleep_range(45, 70);
	}

	result = (val & AS3722_ADC_MASK_MSB_VAL) << 3;

	ret = as3722_read(as3722, AS3722_ADC0_LSB_RESULT_REG, &val);
	if (ret < 0) {
		dev_err(&iio->dev, "error reading adc0 result lsb: %d\n", ret);
		goto out;
	}

	result |= val & AS3722_ADC_MASK_LSB_VAL;

	ret = result;
out:
	mutex_unlock(&iio->mlock);
	return ret;
}

static int as3722_adc_read_raw(struct iio_dev *iio,
		struct iio_chan_spec const *channel,
		int *value, int *value2, long mask)
{
	const struct as3722_adc_chaninfo *chaninfo;
	int rawval;

	chaninfo = &as3722_adc_chaninfo[channel->address];

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		rawval = read_adc_channel(iio, channel);
		if (rawval < 0)
			return rawval;
		*value = rawval;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
		*value = chaninfo->offset;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*value = chaninfo->scale;
		*value2 = AS3722_ADC_SCALE_DENOMINATOR;
		return IIO_VAL_FRACTIONAL;
	default:
		break;
	}

	return -EINVAL;
}

static const struct iio_info as3722_adc_info = {
	.read_raw = as3722_adc_read_raw,
	.driver_module = THIS_MODULE,
};

static int as3722_adc_probe(struct platform_device *pdev)
{
	struct as3722 *as3722;
	struct iio_dev *iio;
	int ret;

	iio = iio_device_alloc(0);
	if (!iio)
		return -ENOMEM;


	as3722 = dev_get_drvdata(pdev->dev.parent);
	iio_device_set_drvdata(iio, as3722);

	iio->name = dev_name(&pdev->dev);
	iio->dev.parent = &pdev->dev;
	iio->modes = INDIO_DIRECT_MODE;
	iio->channels = as3722_adc_channels;
	iio->num_channels = ARRAY_SIZE(as3722_adc_channels);
	iio->info = &as3722_adc_info;

	ret = iio_device_register(iio);
	if (ret)
		goto iio_free;

	platform_set_drvdata(pdev, iio);

	return 0;

iio_free:
	iio_device_free(iio);

	return ret;
}

static int as3722_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *iio = platform_get_drvdata(pdev);

	iio_device_unregister(iio);
	iio_device_free(iio);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id as3722_adc_match[] = {
	{ .compatible = "ams,as3722-adc", },
	{ },
};
#endif

static struct platform_driver as3722_adc_driver = {
	.probe  = as3722_adc_probe,
	.remove = as3722_adc_remove,
	.driver = {
		.name = "as3722-adc",
		.of_match_table = of_match_ptr(as3722_adc_match),
	},
};
module_platform_driver(as3722_adc_driver);

MODULE_AUTHOR("Nikolaus Schulz <nikolaus.schulz@avionic-design.de>");
MODULE_DESCRIPTION("iio interface for the AS3722 PMU ADC");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:as3722-adc");
