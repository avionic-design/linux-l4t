/*
 * Copyright 2015 Alban Bedel <alban.bedel@avionic-design.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/nvhost.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/completion.h>
#include <linux/bug.h>

#include <mach/powergate.h>
#include <mach/clk.h>

#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-of.h>
#include <media/videobuf2-dma-contig.h>
#include <media/soc_mediabus.h>

#include "bus_client.h"
#include "nvhost_acm.h"
#include "nvhost_channel.h"
#include "t124/t124.h"

#include "tegra_vi2.h"

#define DRV_NAME "tegra-vi2"

#define V4L2_MBUS_CSI2_CLOCK (V4L2_MBUS_CSI2_CONTINUOUS_CLOCK | \
				V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK)

#define V4L2_FOURCC_TO_CHARS(f) ((f) & 0xFF), (((f) >> 8) & 0xFF), \
		(((f) >> 16) & 0xFF), (((f) >> 24) & 0xFF)

#define TEGRA_MBUS_FORMATS_YUV422	\
	V4L2_MBUS_FMT_UYVY8_2X8,	\
	V4L2_MBUS_FMT_UYVY8_1X16

#define TEGRA_FORMATS_RAW(order)				\
	{							\
		.v4l2 = V4L2_PIX_FMT_S ## order ## 8,		\
		.nv = 16,					\
		.mbus = {					\
			V4L2_MBUS_FMT_S ## order ## 8_1X8,	\
			V4L2_MBUS_FMT_S ## order ## 10_1X10,	\
			V4L2_MBUS_FMT_S ## order ## 12_1X12,	\
		},						\
	},							\
	{							\
		.v4l2 = V4L2_PIX_FMT_S ## order ## 14,		\
		.nv = 32,					\
		.mbus = {					\
			V4L2_MBUS_FMT_S ## order ## 12_1X12,	\
			V4L2_MBUS_FMT_S ## order ## 10_1X10,	\
			V4L2_MBUS_FMT_S ## order ## 8_1X8,	\
		},						\
	}

static int tegra_vi_channel_set_format(
	struct tegra_vi_channel *chan, struct v4l2_pix_format *pf);

static int tegra_vi_input_get_csi_params(
	const struct tegra_vi_input *input, int *csi_lanes, int *csi_channel,
	bool *continuous_clk);

static struct device_dma_parameters dma_parameters = {
	.max_segment_size = UINT_MAX,
	.segment_boundary_mask = 0xffffffff,
};

/* List of all supported pixel formats along with the MBUS formats
 * they support. The MBUS formats are ordered by preference, so we
 * always start with MBUS format with the same bit depth, then
 * the formats that would reduce the bit depth, then those
 * that would increase it.
 */
static const struct tegra_formats tegra_formats[] = {
	/* RGB formats */
	{
		.v4l2 = V4L2_PIX_FMT_RGB32,
		.nv = 66,
		.mbus = {
			V4L2_MBUS_FMT_RGB888_1X24,
			V4L2_MBUS_FMT_RGB666_1X18,
		},
	},
	{
		.v4l2 = V4L2_PIX_FMT_BGR32,
		.nv = 65,
		.mbus = {
			V4L2_MBUS_FMT_RGB888_1X24,
			V4L2_MBUS_FMT_RGB666_1X18,
		},
	},
	{
		.v4l2 = V4L2_PIX_FMT_RGB565,
		.nv = 33,
		.mbus = {
			V4L2_MBUS_FMT_RGB565_2X8_LE,
			V4L2_MBUS_FMT_RGB666_1X18,
			V4L2_MBUS_FMT_RGB888_1X24,
			V4L2_MBUS_FMT_RGB555_2X8_PADHI_LE,
			V4L2_MBUS_FMT_RGB444_2X8_PADHI_LE,
		},
	},
	{
		.v4l2 = V4L2_PIX_FMT_RGB555,
		.nv = 37,
		.mbus = {
			V4L2_MBUS_FMT_RGB555_2X8_PADHI_LE,
			V4L2_MBUS_FMT_RGB565_2X8_LE,
			V4L2_MBUS_FMT_RGB666_1X18,
			V4L2_MBUS_FMT_RGB888_1X24,
			V4L2_MBUS_FMT_RGB444_2X8_PADHI_LE,
		},
	},

	/* Bayer formats */
	TEGRA_FORMATS_RAW(BGGR),
	TEGRA_FORMATS_RAW(GBRG),
	TEGRA_FORMATS_RAW(GRBG),
	TEGRA_FORMATS_RAW(RGGB),

	/* YUV packed formats */
	{
		.v4l2 = V4L2_PIX_FMT_YUYV,
		.nv = 200,
		.mbus = {
			TEGRA_MBUS_FORMATS_YUV422,
		},
	},
	{
		.v4l2 = V4L2_PIX_FMT_YVYU,
		.nv = 201,
		.mbus = {
			TEGRA_MBUS_FORMATS_YUV422,
		},
	},
	{
		.v4l2 = V4L2_PIX_FMT_UYVY,
		.nv = 202,
		.mbus = {
			TEGRA_MBUS_FORMATS_YUV422,
		},
	},
	{
		.v4l2 = V4L2_PIX_FMT_VYUY,
		.nv = 203,
		.mbus = {
			TEGRA_MBUS_FORMATS_YUV422,
		},
	},

	/* YUV planar formats */
	{
		.v4l2 = V4L2_PIX_FMT_YUV422P,
		.nv = 227,
		.mbus = {
			TEGRA_MBUS_FORMATS_YUV422,
		},
	},

	/* YUV semi-planar formats */
	{
		.v4l2 = V4L2_PIX_FMT_NV16,
		.nv = 228,
		.mbus = {
			TEGRA_MBUS_FORMATS_YUV422,
		},
	},
	{
		.v4l2 = V4L2_PIX_FMT_NV61,
		.nv = 229,
		.mbus = {
			TEGRA_MBUS_FORMATS_YUV422,
		},
	},

	/* Grey formats */
	{
		.v4l2 = V4L2_PIX_FMT_GREY,
		.nv = 16,
		.mbus = {
			V4L2_MBUS_FMT_Y8_1X8,
			V4L2_MBUS_FMT_Y10_1X10,
			V4L2_MBUS_FMT_Y12_1X12,
			TEGRA_MBUS_FORMATS_YUV422,
			V4L2_MBUS_FMT_RGB888_1X24,
			V4L2_MBUS_FMT_RGB666_1X18,
			V4L2_MBUS_FMT_RGB565_2X8_LE,
			V4L2_MBUS_FMT_RGB555_2X8_PADHI_LE,
			V4L2_MBUS_FMT_RGB444_2X8_PADHI_LE,
		},
	},
	{
		.v4l2 = V4L2_PIX_FMT_Y14,
		.nv = 32,
		.mbus = {
			V4L2_MBUS_FMT_Y12_1X12,
			V4L2_MBUS_FMT_Y10_1X10,
			V4L2_MBUS_FMT_Y8_1X8,
		},
	},
};

static bool tegra_vi_field_is_interlaced(enum v4l2_field field)
{
	switch (field) {
	case V4L2_FIELD_INTERLACED:
	case V4L2_FIELD_SEQ_TB:
	case V4L2_FIELD_SEQ_BT:
	case V4L2_FIELD_INTERLACED_TB:
	case V4L2_FIELD_INTERLACED_BT:
		return true;
	default:
		return false;
	}
}

static int tegra_vi_fill_pix_format(struct v4l2_pix_format *pf,
				const struct v4l2_mbus_framefmt *framefmt)
{
	enum v4l2_field field;
	bool interlaced;

	/* Check for a sane resolution */
	if (!framefmt->height || !framefmt->width)
		return -EINVAL;

	/* Convert the field format */
	switch (framefmt->field) {
	case V4L2_FIELD_ANY:
	case V4L2_FIELD_NONE:
		interlaced = false;
		break;
	case V4L2_FIELD_INTERLACED:
	case V4L2_FIELD_SEQ_TB:
	case V4L2_FIELD_SEQ_BT:
	case V4L2_FIELD_INTERLACED_TB:
	case V4L2_FIELD_INTERLACED_BT:
		interlaced = true;
		break;
	default:
		return -EINVAL;
	}

	/* With interlaced try to return the requested field format */
	if (interlaced)
		field = tegra_vi_field_is_interlaced(pf->field) ?
			pf->field : V4L2_FIELD_INTERLACED_TB;
	else
		field = V4L2_FIELD_NONE;

	v4l2_fill_pix_format(pf, framefmt);
	pf->field = field;

	return v4l2_pix_format_set_sizeimage(pf);
}

static int mbus_format_to_csi_data_type(enum v4l2_mbus_pixelcode mbus)
{
	switch (mbus) {
	case V4L2_MBUS_FMT_UYVY8_2X8:
	case V4L2_MBUS_FMT_UYVY8_1X16:
		return 30;
	case V4L2_MBUS_FMT_RGB444_2X8_PADHI_LE:
		return 32;
	case V4L2_MBUS_FMT_RGB555_2X8_PADHI_LE:
		return 33;
	case V4L2_MBUS_FMT_BGR565_2X8_LE:
		return 34;
	case V4L2_MBUS_FMT_RGB666_1X18:
		return 35;
	case V4L2_MBUS_FMT_RGB888_1X24:
		return 36;
	case V4L2_MBUS_FMT_SBGGR8_1X8:
	case V4L2_MBUS_FMT_SGBRG8_1X8:
	case V4L2_MBUS_FMT_SGRBG8_1X8:
	case V4L2_MBUS_FMT_SRGGB8_1X8:
	case V4L2_MBUS_FMT_Y8_1X8:
		return 42;
	case V4L2_MBUS_FMT_SBGGR10_1X10:
	case V4L2_MBUS_FMT_SGBRG10_1X10:
	case V4L2_MBUS_FMT_SGRBG10_1X10:
	case V4L2_MBUS_FMT_SRGGB10_1X10:
	case V4L2_MBUS_FMT_Y10_1X10:
		return 43;
	case V4L2_MBUS_FMT_SBGGR12_1X12:
	case V4L2_MBUS_FMT_SGBRG12_1X12:
	case V4L2_MBUS_FMT_SGRBG12_1X12:
	case V4L2_MBUS_FMT_SRGGB12_1X12:
	case V4L2_MBUS_FMT_Y12_1X12:
		return 44;

	default:
		return -EINVAL;
	}
};

