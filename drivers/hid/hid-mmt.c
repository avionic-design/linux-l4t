/*
 * Copyright 2014  Alban Bedel (alban.bedel@avionic-design.de)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/hid.h>
#include <linux/usb.h>
#include <linux/sysfs.h>

#include "usbhid/usbhid.h"

struct mmt_device {
	struct hid_device *hdev;
	struct hid_report *report;
	unsigned attr_count;
};

struct mmt_attribute {
	struct device_attribute devattr;
	unsigned field;
	unsigned offset;
};

#define mmt_map_key_clear(c)	hid_map_usage_clear(hi, usage, bit, max, \
					EV_KEY, (c))

static int mmt_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{

	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_TELEPHONY) {
		unsigned int hid_usage = usage->hid & HID_USAGE;
		unsigned int key;
		/* Extended avionic codes: 0xAcci
		   where cc is the original code, i the index */
		if ((hid_usage & 0xF000) != 0xA000)
			return 0;
		switch ((hid_usage >> 4) & 0xFF) {
		case 0x50: key = KEY_PHONE_SPEED_DIAL;		break;
		case 0x51: key = KEY_PHONE_STORE_NUMBER;	break;
		default: return -1; /* Ignore */
		}
		mmt_map_key_clear(key + (hid_usage & 0xF));
		return 1;
	}

	return 0;
}

static ssize_t mmt_show_field(struct device *dev,
			struct device_attribute *dev_attr, char *buf)
{
	struct mmt_device *mmt = dev_get_drvdata(dev);
	struct mmt_attribute *attr =
		container_of(dev_attr, struct mmt_attribute, devattr);
	struct hid_field *field = mmt->report->field[attr->field];

	return sprintf(buf, "%ld\n", (long)*field->value);
}

static ssize_t mmt_store_field(struct device *dev,
			struct device_attribute *dev_attr,
			const char *buf, size_t count)
{
	struct mmt_device *mmt = dev_get_drvdata(dev);
	struct mmt_attribute *attr =
		container_of(dev_attr, struct mmt_attribute, devattr);
	struct hid_field *field = mmt->report->field[attr->field];
	long val;

	if (kstrtol(buf, 0, &val))
		return -EINVAL;

	/* Clamp the value to the allowed range */
	if (val < field->logical_minimum)
		val = field->logical_minimum;
	else if (val > field->logical_maximum)
		val = field->logical_maximum;

	hid_set_field(mmt->report->field[attr->field], attr->offset, val);
	usbhid_submit_report(mmt->hdev, mmt->report, USB_DIR_OUT);

	/* If the field has a NULL state reset the value */
	if (field->flags & HID_MAIN_ITEM_NULL_STATE) {
		/* We need a value outside of the legal range */
		if (field->logical_minimum > 0)
			val = field->logical_minimum - 1;
		else
			val = field->logical_maximum + 1;
		hid_set_field(mmt->report->field[attr->field],
			attr->offset, val);
	}

	return count;
}

#define MMT_FIELD_ATTR(name, f, o)			\
	{						\
		.devattr = __ATTR(name,			\
				S_IWUSR | S_IRUGO,	\
				mmt_show_field,		\
				mmt_store_field),	\
		.field = f,				\
		.offset = o,				\
	}

static struct mmt_attribute mmt_attrs[] = {
	MMT_FIELD_ATTR(reading_light, 0, 0),
	MMT_FIELD_ATTR(room_light, 0, 1),
	MMT_FIELD_ATTR(nurse_call, 0, 2),
	MMT_FIELD_ATTR(blinds, 1, 0),
	MMT_FIELD_ATTR(reset, 2, 0),
	MMT_FIELD_ATTR(bootloader, 3, 0),
	MMT_FIELD_ATTR(backlight, 4, 0),
};

static int mmt_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct mmt_device *mmt;
	struct list_head *report_list;
	struct hid_report *report;
	int i, err;

	/* Parse the HID report descriptors and get the first output report */
	err = hid_parse(hdev);
	if (err) {
		hid_err(hdev, "parse failed\n");
		return err;
	}

	report_list = &hdev->report_enum[HID_OUTPUT_REPORT].report_list;
	if (list_empty(report_list)) {
		hid_err(hdev, "no output report found\n");
		return -ENODEV;
	}
	report = list_entry(report_list->next, struct hid_report, list);

	/* Alloc and init our data struct */
	mmt = devm_kzalloc(&hdev->dev, sizeof(*mmt), GFP_KERNEL);
	if (!mmt)
		return -ENOMEM;

	hid_set_drvdata(hdev, mmt);
	mmt->hdev = hdev;
	mmt->report = report;

	/* Register the sysfs files */
	for (i = 0 ; i < ARRAY_SIZE(mmt_attrs) ; i += 1) {
		struct mmt_attribute *attr = &mmt_attrs[i];
		if (attr->field >= report->maxfield ||
			attr->offset >=
			report->field[attr->field]->report_count)
			continue;
		err = sysfs_create_file(&hdev->dev.kobj, &attr->devattr.attr);
		if (err) {
			hid_warn(hdev,
				"failed to create sysfs entry %s: %d\n",
				mmt_attrs[i].devattr.attr.name, err);
			goto sysfs_release;
		}
		mmt->attr_count += 1;
	}

	/* Start the HID hardware */
	err = hid_hw_start(hdev, HID_CONNECT_DEFAULT & ~HID_CONNECT_FF);
	if (err) {
		hid_err(hdev, "failed to start hid hw: %d\n", err);
		goto sysfs_release;
	}

	return 0;

sysfs_release:
	for (i = 0 ; i < mmt->attr_count ; i += 1)
		sysfs_remove_file(&hdev->dev.kobj,
				&mmt_attrs[i].devattr.attr);
	return err;
}

static void mmt_remove(struct hid_device *hdev)
{
	struct mmt_device *mmt = hid_get_drvdata(hdev);
	struct hid_report *report = mmt->report;
	int i;

	/* Stop the HID hardware */
	hid_hw_stop(hdev);

	/* Remove the sysfs entries */
	for (i = 0 ; i < mmt->attr_count ; i += 1) {
		struct mmt_attribute *attr = &mmt_attrs[i];
		if (attr->field >= report->maxfield ||
			attr->offset >=
			report->field[attr->field]->report_count)
			continue;
		sysfs_remove_file(&hdev->dev.kobj, &attr->devattr.attr);
	}
}

static const struct hid_device_id mmt_devices[] = {
	{ HID_USB_DEVICE(0xadad, 0x0001) },
	{ }
};
MODULE_DEVICE_TABLE(hid, mmt_devices);

static struct hid_driver mmt_driver = {
	.name = "mmt",
	.id_table = mmt_devices,
	.probe = mmt_probe,
	.remove = mmt_remove,
	.input_mapping = mmt_input_mapping,
};

static int __init mmt_init(void)
{
	return hid_register_driver(&mmt_driver);
}

static void __exit mmt_exit(void)
{
	hid_unregister_driver(&mmt_driver);
}

module_init(mmt_init);
module_exit(mmt_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alban Bedel <alban.bedel@avionic-design.de>");
