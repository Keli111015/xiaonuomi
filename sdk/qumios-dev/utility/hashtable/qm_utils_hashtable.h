#ifndef _QM_UTILS_HASHTABLE_H
#define _QM_UTILS_HASHTABLE_H


#if defined(__cplusplus) 
extern "C" {
#endif

typedef struct hash_item
{
    void *key; 
    int len_key;
    void *val;
    int size_val;
    struct hash_item *next;
}ht_item_t;

typedef struct {
    int             cnt;
    ht_item_t       *item;
} ht_t;

void *ht_init(int max_count);
int ht_deinit(void *ht);
int ht_add(void *ht, const void *key, int len_key, const void *val, int size_val);
void *ht_find(void *ht, const void *key, int len_key, void *val, int *size_val);
int ht_del(void *ht, const void *key, int len_key);
int ht_clear(void *ht);
int ht_traverse(void *ht, int (*traverse_cb)(void *key, int key_len, void *val, int size_val, void *arg), void *arg);

#if defined(__cplusplus)
}
#endif


#endif

