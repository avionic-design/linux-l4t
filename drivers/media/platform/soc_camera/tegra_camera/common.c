/*
 * Copyright (c) 2013-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/nvhost.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/tegra_pm_domains.h>

#include <mach/powergate.h>

#include <media/soc_camera.h>
#include <media/soc_mediabus.h>
#include <media/videobuf2-dma-contig.h>
#include <media/tegra_v4l2_camera.h>

#include "dev.h"
#include "bus_client.h"
#include "nvhost_acm.h"
#include "t124/t124.h"

#include "common.h"

static int tpg_mode;
module_param(tpg_mode, int, 0644);

#define TEGRA_CAM_DRV_NAME "vi"
#define TEGRA_CAM_VERSION_CODE KERNEL_VERSION(0, 0, 5)

static const struct soc_mbus_pixelfmt tegra_camera_yuv_formats[] = {
	{
		.fourcc			= V4L2_PIX_FMT_UYVY,
		.name			= "YUV422 (UYVY) packed",
		.bits_per_sample	= 16,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
	},
	{
		.fourcc			= V4L2_PIX_FMT_VYUY,
		.name			= "YUV422 (VYUY) packed",
		.bits_per_sample	= 16,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
	},
	{
		.fourcc			= V4L2_PIX_FMT_YUYV,
		.name			= "YUV422 (YUYV) packed",
		.bits_per_sample	= 16,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
	},
	{
		.fourcc			= V4L2_PIX_FMT_YVYU,
		.name			= "YUV422 (YVYU) packed",
		.bits_per_sample	= 16,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
	},
	{
		.fourcc			= V4L2_PIX_FMT_YUV420,
		.name			= "YUV420 (YU12) planar",
		.bits_per_sample	= 12,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
	},
	{
		.fourcc			= V4L2_PIX_FMT_YVU420,
		.name			= "YVU420 (YV12) planar",
		.bits_per_sample	= 12,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
	},
};

static const struct soc_mbus_pixelfmt tegra_camera_bayer_formats[] = {
	{
		.fourcc			= V4L2_PIX_FMT_SBGGR8,
		.name			= "Bayer 8 BGBG.. GRGR..",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
	},
	{
		.fourcc			= V4L2_PIX_FMT_SRGGB8,
		.name			= "Bayer 8 RGRG.. GBGB..",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
	},
	{
		.fourcc			= V4L2_PIX_FMT_SBGGR10,
		.name			= "Bayer 10 BGBG.. GRGR..",
		.bits_per_sample	= 16,
		.packing		= SOC_MBUS_PACKING_EXTEND16,
		.order			= SOC_MBUS_ORDER_LE,
	},
	{
		.fourcc			= V4L2_PIX_FMT_SRGGB10,
		.name			= "Bayer 10 RGRG.. GBGB..",
		.bits_per_sample	= 16,
		.packing		= SOC_MBUS_PACKING_EXTEND16,
		.order			= SOC_MBUS_ORDER_LE,
	},
};

static const struct soc_mbus_pixelfmt tegra_camera_rgb_formats[] = {
	{
		.fourcc			= V4L2_PIX_FMT_RGB32,
		.name			= "RGBA 8-8-8-8",
		.bits_per_sample	= 32,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
	},
};

static int tegra_camera_activate(struct tegra_camera_dev *cam,
				 struct soc_camera_device *icd)
{
	struct tegra_camera_ops *cam_ops = cam->ops;
	struct soc_camera_subdev_desc *ssdesc = &icd->sdesc->subdev_desc;
	struct tegra_camera_platform_data *pdata = ssdesc->drv_priv;
	int port = pdata->port;
	int ret;

	ret = nvhost_module_busy_ext(cam->ndev);
	if (ret) {
		dev_err(&cam->ndev->dev, "nvhost module is busy\n");
		return ret;
	}

	/* Enable external power */
	if (cam->reg) {
		ret = regulator_enable(cam->reg);
		if (ret)
			dev_err(&cam->ndev->dev, "enabling regulator failed\n");
	}

	if (cam_ops->activate)
		cam_ops->activate(cam);

	/* Unpowergate VE */
	tegra_unpowergate_partition(TEGRA_POWERGATE_VENC);

	/* Init Clocks */
	if (cam_ops->clks_init)
		cam_ops->clks_init(cam, port);

	if (cam_ops->clks_enable)
		cam_ops->clks_enable(cam);

	if (cam_ops->capture_clean)
		cam_ops->capture_clean(cam);

	cam->sof = 1;

	return 0;
}

