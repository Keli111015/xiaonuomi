#include "qm_schedule_internal.h"

#include "qm_time.h"

static qm_schedule_occurrence_record_t *find_occurrence_locked(
    qm_schedule_t schedule,
    const char *schedule_id,
    int64_t occurrence_at,
    const char *reminder_id)
{
    uint16_t i;

    for (i = 0; i < QM_SCHEDULE_MAX_OCCURRENCES_INTERNAL; i++) {
        qm_schedule_occurrence_record_t *occ = &schedule->store.occurrences[i];
        if (occ->used &&
            strcmp(occ->schedule_id, schedule_id) == 0 &&
            occ->occurrence_at == occurrence_at &&
            strcmp(occ->reminder_id, reminder_id) == 0) {
            return occ;
        }
    }
    return NULL;
}

static qm_schedule_occurrence_record_t *alloc_occurrence_locked(
    qm_schedule_t schedule,
    const char *schedule_id,
    int64_t occurrence_at,
    const char *reminder_id)
{
    uint16_t i;

    for (i = 0; i < QM_SCHEDULE_MAX_OCCURRENCES_INTERNAL; i++) {
        qm_schedule_occurrence_record_t *occ = &schedule->store.occurrences[i];
        if (!occ->used) {
            memset(occ, 0, sizeof(*occ));
            occ->used = true;
            snprintf(occ->schedule_id, sizeof(occ->schedule_id), "%s",
                     schedule_id);
            occ->occurrence_at = occurrence_at;
            snprintf(occ->reminder_id, sizeof(occ->reminder_id), "%s",
                     reminder_id);
            occ->occurrence_state = QM_SCHEDULE_OCCURRENCE_PENDING;
            occ->reminder_state = QM_SCHEDULE_REMINDER_ACTIVE;
            return occ;
        }
    }
    return NULL;
}

static int append_triggered_outbox_locked(qm_schedule_t schedule,
                                          const char *schedule_id,
                                          int64_t occurrence_at,
                                          const char *reminder_id,
                                          int64_t event_at)
{
    uint16_t start = schedule->config.max_outbox_user;
    uint16_t end = start + schedule->config.max_outbox_system;
    uint16_t i;

    if (end > QM_SCHEDULE_MAX_OUTBOX_INTERNAL) {
        end = QM_SCHEDULE_MAX_OUTBOX_INTERNAL;
    }
    if (schedule->config.max_outbox_system == 0 || start >= end) {
        return -QM_SCHEDULE_E_OUTBOX_FULL;
    }

    for (i = start; i < end; i++) {
        qm_schedule_outbox_record_t *out = &schedule->store.outbox[i];
        if (out->used &&
            out->event_type == QM_SCHEDULE_OUTBOX_SYSTEM_TRIGGERED &&
            strcmp(out->schedule_id, schedule_id) == 0 &&
            out->occurrence_at == occurrence_at &&
            strcmp(out->reminder_id, reminder_id) == 0) {
            return QM_EOK;
        }
    }

    for (i = start; i < end; i++) {
        qm_schedule_outbox_record_t *out = &schedule->store.outbox[i];
        if (!out->used) {
            memset(out, 0, sizeof(*out));
            out->used = true;
            out->event_type = QM_SCHEDULE_OUTBOX_SYSTEM_TRIGGERED;
            snprintf(out->event_id, sizeof(out->event_id),
                     "trg:%s:%lld:%s", schedule_id,
                     (long long)occurrence_at, reminder_id);
            snprintf(out->binding_version, sizeof(out->binding_version), "%s",
                     schedule->store.meta.binding_version);
            snprintf(out->schedule_id, sizeof(out->schedule_id), "%s",
                     schedule_id);
            for (uint16_t item_index = 0;
                 item_index < schedule->config.max_items; item_index++) {
                if (schedule->store.items[item_index].used &&
                    strcmp(schedule->store.items[item_index].id,
                           schedule_id) == 0) {
                    out->revision = schedule->store.items[item_index].revision;
                    break;
                }
            }
            out->occurrence_at = occurrence_at;
            snprintf(out->reminder_id, sizeof(out->reminder_id), "%s",
                     reminder_id);
            out->event_at = event_at;
            return QM_EOK;
        }
    }
    return -QM_SCHEDULE_E_OUTBOX_FULL;
}

