/*
 * Driver for MT9V126 CMOS Image Sensor from Aptina
 *
 * Copyright (C) 2010-2013, Alban Bedel <alban.bedel@avionic-design.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/videodev2.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/log2.h>
#include <stdarg.h>

#include <media/v4l2-device.h>
#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>
#include <media/soc_camera.h>
#include <linux/mt9v126.h>

#include "mt9v126_driver.h"

#define REG_ADDR_MSG(cl, r)		\
	{				\
		.addr = (cl)->addr,	\
		.flags = 0,		\
		.len = sizeof(r),	\
		.buf = (u8 *)&(r)	\
	}

struct mt9v126 {
	struct v4l2_subdev sd;
	int model;
	int reset_gpio;
	int progressive;
};

#define HOST_CMD_TYPE_NONE		0x0000
#define HOST_CMD_TYPE_U8		0x0111
#define HOST_CMD_TYPE_S8		0x0211
#define HOST_CMD_TYPE_U16		0x0112
#define HOST_CMD_TYPE_S16		0x0212
#define HOST_CMD_TYPE_U32		0x0114
#define HOST_CMD_TYPE_S32		0x0214

#define HOST_CMD_TYPE_SIZE(type)	((type) & 0xF)
#define HOST_CMD_TYPE_IS_INT(type)	((type) & 0x10)

#define HOST_CMD_ARG_NONE \
	{ HOST_CMD_TYPE_NONE }
#define HOST_CMD_ARG_U8(val) \
	{ HOST_CMD_TYPE_U8, { .int_u8 = val } }
#define HOST_CMD_ARG_S8(val) \
	{ HOST_CMD_TYPE_S8, { .int_s8 = val } }
#define HOST_CMD_ARG_U16(val) \
	{ HOST_CMD_TYPE_U16, { .int_u16 = cpu_to_be16(val) } }
#define HOST_CMD_ARG_S16(val) \
	{ HOST_CMD_TYPE_S16, { .int_s16 = cpu_to_be16(val) } }
#define HOST_CMD_ARG_U32(val) \
	{ HOST_CMD_TYPE_U32, { .int_u32 = cpu_to_be32(val) } }
#define HOST_CMD_ARG_S32(val) \
	{ HOST_CMD_TYPE_S32, { .int_s32 = cpu_to_be32(val) } }

struct host_cmd_arg {
	unsigned	type;
	union {
		u8	int_u8;
		s8	int_s8;
		u16	int_u16;
		s16	int_s16;
		u32	int_u32;
		s32	int_s32;
	};
};

static int flicker_frequency = 50;
module_param(flicker_frequency, int, 0644);
MODULE_PARM_DESC(flicker_frequency,
		"Set the ambient light flicker frequency");

static enum v4l2_mbus_pixelcode mt9v126_mbus_fmt[] = {
	V4L2_MBUS_FMT_UYVY8_2X8,
};

static const struct v4l2_queryctrl mt9v126_controls[] = {
	{
		.id = V4L_CID_MT9V126_INV_BRIGHTNESS_METRIC,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Inverted Brightness metric",
		.minimum = 0,
		.maximum = 65535,
		.step = 1,
		.default_value = 0,
		.flags = V4L2_CTRL_FLAG_READ_ONLY,
	},
	{
		.id = V4L_CID_MT9V126_GAIN_METRIC,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Gain metric",
		.minimum = 0,
		.maximum = 65535,
		.step = 1,
		.default_value = 0,
		.flags = V4L2_CTRL_FLAG_READ_ONLY,
	},
};

static inline struct mt9v126 *to_mt9v126(struct v4l2_subdev *sd)
{
	return container_of(sd, struct mt9v126, sd);
}

static int regs_write(struct i2c_client *client, const u16 start,
		const void *data, unsigned len)
{
	u8 buffer[2 + len];
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = len + 2,
			.buf = buffer
		}
	};
	int err;

	((u16 *)buffer)[0] = cpu_to_be16(start);
	memcpy(buffer + 2, data, len);

	err = i2c_transfer(client->adapter, msg, 1);
	if (err != 1)
		dev_err(&client->dev, "Write reg %04x+%d: FAILED (%x)!\n",
			start, len, err);

	return err < 0 ? err : (err == 1 ? 0 : -EIO);
}

static int regs_read(struct i2c_client *client, const u16 start,
		void *data, unsigned len)
{
	const u16 dev_start = cpu_to_be16(start);
	struct i2c_msg msg[] = {
		REG_ADDR_MSG(client, dev_start),
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = data
		}
	};
	int err;

	err = i2c_transfer(client->adapter, msg, 2);
	if (err != 2)
		dev_err(&client->dev, "Read reg %04x+%d: FAILED (%x)!\n",
			start, len, err);

	return err < 0 ? err : (err == 2 ? 0 : -EIO);
}

static int reg_read(struct i2c_client *client, const u16 reg, u16 *value)
{
	u16 dev_value = cpu_to_be16(0xDEAD);
	int err;

	err = regs_read(client, reg, &dev_value, sizeof(dev_value));
	if (err < 0)
		return err;

	*value = be16_to_cpu(dev_value);
	return 0;
}

static int reg_write(struct i2c_client *client, const u16 reg, const u16 data)
{
	u16 dev_data = cpu_to_be16(data);
	return regs_write(client, reg, &dev_data, sizeof(dev_data));
}

static int reg_set(struct i2c_client *client, const u16 reg,
		const u16 mask, const u16 data)
{
	u16 val = 0xDEAD;
	int err;

	err = reg_read(client, reg, &val);
	if (err)
		return err;

	val &= ~mask;
	val |= (data & mask);

	return reg_write(client, reg, val);
}

static int reg_writev(struct i2c_client *client, ...)
{
	int reg, val, err = 0;
	va_list args;

	va_start(args, client);
	while (1) {
		reg = va_arg(args, int);
		if (reg < 0)
			break;
		val = va_arg(args, int);
		err = reg_write(client, reg, val);
		if (err)
			break;
	}
	va_end(args);

	return err;
}

static int encode_cmd_arg(const struct host_cmd_arg *arg,
			void *dst, unsigned len)
{
	unsigned size = HOST_CMD_TYPE_SIZE(arg->type);
	if (size > len)
		return -ENOMEM;
	memcpy(dst, &arg->int_u8, size);
	return size;
}

static int mt9v126_errno(int err)
{
	switch (err) {
	case MT9V126_ENOERR:
		return 0;
	case MT9V126_ENOENT:
		return -ENOENT;
	case MT9V126_EINTR:
		return -EINTR;
	case MT9V126_EIO:
		return -EIO;
	case MT9V126_E2BIG:
		return -E2BIG;
	case MT9V126_EBADF:
		return -EBADF;
	case MT9V126_EAGAIN:
		return -EAGAIN;
	case MT9V126_ENOMEM:
		return -ENOMEM;
	case MT9V126_EACCESS:
		return -EACCES;
	case MT9V126_EBUSY:
		return -EBUSY;
	case MT9V126_EEXIST:
		return -EEXIST;
	case MT9V126_ENODEV:
		return -ENODEV;
	case MT9V126_EINVAL:
		return -EINVAL;
	case MT9V126_ENOSPC:
		return -ENOSPC;
	case MT9V126_ERANGE:
		return -ERANGE;
	case MT9V126_ENOSYS:
		return -ENOSYS;
	case MT9V126_EALREADY:
		return -EALREADY;
	}
	return -EINVAL;
}

static int wait_for_no_doorbell(struct i2c_client *client, u16* cmd_ret)
{
	int ret, try = 100;
	u16 cmd;

	while (try > 0) {
		ret = reg_read(client, MT9V126_COMMAND_REGISTER, &cmd);
		if (ret)
			return ret;

		if (!(cmd & MT9V126_COMMAND_DOORBELL)) {
			if (cmd_ret)
				*cmd_ret = cmd;
			return 0;
		}

		dev_dbg(&client->dev, "Waiting for doorbell!\n");
		usleep_range(1000, 10000);
		try -= 1;
	}

	return -EBUSY;
}

static int host_cmd(struct i2c_client *client, u16 cmd,
		const struct host_cmd_arg *args)
{
	u8 args_data[MT9V126_PARAMS_POOL_SIZE];
	unsigned args_data_len = 0;
	int ret, i, try = 10;

	ret = wait_for_no_doorbell(client, NULL);
	if (ret)
		return ret;

	/* Encode the arguments */
	for (i = 0 ;
	     args && args[i].type != HOST_CMD_TYPE_NONE ;
	     i++) {
		ret = encode_cmd_arg(args + i,
				args_data + args_data_len,
				sizeof(args_data) - args_data_len);
		if (ret < 0)
			return ret;
		args_data_len += ret;
	}

	/* Send the arguments */
	if (args_data_len > 0) {
		/* Align on 16 bits */
		if (args_data_len & 1) {
			args_data[args_data_len] = 0;
			args_data_len += 1;
		}
		ret = regs_write(client,
				MT9V126_CMD_HANDLER_PARAMS_POOL_BASE,
				args_data, args_data_len);
		if (ret)
			return ret;
	}

	/* Then send the command */
	while (try > 0) {
		u16 cmd_ret = MT9V126_EINVAL;

		ret = reg_write(client, MT9V126_COMMAND_REGISTER, cmd);
		if (ret)
			return ret;

		ret = wait_for_no_doorbell(client, &cmd_ret);
		if (ret)
			return ret;

		if (cmd_ret != MT9V126_EAGAIN)
			return mt9v126_errno(cmd_ret);

		/* If the SoC wasn't finished yet retry a bit later */
		usleep_range(1000, 10000);
		try -= 1;
	}

	/* Retry exhausted */
	return -EAGAIN;
}

