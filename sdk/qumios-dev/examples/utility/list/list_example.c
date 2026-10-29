#include "qm_utils_list.h"
#include "qm_types.h"
#include "qm_log.h"
#include "qm_errno.h"

#define LOG_TAG "list example"

int qm_list_example_init(void)
{
    int i = 0;
    qm_list_node_t *node = NULL;
    qm_list_t *list = NULL;
    qm_list_iterator_t *self = NULL;

    list = qm_list_new();
    if(list == NULL){
        QM_LOGE(LOG_TAG, "list new error");
        return -QM_ENOMEM;
    }

    int val[5] = {1,2,3,4,5};

    for(i = 0; i < 2; i++){
        node = qm_list_node_new(&val[i]);
        if(node == NULL){
            QM_LOGE(LOG_TAG, "node new error");
            return -QM_ENOMEM;
        }
        qm_list_rpush(list, node);
    }

    self = qm_list_iterator_new(list, LIST_HEAD);
    if(self == NULL){
        return -QM_ENOMEM;
    }
    node = qm_list_iterator_next(self);
    while(node){

        QM_LOGD(LOG_TAG, "%d", *(int*)(node->val));

        node = qm_list_iterator_next(self);
    }

    qm_list_iterator_destroy(self);

    qm_list_reverse(list);

    self = qm_list_iterator_new(list, LIST_HEAD);
    if(self == NULL){
        return -QM_ENOMEM;
    }
    node = qm_list_iterator_next(self);
    while(node){

        QM_LOGD(LOG_TAG, "%d", *(int*)(node->val));

        node = qm_list_iterator_next(self);
    }

    qm_list_iterator_destroy(self);

    return QM_EOK;
}