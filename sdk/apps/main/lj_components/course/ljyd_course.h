/**
 ******************************************************************************
 * @file    ljyd_course.h
 * @brief   课程表（课表）业务 API。
 *
 * @details
 * 本模块独立实现课程表的完整链路，不依赖 qm_course 中间件：
 *  - 数据模型（周几 / 节次 / 科目 / 教材）；
 *  - Flash 持久化（qm_kv，key = "course_cfg"，与旧固件格式兼容）；
 *  - 云端 `course/sync` 全量同步、`course/syncAck` 应答；
 *  - 当日课程查询与晨间播报触发。
 *
 * 端侧是“被动接收端”，不会主动向云端请求课表。
 ******************************************************************************
 */
#ifndef LJYD_COURSE_H
#define LJYD_COURSE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 课表条目上限（与云端同步、Flash 存储格式绑定，请勿随意修改）。 */
#define LJYD_COURSE_MAX_ENTRIES         32

/** @brief 科目名称最大字节数（含 '\0'）。 */
#define LJYD_COURSE_SUBJECT_MAX_LEN     32

/** @brief 教材名称最大字节数（含 '\0'）。 */
#define LJYD_COURSE_TEXTBOOK_MAX_LEN    64

/** @brief Flash 存储 key。 */
#define LJYD_COURSE_KV_KEY              "course_cfg"

/** @brief 云端下行 method。 */
#define LJYD_COURSE_METHOD              "course"
/** @brief 全量同步子方法。 */
#define LJYD_COURSE_SUB_SYNC            "sync"
/** @brief 全量同步应答子方法。 */
#define LJYD_COURSE_SUB_ACK             "syncAck"

/** @brief 晨间播报使用的产品物模型属性：siid=10 / piid=1。 */
#define LJYD_COURSE_TTS_SIID            10
#define LJYD_COURSE_TTS_PIID            1

/** @brief 本地时间可信的最小年份（未对计时为 1970，周几不可用）。 */
#define LJYD_COURSE_MIN_VALID_YEAR      2024

/**
 * @brief 单条课程。
 *
 * @note @p subject 与 @p textbook 为 UTF-8 字符串，超长时由同步方截断。
 */
typedef struct {
    uint8_t weekday;                                  /**< 周几，1~7，1 = 周一。 */
    uint8_t period;                                   /**< 节次，升序显示。 */
    char    subject[LJYD_COURSE_SUBJECT_MAX_LEN];     /**< 科目名称。 */
    char    textbook[LJYD_COURSE_TEXTBOOK_MAX_LEN];   /**< 教材名称，可为空串。 */
} ljyd_course_entry_t;

/**
 * @brief 启动课程表：从 Flash 恢复课表，并注册云事件与 `course` 下行。
 *
 * @param[in] iot_handle qm_cloud 会话句柄（ljyd_cloud_get_handle()）。
 *
 * @return int
 * @retval QM_EOK     成功（下行注册失败会在连云事件中重试）。
 * @retval -QM_EINVAL @p iot_handle 为 NULL。
 *
 * @note 需在 ljyd_cloud_init() 之后调用。
 */
int ljyd_course_init(void *iot_handle);

/**
 * @brief 清空内存中的课表并删除 Flash 数据（恢复出厂时调用）。
 *
 * @note 可重复调用；未初始化时也可安全调用。
 */
void ljyd_course_clear_configs(void);

/**
 * @brief 查询是否已完成过一次云端同步。
 *
 * @return int 1 = 已同步；0 = 未同步。
 */
int ljyd_course_is_synced(void);

/**
 * @brief 获取当前课表条目总数（全部周几）。
 *
 * @return uint16_t 条目数。
 */
uint16_t ljyd_course_entry_count(void);

/**
 * @brief 获取指定周几的课程，并按节次（period）升序排列。
 *
 * @param[in]  weekday 周几，1~7（1 = 周一）。
 * @param[out] out     输出缓冲区。
 * @param[in]  max     @p out 可容纳的最大条目数。
 *
 * @return int 实际写入 @p out 的条目数（>= 0）；参数非法返回 -QM_EINVAL 。
 */
int ljyd_course_get_day_entries(int weekday, ljyd_course_entry_t *out, int max);

/**
 * @brief 取当前本地日期对应的周几（1~7，周一 = 1）。
 *
 * @param[out] out_weekday 输出周几。
 *
 * @return int
 * @retval QM_EOK     成功。
 * @retval -QM_EINVAL @p out_weekday 为 NULL。
 * @retval -QM_EINIT  本地时间未就绪（无法取得有效周几）。
 */
int ljyd_course_get_today_weekday(int *out_weekday);

/**
 * @brief 读取并清除“课表已更新”标志（供 UI / 业务轮询）。
 *
 * @return int 1 = 自上次读取后收到过云端同步；0 = 无更新。
 */
int ljyd_course_take_update_flag(void);

/**
 * @brief 晨间播报：上报今日周几（siid=10 / piid=1），由云端 TTS 播报当天课程。
 *
 * @note 未同步、云端未就绪或本地时间无效（年份 < 2024）时直接跳过。
 */
void ljyd_course_broadcast_today(void);

#ifdef __cplusplus
}
#endif

#endif /* LJYD_COURSE_H */