static int mt9v126_hard_reset(struct v4l2_subdev *sd)
{
	struct mt9v126 *mt9v126 = to_mt9v126(sd);

	if (mt9v126->reset_gpio < 0)
		return -ENODEV;

	gpio_set_value_cansleep(mt9v126->reset_gpio, 0);
	usleep_range(1000, 10000);
	gpio_set_value_cansleep(mt9v126->reset_gpio, 1);
	msleep(MT9V126_INTERNAL_INIT_TIME);

	return 0;
}

static int mt9v126_soft_reset(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	return reg_set(client, MT9V126_RESET_REGISTER, 0x0003, 3);
}

static int mt9v126_soft_restart(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	return reg_write(client, MT9V126_SYS_REFRESH_MASK, 3);
}

static int mt9v126_get_applied_patches(struct v4l2_subdev *sd,
				u16 *patches,
				int *patches_count)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct host_cmd_arg args[] = {
		HOST_CMD_ARG_U8(0),
		HOST_CMD_ARG_NONE
	};
	int max_patches = *patches_count;
	int num_patches = 0;
	int err;

	while (num_patches < max_patches) {
		args[0].int_u8 = num_patches;
		err = host_cmd(client,
			MT9V126_CMD_PATCHLDR_PATCH_INFO, args);
		if (err == -ERANGE)
			break;
		if (err) {
			dev_err(&client->dev, "Failed to get info for patch %d\n",
				num_patches);
			return err;
		}

		err = reg_read(client, MT9V126_CMD_HANDLER_PARAMS_POOL(2),
			patches + num_patches);
		if (err)
			return err;

		dev_info(&client->dev, "Patch %x is applied\n",
			patches[num_patches]);
		num_patches += 1;
	}

	*patches_count = num_patches;
	return 0;
}

static int mt9v126_apply_patch(struct v4l2_subdev *sd,
			const struct mt9v126_patch *patch)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	const struct host_cmd_arg args[] = {
		HOST_CMD_ARG_U16(patch->loader_addr),
		HOST_CMD_ARG_U16(patch->id),
		HOST_CMD_ARG_U32(patch->fw_version),
		HOST_CMD_ARG_U16(patch->code_size),
		HOST_CMD_ARG_NONE
	};
	u16 set_addr = ~patch->code_addr;
	u16 dst_addr = patch->code_addr;
	int err = 0, pos = 0, try = 10;

	/* Load the code */
	while (pos < patch->code_size) {
		int to_write = min(patch->code_size - pos, 16);

		/* Setup the DMA register destination */
		if ((set_addr & 0x8000) != (dst_addr & 0x8000)) {
			err = reg_write(client,
					MT9V126_ACCESS_CTL_STAT,
					(dst_addr >> 15) & 0x1);
			if (err)
				return err;
		}
		if ((set_addr & 0x7FFF) != (dst_addr & 0x7FFF)) {
			err = reg_write(client,
					MT9V126_PHYSICAL_ADDRESS_ACCESS,
					dst_addr & 0x7FFF);
			if (err)
				return err;
		}
		set_addr = dst_addr;

		/* Make sure we write an even amount of bytes */
		if (to_write & 1)
			to_write += 1;
		err = regs_write(client, MT9V126_MCU_VARIABLE_DATA_BASE,
				patch->code + pos, to_write);
		if (err)
			return err;

		pos += to_write;
		dst_addr += to_write;
	}

	/* Restore the default logical addressing */
	err = reg_write(client, MT9V126_LOGICAL_ADDRESS_ACCESS,
			(MT9V126_CMD_HANDLER_PARAMS_POOL_BASE & 0x7FFF));
	if (err)
		return err;

	/* Apply the patch */
	err = host_cmd(client, MT9V126_CMD_PATCHLDR_APPLY_PATCH, args);
	if (err)
		return err;

	/* Wait for the patch manager to be finished */
	while (try > 0) {
		err = host_cmd(client, MT9V126_CMD_PATCHLDR_STATUS, NULL);
		if (err != -EBUSY)
			return err;
		usleep_range(1000, 10000);
		try -= 1;
	}
	return err;
}