static int64_t now_utc(void)
{
    return (int64_t)qm_time(NULL);
}

static int compute_next_locked(qm_schedule_t schedule)
{
    int64_t now = now_utc();
    int64_t best_trigger = 0;
    uint16_t best_item = 0;
    uint8_t best_reminder = 0;
    uint16_t best_occurrence = 0;
    bool best_is_snooze = false;
    uint16_t i;

    if (schedule->store.meta.binding_version[0] == '\0' ||
        schedule->store.meta.lease_expires_at <= now) {
        schedule->next_trigger_at = 0;
        schedule->next_is_snooze = false;
        schedule->schedulable = false;
        return -QM_EBUSY;
    }

    for (i = 0; i < QM_SCHEDULE_MAX_OCCURRENCES_INTERNAL; i++) {
        qm_schedule_occurrence_record_t *occ = &schedule->store.occurrences[i];
        int item_idx;

        if (!occ->used || occ->reminder_state != QM_SCHEDULE_REMINDER_SNOOZED ||
            occ->snooze_until <= 0) {
            continue;
        }
        if (occ->snooze_until + CONFIG_QM_SCHEDULE_SNOOZE_RECOVERY_WINDOW_SEC <
            now) {
            occ->reminder_state = QM_SCHEDULE_REMINDER_CLOSED;
            occ->occurrence_state = QM_SCHEDULE_OCCURRENCE_EXPIRED;
            occ->close_reason = QM_SCHEDULE_CLOSE_EXPIRED;
            continue;
        }
        item_idx = -1;
        for (uint16_t j = 0; j < schedule->config.max_items; j++) {
            if (schedule->store.items[j].used &&
                strcmp(schedule->store.items[j].id, occ->schedule_id) == 0) {
                item_idx = (int)j;
                break;
            }
        }
        if (item_idx < 0) {
            continue;
        }
        if (best_trigger == 0 || occ->snooze_until < best_trigger) {
            best_trigger = occ->snooze_until;
            best_occurrence = i;
            best_is_snooze = true;
        }
    }

    for (i = 0; i < schedule->config.max_items; i++) {
        qm_schedule_item_record_t *item = &schedule->store.items[i];
        uint8_t r;

        if (!item->used || !item->enabled ||
            strcmp(item->status, "cancelled") == 0 ||
            strcmp(item->status, "ended") == 0) {
            continue;
        }
        for (r = 0; r < item->reminder_count; r++) {
            int64_t occurrence_at;
            int64_t trigger_at;
            int64_t after = now + ((int64_t)item->reminders[r].offset_min * 60);

            if (qm_schedule_recurrence_next_record(item, after,
                                                   &occurrence_at) != QM_EOK) {
                continue;
            }
            trigger_at = occurrence_at -
                ((int64_t)item->reminders[r].offset_min * 60);
            if (trigger_at <= now) {
                continue;
            }
            if (best_trigger == 0 || trigger_at < best_trigger) {
                best_trigger = trigger_at;
                best_item = i;
                best_reminder = r;
                best_is_snooze = false;
            }
        }
    }

    if (best_trigger == 0) {
        schedule->next_trigger_at = 0;
        schedule->schedulable = false;
        return -QM_ENOENT;
    }

    schedule->next_trigger_at = best_trigger;
    schedule->next_item_index = best_item;
    schedule->next_reminder_index = best_reminder;
    schedule->next_occurrence_index = best_occurrence;
    schedule->next_is_snooze = best_is_snooze;
    schedule->schedulable = true;
    return QM_EOK;
}

