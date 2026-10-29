#ifndef QM_SOFT_DIMMING_H_
#define QM_SOFT_DIMMING_H_

#include <stdint.h>
#include <stdbool.h>

#ifndef QM_SOFT_DIM_MAX_CHANNELS
#define QM_SOFT_DIM_MAX_CHANNELS   5
#endif

typedef struct {
    uint8_t  channel_count;
    uint32_t sw_resolution;
    uint32_t hw_resolution;
    uint32_t damping_coef;
    float    max_speed_pct;
    uint32_t timer_base_ms;
} qm_softdim_config_t;

typedef struct {
    uint16_t curr[QM_SOFT_DIM_MAX_CHANNELS];
    uint16_t dest[QM_SOFT_DIM_MAX_CHANNELS];

    /* 核心升级：Q.2 格式的定点数 (实际值 * 100) */
    uint32_t curr_fixed[SOFT_DIM_MAX_CHANNELS];

    uint8_t  active_ch_count;
    uint32_t sw_res;
    uint32_t curve_divisor;

    uint32_t min_step_fixed;
    uint32_t max_step_fixed;

    uint32_t timer_period;
    uint32_t phys_total_steps;
    uint32_t tick_interval;
    uint32_t tick_acc;
    uint32_t prev_step_fixed;
} qm_softdim_handle_t;

void qm_softdim_init(qm_softdim_handle_t *handle, const qm_softdim_config_t *config);
void qm_softdim_setTarget(qm_softdim_handle_t *handle, uint8_t ch_idx, uint16_t val, uint32_t fade_ms);
void qm_softdim_setAll(qm_softdim_handle_t *handle, uint16_t val, uint32_t fade_ms);
uint16_t qm_softdim_getCurrent(qm_softdim_handle_t *handle, uint8_t ch_idx);
bool qm_softdim_run(qm_softdim_handle_t *handle);
bool qm_softdim_isActive(qm_softdim_handle_t *handle);

#endif
