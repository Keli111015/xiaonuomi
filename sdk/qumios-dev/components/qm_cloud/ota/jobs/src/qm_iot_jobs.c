#include "qm_cloud_log.h"

#include "qm.h"
#include "qm_iot_jobs.h"

#include "json_parser.h"
#include "qm_utils_string.h"

#define LOG_TAG "JOBS"

/** @cond DO_NOT_DOCUMENT */

/**
 * @brief Get the length of a string literal.
 */
#define CONST_STRLEN( x )    ( sizeof( ( x ) ) - 1U )

/**
 * @brief Get the length on an array.
 */
#define ARRAY_LENGTH( x )    ( sizeof( ( x ) ) / sizeof( ( x )[ 0 ] ) )

/**
 * @brief Table of topic API strings in qm_iot_jobs_topic_t order.
 */
static const char * const apiTopic[] =
{
    JOBS_API_NEXTJOBCHANGED,
};

/**
 * @brief Table of topic API string lengths in qm_iot_jobs_topic_t order.
 */
static const int apiTopicLength[] =
{
    JOBS_API_NEXTJOBCHANGED_LENGTH,
};

/**
 * @brief Predicate returns true for a valid thing name or job ID character.
 *
 * @param[in] a  character to check
 * @param[in] allowColon  set to true for thing names
 *
 * @return true if the character is valid;
 * false otherwise
 */
static bool isValidChar( char a,
                         bool allowColon )
{
    bool ret;

    if( ( a == '-' ) || ( a == '_' ) ){
        ret = true;
    }else if( ( a >= '0' ) && ( a <= '9' ) ){
        ret = true;
    }else if( ( a >= 'A' ) && ( a <= 'Z' ) ){
        ret = true;
    }else if( ( a >= 'a' ) && ( a <= 'z' ) ){
        ret = true;
    }else if( a == ':' ){
        ret = allowColon;
    }else{
        ret = false;
    }

    return ret;
}

/**
 * @brief Predicate returns true for a valid identifier.
 *
 * The identifier may be a thing name or a job ID.
 *
 * @param[in] id  character sequence to check
 * @param[in] length  length of the character sequence
 * @param[in] max  maximum length of a valid identifier
 * @param[in] allowColon  set to true for thing names
 *
 * @return true if the identifier is valid;
 * false otherwise
 */
static bool isValidID( const char * id,
                       uint16_t length,
                       uint16_t max,
                       bool allowColon )
{
    bool ret = false;

    if( ( id != NULL ) && ( length > 0U ) &&
        ( length <= max ) )
    {
        int i;

        for( i = 0; i < length; i++ )
        {
            if( isValidChar( id[ i ], allowColon ) == false )
            {
                break;
            }
        }

        ret = ( i == length ) ? true : false;
    }

    return ret;
}


/**
 * @brief Predicate returns true for a valid thing name string.
 *
 * @param[in] thingName  character sequence to check
 * @param[in] thingNameLength  length of the character sequence
 *
 * @return true if the thing name is valid;
 * false otherwise
 */
static bool isValidThingName( const char * thingName,
                              uint16_t thingNameLength )
{
    return isValidID( thingName, thingNameLength,
                      THINGNAME_MAX_LENGTH, true );
}

/**
 * @brief Predicate returns true for a valid job ID string.
 *
 * @param[in] jobId  character sequence to check
 * @param[in] jobIdLength  length of the character sequence
 *
 * @return true if the job ID is valid;
 * false otherwise
 */
static bool isValidJobId( const char * jobId,
                          uint16_t jobIdLength )
{
    return isValidID( jobId, jobIdLength,
                      JOBID_MAX_LENGTH, false );
}

/**
 * @brief A strncpy replacement based on lengths only.
 *
 * @param[in] buffer  The buffer to be written.
 * @param[in,out] start  The index at which to begin.
 * @param[in] max  The size of the buffer.
 * @param[in] value  The characters to copy.
 * @param[in] valueLength  How many characters to copy.
 *
 * @return #QM_IOT_JOBSSUCCESS if the value was written to the buffer;
 * #JobsBufferTooSmall if the buffer cannot hold the entire value.
 *
 * @note There is no harm from calling this function when
 * start >= max.  This allows for sequential calls to
 * strnAppend() where only the final call's return value
 * is needed.
 */
