/**
 * @file qm_agent_module.c
 * @brief Managed-module registry, cloud route dispatch, and callback lifetime guards.
 *
 * 托管模块注册表、云端路由分发与回调生命周期保护。MQTT 回调只复制请求并投递给控制任务；
 * 注销时先关闭回调准入，再等待存量回调离开，最后释放模块存储。
 */

#include "qm_agent_internal.h"
#include "cJSON.h"
#include "qm_iot_api.h"
#include "qm_kernel.h"
#include "qm_string.h"
#include "qm_utils_list.h"

#define LOG_TAG "qm_agent_module"
#define MODULE_DEFAULT_SUBMETHOD ""

/**
 * @brief Owned form of a cloud request while it waits for qm_agent_ctl.
 *
 * The strings are copied before the MQTT callback returns.  The control queue
 * owns this object and releases it after dispatch or rejection.
 */
struct qm_agent_module_cloud_request {
    qm_agent_module_req_t request;
    char *method;
    char *submethod;
    char *params;
};

/**
 * @brief Process-lifetime callback identity for one running module manager.
 *
 * Cloud implementations may invoke a callback that was copied before
 * unregister.  They receive only cookie, so removal from this registry makes
 * every delayed invocation harmless before the Agent storage is released.
 */
struct qm_agent_module_callback_registration {
    qm_list_node_t node;
    qm_agent_handle_t agent;
    uint32_t cookie;
    qm_bool accepting;
};

/* The lock and list intentionally remain process-lifetime.  A stale cloud
 * callback must still be able to reject a removed cookie after the owning
 * session has been freed. */
static qm_mutex_t s_module_callback_lock;
static qm_list_t s_module_callback_registry;
static uint32_t s_module_callback_next_cookie;
static volatile uint8_t s_module_callback_init_lock;

/**
 * @brief Handle a qm_cloud device/down method packet and route it to modules.
 */
static void cloud_method_recv_handler(void *handle,
                                      const qm_iot_mqtt_recv_t *packet,
                                      void *userdata);

/**
 * @brief Translate qm_cloud connection events into module events.
 */
static void cloud_event_handler(void *iot_handle,
                                const qm_iot_event_t *event,
                                void *userdata);

/** @brief Create the process-wide lock used by callback cookie lookup. */
static int callback_registry_init(void);

/** @brief Add one inactive callback identity for a module manager start. */
static int callback_registration_create(qm_agent_handle_t agent);

/** @brief Remove one callback identity before unregistering external hooks. */
static void callback_registration_destroy(qm_agent_handle_t agent);

/** @brief Close callback admission while retaining the cookie for unregister. */
static void callback_registration_disable(qm_agent_handle_t agent);

/** @brief Enable delivery after every module and cloud route is ready. */
static void callback_registration_enable(qm_agent_handle_t agent);

/** @brief Resolve a cookie and hold an Agent callback lifetime guard. */
static qm_agent_handle_t callback_enter(void *userdata);

/** @brief Remove the manager cloud lifecycle subscription when installed. */
static void unregister_cloud_event(qm_agent_handle_t agent);

/**
 * @brief Validate descriptor shape before it enters the manager.
 */
static qm_bool module_desc_valid(const qm_agent_module_desc_t *desc);

/**
 * @brief Register one built-in module node into the runtime registry.
 */
int qm_agent_module_manager_register_node(qm_agent_module_registry_node_t *node);

/**
 * @brief Compare two route key strings.
 */
static qm_bool module_method_match(const char *left, const char *right)
{
    return (left != NULL && right != NULL && qm_strcmp(left, right) == 0);
}

/**
 * @brief Return the default route key for omitted subMethod values.
 */
static const char *module_submethod_or_default(const char *submethod)
{
    return (submethod != NULL) ? submethod : MODULE_DEFAULT_SUBMETHOD;
}

/**
 * @brief Compare two subMethod route keys with default handling.
 */
static qm_bool module_submethod_match(const char *left, const char *right)
{
    return qm_strcmp(module_submethod_or_default(left),
                     module_submethod_or_default(right)) == 0;
}

/** @brief Head of the built-in module registry list. */
static qm_agent_module_registry_node_t *s_module_registry_head = NULL;

/**
 * @brief Initialize the process-lifetime cookie registry exactly once.
 */
static int callback_registry_init(void)
{
    int ret = QM_EOK;

    if (qm_mutex_is_valid(&s_module_callback_lock)) {
        return QM_EOK;
    }

    while (__sync_lock_test_and_set(&s_module_callback_init_lock, 1U) != 0U) {}
    if (!qm_mutex_is_valid(&s_module_callback_lock)) {
        ret = qm_mutex_new(&s_module_callback_lock);
    }
    __sync_lock_release(&s_module_callback_init_lock);
    return ret;
}

