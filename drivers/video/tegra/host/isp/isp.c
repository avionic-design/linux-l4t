/*
 * drivers/video/tegra/host/isp/isp.c
 *
 * Tegra Graphics ISP
 *
 * Copyright (c) 2012-2015, NVIDIA Corporation.  All rights reserved.
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

#include <linux/export.h>
#include <linux/resource.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/irq.h>
#include <linux/workqueue.h>
#include <linux/tegra_pm_domains.h>

#include "dev.h"
#include "bus_client.h"
#include "nvhost_acm.h"
#include "t124/t124.h"

#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/nvhost_isp_ioctl.h>
#include <mach/latency_allowance.h>
#include "isp.h"

#define T12_ISP_CG_CTRL		0x74
#define T12_CG_2ND_LEVEL_EN	1
#define T12_ISPA_DEV_ID		0
#define T12_ISPB_DEV_ID		1

#define	ISP_MAX_BPP		2

static struct of_device_id tegra_isp_of_match[] = {
#ifdef TEGRA_12X_OR_HIGHER_CONFIG
	{ .compatible = "nvidia,tegra124-isp",
		.data = (struct nvhost_device_data *)&t124_isp_info },
#endif
	{ },
};

#ifdef TEGRA_12X_OR_HIGHER_CONFIG
static void (*mfi_callback)(void *);
static void *mfi_callback_arg;
static DEFINE_MUTEX(isp_isr_lock);

static int __init init_tegra_isp_isr_callback(void)
{
	mutex_init(&isp_isr_lock);
	return 0;
}

pure_initcall(init_tegra_isp_isr_callback);
#endif

int nvhost_isp_t124_finalize_poweron(struct platform_device *pdev)
{
	host1x_writel(pdev, T12_ISP_CG_CTRL, T12_CG_2ND_LEVEL_EN);
	return 0;
}

#if defined(CONFIG_TEGRA_ISOMGR)
static int isp_isomgr_register(struct isp *tegra_isp)
{
	int iso_client_id = TEGRA_ISO_CLIENT_ISP_A;
	struct clk *isp_clk;
	unsigned long max_bw = 0;
	struct nvhost_device_data *pdata =
				platform_get_drvdata(tegra_isp->ndev);

	dev_dbg(&tegra_isp->ndev->dev, "%s++\n", __func__);

	if (WARN_ONCE(pdata == NULL, "pdata not found, %s failed\n", __func__))
		return -ENODEV;

	if (tegra_isp->dev_id == T12_ISPB_DEV_ID)
		iso_client_id = TEGRA_ISO_CLIENT_ISP_B;
	if (tegra_isp->dev_id == T12_ISPA_DEV_ID)
		iso_client_id = TEGRA_ISO_CLIENT_ISP_A;

	/* Get max ISP BW */
	isp_clk = pdata->clk[0];
	max_bw = (clk_round_rate(isp_clk, UINT_MAX) / 1000) * ISP_MAX_BPP;

	/* Register with max possible BW for ISP usecases.*/
	tegra_isp->isomgr_handle = tegra_isomgr_register(iso_client_id,
					max_bw,
					NULL,	/* tegra_isomgr_renegotiate */
					NULL);	/* *priv */

	if (!tegra_isp->isomgr_handle) {
		dev_err(&tegra_isp->ndev->dev,
			"%s: unable to register isomgr\n",
				__func__);
		return -ENOMEM;
	}

	return 0;
}

static int isp_isomgr_unregister(struct isp *tegra_isp)
{
	tegra_isomgr_unregister(tegra_isp->isomgr_handle);
	tegra_isp->isomgr_handle = NULL;

	return 0;
}

static int isp_isomgr_request(struct isp *tegra_isp, uint isp_bw, uint lt)
{
	int ret = 0;

	dev_dbg(&tegra_isp->ndev->dev,
		"%s++ bw=%u, lt=%u\n", __func__, isp_bw, lt);

	/* return value of tegra_isomgr_reserve is dvfs latency in usec */
	ret = tegra_isomgr_reserve(tegra_isp->isomgr_handle,
				isp_bw,	/* KB/sec */
				lt);	/* usec */
	if (!ret) {
		dev_err(&tegra_isp->ndev->dev,
		"%s: failed to reserve %u KBps\n", __func__, isp_bw);
		return -ENOMEM;
	}

	/* return value of tegra_isomgr_realize is dvfs latency in usec */
	ret = tegra_isomgr_realize(tegra_isp->isomgr_handle);
	if (ret)
		dev_dbg(&tegra_isp->ndev->dev,
		"%s: tegra_isp isomgr latency is %d usec",
		__func__, ret);
	else {
		dev_err(&tegra_isp->ndev->dev,
		"%s: failed to realize %u KBps\n", __func__, isp_bw);
			return -ENOMEM;
	}
	return 0;
}

