/*
 * Copyright 2014  Dirk Leber (dirk.leber@avionic-design.de)
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

struct bgu_device {
	struct hid_device *hdev;
	struct list_head *report_list;
	unsigned attr_count;
};

struct bgu_attribute {
	struct device_attribute devattr;
	unsigned report;
	unsigned field;
	unsigned offset;
};

static struct hid_report *bgu_get_report(struct bgu_device *bgu, int report_id)
{
	struct hid_report *report;
	struct list_head *pos;

	list_for_each(pos, bgu->report_list) {
		report = list_entry(pos, struct hid_report, list);
		if (report->id == report_id)
			return report;
	}
	return NULL;
}

#define bgu_map_key_clear(c)	hid_map_usage_clear(hi, usage, bit, max, \
					EV_KEY, (c))

static int bgu_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	if ((usage->hid & HID_USAGE_PAGE) != HID_UP_TELEPHONY)
		return 0;

	if ((field->logical & HID_USAGE_PAGE) == HID_UP_TELEPHONY &&
			(field->logical & HID_USAGE) == 0x7) {
		/* Programmable Button */
		switch (usage->hid & HID_USAGE) {
			case 1: bgu_map_key_clear(KEY_PROG1); break;
			case 2: bgu_map_key_clear(KEY_PROG2); break;
			case 3: bgu_map_key_clear(KEY_PROG3); break;
			case 4: bgu_map_key_clear(KEY_PROG4); break;
			default: return -1; /* Ignore key */
		}
		return 1;
	}
	switch (usage->hid & HID_USAGE) {
		case 0x21: bgu_map_key_clear(KEY_PHONE_LINE); break;
		case 0x24: bgu_map_key_clear(KEY_PHONE_REDIAL); break;
		case 0x53: bgu_map_key_clear(KEY_INFO); break;
		default: return 0; /* Do not remap */
	}
	return 1;
}

static ssize_t bgu_show_field(struct device *dev,
		struct device_attribute *dev_attr, char *buf)
{
	struct bgu_device *bgu = dev_get_drvdata(dev);
	struct bgu_attribute *attr = container_of(dev_attr,
			struct bgu_attribute, devattr);
	struct hid_report *report = bgu_get_report(bgu, attr->report);
	struct hid_field *field = report ? report->field[attr->field] : NULL;

	if (!field)
		return -ENOENT;

	return sprintf(buf, "%ld\n", (long)*field->value);
}

static ssize_t bgu_store_field(struct device *dev,
		struct device_attribute *dev_attr,
		const char *buf, size_t count)
{
	struct bgu_device *bgu = dev_get_drvdata(dev);
	struct bgu_attribute *attr = container_of(dev_attr,
			struct bgu_attribute, devattr);
	struct hid_report *report = bgu_get_report(bgu, attr->report);
	struct hid_field *field = report ? report->field[attr->field] : NULL;
	long val;

	if (!field)
		return -ENOENT;

	if (kstrtol(buf, 0, &val))
		return -EINVAL;

	/* Clamp the value to the allowed range */
	if (val < field->logical_minimum)
		val = field->logical_minimum;
	else if (val > field->logical_maximum)
		val = field->logical_maximum;

	hid_set_field(field, attr->offset, val);
	usbhid_submit_report(bgu->hdev, report, USB_DIR_OUT);

	/* If the field has a NULL state reset the value */
	if (field->flags & HID_MAIN_ITEM_NULL_STATE) {
		/* We need a value outside of the legal range */
		if (field->logical_minimum > 0)
			val = field->logical_minimum - 1;
		else
			val = field->logical_maximum + 1;
		hid_set_field(field, attr->offset, val);
	}

	return count;
}

#define BGU_FIELD_ATTR(name, r, f, o)                                          \
	{                                                                      \
		.devattr = __ATTR(name,                                        \
				S_IWUSR | S_IRUGO,                             \
				bgu_show_field,                                \
				bgu_store_field),                              \
		.report = r,                                                   \
		.field = f,                                                    \
		.offset = o,                                                   \
	}

