#include <linux/of_fdt.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/of.h>
#include <mach/dc.h>

#include "iomap.h"

static int __init of_flat_dt_device_is_available(unsigned long node)
{
	const char *status = of_get_flat_dt_prop(node, "status", NULL);

	return status && !strcmp(status, "okay");
}

#define DC_BUS_ADDRESS_CELLS 1

/*
 * A trivial method for extracting the address of a node from the flat
 * dt.  Assumes that the address can be found in the "reg" node
 * property and is one cell wide.  No bus address mapping is performed.
 */
static phys_addr_t __init of_flat_dt_trivial_physaddr(unsigned long node)
{
	const __be32 *prop;

	prop = of_get_flat_dt_prop(node, "reg", NULL);
	if (!prop)
		return 0;

	return of_read_number(prop, DC_BUS_ADDRESS_CELLS);
}

static const char *const __initdata tegra_display_compat[] = {
	"nvidia,tegra114-dc",
	"nvidia,tegra124-dc",
};

static int __init tegra_dc_node_get_dc_conn(unsigned long node,
		const char *uname, int depth, void *data)
{
	enum tegra_dc_conn_type *dc_conn = (enum tegra_dc_conn_type*)data;
	const char *of_dc_connection;
	phys_addr_t disp_physaddr;
	int disp;

	if (!of_flat_dt_match(node, tegra_display_compat))
		return 0;

	/* Identify dc by display address. */
	disp_physaddr = of_flat_dt_trivial_physaddr(node);

	switch (disp_physaddr) {
	case TEGRA_DISPLAY_BASE:
		disp = 0;
		break;
	case TEGRA_DISPLAY2_BASE:
		disp = 1;
		break;
	default:
		pr_err("tegradc: dc with unexpected physical addr %pa\n",
				&disp_physaddr);
		return 0;
	}

	if (!of_flat_dt_device_is_available(node)) {
		dc_conn[disp] = TEGRA_DC_CONN_NONE;
		return 0;
	}

	of_dc_connection = of_get_flat_dt_prop(node, "nvidia,dc-connection",
			NULL);
	if (!of_dc_connection) {
		pr_err("tegradc: missing nvidia,dc-connection property\n");
		return 0;
	}

	if (!strcmp(of_dc_connection, "external-display"))
		dc_conn[disp] = TEGRA_DC_CONN_EXTERNAL;
	else
		dc_conn[disp] = TEGRA_DC_CONN_INTERNAL;

	return 0;
}

int __init tegra_dc_early_get_dc_connections(enum tegra_dc_conn_type *dc_conn)
{
	return of_scan_flat_dt(tegra_dc_node_get_dc_conn, dc_conn);
}
