# qm_alarm

For the Chinese version, see [README_CN.md](README_CN.md).

`qm_alarm` is a standalone alarm component split out from `qm_agent_alarm`.
It keeps the mature storage, scheduling, snooze, JSON, and cloud-sync behavior
from the original implementation, but removes the runtime dependency on
`qm_agent`.

## Overview

`qm_alarm` owns the device-side alarm domain:

- in-memory alarm runtime state
- persistent alarm storage through KV
- timer-based scheduling and next-alarm computation
- active-alarm ring, dismiss, and snooze flow
- optional cloud sync through `qm_iot`

## Scope

`qm_alarm` is responsible for:

- creating, modifying, deleting, and enabling alarms
- persisting alarm data and restoring it on startup
- computing the next effective alarm from repeat rules
- driving ring, dismiss, and snooze callbacks
- receiving and reporting the `alarm` / `alarmReply` cloud protocol

`qm_alarm` is not responsible for:

- actual audio playback or vibration hardware control
- user interface presentation
- `qm_agent` session lifecycle
- generic cloud-session creation

## Current Shape

- Public API is instance-based: `qm_alarm_t`
- Local operations no longer require `qm_agent_handle_t`
- Cloud sync still uses the original `alarm` / `alarmReply` protocol
- Cloud transport is bound directly by `qm_alarm_config_t.iot_handle`
- Internal logic is still largely copied from `qm_agent_alarm` to reduce migration risk

## Runtime Configuration

Runtime parameters are provided through `qm_alarm_config_t`:

- `iot_handle`: existing `qm_iot` session handle; `NULL` disables cloud sync
- `max_alarms`: maximum number of alarms kept in memory; `0` uses
  `CONFIG_QM_ALARM_MAX_ALARMS`, non-zero values must not exceed it
- `notify_cb`: callback invoked on ring, dismiss, snooze, and alarm changes
- `user_ctx`: user context passed back to the callback

Fixed defaults are configured by Kconfig:

- `CONFIG_QM_ALARM_SUPPORT`: enables the standalone alarm component
- `CONFIG_QM_ALARM_MAX_ALARMS`: compile-time maximum alarm capacity, also used
  as the default runtime capacity
- `CONFIG_QM_ALARM_SNOOZE_INTERVAL_MIN`: default snooze interval in minutes

## Logging

`QM_ALARM_LOG_CONFIG` is the master Kconfig switch for category defaults. When
disabled, only error and lifecycle logs are enabled. When enabled, choose the
required categories from `QM_ALARM_LOG_ERROR`, `QM_ALARM_LOG_LIFECYCLE`,
`QM_ALARM_LOG_CONTROL`, `QM_ALARM_LOG_SCHEDULER`, `QM_ALARM_LOG_STORAGE`,
`QM_ALARM_LOG_CLOUD`, and `QM_ALARM_LOG_STATS`.

The runtime mask is component-wide and applies to every alarm instance:

```c
qm_alarm_set_log_flags(QM_ALARM_LOG_ERROR |
                       QM_ALARM_LOG_LIFECYCLE |
                       QM_ALARM_LOG_CLOUD);
```

Use `qm_alarm_set_log_flags(QM_ALARM_LOG_NONE)` to disable all `qm_alarm`
logs. This does not change the QUMI global log level or other components.

## Data Model

`qm_alarm_item_t` is the core alarm snapshot type. Important fields:

- `alarm_id`: numeric alarm identifier in range `1..255`
- `source`: alarm source such as `device` or `app`
- `hour` / `minute`: local trigger time
- `enabled`: whether the alarm participates in scheduling
- `repeat_mask`: weekday repeat bits using `QM_ALARM_REPEAT_*`
- `tone`: structured tone selection with `type` and `id`
- `snooze`: per-alarm snooze configuration
- `extensions`: optional JSON object string copied and stored by the component

## Public API Summary

```c
qm_alarm_t qm_alarm_create(const qm_alarm_config_t *config);
int qm_alarm_start(qm_alarm_t alarm);
int qm_alarm_stop(qm_alarm_t alarm);
int qm_alarm_create_item(qm_alarm_t alarm, const qm_alarm_item_t *item);
int qm_alarm_modify_item(qm_alarm_t alarm, const qm_alarm_item_t *item);
int qm_alarm_delete_item(qm_alarm_t alarm, const char *source, uint8_t alarm_id);
int qm_alarm_set_enabled(qm_alarm_t alarm, const char *source, uint8_t alarm_id, qm_bool enabled);
int qm_alarm_snooze(qm_alarm_t alarm);
int qm_alarm_cancel_snooze(qm_alarm_t alarm);
int qm_alarm_dismiss(qm_alarm_t alarm);
int qm_alarm_get_state(qm_alarm_t alarm, qm_alarm_state_t *state, uint8_t *alarm_id);
int qm_alarm_report(qm_alarm_t alarm);
int qm_alarm_list(qm_alarm_t alarm, qm_alarm_item_t *out, uint16_t capacity, uint16_t *count);
int qm_alarm_get_upcoming(qm_alarm_t alarm, qm_alarm_item_t *out, qm_time_t *trigger_time);
void qm_alarm_destroy(qm_alarm_t alarm);
```

