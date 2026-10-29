#ifndef QM_RES_MGR_H
#define QM_RES_MGR_H

#include "qm_errno.h"
#include "qm_types.h"
#include "qm_res_mgr_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration ownership.
 *
 * Keep values that change per request, per device, or per selected resource in
 * API inputs:
 * - qm_res_mgr_set_env(): runtime environment override for HTTP metadata builds.
 * - qm_res_mgr_get_list(): token, did, resource bundle name, page number, page
 *   size, and an optional per-page callback.
 * - qm_res_mgr_get_category(): product id, did, and channel-specific
 *   credentials or handles for the compiled metadata channel.
 * - qm_res_mgr_get_children_category(): product id, did, category selector,
 *   and channel-specific credentials or handles for the compiled metadata
 *   channel.
 * - qm_res_mgr_get_detail(): device identity, resource id, and credentials or
 *   handles required by the compiled metadata channel.
 * - qm_res_mgr_download(): selected resource metadata, data callback, and
 *   callback user pointer.
 *
 * HTTPS uses the qm_http_client transport default: one-way TLS with no
 * resource-manager-specified CA certificate or client certificate.
 *
 * Keep fixed build-time tuning in qm_config/Kconfig: component support, default
 * environment, segment size, retry count, shared page size, HTTP timeout, and
 * HTTP buffer/header/body limits. The HTTP transport knobs are configurable
 * only in HTTP metadata builds; MQTT metadata builds use component defaults for
 * the internal HTTP file-download transport.
 *
 * Do not put per-device credentials, DID, bundle name, resource id, resource
 * URL, callback user pointer, or storage policy in qm_config. Do not put fixed
 * tuning like timeouts, segment size, retry count, or HTTP buffer sizes in
 * request structs.
 */

/**
 * @brief Resource API environment.
 */
typedef enum {
    QM_RES_MGR_ENV_DEV = 0,                /*!< Development environment: https://api-dev.ljyd.cn */
    QM_RES_MGR_ENV_TEST,                   /*!< Test environment: https://api-test.ljyd.cn */
    QM_RES_MGR_ENV_PROD,                   /*!< Production environment: https://api.ljyd.cn */
} qm_res_mgr_env_t;

/**
 * @brief Resource metadata returned by list/detail APIs.
 */
typedef struct {
    uint32_t id;                           /*!< Resource id returned by server */
    char *title;                           /*!< Resource title, dynamically allocated by list/detail APIs */
    char *description;                     /*!< Resource description, dynamically allocated by list/detail APIs */
    char *language;                        /*!< Resource language, dynamically allocated by list/detail APIs */
    char *type;                            /*!< Resource business type, dynamically allocated by list/detail APIs */
    char *audio_type;                      /*!< Audio codec/container from audioType, dynamically allocated by list/detail APIs */
    char *url;                             /*!< Resource download URL, dynamically allocated by list/detail APIs */
    char *file_sign;                       /*!< Resource file signature from fileSign, dynamically allocated by list/detail APIs */
    char *etag;                            /*!< Resource ETag when provided, dynamically allocated by list/detail APIs */
    char *create_time;                     /*!< Resource create time, dynamically allocated by list/detail APIs */
    char *update_time;                     /*!< Resource update time, dynamically allocated by list/detail APIs */
    uint32_t total_duration;               /*!< Total duration in seconds when provided */
    uint32_t file_size;                    /*!< Resource file size in bytes */
} qm_res_mgr_resource_t;

/**
 * @brief Resource list returned by qm_res_mgr_get_list().
 */
typedef struct {
    uint32_t total;                       /*!< Total resources reported by server */
    uint32_t page_num;                    /*!< First requested page number */
    uint32_t page_size;                   /*!< Page size reported by server */
    uint32_t pages;                       /*!< Total page count reported by server */
    uint32_t count;                       /*!< Number of parsed items across fetched pages */
    int status_code;                      /*!< Last list API HTTP status code, useful on failure; 0 in MQTT mode */
    int biz_code;                         /*!< Service business code, 0 means success */
    char *resource_bundle_title;          /*!< Optional resource bundle title from service reply */
    qm_res_mgr_resource_t *items;         /*!< Allocated by get_list; release with qm_res_mgr_list_free */
} qm_res_mgr_list_t;