static struct bgu_attribute bgu_attrs[] = {
	BGU_FIELD_ATTR(off_hook, 1, 0, 0),
	BGU_FIELD_ATTR(hold, 1, 0, 1),
	BGU_FIELD_ATTR(euro, 1, 0, 2),
	BGU_FIELD_ATTR(bar, 1, 0, 3),
	BGU_FIELD_ATTR(rect, 1, 0, 4),
	BGU_FIELD_ATTR(tv_on, 1, 0, 5),
	BGU_FIELD_ATTR(tv_off, 1, 0, 6),
	BGU_FIELD_ATTR(radio_on, 1, 0, 7),
	BGU_FIELD_ATTR(radio_off, 1, 0, 8),
	BGU_FIELD_ATTR(reset, 2, 0, 0),
	BGU_FIELD_ATTR(bootloader, 2, 1, 0),
};

static int bgu_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct bgu_device *bgu;
	struct list_head *report_list;
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

	/* Alloc and init our data struct */
	bgu = devm_kzalloc(&hdev->dev, sizeof(*bgu), GFP_KERNEL);
	if (!bgu)
		return -ENOMEM;

	hid_set_drvdata(hdev, bgu);
	bgu->hdev = hdev;
	bgu->report_list = report_list;

	/* Register the sysfs files */
	for (i = 0 ; i < ARRAY_SIZE(bgu_attrs) ; i += 1) {
		struct bgu_attribute *attr = &bgu_attrs[i];
		struct hid_report *report = bgu_get_report(bgu, attr->report);

		if (!report) {
			hid_warn(hdev, "[%s] No report for id %d found\n",
					bgu_attrs[i].devattr.attr.name,
					attr->report);
			continue;
		}
		if (attr->field >= report->maxfield) {
			hid_warn(hdev, "[%s] Field out of range (%d/%d)\n",
					bgu_attrs[i].devattr.attr.name,
					attr->field, report->maxfield);
			continue;
		}
		if (attr->offset >= report->field[attr->field]->report_count) {
			hid_warn(hdev, "[%s] Offset out of range (%d/%d)\n",
					bgu_attrs[i].devattr.attr.name,
					attr->offset,
					report->field[attr->field]->report_count);
			continue;
		}
		err = sysfs_create_file(&hdev->dev.kobj, &attr->devattr.attr);
		if (err) {
			hid_warn(hdev, "failed to create sysfs entry %s: %d\n",
					bgu_attrs[i].devattr.attr.name, err);
			goto sysfs_release;
		}
		bgu->attr_count += 1;
	}

	/* Start the HID hardware */
	err = hid_hw_start(hdev, HID_CONNECT_DEFAULT & ~HID_CONNECT_FF);
	if (err) {
		hid_err(hdev, "failed to start hid hw: %d\n", err);
		goto sysfs_release;
	}

	return 0;

sysfs_release:
	for (i = 0 ; i < bgu->attr_count ; i += 1)
		sysfs_remove_file(&hdev->dev.kobj, &bgu_attrs[i].devattr.attr);

	return err;
}

static void bgu_remove(struct hid_device *hdev)
{
	struct bgu_device *bgu = hid_get_drvdata(hdev);
	int i;

	/* Stop the HID hardware */
	hid_hw_stop(hdev);

	/* Remove the sysfs entries */
	for (i = 0 ; i < bgu->attr_count ; i += 1) {
		struct bgu_attribute *attr = &bgu_attrs[i];
		struct hid_report *report = bgu_get_report(bgu, attr->report);

		if (!report)
			continue;
		if (attr->field >= report->maxfield)
			continue;
		if (attr->offset >= report->field[attr->field]->report_count)
			continue;
		sysfs_remove_file(&hdev->dev.kobj, &attr->devattr.attr);
	}
}

static const struct hid_device_id bgu_devices[] = {
	{ HID_USB_DEVICE(0xadad, 0x0042) },
	{ }
};
MODULE_DEVICE_TABLE(hid, bgu_devices);

static struct hid_driver bgu_driver = {
	.name = "bgu",
	.id_table = bgu_devices,
	.probe = bgu_probe,
	.remove = bgu_remove,
	.input_mapping = bgu_input_mapping,
};

static int __init bgu_init(void)
{
	return hid_register_driver(&bgu_driver);
}

static void __exit bgu_exit(void)
{
	hid_unregister_driver(&bgu_driver);
}

module_init(bgu_init);
module_exit(bgu_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dirk Leber <dirk.leber@avionic-design.de>");
