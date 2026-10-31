#ifndef MC_STUDY_H
#define MC_STUDY_H

#include <stdbool.h>
#include <stdint.h>

/* Single-owner task only. The SDK Pomodoro remains the only focus clock. */
typedef enum { MC_UNKNOWN, MC_NORMAL, MC_TILTED, MC_HEAD_LOWERED } mc_pose;

typedef struct {
    uint8_t hp;
    uint16_t hp_tenths; /* 0..1000，代表 0.0..100.0 HP（精度 0.1） */
    bool active, paused_bleeding, exhausted, has_penalty, has_notice, has_pose;
    uint32_t penalties, reminders;
    uint64_t penalty_ms, pause_tick_ms, notice_ms, pose_ms, abnormal_ms;
} mc_study;

void mc_study_reset(mc_study *s);
void mc_study_set_active(mc_study *s, bool active);
void mc_study_set_paused(mc_study *s, bool paused, uint64_t now_ms);
/* 暂停期间每秒扣 0.2 点血（无音效），返回 true 表示本次扣减了血量 */
bool mc_study_pause_tick(mc_study *s, uint64_t now_ms);
/* Return true only when HP actually changed; caller pauses SDK at zero HP. */
bool mc_study_chat_detected(mc_study *s, uint64_t now_ms);
/* Feed each fresh inference once, using monotonic receipt time.
 * Unknown/normal or gaps >2s break continuity. Returns reminder event only.
 */
bool mc_study_pose(mc_study *s, mc_pose pose, uint64_t now_ms);

#endif