static int tegra_vi_channel_input_lock(struct tegra_vi_channel *chan,
				bool not_streaming)
{
	mutex_lock(&chan->lock);

	if (!chan->input) {
		mutex_unlock(&chan->lock);
		return -ENODEV;
	}

	if (not_streaming && vb2_is_streaming(&chan->vb)) {
		mutex_unlock(&chan->lock);
		return -EBUSY;
	}

	mutex_lock(&chan->input->lock);

	if (!chan->input->sensor) {
		mutex_unlock(&chan->input->lock);
		mutex_unlock(&chan->lock);
		return -ENODEV;
	}

	return 0;
}

static void tegra_vi_channel_input_unlock(struct tegra_vi_channel *chan)
{
	mutex_unlock(&chan->input->lock);
	mutex_unlock(&chan->lock);
}

static int tegra_vi_channel_querycap(
	struct file *file, void *__fh, struct v4l2_capability *cap)
{
	memset(cap, 0, sizeof(struct v4l2_capability));
	strcpy(cap->driver, DRV_NAME);
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	return 0;
}

static int tegra_vi_channel_enum_input(
	struct file *file, void *__fh, struct v4l2_input *i)
{
	struct video_device *vdev = video_devdata(file);
	struct tegra_vi2 *vi2 =
		container_of(vdev->v4l2_dev, struct tegra_vi2, v4l2_dev);
	struct tegra_vi_channel *chan =
		container_of(vdev, struct tegra_vi_channel, vdev);
	struct tegra_vi_input *input;
	u32 index = i->index;
	int has_s_power;

	memset(i, 0, sizeof(*i));
	i->index = index;

	switch (i->index) {
	case INPUT_CSI_A:
		strcpy(i->name, "CSI A");
		break;
	case INPUT_CSI_B:
		strcpy(i->name, "CSI B");
		break;
	case INPUT_CSI_C:
		strcpy(i->name, "CSI C");
		break;
	case INPUT_PATTERN_GENERATOR:
		strcpy(i->name, "Pattern Generator");
		break;
	default:
		return -EINVAL;
	}

	/* Report the input as powered down for compatibility with naive
	 * enumerations that stop on any error, not only EINVAL. */
	input = i->index == INPUT_PATTERN_GENERATOR ?
		&chan->tpg : &vi2->input[i->index];
	if (!input->sensor) {
		i->status = V4L2_IN_ST_NO_POWER;
		return 0;
	}

	/* Guess the input type */
	if (v4l2_subdev_has_op(input->sensor, tuner, s_frequency))
		i->type = V4L2_INPUT_TYPE_TUNER;
	else
		i->type = V4L2_INPUT_TYPE_CAMERA;

	/* Get the input status if possible */
	has_s_power = v4l2_subdev_has_op(input->sensor, core, s_power);
	if (has_s_power && input->use_count == 0)
		i->status = V4L2_IN_ST_NO_POWER;
	else
		v4l2_subdev_call(input->sensor, video,
				g_input_status, &i->status);

	/* Check if S_DV_TIMINGS is supported by the sensor */
	if (v4l2_subdev_has_op(input->sensor, video, s_dv_timings))
		i->capabilities |= V4L2_IN_CAP_DV_TIMINGS;

	/* Check if S_STD is supported by the sensor */
	if (v4l2_subdev_has_op(input->sensor, core, s_std))
		i->capabilities |= V4L2_IN_CAP_STD;

	return 0;
}

static int tegra_vi_channel_g_input(
	struct file *file, void *__fh, unsigned int *i)
{
	struct video_device *vdev = video_devdata(file);
	struct tegra_vi_channel *chan =
		container_of(vdev, struct tegra_vi_channel, vdev);
	int err = 0;

	mutex_lock(&chan->lock);
	if (chan->input_id >= 0)
		*i = chan->input_id;
	else
		err = -EINVAL;
	mutex_unlock(&chan->lock);

	return err;
}

static int tegra_format_support_mbus(
	const struct tegra_formats *fmt, enum v4l2_mbus_pixelcode mbus)
{
	unsigned i;

	if (!fmt)
		return -EINVAL;

	for (i = 0; fmt->mbus[i]; i++) {
		if (fmt->mbus[i] == mbus)
			return 1;
	}

	return 0;
}

static int tegra_formats_match_mbus(
	enum v4l2_mbus_pixelcode mbus, unsigned long *mask)
{
	int count = 0;
	int f;

	compiletime_assert(
		sizeof(unsigned long) * 8 >= ARRAY_SIZE(tegra_formats),
		"Too many output formats, use a larger mask size");

	for (f = 0; f < ARRAY_SIZE(tegra_formats); f++) {
		if (*mask & BIT(f))
			continue;
		if (tegra_format_support_mbus(&tegra_formats[f], mbus) > 0) {
			*mask |= BIT(f);
			count++;
		}
	}
	return count;
}

static int tegra_vi_sensor_support_mbus(
	struct v4l2_subdev *sensor, enum v4l2_mbus_pixelcode code)
{
	enum v4l2_mbus_pixelcode search;
	unsigned i = 0;

	while (!v4l2_subdev_call(sensor, video, enum_mbus_fmt, i, &search)) {
		if (search == code)
			return 1;
		i++;
	}

	return 0;
}

static int tegra_vi_channel_update_sensor_formats(
	struct tegra_vi_channel *chan, const struct tegra_vi_input *input)
{
	struct v4l2_subdev *sensor = input->sensor;
	enum v4l2_mbus_pixelcode code;
	unsigned long mask = 0;
	unsigned index = 0;
	int i, f, m;

	/* Make sure the mask is large enough */
	BUILD_BUG_ON(ARRAY_SIZE(tegra_formats) > BITS_PER_LONG);

	/* Get all output formats that can be used */
	if (input->use_count > 1) {
		chan->formats_count = tegra_formats_match_mbus(
			input->framefmt.code, &mask);
	} else {
		chan->formats_count = 0;
		while (!v4l2_subdev_call(sensor, video, enum_mbus_fmt,
					 index, &code)) {
			chan->formats_count +=
				tegra_formats_match_mbus(code, &mask);
			index++;
		}
	}

	if (chan->formats_count >= ARRAY_SIZE(chan->formats))
		chan->formats_count = ARRAY_SIZE(chan->formats);

	/* Fill the table with the supported formats */
	for (i = 0; i < chan->formats_count && mask; i++) {
		const struct tegra_formats *src;
		struct tegra_formats *dst;

		f = __ffs(mask);
		mask &= ~BIT(f);

		src = &tegra_formats[f];
		dst = &chan->formats[i];

		dst->v4l2 = src->v4l2;
		dst->nv = src->nv;

		/* Fill the supported mbus formats */
		for (m = index = 0; src->mbus[index]; index++) {
			if (tegra_vi_sensor_support_mbus(
					sensor, src->mbus[index]) > 0) {
				dst->mbus[m] = src->mbus[index];
				m++;
			}
		}
	}

	/* Invalidate the rest of the table */
	for (; i < ARRAY_SIZE(chan->formats); i++) {
		struct tegra_formats *fmt = &chan->formats[i];
		fmt->mbus[0] = fmt->nv = fmt->v4l2 = 0;
	}

	return chan->formats_count;
}

static u32 tegra_vi_channel_find_format_for_mbus(
	const struct tegra_vi_channel *chan, enum v4l2_mbus_pixelcode code)
{
	const struct tegra_formats *best_fmt = NULL;
	int f, m, best_fmt_pos = 0;

	for (f = 0; f < chan->formats_count; f++) {
		const struct tegra_formats *fmt = &chan->formats[f];
		for (m = 0; m < ARRAY_SIZE(fmt->mbus) && fmt->mbus[m]; m++) {
			if (fmt->mbus[m] != code)
				continue;
			if (!best_fmt || m < best_fmt_pos) {
				best_fmt = fmt;
				best_fmt_pos = m;
			}
			break;
		}
	}

	return best_fmt ? best_fmt->v4l2 : 0;
}

static int tegra_vi_input_enable(struct tegra_vi_input *input)
{
	int err;

	/* Nothing to do if it is already running */
	if (input->use_count > 0) {
		input->use_count++;
		return 0;
	}

	/* Enable the CIL clock and power the sensor */
	if (input->cil_clk) {
		err = clk_prepare_enable(input->cil_clk);
		if (err)
			return err;
	}

	err = v4l2_subdev_call(input->sensor, core, s_power, 1);
	if (err && err != -ENOIOCTLCMD) {
		if (input->cil_clk)
			clk_disable_unprepare(input->cil_clk);
		return err;
	}

	input->use_count++;
	return 0;
}

