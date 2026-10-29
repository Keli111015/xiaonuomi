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
 * @file MQTTFileDownloader.h
 * @brief MQTT file streaming APIs declaration.
 */

#ifndef QM_IOT_STREAMS_H
#define QM_IOT_STREAMS_H

#include "qm.h"

/**
 *  @brief Topic strings used by the MQTT downloader.
 *
 * These first few are topic extensions to the dynamic base topic that includes
 * the Thing name.
 */
#define QM_IOT_STREAMS_API_THINGS                   "$aws/things/"       /*!< Topic prefix for thing APIs. */
#define QM_IOT_STREAMS_API_STREAMS                  "/streams/"          /*!< Stream API identifier. */
#define QM_IOT_STREAMS_API_DATA_CBOR                "/data/cbor"         /*!< Stream API suffix. */
#define QM_IOT_STREAMS_API_GET_CBOR                 "/get/cbor"          /*!< Stream API suffix. */
#define QM_IOT_STREAMS_API_DATA_JSON                "/data/json"         /*!< JSON DATA Stream API suffix. */
#define QM_IOT_STREAMS_API_GET_JSON                 "/get/json"          /*!< JSON GET Stream API suffix. */

/**
 * Message field definitions, per the server specification. These are
 * not part of the library interface but are included here for testability.
 */
#define QM_IOT_STREAMS_CLIENTTOKEN_KEY          "c" /*!< Key for client id. */
#define QM_IOT_STREAMS_FILEID_KEY               "f" /*!< Key for file id. */
#define QM_IOT_STREAMS_BLOCKSIZE_KEY            "l" /*!< Key for file block size. */
#define QM_IOT_STREAMS_BLOCKOFFSET_KEY          "o" /*!< Key for file block offset. */
#define QM_IOT_STREAMS_BLOCKBITMAP_KEY          "b" /*!< Key for bitmap. */
#define QM_IOT_STREAMS_STREAMDESCRIPTION_KEY    "d" /*!< Key for stream name. */
#define QM_IOT_STREAMS_STREAMFILES_KEY          "r" /*!< Key for file attributes. */
#define QM_IOT_STREAMS_FILESIZE_KEY             "z" /*!< Key for file size. */
#define QM_IOT_STREAMS_BLOCKID_KEY              "i" /*!< Key for block id. */
#define QM_IOT_STREAMS_BLOCKPAYLOAD_KEY         "p" /*!< Key for payload of a block. */
#define QM_IOT_STREAMS_NUMBEROFBLOCKS_KEY       "n" /*!< Key for number of blocks. */


#ifndef CONFIG_QM_IOT_STREAMS_BLOCK_SIZE
#define CONFIG_QM_IOT_STREAMS_BLOCK_SIZE    2048
#endif


/**
 * @ingroup mqtt_file_downloader_const_types
 * Maximum stream name length.
 */
#define QM_IOT_STREAMS_NAME_MAX_LEN               44U

/**
 * @ingroup mqtt_file_downloader_const_types
 * Length of NULL character. Used in calculating lengths of MQTT topics.
 */
#define QM_IOT_STREAMS_NULL_CHAR_LEN                     1U

/**
 * @ingroup mqtt_file_downloader_const_types
 * Maximum thing name length.
 */
#define QM_IOT_STREAMS_MAX_THINGNAME_LEN                 128U

/**
 * @ingroup mqtt_file_downloader_const_types
 * Stream Request Buffer Size
 */
#define QM_IOT_STREAMS_REQUEST_BUFFER_SIZE               128U

/**
 * @ingroup mqtt_file_downloader_const_types
 * Stream Request Buffer Size
 */
#define QM_IOT_STREAMS_PROGRESS_BUFFER_SIZE              256U

/**
 * @brief Macro to calculate string length.
 */
#define QM_IOT_STREAMS_CONST_STRLEN( s )    ( ( ( uint32_t ) sizeof( s ) ) - 1UL )

/**
 * @brief Length of common parts MQTT topic.
 */
#define QM_IOT_STREAMS_TOPIC_COMMON_PARTS_LEN                              \
    ( QM_IOT_STREAMS_CONST_STRLEN( QM_IOT_STREAMS_API_THINGS ) + QM_IOT_STREAMS_MAX_THINGNAME_LEN + \
      QM_IOT_STREAMS_CONST_STRLEN( QM_IOT_STREAMS_API_STREAMS ) + QM_IOT_STREAMS_NAME_MAX_LEN + QM_IOT_STREAMS_NULL_CHAR_LEN )

/**
 * @brief Length stream data buffer.
 */
#define QM_IOT_STREAM_TOPIC_STREAM_DATA_BUFFER_SIZE \
    ( QM_IOT_STREAMS_TOPIC_COMMON_PARTS_LEN + QM_IOT_STREAMS_CONST_STRLEN( QM_IOT_STREAMS_API_DATA_CBOR ) )

/**
 * @brief Length of get stream buffer.
 */
#define QM_IOT_STREAM_TOPIC_GET_STREAM_BUFFER_SIZE \
    ( QM_IOT_STREAMS_TOPIC_COMMON_PARTS_LEN + QM_IOT_STREAMS_CONST_STRLEN( QM_IOT_STREAMS_API_GET_CBOR ) )

/**
 * @ingroup mqtt_file_downloader_enum_types
 * @brief  MQTT File Downloader return codes.
 */
