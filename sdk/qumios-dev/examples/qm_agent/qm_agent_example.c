#include "qm_config.h"
#include "qm_agent.h"
#include "qm_agent_example_audio.h"
#include "qm_event.h"
#include "qm_kernel.h"
#include "qm_log.h"
#include "qm_string.h"
#include "qm_wifi.h"

#define LOG_TAG "qm_agent_example"

#ifndef CONFIG_QM_AGENT_EXAMPLE_USE_CUSTOM_TRANSPORT
#define CONFIG_QM_AGENT_EXAMPLE_USE_CUSTOM_TRANSPORT 1
#endif

#ifndef CONFIG_QM_AGENT_EXAMPLE_USE_WIFI
#define CONFIG_QM_AGENT_EXAMPLE_USE_WIFI 1
#endif

#ifndef CONFIG_QM_AGENT_EXAMPLE_WIFI_SSID
#define CONFIG_QM_AGENT_EXAMPLE_WIFI_SSID "your_ssid"
#endif

#ifndef CONFIG_QM_AGENT_EXAMPLE_WIFI_PASSWORD
#define CONFIG_QM_AGENT_EXAMPLE_WIFI_PASSWORD "your_password"
#endif

#ifndef CONFIG_QM_AGENT_EXAMPLE_WIFI_RETRY_MAX_COUNT
#define CONFIG_QM_AGENT_EXAMPLE_WIFI_RETRY_MAX_COUNT 10
#endif

#ifndef CONFIG_QM_AGENT_EXAMPLE_START_TEXT
#define CONFIG_QM_AGENT_EXAMPLE_START_TEXT "hello qm_agent"
#endif

#ifndef CONFIG_QM_AGENT_EXAMPLE_TTS_TEXT
#define CONFIG_QM_AGENT_EXAMPLE_TTS_TEXT "qm_agent example is ready"
#endif

#ifndef CONFIG_QM_AGENT_EXAMPLE_JOYINSIDE_BOT_ID
#define CONFIG_QM_AGENT_EXAMPLE_JOYINSIDE_BOT_ID "your-bot-id"
#endif

#ifndef CONFIG_QM_AGENT_EXAMPLE_JOYINSIDE_ACCESS_TOKEN
#define CONFIG_QM_AGENT_EXAMPLE_JOYINSIDE_ACCESS_TOKEN "your-access-token"
#endif

#ifndef CONFIG_QM_AGENT_EXAMPLE_JOYINSIDE_UID
#define CONFIG_QM_AGENT_EXAMPLE_JOYINSIDE_UID "qm-agent-example-device"
#endif

#ifndef CONFIG_QM_AGENT_EXAMPLE_JOYINSIDE_CHAT_ROLE_CODE
#define CONFIG_QM_AGENT_EXAMPLE_JOYINSIDE_CHAT_ROLE_CODE ""
#endif

#ifndef CONFIG_QM_AGENT_EXAMPLE_JOYINSIDE_TIMEOUT_MS
#define CONFIG_QM_AGENT_EXAMPLE_JOYINSIDE_TIMEOUT_MS 5000
#endif

typedef struct {
    qm_agent_transport_event_cb_t event_cb;
    void *user_ctx;
    qm_bool started;
} qm_agent_example_transport_ctx_t;

static qm_agent_handle_t s_qm_agent_example;
static qm_agent_example_audio_t s_qm_agent_example_audio;
static qm_agent_example_transport_ctx_t s_example_transport_ctx;
static qm_bool s_qm_agent_example_initial_prompt_pending;
static qm_bool s_qm_agent_example_event_registered;
#if CONFIG_QM_AGENT_EXAMPLE_USE_WIFI
static int s_qm_agent_example_wifi_retry_count;
static qm_bool s_qm_agent_example_wifi_registered;
#endif

static int qm_agent_example_check(const char *name, int ret);
static void qm_agent_example_start_session(void);

static int qm_agent_example_ensure_audio(void)
{
    return qm_agent_example_audio_init(&s_qm_agent_example_audio);
}

