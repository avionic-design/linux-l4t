/*
 * arch/arm/mach-tegra/com-tamonten-ng-pinmux.c
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <mach/pinmux.h>
#include "board.h"
#include "com-tamonten.h"

#define DEFAULT_DRIVE(_name)					\
	{							\
		.pingroup = TEGRA_DRIVE_PINGROUP_##_name,	\
		.hsm = TEGRA_HSM_DISABLE,			\
		.schmitt = TEGRA_SCHMITT_ENABLE,		\
		.drive = TEGRA_DRIVE_DIV_1,			\
		.pull_down = TEGRA_PULL_31,			\
		.pull_up = TEGRA_PULL_31,			\
		.slew_rising = TEGRA_SLEW_SLOWEST,		\
		.slew_falling = TEGRA_SLEW_SLOWEST,		\
	}
/* Setting the drive strength of pins
 * hsm: Enable High speed mode (ENABLE/DISABLE)
 * Schimit: Enable/disable schimit (ENABLE/DISABLE)
 * drive: low power mode (DIV_1, DIV_2, DIV_4, DIV_8)
 * pulldn_drive - drive down (falling edge) - Driver Output Pull-Down drive
 *                strength code. Value from 0 to 31.
 * pullup_drive - drive up (rising edge)  - Driver Output Pull-Up drive
 *                strength code. Value from 0 to 31.
 * pulldn_slew -  Driver Output Pull-Up slew control code  - 2bit code
 *                code 11 is least slewing of signal. code 00 is highest
 *                slewing of the signal.
 *                Value - FASTEST, FAST, SLOW, SLOWEST
 * pullup_slew -  Driver Output Pull-Down slew control code -
 *                code 11 is least slewing of signal. code 00 is highest
 *                slewing of the signal.
 *                Value - FASTEST, FAST, SLOW, SLOWEST
 */
#define SET_DRIVE(_name, _hsm, _schmitt, _drive, _pulldn_drive, _pullup_drive, _pulldn_slew, _pullup_slew) \
	{                                               \
		.pingroup = TEGRA_DRIVE_PINGROUP_##_name,   \
		.hsm = TEGRA_HSM_##_hsm,                    \
		.schmitt = TEGRA_SCHMITT_##_schmitt,        \
		.drive = TEGRA_DRIVE_##_drive,              \
		.pull_down = TEGRA_PULL_##_pulldn_drive,    \
		.pull_up = TEGRA_PULL_##_pullup_drive,		\
		.slew_rising = TEGRA_SLEW_##_pulldn_slew,   \
		.slew_falling = TEGRA_SLEW_##_pullup_slew,	\
	}

/* !!!FIXME!!!! TODO POPULATE THIS TABLE */
#if 0
static __initdata struct tegra_drive_pingroup_config tamonten_ng_drive_pinmux[] = {
	/* DEFAULT_DRIVE(<pin_group>), */
	/* SET_DRIVE(ATA, DISABLE, DISABLE, DIV_1, 31, 31, FAST, FAST) */
	SET_DRIVE(DAP2, 	DISABLE, ENABLE, DIV_1, 31, 31, FASTEST, FASTEST),
	SET_DRIVE(DAP1, 	DISABLE, ENABLE, DIV_1, 31, 31, FASTEST, FASTEST),

	/* All I2C pins are driven to maximum drive strength */
	/* GEN1 I2C */
	SET_DRIVE(DBG,		DISABLE, ENABLE, DIV_1, 31, 31, FASTEST, FASTEST),

	/* GEN2 I2C */
	SET_DRIVE(AT5,		DISABLE, ENABLE, DIV_1, 31, 31, FASTEST, FASTEST),

	/* CAM I2C */
	SET_DRIVE(GME,		DISABLE, ENABLE, DIV_1, 31, 31, FASTEST, FASTEST),

	/* DDC I2C */
	SET_DRIVE(DDC,		DISABLE, ENABLE, DIV_1, 31, 31, FASTEST, FASTEST),

	/* PWR_I2C */
	SET_DRIVE(AO1,		DISABLE, ENABLE, DIV_1, 31, 31, FASTEST, FASTEST),

	/* UART3 */
	SET_DRIVE(UART3,	DISABLE, ENABLE, DIV_1, 31, 31, FASTEST, FASTEST),

	/* SDMMC1 */
	SET_DRIVE(SDIO1,	DISABLE, DISABLE, DIV_1, 46, 42, FAST, FAST),

	/* SDMMC3 */
	SET_DRIVE(SDIO3,	DISABLE, DISABLE, DIV_1, 46, 42, FAST, FAST),

	/* SDMMC4 */
	SET_DRIVE(GMA,		DISABLE, DISABLE, DIV_1, 9, 9, SLOWEST, SLOWEST),
	SET_DRIVE(GMB,		DISABLE, DISABLE, DIV_1, 9, 9, SLOWEST, SLOWEST),
	SET_DRIVE(GMC,		DISABLE, DISABLE, DIV_1, 9, 9, SLOWEST, SLOWEST),
	SET_DRIVE(GMD,		DISABLE, DISABLE, DIV_1, 9, 9, SLOWEST, SLOWEST),

};
#endif

