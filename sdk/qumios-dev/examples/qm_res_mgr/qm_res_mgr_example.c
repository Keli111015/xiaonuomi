#include "qm_config.h"

#include "qm_res_mgr.h"

#include "qm.h"
#include "qm_ble_gap.h"
#include "qm_cli.h"
#include "qm_errno.h"
#include "qm_event.h"
#include "qm_iot_api.h"
#include "qm_iot_dynreg.h"
#include "qm_log.h"
#include "qm_provision.h"
#include "qm_string.h"
#include "qm_wifi.h"

#define LOG_TAG "qm_res_mgr_example"

#ifndef EXAMPLE_WIFI_SSID
#define EXAMPLE_WIFI_SSID "example_wifi_ssid"
#endif

#ifndef EXAMPLE_WIFI_PASSWORD
#define EXAMPLE_WIFI_PASSWORD "example_wifi_password"
#endif

#ifndef QM_RES_MGR_EXAMPLE_TOKEN
#define QM_RES_MGR_EXAMPLE_TOKEN "example_device_token"
#endif

#ifndef QM_RES_MGR_EXAMPLE_DID
#define QM_RES_MGR_EXAMPLE_DID "example_device_did"
#endif

#ifndef QM_RES_MGR_EXAMPLE_BUNDLE
#define QM_RES_MGR_EXAMPLE_BUNDLE "example_resource_bundle"
#endif

#ifndef QM_RES_MGR_EXAMPLE_RESOURCE_ID
#define QM_RES_MGR_EXAMPLE_RESOURCE_ID 0
#endif

#ifndef QM_RES_MGR_EXAMPLE_DIRECT_URL
#define QM_RES_MGR_EXAMPLE_DIRECT_URL ""
#endif

#ifndef QM_RES_MGR_EXAMPLE_DIRECT_FILE_SIZE
#define QM_RES_MGR_EXAMPLE_DIRECT_FILE_SIZE 0
#endif

#ifndef QM_RES_MGR_EXAMPLE_ENV
#define QM_RES_MGR_EXAMPLE_ENV QM_RES_MGR_ENV_DEV
#endif

#if (CONFIG_QM_RES_MGR_HTTP_SUPPORT && CONFIG_QM_RES_MGR_MQTT_SUPPORT) || \
    (!CONFIG_QM_RES_MGR_HTTP_SUPPORT && !CONFIG_QM_RES_MGR_MQTT_SUPPORT)
#error "qm_res_mgr example requires exactly one metadata channel: CONFIG_QM_RES_MGR_HTTP_SUPPORT or CONFIG_QM_RES_MGR_MQTT_SUPPORT"
#endif

#ifndef CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_ID
#define CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_ID 0U
#endif

#ifndef CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_SECRET
#define CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_SECRET ""
#endif

#ifndef CONFIG_QM_RES_MGR_EXAMPLE_VERSION
#define CONFIG_QM_RES_MGR_EXAMPLE_VERSION "1.0.0"
#endif

#ifndef CONFIG_QM_RES_MGR_EXAMPLE_DYNREG_HOST
#define CONFIG_QM_RES_MGR_EXAMPLE_DYNREG_HOST ""
#endif

#ifndef CONFIG_QM_RES_MGR_EXAMPLE_ADV_CID
#define CONFIG_QM_RES_MGR_EXAMPLE_ADV_CID ""
#endif

#ifndef CONFIG_QM_RES_MGR_EXAMPLE_DYNREG_ENV
#define CONFIG_QM_RES_MGR_EXAMPLE_DYNREG_ENV QM_IOT_DYNREG_ENV_PROD
#endif

#ifndef CONFIG_QM_RES_MGR_EXAMPLE_WIFI_RETRY_MAX
#define CONFIG_QM_RES_MGR_EXAMPLE_WIFI_RETRY_MAX 10U
#endif

#ifndef CONFIG_QM_RES_MGR_EXAMPLE_MQTT_TIMEOUT_MS
#define CONFIG_QM_RES_MGR_EXAMPLE_MQTT_TIMEOUT_MS 15000U
#endif

#define QM_RES_MGR_EXAMPLE_LOG_URL_MAX 160
#define EXAMPLE_WIFI_RETRY_MAX_COUNT 10

static void *s_iot_handle;
static qm_iot_cloud_auth_t s_cloud_credential;
static char s_server_cert[CONFIG_QM_IOT_CRT_MAX_LEN + 1];
static char s_client_crt[CONFIG_QM_IOT_CRT_MAX_LEN + 1];
static char s_client_key[CONFIG_QM_IOT_CRT_MAX_LEN + 1];
static char s_ota_public_key[CONFIG_QM_IOT_OTA_CRT_MAX_LEN + 1];
static qm_provision_handle_t s_provision_handle;
static qm_bool s_iot_started;
static qm_bool s_wifi_event_registered;
static qm_bool s_wifi_started;
static qm_bool s_wifi_reconfiguring;
static qm_bool s_provision_running;
static qm_bool s_res_mgr_demo_started;
static qm_bool s_res_mgr_started_by_cloud_conn;
static int s_wifi_retry_count;
static char s_dynreg_host[QM_PROVISION_DYNREG_HOST_SIZE];
static qm_wifi_ap_record_t s_wifi_ap_info;
static qm_bool s_wifi_ap_info_valid;
static char s_wifi_ip[sizeof(((qm_iot_wifi_devinfo_t *)0)->ip)];
static char s_device_sn[13];

static int qm_res_mgr_example_start_inner(void *iot_handle);