static void tegra_vi_input_disable(struct tegra_vi_input *input)
{
	if (input->use_count <= 0)
		return;

	input->use_count--;

	/* Nothing to do if there is still another user */
	if (input->use_count > 0)
		return;

	/* Disable the sensor power and CIL clock */
	v4l2_subdev_call(input->sensor, core, s_power, 0);
	if (input->cil_clk)
		clk_disable_unprepare(input->cil_clk);

	memset(&input->framefmt, 0, sizeof(input->framefmt));
}

static int tegra_vi_channel_set_input(
	struct tegra_vi_channel *chan, enum tegra_vi_input_id i)
{
	struct video_device *vdev = &chan->vdev;
	struct tegra_vi2 *vi2 =
		container_of(vdev->v4l2_dev, struct tegra_vi2, v4l2_dev);
	struct v4l2_mbus_framefmt framefmt = {};
	struct v4l2_pix_format pf = {};
	struct tegra_vi_input *input;
	int err;

	switch (i) {
	case INPUT_NONE:
		input = NULL;
		break;
	case INPUT_CSI_A:
	case INPUT_CSI_B:
	case INPUT_CSI_C:
		input = &vi2->input[i];
		break;
	case INPUT_PATTERN_GENERATOR:
		input = &chan->tpg;
		break;
	default:
		dev_err(&vdev->dev, "Bad input ID\n");
		return -EINVAL;
	}

	/* Release the old input */
	if (chan->input) {
		mutex_lock(&chan->input->lock);

		/* Same input, nothing to do */
		if (input == chan->input) {
			mutex_unlock(&chan->input->lock);
			return 0;
		}

		tegra_vi_input_disable(chan->input);

		mutex_unlock(&chan->input->lock);

		chan->input = NULL;
		memset(&chan->pixfmt, 0, sizeof(chan->pixfmt));
		chan->formats_count = 0;
		chan->vdev.ctrl_handler = NULL;
	}

	if (!input)
		return 0;

	/* Set the new input */
	mutex_lock(&input->lock);

	/* No sensor connected to this input */
	if (!input->sensor) {
		mutex_unlock(&input->lock);
		return -ENODEV;
	}

	err = tegra_vi_input_enable(input);
	if (err) {
		mutex_unlock(&input->lock);
		return err;
	}

	/* Build the list of formats supported by the sensor */
	tegra_vi_channel_update_sensor_formats(chan, input);

	/* Clear the current channel format */
	memset(&chan->pixfmt, 0, sizeof(chan->pixfmt));

	if (chan->formats_count <= 0) {
		dev_warn(&vdev->dev,
			"Input set to %d but no format available\n", i);
		err = -EINVAL;
		goto disable_input;
	}

	/* Get the frame format */
	if (input->use_count > 1) {
		framefmt = input->framefmt;
	} else {
		/* Try to get the current format from the sensor */
		err = v4l2_subdev_call(input->sensor, video,
				g_mbus_fmt, &framefmt);
		if (err) {
			framefmt.width = 0xFFFFFFFF;
			framefmt.height = 0xFFFFFFFF;
			framefmt.code = chan->formats[0].mbus[0];
			framefmt.field = V4L2_FIELD_NONE;

			err = v4l2_subdev_call(input->sensor, video,
					try_mbus_fmt, &framefmt);
			if (err) {
				dev_err(&vdev->dev,
					"Failed to try default sensor format\n");
				goto disable_input;
			}
		}
	}

	chan->input = input;
	chan->input_id = input->id;

	/* Now find the best format that support this mbus format */
	v4l2_fill_pix_format(&pf, &framefmt);
	pf.pixelformat =
		tegra_vi_channel_find_format_for_mbus(chan, framefmt.code);
	/* Abort if none has been found */
	if (!pf.pixelformat) {
		dev_warn(&vdev->dev, "Failed to find format for input %d\n", i);
		err = -EINVAL;
		goto disable_input;
	}

	err = tegra_vi_channel_set_format(chan, &pf);
	if (err) {
		dev_warn(&vdev->dev, "Failed to set format for input %d\n", i);
		goto disable_input;
	}

	/* Connect ctrl_handler */
	chan->vdev.ctrl_handler = input->sensor->ctrl_handler;

	mutex_unlock(&input->lock);

	return 0;

disable_input:
	tegra_vi_input_disable(input);
	mutex_unlock(&input->lock);
	return err;
}

static int tegra_vi_channel_s_input(
	struct file *file, void *__fh, unsigned int i)
{
	struct video_device *vdev = video_devdata(file);
	struct tegra_vi_channel *chan =
		container_of(vdev, struct tegra_vi_channel, vdev);
	int err;

	mutex_lock(&chan->lock);

	if (vb2_is_streaming(&chan->vb)) {
		dev_err(&vdev->dev, "Can't set input when streaming!\n");
		err = -EBUSY;
	} else {
		err = tegra_vi_channel_set_input(chan, i);
		if (err)
			dev_err(&vdev->dev, "Failed to set input to %d: %d\n",
				i, err);
	}

	mutex_unlock(&chan->lock);

	return err;
}

static int tegra_vi_channel_enum_fmt_vid_cap(
	struct file *file, void *__fh, struct v4l2_fmtdesc *fd)
{
	struct video_device *vdev = video_devdata(file);
	struct tegra_vi_channel *chan =
		container_of(vdev, struct tegra_vi_channel, vdev);
	int err;

	if (fd->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	err = tegra_vi_channel_input_lock(chan, false);
	if (err)
		return err;

	if (fd->index >= chan->formats_count) {
		err = -EINVAL;
	} else {
		fd->pixelformat = chan->formats[fd->index].v4l2;
	}

	tegra_vi_channel_input_unlock(chan);

	return err;
}

static const struct tegra_formats *tegra_vi_channel_get_format(
	const struct tegra_vi_channel *chan, u32 pixfmt)
{
	unsigned i;

	/* Lookup this pixel format in our list */
	for (i = 0; i < chan->formats_count; i++) {
		if (chan->formats[i].v4l2 == pixfmt)
			return &chan->formats[i];
	}

	return NULL;
}

void tegra_vi_input_start(const struct tegra_vi2 *vi2,
			const struct tegra_vi_input *input)
{
	u32 ctrl0 = 0x46;
	u32 cfg0;

	/* Nothing to do if there is no CIL */
	if (!input->cil_regs[0])
		return;

	/* If the clock is continuous we must bypass the LP sequence */
	if (input->csi_clk_continuous)
		ctrl0 |= BIT(6);

	/* Enable the clock and lanes */
	cfg0 = input->csi_lanes > 1 ? 0 : 2;

	/* Dual blocks should use the first clock */
	if (input->cil_regs[1] && input->csi_lanes > 2)
		cfg0 |= 1 << 16;

	vi_writel(0, &input->cil_regs[0]->interrupt_mask);
	vi_writel(cfg0, &input->cil_regs[0]->pad_config0);
	vi_writel(ctrl0, &input->cil_regs[0]->cil_control0);

	if (input->cil_regs[1]) {
		/* Always disable the second block */
		cfg0 = 7;

		/* Enable clock and lane 2 */
		/* TODO: Check if the clock is needed here */
		if (input->csi_lanes > 2)
			cfg0 &= ~(BIT(2) | BIT(0));
		if (input->csi_lanes > 3)
			cfg0 &= ~BIT(1);

		vi_writel(0, &input->cil_regs[1]->interrupt_mask);
		vi_writel(cfg0, &input->cil_regs[1]->pad_config0);
		vi_writel(ctrl0, &input->cil_regs[1]->cil_control0);
	}

	/* Always enable the first PHY */
	cfg0 = 1 << input->phy_shift[0];
	/* Only enable the second PHY if there is more than 2 lanes */
	if (input->cil_regs[1] && input->csi_lanes > 2)
		cfg0 |= 1 << input->phy_shift[1];
	vi_writel(cfg0, &vi2->phy_regs->cil_command);
}

void tegra_vi_input_stop(const struct tegra_vi2 *vi2,
			const struct tegra_vi_input *input)
{
	u32 val;

	if (!input->cil_regs[0])
		return;

	/* Always disable the first PHY */
	val = 2 << input->phy_shift[0];
	/* Only disable the second PHY if there is more than 2 lanes */
	if (input->cil_regs[1] && input->csi_lanes > 2)
		val |= 2 << input->phy_shift[1];
	vi_writel(val, &vi2->phy_regs->cil_command);

	if (input->cil_regs[1])
		vi_writel(0x7, &input->cil_regs[1]->pad_config0);
	vi_writel(0x7, &input->cil_regs[0]->pad_config0);
}

int tegra_vi_calibrate_input(struct tegra_vi2 *vi2,
			struct tegra_vi_input *input)
{
	struct device *dev = vi2->v4l2_dev.dev;
	struct tegra_mipi_cal_regs *regs = vi2->cal_regs;
	struct clk *clk = clk_get_sys("mipi-cal", NULL);
	struct clk *clk72mhz = clk_get_sys("clk72mhz", NULL);
	unsigned cal_channel = input->id * 2;
	unsigned noise_flt;
	int i, try = 100;
	u32 val;
	int err;

	/* Skip if already calibrated, or if no CIL is available */
	if (input->calibrated || !input->cil_regs[0])
		return 0;

	if (!vi2->cal_regs) {
		dev_err(dev, "MIPI calibration not available, skipping!\n");
		return 0;
	}

	if (IS_ERR_OR_NULL(clk) || IS_ERR_OR_NULL(clk72mhz)) {
		dev_err(dev, "Failed to get MIPI cal clocks\n");
		return -EINVAL;
	}

	err = clk_prepare_enable(clk);
	if (err) {
		dev_err(dev, "Failed to enable MIPI cal clock: %d\n",
			err);
		return err;
	}

