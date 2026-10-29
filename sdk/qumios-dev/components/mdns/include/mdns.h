/**
 * @file mdns.h
 * @brief Synchronous DNS-SD service discovery API.
 */
#ifndef MDNS_H
#define MDNS_H

#include "qm_errno.h"
#include "qm_types.h"

/** @brief Maximum decoded DNS name length, including the NUL terminator. */
#define MDNS_NAME_LEN 256U
/** @brief Maximum host/address string length, including the NUL terminator. */
#define MDNS_HOST_LEN MDNS_NAME_LEN
/** @brief Maximum service instance string length, including the NUL terminator. */
#define MDNS_INSTANCE_LEN MDNS_NAME_LEN
/** @brief Maximum raw TXT RDATA length retained for one service. */
#define MDNS_TXT_DATA_LEN 192U

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A service endpoint returned by mdns_query().
 *
 * All strings are NUL-terminated and are owned by the component query
 * storage. They are valid only while the mdns_service_cb_t callback is
 * running and must be copied by the application if they are needed later.
 */
typedef struct {
    char host[MDNS_HOST_LEN]; /**< Resolved IPv4 address, or the SRV target host name when no A
                                 record is available. */
    uint16_t port; /**< Non-zero service port from a valid SRV record. */
    char instance[MDNS_INSTANCE_LEN]; /**< Fully qualified DNS-SD service instance name such as
                                         "Printer._ipp._tcp.local". */
    uint8_t txt[MDNS_TXT_DATA_LEN];   /**< Raw DNS-SD TXT RDATA. */
    uint16_t txt_len;                 /**< Number of valid bytes in @ref txt. */
    uint8_t txt_truncated;            /**< Non-zero when TXT RDATA exceeded the local limit. */
} mdns_service_t;

/**
 * @brief Copy a value from raw DNS-SD TXT RDATA.
 *
 * TXT keys are matched case-insensitively. A key without an equals sign is
 * returned as an empty string.
 *
 * @param[in] service Service returned by mdns_query().
 * @param[in] key NUL-terminated TXT key to find.
 * @param[out] value Destination for the NUL-terminated value.
 * @param[in] capacity Size of @p value in bytes, including its terminator.
 *
 * @return QM_EOK on success, -QM_ENOENT when absent, -QM_EFULL when @p value
 *         is too small or TXT RDATA was truncated, or -QM_EINVAL for invalid
 *         arguments or malformed TXT data.
 */
int mdns_txt_get(const mdns_service_t *service, const char *key, char *value, uint16_t capacity);

/**
 * @brief Handle one discovered service.
 *
 * The callback is invoked synchronously once per discovered service instance
 * and stops either when it returns a non-zero value or when all instances
 * have been reported.
 *
 * @param[in] service Service data valid for the duration of this callback.
 * @param[in] user User context passed to mdns_query().
 *
 * @return QM_EOK to continue with the next service. Any non-zero value stops
 *         discovery and is returned by mdns_query().
 */
typedef int (*mdns_service_cb_t)(const mdns_service_t *service, void *user);

/**
 * @brief Query DNS-SD services synchronously.
 *
 * The query sends a PTR request to the mDNS multicast endpoint through the
 * QUMI UDP HAL, requests unicast responses, collects and aggregates the
 * PTR/SRV/TXT/A records from one or more response packets, and reports each
 * fully resolved service instance through @p callback. The component owns the
 * temporary packet buffers and closes the UDP handle before returning.
 *
 * @param[in] service_type DNS-SD type such as "_ipp._tcp.local".
 * @param[in] timeout_ms Maximum wait for the first response packet in
 *            milliseconds; subsequent tail packets use a short fixed wait.
 * @param[in] callback Callback invoked once per discovered service instance.
 * @param[in] user User context passed unchanged to callback.
 *
 * @return QM_EOK when at least one service was discovered and the callback
 *         returned QM_EOK; a non-zero callback value is returned unchanged.
 *         Otherwise, a negative QM_E* error code is returned. HAL errors are
 *         propagated when no valid response was accepted, and -QM_ENOENT is
 *         returned when valid responses contained no complete service
 *         instance.
 */
int mdns_query(const char *service_type, uint32_t timeout_ms, mdns_service_cb_t callback,
               void *user);

#ifdef __cplusplus
}
#endif
#endif