static void qm_res_mgr_example_url_for_log(const char *url, char *out, int out_len)
{
    const char *scheme;
    const char *host;
    const char *end;
    int len;

    if (!out || out_len <= 0) return;
    out[0] = '\0';
    if (!url) return;

    scheme = qm_strstr(url, "://");
    if (scheme) {
        host = scheme + 3;
        end = host;
        while (*end && *end != '/' && *end != '?' && *end != '#') end++;
        len = (int)(end - url);
        if (len >= out_len) len = out_len - 1;
        qm_memcpy(out, url, (uint32_t)len);
        out[len] = '\0';
        if (*end == '/' && len + 4 < out_len) {
            out[len++] = '/';
            out[len++] = '.';
            out[len++] = '.';
            out[len++] = '.';
            out[len] = '\0';
        }
        return;
    }

    end = url;
    while (*end && *end != '?' && *end != '#') end++;
    len = (int)(end - url);
    if (len >= out_len) len = out_len - 1;
    qm_memcpy(out, url, (uint32_t)len);
    out[len] = '\0';
    if ((*end == '?' || *end == '#') && len + 4 < out_len) {
        out[len++] = *end;
        out[len++] = '.';
        out[len++] = '.';
        out[len++] = '.';
        out[len] = '\0';
    }
}

static void qm_res_mgr_example_log_resource(const char *tag,
                                            const qm_res_mgr_resource_t *resource)
{
    char log_url[QM_RES_MGR_EXAMPLE_LOG_URL_MAX];

    if (!resource) return;
    qm_res_mgr_example_url_for_log(resource->url, log_url, sizeof(log_url));

    QM_LOGI(LOG_TAG,
            "%s id=%u title=%s description=%s language=%s type=%s audio_type=%s total_duration=%u url=%s file_sign=%s etag=%s create_time=%s update_time=%s file_size=%u",
            tag,
            (unsigned int)resource->id,
            resource->title ? resource->title : "",
            resource->description ? resource->description : "",
            resource->language ? resource->language : "",
            resource->type ? resource->type : "",
            resource->audio_type ? resource->audio_type : "",
            (unsigned int)resource->total_duration,
            log_url,
            resource->file_sign ? resource->file_sign : "",
            resource->etag ? resource->etag : "",
            resource->create_time ? resource->create_time : "",
            resource->update_time ? resource->update_time : "",
            (unsigned int)resource->file_size);
}

static int qm_res_mgr_example_page_cb(const qm_res_mgr_list_page_t *page, void *user)
{
    uint32_t i;

    (void)user;

    if (page == NULL) {
        return -QM_EINVAL;
    }

    QM_LOGI(LOG_TAG,
            "page callback page=%u/%u page_size=%u count=%u total=%u bundle_title=%s",
            (unsigned int)page->page_num,
            (unsigned int)page->pages,
            (unsigned int)page->page_size,
            (unsigned int)page->count,
            (unsigned int)page->total,
            page->resource_bundle_title ? page->resource_bundle_title : "");

    for (i = 0; i < page->count; i++) {
        char tag[24];
        int ret = qm_snprintf(tag, sizeof(tag), "page[%u].item[%u]",
                              (unsigned int)page->page_num,
                              (unsigned int)i);

        if (ret < 0 || ret >= (int)sizeof(tag)) {
            (void)qm_snprintf(tag, sizeof(tag), "page_item");
        }
        qm_res_mgr_example_log_resource(tag, &page->items[i]);
    }

    return QM_EOK;
}

static int qm_res_mgr_example_data_cb(const qm_res_mgr_resource_t *resource,
                                      const uint8_t *data,
                                      uint32_t len,
                                      uint32_t offset,
                                      uint32_t total_size,
                                      void *user)
{
    uint32_t *total_read = (uint32_t *)user;

    (void)resource;
    (void)data;
    (void)total_size;

    if (!total_read || !data || len == 0) return -QM_EINVAL;

    *total_read += len;
    QM_LOGI(LOG_TAG, "download data offset=%u chunk=%u total_read=%u total=%u",
            (unsigned int)offset,
            (unsigned int)len,
            (unsigned int)*total_read,
            (unsigned int)total_size);

    return QM_EOK;
}

static int qm_res_mgr_example_download_resource(const qm_res_mgr_resource_t *resource)
{
    qm_res_mgr_download_req_t download_req;
    qm_res_mgr_download_result_t result;
    uint32_t total_read = 0;
    int ret;

    if (!resource || !resource->url || !resource->url[0]) return -QM_EINVAL;

    qm_memset(&download_req, 0, sizeof(download_req));
    qm_memset(&result, 0, sizeof(result));
    download_req.resource = *resource;
    download_req.on_data = qm_res_mgr_example_data_cb;
    download_req.user = &total_read;

    ret = qm_res_mgr_download(&download_req, &result);
    QM_LOGI(LOG_TAG, "download result ret=%d status=%d total_read=%u downloaded=%u total=%u",
            ret,
            result.status_code,
            (unsigned int)total_read,
            (unsigned int)result.downloaded_size,
            (unsigned int)result.total_size);
    return ret;
}

static int qm_res_mgr_example_direct_start(void)
{
    qm_res_mgr_resource_t resource;
    int ret;

    qm_memset(&resource, 0, sizeof(resource));
    resource.id = QM_RES_MGR_EXAMPLE_RESOURCE_ID ? QM_RES_MGR_EXAMPLE_RESOURCE_ID : 1;
    resource.file_size = QM_RES_MGR_EXAMPLE_DIRECT_FILE_SIZE;
    resource.url = qm_strdup(QM_RES_MGR_EXAMPLE_DIRECT_URL);
    if (!resource.url) {
        QM_LOGE(LOG_TAG, "direct url alloc failed");
        return -QM_ENOMEM;
    }

    qm_res_mgr_example_log_resource("direct", &resource);
    ret = qm_res_mgr_example_download_resource(&resource);
    qm_res_mgr_resource_free(&resource);
    return ret;
}

