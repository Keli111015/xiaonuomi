#ifndef QM_EVENTBUS_STATIC_H
#define QM_EVENTBUS_STATIC_H

#include "stdint.h"
#include "qm_config.h"

#ifndef CONFIG_QM_EVENTBUS_MAX_BUS_NUM
#  define CONFIG_QM_EVENTBUS_MAX_BUS_NUM 1
#endif

#define NULL_MODULE_ID 0
#define NULL_EVENT 0
#define SYS_MODULE_ID 0xFE
#define ALL_MODULE 0xFF
#define ALL_EVENT 0xFF

#define SYS_EVENT_TICK_100MS (0x01) //100ms tick event

#define EMERGENCY_EVENT(e) (e | 0x80)
#define NORMAL_EVENT(e)    (e & 0x7F)
#define EVENT_ID(e)        (e & 0x7F)
#define IS_EMERGE_EVENT(e)  (e & 0x80)

#define QM_EVENTBUS_NORMAL_EVENT 1
#define QM_EVENTBUS_EMERGENCY_EVENT 2
#define QM_EVENTBUS_ALL_EVENT 3


typedef struct {
	uint8_t module_id;
	uint8_t event_id;
}qm_eventbus_event_t;

typedef int(*qm_eventbus_handler_t)(int bus_id, qm_eventbus_event_t *ev);

typedef struct {
	qm_eventbus_event_t  event;
	qm_eventbus_handler_t handler;
}qm_eventbus_event_table_t;


typedef struct {
	uint8_t normal_event_cnt;
	uint8_t emergency_event_cnt;
	uint8_t resvered1;
	uint8_t resvered2;
	uint16_t normal_event_buffer_left;
	uint16_t emergency_event_buffer_left;
}qm_eventbus_info_t;


/**
 * @brief 初始化EventBus
 * 
 * @param bus_id EventBus id, 0~CONFIG_QM_EVENTBUS_MAX_BUS_NUM-1
 * @param buffer EventBus使用的缓冲区
 * @param size   缓冲区大小，建议4字节对齐
 * @param event_table Event Table
 * @param emergency_percent 紧急事件缓冲区占总缓冲区的百分比, 0表示没有紧急事件
 * @return int QM_OK:成功 other:失败
 */
int qm_eventbus_init(int bus_id, void *buffer, int size, qm_eventbus_event_table_t *event_table, int emergency_percent);

/**
 * @brief Post Event
 * 
 * @param bus_id 
 * @param ev 
 * @param size 
 * @param emergency 是否紧急事件
 * @return int 
 */
int qm_eventbus_post_event(int bus_id, qm_eventbus_event_t *ev, int size);

/**
 * @brief Call Event synchronously
 * 
 * @param bus_id 
 * @param ev 
 * @param data 
 * @param size 
 * @return int 
 */
int qm_eventbus_call_event(int bus_id, qm_eventbus_event_t *ev, int size);

/**
 * @brief 处理事件, 一般在main循环中调用
 * 
 * @param bus_id 
 * @return int 
 */
int qm_eventbus_process_events(int bus_id);

/**
 * @brief 获取当前事件的数据
 * 
 * @param bus_id 
 * @param data 
 * @param size 
 * @return int 
 */
int qm_eventbus_peek_event_data(int bus_id, qm_eventbus_event_t *ev, void *data, int size);

/**
 * @brief 总线中事件的数量
 * 
 * @param bus_id 
 * @param type QM_EVENTBUS_NORMAL_EVENT:普通事件, QM_EVENTBUS_EMERGENCY_EVENT:紧急事件
 * @return int 
 */
int qm_eventbus_event_counts(int bus_id, int type);

/**
 * @brief 获取EventBus信息
 * 
 * @param bus_id 
 * @param info 
 * @return int 
 */
int qm_eventbus_info(int bus_id, qm_eventbus_info_t *info);

#endif