/**
 * @brief Find one live callback registration while the registry lock is held.
 */
static qm_agent_module_callback_registration_t *
callback_registration_find_locked(uint32_t cookie)
{
    qm_list_node_t *node;

    for (node = s_module_callback_registry.head; node != NULL; node = node->next) {
        qm_agent_module_callback_registration_t *registration = node->val;

        if (registration != NULL && registration->cookie == cookie) {
            return registration;
        }
    }

    return NULL;
}

/**
 * @brief Install an inactive callback identity before cloud routes are exposed.
 */
static int callback_registration_create(qm_agent_handle_t agent)
{
    qm_agent_module_callback_registration_t *registration;
    int ret;

    if (agent == NULL) {
        return -QM_EINVAL;
    }
    if (agent->config.iot_handle == NULL ||
        agent->modules.callback_registration != NULL) {
        return QM_EOK;
    }

    ret = callback_registry_init();
    if (ret != QM_EOK) {
        return ret;
    }

    registration = qm_malloc(sizeof(*registration));
    if (registration == NULL) {
        return -QM_ENOMEM;
    }
    qm_memset(registration, 0, sizeof(*registration));
    registration->agent = agent;

    qm_mutex_lock(&s_module_callback_lock, QM_WAIT_FOREVER);
    if (s_module_callback_next_cookie == UINT32_MAX) {
        qm_mutex_unlock(&s_module_callback_lock);
        qm_free(registration);
        return -QM_EFULL;
    }
    registration->cookie = ++s_module_callback_next_cookie;
    qm_list_node_init(&registration->node, registration);
    qm_list_rpush(&s_module_callback_registry, &registration->node);
    qm_mutex_unlock(&s_module_callback_lock);

    agent->modules.callback_registration = registration;
    return QM_EOK;
}

/**
 * @brief Remove a callback identity before its agent storage can be released.
 */
static void callback_registration_destroy(qm_agent_handle_t agent)
{
    qm_agent_module_callback_registration_t *registration;

    if (agent == NULL) {
        return;
    }

    registration = agent->modules.callback_registration;
    if (registration == NULL || !qm_mutex_is_valid(&s_module_callback_lock)) {
        return;
    }

    qm_mutex_lock(&s_module_callback_lock, QM_WAIT_FOREVER);
    registration->accepting = QM_FALSE;
    qm_list_remove(&s_module_callback_registry, &registration->node);
    qm_mutex_unlock(&s_module_callback_lock);

    agent->modules.callback_registration = NULL;
    qm_free(registration);
}

/**
 * @brief Reject new cloud callbacks before unhooking the external routes.
 */
/**
 * @brief Close cloud callback admission before module storage is released.
 * 在释放模块存储前关闭云端回调准入。
 */
static void callback_registration_disable(qm_agent_handle_t agent)
{
    qm_agent_module_callback_registration_t *registration;

    if (agent == NULL || !qm_mutex_is_valid(&s_module_callback_lock)) {
        return;
    }

    registration = agent->modules.callback_registration;
    if (registration == NULL) {
        return;
    }

    qm_mutex_lock(&s_module_callback_lock, QM_WAIT_FOREVER);
    registration->accepting = QM_FALSE;
    qm_mutex_unlock(&s_module_callback_lock);
}

/**
 * @brief Open callback delivery only after all module routes are installed.
 */
static void callback_registration_enable(qm_agent_handle_t agent)
{
    qm_agent_module_callback_registration_t *registration;

    if (agent == NULL || !qm_mutex_is_valid(&s_module_callback_lock)) {
        return;
    }

    registration = agent->modules.callback_registration;
    if (registration == NULL) {
        return;
    }

    qm_mutex_lock(&s_module_callback_lock, QM_WAIT_FOREVER);
    registration->accepting = QM_TRUE;
    qm_mutex_unlock(&s_module_callback_lock);
}

/**
 * @brief Resolve one cookie and acquire the Agent lifetime guard atomically.
 *
 * Holding the registry lock through callback_enter() closes the window where
 * teardown could remove the registration after lookup but before the Agent
 * callback depth protects the raw handle.
 */
/**
 * @brief Resolve a cloud callback cookie and acquire the Agent lifetime guard.
 * 解析云端回调 cookie 并获取 Agent 生命周期保护。
 *
 * A callback receives a handle only while the registration is active and the
 * session is not destroying; callback leave releases this temporary guard.
 * 仅当注册仍有效且会话未销毁时，回调才能获得句柄；回调离开时释放该临时保护。
 */