static void tegra_camera_deactivate(struct tegra_camera_dev *cam)
{
	struct tegra_camera_ops *cam_ops = cam->ops;

	if (cam_ops->clks_disable)
		cam_ops->clks_disable(cam);

	if (cam->ops->clks_deinit)
		cam->ops->clks_deinit(cam);

	if (cam_ops->deactivate)
		cam_ops->deactivate(cam);

	/* Powergate VE */
	tegra_powergate_partition(TEGRA_POWERGATE_VENC);

	/* Disable external power */
	if (cam->reg)
		regulator_disable(cam->reg);

	nvhost_module_idle_ext(cam->ndev);

	cam->sof = 0;
	cam->cal_done = 0;
}

static int tegra_camera_capture_frame(struct tegra_camera_dev *cam,
				      struct tegra_camera_buffer *buf)
{
	int err;

	/* Setup capture registers */
	cam->ops->capture_setup(cam, buf);

	cam->ops->incr_syncpts(cam);

	/* MIPI CSI pads calibration after starting capture */
	if (cam->ops->mipi_calibration && !cam->cal_done) {
		err = cam->ops->mipi_calibration(cam, buf);
		if (!err)
			cam->cal_done = 1;
	}

	/* Issue start capture */
	cam->ops->capture_start(cam, buf);

	/* Move buffer to capture done queue */
	spin_lock(&cam->done_lock);
	list_add_tail(&buf->queue, &cam->done);
	spin_unlock(&cam->done_lock);

	/* Wait up kthread for capture done */
	wake_up_interruptible(&cam->capture_done_wait);

	/* Wait for next frame start */
	return cam->ops->capture_wait(cam, buf);
}

static int tegra_camera_kthread_capture_start(void *data)
{
	struct tegra_camera_dev *cam = data;
	struct tegra_camera_buffer *buf;

	while (1) {
		try_to_freeze();

		wait_event_interruptible(cam->capture_start_wait,
					 !list_empty(&cam->capture) ||
					 kthread_should_stop());
		if (kthread_should_stop())
			break;

		spin_lock(&cam->capture_lock);
		if (list_empty(&cam->capture)) {
			spin_unlock(&cam->capture_lock);
			continue;
		}

		buf = list_entry(cam->capture.next, struct tegra_camera_buffer,
				 queue);
		list_del_init(&buf->queue);
		spin_unlock(&cam->capture_lock);

		tegra_camera_capture_frame(cam, buf);
	}

	return 0;
}

static int tegra_camera_capture_done(struct tegra_camera_dev *cam,
				     struct tegra_camera_buffer *buf)
{
	struct vb2_buffer *vb = &buf->vb;
	struct soc_camera_device *icd = buf->icd;
	struct soc_camera_subdev_desc *ssdesc = &icd->sdesc->subdev_desc;
	struct tegra_camera_platform_data *pdata = ssdesc->drv_priv;
	int port = pdata->port;
	int err;

	/* Wait for buffer is output to memeory  */
	err = cam->ops->capture_done(cam, port);

	/* Buffer is done */
	do_gettimeofday(&vb->v4l2_buf.timestamp);
	vb->v4l2_buf.field = cam->field;
	if (port == TEGRA_CAMERA_PORT_CSI_A)
		vb->v4l2_buf.sequence = cam->sequence_a++;
	else if (port == TEGRA_CAMERA_PORT_CSI_B)
		vb->v4l2_buf.sequence = cam->sequence_b++;
	vb2_buffer_done(vb, err < 0 ? VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
	cam->num_frames++;

	return err;
}

static int tegra_camera_kthread_capture_done(void *data)
{
	struct tegra_camera_dev *cam = data;
	struct tegra_camera_buffer *buf;

	while (1) {
		try_to_freeze();

		wait_event_interruptible(cam->capture_done_wait,
					 !list_empty(&cam->done) ||
					 kthread_should_stop());
		if (kthread_should_stop() && list_empty(&cam->done))
			break;

		spin_lock(&cam->done_lock);
		if (list_empty(&cam->done)) {
			spin_unlock(&cam->done_lock);
			continue;
		}

		buf = list_entry(cam->done.next, struct tegra_camera_buffer,
				 queue);
		list_del_init(&buf->queue);
		spin_unlock(&cam->done_lock);

		tegra_camera_capture_done(cam, buf);
	}

	return 0;
}

static int tegra_camera_init_buffer(struct tegra_camera_buffer *buf)
{
	struct soc_camera_device *icd = buf->icd;
	int bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
						icd->current_fmt->host_fmt);
	struct soc_camera_subdev_desc *ssdesc = &icd->sdesc->subdev_desc;
	struct tegra_camera_platform_data *pdata = ssdesc->drv_priv;