/**
 * @brief Borrowed per-page view passed to qm_res_mgr_get_list() page callbacks.
 *
 * `items` points into the final caller-owned `qm_res_mgr_list_t.items` array.
 * The page slice stays valid until the caller releases the full list with
 * qm_res_mgr_list_free().
 */
typedef struct {
    uint32_t total;                       /*!< Total resources reported by server */
    uint32_t page_num;                    /*!< Current page number being delivered */
    uint32_t page_size;                   /*!< Requested page size for the current fetch */
    uint32_t pages;                       /*!< Total page count reported by server */
    uint32_t count;                       /*!< Number of parsed items in this page view */
    int status_code;                      /*!< Current page HTTP status code, 0 in MQTT mode */
    int biz_code;                         /*!< Current page business code, 0 means success */
    const char *resource_bundle_title;    /*!< Borrowed optional resource bundle title */
    const qm_res_mgr_resource_t *items;   /*!< Points to the current page slice inside qm_res_mgr_list_t.items */
} qm_res_mgr_list_page_t;

/**
 * @brief Resource list page callback.
 *
 * Called synchronously from qm_res_mgr_get_list() in the caller's thread after
 * each page is parsed successfully and before the next page is requested.
 * Return QM_EOK to continue. Return -QM_EINTR to stop pagination successfully
 * while retaining the pages already parsed; other negative QM_E* codes abort
 * the list fetch. Positive callback return values are converted to -QM_EIO.
 */
typedef int (*qm_res_mgr_list_page_cb_t)(const qm_res_mgr_list_page_t *page,
                                         void *user);

/**
 * @brief Per-call input for qm_res_mgr_get_list().
 *
 * Channel-specific fields inside this struct are compiled by
 * CONFIG_QM_RES_MGR_HTTP_SUPPORT / CONFIG_QM_RES_MGR_MQTT_SUPPORT.
 */
typedef struct {
    const char *did;                      /*!< Device DID */
    const char *resource_bundle_name;     /*!< Resource bundle name */
    uint32_t page_num;                    /*!< Start page number, 0 uses 1 */
    uint32_t page_size;                   /*!< Page size, 0 uses CONFIG_QM_RES_MGR_PAGE_SIZE */
    qm_res_mgr_list_page_cb_t on_page;    /*!< Optional synchronous page callback */
    void *page_user;                      /*!< Caller-owned pointer passed to on_page */
#if defined(CONFIG_QM_RES_MGR_HTTP_SUPPORT) && CONFIG_QM_RES_MGR_HTTP_SUPPORT
    const char *token;                    /*!< Caller-owned device authorization token */
#elif defined(CONFIG_QM_RES_MGR_MQTT_SUPPORT) && CONFIG_QM_RES_MGR_MQTT_SUPPORT
    void *iot_handle;                     /*!< Required when metadata channel is compiled as MQTT */
    uint32_t timeout_ms;                  /*!< MQTT reply wait timeout, 0 uses component default */
#endif
} qm_res_mgr_list_req_t;

/**
 * @brief Second-level category node returned by qm_res_mgr_get_children_category().
 */
typedef struct {
    uint32_t id;                           /*!< Category id returned by server */
    char *name;                            /*!< Category display name, dynamically allocated */
    char *resource_bundle_name;            /*!< Resource bundle name, dynamically allocated */
    uint32_t parent_id;                    /*!< Parent category id */
    uint32_t level;                        /*!< Category level in the service response */
    uint8_t is_leaf;                       /*!< 0 means non-leaf, 1 means leaf */
    uint32_t sort_order;                   /*!< Sort order among sibling categories */
    char *create_time;                     /*!< Category create time, dynamically allocated */
    char *description;                     /*!< Category description, dynamically allocated */
    char *icon;                            /*!< Category icon URL, dynamically allocated */
    char *background_image;                /*!< Category background image URL, dynamically allocated */
} qm_res_mgr_child_category_t;