static const char *qm_agent_example_state_name(qm_agent_state_t state)
{
    switch (state) {
        case QM_AGENT_STATE_IDLE:
            return "idle";
        case QM_AGENT_STATE_READY:
            return "ready";
        case QM_AGENT_STATE_STARTING:
            return "starting";
        case QM_AGENT_STATE_RUNNING:
            return "running";
        case QM_AGENT_STATE_STOPPING:
            return "stopping";
        case QM_AGENT_STATE_ERROR:
            return "error";
        case QM_AGENT_STATE_NONE:
        default:
            return "none";
    }
}

static const char *qm_agent_example_agent_state_name(qm_agent_dialog_state_t state)
{
    switch (state) {
        case QM_AGENT_DIALOG_STATE_SILENT:
            return "silent";
        case QM_AGENT_DIALOG_STATE_LISTENING:
            return "listening";
        case QM_AGENT_DIALOG_STATE_THINKING:
            return "thinking";
        case QM_AGENT_DIALOG_STATE_SPEAKING:
            return "speaking";
        case QM_AGENT_DIALOG_STATE_NONE:
        default:
            return "none";
    }
}

static void qm_agent_example_event_handler(void *event_handler_arg,
                                           qm_event_base_t event_base,
                                           int32_t event_id,
                                           void *event_data,
                                           size_t event_data_size)
{
    const qm_agent_event_notice_t *notice =
        (const qm_agent_event_notice_t *)event_data;

    QM_UNUSED_ARG(event_handler_arg);

    if (event_base != QM_AGENT_EVENT_BASE || notice == NULL ||
        event_data_size != sizeof(*notice)) {
        return;
    }

    switch ((qm_agent_event_id_t)event_id) {
        case QM_AGENT_EVENT_STATE_CHANGED:
            QM_LOGI(LOG_TAG,
                    "state %s -> %s",
                    qm_agent_example_state_name(notice->data.state.old_state),
                    qm_agent_example_state_name(notice->data.state.new_state));
            break;
        case QM_AGENT_EVENT_CONNECTED:
            QM_LOGI(LOG_TAG, "connected");
            if (s_qm_agent_example_initial_prompt_pending) {
                qm_agent_set_upload_enabled(s_qm_agent_example, QM_FALSE);
                (void)qm_agent_example_check("send_text",
                                          qm_agent_send_text(s_qm_agent_example,
                                                          CONFIG_QM_AGENT_EXAMPLE_START_TEXT));
                {
                    qm_agent_tts_request_t request;

                    qm_memset(&request, 0, sizeof(request));
                    request.text = CONFIG_QM_AGENT_EXAMPLE_TTS_TEXT;
                    (void)qm_agent_example_check("tts_request",
                                                  qm_agent_tts_request(s_qm_agent_example,
                                                                       &request,
                                                                       NULL));
                }
                qm_agent_set_upload_enabled(s_qm_agent_example, QM_TRUE);
                s_qm_agent_example_initial_prompt_pending = QM_FALSE;
            }
            break;
        case QM_AGENT_EVENT_DISCONNECTED:
            QM_LOGI(LOG_TAG, "disconnected");
            break;
        case QM_AGENT_EVENT_ASR:
            QM_LOGI(LOG_TAG,
                    "asr final=%d text=%s",
                    notice->data.text.final,
                    notice->data.text.text);
            break;
        case QM_AGENT_EVENT_TTS:
            QM_LOGI(LOG_TAG,
                    "tts final=%d text=%s",
                    notice->data.text.final,
                    notice->data.text.text);
            break;
        case QM_AGENT_EVENT_AGENT_TEXT:
            QM_LOGI(LOG_TAG,
                    "agent final=%d text=%s",
                    notice->data.text.final,
                    notice->data.text.text);
            break;
        case QM_AGENT_EVENT_AGENT_STATE:
            QM_LOGI(LOG_TAG,
                    "agent_state=%s text=%s",
                    qm_agent_example_agent_state_name(notice->data.agent_state.state),
                    notice->data.agent_state.detail.text);
            break;
        case QM_AGENT_EVENT_DOA:
            QM_LOGI(LOG_TAG,
                    "doa azimuth=%d elevation=%d has_elevation=%d confidence=%u has_confidence=%d",
                    (int)notice->data.doa.azimuth,
                    (int)notice->data.doa.elevation,
                    notice->data.doa.has_elevation,
                    (unsigned int)notice->data.doa.confidence,
                    notice->data.doa.has_confidence);
            break;
        case QM_AGENT_EVENT_AGENT_ABORTED:
            QM_LOGI(LOG_TAG, "agent aborted");
            break;
        case QM_AGENT_EVENT_ERROR:
            QM_LOGE(LOG_TAG,
                    "error code=%d message=%s",
                    notice->code,
                    notice->data.text.text);
            break;
        default:
            QM_LOGI(LOG_TAG, "event id=%d", (int)event_id);
            break;
    }
}

