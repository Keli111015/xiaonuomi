#include "qm.h"
#include "qm_uart.h"
#include "serial_platform.h"
#if CONFIG_QM_RTC_SUPPORT
#include "qm_rtc.h"
#endif

#define LOG_TAG "serial"

#ifndef CONFIG_QM_SERIAL_UART_PORT
#define CONFIG_QM_SERIAL_UART_PORT  1
#endif

#ifndef CONFIG_QM_SERIAL_UART_BAUDRATE
#define CONFIG_QM_SERIAL_UART_BAUDRATE  9600
#endif

#ifndef CONFIG_QM_SERIAL_UART_TX_PIN
#define CONFIG_QM_SERIAL_UART_TX_PIN  1
#endif

#ifndef CONFIG_QM_SERIAL_UART_RX_PIN
#define CONFIG_QM_SERIAL_UART_RX_PIN  2
#endif

/**
 * @brief Create a mutex.
 *
 * @return Mutex handle.
 * @see None.
 * @note None.
 */
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
void *qm_serial_mutex_init(void)
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

int qm_serial_mutex_deinit( void *mutex )
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
int qm_serial_mutex_lock(void *mutex )
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
int qm_serial_mutex_unlock(void *mutex )
{
    qm_mutex_t *qm_mutex = (qm_mutex_t*)mutex;
    if(mutex == NULL){
        return -QM_EINVAL;
    }
    return qm_mutex_unlock(qm_mutex);
}


void *qm_serial_sem_init(uint8_t count)
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
int qm_serial_sem_signal(void *sem)
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
int qm_serial_sem_wait(void *sem, uint32_t timeout)
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
int qm_serial_sem_deinit(void *sem)
{
    qm_sem_t *qm_sem = (qm_sem_t*)sem;
    if(qm_sem == NULL){
        return -QM_EINVAL;
        
    }
    return qm_sem_free(qm_sem);
}
#endif

static qm_uart_dev_t uart_dev = {
    .config = {
        .baud_rate = CONFIG_QM_SERIAL_UART_BAUDRATE,
        .data_bits = QM_UART_DATA_8_BITS,
        .parity    = QM_UART_PARITY_NONE,
        .pins.tx_pin = CONFIG_QM_SERIAL_UART_TX_PIN,
        .pins.rx_pin = CONFIG_QM_SERIAL_UART_RX_PIN,
        .stop_bits = QM_UART_STOP_BITS_1 ,
    },
    .port = CONFIG_QM_SERIAL_UART_PORT,
};
    
void *qm_serial_uart_init(void)
{
    qm_uart_init(&uart_dev);
    return (void*)&uart_dev;
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
int qm_serial_send(void *handle, uint8_t* buffer, uint32_t length)
{
    qm_uart_dev_t *uart_dev = (qm_uart_dev_t*)handle;
    QM_RETURN_ON_FALSE(handle, -QM_EINVAL, LOG_TAG, "handle NULL");

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
int qm_serial_recv(void *handle, uint8_t* buffer, uint32_t length, uint32_t timeout_ms)
{
    int ret = 0; 
    qm_uart_dev_t *uart_dev = (qm_uart_dev_t*)handle;
	
    QM_RETURN_ON_FALSE(handle, -QM_EINVAL, LOG_TAG, "handle NULL");

    ret = qm_uart_read(uart_dev, buffer, length, timeout_ms);
    if(ret > 0){
        return ret;
    }else{
        return 0;
    }
}

#if CONFIG_QM_RTC_SUPPORT
#define TIMESTAMP_2000  943891200  /* 1970~2000 in seconds */

static void bbt_localtime(uint32_t timestamp, serial_time_info_t *time)
{
	uint32_t pre_year;
	uint32_t Pass4year;
	int hours_per_year;
    int ONE_YEAR_HOURS = 8760;
    const char Days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	//取当前的周时间
    time->wday = ((timestamp / 86400) + 4) % 7;
	//取秒时间
	time->second = (uint8_t)(timestamp % 60);
	timestamp /= 60;
    //取分钟时间
	time->minute =(uint8_t)(timestamp % 60);
	timestamp /= 60;
	//取过去多少个四年，每四年有 1461*24 小时
	Pass4year = timestamp / (1461L * 24L);
    //计算年份
	pre_year = (Pass4year << 2) + 1970;
	//四年中剩下的小时数
	timestamp %= 1461L * 24L;
	//校正闰年影响的年份，计算一年中剩下的小时数
	while(1)
	{
		//一年的小时数
		hours_per_year = ONE_YEAR_HOURS;
		//判断闰年，是闰年，一年则多24小时，即一天
		if ((pre_year & 3) == 0){
            hours_per_year += 24;
        }
		if (timestamp < hours_per_year){
            break;
        }
		pre_year++;
		timestamp -= hours_per_year;
	}
	//小时数
	time->hour =(uint8_t)(timestamp % 24);
	//一年中剩下的天数
	timestamp /= 24;
	//假定为闰年
	timestamp++;
	//校正闰年的误差，计算月份，日期
	if((pre_year & 3) == 0) {
		if (timestamp > 60) {
			timestamp--;
		} else {
			if (timestamp == 60) {
				time->month = 1;
				time->day = 29;
				return ;
			}
		}
	}
	//计算月日
	time->month = 0;
	while (Days[time->month] < timestamp)
	{
		timestamp -= Days[time->month];
		time->month++;
	}
    time->year = (uint8_t)(pre_year - 2000);
	time->day = (uint8_t)(timestamp);
    time->month  = time->month + 1;

    if(time->wday == 0){
        time->wday = 7;
    }


	return;
}
#endif
int qm_serial_get_time(serial_time_info_t *time_info)
{
#if CONFIG_QM_RTC_SUPPORT
    qm_err_t ret = QM_EOK;
    qm_rtc_dev_t info = {0};
    uint32_t timestamp = {0};
    QM_RETURN_ON_FALSE(time_info, -QM_EINVAL, LOG_TAG, "time_info NULL");
    info.config.format = QM_RTC_FORMAT_DEC;
    ret = qm_rtc_get_timestamp(&info, &timestamp);
    if(ret != QM_EOK){
        return ret;
    }

    timestamp += time_info->timezone * 3600;

    if(timestamp < TIMESTAMP_2000){
        return -QM_EINVAL;
    }

    bbt_localtime(timestamp, time_info);
    return 0;
#else
    return -QM_EINVAL;
#endif
}


/**
 * @brief Retrieves the number of milliseconds that have elapsed since the system was boot.
 *
 * @param None.
 * @return the number of milliseconds.
 * @see None.
 * @note None.
 */

uint32_t qm_serial_get_timems( void )
{
    return (uint32_t)qm_now_ms();
}


/**
 * @brief sleep thread itself.
 *
 * @param[in] ms @n the time interval for which execution is to be suspended, in milliseconds.
 * @return None.
 * @see None.
 * @note None.
 */

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
void qm_serial_msleep(uint32_t ms)
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
void *qm_serial_malloc(uint32_t size)
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
void qm_serial_free(void *ptr)
{
    qm_free(ptr);
}

#if !CONFIG_QM_SERIAL_PRINTF_SUPPORT
int qm_serial_printf(const char *fmt, ...)
{
    return 0;
}

int qm_serial_hex_printf(char *name, uint8_t *p_data, int len)
{
    return 0;
}

#endif


