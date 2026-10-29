/** @file test_module.c @brief Cloud-module callback lifetime and control-task routing tests. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "qm_agent_internal.h"

/* Include the implementation to exercise its private callback ingress path. */
#include "../src/core/qm_agent_module.c"

static qm_iot_mqtt_recv_handler_t registered_method_handler;
static qm_iot_event_handler_t registered_event_handler;
static void *registered_method_cookie;
static void *registered_event_cookie;
static qm_agent_module_cloud_request_t *pending_request;
static qm_agent_module_event_id_t pending_event;
static unsigned method_posts;
static unsigned event_posts;
static unsigned module_starts;
static unsigned module_stops;
static unsigned module_method_calls;
static unsigned module_event_calls;

uint32_t qm_agent_get_log_flags(void)
{
    return QM_AGENT_LOG_NONE;
}

qm_bool qm_agent_callback_enter(qm_agent_handle_t handle)
{
    qm_bool allowed;

    if (handle == NULL) {
        return QM_FALSE;
    }
    assert(qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER) == QM_EOK);
    allowed = !handle->destroying;
    if (allowed) {
        handle->callback_depth++;
    }
    assert(qm_mutex_unlock(&handle->network_lock) == QM_EOK);
    return allowed;
}

qm_bool qm_agent_callback_try_enter(qm_agent_handle_t handle)
{
    return qm_agent_callback_enter(handle);
}

void qm_agent_callback_leave(qm_agent_handle_t handle)
{
    assert(handle != NULL);
    assert(qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER) == QM_EOK);
    assert(handle->callback_depth != 0U);
    handle->callback_depth--;
    assert(qm_mutex_unlock(&handle->network_lock) == QM_EOK);
}

int qm_agent_control_post_module_request(
    qm_agent_handle_t handle,
    qm_agent_module_cloud_request_t *request)
{
    assert(handle != NULL);
    assert(request != NULL);
    assert(pending_request == NULL);
    pending_request = request;
    method_posts++;
    return QM_EOK;
}

int qm_agent_control_post_module_event(qm_agent_handle_t handle,
                                       qm_agent_module_event_id_t event)
{
    assert(handle != NULL);
    pending_event = event;
    event_posts++;
    return QM_EOK;
}

int32_t qm_iot_down_register(void *iot_handle,
                             char *method,
                             qm_iot_mqtt_recv_handler_t handler,
                             void *userdata)
{
    (void)iot_handle;
    assert(method != NULL);
    assert(handler != NULL);
    registered_method_handler = handler;
    registered_method_cookie = userdata;
    return QM_EOK;
}

int32_t qm_iot_down_unregister(void *iot_handle, char *method)
{
    (void)iot_handle;
    assert(method != NULL);
    registered_method_handler = NULL;
    registered_method_cookie = NULL;
    return QM_EOK;
}

int32_t qm_iot_event_register(void *iot_handle,
                              qm_iot_event_handler_t handler,
                              void *userdata)
{
    (void)iot_handle;
    assert(handler != NULL);
    registered_event_handler = handler;
    registered_event_cookie = userdata;
    return QM_EOK;
}

int32_t qm_iot_event_unregister_one(void *iot_handle,
                                    qm_iot_event_handler_t handler,
                                    void *userdata)
{
    (void)iot_handle;
    assert(handler == registered_event_handler);
    assert(userdata == registered_event_cookie);
    registered_event_handler = NULL;
    registered_event_cookie = NULL;
    return QM_EOK;
}

int32_t qm_iot_down_reply(void *iot_handle,
                          const qm_iot_down_req_t *request,
                          int code,
                          char *payload,
                          int payload_len)
{
    (void)iot_handle;
    (void)request;
    (void)code;
    (void)payload;
    (void)payload_len;
    return QM_EOK;
}

int32_t qm_iot_up_report_ex(void *iot_handle,
                            char *method,
                            char *submethod,
                            uint32_t msg_id,
                            char *payload,
                            int payload_len)
{
    (void)iot_handle;
    (void)method;
    (void)submethod;
    (void)msg_id;
    (void)payload;
    (void)payload_len;
    return QM_EOK;
}

static int module_start(qm_agent_module_t *module)
{
    assert(module != NULL);
    module_starts++;
    return QM_EOK;
}

static int module_stop(qm_agent_module_t *module)
{
    assert(module != NULL);
    module_stops++;
    return QM_EOK;
}

