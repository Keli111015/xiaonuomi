// Copyright (C) 2018-2020 Alibaba Group Holding Limited
// Adaptations to ESP-IDF Copyright (c) 2020 Espressif Systems (Shanghai) Co. Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef _GENIE_UTIL_H_
#define _GENIE_UTIL_H_

#ifdef __cplusplus
extern "C" {
#endif /**< __cplusplus */

typedef struct {
    esp_ble_mesh_elem_t *p_elem;
    int8_t retry;
    uint16_t retry_period;
    uint8_t opid;
    uint8_t tid;
    uint16_t len;
    uint8_t *data;
} genie_model_msg_t;

/**
 * @brief Get a semaphore's count.
 *
 * This routine returns the current count of @a sem.
 *
 * @param sem Address of the semaphore.
 *
 * @return Current semaphore count.
 */
const char *util_hex2str(const void *buf, size_t len);

#ifdef __cplusplus
}
#endif /**< __cplusplus */

#endif /* _GENIE_UTIL_H_ */
