/**
 * @file qm_agent_core.c
 * @brief Public session lifecycle wiring.
 *
 * qm_agent_core.c wires together optional media drivers, qm_agent_auth authentication,
 * and the active transport while keeping applications behind the stable qm_agent.h API.
 *
 * 此文件编排可选音视频驱动、qm_agent_auth 认证和当前传输适配器，对应用保持稳定的
 * qm_agent.h 接口。生命周期操作由 qm_agent_ctl 串行执行。
 */

#include "qm_agent_internal.h"

#define QM_AGENT_DEFAULT_VIDEO_FRAME_RATE 15U
#define QM_AGENT_RECONCILE_RETRY_DELAY_MS 200U
#define LOG_TAG "qm_agent"

static volatile uint32_t g_qm_agent_log_flags = QM_AGENT_KCONFIG_LOG_FLAGS;

#if defined(CONFIG_QM_AGENT_TRANSPORT_AGORA) && CONFIG_QM_AGENT_TRANSPORT_AGORA
#define QM_AGENT_HAS_KCONFIG_AGORA 1
#else
#define QM_AGENT_HAS_KCONFIG_AGORA 0
#endif

#if defined(CONFIG_QM_AGENT_TRANSPORT_BAIDU) && CONFIG_QM_AGENT_TRANSPORT_BAIDU
#define QM_AGENT_HAS_KCONFIG_BAIDU 1
#else
#define QM_AGENT_HAS_KCONFIG_BAIDU 0
#endif

#if defined(CONFIG_QM_AGENT_TRANSPORT_JOYINSIDE) && CONFIG_QM_AGENT_TRANSPORT_JOYINSIDE
#define QM_AGENT_HAS_KCONFIG_JOYINSIDE 1
#else
#define QM_AGENT_HAS_KCONFIG_JOYINSIDE 0
#endif

#if defined(CONFIG_QM_AGENT_TRANSPORT_LGEINE) && CONFIG_QM_AGENT_TRANSPORT_LGEINE
#define QM_AGENT_HAS_KCONFIG_LGEINE 1
#else
#define QM_AGENT_HAS_KCONFIG_LGEINE 0
#endif

#define QM_AGENT_KCONFIG_TRANSPORT_COUNT \
    (QM_AGENT_HAS_KCONFIG_AGORA + \
     QM_AGENT_HAS_KCONFIG_BAIDU + QM_AGENT_HAS_KCONFIG_JOYINSIDE + \
     QM_AGENT_HAS_KCONFIG_LGEINE)

static qm_agent_transport_type_t sanitize_transport_type(qm_agent_transport_type_t type)
{
    switch (type) {
        case QM_AGENT_TRANSPORT_NONE:
        case QM_AGENT_TRANSPORT_AGORA:
        case QM_AGENT_TRANSPORT_BAIDU:
        case QM_AGENT_TRANSPORT_JOYINSIDE:
        case QM_AGENT_TRANSPORT_LGEINE:
        case QM_AGENT_TRANSPORT_CUSTOM:
            return type;
        default:
            return QM_AGENT_TRANSPORT_NONE;
    }
}

static qm_agent_transport_type_t kconfig_default_transport_type(void)
{
#if QM_AGENT_KCONFIG_TRANSPORT_COUNT == 1
#if QM_AGENT_HAS_KCONFIG_AGORA
    return QM_AGENT_TRANSPORT_AGORA;
#elif QM_AGENT_HAS_KCONFIG_BAIDU
    return QM_AGENT_TRANSPORT_BAIDU;
#elif QM_AGENT_HAS_KCONFIG_LGEINE
    return QM_AGENT_TRANSPORT_LGEINE;
#else
    return QM_AGENT_TRANSPORT_JOYINSIDE;
#endif
#else
    return QM_AGENT_TRANSPORT_NONE;
#endif
}

static int qm_agent_start_now(qm_agent_handle_t handle);
static int qm_agent_stop_now(qm_agent_handle_t handle);
static int qm_agent_send_text_now(qm_agent_handle_t handle, const char *text);
static int qm_agent_asr_start_now(qm_agent_handle_t handle, uint32_t timeout_ms);
static int qm_agent_asr_stop_now(qm_agent_handle_t handle, uint32_t timeout_ms);
static int qm_agent_asr_realtime_now(qm_agent_handle_t handle,
                                     qm_bool enabled,
                                     uint32_t timeout_ms);
static int qm_agent_network_update_now(qm_agent_handle_t handle,
                                       qm_agent_network_state_t state);
static int interrupt_session(qm_agent_handle_t handle);
static int qm_agent_auth_runtime_init(qm_agent_handle_t handle);
static int qm_agent_auth_runtime_deinit(qm_agent_handle_t handle);
static int qm_agent_tts_runtime_init(qm_agent_handle_t handle);
static int qm_agent_tts_runtime_deinit(qm_agent_handle_t handle);
static void qm_agent_tts_runtime_finish(qm_agent_handle_t handle,
                                        uint32_t request_id,
                                        uint32_t generation,
                                        qm_bool check_generation,
                                        qm_agent_tts_event_t event,
                                        int code);
static void qm_agent_tts_runtime_cancel_active(qm_agent_handle_t handle,
                                               qm_agent_tts_event_t event,
                                               int code);
void qm_agent_tts_runtime_on_event(qm_agent_handle_t handle,
                                   const qm_agent_event_t *event);

#if defined(CONFIG_QM_AGENT_AUTH_CHANNEL_HTTP) && CONFIG_QM_AGENT_AUTH_CHANNEL_HTTP
#define QM_AGENT_AUTH_USES_CLOUD_EVENTS 0
#else
#define QM_AGENT_AUTH_USES_CLOUD_EVENTS 1
#endif

int qm_agent_lifecycle_begin(qm_agent_handle_t handle, qm_bool destroy)
{
    int ret = QM_EOK;

    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    /* A destroy request closes callback admission immediately. Existing
     * callbacks may still force this attempt to return EBUSY, but cannot keep
     * a retry starving by continuously entering after the request. */
    if (destroy) {
        handle->destroying = QM_TRUE;
    }
    if (handle->lifecycle_busy || handle->callback_depth != 0U ||
        (handle->destroying && !destroy)) {
        ret = -QM_EBUSY;
    } else {
        handle->lifecycle_busy = QM_TRUE;
    }
    qm_mutex_unlock(&handle->network_lock);
    return ret;
}

void qm_agent_lifecycle_end(qm_agent_handle_t handle)
{
    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    handle->lifecycle_busy = QM_FALSE;
    qm_mutex_unlock(&handle->network_lock);
}

static uint16_t qm_agent_pcm_sample_bits(qm_audio_pcm_sample_t sample)
{
    switch (sample) {
        case QM_AUDIO_PCM_S16:
            return 16U;
        case QM_AUDIO_PCM_S24:
            return 24U;
        case QM_AUDIO_PCM_S32:
            return 32U;
        default:
            return 0U;
    }
}

static qm_bool qm_agent_pcm_format_valid(const qm_audio_pcm_format_t *format)
{
    return (format != NULL && format->sample_rate_hz != 0U &&
            format->channels != 0U &&
            qm_agent_pcm_sample_bits(format->sample) != 0U) ?
           QM_TRUE : QM_FALSE;
}

static int build_transport_audio_formats(qm_agent_handle_t handle,
                                         qm_agent_audio_format_t *input_format,
                                         qm_agent_audio_format_t *output_format)
{
    int ret;

    if (handle == NULL || input_format == NULL || output_format == NULL) {
        return -QM_EINVAL;
    }

#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
    if (handle->audio.control == NULL ||
        !qm_agent_pcm_format_valid(&handle->audio.output_stream_format.pcm)) {
        return -QM_EINVAL;
    }

    /* A playback-only Engine has no input format.  Keep transport negotiation
     * valid by using the output PCM geometry as a fallback; automatic capture
     * remains disabled by capture_enabled. */
    if (qm_agent_pcm_format_valid(&handle->audio.input_stream_format.pcm)) {
        ret = qm_agent_audio_engine_resolve_stream_format(
            input_format,
            &handle->config.audio.uplink_format,
            &handle->audio.input_stream_format);
    } else {
        ret = qm_agent_audio_engine_resolve_stream_format(
            input_format,
            &handle->config.audio.uplink_format,
            &handle->audio.output_stream_format);
    }
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_agent_audio_engine_resolve_stream_format(
        output_format, &handle->config.audio.downlink_format,
        &handle->audio.output_stream_format);
    if (ret == QM_EOK) {
        handle->audio.transport_input_format = *input_format;
        handle->audio.transport_output_format = *output_format;
    }
    return ret;
#else
    /* Audio-disabled builds still permit transports that do not use media.
     * Leave their descriptors exactly as configured; an audio-capable
     * built-in transport may reject the missing format during its own init. */
    *input_format = handle->config.audio.uplink_format;
    *output_format = handle->config.audio.downlink_format;
    return QM_EOK;
#endif
}

static void qm_agent_config_attach_managed_modules(qm_agent_config_t *config)
{
    if (config == NULL) {
        return;
    }

    if (config->module_count == 0) {
        config->module_names = NULL;
        config->module_configs = NULL;
        return;
    }

    config->module_names = config->managed_module_names;
    config->module_configs = config->managed_module_configs;
}

static int transport_init(qm_agent_handle_t handle)
{
    const qm_agent_transport_if_t *transport;
    qm_agent_audio_format_t input_format;
    qm_agent_audio_format_t output_format;
    int ret;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    /**
     * A caller may provide a custom transport_if. Otherwise select one of the
     * built-in adapters from the configured transport type.
     */
    transport = handle->config.transport_if;
    if (transport == NULL) {
        transport = qm_agent_transport_get_default(handle->config.transport.type);
        handle->config.transport_if = transport;
    }

    if (handle->transport_ctx != NULL) {
        return QM_EOK;
    }

    if (transport == NULL || transport->init == NULL) {
        return -QM_ENOSYS;
    }

    ret = build_transport_audio_formats(handle, &input_format, &output_format);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = transport->init(&handle->transport_ctx,
                           &handle->config.transport,
                           &input_format,
                           &output_format,
                           &handle->config.input_video,
                           &handle->config.output_video,
                           qm_agent_on_transport_event,
                           handle);
    if (ret == QM_EOK) {
        qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
        handle->network_forwarding = QM_TRUE;
        if (transport->network_update != NULL) {
            ret = transport->network_update(handle->transport_ctx, handle->network_state);
        }
        qm_mutex_unlock(&handle->network_lock);
    }
    return ret;
}

