#ifndef QM_AGENT_MODULE_H
#define QM_AGENT_MODULE_H

/**
 * @file qm_agent_module.h
 * @brief Internal qm_agent module extension and manager definitions.
 *
 * qm_agent 托管模块扩展和管理器定义。模块生命周期从属于 Agent 会话；云端请求先复制
 * 并投递到控制任务，再调用模块回调，避免在 MQTT 接收回调中执行应用逻辑。
 */

#include "qm_agent_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Current module descriptor ABI version. 当前模块描述符 ABI 版本。 */
#define QM_AGENT_MODULE_VERSION 1U

/** @brief Opaque qm_agent module runtime handle. 不透明的 qm_agent 模块运行时句柄。 */
typedef struct qm_agent_module qm_agent_module_t;

/** @brief Runtime entry behind a public module handle. 公共模块句柄对应的运行时条目。 */
typedef struct qm_agent_module_entry qm_agent_module_entry_t;

/** @brief Built-in module registration node. 内置模块注册节点。 */
typedef struct qm_agent_module_registry_node qm_agent_module_registry_node_t;

/** @brief Private cookie identity used by cloud callback registration. 云端回调注册使用的私有 cookie 标识。 */
typedef struct qm_agent_module_callback_registration
    qm_agent_module_callback_registration_t;

/**
 * @brief Request source used by the module router.
 * 模块路由器的请求来源。云端请求会复制后进入 Agent 控制任务。
 */
typedef enum {
    /** @brief Request was issued by local application code. */
    QM_AGENT_MODULE_REQ_SOURCE_LOCAL = 0,

    /** @brief Request was received from qm_cloud method downlink. */
    QM_AGENT_MODULE_REQ_SOURCE_CLOUD,
} qm_agent_module_req_source_t;

/**
 * @brief Method request routed to a qm_agent module.
 *
 * method and submethod are the routing keys. params is an optional JSON
 * fragment or caller-defined payload and is only valid for the handler call.
 * method 和 submethod 为路由键；params 是可选 JSON 片段或调用方定义的载荷，只在处理器
 * 调用期间有效。
 */
typedef struct {
    /** @brief Request source. */
    qm_agent_module_req_source_t source;

    /**
     * @brief Platform message id.
     *
     * Zero for local requests or auto-generated reports.
     */
    uint32_t msg_id;

    /** @brief Top-level method field used as the first route key. */
    const char *method;

    /** @brief Top-level submethod field used as the second route key. */
    const char *submethod;

    /** @brief Optional reply method override for cloud ACK messages. */
    const char *reply_method;

    /** @brief Optional reply subMethod override for cloud ACK messages. */
    const char *reply_submethod;

    /** @brief Optional JSON params fragment or caller-defined payload. */
    const char *params;

    /** @brief Byte length of params. */
    uint32_t params_len;

    /** @brief Optional caller context passed through dispatch. */
    void *user_data;
} qm_agent_module_req_t;

/**
 * @brief Event type delivered to modules.
 * 交付给模块的生命周期事件类型。
 */
typedef enum {
    /** @brief No module event. */
    QM_AGENT_MODULE_EVENT_NONE = 0,

    /** @brief qm_agent has started its runtime path. */
    QM_AGENT_MODULE_EVENT_AGENT_STARTED,

    /** @brief qm_agent is about to stop its runtime path. */
    QM_AGENT_MODULE_EVENT_AGENT_STOPPING,

    /** @brief qm_cloud reported cloud connection ready. */
    QM_AGENT_MODULE_EVENT_CLOUD_CONNECTED,

    /** @brief qm_cloud reported cloud disconnection. */
    QM_AGENT_MODULE_EVENT_CLOUD_DISCONNECTED,

    /**
     * @brief Complete device command on the control task. / 控制任务中的完整设备指令。
     * data points to a borrowed qm_agent_event_t with id QM_AGENT_EVENT_COMMAND;
     * copy needed fields before returning if processing asynchronously.
     * data 为借用的 qm_agent_event_t，id 为 QM_AGENT_EVENT_COMMAND；异步处理前须复制所需字段。
     */
    QM_AGENT_MODULE_EVENT_COMMAND,
} qm_agent_module_event_id_t;

/**
 * @brief Module event payload.
 * 模块事件载荷。data 为借用字段，模块不得在回调返回后保存。
 */
typedef struct {
    /** @brief Event identifier. */
    qm_agent_module_event_id_t id;

    /** @brief Optional event-specific status code. */
    int code;

    /**
     * @brief Optional borrowed event payload.
     *
     * Cloud connection events cross into the Agent control task and therefore
     * carry no qm_cloud-owned payload. Their data and data_len are NULL/zero.
     */
    const void *data;

    /** @brief Byte length of data when applicable. */
    uint32_t data_len;
} qm_agent_module_event_t;

