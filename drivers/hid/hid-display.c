/*
 *  HID alphanumeric display frame buffer driver.
 *
 *  Copyright (C) 2014  Alban Bedel <alban.bedel@avionic-design.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <linux/hid.h>
#include <linux/usb.h>
#include <linux/sysfs.h>
#include <linux/fb.h>
#include <linux/uaccess.h>

#include "usbhid/usbhid.h"

#define HID_DISP_ALPHANUMERIC		(HID_UP_DISPLAY | 0x01)
#define HID_DISP_BITMAPPED		(HID_UP_DISPLAY | 0x02)

#define HID_DISP_ATTRIBUTES_REPORT	(HID_UP_DISPLAY | 0x20)
#define HID_DISP_ASCII_CHARACTER_SET	(HID_UP_DISPLAY | 0x21)
#define HID_DISP_DATA_READ_BACK		(HID_UP_DISPLAY | 0x22)
#define HID_DISP_FONT_READ_BACK		(HID_UP_DISPLAY | 0x23)

#define HID_DISP_CONTROL_REPORT		(HID_UP_DISPLAY | 0x24)
#define HID_DISP_CLEAR_DISPLAY		(HID_UP_DISPLAY | 0x25)

#define HID_DISP_VERTICAL_SCROLL	(HID_UP_DISPLAY | 0x29)
#define HID_DISP_HORIZONTAL_SCROLL	(HID_UP_DISPLAY | 0x2A)

#define HID_DISP_DATA			(HID_UP_DISPLAY | 0x2C)

#define HID_DISP_ROW			(HID_UP_DISPLAY | 0x33)
#define HID_DISP_COLUMN			(HID_UP_DISPLAY | 0x34)
#define HID_DISP_ROWS			(HID_UP_DISPLAY | 0x35)
#define HID_DISP_COLUMNS		(HID_UP_DISPLAY | 0x36)

struct hid_ctrl {
	struct hid_field *field;
	unsigned offset;
};

struct hid_display_par {
	struct fb_info *info;
	struct hid_device *hid;

	struct delayed_work free_work;
	int open;

	unsigned rows;
	unsigned columns;
	bool ascii_character_set;

	struct hid_ctrl row;
	struct hid_ctrl column;
	struct hid_ctrl data;
};

/* Default screeninfo */
static struct fb_fix_screeninfo hid_display_fix_screeninfo = {
	.id		= "alphanumeric-lcd",
	.type		= FB_TYPE_TEXT,
	.type_aux	= FB_AUX_TEXT_MDA,
	.visual		= FB_VISUAL_MONO01,
	.accel		= FB_ACCEL_NONE,
};

#define MAX_DATA_REPORT_SIZE 64

static void hid_display_free_work(struct work_struct *work)
{
	struct hid_display_par *par = container_of(
		work, struct hid_display_par, free_work.work);
	struct fb_info *info = par->info;

	unregister_framebuffer(info);
	framebuffer_release(info);
}

/* Run with fb_info lock held */
static int hid_display_open(struct fb_info *info, int user)
{
	struct hid_display_par *par = info->par;
	int err;

	if (!par->hid)
		return -ENODEV;

	if (par->open == 0) {
		err = hid_hw_power(par->hid, PM_HINT_FULLON);
		if (err < 0) {
			hid_err(par->hid, "failed to power device\n");
			return err;
		}

		err = hid_hw_open(par->hid);
		if (err < 0) {
			hid_err(par->hid, "failed to open device\n");
			hid_hw_power(par->hid, PM_HINT_NORMAL);
			return err;
		}
	}
	par->open += 1;
	return 0;
}

/* Run with fb_info lock held */
static int hid_display_release(struct fb_info *info, int user)
{
	struct hid_display_par *par = info->par;

	if (par->open <= 0)
		return -EINVAL;

	par->open -= 1;
	if (par->open == 0) {
		if (par->hid) {
			hid_hw_close(par->hid);
			hid_hw_power(par->hid, PM_HINT_NORMAL);
		} else {
			/* Free fb_info once this function returned */
			schedule_delayed_work(&par->free_work, HZ);
		}
	}
	return 0;
}

static ssize_t hid_display_read(struct fb_info *info, char __user *buf,
			size_t count, loff_t *ppos)
{
	return -ENOTSUPP;
}