static int transport_deinit(qm_agent_handle_t handle)
{
    const qm_agent_transport_if_t *transport;
    int ret = QM_EOK;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    transport = handle->config.transport_if;
    /* Block new notifications before transport teardown can free its context. */
    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    handle->network_forwarding = QM_FALSE;
    qm_mutex_unlock(&handle->network_lock);
    if (transport && transport->deinit && handle->transport_ctx) {
        ret = transport->deinit(handle->transport_ctx);
    }

    if (ret == QM_EOK) {
        handle->transport_ctx = NULL;
    } else {
        qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
        handle->network_forwarding = QM_TRUE;
        if (transport && transport->network_update) {
            (void)transport->network_update(handle->transport_ctx, handle->network_state);
        }
        qm_mutex_unlock(&handle->network_lock);
    }
    return ret;
}

static qm_bool transport_available(qm_agent_handle_t handle)
{
    return handle->started && !handle->destroying && handle->state != QM_AGENT_STATE_STOPPING &&
           handle->transport_ctx != NULL;
}

static qm_bool qm_agent_transport_needs_auth(qm_agent_transport_type_t type)
{
    switch (type) {
        case QM_AGENT_TRANSPORT_AGORA:
        case QM_AGENT_TRANSPORT_BAIDU:
        case QM_AGENT_TRANSPORT_JOYINSIDE:
        case QM_AGENT_TRANSPORT_LGEINE:
            return QM_TRUE;
        default:
            return QM_FALSE;
    }
}

static qm_bool qm_agent_uses_auto_auth(qm_agent_handle_t handle)
{
    return (handle != NULL &&
            handle->auth.helper != NULL &&
            qm_agent_transport_needs_auth(handle->config.transport.type));
}

#if defined(CONFIG_QM_AGENT_AUTH_CHANNEL_HTTP) && CONFIG_QM_AGENT_AUTH_CHANNEL_HTTP
static qm_bool qm_agent_http_auth_config_valid(const qm_agent_auth_http_config_t *config)
{
    return (config != NULL &&
            config->product_id != 0U &&
            config->product_secret != NULL &&
            config->product_secret[0] != '\0' &&
            config->sn != NULL &&
            config->sn[0] != '\0');
}
#endif

static int qm_agent_apply_auth(qm_agent_handle_t handle,
                            const qm_agent_auth_info_t *auth)
{
    if (handle == NULL || auth == NULL) {
        return -QM_EINVAL;
    }

    switch (handle->config.transport.type) {
        case QM_AGENT_TRANSPORT_JOYINSIDE:
#if QM_AGENT_HAS_KCONFIG_JOYINSIDE
            if (auth->type != QM_AGENT_AUTH_TYPE_JOYINSIDE ||
                auth->data.joyinside.access_token == NULL ||
                auth->data.joyinside.bot_id == NULL) {
                QM_AGENT_LOGE(LOG_TAG,
                        "auth mismatch transport=joyinside auth_type=%d token=%p bot_id=%p",
                        auth->type,
                        auth->data.joyinside.access_token,
                        auth->data.joyinside.bot_id);
                return -QM_ENOENT;
            }

            handle->config.transport.data.joyinside.access_token =
                auth->data.joyinside.access_token;
            handle->config.transport.data.joyinside.bot_id =
                auth->data.joyinside.bot_id;
            handle->config.transport.data.joyinside.uid =
                auth->ljyd.user_id;
            return QM_EOK;
#else
            return -QM_ENOSYS;
#endif

        case QM_AGENT_TRANSPORT_AGORA:
#if QM_AGENT_HAS_KCONFIG_AGORA
            if (auth->type != QM_AGENT_AUTH_TYPE_AGORA ||
                auth->data.agora.app_id == NULL ||
                auth->data.agora.channel_name == NULL ||
                auth->data.agora.token == NULL ||
                auth->data.agora.uid == 0U) {
                QM_AGENT_LOGE(LOG_TAG,
                        "auth mismatch transport=agora auth_type=%d appid=%p channel=%p token=%p uid=%u",
                        auth->type,
                        auth->data.agora.app_id,
                        auth->data.agora.channel_name,
                        auth->data.agora.token,
                        (unsigned int)auth->data.agora.uid);
                return -QM_ENOENT;
            }

            handle->config.transport.data.agora.app_id =
                auth->data.agora.app_id;
            handle->config.transport.data.agora.channel_name =
                auth->data.agora.channel_name;
            handle->config.transport.data.agora.token = auth->data.agora.token;
            handle->config.transport.data.agora.license =
                auth->data.agora.license;
            handle->config.transport.data.agora.uid = auth->data.agora.uid;
            return QM_EOK;
#else
            return -QM_ENOSYS;
#endif

        case QM_AGENT_TRANSPORT_BAIDU:
#if QM_AGENT_HAS_KCONFIG_BAIDU
            if (auth->type != QM_AGENT_AUTH_TYPE_BAIDU ||
                auth->data.baidu.app_id == NULL ||
                auth->data.baidu.instance_id == NULL ||
                auth->data.baidu.token == NULL ||
                auth->data.baidu.audio_codec == NULL) {
                QM_AGENT_LOGE(LOG_TAG,
                        "auth mismatch transport=baidu auth_type=%d appid=%p instance=%p token=%p ac=%p",
                        auth->type,
                        auth->data.baidu.app_id,
                        auth->data.baidu.instance_id,
                        auth->data.baidu.token,
                        auth->data.baidu.audio_codec);
                return -QM_ENOENT;
            }

            handle->config.transport.data.baidu_chat_agent.app_id =
                auth->data.baidu.app_id;
            handle->config.transport.data.baidu_chat_agent.instance_id =
                auth->data.baidu.instance_id;
            handle->config.transport.data.baidu_chat_agent.token =
                auth->data.baidu.token;
            handle->config.transport.data.baidu_chat_agent.audio_codec =
                auth->data.baidu.audio_codec;
            handle->config.transport.data.baidu_chat_agent.license_key =
                auth->data.baidu.license_key;
            return QM_EOK;
#else
            return -QM_ENOSYS;
#endif

        case QM_AGENT_TRANSPORT_LGEINE:
#if QM_AGENT_HAS_KCONFIG_LGEINE && \
    defined(CONFIG_LGEINE_AGENT_SUPPORT) && CONFIG_LGEINE_AGENT_SUPPORT
            if (auth->type != QM_AGENT_AUTH_TYPE_LGEINE ||
                auth->ljyd.token == NULL) {
                QM_AGENT_LOGE(LOG_TAG,
                        "auth mismatch transport=lgeine auth_type=%d token=%p",
                        auth->type,
                        auth->ljyd.token);
                return -QM_ENOENT;
            }

            handle->config.transport.data.lgeine.access_token =
                auth->ljyd.token;
            return QM_EOK;
#else
            return -QM_ENOSYS;
#endif

        default:
            return QM_EOK;
    }
}

static void qm_agent_clear_transport_auth(qm_agent_handle_t handle)
{
    if (handle == NULL) {
        return;
    }

    switch (handle->config.transport.type) {
        case QM_AGENT_TRANSPORT_JOYINSIDE:
#if QM_AGENT_HAS_KCONFIG_JOYINSIDE
            handle->config.transport.data.joyinside.access_token = NULL;
            handle->config.transport.data.joyinside.bot_id = NULL;
            handle->config.transport.data.joyinside.uid = NULL;
#endif
            break;
        case QM_AGENT_TRANSPORT_AGORA:
#if QM_AGENT_HAS_KCONFIG_AGORA
            handle->config.transport.data.agora.app_id = NULL;
            handle->config.transport.data.agora.channel_name = NULL;
            handle->config.transport.data.agora.token = NULL;
            handle->config.transport.data.agora.license = NULL;
            handle->config.transport.data.agora.uid = 0;
#endif
            break;
        case QM_AGENT_TRANSPORT_BAIDU:
#if QM_AGENT_HAS_KCONFIG_BAIDU
            handle->config.transport.data.baidu_chat_agent.app_id = NULL;
            handle->config.transport.data.baidu_chat_agent.instance_id = NULL;
            handle->config.transport.data.baidu_chat_agent.token = NULL;
            handle->config.transport.data.baidu_chat_agent.audio_codec = NULL;
            handle->config.transport.data.baidu_chat_agent.license_key = NULL;
#endif
            break;
        case QM_AGENT_TRANSPORT_LGEINE:
#if QM_AGENT_HAS_KCONFIG_LGEINE && \
    defined(CONFIG_LGEINE_AGENT_SUPPORT) && CONFIG_LGEINE_AGENT_SUPPORT
            handle->config.transport.data.lgeine.access_token = NULL;
#endif
            break;
        default:
            break;
    }
}

static int qm_agent_prepare_transport(qm_agent_handle_t handle)
{
    int ret;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    if (handle->transport_ctx != NULL) {
        return QM_EOK;
    }

    if (qm_agent_uses_auto_auth(handle)) {
        if (!handle->auth.has_auth) {
            return -QM_EBUSY;
        }

        ret = qm_agent_apply_auth(handle, &handle->auth.info);
        if (ret != QM_EOK) {
            return ret;
        }
    }

    return transport_init(handle);
}

static int qm_agent_schedule_reconcile_after(qm_agent_handle_t handle, uint32_t delay_ms)
{
    int ret;
    if (handle == NULL) {
        return -QM_EINVAL;
    }

    ret = qm_agent_control_schedule(handle,
                                    QM_AGENT_CONTROL_RECONCILE,
                                    delay_ms,
                                    0U,
                                    0U);
    if (ret != QM_EOK) {
        QM_AGENT_LOGE(LOG_TAG, "reconcile schedule failed ret=%d", ret);
    }

    return ret;
}

static int qm_agent_schedule_reconcile(qm_agent_handle_t handle)
{
    return qm_agent_schedule_reconcile_after(handle, 0);
}