static int isp_isomgr_release(struct isp *tegra_isp)
{
	int ret = 0;
	dev_dbg(&tegra_isp->ndev->dev, "%s++\n", __func__);

	/* deallocate isomgr bw */
	ret = isp_isomgr_request(tegra_isp, 0, 0);
	if (ret) {
		dev_err(&tegra_isp->ndev->dev,
		"%s: failed to deallocate memory in isomgr\n",
		__func__);
		return -ENOMEM;
	}

	return 0;
}
#endif

#ifdef TEGRA_12X_OR_HIGHER_CONFIG
static inline u32 tegra_isp_read(struct isp *tegra_isp, u32 offset)
{
	return readl(tegra_isp->base + offset);
}

static inline void tegra_isp_write(struct isp *tegra_isp, u32 offset, u32 data)
{
	writel(data, tegra_isp->base + offset);
}

int tegra_isp_register_mfi_cb(callback cb, void *cb_arg)
{
	if (mfi_callback || mfi_callback_arg) {
		pr_err("cb already registered\n");
		return -1;
	}

	mutex_lock(&isp_isr_lock);
	mfi_callback = cb;
	mfi_callback_arg = cb_arg;
	mutex_unlock(&isp_isr_lock);

	return 0;
}
EXPORT_SYMBOL(tegra_isp_register_mfi_cb);

int tegra_isp_unregister_mfi_cb()
{
	mutex_lock(&isp_isr_lock);
	mfi_callback = NULL;
	mfi_callback_arg = NULL;
	mutex_unlock(&isp_isr_lock);

	return 0;
}
EXPORT_SYMBOL(tegra_isp_unregister_mfi_cb);

static void isp_isr_work(struct work_struct *isp_work)
{
	if (mfi_callback == NULL) {
		pr_debug("NULL callback\n");
		return;
	}

	mutex_lock(&isp_isr_lock);
	mfi_callback(mfi_callback_arg);
	mutex_unlock(&isp_isr_lock);
	return;
}

static irqreturn_t isp_isr(int irq, void *dev_id)
{
	struct isp *dev = dev_id;
	unsigned long flags;
	u32 reg, enable_reg;

	spin_lock_irqsave(&dev->lock, flags);

	reg = tegra_isp_read(dev, 0xf8);

	if (reg | (1<<5)) {
		/* Disable */
		enable_reg = tegra_isp_read(dev, 0x14c);
		enable_reg &= ~1;
		tegra_isp_write(dev, 0x14c, enable_reg);

		/* Clear */
		reg = reg & (1<<5);
		tegra_isp_write(dev, 0xf8, reg);

		/* put work into queue */
		queue_work(dev->isp_workqueue,
			(struct work_struct *)dev->my_isr_work);

	} else {
		pr_err("Unkown interrupt - ISR status %x\n", reg);
	}

	spin_unlock_irqrestore(&dev->lock, flags);
	return IRQ_HANDLED;
}
#endif