static qm_iot_jobs_code_t strnAppend( char * buffer,
                                    int * start,
                                    int max,
                                    const char * value,
                                    int valueLength )
{
    int i, j = 0;

    if( ( buffer == NULL ) || ( start == NULL ) || ( value == NULL ) ){
        return QM_IOT_JOBSERROR;
    }

    i = *start;

    while( ( i < max ) && ( j < valueLength ) )
    {
        buffer[ i ] = value[ j ];
        i++;
        j++;
    }

    *start = i;

    return ( i < max ) ? QM_IOT_JOBSSUCCESS : QM_IOT_JOBSBUFFTOSMALL;
}

/**
 * @brief Populate the common leading portion of a topic string.
 *
 * @param[in] buffer  The buffer to contain the topic string.
 * @param[in,out] start  The index at which to begin.
 * @param[in] length  The size of the buffer.
 * @param[in] thingName  The device's thingName as registered with AWS IoT.
 * @param[in] thingNameLength  The length of the thingName.
 */
static void writePreamble( char * buffer,
                           int * start,
                           int length,
                           const char * thingName,
                           uint16_t thingNameLength )
{
    ( void ) strnAppend( buffer, start, length,
                         JOBS_API_PREFIX, JOBS_API_PREFIX_LENGTH );
    ( void ) strnAppend( buffer, start, length,
                         thingName, thingNameLength );
    ( void ) strnAppend( buffer, start, length,
                         JOBS_API_BRIDGE, JOBS_API_BRIDGE_LENGTH );
}

#define checkThingParams(thingName, thingNameLength) \
    ( isValidThingName( thingName, thingNameLength ) == true )

#define checkCommonParams(buffer, length, thingName, thingNameLength) \
    ( ( buffer != NULL ) && ( length > 0UL ) && checkThingParams(thingName, thingNameLength) )

/**
 * @brief Compare the leading n bytes of two character sequences.
 *
 * @param[in] a  first character sequence
 * @param[in] b  second character sequence
 * @param[in] n  number of bytes
 *
 * @return QM_IOT_JOBSSUCCESS if the sequences are the same;
 * QM_IOT_JOBSNOMATCH otherwise
 */
static qm_iot_jobs_code_t strnEquals( const char * a,
                                const char * b,
                                int n )
{
    int i;
    if(a == NULL || b == NULL){
        return QM_IOT_JOBSERROR;
    }

    for( i = 0U; i < n; i++ )
    {
        if( a[ i ] != b[ i ] )
        {
            break;
        }
    }

    return ( i == n ) ? QM_IOT_JOBSSUCCESS : QM_IOT_JOBSNOMATCH;
}

/**
 * @brief Wrap strnEquals() with a check to compare two lengths.
 *
 * @param[in] a  first character sequence
 * @param[in] aLength  Length of a
 * @param[in] b  second character sequence
 * @param[in] bLength  Length of b
 *
 * @return QM_IOT_JOBSSUCCESS if the sequences are the same;
 * QM_IOT_JOBSNOMATCH otherwise
 */
static qm_iot_jobs_code_t strnnEq( const char * a,
                             int aLength,
                             const char * b,
                             int bLength )
{
    qm_iot_jobs_code_t ret = QM_IOT_JOBSNOMATCH;

    if( aLength == bLength )
    {
        ret = strnEquals( a, b, aLength );
    }

    return ret;
}

/**
 * @brief Predicate returns true for a match to JOBS_API_JOBID_NEXT
 *
 * @param[in] jobId  character sequence to check
 * @param[in] jobIdLength  length of the character sequence
 *
 * @return true if the job ID matches;
 * false otherwise
 */
