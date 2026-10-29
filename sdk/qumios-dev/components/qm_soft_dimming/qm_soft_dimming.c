#include "qm.h"
#include "qm_soft_dimming.h"
#include <string.h>

/* ==========================================================
 * 核心配置：定点数倍率
 * 改为 100 倍。即内部计算精度为 0.01
 * ========================================================== */
#define FIXED_SCALE  100

/* 内部工具保持不变... */
static uint32_t _int_sqrt(uint32_t n) {
    if (n < 2) return n;
    uint32_t x = n, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return x;
}

static uint32_t _calc_phys_total_steps(SoftDim_Handle_t *handle) {
    uint32_t R  = handle->sw_res;
    /* 还原回整数估算: 除以 100 */
    uint32_t M  = handle->min_step_fixed / FIXED_SCALE;
    uint32_t Vm = handle->max_step_fixed / FIXED_SCALE;
    uint32_t RK = handle->curve_divisor;

    if (M == 0) M = 1;

    uint32_t C = _int_sqrt(M * RK);
    uint32_t S = _int_sqrt(Vm * RK);

    if (S > R) S = R;
    if (C > R) C = R;
    if (C > S) C = S;

    uint32_t steps = 0;
    if (M > 0) steps += C / M;
    if (S > C && C > 0) steps += (RK / C) - (RK / S);
    if (R > S && Vm > 0) steps += (R - S) / Vm;
    return steps;
}

static void _update_tick_interval(SoftDim_Handle_t *handle, uint32_t fade_ms) {
    if (handle->timer_period == 0) return;

    /* 1. 计算总中断次数 */
    uint32_t total_timer_ticks = fade_ms / handle->timer_period;

    /* 安全检查：防止被除数为 0 */
    if (handle->phys_total_steps == 0) handle->phys_total_steps = 1;

    /* 2. 计算间隔 (向上取整) */
    /* 原理：(被除数 + 除数 - 1) / 除数 */
    /* 例子：如果需要跑 100 步，总 tick 是 250。
     *      向下取整: 250 / 100 = 2 (间隔2, 总耗时200 tick, 偏快)
     *      向上取整: (250 + 99) / 100 = 3 (间隔3, 总耗时300 tick, 偏慢但保底)
     */
    uint32_t interval = (total_timer_ticks + handle->phys_total_steps - 1) / handle->phys_total_steps;

    /* 3. 限制最小间隔 */
    if (interval < 1) interval = 1;

    handle->tick_interval = interval;

    /* 立即重置计数器，确保新指令立即响应 */
    handle->tick_acc = interval;

    /* 重置速度记忆 */
    handle->prev_step_fixed = handle->min_step_fixed;
}

void qm_softdim_init(qm_softdim_handle_t *handle, const qm_softdim_config_t *config)
{
    handle->active_ch_count = config->channel_count;
    if (handle->active_ch_count > SOFT_DIM_MAX_CHANNELS) handle->active_ch_count = SOFT_DIM_MAX_CHANNELS;

    handle->sw_res = config->sw_resolution;
    handle->timer_period = config->timer_base_ms;

    for(int i=0; i < SOFT_DIM_MAX_CHANNELS; i++) {
        handle->curr[i] = 0;
        handle->dest[i] = 0;
        handle->curr_fixed[i] = 0;
    }

    /* 1. 计算启动马达 (定点数化 x100) */
    uint32_t ratio = config->sw_resolution / config->hw_resolution;
    if (ratio < 1) ratio = 1;
    handle->min_step_fixed = ratio * FIXED_SCALE;

    /* 2. 计算限速 (定点数化 x100) */
    uint16_t max_int = (uint16_t)(config->sw_resolution * config->max_speed_pct / 100.0f);
    if (max_int < 1) max_int = 1;
    handle->max_step_fixed = max_int * FIXED_SCALE;

    handle->curve_divisor = config->sw_resolution * config->damping_coef;

    handle->phys_total_steps = _calc_phys_total_steps(handle);
    if (handle->phys_total_steps == 0) handle->phys_total_steps = 1;

    handle->tick_interval = 1;
    handle->tick_acc = 0;
    handle->prev_step_fixed = handle->min_step_fixed;
}

void softdim_setTarget(softdim_handle_t *handle, uint8_t ch_idx, uint16_t val, uint32_t fade_ms)
{
    if (ch_idx < handle->active_ch_count) {
        handle->dest[ch_idx] = val;
    }
    _update_tick_interval(handle, fade_ms);
}

void softdim_setAll(qm_softdim_handle_t *handle, uint16_t val, uint32_t fade_ms)
{
    for(int i=0; i < handle->active_ch_count; i++) {
        handle->dest[i] = val;
    }
    _update_tick_interval(handle, fade_ms);
}

