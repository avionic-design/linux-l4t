/*
 * Copyright (C) 2013 Avionic Design GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef MFD_SX8634_H
#define MFD_SX8634_H

#define I2C_IRQ_SRC 0x00
#define I2C_IRQ_SRC_MODE (1 << 0)
#define I2C_IRQ_SRC_COMPENSATION (1 << 1)
#define I2C_IRQ_SRC_BUTTONS (1 << 2)
#define I2C_IRQ_SRC_SLIDER (1 << 3)
#define I2C_IRQ_SRC_GPI (1 << 4)
#define I2C_IRQ_SRC_SPM (1 << 5)
#define I2C_IRQ_SRC_NVM (1 << 6)
#define I2C_IRQ_SRC_READY (1 << 7)

#define I2C_CAP_STAT_MSB 0x01
#define I2C_CAP_STAT_LSB 0x02
#define I2C_SLD_POS_MSB 0x03
#define I2C_SLD_POS_LSB 0x04
#define I2C_GPI_STAT 0x07
#define I2C_SPM_STAT 0x08
#define I2C_COMP_OP_MODE 0x09
#define I2C_GPO_CTRL 0x0a
#define I2C_GPP_PIN_ID 0x0b
#define I2C_GPP_INTENSITY 0x0c
#define I2C_SPM_CFG 0x0d
#define I2C_SPM_CFG_WRITE (0 << 3)
#define I2C_SPM_CFG_READ (1 << 3)
#define I2C_SPM_CFG_OFF (0 << 4)
#define I2C_SPM_CFG_ON (1 << 4)
#define I2C_SPM_BASE 0x0e
#define I2C_SPM_KEY_MSB 0xac
#define I2C_SPM_KEY_LSB 0xad
#define I2C_SOFT_RESET 0xb1

#define SPM_CFG 0x00
#define SPM_CAP_MODE_MISC 0x09

#define SPM_CAP_MODE(x) (((x) <= 3) ? 0x0c : (((x) <= 7) ? 0x0b : 0x0a))
#define SPM_CAP_MODE_SHIFT(x) (((x) & 3) * 2)
#define SPM_CAP_MODE_MASK 0x3
#define SPM_CAP_MODE_MASK_SHIFTED(x) \
	(SPM_CAP_MODE_MASK << SPM_CAP_MODE_SHIFT(x))

#define SPM_CAP_SENS(x) (0x0d + ((x) / 2))
#define SPM_CAP_SENS_MAX 0x7
#define SPM_CAP_SENS_SHIFT(x) (((x) & 1) ? 0 : 4)
#define SPM_CAP_SENS_MASK 0x7
#define SPM_CAP_SENS_MASK_SHIFTED(x) \
	(SPM_CAP_SENS_MASK << SPM_CAP_SENS_SHIFT(x))

#define SPM_CAP_THRESHOLD(x) (0x13 + (x))
#define SPM_CAP_THRESHOLD_MAX 0xff

#define SPM_BTN_CFG 0x21
#define SPM_BTN_CFG_TOUCH_DEBOUNCE_MASK 0x03
#define SPM_BTN_CFG_TOUCH_DEBOUNCE_SHIFT 0

#define SPM_BLOCK_SIZE 8
#define SPM_NUM_BLOCKS 16
#define SPM_SIZE (SPM_BLOCK_SIZE * SPM_NUM_BLOCKS)

struct platform_device;
struct notifier_block;

struct sx8634;
struct sx8634_touch_platform_data;

struct sx8634_platform_data {
	int id;
	int reset_gpio;
	struct sx8634_touch_platform_data *touch;
};

/* Helper for the cell implementations */
static inline struct sx8634 *cell_to_sx8634(struct platform_device *pdev)
{
	return dev_get_drvdata(pdev->dev.parent);
}

/* Access to the sx8634 must be locked */
void sx8634_lock(struct sx8634 *sx);
void sx8634_unlock(struct sx8634 *sx);

int sx8634_read_reg(struct sx8634 *sx, u8 reg);
int sx8634_write_reg(struct sx8634 *sx, u8 reg, u8 val);

ssize_t sx8634_spm_load(struct sx8634 *sx);
ssize_t sx8634_spm_sync(struct sx8634 *sx);

int sx8634_spm_read(struct sx8634 *sx, unsigned int offset, u8 *value);
int sx8634_spm_write(struct sx8634 *sx, unsigned int offset, u8 value);

int sx8634_register_notifier(struct sx8634 *sx, struct notifier_block *nb);
int sx8634_unregister_notifier(struct sx8634 *sx, struct notifier_block *nb);

#endif /* MFD_SX8634_H */