static qm_agent_handle_t callback_enter(void *userdata)
{
    qm_agent_module_callback_registration_t *registration;
    qm_agent_handle_t agent = NULL;
    uint32_t cookie = (uint32_t)(uintptr_t)userdata;

    if (!qm_mutex_is_valid(&s_module_callback_lock) || cookie == 0U) {
        return NULL;
    }

    qm_mutex_lock(&s_module_callback_lock, QM_WAIT_FOREVER);
    registration = callback_registration_find_locked(cookie);
    if (registration != NULL && registration->accepting &&
        qm_agent_callback_enter(registration->agent)) {
        agent = registration->agent;
    }
    qm_mutex_unlock(&s_module_callback_lock);
    return agent;
}

/**
 * @brief Parse a JSON number/string item as uint32_t.
 */
static uint32_t json_uint_value(const cJSON *item)
{
    if (cJSON_IsNumber(item) && item->valuedouble >= 0) {
        return (uint32_t)item->valuedouble;
    }

    if (cJSON_IsString(item) && item->valuestring != NULL) {
        uint32_t value = 0;
        const char *p = item->valuestring;

        while (*p >= '0' && *p <= '9') {
            value = value * 10U + (uint32_t)(*p - '0');
            p++;
        }
        return value;
    }

    return 0;
}

/**
 * @brief Check whether a method has already been registered before a route.
 */
static qm_bool method_already_registered(qm_agent_handle_t agent,
                                         uint32_t current_module,
                                         uint32_t current_route,
                                         const char *method)
{
    uint32_t i;

    if (agent == NULL || method == NULL) {
        return QM_FALSE;
    }

    for (i = 0; i <= current_module; i++) {
        const qm_agent_module_desc_t *desc = agent->modules.entries[i].desc;
        uint32_t route_limit;
        uint32_t j;

        if (desc == NULL || desc->methods == NULL) {
            continue;
        }

        route_limit = (i == current_module) ? current_route : desc->method_count;
        for (j = 0; j < route_limit; j++) {
            if (module_method_match(desc->methods[j].method, method)) {
                return QM_TRUE;
            }
        }
    }

    return QM_FALSE;
}

/**
 * @brief Check whether one route collides with any existing loaded module.
 */
static int check_duplicate_route_against_loaded(qm_agent_handle_t agent,
                                                const qm_agent_module_desc_t *desc,
                                                uint32_t route_index)
{
    uint32_t module_i;

    if (agent == NULL || desc == NULL || route_index >= desc->method_count) {
        return -QM_EINVAL;
    }

    for (module_i = 0; module_i < agent->modules.count; module_i++) {
        const qm_agent_module_desc_t *loaded = agent->modules.entries[module_i].desc;
        uint32_t loaded_route_i;

        if (!module_desc_valid(loaded)) {
            return -QM_EINVAL;
        }

        for (loaded_route_i = 0; loaded_route_i < loaded->method_count; loaded_route_i++) {
            if (module_method_match(desc->methods[route_index].method,
                                    loaded->methods[loaded_route_i].method) &&
                module_submethod_match(desc->methods[route_index].submethod,
                                       loaded->methods[loaded_route_i].submethod)) {
                QM_AGENT_LOGE(LOG_TAG,
                        "duplicate route method=%s submethod=%s modules=%s/%s",
                        desc->methods[route_index].method,
                        module_submethod_or_default(desc->methods[route_index].submethod),
                        desc->name,
                        loaded->name);
                return -QM_EINVAL;
            }
        }
    }

    return QM_EOK;
}

/**
 * @brief Validate a new module against itself and all currently loaded modules.
 */
static int check_module_routes_against_loaded(qm_agent_handle_t agent,
                                              const qm_agent_module_desc_t *desc)
{
    uint32_t i;

    if (!module_desc_valid(desc)) {
        return -QM_EINVAL;
    }

    for (i = 0; i < desc->method_count; i++) {
        uint32_t j;
        int ret;

        ret = check_duplicate_route_against_loaded(agent, desc, i);
        if (ret != QM_EOK) {
            return ret;
        }

        for (j = i + 1U; j < desc->method_count; j++) {
            if (module_method_match(desc->methods[i].method,
                                    desc->methods[j].method) &&
                module_submethod_match(desc->methods[i].submethod,
                                       desc->methods[j].submethod)) {
                QM_AGENT_LOGE(LOG_TAG,
                        "duplicate route method=%s submethod=%s modules=%s/%s",
                        desc->methods[i].method,
                        module_submethod_or_default(desc->methods[i].submethod),
                        desc->name,
                        desc->name);
                return -QM_EINVAL;
            }
        }
    }

    return QM_EOK;
}

