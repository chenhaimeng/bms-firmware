/*
 * Copyright (c) The Libre Solar Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bms.h"
#include "board.h"
#include "helper.h"

#include <stdio.h>

static float ocv_lfp[] = {
    3.392F, 3.314F, 3.309F, 3.308F, 3.304F, 3.296F, 3.283F, 3.275F, 3.271F, 3.268F, 3.265F,
    3.264F, 3.262F, 3.252F, 3.240F, 3.226F, 3.213F, 3.190F, 3.177F, 3.132F, 2.833F
};

void bms_init_status(BmsStatus *status)
{
    status->chg_enable = true;
    status->dis_enable = true;
}

void bms_init_config(BmsConfig *conf, int type, float nominal_capacity)
{
    conf->auto_balancing_enabled = true;
    conf->bal_idle_delay = 1800;            // default: 30 minutes
    conf->bal_idle_current = 0.1F;          // A
    conf->bal_cell_voltage_diff = 0.01F;    // 10 mV

    conf->thermistor_beta = 3435;  // typical value for Semitec 103AT-5 thermistor

    conf->nominal_capacity_Ah = nominal_capacity;

    if (conf->nominal_capacity_Ah < BOARD_MAX_CURRENT) {
        // 1C should be safe for all batteries
        conf->dis_oc_limit = conf->nominal_capacity_Ah;
        conf->chg_oc_limit = conf->nominal_capacity_Ah;
    }
    else {
        conf->dis_oc_limit = BOARD_MAX_CURRENT;
        conf->chg_oc_limit = BOARD_MAX_CURRENT;
    }
    conf->dis_oc_delay_ms  = 320;
    conf->chg_oc_delay_ms  = 320;

    conf->dis_sc_limit = conf->dis_oc_limit * 2;
    conf->dis_sc_delay_us  = 200;

    conf->dis_ut_limit = -20;
    conf->dis_ot_limit =  45;
    conf->chg_ut_limit =   0;
    conf->chg_ot_limit =  45;
    conf->t_limit_hyst =   5;

    conf->shunt_res_mOhm = BOARD_SHUNT_RESISTOR;

    conf->cell_ov_delay_ms = 2000;
    conf->cell_uv_delay_ms = 2000;

    switch (type)
    {
        case CELL_TYPE_LFP:
            conf->cell_ov_limit         = 3.80F;
            conf->cell_chg_voltage      = 3.55F;
            conf->cell_ov_reset         = 3.40F;
            conf->bal_cell_voltage_min  = 3.30F;
            conf->cell_uv_reset         = 3.10F;
            conf->cell_dis_voltage      = 2.80F;
            conf->cell_uv_limit         = 2.50F;  // most cells survive even 2.0V, but we should
                                                  // keep some margin for further self-discharge
            conf->ocv = ocv_lfp;
            conf->num_ocv_points = sizeof(ocv_lfp)/sizeof(float);
            break;
        case CELL_TYPE_NMC:
            conf->cell_ov_limit         = 4.25F;
            conf->cell_chg_voltage      = 4.20F;
            conf->cell_ov_reset         = 4.05F;
            conf->bal_cell_voltage_min  = 3.80F;
            conf->cell_uv_reset         = 3.50F;
            conf->cell_dis_voltage      = 3.20F;
            conf->cell_uv_limit         = 3.00F;
            // ToDo: Use typical OCV curve for NMC cells
            conf->ocv = NULL;
            conf->num_ocv_points = 0;
            break;
        case CELL_TYPE_NMC_HV:
            conf->cell_ov_limit         = 4.35F;
            conf->cell_chg_voltage      = 4.30F;
            conf->cell_ov_reset         = 4.15F;
            conf->bal_cell_voltage_min  = 3.80F;
            conf->cell_uv_reset         = 3.50F;
            conf->cell_dis_voltage      = 3.20F;
            conf->cell_uv_limit         = 3.00F;
            // ToDo: Use typical OCV curve for NMC_HV cells
            conf->ocv = NULL;
            conf->num_ocv_points = 0;
            break;
        case CELL_TYPE_LTO:
            conf->cell_ov_limit         = 2.85F;
            conf->cell_chg_voltage      = 2.80F;
            conf->cell_ov_reset         = 2.70F;
            conf->bal_cell_voltage_min  = 2.50F;
            conf->cell_uv_reset         = 2.10F;
            conf->cell_dis_voltage      = 2.00F;
            conf->cell_uv_limit         = 1.90F;
            // ToDo: Use typical OCV curve for LTO cells
            conf->ocv = NULL;
            conf->num_ocv_points = 0;
            break;
        case CELL_TYPE_CUSTOM:
            break;
    }
}

void bms_state_machine(BmsConfig *conf, BmsStatus *status)
{
    bms_handle_errors(conf, status);

    switch(status->state) {
        case BMS_STATE_OFF:
            if (bms_startup_inhibit()) {
                return;
            }

            if (bms_dis_allowed(status)) {
                bms_dis_switch(conf, status, true);
                status->state = BMS_STATE_DIS;
                printf("Going to state DIS\n");
            }
            else if (bms_chg_allowed(status)) {
                bms_chg_switch(conf, status, true);
                status->state = BMS_STATE_CHG;
                printf("Going to state CHG\n");
            }
            break;
        case BMS_STATE_CHG:
            if (!bms_chg_allowed(status)) {
                bms_chg_switch(conf, status, false);
                bms_dis_switch(conf, status, false);    // if on because of ideal diode control
                status->state = BMS_STATE_OFF;
                printf("Going back to state OFF\n");
            }
            else if (bms_dis_allowed(status)) {
                bms_dis_switch(conf, status, true);
                status->state = BMS_STATE_NORMAL;
                printf("Going to state NORMAL\n");
            }
            else {
                // ideal diode control for discharge MOSFET (with hysteresis)
                if (status->pack_current > 0.5F) {
                    bms_dis_switch(conf, status, true);
                }
                else if (status->pack_current < 0.1F) {
                    bms_dis_switch(conf, status, false);
                }
            }
            break;
        case BMS_STATE_DIS:
            if (!bms_dis_allowed(status)) {
                bms_dis_switch(conf, status, false);
                bms_chg_switch(conf, status, false);    // if on because of ideal diode control
                status->state = BMS_STATE_OFF;
                printf("Going back to state OFF\n");
            }
            else if (bms_chg_allowed(status)) {
                bms_chg_switch(conf, status, true);
                status->state = BMS_STATE_NORMAL;
                printf("Going to state NORMAL\n");
            }
            else {
                // ideal diode control for charge MOSFET (with hysteresis)
                if (status->pack_current < -0.5F) {
                    bms_chg_switch(conf, status, true);
                }
                else if (status->pack_current > -0.1F) {
                    bms_chg_switch(conf, status, false);
                }
            }
            break;
        case BMS_STATE_NORMAL:
            if (!bms_dis_allowed(status)) {
                bms_dis_switch(conf, status, false);
                status->state = BMS_STATE_CHG;
                printf("Going back to state CHG\n");
            }
            else if (!bms_chg_allowed(status)) {
                bms_chg_switch(conf, status, false);
                status->state = BMS_STATE_DIS;
                printf("Going back to state DIS\n");
            }
            break;
    }
}

bool bms_chg_error(uint32_t error_flags)
{
    return (error_flags & (1U << BMS_ERR_CELL_OVERVOLTAGE))
        || (error_flags & (1U << BMS_ERR_CHG_OVERCURRENT))
        || (error_flags & (1U << BMS_ERR_OPEN_WIRE))
        || (error_flags & (1U << BMS_ERR_CHG_UNDERTEMP))
        || (error_flags & (1U << BMS_ERR_CHG_OVERTEMP))
        || (error_flags & (1U << BMS_ERR_INT_OVERTEMP))
        || (error_flags & (1U << BMS_ERR_CELL_FAILURE))
        || (error_flags & (1U << BMS_ERR_CHG_OFF));
}

bool bms_dis_error(uint32_t error_flags)
{
    return (error_flags & (1U << BMS_ERR_CELL_UNDERVOLTAGE))
        || (error_flags & (1U << BMS_ERR_SHORT_CIRCUIT))
        || (error_flags & (1U << BMS_ERR_DIS_OVERCURRENT))
        || (error_flags & (1U << BMS_ERR_OPEN_WIRE))
        || (error_flags & (1U << BMS_ERR_DIS_UNDERTEMP))
        || (error_flags & (1U << BMS_ERR_DIS_OVERTEMP))
        || (error_flags & (1U << BMS_ERR_INT_OVERTEMP))
        || (error_flags & (1U << BMS_ERR_CELL_FAILURE))
        || (error_flags & (1U << BMS_ERR_DIS_OFF));
}

bool bms_chg_allowed(BmsStatus *status)
{
    return !bms_chg_error(status->error_flags & ~BMS_ERR_CHG_OFF)
        && !status->full && status->chg_enable;
}

bool bms_dis_allowed(BmsStatus *status)
{
    return !bms_dis_error(status->error_flags & ~BMS_ERR_CHG_OFF)
        && !status->empty && status->dis_enable;
}

bool bms_balancing_allowed(BmsConfig *conf, BmsStatus *status)
{
    int idle_sec = uptime() - status->no_idle_timestamp;
    float voltage_diff = status->cell_voltage_max -
        status->cell_voltage_min;

    return idle_sec >= conf->bal_idle_delay &&
        status->cell_voltage_max > conf->bal_cell_voltage_min &&
        voltage_diff > conf->bal_cell_voltage_diff;
}
