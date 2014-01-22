/*
 * Copyright (C) 2011-2012 Avionic Design GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <linux/i2c/adnp.h>

#define GPIO_DDR(gpio) (0x00 << (gpio)->reg_shift)
#define GPIO_PLR(gpio) (0x01 << (gpio)->reg_shift)
#define GPIO_IER(gpio) (0x02 << (gpio)->reg_shift)
#define GPIO_ISR(gpio) (0x03 << (gpio)->reg_shift)
#define GPIO_PTR(gpio) (0x04 << (gpio)->reg_shift)

struct adnp {
	struct i2c_client *client;
	struct gpio_chip gpio;
	unsigned int reg_shift;

	struct mutex i2c_lock;
	int gpio_base;

#ifdef CONFIG_GPIO_ADNP_IRQ
	struct mutex irq_lock;
	int irq_base;

	u8 *irq_enable;
	u8 *irq_level;
	u8 *irq_none;
	u8 *irq_rise;
	u8 *irq_fall;
	u8 *irq_high;
	u8 *irq_low;
#endif
};

static inline struct adnp *to_adnp(struct gpio_chip *chip)
{
	return container_of(chip, struct adnp, gpio);
}

static int adnp_read(struct adnp *adnp, unsigned offset, uint8_t *value)
{
	int err;

	err = i2c_smbus_read_byte_data(adnp->client, offset);
	if (err < 0) {
		dev_err(adnp->gpio.dev, "%s failed: %d\n",
			"i2c_smbus_read_byte_data()", err);
		return err;
	}

	*value = err;
	return 0;
}

static int adnp_write(struct adnp *adnp, unsigned offset, uint8_t value)
{
	int err;

	err = i2c_smbus_write_byte_data(adnp->client, offset, value);
	if (err < 0) {
		dev_err(adnp->gpio.dev, "%s failed: %d\n",
			"i2c_smbus_write_byte_data()", err);
		return err;
	}

	return 0;
}

static int adnp_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct adnp *adnp = to_adnp(chip);
	unsigned int reg = offset >> adnp->reg_shift;
	unsigned int pos = offset & 7;
	u8 value;
	int err;

	err = adnp_read(adnp, GPIO_PLR(adnp) + reg, &value);
	if (err < 0)
		return err;

	return (value & BIT(pos)) ? 1 : 0;
}

static void __adnp_gpio_set(struct adnp *adnp, unsigned offset, int value)
{
	unsigned int reg = offset >> adnp->reg_shift;
	unsigned int pos = offset & 7;
	int err;
	u8 val;

	err = adnp_read(adnp, GPIO_PLR(adnp) + reg, &val);
	if (err < 0)
		return;

	if (value)
		val |= BIT(pos);
	else
		val &= ~BIT(pos);

	adnp_write(adnp, GPIO_PLR(adnp) + reg, val);
}

static void adnp_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct adnp *adnp = to_adnp(chip);

	mutex_lock(&adnp->i2c_lock);
	__adnp_gpio_set(adnp, offset, value);
	mutex_unlock(&adnp->i2c_lock);
}

static int adnp_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct adnp *adnp = to_adnp(chip);
	unsigned int reg = offset >> adnp->reg_shift;
	unsigned int pos = offset & 7;
	u8 value;
	int err;

	mutex_lock(&adnp->i2c_lock);

	err = adnp_read(adnp, GPIO_DDR(adnp) + reg, &value);
	if (err < 0)
		goto out;

	value &= ~BIT(pos);

	err = adnp_write(adnp, GPIO_DDR(adnp) + reg, value);
	if (err < 0)
		goto out;

	err = adnp_read(adnp, GPIO_DDR(adnp) + reg, &value);
	if (err < 0)
		goto out;

	if (err & BIT(pos))
		err = -EACCES;

	err = 0;

out:
	mutex_unlock(&adnp->i2c_lock);
	return err;
}

static int adnp_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
				      int value)
{
	struct adnp *adnp = to_adnp(chip);
	unsigned int reg = offset >> adnp->reg_shift;
	unsigned int pos = offset & 7;
	int err;
	u8 val;

	mutex_lock(&adnp->i2c_lock);

	err = adnp_read(adnp, GPIO_DDR(adnp) + reg, &val);
	if (err < 0)
		goto out;

	val |= BIT(pos);

	err = adnp_write(adnp, GPIO_DDR(adnp) + reg, val);
	if (err < 0)
		goto out;

	err = adnp_read(adnp, GPIO_DDR(adnp) + reg, &val);
	if (err < 0)
		goto out;

	if (!(val & BIT(pos))) {
		err = -EPERM;
		goto out;
	}

	__adnp_gpio_set(adnp, offset, value);
	err = 0;

out:
	mutex_unlock(&adnp->i2c_lock);
	return err;
}

#ifdef CONFIG_DEBUG_FS
static void adnp_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	struct adnp *adnp = to_adnp(chip);
	unsigned int num_regs = 1 << adnp->reg_shift, i, j;
	int err;

	for (i = 0; i < num_regs; i++) {
		u8 ddr, plr, ier, isr, ptr;

		mutex_lock(&adnp->i2c_lock);

		err = adnp_read(adnp, GPIO_DDR(adnp) + i, &ddr);
		if (err < 0) {
			mutex_unlock(&adnp->i2c_lock);
			return;
		}

		err = adnp_read(adnp, GPIO_PLR(adnp) + i, &plr);
		if (err < 0) {
			mutex_unlock(&adnp->i2c_lock);
			return;
		}

		err = adnp_read(adnp, GPIO_IER(adnp) + i, &ier);
		if (err < 0) {
			mutex_unlock(&adnp->i2c_lock);
			return;
		}

		err = adnp_read(adnp, GPIO_ISR(adnp) + i, &isr);
		if (err < 0) {
			mutex_unlock(&adnp->i2c_lock);
			return;
		}

		err = adnp_read(adnp, GPIO_PTR(adnp) + i, &ptr);
		if (err < 0) {
			mutex_unlock(&adnp->i2c_lock);
			return;
		}

		mutex_unlock(&adnp->i2c_lock);

		for (j = 0; j < 8; j++) {
			unsigned int bit = (i << adnp->reg_shift) + j;
			const char *direction = "input ";
			const char *level = "low     ";
			const char *interrupt = "disabled";
			const char *pending = "";

			if (ddr & BIT(j))
				direction = "output";

			if (ptr & BIT(j))
				level = "tristate";
			else if (plr & BIT(j))
				level = "high    ";

			if (ier & BIT(j))
				interrupt = "enabled ";

			if (isr & BIT(j))
				pending = "pending";

			seq_printf(s, "%2u: %s %s IRQ %s %s\n", bit,
				   direction, level, interrupt, pending);
		}
	}
}
#else
#define adnp_gpio_dbg_show NULL
#endif

static int adnp_gpio_setup(struct adnp *adnp, unsigned int num_gpios)
{
	struct gpio_chip *chip = &adnp->gpio;

	adnp->reg_shift = get_count_order(num_gpios) - 3;

	chip->direction_input = adnp_gpio_direction_input;
	chip->direction_output = adnp_gpio_direction_output;
	chip->get = adnp_gpio_get;
	chip->set = adnp_gpio_set;
	chip->can_sleep = 1;

	chip->dbg_show = adnp_gpio_dbg_show;

	chip->base = adnp->gpio_base;
	chip->ngpio = num_gpios;
	chip->label = adnp->client->name;
	chip->dev = &adnp->client->dev;
	chip->owner = THIS_MODULE;

	return 0;
}

#ifdef CONFIG_GPIO_ADNP_IRQ
static irqreturn_t adnp_irq(int irq, void *data)
{
	struct adnp *adnp = data;
	unsigned int num_regs, i;

	num_regs = 1 << adnp->reg_shift;

	for (i = 0; i < num_regs; i++) {
		unsigned int base = i << adnp->reg_shift, bit;
		u8 changed, level, isr, ier;
		unsigned long pending;
		int err;

		mutex_lock(&adnp->i2c_lock);

		err = adnp_read(adnp, GPIO_PLR(adnp) + i, &level);
		if (err < 0) {
			mutex_unlock(&adnp->i2c_lock);
			continue;
		}

		err = adnp_read(adnp, GPIO_ISR(adnp) + i, &isr);
		if (err < 0) {
			mutex_unlock(&adnp->i2c_lock);
			continue;
		}

		err = adnp_read(adnp, GPIO_IER(adnp) + i, &ier);
		if (err < 0) {
			mutex_unlock(&adnp->i2c_lock);
			continue;
		}

		mutex_unlock(&adnp->i2c_lock);

		/* determine pins that changed levels */
		changed = level ^ adnp->irq_level[i];
		adnp->irq_level[i] = level;

		/* compute edge-triggered interrupts */
		pending = changed & ((adnp->irq_fall[i] & ~level) |
				     (adnp->irq_rise[i] & level));

		/* add in level-triggered interrupts */
		pending |= (adnp->irq_high[i] & level) |
			   (adnp->irq_low[i] & ~level);

		/*
		 * Always consider interrupts of unspecified type. They may
		 * trigger on any of the above condition and will be masked
		 * out by ISR and IER masks below if they are not actually
		 * pending or enabled.
		 */
		pending |= adnp->irq_none[i];

		/* mask out non-pending and disabled interrupts */
		pending &= isr & ier;

		for_each_set_bit(bit, &pending, 8)
			handle_nested_irq(adnp->irq_base + base + bit);
	}

	return IRQ_HANDLED;
}

