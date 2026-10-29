#include "qm_config.h"


#if CONFIG_OPPLE_SERIAL_SUPPORT

#include "qm.h"
#include "opple_serial_platform.h"
#include "qm_uart.h"
#include "qm_kv.h"
#include "qm_time.h"

#if CONFIG_BOARD_PLATFORM_ESP32C3
#include "virserial.h"
#else
#include "qm_wifi_mgnt.h"
#endif

#ifndef CONFIG_QM_TIME_SUPPORT
#error "need open qm_time component"
#endif

#ifndef CONFIG_OPPLE_SERIAL_UART_PORT
#define CONFIG_OPPLE_SERIAL_UART_PORT  1
#endif

#ifndef CONFIG_OPPLE_SERIAL_UART_BAUDRATE
#define CONFIG_OPPLE_SERIAL_UART_BAUDRATE  9600
#endif

#ifndef CONFIG_OPPLE_SERIAL_UART_TX_PIN
#define CONFIG_OPPLE_SERIAL_UART_TX_PIN  1
#endif

#ifndef CONFIG_OPPLE_SERIAL_UART_RX_PIN
#define CONFIG_OPPLE_SERIAL_UART_RX_PIN  2
#endif

#define TIMESTAMP_2000  943891200  /* 1970~2000 in seconds */

/**
 * @brief Create a mutex.
 *
 * @return Mutex handle.
 * @see None.
 * @note None.
 */
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
void *opple_serial_mutex_init(void)
{
    qm_err_t ret = QM_EOK;
    qm_mutex_t *qm_mutex = NULL;
    qm_mutex= qm_malloc(sizeof(qm_mutex_t));
    if(qm_mutex == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }
    ret = qm_mutex_new(qm_mutex);
    if(ret != QM_EOK){
        goto __exit;
    }
    
    return (void*)qm_mutex;
    
__exit:
    if(qm_mutex){
        qm_free(qm_mutex);
        qm_mutex = NULL;
    }
    return NULL;
}

/**
 * @brief Destroy the specified mutex object, it will free related resource.
 *
 * @param[in] mutex @n The specified mutex.
 * @return None.
 * @see None.
 * @note None.
 */

int opple_serial_mutex_deinit( void *mutex )
{
    qm_mutex_t *qm_mutex = (qm_mutex_t*)mutex;
    if(mutex == NULL){
        return -QM_EINVAL;
        
    }
    qm_mutex_free(qm_mutex);
    return QM_EOK;
}

/**
 * @brief Waits until the specified mutex is in the signaled state.
 *
 * @param[in] mutex @n the specified mutex.
 * @return None.
 * @see None.
 * @note None.
 */
int opple_serial_mutex_lock(void *mutex )
{
    qm_mutex_t *qm_mutex = (qm_mutex_t*)mutex;
    if(mutex == NULL){
        return -QM_EINVAL;
    }
    return qm_mutex_lock(qm_mutex, QM_WAIT_FOREVER);
}

/**
 * @brief Releases ownership of the specified mutex object..
 *
 * @param[in] mutex @n the specified mutex.
 * @return None.
 * @see None.
 * @note None.
 */
int opple_serial_mutex_unlock(void *mutex )
{
    qm_mutex_t *qm_mutex = (qm_mutex_t*)mutex;
    if(mutex == NULL){
        return -QM_EINVAL;
    }
    return qm_mutex_unlock(qm_mutex);
}


void *opple_serial_sem_init(uint8_t count)
{
    qm_err_t ret = QM_EOK;
    qm_sem_t *qm_sem = NULL;
    qm_sem = qm_malloc(sizeof(qm_sem_t));
    if(qm_sem == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }
    ret = qm_sem_new(qm_sem, count);
    if(ret != QM_EOK){
        goto __exit;
    }
    
    return (void*)qm_sem;
    
__exit:
    if(qm_sem){
        qm_free(qm_sem);
        qm_sem = NULL;
    }
    return NULL;
}
/**
 * @brief Signals a semaphore
 * @param sem the semaphore to signal */