static bool isNextJobId( const char * jobId,
                         uint16_t jobIdLength )
{
    bool ret = false;

    if( ( jobId != NULL ) &&
        ( strnnEq( JOBS_API_JOBID_NEXT, JOBS_API_JOBID_NEXT_LENGTH, jobId, jobIdLength ) == QM_IOT_JOBSSUCCESS ) )
    {
        ret = true;
    }

    return ret;
}

/** @endcond */

static void populateMqttStreamingFiles( const char * jobDoc,
                                        const size_t jobDocLength,
                                        int32_t fileIndex,  
                                        qm_iot_jobs_file_t * job_files )
{
    char *pos = 0;
    int index = 0, type = 0;
    
    char *root = NULL;
    int root_len = 0;

    char *data = NULL;
    int data_len = 0U;

    char *files = NULL;
    int files_len = 0U;

    char *files_array = NULL;
    int files_array_len = 0U;

    uint32_t int_value = 0;

    root = qm_json_get_value_by_name((char *)jobDoc, jobDocLength, "afr_ota", &root_len, &type);
    if(root == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "afr_ota find failed!!");
        return ;
    }

    data = qm_json_get_value_by_name((char *)root, root_len, "streamname", &data_len, &type);
    if(data == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "streamname find failed!!");
        return ;
    }

    job_files->imageRef = data;
    job_files->imageRefLen = ( size_t ) data_len;

    files_array = qm_json_get_value_by_name((char *)root, root_len, "files", &files_array_len, &type);
    if(files_array == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "files find failed!!");
        return ;
    }

    json_array_for_each_entry(files_array, pos, files, files_len, type) 
    {
        if (!files || !files_len) {
            break;
        }

        if(index++ != fileIndex){
            continue;
        }

        data = qm_json_get_value_by_name((char *)files, files_len, "filepath", &data_len, &type);
        if(data == NULL){
            QM_CLOUD_LOGE(LOG_TAG, "filepath find failed!!");
            break;
        }

        job_files->filepath = data;
        job_files->filepathLen = ( size_t ) data_len;

        data = qm_json_get_value_by_name((char *)files, files_len, "filesize", &data_len, &type);
        if(data == NULL){
            QM_CLOUD_LOGE(LOG_TAG, "filesize find failed!!");
            break;
        }
        int_value = int_str_to_num(data, data_len);
        if(int_value == 0){
            QM_CLOUD_LOGE(LOG_TAG, "fileSize 0 failed!!");
            break;
        }
        job_files->fileSize = int_value;

        data = qm_json_get_value_by_name((char *)files, files_len, "fileid", &data_len, &type);
        if(data == NULL){
            QM_CLOUD_LOGE(LOG_TAG, "fileid find failed!!");
            break;
        }
        int_value = int_str_to_num(data, data_len);
        job_files->fileId = int_value;

        data = qm_json_get_value_by_name((char *)files, files_len, "certfile", &data_len, &type);
        if(data == NULL){
            QM_CLOUD_LOGE(LOG_TAG, "certfile find failed!!");
            break;
        }
        job_files->certfile = data;
        job_files->certfileLen = ( uint32_t ) data_len;

        data = qm_json_get_value_by_name((char *)files, files_len, "sig-sha1-rsa", &data_len, &type);
        if(data != NULL){
            job_files->signature = data;
            job_files->signatureLen = ( uint32_t ) data_len;
            job_files->hash_crypto = QM_OTA_CRYPTO_HASH_ALGORITHM_SHA1;
            job_files->asymmetric_crypto = QM_OTA_CRYPTO_ASYMMETRIC_ALGORITHM_RSA;
        }

        data = qm_json_get_value_by_name((char *)files, files_len, "sig-sha1-ecdsa", &data_len, &type);
        if(data != NULL){
            job_files->signature = data;
            job_files->signatureLen = ( uint32_t ) data_len;
            job_files->hash_crypto = QM_OTA_CRYPTO_HASH_ALGORITHM_SHA1;
            job_files->asymmetric_crypto = QM_OTA_CRYPTO_ASYMMETRIC_ALGORITHM_ECDSA;
        }

        data = qm_json_get_value_by_name((char *)files, files_len, "sig-sha256-ecdsa", &data_len, &type);
        if(data != NULL){
            job_files->signature = data;
            job_files->signatureLen = ( uint32_t ) data_len;
            job_files->hash_crypto = QM_OTA_CRYPTO_HASH_ALGORITHM_SHA256;
            job_files->asymmetric_crypto = QM_OTA_CRYPTO_ASYMMETRIC_ALGORITHM_ECDSA;
        }

        data = qm_json_get_value_by_name((char *)files, files_len, "sig-sha1-ecdsa", &data_len, &type);
        if(data != NULL){
            job_files->signature = data;
            job_files->signatureLen = ( uint32_t ) data_len;
            job_files->hash_crypto = QM_OTA_CRYPTO_HASH_ALGORITHM_SHA1;
            job_files->asymmetric_crypto = QM_OTA_CRYPTO_ASYMMETRIC_ALGORITHM_ECDSA;
        }

        data = qm_json_get_value_by_name((char *)files, files_len, "fileType", &data_len, &type);
        if(data == NULL){
            QM_CLOUD_LOGE(LOG_TAG, "fileType find failed!!");
            break;
        }
        int_value = int_str_to_num(data, data_len);
        job_files->fileType = int_value;

        data = qm_json_get_value_by_name((char *)files, files_len, "version", &data_len, &type);
        if(data != NULL){
            job_files->version = data;
            job_files->versionLen = ( uint32_t ) data_len;
        }

        data = qm_json_get_value_by_name((char *)files, files_len, "extra", &data_len, &type);
        if(data != NULL){
            job_files->extra = data;
            job_files->extraLen = ( uint32_t ) data_len;
        }
        
        break;
    }

}


