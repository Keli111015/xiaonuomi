#include "qm.h"
#include "qm_lpc.h"
#include "qm_lpc_hal.h"
#include "qm_utils_list.h"


typedef struct{
    uint32_t lpc_id;
    qm_lpc_mode_t mode;
    lpc_callback_t resume;
    lpc_callback_t prepare;
    qm_list_t   check_list;
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_t  check_mutex;
#endif
}qm_lpc_ctx_t;

static qm_lpc_ctx_t g_lpc_ctx = {0};
static bool_t g_lpc_initialized = QM_FALSE;

static bool_t lpc_mode_is_valid(qm_lpc_mode_t mode)
{
    return mode >= QM_LPC_NO_SLEEP && mode < QM_LPC_MAX;
}

int32_t qm_lpc_init(void)
{
    int32_t ret = QM_EOK;

    if (g_lpc_initialized) {
        return QM_EOK;
    }

    g_lpc_initialized = QM_FALSE;
    memset(&g_lpc_ctx, 0, sizeof(qm_lpc_ctx_t));
#if CONFIG_QM_OS_SUPPORT
    ret = qm_mutex_new(&g_lpc_ctx.check_mutex);
    if (ret != QM_EOK) {
        return ret;
    }
#endif

    ret = qm_hal_lpc_init();
    if (ret != QM_EOK) {
#if CONFIG_QM_OS_SUPPORT
        (void)qm_mutex_free(&g_lpc_ctx.check_mutex);
#endif
        return ret;
    }

    g_lpc_initialized = QM_TRUE;
    return QM_EOK;
}

int32_t qm_lpc_wakeup_io_config(uint8_t pin, uint16_t type, lpc_callback_t wakeup_cb, bool_t enable)
{
    if (!g_lpc_initialized) {
        return -QM_EINIT;
    }

    return qm_hal_lpc_wakeup_io_config(pin, type, wakeup_cb, enable);
}

int32_t qm_lpc_mode_set(qm_lpc_mode_t mode)
{
    int32_t ret;

    if (!g_lpc_initialized) {
        return -QM_EINIT;
    }
    if (!lpc_mode_is_valid(mode)) {
        return -QM_EINVAL;
    }

    ret = qm_hal_lpc_mode_set(mode);
    if (ret == QM_EOK) {
        g_lpc_ctx.mode = mode;
    }
    return ret;
}

qm_lpc_mode_t qm_lpc_mode_get(void)
{
    return g_lpc_ctx.mode;
}


void *qm_lpc_check_register(lpc_callback_t callback)
{
    qm_list_node_t *node = NULL;

    if (!g_lpc_initialized || callback == NULL) {
        return NULL;
    }

    node = qm_list_node_new((void *)callback);
    if (node == NULL) {
        return NULL;
    }

#if CONFIG_QM_OS_SUPPORT
    qm_mutex_lock(&g_lpc_ctx.check_mutex, QM_WAIT_FOREVER);
#endif
    qm_list_rpush(&g_lpc_ctx.check_list, node);
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_unlock(&g_lpc_ctx.check_mutex);
#endif
    return (void *)node;
}

int32_t qm_lpc_check_unregister(void *handle)
{
    if (!g_lpc_initialized) {
        return -QM_EINIT;
    }
    if (handle == NULL) {
        return -QM_EINVAL;
    }

    qm_list_node_t *node = (qm_list_node_t *)handle;
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_lock(&g_lpc_ctx.check_mutex, QM_WAIT_FOREVER);
#endif
    qm_list_remove(&g_lpc_ctx.check_list, node);
    qm_list_node_destroy(node);
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_unlock(&g_lpc_ctx.check_mutex);
#endif
    return QM_EOK;
}


int32_t __attribute__((weak))  qm_hal_lpc_enable(void)
{
    return QM_EOK;
}

int32_t  __attribute__((weak)) qm_hal_lpc_disable(void)
{
    return QM_EOK;
}

int32_t __attribute__((weak)) qm_hal_lpc_enter_powerdown(void)
{
    return -QM_ENOSYS;
}

qm_lpc_wakeup_reason_t __attribute__((weak)) qm_hal_lpc_wakeup_reason_get(void)
{
    return QM_LPC_WAKEUP_REASON_NONE;
}

int32_t __attribute__((weak)) qm_hal_lpc_wakeup_reason_clear(void)
{
    return -QM_ENOSYS;
}

int32_t qm_lpc_veto_add(qm_lpc_id_t id)
{
    if (!g_lpc_initialized) {
        return -QM_EINIT;
    }
    if (id == 0) {
        return -QM_EINVAL;
    }

#if CONFIG_QM_OS_SUPPORT
    qm_mutex_lock(&g_lpc_ctx.check_mutex, QM_WAIT_FOREVER); 
#endif
    g_lpc_ctx.lpc_id |= id;
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_unlock(&g_lpc_ctx.check_mutex);
#endif  

    if(g_lpc_ctx.lpc_id){
        qm_hal_lpc_disable();
    }

    return QM_EOK;
}

