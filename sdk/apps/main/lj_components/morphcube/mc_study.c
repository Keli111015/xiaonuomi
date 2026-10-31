#include "mc_study.h"

#include <string.h>

void mc_study_reset(mc_study *s)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->hp = 100;
}

void mc_study_set_active(mc_study *s, bool active)
{
    if (!s) return;
    s->active = active && !s->exhausted;
    s->has_pose = false;
    s->abnormal_ms = 0;
}

bool mc_study_distraction(mc_study *s, bool confirmed, uint64_t now)
{
    if (!s || !s->active || !confirmed || s->exhausted) return false;
    if (s->has_penalty &&
        (now < s->penalty_ms || now - s->penalty_ms < 60000)) return false;
    s->hp = s->hp >= 10 ? (uint8_t)(s->hp - 10) : 0;
    s->penalty_ms = now;
    s->has_penalty = true;
    s->penalties++;
    if (!s->hp) {
        s->exhausted = true;
        mc_study_set_active(s, false);
    }
    return true;
}

bool mc_study_pose(mc_study *s, mc_pose pose, uint64_t now)
{
    if (!s) return false;
    if (!s->active || (pose != MC_TILTED && pose != MC_HEAD_LOWERED)) {
        s->has_pose = false;
        s->abnormal_ms = 0;
        return false;
    }
    /* 抽样周期 2 秒；若超过 8 秒未有新的不良样本（例如中途检测跳过或恢复正常），重置累计时长 */
    if (!s->has_pose || now < s->pose_ms || now - s->pose_ms > 8000) {
        s->abnormal_ms = 0;
    } else {
        uint64_t dt = now - s->pose_ms;
        s->abnormal_ms = s->abnormal_ms + dt > 15000 ? 15000 : s->abnormal_ms + dt;
    }
    s->has_pose = true;
    s->pose_ms = now;
    if (s->abnormal_ms < 15000) return false;
    if (s->has_notice && (now < s->notice_ms || now - s->notice_ms < 60000)) return false;
    s->notice_ms = now;
    s->has_notice = true;
    s->reminders++;
    s->abnormal_ms = 0;
    return true;
}
