/*
 * DVB USB Linux driver for Avionic Design DVB-C/T USB 2.0 Stick (AD-1456).
 *
 * Copyright (C) 2013-2014 Julian Scheel <julian@jusst.de>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "ad1456.h"
#include "tda18212.h"
#include "stv0367.h"
#include "cypress_firmware.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static int ad1456_download_firmware(struct dvb_usb_device *d,
	const struct firmware *fw)
{
	pr_debug("Loading ad1456 firmware\n");

	return cypress_load_ihex_firmware(d->udev, fw, CYPRESS_FX2);
}

static int ad1456_identify_state(struct dvb_usb_device *d, const char **name)
{
	return COLD;
}

static int ad1456_get_hardware_revision(struct dvb_usb_device *d)
{
	u8 *read_buf;
	u8 *buf;
	int ret;

	buf = kmalloc(6, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	read_buf = buf + 1;

	buf[0] = AD1456_REVISION;

	ret = dvb_usbv2_generic_rw(d, buf, 1, read_buf, 5);
	if (ret)
		goto error;

	ret = read_buf[4];

error:
	kfree(buf);
	return ret;
}

static int ad1456_set_sleep_mode(struct dvb_usb_device *d, u8 sleep)
{
	u8 *read_buf;
	u8 *buf;
	int ret;

	buf = kmalloc(4, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	read_buf = buf + 2;

	buf[0] = AD1456_SLEEP_MODE;
	buf[1] = !sleep;

	ret = dvb_usbv2_generic_rw(d, buf, 2, read_buf, 2);
	if (ret)
		goto error;

	ret = (read_buf[1] == !sleep) ? 0 : -EREMOTEIO;

	if (ret)
		goto error;

	/* Give the tuner time to wake up */
	msleep(1000);

error:
	kfree(buf);
	return ret;
}

