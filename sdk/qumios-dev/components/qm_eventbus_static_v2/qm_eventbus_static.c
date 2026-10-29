/**
 * @file qm_eventbus.c
 */

#include "qm_eventbus_static.h"
#include <string.h>
#include <stddef.h>

/* --- 内部数据结构 (对上层绝对隐藏) --- */

// 统一的定长包头
typedef struct {
    uint8_t event_id; 
    uint8_t module_id;
    uint8_t is_cancelled;
    uint8_t payload_size;           // 负载数据大小
} qm_event_header_t;

// EventBus 实例控制块
typedef struct {
    volatile uint8_t push_index;
    volatile uint8_t pop_index;
    uint8_t capacity;
    uint8_t item_size;
    uint8_t *buffer;
    qm_eventbus_event_table_t *event_table;
    uint8_t *delay_buffer;     // 延时池内存
    uint16_t delay_capacity;   // 延时池最多容纳的事件数
    uint16_t delay_item_size;  // 延时节点的步长
#if CONFIG_QM_EVENTBUS_STATS
    qm_eventbus_stat_t stats;
#endif
} qm_eventbus_t;

// 延时事件节点头 (固定 12 字节)
typedef struct {
    uint32_t trigger_tick;     // 下一次触发的绝对时间戳
    uint32_t period_ms;        // 重复触发周期 (0 表示一次性事件)
    
    uint8_t  slot_state;       // EMPTY (0) 或 BUSY (1)
    uint8_t  module_id;
    uint8_t  event_id;
    uint8_t  payload_size;         // 凑对齐
} qm_delayed_node_t; 

/* --- 静态实例池 --- */
static qm_eventbus_t _eventbus_instances[CONFIG_QM_EVENTBUS_MAX_BUS_NUM] = {0};

/* --- 内部辅助函数 --- */
static inline qm_eventbus_t* get_bus_instance(int bus_id) {
    if (bus_id < 0 || bus_id >= CONFIG_QM_EVENTBUS_MAX_BUS_NUM) {
        return NULL;
    }
    qm_eventbus_t *bus = &_eventbus_instances[bus_id];
    // 简单校验是否已经初始化
    if (bus->buffer == NULL || bus->capacity == 0) {
        return NULL;
    }
    return bus;
}

/* --- API 实现 --- */

int qm_eventbus_init(int bus_id, void *buffer, uint32_t buffer_size, void *delay_buffer, uint32_t delay_buffer_size,
                     uint8_t payload_size, qm_eventbus_event_table_t *event_table)
{
    if (bus_id < 0 || bus_id >= CONFIG_QM_EVENTBUS_MAX_BUS_NUM) 
    {
        return -1;
    }
    if (!buffer || buffer_size == 0 || !event_table) 
    {
        return -2;
    }

    qm_eventbus_t *bus = &_eventbus_instances[bus_id];

    uint8_t aligned_payload = (payload_size + 3) & ~3;
    bus->item_size = sizeof(qm_event_header_t) + aligned_payload;
    bus->capacity = (uint8_t)(buffer_size / bus->item_size);

    if (bus->capacity < 2) return -1; 

    bus->push_index = 0;
    bus->pop_index = 0;
    bus->buffer = (uint8_t *)buffer;
    bus->event_table = event_table;

#if CONFIG_QM_EVENTBUS_STATS
    memset(&bus->stats, 0, sizeof(qm_eventbus_stat_t));
    bus->stats.ring_slot_capacity = (uint16_t)bus->capacity;
    bus->stats.slot_size_bytes = (uint16_t)bus->item_size;
    bus->stats.delay_slot_capacity = 0;
#endif

    memset(buffer, 0, buffer_size);

    if (delay_buffer && delay_buffer_size > 0) {
        // 延时节点的步长 = 8字节控制头 + 业务Payload大小
        bus->delay_item_size = sizeof(qm_delayed_node_t) + aligned_payload;
        bus->delay_capacity = delay_buffer_size / bus->delay_item_size;
        bus->delay_buffer = (uint8_t *)delay_buffer;
        memset(delay_buffer, 0, delay_buffer_size); // 默认全为 EMPTY
#if CONFIG_QM_EVENTBUS_STATS
        bus->stats.delay_slot_capacity = bus->delay_capacity;
#endif
    } else {
        bus->delay_buffer = NULL;
        bus->delay_capacity = 0;
    }
    return 0;
}