/**
 * @brief First-level category node returned by qm_res_mgr_get_category().
 *
 * `children` only contains second-level categories.
 */
typedef struct {
    uint32_t id;                           /*!< Category id returned by server */
    char *name;                            /*!< Category display name, dynamically allocated */
    char *resource_bundle_name;            /*!< Resource bundle name, dynamically allocated */
    uint32_t parent_id;                    /*!< Parent category id */
    uint32_t level;                        /*!< Category level in the service response */
    uint8_t is_leaf;                       /*!< 0 means non-leaf, 1 means leaf */
    uint32_t sort_order;                   /*!< Sort order among sibling categories */
    char *create_time;                     /*!< Category create time, dynamically allocated */
    char *description;                     /*!< Category description, dynamically allocated */
    char *icon;                            /*!< Category icon URL, dynamically allocated */
    char *background_image;                /*!< Category background image URL, dynamically allocated */
    uint32_t child_count;                  /*!< Parsed second-level child count */
    qm_res_mgr_child_category_t *children; /*!< Parsed second-level child nodes */
} qm_res_mgr_category_t;

/**
 * @brief First-level category list returned by qm_res_mgr_get_category().
 */
typedef struct {
    uint32_t count;                        /*!< Parsed root node count */
    int biz_code;                          /*!< Service business code, 0 means success */
    qm_res_mgr_category_t *items;          /*!< Allocated by category APIs; release with qm_res_mgr_category_list_free */
} qm_res_mgr_category_list_t;

/**
 * @brief Second-level category list returned by qm_res_mgr_get_children_category().
 */
typedef struct {
    uint32_t count;                        /*!< Parsed child node count */
    int biz_code;                          /*!< Service business code, 0 means success */
    qm_res_mgr_child_category_t *items;    /*!< Allocated by category APIs; release with qm_res_mgr_child_category_list_free */
} qm_res_mgr_child_category_list_t;

/**
 * @brief Per-call input for qm_res_mgr_get_category().
 *
 * Channel-specific fields inside this struct are compiled by
 * CONFIG_QM_RES_MGR_HTTP_SUPPORT / CONFIG_QM_RES_MGR_MQTT_SUPPORT.
 */
typedef struct {
    const char *did;                       /*!< Device DID */
#if defined(CONFIG_QM_RES_MGR_HTTP_SUPPORT) && CONFIG_QM_RES_MGR_HTTP_SUPPORT
    uint32_t product_id;                   /*!< HTTP mode requires a valid product id and sends it as pid */
    const char *token;                     /*!< Optional HTTP authorization token */
#elif defined(CONFIG_QM_RES_MGR_MQTT_SUPPORT) && CONFIG_QM_RES_MGR_MQTT_SUPPORT
    void *iot_handle;                      /*!< Required when metadata channel is compiled as MQTT */
    uint32_t timeout_ms;                   /*!< MQTT reply wait timeout, 0 uses component default */
#endif
} qm_res_mgr_category_req_t;

/**
 * @brief Per-call input for qm_res_mgr_get_children_category().
 *
 * Channel-specific fields inside this struct are compiled by
 * CONFIG_QM_RES_MGR_HTTP_SUPPORT / CONFIG_QM_RES_MGR_MQTT_SUPPORT.
 */
typedef struct {
    const char *did;                       /*!< Device DID */
    uint32_t category_id;                  /*!< Optional category id selector, 0 means omit categoryId */
    const char *resource_bundle_name;      /*!< Optional resource bundle selector, NULL/empty means omit resourceBundleName */
#if defined(CONFIG_QM_RES_MGR_HTTP_SUPPORT) && CONFIG_QM_RES_MGR_HTTP_SUPPORT
    uint32_t product_id;                   /*!< HTTP mode requires a valid product id and sends it as pid */
    const char *token;                     /*!< Optional HTTP authorization token */
#elif defined(CONFIG_QM_RES_MGR_MQTT_SUPPORT) && CONFIG_QM_RES_MGR_MQTT_SUPPORT
    void *iot_handle;                      /*!< Required when metadata channel is compiled as MQTT */
    uint32_t timeout_ms;                   /*!< MQTT reply wait timeout, 0 uses component default */
#endif
} qm_res_mgr_children_category_req_t;

