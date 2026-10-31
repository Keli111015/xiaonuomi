#include "mc_study.h"

#include <string.h>

#define MC_POSE_SAMPLE_GAP_MS   25000u  /* 两次不良样本之间允许的最大间隔 */
#define MC_POSE_PENALTY_TENTHS  50u     /* 坐姿提醒扣血：5 点（单位 0.1） */

void mc_study_reset(mc_study *s)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->hp = 100;
    s->hp_tenths = 1000;
}

void mc_study_set_active(mc_study *s, bool active)
{
    if (!s) return;
    s->active = active && !s->exhausted;
    if (s->active) {
        s->paused_bleeding = false;
    }
    s->has_pose = false;
    s->abnormal_ms = 0;
}

void mc_study_set_paused(mc_study *s, bool paused, uint64_t now)
{
    if (!s) return;
    if (paused && !s->exhausted) {
        s->active = false;
        s->paused_bleeding = true;
        s->pause_tick_ms = now;
        s->has_pose = false;
        s->abnormal_ms = 0;
    } else {
        s->paused_bleeding = false;
    }
}

bool mc_study_pause_tick(mc_study *s, uint64_t now)
{
    if (!s || !s->paused_bleeding || s->exhausted) return false;
    if (now < s->pause_tick_ms || now - s->pause_tick_ms < 1000) return false;

    while (now - s->pause_tick_ms >= 1000 && !s->exhausted) {
        s->pause_tick_ms += 1000;
        s->hp_tenths = s->hp_tenths >= 2 ? (uint16_t)(s->hp_tenths - 2) : 0;
        s->hp = (uint8_t)(s->hp_tenths / 10);
        if (s->hp_tenths == 0) {
            s->exhausted = true;
            s->paused_bleeding = false;
        }
    }
    return true;
}

bool mc_study_chat_detected(mc_study *s, uint64_t now)
{
    if (!s || !s->active || s->exhausted) return false;
    /* 1秒防抖：避免同一句话的重复事件在极短时间内重复扣血 */
    if (s->has_penalty &&
        (now < s->penalty_ms || now - s->penalty_ms < 1000)) return false;
    s->hp_tenths = s->hp_tenths >= 100 ? (uint16_t)(s->hp_tenths - 100) : 0;
    s->hp = (uint8_t)(s->hp_tenths / 10);
    s->penalty_ms = now;
    s->has_penalty = true;
    s->penalties++;
    if (s->hp_tenths == 0) {
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
    /* 抽样周期 10 秒（语音对话忙时会跳过一次，间隔可能到 20 秒）；
     * 若超过 25 秒未有新的不良样本（例如中途检测跳过或恢复正常），重置累计时长 */
    if (!s->has_pose || now < s->pose_ms || now - s->pose_ms > MC_POSE_SAMPLE_GAP_MS) {
        s->abnormal_ms = 0;
    } else {
        uint64_t dt = now - s->pose_ms;
        /* Missing inference time is not evidence of continued bad posture. */
        if (dt > 2000) dt = 2000;
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
    /* 坐姿提醒：扣 5 点血 */
    s->hp_tenths = s->hp_tenths >= MC_POSE_PENALTY_TENTHS
                       ? (uint16_t)(s->hp_tenths - MC_POSE_PENALTY_TENTHS) : 0;
    s->hp = (uint8_t)(s->hp_tenths / 10);
    s->penalties++;
    if (s->hp_tenths == 0) {
        s->exhausted = true;
        mc_study_set_active(s, false);
    }
    return true;
}