	err = clk_prepare_enable(clk72mhz);
	if (err) {
		dev_err(dev, "Failed to enable MIPI cal fixed clock: %d\n",
			err);
		goto disable_clk;
	}

	mutex_lock(&vi2->lock);

	vi_writel((0xA << 26) | (0x2 << 24) | BIT(4), &regs->ctrl);
	vi_writel(0xFFFF0000, &regs->status);
	vi_writel(0x0000001F, &regs->clk_status);

	/* Enable VCLAMP */
	vi_writel(BIT(0), &regs->pad_cfg0);
	/* Set adjustment */
	vi_writel((2 << 16), &regs->pad_cfg1);
	/* Enable regulator */
	vi_writel(0, &regs->pad_cfg2);

	/* Clear all channels */
	for (i = 0; i < ARRAY_SIZE(regs->data_config); i++) {
		val = readl(&regs->data_config[i]);
		val &= ~BIT(21);
		vi_writel(val, &regs->data_config[i]);
	}
	for (i = 0; i < ARRAY_SIZE(regs->clk_config); i++) {
		val = readl(&regs->clk_config[i]);
		val &= ~BIT(21);
		vi_writel(val, &regs->clk_config[i]);
	}

	/* Enable the channels for this input */
	vi_writel(BIT(21), &regs->data_config[cal_channel]);
	vi_writel(BIT(21), &regs->clk_config[cal_channel]);
	if (input->csi_lanes > 2) {
		vi_writel(BIT(21), &regs->data_config[cal_channel + 1]);
		vi_writel(BIT(21), &regs->clk_config[cal_channel + 1]);
	}


	err = -ETIMEDOUT;
	for (noise_flt = 10; err && noise_flt <= 15; noise_flt++) {
		/* Clear the status and start the calibration process */
		vi_writel((noise_flt << 26) | (0x3 << 24) | BIT(4) | BIT(0), &regs->ctrl);

		for (try = 1000; try > 0; try--) {
			msleep(1);
			val = readl(&regs->status);
			if (!(val & BIT(0)) && (val & BIT(16))) {
				err = 0;
				break;
			}
		}

		if (err) {
			vi_writel((0xA << 26) | (0x2 << 24) | BIT(4), &regs->ctrl);
			vi_writel(0xFFFF0000, &regs->status);
			vi_writel(0x0000001F, &regs->clk_status);
		}
	}

	if (!err)
		input->calibrated = 1;
	else
		dev_err(dev, "Calibration of input %c timed out!\n",
			'A' + input->id);

	mutex_unlock(&vi2->lock);

	clk_disable_unprepare(clk72mhz);
disable_clk:
	clk_disable_unprepare(clk);

	if (!IS_ERR_OR_NULL(clk72mhz))
		clk_put(clk72mhz);

	if (!IS_ERR_OR_NULL(clk))
		clk_put(clk);

	return err;
}

static int tegra_vi_input_get_mbus_flags(
	const struct tegra_vi_input *input, unsigned int *flags)
{
	struct v4l2_mbus_config mbus_cfg = {
		.type = V4L2_MBUS_CSI2,
	};
	int err;

	/* Make sure we have input and sensor */
	if (!input || !input->sensor)
		return -EINVAL;

	/* Get the mbus config */
	err = v4l2_subdev_call(input->sensor, video, g_mbus_config, &mbus_cfg);
	if (err)
		return err;

	if (mbus_cfg.type != V4L2_MBUS_CSI2)
		return -EINVAL;

	/* Mask with the input capabilites */
	mbus_cfg.flags &= input->mbus_caps;

	/* Verifiy the config */
	if (!(mbus_cfg.flags & V4L2_MBUS_CSI2_LANES))
		return -EINVAL;
	if (!(mbus_cfg.flags & V4L2_MBUS_CSI2_CHANNELS))
		return -EINVAL;

	if (flags)
		*flags = mbus_cfg.flags;

	return 0;
}

static int tegra_vi_input_get_csi_params(
	const struct tegra_vi_input *input, int *csi_lanes, int *csi_channel,
	bool *continuous_clk)
{
	int mbus_flags;
	int err;

	/* If the input is already in use copy the current settings */
	if (input->use_count > 1) {
		if (csi_lanes)
			*csi_lanes = input->csi_lanes;
		if (csi_channel)
			*csi_channel = input->csi_channel;
		if (continuous_clk)
			*continuous_clk = input->csi_clk_continuous;
		return 0;
	}

	/* Get mbus format flags */
	err = tegra_vi_input_get_mbus_flags(input, &mbus_flags);
	if (err)
		return err;

	/* Get the lanes count */
	if (csi_lanes) {
		if (mbus_flags & V4L2_MBUS_CSI2_4_LANE)
			*csi_lanes = 4;
		else if (mbus_flags & V4L2_MBUS_CSI2_3_LANE)
			*csi_lanes = 3;
		else if (mbus_flags & V4L2_MBUS_CSI2_2_LANE)
			*csi_lanes = 2;
		else if (mbus_flags & V4L2_MBUS_CSI2_1_LANE)
			*csi_lanes = 1;
		else /* Shouldn't happen */
			return -EINVAL;
	}

	if (csi_channel) {
		if (mbus_flags & V4L2_MBUS_CSI2_CHANNEL_0)
			*csi_channel = 0;
		else if (mbus_flags & V4L2_MBUS_CSI2_CHANNEL_1)
			*csi_channel = 1;
		else if (mbus_flags & V4L2_MBUS_CSI2_CHANNEL_2)
			*csi_channel = 2;
		else if (mbus_flags & V4L2_MBUS_CSI2_CHANNEL_3)
			*csi_channel = 3;
		else /* Shouldn't happen */
			return -EINVAL;
	}

	if (continuous_clk) {
		if (mbus_flags & V4L2_MBUS_CSI2_CONTINUOUS_CLOCK)
			*continuous_clk = true;
		else
			*continuous_clk = false;
	}

	return 0;
}

static int tegra_vi_channel_get_mbus_framefmt(const struct tegra_vi_channel *chan,
					struct v4l2_pix_format *pf,
					struct v4l2_mbus_framefmt *framefmt,
					int *nv_fmt)
{
	const struct video_device *vdev = &chan->vdev;
	const struct tegra_formats *fmt;
	int err = -EINVAL;
	unsigned i;

	/* Get the tegra format from the output pixelformat */
	fmt = tegra_vi_channel_get_format(chan, pf->pixelformat);
	if (!fmt) {
		dev_err(&vdev->dev,
			"Failed to get the format struct for format %c%c%c%c\n",
			V4L2_FOURCC_TO_CHARS(pf->pixelformat));
		return -EINVAL;
	}

	/* Then convert to an mbus frame format */
	if (chan->input->use_count > 1) {
		*framefmt = chan->input->framefmt;
	} else {
		memset(framefmt, 0, sizeof(*framefmt));
		v4l2_fill_mbus_format(framefmt, pf, 0);

		/* And get the first accepted frame format */
		for (i = 0; err && fmt->mbus[i]; i++) {
			framefmt->code = fmt->mbus[i];
			err = v4l2_subdev_call(chan->input->sensor, video,
					try_mbus_fmt, framefmt);
		}

		if (err) {
			dev_err(&vdev->dev,
				"Failed to get a supported bus format\n");
			return err;
		}
	}

	/* Check that the mbus format is acceptable and fill pf */
	err = tegra_vi_fill_pix_format(pf, framefmt);
	if (err) {
		dev_err(&vdev->dev, "Got invalid bus format\n");
		return err;
	}

	if (nv_fmt)
		*nv_fmt = fmt->nv;

	return 0;
}

static int tegra_vi_channel_try_fmt_vid_cap(
	struct file *file, void *__fh, struct v4l2_format *f)
{
	struct video_device *vdev = video_devdata(file);
	struct tegra_vi_channel *chan =
		container_of(vdev, struct tegra_vi_channel, vdev);
	struct v4l2_mbus_framefmt framefmt;
	int err;

	/* We only support capture */
	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	err = tegra_vi_channel_input_lock(chan, false);
	if (err)
		return err;

	err = tegra_vi_input_get_mbus_flags(chan->input, NULL);

	if (!err)
		err = tegra_vi_channel_get_mbus_framefmt(
			chan, &f->fmt.pix, &framefmt, NULL);

	tegra_vi_channel_input_unlock(chan);

	return err;
}

static int tegra_vi_channel_set_format(
	struct tegra_vi_channel *chan, struct v4l2_pix_format *pf)
{
	struct tegra_vi_input *input = chan->input;
	struct video_device *vdev = &chan->vdev;
	int src, csi_dt, nv_fmt, line_size;
	struct v4l2_mbus_framefmt framefmt;
	unsigned int interlaced;
	int err;

	if (input == NULL) {
		dev_err(&vdev->dev, "No input selected\n");
		return -ENODEV;
	}

	/* Find the channel source */
	switch (input->id) {
	case INPUT_CSI_A:
		src = 0;
		break;
	case INPUT_CSI_B:
	case INPUT_CSI_C:
		src = 1;
		break;
	case INPUT_PATTERN_GENERATOR:
		src = chan->id;
		break;
	default:
		return -EINVAL;
	}

	/* Get the bus frame format */
	err = tegra_vi_channel_get_mbus_framefmt(
		chan, pf, &framefmt, &nv_fmt);
	if (err) {
		dev_err(&vdev->dev, "Failed to get bus format for pixfmt %c%c%c%c\n",
			V4L2_FOURCC_TO_CHARS(pf->pixelformat));
		return err;
	}