static int qm_agent_request_auth(qm_agent_handle_t handle)
{
    int ret;

    if (handle == NULL || handle->auth.helper == NULL) {
        return -QM_EINVAL;
    }

    if (handle->auth.has_auth) {
        return QM_EOK;
    }

    ret = qm_agent_auth_request_token(handle->auth.helper, 0);
    if (ret == QM_EOK) {
        QM_AGENT_LOGI(LOG_TAG, QM_AGENT_LOG_CONTROL, "getAgentToken requested");
    } else {
        QM_AGENT_LOGE(LOG_TAG, "getAgentToken request failed ret=%d", ret);
        if (ret != -QM_EBUSY) qm_agent_set_state(handle, QM_AGENT_STATE_ERROR);
    }

    return ret;
}

void qm_agent_auth_ready(qm_agent_handle_t handle)
{
    (void)qm_agent_schedule_reconcile(handle);
}

/**
 * @brief Reconcile desired session state with network and credential availability.
 * 将期望会话状态与网络和凭据可用性协调一致。
 *
 * This function runs on the control task. It may request credentials, apply a
 * fresh auth reply to transport configuration, or start/stop transport work.
 * 本函数在控制任务运行，可请求凭据、将新认证回复应用到传输配置，或启动/停止传输工作。
 */
void qm_agent_reconcile(qm_agent_handle_t handle)
{
    qm_bool start_attempted = QM_FALSE;
    qm_bool retry_pending_stop = QM_FALSE;
    int ret = QM_EOK;

    if (handle == NULL || handle->auth.helper == NULL) {
        return;
    }
    qm_agent_auth_process_pending(handle->auth.helper);

    QM_AGENT_LOGI(LOG_TAG, QM_AGENT_LOG_CONTROL,
            "reconcile work enter desired_running=%d has_auth=%d started=%d",
            handle->desired_running,
            handle->auth.has_auth,
            handle->started);

    if (handle->started && handle->state == QM_AGENT_STATE_STOPPING) {
        ret = qm_agent_stop_now(handle);
        if (ret != QM_EOK) {
            QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_LIFECYCLE,
                    "reconcile pending stop incomplete ret=%d", ret);
            retry_pending_stop = QM_TRUE;
            goto reconcile_done;
        }
    }

    if (!handle->auth.has_auth) {
        if (handle->started) {
            ret = qm_agent_stop_now(handle);
            if (ret != QM_EOK) {
                QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_LIFECYCLE,
                        "reconcile stop incomplete ret=%d", ret);
                retry_pending_stop = (handle->state == QM_AGENT_STATE_STOPPING);
                goto reconcile_done;
            }
        }
        ret = transport_deinit(handle);
        if (ret != QM_EOK) {
            QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_LIFECYCLE,
                    "reconcile transport deinit incomplete ret=%d", ret);
            retry_pending_stop = QM_TRUE;
            goto reconcile_done;
        }
        qm_agent_auth_info_deinit(&handle->auth.info);
        qm_agent_clear_transport_auth(handle);
        /* The old transport no longer borrows the previous credential set. */
        if (qm_agent_auth_get_token(handle->auth.helper, &handle->auth.info) == QM_EOK) {
            handle->auth.has_auth = QM_TRUE;
        }
    }

    if (handle->desired_running) {
        if (handle->auth.has_auth) {
            start_attempted = QM_TRUE;
            ret = qm_agent_start_now(handle);
            if (ret != QM_EOK) {
                QM_AGENT_LOGE(LOG_TAG, "start after auth failed ret=%d", ret);
                retry_pending_stop = (handle->state == QM_AGENT_STATE_STOPPING);
            }
        } else {
            QM_AGENT_LOGI(LOG_TAG, QM_AGENT_LOG_CONTROL, "reconcile work request token");
            ret = qm_agent_request_auth(handle);
            if (ret != QM_EOK) {
                QM_AGENT_LOGE(LOG_TAG, "reconcile work request failed ret=%d", ret);
            }
        }
    }

reconcile_done:
    if (retry_pending_stop) {
        ret = qm_agent_schedule_reconcile_after(handle,
                                                QM_AGENT_RECONCILE_RETRY_DELAY_MS);
        if (ret != QM_EOK) {
            QM_AGENT_LOGE(LOG_TAG, "reconcile stop retry schedule failed ret=%d", ret);
        }
    } else if (handle->desired_running &&
        handle->auth.has_auth &&
        !handle->started &&
        !start_attempted) {
        ret = qm_agent_schedule_reconcile(handle);
        if (ret != QM_EOK) {
            QM_AGENT_LOGE(LOG_TAG, "reconcile schedule after token failed ret=%d", ret);
        }
    }
}

static int qm_agent_auth_runtime_init(qm_agent_handle_t handle)
{
    int ret;

    if (handle == NULL ||
        !qm_agent_transport_needs_auth(handle->config.transport.type)) {
        return QM_EOK;
    }

#if QM_AGENT_AUTH_USES_CLOUD_EVENTS
    if (handle->config.iot_handle == NULL) {
        return QM_EOK;
    }
#else
    if (!qm_agent_http_auth_config_valid(&handle->config.auth_http)) {
        QM_AGENT_LOGI(LOG_TAG, QM_AGENT_LOG_LIFECYCLE, "http auth config incomplete, skip auto auth");
        return QM_EOK;
    }
#endif

    handle->auth.helper = qm_agent_auth_init(handle);
    if (handle->auth.helper == NULL) {
        ret = -QM_EINVAL;
        goto fail;
    }

#if defined(CONFIG_QM_AGENT_AUTH_CHANNEL_HTTP) && CONFIG_QM_AGENT_AUTH_CHANNEL_HTTP
    ret = qm_agent_auth_set_http_config(handle->auth.helper,
                                        &handle->config.auth_http);
    if (ret != QM_EOK) {
        goto fail;
    }
#endif

    return QM_EOK;

fail:
    if (handle->auth.helper != NULL) {
        (void)qm_agent_auth_deinit(handle->auth.helper);
        handle->auth.helper = NULL;
    }
    return ret;
}

static int qm_agent_auth_runtime_deinit(qm_agent_handle_t handle)
{
    int ret;
    if (handle == NULL) {
        return QM_EOK;
    }

    if (handle->auth.helper != NULL) {
        ret = qm_agent_auth_deinit(handle->auth.helper);
        if (ret != QM_EOK) return ret;
        handle->auth.helper = NULL;
    }

    qm_agent_auth_info_deinit(&handle->auth.info);
    qm_agent_clear_transport_auth(handle);

    handle->auth.has_auth = QM_FALSE;
    return QM_EOK;
}

/**
 * @brief Fill configuration defaults without overwriting caller-selected resources.
 * 补充配置默认值，同时不覆盖调用方已选择的资源。
 */
void qm_agent_config_init_defaults(qm_agent_config_t *config)
{
    qm_agent_transport_type_t transport_type;
    const qm_agent_transport_if_t *transport_if;
    qm_agent_audio_config_t audio;
    qm_agent_capture_policy_t transport_capture_policy;
    const qm_agent_video_driver_if_t *video_driver;
    qm_bool managed_modules_in_use;
    const char *module_names[QM_AGENT_MAX_MODULES];
    const void *module_configs[QM_AGENT_MAX_MODULES];
    const char * const *module_name_ptrs;
    const void * const *module_config_ptrs;
    uint32_t module_count;
    uint32_t i;

    if (config == NULL) {
        return;
    }

    transport_type = sanitize_transport_type(config->transport.type);
    if (transport_type == QM_AGENT_TRANSPORT_NONE) {
        transport_type = kconfig_default_transport_type();
    }
    transport_if = (transport_type == QM_AGENT_TRANSPORT_CUSTOM) ? config->transport_if : NULL;
    audio = config->audio;
    transport_capture_policy = config->transport_capture_policy;
    video_driver = config->video_driver;
    module_name_ptrs = config->module_names;
    module_config_ptrs = config->module_configs;
    module_count = config->module_count;
    managed_modules_in_use = (module_name_ptrs == config->managed_module_names);

    qm_memset(module_names, 0, sizeof(module_names));
    qm_memset(module_configs, 0, sizeof(module_configs));
    if (managed_modules_in_use) {
        if (module_count > QM_AGENT_MAX_MODULES) {
            module_count = QM_AGENT_MAX_MODULES;
        }
    }

    if (managed_modules_in_use) {
        for (i = 0; i < module_count; i++) {
            module_names[i] = config->managed_module_names[i];
            module_configs[i] = config->managed_module_configs[i];
        }
    }

    qm_memset(config, 0, sizeof(*config));
    config->transport.type = transport_type;
    config->transport_if = transport_if;
    config->audio = audio;
    config->transport_capture_policy = transport_capture_policy;
    config->video_driver = video_driver;

    if (managed_modules_in_use) {
        config->module_count = module_count;
        for (i = 0; i < module_count; i++) {
            config->managed_module_names[i] = module_names[i];
            config->managed_module_configs[i] = module_configs[i];
        }
        qm_agent_config_attach_managed_modules(config);
    } else {
        config->module_names = module_name_ptrs;
        config->module_configs = module_config_ptrs;
        config->module_count = module_count;
    }
    config->input_video.codec = QM_AGENT_VIDEO_CODEC_JPEG;
    config->input_video.width = 0;
    config->input_video.height = 0;
    config->input_video.frame_rate = QM_AGENT_DEFAULT_VIDEO_FRAME_RATE;
    config->input_video.rotation = 0;
    config->output_video = config->input_video;
}

int qm_agent_config_add_module(qm_agent_config_t *config,
                               const char *module_name,
                               const void *module_config)
{
    if (config == NULL || module_name == NULL) {
        return -QM_EINVAL;
    }

    if (config->module_count >= QM_AGENT_MAX_MODULES) {
        return -QM_EFULL;
    }

    if (config->module_count > 0 &&
        config->module_names != config->managed_module_names &&
        config->module_names != NULL) {
        return -QM_EINVAL;
    }

    config->managed_module_names[config->module_count] = module_name;
    config->managed_module_configs[config->module_count] = module_config;
    config->module_count++;
    qm_agent_config_attach_managed_modules(config);

    return QM_EOK;
}

