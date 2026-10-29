#include "qm_utils_hashtable.h"
#include "qm_types.h"
#include "qm_kernel.h"
#include "qm_log.h"

#define LOG_TAG "ht"


static unsigned int _hash_func(const unsigned char *key, int length)
{
    int i = 0;
    unsigned int hash = 0;
    while (i != length) {
        hash += key[i++];
        hash += hash << 10;
        hash ^= hash >> 6;
    }
    hash += hash << 3;
    hash ^= hash >> 11;
    hash += hash << 15;
    return hash;
}



void *ht_init(int max_count)
{
   ht_t *ht = NULL;
   int len = max_count * sizeof(ht_item_t);

   if(max_count <= 0)
	   return NULL;

   ht = qm_malloc(sizeof(ht_t));
   if(!ht)
      return NULL;

    memset(ht, 0, sizeof(ht_t));

    ht->item = qm_malloc(len);
    if (NULL == ht->item) {
        qm_free(ht);
        return NULL;
    }
    memset(ht->item, 0, len);
    ht->cnt = max_count;
  
    return ht;
}

int ht_deinit(void *ht)
{
    ht_t *pt = (ht_t *)ht;
    if (!ht) {
        return -1;
    }

    ht_clear(ht);

    qm_free(pt->item);
    qm_free(ht);
    return 0;
}

static ht_item_t *_ht_find(void *ht, const void *key, int key_len)
{
    unsigned int pos = 0;
    ht_t *pt = (ht_t *)ht;

    if (!ht || !key || key_len <= 0) {
        return NULL;
    }

    pos = _hash_func((const unsigned char *)key, key_len) % pt->cnt;

    return (pt->item + pos);//users should check the pointer to get all the values.
}



void *ht_find(void *ht, const void *key, int len_key, void *val, int *size_val)
{
    ht_item_t *p = _ht_find(ht, key, len_key);
    void *ret = NULL;

    while (p) {
        if (p->key && (p->len_key == len_key) && !memcmp(p->key, key, len_key)) {
            ret = p->val;
            break;
        }
        p = p->next;
    }

    if (ret && val && size_val) {
        memcpy(val, ret, p->size_val > *size_val ? *size_val : p->size_val);
        *size_val = p->size_val;
    }

    return ret;
}


int ht_add(void *ht, const void *key, int len_key, const void *val, int size_val)
{
    ht_item_t *p_item = NULL;
    ht_item_t *p_tmp = NULL;
    ht_item_t *new_tb = NULL;

    if (!ht || !key || !val || len_key <= 0 || size_val <= 0) {
        return -1;
    }

    p_item = _ht_find(ht, key, len_key);

    if (!p_item->key) {
        p_item->key = qm_malloc(len_key);
        if (!p_item->key) {
            return -1;
        }
        p_item->len_key = len_key;
        memcpy(p_item->key, key, len_key);
        p_item->val = qm_malloc(size_val);
        if (!p_item->val) {
            qm_free(p_item->key);
            p_item->key = NULL;
            return -1;
        }
        memcpy(p_item->val, val, size_val);
        p_item->size_val = size_val;
        return 0;
    }

    //conflict: add to it's next item in the list.
    p_tmp = p_item;
    while (p_tmp) {
        if (NULL != p_tmp->key && (p_tmp->len_key == len_key) && !memcmp(p_tmp->key, key, len_key)) {
    
            qm_free(p_tmp->val);
            p_tmp->val = NULL;
            p_tmp->val = qm_malloc(size_val);
            if (!p_item->val) {
                qm_free(p_tmp);
                return -1;
            }
            p_item->size_val = size_val;
            memcpy(p_item->val, val, size_val);
            return 0;//repeated key , just update it
        }
        p_item = p_tmp;
        p_tmp = p_tmp->next;
    }

    new_tb = (ht_item_t *)qm_malloc(sizeof(ht_item_t));
    if (!new_tb) {
        return -1;
    }
    memset(new_tb, 0, sizeof(ht_item_t));
    new_tb->key = qm_malloc(len_key);
    if (!new_tb->key) {
        qm_free(new_tb);
        return -1;
    }
    new_tb->len_key = len_key;
    memcpy(new_tb->key, key, len_key);
    new_tb->val = qm_malloc(size_val);
    if (!new_tb->val) {
        qm_free(new_tb->key);
        qm_free(new_tb);
        return -1;
    }
    new_tb->size_val = size_val;
    memcpy(new_tb->val, val, size_val);
    p_item->next = new_tb;

    return 0;
}



static int _ht_del_node(void *ht_item, const void *key, int len_key)
{
    ht_item_t *p_tmp = NULL;
    ht_item_t *next = NULL;
    ht_item_t *parent = NULL; // the root node or previous node
    int flag = 0;
    int ret = -1;

    if (!ht_item) {
        return ret;
    }

    parent = (ht_item_t *)ht_item;
    while (parent) {
        if (parent->key && parent->val &&
            (!key || !memcmp(parent->key, key, len_key))) {
            next = parent->next;//store its next item befroe free.

            qm_free(parent->key);
            parent->key = NULL;
            qm_free(parent->val);
            parent->val = NULL;
            ret = 0;
            if (1 == flag) {//just free the added item not the root item.
                qm_free(parent);
                p_tmp->next = next;
            } else {
                p_tmp = parent;
            }

            if (key) {
                break;
            }
            parent = next;
        } else {
            p_tmp = parent;
            parent = parent->next;
        }
        flag = 1;
    }

    return ret;
}


int ht_del(void *ht, const void *key, int len_key)
{
    return _ht_del_node(_ht_find(ht, key, len_key), key, len_key);
}


int ht_clear(void *ht)
{
    ht_t *pt = ht;
    int i = 0;
    if (!ht) {
        return -1;
    }

    for (i = 0; i < pt->cnt; i++) {
        _ht_del_node(pt->item + i, NULL, 0);
    }

    return 0;
}

int ht_traverse(void *ht, int (*traverse_cb)(void *key, int key_len, void *val, int size_val, void *arg), void *arg)
{
    int i = 0;
    int ret = 0;
    int cnt = 0;
    ht_t *pt = (ht_t*)ht;
    ht_item_t *p_item = NULL;
    if(ht == NULL || traverse_cb == NULL){
        return -1;
    }

    cnt = pt->cnt;
    for(i = 0; i < cnt; i++){
        p_item = pt->item + i;
        if(!p_item->key){
            continue;
        }
        while(p_item){
            ret = traverse_cb(p_item->key, p_item->len_key, p_item->val, p_item->size_val, arg);
            if(ret != 0){
                return ret;
            }
            p_item = p_item->next;
        }
    }
    return 0;
}