/**
 * @brief Per-call input for qm_res_mgr_get_detail().
 *
 * Channel-specific fields inside this struct are compiled by
 * CONFIG_QM_RES_MGR_HTTP_SUPPORT / CONFIG_QM_RES_MGR_MQTT_SUPPORT.
 */
typedef struct {
    const char *did;                      /*!< Device DID */
#if defined(CONFIG_QM_RES_MGR_HTTP_SUPPORT) && CONFIG_QM_RES_MGR_HTTP_SUPPORT
    const char *token;                    /*!< Optional HTTP authorization token */
#elif defined(CONFIG_QM_RES_MGR_MQTT_SUPPORT) && CONFIG_QM_RES_MGR_MQTT_SUPPORT
    void *iot_handle;                     /*!< Required when metadata channel is compiled as MQTT */
    uint32_t timeout_ms;                  /*!< MQTT reply wait timeout, 0 uses component default */
#endif
    uint32_t resource_id;                 /*!< Resource id */
} qm_res_mgr_detail_req_t;

/**
 * @brief Resource download data callback.
 *
 * Called from qm_res_mgr_download() in the caller's thread. Return QM_EOK to
 * continue, or a negative QM_E* code to abort and propagate the error. The
 * callback may be called multiple times across segment retries, but the offset
 * always reflects bytes already delivered successfully to previous callbacks.
 */
typedef int (*qm_res_mgr_download_data_cb_t)(const qm_res_mgr_resource_t *resource,
                                             const uint8_t *data,
                                             uint32_t len,
                                             uint32_t offset,
                                             uint32_t total_size,
                                             void *user);

/**
 * @brief Per-resource input for qm_res_mgr_download().
 */
typedef struct {
    qm_res_mgr_resource_t resource;       /*!< Resource metadata from list/detail or caller */
    qm_res_mgr_download_data_cb_t on_data; /*!< Called synchronously for every downloaded chunk */
    void *user;                           /*!< Caller-owned pointer passed to on_data */
} qm_res_mgr_download_req_t;

/**
 * @brief Result metadata returned by qm_res_mgr_download().
 */
typedef struct {
    int status_code;                      /*!< Last HTTP status code */
    uint32_t total_size;                  /*!< Final known total size in bytes */
    uint32_t downloaded_size;             /*!< Bytes delivered to on_data */
} qm_res_mgr_download_result_t;

/**
 * @brief Set resource API environment for HTTP metadata builds.
 *
 * The component maps the environment to an internal fixed base URL. Callers do
 * not pass arbitrary API base URLs. The component is ready to use without an
 * explicit init step; when this API is not called it uses the compiled default
 * environment.
 *
 * This setting is component-global. A list/detail call uses the environment
 * snapshot captured when that call starts, but product code should still avoid
 * switching environments concurrently across unrelated tasks.
 *
 * MQTT-only metadata builds do not use this setting and return `-QM_ENOSYS`.
 *
 * @param[in] env Resource API environment.
 * @return QM_EOK on success, or a negative QM_E* code.
 */
int qm_res_mgr_set_env(qm_res_mgr_env_t env);

/**
 * @brief Fetch resources by device and bundle name.
 *
 * @param[in] req Per-call request parameters. token, did, bundle name, and
 *                paging values are interface inputs. req->on_page may be set
 *                to receive each parsed page immediately. The metadata channel
 *                is selected at compile time. In MQTT mode, replies are
 *                matched to in-flight requests by method and message id.
 * @param[out] out Resource list. items is allocated by this API and must be
 *                 released with qm_res_mgr_list_free().
 * @return QM_EOK on success, or a negative QM_E* code.
 */
int qm_res_mgr_get_list(const qm_res_mgr_list_req_t *req, qm_res_mgr_list_t *out);

/**
 * @brief Release a resource list returned by qm_res_mgr_get_list().
 *
 * @param[in,out] list List to release. Safe to pass NULL.
 */
void qm_res_mgr_list_free(qm_res_mgr_list_t *list);