static int qm_res_mgr_example_start_inner(void *iot_handle)
{
    qm_res_mgr_list_req_t list_req;
    qm_res_mgr_list_t list;
    uint32_t matched_count = 0;
    uint32_t download_ok_count = 0;
    int ret;

    ret = qm_res_mgr_set_env(QM_RES_MGR_EXAMPLE_ENV);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "set env failed ret=%d", ret);
        return ret;
    }

    if (QM_RES_MGR_EXAMPLE_DIRECT_URL[0]) {
        return qm_res_mgr_example_direct_start();
    }

    if (!QM_RES_MGR_EXAMPLE_DID[0] || !QM_RES_MGR_EXAMPLE_BUNDLE[0]) {
        QM_LOGW(LOG_TAG, "set QM_RES_MGR_EXAMPLE_DID/BUNDLE before running list demo");
        return -QM_EINVAL;
    }

#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
    if (!QM_RES_MGR_EXAMPLE_TOKEN[0]) {
        QM_LOGW(LOG_TAG, "set QM_RES_MGR_EXAMPLE_TOKEN before running http list demo");
        return -QM_EINVAL;
    }
#else
    if (iot_handle == NULL) {
        QM_LOGE(LOG_TAG, "iot_handle is required in mqtt transport");
        return -QM_EINVAL;
    }
#endif

    qm_memset(&list, 0, sizeof(list));
    qm_memset(&list_req, 0, sizeof(list_req));
    list_req.did = QM_RES_MGR_EXAMPLE_DID;
    list_req.resource_bundle_name = QM_RES_MGR_EXAMPLE_BUNDLE;
    list_req.page_num = 1;
    list_req.page_size = 5;
    list_req.on_page = qm_res_mgr_example_page_cb;
    list_req.page_user = NULL;
#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
    list_req.token = QM_RES_MGR_EXAMPLE_TOKEN;
#else
    list_req.iot_handle = iot_handle;
    list_req.timeout_ms = CONFIG_QM_RES_MGR_EXAMPLE_MQTT_TIMEOUT_MS;
#endif

    ret = qm_res_mgr_get_list(&list_req, &list);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG,
                "get_list failed channel=%s ret=%d status=%d biz=%d",
#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
                "http",
#else
                "mqtt",
#endif
                ret,
                list.status_code,
                list.biz_code);
        return ret;
    }

    QM_LOGI(LOG_TAG, "list total=%u page=%u page_size=%u pages=%u count=%u channel=%s",
            (unsigned int)list.total,
            (unsigned int)list.page_num,
            (unsigned int)list.page_size,
            (unsigned int)list.pages,
            (unsigned int)list.count,
#if CONFIG_QM_RES_MGR_HTTP_SUPPORT
            "http");
#else
            "mqtt");
#endif
    if (list.resource_bundle_title != NULL) {
        QM_LOGI(LOG_TAG, "resource bundle title=%s", list.resource_bundle_title);
    }
    if (list.count == 0 || !list.items) {
        QM_LOGW(LOG_TAG, "empty resource list");
        qm_res_mgr_list_free(&list);
        return QM_EOK;
    }

    for (uint32_t i = 0; i < list.count; i++) {
        char tag[16];
        qm_bool selected;

        ret = qm_snprintf(tag, sizeof(tag), "list[%u]", (unsigned int)i);
        if (ret < 0 || ret >= (int)sizeof(tag)) {
            QM_LOGW(LOG_TAG, "list item tag truncated index=%u", (unsigned int)i);
            (void)qm_snprintf(tag, sizeof(tag), "list");
        }
        qm_res_mgr_example_log_resource(tag, &list.items[i]);

        selected = (QM_RES_MGR_EXAMPLE_RESOURCE_ID == 0 ||
                    list.items[i].id == QM_RES_MGR_EXAMPLE_RESOURCE_ID) ? QM_TRUE : QM_FALSE;
        if (!selected) continue;

        matched_count++;
        QM_LOGI(LOG_TAG, "download selected index=%u resource_id=%u matched=%u",
                (unsigned int)i,
                (unsigned int)list.items[i].id,
                (unsigned int)matched_count);

        if (!list.items[i].url || !list.items[i].url[0]) {
            QM_LOGW(LOG_TAG, "skip resource_id=%u reason=no_url",
                    (unsigned int)list.items[i].id);
            continue;
        }

        ret = qm_res_mgr_example_download_resource(&list.items[i]);
        if (ret == QM_EOK) {
            download_ok_count++;
        } else {
            QM_LOGE(LOG_TAG, "download resource failed index=%u resource_id=%u ret=%d",
                    (unsigned int)i,
                    (unsigned int)list.items[i].id,
                    ret);
        }
    }

    if (matched_count == 0) {
        QM_LOGW(LOG_TAG, "resource_id=%u not found in list",
                (unsigned int)QM_RES_MGR_EXAMPLE_RESOURCE_ID);
        qm_res_mgr_list_free(&list);
        return -QM_ENOENT;
    }

    QM_LOGI(LOG_TAG, "download summary matched=%u success=%u failed=%u",
            (unsigned int)matched_count,
            (unsigned int)download_ok_count,
            (unsigned int)(matched_count - download_ok_count));
    qm_res_mgr_list_free(&list);
    return QM_EOK;
}

void qm_res_mgr_example_start(void)
{
#if CONFIG_QM_RES_MGR_MQTT_SUPPORT
    (void)qm_res_mgr_example_start_inner(s_iot_handle);
#else
    (void)qm_res_mgr_example_start_inner(NULL);
#endif
}

static void refresh_sn(void)
{
    qm_bd_addr_t mac = {0};
    int ret;

    ret = qm_ble_gap_get_local_addr(mac);
    if (ret != QM_EOK) {
        s_device_sn[0] = '\0';
        QM_LOGI(LOG_TAG, "get ble mac ret=%d", ret);
        return;
    }

    qm_snprintf(s_device_sn,
                sizeof(s_device_sn),
                "%02X%02X%02X%02X%02X%02X",
                mac[0],
                mac[1],
                mac[2],
                mac[3],
                mac[4],
                mac[5]);
}