	switch (icd->current_fmt->host_fmt->fourcc) {
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SRGGB8:
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SRGGB10:
	case V4L2_PIX_FMT_RGB32:
		buf->buffer_addr = vb2_dma_contig_plane_dma_addr(&buf->vb, 0);
		buf->start_addr = buf->buffer_addr;

		if (pdata->flip_v)
			buf->start_addr += bytes_per_line *
					   (icd->user_height-1);

		if (pdata->flip_h)
			buf->start_addr += bytes_per_line - 1;

		break;

	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		buf->buffer_addr = vb2_dma_contig_plane_dma_addr(&buf->vb, 0);
		buf->buffer_addr_u = buf->buffer_addr +
				     icd->user_width * icd->user_height;
		buf->buffer_addr_v = buf->buffer_addr_u +
				     (icd->user_width * icd->user_height) / 4;

		/* For YVU420, we swap the locations of the U and V planes. */
		if (icd->current_fmt->host_fmt->fourcc == V4L2_PIX_FMT_YVU420) {
			dma_addr_t temp = buf->buffer_addr_u;
			buf->buffer_addr_u = buf->buffer_addr_v;
			buf->buffer_addr_v = temp;
		}

		buf->start_addr = buf->buffer_addr;
		buf->start_addr_u = buf->buffer_addr_u;
		buf->start_addr_v = buf->buffer_addr_v;

		if (pdata->flip_v) {
			buf->start_addr += icd->user_width *
					   (icd->user_height - 1);

			buf->start_addr_u += ((icd->user_width/2) *
					      ((icd->user_height/2) - 1));

			buf->start_addr_v += ((icd->user_width/2) *
					      ((icd->user_height/2) - 1));
		}

		if (pdata->flip_h) {
			buf->start_addr += icd->user_width - 1;

			buf->start_addr_u += (icd->user_width/2) - 1;

			buf->start_addr_v += (icd->user_width/2) - 1;
		}

		break;

	default:
		dev_err(icd->parent, "Wrong host format %d\n",
			icd->current_fmt->host_fmt->fourcc);
		return -EINVAL;
	}

	return 0;
}

/*
 *  Videobuf operations
 */
static int tegra_camera_videobuf_setup(struct vb2_queue *vq,
				       const struct v4l2_format *fmt,
				       unsigned int *num_buffers,
				       unsigned int *num_planes,
				       unsigned int sizes[],
				       void *alloc_ctxs[])
{
	struct soc_camera_device *icd = container_of(vq,
						     struct soc_camera_device,
						     vb2_vidq);
	struct soc_camera_subdev_desc *ssdesc = &icd->sdesc->subdev_desc;
	struct tegra_camera_platform_data *pdata = ssdesc->drv_priv;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct tegra_camera_dev *cam = ici->priv;
	int bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
						icd->current_fmt->host_fmt);
	if (bytes_per_line < 0)
		return bytes_per_line;

	*num_planes = 1;

	if (pdata->port == TEGRA_CAMERA_PORT_CSI_A)
		cam->sequence_a = 0;
	else if (pdata->port == TEGRA_CAMERA_PORT_CSI_B)
		cam->sequence_b = 0;
	sizes[0] = bytes_per_line * icd->user_height;
	alloc_ctxs[0] = cam->alloc_ctx;

	if (!*num_buffers)
		*num_buffers = 2;

	return 0;
}