static int recover_one_missed(qm_schedule_t schedule,
                              qm_schedule_notify_t *notify,
                              char *notify_schedule_id,
                              uint32_t schedule_id_len,
                              char *notify_reminder_id,
                              uint32_t reminder_id_len,
                              qm_schedule_notify_cb_t *notify_cb,
                              void **user_ctx)
{
    int64_t now = now_utc();
    int64_t window_start;
    uint16_t i;

    if (CONFIG_QM_SCHEDULE_MISSED_WINDOW_SEC <= 0) {
        return -QM_ENOENT;
    }

    memset(notify, 0, sizeof(*notify));
    notify_schedule_id[0] = '\0';
    notify_reminder_id[0] = '\0';
    *notify_cb = NULL;
    *user_ctx = NULL;

    window_start = now - CONFIG_QM_SCHEDULE_MISSED_WINDOW_SEC;
    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    if (!schedule->running) {
        qm_mutex_unlock(&schedule->lock);
        return -QM_ENOENT;
    }

    for (i = 0; i < schedule->config.max_items; i++) {
        qm_schedule_item_record_t *item = &schedule->store.items[i];
        uint8_t r;

        if (!item->used || !item->enabled ||
            strcmp(item->status, "cancelled") == 0 ||
            strcmp(item->status, "ended") == 0) {
            continue;
        }

        for (r = 0; r < item->reminder_count; r++) {
            qm_schedule_reminder_record_t *reminder = &item->reminders[r];
            int64_t occurrence_at;
            int64_t trigger_at;
            int64_t after = (window_start - 1) +
                ((int64_t)reminder->offset_min * 60);
            qm_schedule_occurrence_record_t *occ;

            if (qm_schedule_recurrence_next_record(item, after,
                                                   &occurrence_at) != QM_EOK) {
                continue;
            }
            trigger_at = occurrence_at -
                ((int64_t)reminder->offset_min * 60);
            if (trigger_at < window_start || trigger_at > now) {
                continue;
            }
            if (find_occurrence_locked(schedule, item->id, occurrence_at,
                                       reminder->id)) {
                continue;
            }

            occ = alloc_occurrence_locked(schedule, item->id, occurrence_at,
                                          reminder->id);
            if (!occ) {
                qm_mutex_unlock(&schedule->lock);
                return -QM_EFULL;
            }
            occ->reminder_state = QM_SCHEDULE_REMINDER_ACTIVE;
            (void)append_triggered_outbox_locked(schedule, item->id,
                                                 occurrence_at, reminder->id,
                                                 now);

            snprintf(notify_schedule_id, schedule_id_len, "%s", item->id);
            snprintf(notify_reminder_id, reminder_id_len, "%s", reminder->id);
            notify->schedule_id = notify_schedule_id;
            notify->occurrence_at = occurrence_at;
            notify->reminder_id = notify_reminder_id;
            notify->occurrence_state = occ->occurrence_state;
            notify->reminder_state = occ->reminder_state;
            notify->close_reason = occ->close_reason;
            notify->event_at = now;
            *notify_cb = schedule->config.notify_cb;
            *user_ctx = schedule->config.user_ctx;
            qm_mutex_unlock(&schedule->lock);
            return QM_EOK;
        }
    }

    qm_mutex_unlock(&schedule->lock);
    return -QM_ENOENT;
}

static int recover_missed(qm_schedule_t schedule)
{
    uint16_t recovered = 0;

    while (recovered < QM_SCHEDULE_MAX_OCCURRENCES_INTERNAL) {
        qm_schedule_notify_t notify;
        qm_schedule_notify_cb_t notify_cb = NULL;
        char notify_schedule_id[QM_SCHEDULE_ID_MAX_DEFAULT];
        char notify_reminder_id[QM_SCHEDULE_REMINDER_ID_MAX_DEFAULT];
        void *user_ctx = NULL;
        int ret;

        ret = recover_one_missed(schedule, &notify,
                                 notify_schedule_id,
                                 sizeof(notify_schedule_id),
                                 notify_reminder_id,
                                 sizeof(notify_reminder_id),
                                 &notify_cb, &user_ctx);
        if (ret == -QM_ENOENT) {
            break;
        }
        if (ret != QM_EOK) {
            return ret;
        }
        (void)qm_schedule_store_save_all(schedule);
        if (notify_cb) {
            notify_cb(schedule, &notify, user_ctx);
        }
        recovered++;
    }
    return QM_EOK;
}