/**
 * @brief Populate the fields of 'result', returning
 * true if successful.
 *
 * @param msg  job document
 * @param jobDocLength job document length
 * @param fileIndex The index of the file to use properties of
 * @param result Job document structure to populate
 * @return true Job document fields were parsed from the document
 * @return false Job document fields were not parsed from the document
 */
/* @[declare_populatejobdocfields] */
bool_t qm_iot_job_pop_files(const char * msg,  const int msg_length, 
                            int32_t fileIndex,  qm_iot_jobs_file_t * job_files)
{
    int type = 0;

    char *root = NULL;
    int root_len = 0;

    char *protocols = NULL;
    int protocols_len = 0;

    root = qm_json_get_value_by_name((char *)msg, msg_length, "afr_ota", &root_len, &type);
    if(root == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "afr_ota find failed!!");
        return QM_FALSE;
    }

    protocols = qm_json_get_value_by_name(root, root_len, "protocols", &protocols_len, &type);
    if(protocols == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "protocols find failed!!");
        return QM_FALSE;
    }

    QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_OTA, "protocols found len=%d", protocols_len);

    if(strncmp( "[\"MQTT\"]", protocols, protocols_len ) == 0 ){
        populateMqttStreamingFiles( msg, msg_length, fileIndex, job_files );
    }else{
        //暂不支持 HTTP功能
        return QM_FALSE;
    }

    /* Should this nullify the fields which have been populated before
     * returning? */
    return QM_TRUE;
}

qm_iot_jobs_code_t qm_iot_jobs_get_topic(qm_iot_jobs_topic_t topic,  
                                            const char * did, int did_len,
                                            const char * jobid, int jobid_len,
                                            char * buffer, int in_buffer_len, int * out_buffer_len)
{
    qm_iot_jobs_code_t ret = QM_IOT_JOBSBADPARAM;
    int start = 0U;

    if( checkCommonParams(buffer, in_buffer_len, did, did_len) &&
        ( topic > QM_IOT_JOBS_TOPIC_INVALID ) && ( topic < QM_IOT_JOBS_TOPIC_MAX ) )
    {
        writePreamble( buffer, &start, in_buffer_len, did, did_len);
    #if 0
        if( topic >= QM_IOT_JOBS_TOPIC_DESCRIBE_SUCCESS )
        {
            ( void ) strnAppend( buffer, &start, in_buffer_len,
                                 jobid, jobid_len);
            ( void ) strnAppend( buffer, &start, in_buffer_len,
                                 "/", strlen("/"));
        }
    #endif
        ret = strnAppend( buffer, &start, in_buffer_len,
                          apiTopic[ topic ], apiTopicLength[ topic ] );

        if( start == did_len )
        {
            start--;
        }

        buffer[ start ] = '\0';

        if( out_buffer_len != NULL )
        {
            *out_buffer_len = start;
        }
    }

    return ret;
}