void qm_agent_tts_timeout(qm_agent_handle_t handle,
                          uint32_t request_id,
                          uint32_t generation)
{
    if (handle == NULL || !handle->tts_request.lock_valid) {
        return;
    }

    qm_mutex_lock(&handle->tts_request.lock, QM_WAIT_FOREVER);
    if (!handle->tts_request.active ||
        handle->tts_request.request_id != request_id ||
        handle->tts_request.generation != generation) {
        qm_mutex_unlock(&handle->tts_request.lock);
        return;
    }
    qm_mutex_unlock(&handle->tts_request.lock);

#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
    (void)qm_agent_audio_engine_abort_stream(handle, QM_TRUE);
#endif
    qm_agent_tts_runtime_finish(handle, request_id, 0U, QM_FALSE,
                                QM_AGENT_TTS_EVENT_TIMEOUT, -QM_ETIMEOUT);
    if (transport_available(handle)) {
        const qm_agent_transport_if_t *transport = handle->config.transport_if;
        if (transport != NULL && transport->interrupt != NULL) {
            (void)transport->interrupt(handle->transport_ctx);
        }
    }
}

static int qm_agent_tts_runtime_init(qm_agent_handle_t handle)
{
    int ret;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    ret = qm_mutex_new(&handle->tts_request.lock);
    if (ret != QM_EOK) {
        return ret;
    }
    handle->tts_request.lock_valid = QM_TRUE;
    handle->tts_request.next_id = 0U;
    return QM_EOK;
}

static int qm_agent_tts_runtime_deinit(qm_agent_handle_t handle)
{
    if (handle == NULL || !handle->tts_request.lock_valid) {
        return QM_EOK;
    }

    qm_mutex_lock(&handle->tts_request.lock, QM_WAIT_FOREVER);
    handle->tts_request.active = QM_FALSE;
    handle->tts_request.callback = NULL;
    handle->tts_request.user_ctx = NULL;
    qm_mutex_unlock(&handle->tts_request.lock);
    (void)qm_mutex_free(&handle->tts_request.lock);
    handle->tts_request.lock_valid = QM_FALSE;
    return QM_EOK;
}

static void qm_agent_tts_runtime_finish(qm_agent_handle_t handle,
                                        uint32_t request_id,
                                        uint32_t generation,
                                        qm_bool check_generation,
                                        qm_agent_tts_event_t event,
                                        int code)
{
    qm_agent_tts_cb_t callback = NULL;
    void *user_ctx = NULL;

    if (handle == NULL || !handle->tts_request.lock_valid) {
        return;
    }

    qm_mutex_lock(&handle->tts_request.lock, QM_WAIT_FOREVER);
    if (!handle->tts_request.active ||
        handle->tts_request.request_id != request_id ||
        (check_generation && handle->tts_request.generation != generation)) {
        qm_mutex_unlock(&handle->tts_request.lock);
        return;
    }
    handle->tts_request.active = QM_FALSE;
    callback = handle->tts_request.callback;
    user_ctx = handle->tts_request.user_ctx;
    handle->tts_request.callback = NULL;
    handle->tts_request.user_ctx = NULL;
    qm_mutex_unlock(&handle->tts_request.lock);

    if (callback != NULL && qm_agent_callback_enter(handle)) {
        callback(handle, request_id, event, code, user_ctx);
        qm_agent_callback_leave(handle);
    }
}

static void qm_agent_tts_runtime_cancel_active(qm_agent_handle_t handle,
                                               qm_agent_tts_event_t event,
                                               int code)
{
    uint32_t request_id;

    if (handle == NULL || !handle->tts_request.lock_valid) {
        return;
    }

    qm_mutex_lock(&handle->tts_request.lock, QM_WAIT_FOREVER);
    request_id = handle->tts_request.request_id;
    if (!handle->tts_request.active) {
        qm_mutex_unlock(&handle->tts_request.lock);
        return;
    }
    qm_mutex_unlock(&handle->tts_request.lock);

    qm_agent_tts_runtime_finish(handle,
                                request_id,
                                0U,
                                QM_FALSE,
                                event,
                                code);
}

void qm_agent_tts_runtime_on_event(qm_agent_handle_t handle,
                                   const qm_agent_event_t *event)
{
    uint32_t request_id;
    uint32_t generation;
    qm_agent_tts_event_t tts_event;
    qm_bool terminal = QM_FALSE;
    int code;

    if (handle == NULL || event == NULL || !handle->tts_request.lock_valid) {
        return;
    }

    if (event->id == QM_AGENT_EVENT_TTS_PLAYBACK_STARTED ||
        event->id == QM_AGENT_EVENT_TTS_PLAYBACK_COMPLETE ||
        event->id == QM_AGENT_EVENT_TTS_PLAYBACK_FAILED) {
        generation = event->data.tts_playback.generation;
        qm_mutex_lock(&handle->tts_request.lock, QM_WAIT_FOREVER);
        if (!handle->tts_request.active ||
            handle->tts_request.generation != generation) {
            qm_mutex_unlock(&handle->tts_request.lock);
            return;
        }
        request_id = handle->tts_request.request_id;
        if (event->id == QM_AGENT_EVENT_TTS_PLAYBACK_STARTED) {
            if (handle->tts_request.started) {
                qm_mutex_unlock(&handle->tts_request.lock);
                return;
            }
            handle->tts_request.started = QM_TRUE;
            tts_event = QM_AGENT_TTS_EVENT_STARTED;
            terminal = QM_FALSE;
        } else {
            tts_event = event->id == QM_AGENT_EVENT_TTS_PLAYBACK_COMPLETE ?
                        QM_AGENT_TTS_EVENT_COMPLETED : QM_AGENT_TTS_EVENT_FAILED;
            terminal = QM_TRUE;
        }
        qm_mutex_unlock(&handle->tts_request.lock);
        if (terminal) {
            code = tts_event == QM_AGENT_TTS_EVENT_COMPLETED ? QM_EOK :
                   (event->code != QM_EOK ? event->code : -QM_EIO);
            qm_agent_tts_runtime_finish(handle, request_id, generation,
                                        QM_TRUE, tts_event, code);
        } else {
            qm_agent_tts_cb_t callback;
            void *user_ctx;

            qm_mutex_lock(&handle->tts_request.lock, QM_WAIT_FOREVER);
            callback = handle->tts_request.callback;
            user_ctx = handle->tts_request.user_ctx;
            qm_mutex_unlock(&handle->tts_request.lock);
            if (callback != NULL && qm_agent_callback_enter(handle)) {
                callback(handle, request_id, tts_event, QM_EOK, user_ctx);
                qm_agent_callback_leave(handle);
            }
        }
        return;
    }

    if (event->id == QM_AGENT_EVENT_AGENT_ABORTED ||
        event->id == QM_AGENT_EVENT_DISCONNECTED) {
        tts_event = (event->id == QM_AGENT_EVENT_AGENT_ABORTED) ?
                    QM_AGENT_TTS_EVENT_CANCELLED : QM_AGENT_TTS_EVENT_FAILED;
        code = event->code != 0 ? event->code :
               ((tts_event == QM_AGENT_TTS_EVENT_CANCELLED) ? -QM_EINTR : -QM_EIO);
        qm_mutex_lock(&handle->tts_request.lock, QM_WAIT_FOREVER);
        request_id = handle->tts_request.request_id;
        terminal = handle->tts_request.active;
        qm_mutex_unlock(&handle->tts_request.lock);
        if (terminal) {
            qm_agent_tts_runtime_finish(handle, request_id, 0U, QM_FALSE,
                                        tts_event, code);
        }
    }
}

qm_agent_handle_t qm_agent_create(const qm_agent_config_t *config)
{
    qm_agent_handle_t handle;
    int ret;

    if (config == NULL) {
        return NULL;
    }
#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
    if (config->audio.control == NULL) {
        return NULL;
    }
#endif

    handle = (qm_agent_handle_t)qm_malloc(sizeof(*handle));
    if (handle == NULL) {
        return NULL;
    }

    qm_memset(handle, 0, sizeof(*handle));
    if (qm_mutex_new(&handle->network_lock) != QM_EOK) {
        qm_free(handle);
        return NULL;
    }
    handle->network_state = QM_AGENT_NETWORK_UP;
    qm_memcpy(&handle->config, config, sizeof(*config));
    handle->state = QM_AGENT_STATE_IDLE;

    /* Public notices use the process-wide default loop. Initialization only
     * creates that shared loop when necessary; it never creates an Agent task. */
    ret = qm_agent_event_runtime_init(handle);
    if (ret != QM_EOK) {
        goto fail;
    }

    ret = qm_agent_control_init(handle);
    if (ret != QM_EOK) {
        goto fail;
    }

    ret = qm_agent_tts_runtime_init(handle);
    if (ret != QM_EOK) {
        goto fail;
    }

#if defined(CONFIG_QM_AGENT_ENABLE_VIDEO) && CONFIG_QM_AGENT_ENABLE_VIDEO
    ret = qm_agent_video_engine_open(handle);
    if (ret != QM_EOK) {
        goto fail;
    }
#endif

    ret = qm_agent_module_manager_init(handle);
    if (ret != QM_EOK) {
        goto fail;
    }

    if (config->module_count > 0U && config->module_names != NULL) {
        uint32_t i;

        for (i = 0; i < config->module_count; i++) {
            ret = qm_agent_load_module(handle,
                                       config->module_names[i],
                                       (config->module_configs != NULL) ?
                                           config->module_configs[i] : NULL);
            if (ret != QM_EOK) {
                goto fail;
            }
        }
    }

    /* Subscribe to shared Audio only after all other fallible initialization.
     * No later create error can strand a callback on a discarded handle. */
#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
    ret = qm_agent_audio_engine_open(handle);
    if (ret != QM_EOK) {
        goto fail;
    }
#endif

    qm_agent_set_state(handle, QM_AGENT_STATE_READY);
    return handle;

fail:
    /* No create caller receives this handle. Close callback admission before
     * releasing any runtime that could still reference it. */
    (void)qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    handle->destroying = QM_TRUE;
    (void)qm_mutex_unlock(&handle->network_lock);
#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
    (void)qm_agent_audio_engine_close(handle);
#endif
    (void)qm_agent_control_deinit_wait(handle);
    qm_agent_module_manager_deinit(handle);
    qm_agent_auth_runtime_deinit(handle);
#if defined(CONFIG_QM_AGENT_ENABLE_VIDEO) && CONFIG_QM_AGENT_ENABLE_VIDEO
    (void)qm_agent_video_engine_close(handle);
#endif
    qm_agent_tts_runtime_deinit(handle);
    qm_agent_event_runtime_deinit(handle);
    qm_mutex_free(&handle->network_lock);
    qm_free(handle);
    return NULL;
}

uint32_t qm_agent_get_log_flags(void)
{
    return g_qm_agent_log_flags;
}

