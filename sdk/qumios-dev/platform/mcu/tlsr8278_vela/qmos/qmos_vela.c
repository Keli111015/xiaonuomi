#include <qmos/qm.h>

#include <zephyr.h>

#include "mible_api.h"
#include "miio_user_api.h"

#include <time.h>
#include "qm_utils_timer.h"
// #include "ntpdate.h"
#include "qm_iot.h"

#include "qm_at.h"
// #include "serial_protocol.h"

extern void iot_lpc_delay_sleep(int ms);

void qm_reboot(void)
{
    miio_system_reboot();
}

const char *qm_version_get(void)
{
    return "V1.0.0";
}

#if !CONFIG_QM_MTRACE_SUPPORT

void *qm_malloc(unsigned int size)
{
    return malloc(size);
}

void *qm_realloc(void *mem, unsigned int size)
{
    return realloc(mem, size);
}

void qm_free(void *mem)
{
    free(mem);
}

#endif

uint32_t qm_now_ms(void)
{
    uint64_t ms = 0;
    struct timespec time = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &time);
    ms = time.tv_nsec/1000000 + time.tv_sec*1000;
    return (uint32_t)(ms & 0xFFFFFFFF);
}

// uint32_t qm_free_mem_get(void)
// {
//     return esp_get_free_heap_size();
// }

// uint32_t weak_function qm_now_ms(void)
// {
//     return (g_timer_crtl.system_time_ms & 0xFFFFFFFF);
// }

int qm_msleep(int ms)
{
    usleep(ms * 1000);
    return 0;
}

int qm_usleep(int us) //暂不支持
{

    return -QM_EINVAL;
}


#define LOG_BUFFER_MAX_LEN  62

typedef struct 
{
    uint16_t buffer_len;
    char buffer[LOG_BUFFER_MAX_LEN];
}qm_printf_handle_t;

#if !QM_LOG_MODULE_MCU
static qm_printf_handle_t printf_handle = {0};
#endif

int qm_printf(const char * fmt, ...)
{      
#if !QM_LOG_MODULE_MCU
    uint16_t temp = 0;
    va_list ParamList;
    va_start(ParamList, fmt);
  	temp = vsnprintf(printf_handle.buffer + printf_handle.buffer_len,LOG_BUFFER_MAX_LEN -printf_handle. buffer_len, fmt, ParamList);   
	if(printf_handle.buffer_len >= LOG_BUFFER_MAX_LEN || strstr((const char *)(printf_handle.buffer + printf_handle.buffer_len), "\n") ){
		temp = 0;
		printf_handle.buffer_len  = 0;
        syslog(6,"%s", printf_handle.buffer);
		memset(printf_handle.buffer, 0, LOG_BUFFER_MAX_LEN);
	}

	printf_handle.buffer_len += temp;
    va_end(ParamList);    
#endif
    return QM_EOK;
}

int qm_snprintf(char *str, const int len, const char *fmt, ...)
{
    va_list args;
    int     rc;
    va_start(args, fmt);
    rc = vsnprintf(str, len, fmt, args);
    va_end(args);
    return rc;
}

#if 0

int qm_task_new(qm_task_t *task, const char *name, void (*fn)(void *), void *arg,
                     int stack_size, int prio)
{
    int ret = miio_timer_create(&task->hdl, fn, MIBLE_TIMER_SINGLE_SHOT);
    if(ret == MI_SUCCESS)
    {
       ret = miio_timer_start(task->hdl, 50, arg);
    }
    return ret;
}

int qm_task_exit(qm_task_t *task)
{
    if(task->hdl != NULL)
    {
        miio_timer_stop(task->hdl);
        miio_timer_delete(task->hdl);
        task->hdl = NULL;
    }
    return QM_EOK;

}
#endif


#if 0
int weak_function  qm_mutex_new(qm_mutex_t *mutex)
{
    return QM_EOK;
    // return -QM_EINVAL;

}

int  weak_function qm_mutex_free(qm_mutex_t *mutex)
{
    return QM_EOK;
    // return -QM_EINVAL;
}

int  weak_function qm_mutex_lock(qm_mutex_t *mutex, unsigned int ms)
{
    return QM_EOK;
    // return -QM_EINVAL;
}

int weak_function  qm_mutex_unlock(qm_mutex_t *mutex)
{
    return QM_EOK;
    // return -QM_EINVAL;
}

int weak_function  qm_mutex_is_valid(qm_mutex_t *mutex)
{
    return QM_EOK;
    // return -QM_EINVAL;
}

int  weak_function qm_sem_new(qm_sem_t *sem, int count)
{
    return QM_EOK;
    // return -QM_EINVAL;
}

int  weak_function qm_sem_free(qm_sem_t *sem)
{
    return QM_EOK;
    // return -QM_EINVAL;
}

int  weak_function qm_sem_wait(qm_sem_t *sem, unsigned int ms)
{
    return QM_EOK;
    // return -QM_EINVAL;
}

int  weak_function qm_sem_signal(qm_sem_t *sem)
{
    return QM_EOK;
    // return -QM_EINVAL;
}

int  weak_function qm_sem_signal_from_isr(qm_sem_t *sem)
{
    return QM_EOK;
    // return -QM_EINVAL;
}

int  weak_function qm_sem_is_valid(qm_sem_t *sem)
{
    return QM_EOK;
    // return -QM_EINVAL;
}

int weak_function  qm_event_new(qm_event_t *event)
{
    return QM_EOK;
    // return -QM_EINVAL;
}

int weak_function  qm_event_free(qm_event_t *event)
{
    return QM_EOK;
    // return -QM_EINVAL;
}

int weak_function qm_event_is_valid(qm_event_t *event)
{
    return (event != NULL && event->hdl != NULL) ? 1 : 0;
}