/**
 * @brief Module method handler callback.
 * 模块方法处理回调。req 在回调期间借用；如需异步处理，模块必须先复制需要的数据。
 *
 * @param module Module runtime handle.
 * @param req Borrowed request valid during the callback.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
typedef int (*qm_agent_module_method_cb_t)(qm_agent_module_t *module,
                                           const qm_agent_module_req_t *req);

/**
 * @brief One method/submethod handler owned by a module.
 * 模块拥有的一条 method/submethod 路由规则。
 */
typedef struct {
    /** @brief Top-level method value. */
    const char *method;

    /** @brief Top-level submethod value. */
    const char *submethod;

    /** @brief Handler invoked when method and submethod both match. */
    qm_agent_module_method_cb_t handler;
} qm_agent_module_method_t;

/**
 * @brief Module descriptor registered with qm_agent.
 * 注册到 qm_agent 的模块描述符。描述符及路由表必须在模块实例整个生命周期内保持有效。
 */
typedef struct {
    /** @brief Descriptor ABI version, must be QM_AGENT_MODULE_VERSION. */
    uint16_t version;

    /** @brief Stable module name used for logs and diagnostics. */
    const char *name;

    /** @brief Optional module initialization callback. */
    int (*init)(qm_agent_module_t *module, const void *config);

    /** @brief Optional callback invoked when qm_agent starts. */
    int (*start)(qm_agent_module_t *module);

    /** @brief Optional callback invoked when qm_agent stops. */
    int (*stop)(qm_agent_module_t *module);

    /** @brief Optional module cleanup callback. */
    void (*deinit)(qm_agent_module_t *module);

    /** @brief Optional callback for lifecycle events and device commands. / 生命周期与设备指令回调。 */
    int (*on_event)(qm_agent_module_t *module,
                    const qm_agent_module_event_t *event);

    /** @brief Static method/submethod route table owned by the module. */
    const qm_agent_module_method_t *methods;

    /** @brief Number of entries in methods. */
    uint32_t method_count;
} qm_agent_module_desc_t;

/**
 * @brief Concrete module handle shared with module callbacks.
 * 传给模块回调的具体模块句柄。
 */
struct qm_agent_module {
    /** @brief Back pointer to the module manager entry. */
    qm_agent_module_entry_t *entry;
};

/**
 * @brief One module instance owned by qm_agent.
 * 由 qm_agent 所有的一个模块实例。模块私有 ctx 的创建和释放由模块实现负责。
 */
struct qm_agent_module_entry {
    /** @brief Owning qm_agent handle. */
    qm_agent_handle_t agent;

    /** @brief Static descriptor provided by the component module. */
    const qm_agent_module_desc_t *desc;

    /** @brief Stable handle passed into module callbacks. */
    qm_agent_module_t module;

    /** @brief Module private context owned by module implementation. */
    void *ctx;

    /** @brief Whether init callback completed successfully. */
    qm_bool initialized;

    /** @brief Whether start callback completed successfully. */
    qm_bool started;

    /** @brief Whether at least one cloud method was registered for this entry. */
    qm_bool method_registered;
};

/**
 * @brief One built-in module registration node.
 */
struct qm_agent_module_registry_node {
    /** @brief Built-in module descriptor. */
    const qm_agent_module_desc_t *desc;

    /** @brief Next registered module node. */
    qm_agent_module_registry_node_t *next;
};

/**
 * @brief Module manager runtime stored in struct qm_agent.
 * 保存在 struct qm_agent 中的模块管理器运行时状态。
 */
typedef struct {
    /** @brief Dynamically allocated module entries. */
    qm_agent_module_entry_t *entries;

    /** @brief Number of entries. */
    uint32_t count;

    /** @brief Whether qm_cloud event callback is registered. */
    qm_bool iot_event_registered;

    /** @brief Modules are started and may accept cloud-originated requests. */
    qm_bool running;

    /** @brief Private never-reused identity for qm_cloud callbacks. */
    qm_agent_module_callback_registration_t *callback_registration;
} qm_agent_module_manager_t;

/**
 * @brief Find a module runtime handle by descriptor.
 *
 * @param agent qm_agent session handle.
 * @param desc Module descriptor defined by the module implementation.
 * @return Module runtime handle, or NULL when not found.
 */
qm_agent_module_t *qm_agent_module_find(qm_agent_handle_t agent,
                                        const qm_agent_module_desc_t *desc);

/**
 * @brief Dispatch a local request through the same router used by cloud methods.
 * 通过与云端方法相同的路由器分发本地请求。
 *
 * @param agent qm_agent session handle.
 * @param req Method request to dispatch.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_module_dispatch(qm_agent_handle_t agent,
                             const qm_agent_module_req_t *req);

/**
 * @brief Get the owning qm_agent handle from a module context.
 *
 * @param module Module runtime handle.
 * @return Owning qm_agent handle, or NULL when module is invalid.
 */