static int mt9v126_apply_patches(struct v4l2_subdev *sd)
{
	int num_applied_patches = MT9V126_PATCHLDR_MAX_PATCHES;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u16 applied_patches[MT9V126_PATCHLDR_MAX_PATCHES];
	int i, j, err = 0;

	err = mt9v126_get_applied_patches(sd, applied_patches,
					&num_applied_patches);
	dev_info(&client->dev, "%d patch already applied!\n",
		num_applied_patches);

	for (i = 0 ; err == 0 && mt9v126_patches[i] ; i++) {
		int apply = 1;
		for (j = 0 ; j < num_applied_patches ; j++)
			if (mt9v126_patches[i]->id == applied_patches[j]) {
				dev_info(&client->dev, "Skipping patch %x\n",
					mt9v126_patches[i]->id);
				apply = 0;
				break;
			}
		if (apply) {
			err = mt9v126_apply_patch(sd, mt9v126_patches[i]);
			dev_info(&client->dev, "Applied patch %x: %s\n",
				mt9v126_patches[i]->id, err ? "failed" : "ok");
		}
	}

	return err;
}

static int mt9v126_get_state(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int err = host_cmd(client, MT9V126_CMD_SYSMGR_GET_STATE, NULL);
	u16 state;

	if (!err)
		err = reg_read(client, MT9V126_CMD_HANDLER_PARAMS_POOL_BASE,
			&state);

	return err ? err : (state >> 8);
}

static int mt9v126_set_state(struct v4l2_subdev *sd, int state)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct host_cmd_arg args[] = {
		HOST_CMD_ARG_U8(state),
		HOST_CMD_ARG_NONE
	};
	return host_cmd(client, MT9V126_CMD_SYSMGR_SET_STATE, args);
}

static int mt9v126_switch_state(struct v4l2_subdev *sd, int new_state)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int err, state, next_state = -1, unknown_state = -1;

	while (1) {
		state = mt9v126_get_state(sd);
		if (state < 0)
			return state;
		if (state == new_state)
			return 0;

		switch (state) {
		case MT9V126_SYS_STATE_STANDBY:
			next_state = MT9V126_SYS_STATE_LEAVE_STANDBY;
			break;
		case MT9V126_SYS_STATE_SUSPENDED:
			switch (new_state) {
			case MT9V126_SYS_STATE_STANDBY:
				next_state =
					MT9V126_SYS_STATE_ENTER_STANDBY;
				break;
			case MT9V126_SYS_STATE_STREAMING:
				next_state =
					MT9V126_SYS_STATE_ENTER_STREAMING;
				break;
			}
			break;
		case MT9V126_SYS_STATE_STREAMING:
			switch (new_state) {
			case MT9V126_SYS_STATE_STANDBY:
				next_state =
					MT9V126_SYS_STATE_ENTER_STANDBY;
				break;
			case MT9V126_SYS_STATE_SUSPENDED:
				next_state =
					MT9V126_SYS_STATE_ENTER_SUSPEND;
				break;
			}
			break;
		default:
			if (state != unknown_state) {
				dev_err(&client->dev,
					"Got unknown state: %x\n", state);
				unknown_state = state;
			} else {
				dev_err(&client->dev,
					"Failed to leave unknown state %x\n",
					state);
				return -EINVAL;
			}
			switch (new_state) {
			case MT9V126_SYS_STATE_STANDBY:
				next_state =
					MT9V126_SYS_STATE_ENTER_STANDBY;
				break;
			case MT9V126_SYS_STATE_SUSPENDED:
				next_state =
					MT9V126_SYS_STATE_ENTER_SUSPEND;
				break;
			case MT9V126_SYS_STATE_STREAMING:
				next_state =
					MT9V126_SYS_STATE_ENTER_STREAMING;
				break;
			}
			break;
		}

		dev_err(&client->dev, "Switching from state %x to %x\n",
			state, next_state);
		err = mt9v126_set_state(sd, next_state);
		if (err) {
			dev_err(&client->dev,
				"Failed to switch from state %x to %x: %x\n",
				state, next_state, err);
			return err;
		}
	}
	return 0;
}

static int mt9v126_get_subsystem_state(struct v4l2_subdev *sd, int cmd,
				struct host_cmd_arg *ret)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 response[MT9V126_PARAMS_POOL_SIZE];
	int i, response_size = 0, pos = 0;
	int err;

	/* Compute the needed read size for the response */
	for (i = 0 ; ret && ret[i].type != HOST_CMD_TYPE_NONE ; i++)
		response_size += HOST_CMD_TYPE_SIZE(ret[i].type);
	if (response_size & 1)
		response_size += 1;
	if (response_size > sizeof(response))
		return -ENOMEM;

	/* Get the state */
	err = host_cmd(client, cmd, NULL);
	if (err)
		return err;
	err = regs_read(client, MT9V126_CMD_HANDLER_PARAMS_POOL_BASE,
			response, response_size);
	if (err)
		return err;

	/* Write back the results */
	for (i = 0 ; ret && ret[i].type != HOST_CMD_TYPE_NONE ; i++) {
		memcpy(&ret[i].int_u8, response + pos,
			HOST_CMD_TYPE_SIZE(ret[i].type));
		/* TODO: convert multibytes back to cpu order */
		pos += HOST_CMD_TYPE_SIZE(ret[i].type);
	}
	return 0;
}


