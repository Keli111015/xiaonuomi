#ifndef QM_SCHEDULE_IOT_H
#define QM_SCHEDULE_IOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_schedule.h"

typedef struct qm_schedule_iot *qm_schedule_iot_t;

typedef struct {
    void *iot_handle;
    qm_schedule_t schedule;
    int (*send_reply)(void *iot_handle, const char *payload, uint32_t len);
    int (*send_event)(void *iot_handle, const char *payload, uint32_t len);
} qm_schedule_iot_config_t;

int qm_schedule_iot_register(qm_schedule_iot_t *out,
                             const qm_schedule_iot_config_t *config);
int qm_schedule_iot_unregister(qm_schedule_iot_t iot);
int qm_schedule_iot_flush_outbox(qm_schedule_iot_t iot);
int qm_schedule_iot_ack_event(qm_schedule_iot_t iot,
                              const char *event_id);
int qm_schedule_iot_handle_downlink(qm_schedule_iot_t iot,
                                    const char *payload,
                                    uint32_t payload_len,
                                    char *reply,
                                    uint32_t reply_len);

#ifdef __cplusplus
}
#endif

#endif /* QM_SCHEDULE_IOT_H */
