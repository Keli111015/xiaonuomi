

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bluetooth.h>
#include <ble_lib_api.h>
#include <gatt.h>
#include "hci_core.h"
#include <hci_driver.h>
#include <aos/kernel.h>
#include <aos/list.h>

#include "qm.h"

static int init_count = 0;
static qm_sem_t init_sem = {0};

static void bt_enable_cb(int err)
{
    QM_LOGD("1", "bt_enable_cb");
}

void qm_ble_hal_comm_init(void)
{
    if(!init_count){
        
        ble_controller_init(configMAX_PRIORITIES - 3);
        hci_driver_init();
        bt_enable(bt_enable_cb);
    }
    init_count++;
}

void qm_ble_hal_comm_deinit(void)
{
    init_count--;

    if(!init_count){
        bt_disable();
        ble_controller_deinit();
    }
}