#include "prodtst_tot.h"
#include "qm_log.h"
#include "qm_kernel.h"
#include "qm_utils_list.h"
#include "qm_utils_timer.h"
#include "qm_errno.h"
#include "prodtst_path.h"

typedef struct prodtst_tot{
    char uuid[CONFIG_PRODTST_UUID_LEN + 1];
    qm_utils_time_t time;
}prodtst_tot_t;

typedef struct {
    int init;
    int exit;
    qm_task_t task;
    qm_mutex_t lock;
    qm_sem_t sem;
    qm_list_t *list;
    prodtst_tot_timeout_fn timeout_fn;
}prodtst_tot_ctx_t;

static prodtst_tot_ctx_t g_prodtst_tot_ctx = {0};

#define CONFIG_PRODTST_TOT_TASK_SIZE   2048
#define CONFIG_PRODTST_TOT_TASK_PRIO   20

static void prodtst_tot_task(void *arg);
static qm_list_node_t *prodtst_tot_min_get(void);

static int prodtst_tot_match(void *a, void *b)
{
    prodtst_tot_t *a_tot = (prodtst_tot_t*)a;
    prodtst_tot_t *b_tot = (prodtst_tot_t*)b;

    if(0 == strcmp(a_tot->uuid, b_tot->uuid)){
        return 1;
    }else{
        return 0;
    }
}

