static int of_property_read_optional_u8(struct device_node *np,
					const char *prop,
					u8 *data)
{
	u32 val;
	int err;

	err = of_property_read_u32(np, prop, &val);
	if (err >= 0) {
		if (val > 0xFF)
			return -ERANGE;
		*data = val;
	} else if (err != -EINVAL)
		return err;

	return 0;
}

static int tegra_usb_parse_host_dt(struct platform_device *pdev,
				struct tegra_usb_host_mode_data *hdata)
{
	struct device_node *np = pdev->dev.of_node;

	hdata->vbus_gpio = of_get_named_gpio(np, "nvidia,vbus-gpio", 0);
	if (hdata->vbus_gpio == -EPROBE_DEFER)
		return hdata->vbus_gpio;
	if (hdata->vbus_gpio < 0)
		hdata->vbus_gpio = -1;

	hdata->hot_plug =
		of_property_read_bool(np, "nvidia,hotplug");
	hdata->remote_wakeup_supported =
		of_property_read_bool(np, "nvidia,remote-wakeup-supported");
	hdata->power_off_on_suspend =
		of_property_read_bool(np, "nvidia,power-off-on-suspend");
	hdata->turn_off_vbus_on_lp0 =
		of_property_read_bool(np, "nvidia,disable-vbus-on-lp0");
	hdata->support_y_cable =
		of_property_read_bool(np, "nvidia,support-y-cable");

	return 0;
}

static int tegra_usb_parse_device_dt(struct platform_device *pdev,
				struct tegra_usb_dev_mode_data *ddata)
{
	struct device_node *np = pdev->dev.of_node;
	u32 val;
	int err;

	ddata->vbus_gpio = of_get_named_gpio(np, "nvidia,vbus-gpio", 0);
	if (ddata->vbus_gpio == -EPROBE_DEFER)
		return ddata->vbus_gpio;
	if (ddata->vbus_gpio < 0)
		ddata->vbus_gpio = -1;

	ddata->charging_supported =
		of_property_read_bool(np, "nvidia,charging-supported");
	ddata->remote_wakeup_supported =
		of_property_read_bool(np, "nvidia,remote-wakeup-supported");
	ddata->is_xhci =
		of_property_read_bool(np, "nvidia,is-xhci");

	if (ddata->charging_supported) {
		err = of_property_read_u32(np, "nvidia,dcp-current-limit", &val);
		if (err >= 0)
			ddata->dcp_current_limit_ma = val;
		else if (err != -EINVAL)
			return err;

		err = of_property_read_u32(np, "nvidia,qc2-current-limit", &val);
		if (err >= 0)
			ddata->qc2_current_limit_ma = val;
		else if (err != -EINVAL)
			return err;
	}

	return 0;
}

static int tegra_usb_parse_utmi_dt(struct platform_device *pdev,
				struct tegra_utmi_config *utmi)
{
	struct device_node *np = pdev->dev.of_node;
	u32 val;
	int err;

	err = of_property_read_optional_u8(
		np, "nvidia,hssync-start-delay", &utmi->hssync_start_delay);
	if (err)
		return err;

	err = of_property_read_optional_u8(
		np, "nvidia,elastic-limit", &utmi->elastic_limit);
	if (err)
		return err;

	err = of_property_read_optional_u8(
		np, "nvidia,idle-wait-delay", &utmi->idle_wait_delay);
	if (err)
		return err;

	err = of_property_read_optional_u8(
		np, "nvidia,term-range-adj", &utmi->term_range_adj);

	err = of_property_read_optional_u8(
		np, "nvidia,xcvr-setup", &utmi->xcvr_setup);
	if (err)
		return err;

	err = of_property_read_optional_u8(
		np, "nvidia,xcvr-lsfslew", &utmi->xcvr_lsfslew);
	if (err)
		return err;

	err = of_property_read_optional_u8(
		np, "nvidia,xcvr-lsrslew", &utmi->xcvr_lsrslew);
	if (err)
		return err;

	err = of_property_read_optional_u8(
		np, "nvidia,xcvr-setup-offset", &utmi->xcvr_setup_offset);
	if (err)
		return err;