int weak_function  qm_event_get(qm_event_t *event, unsigned int wait_event_bits, unsigned char opt, unsigned int *actl_event_bits, unsigned int timeout)
{
    if (!qm_event_is_valid(event) || actl_event_bits == NULL) {
        return -QM_EINVAL;
    }

    return QM_EOK;
    // return -QM_EINVAL;
}

int weak_function  qm_event_set(qm_event_t *event, unsigned int event_bits)
{
    return QM_EOK;
    // return -QM_EINVAL;
}

int weak_function qm_queue_new(qm_queue_t *queue, unsigned int queue_len, unsigned int queue_size)
{
    return QM_EOK;
    // return -QM_EINVAL;
}

int weak_function qm_queue_free(qm_queue_t *queue)
{
    return QM_EOK;
    // return -QM_EINVAL;
}

int weak_function qm_queue_send(qm_queue_t *queue, void *msg, unsigned int size)
{
    return QM_EOK;
    // return -QM_EINVAL;
}

int weak_function qm_queue_send_from_isr(qm_queue_t *queue, void *msg, unsigned int size)
{
    return QM_EOK;
    // return -QM_EINVAL;
}

int weak_function qm_queue_recv(qm_queue_t *queue, void *msg, unsigned int *size, unsigned int ms)
{
    return QM_EOK;
    // return -QM_EINVAL;
}

int weak_function qm_queue_is_valid(qm_queue_t *queue)
{
    return QM_EOK;
    // return -QM_EINVAL;
}

static uint32_t next = 0;  
  

/* RAND_MAX assumed to be 32767 */  
static uint32_t hal_rand(void) {  
    next = next * 1103515245 + 12345;  
    return((uint32_t)(next/65536) % 32768);  
}  
  
void qm_srandom(uint32_t seed)
{
    next = seed; 
}

uint32_t qm_random_get(uint32_t region)
{
    return (region > 0) ? (hal_rand() % region) : 0;
}
#endif

void qm_init(void)
{

}

void void_func(char *mod, ...)
{

}


#define BASE_YEAR	70	//世纪秒的年份基准，BASE_YEAR = 基准年份 - 1900
#define BASE_MON	1	//世纪秒的月份基准，一般是1月
#define BASE_DAY	1	//世纪秒的日基准  ，一般是1日
						//时分秒都默认是0
#define DAY_SEC  (24 * 60 * 60)
#define BASE_NOW  (4)

//初始时间
#define YEAR	(1970 - 1900)	
#define MON		(1 - 1)			//月[0,11],以适应time.h中的struct tm
#define DAY		1
#define HOUR	8
#define MIN		0
#define SEC		0

//是否是UTC时间,单片机中使用为1(因为没有设置时区)，电脑中使用需要为0(因为电脑调用time.h中的相关函数时，调用了系统时区)
//mijia mesh gateway have timezone info, timestamp have add timezone. so set USE_UTC to zero
#define USE_UTC 1


#define TS_START_POINT  (1698825600)  //2023-11-01 16:00:00
#define TS_END_POINT    (2949494400)  //2063-06-20 0:0:0

//校时相关的函数
static uint32_t _ts_base = 0;
static uint32_t g_ts_base = 0;
void *datatime_timer = NULL;

//月份天数表
unsigned char Montbl[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
extern volatile int g_gmt_offset;

bool localtime_set_basets(uint32_t ts) 
{
    if ((ts > _ts_base) && (ts > TS_START_POINT) && (ts < TS_END_POINT)) {
        _ts_base = ts;
        return true;
    }
    return false;
}

uint32_t localtime_get_basets(void)
{
    return _ts_base;
}

void localtime_start(bool start)
{
    if (start) {
        g_ts_base = 0;
        if(datatime_timer != NULL)
            mible_timer_start(datatime_timer, 1000, NULL);
    } else {
        if(datatime_timer != NULL)
            mible_timer_stop(datatime_timer);
    }
}

uint32_t localtime_get_ts(void)
{
    uint32_t ts = 0;
    if (_ts_base) {
   
        ts = _ts_base + g_ts_base;
    } 

    return ts;
}


#if 0
int qm_time_info_get(qm_time_info_t* info)
{
    //TODO: impl
    miio_mesh_request_property(SERVER_UTC_TIME);

    //若已经校过时，则用本地计时填充 info数据
    uint32_t ts = localtime_get_ts();
    if (ts) {

        struct tm _tm;
        ts = localtime_get_ts();
        ts += info->timezone * 3600;

        struct tm *p = localtime(&ts);//取得当地时间
        info->year   = (uint8_t)p->tm_year - 100;
        info->month  = (uint8_t)p->tm_mon + 1;
        info->day    = (uint8_t)p->tm_mday;
        info->hour   = (uint8_t)p->tm_hour;
        info->minute = (uint8_t)p->tm_min;
        info->second = (uint8_t)p->tm_sec;
        if (p->tm_wday == 0) {
            info->wday = 7;
        } else {
            info->wday   = (uint8_t)p->tm_wday;
        }

        qm_printf("datatime: %d m:%d d:%d h:%d m:%d s:%d\n", info->year, info->month,info->day ,info->hour, info->minute, info->second);
        return QM_EOK;
    }
    return QM_EIO;
}
#endif


void sys_datatime_handler(void *arg)
{
    g_ts_base++;
}

void sys_datatime_init(void)
{
    if(datatime_timer == NULL)
    {
        if(mible_timer_create(&datatime_timer, sys_datatime_handler, MIBLE_TIMER_REPEATED) == QM_EOK)
        {
            mible_timer_stop(datatime_timer); 
        }
    }
    
}