static int tegra_camera_videobuf_prepare(struct vb2_buffer *vb)
{
	struct soc_camera_device *icd = container_of(vb->vb2_queue,
						     struct soc_camera_device,
						     vb2_vidq);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct tegra_camera_dev *cam = ici->priv;
	struct tegra_camera_buffer *buf = to_tegra_vb(vb);
	struct soc_camera_subdev_desc *ssdesc = &icd->sdesc->subdev_desc;
	struct tegra_camera_platform_data *pdata = ssdesc->drv_priv;
	int bytes_per_line = soc_mbus_bytes_per_line(icd->user_width,
						icd->current_fmt->host_fmt);
	unsigned long size;

	if (bytes_per_line < 0)
		return bytes_per_line;

	buf->icd = icd;

	if (!pdata) {
		dev_err(icd->parent, "No platform data for this device!\n");
		return -EINVAL;
	}

	if (!cam->ops->port_is_valid(pdata->port)) {
		dev_err(icd->parent,
			"Invalid camera port %d in platform data\n",
			pdata->port);
		return -EINVAL;
	}

#ifdef PREFILL_BUFFER
	dev_info(icd->parent, "%s (vb=0x%p) 0x%p %lu\n", __func__,
		vb, vb2_plane_vaddr(vb, 0), vb2_plane_size(vb, 0));

	/*
	 * This can be useful if you want to see if we actually fill
	 * the buffer with something
	 */
	if (vb2_plane_vaddr(vb, 0))
		memset(vb2_plane_vaddr(vb, 0), 0xbd, vb2_plane_size(vb, 0));
#endif

	if (!icd->current_fmt) {
		dev_err(icd->parent, "%s NULL format point\n", __func__);
		return -EINVAL;
	}

	size = icd->user_height * bytes_per_line;

	if (vb2_plane_size(vb, 0) < size) {
		dev_err(icd->parent, "Buffer too small (%lu < %lu)\n",
			vb2_plane_size(vb, 0), size);
		return -ENOBUFS;
	}

	vb2_set_plane_payload(vb, 0, size);

	return tegra_camera_init_buffer(buf);
}

static void tegra_camera_videobuf_queue(struct vb2_buffer *vb)
{
	struct soc_camera_device *icd = container_of(vb->vb2_queue,
						     struct soc_camera_device,
						     vb2_vidq);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct tegra_camera_dev *cam = ici->priv;
	struct tegra_camera_buffer *buf = to_tegra_vb(vb);

	spin_lock(&cam->capture_lock);
	list_add_tail(&buf->queue, &cam->capture);
	spin_unlock(&cam->capture_lock);

	/* Wait up kthread for capture */
	wake_up_interruptible(&cam->capture_start_wait);
}

static void tegra_camera_videobuf_release(struct vb2_buffer *vb)
{
	struct soc_camera_device *icd = container_of(vb->vb2_queue,
						     struct soc_camera_device,
						     vb2_vidq);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct tegra_camera_buffer *buf = to_tegra_vb(vb);
	struct tegra_camera_dev *cam = ici->priv;

	spin_lock(&cam->done_lock);

	/*
	 * Doesn't hurt also if the list is empty, but it hurts, if queuing the
	 * buffer failed, and .buf_init() hasn't been called
	 */
	if (buf->queue.next)
		list_del_init(&buf->queue);

	spin_unlock(&cam->done_lock);
}

static int tegra_camera_videobuf_init(struct vb2_buffer *vb)
{
	/* This is for locking debugging only */
	INIT_LIST_HEAD(&to_tegra_vb(vb)->queue);

	return 0;
}

static int tegra_camera_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct soc_camera_device *icd = container_of(q,
						     struct soc_camera_device,
						     vb2_vidq);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct tegra_camera_dev *cam = ici->priv;

	/* Start kthread to capture frame */
	cam->kthread_capture_start = kthread_run(
					tegra_camera_kthread_capture_start, cam,
					"tegra-vi/capture-start");

	/* Start kthread to wait data output to buffer */
	cam->kthread_capture_done = kthread_run(
					tegra_camera_kthread_capture_done, cam,
					"tegra-vi/capture-done");
	return 0;
}