## Lifecycle

Typical usage flow:

1. Create a runtime with `qm_alarm_create()`.
2. Start scheduling and optional cloud registration with `qm_alarm_start()`.
3. Apply local operations or receive cloud operations.
4. Handle ring, dismiss, snooze, or changed notifications through `notify_cb`.
5. Stop the runtime with `qm_alarm_stop()` if needed.
6. Release resources with `qm_alarm_destroy()`.

## Runtime Behavior

- Alarm data is persisted through KV and reloaded on the next create/start cycle.
- Local device create auto-assigns the first free alarm id when `alarm_id == 0`.
- One-shot alarms with `repeat_mask == 0` are automatically disabled after they ring.
- The component persists and synchronizes automatic disables, including same-time
  one-shot peers, as one full-list update. Applications only handle the change
  notification; no extra list report or reconnect listener is needed. Snooze
  re-rings do not report the unchanged disabled flag again.
- Snooze reschedules the currently active alarm only, using the alarm's own snooze settings.
- If cloud is unavailable, local changes mark the runtime dirty instead of failing.
- When cloud reconnects, the full alarm list is reported again for reconciliation.
- Failed list serialization or submission retains the pending update for the next
  cloud connection, network recovery with a retained cloud session, or list-report
  attempt; this is not a periodic retry or a
  server-acknowledgment guarantee.
- Runtime transitions are reported with `alarm/stateChange` as `idle`, `ringing`,
  or `snoozing`. A pending state is retried when cloud connectivity returns.
- `qm_alarm_get_upcoming()` returns the next scheduled alarm.

## Cloud Protocol

When `iot_handle` is provided, `qm_alarm_start()` binds directly to `qm_iot`:

- registers the `alarm` downlink handler
- registers the `alarmReply` listener
- registers an IOT event handler for cloud connect and disconnect

The component currently supports these cloud submethods:

- `create`
- `modify`
- `delete`
- `enable`
- `close` for dismissing the currently active alarm
- `snooze` for snoozing the currently active alarm
- `stateChange` for device runtime state reports (`alarmId` and `state`)
- `update` for full-list reporting

Cloud `snooze` and `close` commands are acknowledged first; the resulting
`stateChange` report is published only after a successful ACK.

## Persistence Notes

The component stores alarms in KV using a minimal two-level layout:

- one compact `qmalarm.meta` record containing only store version and the
  used-slot bitmap
- one per-slot alarm payload keyed by slot index, including optional
  `extensions` JSON

The bitmap is only used to locate occupied slots quickly during startup and to
avoid scanning every possible slot on each boot. If an old `qmalarm.meta`
snapshot is found, the component clears the old KV data directly instead of
migrating it.

## Basic Usage

```c
#include "qm_alarm.h"

static void alarm_notify_cb(qm_alarm_t alarm,
                            qm_alarm_notify_t notify,
                            const qm_alarm_item_t *item,
                            void *user_ctx)
{
    (void)alarm;
    (void)notify;
    (void)item;
    (void)user_ctx;
}

void app_alarm_start(void *iot_handle)
{
    qm_alarm_config_t config = {0};
    qm_alarm_t alarm;

    config.iot_handle = iot_handle;
    config.max_alarms = 8;
    config.notify_cb = alarm_notify_cb;

    alarm = qm_alarm_create(&config);
    if (alarm == NULL) {
        return;
    }

    if (qm_alarm_start(alarm) != QM_EOK) {
        qm_alarm_destroy(alarm);
        return;
    }
}
```

## Migration Notes

- `qm_alarm` is the standalone target model.
- `qm_agent_alarm` still exists and has not yet been reduced to a thin adapter.
- This version is intentionally a copy-out migration, not yet a full internal redesign.

## Next Recommended Step

If the standalone component proves stable, the next step is to reduce
`qm_agent_alarm` into a thin adapter over `qm_alarm` instead of keeping two
separate runtime implementations.