static int adnp_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct adnp *adnp = to_adnp(chip);
	return adnp->irq_base + offset;
}

static unsigned int adnp_irq_startup(struct irq_data *data)
{
	struct adnp *adnp = irq_data_get_irq_chip_data(data);
	unsigned int irq = data->irq - adnp->irq_base;
	unsigned int reg = irq >> adnp->reg_shift;
	unsigned int pos = irq & 7;

	adnp->irq_none[reg] |= BIT(pos);
	adnp->irq_enable[reg] |= BIT(pos);

	return 0;
}

static void adnp_irq_shutdown(struct irq_data *data)
{
	struct adnp *adnp = irq_data_get_irq_chip_data(data);
	unsigned int irq = data->irq - adnp->irq_base;
	unsigned int reg = irq >> adnp->reg_shift;
	unsigned int pos = irq & 7;

	adnp->irq_enable[reg] &= ~BIT(pos);
	adnp->irq_none[reg] &= ~BIT(pos);
}

static void adnp_irq_mask(struct irq_data *data)
{
	struct adnp *adnp = irq_data_get_irq_chip_data(data);
	unsigned int irq = data->irq - adnp->irq_base;
	unsigned int reg = irq >> adnp->reg_shift;
	unsigned int pos = irq & 7;

	adnp->irq_enable[reg] &= ~BIT(pos);
}

