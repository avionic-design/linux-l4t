/*
 * ams AS3722 pin control and GPIO driver.
 *
 * Copyright (c) 2013-2014, NVIDIA CORPORATION. All rights reserved.
 *
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mfd/as3722.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pm.h>
#include <linux/slab.h>

#include "core.h"
#include "pinconf.h"
#include "pinctrl-utils.h"

#define AS3722_PIN_GPIO0		0
#define AS3722_PIN_GPIO1		1
#define AS3722_PIN_GPIO2		2
#define AS3722_PIN_GPIO3		3
#define AS3722_PIN_GPIO4		4
#define AS3722_PIN_GPIO5		5
#define AS3722_PIN_GPIO6		6
#define AS3722_PIN_GPIO7		7
#define AS3722_PIN_NUM			(AS3722_PIN_GPIO7 + 1)

#define AS3722_GPIO_CONFIG_PULL_UP      BIT(0)
#define AS3722_GPIO_CONFIG_PULL_DOWN    BIT(1)
#define AS3722_GPIO_CONFIG_HIGH_IMPED   BIT(2)
#define AS3722_GPIO_CONFIG_OPEN_DRAIN   BIT(3)

struct as3722_pin_function {
	const char *name;
	const char * const *groups;
	unsigned ngroups;
	int mux_option;
};

struct as3722_gpio_pin_control {
	bool enable_gpio_invert;
	bool input;
	unsigned config_prop;
	int io_function;
};

struct as3722_pingroup {
	const char *name;
	const unsigned pins[1];
	unsigned npins;
};

struct as3722_pctrl_info {
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct as3722 *as3722;
	struct gpio_chip gpio_chip;
	int pins_current_opt[AS3722_PIN_NUM];
	const struct as3722_pin_function *functions;
	unsigned num_functions;
	const struct as3722_pingroup *pin_groups;
	int num_pin_groups;
	const struct pinctrl_pin_desc *pins;
	unsigned num_pins;
	struct as3722_gpio_pin_control gpio_control[AS3722_PIN_NUM];
};

static const struct pinctrl_pin_desc as3722_pins_desc[] = {
	PINCTRL_PIN(AS3722_PIN_GPIO0, "gpio0"),
	PINCTRL_PIN(AS3722_PIN_GPIO1, "gpio1"),
	PINCTRL_PIN(AS3722_PIN_GPIO2, "gpio2"),
	PINCTRL_PIN(AS3722_PIN_GPIO3, "gpio3"),
	PINCTRL_PIN(AS3722_PIN_GPIO4, "gpio4"),
	PINCTRL_PIN(AS3722_PIN_GPIO5, "gpio5"),
	PINCTRL_PIN(AS3722_PIN_GPIO6, "gpio6"),
	PINCTRL_PIN(AS3722_PIN_GPIO7, "gpio7"),
};

static const char * const gpio_groups[] = {
	"gpio0",
	"gpio1",
	"gpio2",
	"gpio3",
	"gpio4",
	"gpio5",
	"gpio6",
	"gpio7",
};

enum as3722_pinmux_option {
	AS3722_PINMUX_GPIO			= 0,
	AS3722_PINMUX_INTERRUPT_OUT		= 1,
	AS3722_PINMUX_VSUB_VBAT_UNDEB_LOW_OUT	= 2,
	AS3722_PINMUX_GPIO_INTERRUPT		= 3,
	AS3722_PINMUX_PWM_INPUT			= 4,
	AS3722_PINMUX_VOLTAGE_IN_STBY		= 5,
	AS3722_PINMUX_OC_PG_SD0			= 6,
	AS3722_PINMUX_PG_OUT			= 7,
	AS3722_PINMUX_CLK32K_OUT		= 8,
	AS3722_PINMUX_WATCHDOG_INPUT		= 9,
	AS3722_PINMUX_SOFT_RESET_IN		= 11,
	AS3722_PINMUX_PWM_OUTPUT		= 12,
	AS3722_PINMUX_VSUB_VBAT_LOW_DEB_OUT	= 13,
	AS3722_PINMUX_OC_PG_SD6			= 14,
};

#define FUNCTION_GROUP(fname, mux)			\
	{						\
		.name = #fname,				\
		.groups = gpio_groups,			\
		.ngroups = ARRAY_SIZE(gpio_groups),	\
		.mux_option = AS3722_PINMUX_##mux,	\
	}

static const struct as3722_pin_function as3722_pin_function[] = {
	FUNCTION_GROUP(gpio, GPIO),
	FUNCTION_GROUP(interrupt-out, INTERRUPT_OUT),
	FUNCTION_GROUP(gpio-in-interrupt, GPIO_INTERRUPT),
	FUNCTION_GROUP(vsup-vbat-low-undebounce-out, VSUB_VBAT_UNDEB_LOW_OUT),
	FUNCTION_GROUP(vsup-vbat-low-debounce-out, VSUB_VBAT_LOW_DEB_OUT),
	FUNCTION_GROUP(voltage-in-standby, VOLTAGE_IN_STBY),
	FUNCTION_GROUP(oc-pg-sd0, OC_PG_SD0),
	FUNCTION_GROUP(oc-pg-sd6, OC_PG_SD6),
	FUNCTION_GROUP(powergood-out, PG_OUT),
	FUNCTION_GROUP(pwm-in, PWM_INPUT),
	FUNCTION_GROUP(pwm-out, PWM_OUTPUT),
	FUNCTION_GROUP(clk32k-out, CLK32K_OUT),
	FUNCTION_GROUP(watchdog-in, WATCHDOG_INPUT),
	FUNCTION_GROUP(soft-reset-in, SOFT_RESET_IN),
};

#define AS3722_PINGROUP(pg_name, pin_id) \
	{								\
		.name = #pg_name,					\
		.pins = {AS3722_PIN_##pin_id},				\
		.npins = 1,						\
	}

static const struct as3722_pingroup as3722_pingroups[] = {
	AS3722_PINGROUP(gpio0,	GPIO0),
	AS3722_PINGROUP(gpio1,	GPIO1),
	AS3722_PINGROUP(gpio2,	GPIO2),
	AS3722_PINGROUP(gpio3,	GPIO3),
	AS3722_PINGROUP(gpio4,	GPIO4),
	AS3722_PINGROUP(gpio5,	GPIO5),
	AS3722_PINGROUP(gpio6,	GPIO6),
	AS3722_PINGROUP(gpio7,	GPIO7),
};

static int as3722_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);

	return as_pci->num_pin_groups;
}

static const char *as3722_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
		unsigned group)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);

	return as_pci->pin_groups[group].name;
}

static int as3722_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
		unsigned group, const unsigned **pins, unsigned *num_pins)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);

	*pins = as_pci->pin_groups[group].pins;
	*num_pins = as_pci->pin_groups[group].npins;
	return 0;
}

static const struct pinctrl_ops as3722_pinctrl_ops = {
	.get_groups_count = as3722_pinctrl_get_groups_count,
	.get_group_name = as3722_pinctrl_get_group_name,
	.get_group_pins = as3722_pinctrl_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinctrl_utils_dt_free_map,
};

static int as3722_pinctrl_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);

	return as_pci->num_functions;
}

static const char *as3722_pinctrl_get_func_name(struct pinctrl_dev *pctldev,
			unsigned function)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);

	return as_pci->functions[function].name;
}

static int as3722_pinctrl_get_func_groups(struct pinctrl_dev *pctldev,
		unsigned function, const char * const **groups,
		unsigned * const num_groups)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);

	*groups = as_pci->functions[function].groups;
	*num_groups = as_pci->functions[function].ngroups;
	return 0;
}

static int as3722_pinctrl_enable(struct pinctrl_dev *pctldev, unsigned function,
		unsigned group)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);
	int gpio_cntr_reg = AS3722_GPIOn_CONTROL_REG(group);
	u8 val = AS3722_GPIO_IOSF_VAL(as_pci->functions[function].mux_option);
	int ret;

	dev_dbg(as_pci->dev, "%s(): GPIO %u pin to function %u and val %u\n",
		__func__, group, function, val);

	ret = as3722_update_bits(as_pci->as3722, gpio_cntr_reg,
			AS3722_GPIO_IOSF_MASK, val);
	if (ret < 0) {
		dev_err(as_pci->dev, "GPIO%d_CTRL_REG update failed %d\n",
			group, ret);
		return ret;
	}
	as_pci->gpio_control[group].io_function = function;
	val = val & AS3722_GPIO_IOSF_MASK;

	switch (val) {
	case AS3722_GPIO_IOSF_SD0_OUT:
	case AS3722_GPIO_IOSF_PWR_GOOD_OUT:
	case AS3722_GPIO_IOSF_Q32K_OUT:
	case AS3722_GPIO_IOSF_PWM_OUT:
	case AS3722_GPIO_IOSF_SD6_LOW_VOLT_LOW:
		ret = as3722_update_bits(as_pci->as3722, gpio_cntr_reg,
				AS3722_GPIO_MODE_MASK,
				AS3722_GPIO_MODE_OUTPUT_VDDH);
		if (ret < 0) {
			dev_err(as_pci->dev,
				"GPIO%d_CTRL_REG update failed %d\n",
				group, ret);

			return ret;
		}
		as_pci->gpio_control[group].config_prop = 0;
	}

	return ret;
}

static int as3722_pinctrl_gpio_get_mode(unsigned gpio_config_prop, bool input)
{
	if (gpio_config_prop & AS3722_GPIO_CONFIG_HIGH_IMPED)
		return -EINVAL;

	if (gpio_config_prop & AS3722_GPIO_CONFIG_OPEN_DRAIN) {
		if (gpio_config_prop & AS3722_GPIO_CONFIG_PULL_UP)
			return AS3722_GPIO_MODE_IO_OPEN_DRAIN_PULL_UP;
		return AS3722_GPIO_MODE_IO_OPEN_DRAIN;
	}
	if (input) {
		if (gpio_config_prop & AS3722_GPIO_CONFIG_PULL_UP)
			return AS3722_GPIO_MODE_INPUT_PULL_UP;
		else if (gpio_config_prop & AS3722_GPIO_CONFIG_PULL_DOWN)
			return AS3722_GPIO_MODE_INPUT_PULL_DOWN;
		return AS3722_GPIO_MODE_INPUT;
	}
	if (gpio_config_prop & AS3722_GPIO_CONFIG_PULL_DOWN)
		return AS3722_GPIO_MODE_OUTPUT_VDDL;
	return AS3722_GPIO_MODE_OUTPUT_VDDH;
}

static int as3722_pinctrl_gpio_request_enable(struct pinctrl_dev *pctldev,
		struct pinctrl_gpio_range *range, unsigned offset)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);

	if (as_pci->gpio_control[offset].io_function)
		return -EBUSY;
	return 0;
}

static int as3722_pinctrl_gpio_set_direction(struct pinctrl_dev *pctldev,
		struct pinctrl_gpio_range *range, unsigned offset, bool input)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);
	struct as3722 *as3722 = as_pci->as3722;
	int mode;
	int ret;

	mode = as3722_pinctrl_gpio_get_mode(
			as_pci->gpio_control[offset].config_prop, input);
	if (mode < 0) {
		dev_err(as_pci->dev, "%s direction for GPIO %d not supported\n",
			(input) ? "Input" : "Output", offset);
		return mode;
	}

	ret = as3722_update_bits(as3722, AS3722_GPIOn_CONTROL_REG(offset),
				AS3722_GPIO_MODE_MASK, mode);
	if (ret < 0)
		return ret;

	as_pci->gpio_control[offset].input = input;
	return 0;
}

static void as3722_gpio_set_value(struct as3722_pctrl_info *as_pci,
		unsigned offset, int value)
{
	struct as3722 *as3722 = as_pci->as3722;
	int en_invert = as_pci->gpio_control[offset].enable_gpio_invert;
	u32 val;
	int ret;

	if (value)
		val = (en_invert) ? 0 : AS3722_GPIOn_SIGNAL(offset);
	else
		val = (en_invert) ? AS3722_GPIOn_SIGNAL(offset) : 0;

	ret = as3722_update_bits(as3722, AS3722_GPIO_SIGNAL_OUT_REG,
			AS3722_GPIOn_SIGNAL(offset), val);
	if (ret < 0)
		dev_err(as_pci->dev,
			"GPIO_SIGNAL_OUT_REG update failed: %d\n", ret);
}

static const struct pinmux_ops as3722_pinmux_ops = {
	.get_functions_count	= as3722_pinctrl_get_funcs_count,
	.get_function_name	= as3722_pinctrl_get_func_name,
	.get_function_groups	= as3722_pinctrl_get_func_groups,
	.enable			= as3722_pinctrl_enable,
	.gpio_request_enable	= as3722_pinctrl_gpio_request_enable,
	.gpio_set_direction	= as3722_pinctrl_gpio_set_direction,
};

static int as3722_pinconf_get(struct pinctrl_dev *pctldev,
			unsigned pin, unsigned long *config)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	unsigned prop;
	int val = -1;
	int arg = 0;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		prop = AS3722_GPIO_CONFIG_PULL_UP |
			AS3722_GPIO_CONFIG_PULL_DOWN |
			AS3722_GPIO_CONFIG_HIGH_IMPED;
		val = 0;
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		prop = AS3722_GPIO_CONFIG_PULL_UP;
		break;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		prop = AS3722_GPIO_CONFIG_PULL_DOWN;
		break;

	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		prop = AS3722_GPIO_CONFIG_OPEN_DRAIN;
		break;

	case PIN_CONFIG_DRIVE_PUSH_PULL:
		prop = AS3722_GPIO_CONFIG_OPEN_DRAIN;
		val = 0;
		break;

	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		prop = AS3722_GPIO_CONFIG_HIGH_IMPED;
		break;

	default:
		dev_err(as_pci->dev, "Properties not supported\n");
		return -ENOTSUPP;
	}

	if ((as_pci->gpio_control[pin].config_prop & prop) == (val & prop))
		arg = 1;

	*config = pinconf_to_config_packed(param, (u16)arg);
	return 0;
}

static int as3722_pinconf_set(struct pinctrl_dev *pctldev,
		unsigned pin, unsigned long config)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(config);
	unsigned config_prop = as_pci->gpio_control[pin].config_prop;
	u16 param_val = pinconf_to_config_argument(config);
	bool input = as_pci->gpio_control[pin].input;
	int saved_config_prop = config_prop;
	int ret;

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_PIN_DEFAULT:
		break;

	case PIN_CONFIG_BIAS_DISABLE:
		config_prop &= ~(AS3722_GPIO_CONFIG_PULL_UP |
				AS3722_GPIO_CONFIG_PULL_DOWN |
				AS3722_GPIO_CONFIG_HIGH_IMPED);
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (param_val) {
			config_prop |= AS3722_GPIO_CONFIG_PULL_UP;
			config_prop &= ~(AS3722_GPIO_CONFIG_PULL_DOWN |
					AS3722_GPIO_CONFIG_HIGH_IMPED);
		} else {
			config_prop &= ~AS3722_GPIO_CONFIG_PULL_UP;
		}
		break;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (param_val) {
			config_prop |= AS3722_GPIO_CONFIG_PULL_DOWN;
			config_prop &= ~(AS3722_GPIO_CONFIG_PULL_UP |
					AS3722_GPIO_CONFIG_HIGH_IMPED);
		} else {
			config_prop &= ~AS3722_GPIO_CONFIG_PULL_DOWN;
		}
		break;

	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		config_prop |= AS3722_GPIO_CONFIG_HIGH_IMPED;
		config_prop &= ~(AS3722_GPIO_CONFIG_PULL_UP |
				AS3722_GPIO_CONFIG_PULL_DOWN);
		break;

	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		config_prop |= AS3722_GPIO_CONFIG_OPEN_DRAIN;
		break;

	case PIN_CONFIG_DRIVE_PUSH_PULL:
		config_prop &= ~AS3722_GPIO_CONFIG_OPEN_DRAIN;
		break;

	case PIN_CONFIG_OUTPUT:
		as3722_gpio_set_value(as_pci, pin, param_val);
		input = false;
		break;

	case PIN_CONFIG_INPUT_ENABLE:
		input = true;
		break;

	default:
		dev_err(as_pci->dev, "Properties not supported\n");
		return -ENOTSUPP;
	}

	as_pci->gpio_control[pin].config_prop = config_prop;
	ret = as3722_pinctrl_gpio_set_direction(pctldev, NULL, pin, input);
	if (ret < 0) {
		dev_err(as_pci->dev, "Not able to set direction\n");
		as_pci->gpio_control[pin].config_prop = saved_config_prop;
	}
	return ret;
}

static const struct pinconf_ops as3722_pinconf_ops = {
	.pin_config_get = as3722_pinconf_get,
	.pin_config_set = as3722_pinconf_set,
};

static struct pinctrl_desc as3722_pinctrl_desc = {
	.pctlops = &as3722_pinctrl_ops,
	.pmxops = &as3722_pinmux_ops,
	.confops = &as3722_pinconf_ops,
	.owner = THIS_MODULE,
};

static inline struct as3722_pctrl_info *to_as_pci(struct gpio_chip *chip)
{
	return container_of(chip, struct as3722_pctrl_info, gpio_chip);
}

static int as3722_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct as3722_pctrl_info *as_pci = to_as_pci(chip);
	struct as3722 *as3722 = as_pci->as3722;
	int ret;
	u32 reg;
	u32 control;
	u32 val;
	int mode;
	int invert_enable;

	ret = as3722_read(as3722, AS3722_GPIOn_CONTROL_REG(offset), &control);
	if (ret < 0) {
		dev_err(as_pci->dev,
			"GPIO_CONTROL%d_REG read failed: %d\n", offset, ret);
		return ret;
	}

	invert_enable = !!(control & AS3722_GPIO_INV);
	mode = control & AS3722_GPIO_MODE_MASK;
	switch (mode) {
	case AS3722_GPIO_MODE_INPUT:
	case AS3722_GPIO_MODE_INPUT_PULL_UP:
	case AS3722_GPIO_MODE_INPUT_PULL_DOWN:
	case AS3722_GPIO_MODE_IO_OPEN_DRAIN:
	case AS3722_GPIO_MODE_IO_OPEN_DRAIN_PULL_UP:
		reg = AS3722_GPIO_SIGNAL_IN_REG;
		break;
	case AS3722_GPIO_MODE_OUTPUT_VDDH:
	case AS3722_GPIO_MODE_OUTPUT_VDDL:
		reg = AS3722_GPIO_SIGNAL_OUT_REG;
		break;
	default:
		return -EINVAL;
	}

	ret = as3722_read(as3722, reg, &val);
	if (ret < 0) {
		dev_err(as_pci->dev,
			"GPIO_SIGNAL_IN_REG read failed: %d\n", ret);
		return ret;
	}

	val = !!(val & AS3722_GPIOn_SIGNAL(offset));
	return (invert_enable) ? !val : val;
}

static void as3722_gpio_set(struct gpio_chip *chip, unsigned offset,
		int value)
{
	struct as3722_pctrl_info *as_pci = to_as_pci(chip);

	as3722_gpio_set_value(as_pci, offset, value);
}

static int as3722_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	return pinctrl_gpio_direction_input(chip->base + offset);
}

static int as3722_gpio_direction_output(struct gpio_chip *chip,
		unsigned offset, int value)
{
	as3722_gpio_set(chip, offset, value);
	return pinctrl_gpio_direction_output(chip->base + offset);
}

static int as3722_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct as3722_pctrl_info *as_pci = to_as_pci(chip);

	return as3722_irq_get_virq(as_pci->as3722, offset);
}

static int as3722_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	return pinctrl_request_gpio(chip->base + offset);
}

static void as3722_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	pinctrl_free_gpio(chip->base + offset);
}

static const struct gpio_chip as3722_gpio_chip = {
	.label			= "as3722-gpio",
	.owner			= THIS_MODULE,
	.request		= as3722_gpio_request,
	.free			= as3722_gpio_free,
	.get			= as3722_gpio_get,
	.set			= as3722_gpio_set,
	.direction_input	= as3722_gpio_direction_input,
	.direction_output	= as3722_gpio_direction_output,
	.to_irq			= as3722_gpio_to_irq,
	.can_sleep		= 1,
	.ngpio			= AS3722_PIN_NUM,
	.base			= -1,
};

static int as3722_pinctrl_set_single_pin_config(
	struct as3722_pctrl_info *as_pci,
	struct as3722_pinctrl_platform_data *as_pdata)
{
	int pin_id;
	int group_nr;
	int param_val;
	int param;
	int ret = 0;
	int mux_opt;
	int i;
	unsigned long config;

	if (!as_pdata->pin) {
		dev_err(as_pci->dev, "No pin name\n");
		return -EINVAL;
	}

	pin_id = pin_get_from_name(as_pci->pctl, as_pdata->pin);
	if (pin_id < 0) {
		dev_err(as_pci->dev, " Pin %s not found\n", as_pdata->pin);
		return ret;
	}

	/* Configure bias pull */
	if (!as_pdata->prop_bias_pull)
		goto skip_bias_pull;

	if (!strcmp(as_pdata->prop_bias_pull, "pull-up"))
		param = PIN_CONFIG_BIAS_PULL_UP;
	else if (!strcmp(as_pdata->prop_bias_pull, "pull-down"))
		param = PIN_CONFIG_BIAS_PULL_DOWN;
	else if (!strcmp(as_pdata->prop_bias_pull, "normal"))
		param = PIN_CONFIG_BIAS_DISABLE;
	else {
		dev_err(as_pci->dev, "Unknown bias-pull setting %s\n",
			as_pdata->prop_bias_pull);
		goto skip_bias_pull;
	}
	config = pinconf_to_config_packed(param, 0);
	ret = as3722_pinconf_set(as_pci->pctl, pin_id, config);
	if (ret < 0) {
		dev_err(as_pci->dev, "bias-pull setting failed: %d\n", ret);
		return ret;
	}

