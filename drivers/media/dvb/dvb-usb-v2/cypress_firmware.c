/*  cypress_firmware.c is part of the DVB USB library.
 *
 * Copyright (C) 2004-6 Patrick Boettcher (patrick.boettcher@desy.de)
 * see dvb-usb-init.c for copyright information.
 *
 * This file contains functions for downloading the firmware to Cypress FX 1
 * and 2 based devices.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/firmware.h>
#include <linux/ihex.h>
#include "cypress_firmware.h"

struct usb_cypress_controller {
	u8 id;
	const char *name;	/* name of the usb controller */
	u16 cs_reg;		/* needs to be restarted,
				 * when the firmware has been downloaded */
};

static const struct usb_cypress_controller cypress[] = {
	{ .id = CYPRESS_AN2135, .name = "Cypress AN2135", .cs_reg = 0x7f92 },
	{ .id = CYPRESS_AN2235, .name = "Cypress AN2235", .cs_reg = 0x7f92 },
	{ .id = CYPRESS_FX2,    .name = "Cypress FX2",    .cs_reg = 0xe600 },
};

/*
 * load a firmware packet to the device
 */
static int usb_cypress_writemem(struct usb_device *udev, u16 addr, u8 *data,
		u8 len)
{
	unsigned char *transfer_buffer;
	int result;

	transfer_buffer = kmemdup(data, len, GFP_KERNEL | GFP_DMA);
	if (!transfer_buffer) {
		dev_err(&udev->dev, "%s - kmalloc(%d) failed.\n",
							__func__, len);
		return -ENOMEM;
	}

	result = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 0xa0, USB_TYPE_VENDOR, addr, 0x00,
				 transfer_buffer, len, 5000);
	kfree(transfer_buffer);

	return result;
}

static int cypress_get_hexline(const struct firmware *fw,
				struct hexline *hx, int *pos)
{
	u8 *b = (u8 *) &fw->data[*pos];
	int data_offs = 4;

	if (*pos >= fw->size)
		return 0;

	memset(hx, 0, sizeof(struct hexline));
	hx->len = b[0];

	if ((*pos + hx->len + 4) >= fw->size)
		return -EINVAL;

	hx->addr = b[1] | (b[2] << 8);
	hx->type = b[3];

	if (hx->type == 0x04) {
		/* b[4] and b[5] are the Extended linear address record data
		 * field */
		hx->addr |= (b[4] << 24) | (b[5] << 16);
	}

	memcpy(hx->data, &b[data_offs], hx->len);
	hx->chk = b[hx->len + data_offs];
	*pos += hx->len + 5;

	return *pos;
}

int cypress_load_firmware(struct usb_device *udev,
		const struct firmware *fw, int type)
{
	struct hexline *hx;
	int ret, pos = 0;

	hx = kmalloc(sizeof(struct hexline), GFP_KERNEL);
	if (!hx) {
		dev_err(&udev->dev, "%s: kmalloc() failed\n", KBUILD_MODNAME);
		return -ENOMEM;
	}

	/* stop the CPU */
	hx->data[0] = 1;
	ret = usb_cypress_writemem(udev, cypress[type].cs_reg, hx->data, 1);
	if (ret != 1) {
		dev_err(&udev->dev, "%s: CPU stop failed=%d\n",
				KBUILD_MODNAME, ret);
		ret = -EIO;
		goto err_kfree;
	}

	/* write firmware to memory */
	for (;;) {
		ret = cypress_get_hexline(fw, hx, &pos);
		if (ret < 0)
			goto err_kfree;
		else if (ret == 0)
			break;

		ret = usb_cypress_writemem(udev, hx->addr, hx->data, hx->len);
		if (ret < 0) {
			goto err_kfree;
		} else if (ret != hx->len) {
			dev_err(&udev->dev,
					"%s: error while transferring firmware (transferred size=%d, block size=%d)\n",
					KBUILD_MODNAME, ret, hx->len);
			ret = -EIO;
			goto err_kfree;
		}
	}

	/* start the CPU */
	hx->data[0] = 0;
	ret = usb_cypress_writemem(udev, cypress[type].cs_reg, hx->data, 1);
	if (ret != 1) {
		dev_err(&udev->dev, "%s: CPU start failed=%d\n",
				KBUILD_MODNAME, ret);
		ret = -EIO;
		goto err_kfree;
	}

	ret = 0;
	dev_info(&udev->dev, "firmware successfully loaded\n");
err_kfree:
	kfree(hx);
	return ret;
}
EXPORT_SYMBOL(cypress_load_firmware);

int cypress_load_ihex_firmware(struct usb_device *udev,
		const struct firmware *fw, int type)
{
	const struct ihex_binrec *record;
	uint8_t reset;
	int ret;

	ret = ihex_validate_fw(fw);
	if (ret) {
		dev_err(&udev->dev, "Firmware is not valid iHEX.\n");
		return ret;
	}

	/* stop the CPU */
	reset = 1;
	ret = usb_cypress_writemem(udev, cypress[type].cs_reg, &reset, 1);
	if (ret != 1) {
		dev_err(&udev->dev, "%s: CPU stop failed=%d\n",
				KBUILD_MODNAME, ret);
		if (ret >= 0)
			ret = -EIO;
		goto err;
	}

	/* write firmware to memory */
	record = (const struct ihex_binrec *)fw->data;
	for (; record; record = ihex_next_binrec(record)) {
		if (be32_to_cpu(record->addr) > 0x3fff) {
			dev_err(&udev->dev, "%s: error while transferring " \
					"firmware: address out of range\n",
					KBUILD_MODNAME);
			ret = -EINVAL;
			goto err;
		}

		ret = usb_cypress_writemem(udev, be32_to_cpu(record->addr),
				(unsigned char*)record->data,
				be16_to_cpu(record->len));
		if (ret < 0) {
			dev_err(&udev->dev, "usb_cypress_writemem failed:%d\n",
				ret);
			goto err;
		} else if (ret != be16_to_cpu(record->len)) {
			dev_err(&udev->dev, "%s: error while transferring " \
					"firmware (transferred size=%d, " \
					"block size=%d)\n",
					KBUILD_MODNAME, ret,
					be16_to_cpu(record->len));
			ret = -EIO;
			goto err;
		}
	}

	/* start the CPU */
	reset = 0;
	ret = usb_cypress_writemem(udev, cypress[type].cs_reg, &reset, 1);
	if (ret != 1) {
		dev_err(&udev->dev, "%s: CPU start failed=%d\n",
				KBUILD_MODNAME, ret);
		if (ret >= 0)
			ret = -EIO;
		goto err;
	}

	ret = 0;
	dev_info(&udev->dev, "firmware successfully loaded\n");
err:
	return ret;
}
EXPORT_SYMBOL(cypress_load_ihex_firmware);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Cypress firmware download");
MODULE_LICENSE("GPL");