/**
 * @brief Fetch subscribed first-level categories and their second-level children.
 *
 * HTTP mode uses `pid` + `did` against the resource service. MQTT mode uses
 * device/up and device/down and matches replies by method and message id.
 * @param[out] out Root category list. items is allocated by this API and must
 *                 be released with qm_res_mgr_category_list_free().
 * @return QM_EOK on success, or a negative QM_E* code.
 */
int qm_res_mgr_get_category(const qm_res_mgr_category_req_t *req,
                            qm_res_mgr_category_list_t *out);

/**
 * @brief Fetch subscribed child categories through the compiled metadata channel.
 *
 * @param[in] req Per-call request parameters. category_id and
 *                resource_bundle_name are both optional selectors. HTTP mode
 *                uses `pid` + `did` against the resource service. MQTT mode
 *                uses device/up and device/down and matches replies by method
 *                and message id.
 * @param[out] out Child category list. items is allocated by this API and must
 *                 be released with qm_res_mgr_child_category_list_free().
 * @return QM_EOK on success, or a negative QM_E* code.
 */
int qm_res_mgr_get_children_category(const qm_res_mgr_children_category_req_t *req,
                                     qm_res_mgr_child_category_list_t *out);

/**
 * @brief Release a first-level category list returned by qm_res_mgr_get_category().
 *
 * @param[in,out] list Category list to release. Safe to pass NULL.
 */
void qm_res_mgr_category_list_free(qm_res_mgr_category_list_t *list);

/**
 * @brief Release a child category list returned by qm_res_mgr_get_children_category().
 *
 * @param[in,out] list Child category list to release. Safe to pass NULL.
 */
void qm_res_mgr_child_category_list_free(qm_res_mgr_child_category_list_t *list);

/**
 * @brief Release one first-level category node and its parsed second-level children.
 *
 * @param[in,out] category Category node to release. Safe to pass NULL.
 */
void qm_res_mgr_category_free(qm_res_mgr_category_t *category);

/**
 * @brief Release one second-level category node.
 *
 * @param[in,out] category Child category node to release. Safe to pass NULL.
 */
void qm_res_mgr_child_category_free(qm_res_mgr_child_category_t *category);

/**
 * @brief Release dynamic fields inside a resource returned by qm_res_mgr_get_detail().
 *
 * List items are released by qm_res_mgr_list_free(). Do not call this helper on
 * a resource whose dynamic string pointers refer to string literals or
 * application-owned storage.
 *
 * @param[in,out] resource Resource to release. Safe to pass NULL.
 */
void qm_res_mgr_resource_free(qm_res_mgr_resource_t *resource);

/**
 * @brief Fetch resource detail by resource id.
 *
 * @param[in] req Per-call request parameters. token, did, and resource id are
 *                interface inputs for the compiled metadata channel.
 * @param[out] out Resource metadata. Dynamic fields must be released with
 *                 qm_res_mgr_resource_free().
 * @return QM_EOK on success, or a negative QM_E* code.
 */
int qm_res_mgr_get_detail(const qm_res_mgr_detail_req_t *req, qm_res_mgr_resource_t *out);

/**
 * @brief Download a resource synchronously.
 *
 * This API downloads the resource URL through HTTP and is available in both
 * metadata-channel builds.
 *
 * The call blocks until the resource download completes, fails, or on_data
 * returns a non-zero value. Downloaded bytes are never stored by the component;
 * every received chunk is delivered to req->on_data immediately. Product code
 * owns file hit checks, persistence, cache validation, and storage cleanup.
 * When the final total size is known, success means the delivered byte count
 * matches that final total exactly.
 *
 * @param[in] req Resource metadata and data callback.
 * @param[out] result Optional result metadata. May be NULL.
 * @return QM_EOK on success, or a negative QM_E* code. If on_data returns a
 *         negative value, that value is returned; positive callback values are
 *         converted to -QM_EIO.
 */
int qm_res_mgr_download(const qm_res_mgr_download_req_t *req,
                        qm_res_mgr_download_result_t *result);

#ifdef __cplusplus
}
#endif

#endif
