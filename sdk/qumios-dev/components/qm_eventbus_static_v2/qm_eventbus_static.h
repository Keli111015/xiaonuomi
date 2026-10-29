/**
 * @file qm_eventbus.h
 * @brief 高性能 MPSC 事件总线 (句柄隔离版)
 */

#ifndef QM_EVENTBUS_H
#define QM_EVENTBUS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------*/
/* 全局配置                                                                    */
/* --------------------------------------------------------------------------*/
// 系统最大支持的 EventBus 实例数量
#ifndef CONFIG_QM_EVENTBUS_MAX_BUS_NUM
#define CONFIG_QM_EVENTBUS_MAX_BUS_NUM 1
#endif

// 是否开启内部状态统计功能
#ifndef CONFIG_QM_EVENTBUS_STATS
#define CONFIG_QM_EVENTBUS_STATS 1
#endif

/* --------------------------------------------------------------------------*/
/* 平台相关接口 (需在抽象层实现)                                                */
/* --------------------------------------------------------------------------*/
extern uint32_t qm_port_disable_irq(void);
extern void qm_port_enable_irq(uint32_t mask);
/** Monotonic milliseconds (delay scheduling / trigger_tick). */
extern uint32_t qm_now_ms(void);

/* --------------------------------------------------------------------------*/
/* 数据结构定义                                                                */
/* --------------------------------------------------------------------------*/
#define QM_EVENTBUS_ALL_MODULE 0xFF
#define QM_EVENTBUS_ALL_EVENT  0xFF
#define QM_DELAY_FLAG_APPEND    0x00  // 允许重复，新增一个独立的延时节点
#define QM_DELAY_FLAG_OVERWRITE 0x01  // 如果存在同ID的延时事件，直接覆盖并重新计时！(防抖/重置利器)
#define QM_DELAY_FLAG_PERIODIC  0x02  // 周期性事件

#define QM_DELAY_SLOT_EMPTY     0
#define QM_DELAY_SLOT_BUSY      1

// Handler 函数签名 (透传 bus_id 给上层，方便复用 Handler)
typedef int (*qm_eventbus_handler_t)(int bus_id, uint8_t m_id, uint8_t e_id, void *payload, int payload_size);

// 事件定义结构
typedef struct {
    uint8_t module_id;
    uint8_t event_id;
    uint8_t handler_cnt;
    uint8_t reserved;
} qm_eventbus_event_t;

// 事件路由表节点
typedef struct {
    qm_eventbus_event_t event;
    qm_eventbus_handler_t *handlers;
} qm_eventbus_event_table_t;

#define QM_EVENTBUS_CHAIN_DEF(name)  static const qm_eventbus_handler_t name[]
#define QM_EVENTTABLE_ENTRY(m, e, chain) {{.module_id = m, .event_id = e, .handler_cnt = (sizeof(chain)/sizeof(qm_eventbus_handler_t))}, .handlers = (qm_eventbus_handler_t*)chain }
#define QM_EVENTTABLE_END        {{.module_id = 0, .event_id = 0, .handler_cnt = 0}, .handlers = NULL }


#if CONFIG_QM_EVENTBUS_STATS
/**
 * 性能与状态统计信息
 *
 * high_water_mark: 主环上观测到的队列深度峰值（与 _internal_post 中 current_depth+1 同口径）。
 * ring_slot_capacity: 主环 backing 槽位数 (= buffer_size / item_size)。
 * delay_slot_capacity: 延时池槽位数，无延时池时为 0。
 * slot_size_bytes: 主环单槽字节数（包头 + 对齐后 payload）。
 */
typedef struct {
    uint32_t post_count;
    uint32_t drop_count;
    uint32_t process_count;
    uint32_t cancel_count;
    uint16_t high_water_mark;
    uint16_t ring_slot_capacity;
    uint16_t delay_slot_capacity;
    uint16_t slot_size_bytes;
} qm_eventbus_stat_t;
#endif

/* --------------------------------------------------------------------------*/
/* API 声明 (纯 bus_id 句柄操作)                                               */
/* --------------------------------------------------------------------------*/

/**
 * @brief 初始化 EventBus 实例
 * @param bus_id 实例 ID (0 ~ CONFIG_QM_EVENTBUS_MAX_BUS_NUM - 1)
 * @param buffer 用户静态分配的内存池
 * @param buffer_size 内存池总字节数
 * @param payload_size 业务层需要的 Event Payload 大小(框架自动向上对齐)
 * @param event_table 静态路由表
 * @return 0: 成功, <0: 失败
 */
int qm_eventbus_init(int bus_id, void *buffer, uint32_t buffer_size, void *delay_buffer, uint32_t delay_buffer_size,
                     uint8_t payload_size, qm_eventbus_event_table_t *event_table);

/**
 * @brief 发送事件 (MPSC 安全，支持在 ISR 中调用)
 * @param bus_id 实例 ID
 * @param m_id 模块 ID
 * @param e_id 事件 ID
 * @param payload_data 载荷数据指针
 * @return 0: 成功, -1: 参数错误/未初始化, -2: 队列满
 */
int qm_eventbus_post(int bus_id, uint8_t m_id, uint8_t e_id, const void *payload_data, uint8_t payload_size);

/**
 * @brief 取消队列中尚未被处理的事件
 * @param bus_id 实例 ID
 * @param m_id 模块 ID
 * @param e_id 事件 ID
 * @return 成功拦截并取消的事件数量
 */
int qm_eventbus_cancel(int bus_id, uint8_t m_id, uint8_t e_id);

/**
 * @brief 处理总线中的事件 (通常在主循环中调用，无锁执行)
 * @param bus_id 实例 ID
 * @return 本次成功派发处理的事件数量
 */
int qm_eventbus_process(int bus_id);

#if CONFIG_QM_EVENTBUS_STATS
/**
 * @brief 获取统计信息
 */
int qm_eventbus_get_stats(int bus_id, qm_eventbus_stat_t *out_stats);
#endif

/**
 * @brief 发送延时事件 (MPSC 安全，支持在 ISR 中调用)
 * @param bus_id 实例 ID
 * @param m_id 模块 ID
 * @param e_id 事件 ID
 * @param payload_data 载荷数据指针
 * @param payload_size 载荷数据大小
 * @param period_ms 重复触发周期
 * @param flags 延时事件标志 QM_DELAY_FLAG_APPEND 或 QM_DELAY_FLAG_OVERWRITE
 * @return 0: 成功, -1: 参数错误/未初始化, -2: 队列满
 */
int qm_eventbus_post_delayed(int bus_id, uint8_t m_id, uint8_t e_id, const void *payload_data, 
                             uint8_t payload_size, uint32_t period_ms, uint8_t flags);
#ifdef __cplusplus
}
#endif

#endif // QM_EVENTBUS_H