static void adnp_irq_unmask(struct irq_data *data)
{
	struct adnp *adnp = irq_data_get_irq_chip_data(data);
	unsigned int irq = data->irq - adnp->irq_base;
	unsigned int reg = irq >> adnp->reg_shift;
	unsigned int pos = irq & 7;

	adnp->irq_enable[reg] |= BIT(pos);
}

static int adnp_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct adnp *adnp = irq_data_get_irq_chip_data(data);
	unsigned int irq = data->irq - adnp->irq_base;
	unsigned int reg = irq >> adnp->reg_shift;
	unsigned int pos = irq & 7;

	if (type & IRQ_TYPE_EDGE_RISING)
		adnp->irq_rise[reg] |= BIT(pos);
	else
		adnp->irq_rise[reg] &= ~BIT(pos);

	if (type & IRQ_TYPE_EDGE_FALLING)
		adnp->irq_fall[reg] |= BIT(pos);
	else
		adnp->irq_fall[reg] &= ~BIT(pos);

	if (type & IRQ_TYPE_LEVEL_HIGH)
		adnp->irq_high[reg] |= BIT(pos);
	else
		adnp->irq_high[reg] &= ~BIT(pos);

	if (type & IRQ_TYPE_LEVEL_LOW)
		adnp->irq_low[reg] |= BIT(pos);
	else
		adnp->irq_low[reg] &= ~BIT(pos);

	adnp->irq_none[reg] &= ~BIT(pos);

	return 0;
}