int opple_serial_sem_signal(void *sem)
{
    qm_sem_t *qm_sem = (qm_sem_t*)sem;
    if(qm_sem == NULL){
        return -QM_EINVAL;
    }
    return qm_sem_signal(qm_sem);
}
/**
 * @brief Wait for a semaphore for the specified timeout
 * @param sem the semaphore to wait for
 * @param timeout timeout in milliseconds to wait (0 = wait forever)
 * @return time (in milliseconds) waited for the semaphore
 *         or mbus_ARCH_TIMEOUT on timeout */
int opple_serial_sem_wait(void *sem, uint32_t timeout)
{
    qm_sem_t *qm_sem = (qm_sem_t*)sem;
    if(qm_sem == NULL){
        return -QM_EINVAL;
    }
    if(timeout == 0){
        return qm_sem_wait(qm_sem, QM_WAIT_FOREVER);
    }else{
        return qm_sem_wait(qm_sem, timeout);
    }
}
/**
 * @brief Delete a semaphore
 * @param sem semaphore to delete */
int opple_serial_sem_deinit(void *sem)
{
    qm_sem_t *qm_sem = (qm_sem_t *)sem;
    if(qm_sem == NULL){
        return -QM_EINVAL;
        
    }
    return qm_sem_free(qm_sem);
}
#endif

void *opple_serial_uart_init(void)
{
    qm_uart_dev_t uart = {
        .config = {
            .baud_rate = CONFIG_OPPLE_SERIAL_UART_BAUDRATE,
            .data_bits = QM_UART_DATA_8_BITS,
            .parity    = QM_UART_PARITY_NONE,
            .pins.tx_pin = CONFIG_OPPLE_SERIAL_UART_TX_PIN,
            .pins.rx_pin = CONFIG_OPPLE_SERIAL_UART_RX_PIN,
            .stop_bits = QM_UART_STOP_BITS_1 ,
        },
        .port = CONFIG_OPPLE_SERIAL_UART_PORT,
    };
    
    qm_uart_dev_t *uart_dev = qm_malloc(sizeof(qm_uart_dev_t));
    if(uart_dev == NULL){
        return NULL;
    }
    memcpy(uart_dev, &uart, sizeof(qm_uart_dev_t));
    qm_uart_init(uart_dev);
    return (void*)uart_dev;
}

/**
 * @brief Increases the count of the specified semaphore object by 1.
 *
 * @param[in] A pointer to a buffer containing the data to be transmitted.
 * @param[in] length @n The length, in bytes, of the data pointed to by the buffer parameter.
 * @return None.
 * @see None.
 * @note None.
 */
int opple_serial_send(void *handle, uint8_t* buffer, uint32_t length)
{
    qm_uart_dev_t *uart_dev = (qm_uart_dev_t*)handle;
    if(uart_dev == NULL){
        return -QM_EINVAL;
    }
    return qm_uart_write(uart_dev, buffer, length, 0);
}

/**
 * @brief Increases the count of the specified semaphore object by 1.
 *
 * @param[in] buffer @n A pointer to a buffer to receive incoming data.
 * @param[in] length @n The length, in bytes, of the data pointed to by the buffer parameter.
 * @param[in] timeout_ms @n timeout interval in millisecond.
 * @return 0 or length.
 * @see None.
 * @note None.
 */
int opple_serial_recv(void *handle, uint8_t* buffer, uint32_t length, uint32_t timeout_ms)
{
    int ret = 0; 
    qm_uart_dev_t *uart_dev = (qm_uart_dev_t*)handle;
		
    if(uart_dev == NULL){
        return -QM_EINVAL;
    }
    ret = qm_uart_read(uart_dev, buffer, length, timeout_ms);
    if(ret > 0){
        return ret;
    }else{
        return 0;
    }
}