static int tegra_camera_stop_streaming(struct vb2_queue *q)
{
	struct soc_camera_device *icd = container_of(q,
						     struct soc_camera_device,
						     vb2_vidq);
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct tegra_camera_dev *cam = ici->priv;
	struct soc_camera_subdev_desc *ssdesc = &icd->sdesc->subdev_desc;
	struct tegra_camera_platform_data *pdata = ssdesc->drv_priv;
	int port = pdata->port;

	/* Stop the kthread for capture */
	kthread_stop(cam->kthread_capture_start);
	cam->kthread_capture_start = NULL;
	kthread_stop(cam->kthread_capture_done);
	cam->kthread_capture_done = NULL;

	cam->ops->capture_stop(cam, port);

	return 0;
}

static struct vb2_ops tegra_camera_videobuf_ops = {
	.queue_setup	= tegra_camera_videobuf_setup,
	.buf_prepare	= tegra_camera_videobuf_prepare,
	.buf_queue	= tegra_camera_videobuf_queue,
	.buf_cleanup	= tegra_camera_videobuf_release,
	.buf_init	= tegra_camera_videobuf_init,
	.wait_prepare	= soc_camera_unlock,
	.wait_finish	= soc_camera_lock,
	.start_streaming = tegra_camera_start_streaming,
	.stop_streaming	= tegra_camera_stop_streaming,
};

/*
 *  SOC camera host operations
 */
static int tegra_camera_init_videobuf(struct vb2_queue *q,
				      struct soc_camera_device *icd)
{
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->drv_priv = icd;
	q->ops = &tegra_camera_videobuf_ops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct tegra_camera_buffer);
	q->timestamp_type = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;

	return vb2_queue_init(q);
}

/*
 * Called with .video_lock held
 */
static int tegra_camera_add_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct tegra_camera_dev *cam = ici->priv;
	int ret;

	if (!cam->enable_refcnt) {
		ret = tegra_camera_activate(cam, icd);
		if (ret)
			return ret;
		cam->num_frames = 0;
	}
	cam->enable_refcnt++;

	return 0;
}

/* Called with .video_lock held */
static void tegra_camera_remove_device(struct soc_camera_device *icd)
{
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct tegra_camera_dev *cam = ici->priv;

	cam->enable_refcnt--;
	if (!cam->enable_refcnt)
		tegra_camera_deactivate(cam);
}

static int tegra_camera_set_bus_param(struct soc_camera_device *icd)
{
	return 0;
}

static int tegra_camera_get_formats(struct soc_camera_device *icd,
				    unsigned int idx,
				    struct soc_camera_format_xlate *xlate)
{
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	struct device *dev = icd->parent;
	struct soc_camera_host *ici = to_soc_camera_host(icd->parent);
	struct tegra_camera_dev *cam = ici->priv;
	int num_formats;
	const struct soc_mbus_pixelfmt *formats;
	int ret;
	enum v4l2_mbus_pixelcode code;
	int k;

	/*
	 * If we're in test pattern mode, ignore the subdev's formats, and
	 * pick a format that the test pattern mode can handle.
	 */
	if (!cam->tpg_mode) {
		ret = v4l2_subdev_call(sd, video, enum_mbus_fmt, idx, &code);
		if (ret != 0)
			/* No more formats */
			return 0;
	} else
		code = V4L2_MBUS_FMT_RGBA8888_4X8_LE;

	switch (code) {
	case V4L2_MBUS_FMT_UYVY8_2X8:
	case V4L2_MBUS_FMT_VYUY8_2X8:
	case V4L2_MBUS_FMT_YUYV8_2X8:
	case V4L2_MBUS_FMT_YVYU8_2X8:
		formats = tegra_camera_yuv_formats;
		num_formats = ARRAY_SIZE(tegra_camera_yuv_formats);
		break;
	case V4L2_MBUS_FMT_SBGGR8_1X8:
	case V4L2_MBUS_FMT_SRGGB8_1X8:
	case V4L2_MBUS_FMT_SBGGR10_1X10:
	case V4L2_MBUS_FMT_SRGGB10_1X10:
		formats = tegra_camera_bayer_formats;
		num_formats = ARRAY_SIZE(tegra_camera_bayer_formats);
		break;
	case V4L2_MBUS_FMT_RGBA8888_4X8_LE:
	case V4L2_MBUS_FMT_RGB888_1X24:
		formats = tegra_camera_rgb_formats;
		num_formats = ARRAY_SIZE(tegra_camera_rgb_formats);
		break;
	default:
		dev_notice(dev, "Not supporting mbus format code 0x%04x\n",
			   code);
		formats = NULL;
		num_formats = 0;
	}