static int mt9v126_get_dewarp_state(struct v4l2_subdev *sd,
				int *enabled, int *encoding,
				int *output_fmt, int *err_status)
{
	struct host_cmd_arg ret[] = {
		HOST_CMD_ARG_U8(0),
		HOST_CMD_ARG_U8(0),
		HOST_CMD_ARG_U8(0),
		HOST_CMD_ARG_U8(0),
		HOST_CMD_ARG_NONE
	};
	int err;

	err = mt9v126_get_subsystem_state(
		sd, MT9V126_CMD_DWRP_GET_STATE, ret);
	if (err)
		return err;

	if (enabled)
		*enabled = ret[0].int_u8;
	if (encoding)
		*encoding = ret[1].int_u8;
	if (output_fmt)
		*output_fmt = ret[2].int_u8;
	if (err_status)
		*err_status = ret[3].int_u8;

	return 0;
}

static int mt9v126_write_dewarp_config(struct v4l2_subdev *sd,
				int cfg_type, int offset,
				const u8 *data, int data_size)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct host_cmd_arg args[MT9V126_PARAMS_POOL_SIZE] = {
		HOST_CMD_ARG_U8(cfg_type),
		HOST_CMD_ARG_U8(data_size),
		HOST_CMD_ARG_U16(offset),
		HOST_CMD_ARG_NONE
	};
	int i;

	if (data_size > MT9V126_PARAMS_POOL_SIZE - 4)
		return -EINVAL;

	for (i = 0 ; i < data_size ; i++) {
		args[3 + i].int_u8 = data[i];
		args[3 + i].type = HOST_CMD_TYPE_U8;
	}
	args[3 + i].type = HOST_CMD_TYPE_NONE;

	return host_cmd(client, MT9V126_CMD_DWRP_WRITE_CONFIG, args);
}

static int mt9v126_apply_dewarp_config(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int err, try = 10;

	err = host_cmd(client, MT9V126_CMD_DWRP_APPLY_CONFIG, NULL);
	if (err)
		return err;

	while (try > 0) {
		err = host_cmd(client,
			MT9V126_CMD_DWRP_CONFIG_STATUS, NULL);
		if (err != -EBUSY)
			return err;
		usleep_range(1000, 10000);
		try -= 1;
	}

	return -EBUSY;
}

static int mt9v126_set_dewarp_config(struct v4l2_subdev *sd,
				const struct mt9v126_dewarp *cfg)
{
	int wrote = 0;
	/* Write the config out */
	while (wrote < cfg->size) {
		int to_write = min(cfg->size-wrote,
				MT9V126_PARAMS_POOL_SIZE - 4);
		int err = mt9v126_write_dewarp_config(
			sd, cfg->type, wrote, cfg->data + wrote, to_write);
		if (err)
			return err;
		wrote += to_write;
	}
	/* Apply it */
	return mt9v126_apply_dewarp_config(sd);
}

static int mt9v126_enable_dewarp(struct v4l2_subdev *sd, int enable,
				int encoding, int output_fmt)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int state = -EINVAL, retry = 10;
	struct host_cmd_arg args[] = {
		HOST_CMD_ARG_U8(enable),
		HOST_CMD_ARG_U8(encoding),
		HOST_CMD_ARG_U8(output_fmt),
		HOST_CMD_ARG_NONE
	};
	int err;

	err = host_cmd(client, MT9V126_CMD_DWRP_ENABLE, args);
	if (err)
		return err;

	while (retry > 0) {
		err = mt9v126_get_dewarp_state(
			sd, NULL, NULL, NULL, &state);
		if (err)
			return err;
		if (state != -EBUSY)
			return state;
		usleep_range(1000, 10000);
		retry -= 1;
	}

	return err;
}

static int mt9v126_get_overlay_state(struct v4l2_subdev *sd,
				int *enabled, int *input,
				int *mode, int *err_status)
{
	struct host_cmd_arg ret[] = {
		HOST_CMD_ARG_U8(0),
		HOST_CMD_ARG_U8(0),
		HOST_CMD_ARG_U8(0),
		HOST_CMD_ARG_U8(0),
		HOST_CMD_ARG_NONE
	};
	int err;

	err = mt9v126_get_subsystem_state(
		sd, MT9V126_CMD_OVRL_GET_STATE, ret);
	if (err)
		return err;

	if (enabled)
		*enabled = ret[0].int_u8;
	if (input)
		*input = ret[1].int_u8;
	if (mode)
		*mode = ret[2].int_u8;
	if (err_status)
		*err_status = ret[3].int_u8;

	return 0;
}

static int mt9v126_enable_overlay(struct v4l2_subdev *sd, int enable,
				int input, int mode)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct host_cmd_arg args[] = {
		HOST_CMD_ARG_U8(enable),
		HOST_CMD_ARG_U8(input),
		HOST_CMD_ARG_U8(mode),
		HOST_CMD_ARG_NONE
	};
	int err, status, try = 10;

	err = host_cmd(client, MT9V126_CMD_OVRL_ENABLE, args);
	if (err)
		return err;

	while (try > 0) {
		err = mt9v126_get_overlay_state(
			sd, NULL, NULL, NULL, &status);
		if (err)
			return err;
		if (status != -EBUSY)
			return status;
		usleep_range(1000, 10000);
		try -= 1;
	}

	return err;
}

static int mt9v126_set_encoding_mode(struct v4l2_subdev *sd, int mode)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct host_cmd_arg args[] = {
		HOST_CMD_ARG_U8(mode),
		HOST_CMD_ARG_NONE
	};
	return host_cmd(client, MT9V126_CMD_SEQ_SET_ENCODING_MODE, args);
}

static int mt9v126_set_flicker_frequency(struct v4l2_subdev *sd, int freq)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct host_cmd_arg args[] = {
		HOST_CMD_ARG_U8(freq),
		HOST_CMD_ARG_NONE
	};
	return host_cmd(client, MT9V126_CMD_SEQ_SET_FLICKER_FREQ, args);
}

static int mt9v126_config_dac(struct v4l2_subdev *sd, int enable,
			int bw, int pedestal, int test, int pal)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct host_cmd_arg args[] = {
		HOST_CMD_ARG_U32((!!bw) | ((!!pedestal)<<1) |
				((!!test)<<2) | ((!!pal)<<3)),
		HOST_CMD_ARG_U8(enable),
		HOST_CMD_ARG_NONE
	};
	return host_cmd(client, MT9V126_CMD_TXMGR_CONFIG_DAC, args);
}

