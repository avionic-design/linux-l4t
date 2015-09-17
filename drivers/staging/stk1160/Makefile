CONFIG_VIDEO_STK1160 := m
CONFIG_VIDEO_STK1160_AC97 := m
KDIR := /lib/modules/$(shell uname -r)/build
DDIR := /lib/modules/$(shell uname -r)/kernel
PWD := $(shell pwd)

obj-stk1160-ac97-$(CONFIG_VIDEO_STK1160_AC97) := stk1160-ac97.o

stk1160-y := 	stk1160-core.o \
		stk1160-v4l.o \
		stk1160-video.o \
		stk1160-i2c.o \
		$(obj-stk1160-ac97-m)

obj-$(CONFIG_VIDEO_STK1160) += stk1160.o

ccflags-y += -Idrivers/media/i2c

build_module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean_archive:
	rm -f stk1160.ko.xz

create_archive: clean_archive build_module
	xz stk1160.ko

default: create_archive
	sudo rmmod stk1160 || true
	sudo cp stk1160.ko.xz $(DDIR)/drivers/media/usb/stk1160
	sudo depmod -a $(DDIR)