qm_iot_jobs_code_t qm_iot_jobs_describe(const char * did, uint16_t did_len,
                                        const char * jobid, int jobid_len,
                                        char * buffer, int in_buffer_len, int *out_buffer_len)
{
    qm_iot_jobs_code_t ret = QM_IOT_JOBSBADPARAM;
    int start = 0U;

    if(  checkCommonParams(buffer, in_buffer_len, did, did_len)  &&
        ( ( isNextJobId( jobid, jobid_len ) == true ) ||
          ( isValidJobId( jobid, jobid_len ) == true ) ) )
    {
        writePreamble( buffer, &start, in_buffer_len, did, did_len );

        ( void ) strnAppend( buffer, &start, in_buffer_len,
                             jobid, jobid_len );
        ( void ) strnAppend( buffer, &start, in_buffer_len,
                             "/", ( CONST_STRLEN( "/" ) ) );
        ret = strnAppend( buffer, &start, in_buffer_len,
                          JOBS_API_DESCRIBE, JOBS_API_DESCRIBE_LENGTH );

        start = ( start >= in_buffer_len ) ? ( in_buffer_len - 1U ) : start;
        buffer[ start ] = '\0';

        if( out_buffer_len != NULL )
        {
            *out_buffer_len = start;
        }
    }

    return ret;
}

qm_iot_jobs_code_t qm_iot_jobs_update(const char * did, uint16_t did_len,
                                        const char * jobid, int jobid_len,
                                        char * buffer, int in_buffer_len, int *out_buffer_len)
{
    qm_iot_jobs_code_t ret = QM_IOT_JOBSBADPARAM;
    int start = 0U;

    if( checkCommonParams(buffer, in_buffer_len, did, did_len) &&
        ( isValidJobId( jobid, jobid_len ) == true ) )
    {
        writePreamble( buffer, &start, in_buffer_len, did, did_len );

        ( void ) strnAppend( buffer, &start, in_buffer_len,
                             jobid, jobid_len );
        ( void ) strnAppend( buffer, &start, in_buffer_len,
                             "/", ( CONST_STRLEN( "/" ) ) );
        ret = strnAppend( buffer, &start, in_buffer_len,
                          JOBS_API_UPDATE, JOBS_API_UPDATE_LENGTH );

        start = ( start >= in_buffer_len ) ? ( in_buffer_len - 1U ) : start;
        buffer[ start ] = '\0';

        if( out_buffer_len != NULL )
        {
            *out_buffer_len = start;
        }
    }

    return ret;
}
/* @[declare_jobs_update] */

/**
 * @brief Populate a message string for an UpdateJobExecution request.
 *
 * @param status Current status of the job
 * @param progress_num The progressing 
 * @param expect_ver The version that is expected
 * @param expect_ver_len The length of the expectedVersion string
 * @param buffer The buffer to be written to
 * @param buffer_size the size of the buffer
 *
 * @return 0 if write to buffer fails
 * @return messageLength if the write is successful
 *
 */
