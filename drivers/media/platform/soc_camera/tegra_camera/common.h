/*
 * Copyright (c) 2012-2015, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef _TEGRA_CAMERA_COMMON_H_
#define _TEGRA_CAMERA_COMMON_H_

#include <linux/videodev2.h>

#include <media/videobuf2-dma-contig.h>
#include <media/soc_camera.h>

/* Buffer for one video frame */
struct tegra_camera_buffer {
	struct vb2_buffer		vb; /* v4l buffer must be first */
	struct list_head		queue;
	struct soc_camera_device	*icd;
	int				output_channel;

	/*
	 * Various buffer addresses shadowed so we don't have to recalculate
	 * per frame. These are calculated during videobuf_prepare.
	 */
	dma_addr_t			buffer_addr;
	dma_addr_t			buffer_addr_u;
	dma_addr_t			buffer_addr_v;
	dma_addr_t			start_addr;
	dma_addr_t			start_addr_u;
	dma_addr_t			start_addr_v;
};

static struct tegra_camera_buffer *to_tegra_vb(struct vb2_buffer *vb)
{
	return container_of(vb, struct tegra_camera_buffer, vb);
}

struct tegra_camera_dev;

struct tegra_camera_clk {
	const char	*name;
	struct clk	*clk;
	u32		freq;
	int		use_devname;
};

struct tegra_camera_ops {
	int (*clks_init)(struct tegra_camera_dev *cam, int port);
	void (*clks_deinit)(struct tegra_camera_dev *cam);
	void (*clks_enable)(struct tegra_camera_dev *cam);
	void (*clks_disable)(struct tegra_camera_dev *cam);

	void (*capture_clean)(struct tegra_camera_dev *vi2_cam);
	int (*capture_setup)(struct tegra_camera_dev *vi2_cam,
			     struct tegra_camera_buffer *buf);
	int (*capture_start)(struct tegra_camera_dev *vi2_cam,
			     struct tegra_camera_buffer *buf);
	int (*capture_wait)(struct tegra_camera_dev *vi2_cam,
			     struct tegra_camera_buffer *buf);
	int (*capture_done)(struct tegra_camera_dev *vi2_cam, int port);
	int (*capture_stop)(struct tegra_camera_dev *vi2_cam, int port);

	void (*init_syncpts)(struct tegra_camera_dev *vi2_cam);
	void (*free_syncpts)(struct tegra_camera_dev *vi2_cam);
	void (*incr_syncpts)(struct tegra_camera_dev *vi2_cam);
	void (*save_syncpts)(struct tegra_camera_dev *vi2_cam);

	void (*activate)(struct tegra_camera_dev *vi2_cam);
	void (*deactivate)(struct tegra_camera_dev *vi2_cam);
	int (*port_is_valid)(int port);

	int (*mipi_calibration)(struct tegra_camera_dev *vi2_cam,
			        struct tegra_camera_buffer *buf);
};

struct tegra_camera_dev {
	struct soc_camera_host		ici;
	struct platform_device		*ndev;
	struct nvhost_device_data	*ndata;

	struct regulator		*reg;
	const char			*regulator_name;

	struct tegra_camera_clk		*clks;
	int				num_clks;

	struct tegra_camera_ops		*ops;

	void __iomem			*reg_base;
	struct list_head		capture;
	spinlock_t			capture_lock;
	struct list_head		done;
	spinlock_t			done_lock;
	struct vb2_buffer		*active;
	struct vb2_alloc_ctx		*alloc_ctx;
	enum v4l2_field			field;
	int				sequence_a;
	int				sequence_b;

	struct task_struct		*kthread_capture_start;
	struct task_struct		*kthread_capture_done;
	wait_queue_head_t		capture_start_wait;
	wait_queue_head_t		capture_done_wait;

	/* syncpt ids */
	u32				syncpt_id_csi_a;
	u32				syncpt_id_csi_b;
	u32				syncpt_id_vip;

	/* syncpt values */
	u32				syncpt_csi_a;
	u32				syncpt_csi_b;
	u32				syncpt_vip;

	/* Debug */
	int				num_frames;
	int				enable_refcnt;

	/* Test Pattern Generator mode */
	int				tpg_mode;

	int				sof;
	int				cal_done;
};

#define TC_VI_REG_RD(dev, offset) readl(dev->reg_base + offset)
#define TC_VI_REG_WT(dev, offset, val) writel(val, dev->reg_base + offset)

int vi2_register(struct tegra_camera_dev *cam);
int vi_register(struct tegra_camera_dev *cam);

#endif