static int32_t fill_devinfo(qm_iot_devinfo_t *info)
{
    uint8_t mac[6] = {0};

    if (info == NULL) {
        return -QM_EINVAL;
    }

    qm_memset(info, 0, sizeof(*info));
    info->type = QM_IOT_NETWORK_TYPE_WIFI;

    qm_wifi_get_mac(QM_WIFI_IF_STA, mac);
    qm_snprintf(info->data.wifi.mac,
                sizeof(info->data.wifi.mac),
                "%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0],
                mac[1],
                mac[2],
                mac[3],
                mac[4],
                mac[5]);

    if (s_wifi_ap_info_valid) {
        qm_snprintf(info->data.wifi.bssid,
                    sizeof(info->data.wifi.bssid),
                    "%02X:%02X:%02X:%02X:%02X:%02X",
                    s_wifi_ap_info.bssid[0],
                    s_wifi_ap_info.bssid[1],
                    s_wifi_ap_info.bssid[2],
                    s_wifi_ap_info.bssid[3],
                    s_wifi_ap_info.bssid[4],
                    s_wifi_ap_info.bssid[5]);
        qm_snprintf(info->data.wifi.ssid,
                    sizeof(info->data.wifi.ssid),
                    "%s",
                    (char *)s_wifi_ap_info.ssid);
        info->data.wifi.rssi = s_wifi_ap_info.rssi;
        info->data.wifi.channel = s_wifi_ap_info.channel;
    }

    if (s_wifi_ip[0] != '\0') {
        qm_snprintf(info->data.wifi.ip,
                    sizeof(info->data.wifi.ip),
                    "%s",
                    s_wifi_ip);
    }

    return QM_EOK;
}

static int32_t update_devinfo(void)
{
    qm_iot_devinfo_t devinfo = {0};
    int32_t ret;

    if (s_iot_handle == NULL) {
        return -QM_EINVAL;
    }

    ret = fill_devinfo(&devinfo);
    if (ret != QM_EOK) {
        return ret;
    }

    return qm_iot_setopt(s_iot_handle, QM_IOT_OPT_DEVINFO, &devinfo);
}

static qm_bool dynreg_auth_ready(void)
{
    return (CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_SECRET[0] != '\0') ? QM_TRUE : QM_FALSE;
}

static qm_bool product_ready(void)
{
    return (CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_ID != 0U &&
            s_device_sn[0] != '\0' &&
            dynreg_auth_ready());
}

static int32_t set_dynreg_auth(void *dynreg_handle)
{
    if (CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_SECRET[0] != '\0') {
        return qm_iot_dynreg_setopt(dynreg_handle,
                                    QM_IOT_DYNREGOPT_PRODUCT_SECRET,
                                    (void *)CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_SECRET);
    }

    return -QM_EINVAL;
}

static int32_t dynreg_setopt(void *dynreg_handle,
                             qm_iot_dynreg_option_t option,
                             void *value,
                             const char *name)
{
    int32_t ret;

    ret = qm_iot_dynreg_setopt(dynreg_handle, option, value);
    QM_LOGI(LOG_TAG, "%s ret=%d", name, (int)ret);
    return ret;
}

static void dynreg_recv(void *handle,
                        const qm_iot_dynreg_recv_t *packet,
                        void *userdata)
{
    uint32_t len;

    (void)handle;
    (void)userdata;
    qm_memset(&s_cloud_credential, 0, sizeof(s_cloud_credential));
    qm_memset(s_server_cert, 0, sizeof(s_server_cert));
    qm_memset(s_client_crt, 0, sizeof(s_client_crt));
    qm_memset(s_client_key, 0, sizeof(s_client_key));
    qm_memset(s_ota_public_key, 0, sizeof(s_ota_public_key));
    if (packet == NULL) {
        return;
    }

    if (packet->type == QM_IOT_DYNREGRECV_STATUS_CODE) {
        QM_LOGE(LOG_TAG, "dynreg status code=%u",
                (unsigned int)packet->data.status_code.code);
        return;
    }

    s_cloud_credential.did = packet->data.device_info.did;
    qm_snprintf(s_cloud_credential.cloud_info.host,
                sizeof(s_cloud_credential.cloud_info.host),
                "%.*s",
                (int)packet->data.device_info.mqtt_host_len,
                packet->data.device_info.mqtt_host);
    s_cloud_credential.cloud_info.port = packet->data.device_info.mqtt_port;
    if (s_cloud_credential.cloud_info.port == 0) {
        s_cloud_credential.cloud_info.port = 8883;
    }

    len = packet->data.device_info.server_cert_len;
    if (len >= sizeof(s_server_cert)) {
        return;
    }
    qm_memcpy(s_server_cert, packet->data.device_info.server_cert, len);

    len = packet->data.device_info.client_cert_len;
    if (len >= sizeof(s_client_crt)) {
        return;
    }
    qm_memcpy(s_client_crt, packet->data.device_info.client_crt, len);

    len = packet->data.device_info.client_privkey_len;
    if (len >= sizeof(s_client_key)) {
        return;
    }
    qm_memcpy(s_client_key, packet->data.device_info.client_private_key, len);

    len = packet->data.device_info.public_key_len;
    if (len >= sizeof(s_ota_public_key)) {
        return;
    }
    qm_memcpy(s_ota_public_key, packet->data.device_info.public_key, len);

    s_cloud_credential.server_cert = s_server_cert;
    s_cloud_credential.server_cert_len = packet->data.device_info.server_cert_len;
    s_cloud_credential.client_crt = s_client_crt;
    s_cloud_credential.client_crt_len = packet->data.device_info.client_cert_len;
    s_cloud_credential.client_key = s_client_key;
    s_cloud_credential.client_key_len = packet->data.device_info.client_privkey_len;
    s_cloud_credential.ota_public_key = s_ota_public_key;
    s_cloud_credential.ota_public_key_len = packet->data.device_info.public_key_len;
}

