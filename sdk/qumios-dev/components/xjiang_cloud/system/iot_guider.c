
#include "iot_guider.h"
#include "qm_iot_import.h"
#include "iot_cloud_debug.h"
#include "iot_device.h"
#include "iot_ca.h"

#define KV_XJ_URL_KEY "xj_url"
#define DEFAULT_REGION_ID    (0)

const char *iot_secmode_str[] = {
    "TCP + Guider + Plain",
    "TCP + Guider + ID2-Crypto",
    "TLS + Direct",
    "TCP + Direct + Plain",
    "TCP + Direct + ID2-Crypto",
    "TLS + Guider + ID2-Authenticate",
    "",
    "TLS + Direct + ID2-Crypto",
    ""
};

#define DOMAIN_XJ_HTTP_PREFIX "open"
#define DOMAIN_XJ_MQTT_PREFIX "emq"

const char *domain_xj_suffix[] = {
    "xjiangiot.com",       /* China */
    "us.xjiangiot.com",      /* US */
    "eu.xjiangiot.com",      /* EUR */
    "sea.xjiangiot.com",      /*SEA */
    "test.xjiangiot.com",      /*test */
};


static int iot_calc_hmac_signature(
            char *hmac_sigbuf,
            const int hmac_buflen,
            const char *timestamp_str)
{
    char                    signature[64];
    char                    hmac_source[128];
    int                     rc = -1;
    iot_device_info_pt     dev;

    dev = iot_device_info_get();

    memset(signature, 0, sizeof(signature));
    memset(hmac_source, 0, sizeof(hmac_source));
    rc = hal_snprintf(hmac_source,
                      sizeof(hmac_source),
                      "clientId%s" "deviceId%s" "productId%u" "timestamp%s",
                      dev->device_id,
                      dev->device_id,
                      dev->product_id,
                      timestamp_str);
    iot_log_debug("| source: %s (%d)", hmac_source, (int)strlen(hmac_source));
    iot_log_debug("| secret: %s (%d)", dev->device_secret, (int)strlen(dev->device_secret));

#if USING_SHA1_IN_HMAC
    iot_log_debug("| method: %s", SHA_METHOD);
    qm_utils_hmac_sha1(hmac_source, strlen(hmac_source),
                    signature,
                    dev->device_secret,
                    strlen(dev->device_secret));
#else
    iot_log_debug("| method: %s", MD5_METHOD);
    qm_utils_hmac_md5(hmac_source, strlen(hmac_source),
                   signature,
                   dev->device_secret,
                   strlen(dev->device_secret));
#endif

    iot_log_debug("| signature: %s (%d)", signature, (int)strlen(signature));

    memcpy(hmac_sigbuf, signature, hmac_buflen);
    return 0;
}

void iot_guider_print_conn_info(iot_conn_info_pt conn)
{
    iot_log_debug("%s", "-----------------------------------------");
    iot_log_debug("%16s : %-s", "Host", conn->host_name);
    iot_log_debug("%16s : %d",  "Port", conn->port);
    iot_log_debug("%16s : %-s", "UserName", conn->username);
    iot_log_debug("%16s : %-s", "PassWord", conn->password);
    iot_log_debug("%16s : %-s", "ClientID", conn->client_id);
    if (conn->pub_key) {
        iot_log_debug("%16s : %p ('%.16s ...')", "TLS PubKey", conn->pub_key, conn->pub_key);
    }

    iot_log_debug("%s", "-----------------------------------------");
}

void iot_guider_print_dev_guider_info(iot_device_info_pt dev,
                                  int secure_mode,
                                  char *time_stamp,
                                  char *guider_sign,
                                  char *id2,
                                  char *dev_code)
{
    iot_log_debug("%s", "....................................................");
    iot_log_debug("%20s : %-u", "ProductID", dev->product_id);
    iot_log_debug("%20s : %-s", "DeviceID", dev->device_id);
    iot_log_debug("%20s : %-s", "DeviceSecret", dev->device_secret);
    iot_log_debug("%s", "....................................................");
    if (secure_mode > 0) {
        iot_log_debug("%20s : %d (%s)", "Guider SecMode", secure_mode, iot_secmode_str[secure_mode]);
    }
    iot_log_debug("%20s : %s", "Guider Timestamp", time_stamp);
    iot_log_debug("%s", "....................................................");
    iot_log_debug("%20s : %s", "Guider Sign", guider_sign);
    if (id2 != NULL) {
        iot_log_debug("%20s : %s", "Guider ID2", id2);
        iot_log_debug("%20s : %s", "Guider DeviceCode", dev_code);
    }
    iot_log_debug("%s", "....................................................");

    return;
}

static void iot_guider_get_timestamp_str(char *buf, int len)
{
    hal_snprintf(buf, len, "%s", GUIDER_DEFAULT_TS_STR);

    return;
}

static secure_mode_t iot_guider_get_secure_mode(void)
{
    secure_mode_t     rc = MODE_TLS_GUIDER;

#ifdef MQTT_DIRECT

#ifdef IOT_WITHOUT_TLS
    rc = MODE_TCP_DIRECT_PLAIN;
#else
    rc = MODE_TLS_DIRECT;
#endif  /* IOT_WITHOUT_TLS */

#else   /* MQTT_DIRECT */

#ifdef IOT_WITHOUT_TLS
    rc = MODE_TCP_GUIDER_PLAIN;
#else
    rc = MODE_TLS_GUIDER;
#endif  /* IOT_WITHOUT_TLS */

#endif  /* MQTT_DIRECT */

    return  rc;
}

