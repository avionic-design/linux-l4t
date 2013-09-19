dvb_usb_v2-objs := dvb_usb_core.o dvb_usb_urb.o usb_urb.o
obj-$(CONFIG_DVB_USB_V2) += dvb_usb_v2.o

obj-$(CONFIG_CYPRESS_FIRMWARE) += cypress_firmware.o

dvb-usb-ad1456-objs := ad1456.o
obj-$(CONFIG_DVB_USB_AD1456) += dvb-usb-ad1456.o

ccflags-y += -I$(srctree)/drivers/media/dvb/dvb-core
ccflags-y += -I$(srctree)/drivers/media/dvb/frontends
ccflags-y += -I$(srctree)/drivers/media/common/tuners