static int32_t run_dynreg(void)
{
    void *dynreg_handle;
    uint32_t product_id = CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_ID;
    uint16_t port = CONFIG_QM_IOT_DYNREG_PORT;
    uint32_t retry_num = CONFIG_QM_IOT_DYNREG_RETRY_COUNT;
    uint32_t timeout = CONFIG_QM_IOT_DYNREG_RECV_TIMEOUT_MS;
    int32_t ret;

    qm_memset(&s_cloud_credential, 0, sizeof(s_cloud_credential));
    dynreg_handle = qm_iot_dynreg_init();
    if (dynreg_handle == NULL) {
        QM_LOGE(LOG_TAG, "qm_iot_dynreg_init failed");
        return -QM_EINVAL;
    }

    if (s_dynreg_host[0] != '\0') {
        ret = dynreg_setopt(dynreg_handle,
                            QM_IOT_DYNREGOPT_HOST,
                            s_dynreg_host,
                            "dynreg set host");
        if (ret != QM_EOK) {
            goto exit;
        }
    } else if (CONFIG_QM_RES_MGR_EXAMPLE_DYNREG_HOST[0] != '\0') {
        ret = dynreg_setopt(dynreg_handle,
                            QM_IOT_DYNREGOPT_HOST,
                            (void *)CONFIG_QM_RES_MGR_EXAMPLE_DYNREG_HOST,
                            "dynreg set host");
        if (ret != QM_EOK) {
            goto exit;
        }
    }

    ret = dynreg_setopt(dynreg_handle,
                        QM_IOT_DYNREGOPT_PORT,
                        &port,
                        "dynreg set port");
    if (ret != QM_EOK) {
        goto exit;
    }

    ret = dynreg_setopt(dynreg_handle,
                        QM_IOT_DYNREGOPT_PRODUCT_ID,
                        &product_id,
                        "dynreg set product id");
    if (ret != QM_EOK) {
        goto exit;
    }

    ret = set_dynreg_auth(dynreg_handle);
    QM_LOGI(LOG_TAG, "dynreg set auth ret=%d", (int)ret);
    if (ret != QM_EOK) {
        goto exit;
    }

    ret = dynreg_setopt(dynreg_handle,
                        QM_IOT_DYNREGOPT_SN,
                        (void *)s_device_sn,
                        "dynreg set sn");
    if (ret != QM_EOK) {
        goto exit;
    }

    ret = dynreg_setopt(dynreg_handle,
                        QM_IOT_DYNREGOPT_MAX_RETRY_NUM,
                        &retry_num,
                        "dynreg set max retry");
    if (ret != QM_EOK) {
        goto exit;
    }

    ret = dynreg_setopt(dynreg_handle,
                        QM_IOT_DYNREGOPT_RECV_TIMEOUT_MS,
                        &timeout,
                        "dynreg set recv timeout");
    if (ret != QM_EOK) {
        goto exit;
    }

    ret = dynreg_setopt(dynreg_handle,
                        QM_IOT_DYNREGOPT_RECV_HANDLER,
                        dynreg_recv,
                        "dynreg set recv handler");
    if (ret != QM_EOK) {
        goto exit;
    }

    ret = qm_iot_dynreg_start(dynreg_handle, QM_IOT_NETWORK_TYPE_WIFI);
    QM_LOGI(LOG_TAG, "dynreg start ret=%d", (int)ret);
    if (ret != QM_EOK) {
        goto exit;
    }

    if (s_cloud_credential.did == 0) {
        ret = -QM_EINVAL;
        QM_LOGE(LOG_TAG, "dynreg invalid did ret=%d", (int)ret);
        goto exit;
    }

exit:
    (void)qm_iot_dynreg_deinit(&dynreg_handle);
    return ret;
}

static int32_t start_iot(void)
{
    uint32_t product_id = CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_ID;
    int32_t ret;

    if (s_iot_started) {
        return QM_EOK;
    }

    refresh_sn();
    if (!product_ready()) {
        QM_LOGE(LOG_TAG, "set product_id/product_secret and ensure device sn is available");
        return -QM_EINVAL;
    }

    ret = run_dynreg();
    if (ret != QM_EOK) {
        return ret;
    }

    if (s_iot_handle == NULL) {
        s_iot_handle = qm_iot_init();
    }
    if (s_iot_handle == NULL) {
        QM_LOGE(LOG_TAG, "qm_iot_init failed");
        return -QM_EINVAL;
    }

    ret = qm_iot_setopt(s_iot_handle, QM_IOT_OPT_PRODUCT_ID, &product_id);
    QM_LOGI(LOG_TAG, "set product id ret=%d", (int)ret);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = qm_iot_setopt(s_iot_handle,
                        QM_IOT_OPT_PRODUCT_SECRET,
                        (void *)CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_SECRET);
    QM_LOGI(LOG_TAG, "set product secret ret=%d", (int)ret);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = qm_iot_setopt(s_iot_handle, QM_IOT_OPT_SN, (void *)s_device_sn);
    QM_LOGI(LOG_TAG, "set sn ret=%d", (int)ret);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = qm_iot_setopt(s_iot_handle,
                        QM_IOT_OPT_VERSION_STR,
                        (void *)CONFIG_QM_RES_MGR_EXAMPLE_VERSION);
    QM_LOGI(LOG_TAG, "set version ret=%d", (int)ret);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = qm_iot_setopt(s_iot_handle, QM_IOT_OPT_CLOUD_CREDENTIAL, &s_cloud_credential);
    QM_LOGI(LOG_TAG, "set cloud credential ret=%d", (int)ret);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = qm_iot_start(s_iot_handle);
    if (ret != QM_EOK) {
        return ret;
    }

    s_iot_started = QM_TRUE;
    return QM_EOK;
}

static void stop_provision(void)
{
    int32_t ret;

    if (s_provision_handle == NULL) {
        return;
    }

    if (!s_provision_running) {
        return;
    }

    ret = qm_provision_stop(s_provision_handle);
    if (ret == QM_EOK) {
        s_provision_running = QM_FALSE;
    } else {
        QM_LOGE(LOG_TAG, "provision stop failed ret=%d", (int)ret);
    }
}