int qm_schedule_scheduler_start(qm_schedule_t schedule)
{
    if (!schedule) {
        return -QM_EINVAL;
    }
    (void)qm_schedule_expire_binding_if_needed(schedule, now_utc());
    (void)qm_schedule_cleanup(schedule, now_utc());
    (void)recover_missed(schedule);
    return qm_schedule_scheduler_rebuild(schedule);
}

int qm_schedule_scheduler_stop(qm_schedule_t schedule)
{
    if (!schedule) {
        return -QM_EINVAL;
    }

    if (schedule->timer_valid && schedule->timer_armed) {
        (void)qm_timer_stop(&schedule->timer);
    }
    if (!schedule->lock_valid) {
        return QM_EOK;
    }

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    schedule->timer_armed = false;
    schedule->schedulable = false;
    schedule->next_trigger_at = 0;
    schedule->next_is_snooze = false;
    qm_mutex_unlock(&schedule->lock);
    return QM_EOK;
}

int qm_schedule_scheduler_rebuild(qm_schedule_t schedule)
{
    int64_t next;
    int64_t now;
    int64_t delay_sec;
    int delay_ms;
    int ret;

    if (!schedule) {
        return -QM_EINVAL;
    }
    if (!schedule->timer_valid) {
        return QM_EOK;
    }

    (void)qm_schedule_expire_binding_if_needed(schedule, now_utc());

    (void)qm_timer_stop(&schedule->timer);

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    if (!schedule->running) {
        schedule->timer_armed = false;
        schedule->schedulable = false;
        qm_mutex_unlock(&schedule->lock);
        return QM_EOK;
    }
    ret = compute_next_locked(schedule);
    next = schedule->next_trigger_at;
    qm_mutex_unlock(&schedule->lock);
    if (ret != QM_EOK) {
        return QM_EOK;
    }

    now = now_utc();
    delay_sec = next > now ? (next - now) : 1;
    if (delay_sec > (QM_SCHEDULE_MAX_TIMER_MS / 1000)) {
        delay_ms = (int)QM_SCHEDULE_MAX_TIMER_MS;
    } else {
        delay_ms = (int)(delay_sec * 1000);
        if (delay_ms < (int)QM_SCHEDULE_MIN_TIMER_MS) {
            delay_ms = (int)QM_SCHEDULE_MIN_TIMER_MS;
        }
    }

    ret = qm_timer_change(&schedule->timer, delay_ms);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_timer_start(&schedule->timer);
    if (ret == QM_EOK) {
        qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
        schedule->timer_armed = true;
        qm_mutex_unlock(&schedule->lock);
    }
    return ret;
}

