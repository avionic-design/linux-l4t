/*
 * DVB USB Linux driver for Avionic Design DVB-C/T USB 2.0 Stick (AD-1456).
 *
 * Copyright (C) 2013-2014 Julian Scheel <julian@jusst.de>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef AD1456_H
#define AD1456_H
#include "dvb_usb.h"

#define AD1456_REVISION			0x00
#define AD1456_SLEEP_MODE		0x01
#define AD1456_I2C_MSG			0x02
#define AD1456_FIFO_ENABLED		0x03

#define AD1456_I2C_BIT_ERROR		0x06
#define AD1456_I2C_NACK			0x07
#define AD1456_I2C_OK			0x08
#define AD1456_I2C_NOT_VALID		0x10

#define AD1456_USB_TIMEOUT		1000

#define AD1456_FIRMWARE			"dvb-usb-ad1456.fw"

#endif