int32_t qm_lpc_veto_remove(qm_lpc_id_t id)
{
    if (!g_lpc_initialized) {
        return -QM_EINIT;
    }
    if (id == 0) {
        return -QM_EINVAL;
    }

#if CONFIG_QM_OS_SUPPORT
    qm_mutex_lock(&g_lpc_ctx.check_mutex, QM_WAIT_FOREVER);
#endif
    g_lpc_ctx.lpc_id &= ~id;
#if CONFIG_QM_OS_SUPPORT
    qm_mutex_unlock(&g_lpc_ctx.check_mutex);
#endif  

    if(!g_lpc_ctx.lpc_id){
        qm_hal_lpc_enable();
    }

    return QM_EOK;
}

int32_t qm_lpc_check(void)
{
    qm_list_iterator_t self;
    qm_list_node_t *node = NULL;
    lpc_callback_t *callbacks = NULL;
    int callback_count;
    int callback_index;
    int callback_total;
    int check_ret = QM_LPC_DEEP_SLEEP;

    if (!g_lpc_initialized) {
        return QM_LPC_NO_SLEEP;
    }

    /* Snapshot callbacks before invoking them so callbacks may manage LPC. */
#if CONFIG_QM_OS_SUPPORT
    if (qm_mutex_lock(&g_lpc_ctx.check_mutex, QM_WAIT_FOREVER) != QM_EOK) {
        return QM_LPC_NO_SLEEP;
    }
#endif
    if (g_lpc_ctx.lpc_id) {
#if CONFIG_QM_OS_SUPPORT
        (void)qm_mutex_unlock(&g_lpc_ctx.check_mutex);
#endif
        return QM_LPC_NO_SLEEP;
    }

    callback_count = g_lpc_ctx.check_list.len;
#if CONFIG_QM_OS_SUPPORT
    (void)qm_mutex_unlock(&g_lpc_ctx.check_mutex);
#endif
    if (callback_count <= 0) {
        return check_ret;
    }

    callbacks = (lpc_callback_t *)qm_malloc((unsigned int)callback_count * sizeof(*callbacks));
    if (callbacks == NULL) {
        return QM_LPC_NO_SLEEP;
    }

#if CONFIG_QM_OS_SUPPORT
    if (qm_mutex_lock(&g_lpc_ctx.check_mutex, QM_WAIT_FOREVER) != QM_EOK) {
        qm_free(callbacks);
        return QM_LPC_NO_SLEEP;
    }
#endif
    if (g_lpc_ctx.lpc_id || g_lpc_ctx.check_list.len > callback_count) {
#if CONFIG_QM_OS_SUPPORT
        (void)qm_mutex_unlock(&g_lpc_ctx.check_mutex);
#endif
        qm_free(callbacks);
        return QM_LPC_NO_SLEEP;
    }

    self.direction = LIST_HEAD;
    self.next = g_lpc_ctx.check_list.head;
    callback_index = 0;
    node = qm_list_iterator_next(&self);

    while (node != NULL && callback_index < callback_count) {
        if (node->val != NULL) {
            callbacks[callback_index++] = (lpc_callback_t)node->val;
        }
        node = qm_list_iterator_next(&self);
    }
#if CONFIG_QM_OS_SUPPORT
    (void)qm_mutex_unlock(&g_lpc_ctx.check_mutex);
#endif

    callback_total = callback_index;
    for (callback_index = 0; callback_index < callback_total; callback_index++) {
        int callback_ret = callbacks[callback_index]();

        if (!lpc_mode_is_valid((qm_lpc_mode_t)callback_ret)) {
            check_ret = QM_LPC_NO_SLEEP;
            break;
        }
        if (callback_ret < check_ret) {
            check_ret = callback_ret;
        }
        if (check_ret == QM_LPC_NO_SLEEP) {
            break;
        }
    }

    qm_free(callbacks);
    return check_ret;
}

int32_t qm_lpc_enter_powerdown(void)
{
    if (!g_lpc_initialized) {
        return -QM_EINIT;
    }
    if (g_lpc_ctx.mode != QM_LPC_POWERDOWN) {
        return -QM_EINVAL;
    }
    if (qm_lpc_check() == QM_LPC_NO_SLEEP) {
        return -QM_EBUSY;
    }

    return qm_hal_lpc_enter_powerdown();
}

qm_lpc_wakeup_reason_t qm_lpc_wakeup_reason_get(void)
{
    return qm_hal_lpc_wakeup_reason_get();
}

int32_t qm_lpc_wakeup_reason_clear(void)
{
    return qm_hal_lpc_wakeup_reason_clear();
}


int32_t qm_lpc_hw_register(lpc_callback_t prepare, lpc_callback_t resume)
{
    if (!g_lpc_initialized) {
        return -QM_EINIT;
    }
    if (prepare == NULL || resume == NULL) {
        return -QM_EINVAL;
    }

    g_lpc_ctx.prepare = prepare;
    g_lpc_ctx.resume  = resume;

    return QM_EOK;

}


int32_t qm_lpc_prepare(void)
{
    if (!g_lpc_initialized) {
        return -QM_EINIT;
    }
    if (g_lpc_ctx.prepare == NULL) {
        return -QM_EINVAL;
    }

    return g_lpc_ctx.prepare();
}

int32_t qm_lpc_resume(void)
{
    if (!g_lpc_initialized) {
        return -QM_EINIT;
    }
    if (g_lpc_ctx.resume == NULL) {
        return -QM_EINVAL;
    }

    return g_lpc_ctx.resume();
}