	for (k = 0; xlate && (k < num_formats); k++) {
		xlate->host_fmt	= &formats[k];
		xlate->code	= code;
		xlate++;

		dev_notice(dev, "Supporting mbus format code 0x%04x using %s\n",
			   code, formats[k].name);
	}

	return num_formats;
}

static void tegra_camera_put_formats(struct soc_camera_device *icd)
{
	kfree(icd->host_priv);
	icd->host_priv = NULL;
}

static int tegra_camera_set_fmt(struct soc_camera_device *icd,
			      struct v4l2_format *f)
{
	struct device *dev = icd->parent;
	struct soc_camera_host *ici = to_soc_camera_host(dev);
	struct tegra_camera_dev *cam = ici->priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_camera_format_xlate *xlate = NULL;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	int ret = 0;

	xlate = soc_camera_xlate_by_fourcc(icd, pix->pixelformat);
	if (!xlate) {
		dev_warn(dev, "Format %x not found\n", pix->pixelformat);
		return -EINVAL;
	}

	mf.width	= pix->width;
	mf.height	= pix->height;
	mf.field	= pix->field;
	mf.colorspace	= pix->colorspace;
	mf.code		= xlate->code;

	if (!cam->tpg_mode) {
		ret = v4l2_subdev_call(sd, video, s_mbus_fmt, &mf);
		if (IS_ERR_VALUE(ret)) {
			dev_warn(dev, "Failed to configure for format %x\n",
				 pix->pixelformat);
			return ret;
		}

		if (mf.code != xlate->code) {
			dev_warn(dev,
				 "mf.code = 0x%04x, xlate->code = 0x%04x, "
				 "mismatch\n", mf.code, xlate->code);
			return -EINVAL;
		}
	}

	icd->user_width		= mf.width;
	icd->user_height	= mf.height;
	icd->current_fmt	= xlate;

	cam->field = pix->field;

	return ret;
}

static int tegra_camera_try_fmt(struct soc_camera_device *icd,
				struct v4l2_format *f)
{
	struct device *dev = icd->parent;
	struct soc_camera_host *ici = to_soc_camera_host(dev);
	struct tegra_camera_dev *cam = ici->priv;
	struct v4l2_subdev *sd = soc_camera_to_subdev(icd);
	const struct soc_camera_format_xlate *xlate;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct v4l2_mbus_framefmt mf;
	__u32 pixfmt = pix->pixelformat;
	int ret = 0;

	xlate = soc_camera_xlate_by_fourcc(icd, pixfmt);
	if (!xlate) {
		dev_warn(icd->parent, "Format %x not found\n", pixfmt);
		return -EINVAL;
	}

	pix->bytesperline = soc_mbus_bytes_per_line(pix->width,
						    xlate->host_fmt);
	if (pix->bytesperline < 0)
		return pix->bytesperline;
	pix->sizeimage = pix->height * pix->bytesperline;

	/* limit to sensor capabilities */
	mf.width	= pix->width;
	mf.height	= pix->height;
	mf.field	= pix->field;
	mf.colorspace	= pix->colorspace;
	mf.code		= xlate->code;

	if (!cam->tpg_mode) {
		ret = v4l2_subdev_call(sd, video, try_mbus_fmt, &mf);
		if (IS_ERR_VALUE(ret))
			return ret;
	}

	pix->width	= mf.width;
	pix->height	= mf.height;
	pix->colorspace	= mf.colorspace;
	/*
	 * width and height could have been changed, therefore update the
	 * bytesperline and sizeimage here.
	 */
	pix->bytesperline = soc_mbus_bytes_per_line(pix->width,
						    xlate->host_fmt);
	pix->sizeimage = pix->height * pix->bytesperline;

	switch (mf.field) {
	case V4L2_FIELD_ANY:
	case V4L2_FIELD_NONE:
		pix->field	= V4L2_FIELD_NONE;
		break;
	default:
		/* TODO: support interlaced at least in pass-through mode */
		dev_err(icd->parent, "Field type %d unsupported.\n",
			mf.field);
		return -EINVAL;
	}

	return ret;
}

