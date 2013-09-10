#ifndef _LINUX_MT9V126_H
#define _LINUX_MT9V126_H 1

#include <linux/videodev2.h>

#define V4L_CID_MT9V126_INV_BRIGHTNESS_METRIC	(V4L2_CID_PRIVATE_BASE+0)
#define V4L_CID_MT9V126_GAIN_METRIC		(V4L2_CID_PRIVATE_BASE+1)

#ifdef __KERNEL__

struct mt9v126_platform_data {
	int reset_gpio;
	int progressive;
};

#endif /* __KERNEL__ */
#endif /* _LINUX_MT9V126_H */
