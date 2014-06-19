#ifndef _HID_DISPLAY_H
#define _HID_DISPLAY_H 1

/* kernel-only API declarations */
#ifdef __KERNEL__

#ifdef CONFIG_HID_DISPLAY
int hid_display_connect(struct hid_device *);
void hid_display_disconnect(struct hid_device *);
#else
static inline int hid_display_connect(struct hid_device *hid) { return -1; }
static inline void hid_display_disconnect(struct hid_device *hid) { }
#endif

#endif

#endif