static int mt9v126_get_gpio_property(struct v4l2_subdev *sd,
				u32 pin_mask, u8 property, u8 *value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct host_cmd_arg args[] = {
		HOST_CMD_ARG_U32(pin_mask),
		HOST_CMD_ARG_U8(property),
		HOST_CMD_ARG_NONE
	};
	u16 val;
	int err;

	err = host_cmd(client, MT9V126_CMD_GPIO_GET_PROP, args);
	if (err)
		return err;

	err = reg_read(client, MT9V126_CMD_HANDLER_PARAMS_POOL_BASE, &val);
	if (err)
		return err;

	if (value)
		*value = (val >> 8);

	return 0;
}

static int mt9v126_set_gpio_property(struct v4l2_subdev *sd,
				const u32 pin_mask, const u8 property,
				const u32 value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct host_cmd_arg args[] = {
		HOST_CMD_ARG_U32(pin_mask),
		HOST_CMD_ARG_U8(property),
		HOST_CMD_ARG_U8(value),
		HOST_CMD_ARG_NONE
	};
	return host_cmd(client, MT9V126_CMD_GPIO_SET_PROP, args);
}

static int mt9v126_set_parallel_mode(struct v4l2_subdev *sd,
				const u8 mode, const u8 disable_fvlv)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct host_cmd_arg args[] = {
		HOST_CMD_ARG_U8(mode),
		HOST_CMD_ARG_U8(disable_fvlv),
		HOST_CMD_ARG_NONE
	};
	return host_cmd(client, MT9V126_CMD_TXMGR_SET_PARALLEL_MODE, args);
}


static int mt9v126_detect(struct i2c_client *client, int *model)
{
	int err;
	u16 chip_id, model_id, fuse_id4;

	err = reg_read(client, MT9V126_K22B_CHIP_ID, &chip_id);
	if (err) {
		dev_err(&client->dev, "Failed to get chip ID: %d\n", err);
		return err;
	}

	if (chip_id != MT9V126_DEFAULT_K22B_CHIP_ID) {
		dev_err(&client->dev,
			"No MT9V126 detected, got bad chip ID: 0x%x\n",
			chip_id);
		return -ENODEV;
	}

	err = reg_read(client, MT9V126_MODEL_ID, &model_id);
	if (err) {
		dev_err(&client->dev, "Failed to get model ID: %d\n", err);
		return err;
	}

	if (model_id != MT9V126_DEFAULT_MODEL_ID) {
		dev_err(&client->dev,
			"No MT9V126 detected, got bad model ID: 0x%x\n",
			model_id);
		return -ENODEV;
	}

	{
		u16 r26 = 0x1;
		reg_set(client, 0x0018, 0x1, 0);
		while (r26 & 1) {
			reg_read(client, 0x0026, &r26);
			usleep_range(1000, 10000);
		}
	}

	err = reg_set(client, MT9V126_RESET_REGISTER, 0x0020, 0x0020);
	if (err) {
		dev_err(&client->dev,
			"Failed to enable access to fuse registers: %d\n",
			err);
		return err;
	}

	err = reg_read(client, MT9V126_FUSE_ID4, &fuse_id4);
	if (err) {
		dev_err(&client->dev, "Failed to get fuse ID4: %d\n", err);
		return err;
	}

	err = reg_set(client, MT9V126_RESET_REGISTER, 0x0020, 0x0000);
	if (err) {
		dev_err(&client->dev,
			"Failed to disable access to fuse registers: %d\n",
			err);
		return err;
	}

	dev_info(&client->dev,
		"Detected a MT9V126, chip ID %x, model ID %x, rev %x\n",
		chip_id, model_id, (fuse_id4 & 0xFE0) >> 5);
	if (model)
		*model = model_id;

	return 0;
}

static int mt9v126_get_chip_id(struct v4l2_subdev *sd,
			struct v4l2_dbg_chip_ident *id)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9v126 *mt9v126 = to_mt9v126(sd);

	if (id->match.type != V4L2_CHIP_MATCH_I2C_ADDR)
		return -EINVAL;

	if (id->match.addr != client->addr)
		return -ENODEV;

	id->ident	= mt9v126->model;
	id->revision	= 0;

	return 0;
}

static const struct v4l2_queryctrl *mt9v126_find_ctrl(int id)
{
	int i;
	for (i = 0 ; i < ARRAY_SIZE(mt9v126_controls) ; i++)
		if (mt9v126_controls[i].id == id)
			return &mt9v126_controls[i];
	return NULL;
}

static int mt9v126_queryctrl(struct v4l2_subdev *sd,
			struct v4l2_queryctrl *qctrl)
{
	const struct v4l2_queryctrl *ctrl;

	ctrl = mt9v126_find_ctrl(qctrl->id);
	if (!ctrl)
		return -EINVAL;

	memcpy(qctrl, ctrl, sizeof(*qctrl));
	return 0;
}

static int mt9v126_get_control(struct v4l2_subdev *sd,
			struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u16 val;
	int err;

	switch (ctrl->id) {
	case V4L_CID_MT9V126_INV_BRIGHTNESS_METRIC:
		err = reg_read(client,
			MT9V126_STAT_INV_BRIGHTNESS_METRIC,
			&val);
		break;
	case V4L_CID_MT9V126_GAIN_METRIC:
		err = reg_read(client,
			MT9V126_STAT_GAIN_METRIC,
			&val);
		break;
	default:
		return -EINVAL;
	}
	if (err)
		return err;
	ctrl->value = val;
	return 0;
}

