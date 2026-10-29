#include "qm.h"
#include "qm_kv.h"
#include "qm_iot_config.h"
#include "qm_iot_session_internal.h"

#define LOG_TAG "IOT_CONFIG"

#ifndef CONFIG_QM_IOT_KV_HEADER
#define CONFIG_QM_IOT_KV_HEADER   "c"
#endif

#ifndef CONFIG_QM_IOT_KV_KEY_MAX_LEN
/** @brief 单条 KV 完整键名缓冲区长度（含精简前缀与 pid、子键）。 */
#define CONFIG_QM_IOT_KV_KEY_MAX_LEN  (80)
#endif

/** @brief KV 子键：复位信息，与 @ref CONFIG_QM_IOT_KV_HEADER 及 pid 组合为完整键。 */
#define QM_IOT_KV_SUFFIX_RESET          "reset"
#define QM_IOT_KV_SUFFIX_DID            "did"
#define QM_IOT_KV_SUFFIX_CLOUD          "cloud"
#define QM_IOT_KV_SUFFIX_WIFI           "wifi"
#define QM_IOT_KV_SUFFIX_SERVER_CA      "s_ca"
#define QM_IOT_KV_SUFFIX_CLIENT_CA      "c_ca"
#define QM_IOT_KV_SUFFIX_CLIENT_KEY     "c_key"
#define QM_IOT_KV_SUFFIX_PUBLIC_KEY     "p_key"
#define QM_IOT_KV_SUFFIX_AUDIO_URI      "audio"
#define QM_IOT_KV_SUFFIX_AGORA_URI      "agora"
#define QM_IOT_KV_SUFFIX_SN             "sn"
/**
 * @brief 由 @ref qm_iot_init 返回的 会话句柄 解析配置 RAM 上下文。
 * @param[in] iot_handle IOT 会话句柄（@ref qm_iot_session_t ），不可为 NULL。
 * @return 指向 IOT 会话内部结构的指针；iot_handle 为 NULL 时返回 NULL。
 */
static qm_iot_session_t *qm_iot_config_ctx(void *iot_handle)
{
    return (qm_iot_session_t *)iot_handle;
}

static int32_t qm_iot_config_blob_replace(char **dst, uint32_t *dst_len, const char *src, uint32_t src_len)
{
    char *buf = NULL;

    if (dst == NULL || dst_len == NULL) {
        return -QM_EINVAL;
    }

    if (src == NULL || src_len == 0) {
        return -QM_EINVAL;
    }

    buf = (char *)qm_malloc(src_len);
    if (buf == NULL) {
        return -QM_ENOMEM;
    }

    memcpy(buf, src, src_len);

    if (*dst != NULL) {
        qm_free(*dst);
    }

    *dst = buf;
    *dst_len = src_len;
    return QM_EOK;
}

/**
 * @brief 按 iot_handle 对应会话的 pid 生成 KV 键名：{CONFIG_QM_IOT_KV_HEADER}{pid}_{suffix}。
 * @param[in] iot_handle IOT 会话句柄。
 * @param[in] suffix 子键后缀（如 @ref QM_IOT_KV_SUFFIX_DID ）。
 * @param[out] key 输出缓冲区。
 * @param[in] key_len 缓冲区字节长度。
 * @return QM_EOK 成功；负值为参数错误或缓冲区不足。
 */
static int32_t qm_iot_config_kv_key(void *iot_handle, const char *suffix, char *key, size_t key_len)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);
    int n;

    if (cfg == NULL || suffix == NULL || key == NULL || key_len == 0) {
        return -QM_EINVAL;
    }

    n = qm_snprintf(key, key_len, "%s%u_%s", CONFIG_QM_IOT_KV_HEADER, (unsigned)cfg->pid, suffix);
    if (n < 0 || (size_t)n >= key_len) {
        return -QM_EINVAL;
    }

    return QM_EOK;
}

/**
 * @brief 向当前 iot_handle 命名空间写入 KV。
 */
static int32_t qm_iot_config_kv_set(void *iot_handle, const char *suffix, void *val, int len, int sync)
{
    char key[CONFIG_QM_IOT_KV_KEY_MAX_LEN];
    int32_t ret;

    ret = qm_iot_config_kv_key(iot_handle, suffix, key, sizeof(key));
    if (ret != QM_EOK) {
        return ret;
    }

    return qm_kv_set(key, val, len, sync);
}

/**
 * @brief 从当前 iot_handle 命名空间读取 KV。
 */
static int32_t qm_iot_config_kv_get(void *iot_handle, const char *suffix, void *val, int *len)
{
    char key[CONFIG_QM_IOT_KV_KEY_MAX_LEN];
    int32_t ret;

    ret = qm_iot_config_kv_key(iot_handle, suffix, key, sizeof(key));
    if (ret != QM_EOK) {
        return ret;
    }

    return qm_kv_get(key, val, len);
}