/**
 * @brief Register each unique cloud method required by a module entry.
 */
static int register_cloud_methods(qm_agent_handle_t agent,
                                  uint32_t module_index)
{
    qm_agent_module_entry_t *entry;
    qm_agent_module_callback_registration_t *registration;
    uint32_t i;

    if (agent == NULL || agent->config.iot_handle == NULL ||
        module_index >= agent->modules.count) {
        return QM_EOK;
    }

    entry = &agent->modules.entries[module_index];
    if (entry->desc == NULL || entry->desc->methods == NULL) {
        return QM_EOK;
    }
    registration = agent->modules.callback_registration;
    if (registration == NULL) {
        return -QM_EINIT;
    }

    for (i = 0; i < entry->desc->method_count; i++) {
        const char *method = entry->desc->methods[i].method;
        int ret;

        if (method_already_registered(agent, module_index, i, method)) {
            continue;
        }

        ret = qm_iot_down_register(agent->config.iot_handle,
                                   (char *)method,
                                   cloud_method_recv_handler,
                                   (void *)(uintptr_t)registration->cookie);
        if (ret != QM_EOK) {
            QM_AGENT_LOGE(LOG_TAG, "cloud method register failed method=%s ret=%d",
                    method, ret);
            return ret;
        }
        entry->method_registered = QM_TRUE;
    }

    return QM_EOK;
}

/**
 * @brief Unregister cloud methods owned by the module manager.
 */
static void unregister_cloud_methods(qm_agent_handle_t agent)
{
    uint32_t i;

    if (agent == NULL || agent->config.iot_handle == NULL) {
        return;
    }

    for (i = 0; i < agent->modules.count; i++) {
        qm_agent_module_entry_t *entry = &agent->modules.entries[i];
        uint32_t j;

        if (!entry->method_registered || entry->desc == NULL ||
            entry->desc->methods == NULL) {
            continue;
        }

        for (j = 0; j < entry->desc->method_count; j++) {
            const char *method = entry->desc->methods[j].method;

            if (method_already_registered(agent, i, j, method)) {
                continue;
            }

            (void)qm_iot_down_unregister(agent->config.iot_handle,
                                         (char *)method);
        }

        entry->method_registered = QM_FALSE;
    }
}

/**
 * @brief Release an owned cloud request after control dispatch or rejection.
 */
void qm_agent_module_cloud_request_free(qm_agent_module_cloud_request_t *request)
{
    if (request == NULL) {
        return;
    }

    qm_free(request->method);
    qm_free(request->submethod);
    qm_free(request->params);
    qm_free(request);
}

/**
 * @brief Copy the cloud packet fields that must outlive its MQTT callback.
 */
static qm_agent_module_cloud_request_t *
copy_cloud_request(const qm_iot_mqtt_recv_t *packet)
{
    qm_agent_module_cloud_request_t *request = NULL;
    cJSON *root = NULL;
    const cJSON *id;
    const cJSON *method;
    const cJSON *submethod;
    const cJSON *params;
    const char *submethod_value = MODULE_DEFAULT_SUBMETHOD;
    char *printed_params = NULL;

    if (packet == NULL || packet->type != QM_IOT_MQTTRECV_PUB ||
        packet->data.pub.payload == NULL || packet->data.pub.payload_len == 0U) {
        return NULL;
    }

    root = cJSON_ParseWithLength((const char *)packet->data.pub.payload,
                                 packet->data.pub.payload_len);
    if (root == NULL) {
        return NULL;
    }

    method = cJSON_GetObjectItem(root, "method");
    submethod = cJSON_GetObjectItem(root, "subMethod");
    if (!cJSON_IsString(submethod)) {
        submethod = cJSON_GetObjectItem(root, "submethod");
    }
    if (!cJSON_IsString(method) || cJSON_GetStringValue(method) == NULL) {
        goto out;
    }
    if (cJSON_IsString(submethod) && cJSON_GetStringValue(submethod) != NULL) {
        submethod_value = cJSON_GetStringValue(submethod);
    }

    request = qm_malloc(sizeof(*request));
    if (request == NULL) {
        goto out;
    }
    qm_memset(request, 0, sizeof(*request));
    request->method = qm_strdup(cJSON_GetStringValue(method));
    request->submethod = qm_strdup(submethod_value);
    if (request->method == NULL || request->submethod == NULL) {
        goto fail;
    }

    params = cJSON_GetObjectItem(root, "params");
    if (params != NULL) {
        printed_params = cJSON_PrintUnformatted((cJSON *)params);
        if (printed_params == NULL) {
            goto fail;
        }
        request->params = qm_strdup(printed_params);
        if (request->params == NULL) {
            goto fail;
        }
    }

    id = cJSON_GetObjectItem(root, "id");
    request->request.source = QM_AGENT_MODULE_REQ_SOURCE_CLOUD;
    request->request.msg_id = json_uint_value(id);
    request->request.method = request->method;
    request->request.submethod = request->submethod;
    request->request.params = request->params;
    request->request.params_len = request->params ? qm_strlen(request->params) : 0U;

out:
    cJSON_free(printed_params);
    cJSON_Delete(root);
    return request;

fail:
    qm_agent_module_cloud_request_free(request);
    request = NULL;
    goto out;
}

