/** @file test_auth.c @brief Private authentication scheduling and lifetime tests. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/auth/qm_agent_auth.c"

static unsigned requests;
static unsigned schedules;
static unsigned ready_events;
static uint32_t retry_generation;
static int request_result;
static int schedule_result;
static uint32_t scheduled_delay;

void *qm_malloc(unsigned int n) { return malloc(n); }
void qm_free(void *p) { free(p); }
uint32_t qm_agent_get_log_flags(void) { return 0U; }
void qm_agent_auth_ready(qm_agent_handle_t owner) { (void)owner; ready_events++; }
int qm_agent_control_schedule(qm_agent_handle_t owner, qm_agent_control_action_t action,
                               uint32_t delay, uint32_t id, uint32_t generation)
{
    (void)owner; (void)id;
    scheduled_delay = delay;
    schedules++;
    if (action == QM_AGENT_CONTROL_AUTH_RETRY) retry_generation = generation;
    return schedule_result;
}

#if QM_AGENT_AUTH_USE_HTTP
int qm_utils_hmac_sha1(const char *msg, int len, char *digest, const char *key, int key_len)
{
    (void)msg; (void)len; (void)key; (void)key_len;
    memset(digest, '0', QM_UTILS_HMAC_SHA1_HEX_LEN);
    return QM_EOK;
}

int qm_http_client_execute(const qm_http_client_request_t *req, qm_http_client_response_t *resp)
{
    static const char reply[] = "{\"status\":0,\"result\":{\"ljyd\":{\"token\":\"test-token\"}}}";
    requests++;
    if (request_result != QM_EOK) return request_result;
    resp->status_code = 200;
    return req->on_body(reply, sizeof(reply) - 1U, req->user);
}
#else
static qm_iot_mqtt_recv_handler_t reply_handler;
static qm_iot_event_handler_t cloud_handler;
static void *reply_cookie;
static void *cloud_cookie;
static uint32_t request_id;

int32_t qm_iot_down_register(void *iot, char *method,
                            qm_iot_mqtt_recv_handler_t cb, void *context)
{
    (void)iot; (void)method;
    reply_handler = cb;
    reply_cookie = context;
    return QM_EOK;
}
int32_t qm_iot_event_register(void *iot, qm_iot_event_handler_t cb, void *context)
{
    (void)iot;
    cloud_handler = cb;
    cloud_cookie = context;
    return QM_EOK;
}
int32_t qm_iot_event_unregister_one(void *iot, qm_iot_event_handler_t cb, void *context)
{
    (void)iot;
    assert(cb == cloud_handler && context == cloud_cookie);
    cloud_handler = NULL;
    cloud_cookie = NULL;
    return QM_EOK;
}
int32_t qm_iot_up_report(void *iot, char *method, uint32_t id, char *body, int len)
{
    (void)iot; (void)method; (void)body; (void)len;
    requests++;
    request_id = id;
    return request_result;
}

static void deliver_reply(uint32_t id)
{
    char payload[256];
    qm_iot_mqtt_recv_t packet = {0};
    int len = snprintf(payload, sizeof(payload),
        "{\"method\":\"getAgentTokenReply\",\"id\":%u,\"code\":0,"
        "\"params\":{\"ljyd\":{\"token\":\"test-token\"}}}", id);
    assert(len > 0 && (size_t)len < sizeof(payload));
    packet.type = QM_IOT_MQTTRECV_PUB;
    packet.data.pub.payload = (uint8_t *)payload;
    packet.data.pub.payload_len = (uint32_t)len;
    reply_handler(NULL, &packet, reply_cookie);
    memset(payload, 0, sizeof(payload));
}

static void test_copied_ingress_and_late_callbacks(struct qm_agent *owner)
{
    qm_agent_auth_t *auth = qm_agent_auth_init(owner);
    qm_agent_auth_info_t info = {0};
    qm_iot_event_t event = {0};
    qm_iot_mqtt_recv_t packet = {0};
    qm_iot_mqtt_recv_handler_t old_reply = reply_handler;
    qm_iot_event_handler_t old_cloud = cloud_handler;
    void *old_cookie = reply_cookie;
    unsigned before;
    uint32_t old_id;

    assert(auth != NULL);
    assert(qm_agent_auth_init(owner) == NULL);
    assert(qm_agent_auth_request_token(auth, 0U) == QM_EOK);
    old_id = request_id;
    deliver_reply(old_id);
    assert(qm_agent_auth_get_token(auth, &info) == -QM_ENOENT);
    qm_agent_auth_process_pending(auth);
    assert(qm_agent_auth_get_token(auth, &info) == QM_EOK);
    assert(strcmp(info.ljyd.token, "test-token") == 0);
    qm_agent_auth_info_deinit(&info);
    before = requests;
    assert(qm_agent_auth_retry_now(auth, retry_generation) == -QM_EBUSY);
    assert(requests == before);

    event.type = QM_IOT_EVENT_CLOUD_DISCONN;
    cloud_handler(NULL, &event, cloud_cookie);
    qm_agent_auth_process_pending(auth);
    assert(qm_agent_auth_request_token(auth, 0U) == -QM_EBUSY);
    event.type = QM_IOT_EVENT_CLOUD_CONN;
    cloud_handler(NULL, &event, cloud_cookie);
    qm_agent_auth_process_pending(auth);
    assert(qm_agent_auth_request_token(auth, 0U) == QM_EOK);
    assert(request_id != old_id);
    deliver_reply(old_id);
    qm_agent_auth_process_pending(auth);
    assert(qm_agent_auth_get_token(auth, &info) == -QM_ENOENT);

    assert(qm_agent_auth_deinit(auth) == QM_EOK);
    before = schedules;
    packet.type = QM_IOT_MQTTRECV_PUB;
    packet.data.pub.payload = (uint8_t *)"{}";
    packet.data.pub.payload_len = 2U;
    old_reply(NULL, &packet, old_cookie);
    old_cloud(NULL, &event, old_cookie);
    assert(schedules == before);

    auth = qm_agent_auth_init(owner);
    assert(auth != NULL && reply_cookie != old_cookie);
    before = schedules;
    old_reply(NULL, &packet, old_cookie);
    old_cloud(NULL, &event, old_cookie);
    assert(schedules == before);
    assert(qm_agent_auth_deinit(auth) == QM_EOK);
}

static void test_reply_deadline_precedes_retry(struct qm_agent *owner)
{
    qm_agent_auth_t *auth = qm_agent_auth_init(owner);
    unsigned before;

    assert(auth != NULL);
    request_result = QM_EOK;
    assert(qm_agent_auth_request_token(auth, 0U) == QM_EOK);
    assert(auth->awaiting_reply);
    assert(scheduled_delay == QM_AGENT_AUTH_REPLY_TIMEOUT_MS);
    for (unsigned i = 0; i < QM_AGENT_AUTH_RETRY_MAX_ATTEMPTS; ++i) {
        before = requests;
        assert(qm_agent_auth_retry_now(auth, retry_generation) == QM_EOK);
        assert(!auth->awaiting_reply && requests == before);
        assert(qm_agent_auth_retry_now(auth, retry_generation) == QM_EOK);
        assert(auth->awaiting_reply && requests == before + 1U);
        assert(scheduled_delay == QM_AGENT_AUTH_REPLY_TIMEOUT_MS);
    }
    /* The final successful publish still owns a complete reply window. */
    deliver_reply(request_id);
    qm_agent_auth_process_pending(auth);
    assert(auth->has_cached_auth && !auth->retry_active);
    assert(qm_agent_auth_deinit(auth) == QM_EOK);
}
#endif

