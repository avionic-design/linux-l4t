/*
 * arch/arm/mach-tegra/com-tamonten-ng-power.c
 *
 * Copyright (C) 2011-2012, NVIDIA Corporation
 * Copyright (C) 2013, Avionic Design GmbH
 * Copyright (C) 2013, Julian Scheel <julian@jusst.de>
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

#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/tps6591x.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/tps6591x-regulator.h>
#include <linux/regulator/tps62360.h>

#include <asm/mach-types.h>

#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/pinmux.h>
#include <mach/edp.h>

#include "board.h"
#include "com-tamonten.h"
#include "pm.h"

/* FIXME: Taken from cardhu, while not understanding, what's this for */
#define PMC_CTRL 0x0
#define PMC_CTRL_INTR_LOW (1 << 17)

static struct regulator_consumer_supply tps6591x_vdd1_supply[] = {
};

static struct regulator_consumer_supply tps6591x_vdd2_supply[] = {
};

static struct regulator_consumer_supply tps6591x_vddctrl_supply[] = {
	REGULATOR_SUPPLY("vdd_cpu", NULL),
};

static struct regulator_consumer_supply tps6591x_vio_supply[] = {
	REGULATOR_SUPPLY("vddio_sdmmc", "sdhci-tegra.3"),
	REGULATOR_SUPPLY("vmmc", "sdhci-tegra.3"),
	REGULATOR_SUPPLY("avdd_usb_pll", NULL),
	REGULATOR_SUPPLY("avdd_hdmi_pll", NULL),
};

static struct regulator_consumer_supply tps6591x_ldo1_supply[] = {
	REGULATOR_SUPPLY("avdd_sata", NULL),
	REGULATOR_SUPPLY("vdd_sata", NULL), /* FIXME: moved to switcher output 1
					       on rev 01 */
	REGULATOR_SUPPLY("avdd_sata_pll", NULL),
	REGULATOR_SUPPLY("avdd_pexb", NULL),
	REGULATOR_SUPPLY("vdd_pexb", NULL),
	REGULATOR_SUPPLY("avdd_plle", NULL),
};

static struct regulator_consumer_supply tps6591x_ldo2_supply[] = {
};

static struct regulator_consumer_supply tps6591x_ldo3_supply[] = {
};

static struct regulator_consumer_supply tps6591x_ldo4_supply[] = {
};

static struct regulator_consumer_supply tps6591x_ldo5_supply[] = {
	REGULATOR_SUPPLY("vddio_sdmmc", "sdhci-tegra.2"),
	REGULATOR_SUPPLY("vmmc", "sdhci-tegra.2"),
};

static struct regulator_consumer_supply tps6591x_ldo6_supply[] = {
};

static struct regulator_consumer_supply tps6591x_ldo7_supply[] = {
};

static struct regulator_consumer_supply tps6591x_ldo8_supply[] = {
};

#define TPS_INIT(_name, _min_mv, _max_mv, _supply_reg, _always_on, \
		_boot_on, _apply_uv, _init_mv, _init_enable, _init_apply, \
		_ectrl, _flags) \
	static struct tps6591x_regulator_platform_data \
			pdata_##_name = { \
		.regulator = { \
			.constraints = { \
				.min_uV = _min_mv * 1000, \
				.max_uV = _max_mv * 1000, \
				.valid_modes_mask = (REGULATOR_MODE_NORMAL | \
						REGULATOR_MODE_STANDBY), \
				.valid_ops_mask = (REGULATOR_CHANGE_MODE | \
						REGULATOR_CHANGE_STATUS | \
						REGULATOR_CHANGE_VOLTAGE), \
				.always_on = _always_on, \
				.boot_on = _boot_on, \
				.apply_uV = _apply_uv, \
			}, \
			.num_consumer_supplies = \
				ARRAY_SIZE(tps6591x_##_name##_supply), \
			.consumer_supplies = \
				tps6591x_##_name##_supply, \
			.supply_regulator = _supply_reg, \
		}, \
		.init_uV = _init_mv * 1000, \
		.init_enable = _init_enable, \
		.init_apply = _init_apply, \
		.ectrl = _ectrl, \
		.flags = _flags, \
	}