int iot_guider_authenticate(void)
{

    secure_mode_t         secure_mode = MODE_TLS_GUIDER;
    char                guider_sign[GUIDER_SIGN_LEN] = {0};
    char                timestamp_str[GUIDER_TS_LEN] = {0};

    iot_device_info_pt dev = iot_device_info_get();
    iot_conn_info_pt   conn = iot_conn_info_get();
    char               *req_str = NULL;
    int                 gw = 0;
    char domain[XJ_GUIDER_DYNAMIC_URL_LEN] = {0};
    int ret = 0;

    secure_mode = iot_guider_get_secure_mode();
    iot_guider_get_timestamp_str(timestamp_str, sizeof(timestamp_str));
    iot_calc_hmac_signature(guider_sign, sizeof(guider_sign), timestamp_str);

    iot_guider_print_dev_guider_info(dev, secure_mode,
                                 timestamp_str, guider_sign, NULL, NULL);

    /* Start Filling Connection Information */
    conn->pub_key = iot_ca_get();

    conn->port = GUIDER_DIRECT_PORT;
    iot_guider_get_domain(GUIDER_XJ_DOMAIN_MQTT, domain, XJ_GUIDER_DYNAMIC_URL_LEN);
    
    hal_snprintf(conn->host_name, sizeof(conn->host_name),
                      "%s",
                      domain);
    hal_snprintf(conn->username, sizeof(conn->username),
                      "%s&%u",
                      dev->device_id,
                      dev->product_id);
    hal_snprintf(conn->password, sizeof(conn->password),
                      "%s",
                      guider_sign);

    hal_snprintf(conn->client_id, sizeof(conn->client_id),
                      "device-%s"
                      "|securemode=%d"
                      ",signmethod="SHA_METHOD",timestamp=%s|"
                      , dev->device_id
                      , secure_mode
                      , timestamp_str
                      );

    iot_guider_print_conn_info(conn);

    if (req_str) {
        hal_free(req_str);
    }

    return 0;

}

int iot_setup_conn_info(unsigned int product_id,
                      const char *device_id,
                      const char *device_secret,
                      void **info_ptr)
{
    int rc = -1;

    if (!info_ptr) {
        iot_log_err("Invalid argument, info_ptr = %p", info_ptr);
        return -1;
    }

    IOT_STRING_PTR_SANITY_CHECK(device_id, -1);
    IOT_STRING_PTR_SANITY_CHECK(device_secret, -1);

    iot_device_info_init();
    iot_device_info_set(product_id, device_id, device_secret);

    rc = iot_guider_authenticate();
    if (rc == 0) {
        *info_ptr = (void *)iot_conn_info_get();
        return 0;
    } else {
        *info_ptr = NULL;
        return -1;
    }
}


int iot_guider_set_dynamic_url(char *p_mqtt_url)
{
    int len = 0;
    int ret = 0;

    if (!p_mqtt_url)
        return -1;

    len = strlen(p_mqtt_url) + 1;
    if (len > XJ_GUIDER_DYNAMIC_URL_LEN){
        iot_log_err("len is err");
        return -1;
    }

    ret = hal_kv_set(KV_XJ_URL_KEY, p_mqtt_url, len, 1);
    iot_log_debug("set dynamic mqtt url:%s,ret=%d", p_mqtt_url, ret);

    return ret;
}


int iot_guider_get_dynamic_url(char *p_mqtt_url, int mqtt_url_buff_len)
{
    int len = mqtt_url_buff_len;
    return hal_kv_get(KV_XJ_URL_KEY, p_mqtt_url, &len);
}


int iot_guider_get_domain(domain_xj_type_t domain_type, char *domain, int len)
{
    int ret = 0;
    int region_id = 0;
    char url[XJ_GUIDER_DYNAMIC_URL_LEN] = {0};
    int url_len = XJ_GUIDER_DYNAMIC_URL_LEN;
    
    if(domain_type == GUIDER_XJ_DOMAIN_MQTT){
        ret = iot_guider_get_dynamic_url(url, url_len);
        if (ret == 0){
            hal_snprintf(domain, len, "%s.%s", DOMAIN_XJ_MQTT_PREFIX, url);
        }else{
            //default get domain url
            hal_snprintf(domain, len, "%s.%s", DOMAIN_XJ_MQTT_PREFIX, domain_xj_suffix[DEFAULT_REGION_ID]);
            //memcpy(domain, "192.168.1.153", strlen("192.168.1.153"));
        }    
    }else if(domain_type == GUIDER_XJ_DOMAIN_HTTP){
        ret = iot_guider_get_dynamic_url(url, len);
        if (ret == 0){
            hal_snprintf(domain, len, "%s.%s", DOMAIN_XJ_HTTP_PREFIX, url);
        }else{
            //default get domain url
            hal_snprintf(domain, len, "%s.%s", DOMAIN_XJ_HTTP_PREFIX, domain_xj_suffix[DEFAULT_REGION_ID]);
        }     
    }
    return 0;
}


static const char *iot_guider_get_url(int region_id)
{
    return domain_xj_suffix[region_id];
}

int iot_guider_set_region_id(int region_id)
{
    if (region_id < 0 || region_id > IOT_XJ_CLOUD_REGION_MAX){
        iot_log_err("invalid region id:%d", region_id);
        return -1;
    }

    return iot_guider_set_dynamic_url(iot_guider_get_url(region_id));
}