static int32_t clear_provision_state(void)
{
    int32_t ret;

    stop_provision();

    s_wifi_reconfiguring = QM_TRUE;
    (void)qm_wifi_disconnect();
    ret = qm_wifi_stop();
    if (ret == QM_EOK) {
        s_wifi_started = QM_FALSE;
    }

    ret = qm_wifi_clear_config();
    if (ret != QM_EOK) {
        s_wifi_reconfiguring = QM_FALSE;
        return ret;
    }

    s_wifi_retry_count = 0;
    s_wifi_ap_info_valid = QM_FALSE;
    s_wifi_ip[0] = '\0';
    s_res_mgr_demo_started = QM_FALSE;
    s_res_mgr_started_by_cloud_conn = QM_FALSE;
    qm_memset(&s_wifi_ap_info, 0, sizeof(s_wifi_ap_info));
    s_wifi_reconfiguring = QM_FALSE;

    return QM_EOK;
}

static void provision_reset_cmd(char *pcWriteBuffer,
                                int xWriteBufferLen,
                                int argc,
                                char **argv)
{
    int32_t ret;

    (void)argc;
    (void)argv;

    ret = clear_provision_state();
    if (ret == QM_EOK) {
        qm_snprintf(pcWriteBuffer,
                    xWriteBufferLen,
                    "provision reset ok, rebooting\r\n");
        qm_reboot();
        return;
    }

    qm_snprintf(pcWriteBuffer,
                xWriteBufferLen,
                "provision reset failed ret=%d\r\n",
                (int)ret);
}

static const struct qm_cli_command s_cli_cmd = {
    .name = "provision_reset",
    .help = "clear saved wifi config and reboot into BLE provisioning",
    .function = provision_reset_cmd,
};

static int32_t apply_wifi_config(const char *ssid,
                                 const char *password)
{
    qm_wifi_config_t config = {0};
    uint32_t ssid_len;
    uint32_t password_len;

    if (ssid == NULL || ssid[0] == '\0') {
        return -QM_EINVAL;
    }

    ssid_len = qm_strlen(ssid);
    password_len = password ? qm_strlen(password) : 0;
    if (ssid_len > CONFIG_QM_WIFI_SSID_MAX_LEN ||
        password_len > CONFIG_QM_WIFI_PASSWD_MAX_LEN) {
        QM_LOGE(LOG_TAG,
                "wifi credential too long ssid_len=%u password_len=%u",
                (unsigned int)ssid_len,
                (unsigned int)password_len);
        return -QM_EINVAL;
    }

    qm_memcpy(config.sta.ssid, ssid, ssid_len);
    config.sta.ssid_len = (uint8_t)ssid_len;
    if (password_len > 0) {
        qm_memcpy(config.sta.password, password, password_len);
        config.sta.password_len = (uint8_t)password_len;
    }

    return qm_wifi_set_config(QM_WIFI_IF_STA, &config);
}

static int32_t start_wifi_with_config(const char *ssid,
                                      const char *password)
{
    int32_t ret;

    ret = apply_wifi_config(ssid, password);
    if (ret != QM_EOK) {
        return ret;
    }

    if (!s_wifi_started) {
        ret = qm_wifi_start();
        if (ret != QM_EOK) {
            return ret;
        }
        s_wifi_started = QM_TRUE;
        return QM_EOK;
    }

    s_wifi_reconfiguring = QM_TRUE;
    (void)qm_wifi_disconnect();
    ret = qm_wifi_stop();
    if (ret != QM_EOK) {
        s_wifi_reconfiguring = QM_FALSE;
        return ret;
    }

    ret = qm_wifi_start();
    if (ret != QM_EOK) {
        s_wifi_started = QM_FALSE;
        s_wifi_reconfiguring = QM_FALSE;
        return ret;
    }

    s_wifi_started = QM_TRUE;
    return QM_EOK;
}

static qm_bool has_saved_wifi_config(void)
{
    qm_wifi_config_t config = {0};

    if (qm_wifi_get_config(QM_WIFI_IF_STA, &config) != QM_EOK) {
        return QM_FALSE;
    }

    if (config.sta.ssid_len == 0 || config.sta.ssid[0] == '\0') {
        return QM_FALSE;
    }

    return QM_TRUE;
}

static int32_t start_saved_wifi(void)
{
    int32_t ret;

    if (!has_saved_wifi_config()) {
        return -QM_ENOENT;
    }

    if (!s_wifi_started) {
        ret = qm_wifi_start();
        if (ret != QM_EOK) {
            return ret;
        }
        s_wifi_started = QM_TRUE;
        return QM_EOK;
    }

    s_wifi_reconfiguring = QM_TRUE;
    (void)qm_wifi_disconnect();
    ret = qm_wifi_stop();
    if (ret != QM_EOK) {
        s_wifi_reconfiguring = QM_FALSE;
        return ret;
    }

    ret = qm_wifi_start();
    if (ret != QM_EOK) {
        s_wifi_started = QM_FALSE;
        s_wifi_reconfiguring = QM_FALSE;
        return ret;
    }

    s_wifi_started = QM_TRUE;
    return QM_EOK;
}

static void provision_recv(qm_provision_handle_t handle,
                           const qm_provision_recv_t *recv,
                           void *userdata)
{
    int32_t ret;

    (void)handle;
    (void)userdata;

    if (recv == NULL || recv->ssid[0] == '\0') {
        return;
    }

    QM_LOGI(LOG_TAG,
            "provision recv ssid=%s password_len=%u dynreg_host=%s",
            recv->ssid,
            (unsigned int)qm_strlen(recv->password),
            recv->dynreg_host);

    if (recv->dynreg_host[0] != '\0') {
        qm_snprintf(s_dynreg_host,
                    sizeof(s_dynreg_host),
                    "%s",
                    recv->dynreg_host);
    }

    s_wifi_retry_count = 0;
    ret = start_wifi_with_config(recv->ssid, recv->password);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "start wifi with provision config failed ret=%d", (int)ret);
    }
}