static int _internal_post(qm_eventbus_t *bus, uint8_t m_id, uint8_t e_id, const void *payload_data, uint8_t payload_size)
{

    uint8_t next_push = (bus->push_index + 1) % bus->capacity;
    
    if (next_push == bus->pop_index) {
#if CONFIG_QM_EVENTBUS_STATS
        bus->stats.drop_count++;
#endif
        return -2; 
    }

#if CONFIG_QM_EVENTBUS_STATS
    uint8_t current_depth = (bus->push_index + bus->capacity - bus->pop_index) % bus->capacity;
    if ((current_depth + 1) > bus->stats.high_water_mark) {
        bus->stats.high_water_mark = current_depth + 1;
    }
    bus->stats.post_count++;
#endif

    uint8_t *slot_ptr = bus->buffer + (bus->push_index * bus->item_size);
    qm_event_header_t *hdr = (qm_event_header_t *)slot_ptr;
    hdr->module_id = m_id;
    hdr->event_id = e_id;
    hdr->is_cancelled = 0;

    if (payload_size > 0)
    {
        void *payload_dst = slot_ptr + sizeof(qm_event_header_t);
        memcpy(payload_dst, payload_data, payload_size);
        hdr->payload_size = payload_size;
    }
    else
    {
        hdr->payload_size = 0;
    }

    bus->push_index = next_push;
    
    return 0;
}

int qm_eventbus_post(int bus_id, uint8_t m_id, uint8_t e_id, const void *payload_data, uint8_t payload_size)
{
    qm_eventbus_t *bus = get_bus_instance(bus_id);
    if (!bus) 
    {
        return -1;
    }
    if (payload_size > (bus->item_size - sizeof(qm_event_header_t)))
    {
        return -2;
    }

    uint32_t irq_mask = qm_port_disable_irq();
    int ret = _internal_post(bus, m_id, e_id, payload_data, payload_size);
    qm_port_enable_irq(irq_mask);
    return ret;
}

int qm_eventbus_cancel(int bus_id, uint8_t m_id, uint8_t e_id)
{
    qm_eventbus_t *bus = get_bus_instance(bus_id);
    if (!bus) return 0;
    
    int cancel_count = 0;
    uint32_t irq_mask = qm_port_disable_irq();

    uint8_t curr = bus->pop_index;
    while (curr != bus->push_index) {
        qm_event_header_t *hdr = (qm_event_header_t *)(bus->buffer + (curr * bus->item_size));
        if (hdr->module_id == m_id && hdr->event_id == e_id && hdr->is_cancelled == 0) {
            hdr->is_cancelled = 1; 
            cancel_count++;
        }
        curr = (curr + 1) % bus->capacity;
    }

    // 2. 拆除延时池中的事件 (直接置 EMPTY)
    if (bus->delay_buffer) {
        for (int i = 0; i < bus->delay_capacity; i++) {
            qm_delayed_node_t *node = (qm_delayed_node_t *)(bus->delay_buffer + i * bus->delay_item_size);
            if (node->slot_state == QM_DELAY_SLOT_BUSY && 
                node->module_id == m_id && node->event_id == e_id) {
                node->slot_state = QM_DELAY_SLOT_EMPTY; // 直接终止周期！
                cancel_count++;
            }
        }
    }

#if CONFIG_QM_EVENTBUS_STATS
    bus->stats.cancel_count += cancel_count;
#endif

    qm_port_enable_irq(irq_mask);
    return cancel_count;
}

static void qm_eventbus_check_delayed_events(int bus_id)
{
    qm_eventbus_t *bus = get_bus_instance(bus_id);
    if (!bus || !bus->delay_buffer || bus->delay_capacity == 0) 
    {
        return;
    }

    uint32_t current_tick = qm_now_ms();
    uint32_t irq_mask = qm_port_disable_irq(); // 锁定延时池

    for (int i = 0; i < bus->delay_capacity; i++) {
        qm_delayed_node_t *node = (qm_delayed_node_t *)(bus->delay_buffer + i * bus->delay_item_size);
        
        if (node->slot_state == QM_DELAY_SLOT_BUSY) {
            // 安全的时间比较法，完美解决 49.7 天 SysTick 回绕溢出问题
            if ((int32_t)(current_tick - node->trigger_tick) >= 0) 
            {
                // 获取 Payload 指针
                void *payload_ptr = (node->payload_size > 0) ? 
                                    ((uint8_t *)node + sizeof(qm_delayed_node_t)) : NULL;

                // 尝试转正：将事件扔进主 RingBuf 队列
                // 注意：由于我们在一个大的关中断环境中，这里应该直接调内置逻辑，或者解锁调 API
                int ret = _internal_post(bus, node->module_id, node->event_id, payload_ptr, node->payload_size);
                
                if (ret == 0) {
                    // 转正成功，释放延时池槽位
                    if (node->period_ms > 0)
                    {
                        node->trigger_tick = current_tick + node->period_ms;
                    }
                    else
                    {
                        node->trigger_tick = 0;
                        node->slot_state = QM_DELAY_SLOT_EMPTY;
                    }
                } else {
                    // 主 RingBuf 满了！转正失败。
                    // 策略：保留在 BUSY 状态，下一次 tick 循环再试
                }
            }
        }
    }

    qm_port_enable_irq(irq_mask);
}