typedef enum
{
    QM_IOT_STREAMS_STATUS_Failure = -1,           /**< Failure. */
    QM_IOT_STREAMS_STATUS_Success,           /**< Success. */
    QM_IOT_STREAMS_STATUS_BadParameter,      /**< Bad Parameter. */
    QM_IOT_STREAMS_STATUS_NotInitialized,    /**< Downloader not initalized. */
    QM_IOT_STREAMS_STATUS_InitFailed,        /**< Downloader init failed. */
    QM_IOT_STREAMS_STATUS_SubscribeFailed,   /**< MQTT subscribe failed. */
    QM_IOT_STREAMS_STATUS_PublishFailed,     /**< MQTT publish failed. */
    QM_IOT_STREAMS_STATUS_DataDecodingFailed /**< MQTT data decoding failed. */
} qm_iot_streams_status_t;

/**
 * @ingroup mqtt_file_downloader_enum_types
 * @brief contains all the data types supported.
 */
typedef enum
{
    QM_IOT_STREAMS_DATA_TYPE_JSON, /**< JSON data type. */
    QM_IOT_STREAMS_DATA_TYPE_CBOR  /**< CBOR data type. */
} qm_iot_steams_data_type_t;

/**
 * @ingroup mqtt_file_downloader_struct_types
 * @brief Strucure to mqtt file downloader context.
 */
typedef struct
{
    char topicStreamData[ QM_IOT_STREAM_TOPIC_STREAM_DATA_BUFFER_SIZE ]; /**< Stream data MQTT topic. */
    size_t topicStreamDataLength;                          /**< Stream data MQTT topic length. */
    char topicGetStream[ QM_IOT_STREAM_TOPIC_STREAM_DATA_BUFFER_SIZE ];   /**< Get Stream MQTT topic. */
    size_t topicGetStreamLength;                           /**< Get Stream MQTT topic length. */
    uint8_t dataType;                                      /**< Encoding type to be used to download the file. */
} qm_iot_steams_context_t;

/**
 * @brief Initializes the MQTT file downloader. Creates the topic name the DATA and Get Stream Data topics
 *
 * @param[in] context MQTT file downloader context pointer.
 * @param[in] streamName Stream name to download the file.
 * @param[in] streamNameLength Length of the Stream name to download the file.
 * @param[in] thingName Thing name of the Device.
 * @param[in] thingNameLength Length of the Thing name of the Device.
 * @param[in] dataType Either JSON or CBOR data type.
 *
 * @return MQTTFileDownloaderStatus_t returns appropriate MQTT File Downloader Status.
 */
/* @[declare_mqttDownloader_init] */
qm_iot_streams_status_t qm_iot_steams_init( qm_iot_steams_context_t * context,
                                            const char * streamName,
                                            size_t streamNameLength,
                                            const char * thingName,
                                            size_t thingNameLength,
                                            qm_iot_steams_data_type_t dataType);
/* @[declare_mqttDownloader_init] */

/**
 * @brief Creates the get request for Data blocks from MQTT Streams.
 *
 * @param[in] dataType Either JSON or CBOR data type.
 * @param[in] fileId File Id of the file to be downloaded from MQTT Streams.
 * @param[in] blockSize Requested size of block.
 * @param[in] blockOffset Block Offset.
 * @param[in] numberOfBlocksRequested Number of Blocks requested per request.
 * @param[out] getStreamRequest Buffer to store the get stream request.
 * @param[in] getStreamRequestLength Length of getStreamRequest buffer.
 *
 * @return size_t returns Length of the get stream request.
 */
/* @[declare_mqttDownloader_createGetDataBlockRequest] */
int qm_iot_steams_create_request( qm_iot_steams_data_type_t dataType,
                                    uint16_t fileId,
                                    uint32_t blockSize,
                                    uint16_t blockOffset,
                                    uint32_t numberOfBlocksRequested,
                                    char * getStreamRequest,
                                    int getStreamRequestLength );
/* @[declare_mqttDownloader_createGetDataBlockRequest] */

/**
 * @brief Checks if the incoming Publish message contains MQTT Data block.
 *
 * @param[in] context MQTT file downloader context pointer.
 * @param[in] topic incoming Publish message MQTT topic.
 * @param[in] topicLength incoming Publish message MQTT topic length.
 *
 * @return returns True if the message contains Data block else False.
 */
/* @[declare_mqttDownloader_isDataBlockReceived] */
qm_iot_streams_status_t qm_iot_steams_isdatablock_received( const qm_iot_steams_context_t * context,
                                                            const char * topic,
                                                            size_t topicLength );
/* @[declare_mqttDownloader_isDataBlockReceived] */

/**
 * @brief Retrieve the data block from incoming MQTT message and decode it.
 *
 * @param[in] context MQTT file downloader context pointer.
 * @param[in] message Incoming MQTT message containing data block.
 * @param[in] messageLength Incoming MQTT message length.
 * @param[out] fileId ID of the file to which the data block belongs.
 * @param[out] blockId ID of the received block.
 * @param[out] blockSize Size of the receive block in bytes.
 * @param[out] data Decoded data block.
 * @param[in] dataLength Decoded data block length.
 *
 * @return returns True if the message is handled else False.
 */
/* @[declare_mqttDownloader_processReceivedDataBlock] */
qm_iot_streams_status_t qm_iot_streams_recviced_datablock(const qm_iot_steams_context_t *context,
                                                                uint8_t * message,
                                                                int messageLength,
                                                                int * fileId,
                                                                int * blockId,
                                                                int * blockSize,
                                                                uint8_t * data,
                                                                int *dataLength);
/* @[declare_mqttDownloader_processReceivedDataBlock] */
#endif /* #ifndef MQTT_FILE_DOWNLOADER_H */
