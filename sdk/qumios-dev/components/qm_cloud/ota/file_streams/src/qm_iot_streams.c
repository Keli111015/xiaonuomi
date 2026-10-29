#include "qm_cloud_log.h"

#include "qm.h"
#include "qm_iot_streams.h"
#include "qm_iot_streams_cbor.h"
#include "qm_iot_streams_base64.h"

#include "json_parser.h"
#include "qm_utils_string.h"

#define LOG_TAG "streams"

/**
 * @brief Macro to check whether a character is an ASCII digit or not.
 */
#define IS_CHAR_DIGIT( x )    ( ( ( x ) >= '0' ) && ( ( x ) <= '9' ) )

/**
 * @brief Macro to convert an ASCII digit to integer.
 */
#define CHAR_TO_DIGIT( x )    ( ( x ) - '0' )

static size_t stringBuilder( char * buffer,
                             size_t bufferSizeBytes,
                             const char * const strings[] )
{
    size_t i;
    size_t curLen = 0;
    size_t thisLength = 0;

    buffer[ 0 ] = '\0';

    for( i = 0; strings[ i ] != NULL; i++ )
    {
        thisLength = strlen( strings[ i ] );

        if( ( thisLength + curLen + 1U ) > bufferSizeBytes ){
            curLen = 0;
            break;
        }

        strncat( buffer, strings[ i ], bufferSizeBytes - curLen - 1U );
        curLen += thisLength;
    }

    buffer[ curLen ] = '\0';

    return curLen;
}