static void adnp_irq_bus_lock(struct irq_data *data)
{
	struct adnp *adnp = irq_data_get_irq_chip_data(data);

	mutex_lock(&adnp->irq_lock);
}

static void adnp_irq_bus_unlock(struct irq_data *data)
{
	struct adnp *adnp = irq_data_get_irq_chip_data(data);
	unsigned int num_regs = 1 << adnp->reg_shift, i;

	mutex_lock(&adnp->i2c_lock);

	for (i = 0; i < num_regs; i++)
		adnp_write(adnp, GPIO_IER(adnp) + i, adnp->irq_enable[i]);

	mutex_unlock(&adnp->i2c_lock);
	mutex_unlock(&adnp->irq_lock);
}

static struct irq_chip adnp_irq_chip = {
	.name = "gpio-adnp",
	.irq_startup = adnp_irq_startup,
	.irq_shutdown = adnp_irq_shutdown,
	.irq_mask = adnp_irq_mask,
	.irq_unmask = adnp_irq_unmask,
	.irq_set_type = adnp_irq_set_type,
	.irq_bus_lock = adnp_irq_bus_lock,
	.irq_bus_sync_unlock = adnp_irq_bus_unlock,
};

static int adnp_irq_setup(struct adnp *adnp)
{
	unsigned int num_regs = 1 << adnp->reg_shift, i;
	struct gpio_chip *chip = &adnp->gpio;
	int err;

	mutex_init(&adnp->irq_lock);

	/*
	 * Allocate memory to keep track of the current level and trigger
	 * modes of the interrupts. To avoid multiple allocations, a single
	 * large buffer is allocated and pointers are setup to point at the
	 * corresponding offsets. For consistency, the layout of the buffer
	 * is chosen to match the register layout of the hardware in that
	 * each segment contains the corresponding bits for all interrupts.
	 */
	adnp->irq_enable = devm_kzalloc(chip->dev, num_regs * 7, GFP_KERNEL);
	if (!adnp->irq_enable)
		return -ENOMEM;

	adnp->irq_level = adnp->irq_enable + (num_regs * 1);
	adnp->irq_none = adnp->irq_enable + (num_regs * 2);
	adnp->irq_rise = adnp->irq_enable + (num_regs * 3);
	adnp->irq_fall = adnp->irq_enable + (num_regs * 4);
	adnp->irq_high = adnp->irq_enable + (num_regs * 5);
	adnp->irq_low = adnp->irq_enable + (num_regs * 6);

	for (i = 0; i < num_regs; i++) {
		/*
		 * Read the initial level of all pins to allow the emulation
		 * of edge triggered interrupts.
		 */
		err = adnp_read(adnp, GPIO_PLR(adnp) + i, &adnp->irq_level[i]);
		if (err < 0)
			return err;

		/* disable all interrupts */
		err = adnp_write(adnp, GPIO_IER(adnp) + i, 0);
		if (err < 0)
			return err;

		adnp->irq_enable[i] = 0x00;
	}

	err = irq_alloc_descs(adnp->irq_base, 0, adnp->gpio.ngpio, -1);
	if (err < 0) {
		dev_err(chip->dev, "%s failed: %d\n", "irq_alloc_descs()",
			err);
		return err;
	}

	adnp->irq_base = err;

	for (i = 0; i < adnp->gpio.ngpio; i++) {
		int irq = adnp->irq_base + i;

		irq_clear_status_flags(irq, IRQ_NOREQUEST);
		irq_set_chip_data(irq, adnp);
		irq_set_chip(irq, &adnp_irq_chip);
		irq_set_nested_thread(irq, true);
#ifdef CONFIG_ARM
		set_irq_flags(irq, IRQF_VALID);
#else
		irq_set_noprobe(irq);
#endif
	}

	err = request_threaded_irq(adnp->client->irq, NULL, adnp_irq,
				   IRQF_TRIGGER_RISING | IRQF_ONESHOT,
				   dev_name(chip->dev), adnp);
	if (err != 0) {
		dev_err(chip->dev, "can't request IRQ#%d: %d\n",
			adnp->client->irq, err);
		return err;
	}

	adnp->gpio.to_irq = adnp_gpio_to_irq;
	return 0;
}

