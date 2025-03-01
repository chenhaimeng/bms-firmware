/*
 * Copyright (c) The Libre Solar Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bms.h"
#include "board.h"

#include "bq769x0_tests.h"
#include "bq769x2_tests.h"
#include "common_tests.h"
#include "helper_tests.h"
#include "isl94202_tests.h"

#include "unity.h"

#include <stdio.h>

#include <zephyr.h>
#include <device.h>

#ifdef CONFIG_ARCH_POSIX
#include "posix_board_if.h"
#endif

extern "C" {

void setUp (void) {}
void tearDown (void) {}

} /* extern "C" */

BmsConfig bms_conf;
BmsStatus bms_status;

float OCV[] = { // 100, 95, ..., 0 %
    3.392, 3.314, 3.309, 3.308, 3.304, 3.296, 3.283, 3.275, 3.271, 3.268, 3.265,
    3.264, 3.262, 3.252, 3.240, 3.226, 3.213, 3.190, 3.177, 3.132, 2.833
};

void setup()
{
    bms_conf.dis_sc_limit  = 35.0;
    bms_conf.dis_sc_delay_us  = 200;
    bms_apply_dis_scp(&bms_conf);

    bms_conf.dis_oc_limit  = 25.0;
    bms_conf.dis_oc_delay_ms  = 320;
    bms_apply_dis_ocp(&bms_conf);

    bms_conf.chg_oc_limit  = 20.0;
    bms_conf.chg_oc_delay_ms  = 320;
    bms_apply_chg_ocp(&bms_conf);

    bms_conf.cell_ov_limit = 3.65;
    bms_conf.cell_ov_delay_ms = 2000;
    bms_apply_cell_ovp(&bms_conf);

    bms_conf.cell_uv_limit = 2.8;
    bms_conf.cell_uv_delay_ms = 2000;
    bms_apply_cell_uvp(&bms_conf);

    bms_conf.dis_ut_limit = -20;
    bms_conf.dis_ot_limit = 45;
    bms_conf.chg_ut_limit = 0;
    bms_conf.chg_ot_limit = 45;
    bms_conf.t_limit_hyst = 2;
    bms_apply_temp_limits(&bms_conf);

    bms_conf.shunt_res_mOhm = BOARD_SHUNT_RESISTOR;
    bms_conf.ocv = OCV;
    bms_conf.num_ocv_points = sizeof(OCV)/sizeof(float);

    bms_conf.nominal_capacity_Ah = 45.0;
    bms_status.connected_cells = 4;  // ToDo: Function to determine number of cells automatically

    // get voltage and temperature measurements before switching on
    bms_update(&bms_conf, &bms_status);

    bms_conf.bal_cell_voltage_min = 3.2;
    bms_conf.bal_idle_delay = 10 * 60;
    bms_conf.bal_cell_voltage_diff = 0.01;
    bms_conf.bal_idle_current = 0.1;
    bms_conf.auto_balancing_enabled = true;
    bms_apply_balancing(&bms_conf, &bms_status);

    bms_update(&bms_conf, &bms_status);
    bms_soc_reset(&bms_conf, &bms_status, -1);
    bms_dis_switch(&bms_conf, &bms_status, true);
    bms_chg_switch(&bms_conf, &bms_status, true);
}

void main(void)
{
    int err = 0;

    k_sleep(K_MSEC(100));

    err += common_tests();

#ifdef CONFIG_BQ769X0
    err += bq769x0_tests();
#elif defined(CONFIG_BQ769X2)
    err += bq769x2_tests();
#elif defined(CONFIG_ISL94202)
    err += isl94202_tests();
#endif

    err += helper_tests();

#ifdef CONFIG_ARCH_POSIX
    posix_exit(err);
#endif
}
