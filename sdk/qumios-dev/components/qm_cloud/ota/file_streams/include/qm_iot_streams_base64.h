/*
 * MQTT File Streams Embedded C v1.1.0
 * Copyright (C) 2023 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: MIT
 *
 * Licensed under the MIT License. See the LICENSE accompanying this file
 * for the specific language governing permissions and limitations under
 * the License.
 */

/**
 * @file MQTTFileDownloader_base64.h
 * @brief Function declarations and error codes for MQTTFileDownloader_base64.c.
 */

#ifndef QM_IOT_STREAMS_BASE64_H
#define QM_IOT_STREAMS_BASE64_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"

/**
 * @brief Decode Base64 encoded data.
 *
 * @param[in]  data Pointer to a buffer containing the Base64 encoded
 *             data that is intended to be decoded.
 * @param[in]  inputLength Length of the encodedData buffer.
 * @param[in]  outputLenMax Length of the dest buffer.
 * @param[out] decodedData Pointer to a buffer for storing the decoded result.
 * @param[out] outputLength Pointer to the length of the decoded result.
 * 
 * @return     One of the following:
 *             - #Base64Success if the Base64 encoded data was valid
 *               and successfully decoded.
 *             - An error code defined in ota_base64_private.h if the
 *               encoded data or input parameters are invalid.
 */
int qm_iot_streams_base64_decode(const uint8_t *data, uint32_t inputLength, 
                                uint32_t outputLenMax,
                                uint8_t *decodedData, uint32_t *outputLength );

#ifdef __cplusplus
}
#endif

#endif /* ifndef MQTT_FILE_DOWNLOADER_BASE64_H */