static int qm_agent_example_register_events(void)
{
    int ret;

    if (s_qm_agent_example_event_registered) {
        return QM_EOK;
    }

    ret = qm_event_default_create();
    if (ret != QM_EOK && ret != -QM_EINIT) {
        return ret;
    }

    ret = qm_event_handler_register(QM_AGENT_EVENT_BASE,
                                    QM_EVENT_ANY_ID,
                                    qm_agent_example_event_handler,
                                    NULL);
    if (ret == QM_EOK) {
        s_qm_agent_example_event_registered = QM_TRUE;
    }
    return ret;
}

static void qm_agent_example_unregister_events(void)
{
    if (!s_qm_agent_example_event_registered) {
        return;
    }

    (void)qm_event_handler_unregister(QM_AGENT_EVENT_BASE,
                                      QM_EVENT_ANY_ID,
                                      qm_agent_example_event_handler,
                                      NULL);
    s_qm_agent_example_event_registered = QM_FALSE;
}

static void qm_agent_example_transport_emit(qm_agent_example_transport_ctx_t *ctx,
                                         const qm_agent_event_t *event)
{
    if (ctx != NULL && ctx->event_cb != NULL && event != NULL) {
        ctx->event_cb(event, ctx->user_ctx);
    }
}

static void qm_agent_example_transport_emit_agent_state(qm_agent_example_transport_ctx_t *ctx,
                                                     qm_agent_dialog_state_t state,
                                                     const char *text)
{
    qm_agent_event_t event = {0};

    event.id = QM_AGENT_EVENT_AGENT_STATE;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    event.data.agent_state.state = state;
    event.data.agent_state.text = text;
    qm_agent_example_transport_emit(ctx, &event);
}

static int qm_agent_example_transport_init(void **transport_ctx,
                                        const qm_agent_transport_config_t *config,
                                        const qm_agent_audio_format_t *input_format,
                                        const qm_agent_audio_format_t *output_format,
                                        const qm_agent_video_format_t *input_video_format,
                                        const qm_agent_video_format_t *output_video_format,
                                        qm_agent_transport_event_cb_t event_cb,
                                        void *user_ctx)
{
    (void)config;
    (void)input_format;
    (void)output_format;
    (void)input_video_format;
    (void)output_video_format;

    if (transport_ctx == NULL || event_cb == NULL) {
        return -QM_EINVAL;
    }

    s_example_transport_ctx.event_cb = event_cb;
    s_example_transport_ctx.user_ctx = user_ctx;
    s_example_transport_ctx.started = QM_FALSE;
    *transport_ctx = &s_example_transport_ctx;
    return QM_EOK;
}

static int qm_agent_example_transport_deinit(void *transport_ctx)
{
    qm_agent_example_transport_ctx_t *ctx = (qm_agent_example_transport_ctx_t *)transport_ctx;

    if (ctx != NULL) {
        ctx->event_cb = NULL;
        ctx->user_ctx = NULL;
        ctx->started = QM_FALSE;
    }
    return QM_EOK;
}

static int qm_agent_example_transport_start(void *transport_ctx)
{
    qm_agent_example_transport_ctx_t *ctx = (qm_agent_example_transport_ctx_t *)transport_ctx;
    qm_agent_event_t event = {0};

    if (ctx == NULL) {
        return -QM_EINVAL;
    }

    ctx->started = QM_TRUE;
    event.id = QM_AGENT_EVENT_CONNECTED;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    qm_agent_example_transport_emit(ctx, &event);
    qm_agent_example_transport_emit_agent_state(ctx, QM_AGENT_DIALOG_STATE_LISTENING, "example_ready");
    return QM_EOK;
}