void qm_schedule_scheduler_fire(qm_schedule_t schedule)
{
    qm_schedule_notify_t notify;
    qm_schedule_notify_cb_t notify_cb = NULL;
    char notify_schedule_id[QM_SCHEDULE_ID_MAX_DEFAULT];
    char notify_reminder_id[QM_SCHEDULE_REMINDER_ID_MAX_DEFAULT];
    void *user_ctx = NULL;
    int persist = 0;

    if (!schedule) {
        return;
    }

    memset(&notify, 0, sizeof(notify));
    memset(notify_schedule_id, 0, sizeof(notify_schedule_id));
    memset(notify_reminder_id, 0, sizeof(notify_reminder_id));
    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    if (schedule->running && schedule->schedulable &&
        schedule->next_is_snooze &&
        schedule->next_occurrence_index < QM_SCHEDULE_MAX_OCCURRENCES_INTERNAL) {
        qm_schedule_occurrence_record_t *occ =
            &schedule->store.occurrences[schedule->next_occurrence_index];
        qm_schedule_item_record_t *item = NULL;
        qm_schedule_reminder_record_t *reminder = NULL;
        uint16_t i;

        for (i = 0; i < schedule->config.max_items; i++) {
            if (schedule->store.items[i].used &&
                strcmp(schedule->store.items[i].id, occ->schedule_id) == 0) {
                item = &schedule->store.items[i];
                break;
            }
        }
        if (item) {
            for (i = 0; i < item->reminder_count; i++) {
                if (strcmp(item->reminders[i].id, occ->reminder_id) == 0) {
                    reminder = &item->reminders[i];
                    break;
                }
            }
        }
        if (occ->used && item && reminder) {
            occ->snooze_until = 0;
            occ->reminder_state = QM_SCHEDULE_REMINDER_ACTIVE;
            snprintf(notify_schedule_id, sizeof(notify_schedule_id), "%s",
                     occ->schedule_id);
            snprintf(notify_reminder_id, sizeof(notify_reminder_id), "%s",
                     occ->reminder_id);
            notify.schedule_id = notify_schedule_id;
            notify.occurrence_at = occ->occurrence_at;
            notify.reminder_id = notify_reminder_id;
            notify.occurrence_state = occ->occurrence_state;
            notify.reminder_state = occ->reminder_state;
            notify.close_reason = occ->close_reason;
            notify.event_at = now_utc();
            (void)append_triggered_outbox_locked(schedule, occ->schedule_id,
                                                 occ->occurrence_at,
                                                 occ->reminder_id,
                                                 notify.event_at);
            notify_cb = schedule->config.notify_cb;
            user_ctx = schedule->config.user_ctx;
            persist = 1;
        }
    } else if (schedule->running && schedule->schedulable &&
               schedule->next_item_index < schedule->config.max_items) {
        qm_schedule_item_record_t *item =
            &schedule->store.items[schedule->next_item_index];
        qm_schedule_reminder_record_t *reminder =
            &item->reminders[schedule->next_reminder_index];
        int64_t occurrence_at = schedule->next_trigger_at +
            ((int64_t)reminder->offset_min * 60);
        qm_schedule_occurrence_record_t *occ =
            find_occurrence_locked(schedule, item->id, occurrence_at, reminder->id);

        if (!occ) {
            occ = alloc_occurrence_locked(schedule, item->id, occurrence_at,
                                          reminder->id);
        }
        if (occ) {
            occ->reminder_state = QM_SCHEDULE_REMINDER_ACTIVE;
            snprintf(notify_schedule_id, sizeof(notify_schedule_id), "%s",
                     item->id);
            snprintf(notify_reminder_id, sizeof(notify_reminder_id), "%s",
                     reminder->id);
            notify.schedule_id = notify_schedule_id;
            notify.occurrence_at = occurrence_at;
            notify.reminder_id = notify_reminder_id;
            notify.occurrence_state = occ->occurrence_state;
            notify.reminder_state = occ->reminder_state;
            notify.close_reason = occ->close_reason;
            notify.event_at = now_utc();
            (void)append_triggered_outbox_locked(schedule, item->id,
                                                 occurrence_at, reminder->id,
                                                 notify.event_at);
            notify_cb = schedule->config.notify_cb;
            user_ctx = schedule->config.user_ctx;
            persist = 1;
        }
    }
    schedule->timer_armed = false;
    schedule->last_trigger_at = schedule->next_trigger_at;
    schedule->next_trigger_at = 0;
    schedule->next_is_snooze = false;
    qm_mutex_unlock(&schedule->lock);

    if (persist) {
        (void)qm_schedule_store_save_all(schedule);
    }
    if (notify_cb) {
        notify_cb(schedule, &notify, user_ctx);
    }
    (void)qm_schedule_scheduler_rebuild(schedule);
}

void qm_schedule_scheduler_timer_cb(qm_timer_t *timer, void *arg)
{
    (void)timer;
    qm_schedule_scheduler_fire((qm_schedule_t)arg);
}