/**
 * @brief Parse then queue one cloud method request for the Agent control task.
 */
/**
 * @brief Copy an incoming cloud method request and queue it for serialized dispatch.
 * 复制传入的云端方法请求，并入队等待串行分发。
 *
 * The MQTT packet and its strings are provider-owned. This callback performs
 * no module logic directly and frees its copy if control admission fails.
 * MQTT 包及字符串由供应商所有。该回调不直接执行模块逻辑，控制队列拒绝时会释放复制数据。
 */
static void cloud_method_recv_handler(void *handle,
                                      const qm_iot_mqtt_recv_t *packet,
                                      void *userdata)
{
    qm_agent_handle_t agent;
    qm_agent_module_cloud_request_t *request;

    QM_UNUSED_ARG(handle);
    if (packet == NULL) {
        return;
    }

    agent = callback_enter(userdata);
    if (agent == NULL) {
        return;
    }

    request = copy_cloud_request(packet);
    if (request != NULL) {
        (void)qm_agent_control_post_module_request(agent, request);
    }
    qm_agent_callback_leave(agent);
}

/**
 * @brief Queue a pointer-free cloud connection event for the Agent control task.
 */
static void cloud_event_handler(void *iot_handle,
                                const qm_iot_event_t *event,
                                void *userdata)
{
    qm_agent_handle_t agent;
    qm_agent_module_event_id_t module_event;

    QM_UNUSED_ARG(iot_handle);
    if (event == NULL) {
        return;
    }

    if (event->type == QM_IOT_EVENT_CLOUD_CONN) {
        module_event = QM_AGENT_MODULE_EVENT_CLOUD_CONNECTED;
    } else if (event->type == QM_IOT_EVENT_CLOUD_DISCONN) {
        module_event = QM_AGENT_MODULE_EVENT_CLOUD_DISCONNECTED;
    } else {
        return;
    }

    agent = callback_enter(userdata);
    if (agent != NULL) {
        (void)qm_agent_control_post_module_event(agent, module_event);
        qm_agent_callback_leave(agent);
    }
}

/**
 * @brief Validate descriptor shape before it enters the manager.
 */
static qm_bool module_desc_valid(const qm_agent_module_desc_t *desc)
{
    uint32_t i;

    if (desc == NULL || desc->name == NULL ||
        desc->version != QM_AGENT_MODULE_VERSION) {
        return QM_FALSE;
    }

    if (desc->method_count == 0) {
        return QM_TRUE;
    }

    if (desc->methods == NULL) {
        return QM_FALSE;
    }

    for (i = 0; i < desc->method_count; i++) {
        if (desc->methods[i].method == NULL ||
            desc->methods[i].handler == NULL) {
            return QM_FALSE;
        }
    }

    return QM_TRUE;
}

/**
 * @brief Ensure module manager cloud event subscription matches runtime needs.
 */
static int ensure_cloud_event_registration(qm_agent_handle_t agent)
{
    qm_agent_module_callback_registration_t *registration;
    int ret;

    if (agent == NULL || agent->config.iot_handle == NULL ||
        agent->modules.iot_event_registered) {
        return QM_EOK;
    }

    registration = agent->modules.callback_registration;
    if (registration == NULL) {
        return -QM_EINIT;
    }

    ret = qm_iot_event_register(agent->config.iot_handle,
                                cloud_event_handler,
                                (void *)(uintptr_t)registration->cookie);
    if (ret == QM_EOK) {
        agent->modules.iot_event_registered = QM_TRUE;
    }

    return ret;
}

/**
 * @brief Remove the cloud event registration before releasing its cookie.
 */