int qm_iot_sn_set(void *iot_handle, char *sn, uint32_t len)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL) {
        return -QM_EINVAL;
    }

    if(sn == NULL || len == 0) {
        return -QM_EINVAL;
    }

    if(len >= CONFIG_QM_IOT_SN_MAX_LEN) {
        return -QM_EINVAL;
    }

#if CONFIG_QM_IOT_SN_KV_SUPPORT
    qm_iot_config_kv_set(iot_handle, QM_IOT_KV_SUFFIX_SN, sn, (int)len, 1);
#endif
    memset(cfg->sn, 0, CONFIG_QM_IOT_SN_MAX_LEN + 1);
    memcpy(cfg->sn, sn, len);

    return QM_EOK;
}

char *qm_iot_sn_get(void *iot_handle)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL) {
        return NULL;
    }

#if CONFIG_QM_IOT_SN_KV_SUPPORT
    int len = CONFIG_QM_IOT_SN_MAX_LEN;
    if(cfg->sn[0] == '\0'){
        qm_iot_config_kv_get(iot_handle, QM_IOT_KV_SUFFIX_SN, cfg->sn, &len);
    }
#endif
    return cfg->sn;
}


int32_t qm_iot_prodtuct_sercet_set(void *iot_handle, char *prodtuct_sercet, int len)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL) {
        return -QM_EINVAL;
    }

    if(prodtuct_sercet == NULL || len <= 0) {
        return -QM_EINVAL;
    }

    // 先清理旧的密钥
    if(cfg->prodtuct_sercet){
        qm_free(cfg->prodtuct_sercet);
        cfg->prodtuct_sercet = NULL;
    }
    cfg->prodtuct_sercet = (char *)qm_malloc(len + 1);
    if(cfg->prodtuct_sercet == NULL){
        return -QM_ENOMEM;
    }

    memset(cfg->prodtuct_sercet, 0, len + 1);
    memcpy(cfg->prodtuct_sercet, prodtuct_sercet, len);

    return QM_EOK;
}

char *qm_iot_prodtuct_sercet_get(void *iot_handle)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL) {
        return NULL;
    }

    return cfg->prodtuct_sercet;
}


/**
 * @brief 保存设备版本字符串到模块内静态缓冲区（无堆分配）。
 */
int32_t qm_iot_version_str_set(void *iot_handle, char *version)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);
    size_t len;

    if (cfg == NULL) {
        return -QM_EINVAL;
    }

    if (version == NULL) {
        return -QM_EINVAL;
    }

    len = strlen(version);
    if (len > CONFIG_QM_IOT_VER_STR_MAX_LEN) {
        return -QM_EINVAL;
    }

    memcpy(cfg->str_version, version, len);
    cfg->str_version[len] = '\0';

    return QM_EOK;
}

char *qm_iot_version_str_get(void *iot_handle)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL) {
        return NULL;
    }

    if (cfg->str_version[0] == '\0') {
        return NULL;
    }
    return cfg->str_version;
}

#if CONFIG_QM_IOT_OTA_MCU_SUPPORT
int32_t qm_iot_mcu_version_set(void *iot_handle, qm_iot_mcu_ver_t *ver_info, uint16_t num)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);
    uint32_t n;
    uint32_t i;

    if (cfg == NULL) {
        return -QM_EINVAL;
    }

    if (ver_info == NULL || num == 0) {
        return -QM_EINVAL;
    }

    n = (uint32_t)num;
    if (n > (uint32_t)CONFIG_QM_IOT_MCU_VER_MAX_ENTRIES) {
        n = (uint32_t)CONFIG_QM_IOT_MCU_VER_MAX_ENTRIES;
    }

    for (i = 0; i < n; i++) {
        qm_iot_mcu_ver_slot_t *dst = &cfg->mcu_ver_slots[i];
        const qm_iot_mcu_ver_t *src = &ver_info[i];

        dst->type_id = src->type_id;
        dst->version[0] = '\0';
        dst->extra[0] = '\0';

        if (src->version != NULL) {
            memcpy(dst->version, src->version, strlen(src->version));
            dst->version[strlen(src->version)] = '\0';
        }
        if (src->extra != NULL) {
            memcpy(dst->extra, src->extra, strlen(src->extra));
            dst->extra[strlen(src->extra)] = '\0';
        }

        cfg->mcu_ver_pub[i].type_id = dst->type_id;
        cfg->mcu_ver_pub[i].version = dst->version;
        cfg->mcu_ver_pub[i].extra = dst->extra;
    }

    cfg->mcu_version.num = n;
    cfg->mcu_version.version = cfg->mcu_ver_pub;

    return QM_EOK;
}