static int ad1456_set_fifo_enabled(struct dvb_usb_device *d, u8 enabled)
{
	u8 *read_buf;
	u8 *buf;
	int ret;

	buf = kmalloc(2, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	read_buf = buf + 1;

	buf[0] = AD1456_FIFO_ENABLED;

	ret = dvb_usbv2_generic_rw(d, buf, 1, read_buf, 1);
	if (ret)
		goto error;

	ret = (read_buf[0] == enabled) ? 0 : -EREMOTEIO;

error:
	kfree(buf);
	return ret;
}

static inline int ad1456_i2c_status_to_err(u8 status)
{
	int ret = 0;

	switch(status) {
	case AD1456_I2C_OK:
		ret = 0;
		break;
	case AD1456_I2C_BIT_ERROR:
	case AD1456_I2C_NACK:
		ret = -EREMOTEIO;
		break;
	case AD1456_I2C_NOT_VALID:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int ad1456_i2c_msg(struct dvb_usb_device *d, u8 addr,
			  u8 *wbuf, u16 wlen, u8 *rbuf, u16 rlen)
{
	u8 *read_buf;
	u8 *buf;
	int ret;

	buf = kmalloc(rlen + wlen + 6, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	read_buf = buf + wlen + 4;

	buf[0] = AD1456_I2C_MSG;
	buf[1] = addr;
	buf[2] = wlen;
	buf[3] = rlen;
	memcpy(buf + 4, wbuf, wlen);

	ret = dvb_usbv2_generic_rw(d, buf, wlen + 4, read_buf, rlen + 2);
	if (ret)
		goto error;

	ret = ad1456_i2c_status_to_err(read_buf[1]);
	if (ret)
		goto error;

	memcpy(rbuf, read_buf + 2, rlen);

error:
	kfree(buf);
	return ret;
}

static int ad1456_i2c_master_xfer(struct i2c_adapter *adap,
		struct i2c_msg msg[], int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int ret;
	int i;

	ret = mutex_lock_interruptible(&d->i2c_mutex);
	if (ret < 0)
		return ret;

	for (i = 0; i < num; i++) {
		if (msg[i].flags & I2C_M_RD) {
			/* read request */
			ret = ad1456_i2c_msg(d, (msg[i].addr << 1) | 1, NULL, 0,
					     msg[i].buf, msg[i].len);
		} else if (i+1 < num && (msg[i+1].flags & I2C_M_RD)) {
			/* write/read request */
			ret = ad1456_i2c_msg(d, (msg[i].addr << 1) | 1,
					     msg[i].buf, msg[i].len,
					     msg[i+1].buf, msg[i+1].len);
			i++;
		} else {
			/* write request */
			ret = ad1456_i2c_msg(d, (msg[i].addr << 1), msg[i].buf,
					     msg[i].len, NULL, 0);
		}
		if (ret < 0)
			goto out;
	}

	ret = i;

out:
	mutex_unlock(&d->i2c_mutex);
	return ret;
}


static u32 ad1456_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm ad1456_i2c_algo = {
	.master_xfer   = ad1456_i2c_master_xfer,
	.functionality = ad1456_i2c_func,
};

/* demodulator configuration */
static struct stv0367_config ad1456_stv0367_config[] = {
	{
		.demod_address = 0x1c,
		.xtal = 16000000,
		.if_khz = 4570,
		.if_iq_mode = 0,
		.ts_mode = 4,
		.ts_swap = 1,
		.clk_pol = 1,
		.adc_mode = STV0367_ADC_10V,
	}, {
		.demod_address = 0x1c,
		.xtal = 16000000,
		.if_khz = 5000,
		.if_iq_mode = 0,
		.ts_mode = 4,
		.ts_swap = 1,
		.clk_pol = 1,
		.adc_mode = STV0367_ADC_10V,
	},
};

static int ad1456_frontend_attach(struct dvb_usb_adapter *adap)
{
	int hw_revision;
	int err;

	err = ad1456_get_hardware_revision(adap_to_d(adap));
	if (err < 0) {
		pr_err("ad1456: hardware revision could not be detected.\n");
		return err;
	}
	hw_revision = err;

	err = ad1456_set_sleep_mode(adap_to_d(adap), 0);
	if (err < 0) {
		pr_err("ad1456: Could not power up device.\n");
		return err;
	}

	adap->fe[0] = dvb_attach(stv0367cab_attach, &ad1456_stv0367_config[1],
			&adap_to_d(adap)->i2c_adap);
	if (adap->fe[0] == NULL)
		return -ENODEV;

	/*  DVB-T is only supported with Hardware Revision >= 1 */
	if (hw_revision >= 1) {
		adap->fe[1] = dvb_attach(stv0367ter_attach,
				&ad1456_stv0367_config[0],
				&adap_to_d(adap)->i2c_adap);
		if (adap->fe[1] == NULL)
			return -ENODEV;
	} else {
		pr_debug("ad1456: hardware revision 0 detected. "
				"Supports DVB-C only.\n");
		adap->fe[1] = NULL;
	}

	return 0;
}

static struct tda18212_config ad1456_tda18212_config = {
	.i2c_address = 0x60,
	.if_dvbt_6 = 4570,
	.if_dvbt_7 = 4570,
	.if_dvbt_8 = 4570,
	.if_dvbc = 5000,
	.if_level = TDA18212_IF_2_0V,
};

static int ad1456_tuner_attach(struct dvb_usb_adapter *adap)
{
	int ret;
	ret = dvb_attach(tda18212_attach, adap->fe[0],
			&adap_to_d(adap)->i2c_adap,
			&ad1456_tda18212_config) == NULL ? -ENODEV : 0;
	if (ret != 0)
		return ret;

	if (adap->fe[1] != NULL) {
		ret = dvb_attach(tda18212_attach, adap->fe[1],
				&adap_to_d(adap)->i2c_adap,
				&ad1456_tda18212_config) == NULL ? -ENODEV : 0;
		if (ret != 0)
			return ret;
	}

	ad1456_set_fifo_enabled(adap_to_d(adap), 1);

	return ret;
}

static int ad1456_get_stream_config(struct dvb_frontend *fe, u8 *ts_type,
		struct usb_data_stream_properties *stream)
{
	struct dvb_usb_device *d = fe_to_d(fe);
	dev_dbg(&d->udev->dev, "%s: adap=%d\n", __func__, fe_to_adap(fe)->id);

	return 0;
}

static struct dvb_usb_device_properties ad1456_props = {
	.driver_name = KBUILD_MODNAME,
	.owner = THIS_MODULE,
	.adapter_nr = adapter_nr,

	.firmware = AD1456_FIRMWARE,
	.download_firmware = ad1456_download_firmware,
	.identify_state = ad1456_identify_state,

	.i2c_algo = &ad1456_i2c_algo,
	.frontend_attach = ad1456_frontend_attach,
	.tuner_attach = ad1456_tuner_attach,

	.generic_bulk_ctrl_endpoint = 0x01,
	.generic_bulk_ctrl_endpoint_response = 0x81,

	.get_stream_config = ad1456_get_stream_config,
	.num_adapters = 1,
	.adapter = {
		{
			.stream = DVB_USB_STREAM_BULK(0x82, 10, 4096),
		},
	},
};

static const struct usb_device_id ad1456_id_table[] = {
	{ DVB_USB_DEVICE(USB_VID_CYPRESS, 0x1003,
		&ad1456_props, "Avionic Design 1456", NULL) },
	{ }
};
MODULE_DEVICE_TABLE(usb, ad1456_id_table);

static struct usb_driver ad1456_driver = {
	.name = KBUILD_MODNAME,
	.id_table = ad1456_id_table,
	.probe = dvb_usbv2_probe,
	.disconnect = dvb_usbv2_disconnect,
	.suspend = dvb_usbv2_suspend,
	.resume = dvb_usbv2_resume,
	.reset_resume = dvb_usbv2_reset_resume,
	.no_dynamic_id = 1,
	.soft_unbind = 1,
};

module_usb_driver(ad1456_driver);

MODULE_AUTHOR("Julian Scheel <julian@jusst.de>");
MODULE_DESCRIPTION("Driver for Avionic Design DVB-C/T USB 2.0 1456");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");
