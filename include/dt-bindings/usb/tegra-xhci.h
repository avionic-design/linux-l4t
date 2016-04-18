#ifndef _DT_BINDINGS_USB_TEGRA_XHCI_H
#define _DT_BINDINGS_USB_TEGRA_XHCI_H

/* bit positions for SS port muxes */
#define XUSB_PADCTL_SS_PORT_MAP_0_SS0 0
#define XUSB_PADCTL_SS_PORT_MAP_0_SS1 4

/* ss_port_map selectors for assigment of USB2 port to USB3 SS ports */
#define XUSB_PADCTL_USB2_P0 0
#define XUSB_PADCTL_USB2_P1 1
#define XUSB_PADCTL_USB2_P2 2
#define XUSB_PADCTL_USB2_DISABLE 7

#define XUSB_PADCTL_SS_PORTMAP(x, y) \
	((XUSB_PADCTL_USB2_##x << XUSB_PADCTL_SS_PORT_MAP_0_SS0) | \
	 (XUSB_PADCTL_USB2_##y << XUSB_PADCTL_SS_PORT_MAP_0_SS1))

/* USB 3.0 padctl mux enables */
#define XUSB_MUX_SS_P0 1
#define XUSB_MUX_SS_P1 2
#define XUSB_UTMI_INDEX 8
#define XUSB_MUX_USB2_P0 (1 << XUSB_UTMI_INDEX)
#define XUSB_MUX_USB2_P1 (1 << (XUSB_UTMI_INDEX + 1))
#define XUSB_MUX_USB2_P2 (1 << (XUSB_UTMI_INDEX + 2))

#endif