static int mt9v126_tweak(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int err;

	/* [CCM_AWB] */
	err = reg_writev(client,
			MT9V126_CAM1_AWB_CCM_L_0,		0x01EC,
			MT9V126_CAM1_AWB_CCM_L_1,		0xFF80,
			MT9V126_CAM1_AWB_CCM_L_2,		0x0026,
			MT9V126_CAM1_AWB_CCM_L_3,		0xFFC0,
			MT9V126_CAM1_AWB_CCM_L_4,		0x015B,
			MT9V126_CAM1_AWB_CCM_L_5,		0x004D,
			MT9V126_CAM1_AWB_CCM_L_6,		0xFFE6,
			MT9V126_CAM1_AWB_CCM_L_7,		0xFEB3,
			MT9V126_CAM1_AWB_CCM_L_8,		0x0247,
			MT9V126_CAM1_AWB_CCM_L_9,		0x0017,
			MT9V126_CAM1_AWB_CCM_L_10,		0x0052,
			MT9V126_CAM1_AWB_CCM_RL_0,		0x0003,
			MT9V126_CAM1_AWB_CCM_RL_1,		0xFFD2,
			MT9V126_CAM1_AWB_CCM_RL_2,		0x0000,
			MT9V126_CAM1_AWB_CCM_RL_3,		0x0017,
			MT9V126_CAM1_AWB_CCM_RL_4,		0x006B,
			MT9V126_CAM1_AWB_CCM_RL_5,		0xFFB3,
			MT9V126_CAM1_AWB_CCM_RL_6,		0x003B,
			MT9V126_CAM1_AWB_CCM_RL_7,		0x00A1,
			MT9V126_CAM1_AWB_CCM_RL_8,		0xFF83,
			MT9V126_CAM1_AWB_CCM_RL_9,		0x0013,
			MT9V126_CAM1_AWB_CCM_RL_10,		0xFFD8,
			MT9V126_CAM1_AWB_AWB_XSCALE,		0x0003,
			MT9V126_CAM1_AWB_AWB_YSCALE,		0x0002,
			MT9V126_CAM1_AWB_AWB_WEIGHTS_0,		0x48C3,
			MT9V126_CAM1_AWB_AWB_WEIGHTS_1,		0x0CAD,
			MT9V126_CAM1_AWB_AWB_WEIGHTS_2,		0xFF97,
			MT9V126_CAM1_AWB_AWB_WEIGHTS_3,		0x003D,
			MT9V126_CAM1_AWB_AWB_WEIGHTS_4,		0x9103,
			MT9V126_CAM1_AWB_AWB_WEIGHTS_5,		0xAFFE,
			MT9V126_CAM1_AWB_AWB_WEIGHTS_6,		0x402F,
			MT9V126_CAM1_AWB_AWB_WEIGHTS_7,		0x0000,
			MT9V126_CAM1_AWB_AWB_XSHIFT_PRE_ADJ,	0x004B,
			MT9V126_CAM1_AWB_AWB_YSHIFT_PRE_ADJ,	0x0039,
			MT9V126_CAM1_LL_K_R_L,			0x0080,
			MT9V126_CAM1_LL_K_G_L,			0x0088,
			MT9V126_CAM1_LL_K_B_L,			0x0090,
			MT9V126_CAM1_LL_K_R_R,			0x0080,
			MT9V126_CAM1_LL_K_G_R,			0x0088,
			MT9V126_CAM1_LL_K_B_R,			0x0088,
			MT9V126_AWB_R_SCENE_RATIO_LOWER,	0x0032,
			MT9V126_AWB_R_SCENE_RATIO_UPPER,	0x00C8,
			MT9V126_AWB_B_SCENE_RATIO_LOWER,	0x001E,
			MT9V126_AWB_B_SCENE_RATIO_UPPER,	0x00C8,
			-1);
	if (err)
		return err;

	/* [Sensor setup - action] */
#if 1
	/* 3ed8 = (0xF << 10) | 0x02D8 */
	/* 0xF is the Low Light variable set, however there is no docs */
	/* about these variables. */
	err = reg_writev(client,
			0x3ed8, 0x0999,
#if 0
			0x3e14, 0x6886,
			0x3e1a, 0x8507,
			0x3e1c, 0x8705,
			0x3e24, 0x9a10,
			0x3e26, 0x8f09,
			0x3e2a, 0x8060,
			0x3e2c, 0x6169,
			0x3ed0, 0x8f7f,
			0x3eda, 0x68F6,
#endif
			-1);
	if (err)
		return err;
#endif

	/* [Tuning] */
	err = reg_writev(client,
			MT9V126_YUV_YCBCR_CONTROL,		0x000F,
			MT9V126_CAM1_AET_EXT_GAIN_SETUP_0,	2,
			MT9V126_AE_TRACK_MODE,			0xD7,
			MT9V126_CAM1_AET_AE_VIRT_GAIN_TH_CG,	0x100,
			MT9V126_CAM1_AET_AE_VIRT_GAIN_TH_DCG,	0xA0,
			MT9V126_AE_TRACK_TARGET,		0x32,
			MT9V126_AE_TRACK_GATE,			0x0004,
			MT9V126_AE_TRACK_JUMP_DIVISOR,		0x0002,
			MT9V126_CAM1_AET_SKIP_FRAMES,		0x0002,
			MT9V126_CAM1_LL_START_BRIGHTNESS,	0x0064,
			MT9V126_CAM1_LL_STOP_BRIGHTNESS,	0x0320,
			MT9V126_CAM1_LL_START_SATURATION,	0x80,
			MT9V126_CAM1_LL_END_SATURATION,		0x0,
			MT9V126_CAM1_LL_START_GAMMA_BM,		0x0064,
			MT9V126_CAM1_LL_STOP_GAMMA_BM,		0x0320,
			MT9V126_CAM1_SENSOR_0_FINE_CORRECTION,	0x0031,
			MT9V126_CAM1_LL_LL_START_1,		0x0007,
			MT9V126_CAM1_LL_LL_START_2,		0x0002,
			MT9V126_CAM1_LL_LL_STOP_0,		0x0008,
			MT9V126_CAM1_LL_LL_STOP_1,		0x0002,
			MT9V126_CAM1_LL_LL_STOP_2,		0x0020,
			MT9V126_CAM1_LL_NR_STOP_0,		0x0040,
			MT9V126_CAM1_LL_NR_STOP_1,		0x0040,
			MT9V126_CAM1_LL_NR_STOP_2,		0x0040,
			MT9V126_CAM1_LL_NR_STOP_3,		0x0040,
			MT9V126_CAM1_AET_AE_MAX_VIRT_AGAIN,	0x1FFF,
			MT9V126_CAM1_AET_AE_MAX_VIRT_DGAIN,	0x100,
			MT9V126_CAM1_MAX_ANALOG_GAIN,		0x100,
			MT9V126_SYS_REFRESH_MASK,		3,

			MT9V126_LL_GAMMA_NRCURVE_0,		0x0000,
			MT9V126_LL_GAMMA_NRCURVE_1,		0x0018,
			MT9V126_LL_GAMMA_NRCURVE_2,		0x0025,
			MT9V126_LL_GAMMA_NRCURVE_3,		0x003A,
			MT9V126_LL_GAMMA_NRCURVE_4,		0x0059,
			MT9V126_LL_GAMMA_NRCURVE_5,		0x0070,
			MT9V126_LL_GAMMA_NRCURVE_6,		0x0081,
			MT9V126_LL_GAMMA_NRCURVE_7,		0x0090,
			MT9V126_LL_GAMMA_NRCURVE_8,		0x009E,
			MT9V126_LL_GAMMA_NRCURVE_9,		0x00AB,
			MT9V126_LL_GAMMA_NRCURVE_10,		0x00B6,
			MT9V126_LL_GAMMA_NRCURVE_11,		0x00C1,
			MT9V126_LL_GAMMA_NRCURVE_12,		0x00CB,
			MT9V126_LL_GAMMA_NRCURVE_13,		0x00D5,
			MT9V126_LL_GAMMA_NRCURVE_14,		0x00DE,
			MT9V126_LL_GAMMA_NRCURVE_15,		0x00E7,
			MT9V126_LL_GAMMA_NRCURVE_16,		0x00EF,
			MT9V126_LL_GAMMA_NRCURVE_17,		0x00F7,
			MT9V126_LL_GAMMA_NRCURVE_18,		0x00FF,
			-1);
	if (err)
		return err;
	/* [Tweaks] */
	err = reg_writev(client,
			MT9V126_CAM1_AET_EXT_GAIN_SETUP_0,	2,
			MT9V126_SYS_REFRESH_MASK,		3,
			-1);
	if (err)
		return err;

	return 0;
}