int opple_serial_kv_set(const char *key, const void *value, int len)
{
    return qm_kv_set(key, value, len, 1);
}

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
int opple_serial_kv_get(const char *key, void *buffer, int *buffer_len)
{
    return qm_kv_get(key, buffer, buffer_len);
}

/**
 * Delete the KV pair by its key.
 *
 * @param[in]  key  the key of the KV pair to delete.
 *
 * @return  0 on success, negative error on failure.
 */
int opple_serial_kv_del(const char *key)
{
    return qm_kv_del(key);
}


int opple_serial_get_time(opple_serial_time_info_t *time_info)
{
    qm_time_t timestamp = 0;
    qm_err_t ret = QM_EOK;
    struct qm_tm *tm_timeinfo = NULL;

    if(time_info == NULL){
        return -QM_EINVAL;
    }

    timestamp = qm_time(NULL);
    if(timestamp <= TIMESTAMP_2000){
        return -QM_EINVAL;
    }

    timestamp += (time_info->timezone * 3600);
    tm_timeinfo = qm_gmtime(&timestamp);

    time_info->year = tm_timeinfo->tm_year + 1900 - 2000;
    time_info->month = tm_timeinfo->tm_mon + 1;
    time_info->day = tm_timeinfo->tm_mday;
    time_info->hour = tm_timeinfo->tm_hour;
    time_info->minute = tm_timeinfo->tm_min;
    time_info->second = tm_timeinfo->tm_sec;

    if(tm_timeinfo->tm_wday == 0){
        time_info->wday = 7;
    }else{
        time_info->wday = (uint8_t)tm_timeinfo->tm_wday;
    }
    
    return 0;
}


/**
 * @brief Retrieves the number of milliseconds that have elapsed since the system was boot.
 *
 * @param None.
 * @return the number of milliseconds.
 * @see None.
 * @note None.
 */

uint32_t opple_serial_get_timems( void )
{
    return qm_now_ms();
}

int8_t opple_serial_wifi_rssi_get(void)
{
    int8_t temp = 0;
    
#if CONFIG_BOARD_PLATFORM_ESP32C3
    temp = virserial_fast_get_rssi();
#else
    qm_wifi_sta_get_rssi(&temp);
#endif

    if( temp == 0){
        return -127;
    }

    return  temp;
}

int opple_serial_wifi_mac_get(uint8_t mac[6])
{
    int ret = QM_EOK;
#if CONFIG_BOARD_PLATFORM_ESP32C3
    ret =  virserial_fast_get_mac(mac);
#else
    ret =  qm_wifi_get_mac(QM_WIFI_IF_STA, mac);
#endif
    return ret;
}
/**
 * @brief sleep thread itself.
 *
 * @param[in] ms @n the time interval for which execution is to be suspended, in milliseconds.
 * @return None.
 * @see None.
 * @note None.
 */

#ifdef OPPLE_SERIAL_RTOS_SUPPORT
void opple_serial_msleep(uint32_t ms)
{
   qm_msleep(ms);
}
#endif

/**
 * @brief Allocates a block of size bytes of memory, returning a pointer to the beginning of the block.
 *
 * @param[in] size @n specify block size in bytes.
 * @return A pointer to the beginning of the block.
 * @see None.
 * @note Block value is indeterminate.
 */
void *opple_serial_malloc(uint32_t size)
{
    return qm_malloc(size);
}

/**
 * @brief Deallocate memory block
 *
 * @param[in] ptr @n Pointer to a memory block previously allocated with platform_malloc.
 * @return None.
 * @see None.
 * @note None.
 */
void opple_serial_free(void *ptr)
{
    qm_free(ptr);
}

#if CONFIG_OPPLE_SERIAL_PRINTF_SUPPORT == 0
int opple_serial_printf(const char *fmt, ...)
{
    return 0;
}

int opple_serial_hex_printf(char *name, uint8_t *p_data, int len)
{
    return 0;
}
#endif

#endif
