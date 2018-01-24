/*
 * Copyright 2018 Alban Bedel <alban.bedel@avionic-design.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.
 *
 */
#ifndef _DT_BINDINGS_MEDIA_GS3470_H
#define _DT_BINDINGS_MEDIA_GS3470_H

/* Defines for the STAT pin configs */
#define STAT_H_HSYNC		0
#define STAT_V_VSYNC		1
#define STAT_F_DE		2
#define STAT_LOCKED		3
#define STAT_Y_1ANC		4
#define STAT_C_2ANC		5
#define STAT_nDATA_ERROR	6
#define STAT_nVIDEO_ERROR	7
#define STAT_nAUDIO_ERROR	8
#define STAT_EDH_DETECT		9
#define STAT_nCARRIER_DETECT	10
#define STAT_SD_nHD		11
#define STAT_3G_nHD		12

#endif /* _DT_BINDINGS_MEDIA_GS3470_H */
