/*
 * AWS IoT Jobs v1.5.1
 * Copyright (C) 2023 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: MIT
 *
 * Licensed under the MIT License. See the LICENSE accompanying this file
 * for the specific language governing permissions and limitations under
 * the License.
 */

#ifndef QM_IOT_JOBS_H
#define QM_IOT_JOBS_H

#include "qm.h"
#include "qm_iot_crypto.h"
#include "qm_iot_jobs_profile.h"

/**
 * @ingroup jobs file structs
 * @brief struct containing the fields of an AFR OTA Job Document
 */
typedef struct
{
    qm_iot_crypto_hash_t hash_crypto;
    qm_iot_crypto_asymmetric_t asymmetric_crypto;
    /** @brief Code Signing Signature */
    const char * signature;

    /** @brief Length of signature */
    size_t signatureLen;

    /** @brief File path to store OTA Update on device */
    const char * filepath;

    /** @brief Length of filepath */
    size_t filepathLen;

    /** @brief Path to Code Signing Certificate on Device */
    const char * certfile;

    /** @brief Length of certfile */
    size_t certfileLen;

    /** @brief Authentication Scheme for HTTP URL ( null for MQTT ) */
    const char * authScheme;

    /** @brief Length of authScheme */
    size_t authSchemeLen;

    /** @brief MQTT Stream or HTTP URL */
    const char * imageRef;

    /** @brief Length of imageRef */
    size_t imageRefLen;

    /** @brief File ID */
    uint32_t fileId;

    /** @brief Size of the OTA Update */
    uint32_t fileSize;

    /** @brief File Type */
    uint32_t fileType;

    /** @brief Length of version */
    size_t versionLen;

    /** @brief version Name */
    char *version;

    /** @brief Length of extra */
    size_t extraLen;

    /** @brief extra */
    char *extra;

} qm_iot_jobs_file_t;

/**
 * @brief Populate a topic string for a subscription request.
 *
 * @param[in] topic  The desired Jobs API, e.g., QM_IOT_JOBS_TOPIC_NEX_CHANGED.
 * @param[in] did  The device's thingName as registered with AWS IoT.
 * @param[in] did_len  The length of the thingName.
 * @param[in] buffer  The buffer to contain the topic string.
 * @param[in] in_buffer_len  The size of the buffer.
 * @param[out] out_buffer_len  The length of the topic string written to the buffer.
 *
 * @return #JobsSuccess if the topic was written to the buffer;
 * #JobsBadParameter if invalid parameters are passed;
 * #JobsBufferTooSmall if the buffer cannot hold the full topic string.
 *
 * When all parameters are valid, the topic string is written to
 * the buffer up to one less than the buffer size.  The topic is
 * ended with a NUL character.
 *
 * @note The thingName parameter does not need a NUL terminator.
 *
 * @note The AWS IoT Jobs service does not require clients to subscribe
 * to the "/accepted" and "/rejected" response topics for the APIs that
 * accept requests on PUBLISH topics. The Jobs service will send responses
 * to requests from clients irrespective of whether they have subscribed to
 * response topics or not. For more information, refer to the AWS docs here:
 * https://docs.aws.amazon.com/iot/latest/developerguide/jobs-mqtt-api.html
 *
 */
qm_iot_jobs_code_t qm_iot_jobs_get_topic(qm_iot_jobs_topic_t topic,  
                                            const char * did, int did_len,
                                            const char * jobid, int jobid_len,
                                            char * buffer, int in_buffer_len, int *out_buffer_len);

/**
 * @brief Populate a topic string for a GetPendingJobExecutions request.
 *
 * @param[in] did  The device's thingName as registered with AWS IoT.
 * @param[in] did_len  The length of the thingName.
 * @param[in] buffer  The buffer to contain the topic string.
 * @param[in] in_buffer_len  The size of the buffer.
 * @param[out] out_buffer_len  The length of the topic string written to the buffer.
 * 
 * @return #JobsSuccess if the topic was written to the buffer;
 * #JobsBadParameter if invalid parameters are passed;
 * #JobsBufferTooSmall if the buffer cannot hold the full topic string.
 *
 * When all parameters are valid, the topic string is written to
 * the buffer up to one less than the buffer size.  The topic is
 * ended with a NUL character.
 *
 * @note The thingName parameter does not need a NUL terminator.
 *
 * @note The AWS IoT Jobs service does not require clients to subscribe
 * to the "/accepted" and "/rejected" response topics of the
 * GetPendingJobExecutions API.
 * The Jobs service will send responses to requests published to the API
 * from clients irrespective of whether they have subscribed to response topics
 * or not. For more information, refer to the AWS docs here:
 * https://docs.aws.amazon.com/iot/latest/developerguide/jobs-mqtt-api.html
 *
 */
qm_iot_jobs_code_t qm_iot_jobs_get_pending( const char * did, uint16_t did_len,
                                            char *buffer, int in_buffer_len, int * out_buffer_len);