static void unregister_cloud_event(qm_agent_handle_t agent)
{
    qm_agent_module_callback_registration_t *registration;

    if (agent == NULL || !agent->modules.iot_event_registered) {
        return;
    }

    registration = agent->modules.callback_registration;
    if (agent->config.iot_handle != NULL && registration != NULL) {
        (void)qm_iot_event_unregister_one(agent->config.iot_handle,
                                          cloud_event_handler,
                                          (void *)(uintptr_t)registration->cookie);
    }
    agent->modules.iot_event_registered = QM_FALSE;
}

/**
 * @brief Find the module entry and handler for a method/submethod request.
 */
static qm_agent_module_entry_t *find_route(qm_agent_handle_t agent,
                                           const qm_agent_module_req_t *req,
                                           const qm_agent_module_method_t **method)
{
    uint32_t i;

    if (agent == NULL || req == NULL || method == NULL ||
        req->method == NULL) {
        return NULL;
    }

    for (i = 0; i < agent->modules.count; i++) {
        qm_agent_module_entry_t *entry = &agent->modules.entries[i];
        uint32_t j;

        if (entry->desc == NULL || entry->desc->methods == NULL) {
            continue;
        }

        for (j = 0; j < entry->desc->method_count; j++) {
            const qm_agent_module_method_t *candidate = &entry->desc->methods[j];

            if (module_method_match(req->method, candidate->method) &&
                module_submethod_match(req->submethod, candidate->submethod)) {
                *method = candidate;
                return entry;
            }
        }
    }

    return NULL;
}

int qm_agent_module_manager_init(qm_agent_handle_t agent)
{
    if (agent == NULL) {
        return -QM_EINVAL;
    }

    return QM_EOK;
}

int qm_agent_module_manager_register_node(qm_agent_module_registry_node_t *node)
{
    qm_agent_module_registry_node_t *it;
    const qm_agent_module_desc_t *desc;

    if (node == NULL || node->desc == NULL) {
        return -QM_EINVAL;
    }

    desc = node->desc;
    if (!module_desc_valid(desc) || desc->name == NULL) {
        return -QM_EINVAL;
    }

    for (it = s_module_registry_head; it != NULL; it = it->next) {
        if (it == node || it->desc == desc) {
            return QM_EOK;
        }

        if (it->desc != NULL && it->desc->name != NULL &&
            qm_strcmp(it->desc->name, desc->name) == 0) {
            QM_AGENT_LOGE(LOG_TAG,
                         "duplicate module name=%s", desc->name);
            return -QM_EINVAL;
        }
    }

    node->desc = desc;
    node->next = s_module_registry_head;
    s_module_registry_head = node;
    return QM_EOK;
}

const qm_agent_module_desc_t *qm_agent_module_manager_find_desc(const char *module_name)
{
    qm_agent_module_registry_node_t *node;

    if (module_name == NULL) {
        return NULL;
    }

    for (node = s_module_registry_head; node != NULL; node = node->next) {
        if (node->desc != NULL &&
            node->desc->name != NULL &&
            qm_strcmp(node->desc->name, module_name) == 0) {
            return node->desc;
        }
    }

    return NULL;
}

/**
 * @brief Validate, allocate, initialize, and register one module instance.
 * 校验、分配、初始化并注册一个模块实例。
 *
 * A partial failure rolls the candidate back before routes become visible to
 * cloud callbacks, preserving collision-free module routing.
 * 部分失败会在路由暴露给云端回调前回滚候选实例，保持模块路由无冲突。
 */
int qm_agent_module_manager_load(qm_agent_handle_t agent,
                                 const qm_agent_module_desc_t *desc,
                                 const void *config)
{
    qm_agent_module_entry_t *entries;
    qm_agent_module_entry_t *entry;
    int ret;

    if (agent == NULL || desc == NULL) {
        return -QM_EINVAL;
    }

    ret = check_module_routes_against_loaded(agent, desc);
    if (ret != QM_EOK) {
        return ret;
    }

    entries = (qm_agent_module_entry_t *)qm_realloc(
        agent->modules.entries,
        sizeof(*entries) * (agent->modules.count + 1U));
    if (entries == NULL) {
        return -QM_ENOMEM;
    }

    agent->modules.entries = entries;
    entry = &agent->modules.entries[agent->modules.count];
    qm_memset(entry, 0, sizeof(*entry));
    entry->agent = agent;
    entry->desc = desc;
    entry->module.entry = entry;

    if (desc->init != NULL) {
        ret = desc->init(&entry->module, config);
        if (ret != QM_EOK) {
            QM_AGENT_LOGE(LOG_TAG, "module init failed name=%s ret=%d",
                    desc->name, ret);
            return ret;
        }
    }

    entry->initialized = QM_TRUE;
    agent->modules.count++;
    return QM_EOK;
}