/* @[declare_jobs_updatemsg] */
int qm_iot_jobs_update_msg(qm_iot_jobs_status_t status,
                            const char *details_progress, int edetails_progress_len,
                            const char *expect_ver, int expect_ver_len,
                            const char *clienttoken, int clienttoken_len,
                            char *buffer,int buffer_size)
{
    int start = 0U;

    static const char * const jobStatusString[] =
    {
        "QUEUED",
        "IN_PROGRESS",
        "FAILED",
        "SUCCEEDED",
        "REJECTED"
    };

    static const int jobStatusStringLengths[] =
    {
        CONST_STRLEN( "QUEUED" ),
        CONST_STRLEN( "IN_PROGRESS" ),
        CONST_STRLEN( "FAILED" ),
        CONST_STRLEN( "SUCCEEDED" ),
        CONST_STRLEN( "REJECTED" )
    };

    if(status >= ARRAY_LENGTH( jobStatusString ) ){
        return 0;
    }

    if(( buffer_size >= ( 34U + expect_ver_len + jobStatusStringLengths[ status ] ) ) &&
        ( jobStatusString[ status ] != NULL ) )
    {
        ( void ) strnAppend( buffer, &start, buffer_size, JOBS_API_STATUS, JOBS_API_STATUS_LENGTH );
        ( void ) strnAppend( buffer, &start, buffer_size, jobStatusString[ status ], jobStatusStringLengths[ status ] );
        ( void ) strnAppend( buffer, &start, buffer_size, JOBS_API_DETAILS_PROGRESS, JOBS_API_DETAILS_PROGRESS_LENGTH );
        ( void ) strnAppend( buffer, &start, buffer_size, details_progress, edetails_progress_len );
        ( void ) strnAppend( buffer, &start, buffer_size, "\"}", ( CONST_STRLEN( "\"}" ) ) );
        if(( expect_ver != NULL ) && ( expect_ver_len > 0U )){
            ( void ) strnAppend( buffer, &start, buffer_size, JOBS_API_EXPECTED_VERSION, JOBS_API_EXPECTED_VERSION_LENGTH );
            ( void ) strnAppend( buffer, &start, buffer_size, expect_ver, expect_ver_len );
        }
        ( void ) strnAppend( buffer, &start, buffer_size, ",\"clientToken\":\"", strlen(",\"clientToken\":\""));
        ( void ) strnAppend( buffer, &start, buffer_size, clienttoken, clienttoken_len );

        ( void ) strnAppend( buffer, &start, buffer_size, "\"}", ( CONST_STRLEN( "\"}" ) ) );
    }

    return start;
}
/* @[declare_jobs_updatemsg] */


int qm_iot_jobs_get_jobid(const char *msg, int msg_len, const char **job_id, int *out_len)
{
    int type = 0;

    char *root = NULL;
    int root_len = 0;

    char *jobId = NULL;
    int jobIdLength = 0;
    
    root = qm_json_get_value_by_name((char *)msg, msg_len, "execution", &root_len, &type);
    if(root == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "execution find failed!!");
        return -QM_EINVAL;
    }

    jobId = qm_json_get_value_by_name((char *)msg, msg_len, "jobId", &jobIdLength, &type);
    if(jobId == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "jobId find failed!!");
        return -QM_EINVAL;
    }

    if(job_id != NULL){
        *job_id = (const char *)jobId;
    }

    if(out_len != NULL){
        *out_len = jobIdLength;
    }

    return QM_EOK;
}

/* @[declare_jobs_getjobid] */

/**
 * @brief Retrieves the job document from a given message (if applicable)
 *
 * @param msg [In] A JSON formatted message which
 * @param msg_len [In] The length of the message
 * @param jobdoc [Out] The job document
 * @return int The length of the job document
 *
 */
/* @[declare_jobs_getjobdocument] */
int qm_iot_jobs_get_document(const char *msg, int msg_len, const char **jobdoc, int *out_len)
{
    int type = 0;

    char *root = NULL;
    int root_len = 0;

    char *jobDoc = NULL;
    int jobDocLength = 0;

    root = qm_json_get_value_by_name((char *)msg, msg_len, "execution", &root_len, &type);
    if(root == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "execution find failed!!");
        return -QM_EINVAL;
    }

    jobDoc = qm_json_get_value_by_name(root, root_len, "jobDocument", &jobDocLength, &type);
    if(jobDoc == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "jobDocument find failed!!");
        return -QM_EINVAL;  
    }

    if(jobdoc != NULL){
        *jobdoc = (const char *)jobDoc;
    }

    if(out_len != NULL){
        *out_len = jobDocLength;
    }

    return QM_EOK;
}
/* @[declare_jobs_getjobdocument] */