static int qm_agent_example_transport_stop(void *transport_ctx)
{
    qm_agent_example_transport_ctx_t *ctx = (qm_agent_example_transport_ctx_t *)transport_ctx;
    qm_agent_event_t event = {0};

    if (ctx == NULL) {
        return -QM_EINVAL;
    }

    ctx->started = QM_FALSE;
    event.id = QM_AGENT_EVENT_DISCONNECTED;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    qm_agent_example_transport_emit(ctx, &event);
    return QM_EOK;
}

static int qm_agent_example_transport_send_audio(void *transport_ctx,
                                              const void *data,
                                              uint32_t len)
{
    qm_agent_example_transport_ctx_t *ctx = (qm_agent_example_transport_ctx_t *)transport_ctx;

    if (ctx == NULL || data == NULL || len == 0) {
        return -QM_EINVAL;
    }

    if (!ctx->started) {
        return -QM_EBUSY;
    }

    QM_LOGI(LOG_TAG, "custom transport audio len=%u", (unsigned int)len);
    return QM_EOK;
}

static int qm_agent_example_transport_send_video(void *transport_ctx,
                                              const void *data,
                                              uint32_t len,
                                              const qm_agent_video_format_t *format)
{
    qm_agent_example_transport_ctx_t *ctx = (qm_agent_example_transport_ctx_t *)transport_ctx;

    (void)format;

    if (ctx == NULL || data == NULL || len == 0) {
        return -QM_EINVAL;
    }

    if (!ctx->started) {
        return -QM_EBUSY;
    }

    QM_LOGI(LOG_TAG, "custom transport video len=%u", (unsigned int)len);
    return QM_EOK;
}

static int qm_agent_example_transport_send_text(void *transport_ctx, const char *text)
{
    qm_agent_example_transport_ctx_t *ctx = (qm_agent_example_transport_ctx_t *)transport_ctx;
    qm_agent_event_t event = {0};

    if (ctx == NULL || text == NULL) {
        return -QM_EINVAL;
    }

    qm_agent_example_transport_emit_agent_state(ctx, QM_AGENT_DIALOG_STATE_THINKING, "example_thinking");

    event.id = QM_AGENT_EVENT_ASR;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    event.data.asr.text = text;
    event.data.asr.final = QM_TRUE;
    qm_agent_example_transport_emit(ctx, &event);

    event.id = QM_AGENT_EVENT_AGENT_TEXT;
    event.data.agent_text.text = "custom transport reply";
    event.data.agent_text.final = QM_TRUE;
    qm_agent_example_transport_emit(ctx, &event);

    qm_agent_example_transport_emit_agent_state(ctx, QM_AGENT_DIALOG_STATE_LISTENING, "example_listening");
    return QM_EOK;
}

static int qm_agent_example_transport_request_tts(void *transport_ctx, const char *text)
{
    qm_agent_example_transport_ctx_t *ctx = (qm_agent_example_transport_ctx_t *)transport_ctx;
    qm_agent_event_t event = {0};

    if (ctx == NULL || text == NULL) {
        return -QM_EINVAL;
    }

    qm_agent_example_transport_emit_agent_state(ctx, QM_AGENT_DIALOG_STATE_SPEAKING, "example_tts");

    event.id = QM_AGENT_EVENT_TTS;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    event.data.tts.text = text;
    event.data.tts.final = QM_TRUE;
    qm_agent_example_transport_emit(ctx, &event);

    qm_agent_example_transport_emit_agent_state(ctx, QM_AGENT_DIALOG_STATE_LISTENING, "example_listening");
    return QM_EOK;
}

static int qm_agent_example_transport_interrupt(void *transport_ctx)
{
    qm_agent_example_transport_ctx_t *ctx = (qm_agent_example_transport_ctx_t *)transport_ctx;
    qm_agent_event_t event = {0};

    if (ctx == NULL) {
        return -QM_EINVAL;
    }

    event.id = QM_AGENT_EVENT_AGENT_ABORTED;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    qm_agent_example_transport_emit(ctx, &event);
    qm_agent_example_transport_emit_agent_state(ctx, QM_AGENT_DIALOG_STATE_LISTENING, "example_interrupted");
    return QM_EOK;
}

