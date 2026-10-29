/**
 * @file comtrace_discovery.h
 * @brief Discovery of CELP/1 and CEUP/1 service endpoints.
 */
#ifndef COMTRACE_DISCOVERY_H
#define COMTRACE_DISCOVERY_H

#include "comtrace_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COMTRACE_ENDPOINT_HOST_LEN 64U
#define COMTRACE_ENDPOINT_INSTANCE_ID_LEN 40U

/** @brief ComTrace service advertised through DNS-SD. */
typedef enum {
    COMTRACE_SERVICE_LOG_TCP = 0,
    COMTRACE_SERVICE_LOG_UDP,
    COMTRACE_SERVICE_OTA_TCP,
} comtrace_service_t;

/** @brief Validated ComTrace endpoint returned by discovery. */
typedef struct {
    char host[COMTRACE_ENDPOINT_HOST_LEN];
    uint16_t port;
    uint16_t max_frame;
    char instance_id[COMTRACE_ENDPOINT_INSTANCE_ID_LEN];
} comtrace_endpoint_t;

typedef int (*comtrace_endpoint_cb_t)(const comtrace_endpoint_t *endpoint, void *user);

/**
 * @brief Discover and validate ComTrace endpoints synchronously.
 *
 * The endpoint is borrowed and valid only during the callback.
 */
int comtrace_discover(comtrace_service_t service, uint32_t timeout_ms,
                      comtrace_endpoint_cb_t callback, void *user);

#ifdef __cplusplus
}
#endif
#endif
