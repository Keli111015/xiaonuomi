/*
 * Copyright (c) 2013-2015 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Doubly-linked list implementation
 *
 * Doubly-linked list implementation using inline macros/functions.
 * This API is not thread safe, and thus if a list is used across threads,
 * calls to functions must be protected with synchronization primitives.
 *
 * The lists are expected to be initialized such that both the head and tail
 * pointers point to the list itself.  Initializing the lists in such a fashion
 * simplifies the adding and removing of nodes to/from the list.
 */

#ifndef _IOT_MIDDLEWARE_H_
#define _IOT_MIDDLEWARE_H_

#include "qm.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BAIDU_MESH_DATA_LEN       (4)

#define XJIANG_TIMER_TIMEOUT_MS         (100)
#define XJIANG_TIMER_CONFIG_TIMEOUT_MS  (30 * 60 *1000)
#define XJIANG_TIMER_REPORT_TIMEOUT_MS  (360)

#define BAIDU_MESH_ON_OFF_ID        (0x100)


#pragma pack(1)
typedef struct 
{
    int len;
    struct
    {
        uint16_t baidu_id;
        union _data_param 
        {
            uint8_t *pdata;
            uint8_t buff[BAIDU_MESH_DATA_LEN];
        }data_param;
    }payload;
}baidu_mesh_data_t;
#pragma pack()


void iot_middleware_init(void *handle);
int iot_middleware_is_start(void);

void iot_middlewart_mesh_hal_init(uint8_t mac[6]);
void iot_middleware_report(baidu_mesh_data_t *operation);

#ifdef __cplusplus
}
#endif

#endif /* _GENIE_DLIST_H_ */
