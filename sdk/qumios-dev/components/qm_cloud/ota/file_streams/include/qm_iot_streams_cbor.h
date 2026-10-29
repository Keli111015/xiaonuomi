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
 * @file MQTTFileDownloader_cbor.h
 * @brief Function declarations and field declarations for
 * MQTTFileDownloader_cbor.c.
 */

#ifndef QM_IOT_STREAMS_CBOR_H
#define QM_IOT_STREAMS_CBOR_H

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

#include "qm.h"

/**
 * @brief Decode a Get Stream response message from AWS IoT OTA.
 */
bool qm_iot_streams_cbor_decode_msg( const uint8_t * messageBuffer,
                                    int messageSize,
                                    int * fileId,
                                    int * blockId,
                                    int * blockSize,
                                    uint8_t * const * payload,
                                    int * payloadSize );

/**
 * @brief Create an encoded Get Stream Request message for the AWS IoT OTA
 * service. The service allows block count or block bitmap to be requested,
 * but not both.
 */
bool qm_iot_streams_cbor_encode_msg( uint8_t * messageBuffer,
                                    int messageBufferSize,
                                    int * encodedMessageSize,
                                    const char * clientToken,
                                    uint32_t fileId,
                                    uint32_t blockSize,
                                    uint32_t blockOffset,
                                    const uint8_t * blockBitmap,
                                    int blockBitmapSize,
                                    uint32_t numOfBlocksRequested );

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* ifndef MQTT_FILE_DOWNLOADER_CBOR_H */
