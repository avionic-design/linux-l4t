/*
 * arch/arm/mach-tegra/panel-generic
 *
 * Copyright (c) 2015, Avionic Design GmbH
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

#include <linux/backlight.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>

#include "devices.h"
#include "board-panel.h"

static const char dt_match[] = "none,panel-generic";

struct panel_generic {
	struct backlight_device *backlight;
	int enable_gpio;
};

static void panel_generic_release(struct device *dev, void *res)
{
	struct panel_generic *panel = res;

	if (panel->backlight)
		put_device(&panel->backlight->dev);

	if (gpio_is_valid(panel->enable_gpio))
		gpio_free(panel->enable_gpio);
}

static struct panel_generic* panel_generic_init(struct device *dev)
{
	struct device_node *panel_node;
	struct device_node *backlight;
	struct panel_generic *panel;
	int err = 0;

	panel_node = of_parse_phandle(dev->of_node, "panel", 0);
	if (!panel_node) {
		dev_err(dev, "Could not find panel node\n");
		return ERR_PTR(-ENODEV);
	}

	panel = devres_alloc(panel_generic_release, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return ERR_PTR(-ENOMEM);

	backlight = of_parse_phandle(panel_node, "backlight", 0);
	if (backlight) {
		panel->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!panel->backlight)
			return ERR_PTR(-EPROBE_DEFER);
	}

	panel->enable_gpio = of_get_named_gpio(panel_node, "enable-gpios", 0);
	if (IS_ERR_VALUE(panel->enable_gpio)) {
		err = panel->enable_gpio;
		if (err == -EPROBE_DEFER)
			goto free_backlight;
	}

	devres_add(dev, panel);

	/* Activate the gpios */
	if (gpio_is_valid(panel->enable_gpio)) {
		gpio_request(panel->enable_gpio, "panel-generic-enable");
		gpio_direction_output(panel->enable_gpio, 0);
	}

	return panel;

free_backlight:
	if (panel->backlight)
		put_device(&panel->backlight->dev);

	devres_free(panel);

	return ERR_PTR(err);
}

static int panel_generic_enable(struct device *dev)
{
	struct panel_generic *panel = devres_find(dev, panel_generic_release,
						  NULL, NULL);

	if (!panel) {
		panel = panel_generic_init(dev);

		if (IS_ERR(panel))
			return PTR_ERR(panel);
	}

	if (gpio_is_valid(panel->enable_gpio))
		gpio_set_value(panel->enable_gpio, 1);

	return 0;
}

static int panel_generic_disable(struct device *dev)
{
	struct panel_generic *panel = devres_find(dev, panel_generic_release,
						  NULL, NULL);

	if (!panel)
		return -ENODEV;

	if (gpio_is_valid(panel->enable_gpio))
		gpio_set_value(panel->enable_gpio, 0);

	return 0;
}

struct tegra_panel_ops panel_generic_ops = {
	.enable = panel_generic_enable,
	.disable = panel_generic_disable,
};