int qm_eventbus_process(int bus_id)
{
    qm_eventbus_t *bus = get_bus_instance(bus_id);
    if (!bus) return 0;

    qm_eventbus_check_delayed_events(bus_id);
    
    int process_count = 0;

    while (bus->pop_index != bus->push_index) 
    {
        uint8_t *slot_ptr = bus->buffer + (bus->pop_index * bus->item_size);
        qm_event_header_t *hdr = (qm_event_header_t *)slot_ptr;

        if (hdr->is_cancelled == 0) 
        {
            void *payload_ptr = hdr->payload_size > 0 ? (slot_ptr + sizeof(qm_event_header_t)) : NULL;

            for (int i = 0; bus->event_table[i].event.module_id != 0; i++) 
            {
                if ((bus->event_table[i].event.module_id == hdr->module_id) &&
                    ((bus->event_table[i].event.event_id == QM_EVENTBUS_ALL_EVENT) || 
                     (bus->event_table[i].event.event_id == hdr->event_id))) 
                {
                    qm_eventbus_handler_t *funcs = bus->event_table[i].handlers;
                    for (int j = 0; j < bus->event_table[i].event.handler_cnt; j++) {
                        if (funcs[j]) {
                            // 将 bus_id 透传给 Handler
                            funcs[j](bus_id, hdr->module_id, hdr->event_id, payload_ptr, hdr->payload_size);
                        }
                    }
                }
            }
#if CONFIG_QM_EVENTBUS_STATS
            bus->stats.process_count++;
#endif
            process_count++;
        }
        
        bus->pop_index = (bus->pop_index + 1) % bus->capacity;
    }

    return process_count;
}

#if CONFIG_QM_EVENTBUS_STATS
int qm_eventbus_get_stats(int bus_id, qm_eventbus_stat_t *out_stats)
{
    qm_eventbus_t *bus = get_bus_instance(bus_id);
    if (!bus || !out_stats) return -1;

    uint32_t irq_mask = qm_port_disable_irq();
    *out_stats = bus->stats;
    qm_port_enable_irq(irq_mask);
    
    return 0;
}
#endif

/**
 * @brief 发送延时/周期事件
 * @param bus_id 实例 ID
 * @param m_id 模块 ID
 * @param e_id 事件 ID
 * @param payload_data 载荷数据指针
 * @param payload_size 载荷数据大小
 * @param period_ms 重复触发周期
 * @param flags 延时事件标志 QM_DELAY_FLAG_APPEND 或 QM_DELAY_FLAG_OVERWRITE
 */
int qm_eventbus_post_delayed(int bus_id, uint8_t m_id, uint8_t e_id, const void *payload_data, 
                             uint8_t payload_size, uint32_t period_ms, uint8_t flags)
{
    qm_eventbus_t *bus = get_bus_instance(bus_id);
    if (!bus || !bus->delay_buffer || bus->delay_capacity == 0) 
    {
        return -1;
    }
    if (payload_size > (bus->delay_item_size - sizeof(qm_delayed_node_t))) 
    {
        return -2;
    }

    uint32_t irq_mask = qm_port_disable_irq();

    qm_delayed_node_t *target_node = NULL;

    // 1. 如果开启了 OVERWRITE，先尝试寻找是否已有同名的事件正在倒计时
    if (flags & QM_DELAY_FLAG_OVERWRITE) {
        for (int i = 0; i < bus->delay_capacity; i++) {
            qm_delayed_node_t *node = (qm_delayed_node_t *)(bus->delay_buffer + i * bus->delay_item_size);
            if (node->slot_state == QM_DELAY_SLOT_BUSY && 
                node->module_id == m_id && node->event_id == e_id) {
                target_node = node; // 找到了！直接复用这个槽位
                break;
            }
        }
    }

    // 2. 如果没找到，或者没开启 OVERWRITE，那就找一个空槽位
    if (target_node == NULL) {
        for (int i = 0; i < bus->delay_capacity; i++) {
            qm_delayed_node_t *node = (qm_delayed_node_t *)(bus->delay_buffer + i * bus->delay_item_size);
            if (node->slot_state == QM_DELAY_SLOT_EMPTY) {
                target_node = node;
                break;
            }
        }
    }

    // 3. 延时池满了
    if (target_node == NULL) {
#if CONFIG_QM_EVENTBUS_STATS
        bus->stats.drop_count++;
#endif
        qm_port_enable_irq(irq_mask);
        return -2; 
    }

    // 4. 更新节点数据 (重新计时)
    target_node->trigger_tick = qm_now_ms() + period_ms; 
    if (flags & QM_DELAY_FLAG_PERIODIC)
    {
        target_node->period_ms = period_ms;
    }
    else
    {
        target_node->period_ms = 0;
    }
    target_node->module_id = m_id;
    target_node->event_id = e_id;
    target_node->payload_size = payload_size;
    // 更新 Payload
    if (payload_size > 0 && payload_data) {
        memcpy((uint8_t *)target_node + sizeof(qm_delayed_node_t), payload_data, payload_size);
    }
    
    target_node->slot_state = QM_DELAY_SLOT_BUSY; 

    qm_port_enable_irq(irq_mask);
    return 0;
}