int prodtst_tot_init(prodtst_tot_timeout_fn timeout_fn)
{
    int ret = QM_EOK;

    if(timeout_fn == NULL){
        return -QM_EINVAL;
    }
    if(g_prodtst_tot_ctx.init){
        return QM_EOK;
    }
    g_prodtst_tot_ctx.timeout_fn = timeout_fn;

    g_prodtst_tot_ctx.list = qm_list_new();
    if(g_prodtst_tot_ctx.list == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    g_prodtst_tot_ctx.list->match = prodtst_tot_match;

    ret = qm_mutex_new(&g_prodtst_tot_ctx.lock);
    if(ret != QM_EOK){
        goto __exit;
    }

    ret = qm_sem_new(&g_prodtst_tot_ctx.sem, 0);
    if(ret != QM_EOK){
        goto __exit;
    }

    ret = qm_task_new(&g_prodtst_tot_ctx.task, "prodtst tot", prodtst_tot_task, (void*)&g_prodtst_tot_ctx, CONFIG_PRODTST_TOT_TASK_SIZE, CONFIG_PRODTST_TOT_TASK_PRIO);
    if(ret != QM_EOK){
        goto __exit;
    }
    g_prodtst_tot_ctx.init = 1;
    return QM_EOK;

__exit:
    if(g_prodtst_tot_ctx.list){
        qm_list_destroy(g_prodtst_tot_ctx.list);
        g_prodtst_tot_ctx.list = NULL;
    }
    if(qm_mutex_is_valid(&g_prodtst_tot_ctx.lock)){
        qm_mutex_free(&g_prodtst_tot_ctx.lock);
    }
    if(qm_sem_is_valid(&g_prodtst_tot_ctx.sem)){
        qm_sem_free(&g_prodtst_tot_ctx.sem);
    }
    return ret;
}   

static void prodtst_tot_task(void *arg)
{
    int ret = QM_EOK;
    qm_task_t task = {0};
    prodtst_tot_t *prodtst_tot = NULL;
    qm_list_node_t *node = NULL;
    prodtst_tot_ctx_t *tot_ctx = (prodtst_tot_ctx_t*)arg;
    uint32_t timeout = QM_WAIT_FOREVER;

    while(1){
        ret = qm_sem_wait(&tot_ctx->sem, timeout);
        if(tot_ctx->exit){
            break;
        }

        if(ret != QM_EOK){
            //timeout
            qm_mutex_lock(&tot_ctx->lock, QM_WAIT_FOREVER);
            if(node){
                prodtst_tot = (prodtst_tot_t*)node->val;

                if(g_prodtst_tot_ctx.timeout_fn){
                    g_prodtst_tot_ctx.timeout_fn(prodtst_tot->uuid);
                }
                
                qm_list_remove(tot_ctx->list, node);
                qm_free(node);
                node = NULL;
            }
            qm_mutex_unlock(&tot_ctx->lock);
        }

        node = prodtst_tot_min_get();
        if(node == NULL){
            continue;
        }

        prodtst_tot = (prodtst_tot_t*)node->val;
        timeout = qm_utils_time_left(&prodtst_tot->time);
    }

    prodtst_tot_reset();

    if(g_prodtst_tot_ctx.list){
        qm_list_destroy(g_prodtst_tot_ctx.list);
        g_prodtst_tot_ctx.list = NULL;
    }
    if(qm_mutex_is_valid(&g_prodtst_tot_ctx.lock)){
        qm_mutex_free(&g_prodtst_tot_ctx.lock);
    }
    if(qm_sem_is_valid(&g_prodtst_tot_ctx.sem)){
        qm_sem_free(&g_prodtst_tot_ctx.sem);
    }

    memcpy(&task, &tot_ctx->task, sizeof(qm_task_t));
    memset(&g_prodtst_tot_ctx, 0, sizeof(prodtst_tot_ctx_t));

    qm_task_exit(&task);
}

static qm_list_node_t *prodtst_tot_min_get(void)
{
    uint32_t left_time = 0xFFFFFFFF;
    prodtst_tot_t *prodtst_tot = NULL;
    qm_list_node_t *node = NULL;
    qm_list_node_t *node_tmp = NULL;
    qm_list_iterator_t *self = NULL;

    self = qm_list_iterator_new(g_prodtst_tot_ctx.list, LIST_HEAD);
    if(self == NULL){
        return NULL;
    }

    qm_mutex_lock(&g_prodtst_tot_ctx.lock, QM_WAIT_FOREVER);
    node = qm_list_iterator_next(self);

    while(node){
        prodtst_tot = (prodtst_tot_t*)(node->val);
        if(left_time > qm_utils_time_left(&prodtst_tot->time)){
            left_time = qm_utils_time_left(&prodtst_tot->time);
            node_tmp = node;
        }
        node = qm_list_iterator_next(self);
    }
    qm_list_iterator_destroy(self);

    qm_mutex_unlock(&g_prodtst_tot_ctx.lock);

    return node_tmp;
}

int prodtst_tot_record_remove(char uuid[CONFIG_PRODTST_UUID_LEN + 1])
{
    qm_list_node_t *node = NULL;
    prodtst_tot_t prodtst_tot = {0};
    if(uuid == NULL){
        return -QM_EINVAL;
    }

    memcpy(prodtst_tot.uuid, uuid, CONFIG_PRODTST_UUID_LEN);

    qm_mutex_lock(&g_prodtst_tot_ctx.lock, QM_WAIT_FOREVER);    
    node = qm_list_find(g_prodtst_tot_ctx.list, &prodtst_tot);
    if(node == NULL){
        qm_mutex_unlock(&g_prodtst_tot_ctx.lock);
        return -QM_EINVAL;
    }
    qm_list_remove(g_prodtst_tot_ctx.list, node);
    qm_mutex_unlock(&g_prodtst_tot_ctx.lock);

    qm_free(node);
    node = NULL;

    return QM_EOK;
}

int prodtst_tot_record_is_exists(char uuid[CONFIG_PRODTST_UUID_LEN + 1], int *is_exists)
{
    int exist = 0;
    prodtst_tot_t *prodtst_tot = NULL;
    qm_list_node_t *node = NULL;
    qm_list_iterator_t *self = NULL;

    if(is_exists == NULL || uuid == NULL){
       return -QM_EINVAL;
    }

    self = qm_list_iterator_new(g_prodtst_tot_ctx.list, LIST_HEAD);
    if(self == NULL){
        return -QM_ENOMEM;
    }

    qm_mutex_lock(&g_prodtst_tot_ctx.lock, QM_WAIT_FOREVER);
    node = qm_list_iterator_next(self);

    while(node){
        prodtst_tot = (prodtst_tot_t*)(node->val);
        if(prodtst_tot){
            if(strcmp(prodtst_tot->uuid, uuid) == 0){
                exist = 1;
                break; 
            }
        }
        node = qm_list_iterator_next(self);
    }
    qm_list_iterator_destroy(self);
    qm_mutex_unlock(&g_prodtst_tot_ctx.lock);

    if(exist){
        *is_exists = 1;
    }else{
        *is_exists = 0;
    }

    return QM_EOK;
}

int prodtst_tot_reset(void)
{
    qm_mutex_lock(&g_prodtst_tot_ctx.lock, QM_WAIT_FOREVER);
    qm_list_destroy(g_prodtst_tot_ctx.list);
    qm_mutex_unlock(&g_prodtst_tot_ctx.lock);
    g_prodtst_tot_ctx.list = NULL;

    return QM_EOK;
}

int prodtst_tot_deinit(void)
{
    g_prodtst_tot_ctx.exit = 1;

    if(qm_sem_is_valid(&g_prodtst_tot_ctx.sem)){
        qm_sem_signal(&g_prodtst_tot_ctx.sem);
    }
    return QM_EOK;
}