static int tegra_camera_reqbufs(struct soc_camera_device *icd,
				struct v4l2_requestbuffers *p)
{
	return 0;
}

static unsigned int tegra_camera_poll(struct file *file, poll_table *pt)
{
	struct soc_camera_device *icd = file->private_data;

	return vb2_poll(&icd->vb2_vidq, file, pt);
}

static int tegra_camera_querycap(struct soc_camera_host *ici,
				 struct v4l2_capability *cap)
{
	strlcpy(cap->card, TEGRA_CAM_DRV_NAME, sizeof(cap->card));
	cap->version = TEGRA_CAM_VERSION_CODE;
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

	return 0;
}

static struct soc_camera_host_ops tegra_soc_camera_host_ops = {
	.owner		= THIS_MODULE,
	.init_videobuf2	= tegra_camera_init_videobuf,
	.add		= tegra_camera_add_device,
	.remove		= tegra_camera_remove_device,
	.set_bus_param	= tegra_camera_set_bus_param,
	.get_formats	= tegra_camera_get_formats,
	.put_formats	= tegra_camera_put_formats,
	.set_fmt	= tegra_camera_set_fmt,
	.try_fmt	= tegra_camera_try_fmt,
	.reqbufs	= tegra_camera_reqbufs,
	.poll		= tegra_camera_poll,
	.querycap	= tegra_camera_querycap,
};

static struct of_device_id tegra_vi_of_match[] = {
#ifdef TEGRA_12X_OR_HIGHER_CONFIG
	{ .compatible = "nvidia,tegra124-vi",
		.data = (struct nvhost_device_data *)&t124_vi_info },
#endif
	{ },
};

