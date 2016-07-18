/*
 * drivers/video/tegra/dc/lvds.c
 *
 * Copyright (c) 2012-2014, NVIDIA CORPORATION.  All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/i2c.h>

#include <mach/dc.h>

#include "lvds.h"
#include "edid.h"
#include "dc_priv.h"

static int tegra_dc_lvds_i2c_xfer(struct tegra_dc *dc, struct i2c_msg *msgs,
	int num)
{
	struct tegra_dc_lvds_data *lvds = tegra_dc_get_outdata(dc);

	return i2c_transfer(lvds->ddc_client->adapter, msgs, num);
}

static int tegra_dc_lvds_init(struct tegra_dc *dc)
{
	struct i2c_board_info ddc_board_info = {
		.type = "tegra_lvds_edid",
		.addr = 0x50,
	};
	struct i2c_adapter *ddc_adapter = NULL;
	struct tegra_dc_lvds_data *lvds;
	int err;

	lvds = kzalloc(sizeof(*lvds), GFP_KERNEL);
	if (!lvds)
		return -ENOMEM;

	lvds->dc = dc;
	lvds->sor = tegra_dc_sor_init(dc, NULL);

	if (IS_ERR_OR_NULL(lvds->sor)) {
		err = lvds->sor ? PTR_ERR(lvds->sor) : -ENODEV;
		lvds->sor = NULL;
		goto err_init;
	}

	if (dc->out->ddc_bus) {
		ddc_adapter = i2c_get_adapter(dc->out->ddc_bus);
		if (!ddc_adapter)
			return -EPROBE_DEFER;

		ddc_board_info.platform_data = lvds;
		lvds->ddc_client = i2c_new_device(
			ddc_adapter, &ddc_board_info);

		i2c_put_adapter(ddc_adapter);

		if (!lvds->ddc_client) {
			dev_err(&dc->ndev->dev,
				"lvds: Failed to create DDC client\n");
			return -EINVAL;
		}

		lvds->edid = tegra_edid_create(dc, tegra_dc_lvds_i2c_xfer);
		if (IS_ERR(lvds->edid)) {
			dev_err(&dc->ndev->dev, "lvds: Can't create EDID\n");
			i2c_release_client(lvds->ddc_client);
			lvds->ddc_client = NULL;
			lvds->edid = NULL;
		} else {
			tegra_dc_set_edid(dc, lvds->edid);
		}
	}

	tegra_dc_set_outdata(dc, lvds);

	return 0;

err_init:
	kfree(lvds);

	return err;
}


static void tegra_dc_lvds_destroy(struct tegra_dc *dc)
{
	struct tegra_dc_lvds_data *lvds = tegra_dc_get_outdata(dc);

	if (lvds->edid)
		tegra_edid_destroy(lvds->edid);

	if (lvds->ddc_client)
		i2c_release_client(lvds->ddc_client);

	if (lvds->sor)
		tegra_dc_sor_destroy(lvds->sor);
	kfree(lvds);
}


static void tegra_dc_lvds_enable(struct tegra_dc *dc)
{
	struct tegra_dc_lvds_data *lvds = tegra_dc_get_outdata(dc);

	tegra_dc_io_start(dc);

	/* Power on panel */
	tegra_sor_clk_enable(lvds->sor);
	tegra_sor_pad_cal_power(lvds->sor, true);
	tegra_dc_sor_set_internal_panel(lvds->sor, true);
	tegra_dc_sor_set_power_state(lvds->sor, 1);
	tegra_dc_sor_enable_lvds(lvds->sor, false, false);
	tegra_dc_io_end(dc);
}

static void tegra_dc_lvds_disable(struct tegra_dc *dc)
{
	struct tegra_dc_lvds_data *lvds = tegra_dc_get_outdata(dc);

	/* Power down SOR */
	tegra_dc_sor_disable(lvds->sor, true);
}


static void tegra_dc_lvds_suspend(struct tegra_dc *dc)
{
	struct tegra_dc_lvds_data *lvds = tegra_dc_get_outdata(dc);

	tegra_dc_lvds_disable(dc);
	lvds->suspended = true;
}


static void tegra_dc_lvds_resume(struct tegra_dc *dc)
{
	struct tegra_dc_lvds_data *lvds = tegra_dc_get_outdata(dc);

	if (!lvds->suspended)
		return;
	tegra_dc_lvds_enable(dc);
}

static long tegra_dc_lvds_setup_clk(struct tegra_dc *dc, struct clk *clk)
{
	struct tegra_dc_lvds_data *lvds = tegra_dc_get_outdata(dc);
	struct clk	*parent_clk =
		clk_get_sys(NULL, dc->out->parent_clk ? : "pll_p");

	/* The parent must run at the pixel clock rate because
	 * the SOR, unlike the DC, doesn't have any divider itself */
	clk_set_rate(parent_clk, dc->mode.pclk);

	if (clk_get_parent(clk) != parent_clk)
		clk_set_parent(clk, parent_clk);

	if (clk_get_parent(lvds->sor->sor_clk) != parent_clk)
		clk_set_parent(lvds->sor->sor_clk, parent_clk);

	tegra_sor_setup_clk(lvds->sor, clk, true);

	return tegra_dc_pclk_round_rate(dc, dc->mode.pclk);
}

#ifdef CONFIG_FRAMEBUFFER_CONSOLE
static int tegra_dc_lvds_get_monspecs(struct tegra_dc *dc,
		const struct fb_videomode **bestmode)
{
	struct tegra_dc_lvds_data *lvds = tegra_dc_get_outdata(dc);
	const struct fb_videomode *mode;
	struct list_head *modelist;
	struct fb_monspecs *specs;
	int i, ret;

	if (!lvds->edid)
		return 0;

	if (!dc->pdata->fb)
		return -EINVAL;

	specs = &dc->pdata->fb->monspecs;
	modelist = &dc->pdata->fb->modelist;

	ret = tegra_edid_get_monspecs(lvds->edid, specs);
	if (ret) {
		dev_err(&dc->ndev->dev, "error reading edid: %d\n",
				ret);
		return ret;
	}

	for (i = 0; i < specs->modedb_len; i++)
		fb_add_videomode(&specs->modedb[i], modelist);

	mode = fb_find_best_display(specs, modelist);
	if (!mode || PICOS2KHZ(mode->pixclock) >
		     PICOS2KHZ(tegra_dc_get_out_max_pixclock(dc))) {
		dev_info(&dc->ndev->dev, "No, or invalid, best mode found\n");
		return -EINVAL;
	}

	*bestmode = mode;

	dc->out->h_size = specs->max_x * 1000;
	dc->out->v_size = specs->max_y * 1000;

	return ret;
}
#else
static int tegra_dc_lvds_get_monspecs(struct tegra_dc *dc,
		const struct fb_videomode **bestmode)
{
	return 0;
}
#endif

struct tegra_dc_out_ops tegra_dc_lvds_ops = {
	.init	   = tegra_dc_lvds_init,
	.get_monspecs = tegra_dc_lvds_get_monspecs,
	.destroy   = tegra_dc_lvds_destroy,
	.enable	   = tegra_dc_lvds_enable,
	.disable   = tegra_dc_lvds_disable,
	.suspend   = tegra_dc_lvds_suspend,
	.resume	   = tegra_dc_lvds_resume,
	.setup_clk = tegra_dc_lvds_setup_clk,
};