int qm_agent_module_manager_start(qm_agent_handle_t agent)
{
    uint32_t i;
    int ret;

    if (agent == NULL) {
        return -QM_EINVAL;
    }
    if (agent->modules.running || agent->modules.count == 0U) {
        return QM_EOK;
    }

    ret = callback_registration_create(agent);
    if (ret != QM_EOK) {
        return ret;
    }

    for (i = 0; i < agent->modules.count; i++) {
        qm_agent_module_entry_t *entry = &agent->modules.entries[i];

        if (entry->started) {
            continue;
        }

        if (entry->desc != NULL && entry->desc->start != NULL) {
            ret = entry->desc->start(&entry->module);
            if (ret != QM_EOK) {
                QM_AGENT_LOGE(LOG_TAG, "module start failed name=%s ret=%d",
                        entry->desc->name, ret);
                goto fail;
            }
        }

        entry->started = QM_TRUE;

        ret = register_cloud_methods(agent, i);
        if (ret != QM_EOK) {
            goto fail;
        }
    }

    ret = ensure_cloud_event_registration(agent);
    if (ret != QM_EOK) {
        goto fail;
    }

    /* Cloud callbacks can now queue work because every matching entry is
     * initialized, started, and registered with the control-plane router. */
    agent->modules.running = QM_TRUE;
    callback_registration_enable(agent);
    return QM_EOK;

fail:
    (void)qm_agent_module_manager_stop(agent);
    return ret;
}

/**
 * @brief Stop modules in reverse order after cloud callback admission is closed.
 * 关闭云端回调准入后按逆序停止模块。
 */
int qm_agent_module_manager_stop(qm_agent_handle_t agent)
{
    uint32_t i;
    int ret = QM_EOK;

    if (agent == NULL) {
        return -QM_EINVAL;
    }

    /* Stop accepting cloud ingress before routes or module state begin to
     * unwind. Any callback already copying a packet can only enqueue a request
     * that the control task drops while running is false. */
    agent->modules.running = QM_FALSE;
    callback_registration_disable(agent);
    unregister_cloud_methods(agent);
    unregister_cloud_event(agent);
    callback_registration_destroy(agent);

    i = agent->modules.count;
    while (i > 0) {
        qm_agent_module_entry_t *entry = &agent->modules.entries[i - 1];

        if (entry->started && entry->desc != NULL &&
            entry->desc->stop != NULL) {
            int stop_ret = entry->desc->stop(&entry->module);

            if (stop_ret != QM_EOK && ret == QM_EOK) {
                ret = stop_ret;
            }
        }

        entry->started = QM_FALSE;
        i--;
    }

    return ret;
}

void qm_agent_module_manager_deinit(qm_agent_handle_t agent)
{
    uint32_t i;

    if (agent == NULL) {
        return;
    }

    (void)qm_agent_module_manager_stop(agent);

    i = agent->modules.count;
    while (i > 0) {
        qm_agent_module_entry_t *entry = &agent->modules.entries[i - 1];

        if (entry->initialized && entry->desc != NULL &&
            entry->desc->deinit != NULL) {
            entry->desc->deinit(&entry->module);
        }
        entry->initialized = QM_FALSE;
        i--;
    }

    qm_free(agent->modules.entries);
    agent->modules.entries = NULL;
    agent->modules.count = 0;
    agent->modules.running = QM_FALSE;
}

int qm_agent_module_manager_emit(qm_agent_handle_t agent,
                                 const qm_agent_module_event_t *event)
{
    uint32_t i;
    int ret = QM_EOK;

    if (agent == NULL || event == NULL) {
        return -QM_EINVAL;
    }

    for (i = 0; i < agent->modules.count; i++) {
        qm_agent_module_entry_t *entry = &agent->modules.entries[i];

        if (entry->desc != NULL && entry->desc->on_event != NULL) {
            int event_ret = entry->desc->on_event(&entry->module, event);

            if (event_ret != QM_EOK && ret == QM_EOK) {
                ret = event_ret;
            }
        }
    }

    return ret;
}

/**
 * @brief Dispatch an owned cloud request from the serialized control task.
 */
int qm_agent_module_manager_dispatch_cloud_request(
    qm_agent_handle_t agent,
    qm_agent_module_cloud_request_t *request)
{
    if (agent == NULL || request == NULL) {
        return -QM_EINVAL;
    }
    if (agent->destroying || !agent->modules.running) {
        return -QM_EBUSY;
    }

    return qm_agent_module_dispatch(agent, &request->request);
}