skip_bias_pull:
	/* Configure open drain */
	if (!as_pdata->prop_open_drain)
		goto skip_open_drain;

	param = PIN_CONFIG_DRIVE_OPEN_DRAIN;
	if (!strcmp(as_pdata->prop_bias_pull, "enable"))
		param_val = 1;
	else
		param_val = 0;
	config = pinconf_to_config_packed(param, param_val);
	ret = as3722_pinconf_set(as_pci->pctl, pin_id, config);
	if (ret < 0) {
		dev_err(as_pci->dev, "Opendrain setting failed: %d\n", ret);
		return ret;
	}

skip_open_drain:
	/* Configure high impedance */
	if (!as_pdata->prop_high_impedance)
		goto skip_high_impedance;

	param = PIN_CONFIG_BIAS_HIGH_IMPEDANCE;
	if (!strcmp(as_pdata->prop_high_impedance, "enable"))
		param_val = 1;
	else
		param_val = 0;
	config = pinconf_to_config_packed(param, param_val);
	ret = as3722_pinconf_set(as_pci->pctl, pin_id, config);
	if (ret < 0) {
		dev_err(as_pci->dev, "hi-impedance setting failed: %d\n", ret);
		return ret;
	}

skip_high_impedance:
	/* Configure function */
	if (!as_pdata->function)
		goto skip_function;

	for (group_nr = 0; group_nr < as_pci->num_pin_groups; ++group_nr) {
		if (as_pci->pin_groups[group_nr].pins[0] == pin_id)
			break;
	}

	if (group_nr == as_pci->num_pin_groups) {
		dev_err(as_pci->dev,
			"Pinconf is not supported for pin-id %d\n", pin_id);
		return -ENOTSUPP;
	}

	mux_opt = -1;
	for (i = 0; i < as_pci->num_functions; ++i) {
		if (!strcmp(as_pdata->function, as_pci->functions[i].name)) {
			mux_opt = i;
			break;
		}
	}
	if (mux_opt < 0) {
		dev_err(as_pci->dev, "Pinmux function %s not supported\n",
			as_pdata->function);
		return -EINVAL;
	}


	if (!strcmp(as_pdata->function, "gpio") && as_pdata->prop_gpio_mode) {
		bool gpio_input = false;
		int gpio_val = 0;

		if (!strcmp(as_pdata->prop_gpio_mode, "input"))
			gpio_input = true;
		else if (!strcmp(as_pdata->prop_gpio_mode, "output-low"))
			gpio_val = 0;
		else if (!strcmp(as_pdata->prop_gpio_mode, "output-high"))
			gpio_val = 1;
		else {
			dev_err(as_pci->dev, "Invalid gpio mode %s\n",
				as_pdata->prop_gpio_mode);
			goto skip_gpio_config;
		}
		if (gpio_input)
			as3722_gpio_direction_input(&as_pci->gpio_chip, pin_id);
		else
			as3722_gpio_direction_output(&as_pci->gpio_chip, pin_id,
					gpio_val);
	}

