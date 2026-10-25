#ifndef MC_STUDY_H
#define MC_STUDY_H

#include <stdbool.h>
#include <stdint.h>

/* Single-owner task only. The SDK Pomodoro remains the only focus clock. */
typedef enum { MC_UNKNOWN, MC_NORMAL, MC_TILTED, MC_HEAD_LOWERED } mc_pose;

typedef struct {
    uint8_t hp;
    bool active, exhausted, has_penalty, has_notice, has_pose;
    uint32_t penalties, reminders;
    uint64_t penalty_ms, notice_ms, pose_ms, abnormal_ms;
} mc_study;

void mc_study_reset(mc_study *s);
void mc_study_set_active(mc_study *s, bool active);
/* Return true only when HP actually changed; caller pauses SDK at zero HP. */
bool mc_study_distraction(mc_study *s, bool confirmed, uint64_t now_ms);
/* Feed each fresh inference once, using monotonic receipt time.
 * Unknown/normal or gaps >2s break continuity. Returns reminder event only.
 */
bool mc_study_pose(mc_study *s, mc_pose pose, uint64_t now_ms);

#endif