static const qm_agent_transport_if_t s_qm_agent_example_transport = {
    .name = "qm_agent_example_custom",
    .init = qm_agent_example_transport_init,
    .deinit = qm_agent_example_transport_deinit,
    .start = qm_agent_example_transport_start,
    .stop = qm_agent_example_transport_stop,
    .send_audio = qm_agent_example_transport_send_audio,
    .send_video = qm_agent_example_transport_send_video,
    .send_text = qm_agent_example_transport_send_text,
    .request_tts = qm_agent_example_transport_request_tts,
    .interrupt = qm_agent_example_transport_interrupt,
};

static int qm_agent_example_check(const char *name, int ret)
{
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "%s failed ret=%d", name, ret);
        return ret;
    }

    QM_LOGI(LOG_TAG, "%s ok", name);
    return QM_EOK;
}

void qm_agent_example_stop(void)
{
    /* Detach application delivery before the session can be released. */
    qm_agent_example_unregister_events();
    if (s_qm_agent_example != NULL) {
        (void)qm_agent_stop(s_qm_agent_example);
        qm_agent_destroy(s_qm_agent_example);
        s_qm_agent_example = NULL;
    }
    (void)qm_agent_example_audio_deinit(&s_qm_agent_example_audio);
    s_qm_agent_example_initial_prompt_pending = QM_FALSE;
}

static void qm_agent_example_start_session(void)
{
    qm_agent_config_t config = {0};
    int ret;

    qm_agent_example_stop();

    ret = qm_agent_example_ensure_audio();
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "shared audio init failed ret=%d", ret);
        return;
    }

    ret = qm_agent_example_register_events();
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "register agent events failed ret=%d", ret);
        return;
    }

#if CONFIG_QM_AGENT_EXAMPLE_USE_CUSTOM_TRANSPORT
    config.transport.type = QM_AGENT_TRANSPORT_CUSTOM;
    config.transport_if = &s_qm_agent_example_transport;
#else
    config.transport.type = QM_AGENT_TRANSPORT_JOYINSIDE;
#endif
    config.audio.control = s_qm_agent_example_audio.control;
    config.audio.capture_enabled = QM_TRUE;
    qm_agent_config_init_defaults(&config);

#if !CONFIG_QM_AGENT_EXAMPLE_USE_CUSTOM_TRANSPORT
    config.transport_if = NULL;
    config.transport.data.joyinside.bot_id = CONFIG_QM_AGENT_EXAMPLE_JOYINSIDE_BOT_ID;
    config.transport.data.joyinside.access_token = CONFIG_QM_AGENT_EXAMPLE_JOYINSIDE_ACCESS_TOKEN;
    config.transport.data.joyinside.uid = CONFIG_QM_AGENT_EXAMPLE_JOYINSIDE_UID;
    config.transport.data.joyinside.chat_role_code = CONFIG_QM_AGENT_EXAMPLE_JOYINSIDE_CHAT_ROLE_CODE;
    config.transport.data.joyinside.timeout_ms = CONFIG_QM_AGENT_EXAMPLE_JOYINSIDE_TIMEOUT_MS;
#endif
    config.iot_handle = NULL;

    s_qm_agent_example = qm_agent_create(&config);
    if (s_qm_agent_example == NULL) {
        QM_LOGE(LOG_TAG, "create failed");
        qm_agent_example_unregister_events();
        return;
    }

    s_qm_agent_example_initial_prompt_pending = QM_TRUE;
    if (qm_agent_example_check("start", qm_agent_start(s_qm_agent_example)) != QM_EOK) {
        qm_agent_example_stop();
        return;
    }
}