uint16_t qm_softdim_getCurrent(qm_softdim_dandle_t *handle, uint8_t ch_idx)
{
    if (ch_idx < handle->active_ch_count) {
        return handle->curr[ch_idx];
    }
    return 0;
}

bool qm_softdim_isActive(qm_softdim_handle_t *handle)
{
    for(int i = 0; i < handle->active_ch_count; i++) {
        if (handle->curr[i] != handle->dest[i]) return true;
    }
    return false;
}

bool qm_softdim_run(qm_softdim_handle_t *handle)
{
    handle->tick_acc++;
    if (handle->tick_acc < handle->tick_interval) return false;
    handle->tick_acc = 0;

    /* 寻找领头羊 */
    uint32_t max_diff = 0;
    int16_t leader_idx = -1;
    bool leader_is_decreasing = false;
    bool any_change = false;

    for (int i = 0; i < handle->active_ch_count; i++)
    {
        uint32_t diff;
        bool is_decreasing;
        if (handle->curr[i] > handle->dest[i]) {
            diff = handle->curr[i] - handle->dest[i];
            is_decreasing = true;
        } else {
            diff = handle->dest[i] - handle->curr[i];
            is_decreasing = false;
        }

        if (diff == 0) continue;
        any_change = true;

        if (diff > max_diff) {
            max_diff = diff;
            leader_idx = i;
            leader_is_decreasing = is_decreasing;
        } else if (diff == max_diff) {
            if (is_decreasing && !leader_is_decreasing) {
                leader_idx = i;
                leader_is_decreasing = true;
            }
        }
    }

    if (!any_change || leader_idx == -1) return false;

    /* ============================================================
     * 核心计算：高精度定点数域 (x100)
     * ============================================================ */
    uint16_t leader_curr = handle->curr[leader_idx];
    uint32_t target_delta_fixed;

    /* 1. 曲线计算 (使用 64位 中间变量防止溢出) */
    /* speed_fixed = (curr^2 * 100) / divisor */
    /* 1亿 * 100 = 100亿，超过 uint32，必须用 uint64 */
    uint64_t val_sq = (uint64_t)leader_curr * leader_curr;

    /* 改用乘法 */
    uint32_t curve_speed_fixed = (uint32_t)((val_sq * FIXED_SCALE) / handle->curve_divisor);

    /* 2. 竞争 */
    if (curve_speed_fixed > handle->min_step_fixed) {
        target_delta_fixed = curve_speed_fixed;
    } else {
        target_delta_fixed = handle->min_step_fixed;
    }

    /* 3. 封顶 */
    if (target_delta_fixed > handle->max_step_fixed) {
        target_delta_fixed = handle->max_step_fixed;
    }

    /* 4. 自适应加速度限制 */
    if (!leader_is_decreasing && target_delta_fixed > handle->prev_step_fixed)
    {
        /* 允许每次增长 25% -> 除以4 */
        /* 虽然用了乘100，但增长率比例计算还是除以4最快且合理 */
        uint32_t growth = handle->prev_step_fixed >> 2;
        if (growth < 1) growth = 1;

        uint32_t max_allowed = handle->prev_step_fixed + growth;
        if (target_delta_fixed > max_allowed) {
            target_delta_fixed = max_allowed;
        }
    }
    handle->prev_step_fixed = target_delta_fixed;

    /* 执行更新：小数累积机制 */
    for (int i = 0; i < handle->active_ch_count; i++)
    {
        /* 目标值转高精度 x100 */
        uint32_t dest_fixed = (uint32_t)handle->dest[i] * FIXED_SCALE;

        /* 首次同步检测：检查整数部分是否脱节 */
        if ((handle->curr_fixed[i] / FIXED_SCALE) != handle->curr[i]) {
            handle->curr_fixed[i] = (uint32_t)handle->curr[i] * FIXED_SCALE;
        }

        if (handle->curr[i] == handle->dest[i]) {
            handle->curr_fixed[i] = dest_fixed;
            continue;
        }

        if (handle->curr[i] < handle->dest[i]) {
            /* 升亮 */
            if (dest_fixed > handle->curr_fixed[i] &&
                dest_fixed - handle->curr_fixed[i] > target_delta_fixed) {
                handle->curr_fixed[i] += target_delta_fixed;
                } else {
                    handle->curr_fixed[i] = dest_fixed;
                }
        } else {
            /* 变暗 */
            if (handle->curr_fixed[i] > dest_fixed &&
                handle->curr_fixed[i] - dest_fixed > target_delta_fixed) {
                handle->curr_fixed[i] -= target_delta_fixed;
                } else {
                    handle->curr_fixed[i] = dest_fixed;
                }
        }

        /* [输出] 除以 100 还原回整数 */
        handle->curr[i] = handle->curr_fixed[i] / FIXED_SCALE;
    }

    return true;
}