qm_agent_handle_t qm_agent_module_get_agent(qm_agent_module_t *module);

/**
 * @brief Get private module context.
 *
 * @param module Module runtime handle.
 * @return Module private context pointer, or NULL when unset.
 */
void *qm_agent_module_get_ctx(qm_agent_module_t *module);

/**
 * @brief Set private module context.
 *
 * @param module Module runtime handle.
 * @param ctx Module private context pointer.
 */
void qm_agent_module_set_ctx(qm_agent_module_t *module, void *ctx);

/**
 * @brief Reply to a method request.
 *
 * For cloud requests this will be wired to qm_cloud method replies. For local
 * requests the function currently validates the input and returns QM_EOK.
 * 云端请求会生成 qm_cloud 方法回复；本地请求当前仅验证输入并返回 QM_EOK。
 *
 * @param module Module runtime handle.
 * @param req Original request to reply to.
 * @param code Business result code.
 * @param payload Optional JSON params fragment.
 * @param payload_len Byte length of payload.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_module_reply(qm_agent_module_t *module,
                          const qm_agent_module_req_t *req,
                          int code,
                          const char *payload,
                          uint32_t payload_len);

/**
 * @brief Report an event or state through the module transport.
 * 通过模块传输上报事件或状态。payload 仅在本次调用期间借用。
 *
 * @param module Module runtime handle.
 * @param method Top-level method value.
 * @param submethod Top-level submethod value reserved for module routing.
 * @param payload Optional JSON params fragment.
 * @param payload_len Byte length of payload.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_module_report(qm_agent_module_t *module,
                           const char *method,
                           const char *submethod,
                           const char *payload,
                           uint32_t payload_len);

/**
 * @brief Initialize module manager runtime for a qm_agent session.
 * 初始化某个 qm_agent 会话的模块管理器运行时。
 *
 * @param agent qm_agent session handle.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_module_manager_init(qm_agent_handle_t agent);

/**
 * @brief Load one module instance into the manager.
 * 向管理器加载一个模块实例。应在 qm_agent_start() 前完成加载。
 *
 * @param agent qm_agent session handle.
 * @param desc Static module descriptor.
 * @param config Optional descriptor-specific configuration.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_module_manager_load(qm_agent_handle_t agent,
                                 const qm_agent_module_desc_t *desc,
                                 const void *config);

/**
 * @brief Register one built-in module node.
 *
 * @param node Built-in module registration node.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_module_manager_register_node(qm_agent_module_registry_node_t *node);

/**
 * @brief Resolve one built-in module descriptor by name.
 *
 * @param module_name Built-in module name.
 * @return Static module descriptor, or NULL when not found or not enabled.
 */
const qm_agent_module_desc_t *qm_agent_module_manager_find_desc(const char *module_name);

/**
 * @brief Start all initialized modules.
 * 按加载顺序启动所有已初始化模块。
 *
 * @param agent qm_agent session handle.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_module_manager_start(qm_agent_handle_t agent);

/**
 * @brief Stop all started modules in reverse order.
 * 按与启动相反的顺序停止所有已启动模块。
 *
 * @param agent qm_agent session handle.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
int qm_agent_module_manager_stop(qm_agent_handle_t agent);

/**
 * @brief Deinitialize all modules and release module manager resources.
 * 反初始化全部模块并释放模块管理器资源。
 *
 * @param agent qm_agent session handle.
 */
void qm_agent_module_manager_deinit(qm_agent_handle_t agent);

/**
 * @brief Fan out an event to all modules.
 * 向所有模块分发事件。event 为借用载荷，仅在该调用期间有效。
 *
 * @param agent qm_agent session handle.
 * @param event Borrowed event payload.
 * @return QM_EOK on success, first negative qm_errno value on failure.
 */
int qm_agent_module_manager_emit(qm_agent_handle_t agent,
                                 const qm_agent_module_event_t *event);

/**
 * @brief Auto-register one built-in module descriptor at startup.
 *
 * Use this macro in a module implementation file after the descriptor
 * definition. The registration function runs during startup and keeps the
 * descriptor available for qm_agent_load_module().
 * 在模块实现文件的描述符定义之后使用此宏。注册函数在启动时运行，使描述符可被
 * qm_agent_load_module() 查找。
 */
#define QM_AGENT_MODULE_AUTOREGISTER(desc_symbol)                             \
    static qm_agent_module_registry_node_t desc_symbol##_registry_node = {    \
        .desc = &(desc_symbol),                                               \
        .next = NULL,                                                         \
    };                                                                        \
                                                                              \
    static void __attribute__((constructor))                                  \
    desc_symbol##_register_constructor(void)                                  \
    {                                                                         \
        (void)qm_agent_module_manager_register_node(                          \
            &desc_symbol##_registry_node);                                    \
    }

#ifdef __cplusplus
}
#endif

#endif