static uint16_t createTopic( char * topicBuffer,
                             size_t topicBufferLen,
                             const char * streamName,
                             size_t streamNameLength,
                             const char * thingName,
                             size_t thingNameLength,
                             const char * apiSuffix )
{
    uint16_t topicLen = 0;
    char streamNameBuff[ QM_IOT_STREAMS_NAME_MAX_LEN + 1 ] = {0};
    char thingNameBuff[ QM_IOT_STREAMS_MAX_THINGNAME_LEN + 1 ] = {0};

    /* NULL-terminated list of topic string parts. */
    const char * topicParts[] =
    {
        QM_IOT_STREAMS_API_THINGS,
        NULL, /* Thing Name not available at compile
               * time, initialized below. */
        QM_IOT_STREAMS_API_STREAMS,
        NULL, /* Stream Name not available at compile
               * time, initialized below.*/
        NULL,
        NULL
    };

    memset( streamNameBuff, ( int32_t ) '\0', QM_IOT_STREAMS_NAME_MAX_LEN + 1U );
    memcpy( streamNameBuff, streamName, streamNameLength );

    memset( thingNameBuff, ( int32_t ) '\0', QM_IOT_STREAMS_MAX_THINGNAME_LEN + 1U );
    memcpy( thingNameBuff, thingName, thingNameLength );

    topicParts[ 1 ] = ( const char * ) thingNameBuff;
    topicParts[ 3 ] = ( const char * ) streamNameBuff;
    topicParts[ 4 ] = ( const char * ) apiSuffix;

    topicLen = ( uint16_t ) stringBuilder( topicBuffer,
                                           topicBufferLen,
                                           topicParts );

    return topicLen;
}


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
                                            qm_iot_steams_data_type_t dataType)
{
    const char * streamDataApiSuffix = NULL;
    const char * getStreamApiSuffix = NULL;
    qm_iot_streams_status_t initStatus = QM_IOT_STREAMS_STATUS_Success;

    if( ( streamName == NULL ) || ( streamNameLength == 0U ) ||
        ( thingName == NULL ) || ( thingNameLength == 0U ) || ( context == NULL ) )
    {
        return QM_IOT_STREAMS_STATUS_BadParameter;
    }

    /* Initializing MQTT File Downloader context */
    memset( context->topicStreamData, ( int32_t ) '\0', QM_IOT_STREAM_TOPIC_STREAM_DATA_BUFFER_SIZE );
    memset( context->topicGetStream, ( int32_t ) '\0', QM_IOT_STREAM_TOPIC_STREAM_DATA_BUFFER_SIZE );

    context->topicStreamDataLength = 0U;
    context->topicGetStreamLength = 0U;
    context->dataType = ( uint8_t ) dataType;

    if( context->dataType == ( uint8_t ) QM_IOT_STREAMS_DATA_TYPE_JSON ){
        streamDataApiSuffix = QM_IOT_STREAMS_API_DATA_JSON;
    }else{
        streamDataApiSuffix = QM_IOT_STREAMS_API_DATA_CBOR;
    }

    context->topicStreamDataLength = createTopic(
        context->topicStreamData,
        QM_IOT_STREAM_TOPIC_STREAM_DATA_BUFFER_SIZE,
        streamName,
        streamNameLength,
        thingName,
        thingNameLength,
        streamDataApiSuffix );

    if( context->topicStreamDataLength == 0U ){
        return QM_IOT_STREAMS_STATUS_InitFailed;
    }

    if( dataType == QM_IOT_STREAMS_DATA_TYPE_JSON ){
        getStreamApiSuffix = QM_IOT_STREAMS_API_GET_JSON;
    }else{
        getStreamApiSuffix = QM_IOT_STREAMS_API_GET_CBOR;
    }

    context->topicGetStreamLength = createTopic( context->topicGetStream,
                                                QM_IOT_STREAM_TOPIC_GET_STREAM_BUFFER_SIZE,
                                                streamName,
                                                streamNameLength,
                                                thingName,
                                                thingNameLength,
                                                getStreamApiSuffix );

    if( context->topicGetStreamLength == 0U ){
        initStatus = QM_IOT_STREAMS_STATUS_InitFailed;
    }

    return initStatus;
}
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
                                    int getStreamRequestLength)
{
    int requestLength = 0U;

    /*
     * Get stream request format
     *
     *   "{ \"s\" : 1, \"f\": 1, \"l\": 256, \"o\": 0, \"n\": 1 }";
     */
    if( ( getStreamRequestLength >= QM_IOT_STREAMS_REQUEST_BUFFER_SIZE ) &&
        ( getStreamRequest != NULL ) )
    {
        memset( getStreamRequest, ( int32_t )'\0', QM_IOT_STREAMS_REQUEST_BUFFER_SIZE );

        if( dataType == QM_IOT_STREAMS_DATA_TYPE_JSON ){
            /* MISRA Ref 21.6.1 [Use of snprintf] */
            /* coverity[misra_c_2012_rule_21_6_violation] */
            qm_snprintf( getStreamRequest,
                        QM_IOT_STREAMS_REQUEST_BUFFER_SIZE,
                        "{"
                        "\"s\": 1,"
                        "\"f\": %u,"
                        "\"l\": %u,"
                        "\"o\": %u,"
                        "\"n\": %u"
                        "}",
                        fileId,
                        blockSize,
                        blockOffset,
                        numberOfBlocksRequested);

            requestLength = strnlen( getStreamRequest,
                                     QM_IOT_STREAMS_REQUEST_BUFFER_SIZE );
        }else{
            /* MISRA Ref 7.4.1 [Use of string literal] */
            qm_iot_streams_cbor_encode_msg( ( uint8_t * ) getStreamRequest,
                                            QM_IOT_STREAMS_REQUEST_BUFFER_SIZE,
                                            &requestLength,
                                            "rdy",
                                            fileId,
                                            blockSize,
                                            blockOffset,
                                            /* coverity[misra_c_2012_rule_7_4_violation] */
                                            ( const uint8_t * ) "MQ==",
                                            strlen( "MQ==" ),
                                            numberOfBlocksRequested);
        }
    }

    return requestLength;
}
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
                                                            size_t topicLength )
{
    qm_iot_streams_status_t status = QM_IOT_STREAMS_STATUS_Failure;

    if( ( topic == NULL ) || ( topicLength == 0U ) ){
        status = QM_IOT_STREAMS_STATUS_BadParameter;
    }else if( ( topicLength == context->topicStreamDataLength ) &&
             ( 0 == strncmp( context->topicStreamData, topic, topicLength ) ) ){
        status = QM_IOT_STREAMS_STATUS_Success;
    }else{
        /* Empty MISRA body */
    }

    return status;
}
/* @[declare_mqttDownloader_isDataBlockReceived] */