static int32_t start_provision(void)
{
    int32_t ret;
    qm_provision_config_t config = {
        .product_id = CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_ID,
        .product_secret = CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_SECRET,
        .adv_cid = CONFIG_QM_RES_MGR_EXAMPLE_ADV_CID[0] != '\0' ?
            CONFIG_QM_RES_MGR_EXAMPLE_ADV_CID : NULL,
        .recv_handler = provision_recv,
        .userdata = NULL,
    };

    if (s_provision_handle != NULL) {
        ret = qm_provision_start(s_provision_handle);
        if (ret == QM_EOK) {
            s_provision_running = QM_TRUE;
        }
        return ret;
    }

    if (CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_ID == 0U ||
        CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_SECRET[0] == '\0') {
        QM_LOGE(LOG_TAG, "set product_id/product_secret before provision");
        return -QM_EINVAL;
    }

    s_provision_handle = qm_provision_create(&config);
    if (s_provision_handle == NULL) {
        return -QM_EINVAL;
    }

    ret = qm_provision_start(s_provision_handle);
    if (ret == QM_EOK) {
        s_provision_running = QM_TRUE;
    }

    return ret;
}

static void start_res_mgr_once_if_ready(void)
{
#if CONFIG_QM_RES_MGR_MQTT_SUPPORT
    int ret;

    if (s_res_mgr_started_by_cloud_conn) {
        return;
    }
    if (s_iot_handle == NULL) {
        return;
    }

    ret = qm_res_mgr_example_start_inner(s_iot_handle);
    if (ret == QM_EOK) {
        s_res_mgr_started_by_cloud_conn = QM_TRUE;
        s_res_mgr_demo_started = QM_TRUE;
    } else {
        QM_LOGE(LOG_TAG, "mqtt res mgr start failed ret=%d", ret);
    }
#else
    return;
#endif
}

static void iot_event_handler(void *handle,
                              const qm_iot_event_t *event,
                              void *userdata)
{
    (void)handle;
    (void)userdata;

    if (event == NULL) {
        return;
    }

    QM_LOGI(LOG_TAG, "iot event=%d", event->type);

    switch (event->type) {
        case QM_IOT_EVENT_CLOUD_CONN:
            start_res_mgr_once_if_ready();
            break;

        case QM_IOT_EVENT_CLOUD_DISCONN:
            s_res_mgr_started_by_cloud_conn = QM_FALSE;
            break;

        case QM_IOT_EVENT_RESET:
            QM_LOGW(LOG_TAG, "cloud reset event");
            if (clear_provision_state() == QM_EOK) {
                qm_reboot();
            }
            break;

        default:
            break;
    }
}

static void qm_res_mgr_example_wifi_event_handler(void *event_handler_arg,
                                                  qm_event_base_t event_base,
                                                  int32_t event_id,
                                                  void *event_data,
                                                  size_t event_data_size)
{
    qm_wifi_event_info_t *event_info = (qm_wifi_event_info_t *)event_data;
    int32_t ret;

    QM_UNUSED_ARG(event_handler_arg);
    QM_UNUSED_ARG(event_base);
    QM_UNUSED_ARG(event_data_size);

    switch ((qm_wifi_event_t)event_id) {
        case QM_WIFI_EVENT_STA_START:
            QM_LOGI(LOG_TAG, "wifi sta start");
            s_wifi_started = QM_TRUE;
            s_wifi_reconfiguring = QM_FALSE;
            if (qm_wifi_connect() != QM_EOK) {
                QM_LOGE(LOG_TAG, "wifi connect failed");
            }
            break;

        case QM_WIFI_EVENT_STA_STOP:
            QM_LOGI(LOG_TAG, "wifi sta stop");
            if (!s_wifi_reconfiguring) {
                s_wifi_started = QM_FALSE;
            }
            break;

        case QM_WIFI_EVENT_STA_CONNECTED:
            QM_LOGI(LOG_TAG, "wifi sta connected");
            s_wifi_retry_count = 0;
            if (qm_wifi_sta_get_ap_info(&s_wifi_ap_info) == QM_EOK) {
                s_wifi_ap_info_valid = QM_TRUE;
            } else {
                s_wifi_ap_info_valid = QM_FALSE;
                qm_memset(&s_wifi_ap_info, 0, sizeof(s_wifi_ap_info));
            }
            break;

        case QM_WIFI_EVENT_STA_GOT_IP:
            QM_LOGI(LOG_TAG, "wifi got ip");
            if (event_info != NULL) {
                qm_snprintf(s_wifi_ip,
                            sizeof(s_wifi_ip),
                            QM_IPSTR,
                            QM_IP2STR(&event_info->got_ip.ip_info.ip));
                QM_LOGI(LOG_TAG,
                        "ip:" QM_IPSTR,
                        QM_IP2STR(&event_info->got_ip.ip_info.ip));
            }
            if (qm_wifi_sta_get_ap_info(&s_wifi_ap_info) == QM_EOK) {
                s_wifi_ap_info_valid = QM_TRUE;
            }

#if CONFIG_QM_RES_MGR_MQTT_SUPPORT
            ret = start_iot();
            if (ret == QM_EOK && s_iot_handle != NULL) {
                stop_provision();
                ret = update_devinfo();
                if (ret != QM_EOK) {
                    QM_LOGE(LOG_TAG, "update devinfo failed ret=%d", (int)ret);
                }
                ret = qm_iot_network_update(s_iot_handle, QM_IOT_NETWORK_UP);
                if (ret != QM_EOK) {
                    QM_LOGE(LOG_TAG, "network up failed ret=%d", (int)ret);
                }
            }
#else
            if (!s_res_mgr_demo_started) {
                s_res_mgr_demo_started = QM_TRUE;
                (void)qm_res_mgr_example_start_inner(NULL);
            }
#endif
            break;

        case QM_WIFI_EVENT_STA_DISCONNECTED:
            QM_LOGW(LOG_TAG, "wifi disconnected");
            s_wifi_ip[0] = '\0';
            s_wifi_ap_info_valid = QM_FALSE;
            s_res_mgr_demo_started = QM_FALSE;
            s_res_mgr_started_by_cloud_conn = QM_FALSE;
            qm_memset(&s_wifi_ap_info, 0, sizeof(s_wifi_ap_info));
            if (s_wifi_reconfiguring) {
                break;
            }

#if CONFIG_QM_RES_MGR_MQTT_SUPPORT
            if (s_iot_handle != NULL) {
                (void)qm_iot_network_update(s_iot_handle, QM_IOT_NETWORK_DOWN);
            }

            if (s_wifi_retry_count < (int)CONFIG_QM_RES_MGR_EXAMPLE_WIFI_RETRY_MAX) {
                s_wifi_retry_count++;
                if (qm_wifi_connect() != QM_EOK) {
                    QM_LOGE(LOG_TAG, "wifi reconnect failed");
                    break;
                }
                QM_LOGI(LOG_TAG,
                        "retry wifi connect %d/%d",
                        s_wifi_retry_count,
                        (int)CONFIG_QM_RES_MGR_EXAMPLE_WIFI_RETRY_MAX);
            }
#else
            if (s_wifi_retry_count < EXAMPLE_WIFI_RETRY_MAX_COUNT) {
                s_wifi_retry_count++;
                if (qm_wifi_connect() != QM_EOK) {
                    QM_LOGE(LOG_TAG, "wifi reconnect failed");
                    break;
                }
                QM_LOGI(LOG_TAG,
                        "retry wifi connect %d/%d",
                        s_wifi_retry_count,
                        EXAMPLE_WIFI_RETRY_MAX_COUNT);
            }
#endif
            break;

        default:
            break;
    }
}