#define DEFAULT_PINMUX(_pingroup, _mux, _pupd, _tri, _io)	\
	{							\
		.pingroup	= TEGRA_PINGROUP_##_pingroup,	\
		.func		= TEGRA_MUX_##_mux,		\
		.pupd		= TEGRA_PUPD_##_pupd,		\
		.tristate	= TEGRA_TRI_##_tri,		\
		.io		= TEGRA_PIN_##_io,		\
		.lock		= TEGRA_PIN_LOCK_DEFAULT,	\
		.od		= TEGRA_PIN_OD_DEFAULT,		\
		.ioreset	= TEGRA_PIN_IO_RESET_DEFAULT,	\
	}

#define I2C_PINMUX(_pingroup, _mux, _pupd, _tri, _io, _lock, _od) \
	{							\
		.pingroup	= TEGRA_PINGROUP_##_pingroup,	\
		.func		= TEGRA_MUX_##_mux,		\
		.pupd		= TEGRA_PUPD_##_pupd,		\
		.tristate	= TEGRA_TRI_##_tri,		\
		.io		= TEGRA_PIN_##_io,		\
		.lock		= TEGRA_PIN_LOCK_##_lock,	\
		.od		= TEGRA_PIN_OD_##_od,		\
		.ioreset	= TEGRA_PIN_IO_RESET_DEFAULT,	\
	}

#define VI_PINMUX(_pingroup, _mux, _pupd, _tri, _io, _lock, _ioreset) \
	{							\
		.pingroup	= TEGRA_PINGROUP_##_pingroup,	\
		.func		= TEGRA_MUX_##_mux,		\
		.pupd		= TEGRA_PUPD_##_pupd,		\
		.tristate	= TEGRA_TRI_##_tri,		\
		.io		= TEGRA_PIN_##_io,		\
		.lock		= TEGRA_PIN_LOCK_##_lock,	\
		.od		= TEGRA_PIN_OD_DEFAULT,		\
		.ioreset	= TEGRA_PIN_IO_RESET_##_ioreset	\
	}
#define CEC_PINMUX(_pingroup, _mux, _pupd, _tri, _io, _lock, _od) \
	{                                                       \
		.pingroup       = TEGRA_PINGROUP_##_pingroup,   \
			.func           = TEGRA_MUX_##_mux,             \
			.pupd           = TEGRA_PUPD_##_pupd,           \
			.tristate       = TEGRA_TRI_##_tri,             \
			.io             = TEGRA_PIN_##_io,              \
			.lock           = TEGRA_PIN_LOCK_##_lock,       \
			.od             = TEGRA_PIN_OD_##_od,           \
			.ioreset        = TEGRA_PIN_IO_RESET_DEFAULT,   \
	}