qm_iot_mcu_ver_info_t *qm_iot_mcu_version_get(void *iot_handle)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL) {
        return NULL;
    }

    return &cfg->mcu_version;
}
#endif

int32_t qm_iot_reset_info_set(void *iot_handle, qm_iot_reset_info_t *reset_info)
{
    if (iot_handle == NULL || reset_info == NULL) {
        return -QM_EINVAL;
    }

    return qm_iot_config_kv_set(iot_handle, QM_IOT_KV_SUFFIX_RESET, reset_info,
                                (int)sizeof(qm_iot_reset_info_t), 1);
}

int32_t qm_iot_reset_info_get(void *iot_handle, qm_iot_reset_info_t *reset_info)
{
    int len = sizeof(qm_iot_reset_info_t);

    if (iot_handle == NULL || reset_info == NULL) {
        return -QM_EINVAL;
    }

    return qm_iot_config_kv_get(iot_handle, QM_IOT_KV_SUFFIX_RESET, reset_info, &len);
}

int32_t qm_iot_pid_set(void *iot_handle, uint32_t pid)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL) {
        return -QM_EINVAL;
    }

    cfg->pid = pid;
    return QM_EOK;
}

uint32_t qm_iot_pid_get(void *iot_handle)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL) {
        return 0;
    }

    return cfg->pid;
}

int32_t qm_iot_did_set(void *iot_handle, uint32_t did)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL) {
        return -QM_EINVAL;
    }

    cfg->did = did;
    return QM_EOK;
}

uint32_t qm_iot_did_get(void *iot_handle)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL) {
        return 0;
    }

    return cfg->did;
}

int32_t qm_iot_cloud_info_set(void *iot_handle, qm_iot_cloud_info_t *cloud_info)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL || cloud_info == NULL) {
        return -QM_EINVAL;
    }

    memcpy(&cfg->cloud_info, cloud_info, sizeof(cfg->cloud_info));
    return QM_EOK;
}

int32_t qm_iot_cloud_info_get(void *iot_handle, qm_iot_cloud_info_t *cloud_info)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL || cloud_info == NULL) {
        return -QM_EINVAL;
    }

    memcpy(cloud_info, &cfg->cloud_info, sizeof(*cloud_info));
    return QM_EOK;
}

/**
 * @brief 保存 MQTT 连接用户名到会话 RAM；len 为 0 或 username 为 NULL 时清空。
 */
int32_t qm_iot_mqtt_username_set(void *iot_handle, char *username, uint32_t len)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL) {
        return -QM_EINVAL;
    }

    if (username == NULL || len == 0) {
        memset(cfg->mqtt_user_name, 0, sizeof(cfg->mqtt_user_name));
        return QM_EOK;
    }

    if (len > CONFIG_QM_IOT_MQTT_USERNAME_MAX_LEN) {
        return -QM_EINVAL;
    }

    memset(cfg->mqtt_user_name, 0, sizeof(cfg->mqtt_user_name));
    memcpy(cfg->mqtt_user_name, username, len);
    return QM_EOK;
}

/**
 * @brief 读取会话内已保存的 MQTT 用户名；未设置时返回空字符串指针（非 NULL）。
 */
char *qm_iot_mqtt_username_get(void *iot_handle)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL) {
        return NULL;
    }

    return cfg->mqtt_user_name;
}

int32_t qm_iot_server_crt_set(void *iot_handle, char *server_crt, uint32_t len)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL) {
        return -QM_EINVAL;
    }

    if(server_crt == NULL || len == 0) {
        return -QM_EINVAL;
    }

    if(len > CONFIG_QM_IOT_CRT_MAX_LEN) {
        return -QM_EINVAL;
    }

    return qm_iot_config_blob_replace(&cfg->server_crt, &cfg->server_crt_len, server_crt, len);
}

int32_t qm_iot_server_crt_get(void *iot_handle, char *server_crt, uint32_t *len)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL || server_crt == NULL || len == NULL) {
        return -QM_EINVAL;
    }

    if (cfg->server_crt == NULL || cfg->server_crt_len == 0 || *len < cfg->server_crt_len) {
        return -QM_EINVAL;
    }

    memcpy(server_crt, cfg->server_crt, cfg->server_crt_len);
    *len = cfg->server_crt_len;
    return QM_EOK;
}

