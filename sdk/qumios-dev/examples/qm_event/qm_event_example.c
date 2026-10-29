#include "qm.h"
#include "qm_work.h"
#include "qm_log.h"

#define LOG_TAG "qm_event"

/**
 * Application event base used by this example.
 */
QM_EVENT_DEFINE_BASE(QM_EXAMPLE_EVENT);

/**
 * Event id used to demonstrate qm_event dispatch.
 */
#define QM_EXAMPLE_EVENT_ID_START      1

/**
 * First handler argument, used to show independent callback registration.
 */
static char *g_handler_arg_a = "handler_a";

/**
 * Second handler argument, used to show independent callback registration.
 */
static char *g_handler_arg_b = "handler_b";

/**
 * Event loop handle owned by this example.
 */
static qm_event_handle_t g_example_loop = NULL;

/**
 * Handle events posted to the example event loop.
 *
 * @param[in] event_handler_arg User argument registered with this handler.
 * @param[in] event_base Event base that identifies the event source.
 * @param[in] event_id Event id that identifies the event.
 * @param[in] event_data Event payload copied by qm_event_post_to().
 * @param[in] event_data_size Event payload size in bytes.
 */
static void example_event_handler(void *event_handler_arg, qm_event_base_t event_base,
                                  int32_t event_id, void *event_data, size_t event_data_size)
{
    char *payload = (char *)event_data;

    QM_UNUSED_ARG(event_data_size);

    QM_LOGD(LOG_TAG, "base: %s, id: %d, arg: %s, data: %s",
            event_base, event_id, (char *)event_handler_arg, payload);
}

void qm_application_start(void)
{
    int ret = 0;
    char *payload = "hello qm_event";
    qm_event_args_t loop_args = {
        .queue_size = CONFIG_QM_WORKQUEUE_NUM,
        .task_name = NULL,
    };

    ret = qm_event_create(&loop_args, &g_example_loop);
    QM_LOGD(LOG_TAG, "create loop ret: %d", ret);
    if(ret != QM_EOK){
        return;
    }

    ret = qm_event_handler_register_with(g_example_loop, QM_EXAMPLE_EVENT, QM_EXAMPLE_EVENT_ID_START,
                                         example_event_handler, g_handler_arg_a);
    QM_LOGD(LOG_TAG, "register handler a ret: %d", ret);

    ret = qm_event_handler_register_with(g_example_loop, QM_EXAMPLE_EVENT, QM_EXAMPLE_EVENT_ID_START,
                                         example_event_handler, g_handler_arg_b);
    QM_LOGD(LOG_TAG, "register handler b ret: %d", ret);

    ret = qm_event_post_to(g_example_loop, QM_EXAMPLE_EVENT, QM_EXAMPLE_EVENT_ID_START,
                           payload, strlen(payload) + 1, QM_NO_WAIT);
    QM_LOGD(LOG_TAG, "post ret: %d", ret);

    qm_event_run(g_example_loop, QM_NO_WAIT);
    (void)qm_event_handler_unregister_with(g_example_loop,
                                           QM_EXAMPLE_EVENT,
                                           QM_EXAMPLE_EVENT_ID_START,
                                           example_event_handler,
                                           g_handler_arg_b);
    (void)qm_event_handler_unregister_with(g_example_loop,
                                           QM_EXAMPLE_EVENT,
                                           QM_EXAMPLE_EVENT_ID_START,
                                           example_event_handler,
                                           g_handler_arg_a);
    (void)qm_event_delete(g_example_loop);
    g_example_loop = NULL;
}