skip_gpio_config:
	ret = as3722_pinctrl_enable(as_pci->pctl, mux_opt, group_nr);
	if (ret < 0) {
		dev_err(as_pci->dev,
			"Pinconf config for pin %s failed %d\n",
			as_pdata->pin, ret);
		return ret;
	}

skip_function:
	return ret;
}

static int as3722_pinctrl_probe(struct platform_device *pdev)
{
	struct as3722_pctrl_info *as_pci;
	struct as3722 *as3722 = dev_get_drvdata(pdev->dev.parent);
	struct as3722_platform_data *pdata = as3722->dev->platform_data;
	int ret;
	int tret;
	int i;

	as_pci = devm_kzalloc(&pdev->dev, sizeof(*as_pci), GFP_KERNEL);
	if (!as_pci)
		return -ENOMEM;

	as_pci->dev = &pdev->dev;
	as_pci->dev->of_node = pdev->dev.parent->of_node;
	as_pci->as3722 = as3722;
	platform_set_drvdata(pdev, as_pci);

	for (i = 0; i < ARRAY_SIZE(as3722_pingroups); ++i) {
		int gpio_cntr_reg = AS3722_GPIOn_CONTROL_REG(i);
		u32 val;

		ret = as3722_read(as3722, gpio_cntr_reg, &val);
		if (!ret)
			as_pci->gpio_control[i].enable_gpio_invert =
					!!(val & AS3722_GPIO_INV);
	}

	as_pci->pins = as3722_pins_desc;
	as_pci->num_pins = ARRAY_SIZE(as3722_pins_desc);
	as_pci->functions = as3722_pin_function;
	as_pci->num_functions = ARRAY_SIZE(as3722_pin_function);
	as_pci->pin_groups = as3722_pingroups;
	as_pci->num_pin_groups = ARRAY_SIZE(as3722_pingroups);
	as3722_pinctrl_desc.name = dev_name(&pdev->dev);
	as3722_pinctrl_desc.pins = as3722_pins_desc;
	as3722_pinctrl_desc.npins = ARRAY_SIZE(as3722_pins_desc);
	as_pci->pctl = pinctrl_register(&as3722_pinctrl_desc,
					&pdev->dev, as_pci);
	if (!as_pci->pctl) {
		dev_err(&pdev->dev, "Couldn't register pinctrl driver\n");
		return -EINVAL;
	}

	as_pci->gpio_chip = as3722_gpio_chip;
	if (pdata && pdata->gpio_base)
		as_pci->gpio_chip.base = pdata->gpio_base;
	as_pci->gpio_chip.dev = &pdev->dev;
	as_pci->gpio_chip.of_node = pdev->dev.parent->of_node;
	ret = gpiochip_add(&as_pci->gpio_chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "Couldn't register gpiochip, %d\n", ret);
		goto fail_chip_add;
	}

	ret = gpiochip_add_pin_range(&as_pci->gpio_chip, dev_name(&pdev->dev),
				0, 0, AS3722_PIN_NUM);
	if (ret < 0) {
		dev_err(&pdev->dev, "Couldn't add pin range, %d\n", ret);
		goto fail_range_add;
	}

	if (pdata) {
		struct as3722_pinctrl_platform_data *as_pdata;

		for (i = 0; i < pdata->num_pinctrl; ++i) {
			as_pdata = &pdata->pinctrl_pdata[i];
			ret = as3722_pinctrl_set_single_pin_config(as_pci,
					as_pdata);
			if (ret < 0)
				dev_warn(&pdev->dev,
					"Pin config of pin %s failed %d\n",
					as_pdata->pin, ret);
		}
	}

	return 0;