/**
 * @brief Deliver a cloud lifecycle transition from the serialized control task.
 */
int qm_agent_module_manager_emit_cloud_event(
    qm_agent_handle_t agent,
    qm_agent_module_event_id_t event_id)
{
    qm_agent_module_event_t event;

    if (agent == NULL) {
        return -QM_EINVAL;
    }
    if (agent->destroying || !agent->modules.running) {
        return -QM_EBUSY;
    }
    if (event_id != QM_AGENT_MODULE_EVENT_CLOUD_CONNECTED &&
        event_id != QM_AGENT_MODULE_EVENT_CLOUD_DISCONNECTED) {
        return -QM_EINVAL;
    }

    qm_memset(&event, 0, sizeof(event));
    event.id = event_id;
    return qm_agent_module_manager_emit(agent, &event);
}

/**
 * @brief Resolve and invoke one module route on the serialized Agent path.
 * 在串行 Agent 路径上解析并调用一条模块路由。
 */
int qm_agent_module_dispatch(qm_agent_handle_t agent,
                             const qm_agent_module_req_t *req)
{
    qm_agent_module_entry_t *entry;
    const qm_agent_module_method_t *method = NULL;

    if (agent == NULL || req == NULL || req->method == NULL) {
        return -QM_EINVAL;
    }

    entry = find_route(agent, req, &method);
    if (entry == NULL || method == NULL || method->handler == NULL) {
        QM_AGENT_LOGE(LOG_TAG, "route not found method=%s submethod=%s",
                req->method, module_submethod_or_default(req->submethod));
        return -QM_ENOENT;
    }

    return method->handler(&entry->module, req);
}

qm_agent_module_t *qm_agent_module_find(qm_agent_handle_t agent,
                                        const qm_agent_module_desc_t *desc)
{
    uint32_t i;

    if (agent == NULL || desc == NULL) {
        return NULL;
    }

    for (i = 0; i < agent->modules.count; i++) {
        qm_agent_module_entry_t *entry = &agent->modules.entries[i];

        if (entry->desc == desc) {
            return &entry->module;
        }
    }

    return NULL;
}

qm_agent_handle_t qm_agent_module_get_agent(qm_agent_module_t *module)
{
    if (module == NULL || module->entry == NULL) {
        return NULL;
    }

    return module->entry->agent;
}

void *qm_agent_module_get_ctx(qm_agent_module_t *module)
{
    if (module == NULL || module->entry == NULL) {
        return NULL;
    }

    return module->entry->ctx;
}

void qm_agent_module_set_ctx(qm_agent_module_t *module, void *ctx)
{
    if (module == NULL || module->entry == NULL) {
        return;
    }

    module->entry->ctx = ctx;
}

int qm_agent_module_reply(qm_agent_module_t *module,
                          const qm_agent_module_req_t *req,
                          int code,
                          const char *payload,
                          uint32_t payload_len)
{
    qm_agent_handle_t agent;

    if (module == NULL || req == NULL) {
        return -QM_EINVAL;
    }

    agent = qm_agent_module_get_agent(module);
    if (agent == NULL) {
        return -QM_EINVAL;
    }

    if (req->source != QM_AGENT_MODULE_REQ_SOURCE_CLOUD) {
        return QM_EOK;
    }

    if (agent->config.iot_handle == NULL || req->method == NULL) {
        return -QM_EINVAL;
    }

    {
        qm_iot_down_req_t reply_req = {0};

        reply_req.method = (req->reply_method != NULL) ? req->reply_method : req->method;
        reply_req.submethod = module_submethod_or_default(
            req->reply_submethod != NULL ? req->reply_submethod : req->submethod);
        reply_req.msg_id = req->msg_id;

        return qm_iot_down_reply(agent->config.iot_handle,
                                 &reply_req,
                                 code,
                                 (char *)payload,
                                 (int)payload_len);
    }
}

int qm_agent_module_report(qm_agent_module_t *module,
                           const char *method,
                           const char *submethod,
                           const char *payload,
                           uint32_t payload_len)
{
    qm_agent_handle_t agent;

    if (module == NULL || method == NULL) {
        return -QM_EINVAL;
    }

    agent = qm_agent_module_get_agent(module);
    if (agent == NULL || agent->config.iot_handle == NULL) {
        return -QM_EINVAL;
    }

    return qm_iot_up_report_ex(agent->config.iot_handle,
                               (char *)method,
                               (char *)submethod,
                               0,
                               (char *)payload,
                               (int)payload_len);
}
