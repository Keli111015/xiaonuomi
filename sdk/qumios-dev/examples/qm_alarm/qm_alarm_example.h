#ifndef QM_ALARM_EXAMPLE_H
#define QM_ALARM_EXAMPLE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the qm_alarm example with an existing qm_iot session.
 *
 * @param[in] iot_handle Started qm_iot handle used by qm_alarm MQTT methods.
 *
 * @return
 *      - QM_EOK: Success.
 *      - Negative error code: Failed.
 */
int qm_alarm_example_start(void *iot_handle);

/**
 * @brief Stop the qm_alarm example and destroy the alarm instance.
 */
void qm_alarm_example_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* QM_ALARM_EXAMPLE_H */
