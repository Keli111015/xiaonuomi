#ifndef MORPHCUBE_STUDY_RUNTIME_H
#define MORPHCUBE_STUDY_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "qm_pomodoro.h"

typedef struct {
    uint8_t hp;
    bool active;
    bool exhausted;
    uint32_t penalties;
    uint32_t posture_reminders;
    qm_pomodoro_status_t pomodoro;
} morphcube_study_status_t;

typedef enum {
    MORPHCUBE_POSE_UNKNOWN = 0,
    MORPHCUBE_POSE_NORMAL,
    MORPHCUBE_POSE_TILTED,
    MORPHCUBE_POSE_HEAD_LOWERED,
} morphcube_pose_t;

/* Call once after qm_pomodoro_create() and before starting the timer. */
int morphcube_study_runtime_init(qm_pomodoro_t timer);

/* These functions only enqueue work and never block the caller. */
int morphcube_study_runtime_on_pomodoro(const qm_pomodoro_event_data_t *event);
int morphcube_study_runtime_on_chat_detected(void);
int morphcube_study_runtime_submit_pose(morphcube_pose_t pose);

/* Thread-safe status snapshot for future UI/cloud reporting. */
int morphcube_study_runtime_get_status(morphcube_study_status_t *out);

/* Return true when Pomodoro is currently in active (non-paused, non-exhausted) Focus. */
bool morphcube_study_runtime_is_focus_active(void);

#endif