int32_t qm_iot_client_crt_set(void *iot_handle, char *client_crt, uint32_t len)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL) {
        return -QM_EINVAL;
    }

    if(client_crt == NULL || len == 0) {
        return -QM_EINVAL;
    }

    if(len > CONFIG_QM_IOT_CRT_MAX_LEN) {
        return -QM_EINVAL;
    }

    return qm_iot_config_blob_replace(&cfg->client_crt, &cfg->client_crt_len, client_crt, len);
}

int32_t qm_iot_client_crt_get(void *iot_handle, char *client_crt, uint32_t *len)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL || client_crt == NULL || len == NULL) {
        return -QM_EINVAL;
    }

    if (cfg->client_crt == NULL || cfg->client_crt_len == 0 || *len < cfg->client_crt_len) {
        return -QM_EINVAL;
    }

    memcpy(client_crt, cfg->client_crt, cfg->client_crt_len);
    *len = cfg->client_crt_len;
    return QM_EOK;
}

int32_t qm_iot_client_key_set(void *iot_handle, char *client_key, uint32_t len)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL) {
        return -QM_EINVAL;
    }

    if(client_key == NULL || len == 0) {
        return -QM_EINVAL;
    }

    if(len > CONFIG_QM_IOT_CRT_MAX_LEN) {
        return -QM_EINVAL;
    }

    return qm_iot_config_blob_replace(&cfg->client_key, &cfg->client_key_len, client_key, len);
}

int32_t qm_iot_client_key_get(void *iot_handle, char *client_key, uint32_t *len)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL || client_key == NULL || len == NULL) {
        return -QM_EINVAL;
    }

    if (cfg->client_key == NULL || cfg->client_key_len == 0 || *len < cfg->client_key_len) {
        return -QM_EINVAL;
    }

    memcpy(client_key, cfg->client_key, cfg->client_key_len);
    *len = cfg->client_key_len;
    return QM_EOK;
}

int32_t qm_iot_ota_public_key_set(void *iot_handle, char *public_key, uint32_t len)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL) {
        return -QM_EINVAL;
    }

    if(public_key == NULL || len == 0) {
        return -QM_EINVAL;
    }

    if(len > CONFIG_QM_IOT_OTA_CRT_MAX_LEN) {
        return -QM_EINVAL;
    }

    return qm_iot_config_blob_replace(&cfg->ota_public_key, &cfg->ota_public_key_len, public_key, len);
}

int32_t qm_iot_ota_public_key_get(void *iot_handle, char *public_key, uint32_t *len)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL || public_key == NULL || len == NULL) {
        return -QM_EINVAL;
    }

    if (cfg->ota_public_key == NULL || cfg->ota_public_key_len == 0 || *len < cfg->ota_public_key_len) {
        return -QM_EINVAL;
    }

    memcpy(public_key, cfg->ota_public_key, cfg->ota_public_key_len);
    *len = cfg->ota_public_key_len;
    return QM_EOK;
}

#if CONFIG_QM_IOT_AUDIO_URI_SUPPORT
int qm_iot_audio_uri_set(void *iot_handle, char *uri, uint32_t len)
{
    if (iot_handle == NULL) {
        return -QM_EINVAL;
    }

    return qm_iot_config_kv_set(iot_handle, QM_IOT_KV_SUFFIX_AUDIO_URI, uri, (int)len, 1);
}

int32_t qm_iot_audio_uri_get(void *iot_handle, char *uri, uint32_t *len)
{
    if (iot_handle == NULL) {
        return -QM_EINVAL;
    }

    return qm_iot_config_kv_get(iot_handle, QM_IOT_KV_SUFFIX_AUDIO_URI, uri, (int *)len);
}

int qm_iot_agora_uri_set(void *iot_handle, char *uri, uint32_t len)
{
    if (iot_handle == NULL) {
        return -QM_EINVAL;
    }

    return qm_iot_config_kv_set(iot_handle, QM_IOT_KV_SUFFIX_AGORA_URI, uri, (int)len, 1);
}

int32_t qm_iot_agora_uri_get(void *iot_handle, char *uri, uint32_t *len)
{
    if (iot_handle == NULL) {
        return -QM_EINVAL;
    }

    return qm_iot_config_kv_get(iot_handle, QM_IOT_KV_SUFFIX_AGORA_URI, uri, (int *)len);
}
#endif

#if CONFIG_QM_IOT_AUTHCODE_SUPPORT
char *qm_iot_authcode_get(void *iot_handle)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL) {
        return NULL;
    }

    return cfg->authcode;
}

int qm_iot_authcode_set(void *iot_handle, char *auth_code, uint32_t len)
{
    qm_iot_session_t *cfg = qm_iot_config_ctx(iot_handle);

    if (cfg == NULL || auth_code == NULL) {
        return -QM_EINVAL;
    }

    memcpy(cfg->authcode, auth_code, len);
    return QM_EOK;
}

#endif
