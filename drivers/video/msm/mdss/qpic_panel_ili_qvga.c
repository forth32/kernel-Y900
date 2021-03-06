/* Copyright (c) 2013 - 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/memory.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/io.h>

#include <mach/hardware.h>

#include "mdss_qpic.h"
#include "mdss_qpic_panel.h"

static int panel_io_init(struct qpic_panel_io_desc *panel_io)
{
	int rc;
	if (panel_io->vdd_vreg) {
		rc = regulator_set_voltage(panel_io->vdd_vreg,
			1800000, 1800000);
		if (rc) {
			pr_err("vdd_vreg->set_voltage failed, rc=%d\n", rc);
			return -EINVAL;
		}
	}
	if (panel_io->avdd_vreg) {
		rc = regulator_set_voltage(panel_io->avdd_vreg,
			2700000, 2700000);
		if (rc) {
			pr_err("vdd_vreg->set_voltage failed, rc=%d\n", rc);
			return -EINVAL;
		}
	}
	return 0;
}

static void panel_io_off(struct qpic_panel_io_desc *qpic_panel_io)
{
	if (qpic_panel_io->ad8_gpio)
		gpio_free(qpic_panel_io->ad8_gpio);
	if (qpic_panel_io->cs_gpio)
		gpio_free(qpic_panel_io->cs_gpio);
	if (qpic_panel_io->rst_gpio)
		gpio_free(qpic_panel_io->rst_gpio);
	if (qpic_panel_io->te_gpio)
		gpio_free(qpic_panel_io->te_gpio);
	if (qpic_panel_io->bl_gpio)
		gpio_free(qpic_panel_io->bl_gpio);
	if (qpic_panel_io->vdd_vreg)
		regulator_disable(qpic_panel_io->vdd_vreg);
	if (qpic_panel_io->avdd_vreg)
		regulator_disable(qpic_panel_io->avdd_vreg);
}

void ili9341_off(struct qpic_panel_io_desc *qpic_panel_io)
{
	qpic_send_pkt(OP_SET_DISPLAY_OFF, NULL, 0);
	/* wait for 20 ms after disply off */
	msleep(20);
	panel_io_off(qpic_panel_io);
}

static int panel_io_on(struct qpic_panel_io_desc *qpic_panel_io)
{
	int rc;
	if (qpic_panel_io->vdd_vreg) {
		rc = regulator_enable(qpic_panel_io->vdd_vreg);
		if (rc) {
			pr_err("enable vdd failed, rc=%d\n", rc);
			return -ENODEV;
		}
	}

	if (qpic_panel_io->avdd_vreg) {
		rc = regulator_enable(qpic_panel_io->avdd_vreg);
		if (rc) {
			pr_err("enable avdd failed, rc=%d\n", rc);
			goto power_on_error;
		}
	}

	if ((qpic_panel_io->rst_gpio) &&
		(gpio_request(qpic_panel_io->rst_gpio, "disp_rst_n"))) {
		pr_err("%s request reset gpio failed\n", __func__);
		goto power_on_error;
	}

	if ((qpic_panel_io->cs_gpio) &&
		(gpio_request(qpic_panel_io->cs_gpio, "disp_cs_n"))) {
		pr_err("%s request cs gpio failed\n", __func__);
		goto power_on_error;
	}

	if ((qpic_panel_io->ad8_gpio) &&
		(gpio_request(qpic_panel_io->ad8_gpio, "disp_ad8_n"))) {
		pr_err("%s request ad8 gpio failed\n", __func__);
		goto power_on_error;
	}

	if ((qpic_panel_io->te_gpio) &&
		(gpio_request(qpic_panel_io->te_gpio, "disp_te_n"))) {
		pr_err("%s request te gpio failed\n", __func__);
		goto power_on_error;
	}

	if ((qpic_panel_io->bl_gpio) &&
		(gpio_request(qpic_panel_io->bl_gpio, "disp_bl_n"))) {
		pr_err("%s request bl gpio failed\n", __func__);
		goto power_on_error;
	}
	/* wait for 20 ms after enable gpio as suggested by hw */
	msleep(20);
	return 0;
power_on_error:
	panel_io_off(qpic_panel_io);
	return -EINVAL;
}

int ili9341_on(struct qpic_panel_io_desc *qpic_panel_io)
{
	u8 param[4];
	int ret;
	if (!qpic_panel_io->init) {
		panel_io_init(qpic_panel_io);
		qpic_panel_io->init = true;
	}
	ret = panel_io_on(qpic_panel_io);
	if (ret)
		return ret;
	qpic_send_pkt(OP_SOFT_RESET, NULL, 0);
	/* wait for 120 ms after reset as panel spec suggests */
	msleep(120);
	qpic_send_pkt(OP_SET_DISPLAY_OFF, NULL, 0);
	/* wait for 20 ms after disply off */
	msleep(20);

	/* set memory access control */
	param[0] = 0x48;
	qpic_send_pkt(OP_SET_ADDRESS_MODE, param, 1);
	/* wait for 20 ms after command sent as panel spec suggests */
	msleep(20);

	param[0] = 0x66;
	qpic_send_pkt(OP_SET_PIXEL_FORMAT, param, 1);
	/* wait for 20 ms after command sent as panel spec suggests */
	msleep(20);

	/* set interface */
	param[0] = 1;
	param[1] = 0;
	param[2] = 0;
	qpic_send_pkt(OP_ILI9341_INTERFACE_CONTROL, param, 3);
	/* wait for 20 ms after command sent */
	msleep(20);

	/* exit sleep mode */
	qpic_send_pkt(OP_EXIT_SLEEP_MODE, NULL, 0);
	/* wait for 20 ms after command sent as panel spec suggests */
	msleep(20);

	/* normal mode */
	qpic_send_pkt(OP_ENTER_NORMAL_MODE, NULL, 0);
	/* wait for 20 ms after command sent as panel spec suggests */
	msleep(20);

	/* display on */
	qpic_send_pkt(OP_SET_DISPLAY_ON, NULL, 0);
	/* wait for 20 ms after command sent as panel spec suggests */
	msleep(20);

	param[0] = 0;
	qpic_send_pkt(OP_ILI9341_TEARING_EFFECT_LINE_ON, param, 1);

	/* test */
	param[0] = qpic_read_data(OP_GET_PIXEL_FORMAT, 1);
	pr_debug("Pixel format =%x", param[0]);

	return 0;
}