/**
 * @brief Populate a topic string for a DescribeJobExecution request.
 *
 * @param[out] jobid  The ID of the job to describe.
 * @param[out] jobid_len  The length of the job ID.
 * @param[in] did  The device's thingName as registered with AWS IoT.
 * @param[in] did_len  The length of the thingName.
 * @param[in] buffer  The buffer to contain the topic string.
 * @param[in] in_buffer_len  The size of the buffer.
 * @param[out] out_buffer_len  The length of the topic string written to the buffer.
 *
 *
 * @return #JobsSuccess if the topic was written to the buffer;
 * #JobsBadParameter if invalid parameters are passed;
 * #JobsBufferTooSmall if the buffer cannot hold the full topic string.
 *
 * When all parameters are valid, the topic string is written to
 * the buffer up to one less than the buffer size.  The topic is
 * ended with a NUL character.
 *
 * @note A jobId consisting of the string, "$next", is supported to generate
 * a topic string to request the next pending job.
 *
 * @note The thingName and jobId parameters do not need a NUL terminator.
 *
 * @note The AWS IoT Jobs service does not require clients to subscribe
 * to the "/accepted" and "/rejected" response topics of the
 * DescribeJobExecution API.
 * The Jobs service will send responses to requests published to the API
 * from clients irrespective of whether they have subscribed to response topics
 * or not. For more information, refer to the AWS docs here:
 * https://docs.aws.amazon.com/iot/latest/developerguide/jobs-mqtt-api.html
 *
 * @endcode
 */
/* @[declare_jobs_describe] */
qm_iot_jobs_code_t qm_iot_jobs_describe(const char * did, uint16_t did_len,
                                        const char * jobid, int jobid_len,
                                        char * buffer, int in_buffer_len, int *out_buffer_len);
/* @[declare_jobs_describe] */

/**
 * @brief Populate a topic string for an UpdateJobExecution request.
 *
 * @param[in] did  The device's thingName as registered with AWS IoT.
 * @param[in] did_len  The length of the thingName.
 * @param[out] jobid  The ID of the job to describe.
 * @param[out] jobid_len  The length of the job ID.
 * @param[in] buffer  The buffer to contain the topic string.
 * @param[in] in_buffer_len  The size of the buffer.
 * @param[out] out_buffer_len  The length of the topic string written to the buffer.
 *
 * @return #JobsSuccess if the topic was written to the buffer;
 * #JobsBadParameter if invalid parameters are passed;
 * #JobsBufferTooSmall if the buffer cannot hold the full topic string.
 *
 * When all parameters are valid, the topic string is written to
 * the buffer up to one less than the buffer size.  The topic is
 * ended with a NUL character.
 *
 * @note The thingName and jobId parameters do not need a NUL terminator.
 *
 * @note The AWS IoT Jobs service does not require clients to subscribe
 * to the "/accepted" and "/rejected" response topics of the
 * UpdateJobExecution API.
 * The Jobs service will send responses to requests published to the API
 * from clients irrespective of whether they have subscribed to response topics
 * or not. For more information, refer to the AWS docs here:
 * https://docs.aws.amazon.com/iot/latest/developerguide/jobs-mqtt-api.html
 *
 */
/* @[declare_jobs_update] */
qm_iot_jobs_code_t qm_iot_jobs_update(const char * did, uint16_t did_len,
                                        const char * jobid, int jobid_len,
                                        char * buffer, int in_buffer_len, int *out_buffer_len);
/* @[declare_jobs_update] */

/**
 * @brief Populate a message string for an UpdateJobExecution request.
 *
 * @param status Current status of the job
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
                            char *buffer,int buffer_size);
/* @[declare_jobs_updatemsg] */

/**
 * @brief Retrieves the job ID from a given message (if applicable)
 *
 * @param msg [In] A JSON formatted message
 * @param msg_len [In] The length of the message
 * @param job_id [Out] The job ID
 * @param out_len [Out] The job ID string len;
 * @return int The job ID length
 *
 */
/* @[declare_jobs_getjobid] */
int qm_iot_jobs_get_jobid(const char *msg, int msg_len, const char **job_id, int *out_len);
/* @[declare_jobs_getjobid] */

/**
 * @brief Retrieves the job document from a given message (if applicable)
 *
 * @param msg [In] A JSON formatted message which
 * @param msg_len [In] The length of the message
 * @param jobdoc [Out] The job document
 * @param out_len [Out] The job ID string len;
 * @return size_t The length of the job document
 *
 */
/* @[declare_jobs_getjobdocument] */
int qm_iot_jobs_get_document(const char *msg, int msg_len, const char **jobdoc, int *out_len);
/* @[declare_jobs_getjobdocument] */


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
                            int32_t fileIndex,  qm_iot_jobs_file_t * job_files);
#endif /*OTA_JOB_PROCESSOR_H*/
