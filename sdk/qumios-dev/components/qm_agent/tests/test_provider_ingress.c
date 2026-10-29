/** @file test_provider_ingress.c @brief Provider zero-wait audio admission. */
#include <assert.h>
#include <pthread.h>
#include <string.h>

#if TEST_LGEINE
#include "../../lgeine_agent/src/lgeine_agent.c"
#else
#include "../../baidu_chat_agent/src/baidu_chat_agent.c"
#endif

static int send_result = 2;
static unsigned send_calls;

int qm_websocket_client_send_bin(void *client,
                                  const char *data, int len, uint32_t timeout_ms)
{
    assert(client != NULL && len == 2 && memcmp(data, "ab", 2U) == 0);
    assert(timeout_ms == 0U);
    send_calls++;
    return send_result;
}

#if TEST_LGEINE
static void *try_upload(void *ctx)
{
    assert(lgeine_agent_send_audio(ctx, "ab", 2U, 0U) == -QM_EBUSY);
    return NULL;
}
#endif

int main(void)
{
#if TEST_LGEINE
    struct lgeine_agent provider = {0};
    pthread_t caller;
    assert(qm_mutex_new(&provider.lock) == QM_EOK);
    provider.ready = QM_TRUE;
    provider.listen_recording = QM_TRUE;
    provider.requested_audio.format = LGEINE_AGENT_AUDIO_FORMAT_OPUS;
    provider.ws = &provider;
    assert(lgeine_agent_send_audio(&provider, "ab", 2U, 0U) == QM_EOK);
    assert(qm_mutex_lock(&provider.lock, QM_WAIT_FOREVER) == QM_EOK);
    assert(pthread_create(&caller, NULL, try_upload, &provider) == 0);
    assert(pthread_join(caller, NULL) == 0);
    assert(qm_mutex_unlock(&provider.lock) == QM_EOK);
    send_result = -QM_EFULL;
    assert(lgeine_agent_send_audio(&provider, "ab", 2U, 0U) == -QM_EFULL);
    provider.ready = QM_FALSE;
    assert(lgeine_agent_send_audio(&provider, "ab", 2U, 0U) == -QM_EBUSY);
    assert(qm_mutex_free(&provider.lock) == QM_EOK);
#else
    struct baidu_chat_agent provider = {0};
    provider.ws = &provider;
    assert(baidu_chat_agent_send_audio(&provider, (const uint8_t *)"ab", 2U, 0U) == QM_EOK);
    send_result = -QM_EFULL;
    assert(baidu_chat_agent_send_audio(&provider, (const uint8_t *)"ab", 2U, 0U) == -QM_EFULL);
    assert(baidu_chat_agent_send_audio(&provider, (const uint8_t *)"ab", UINT32_MAX, 0U) == -QM_EINVAL);
    provider.ws = NULL;
    assert(baidu_chat_agent_send_audio(&provider, (const uint8_t *)"ab", 2U, 0U) == -QM_EBUSY);
#endif
    assert(send_calls == 2U);
    return 0;
}