static int tegra_camera_probe(struct platform_device *pdev)
{
	struct tegra_camera_dev *cam;
	struct nvhost_device_data *ndata = NULL;
	int err = 0;

	if (pdev->dev.of_node) {
		const struct of_device_id *match;

		match = of_match_device(tegra_vi_of_match, &pdev->dev);
		if (match) {
			ndata = (struct nvhost_device_data *) match->data;
			pdev->dev.platform_data = ndata;
		}

		/*
		 * Device Tree will initialize this ID as -1
		 * Set it to the right value for future usage
		 */
		pdev->id = pdev->dev.id;
	} else
		ndata = pdev->dev.platform_data;

	if (!ndata) {
		dev_err(&pdev->dev, "No nvhost device data!\n");
		err = -EINVAL;
		goto exit;
	}

	/* vi.1 has to wait vi.0 initialized, so defer probing */
	if (pdev->id && ndata->master) {
		struct nvhost_device_data *master_ndata =
			ndata->master->dev.platform_data;
		if (master_ndata == platform_get_drvdata(ndata->master))
			return -EPROBE_DEFER;
	}

	cam = devm_kzalloc(&pdev->dev, sizeof(struct tegra_camera_dev),
			   GFP_KERNEL);
	if (!cam) {
		dev_err(&pdev->dev, "couldn't allocate cam\n");
		err = -ENOMEM;
		goto exit;
	}

	cam->ndata = ndata;
	cam->ndev = pdev;

	cam->ici.priv = cam;
	cam->ici.v4l2_dev.dev = &pdev->dev;
	cam->ici.nr = pdev->id;
	cam->ici.drv_name = dev_name(&pdev->dev);
	cam->ici.ops = &tegra_soc_camera_host_ops;

	cam->tpg_mode = tpg_mode;

	INIT_LIST_HEAD(&cam->capture);
	INIT_LIST_HEAD(&cam->done);
	spin_lock_init(&cam->capture_lock);
	spin_lock_init(&cam->done_lock);
	init_waitqueue_head(&cam->capture_start_wait);
	init_waitqueue_head(&cam->capture_done_wait);

	if (pdev->dev.of_node) {
		int cplen;
		const char *compat;
		compat = of_get_property(pdev->dev.of_node,
					 "compatible", &cplen);

		if (!strcmp(compat, "nvidia,tegra124-vi"))
			vi2_register(cam);
		else
			vi_register(cam);
	} else {
#ifdef TEGRA_12X_OR_HIGHER_CONFIG
	/* Register VI/CSI or VI2/CSI2 structs */
		vi2_register(cam);
#else
		vi_register(cam);
#endif
	}

	/* Init Regulator */
	cam->reg = devm_regulator_get(&pdev->dev, cam->regulator_name);
	if (IS_ERR_OR_NULL(cam->reg)) {
		dev_err(&pdev->dev, "%s: couldn't get regulator %s, err %ld\n",
			__func__, cam->regulator_name, PTR_ERR(cam->reg));
		err = PTR_ERR(cam->reg);
		cam->reg = NULL;
		goto exit;
	}

	mutex_init(&ndata->lock);
	platform_set_drvdata(pdev, ndata);
	err = nvhost_client_device_get_resources(pdev);
	if (err) {
		dev_err(&pdev->dev, "%s: nvhost get resources failed %d\n",
				__func__, err);
		goto exit;
	}

	/* Init syncpts */
	cam->ops->init_syncpts(cam);

	if (!ndata->aperture[0]) {
		if (ndata->master) {
			struct nvhost_device_data *master_ndata =
				ndata->master->dev.platform_data;
			ndata->aperture[0] = master_ndata->aperture[0];
		} else {
			dev_err(&pdev->dev, "%s: failed to map register base\n",
				__func__);
			err = -ENXIO;
			goto exit_free_syncpts;
		}
	}
	cam->reg_base = ndata->aperture[0];

	/* Match the nvhost_module_init VENC powergating */
	tegra_unpowergate_partition(TEGRA_POWERGATE_VENC);
	nvhost_module_init(pdev);

	err = nvhost_client_device_init(pdev);
	if (err) {
		dev_err(&pdev->dev, "%s: nvhost init failed %d\n",
				__func__, err);
		goto exit_nvhost_deinit;
	}

	cam->alloc_ctx = vb2_dma_contig_init_ctx(&pdev->dev);
	if (IS_ERR(cam->alloc_ctx)) {
		err = PTR_ERR(cam->alloc_ctx);
		goto exit_nvhost_client_deinit;
	}

	platform_set_drvdata(pdev, cam);
	err = soc_camera_host_register(&cam->ici);
	if (IS_ERR_VALUE(err))
		goto exit_cleanup_alloc_ctx;

	dev_notice(&pdev->dev, "Tegra camera driver loaded.\n");

	return err;

exit_cleanup_alloc_ctx:
	platform_set_drvdata(pdev, cam->ndata);
	vb2_dma_contig_cleanup_ctx(cam->alloc_ctx);
exit_nvhost_client_deinit:
	nvhost_client_device_release(pdev);
	/* HACK: nvhost_client_device_release() call nvhost_module_deinit() */
	if (0) {
exit_nvhost_deinit:
		nvhost_module_deinit(pdev);
	}
exit_free_syncpts:
	cam->ops->free_syncpts(cam);
exit:
	return err;
}

static int tegra_camera_remove(struct platform_device *pdev)
{
	struct soc_camera_host *ici = to_soc_camera_host(&pdev->dev);
	struct tegra_camera_dev *cam = container_of(ici,
					struct tegra_camera_dev, ici);

	soc_camera_host_unregister(ici);

	platform_set_drvdata(pdev, cam->ndata);
	nvhost_client_device_release(pdev);
	cam->ndata->aperture[0] = NULL;

	vb2_dma_contig_cleanup_ctx(cam->alloc_ctx);

	if (cam->ops)
		cam->ops->free_syncpts(cam);

	dev_notice(&pdev->dev, "Tegra camera host driver unloaded\n");

	return 0;
}

static struct platform_driver tegra_camera_driver = {
	.driver	= {
		.name	= TEGRA_CAM_DRV_NAME,
		.owner	= THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = tegra_vi_of_match,
#endif
	},
	.probe		= tegra_camera_probe,
	.remove		= tegra_camera_remove,
};

static int __init tegra_camera_init(void)
{
	return platform_driver_register(&tegra_camera_driver);
}

static void __exit tegra_camera_exit(void)
{
	platform_driver_unregister(&tegra_camera_driver);
}

module_init(tegra_camera_init);
module_exit(tegra_camera_exit);

MODULE_DESCRIPTION("TEGRA SoC Camera Host driver");
MODULE_AUTHOR("Bryan Wu <pengw@nvidia.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("nvhost:" TEGRA_CAM_DRV_NAME);