	/* Set the sensor mbus format if this is the first open */
	if (input->use_count == 1) {
		struct v4l2_mbus_framefmt ffmt = framefmt;

		err = v4l2_subdev_call(chan->input->sensor, video,
				       s_mbus_fmt, &framefmt);
		if (err) {
			dev_err(&vdev->dev, "Failed to set sensor format "
				"(width=%d,height=%d,code=0x%x)\n",
				ffmt.width, ffmt.height, ffmt.code);
			return err;
		}
		/* Store the effective format */
		chan->input->framefmt = framefmt;
	}

	/* Get the nvidia type for this bus format */
	csi_dt = mbus_format_to_csi_data_type(framefmt.code);
	if (csi_dt < 0) {
		dev_err(&vdev->dev, "No CSI data type found for MBUS format %x\n",
			framefmt.code);
		return csi_dt;
	}

	/* Get the line size in bytes */
	switch (csi_dt) {
	case 24: /* YUV420_8 */
	case 42: /* RAW8 */
		line_size = framefmt.width;
		break;
	case 30: /* YUV422_8 */
	case 32: /* RGB444 */
	case 33: /* RGB555 */
	case 34: /* RGB565 */
		line_size = framefmt.width * 2;
		break;
	case 36: /* RGB888 */
		line_size = framefmt.width * 3;
		break;
	case 25: /* YUV420_10 */
	case 43: /* RAW10 */
		line_size = framefmt.width / 4 * 5;
		break;
	case 31: /* YUV422_10 */
		line_size = framefmt.width / 2 * 5;
		break;
	case 44: /* RAW12 */
		line_size = framefmt.width / 2 * 3;
		break;
	default:
		dev_err(&vdev->dev, "Failed to get memory bus line size\n");
		return -EINVAL;
	}

	/* Return the effective settings */
	err = tegra_vi_fill_pix_format(pf, &framefmt);
	if (err) {
		dev_err(&vdev->dev, "Got invalid bus format\n");
		return err;
	}

	/* And store them to allow get */
	chan->pixfmt = *pf;

	/* Check if the format is interlaced */
	interlaced = tegra_vi_field_is_interlaced(pf->field);

	/* We must allow bad frames to be able to return buffers
	 * on errors. */
	vi_writel(0, &chan->vi_regs->single_shot_state_update);

	/* Reset the pixel parser and sensor logic */
	vi_writel(1, &chan->mipi_regs->sensor_reset);
	vi_writel(0xF003, &chan->mipi_regs->pp_command);
	vi_writel(0, &chan->mipi_regs->sensor_reset);

	/* Configure the pixel parser */
	vi_writel(src | /* Source */
		BIT(4) | /* With header */
		BIT(5) | /* With data identifier */
		BIT(6) | /* Header word count */
		BIT(7) | /* CRC check */
		BIT(8) | /* WC check */
		(1 << 16) | /* Format: PIXEL */
		/* Discard embedded data */
		/* Short line pad with 0 */
		/* EC enable */
		/* Pad frame with 0 */
		0,
		&chan->mipi_regs->control0);

	vi_writel(0x11, &chan->mipi_regs->control1);
	vi_writel(0x14 << 16, &chan->mipi_regs->gap);
	vi_writel(0, &chan->mipi_regs->expected_frame);

	/* Setup the number of lanes */
	vi_writel((input->csi_lanes - 1) | (0x3F << 16),
		&chan->mipi_regs->control);

	/* Setup the output format with MEM output */
	vi_writel((nv_fmt << 16) | BIT(0), &chan->vi_regs->image_def);
	/* Bus format */
	vi_writel(csi_dt | (input->csi_channel << 8) | (interlaced << 12),
		&chan->vi_regs->image_dt);
	/* Line size on the memory bus rounded up to the next word */
	vi_writel((line_size + 1) & ~1,
		&chan->vi_regs->image_size_wc);
	/* Resolution */
	vi_writel(((framefmt.height >> interlaced) << 16) | framefmt.width,
		&chan->vi_regs->image_size);

	return 0;
}


static int tegra_vi_channel_s_fmt_vid_cap(
	struct file *file, void *__fh, struct v4l2_format *f)
{
	struct video_device *vdev = video_devdata(file);
	struct tegra_vi_channel *chan =
		container_of(vdev, struct tegra_vi_channel, vdev);
	struct v4l2_pix_format *pf = &f->fmt.pix;
	int err;

	err = tegra_vi_channel_input_lock(chan, true);
	if (err)
		return err;

	err = tegra_vi_channel_set_format(chan, pf);

	tegra_vi_channel_input_unlock(chan);

	return err;
}

static int tegra_vi_channel_g_fmt_vid_cap(
	struct file *file, void *__fh, struct v4l2_format *f)
{
	struct video_device *vdev = video_devdata(file);
	struct tegra_vi_channel *chan =
		container_of(vdev, struct tegra_vi_channel, vdev);
	int err;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	err = tegra_vi_channel_input_lock(chan, false);
	if (err)
		return err;

	f->fmt.pix = chan->pixfmt;

	tegra_vi_channel_input_unlock(chan);

	return 0;
}

static int tegra_vi_channel_cropcap(
	struct file *file, void *__fh, struct v4l2_cropcap *cc)
{
	struct video_device *vdev = video_devdata(file);
	struct tegra_vi_channel *chan =
		container_of(vdev, struct tegra_vi_channel, vdev);
	int err;

	if (cc->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	err = tegra_vi_channel_input_lock(chan, false);
	if (err)
		return err;

	/* Fill with default values */
	cc->bounds.left = 0;
	cc->bounds.top = 0;
	cc->bounds.width = chan->pixfmt.width;
	cc->bounds.height = chan->pixfmt.height;
	cc->defrect = cc->bounds;
	cc->pixelaspect.numerator = 1;
	cc->pixelaspect.denominator = 1;

	/* Allow the sensor to override */
	err = v4l2_subdev_call(chan->input->sensor, video, cropcap, cc);
	if (err == -ENOIOCTLCMD)
		err = 0;

	tegra_vi_channel_input_unlock(chan);

	return err;
}

static int tegra_vi_channel_s_parm(struct file *file, void *priv,
	struct v4l2_streamparm *sparm)
{
	struct video_device *vdev = video_devdata(file);
	struct tegra_vi_channel *chan =
		container_of(vdev, struct tegra_vi_channel, vdev);
	struct v4l2_captureparm *cparm = &sparm->parm.capture;
	struct v4l2_subdev_frame_interval interval = {};
	int err = 0;

	if (sparm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	err = tegra_vi_channel_input_lock(chan, true);
	if (err)
		return err;

	if (cparm->timeperframe.numerator && cparm->timeperframe.denominator) {
		interval.interval = cparm->timeperframe;
		err = v4l2_subdev_call(chan->input->sensor, video,
				s_frame_interval, &interval);
		if (err == -ENOIOCTLCMD)
			err = 0;
	}

	cparm->timeperframe = interval.interval;

	if (cparm->readbuffers > 0)
		chan->readbuffers = min(cparm->readbuffers, 2u);
	cparm->readbuffers = chan->readbuffers;

	tegra_vi_channel_input_unlock(chan);

	return err;
}

static int tegra_vi_channel_g_parm(struct file *file, void *priv,
	struct v4l2_streamparm *sparm)
{
	struct video_device *vdev = video_devdata(file);
	struct tegra_vi_channel *chan =
		container_of(vdev, struct tegra_vi_channel, vdev);
	struct v4l2_captureparm *cparm = &sparm->parm.capture;
	struct v4l2_subdev_frame_interval interval = {};
	int err;

	if (sparm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	err = tegra_vi_channel_input_lock(chan, false);
	if (err)
		return err;

	err = v4l2_subdev_call(chan->input->sensor, video,
			g_frame_interval, &interval);
	if (!err)
		cparm->capability |= V4L2_CAP_TIMEPERFRAME;
	else if (err == -ENOIOCTLCMD)
		err = 0;

	cparm->timeperframe = interval.interval;
	cparm->readbuffers = chan->readbuffers;

	tegra_vi_channel_input_unlock(chan);

	return err;
}

static int tegra_vi_channel_get_mbus_code(struct tegra_vi_channel *chan,
					u32 pixel_format, u32 *code)
{
	const struct tegra_formats *fmt;

	if (chan->input->use_count > 1) {
		*code = chan->input->framefmt.code;
		return 0;
	}

	fmt = tegra_vi_channel_get_format(chan, pixel_format);
	if (!fmt)
		return -EINVAL;

	/* Assume we get the same enums with all mbus formats */
	*code = fmt->mbus[0];
	return 0;
}

static int tegra_vi_channel_enum_framesizes(struct file *file, void *priv,
					struct v4l2_frmsizeenum *fsize)
{
	struct video_device *vdev = video_devdata(file);
	struct tegra_vi_channel *chan =
		container_of(vdev, struct tegra_vi_channel, vdev);
	struct v4l2_frmsizeenum sd_fsize = *fsize;
	int err;

	err = tegra_vi_channel_input_lock(chan, false);
	if (err)
		return err;

	err = tegra_vi_channel_get_mbus_code(chan, fsize->pixel_format,
					&sd_fsize.pixel_format);
	if (err)
		goto unlock;

	err = v4l2_subdev_call(chan->input->sensor, video,
			enum_framesizes, &sd_fsize);
	if (err)
		goto unlock;

	sd_fsize.pixel_format = fsize->pixel_format;
	*fsize = sd_fsize;

unlock:
	tegra_vi_channel_input_unlock(chan);