static int isp_probe(struct platform_device *dev)
{
	int err = 0;
	int dev_id = 0;

	struct isp *tegra_isp;
	struct nvhost_device_data *pdata = NULL;

	if (dev->dev.of_node) {
		const struct of_device_id *match;

		match = of_match_device(tegra_isp_of_match, &dev->dev);
		if (match)
			pdata = (struct nvhost_device_data *)match->data;
#ifdef TEGRA_12X_OR_HIGHER_CONFIG
		if (sscanf(dev->name, "isp.%1d", &dev_id) != 1)
			return -EINVAL;
		if (dev_id == T12_ISPB_DEV_ID)
			pdata = &t124_ispb_info;
		if (dev_id == T12_ISPA_DEV_ID)
			pdata = &t124_isp_info;
#endif

	} else
		pdata = (struct nvhost_device_data *)dev->dev.platform_data;

	WARN_ON(!pdata);
	if (!pdata) {
		dev_info(&dev->dev, "no platform data\n");
		return -ENODATA;
	}

	tegra_isp = devm_kzalloc(&dev->dev, sizeof(struct isp), GFP_KERNEL);
	if (!tegra_isp) {
		dev_err(&dev->dev, "can't allocate memory for isp\n");
		return -ENOMEM;
	}

	pdata->pdev = dev;
	mutex_init(&pdata->lock);
	platform_set_drvdata(dev, pdata);

	err = nvhost_client_device_get_resources(dev);
	if (err)
		goto camera_isp_unregister;

	tegra_isp->dev_id = dev_id;
	tegra_isp->ndev = dev;

	pdata->private_data = tegra_isp;

#ifdef TEGRA_12X_OR_HIGHER_CONFIG
	/* init ispa isr */
	tegra_isp->base = pdata->aperture[0];
	if (!tegra_isp->base) {
		pr_err("%s: can't ioremap gnt_base\n", __func__);
		err = -ENOMEM;
	}

	tegra_isp->irq = platform_get_irq(dev, 0);
	if (tegra_isp->irq <= 0) {
		dev_err(&dev->dev, "no irq\n");
		err = -ENOENT;
		goto camera_isp_unregister;
	}

	err = request_irq(tegra_isp->irq,
		isp_isr, 0, "tegra-isp-isr", tegra_isp);
	if (err) {
		pr_err("%s: request_irq(%d) failed(%d)\n", __func__,
		tegra_isp->irq, err);
		goto camera_isp_unregister;
	}

	spin_lock_init(&tegra_isp->lock);

	/* creating workqueue */
	if (dev_id == 0)
		tegra_isp->isp_workqueue = alloc_workqueue("ispa_workqueue",
						 WQ_HIGHPRI | WQ_UNBOUND, 1);
	else
		tegra_isp->isp_workqueue = alloc_workqueue("ispb_workqueue",
						 WQ_HIGHPRI | WQ_UNBOUND, 1);

	if (!tegra_isp->isp_workqueue) {
		pr_err("failed to allocate isp_workqueue\n");
		goto camera_isp_unregister;
	}

	tegra_isp->my_isr_work =
		kmalloc(sizeof(struct tegra_isp_mfi), GFP_KERNEL);
	INIT_WORK((struct work_struct *)tegra_isp->my_isr_work, isp_isr_work);
	disable_irq(tegra_isp->irq);
	enable_irq(tegra_isp->irq);
#endif

	nvhost_module_init(dev);

#ifdef CONFIG_PM_GENERIC_DOMAINS
	pdata->pd.name = "ve";

	/* add module power domain and also add its domain
	 * as sub-domain of MC domain */
	err = nvhost_module_add_domain(&pdata->pd, dev);
	if (err)
		goto camera_isp_unregister;
#endif

	err = nvhost_client_device_init(dev);
	if (err)
		goto camera_isp_unregister;

	return 0;

camera_isp_unregister:
	dev_err(&dev->dev, "%s: failed\n", __func__);

	return err;
}

static int __exit isp_remove(struct platform_device *dev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(dev);
	struct isp *tegra_isp = (struct isp *)pdata->private_data;

#if defined(CONFIG_TEGRA_ISOMGR)
	if (tegra_isp->isomgr_handle)
		isp_isomgr_unregister(tegra_isp);
#endif
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_put(&dev->dev);
	pm_runtime_disable(&dev->dev);
#else
	nvhost_module_disable_clk(&dev->dev);
#endif
	nvhost_client_device_release(dev);
#ifdef TEGRA_12X_OR_HIGHER_CONFIG
	disable_irq(tegra_isp->irq);
	kfree(tegra_isp->my_isr_work);
	flush_workqueue(tegra_isp->isp_workqueue);
	destroy_workqueue(tegra_isp->isp_workqueue);
	tegra_isp = NULL;
#endif
	return 0;
}

static struct platform_driver isp_driver = {
	.probe = isp_probe,
	.remove = __exit_p(isp_remove),
	.driver = {
		.owner = THIS_MODULE,
		.name = "isp",
#ifdef CONFIG_PM
		.pm = &nvhost_module_pm_ops,
#endif
#ifdef CONFIG_OF
		.of_match_table = tegra_isp_of_match,
#endif
	}
};

static int isp_set_la(struct isp *tegra_isp, uint isp_bw, uint la_client)
{
	int ret = 0;

	if (tegra_isp->dev_id == T12_ISPB_DEV_ID)
		ret = tegra_set_camera_ptsa(TEGRA_LA_ISP_WAB,
				isp_bw, la_client);
	else
		ret = tegra_set_camera_ptsa(TEGRA_LA_ISP_WA,
				isp_bw, la_client);

	return ret;
}