static void adnp_irq_teardown(struct adnp *adnp)
{
	irq_free_descs(adnp->irq_base, adnp->gpio.ngpio);
	if (adnp->gpio.to_irq)
		free_irq(adnp->client->irq, adnp);
}
#else
static int adnp_irq_setup(struct adnp *adnp)
{
	return 0;
}

static void adnp_irq_teardown(struct adnp *adnp)
{
}
#endif

#ifdef CONFIG_GPIO_ADNP_MACHXO
static int machxo_cmd(struct i2c_client *client,
		u8 *cmd_buf, int cmd_len,
		u8 *data, int data_len)
{
	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.flags = client->flags & I2C_M_TEN,
			.len = cmd_len,
			.buf = cmd_buf,
		},{
			.addr = client->addr,
			.flags = (client->flags & I2C_M_TEN) | I2C_M_RD,
			.len = data_len,
			.buf = data,
		}
	};
	int err, msg_cnt = 1 + (data && data_len > 0);

	err = i2c_transfer(client->adapter, msgs, msg_cnt);
	if (err < 0)
		return err;
	if (err != msg_cnt)
		return -EIO;

	return 0;
}

static int machxo_read_reg(struct i2c_client *client, u8 addr, u32 *val)
{
	u8 cmd[] = { addr, 0x00, 0x00, 0x00 };
	int err;

	err = machxo_cmd(client, cmd, sizeof(cmd), (u8 *)val, sizeof(*val));
	if (!err)
		*val = be32_to_cpu(*val);

	return err;
}

static int machxo_enable_cfg_if_transparent(struct i2c_client *client)
{
	u8 cmd[] = { 0x74, 0x08, 0x00, 0x00 };
	int err;

	err = machxo_cmd(client, cmd, sizeof(cmd), NULL, 0);
	if (err)
		return err;

	usleep_range(10, 20000);
	return 0;
}

static int machxo_disable_cfg_if(struct i2c_client *client)
{
	u8 disable_cmd[] = { 0x26, 0x00, 0x00 };
	u8 bypass_cmd[] = { 0xff, 0xff, 0xff, 0xff };
	int err;

	err = machxo_cmd(client, disable_cmd, sizeof(disable_cmd), NULL, 0);
	if (err)
		return err;

	return machxo_cmd(client, bypass_cmd, sizeof(bypass_cmd), NULL, 0);
}

static int machxo_read_devid(struct i2c_client *client, u32 *usercode)
{
	return machxo_read_reg(client, 0xe0, usercode);
}

static int machxo_read_traceid(struct i2c_client *client, u32 *usercode)
{
	return machxo_read_reg(client, 0x19, usercode);
}

static int machxo_read_usercode(struct i2c_client *client, u32 *usercode)
{
	return machxo_read_reg(client, 0xc0, usercode);
}