	return err;
}

static int tegra_vi_channel_enum_frameintervals(struct file *file, void *priv,
						struct v4l2_frmivalenum *fival)
{
	struct video_device *vdev = video_devdata(file);
	struct tegra_vi_channel *chan =
		container_of(vdev, struct tegra_vi_channel, vdev);
	struct v4l2_frmivalenum sd_fival = *fival;
	int err;

	err = tegra_vi_channel_input_lock(chan, false);
	if (err)
		return err;

	err = tegra_vi_channel_get_mbus_code(chan, fival->pixel_format,
					&sd_fival.pixel_format);
	if (err)
		goto unlock;

	err = v4l2_subdev_call(chan->input->sensor, video,
			enum_frameintervals, &sd_fival);
	if (err)
		goto unlock;

	sd_fival.pixel_format = fival->pixel_format;
	*fival = sd_fival;

unlock:
	tegra_vi_channel_input_unlock(chan);

	return err;
}

#define tegra_vi_channel_subdev_op(file, ops, func, args...)		\
	({								\
		struct video_device *vdev = video_devdata(file);	\
		struct tegra_vi_channel *chan = container_of(		\
			vdev, struct tegra_vi_channel, vdev);		\
		int err;						\
									\
		err = tegra_vi_channel_input_lock(chan, false);		\
		if (err)						\
			return err;					\
									\
		err = v4l2_subdev_call(chan->input->sensor,		\
				ops, func, args);			\
									\
		tegra_vi_channel_input_unlock(chan);			\
									\
		err;							\
	})

static int tegra_vi_channel_g_dv_timings(struct file *file, void *priv,
				struct v4l2_dv_timings *timings)
{
	return tegra_vi_channel_subdev_op(file, video, g_dv_timings, timings);
}

static int tegra_vi_channel_s_dv_timings(struct file *file, void *priv,
				struct v4l2_dv_timings *timings)
{
	return tegra_vi_channel_subdev_op(file, video, s_dv_timings, timings);
}

static int tegra_vi_channel_enum_dv_timings(struct file *file, void *priv,
				struct v4l2_enum_dv_timings *timings)
{
	return tegra_vi_channel_subdev_op(file, video,
					enum_dv_timings, timings);
}

static int tegra_vi_channel_dv_timings_cap(struct file *file, void *priv,
				struct v4l2_dv_timings_cap *cap)
{
	return tegra_vi_channel_subdev_op(file, video, dv_timings_cap, cap);
}

static int tegra_vi_channel_g_std(struct file *file, void *priv,
				v4l2_std_id *norm)
{
	return tegra_vi_channel_subdev_op(file, core, g_std, norm);
}

static int tegra_vi_channel_s_std(struct file *file, void *priv,
				v4l2_std_id norm)
{
	return tegra_vi_channel_subdev_op(file, core, s_std, norm);
}

static int tegra_vi_channel_querystd(struct file *file, void *priv,
				v4l2_std_id *norm)
{
	return tegra_vi_channel_subdev_op(file, video, querystd, norm);
}

static int tegra_vi_channel_g_tuner(struct file *file, void *priv,
				struct v4l2_tuner *a)
{
	return tegra_vi_channel_subdev_op(file, tuner, g_tuner, a);
}

static int tegra_vi_channel_s_tuner(struct file *file, void *priv,
				const struct v4l2_tuner *a)
{
	return tegra_vi_channel_subdev_op(file, tuner, s_tuner, a);
}

static int tegra_vi_channel_g_frequency(struct file *file, void *priv,
				struct v4l2_frequency *freq)
{
	return tegra_vi_channel_subdev_op(file, tuner, g_frequency, freq);
}

static int tegra_vi_channel_s_frequency(struct file *file, void *priv,
				const struct v4l2_frequency *freq)
{
	return tegra_vi_channel_subdev_op(file, tuner, s_frequency, freq);
}

static int tegra_vi_channel_g_chip_ident(struct file *file, void *fh,
					struct v4l2_dbg_chip_ident *chip)
{
	return tegra_vi_channel_subdev_op(file, core, g_chip_ident, chip);
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int tegra_vi_channel_g_register(struct file *file, void *fh,
				struct v4l2_dbg_register *reg)
{
	return tegra_vi_channel_subdev_op(file, core, g_register, reg);
}

static int tegra_vi_channel_s_register(struct file *file, void *fh,
				const struct v4l2_dbg_register *reg);
{
	return tegra_vi_channel_subdev_op(file, core, s_register, reg);
}
#endif

static int tegra_vi_channel_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct tegra_vi2 *vi2 =
		container_of(vdev->v4l2_dev, struct tegra_vi2, v4l2_dev);
	struct tegra_vi_channel *chan =
		container_of(vdev, struct tegra_vi_channel, vdev);
	int err = 0;

	if (!vi2->sensors_complete) {
		dev_err(vi2->v4l2_dev.dev, "Some sensors are missing\n");
		return -ENODEV;
	}

	mutex_lock(&chan->lock);

	if (chan->use_count == 0)
		err = tegra_vi_channel_set_input(chan, chan->input_id);

	if (!err)
		err = v4l2_fh_open(file);

	if (!err)
		chan->use_count++;

	mutex_unlock(&chan->lock);

