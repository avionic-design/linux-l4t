/*
 * Copyright 2014  Alban Bedel (alban.bedel@avionic-design.de)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/hid.h>

static int ad_usb_handset_input_mapping(
	struct hid_device *hdev, struct hid_input *hi,
	struct hid_field *field, struct hid_usage *usage,
	unsigned long **bit, int *max)
{
	/* Remap mute to the phone hook */
	if (usage->hid == (HID_UP_CONSUMER | 0xe2)) {
		hid_map_usage_clear(hi, usage, bit, max,
				EV_SW, SW_PHONE_HOOK);
		return 1;
	}

	return 0;
}

static const struct hid_device_id ad_usb_handset_devices[] = {
	{ HID_USB_DEVICE(0x08bb, 0x29c6) },
	{ }
};
MODULE_DEVICE_TABLE(hid, ad_usb_handset_devices);

static struct hid_driver ad_usb_handset_driver = {
	.name = "Avionic Design USB Handset",
	.id_table = ad_usb_handset_devices,
	.input_mapping = ad_usb_handset_input_mapping,
};

static int __init ad_usb_handset_init(void)
{
	return hid_register_driver(&ad_usb_handset_driver);
}

static void __exit ad_usb_handset_exit(void)
{
	hid_unregister_driver(&ad_usb_handset_driver);
}

module_init(ad_usb_handset_init);
module_exit(ad_usb_handset_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alban Bedel <alban.bedel@avionic-design.de>");
