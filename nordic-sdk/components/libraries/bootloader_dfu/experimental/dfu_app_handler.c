/* Copyright (c) 2014 Nordic Semiconductor. All Rights Reserved.
 *
 * The information contained herein is property of Nordic Semiconductor ASA.
 * Terms and conditions of usage are described in detail in NORDIC
 * SEMICONDUCTOR STANDARD SOFTWARE LICENSE AGREEMENT.
 *
 * Licensees are granted free, non-transferable use of the information. NO
 * WARRANTY of ANY KIND is provided. This heading must NOT be removed from
 * the file.
 *
 */

#include <string.h>
#include "dfu_app_handler.h"
#include "bootloader_util.h"
#include "nrf.h"
#include "nrf_sdm.h"
#include "ble_gatts.h"
#include "app_error.h"
#include "dfu_ble_svc.h"
#include "device_manager.h"

#define IRQ_ENABLED            0x01                                            /**< Field identifying if an interrupt is enabled. */
#define MAX_NUMBER_INTERRUPTS  32                                              /**< Maximum number of interrupts available. */

static void                    dfu_app_reset_prepare(void);                    /**< Forward declare of default reset handler. */
static dfu_app_reset_prepare_t m_reset_prepare = dfu_app_reset_prepare;        /**< Callback function to application to prepare for system reset. Allows application to cleanup of service and memory prior to reset. */
static dfu_ble_peer_data_t     m_peer_data;                                    /**< Peer data to be used for data exchange when reseting into DFU mode. */
static dm_handle_t             m_dm_handle;                                    /**< Device Manager handle with instance id's of current BLE connection. */
static bool                    m_dm_handle_valid = false;                      /**< Variable indicating if the Device Manager handle is valid. */


/**@brief Default reset prepare handler if application hasn't registered a handler.
 */
static void dfu_app_reset_prepare(void)
{
    // Reset prepare should be handled by application.
    // This function can be extended to include default handling if application does not implement
    // own handler.
}


/**@brief Function for disabling all interrupts before jumping from bootloader to application.
 */
static void interrupts_disable(void)
{
    uint32_t interrupt_setting_mask;
    uint32_t irq = 0; // We start from first interrupt, i.e. interrupt 0.

    // Fetch the current interrupt settings.
    interrupt_setting_mask = NVIC->ISER[0];

    for (; irq < MAX_NUMBER_INTERRUPTS; irq++)
    {
        if (interrupt_setting_mask & (IRQ_ENABLED << irq))
        {
            // The interrupt was enabled, and hence disable it.
            NVIC_DisableIRQ((IRQn_Type)irq);
        }
    }
}


/**@brief Function for providing peer information to DFU for re-establishing bonded connection in
 *        DFU mode.
 */
static void dfu_app_set_peer_data(void)
{
    uint32_t        err_code;
    dm_sec_keyset_t key_set;

/** [DFU bond sharing] */
    err_code = dm_distributed_keys_get(&m_dm_handle, &key_set);
    APP_ERROR_CHECK(err_code);

    m_peer_data.addr    = key_set.keys_central.p_id_key->id_addr_info;
    m_peer_data.enc_key = *((ble_gap_enc_key_t *)(key_set.keys_central.enc_key.p_enc_key));
    m_peer_data.irk     = key_set.keys_central.p_id_key->id_info;

    err_code = dfu_ble_svc_set_peer_data(&m_peer_data);
    APP_ERROR_CHECK(err_code);
/** [DFU bond sharing] */
}


/**@brief Function for preparing the reset, disabling SoftDevice and jump to the bootloader.
 */
void bootloader_start(void)
{
    m_reset_prepare();

    uint32_t err_code = sd_power_gpregret_set(BOOTLOADER_DFU_START);
    APP_ERROR_CHECK(err_code);

    err_code = sd_softdevice_disable();
    APP_ERROR_CHECK(err_code);

    err_code = sd_softdevice_vector_table_base_set(NRF_UICR->BOOTLOADERADDR);
    APP_ERROR_CHECK(err_code);

    // Commenting out the following block because it brings in unwanted dependencies from bonding.
    // TODO: discuss this with Nordic.
    // if (m_dm_handle_valid)
    // {
    //     dfu_app_set_peer_data();
    // }

    NVIC_ClearPendingIRQ(SWI2_IRQn);
    interrupts_disable();
    bootloader_util_app_start(NRF_UICR->BOOTLOADERADDR);
}


void dfu_app_on_dfu_evt(ble_dfu_t * p_dfu, ble_dfu_evt_t * p_evt)
{
    switch (p_evt->ble_dfu_evt_type)
    {
        case BLE_DFU_START:
            // Starting the bootloader - will cause reset.
            bootloader_start();
            break;

        case BLE_DFU_VALIDATE:
        case BLE_DFU_ACTIVATE_N_RESET:
        case BLE_DFU_SYS_RESET:
        case BLE_DFU_RECEIVE_INIT_DATA:
        case BLE_DFU_RECEIVE_APP_DATA:
        case BLE_DFU_PACKET_WRITE:
        case BLE_DFU_PKT_RCPT_NOTIF_ENABLED:
        case BLE_DFU_PKT_RCPT_NOTIF_DISABLED:
        case BLE_DFU_BYTES_RECEIVED_SEND:
        default:
            {
                // Unsupported event received from DFU Service.
                // Send back BLE_DFU_RESP_VAL_NOT_SUPPORTED message to peer.
                uint32_t err_code = ble_dfu_response_send(p_dfu,
                                                          BLE_DFU_START_PROCEDURE,
                                                          BLE_DFU_RESP_VAL_NOT_SUPPORTED);
                APP_ERROR_CHECK(err_code);
            }
            break;
    }
}


void dfu_app_reset_prepare_set(dfu_app_reset_prepare_t reset_prepare_func)
{
    m_reset_prepare = reset_prepare_func;
}


void dfu_app_set_dm_handle(dm_handle_t const * p_dm_handle)
{
    m_dm_handle_valid = false;

    if (p_dm_handle != NULL)
    {
        m_dm_handle       = *p_dm_handle;
        m_dm_handle_valid = true;
    }
}

