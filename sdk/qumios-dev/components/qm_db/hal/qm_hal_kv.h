
#ifndef QM_HAL_KV_H
#define QM_HAL_KV_H

#ifdef __cplusplus
extern "C"{
#endif

/**
 * initialize kv store
 *
 * @return  0 on success, negative error on failure.
 */
int qm_hal_kv_init(void);

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
int qm_hal_kv_set(const char *key, const void *value, int len, int sync);

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
int qm_hal_kv_get(const char *key, void *buffer, int *buffer_len);

/**
 * Delete the KV pair by its key.
 *
 * @param[in]  key  the key of the KV pair to delete.
 *
 * @return  0 on success, negative error on failure.
 */
int qm_hal_kv_del(const char *key);

/**
 * Delete the all KV pair
 *
 * @return  0 on success, negative error on failure.
 */
int qm_hal_kv_clean(void);


#ifdef __cplusplus
}
#endif

#endif /* QM_HAL_KV_H */

