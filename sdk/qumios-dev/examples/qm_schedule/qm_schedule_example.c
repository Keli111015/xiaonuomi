#include "qm_schedule.h"
#include "qm_schedule_iot.h"

#include "qm_kernel.h"

#include <string.h>

static void schedule_notify(qm_schedule_t schedule,
                            const qm_schedule_notify_t *event,
                            void *user_ctx)
{
    QM_UNUSED_ARG(schedule);
    QM_UNUSED_ARG(event);
    QM_UNUSED_ARG(user_ctx);
}

static int send_reply(void *iot_handle, const char *payload, uint32_t len)
{
    QM_UNUSED_ARG(iot_handle);
    QM_UNUSED_ARG(payload);
    QM_UNUSED_ARG(len);
    return QM_EOK;
}

static int send_event(void *iot_handle, const char *payload, uint32_t len)
{
    QM_UNUSED_ARG(iot_handle);
    QM_UNUSED_ARG(payload);
    QM_UNUSED_ARG(len);
    return QM_EOK;
}

void qm_schedule_example_run(void)
{
    qm_schedule_t schedule;
    qm_schedule_iot_t iot;
    qm_schedule_config_t config = {0};
    qm_schedule_iot_config_t iot_config = {0};
    qm_schedule_binding_t binding = {
        .binding_version = "bind-v1",
        .lease_expires_at = 4102416000LL,
        .capability_version = "cap-v1",
    };
    qm_schedule_apply_ctx_t ctx = {
        .operation_id = "op-local-create",
        .binding_version = "bind-v1",
        .desired_revision = 1,
    };
    qm_schedule_reminder_t reminders[] = {
        {
            .id = "r0",
            .offset_min = 0,
            .requires_ack = true,
            .snooze_enabled = true,
            .snooze_interval_min = 5,
            .snooze_max_count = 3,
        },
    };
    qm_schedule_item_t item = {
        .id = "schedule-1",
        .revision = 1,
        .type = "other",
        .source = "device",
        .title = "Device local schedule",
        .start_at = 4102412400LL,
        .end_at = 0,
        .timezone = "UTC+08:00",
        .recurrence = QM_SCHEDULE_RECURRENCE_DAILY,
        .weekly_mask = 0,
        .recurrence_start_date = "2099-12-31",
        .recurrence_interval = 1,
        .recurrence_until = 0,
        .reminders = reminders,
        .reminder_count = 1,
        .enabled = true,
    };
    qm_schedule_action_t action = {
        .schedule_id = "schedule-1",
        .occurrence_at = 4102412400LL,
        .reminder_id = "r0",
        .action_at = 4102412410LL,
        .operation_id = "op-local-ack",
        .event_id = "evt-local-ack",
        .binding_version = "bind-v1",
    };
    char reply[512];
    const char *downlink =
        "{\"method\":\"schedule\",\"subMethod\":\"sync\",\"id\":1,"
        "\"params\":{\"schemaVersion\":1,\"operationId\":\"op-sync-1\","
        "\"bindingVersion\":\"bind-v1\",\"desiredRevision\":2,"
        "\"syncId\":\"sync-1\",\"syncScope\":\"group\","
        "\"groupId\":\"demo\",\"groupRevision\":1,\"replace\":true,"
        "\"chunkIndex\":0,\"chunkCount\":1,\"totalItems\":1,"
        "\"contentDigest\":\"sha256:710d82131ac7593954318f809ff0b7380dce36abb885fa0ce236dfd978a05809\","
        "\"schedules\":[{\"id\":\"schedule-1\",\"revision\":1,"
        "\"type\":\"other\",\"source\":\"device\","
        "\"title\":\"Device local schedule\",\"startAt\":4102412400,"
        "\"timezone\":\"UTC+08:00\",\"recurrence\":{\"frequency\":\"daily\","
        "\"startDate\":\"2099-12-31\",\"interval\":1},"
        "\"reminders\":[{\"id\":\"r0\",\"offsetMin\":0,"
        "\"action\":\"notify\",\"channels\":[],\"requiresAck\":true,"
        "\"snooze\":{\"enabled\":true,\"intervalMin\":5,\"maxCount\":3}}],"
        "\"enabled\":true}]}}";

    config.notify_cb = schedule_notify;
    schedule = qm_schedule_create(&config);
    if (!schedule) {
        return;
    }

    (void)qm_schedule_start(schedule);
    (void)qm_schedule_apply_binding(schedule, &binding);
    (void)qm_schedule_create_item(schedule, &item, &ctx);
    (void)qm_schedule_acknowledge(schedule, &action);

    iot_config.iot_handle = schedule;
    iot_config.schedule = schedule;
    iot_config.send_reply = send_reply;
    iot_config.send_event = send_event;
    if (qm_schedule_iot_register(&iot, &iot_config) == QM_EOK) {
        (void)qm_schedule_iot_handle_downlink(
            iot, downlink, (uint32_t)strlen(downlink), reply, sizeof(reply));
        (void)qm_schedule_iot_flush_outbox(iot);
        (void)qm_schedule_iot_unregister(iot);
    }

    qm_schedule_destroy(schedule);
}
