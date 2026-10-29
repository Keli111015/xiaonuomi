/*
 * AWS IoT Jobs v1.5.1
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * @file jobs.h
 * @brief Client library APIs for the AWS IoT Jobs service.
 *
 * https://docs.aws.amazon.com/iot/latest/developerguide/jobs-api.html#jobs-mqtt-api
 */

#ifndef JOBS_H_
#define JOBS_H_

#include "qm.h"

/* *INDENT-OFF* */
#ifdef __cplusplus
    extern "C" {
#endif
/* *INDENT-ON* */

/**
 * @ingroup jobs_constants
 * @brief  Size of Topic Buffer
 */
#define TOPIC_BUFFER_SIZE            256U

/**
 * @ingroup jobs_constants
 * @brief Size of Jobs Start Next Message Buffer
 */
#define START_JOB_MSG_LENGTH         147U

/**
 * @ingroup jobs_constants
 * @brief Size of Jobs Update Message Buffer
 */
#define UPDATE_JOB_MSG_LENGTH        48U

/**
 * @ingroup jobs_constants
 * @brief Maximum length of a thing name for the AWS IoT Jobs Service.
 */
#define JOBS_THINGNAME_MAX_LENGTH    128U      /* per AWS IoT API Reference */

/**
 * @ingroup jobs_constants
 * @brief Maximum length of a job ID for the AWS IoT Jobs Service.
 */
#define JOBS_JOBID_MAX_LENGTH        64U       /* per AWS IoT API Reference */

#ifndef THINGNAME_MAX_LENGTH

/**
 * @brief User defined maximum length of a thing name for the application.
 *
 * <br><b>Default value</b>: @ref JOBS_THINGNAME_MAX_LENGTH "JOBS_THINGNAME_MAX_LENGTH"
 */
    #define THINGNAME_MAX_LENGTH    JOBS_THINGNAME_MAX_LENGTH
#endif

#ifndef JOBID_MAX_LENGTH

/**
 * @brief User defined maximum length of a job ID for the application.
 *
 * <br><b>Default value</b>: @ref JOBS_JOBID_MAX_LENGTH "JOBS_JOBID_MAX_LENGTH"
 */
    #define JOBID_MAX_LENGTH    JOBS_JOBID_MAX_LENGTH
#endif

#if ( THINGNAME_MAX_LENGTH > JOBS_THINGNAME_MAX_LENGTH )
    #error "The value of THINGNAME_MAX_LENGTH exceeds the AWS IoT Jobs Service limit."
#endif

#if ( JOBID_MAX_LENGTH > JOBS_JOBID_MAX_LENGTH )
    #error "The value of JOBID_MAX_LENGTH exceeds the AWS IoT Jobs Service limit."
#endif

/**
 * @cond DOXYGEN_IGNORE
 * Doxygen should ignore these macros as they are private.
 */

#define JOBS_API_PREFIX                     "$aws/things/"
#define JOBS_API_PREFIX_LENGTH              ( sizeof( JOBS_API_PREFIX ) - 1U )

#define JOBS_API_BRIDGE                     "/jobs/"
#define JOBS_API_BRIDGE_LENGTH              ( sizeof( JOBS_API_BRIDGE ) - 1U )

#define JOBS_API_FAILURE                    "/rejected"
#define JOBS_API_FAILURE_LENGTH             ( sizeof( JOBS_API_FAILURE ) - 1U )

#define JOBS_API_JOBSCHANGED                "notify"
#define JOBS_API_JOBSCHANGED_LENGTH         ( sizeof( JOBS_API_JOBSCHANGED ) - 1U )

#define JOBS_API_NEXTJOBCHANGED             "notify-next"
#define JOBS_API_NEXTJOBCHANGED_LENGTH      ( sizeof( JOBS_API_NEXTJOBCHANGED ) - 1U )

#define JOBS_API_GETPENDING                 "get"
#define JOBS_API_GETPENDING_LENGTH          ( sizeof( JOBS_API_GETPENDING ) - 1U )

#define JOBS_API_STARTNEXT                  "start-next"
#define JOBS_API_STARTNEXT_LENGTH           ( sizeof( JOBS_API_STARTNEXT ) - 1U )

#define JOBS_API_DESCRIBE                   "get"
#define JOBS_API_DESCRIBE_LENGTH            ( sizeof( JOBS_API_DESCRIBE ) - 1U )

#define JOBS_API_UPDATE                     "update"
#define JOBS_API_UPDATE_LENGTH              ( sizeof( JOBS_API_UPDATE ) - 1U )

#define JOBS_API_JOBID_NEXT                 "$next"
#define JOBS_API_JOBID_NEXT_LENGTH          ( sizeof( JOBS_API_JOBID_NEXT ) - 1U )

#define JOBS_API_JOBID_NULL                 ""
#define JOBS_API_LEVEL_SEPARATOR            "/"

#define JOBS_API_CLIENTTOKEN                "{\"clientToken\":\""
#define JOBS_API_CLIENTTOKEN_LENGTH         ( sizeof( JOBS_API_CLIENTTOKEN ) - 1U )

#define JOBS_API_STATUS                     "{\"status\":\""
#define JOBS_API_STATUS_LENGTH              ( sizeof( JOBS_API_STATUS ) - 1U )

#define JOBS_API_DETAILS_PROGRESS           "\",\"statusDetails\":{\"progress\":\""
#define JOBS_API_DETAILS_PROGRESS_LENGTH    ( sizeof( JOBS_API_DETAILS_PROGRESS ) - 1U )

#define JOBS_API_EXPECTED_VERSION           ",\"expectedVersion\":\""
#define JOBS_API_EXPECTED_VERSION_LENGTH    ( sizeof( JOBS_API_EXPECTED_VERSION ) - 1U )

#define JOBS_API_COMMON_LENGTH( thingNameLength ) \
    ( JOBS_API_PREFIX_LENGTH + ( thingNameLength ) + JOBS_API_BRIDGE_LENGTH )

/** @endcond */


/**
 * @cond DOXYGEN_IGNORE
 * Doxygen should ignore this macro as it is private.
 */

/* AWS IoT Jobs API topics. */
#define JOBS_TOPIC_COMMON( thingName, jobId, jobsApi ) \
    ( JOBS_API_PREFIX                                  \
      thingName                                        \
      JOBS_API_BRIDGE                                  \
      jobId                                            \
      jobsApi )
/** @endcond */

/**
 * @ingroup jobs_constants
 * @brief Topic string for subscribing to the NextJobExecutionChanged API.
 *
 * This macro should be used when the thing name is known at the compile time.
 * If the thing name is not known at compile time, the #Jobs_GetTopic API
 * should be used instead.
 *
 * @param thingName The registered thing name.
 */
#define JOBS_API_SUBSCRIBE_NEXTJOBCHANGED( thingName ) \
    JOBS_TOPIC_COMMON( thingName, JOBS_API_JOBID_NULL, JOBS_API_NEXTJOBCHANGED )

/**
 * @ingroup jobs_constants
 * @brief Topic string for subscribing to the JobExecutionsChanged API.
 *
 * This macro should be used when the thing name is known at the compile time.
 * If the thing name is not known at compile time, the #Jobs_GetTopic API
 * should be used instead.
 *
 * @param thingName The registered thing name.
 */
#define JOBS_API_SUBSCRIBE_JOBSCHANGED( thingName ) \
    JOBS_TOPIC_COMMON( thingName, JOBS_API_JOBID_NULL, JOBS_API_JOBSCHANGED )

/**
 * @ingroup jobs_constants
 * @brief Topic string for publishing to the StartNextPendingJobExecution API.
 *
 * This macro should be used when the thing name is known at the compile time.
 * If the thing name is not known at compile time, the #Jobs_StartNext API
 * should be used instead.
 *
 * @param thingName The registered thing name.
 */
#define JOBS_API_PUBLISH_STARTNEXT( thingName ) \
    JOBS_TOPIC_COMMON( thingName, JOBS_API_JOBID_NULL, JOBS_API_STARTNEXT )

/**
 * @ingroup jobs_constants
 * @brief Topic string for publishing to the GetPendingJobExecutions API.
 *
 * This macro should be used when the thing name is known at the compile time.
 * If the thing name is not known at compile time, the #Jobs_GetPending API
 * should be used instead.
 *
 * @param thingName The registered thing name.
 */
#define JOBS_API_PUBLISH_GETPENDING( thingName ) \
    JOBS_TOPIC_COMMON( thingName, JOBS_API_JOBID_NULL, JOBS_API_GETPENDING )

/**
 * @ingroup jobs_constants
 * @brief Topic string for querying the next pending job from the
 * DescribeJobExecution API.
 *
 * This macro should be used when the thing name and jobID are known at the
 * compile time. If next pending job is being queried, use $next as job ID.
 * If the thing name or job ID are not known at compile time, the #Jobs_Describe API
 * should be used instead.
 *
 * @param thingName The registered thing name.
 */
#define JOBS_API_PUBLISH_DESCRIBENEXTJOB( thingName ) \
    JOBS_TOPIC_COMMON( thingName, JOBS_API_JOBID_NEXT JOBS_API_LEVEL_SEPARATOR, JOBS_API_DESCRIBE )

/**
 * @ingroup jobs_constants
 * @brief The size needed to hold the longest topic for a given thing name length.
 * @note This includes space for a terminating NUL character.
 */
#define JOBS_API_MAX_LENGTH( thingNameLength )                    \
    ( JOBS_API_COMMON_LENGTH( thingNameLength ) +                 \
      JOBID_MAX_LENGTH + sizeof( '/' ) + JOBS_API_UPDATE_LENGTH + \
      JOBS_API_SUCCESS_LENGTH + 1U )


/**
 * @ingroup jobs_enum_types
 * @brief Return codes from jobs functions.
 */
typedef enum
{
    QM_IOT_JOBSERROR = 0,
    QM_IOT_JOBSSUCCESS,       /**< @brief The buffer was properly written or a match was found. */
    QM_IOT_JOBSNOMATCH,       /**< @brief The buffer does not contain a jobs topic. */
    QM_IOT_JOBSBADPARAM,      /**< @brief A function parameter was NULL or has an illegal value. */
    QM_IOT_JOBSBUFFTOSMALL,   /**< @brief The buffer write was truncated. */
} qm_iot_jobs_code_t;

/**
 * @brief Status codes for jobs
 */
typedef enum
{
    QM_IOT_JOBS_QUEUED,
    QM_IOT_JOBS_INPROGRESS,
    QM_IOT_JOBS_FAILED,
    QM_IOT_JOBS_SUCCESSED,
    QM_IOT_JOBS_REJECTED,
} qm_iot_jobs_status_t;

/**
 * @brief Status codes for job update status
 */
typedef enum
{
    QM_IOT_JOBS_UPDATE_ACCEPTED,
    QM_IOT_JOBS_UPDATE_REJECTED,
} qm_iot_jobs_update_t;

/**
 * @ingroup jobs_enum_types
 * @brief Topic values for subscription requests.
 *
 * @note The enum values for valid topics must be contiguous,
 * starting with 0.  The last valid topic must be followed
 * by JobsMaxTopic.  This arrangement is necessary since the
 * enum values are used as indexes to arrays of topic strings
 * and lengths.
 *
 * @note The ordering is important, providing a means
 * to divide topics into those that use a job ID
 * and those that do not.
 *
 * @note These constraints are enforced by a unit test.
 */
typedef enum
{
    QM_IOT_JOBS_TOPIC_INVALID = -1,
    
    QM_IOT_JOBS_TOPIC_NEX_CHANGED,

    QM_IOT_JOBS_TOPIC_MAX,
} qm_iot_jobs_topic_t;

/* *INDENT-OFF* */
#ifdef __cplusplus
    }
#endif
/* *INDENT-ON* */

#endif /* ifndef JOBS_H_ */
