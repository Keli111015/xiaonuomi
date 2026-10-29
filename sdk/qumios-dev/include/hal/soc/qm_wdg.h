#ifndef QM_WDG_H
#define QM_WDG_H

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup qm_wdg WDG
 * qm wdg API.
 *
 *  @{
 */

#include "qm_types.h"

/* Define wdt expired time */
typedef struct {
    uint32_t timeout; /**< Watchdag timeout */
} qm_wdg_config_t;

/* Define wdg dev handle */
typedef struct {
    uint8_t       port;   /**< wdg port */
    qm_wdg_config_t  config; /**< wdg config */
    void         *priv;   /**< priv data */
} qm_wdg_dev_t;

/**
 * This function will initialize the on board CPU hardware watch dog
 *
 * @param[in]  wdg  the watch dog device
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_wdg_init(qm_wdg_dev_t *wdg);

/**
 * Reload watchdog counter.
 *
 * @param[in]  wdg  the watch dog device
 */
void qm_wdg_reload(qm_wdg_dev_t *wdg);

/**
 * This function performs any platform-specific cleanup needed for hardware watch dog.
 *
 * @param[in]  wdg  the watch dog device
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_wdg_deinit(qm_wdg_dev_t *wdg);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* QM_WDG_H */