static qm_iot_streams_status_t handleCborMessage( const uint8_t * message,
                                                     size_t messageLength,
                                                     int * fileId,
                                                     int * blockId,
                                                     int * blockSize,
                                                     uint8_t * decodedData,
                                                     int * decodedDataLength )
{
    bool cborDecodeRet = false;
    uint8_t * payload = decodedData;
    int payloadSize = CONFIG_QM_IOT_STREAMS_BLOCK_SIZE;
    qm_iot_streams_status_t handleStatus = QM_IOT_STREAMS_STATUS_Success;

    cborDecodeRet = qm_iot_streams_cbor_decode_msg( message,
                                                    messageLength,
                                                    fileId,
                                                    blockId,
                                                    blockSize,
                                                    &payload,
                                                    &payloadSize );

    if( cborDecodeRet ){
        *decodedDataLength = payloadSize;
    }else{
        handleStatus = QM_IOT_STREAMS_STATUS_DataDecodingFailed;
    }

    return handleStatus;
}

static qm_iot_streams_status_t handleJsonMessage( uint8_t * message,
                                                     int messageLength,
                                                     int * fileId,
                                                     int * blockId,
                                                     int * blockSize,
                                                     uint8_t * decodedData,
                                                     int * decodedDataLength )
{

    int type = 0;

    char *data = NULL;
    int data_len = 0U;

    int base64Status = QM_EOK;

    qm_iot_streams_status_t handleStatus = QM_IOT_STREAMS_STATUS_Success;

    data = qm_json_get_value_by_name((char *)message, messageLength, QM_IOT_STREAMS_FILEID_KEY, &data_len, &type);
    if(data == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "%s find failed!!", QM_IOT_STREAMS_FILEID_KEY);
        return QM_IOT_STREAMS_STATUS_Failure;
    }
    *fileId = (int)int_str_to_num(data, data_len);
    
    data = qm_json_get_value_by_name((char *)message, messageLength, QM_IOT_STREAMS_BLOCKID_KEY, &data_len, &type);
    if(data == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "%s find failed!!", QM_IOT_STREAMS_BLOCKID_KEY);
        return QM_IOT_STREAMS_STATUS_Failure;
    }
    *blockId = (int)int_str_to_num(data, data_len);
    
    data = qm_json_get_value_by_name((char *)message, messageLength, QM_IOT_STREAMS_BLOCKSIZE_KEY, &data_len, &type);
    if(data == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "%s find failed!!", QM_IOT_STREAMS_BLOCKSIZE_KEY);
        return QM_IOT_STREAMS_STATUS_Failure;
    }
    *blockSize = (int)int_str_to_num(data, data_len);

    data = qm_json_get_value_by_name((char *)message, messageLength, QM_IOT_STREAMS_BLOCKPAYLOAD_KEY, &data_len, &type);
    if(data == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "%s find failed!!", QM_IOT_STREAMS_BLOCKPAYLOAD_KEY);
        return QM_IOT_STREAMS_STATUS_Failure;
    }

    base64Status = qm_iot_streams_base64_decode((const uint8_t *)data, 
                                                data_len,
                                                (uint32_t)(*blockSize),
                                                decodedData,
                                                (uint32_t *)decodedDataLength);

    if( base64Status != QM_EOK){
        handleStatus = QM_IOT_STREAMS_STATUS_DataDecodingFailed;
    }

    return handleStatus;
}


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
                                                                int *dataLength)
{
    qm_iot_streams_status_t decodingStatus = QM_IOT_STREAMS_STATUS_Failure;

    if( ( message == NULL ) || ( messageLength == 0U ) || ( data == NULL ) || 
        ( dataLength == NULL ) || ( fileId == NULL ) || ( blockId == NULL ) || ( blockSize == NULL )){
        return decodingStatus;
    }
    
    memset( data, ( int32_t ) '\0', CONFIG_QM_IOT_STREAMS_BLOCK_SIZE);

    if( context->dataType == ( uint8_t ) QM_IOT_STREAMS_DATA_TYPE_JSON ){
        decodingStatus = handleJsonMessage( message,
                                            messageLength,
                                            fileId,
                                            blockId,
                                            blockSize,
                                            data,
                                            dataLength );
    }else{
        decodingStatus = handleCborMessage( message,
                                            messageLength,
                                            fileId,
                                            blockId,
                                            blockSize,
                                            data,
                                            dataLength );
    }

    return decodingStatus;
}
/* @[declare_mqttDownloader_processReceivedDataBlock] */