/* Run without fb_info lock held, we must take it ourself. */
static ssize_t hid_display_write(struct fb_info *info, const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct hid_display_par *par = info->par;
	char data[MAX_DATA_REPORT_SIZE];
	int display_size;
	int report_size;
	int row, column;
	int pos, len;
	int err;

	lock_fb_info(info);
	if (!par->hid) {
		err = -ENODEV;
		goto finish;
	}

	pos = *ppos;
	display_size = par->columns * par->rows;
	report_size = par->data.field->report_count;

	if (pos >= display_size || count == 0) {
		err = 0;
		goto finish;
	}

	if (pos + count > display_size)
		count = display_size - pos;

	/* Set the initial cursor position */
	row = pos / par->columns;
	column = pos % par->columns;

	for (len = 0; len < count; len += report_size) {
		int i, block_len = report_size;

		if (len + block_len >= count)
			block_len = count - len;

		err = copy_from_user(data, buf + len, block_len);
		if (err)
			goto finish;

		/* Update the cursor position */
		hid_set_field(par->row.field, par->row.offset, row);
		if (par->row.field->report != par->data.field->report &&
			par->row.field->report != par->column.field->report)
			usbhid_submit_report(par->hid, par->row.field->report,
					USB_DIR_OUT);

		hid_set_field(par->column.field, par->column.offset, column);
		if (par->column.field->report != par->data.field->report)
			usbhid_submit_report(par->hid, par->column.field->report,
					USB_DIR_OUT);

		/* Fill the data report */
		for (i = 0; i < block_len; i++)
			hid_set_field(par->data.field, par->data.offset + i,
				data[i]);

		for (; i < report_size ; i++)
			hid_set_field(par->data.field, par->data.offset + i,
				0);

		/* Send the data report */
		usbhid_submit_report(par->hid, par->data.field->report,
				USB_DIR_OUT);

		/* Update the cursor position */
		column += block_len;
		while (column > par->columns) {
			column -= par->columns;
			row += 1;
		}
	}
	err = len;

finish:
	unlock_fb_info(info);
	return err;
}

static struct fb_ops hid_display_fbops = {
	.owner		= THIS_MODULE,
	.fb_open	= hid_display_open,
	.fb_release	= hid_display_release,

	.fb_read	= hid_display_read,
	.fb_write	= hid_display_write,
};

static int hid_display_read_feature_usage(
	struct hid_display_par *par, struct hid_field *field, unsigned offset)
{
	struct hid_usage *usage = &field->usage[offset];

	switch (usage->hid) {
	case HID_DISP_ASCII_CHARACTER_SET:
		par->ascii_character_set = field->value[offset];
		return 0;
	case HID_DISP_ROWS:
		par->rows = field->value[offset];
		return 0;
	case HID_DISP_COLUMNS:
		par->columns = field->value[offset];
		return 0;
	case HID_DISP_ROW:
		par->row.field = field;
		par->row.offset = offset;
		return 0;
	case HID_DISP_COLUMN:
		par->column.field = field;
		par->column.offset = offset;
		return 0;
	case HID_DISP_DATA:
		par->data.field = field;
		par->data.offset = offset;
		return 0;
	}

	return 0;
}

int hid_display_read_features(struct hid_display_par *par)
{
	struct hid_report_enum *rep_enum;
	struct hid_report *rep;
	int i, j, err;

	rep_enum = &par->hid->report_enum[HID_FEATURE_REPORT];
	list_for_each_entry(rep, &rep_enum->report_list, list) {
		for (i = 0; i < rep->maxfield; i++) {
			struct hid_field *field = rep->field[i];
			for (j = 0; j < field->maxusage; j++) {
				err = hid_display_read_feature_usage(
					par, field, j);
				if (err)
					return err;
			}
		}
	}

	return 0;
}

int hid_display_connect(struct hid_device *hid)
{
	struct hid_display_par *par;
	struct fb_info *info;
	int err;

	info = framebuffer_alloc(sizeof(*par), &hid->dev);
	if (!info)
		return -ENOMEM;

	par = info->par;
	par->info = info;
	par->hid = hid;
	INIT_DELAYED_WORK(&par->free_work, hid_display_free_work);

	err = hid_display_read_features(par);
	if (err) {
		framebuffer_release(info);
		return err;
	}

	hid_dbg(hid, "ascii_character_set = %d\n", par->ascii_character_set);
	hid_dbg(hid, "rows = %d\n", par->rows);
	hid_dbg(hid, "columns = %d\n", par->columns);
	hid_dbg(hid, "row = %p/%u\n", par->row.field, par->row.offset);
	hid_dbg(hid, "column = %p/%u\n", par->column.field, par->column.offset);
	hid_dbg(hid, "data = %p/%u\n", par->data.field, par->data.offset);

	/* Check that the display is usable */
	if (!(par->ascii_character_set && par->rows && par->columns &&
		par->row.field && par->column.field && par->data.field)) {
		hid_dbg(hid, "some parameter is missing\n");
		framebuffer_release(info);
		return -ENODEV;
	}

	info->fbops = &hid_display_fbops;
	info->fix = hid_display_fix_screeninfo;
	info->fix.line_length = par->columns;
	snprintf(info->fix.id, sizeof(info->fix.id), "HID:%04X:%04X",
		hid->vendor, hid->product);
	info->flags = FBINFO_DEFAULT;
	info->var.xres = info->var.xres_virtual = par->columns;
	info->var.yres = info->var.yres_virtual = par->rows;

	err = register_framebuffer(info);
	if (err < 0) {
		framebuffer_release(info);
		return err;
	}

	hid->display = info;
	return 0;
}
EXPORT_SYMBOL_GPL(hid_display_connect);

void hid_display_disconnect(struct hid_device *hid)
{
	struct fb_info *info = hid->display;
	struct hid_display_par *par = info->par;

	lock_fb_info(info);
	par->hid = NULL;
	unlock_fb_info(info);

	hid->display = NULL;
	if (par->open == 0)
		schedule_delayed_work(&par->free_work, 0);
}
EXPORT_SYMBOL_GPL(hid_display_disconnect);