fail_range_add:
	tret = gpiochip_remove(&as_pci->gpio_chip);
	if (tret < 0)
		dev_warn(&pdev->dev, "Couldn't remove gpio chip, %d\n", tret);

fail_chip_add:
	pinctrl_unregister(as_pci->pctl);
	return ret;
}

static int as3722_pinctrl_remove(struct platform_device *pdev)
{
	struct as3722_pctrl_info *as_pci = platform_get_drvdata(pdev);
	int ret;

	ret = gpiochip_remove(&as_pci->gpio_chip);
	if (ret < 0)
		return ret;
	pinctrl_unregister(as_pci->pctl);
	return 0;
}

static struct of_device_id as3722_pinctrl_of_match[] = {
	{ .compatible = "ams,as3722-pinctrl", },
	{ },
};
MODULE_DEVICE_TABLE(of, as3722_pinctrl_of_match);

static struct platform_driver as3722_pinctrl_driver = {
	.driver = {
		.name = "as3722-pinctrl",
		.owner = THIS_MODULE,
		.of_match_table = as3722_pinctrl_of_match,
	},
	.probe = as3722_pinctrl_probe,
	.remove = as3722_pinctrl_remove,
};

static int __init as3722_pinctrl_init(void)
{
	return platform_driver_register(&as3722_pinctrl_driver);
}
subsys_initcall(as3722_pinctrl_init);

static void __exit as3722_pinctrl_exit(void)
{
	platform_driver_unregister(&as3722_pinctrl_driver);
}
module_exit(as3722_pinctrl_exit);


MODULE_ALIAS("platform:as3722-pinctrl");
MODULE_DESCRIPTION("AS3722 pin control and GPIO driver");
MODULE_AUTHOR("Laxman Dewangan<ldewangan@nvidia.com>");
MODULE_LICENSE("GPL v2");