TPS_INIT(vdd1, 600, 1500, 0, 0, 1, 0, -1, 0, 0, EXT_CTRL_SLEEP_OFF, 0);
TPS_INIT(vdd2, 1500, 1500, 0, 0, 1, 0, -1, 0, 0, 0, 0);
TPS_INIT(vddctrl, 600, 1400, 0, 0, 1, 0, -1, 0, 0, EXT_CTRL_EN1, 0);
TPS_INIT(vio, 1800, 1800, 0, 0, 1, 0, -1, 0, 0, 0, 0);
TPS_INIT(ldo1, 1050, 1050, tps6591x_rails(VDD_2), 0, 0, 0, -1, 0, 0, 0, 0);

TPS_INIT(ldo2, 1000, 1000, 0, 0, 0, 0, -1, 0, 0, 0, 0);
TPS_INIT(ldo3, 1000, 1000, 0, 0, 0, 0, -1, 0, 0, 0, 0);

TPS_INIT(ldo4, 1200, 1200, 0, 0, 1 ,0, -1, 1, 1, EXT_CTRL_EN1, 0);
TPS_INIT(ldo5, 3300, 3300, 0, 0, 1, 0, -1, 0, 1, 0, 0);
TPS_INIT(ldo6, 1200, 1200, tps6591x_rails(VIO), 0, 0, 1, -1, 0, 1, \
		EXT_CTRL_EN1, 0);
TPS_INIT(ldo7, 1200, 1200, tps6591x_rails(VIO), 0, 0, 1, -1, 0, 1, \
		EXT_CTRL_EN1, 0);
TPS_INIT(ldo8, 1000, 1000, tps6591x_rails(VIO), 0, 0, 1, -1, 0, 1, \
		EXT_CTRL_EN1, 0);

#define TPS_REG(_id, _name) \
{ \
	.id = TPS6591X_ID_##_id, \
	.name = "tps6591x-regulator", \
	.platform_data = &pdata_##_name, \
}

static struct tps6591x_subdev_info tps_devs[] = {
	TPS_REG(VIO, vio),
	TPS_REG(VDD_1, vdd1),
	TPS_REG(VDD_2, vdd2),
	TPS_REG(VDDCTRL, vddctrl),
	TPS_REG(LDO_1, ldo1),
	TPS_REG(LDO_2, ldo2),
	TPS_REG(LDO_3, ldo3),
	TPS_REG(LDO_4, ldo4),
	TPS_REG(LDO_5, ldo5),
	TPS_REG(LDO_6, ldo6),
	TPS_REG(LDO_7, ldo7),
	TPS_REG(LDO_8, ldo8),
};

#define TPS_GPIO_INIT_PDATA(gpio_nr, _init_apply, _sleep_en, _pulldn_en, \
		_output_en, _output_val)[gpio_nr] = { \
	.sleep_en = _sleep_en, \
	.pulldn_en = _pulldn_en, \
	.output_mode_en = _output_en, \
	.output_val = _output_val, \
	.init_apply = _init_apply, \
}

static struct tps6591x_gpio_init_data tps_gpio_pdata[] = {
	TPS_GPIO_INIT_PDATA(0, 1, 0, 0, 1, 1),
	TPS_GPIO_INIT_PDATA(1, 1, 0, 0, 0, 0),
	TPS_GPIO_INIT_PDATA(2, 1, 0, 0, 1, 1),
	TPS_GPIO_INIT_PDATA(3, 1, 0, 0, 0, 0),
	TPS_GPIO_INIT_PDATA(4, 1, 0, 0, 0, 0),
	TPS_GPIO_INIT_PDATA(5, 1, 0, 0, 0, 0),
	TPS_GPIO_INIT_PDATA(6, 1, 0, 0, 1, 1),
	TPS_GPIO_INIT_PDATA(7, 1, 0, 0, 1, 1),
	TPS_GPIO_INIT_PDATA(8, 1, 0, 0, 1, 1),
};

static struct tps6591x_sleep_keepon_data tps_slp_keepon = {
	.clkout32k_keepon = 1,
};

static struct tps6591x_platform_data tps_platform = {
	.irq_base = TAMONTEN_PMU_IRQ_BASE,
	.gpio_base = TAMONTEN_PMU_GPIO_BASE,
	.dev_slp_en = true,
	.slp_keepon = &tps_slp_keepon,
	.use_power_off = true,
	.num_subdevs = ARRAY_SIZE(tps_devs),
	.subdevs = tps_devs,
	.num_gpioinit_data = ARRAY_SIZE(tps_gpio_pdata),
	.gpio_init_data = tps_gpio_pdata,
};

static struct i2c_board_info __initdata tamonten_ng_regulators[] = {
	{
		I2C_BOARD_INFO("tps6591x", 0x2d),
		.irq = INT_EXTERNAL_PMU,
		.platform_data = &tps_platform,
	},
};