	return err;
}

static int tegra_vi_channel_release(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct tegra_vi_channel *chan =
		container_of(vdev, struct tegra_vi_channel, vdev);
	int err;

	/* vb2_fop_release() must be called without lock as it will
	 * stop the vidq if it is running. This would deadlock as
	 * the capture thread need the lock to properly finish. */
	err = vb2_fop_release(file);
	if (err)
		return err;

	mutex_lock(&chan->lock);

	if (chan->use_count == 1) {
		err = tegra_vi_channel_set_input(chan, INPUT_NONE);
		if (!err)
			chan->use_count--;
	}

	mutex_unlock(&chan->lock);

	return err;
}

static const struct v4l2_ioctl_ops tegra_vi_channel_ioctl_ops = {
	.vidioc_querycap		= tegra_vi_channel_querycap,
	.vidioc_enum_input		= tegra_vi_channel_enum_input,
	.vidioc_g_input			= tegra_vi_channel_g_input,
	.vidioc_s_input			= tegra_vi_channel_s_input,
	.vidioc_enum_fmt_vid_cap	= tegra_vi_channel_enum_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= tegra_vi_channel_try_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= tegra_vi_channel_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= tegra_vi_channel_s_fmt_vid_cap,
	.vidioc_cropcap			= tegra_vi_channel_cropcap,
	.vidioc_s_parm			= tegra_vi_channel_s_parm,
	.vidioc_g_parm			= tegra_vi_channel_g_parm,
	.vidioc_enum_framesizes		= tegra_vi_channel_enum_framesizes,
	.vidioc_enum_frameintervals	= tegra_vi_channel_enum_frameintervals,
	.vidioc_g_dv_timings		= tegra_vi_channel_g_dv_timings,
	.vidioc_s_dv_timings		= tegra_vi_channel_s_dv_timings,
	.vidioc_enum_dv_timings		= tegra_vi_channel_enum_dv_timings,
	.vidioc_dv_timings_cap		= tegra_vi_channel_dv_timings_cap,
	.vidioc_g_std			= tegra_vi_channel_g_std,
	.vidioc_s_std			= tegra_vi_channel_s_std,
	.vidioc_querystd		= tegra_vi_channel_querystd,
	.vidioc_g_tuner			= tegra_vi_channel_g_tuner,
	.vidioc_s_tuner			= tegra_vi_channel_s_tuner,
	.vidioc_g_frequency		= tegra_vi_channel_g_frequency,
	.vidioc_s_frequency		= tegra_vi_channel_s_frequency,
	.vidioc_g_chip_ident		= tegra_vi_channel_g_chip_ident,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register		= tegra_vi_channel_g_register,
	.vidioc_s_register		= tegra_vi_channel_s_register,
#endif
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
	.vidioc_log_status		= v4l2_ctrl_log_status,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

static const struct v4l2_file_operations tegra_vi_channel_fops = {
	.owner		= THIS_MODULE,
	.open		= tegra_vi_channel_open,
	.release	= tegra_vi_channel_release,
	.read           = vb2_fop_read,
	.poll		= vb2_fop_poll,
	.mmap           = vb2_fop_mmap,
	.unlocked_ioctl	= video_ioctl2,
};

static void tegra_vi_channel_event(struct tegra_vi_channel *chan,
				struct v4l2_event *ev)
{
	switch (ev->type) {
	/* Handle source change like an EOS for now */
	case V4L2_EVENT_SOURCE_CHANGE:
	case V4L2_EVENT_EOS:
		chan->should_stop = true;
		break;
	}
}

static void tegra_vi_notify(struct v4l2_subdev *sd,
			unsigned int notification, void *arg)
{
	struct tegra_vi2 *vi2 =
		container_of(sd->v4l2_dev, struct tegra_vi2, v4l2_dev);
	int i;

	/* We are only interrested in event notifications */
	if (notification != V4L2_DEVICE_NOTIFY_EVENT)
		return;

	for (i = 0; i < ARRAY_SIZE(vi2->channel); i++) {
		struct tegra_vi_channel *chan = &vi2->channel[i];

		mutex_lock(&chan->lock);
		if (chan->input) {
			mutex_lock(&chan->input->lock);
			if (chan->input->sensor == sd)
				tegra_vi_channel_event(chan, arg);
			mutex_unlock(&chan->input->lock);
		}
		mutex_unlock(&chan->lock);
	}
}

static int tegra_vi_sensor_bound(struct v4l2_async_notifier *notifier,
				struct v4l2_subdev *subdev,
				struct v4l2_async_subdev *asd)
{
	struct tegra_vi2 *vi2 =
		container_of(notifier, struct tegra_vi2, sd_notifier);
	struct tegra_vi_input *input =
		container_of(asd, struct tegra_vi_input, asd);
	struct device *dev = vi2->v4l2_dev.dev;
	int err = 0;

	mutex_lock(&input->lock);

	if (input->sensor) {
		err = -EBUSY;
	} else {
		input->sensor = subdev;

		/* Check and store the CIS config */
		err = tegra_vi_input_get_csi_params(
			input, &input->csi_lanes, &input->csi_channel,
			&input->csi_clk_continuous);
		if (err) {
			input->sensor = NULL;
			dev_err(dev, "Sensor on input %c has incompatible "
				"CSI config\n", 'A' + input->id);
		}
	}

	mutex_unlock(&input->lock);

	return err;
}

static int tegra_vi_sensors_complete(struct v4l2_async_notifier *notifier)
{
	struct tegra_vi2 *vi2 =
		container_of(notifier, struct tegra_vi2, sd_notifier);
	unsigned int inputs = 0;
	int err, c, i;

	/* Create a bitmap of the sensors */
	for (i = 0; i < ARRAY_SIZE(vi2->input); i++) {
		if (vi2->input[i].sensor)
			inputs |= BIT(i);
	}

	if (inputs == 0)
		return -ENODEV;

	/* Assign them to the inputs, if none left use the previous one */
	for (c = 0; c < ARRAY_SIZE(vi2->channel); c++) {
		if (inputs) {
			i = __ffs(inputs);
			vi2->channel[c].input_id = i;
			inputs &= ~BIT(i);
		} else {
			vi2->channel[c].input_id =
				vi2->channel[c - 1].input_id;
		}
	}

	err = v4l2_device_register_subdev_nodes(&vi2->v4l2_dev);
	if (err) {
		dev_err(vi2->v4l2_dev.dev,
			"Failed to register subdevices nodes\n");
		return err;
	}

	vi2->sensors_complete = true;
	return 0;
}

static void tegra_vi_sensor_unbind(struct v4l2_async_notifier *notifier,
				struct v4l2_subdev *subdev,
				struct v4l2_async_subdev *asd)
{
	struct tegra_vi_input *input =
		container_of(asd, struct tegra_vi_input, asd);

	mutex_lock(&input->lock);

	if (input->sensor == subdev)
		input->sensor = NULL;

	mutex_unlock(&input->lock);
}

static int tegra_vi_input_init(struct platform_device *pdev,
			enum tegra_vi_input_id id)
{
	struct tegra_vi2 *vi2 = platform_get_drvdata(pdev);
	struct tegra_vi_input *input = &vi2->input[id];

	input->id = id;
	mutex_init(&input->lock);

	switch (id) {
	case INPUT_CSI_A:
		input->cil_regs[0] = vi2->base + 0x92C;
		input->cil_regs[1] = vi2->base + 0x960;
		input->mbus_caps = V4L2_MBUS_CSI2_LANES | \
			V4L2_MBUS_CSI2_CHANNELS | \
			V4L2_MBUS_CSI2_CLOCK;
		input->phy_shift[0] = 0;
		input->phy_shift[1] = 8;
		input->cil_clk = devm_clk_get(&pdev->dev, "cilab");
		break;
	case INPUT_CSI_B:
		input->cil_regs[0] = vi2->base + 0x994;
		input->cil_regs[1] = vi2->base + 0x9C8;
		input->mbus_caps = V4L2_MBUS_CSI2_LANES | \
			V4L2_MBUS_CSI2_CHANNELS | \
			V4L2_MBUS_CSI2_CLOCK;
		input->phy_shift[0] = 16;
		input->phy_shift[1] = 24;
		input->cil_clk = devm_clk_get(&pdev->dev, "cilcd");
		break;
	case INPUT_CSI_C:
		input->cil_regs[0] = vi2->base + 0xA08;
		input->mbus_caps = V4L2_MBUS_CSI2_1_LANE | \
			V4L2_MBUS_CSI2_CHANNELS | \
			V4L2_MBUS_CSI2_CLOCK;
		input->phy_shift[0] = 28;
		input->cil_clk = devm_clk_get(&pdev->dev, "cile");
		break;
	default:
		return -EINVAL;
	}

	if (IS_ERR(input->cil_clk))
		return PTR_ERR(input->cil_clk);

	return 0;
}

static void tegra_vi_input_reset(const struct tegra_vi_input *input, bool reset)
{
	if (input->cil_regs[0])
		vi_writel(reset ? 1 : 0, &input->cil_regs[0]->sensor_reset);
	if (input->cil_regs[1])
		vi_writel(reset ? 1 : 0, &input->cil_regs[1]->sensor_reset);
}

static int tegra_vi_channel_init(struct platform_device *pdev, unsigned id)
{
	struct tegra_vi2 *vi2 = platform_get_drvdata(pdev);
	struct tegra_vi_channel *chan = &vi2->channel[id];
	struct vb2_queue *q;
	int err;

	switch (id) {
	case 0:
		strcpy(chan->vdev.name, "VI A");
		chan->vi_regs = vi2->base + 0x100;
		chan->mipi_regs = vi2->base + 0x838;
		chan->tpg.sensor = tegra_tpg_init(pdev, vi2->base + 0xA68);
		chan->sensor_clk = devm_clk_get(&pdev->dev, "vi_sensor");
		break;
	case 1:
		strcpy(chan->vdev.name, "VI B");
		chan->vi_regs = vi2->base + 0x200;
		chan->mipi_regs = vi2->base + 0x86C;
		chan->tpg.sensor = tegra_tpg_init(pdev, vi2->base + 0xA9C);
		chan->sensor_clk = devm_clk_get(&pdev->dev, "vi_sensor2");
		break;
	default:
		return -EINVAL;
	}

	if (IS_ERR(chan->tpg.sensor)) {
		dev_err(&pdev->dev, "Failed to create TPG subdev\n");
		return PTR_ERR(chan->tpg.sensor);
	}

	if (IS_ERR(chan->sensor_clk)) {
		dev_err(&pdev->dev, "Failed to get sensor clock\n");
		return PTR_ERR(chan->sensor_clk);
	}

	err = clk_prepare_enable(chan->sensor_clk);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable sensor clock\n");
		return err;
	}

	/* Setup the TPG input for this channel */
	chan->tpg.id = INPUT_PATTERN_GENERATOR;
	mutex_init(&chan->tpg.lock);
	sprintf(chan->tpg.sensor->name, "TPG %d", id);
	chan->tpg.mbus_caps = V4L2_MBUS_CSI2_1_LANE | V4L2_MBUS_CSI2_2_LANE |
			V4L2_MBUS_CSI2_4_LANE | V4L2_MBUS_CSI2_CHANNELS | \
			V4L2_MBUS_CSI2_CLOCK;

	err = v4l2_device_register_subdev(&vi2->v4l2_dev, chan->tpg.sensor);
	if (err) {
		dev_err(&pdev->dev, "Failed to register channel TPG subdev\n");
		goto disable_clk;
	}

	chan->vb2_alloc_ctx = vb2_dma_contig_init_ctx(&pdev->dev);
	if (IS_ERR(chan->vb2_alloc_ctx)) {
		dev_err(&pdev->dev, "Failed to create VB2 DMA context\n");
		goto unregister_subdev;
	}

	q = &chan->vb;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	q->drv_priv = chan;
	q->buf_struct_size = sizeof(struct tegra_vi_buffer);
	q->ops = &tegra_vi_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->timestamp_type = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;

	err = vb2_queue_init(q);
	if (err) {
		dev_err(&pdev->dev, "Failed to init VB2 queue\n");
		goto cleanup_ctx;
	}

	/* Finish setting up the channel */
	chan->id = id;
	chan->input_id = INPUT_NONE;
	chan->readbuffers = 4;
	chan->vdev.fops = &tegra_vi_channel_fops;
	chan->vdev.ioctl_ops = &tegra_vi_channel_ioctl_ops;
	chan->vdev.v4l2_dev = &vi2->v4l2_dev;
	chan->vdev.queue = q;
	chan->vdev.release = video_device_release_empty;
	INIT_LIST_HEAD(&chan->capture);
	mutex_init(&chan->lock);
	spin_lock_init(&chan->vq_lock);

	chan->syncpt_id = nvhost_get_syncpt_client_managed(chan->vdev.name);

	video_set_drvdata(&chan->vdev, chan);

	err = video_register_device(&chan->vdev, VFL_TYPE_GRABBER, -1);
	if (err) {
		dev_err(&pdev->dev, "Failed to register video device\n");
		goto free_syncpt;
	}

	return 0;

free_syncpt:
	nvhost_free_syncpt(chan->syncpt_id);
	vb2_queue_release(q);
cleanup_ctx:
	vb2_dma_contig_cleanup_ctx(chan->vb2_alloc_ctx);
unregister_subdev:
	v4l2_device_unregister_subdev(chan->tpg.sensor);
disable_clk:
	clk_disable_unprepare(chan->sensor_clk);
	return err;
}

static void tegra_vi_channel_uninit(struct tegra_vi_channel *chan)
{
	nvhost_free_syncpt(chan->syncpt_id);
	vb2_queue_release(&chan->vb);
	vb2_dma_contig_cleanup_ctx(chan->vb2_alloc_ctx);
	video_unregister_device(&chan->vdev);
	v4l2_device_unregister_subdev(chan->tpg.sensor);
	clk_disable_unprepare(chan->sensor_clk);
}

static void tegra_vi_channel_reset(const struct tegra_vi_channel *chan, bool reset)
{
	if (reset)
		vi_writel(0, &chan->vi_regs->image_dt);
	vi_writel(reset ? 1 : 0, &chan->mipi_regs->sensor_reset);
	vi_writel(reset ? 0x1F : 0, &chan->vi_regs->sw_reset);
}

static int tegra_vi2_probe(struct platform_device *pdev)
{
	static struct resource cal_regs = {
		.flags = IORESOURCE_MEM,
		.start = 0x700E3000,
		.end = 0x700E3000 + 0x00000100 - 1,
	};
	struct device_node *np = NULL;
	struct tegra_vi2 *vi2;
	struct resource *regs;
	int input, chan, err;

	vi2 = devm_kzalloc(&pdev->dev, sizeof(*vi2), GFP_KERNEL);
	if (!vi2)
		return -ENOMEM;
	platform_set_drvdata(pdev, vi2);

	/* Read the config from OF */
	while ((np = v4l2_of_get_next_endpoint(pdev->dev.of_node, np))) {
		struct v4l2_async_subdev *asd;
		struct device_node *port;
		struct device_node *ep;
		struct device_node *sd;
		u32 reg;

		port = of_get_parent(np);
		err = of_property_read_u32(port, "reg", &reg);
		of_node_put(port);

		if (err || reg > ARRAY_SIZE(vi2->input)) {
			dev_err(&pdev->dev,
				"Port is missing/invalid 'reg' property\n");
			of_node_put(np);
			return -EINVAL;
		}
		asd = &vi2->input[reg].asd;

		if (asd->match.of.node) {
			dev_err(&pdev->dev,
				"Port must have only one endpoint\n");
			of_node_put(np);
			return -EINVAL;
		}

		ep = of_parse_phandle(np, "remote-endpoint", 0);
		if (!ep || !of_device_is_available(ep)) {
			of_node_put(np);
			continue;
		}

		sd = v4l2_of_get_remote_port_parent(np);
		of_node_put(np);

		if (!sd || !of_device_is_available(sd))
			continue;

		asd->match_type = V4L2_ASYNC_MATCH_OF;
		asd->match.of.node = sd;

		vi2->asd[vi2->sd_notifier.num_subdevs] = asd;
		vi2->sd_notifier.num_subdevs++;

		of_node_put(sd);
	}

	if (!vi2->sd_notifier.num_subdevs) {
		dev_err(&pdev->dev, "No sensor defined in DT\n");
		return -ENODEV;
	}

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(&pdev->dev, "Missing the mem resource\n");
		return -ENXIO;
	}