#if CONFIG_QM_AGENT_EXAMPLE_USE_WIFI
static void qm_agent_example_wifi_event_handler(void *event_handler_arg,
                                               qm_event_base_t event_base,
                                               int32_t event_id,
                                               void *event_data,
                                               size_t event_data_size)
{
    qm_wifi_event_info_t *event_info = (qm_wifi_event_info_t *)event_data;

    QM_UNUSED_ARG(event_handler_arg);
    QM_UNUSED_ARG(event_base);
    QM_UNUSED_ARG(event_data_size);
    switch ((qm_wifi_event_t)event_id) {
        case QM_WIFI_EVENT_STA_START:
            QM_LOGI(LOG_TAG, "wifi sta start");
            if (qm_wifi_connect() != QM_EOK) {
                QM_LOGE(LOG_TAG, "wifi connect failed");
            }
            break;
        case QM_WIFI_EVENT_STA_CONNECTED:
            QM_LOGI(LOG_TAG, "wifi sta connected");
            s_qm_agent_example_wifi_retry_count = 0;
            break;
        case QM_WIFI_EVENT_STA_GOT_IP:
            QM_LOGI(LOG_TAG, "wifi got ip");
            if (event_info != NULL) {
                QM_LOGI(LOG_TAG, "ip:" QM_IPSTR, QM_IP2STR(&event_info->got_ip.ip_info.ip));
                QM_LOGI(LOG_TAG, "netmask:" QM_IPSTR, QM_IP2STR(&event_info->got_ip.ip_info.netmask));
                QM_LOGI(LOG_TAG, "gw:" QM_IPSTR, QM_IP2STR(&event_info->got_ip.ip_info.gw));
            }
            if (s_qm_agent_example == NULL) {
                qm_agent_example_start_session();
            }
            break;
        case QM_WIFI_EVENT_STA_DISCONNECTED:
            QM_LOGW(LOG_TAG, "wifi disconnected");
            qm_agent_example_stop();
            if (s_qm_agent_example_wifi_retry_count < CONFIG_QM_AGENT_EXAMPLE_WIFI_RETRY_MAX_COUNT) {
                s_qm_agent_example_wifi_retry_count++;
                if (qm_wifi_connect() != QM_EOK) {
                    QM_LOGE(LOG_TAG, "wifi reconnect failed");
                    break;
                }
                QM_LOGI(LOG_TAG,
                        "retry wifi connect %d/%d",
                        s_qm_agent_example_wifi_retry_count,
                        CONFIG_QM_AGENT_EXAMPLE_WIFI_RETRY_MAX_COUNT);
            }
            break;
        default:
            break;
    }
}

static void qm_agent_example_wifi_start(void)
{
    qm_err_t ret;
    qm_wifi_config_t config = {
        .sta = {
            .ssid = CONFIG_QM_AGENT_EXAMPLE_WIFI_SSID,
            .ssid_len = qm_strlen(CONFIG_QM_AGENT_EXAMPLE_WIFI_SSID),
            .password = CONFIG_QM_AGENT_EXAMPLE_WIFI_PASSWORD,
            .password_len = qm_strlen(CONFIG_QM_AGENT_EXAMPLE_WIFI_PASSWORD),
        },
    };

    if (!s_qm_agent_example_wifi_registered) {
        ret = qm_event_handler_register(QM_EVENT_WIFI, QM_EVENT_ANY_ID,
                                             qm_agent_example_wifi_event_handler, NULL);
        if (ret != QM_EOK) {
            QM_LOGE(LOG_TAG, "wifi event register failed: %d", ret);
            return;
        }
        s_qm_agent_example_wifi_registered = QM_TRUE;
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
#endif

void qm_agent_example_start(void)
{
#if CONFIG_QM_AGENT_EXAMPLE_USE_WIFI
    qm_agent_example_wifi_start();
#else
    qm_agent_example_start_session();
#endif
}

void qm_agent_example_send_text(const char *text)
{
    if (s_qm_agent_example == NULL || text == NULL) {
        return;
    }

    (void)qm_agent_example_check("send_text", qm_agent_send_text(s_qm_agent_example, text));
}

void qm_agent_example_request_tts(const char *text)
{
    if (s_qm_agent_example == NULL || text == NULL) {
        return;
    }

    {
        qm_agent_tts_request_t request;

        qm_memset(&request, 0, sizeof(request));
        request.text = text;
        (void)qm_agent_example_check("tts_request",
                                      qm_agent_tts_request(s_qm_agent_example,
                                                           &request,
                                                           NULL));
    }
}

void qm_agent_example_send_video(const void *data,
                              uint32_t len,
                              const qm_agent_video_format_t *format)
{
    if (s_qm_agent_example == NULL || data == NULL || len == 0) {
        return;
    }

    (void)qm_agent_example_check("send_video",
                              qm_agent_send_video(s_qm_agent_example, data, len, format));
}

void qm_agent_example_interrupt(void)
{
    if (s_qm_agent_example == NULL) {
        return;
    }

    (void)qm_agent_example_check("interrupt", qm_agent_interrupt(s_qm_agent_example));
}

void qm_application_start(void)
{
    QM_LOGI(LOG_TAG, "qm_agent example start");
    qm_agent_example_start();
}
