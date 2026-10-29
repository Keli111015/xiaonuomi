#ifndef __QM_EVENT_H__
#define __QM_EVENT_H__

#include "qm_kernel.h"
#include "qm_types.h"
#include "qm_errno.h"
#include "qm_config.h"

#ifdef __cplusplus
extern "C" {
#endif


#if CONFIG_QM_WORK_SUPPORT


#ifndef CONFIG_QM_WORKQUEUE_TASK_NAME
/**
 * Default worker task name used by qm_event_default_create().
 */
#define CONFIG_QM_WORKQUEUE_TASK_NAME      "qm_workqueue"
#endif


#ifndef CONFIG_QM_WORKQUEUE_NO_TASK
/**
 * Whether the default workqueue runs without a dedicated worker task.
 */
#define CONFIG_QM_WORKQUEUE_NO_TASK         0
#endif

#ifndef CONFIG_QM_WORKQUEUE_TASK_PRIO
/**
 * Default worker task priority used by the default event loop in task mode.
 */
#define CONFIG_QM_WORKQUEUE_TASK_PRIO       20
#endif

#ifndef CONFIG_QM_WORKQUEUE_TASK_SIZE
/**
 * Default worker task stack size in bytes used by the default event loop in task mode.
 */
#define CONFIG_QM_WORKQUEUE_TASK_SIZE       2048
#endif

#ifndef CONFIG_QM_WORKQUEUE_NUM
/**
 * Default workqueue length used when CONFIG_QM_WORKQUEUE_NUM is not provided.
 */
#define CONFIG_QM_WORKQUEUE_NUM             16
#endif

#ifndef CONFIG_QM_EVENT_NUM
/**
 * Default legacy event handler slot count used when CONFIG_QM_EVENT_NUM is not provided.
 */
#define CONFIG_QM_EVENT_NUM                 16
#endif


/**
 * Declare an event base symbol in a header.
 */
#ifndef QM_EVENT_DECLARE_BASE
#define QM_EVENT_DECLARE_BASE(id)           extern qm_event_base_t const id
#endif

/**
 * Define an event base symbol in one source file.
 */
#ifndef QM_EVENT_DEFINE_BASE
#define QM_EVENT_DEFINE_BASE(id)            qm_event_base_t const id = #id
#endif

/**
 * Wildcard event base used when a handler wants to receive all event bases.
 */
#define QM_EVENT_ANY_BASE                   NULL

/**
 * Wildcard event id used when a handler wants to receive all event ids in a base.
 */
#define QM_EVENT_ANY_ID                     (-1)

typedef const char *qm_event_base_t;

typedef struct qm_event *qm_event_handle_t;

/**
 * Event handler registration instance. Reserved for future instance unregister APIs.
 */
typedef void *qm_event_handler_instance_t;

/**
 * Event loop creation arguments.
 */
typedef struct {
    /**
     * Queue length for pending events. Uses CONFIG_QM_EVENT_LOOP_QUEUE_NUM when zero.
     */
    uint32_t queue_size;
    /**
     * Optional task name. When NULL, no task is created and qm_event_run() must be called by the user.
     */
    const char *task_name;
    /**
     * Event loop task priority. Ignored when task_name is NULL.
     */
    int task_priority;
    /**
     * Event loop task stack size in bytes. Ignored when task_name is NULL.
     */
    int task_stack_size;

} qm_event_args_t;

/**
 * Event loop handler callback.
 *
 * @param[in] event_handler_arg User argument passed during registration.
 * @param[in] event_base Event base that identifies the event source.
 * @param[in] event_id Event id that identifies the event within event_base.
 * @param[in] event_data Event payload. The payload lifetime is limited to this callback.
 * @param[in] event_data_size Event payload size in bytes.
 */
typedef void (*qm_event_handler_t)(void *event_handler_arg, qm_event_base_t event_base,
                                   int32_t event_id, void *event_data, size_t event_data_size);

/**
 * Create a qm event loop.
 *
 * @param[in] args Event loop configuration.
 * @param[out] event_loop Created event loop handle.
 *
 * @return the operation status, 0 is OK, others is error.
 */
int32_t qm_event_create(const qm_event_args_t *event_loop_args, qm_event_handle_t *event_loop);

/**
 * Create the default qm event loop.
 *
 * @return the operation status, 0 is OK, others is error.
 */
int32_t qm_event_default_create(void);

/**
 * Delete a qm event loop and release resources owned by this module.
 * Loops created with task_name are not deleted by this API because task shutdown is platform dependent.
 *
 * @param[in] event_loop Event loop handle to delete.
 *
 * @return the operation status, 0 is OK, others is error.
 */
int32_t qm_event_delete(qm_event_handle_t event_loop);

/**
 * Delete the default qm event loop.
 *
 * @return the operation status, 0 is OK, others is error.
 */
int32_t qm_event_default_delete(void);

/**
 * Run one dispatch pass on an event loop created without a task.
 *
 * @param[in] event_loop Event loop handle to run.
 * @param[in] ticks_to_run Maximum wait time in milliseconds for one pending event.
 *
 * @return the operation status, 0 is OK, others is error.
 */
int32_t qm_event_run(qm_event_handle_t event_loop, uint32_t ticks_to_run);

/**
 * Run one dispatch pass on the default event loop.
 *
 * @param[in] ticks_to_run Maximum wait time in milliseconds for one pending event.
 *
 * @return the operation status, 0 is OK, others is error.
 */
int32_t qm_event_default_run(uint32_t ticks_to_run);

/**
 * Register an event handler to the default event loop.
 *
 * @param[in] event_base Event base or QM_EVENT_ANY_BASE.
 * @param[in] event_id Event id or QM_EVENT_ANY_ID.
 * @param[in] event_handler Handler callback.
 * @param[in] event_handler_arg User argument passed to handler.
 *
 * @return the operation status, 0 is OK, others is error.
 */
int32_t qm_event_handler_register(qm_event_base_t event_base, int32_t event_id,
                                       qm_event_handler_t event_handler, void *event_handler_arg);

/**
 * Register an event handler to a specific event loop.
 *
 * @param[in] event_loop Event loop handle.
 * @param[in] event_base Event base or QM_EVENT_ANY_BASE.
 * @param[in] event_id Event id or QM_EVENT_ANY_ID.
 * @param[in] event_handler Handler callback.
 * @param[in] event_handler_arg User argument passed to handler.
 *
 * @return the operation status, 0 is OK, others is error.
 */
int32_t qm_event_handler_register_with(qm_event_handle_t event_loop, qm_event_base_t event_base,
                                       int32_t event_id, qm_event_handler_t event_handler,
                                       void *event_handler_arg);

/**
 * Unregister an event handler from the default event loop.
 *
 * @param[in] event_base Event base used during registration.
 * @param[in] event_id Event id used during registration.
 * @param[in] event_handler Handler callback used during registration.
 * @param[in] event_handler_arg User argument used during registration.
 *
 * @return the operation status, 0 is OK, others is error.
 */
int32_t qm_event_handler_unregister(qm_event_base_t event_base, int32_t event_id,
                                         qm_event_handler_t event_handler, void *event_handler_arg);

/**
 * Unregister an event handler from a specific event loop.
 *
 * @param[in] event_loop Event loop handle.
 * @param[in] event_base Event base used during registration.
 * @param[in] event_id Event id used during registration.
 * @param[in] event_handler Handler callback used during registration.
 * @param[in] event_handler_arg User argument used during registration.
 *
 * @return the operation status, 0 is OK, others is error.
 */
int32_t qm_event_handler_unregister_with(qm_event_handle_t event_loop, qm_event_base_t event_base,
                                         int32_t event_id, qm_event_handler_t event_handler,
                                         void *event_handler_arg);

/**
 * Post an event to the default event loop.
 *
 * @param[in] event_base Event base that identifies the event source.
 * @param[in] event_id Event id that identifies the event within event_base.
 * @param[in] event_data Event payload. When event_data_size is non-zero, payload is copied.
 * @param[in] event_data_size Payload size in bytes.
 * @param[in] ticks_to_wait Maximum wait time in milliseconds for queue send.
 *
 * @return the operation status, 0 is OK, others is error.
 */
int32_t qm_event_post(qm_event_base_t event_base, int32_t event_id,
                           const void *event_data, size_t event_data_size, uint32_t ticks_to_wait);

/**
 * Post an event to a qm event loop.
 *
 * @param[in] event_loop Target event loop handle.
 * @param[in] event_base Event base that identifies the event source.
 * @param[in] event_id Event id that identifies the event within event_base.
 * @param[in] event_data Event payload. When event_data_size is non-zero, payload is copied.
 * @param[in] event_data_size Payload size in bytes.
 * @param[in] ticks_to_wait Maximum wait time in milliseconds for queue send.
 *
 * @return the operation status, 0 is OK, others is error.
 */
int32_t qm_event_post_to(qm_event_handle_t event_loop, qm_event_base_t event_base, int32_t event_id,
                         const void *event_data, size_t event_data_size, uint32_t ticks_to_wait);

/**
 * Post an event to the default event loop from interrupt context.
 *
 * @param[in] event_base Event base that identifies the event source.
 * @param[in] event_id Event id that identifies the event within event_base.
 * @param[in] event_data Event payload. When event_data_size is non-zero, payload is copied.
 * @param[in] event_data_size Payload size in bytes.
 *
 * @return the operation status, 0 is OK, others is error.
 */
int32_t qm_event_isr_post(qm_event_base_t event_base, int32_t event_id,
                               const void *event_data, size_t event_data_size);

/**
 * Post an event to a qm event loop from interrupt context.
 *
 * @param[in] event_loop Target event loop handle.
 * @param[in] event_base Event base that identifies the event source.
 * @param[in] event_id Event id that identifies the event within event_base.
 * @param[in] event_data Event payload. When event_data_size is non-zero, payload is copied.
 * @param[in] event_data_size Payload size in bytes.
 *
 * @return the operation status, 0 is OK, others is error.
 */
int32_t qm_event_isr_post_to(qm_event_handle_t event_loop, qm_event_base_t event_base, int32_t event_id,
                             const void *event_data, size_t event_data_size);

#endif

#ifdef __cplusplus
}
#endif

#endif