static __initdata struct tegra_pingroup_config tamonten_ng_pinmux_common[] = {

	/* NAND, GMI - p9 */
	DEFAULT_PINMUX(GMI_AD0,         NAND,            NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(GMI_AD1,         NAND,            NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(GMI_AD2,         NAND,            NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(GMI_AD3,         NAND,            NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(GMI_AD4,         NAND,            NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(GMI_AD5,         NAND,            NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(GMI_AD6,         NAND,            NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(GMI_AD7,         NAND,            NORMAL,    NORMAL,     INPUT),

	DEFAULT_PINMUX(GMI_ADV_N,       NAND,            NORMAL,    NORMAL,     OUTPUT),
	DEFAULT_PINMUX(GMI_CLK,         NAND,            NORMAL,    NORMAL,     OUTPUT),

	DEFAULT_PINMUX(GMI_WR_N,        NAND,            NORMAL,    NORMAL,     OUTPUT),
	DEFAULT_PINMUX(GMI_OE_N,        NAND,            NORMAL,    NORMAL,     OUTPUT),



	/* SDMMC1 pinmux - goes to edge connector */
	DEFAULT_PINMUX(SDMMC1_CLK,      SDMMC1,          NORMAL,     NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC1_CMD,      SDMMC1,          PULL_UP,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC1_DAT3,     SDMMC1,          PULL_UP,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC1_DAT2,     SDMMC1,          PULL_UP,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC1_DAT1,     SDMMC1,          PULL_UP,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC1_DAT0,     SDMMC1,          PULL_UP,    NORMAL,     INPUT),

	/* SDMMC2 is not used */

	/* SDMMC3 pinmux - goes to edge connector */
	DEFAULT_PINMUX(SDMMC3_CLK,      SDMMC3,          NORMAL,     NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC3_CMD,      SDMMC3,          PULL_UP,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC3_DAT0,     SDMMC3,          PULL_UP,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC3_DAT1,     SDMMC3,          PULL_UP,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC3_DAT2,     SDMMC3,          PULL_UP,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC3_DAT3,     SDMMC3,          PULL_UP,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC3_DAT4,     SDMMC3,          PULL_UP,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC3_DAT5,     SDMMC3,          PULL_UP,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC3_DAT6,     SDMMC3,          PULL_UP,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC3_DAT7,     SDMMC3,          PULL_UP,    NORMAL,     INPUT),

	/* SDMMC4 pinmux - This is used for the eMMC on TT30 */
	DEFAULT_PINMUX(SDMMC4_CLK,      SDMMC4,          NORMAL,     NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC4_CMD,      SDMMC4,          PULL_UP,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC4_DAT0,     SDMMC4,          PULL_UP,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC4_DAT1,     SDMMC4,          PULL_UP,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC4_DAT2,     SDMMC4,          PULL_UP,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC4_DAT3,     SDMMC4,          PULL_UP,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC4_DAT4,     SDMMC4,          PULL_UP,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC4_DAT5,     SDMMC4,          PULL_UP,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC4_DAT6,     SDMMC4,          PULL_UP,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC4_DAT7,     SDMMC4,          PULL_UP,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SDMMC4_RST_N,    POPSDMMC4,       PULL_UP,    NORMAL,     INPUT),




	/* UART1 goes to JTAG connector - p22 */
	DEFAULT_PINMUX(ULPI_DATA0,      UARTA,           NORMAL,    NORMAL,     OUTPUT),
	DEFAULT_PINMUX(ULPI_DATA1,      UARTA,           NORMAL,    NORMAL,     INPUT),

	/* UART 2 is not used - p21 */

	/* UART 3 Goes to edge connector - p21 */
	DEFAULT_PINMUX(UART3_TXD,       UARTC,           NORMAL,    NORMAL,     OUTPUT),
	DEFAULT_PINMUX(UART3_RXD,       UARTC,           NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(UART3_CTS_N,     UARTC,           NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(UART3_RTS_N,     UARTC,           NORMAL,    NORMAL,     OUTPUT),

	/* UART 4 goes to edge connector - p22 */
	DEFAULT_PINMUX(ULPI_CLK,        UARTD,           NORMAL,    NORMAL,     OUTPUT),
	DEFAULT_PINMUX(ULPI_DIR,        UARTD,           NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(ULPI_NXT,        UARTD,           NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(ULPI_STP,        UARTD,           NORMAL,    NORMAL,     OUTPUT),

	/* No UART 5 because SDMMC1 is using the pins */



	/* I2C0 Power I2C pinmux */
	I2C_PINMUX(PWR_I2C_SCL,		I2CPWR,		NORMAL,	NORMAL,	INPUT,	DISABLE,	ENABLE),
	I2C_PINMUX(PWR_I2C_SDA,		I2CPWR,		NORMAL,	NORMAL,	INPUT,	DISABLE,	ENABLE),

	/* I2C1 aka GEN1 i2c goes to edge connector - p21 */
	I2C_PINMUX(GEN1_I2C_SCL,	I2C1,		NORMAL,	NORMAL,	INPUT,	DISABLE,	ENABLE),
	I2C_PINMUX(GEN1_I2C_SDA,	I2C1,		NORMAL,	NORMAL,	INPUT,	DISABLE,	ENABLE),

	/* I2C2 aka GEN2 i2c goes to edge connector - p9*/
	I2C_PINMUX(GEN2_I2C_SCL,	I2C2,		NORMAL,	NORMAL,	INPUT,	DISABLE,	ENABLE),
	I2C_PINMUX(GEN2_I2C_SDA,	I2C2,		NORMAL,	NORMAL,	INPUT,	DISABLE,	ENABLE),

	/* I2C3 aka CAM i2c goes to edge connector */
	I2C_PINMUX(CAM_I2C_SCL,		I2C3,		NORMAL,	NORMAL,	INPUT,	DISABLE,	ENABLE),
	I2C_PINMUX(CAM_I2C_SDA,		I2C3,		NORMAL,	NORMAL,	INPUT,	DISABLE,	ENABLE),

	/* I2C4 aka DDC i2c goes to edge connector and then to VGA connector */
	I2C_PINMUX(DDC_SCL,			I2C4,		NORMAL,	NORMAL,	INPUT,	DISABLE,	ENABLE),
	I2C_PINMUX(DDC_SDA,			I2C4,		NORMAL,	NORMAL,	INPUT,	DISABLE,	ENABLE),



	/* AUDIO */

	/* CLK1_REQ is NOT used, but CLK1_OUT is. */
	DEFAULT_PINMUX(CLK1_OUT,        EXTPERIPH1,      NORMAL,    NORMAL,     INPUT),

	/* DAP1 is not used */

	/* DAP2 goes to edge connector */
	DEFAULT_PINMUX(DAP2_FS,         I2S1,            NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(DAP2_DIN,        I2S1,            NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(DAP2_DOUT,       I2S1,            NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(DAP2_SCLK,       I2S1,            NORMAL,    NORMAL,     INPUT),

	/* SPDIF goes to edge connector */
	DEFAULT_PINMUX(SPDIF_IN,        SPDIF,           NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SPDIF_OUT,       SPDIF,           NORMAL,    NORMAL,     OUTPUT),

	/* DAP4 goes to CSInJTAG connector - p21 */
	DEFAULT_PINMUX(DAP4_FS,         I2S3,            NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(DAP4_DIN,        I2S3,            NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(DAP4_SCLK,       I2S3,            NORMAL,    NORMAL,     INPUT),



	/* SPI */

	/* SPI1 is used */
	DEFAULT_PINMUX(SPI1_MOSI,       SPI1,            NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SPI1_SCK,        SPI1,            NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SPI1_CS0_N,      SPI1,            NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(SPI1_MISO,       SPI1,            NORMAL,    NORMAL,     INPUT),

	/* SPI2 is not used */

	/* SPI3 is not used */

	/* SPI4 is not used */


	/* LCD */
	DEFAULT_PINMUX(LCD_PCLK,        DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_DE,          DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_HSYNC,       DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_VSYNC,       DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D0,          DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D1,          DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D2,          DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D3,          DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D4,          DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D5,          DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D6,          DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D7,          DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D8,          DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D9,          DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D10,         DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D11,         DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D12,         DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D13,         DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D14,         DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D15,         DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D16,         DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D17,         DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D18,         DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D19,         DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D20,         DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D21,         DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D22,         DISPLAYA,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(LCD_D23,         DISPLAYA,        NORMAL,    NORMAL,     INPUT),

	/* LCD Backlight */
	DEFAULT_PINMUX(GMI_AD8,         PWM,             NORMAL,    NORMAL,     OUTPUT),

	/* USB are always running on their SFIO */

	/* PCIe are mostly running their default SFIO - p23 */
	DEFAULT_PINMUX(PEX_L1_CLKREQ_N,      PCIE,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(PEX_L1_PRSNT_N,       PCIE,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(PEX_L1_RST_N,         PCIE,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(PEX_L2_CLKREQ_N,      PCIE,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(PEX_L2_PRSNT_N,       PCIE,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(PEX_L2_RST_N,         PCIE,        NORMAL,    NORMAL,     INPUT),
	DEFAULT_PINMUX(PEX_WAKE_N,           PCIE,        NORMAL,    NORMAL,     INPUT),


	/* SATA are always running SFIO - p24 */

};


static __initdata struct tegra_pingroup_config tamonten_ng_unused_pins_lowpower[] = {
		/* TODO */
};


#define GPIO_INIT_PIN_MODE(_gpio, _is_input, _value)	\
	{					\
		.gpio_nr	= _gpio,	\
		.is_input	= _is_input,	\
		.value		= _value,	\
	}


static struct gpio_init_pin_info init_gpio_mode_tamonten_ng[] = {

	/* NAND GMI recycle - p9 */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PH1, false, 0), /* PWM_3D */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PH2, false, 0), /* LCD1_BL_EN */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PH3, false, 0), /* EN_3V3_FUSE */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PH4, true, 0), /* TS_nIRQ */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PH5, true, 0), /* W_DISABLE */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PH6, true, 0), /* SDIO2_WP */

	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PJ2, false, 0), /* EN_3V3_EMMC */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PK3, false, 0), /* EN_3V3_SATA_HVDD */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PK2, true, 0), /* HSCI_MST_nSLV */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PI3, false, 0), /* HSMMC_WP */

	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PI4, false, 1), /* nRST_PERIPHERALS */

	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PI5, true, 0), /* HSMMC_nCD */


	/* Unused SPI2 - p20 */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PW2, true, 0), /* HEAD_DET */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PW3, true, 0), /* CDC_IRQ */

	/* Used for CAM - p13 */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PBB0, false, 0), /* CAM_RST */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PBB7, false, 0), /* CAM_PWDN */

	/* LCD recycle - p14 */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PB2, false, 0), /* LVDS_SHTDN */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PC1, true, 0), /* DBG_IRQ */

	/* HDMI - p14 */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PN7, true, 0), /* HDMI_INT */

	/* Various - p13 */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PCC1, false, 1), /* EN_VIP_LS */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PCC2, true, 0), /* TEMP_nALERT */

	/* Various - p19 */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PV2, false, 0), /* ALIVE */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PV3, true, 0), /* WAKEUP */

	/* Pure GPIOs - p21 */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PU5, true, 0), /* GPIO0 */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PU6, true, 0), /* GPIO1 */

	/* SATA - p22 */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PP0, true, 0), /* SATA_nDET */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PP3, true, 0), /* SATA_EN_nOC */

	/* Various - p22 */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PV0, true, 0), /* AP_nONKEY */

	/* PCIe recycle - p23 */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PDD2, false, 0), /* EN_VDD_BL1 */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PDD0, false, 0), /* EN_VDD_BL2 */

	/* VI recycling - p33 */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PT3, false, 0), /* EN_3V3_PEX_HVDD */

	/* USB3 Enable VBUS - p9 */
	GPIO_INIT_PIN_MODE(TEGRA_GPIO_PI7, false, 0), /* EN_USB3_VBUS_OC */
};