	vi2->base = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(vi2->base)) {
		dev_err(&pdev->dev, "Failed to remap registers\n");
		return PTR_ERR(vi2->base);
	}

	vi2->vi_regs = vi2->base;
	vi2->phy_regs = vi2->base + 0x908;
	vi2->misc_regs = vi2->base + 0xAD0;
	vi2->cal_regs = devm_ioremap_nocache(&pdev->dev, cal_regs.start,
					cal_regs.end + 1 - cal_regs.start);
	if (IS_ERR(vi2->cal_regs)) {
		dev_err(&pdev->dev, "Failed to remap calibration registers\n");
		return PTR_ERR(vi2->cal_regs);
	}

	vi2->vi_clk = devm_clk_get(&pdev->dev, "vi");
	if (IS_ERR(vi2->vi_clk)) {
		dev_err(&pdev->dev, "Failed to get VI clock\n");
		return PTR_ERR(vi2->vi_clk);
	}
	tegra_clk_cfg_ex(vi2->vi_clk, TEGRA_CLK_VI_INP_SEL, 2);

	vi2->csi_clk = devm_clk_get(&pdev->dev, "csi");
	if (IS_ERR(vi2->csi_clk)) {
		dev_err(&pdev->dev, "Failed to get CSI clock\n");
		return PTR_ERR(vi2->csi_clk);
	}

	vi2->csus_clk = devm_clk_get(&pdev->dev, "csus");
	if (IS_ERR(vi2->csus_clk)) {
		dev_err(&pdev->dev, "Failed to get CSUS clock\n");
		return PTR_ERR(vi2->csus_clk);
	}

	vi2->isp_clk = devm_clk_get(&pdev->dev, "isp");
	if (IS_ERR(vi2->isp_clk)) {
		dev_err(&pdev->dev, "Failed to get ISP clock\n");
		return PTR_ERR(vi2->isp_clk);
	}

	vi2->csi_reg = devm_regulator_get(&pdev->dev, "avdd_dsi_csi");
	if (IS_ERR(vi2->csi_reg)) {
		dev_err(&pdev->dev, "Failed to get CSI regulator\n");
		return PTR_ERR(vi2->csi_reg);
	}

	/* The default DMA segment size is 64K, however we need more
	 * as video buffer are much larger. If we have an IOMMU it
	 * shouldn't be a problem to support such large segments, so
	 * apply the DMA parameters if none have been set yet.
	 */
	if (device_is_iommuable(&pdev->dev) && !pdev->dev.dma_parms)
		pdev->dev.dma_parms = &dma_parameters;

	mutex_init(&vi2->lock);

	err = regulator_enable(vi2->csi_reg);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable CSI regulator\n");
		return err;
	}

	err = nvhost_module_enable_clk(pdev->dev.parent);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable host1x clocks\n");
		goto regulator_disable;
	}

	err = clk_prepare_enable(vi2->vi_clk);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable VI clock\n");
		goto host1x_disable;
	}

	err = clk_prepare_enable(vi2->csi_clk);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable CSI clock\n");
		goto disable_vi_clk;
	}

	err = clk_prepare_enable(vi2->isp_clk);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable ISP clock\n");
		goto disable_csi_clk;
	}

	err = clk_prepare_enable(vi2->csus_clk);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable CSUS clock\n");
		goto disable_isp_clk;
	}

	/* VI */
	tegra_unpowergate_partition(TEGRA_POWERGATE_VENC);
	/* Depends on DIS? See comment in t124.c on t124_vi_info */
	tegra_unpowergate_partition(TEGRA_POWERGATE_DISA);
	tegra_unpowergate_partition(TEGRA_POWERGATE_DISB);

	vi2->v4l2_dev.notify = tegra_vi_notify;
	err = v4l2_device_register(&pdev->dev, &vi2->v4l2_dev);
	if (err) {
		dev_err(&pdev->dev, "Failed to register V4L2 device\n");
		goto powergate_partition;
	}

	for (input = 0; input < ARRAY_SIZE(vi2->input); input++) {
		err = tegra_vi_input_init(pdev, input);
		if (err) {
			dev_err(&pdev->dev, "Failed to init input %d\n", input);
			goto v4l2_unregister;
		}
		tegra_vi_input_reset(&vi2->input[input], true);
	}

	for (chan = 0; chan < ARRAY_SIZE(vi2->channel); chan++) {
		err = tegra_vi_channel_init(pdev, chan);
		if (err) {
			dev_err(&pdev->dev, "Failed to init channel %d\n", chan);
			goto uninit_channels;
		}
		tegra_vi_channel_reset(&vi2->channel[chan], true);
	}

	/* Remove the resets */
	for (input = 0; input < ARRAY_SIZE(vi2->input); input++)
		tegra_vi_input_reset(&vi2->input[input], false);
	for (chan = 0; chan < ARRAY_SIZE(vi2->channel); chan++)
		tegra_vi_channel_reset(&vi2->channel[chan], false);

	/* Init the async notifier once everything is setup */
	vi2->sd_notifier.subdevs = vi2->asd;
	vi2->sd_notifier.bound = tegra_vi_sensor_bound;
	vi2->sd_notifier.complete = tegra_vi_sensors_complete;
	vi2->sd_notifier.unbind = tegra_vi_sensor_unbind;
	err = v4l2_async_notifier_register(&vi2->v4l2_dev, &vi2->sd_notifier);
	if (err) {
		dev_err(&pdev->dev, "Failed to register async notifier\n");
		goto uninit_channels;
	}

	return 0;

uninit_channels:
	for (chan--; chan >= 0; chan--)
		tegra_vi_channel_uninit(&vi2->channel[chan]);
v4l2_unregister:
	v4l2_device_unregister(&vi2->v4l2_dev);
powergate_partition:
	tegra_powergate_partition(TEGRA_POWERGATE_DISB);
	tegra_powergate_partition(TEGRA_POWERGATE_DISA);
	tegra_powergate_partition(TEGRA_POWERGATE_VENC);
	clk_disable_unprepare(vi2->csus_clk);
disable_isp_clk:
	clk_disable_unprepare(vi2->isp_clk);
disable_csi_clk:
	clk_disable_unprepare(vi2->csi_clk);
disable_vi_clk:
	clk_disable_unprepare(vi2->vi_clk);
host1x_disable:
	nvhost_module_disable_clk(pdev->dev.parent);
regulator_disable:
	WARN_ON(regulator_disable(vi2->csi_reg));
	return err;
}

static const struct of_device_id tegra_vi2_of_match[] = {
	{ .compatible = "nvidia,tegra124-vi" },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_vi2_of_match);

static struct platform_driver tegra_vi2_driver = {
	.probe = tegra_vi2_probe,
	.driver	 = {
		.of_match_table = of_match_ptr(tegra_vi2_of_match),
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	},
};
module_platform_driver(tegra_vi2_driver);

MODULE_AUTHOR("Alban Bedel <alban.bedel@avionic-design.de>");
MODULE_DESCRIPTION("Tegra VI2 Video for Linux driver");
MODULE_LICENSE("GPL");