int qm_agent_set_log_flags(uint32_t log_flags)
{
    if ((log_flags & ~(uint32_t)QM_AGENT_LOG_ALL) != 0U) {
        return -QM_EINVAL;
    }

    g_qm_agent_log_flags = log_flags;
    return QM_EOK;
}

int qm_agent_load_module(qm_agent_handle_t handle,
                         const char *module_name,
                         const void *module_config)
{
    const qm_agent_module_desc_t *module_desc;

    if (handle == NULL || module_name == NULL) {
        return -QM_EINVAL;
    }

    if (handle->started) {
        return -QM_EBUSY;
    }

    module_desc = qm_agent_module_manager_find_desc(module_name);
    if (module_desc == NULL) {
        return -QM_ENOENT;
    }

    return qm_agent_module_manager_load(handle, module_desc, module_config);
}

/**
 * @brief Execute ordered session teardown through the control task.
 * 通过控制任务执行有序会话拆除。
 *
 * The function keeps a failed teardown retryable because provider callback
 * barriers and audio cleanup must complete before freeing session memory.
 * 本函数使失败的拆除可以重试，因为供应商回调屏障和音频清理必须在释放会话内存前完成。
 */
int qm_agent_destroy(qm_agent_handle_t handle)
{
    qm_agent_control_message_t message;
    int ret;

    if (handle == NULL) {
        return QM_EOK;
    }

    ret = qm_agent_lifecycle_begin(handle, QM_TRUE);
    if (ret != QM_EOK) return ret;
    /* Destruction is intentionally resumable.  A later resource (for
     * example, an audio callback barrier) may fail after the control task has
     * already exited; retrying must not submit a command to that old queue. */
    if (handle->control.task_started) {
        qm_bool shutdown_requested;

        (void)qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
        shutdown_requested = handle->control.shutdown_requested;
        (void)qm_mutex_unlock(&handle->network_lock);
        qm_memset(&message, 0, sizeof(message));
        message.type = QM_AGENT_CONTROL_DESTROY;
        if (!shutdown_requested) {
            ret = qm_agent_control_call(handle, &message);
            if (ret != QM_EOK) goto retry_destroy;
        }
        ret = qm_agent_control_deinit(handle);
        if (ret != QM_EOK) goto retry_destroy;
    }

#if defined(CONFIG_QM_AGENT_ENABLE_VIDEO) && CONFIG_QM_AGENT_ENABLE_VIDEO
    /* A video callback uses transport_ctx synchronously.  Quiesce the driver
     * before transport_deinit() can release that context. */
    ret = qm_agent_video_engine_close(handle);
    if (ret != QM_EOK) goto retry_destroy;
#endif

#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
    /* Capture and lifecycle callbacks can borrow transport_ctx.  Detach them
     * before transport_deinit() releases the provider-owned context. */
    ret = qm_agent_audio_engine_close(handle);
    if (ret != QM_EOK) {
        goto retry_destroy;
    }
#endif

    ret = transport_deinit(handle);
    if (ret != QM_EOK) {
        QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_LIFECYCLE,
                "destroy deferred: transport still active ret=%d", ret);
        goto retry_destroy;
    }
    ret = qm_agent_auth_runtime_deinit(handle);
    if (ret != QM_EOK) goto retry_destroy;
    ret = qm_agent_tts_runtime_deinit(handle);
    if (ret != QM_EOK) goto retry_destroy;
    qm_agent_module_manager_deinit(handle);
    qm_agent_event_runtime_deinit(handle);
    qm_mutex_free(&handle->network_lock);
    qm_free(handle);
    return QM_EOK;

retry_destroy:
    qm_agent_lifecycle_end(handle);
    return ret;
}

static int qm_agent_start_now(qm_agent_handle_t handle)
{
    const qm_agent_transport_if_t *transport;
    int ret;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    if (handle->started) {
        if (handle->state == QM_AGENT_STATE_STOPPING) {
            QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_LIFECYCLE,
                    "start rejected: previous stop is incomplete");
            return -QM_EBUSY;
        }
        return QM_EOK;
    }

    ret = qm_agent_prepare_transport(handle);
    if (ret != QM_EOK) {
        QM_AGENT_LOGE(LOG_TAG,
                "prepare transport failed ret=%d transport=%d auth_type=%d has_auth=%d",
                ret,
                handle->config.transport.type,
                handle->auth.info.type,
                handle->auth.has_auth);
        qm_agent_set_state(handle, QM_AGENT_STATE_ERROR);
        return ret;
    }

    transport = handle->config.transport_if;
    if (transport == NULL || transport->start == NULL) {
        QM_AGENT_LOGE(LOG_TAG, "transport start op missing transport=%d", handle->config.transport.type);
        return -QM_ENOSYS;
    }

    /* Local consumers must be ready before start can synchronously report
     * CONNECTED or AUDIO. started also records partial-start cleanup duty. */
    handle->started = QM_TRUE;
    qm_agent_set_state(handle, QM_AGENT_STATE_STARTING);
#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
    ret = qm_agent_audio_engine_start(handle);
    if (ret != QM_EOK) {
        QM_AGENT_LOGE(LOG_TAG, "audio engine start failed ret=%d", ret);
        goto fail;
    }
#endif

#if defined(CONFIG_QM_AGENT_ENABLE_VIDEO) && CONFIG_QM_AGENT_ENABLE_VIDEO
    ret = qm_agent_video_engine_start(handle);
    if (ret != QM_EOK) {
        QM_AGENT_LOGE(LOG_TAG, "video engine start failed ret=%d", ret);
        goto fail;
    }
#endif

    ret = qm_agent_module_manager_start(handle);
    if (ret != QM_EOK) {
        QM_AGENT_LOGE(LOG_TAG, "module manager start failed ret=%d", ret);
        goto fail;
    }

    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    handle->network_forwarding = QM_TRUE;
    if (transport->network_update != NULL) {
        ret = transport->network_update(handle->transport_ctx, handle->network_state);
    }
    qm_mutex_unlock(&handle->network_lock);
    if (ret != QM_EOK) goto fail;
    ret = transport->start(handle->transport_ctx);
    if (ret != QM_EOK) {
        QM_AGENT_LOGE(LOG_TAG, "transport start failed ret=%d transport=%d",
                      ret, handle->config.transport.type);
        goto fail;
    }

    {
        qm_agent_module_event_t event;

        qm_memset(&event, 0, sizeof(event));
        event.id = QM_AGENT_MODULE_EVENT_AGENT_STARTED;
        (void)qm_agent_module_manager_emit(handle, &event);
    }

    return QM_EOK;

fail:
    /* Keep STOPPING and ownership intact if rollback cannot finish. */
    if (qm_agent_stop_now(handle) == QM_EOK) {
        qm_agent_set_state(handle, QM_AGENT_STATE_ERROR);
    }
    return ret;
}

static int start_session(qm_agent_handle_t handle)
{
    qm_agent_state_t state;
    int ret;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    if (handle->auth.helper == NULL) {
        ret = qm_agent_auth_runtime_init(handle);
        if (ret != QM_EOK) return ret;
    }
    handle->desired_running = QM_TRUE;
    state = qm_agent_get_state(handle);
    if (handle->started && state == QM_AGENT_STATE_STOPPING) {
        /* A transport stop may time out while its worker is still unwinding.
         * Retry that stop before touching auth or starting a new transport;
         * returning success here would leave the cloud-recovery path with no
         * live connection and could create two workers on the next retry. */
        ret = qm_agent_stop_now(handle);
        if (ret != QM_EOK) {
            QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_LIFECYCLE,
                    "start recovery deferred: stop incomplete ret=%d", ret);
            if (handle->auth.helper != NULL) {
                (void)qm_agent_schedule_reconcile_after(handle,
                                                        QM_AGENT_RECONCILE_RETRY_DELAY_MS);
            }
            return ret;
        }
    }

    if (qm_agent_uses_auto_auth(handle) && !handle->auth.has_auth) {
        qm_agent_set_state(handle, QM_AGENT_STATE_STARTING);
        ret = qm_agent_schedule_reconcile(handle);
        if (ret != QM_EOK) {
            QM_AGENT_LOGE(LOG_TAG, "reconcile schedule on start failed ret=%d", ret);
        }
        return ret;
    }

    return qm_agent_start_now(handle);
}

int qm_agent_start(qm_agent_handle_t handle)
{
    qm_agent_control_message_t message;

    if (handle == NULL) return -QM_EINVAL;
    qm_memset(&message, 0, sizeof(message));
    message.type = QM_AGENT_CONTROL_START;
    return qm_agent_control_call(handle, &message);
}

static int qm_agent_stop_now(qm_agent_handle_t handle)
{
    const qm_agent_transport_if_t *transport;
    int ret = QM_EOK;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    if (!handle->started) {
        return QM_EOK;
    }

    if (handle->state != QM_AGENT_STATE_STOPPING) {
        qm_agent_module_event_t event;

        qm_agent_set_state(handle, QM_AGENT_STATE_STOPPING);
        qm_agent_tts_runtime_cancel_active(handle,
                                           QM_AGENT_TTS_EVENT_CANCELLED,
                                           -QM_EINTR);
        qm_memset(&event, 0, sizeof(event));
        event.id = QM_AGENT_MODULE_EVENT_AGENT_STOPPING;
        (void)qm_agent_module_manager_emit(handle, &event);
    }
    ret = qm_agent_module_manager_stop(handle);
    if (ret != QM_EOK) {
        return ret;
    }
#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
    ret = qm_agent_audio_engine_stop(handle);
    if (ret != QM_EOK) {
        return ret;
    }
#endif
#if defined(CONFIG_QM_AGENT_ENABLE_VIDEO) && CONFIG_QM_AGENT_ENABLE_VIDEO
    ret = qm_agent_video_engine_stop(handle);
    if (ret != QM_EOK) {
        return ret;
    }
#endif
    transport = handle->config.transport_if;
    if (transport && transport->stop) {
        /* Wait for any admitted network notification, then close that path.
         * A transport stop may synchronously emit application events. */
        qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
        handle->network_forwarding = QM_FALSE;
        qm_mutex_unlock(&handle->network_lock);
        ret = transport->stop(handle->transport_ctx);
    }

    if (ret != QM_EOK) {
        /* Do not advertise READY while the transport task may still own
         * sockets or callbacks.  A subsequent start must wait for this stop
         * to complete instead of creating a second transport task. */
        QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_LIFECYCLE,
                "transport stop incomplete ret=%d; keep agent stopping", ret);
        return ret;
    }

    handle->started = QM_FALSE;
    qm_agent_set_state(handle, QM_AGENT_STATE_READY);
    return ret;
}