static void __init tamonten_ng_gpio_init_configure(void)
{
	int len;
	int i;
	struct gpio_init_pin_info *pins_info;

	len = ARRAY_SIZE(init_gpio_mode_tamonten_ng);
	pins_info = init_gpio_mode_tamonten_ng;

	for (i = 0; i < len; ++i) {
		tegra_gpio_init_configure(pins_info->gpio_nr,
			pins_info->is_input, pins_info->value);
		pins_info++;
	}
}

int __init tamonten_ng_pinmux_init(void)
{
	/* init all used as gpios */
	tamonten_ng_gpio_init_configure();

	/* init all used as SFIO */
	tegra_pinmux_config_table(tamonten_ng_pinmux_common, ARRAY_SIZE(tamonten_ng_pinmux_common));

	/* low power unused ones */
	tegra_pinmux_config_table(tamonten_ng_unused_pins_lowpower, ARRAY_SIZE(tamonten_ng_unused_pins_lowpower));

	return 0;
}

#define PIN_GPIO_LPM(_name, _gpio, _is_input, _value)	\
	{					\
		.name		= _name,	\
		.gpio_nr	= _gpio,	\
		.is_gpio	= true,		\
		.is_input	= _is_input,	\
		.value		= _value,	\
	}

struct gpio_init_pin_info pin_gpio_anyway [] = {
		/* TODO */
};


