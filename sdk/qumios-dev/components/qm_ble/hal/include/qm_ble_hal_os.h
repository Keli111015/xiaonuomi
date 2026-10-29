#ifndef _QM_BLE_HAL_OS_H_
#define _QM_BLE_HAL_OS_H_

#include "qm_ble_bzopt.h"

#define QM_WAIT_FOREVER    0xffffffffu

typedef struct {
    void *hdl;
} qm_ble_hdl_t;

typedef qm_ble_hdl_t qm_ble_timer_t;
typedef qm_ble_hdl_t qm_ble_mutex_t;


/* Timer callback */
typedef void (*qm_ble_timer_cb_t)(void *arg);

/**
 * This function will create a timer.
 *
 * @param[in]  timer   pointer to the timer.
 * @param[in]  fn      callbak of the timer.
 * @param[in]  arg     the argument of the callback.
 * @param[in]  ms      ms of the normal timer triger.
 * @param[in]  repeat  repeat or not when the timer is created.
 *
 * @return  0: success.
 */
int qm_ble_timer_new(qm_ble_timer_t *timer, qm_ble_timer_cb_t cb, void *arg, int ms, int repeat);

/**
 * This function will start a timer.
 *
 * @param[in]  timer  pointer to the timer.
 *
 * @return  0: success.
 */
int qm_ble_timer_start(qm_ble_timer_t *timer);

/**
 * This function will stop a timer.
 *
 * @param[in]  timer  pointer to the timer.
 *
 * @return  0: success.
 */
int qm_ble_timer_stop(qm_ble_timer_t *timer);

/**
 * This function will delete a timer.
 *
 * @param[in]  timer  pointer to a timer.
 */
void qm_ble_timer_free(qm_ble_timer_t *timer);

/**
 * Alloc a mutex.
 *
 * @param[in]  mutex  pointer of mutex object, mutex object must be alloced,
 *                    hdl pointer in qm_ble_mutex_t will refer a kernel obj internally.
 *
 * @return  0: success.
 */
int qm_ble_mutex_new(qm_ble_mutex_t *mutex);

/**
 * Free a mutex.
 *
 * @param[in]  mutex  mutex object, mem refered by hdl pointer in qm_ble_mutex_t will
 *                    be freed internally.
 */
void qm_ble_mutex_free(qm_ble_mutex_t *mutex);

/**
 * Lock a mutex.
 *
 * @param[in]  mutex    mutex object, it contains kernel obj pointer which qm_ble_mutex_new alloced.
 * @param[in]  timeout  waiting until timeout in milliseconds.
 *
 * @return  0: success.
 */
int qm_ble_mutex_lock(qm_ble_mutex_t *mutex, unsigned int timeout);

/**
 * Unlock a mutex.
 *
 * @param[in]  mutex  mutex object, it contains kernel obj pointer which oc_mutex_new alloced.
 *
 * @return  0: success.
 */
int qm_ble_mutex_unlock(qm_ble_mutex_t *mutex);

/**
 * Alloc memory.
 *
 * @param[in]  size  size of the mem to malloc.
 *
 * @return  NULL: error.
 */
void *qm_ble_malloc(unsigned int size);

/**
 * Free memory.
 *
 * @param[in]  ptr  address point of the mem.
 */
void qm_ble_free(void *mem);

/**
 * Reboot system.
 */
void qm_ble_reboot(void);

/**
 * Msleep.
 *
 * @param[in]  ms  sleep time in milliseconds.
 */
void qm_ble_msleep(int ms);

/**
 * Get current time in mini seconds.
 *
 * @return  elapsed time in mini seconds from system starting.
 */
long long qm_ble_now_ms(void);

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
int qm_ble_kv_set(const char *key, const void *value, int len, int sync);

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
int qm_ble_kv_get(const char *key, void *buffer, int *buffer_len);

/**
 * Delete the KV pair by its key.
 *
 * @param[in]  key  the key of the KV pair to delete.
 *
 * @return  0 on success, negative error on failure.
 */
int qm_ble_kv_del(const char *key);

/**
 * Generate random number.
 *
 * @return  random value implemented by platform.
 */
int qm_ble_rand(void);

#endif