long isp_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	struct isp *tegra_isp;

	if (_IOC_TYPE(cmd) != NVHOST_ISP_IOCTL_MAGIC)
		return -EFAULT;

	tegra_isp = file->private_data;

	switch (cmd) {
	case NVHOST_ISP_IOCTL_SET_EMC: {
		int ret;
		uint la_client = 0;
		uint isp_bw = 0;
		struct isp_emc emc_info;
		if (copy_from_user(&emc_info,
			(const void __user *)arg, sizeof(struct isp_emc))) {
			dev_err(&tegra_isp->ndev->dev,
				"%s: Failed to copy arg from user\n", __func__);
			return -EFAULT;
			}

		if (emc_info.bpp_output && emc_info.bpp_input)
			la_client = ISP_SOFT_ISO_CLIENT;
		else
			la_client = ISP_HARD_ISO_CLIENT;

		isp_bw = (((emc_info.isp_clk/1000) * emc_info.bpp_output) >> 3);

		/* Set latency allowance for given BW of ISP clients */
		ret = isp_set_la(tegra_isp, isp_bw, la_client);
		if (ret) {
			dev_err(&tegra_isp->ndev->dev,
			"%s: failed to set la for isp_bw %u MBps\n",
			__func__, isp_bw);
			return -ENOMEM;
		}

#if defined(CONFIG_TEGRA_ISOMGR)
		/*
		 * Register ISP as isomgr client.
		 */
		if (!tegra_isp->isomgr_handle) {
			ret = isp_isomgr_register(tegra_isp);
			if (ret) {
				dev_err(&tegra_isp->ndev->dev,
				"%s: failed to register ISP as isomgr client\n",
				__func__);
				return -ENOMEM;
			}
		}

		if (tegra_isp->isomgr_handle &&
			la_client == ISP_HARD_ISO_CLIENT) {
			/*
			 * Set ISP ISO BW requirements, only if it is
			 * hard ISO client, i.e. VI is in streaming mode.
			 * There is no way to figure out what latency
			 * can be tolerated in ISP without reading ISP
			 * registers for now. 3 usec is minimum time
			 * to switch PLL source. Let's put 4 usec as
			 * latency for now.
			 */

			/* isomgr driver expects BW in KBps */
			isp_bw = isp_bw * 1000;

			ret = isp_isomgr_request(tegra_isp, isp_bw, 4);
			if (ret) {
				dev_err(&tegra_isp->ndev->dev,
				"%s: failed to reserve %u KBps with isomgr\n",
				__func__, isp_bw);
				return -ENOMEM;
			}
		}
#endif
		return ret;
	}
	default:
		dev_err(&tegra_isp->ndev->dev,
			"%s: Unknown ISP ioctl.\n", __func__);
		return -EINVAL;
	}
	return 0;
}

static int isp_open(struct inode *inode, struct file *file)
{
	struct nvhost_device_data *pdata;
	struct isp *tegra_isp;

	pdata = container_of(inode->i_cdev,
		struct nvhost_device_data, ctrl_cdev);
	if (WARN_ONCE(pdata == NULL, "pdata not found, %s failed\n", __func__))
		return -ENODEV;

	tegra_isp = pdata->private_data;
	if (WARN_ONCE(tegra_isp == NULL,
		"tegra_isp not found, %s failed\n", __func__))
		return -ENODEV;

	file->private_data = tegra_isp;

	return 0;
}

static int isp_release(struct inode *inode, struct file *file)
{
#if defined(CONFIG_TEGRA_ISOMGR)
	int ret = 0;
	struct isp *tegra_isp = file->private_data;

	/* nullify isomgr request */
	if (tegra_isp->isomgr_handle) {
		ret = isp_isomgr_release(tegra_isp);
		if (ret) {
			dev_err(&tegra_isp->ndev->dev,
			"%s: failed to deallocate memory in isomgr\n",
			__func__);
			return -ENOMEM;
		}
	}
#endif
	return 0;
}

const struct file_operations tegra_isp_ctrl_ops = {
	.owner = THIS_MODULE,
	.open = isp_open,
	.unlocked_ioctl = isp_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = isp_ioctl,
#endif
	.release = isp_release,
};


static int __init isp_init(void)
{
	return platform_driver_register(&isp_driver);
}

static void __exit isp_exit(void)
{
	platform_driver_unregister(&isp_driver);
}

module_init(isp_init);
module_exit(isp_exit);
