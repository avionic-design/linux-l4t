/*
 * Copyright (c) 2014 Avionic Design GmbH
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/nct1008.h>
#include <linux/pid_thermal_gov.h>
#include <linux/tegra-fuse.h>
#include <linux/nvmap.h>

#include <mach/io_dpd.h>
#include <mach/edp.h>
#include <mach/isomgr.h>

#include "com-meerkat.h"
#include "board.h"
#include "board-common.h"
#include "clock.h"
#include "common.h"
#include "devices.h"
#include "dvfs.h"
#include "gpio-names.h"
#include "pm.h"

static __initdata struct tegra_clk_init_table meerkat_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "pll_m",	NULL,		0,		false},
	{ "hda",	"pll_p",	108000000,	false},
	{ "hda2codec_2x", "pll_p",	48000000,	false},
	{ "pwm",	"pll_p",	48000000,	false},
	{ "pll_a",	"pll_p_out1",	282240000,	false},
	{ "pll_a_out0",	"pll_a",	12288000,	false},
	{ "i2s0",	"pll_a_out0",	0,		false},
	{ "i2s1",	"pll_a_out0",	0,		false},
	{ "i2s2",	"pll_a_out0",	0,		false},
	{ "i2s3",	"pll_a_out0",	0,		false},
	{ "i2s4",	"pll_a_out0",	0,		false},
	{ "spdif_out",	"pll_a_out0",	0,		false},
	{ "d_audio",	"pll_a_out0",	12288000,	false},
	{ "dam0",	"clk_m",	12000000,	false},
	{ "dam1",	"clk_m",	12000000,	false},
	{ "dam2",	"clk_m",	12000000,	false},
	{ "audio1",	"i2s1_sync",	0,		false},
	{ "audio3",	"i2s3_sync",	0,		false},
	{ "vi_sensor",	"pll_p",	150000000,	false},
	{ "vi_sensor2",	"pll_p",	150000000,	false},
	{ "cilab",	"pll_p",	150000000,	false},
	{ "cilcd",	"pll_p",	150000000,	false},
	{ "cile",	"pll_p",	150000000,	false},
	{ "i2c1",	"pll_p",	3200000,	false},
	{ "i2c2",	"pll_p",	3200000,	false},
	{ "i2c3",	"pll_p",	3200000,	false},
	{ "i2c4",	"pll_p",	3200000,	false},
	{ "i2c5",	"pll_p",	3200000,	false},
	{ "sbc1",	"pll_p",	25000000,	false},
	{ "sbc2",	"pll_p",	25000000,	false},
	{ "sbc3",	"pll_p",	25000000,	false},
	{ "sbc4",	"pll_p",	25000000,	false},
	{ "sbc5",	"pll_p",	25000000,	false},
	{ "sbc6",	"pll_p",	25000000,	false},
	{ "uarta",	"pll_p",	408000000,	false},
	{ "uartb",	"pll_p",	408000000,	false},
	{ "uartc",	"pll_p",	408000000,	false},
	{ "uartd",	"pll_p",	408000000,	false},
	{ NULL,		NULL,		0,		0},
};

static struct nvmap_platform_carveout meerkat_carveouts[] = {
	[0] = {
		.name = "iram",
		.usage_mask = NVMAP_HEAP_CARVEOUT_IRAM,
		.base = TEGRA_IRAM_BASE + TEGRA_RESET_HANDLER_SIZE,
		.size = TEGRA_IRAM_SIZE - TEGRA_RESET_HANDLER_SIZE,
		.dma_dev = &tegra_iram_dev,
	},
};

static struct nvmap_platform_data meerkat_nvmap_data = {
	.carveouts = meerkat_carveouts,
	.nr_carveouts = ARRAY_SIZE(meerkat_carveouts),
};

static struct platform_device meerkat_nvmap_device = {
	.name = "tegra-nvmap",
	.id = -1,
	.dev = {
		.platform_data = &meerkat_nvmap_data,
	},
};

static struct platform_device *meerkat_devices[] __initdata = {
#if defined(CONFIG_TEGRA_WATCHDOG)
	&tegra_wdt0_device,
#endif
#if defined(CONFIG_CRYPTO_DEV_TEGRA_AES)
	&tegra_aes_device,
#endif
	&tegra_hier_ictlr_device,
	&meerkat_nvmap_device,
};

static struct of_dev_auxdata meerkat_auxdata_lookup[] __initdata = {
	COM_MEERKAT_AUXDATA,
	{}
};

static struct tegra_suspend_platform_data meerkat_suspend_data = {
	.cpu_timer      = 500,
	.cpu_off_timer  = 300,
	.suspend_mode   = TEGRA_SUSPEND_LP0,
	.core_timer     = 0x157e,
	.core_off_timer = 10,
	.corereq_high   = true,
	.sysclkreq_high = true,
	.cpu_lp2_min_residency = 1000,
	.min_residency_vmin_fmin = 1000,
	.min_residency_ncpu_fast = 8000,
	.min_residency_ncpu_slow = 5000,
	.min_residency_mclk_stop = 5000,
	.min_residency_crail = 20000,
};

static struct pid_thermal_gov_params cpu_pid_params = {
	.max_err_temp = 4000,
	.max_err_gain = 1000,

	.gain_p = 1000,
	.gain_d = 0,

	.up_compensation = 15,
	.down_compensation = 15,
};

static struct thermal_zone_params cpu_tzp = {
	.governor_name = "pid_thermal_gov",
	.governor_params = &cpu_pid_params,
};

static struct thermal_zone_params board_tzp = {
	.governor_name = "pid_thermal_gov"
};

static struct nct1008_platform_data meerkat_nct72_pdata = {
	.loc_name = "tegra",
	.supported_hwrev = true,
	.conv_rate = 0x06, /* 4Hz conversion rate */
	.offset = 0,
	.extended_range = true,

	.sensors = {
		[LOC] = {
			.tzp = &board_tzp,
			.shutdown_limit = 120, /* C */
			.passive_delay = 1000,
			.num_trips = 1,
			.trips = {
				{
					.cdev_type = "therm_est_activ",
					.trip_temp = 40000,
					.trip_type = THERMAL_TRIP_ACTIVE,
					.hysteresis = 1000,
					.upper = THERMAL_NO_LIMIT,
					.lower = THERMAL_NO_LIMIT,
					.mask = 1,
				},
			},
		},
		[EXT] = {
			.tzp = &cpu_tzp,
			.shutdown_limit = 95, /* C */
			.passive_delay = 1000,
			.num_trips = 2,
			.trips = {
				{
					.cdev_type = "shutdown_warning",
					.trip_temp = 93000,
					.trip_type = THERMAL_TRIP_PASSIVE,
					.upper = THERMAL_NO_LIMIT,
					.lower = THERMAL_NO_LIMIT,
					.mask = 0,
				},
				{
					.cdev_type = "cpu-balanced",
					.trip_temp = 83000,
					.trip_type = THERMAL_TRIP_PASSIVE,
					.upper = THERMAL_NO_LIMIT,
					.lower = THERMAL_NO_LIMIT,
					.hysteresis = 1000,
					.mask = 1,
				},
			}
		}
	}
};