static void test_retry_cycle(struct qm_agent *owner)
{
    qm_agent_auth_t *auth = qm_agent_auth_init(owner);
    unsigned before;
    uint32_t stale_generation;

    assert(auth != NULL);
#if QM_AGENT_AUTH_USE_HTTP
    qm_agent_auth_http_config_t config = {0};
    config.product_id = 1U;
    config.product_secret = "test-secret";
    config.sn = "test-device";
    assert(qm_agent_auth_set_http_config(auth, &config) == QM_EOK);
#endif
    request_result = -QM_EIO;
    assert(qm_agent_auth_request_token(auth, 0U) == QM_EOK);
    stale_generation = retry_generation;
    before = requests;
    assert(qm_agent_auth_request_token(auth, 0U) == QM_EOK);
    assert(requests == before);
    qm_agent_auth_cancel(auth);
    assert(qm_agent_auth_retry_now(auth, stale_generation) == -QM_EBUSY);
    assert(requests == before);
    assert(qm_agent_auth_request_token(auth, 0U) == QM_EOK);
    assert(retry_generation != stale_generation);
    for (unsigned i = 1U; i < QM_AGENT_AUTH_RETRY_MAX_ATTEMPTS; ++i) {
        assert(qm_agent_auth_retry_now(auth, retry_generation) == QM_EOK);
    }
    assert(qm_agent_auth_retry_now(auth, retry_generation) == -QM_EIO);
    before = requests;
    assert(qm_agent_auth_request_token(auth, 0U) == -QM_ERROR);
    assert(requests == before);
    qm_agent_auth_cancel(auth);
    request_result = QM_EOK;
    assert(qm_agent_auth_request_token(auth, 0U) == QM_EOK);
#if QM_AGENT_AUTH_USE_HTTP
    assert(ready_events != 0U && !auth->retry_active);
#else
    deliver_reply(request_id);
    qm_agent_auth_process_pending(auth);
    assert(ready_events != 0U && !auth->retry_active);
#endif
    assert(qm_agent_auth_deinit(auth) == QM_EOK);
}

int main(void)
{
    struct qm_agent owner = {0};
    owner.config.iot_handle = &owner;
    test_retry_cycle(&owner);
#if !QM_AGENT_AUTH_USE_HTTP
    test_copied_ingress_and_late_callbacks(&owner);
    test_reply_deadline_precedes_retry(&owner);
#endif
    puts("PASS: private auth retry ownership and callback lifetime");
    return 0;
}