int qm_agent_stop(qm_agent_handle_t handle)
{
    qm_agent_control_message_t message;

    if (handle == NULL) {
        return -QM_EINVAL;
    }
    qm_memset(&message, 0, sizeof(message));
    message.type = QM_AGENT_CONTROL_STOP;
    return qm_agent_control_call(handle, &message);
}

/**
 * @brief Admit one borrowed capture frame while holding transport readiness stable.
 * 在保持传输就绪状态稳定时准入一个借用的采集帧。
 *
 * The network lock prevents stop/deinit from invalidating transport_ctx until
 * the adapter has consumed or copied data.
 * network_lock 防止 stop/deinit 在适配器消费或复制 data 前使 transport_ctx 失效。
 */
int qm_agent_transport_send_audio(qm_agent_handle_t handle, const void *data, uint32_t len)
{
    const qm_agent_transport_if_t *transport;
    qm_bool enabled;
    int ret;

    if (handle == NULL || data == NULL || len == 0) {
        return -QM_EINVAL;
    }

    /* Wait for the lock instead of dropping the frame. Holding it across send
     * keeps transport teardown behind this call. */
    if (qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER) != QM_EOK) {
        return -QM_EBUSY;
    }
    if (handle->destroying || !handle->network_forwarding ||
        handle->transport_ctx == NULL || !handle->audio.runtime_valid) {
        qm_mutex_unlock(&handle->network_lock);
        return -QM_EBUSY;
    }
    enabled = qm_agent_audio_engine_upload_allowed(handle);
    transport = handle->config.transport_if;
    ret = !enabled ? -QM_EBUSY :
          (transport == NULL || transport->send_audio == NULL) ? -QM_ENOSYS :
          transport->send_audio(handle->transport_ctx, data, len);
    qm_mutex_unlock(&handle->network_lock);
    return ret;
}

int qm_agent_transport_send_video(qm_agent_handle_t handle,
                               const void *data,
                               uint32_t len,
                               const qm_agent_video_format_t *format)
{
    const qm_agent_transport_if_t *transport;
    const qm_agent_video_format_t *frame_format;
    int ret;

    if (handle == NULL || data == NULL || len == 0) {
        return -QM_EINVAL;
    }

    /* Video drivers push from their own producer context.  Admission is
     * bounded and the lock is held through provider consumption so transport
     * deinit cannot release transport_ctx while send_video() is using it. */
    if (qm_mutex_lock(&handle->network_lock, QM_NO_WAIT) != QM_EOK) {
        return -QM_EBUSY;
    }

    if (!handle->started || handle->destroying ||
        handle->state == QM_AGENT_STATE_STOPPING ||
        !handle->network_forwarding || handle->transport_ctx == NULL ||
        !handle->video.running || !handle->video.transport_ready) {
        qm_mutex_unlock(&handle->network_lock);
        return -QM_EBUSY;
    }

    transport = handle->config.transport_if;
    if (transport == NULL || transport->send_video == NULL) {
        qm_mutex_unlock(&handle->network_lock);
        return -QM_ENOSYS;
    }

    frame_format = format ? format : &handle->config.input_video;
    ret = transport->send_video(handle->transport_ctx, data, len, frame_format);
    qm_mutex_unlock(&handle->network_lock);
    return ret;
}

static int qm_agent_send_text_now(qm_agent_handle_t handle, const char *text)
{
    const qm_agent_transport_if_t *transport;

    if (handle == NULL || text == NULL) {
        return -QM_EINVAL;
    }

    if (!transport_available(handle)) {
        return -QM_EBUSY;
    }

    transport = handle->config.transport_if;
    if (transport == NULL || transport->send_text == NULL) {
        return -QM_ENOSYS;
    }

    return transport->send_text(handle->transport_ctx, text);
}

int qm_agent_send_text(qm_agent_handle_t handle, const char *text)
{
    qm_agent_control_message_t message;

    if (handle == NULL || text == NULL) return -QM_EINVAL;
    qm_memset(&message, 0, sizeof(message));
    message.type = QM_AGENT_CONTROL_SEND_TEXT;
    message.data.text = text;
    return qm_agent_control_call(handle, &message);
}

static int tts_request(qm_agent_handle_t handle,
                         const qm_agent_tts_request_t *request,
                         uint32_t *request_id)
{
    const qm_agent_transport_if_t *transport;
    char *text;
    uint32_t id;
    uint32_t generation = 0U;
    qm_utils_time_t deadline;
    int ret;

    if (handle == NULL || request == NULL || request->text == NULL ||
        request->text[0] == '\0' || !handle->tts_request.lock_valid ||
        request->timeout_ms >= UINT32_MAX / 2U) {
        return -QM_EINVAL;
    }
    if (!transport_available(handle)) {
        return -QM_EBUSY;
    }
    transport = handle->config.transport_if;
    if (transport == NULL || transport->request_tts == NULL) {
        return -QM_ENOSYS;
    }

    text = qm_strdup(request->text);
    if (text == NULL) {
        return -QM_ENOMEM;
    }

    qm_mutex_lock(&handle->tts_request.lock, QM_WAIT_FOREVER);
    if (handle->tts_request.active) {
        qm_mutex_unlock(&handle->tts_request.lock);
        qm_free(text);
        return -QM_EBUSY;
    }
    id = ++handle->tts_request.next_id;
    if (id == 0U) {
        id = ++handle->tts_request.next_id;
    }
    handle->tts_request.active = QM_TRUE;
    handle->tts_request.started = QM_FALSE;
    handle->tts_request.request_id = id;
    handle->tts_request.generation = 0U;
    handle->tts_request.callback = request->callback;
    handle->tts_request.user_ctx = request->user_ctx;
    qm_mutex_unlock(&handle->tts_request.lock);
    qm_utils_time_countdown_ms(&deadline, request->timeout_ms);

#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
    /* Every active request owns a fresh local generation. */
    ret = qm_agent_audio_engine_abort_stream(handle, QM_FALSE);
    if (ret != QM_EOK) {
        qm_free(text);
        qm_agent_tts_runtime_finish(handle, id, 0U, QM_FALSE,
                                    QM_AGENT_TTS_EVENT_FAILED, ret);
        return ret;
    }
    generation = qm_agent_audio_engine_stream_generation(handle);

    /* Admit the PUSH operation before asking the transport for TTS data.  A
     * provider may deliver its first frame from inside request_tts(); that
     * callback must therefore only perform a bounded stream write and never
     * synchronously wait for the Agent control task. */
    ret = qm_agent_audio_engine_begin_stream(
        handle, &handle->audio.transport_output_format);
    if (ret != QM_EOK) {
        (void)qm_agent_audio_engine_abort_stream(handle, QM_TRUE);
        qm_free(text);
        qm_agent_tts_runtime_finish(handle, id, generation, QM_FALSE,
                                    QM_AGENT_TTS_EVENT_FAILED, ret);
        return ret;
    }
    /* begin_stream advances the audio generation for this operation. */
    generation = qm_agent_audio_engine_stream_generation(handle);
#endif
    qm_mutex_lock(&handle->tts_request.lock, QM_WAIT_FOREVER);
    handle->tts_request.generation = generation;
    qm_mutex_unlock(&handle->tts_request.lock);

    ret = transport->request_tts(handle->transport_ctx, text);
    /* The transport copies any text it retains. A synchronous callback may
     * finish this request, but cannot free the argument still in use here. */
    qm_free(text);
    if (ret != QM_EOK) {
#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
        (void)qm_agent_audio_engine_abort_stream(handle, QM_TRUE);
#endif
        qm_agent_tts_runtime_finish(handle, id, generation, QM_FALSE,
                                    QM_AGENT_TTS_EVENT_FAILED, ret);
        return ret;
    }

    if (request->timeout_ms != 0U) {
        qm_mutex_lock(&handle->tts_request.lock, QM_WAIT_FOREVER);
        if (handle->tts_request.active && handle->tts_request.request_id == id) {
            qm_mutex_unlock(&handle->tts_request.lock);
            ret = qm_agent_control_schedule(handle,
                                            QM_AGENT_CONTROL_TTS_TIMEOUT,
                                            qm_utils_time_left(&deadline),
                                            id,
                                            generation);
        } else {
            ret = QM_EOK;
            qm_mutex_unlock(&handle->tts_request.lock);
        }
        if (ret != QM_EOK) {
#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
            (void)qm_agent_audio_engine_abort_stream(handle, QM_TRUE);
#endif
            qm_agent_tts_runtime_finish(handle, id, generation, QM_FALSE,
                                        QM_AGENT_TTS_EVENT_FAILED, ret);
            return ret;
        }
    }

    if (request_id != NULL) {
        *request_id = id;
    }
    return QM_EOK;
}

int qm_agent_tts_request(qm_agent_handle_t handle,
                         const qm_agent_tts_request_t *request,
                         uint32_t *request_id)
{
    qm_agent_control_message_t message;

    if (handle == NULL) return -QM_EINVAL;
    qm_memset(&message, 0, sizeof(message));
    message.type = QM_AGENT_CONTROL_TTS_REQUEST;
    message.data.tts_request.request = request;
    message.data.tts_request.request_id = request_id;
    return qm_agent_control_call(handle, &message);
}

static int tts_cancel(qm_agent_handle_t handle, uint32_t request_id)
{
    qm_bool active;

    if (handle == NULL || request_id == 0U || !handle->tts_request.lock_valid) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&handle->tts_request.lock, QM_WAIT_FOREVER);
    active = handle->tts_request.active &&
             handle->tts_request.request_id == request_id;
    qm_mutex_unlock(&handle->tts_request.lock);
    if (!active) {
        return -QM_ENOENT;
    }

#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
    /* Input closes immediately; retained Audio cleanup must not prevent the
     * request cancellation or its remote interrupt. */
    if (qm_agent_audio_engine_abort_stream(handle, QM_TRUE) != QM_EOK) {
        QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_CONTROL,
                      "TTS cancelled with audio cleanup pending");
    }