static struct i2c_board_info meerkat_nct72_board_info = {
	I2C_BOARD_INFO("nct72", 0x4c),
	.platform_data = &meerkat_nct72_pdata,
	.irq = -1,
};

static int meerkat_nc72_init(void)
{
	static const int irq_gpio = TEGRA_GPIO_PI6;
	struct thermal_trip_info *trip_state;
	int i, err;

	/* raise NCT's thresholds if soctherm CP,FT fuses are ok */
	if ((tegra_fuse_calib_base_get_cp(NULL, NULL) >= 0) &&
	    (tegra_fuse_calib_base_get_ft(NULL, NULL) >= 0)) {
		meerkat_nct72_pdata.sensors[EXT].shutdown_limit += 20;
		for (i = 0; i < meerkat_nct72_pdata.sensors[EXT].num_trips;
			 i++) {
			trip_state = &meerkat_nct72_pdata.sensors[EXT].trips[i];
			if (!strncmp(trip_state->cdev_type, "cpu-balanced",
					THERMAL_NAME_LENGTH)) {
				trip_state->cdev_type = "_none_";
				break;
			}
		}
	} else {
		tegra_platform_edp_init(
			meerkat_nct72_pdata.sensors[EXT].trips,
			&meerkat_nct72_pdata.sensors[EXT].num_trips,
					12000); /* edp temperature margin */
		tegra_add_cpu_vmax_trips(
			meerkat_nct72_pdata.sensors[EXT].trips,
			&meerkat_nct72_pdata.sensors[EXT].num_trips);
		tegra_add_tgpu_trips(
			meerkat_nct72_pdata.sensors[EXT].trips,
			&meerkat_nct72_pdata.sensors[EXT].num_trips);
		tegra_add_vc_trips(
			meerkat_nct72_pdata.sensors[EXT].trips,
			&meerkat_nct72_pdata.sensors[EXT].num_trips);
		tegra_add_core_vmax_trips(
			meerkat_nct72_pdata.sensors[EXT].trips,
			&meerkat_nct72_pdata.sensors[EXT].num_trips);
	}

	err = gpio_request(irq_gpio, "temp_alert");
	if (err < 0)
		return err;

	err = gpio_direction_input(irq_gpio);
	if (err < 0)
		goto free_gpio;

	meerkat_nct72_board_info.irq = gpio_to_irq(irq_gpio);
	err = i2c_register_board_info(4, &meerkat_nct72_board_info, 1);
	if (err) {
		pr_err("nct72: Failed to register: %d\n", err);
		goto free_gpio;
	}

	pr_info("nct72 registered\n");
	return 0;

free_gpio:
	gpio_free(irq_gpio);
	return err;
}