static int module_method(qm_agent_module_t *module,
                         const qm_agent_module_req_t *request)
{
    assert(module != NULL);
    assert(request != NULL);
    assert(strcmp(request->method, "moduleCommand") == 0);
    assert(strcmp(request->submethod, "run") == 0);
    assert(strcmp(request->params, "{\"level\":3}") == 0);
    module_method_calls++;
    return QM_EOK;
}

static int module_event(qm_agent_module_t *module,
                        const qm_agent_module_event_t *event)
{
    assert(module != NULL);
    assert(event != NULL);
    assert(event->data == NULL && event->data_len == 0U);
    module_event_calls++;
    return QM_EOK;
}

static const qm_agent_module_method_t module_methods[] = {
    {
        .method = "moduleCommand",
        .submethod = "run",
        .handler = module_method,
    },
};

static const qm_agent_module_desc_t module_desc = {
    .version = QM_AGENT_MODULE_VERSION,
    .name = "module-test",
    .start = module_start,
    .stop = module_stop,
    .on_event = module_event,
    .methods = module_methods,
    .method_count = 1U,
};

static void deliver_method(qm_iot_mqtt_recv_handler_t handler, void *cookie)
{
    char payload[] =
        "{\"method\":\"moduleCommand\",\"subMethod\":\"run\","
        "\"id\":11,\"params\":{\"level\":3}}";
    qm_iot_mqtt_recv_t packet = {0};

    packet.type = QM_IOT_MQTTRECV_PUB;
    packet.data.pub.payload = (uint8_t *)payload;
    packet.data.pub.payload_len = (uint32_t)strlen(payload);
    handler(NULL, &packet, cookie);
    qm_memset(payload, 0, sizeof(payload));
}

static void test_cookie_lifetime_and_control_routing(void)
{
    struct qm_agent agent = {0};
    qm_iot_mqtt_recv_handler_t stale_method_handler;
    qm_iot_event_handler_t stale_event_handler;
    void *stale_method_cookie;
    void *stale_event_cookie;
    qm_iot_event_t cloud_event = {0};
    unsigned old_posts;

    assert(qm_mutex_new(&agent.network_lock) == QM_EOK);
    agent.config.iot_handle = &agent;
    assert(qm_agent_module_manager_init(&agent) == QM_EOK);
    assert(qm_agent_module_manager_load(&agent, &module_desc, NULL) == QM_EOK);
    assert(qm_agent_module_manager_start(&agent) == QM_EOK);
    assert(agent.modules.running);
    assert(module_starts == 1U);
    assert(registered_method_handler != NULL && registered_event_handler != NULL);

    stale_method_handler = registered_method_handler;
    stale_method_cookie = registered_method_cookie;
    stale_event_handler = registered_event_handler;
    stale_event_cookie = registered_event_cookie;

    deliver_method(registered_method_handler, registered_method_cookie);
    assert(agent.callback_depth == 0U);
    assert(method_posts == 1U && pending_request != NULL);
    assert(qm_agent_module_manager_dispatch_cloud_request(&agent, pending_request) == QM_EOK);
    qm_agent_module_cloud_request_free(pending_request);
    pending_request = NULL;
    assert(module_method_calls == 1U);

    cloud_event.type = QM_IOT_EVENT_CLOUD_CONN;
    registered_event_handler(NULL, &cloud_event, registered_event_cookie);
    assert(event_posts == 1U && pending_event == QM_AGENT_MODULE_EVENT_CLOUD_CONNECTED);
    assert(qm_agent_module_manager_emit_cloud_event(&agent, pending_event) == QM_EOK);
    assert(module_event_calls == 1U);

    assert(qm_agent_module_manager_stop(&agent) == QM_EOK);
    assert(!agent.modules.running && module_stops == 1U);
    old_posts = method_posts + event_posts;
    deliver_method(stale_method_handler, stale_method_cookie);
    stale_event_handler(NULL, &cloud_event, stale_event_cookie);
    assert(method_posts + event_posts == old_posts);

    assert(qm_agent_module_manager_start(&agent) == QM_EOK);
    assert(registered_method_cookie != stale_method_cookie);
    assert(registered_event_cookie != stale_event_cookie);
    deliver_method(stale_method_handler, stale_method_cookie);
    stale_event_handler(NULL, &cloud_event, stale_event_cookie);
    assert(method_posts + event_posts == old_posts);

    qm_agent_module_manager_deinit(&agent);
    assert(qm_mutex_free(&agent.network_lock) == QM_EOK);
}

int main(void)
{
    test_cookie_lifetime_and_control_routing();
    puts("PASS: module cloud callback cookie and control routing");
    return 0;
}