static int adnp_check_machxo(struct i2c_client *client)
{
	struct adnp_platform_data *pdata = client->dev.platform_data;
	struct i2c_client *cpld;
	u32 devid, traceid, cfg_usercode, sram_usercode;
	int err;

	/* Only run if there is a MachXO check function */
	if (pdata->machxo_check == NULL)
		return 0;

	cpld = i2c_new_dummy(client->adapter, client->addr - 1);
	if (!cpld)
		return -ENOMEM;

	/* Make sure the CFG interface is disabled */
	err = machxo_disable_cfg_if(cpld);
	if (err) {
		dev_err(&client->dev,
			"Failed to disable CFG interface: %d\n", err);
		return err;
	}

	/* Get the device ID */
	err = machxo_read_devid(cpld, &devid);
	if (err) {
		dev_err(&client->dev,
			"Failed to read MachXO device ID: %d\n", err);
		goto unregister_device;
	}
	dev_info(&client->dev, "MachXO device ID: %08x\n", devid);

	/* Get the trace ID */
	err = machxo_read_traceid(cpld, &traceid);
	if (err) {
		dev_err(&client->dev,
			"Failed to read MachXO device ID: %d\n", err);
		goto unregister_device;
	}
	dev_info(&client->dev, "MachXO trace ID: %08x\n", traceid);

	/* Then the SRAM usercode */
	err = machxo_read_usercode(cpld, &sram_usercode);
	if (err) {
		dev_err(&client->dev,
			"Failed to read MachXO SRAM user code: %d\n", err);
		goto unregister_device;
	}
	dev_info(&client->dev, "MachXO SRAM usercode: %08x\n", sram_usercode);

	/* Enable the config interface to read the CFG usercode */
	err = machxo_enable_cfg_if_transparent(cpld);
	if (err) {
		dev_err(&client->dev,
			"Failed to enable CFG interface: %d\n", err);
		goto unregister_device;
	}

	/* Read the CFG usercode */
	err = machxo_read_usercode(cpld, &cfg_usercode);
	if (!err)
		err = machxo_read_usercode(cpld, &cfg_usercode);
	if (err) {
		dev_err(&client->dev,
			"Failed to read MachXO CFG user code: %d\n", err);
		goto disable_cfg_if;
	}
	dev_info(&client->dev, "MachXO CFG usercode: %08x\n", cfg_usercode);

disable_cfg_if:
	err = machxo_disable_cfg_if(cpld);
	if (err)
		dev_err(&client->dev,
			"Failed to disable CFG interface: %d\n", err);

	if (!err)
		err = pdata->machxo_check(devid, traceid,
					sram_usercode, cfg_usercode);

unregister_device:
	i2c_unregister_device(cpld);
	return err;
}
#else
static int adnp_check_machxo(struct i2c_client *client)
{
	return 0;
}
#endif

static __devinit int adnp_i2c_probe(struct i2c_client *client,
				    const struct i2c_device_id *id)
{
	struct adnp_platform_data *pdata = client->dev.platform_data;
	struct adnp *adnp;
	int err;

	err = adnp_check_machxo(client);
	if (err)
		return err;

	adnp = kzalloc(sizeof(*adnp), GFP_KERNEL);
	if (!adnp)
		return -ENOMEM;

	if (!pdata)
		return -ENODEV;

	adnp->gpio_base = pdata->gpio_base;
	adnp->irq_base = pdata->irq_base;

	mutex_init(&adnp->i2c_lock);
	adnp->client = client;

	err = adnp_gpio_setup(adnp, pdata->nr_gpios);
	if (err < 0)
		goto gpio_failed;

	err = adnp_irq_setup(adnp);
	if (err < 0)
		goto irq_failed;

	err = gpiochip_add(&adnp->gpio);
	if (err < 0)
		goto irq_failed;

	i2c_set_clientdata(client, adnp);
	return 0;

irq_failed:
	adnp_irq_teardown(adnp);
gpio_failed:
	kfree(adnp);
	return err;
}

static __devexit int adnp_i2c_remove(struct i2c_client *client)
{
	struct adnp *adnp = i2c_get_clientdata(client);
	int err;

	err = gpiochip_remove(&adnp->gpio);
	if (err < 0) {
		dev_err(&client->dev, "%s failed: %d\n", "gpiochip_remove()",
			err);
		return err;
	}

	adnp_irq_teardown(adnp);
	kfree(adnp);
	return 0;
}

static const struct i2c_device_id adnp_i2c_id[] = {
	{ "gpio-adnp" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adnp_i2c_id);

static struct i2c_driver adnp_i2c_driver = {
	.driver = {
		.name = "gpio-adnp",
		.owner = THIS_MODULE,
	},
	.probe = adnp_i2c_probe,
	.remove = __devexit_p(adnp_i2c_remove),
	.id_table = adnp_i2c_id,
};

static int __init adnp_init(void)
{
	int ret = 0;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&adnp_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register %s I2C driver: %d\n",
				adnp_i2c_driver.driver.name, ret);
	}
#endif
	return ret;
}
module_init(adnp_init);

static void __exit adnp_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&adnp_i2c_driver);
#endif
}
module_exit(adnp_exit);

MODULE_DESCRIPTION("Avionic Design N-bit GPIO expander");
MODULE_AUTHOR("Thierry Reding <thierry.reding@avionic-design.de>");
MODULE_LICENSE("GPL");
