#ifndef _QM_KVMGR_H_
#define _QM_KVMGR_H_

#include "qm_config.h"

#if defined(__cplusplus) /* If this is a C++ compiler, use C linkage */
extern "C"
{
#endif

#if CONFIG_QM_KV_SUPPORT

/* The totally storage size for key-value store */
#ifndef CONFIG_KV_BUFFER_SIZE
#define KV_TOTAL_SIZE   (8 * 1024)
#else
#define KV_TOTAL_SIZE   CONFIG_KV_BUFFER_SIZE
#endif


typedef enum _qm_kv_get_type_e
{
    QM_KV_GET_TYPE_STRING = 1,
    QM_KV_GET_TYPE_BINARY,
    QM_KV_GET_TYPE_INT,
    QM_KV_GET_TYPE_FLOAT,
    QM_KV_GET_TYPE_MAX
}qm_kv_get_type_e;
    

/**
 * Add a new KV pair.
 *
 * @param[in]  key    the key of the KV pair.
 * @param[in]  value  the value of the KV pair.
 * @param[in]  len    the length of the value.
 * @param[in]  sync   save the KV pair to flash right now (should always be 1).
 *
 * @return  0 on success, negative error on failure.
 */
int qm_kv_set(const char *key, const void *value, int len, int sync);

/**
 * Get the KV pair's value stored in buffer by its key.
 *
 * @note: the buffer_len should be larger than the real length of the value,
 *        otherwise buffer would be NULL.
 *
 * @param[in]      key         the key of the KV pair to get.
 * @param[out]     buffer      the memory to store the value.
 * @param[in-out]  buffer_len  in: the length of the input buffer.
 *                             out: the real length of the value.
 *
 * @return  0 on success, negative error on failure.
 */
int qm_kv_get(const char *key, void *buffer, int *buffer_len);

/**
 * Delete the KV pair by its key.
 *
 * @param[in]  key  the key of the KV pair to delete.
 *
 * @return  0 on success, negative error on failure.
 */
int qm_kv_del(const char *key);


/**
 * @brief init the kv module.
 *
 * @param[in] none.
 *
 * @note: the default KV size is @HASH_TABLE_MAX_SIZE, the path to store
 *        the kv file is @KVFILE_PATH.
 * @retval  0 on success, otherwise -1 will be returned
 */
int qm_kvmgr_init(void);

/**
 * @brief deinit the kv module.
 *
 * @param[in] none.
 *
 * @note: all the KV in RAM will be released.
 * @retval none.
 */
void qm_kvmgr_deinit(void);

#endif

#if defined(__cplusplus) /* If this is a C++ compiler, use C linkage */
}
#endif

#endif