static int mt9v126_set_config(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mt9v126 *mt9v126 = to_mt9v126(sd);
	int err, mode, state, configured;

	mode = mt9v126->progressive ?
		MT9V126_TXMGR_PARALLEL_MODE_CPIPE :
		MT9V126_TXMGR_PARALLEL_MODE_DEWARP_BT656;
	state = mt9v126_get_state(sd);
	configured = (state != MT9V126_SYS_STATE_UNCONFIGURED);

	dev_dbg(&client->dev, "mt9v126 init state: %x\n", state);

	/* Apply the firmware patches */
	err = mt9v126_apply_patches(sd);
	if (err) {
		dev_err(&client->dev, "Failed to apply patches\n");
		goto error;
	}

	/* Setup the parallel output mode */
	err = mt9v126_set_parallel_mode(sd, mode, 0);
	if (err) {
		dev_err(&client->dev, "Failed to set parallel mode\n");
		goto error;
	}

	if (!configured) {
		/* Set the encoder mode to PAL */
		err = mt9v126_set_encoding_mode(sd, 1);
		if (err) {
			dev_err(&client->dev, "Failed to set PAL mode\n");
			goto error;
		}

		/* Set 50 Hz flicker freq. */
		err = mt9v126_set_flicker_frequency(sd, flicker_frequency);
		if (err) {
			dev_err(&client->dev,
				"Failed to set flicker frequency\n");
			goto error;
		}

		if (!mt9v126->progressive) {
			/* Apply the dewarp config */
			err = mt9v126_set_dewarp_config(
				sd, &mt9v126_dewarp_config_pal_640x480);
			if (err) {
				dev_err(&client->dev,
					"Failed to set dewarp config\n");
				goto error;
			}

			/* Enable dewarp */
			err = mt9v126_enable_dewarp(sd, 1, 1, 0);
			if (err && err != -EALREADY) {
				dev_err(&client->dev,
					"Failed to enable dewarp\n");
				goto error;
			}
		}
	}

	err = mt9v126_tweak(sd);
	if (err) {
		dev_err(&client->dev, "Failed to apply tweaks\n");
		goto error;
	}

	err = mt9v126_soft_restart(sd);
	if (err) {
		dev_err(&client->dev, "Failed to soft restart\n");
		goto error;
	}

	/* Suspend until the stream start */
	err = mt9v126_switch_state(sd, MT9V126_SYS_STATE_SUSPENDED);
	if (err) {
		dev_err(&client->dev, "Failed to suspend\n");
		goto error;
	}

	return 0;
error:
	dev_err(&client->dev, "Set config failed (%d)\n", err);
	return err;
}

static int mt9v126_try_mbus_fmt(struct v4l2_subdev *sd,
				struct v4l2_mbus_framefmt *fmt)
{
	struct mt9v126 *mt9v126 = to_mt9v126(sd);

	if (mt9v126->progressive) {
		fmt->width = 640;
		fmt->height = 480;
		fmt->field = V4L2_FIELD_NONE;
	} else {
		fmt->width = 720;
		fmt->height = 576;
		fmt->field = V4L2_FIELD_INTERLACED;
	}

	fmt->code = V4L2_MBUS_FMT_UYVY8_2X8;
	fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;

	return 0;
}

static int mt9v126_set_mbus_fmt(struct v4l2_subdev *sd,
				struct v4l2_mbus_framefmt *fmt)
{
	/* TODO: Check the fmt passed here. */
	return 0;
}

static int mt9v126_enum_mbus_fmt(struct v4l2_subdev *sd,
				unsigned int index,
				enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(mt9v126_mbus_fmt))
		return -EINVAL;

	*code = mt9v126_mbus_fmt[index];

	return 0;
}

static int mt9v126_get_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *cfg)
{
	struct mt9v126 *mt9v126 = to_mt9v126(sd);

	if (mt9v126->progressive)
		cfg->type = V4L2_MBUS_PARALLEL;
	else
		cfg->type = V4L2_MBUS_BT656;

	cfg->flags = V4L2_MBUS_MASTER |
		V4L2_MBUS_HSYNC_ACTIVE_HIGH |
		V4L2_MBUS_VSYNC_ACTIVE_HIGH |
		V4L2_MBUS_PCLK_SAMPLE_RISING |
		V4L2_MBUS_DATA_ACTIVE_HIGH;

	return 0;
}

static int mt9v126_set_mbus_config(struct v4l2_subdev *sd,
				const struct v4l2_mbus_config *cfg)
{
	struct mt9v126 *mt9v126 = to_mt9v126(sd);

	switch (cfg->type) {
	case V4L2_MBUS_PARALLEL:
		if (!mt9v126->progressive)
			return -EINVAL;
		if ((cfg->flags & V4L2_MBUS_HSYNC_ACTIVE_LOW) ||
			(cfg->flags & V4L2_MBUS_VSYNC_ACTIVE_LOW))
			return -EINVAL;
		break;
	case V4L2_MBUS_BT656:
		if (mt9v126->progressive)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}
	if ((cfg->flags & V4L2_MBUS_PCLK_SAMPLE_FALLING) ||
		(cfg->flags & V4L2_MBUS_DATA_ACTIVE_LOW))
		return -EINVAL;

	return 0;
}