	err = of_property_read_optional_u8(
		np, "nvidia,xcvr-use-lsb", &utmi->xcvr_use_lsb);
	if (err)
		return err;

	err = of_property_read_optional_u8(
		np, "nvidia,xcvr-use-fuses", &utmi->xcvr_use_fuses);
	if (err)
		return err;

	err = of_property_read_optional_u8(
		np, "nvidia,vbus-oc-map", &utmi->vbus_oc_map);
	if (err)
		return err;

	err = of_property_read_u32(np, "nvidia,xcvr-hsslew-lsb", &val);
	if (err >= 0)
		utmi->xcvr_hsslew_lsb = val;
	else if (err != -EINVAL)
		return err;

	err = of_property_read_u32(np, "nvidia,xcvr-hsslew-msb", &val);
	if (err >= 0)
		utmi->xcvr_hsslew_msb = val;
	else if (err != -EINVAL)
		return err;

	return 0;
}

static void tegra_usb_clear_pdata(void *data)
{
	struct platform_device *pdev = data;
	pdev->dev.platform_data = NULL;
}

static int tegra_usb_parse_dt(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct tegra_usb_platform_data *pdata;
	u32 val;
	int err;

	if (pdev->dev.platform_data)
		return 0;

	if (!np)
		return -EINVAL;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	pdata->port_otg =
		of_property_read_bool(np, "nvidia,port-otg");
	pdata->has_hostpc =
		of_property_read_bool(np, "nvidia,has-hostpc");
	pdata->unaligned_dma_buf_supported =
		of_property_read_bool(np, "nvidia,unaligned-dma");
	pdata->support_pmu_vbus =
		of_property_read_bool(np, "nvidia,pmu-vbus");

	err = of_property_read_u32(np, "nvidia,phy-interface", &val);
	if (err) {
		dev_err(&pdev->dev, "Failed to get phy-interface\n");
		return err;
	}
	pdata->phy_intf = val;

	err = of_property_read_u32(np, "nvidia,mode", &val);
	if (err) {
		dev_err(&pdev->dev, "Failed to get mode\n");
		return err;
	}
	pdata->op_mode = val;

	err = of_property_read_u32(np, "nvidia,id-det-type", &val);
	if (err) {
		dev_err(&pdev->dev, "Failed to get id-det-type, assume default\n");
		val = TEGRA_USB_ID;
	}
	pdata->id_det_type = val;

	err = of_property_read_string(np, "nvidia,id-extcon-dev-name",
			&pdata->id_extcon_dev_name);
	if (err)
		dev_dbg(&pdev->dev, "Failed to get id-extcon-dev-name\n");

	err = of_property_read_string(np, "nvidia,vbus-extcon-dev-name",
			&pdata->vbus_extcon_dev_name);
	if (err)
		dev_dbg(&pdev->dev, "Failed to get id-extcon-dev-name\n");

	switch (pdata->op_mode) {
	case TEGRA_USB_OPMODE_DEVICE:
		err = tegra_usb_parse_device_dt(pdev, &pdata->u_data.dev);
		break;
	case TEGRA_USB_OPMODE_HOST:
		err = tegra_usb_parse_host_dt(pdev, &pdata->u_data.host);
		break;
	default:
		dev_err(&pdev->dev, "Invalid device mode: %d\n",
			pdata->op_mode);
		return -EINVAL;
	}

	if (err) {
		dev_err(&pdev->dev, "Failed to get mode config\n");
		return err;
	}

	switch (pdata->phy_intf) {
	case TEGRA_USB_PHY_INTF_UTMI:
		err = tegra_usb_parse_utmi_dt(pdev, &pdata->u_cfg.utmi);
		break;
	default:
		dev_err(&pdev->dev, "Unsupported PHY type: %d\n",
			pdata->phy_intf);
		return -EINVAL;
	}

	if (err) {
		dev_err(&pdev->dev, "Failed to get phy config\n");
		return err;
	}

	/* Add the handler to clear platform data */
	err = devm_add_action(&pdev->dev, tegra_usb_clear_pdata, pdev);
	if (err)
		return err;
	pdev->dev.platform_data = pdata;

	return 0;
}