static void set_unused_pin_gpio(struct gpio_init_pin_info *lpm_pin_info,
		int list_count)
{
	int i;
	struct gpio_init_pin_info *pin_info;
	int ret;

	for (i = 0; i < list_count; ++i) {
		pin_info = (struct gpio_init_pin_info *)(lpm_pin_info + i);
		if (!pin_info->is_gpio)
			continue;

		ret = gpio_request(pin_info->gpio_nr, pin_info->name);
		if (ret < 0) {
			pr_err("%s() Error in gpio_request() for gpio %d\n",
					__func__, pin_info->gpio_nr);
			continue;
		}
		if (pin_info->is_input)
			ret = gpio_direction_input(pin_info->gpio_nr);
		else
			ret = gpio_direction_output(pin_info->gpio_nr,
							pin_info->value);
		if (ret < 0) {
			pr_err("%s() Error in setting gpio %d to in/out\n",
				__func__, pin_info->gpio_nr);
			gpio_free(pin_info->gpio_nr);
			continue;
		}
	}
}

/* Initialize the pins to desired state as per power/asic/system-eng
 * recomendation */
int __init tamonten_ng_pins_state_late_init(void)
{
	set_unused_pin_gpio(&pin_gpio_anyway[0], ARRAY_SIZE(pin_gpio_anyway));

	return 0;
}
