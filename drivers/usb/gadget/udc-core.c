/**
 * udc.c - Core UDC Framework
 *
 * Copyright (C) 2010 Texas Instruments
 * Author: Felipe Balbi <balbi@ti.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

/**
 * struct usb_udc - describes one usb device controller
 * @driver - the gadget driver pointer. For use by gadget bus
 * @dev - the child device to the actual controller
 * @gadget - the gadget. For use by the gadget bus code
 *
 * This represents the internal data structure which is used by the gadget bus
 * to hold information about udc driver and gadget together.
 */
struct usb_udc {
	struct usb_gadget_driver	*driver;
	struct usb_gadget		*gadget;
	struct device			dev;
};

static struct bus_type gadget_bus_type;
static DEFINE_MUTEX(udc_lock);
static DEFINE_IDA(udc_ida);

/*
 * We can bind any unused udc to specfic driver after setting manual_binding
 * eg:
 * echo udc-0 > /sys/bus/usb_gadget/drivers/g_serial
 * echo udc-1 > /sys/bus/usb_gadget/drivers/g_mass_storage
 *
 * How to use manual_binding:
 * First, set manual_binding = 1 before drivers and devices are added to bus
 * Second, set manual_binding = 0
 * Third, do manual_binding like above
 */
static bool manual_binding;
module_param(manual_binding, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(manual_binding, "binding udc and gadget driver manully");

/* ------------------------------------------------------------------------- */

int usb_gadget_map_request(struct usb_gadget *gadget,
		struct usb_request *req, int is_in)
{
	if (req->length == 0)
		return 0;

	if (req->num_sgs) {
		int     mapped;

		mapped = dma_map_sg(&gadget->dev, req->sg, req->num_sgs,
				is_in ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
		if (mapped == 0) {
			dev_err(&gadget->dev, "failed to map SGs\n");
			return -EFAULT;
		}

		req->num_mapped_sgs = mapped;
	} else {
		req->dma = dma_map_single(&gadget->dev, req->buf, req->length,
				is_in ? DMA_TO_DEVICE : DMA_FROM_DEVICE);

		if (dma_mapping_error(&gadget->dev, req->dma)) {
			dev_err(&gadget->dev, "failed to map buffer\n");
			return -EFAULT;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(usb_gadget_map_request);

void usb_gadget_unmap_request(struct usb_gadget *gadget,
		struct usb_request *req, int is_in)
{
	if (req->length == 0)
		return;

	if (req->num_mapped_sgs) {
		dma_unmap_sg(&gadget->dev, req->sg, req->num_mapped_sgs,
				is_in ? DMA_TO_DEVICE : DMA_FROM_DEVICE);

		req->num_mapped_sgs = 0;
	} else {
		dma_unmap_single(&gadget->dev, req->dma, req->length,
				is_in ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
	}
}
EXPORT_SYMBOL_GPL(usb_gadget_unmap_request);

/* ------------------------------------------------------------------------- */

void usb_gadget_set_state(struct usb_gadget *gadget,
		enum usb_device_state state)
{
	gadget->state = state;
	sysfs_notify(&gadget->dev.kobj, NULL, "state");
}
EXPORT_SYMBOL_GPL(usb_gadget_set_state);

/* ------------------------------------------------------------------------- */

static int __find_gadget(struct device *dev, void *data)
{
	struct usb_gadget *gadget = data;
	struct usb_udc *udc = container_of(dev, struct usb_udc, dev);

	if (udc->gadget == gadget)
		return 1;
	else
		return 0;
}

static int __find_driver(struct device *dev, void *data)
{
	struct usb_gadget_driver *driver = data;
	struct usb_udc *udc = container_of(dev, struct usb_udc, dev);

	if (udc->driver == driver)
		return 1;
	else
		return 0;
}

static int __find_udc(struct device *dev, void *data)
{
	char *name = data;

	if (strcmp(name, dev_name(dev)) == 0)
		return 1;
	else
		return 0;
}

/**
 * usb_gadget_udc_start - tells usb device controller to start up
 * @gadget: The gadget we want to get started
 * @driver: The driver we want to bind to @gadget
 *
 * This call is issued by the gadget bus driver when it's about
 * to register a gadget driver to the device controller, before
 * calling gadget driver's bind() method.
 *
 * It allows the controller to be powered off until strictly
 * necessary to have it powered on.
 *
 * Returns zero on success, else negative errno.
 */
static inline int usb_gadget_udc_start(struct usb_gadget *gadget,
		struct usb_gadget_driver *driver)
{
	return gadget->ops->udc_start(gadget, driver);
}

/**
 * usb_gadget_udc_stop - tells usb device controller we don't need it anymore
 * @gadget: The device we want to stop activity
 * @driver: The driver to unbind from @gadget
 *
 * This call is issued by the gadget bus driver after calling
 * gadget driver's unbind() method.
 *
 * The details are implementation specific, but it can go as
 * far as powering off UDC completely and disable its data
 * line pullups.
 */
static inline void usb_gadget_udc_stop(struct usb_gadget *gadget,
		struct usb_gadget_driver *driver)
{
	gadget->ops->udc_stop(gadget, driver);
}

/**
 * usb_udc_release - release the usb_udc struct
 * @dev: the dev member within usb_udc
 *
 * This is called by driver's core in order to free memory once the last
 * reference is released.
 */
static void usb_udc_release(struct device *dev)
{
	struct usb_udc *udc;

	udc = container_of(dev, struct usb_udc, dev);
	dev_dbg(dev, "releasing '%s'\n", dev_name(dev));
	kfree(udc);
}

static const struct attribute_group *usb_udc_attr_groups[];

static void usb_udc_nop_release(struct device *dev)
{
	dev_vdbg(dev, "%s\n", __func__);
}

/**
 * usb_add_gadget_udc_release - adds a new gadget to the udc framework
 * @parent: the parent device to this udc. Usually the controller driver's
 * device.
 * @gadget: the gadget to be added to the gadget bus.
 * @release: a gadget release function.
 *
 * Returns zero on success, negative errno otherwise.
 */
int usb_add_gadget_udc_release(struct device *parent, struct usb_gadget *gadget,
		void (*release)(struct device *dev))
{
	struct usb_udc		*udc;
	int			id, ret = -ENOMEM;

	id = ida_simple_get(&udc_ida, 0, 0xffff, GFP_KERNEL);
	if (id < 0)
		return id;

	udc = kzalloc(sizeof(*udc), GFP_KERNEL);
	if (!udc)
		goto err1;

	dev_set_name(&gadget->dev, "gadget");
	gadget->dev.parent = parent;

	dma_set_coherent_mask(&gadget->dev, parent->coherent_dma_mask);
	gadget->dev.dma_parms = parent->dma_parms;
	gadget->dev.dma_mask = parent->dma_mask;

	if (release)
		gadget->dev.release = release;
	else
		gadget->dev.release = usb_udc_nop_release;

	ret = device_register(&gadget->dev);
	if (ret)
		goto err2;

	device_initialize(&udc->dev);
	udc->dev.release = usb_udc_release;
	udc->dev.bus = &gadget_bus_type;
	udc->dev.groups = usb_udc_attr_groups;
	udc->dev.parent = parent;
	udc->dev.id = id;
	ret = dev_set_name(&udc->dev, "%s-%d", "udc", id);
	if (ret)
		goto err3;

	udc->gadget = gadget;

	mutex_lock(&udc_lock);

	ret = device_add(&udc->dev);
	if (ret)
		goto err4;

	usb_gadget_set_state(gadget, USB_STATE_NOTATTACHED);

	mutex_unlock(&udc_lock);

	return 0;

err4:
	mutex_unlock(&udc_lock);
err3:
	put_device(&udc->dev);
err2:
	put_device(&gadget->dev);
	kfree(udc);

err1:
	ida_simple_remove(&udc_ida, id);
	return ret;
}
EXPORT_SYMBOL_GPL(usb_add_gadget_udc_release);

/**
 * usb_add_gadget_udc - adds a new gadget to the udc framework
 * @parent: the parent device to this udc. Usually the controller
 * driver's device.
 * @gadget: the gadget to be added to the gadget bus
 *
 * Returns zero on success, negative errno otherwise.
 */
int usb_add_gadget_udc(struct device *parent, struct usb_gadget *gadget)
{
	return usb_add_gadget_udc_release(parent, gadget, NULL);
}
EXPORT_SYMBOL_GPL(usb_add_gadget_udc);

static void usb_gadget_remove_driver(struct usb_udc *udc)
{
	dev_dbg(&udc->dev, "unregistering UDC driver [%s]\n",
			udc->gadget->name);

	kobject_uevent(&udc->dev.kobj, KOBJ_CHANGE);

	usb_gadget_disconnect(udc->gadget);
	udc->driver->disconnect(udc->gadget);
	udc->driver->unbind(udc->gadget);
	usb_gadget_udc_stop(udc->gadget, NULL);

	udc->driver = NULL;
	udc->gadget->dev.driver = NULL;
}

/**
 * usb_del_gadget_udc - deletes @udc from gadget bus
 * @gadget: the gadget to be removed.
 *
 * This, will call usb_gadget_unregister_driver() if
 * the @udc is still busy.
 */
void usb_del_gadget_udc(struct usb_gadget *gadget)
{
	struct usb_udc *udc;
	int id;
	struct device *dev;

	mutex_lock(&udc_lock);
	dev = bus_find_device(&gadget_bus_type, NULL, gadget, __find_gadget);
	mutex_unlock(&udc_lock);
	if (dev)
		goto found;

	dev_err(gadget->dev.parent, "gadget not registered.\n");

	return;

found:
	dev_vdbg(gadget->dev.parent, "unregistering gadget\n");

	udc = container_of(dev, struct usb_udc, dev);
	kobject_uevent(&udc->dev.kobj, KOBJ_REMOVE);
	id = udc->dev.id;
	device_unregister(&udc->dev);
	ida_simple_remove(&udc_ida, id);
	device_unregister(&gadget->dev);
}
EXPORT_SYMBOL_GPL(usb_del_gadget_udc);

/* ------------------------------------------------------------------------- */

static int udc_bind_to_driver(struct usb_udc *udc, struct usb_gadget_driver *driver)
{
	int ret;

	dev_dbg(&udc->dev, "registering UDC driver [%s]\n",
			driver->function);

	udc->driver = driver;
	udc->gadget->dev.driver = &driver->driver;

	ret = driver->bind(udc->gadget, driver);
	if (ret)
		goto err1;
	ret = usb_gadget_udc_start(udc->gadget, driver);
	if (ret) {
		driver->unbind(udc->gadget);
		goto err1;
	}
	/*
	 * HACK: The Android gadget driver disconnects the gadget
	 * on bind and expects the gadget to stay disconnected until
	 * it calls usb_gadget_connect when userspace is ready. Remove
	 * the call to usb_gadget_connect bellow to avoid enabling the
	 * pullup before userspace is ready.
	 */
#ifdef CONFIG_USB_GADGET_AUTO_CONNECT
	usb_gadget_connect(udc->gadget);
#endif

	kobject_uevent(&udc->dev.kobj, KOBJ_CHANGE);
	return 0;
err1:
	dev_err(&udc->dev, "failed to start %s: %d\n",
			udc->driver->function, ret);
	udc->driver = NULL;
	udc->gadget->dev.driver = NULL;
	return ret;
}

int udc_attach_driver(const char *name, struct usb_gadget_driver *driver)
{
	struct usb_udc *udc;
	struct device *dev;
	int ret = -ENODEV;

	mutex_lock(&udc_lock);
	dev = bus_find_device(&gadget_bus_type,
			NULL, (char *)name, __find_udc);
	if (!dev)
		goto out;
	udc = container_of(dev, struct usb_udc, dev);
	if (udc->driver) {
		ret = -EBUSY;
		goto out;
	}
	ret = udc_bind_to_driver(udc, driver);
out:
	mutex_unlock(&udc_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(udc_attach_driver);

int usb_gadget_probe_driver(struct usb_gadget_driver *driver)
{
	struct device_driver *drv;

	if (!driver || !driver->bind || !driver->setup)
		return -EINVAL;

	drv = &driver->driver;
	drv->bus = &gadget_bus_type;

	return driver_register(drv);
}
EXPORT_SYMBOL_GPL(usb_gadget_probe_driver);

int usb_gadget_unregister_driver(struct usb_gadget_driver *driver)
{
	if (!driver || !driver->unbind)
		return -EINVAL;

	mutex_lock(&udc_lock);
	driver_unregister(&driver->driver);
	mutex_unlock(&udc_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(usb_gadget_unregister_driver);

/* ------------------------------------------------------------------------- */

static ssize_t usb_udc_srp_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t n)
{
	struct usb_udc		*udc = container_of(dev, struct usb_udc, dev);

	if (sysfs_streq(buf, "1"))
		usb_gadget_wakeup(udc->gadget);

	return n;
}
static DEVICE_ATTR(srp, S_IWUSR, NULL, usb_udc_srp_store);

static ssize_t usb_udc_softconn_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t n)
{
	struct usb_udc		*udc = container_of(dev, struct usb_udc, dev);

	if (sysfs_streq(buf, "connect")) {
		usb_gadget_udc_start(udc->gadget, udc->driver);
		usb_gadget_connect(udc->gadget);
	} else if (sysfs_streq(buf, "disconnect")) {
		usb_gadget_disconnect(udc->gadget);
		usb_gadget_udc_stop(udc->gadget, udc->driver);
	} else {
		dev_err(dev, "unsupported command '%s'\n", buf);
		return -EINVAL;
	}

	return n;
}
static DEVICE_ATTR(soft_connect, S_IWUSR, NULL, usb_udc_softconn_store);

static ssize_t usb_gadget_state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct usb_udc		*udc = container_of(dev, struct usb_udc, dev);
	struct usb_gadget	*gadget = udc->gadget;

	return sprintf(buf, "%s\n", usb_state_string(gadget->state));
}
static DEVICE_ATTR(state, S_IRUGO, usb_gadget_state_show, NULL);

#define USB_UDC_SPEED_ATTR(name, param)					\
ssize_t usb_udc_##param##_show(struct device *dev,			\
		struct device_attribute *attr, char *buf)		\
{									\
	struct usb_udc *udc = container_of(dev, struct usb_udc, dev);	\
	return snprintf(buf, PAGE_SIZE, "%s\n",				\
			usb_speed_string(udc->gadget->param));		\
}									\
static DEVICE_ATTR(name, S_IRUGO, usb_udc_##param##_show, NULL)

static USB_UDC_SPEED_ATTR(current_speed, speed);
static USB_UDC_SPEED_ATTR(maximum_speed, max_speed);

#define USB_UDC_ATTR(name)					\
ssize_t usb_udc_##name##_show(struct device *dev,		\
		struct device_attribute *attr, char *buf)	\
{								\
	struct usb_udc		*udc = container_of(dev, struct usb_udc, dev); \
	struct usb_gadget	*gadget = udc->gadget;		\
								\
	return snprintf(buf, PAGE_SIZE, "%d\n", gadget->name);	\
}								\
static DEVICE_ATTR(name, S_IRUGO, usb_udc_##name##_show, NULL)

static USB_UDC_ATTR(is_otg);
static USB_UDC_ATTR(is_a_peripheral);
static USB_UDC_ATTR(b_hnp_enable);
static USB_UDC_ATTR(a_hnp_support);
static USB_UDC_ATTR(a_alt_hnp_support);

static struct attribute *usb_udc_attrs[] = {
	&dev_attr_srp.attr,
	&dev_attr_soft_connect.attr,
	&dev_attr_state.attr,
	&dev_attr_current_speed.attr,
	&dev_attr_maximum_speed.attr,

	&dev_attr_is_otg.attr,
	&dev_attr_is_a_peripheral.attr,
	&dev_attr_b_hnp_enable.attr,
	&dev_attr_a_hnp_support.attr,
	&dev_attr_a_alt_hnp_support.attr,
	NULL,
};

static const struct attribute_group usb_udc_attr_group = {
	.attrs = usb_udc_attrs,
};

static const struct attribute_group *usb_udc_attr_groups[] = {
	&usb_udc_attr_group,
	NULL,
};

static int usb_udc_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct usb_udc		*udc = container_of(dev, struct usb_udc, dev);
	int			ret;

	ret = add_uevent_var(env, "USB_UDC_NAME=%s", udc->gadget->name);
	if (ret) {
		dev_err(dev, "failed to add uevent USB_UDC_NAME\n");
		return ret;
	}

	if (udc->driver) {
		ret = add_uevent_var(env, "USB_UDC_DRIVER=%s",
				udc->driver->function);
		if (ret) {
			dev_err(dev, "failed to add uevent USB_UDC_DRIVER\n");
			return ret;
		}
	}

	return 0;
}

static int usb_gadget_match(struct device *dev, struct device_driver *drv)
{
	struct usb_udc *udc;
	struct usb_gadget_driver *driver =
		container_of(drv, struct usb_gadget_driver, driver);
	bool dev_not_in_use = true, driver_not_in_use = true;

	if (manual_binding)
		return 0;

	dev_dbg(dev, "%s: driver: %s\n", __func__, drv->name);

	udc = container_of(dev, struct usb_udc, dev);

	if (udc->driver)
		dev_not_in_use = false;

	dev = bus_find_device(&gadget_bus_type, NULL, driver, __find_driver);
	if (dev)
		driver_not_in_use = false;

	if (dev_not_in_use && driver_not_in_use)
		return 1;
	else
		return 0;
}

static int usb_gadget_probe(struct device *dev)
{
	struct usb_udc *udc = container_of(dev, struct usb_udc, dev);
	struct device_driver *drv = dev->driver;
	struct usb_gadget_driver *driver =
		container_of(drv, struct usb_gadget_driver, driver);

	return udc_bind_to_driver(udc, driver);
}

static int usb_gadget_remove(struct device *dev)
{
	struct usb_udc		*udc =
		container_of(dev, struct usb_udc, dev);

	usb_gadget_remove_driver(udc);

	return 0;
}

static struct bus_type gadget_bus_type = {
	.name =		"usb_gadget",
	.match =	usb_gadget_match,
	.probe =	usb_gadget_probe,
	.uevent =	usb_udc_uevent,
	.remove =	usb_gadget_remove,
};

static int __init usb_udc_init(void)
{
	int ret = bus_register(&gadget_bus_type);

	if (ret)
		pr_err("failed to register gadget bus: %d\n", ret);

	return ret;

}
subsys_initcall(usb_udc_init);

static void __exit usb_udc_exit(void)
{
	bus_unregister(&gadget_bus_type);
}
module_exit(usb_udc_exit);

MODULE_DESCRIPTION("UDC Framework");
MODULE_AUTHOR("Felipe Balbi <balbi@ti.com>");
MODULE_LICENSE("GPL v2");