static struct regulator_consumer_supply tps62361_dcdc_supply[] = {
	REGULATOR_SUPPLY("vdd_core", NULL),
};

static struct tps62360_regulator_platform_data tps62361_pdata = {
	.reg_init_data = {
		.constraints = {
			.min_uV = 500000,
			.max_uV = 1770000,
			.valid_modes_mask = (REGULATOR_MODE_NORMAL |
					REGULATOR_MODE_STANDBY),
			.valid_ops_mask = (REGULATOR_CHANGE_MODE |
					REGULATOR_CHANGE_STATUS |
					REGULATOR_CHANGE_VOLTAGE),
			.always_on = 1,
			.boot_on = 1,
			.apply_uV = 0,
		},
		.num_consumer_supplies = ARRAY_SIZE(tps62361_dcdc_supply),
		.consumer_supplies = tps62361_dcdc_supply,
	},
	.en_discharge = true,
	.vsel0_gpio = -1,
	.vsel1_gpio = -1,
	.vsel0_def_state = 0,
	.vsel1_def_state = 0,
};

static struct i2c_board_info __initdata tps62361_boardinfo[] = {
	{
		I2C_BOARD_INFO("tps62361", 0x60),
		.platform_data = &tps62361_pdata,
	}
};

int __init tamonten_ng_regulator_init(void)
{
	void __iomem *pmc = IO_ADDRESS(TEGRA_PMC_BASE);
	u32 pmc_ctrl;

	pmc_ctrl = readl(pmc + PMC_CTRL);
	writel(pmc_ctrl | PMC_CTRL_INTR_LOW, pmc + PMC_CTRL);

	regulator_has_full_constraints();
	i2c_register_board_info(COM_I2C_BUS_PWR, tamonten_ng_regulators, 1);
	i2c_register_board_info(COM_I2C_BUS_PWR, tps62361_boardinfo, 1);

	return 0;
}

static struct regulator_consumer_supply fixed_reg_en_5v_cp_supply[] = {
};
static struct regulator_consumer_supply fixed_reg_en_soc_supply[] = {
};
static struct regulator_consumer_supply fixed_reg_en_5v0_supply[] = {
};
static struct regulator_consumer_supply fixed_reg_en_ddr_supply[] = {
};
static struct regulator_consumer_supply fixed_reg_en_3v3_sys_supply[] = {
	REGULATOR_SUPPLY("avdd_usb", "tegra-ehci.0"),
	REGULATOR_SUPPLY("avdd_usb", "tegra-ehci.1"),
	REGULATOR_SUPPLY("avdd_usb", "tegra-ehci.2"),
	REGULATOR_SUPPLY("avdd_hdmi", NULL),
};
static struct regulator_consumer_supply fixed_reg_en_vdd_bl_supply[] = {
};
static struct regulator_consumer_supply fixed_reg_en_3v3_fuse_supply[] = {
};
static struct regulator_consumer_supply fixed_reg_en_3v3_emmc_supply[] = {
};
static struct regulator_consumer_supply fixed_reg_en_3v3_pex_hvdd_supply[] = {
	REGULATOR_SUPPLY("hvdd_pex", NULL),
};
static struct regulator_consumer_supply fixed_reg_en_3v3_sata_hvdd_supply[] = {
	REGULATOR_SUPPLY("hvdd_sata", NULL),
};
static struct regulator_consumer_supply fixed_reg_en_usb3_vbus_oc_supply[] = {
	REGULATOR_SUPPLY("vdd_vbus_typea_usb", NULL),
};

#define FIXED_SUPPLY(_name) "fixed_reg_"#_name
#define FIXED_REG(_id, _name, _in_supply, _always_on, _boot_on, _gpio_nr, \
		_active_high, _boot_state, _millivolts, _od_state) \
	static struct regulator_init_data ri_data_##_name= \
	{ \
		.supply_regulator = _in_supply, \
		.num_consumer_supplies = \
			ARRAY_SIZE(fixed_reg_##_name##_supply), \
		.consumer_supplies = fixed_reg_##_name##_supply, \
		.constraints = { \
			.valid_modes_mask = (REGULATOR_MODE_NORMAL | \
					REGULATOR_MODE_STANDBY), \
			.valid_ops_mask = (REGULATOR_CHANGE_MODE | \
					REGULATOR_CHANGE_STATUS | \
					REGULATOR_CHANGE_VOLTAGE), \
			.always_on = _always_on, \
			.boot_on = _boot_on, \
		}, \
	}; \
	static struct fixed_voltage_config fixed_reg_##_name##_pdata = \
	{ \
		.supply_name = FIXED_SUPPLY(_name), \
		.microvolts = _millivolts * 1000, \
		.gpio = _gpio_nr, \
		.enable_high = _active_high, \
		.enabled_at_boot = _boot_state, \
		.init_data = &ri_data_##_name, \
		.gpio_is_open_drain = _od_state, \
	}; \
	static struct platform_device fixed_reg_##_name##_dev = { \
		.name = "reg-fixed-voltage", \
		.id = _id, \
		.dev = { \
			.platform_data = &fixed_reg_##_name##_pdata, \
		}, \
	}

