#include "qm_config.h"
#include "qm_log.h"
#include "lgeine_agent.h"

#define LOG_TAG "lgeine_example"

static void on_agent_event(lgeine_agent_handle_t agent,
                           const lgeine_agent_event_t *event,
                           void *user_ctx)
{
    (void)user_ctx;
    switch (event->id) {
        case LGEINE_AGENT_EVENT_READY:
            QM_LOGI(LOG_TAG, "protocol ready");
            (void)lgeine_agent_send_listen(agent,
                                           LGEINE_AGENT_LISTEN_STATE_START,
                                           LGEINE_AGENT_LISTEN_MODE_REALTIME,
                                           NULL,
                                           5000U);
            break;
        case LGEINE_AGENT_EVENT_STT:
            QM_LOGI(LOG_TAG, "stt is_final=%d", event->data.stt.is_final ? 1 : 0);
            break;
        case LGEINE_AGENT_EVENT_LLM:
            QM_LOGI(LOG_TAG, "llm emotion=%s",
                    event->data.llm.emotion ? event->data.llm.emotion : "");
            break;
        case LGEINE_AGENT_EVENT_TTS:
            QM_LOGI(LOG_TAG, "tts state=%d type=%d",
                    event->data.tts.state, event->data.tts.type);
            break;
        case LGEINE_AGENT_EVENT_AUDIO:
            /* Queue the borrowed audio chunk to the product audio pipeline here. */
            break;
        case LGEINE_AGENT_EVENT_ERROR:
            QM_LOGE(LOG_TAG, "error type=%d code=%d",
                    event->data.error.type, event->data.error.code);
            break;
        default:
            break;
    }
}

lgeine_agent_handle_t lgeine_agent_example_start(const char *uri,
                                                 const char *pid,
                                                 const char *sn,
                                                 const char *access_token)
{
    lgeine_agent_init_config_t init = {
        .event_cb = on_agent_event,
    };
    lgeine_agent_start_params_t start = {
        .uri = uri,
        .pid = pid,
        .sn = sn,
        .access_token = access_token,
        .audio_info = {
            .format = LGEINE_AGENT_AUDIO_FORMAT_OPUS,
            .rate = 16000U,
            .bits = 16U,
            .channel = 1U,
            .frame_duration_ms = 60U,
        },
    };
    lgeine_agent_handle_t agent = lgeine_agent_init(&init);

    if (!agent) {
        return NULL;
    }
    if (lgeine_agent_start(agent, &start) != 0) {
        (void)lgeine_agent_destroy(agent);
        return NULL;
    }
    return agent;
}

int lgeine_agent_example_stop(lgeine_agent_handle_t agent)
{
    return lgeine_agent_destroy(agent);
}