#endif
    qm_agent_tts_runtime_finish(handle, request_id, 0U, QM_FALSE,
                                QM_AGENT_TTS_EVENT_CANCELLED, -QM_EINTR);
    if (transport_available(handle)) {
        const qm_agent_transport_if_t *transport = handle->config.transport_if;
        if (transport != NULL && transport->interrupt != NULL) {
            (void)transport->interrupt(handle->transport_ctx);
        }
    }
    return QM_EOK;
}

int qm_agent_tts_cancel(qm_agent_handle_t handle, uint32_t request_id)
{
    qm_agent_control_message_t message;

    if (handle == NULL) return -QM_EINVAL;
    qm_memset(&message, 0, sizeof(message));
    message.type = QM_AGENT_CONTROL_TTS_CANCEL;
    message.data.tts_cancel.request_id = request_id;
    return qm_agent_control_call(handle, &message);
}

int qm_agent_send_audio(qm_agent_handle_t handle,
                        const void *data,
                        uint32_t len)
{
    if (handle == NULL || data == NULL || len == 0U) return -QM_EINVAL;
    if (handle->config.audio.capture_enabled) return -QM_EBUSY;
    return qm_agent_transport_send_audio(handle, data, len);
}

static int qm_agent_asr_start_now(qm_agent_handle_t handle, uint32_t timeout_ms)
{
    const qm_agent_transport_if_t *transport;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    if (!transport_available(handle)) {
        return -QM_EBUSY;
    }

    transport = handle->config.transport_if;
    if (transport == NULL || transport->asr_start_longform == NULL) {
        return -QM_ENOSYS;
    }

    return transport->asr_start_longform(handle->transport_ctx, timeout_ms);
}

static int qm_agent_asr_stop_now(qm_agent_handle_t handle, uint32_t timeout_ms)
{
    const qm_agent_transport_if_t *transport;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    if (!transport_available(handle)) {
        return -QM_EBUSY;
    }

    transport = handle->config.transport_if;
    if (transport == NULL || transport->asr_stop_longform == NULL) {
        return -QM_ENOSYS;
    }

    return transport->asr_stop_longform(handle->transport_ctx, timeout_ms);
}

static int qm_agent_asr_realtime_now(qm_agent_handle_t handle,
                                     qm_bool enabled,
                                     uint32_t timeout_ms)
{
    const qm_agent_transport_if_t *transport;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    if (!transport_available(handle)) {
        return -QM_EBUSY;
    }

    transport = handle->config.transport_if;
    if (transport == NULL || transport->asr_set_realtime_enabled == NULL) {
        return -QM_ENOSYS;
    }

    return transport->asr_set_realtime_enabled(handle->transport_ctx,
                                               enabled,
                                               timeout_ms);
}

int qm_agent_asr_start_longform_recognition(qm_agent_handle_t handle,
                                            uint32_t timeout_ms)
{
    qm_agent_control_message_t message;

    if (handle == NULL) return -QM_EINVAL;
    qm_memset(&message, 0, sizeof(message));
    message.type = QM_AGENT_CONTROL_ASR_START;
    message.data.asr.timeout_ms = timeout_ms;
    return qm_agent_control_call(handle, &message);
}

int qm_agent_asr_stop_longform_recognition(qm_agent_handle_t handle,
                                           uint32_t timeout_ms)
{
    qm_agent_control_message_t message;

    if (handle == NULL) return -QM_EINVAL;
    qm_memset(&message, 0, sizeof(message));
    message.type = QM_AGENT_CONTROL_ASR_STOP;
    message.data.asr.timeout_ms = timeout_ms;
    return qm_agent_control_call(handle, &message);
}

int qm_agent_asr_set_realtime_enabled(qm_agent_handle_t handle,
                                      qm_bool enabled,
                                      uint32_t timeout_ms)
{
    qm_agent_control_message_t message;

    if (handle == NULL) return -QM_EINVAL;
    qm_memset(&message, 0, sizeof(message));
    message.type = QM_AGENT_CONTROL_ASR_REALTIME;
    message.data.asr.enabled = enabled;
    message.data.asr.timeout_ms = timeout_ms;
    return qm_agent_control_call(handle, &message);
}

int qm_agent_send_video(qm_agent_handle_t handle,
                     const void *data,
                     uint32_t len,
                     const qm_agent_video_format_t *format)
{
#if !defined(CONFIG_QM_AGENT_ENABLE_VIDEO) || !CONFIG_QM_AGENT_ENABLE_VIDEO
    (void)handle;
    (void)data;
    (void)len;
    (void)format;
    return -QM_ENOSYS;
#else
    return qm_agent_transport_send_video(handle, data, len, format);
#endif
}

static int interrupt_session(qm_agent_handle_t handle)
{
    const qm_agent_transport_if_t *transport;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    if (!transport_available(handle)) {
        return -QM_EBUSY;
    }

    transport = handle->config.transport_if;
    if (transport == NULL || transport->interrupt == NULL) {
        return -QM_ENOSYS;
    }

#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
    /* The next dialog owns a new local TTS generation. Do this before the
     * backend interrupt so delayed PCM cannot re-enter the old session.
     * Cleanup retries independently if an Audio callback has not exited. */
    if (qm_agent_audio_engine_abort_stream(handle, QM_TRUE) != QM_EOK) {
        QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_CONTROL,
                      "session interrupted with audio cleanup pending");
    }
    qm_agent_audio_engine_set_dialog_state(handle, QM_AGENT_DIALOG_STATE_SILENT);
#endif
    qm_agent_tts_runtime_cancel_active(handle, QM_AGENT_TTS_EVENT_CANCELLED,
                                       -QM_EINTR);
    return transport->interrupt(handle->transport_ctx);
}

int qm_agent_interrupt(qm_agent_handle_t handle)
{
    qm_agent_control_message_t message;

    if (handle == NULL) return -QM_EINVAL;
    qm_memset(&message, 0, sizeof(message));
    message.type = QM_AGENT_CONTROL_INTERRUPT;
    return qm_agent_control_call(handle, &message);
}

#if !defined(CONFIG_QM_AGENT_ENABLE_AUDIO) || !CONFIG_QM_AGENT_ENABLE_AUDIO
void qm_agent_set_upload_enabled(qm_agent_handle_t handle, qm_bool enabled)
{
    (void)handle;
    (void)enabled;
}
#endif

int qm_agent_copy_resource_token(qm_agent_handle_t handle, char *token,
                                  uint32_t capacity)
{
    qm_agent_control_message_t message = {0};

    if (handle == NULL || token == NULL || capacity == 0U) return -QM_EINVAL;
    token[0] = '\0';
    message.type = QM_AGENT_CONTROL_COPY_RESOURCE_TOKEN;
    message.data.resource_token.buffer = token;
    message.data.resource_token.capacity = capacity;
    return qm_agent_control_call(handle, &message);
}

qm_agent_state_t qm_agent_get_state(qm_agent_handle_t handle)
{
    qm_agent_state_t state;

    if (handle == NULL) {
        return QM_AGENT_STATE_NONE;
    }

    (void)qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    state = handle->state;
    (void)qm_mutex_unlock(&handle->network_lock);
    return state;
}

static int qm_agent_network_update_now(qm_agent_handle_t handle,
                                       qm_agent_network_state_t state)
{
    const qm_agent_transport_if_t *transport;
    int ret = QM_EOK;

    if (handle == NULL || (state != QM_AGENT_NETWORK_DOWN && state != QM_AGENT_NETWORK_UP)) {
        return -QM_EINVAL;
    }
    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    handle->network_state = state;
    if (handle->network_forwarding) {
        transport = handle->config.transport_if;
        if (transport != NULL && transport->network_update != NULL) {
            ret = transport->network_update(handle->transport_ctx, state);
        }
    }
    qm_mutex_unlock(&handle->network_lock);
    return ret;
}

int qm_agent_network_update(qm_agent_handle_t handle, qm_agent_network_state_t state)
{
    qm_agent_control_message_t message;

    if (handle == NULL || (state != QM_AGENT_NETWORK_DOWN &&
                           state != QM_AGENT_NETWORK_UP)) {
        return -QM_EINVAL;
    }
    qm_memset(&message, 0, sizeof(message));
    message.type = QM_AGENT_CONTROL_NETWORK_UPDATE;
    message.data.network_state = state;
    return qm_agent_control_call(handle, &message);
}

static void qm_agent_transport_event_now(const qm_agent_event_t *event,
                                         qm_agent_handle_t handle)
{
    if (handle == NULL || event == NULL) {
        return;
    }

    /**
     * Transport events are the point where Core applies policy: downstream
     * audio is played and local upload is gated while the agent is speaking.
     * transport 事件是 Core 应用策略的边界：下行音频在此播放，Agent 说话期间本地上行也在此门控。
     */
    switch (event->id) {
        case QM_AGENT_EVENT_COMMAND: {
            /* The control queue already owns a full clone. Keep the public
             * notice bounded, but deliver the complete document to modules.
             * 控制队列已持有完整副本；模块接收全文，公共通知仍保持有界。 */
            qm_agent_module_event_t command = {
                .id = QM_AGENT_MODULE_EVENT_COMMAND,
                .code = event->code,
                .data = event,
                .data_len = sizeof(*event),
            };
            int ret = qm_agent_module_manager_emit(handle, &command);
            if (ret != QM_EOK) {
                QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_CONTROL,
                              "device command handler failed ret=%d", ret);
            }
            break;
        }
        case QM_AGENT_EVENT_CONNECTED:
            if (handle->started && handle->state != QM_AGENT_STATE_STOPPING) {
#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
                qm_agent_audio_engine_set_transport_ready(handle, QM_TRUE);
                qm_agent_audio_engine_set_dialog_state(handle, QM_AGENT_DIALOG_STATE_SILENT);
#endif
#if defined(CONFIG_QM_AGENT_ENABLE_VIDEO) && CONFIG_QM_AGENT_ENABLE_VIDEO
                qm_agent_video_engine_set_transport_ready(handle, QM_TRUE);
#endif
                qm_agent_set_state(handle, QM_AGENT_STATE_RUNNING);
            }
            break;
        case QM_AGENT_EVENT_DISCONNECTED:
            if (handle->started && handle->state != QM_AGENT_STATE_STOPPING) {
#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
                qm_agent_audio_engine_set_transport_ready(handle, QM_FALSE);
                qm_agent_audio_engine_set_dialog_state(handle, QM_AGENT_DIALOG_STATE_SILENT);
#endif
#if defined(CONFIG_QM_AGENT_ENABLE_VIDEO) && CONFIG_QM_AGENT_ENABLE_VIDEO
                qm_agent_video_engine_set_transport_ready(handle, QM_FALSE);
#endif
                qm_agent_set_state(handle, QM_AGENT_STATE_ERROR);
            }
            break;
        case QM_AGENT_EVENT_AUDIO:
#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
        {
            int ret;

            /* TTS requests admit their stream before calling the provider.
             * A transport such as RTC can still start a downlink without a
             * TTS_BEGIN marker; request admission asynchronously and never
             * block this high-rate callback on the control task.
             * TTS 请求会在调用供应商前准入其流。RTC 等传输仍可能不带 TTS_BEGIN 标记便开始
             * 下行，因此异步请求准入，绝不能在这个高频回调中等待控制任务。 */
            if (!qm_agent_audio_engine_stream_active(handle)) {
                ret = qm_agent_audio_engine_request_stream_begin(
                    handle, &event->data.audio.format);
                if (ret != QM_EOK && ret != -QM_EBUSY) {
                    QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_DOWNLINK,
                                  "audio stream admission post failed ret=%d", ret);
                }
                break;
            }

            ret = qm_agent_audio_engine_enqueue_transport(
                handle,
                event->data.audio.data,
                event->data.audio.len,
                &event->data.audio.format);
            if (ret < 0 && ret != -QM_EINTR) {
                QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_DOWNLINK,
                        "audio enqueue failed ret=%d len=%u codec=%d",
                        ret,
                        (unsigned int)event->data.audio.len,
                        (int)event->data.audio.format.codec);
            }
        }