FIXED_REG(0, en_5v_cp, NULL, 1, 0, TAMONTEN_PMU_GPIO(0), true, 1, 5000, false);
FIXED_REG(1, en_soc, NULL, 1, 0, TAMONTEN_PMU_GPIO(2), true, 1, 1200, false);
FIXED_REG(2, en_5v0, NULL, 1, 0, TAMONTEN_PMU_GPIO(8), true, 1, 5000, false);
FIXED_REG(3, en_ddr, NULL, 1, 0, TAMONTEN_PMU_GPIO(7), true, 1, 1500, false);
FIXED_REG(4, en_3v3_sys, NULL, 1, 0, TAMONTEN_PMU_GPIO(6), true, 1, 3300, false);
FIXED_REG(5, en_vdd_bl, NULL, 0, 0, TEGRA_GPIO_PW0, true, 0, 5000, false);
FIXED_REG(6, en_3v3_fuse, FIXED_SUPPLY(en_3v3_sys), 0, 0, TEGRA_GPIO_PH3, true,
		0, 3300, false);
FIXED_REG(7, en_3v3_emmc, FIXED_SUPPLY(en_3v3_sys), 1, 0, TEGRA_GPIO_PJ2, true,
		1, 3300, false);
FIXED_REG(8, en_3v3_pex_hvdd, FIXED_SUPPLY(en_3v3_sys), 0, 0, TEGRA_GPIO_PT3,
		true, 0, 3300, false);
FIXED_REG(9, en_3v3_sata_hvdd, FIXED_SUPPLY(en_3v3_sys), 0, 0, TEGRA_GPIO_PK3,
		true, 0, 3300, false);
FIXED_REG(10, en_usb3_vbus_oc, NULL, 0, 0, TEGRA_GPIO_PI7, true, 0, 5000, true);

static struct platform_device *fixed_reg_devs[] = {
	&fixed_reg_en_5v_cp_dev,
	&fixed_reg_en_soc_dev,
	&fixed_reg_en_5v0_dev,
	&fixed_reg_en_ddr_dev,
	&fixed_reg_en_3v3_sys_dev,
	&fixed_reg_en_vdd_bl_dev,
	&fixed_reg_en_3v3_fuse_dev,
	&fixed_reg_en_3v3_emmc_dev,
	&fixed_reg_en_3v3_pex_hvdd_dev,
	&fixed_reg_en_3v3_sata_hvdd_dev,
	&fixed_reg_en_usb3_vbus_oc_dev,
};

int __init tamonten_ng_fixed_regulator_init(void)
{
	return platform_add_devices(fixed_reg_devs, ARRAY_SIZE(fixed_reg_devs));
}
subsys_initcall_sync(tamonten_ng_fixed_regulator_init);

static struct tegra_suspend_platform_data tamonten_ng_suspend_data = {
	.cpu_timer	= 2000,
	.cpu_off_timer	= 0,
	.suspend_mode	= TEGRA_SUSPEND_NONE,
	.core_timer	= 0x7e7e,
	.core_off_timer = 0,
	.corereq_high	= true,
	.sysclkreq_high	= true,
};

int __init tamonten_ng_suspend_init(void)
{
	tegra_init_suspend(&tamonten_ng_suspend_data);
	return 0;
}

int __init tamonten_ng_edp_init(void)
{
#ifdef CONFIG_TEGRA_EDP_LIMITS
	unsigned int regulator_mA;

	regulator_mA = get_maximum_cpu_current_supported();
	if (!regulator_mA)
		regulator_mA = 2000;
	pr_info("%s: CPU regulator %d mA\n", __func__, regulator_mA);

	tegra_init_cpu_edp_limits(regulator_mA);
#endif
	return 0;
}