void __init tegra_meerkat_init_early(void)
{
	tegra12x_init_early();
}

static void __init tegra_meerkat_edp_init(void)
{
	unsigned int regulator_mA;

	regulator_mA = get_maximum_cpu_current_supported();
	if (!regulator_mA)
		regulator_mA = 15000;

	pr_info("%s: CPU regulator %d mA\n", __func__, regulator_mA);

	tegra_init_cpu_edp_limits(regulator_mA);

	/* gpu maximum current */
	regulator_mA = 8000;
	pr_info("%s: GPU regulator %d mA\n", __func__, regulator_mA);

	tegra_init_gpu_edp_limits(regulator_mA);
}

void __init tegra_meerkat_dt_init(struct of_dev_auxdata *auxdata)
{
	tegra_clk_init_from_table(meerkat_clk_init_table);
	tegra_clk_verify_parents();
	/* FIXME: This should perhaps be called with the board name instead */
	tegra_soc_device_init("Meerkat COM");

	of_platform_populate(NULL, of_default_bus_match_table,
			auxdata, &platform_bus);
	platform_add_devices(meerkat_devices, ARRAY_SIZE(meerkat_devices));

	tegra_io_dpd_init();
	tegra_init_suspend(&meerkat_suspend_data);
	tegra_meerkat_edp_init();
	isomgr_init();

	/* put PEX pads into DPD mode to save additional power */
	//tegra_io_dpd_enable(&pexbias_io);
	//tegra_io_dpd_enable(&pexclk1_io);
	//tegra_io_dpd_enable(&pexclk2_io);

#ifdef CONFIG_TEGRA_WDT_RECOVERY
	tegra_wdt_recovery_init();
#endif

	if (meerkat_nc72_init())
		pr_err("Failed to register temperature sensor\n");
}

void __init tegra_meerkat_init(void)
{
	tegra_meerkat_dt_init(meerkat_auxdata_lookup);
}

void __init tegra_meerkat_reserve(void)
{
#ifdef CONFIG_TEGRA_HDMI_PRIMARY
	ulong tmp;
#endif /* CONFIG_TEGRA_HDMI_PRIMARY */

	ulong carveout_size = 0;
	ulong fb2_size = SZ_16M;
	ulong fb1_size = SZ_16M + SZ_2M;
	ulong vpr_size = 186 * SZ_1M;

#ifdef CONFIG_FRAMEBUFFER_CONSOLE
	/* support FBcon on 4K monitors */
	fb2_size = SZ_64M + SZ_8M;	/* 4096*2160*4*2 = 70778880 bytes */
#endif /* CONFIG_FRAMEBUFFER_CONSOLE */

#ifdef CONFIG_TEGRA_HDMI_PRIMARY
	tmp = fb1_size;
	fb1_size = fb2_size;
	fb2_size = tmp;
#endif /* CONFIG_TEGRA_HDMI_PRIMARY */

	tegra_reserve4(carveout_size, fb1_size, fb2_size, vpr_size);
}