static void qm_res_mgr_http_demo_start(void)
{
    int ret = QM_EOK;
    qm_wifi_config_t config = {
        .sta = {
            .ssid = EXAMPLE_WIFI_SSID,
            .ssid_len = qm_strlen(EXAMPLE_WIFI_SSID),
            .password = EXAMPLE_WIFI_PASSWORD,
            .password_len = qm_strlen(EXAMPLE_WIFI_PASSWORD),
        },
    };

    QM_LOGI(LOG_TAG, "resource manager http wifi demo start");

    ret = qm_event_handler_register(QM_EVENT_WIFI, QM_EVENT_ANY_ID,
                                    qm_res_mgr_example_wifi_event_handler, NULL);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "register wifi handler failed: %d", ret);
        return;
    }

    ret = qm_wifi_init();
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "wifi init failed: %d", ret);
        return;
    }

    ret = qm_wifi_set_mode(QM_WIFI_MODE_STA);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "wifi set mode failed: %d", ret);
        return;
    }

    ret = qm_wifi_set_config(QM_WIFI_IF_STA, &config);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "wifi set config failed: %d", ret);
        return;
    }

    ret = qm_wifi_start();
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "wifi start failed: %d", ret);
    }
}

static void qm_res_mgr_mqtt_demo_start(void)
{
    qm_err_t ret;
    int32_t call_ret;

    if (CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_ID == 0U ||
        CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_SECRET[0] == '\0') {
        QM_LOGE(LOG_TAG, "set CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_ID/PRODUCT_SECRET before starting mqtt demo");
        return;
    }

    ret = qm_cli_register_command(&s_cli_cmd);
    QM_LOGI(LOG_TAG, "cli register ret=%d", (int)ret);

    qm_iot_dynreg_env_set(CONFIG_QM_RES_MGR_EXAMPLE_DYNREG_ENV);

    if (!s_wifi_event_registered) {
        ret = qm_event_handler_register(QM_EVENT_WIFI, QM_EVENT_ANY_ID,
                                        qm_res_mgr_example_wifi_event_handler, NULL);
        if (ret != QM_EOK) {
            QM_LOGE(LOG_TAG, "wifi event register failed: %d", ret);
            return;
        }
        s_wifi_event_registered = QM_TRUE;
    }

    call_ret = qm_wifi_init();
    QM_LOGI(LOG_TAG, "wifi init ret=%d", (int)call_ret);
    if (call_ret != QM_EOK) {
        return;
    }

    call_ret = qm_wifi_set_mode(QM_WIFI_MODE_STA);
    QM_LOGI(LOG_TAG, "wifi set mode ret=%d", (int)call_ret);
    if (call_ret != QM_EOK) {
        return;
    }

    refresh_sn();

    s_iot_handle = qm_iot_init();
    if (s_iot_handle == NULL) {
        QM_LOGE(LOG_TAG, "qm_iot_init failed");
        return;
    }

    call_ret = qm_iot_setopt(s_iot_handle, QM_IOT_OPT_EVENT_HANDLER, iot_event_handler);
    QM_LOGI(LOG_TAG, "set event handler ret=%d", (int)call_ret);
    if (call_ret != QM_EOK) {
        return;
    }

    call_ret = start_saved_wifi();
    QM_LOGI(LOG_TAG, "start saved wifi ret=%d", (int)call_ret);
    if (call_ret == QM_EOK) {
        return;
    }

    call_ret = start_provision();
    QM_LOGI(LOG_TAG, "start provision ret=%d", (int)call_ret);
}

void app_main(void)
{
#if CONFIG_QM_RES_MGR_MQTT_SUPPORT
    QM_LOGI(LOG_TAG, "resource manager mqtt demo start");
    qm_res_mgr_mqtt_demo_start();
#else
    qm_res_mgr_http_demo_start();
#endif
}
