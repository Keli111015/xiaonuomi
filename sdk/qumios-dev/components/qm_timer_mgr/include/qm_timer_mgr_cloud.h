#ifndef QM_TIMER_MGR_CLOUD_H
#define QM_TIMER_MGR_CLOUD_H

#include "qm_timer_mgr.h"

#ifdef __cplusplus
extern "C" {
#endif

int qm_timer_mgr_cloud_init(qm_timer_mgr_t timer,
                            const qm_timer_mgr_cloud_config_t *config);
int qm_timer_mgr_cloud_attach(qm_timer_mgr_t timer,
                              const qm_timer_mgr_cloud_config_t *config);
void qm_timer_mgr_cloud_quiesce(qm_timer_mgr_t timer);
void qm_timer_mgr_cloud_deinit(qm_timer_mgr_t timer);
void qm_timer_mgr_cloud_event(qm_timer_mgr_t timer,
                              const qm_timer_mgr_event_data_t *event);
void qm_timer_mgr_cloud_set_context(qm_timer_mgr_t timer, void *context);
void *qm_timer_mgr_cloud_get_context(qm_timer_mgr_t timer);
int qm_timer_mgr_cloud_set_config(qm_timer_mgr_t timer,
                                  const qm_timer_mgr_cloud_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* QM_TIMER_MGR_CLOUD_H */