#endif
            break;
        case QM_AGENT_EVENT_TTS_INPUT_BEGIN:
#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
            if (handle->started && handle->state == QM_AGENT_STATE_RUNNING) {
                int ret = qm_agent_audio_engine_begin_reply(handle, &event->data.audio.format);
                if (ret != QM_EOK) {
                    QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_DOWNLINK,
                                  "audio begin rejected ret=%d", ret);
                }
            }
#endif
            break;
        case QM_AGENT_EVENT_TTS_INPUT_COMPLETE:
#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
            (void)qm_agent_audio_engine_finish_stream_input(handle);
#endif
            break;
        case QM_AGENT_EVENT_TTS_PLAYBACK_COMPLETE:
            break;
        case QM_AGENT_EVENT_AGENT_ABORTED:
#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
            if (qm_agent_audio_engine_abort_stream(handle, QM_FALSE) != QM_EOK) {
                QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_CONTROL,
                              "audio abort after agent interrupt failed");
            }
            qm_agent_audio_engine_set_dialog_state(handle, QM_AGENT_DIALOG_STATE_SILENT);
#endif
            break;
        case QM_AGENT_EVENT_VIDEO:
        case QM_AGENT_EVENT_DOA:
            break;
        case QM_AGENT_EVENT_AGENT_STATE:
#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
            qm_agent_audio_engine_set_dialog_state(handle, event->data.agent_state.state);
#endif
            break;
        default:
            break;
    }

    qm_agent_emit(handle, event);
}

/**
 * @brief Receive a normalized borrowed provider event at the qm_agent boundary.
 * 在 qm_agent 边界接收标准化的借用供应商事件。
 *
 * High-rate media stays on the data path; low-rate control events are cloned
 * and queued so provider callbacks do not execute blocking lifecycle work.
 * 高频媒体留在数据路径；低频控制事件会克隆并入队，避免供应商回调执行阻塞生命周期工作。
 */
void qm_agent_on_transport_event(const qm_agent_event_t *event, void *user_ctx)
{
    qm_agent_handle_t handle = (qm_agent_handle_t)user_ctx;
    qm_agent_event_t *copy;

    if (handle == NULL || event == NULL) return;

    /* Audio frames are the high-rate data plane and must never be copied into
     * the serialized control queue. The transport owns the borrow for this
     * callback, so process it before returning.
     * 音频帧属于高频数据平面，绝不能复制到串行控制队列。传输层只在本回调期间持有借用数据，
     * 因此必须在返回前处理。 */
    if (event->id == QM_AGENT_EVENT_AUDIO) {
        if (!qm_agent_callback_enter(handle)) return;
        qm_agent_transport_event_now(event, handle);
        qm_agent_callback_leave(handle);
        return;
    }

    if (!qm_agent_callback_enter(handle)) return;

    if (qm_agent_control_is_current(handle)) {
        qm_agent_transport_event_now(event, handle);
        qm_agent_callback_leave(handle);
        return;
    }

    copy = (qm_agent_event_t *)qm_malloc(sizeof(*copy));
    if (copy == NULL || qm_agent_event_clone(copy, event) != QM_EOK) {
        if (copy != NULL) qm_free(copy);
        QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_CONTROL,
                "drop transport event id=%d: clone failed", (int)event->id);
        qm_agent_callback_leave(handle);
        return;
    }
    if (qm_agent_control_post_event(handle, copy) != QM_EOK) {
        /* post_event releases copy on failure. post_event 在失败时释放 copy。 */
        QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_CONTROL,
                "drop transport event id=%d: control queue full", (int)event->id);
    }
    qm_agent_callback_leave(handle);
}

int qm_agent_control_execute(qm_agent_handle_t handle,
                             const qm_agent_control_message_t *message)
{
    if (handle == NULL || message == NULL) return -QM_EINVAL;

    switch (message->type) {
        case QM_AGENT_CONTROL_WAKE:
            return QM_EOK;
        case QM_AGENT_CONTROL_START:
            return start_session(handle);
        case QM_AGENT_CONTROL_STOP:
            handle->desired_running = QM_FALSE;
            qm_agent_auth_cancel(handle->auth.helper);
            return qm_agent_stop_now(handle);
        case QM_AGENT_CONTROL_DESTROY:
            handle->desired_running = QM_FALSE;
            qm_agent_auth_cancel(handle->auth.helper);
            return qm_agent_stop_now(handle);
        case QM_AGENT_CONTROL_SEND_TEXT:
            return qm_agent_send_text_now(handle, message->data.text);
        case QM_AGENT_CONTROL_TTS_REQUEST:
            return tts_request(handle,
                               message->data.tts_request.request,
                               message->data.tts_request.request_id);
        case QM_AGENT_CONTROL_TTS_CANCEL:
            return tts_cancel(handle, message->data.tts_cancel.request_id);
        case QM_AGENT_CONTROL_ASR_START:
            return qm_agent_asr_start_now(handle, message->data.asr.timeout_ms);
        case QM_AGENT_CONTROL_ASR_STOP:
            return qm_agent_asr_stop_now(handle, message->data.asr.timeout_ms);
        case QM_AGENT_CONTROL_ASR_REALTIME:
            return qm_agent_asr_realtime_now(handle,
                                             message->data.asr.enabled,
                                             message->data.asr.timeout_ms);
        case QM_AGENT_CONTROL_INTERRUPT:
            return interrupt_session(handle);
        case QM_AGENT_CONTROL_NETWORK_UPDATE:
            return qm_agent_network_update_now(handle,
                                               message->data.network_state);
        case QM_AGENT_CONTROL_TRANSPORT_EVENT:
            if (message->data.event == NULL) return -QM_EINVAL;
            if (handle->destroying) return -QM_EBUSY;
            qm_agent_transport_event_now(message->data.event, handle);
            return QM_EOK;
        case QM_AGENT_CONTROL_MODULE_REQUEST:
            if (message->data.module_request == NULL || handle->destroying) {
                return -QM_EBUSY;
            }
            return qm_agent_module_manager_dispatch_cloud_request(
                handle, message->data.module_request);
        case QM_AGENT_CONTROL_MODULE_EVENT:
            if (handle->destroying) {
                return -QM_EBUSY;
            }
            return qm_agent_module_manager_emit_cloud_event(
                handle, message->data.module_event);
        case QM_AGENT_CONTROL_AUDIO_BEGIN:
#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
            if (message->data.audio_begin.generation !=
                qm_agent_audio_engine_stream_generation(handle)) return QM_EOK;
            return qm_agent_audio_engine_begin_stream(handle,
                                                   &message->data.audio_begin.format);
#else
            return -QM_ENOSYS;
#endif
        case QM_AGENT_CONTROL_AUDIO_RECONCILE:
#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
            return qm_agent_audio_engine_reconcile(handle);
#else
            return QM_EOK;
#endif
        case QM_AGENT_CONTROL_AUDIO_EVENT:
#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
            return qm_agent_audio_engine_handle_event(handle,
                                                      &message->data.audio_event);
#else
            return -QM_ENOSYS;
#endif
        case QM_AGENT_CONTROL_AUDIO_INPUT_ERROR:
#if defined(CONFIG_QM_AGENT_ENABLE_AUDIO) && CONFIG_QM_AGENT_ENABLE_AUDIO
            return qm_agent_audio_engine_fail_stream(handle, message->data.audio_error.code,
                                                       message->data.audio_error.generation);
#else
            return -QM_ENOSYS;
#endif
        case QM_AGENT_CONTROL_RECONCILE:
            qm_agent_reconcile(handle);
            return QM_EOK;
        case QM_AGENT_CONTROL_COPY_RESOURCE_TOKEN: {
            const char *token = handle->auth.info.ljyd.token;
            uint32_t length;
            if (!handle->auth.has_auth || token == NULL) return -QM_ENOENT;
            length = qm_strlen(token) + 1U;
            if (length > message->data.resource_token.capacity) return -QM_EFULL;
            qm_memcpy(message->data.resource_token.buffer, token, length);
            return QM_EOK;
        }
        case QM_AGENT_CONTROL_AUTH_RETRY:
            if (handle->desired_running && !handle->destroying &&
                handle->auth.helper != NULL) {
                int ret = qm_agent_auth_retry_now(
                    handle->auth.helper, message->data.asr.timeout_ms);
                if (ret != QM_EOK && ret != -QM_EBUSY) {
                    qm_agent_set_state(handle, QM_AGENT_STATE_ERROR);
                }
                return ret;
            }
            return QM_EOK;
        case QM_AGENT_CONTROL_TTS_TIMEOUT:
            qm_agent_tts_timeout(handle,
                                 message->data.tts_cancel.request_id,
                                 message->data.tts_cancel.generation);
            return QM_EOK;
        default:
            return -QM_EINVAL;
    }
}