static int mt9v126_s_stream(struct v4l2_subdev *sd, int enable)
{
	int new_state = enable ?
		MT9V126_SYS_STATE_STREAMING :
		MT9V126_SYS_STATE_SUSPENDED;
	return mt9v126_switch_state(sd, new_state);
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int mt9v126_get_register(struct v4l2_subdev *sd,
				struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u16 val = 0xDEAD;
	int err;

	if (reg->match.type != V4L2_CHIP_MATCH_I2C_ADDR ||
		reg->reg > 0xFFFF)
		return -EINVAL;

	if (reg->match.addr != client->addr)
		return -ENODEV;

	err = reg_read(client, reg->reg, &val);
	if (err)
		return err;

	reg->size = 2;
	reg->val = val;

	return 0;
}

static int mt9v126_set_register(struct v4l2_subdev *sd,
				struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->match.type != V4L2_CHIP_MATCH_I2C_ADDR ||
		reg->reg > 0xFFFF)
		return -EINVAL;

	if (reg->match.addr != client->addr)
		return -ENODEV;

	return reg_write(client, reg->reg, reg->val);
}
#endif

static const struct v4l2_subdev_core_ops mt9v126_core_ops = {
	.g_chip_ident = mt9v126_get_chip_id,
	.queryctrl = mt9v126_queryctrl,
	.g_ctrl = mt9v126_get_control,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = mt9v126_get_register,
	.s_register = mt9v126_set_register,
#endif
};

static unsigned long mt9v126_query_bus_param(struct soc_camera_device *icd)
{
	struct soc_camera_link *icl = to_soc_camera_link(icd);
	unsigned long flags =
		SOCAM_MASTER | SOCAM_PCLK_SAMPLE_RISING |
		SOCAM_HSYNC_ACTIVE_HIGH | SOCAM_VSYNC_ACTIVE_HIGH |
		SOCAM_DATA_ACTIVE_HIGH | SOCAM_DATAWIDTH_8;
	return soc_camera_apply_sensor_flags(icl, flags);
}

static int mt9v126_set_bus_param(struct soc_camera_device *icd,
				unsigned long flags)
{
	return 0;
}

static struct soc_camera_ops mt9v126_camera_ops = {
	.query_bus_param = mt9v126_query_bus_param,
	.set_bus_param = mt9v126_set_bus_param,
};

static const struct v4l2_subdev_video_ops mt9v126_video_ops = {
	.try_mbus_fmt = mt9v126_try_mbus_fmt,
	.s_mbus_fmt = mt9v126_set_mbus_fmt,
	.enum_mbus_fmt = mt9v126_enum_mbus_fmt,
	.g_mbus_config = mt9v126_get_mbus_config,
	.s_mbus_config = mt9v126_set_mbus_config,
	.s_stream = mt9v126_s_stream,
};

static const struct v4l2_subdev_ops mt9v126_subdev_ops = {
	.core = &mt9v126_core_ops,
	.video = &mt9v126_video_ops,
};

static int mt9v126_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct soc_camera_device *icd = client->dev.platform_data;
	struct mt9v126_platform_data *pdata = NULL;
	struct mt9v126 *mt9v126 = NULL;
	struct soc_camera_link *icl;
	int err = 0, model = 0;

	if (!icd) {
		dev_err(&client->dev, "mt9v126: soc-camera data missing!\n");
		return -EINVAL;
	}

	icl = to_soc_camera_link(icd);
	if (icl)
		pdata = icl->priv;

	if (pdata && pdata->reset_gpio >= 0) {
		/* Setup the GPIO for hard reset */
		err = gpio_request(pdata->reset_gpio, "mt9v126-reset");
		if (err) {
			dev_err(&client->dev,
				"Failed to request reset GPIO (%d)!\n",
				pdata->reset_gpio);
			goto error;
		}
		err = gpio_direction_output(pdata->reset_gpio, 1);
		if (err) {
			dev_err(&client->dev,
				"Failed to set reset GPIO (%d) direction "
				"to output!\n",
				pdata->reset_gpio);
			goto error;
		}
		/* Wait for the SoC init to finish before */
		/* we do the detection. */
		msleep(MT9V126_INTERNAL_INIT_TIME);
	}

	err = mt9v126_detect(client, &model);
	if (err)
		goto error;

	mt9v126 = kzalloc(sizeof(struct mt9v126), GFP_KERNEL);
	if (!mt9v126) {
		err = -ENOMEM;
		goto error;
	}

	mt9v126->model = model;
	if (pdata) {
		mt9v126->reset_gpio = pdata->reset_gpio;
		mt9v126->progressive = pdata->progressive;
	} else {
		mt9v126->reset_gpio = -1;
		mt9v126->progressive = 0;
	}

	/* Register with V4L2 layer as slave device */
	v4l2_i2c_subdev_init(&mt9v126->sd, client, &mt9v126_subdev_ops);
	v4l2_info(&mt9v126->sd, "%s camera driver registered\n",
		mt9v126->sd.name);

	icd->ops = &mt9v126_camera_ops;

	mt9v126_hard_reset(&mt9v126->sd);
	err = mt9v126_set_config(&mt9v126->sd);
	if (err)
		goto error;

	return 0;

error:
	if (pdata && pdata->reset_gpio >= 0)
		gpio_free(pdata->reset_gpio);
	kfree(mt9v126);
	return err;
}

static int mt9v126_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct mt9v126 *mt9v126 = to_mt9v126(sd);

	v4l2_device_unregister_subdev(sd);

	if (mt9v126->reset_gpio >= 0)
		gpio_free(mt9v126->reset_gpio);

	kfree(mt9v126);

	return 0;
}

static const struct i2c_device_id mt9v126_id[] = {
	{ "mt9v126", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mt9v126_id);

static struct i2c_driver mt9v126_i2c_driver = {
	.driver = {
		.name = "mt9v126",
	},
	.probe		= mt9v126_probe,
	.remove		= mt9v126_remove,
	.id_table	= mt9v126_id,
};

module_i2c_driver(mt9v126_i2c_driver);

MODULE_DESCRIPTION("Aptina MT9V126 Camera driver");
MODULE_AUTHOR("Alban Bedel <alban.bedel@avionic-design.de>");
MODULE_LICENSE("GPL v2");
