#include "qm.h"
#include "qm_wdg.h"

#include <applet.h>
#include <fcntl.h>
#include <nuttx/config.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <nuttx/timers/watchdog.h>

#define QM_WDG_DEBUG_LOG    (0)

#define QM_WDG_DEFAULT_DEV_PATH "/dev/watchdog0"


typedef struct 
{
    int dev_fd;
    char devpath[64];
}qm_wdg_hal_handle_t;

static qm_wdg_hal_handle_t g_wdg_handle = {
    .dev_fd = -1,
    .devpath = QM_WDG_DEFAULT_DEV_PATH,
};


/**
 * This function will initialize the on board CPU hardware watch dog
 *
 * @param[in]  wdg  the watch dog device
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_wdg_init(qm_wdg_dev_t *wdg)
{
    int ret = QM_EOK;
    struct watchdog_status_s status = {0};

    if(wdg->port != 0 || g_wdg_handle.dev_fd >= 0){
        return -QM_EINVAL;
    }

    /* Open the watchdog device for reading */

    g_wdg_handle.dev_fd = open(g_wdg_handle.devpath, O_RDONLY);
    if (g_wdg_handle.dev_fd < 0) {
    #if QM_WDG_DEBUG_LOG
        syslog(LOG_INFO, "Failed to open %s\n", g_wdg_handle.devpath);
    #endif
        return -1;
    }
#if QM_WDG_DEBUG_LOG
    syslog(LOG_INFO, "wdg_init dev_fd:%d \n", g_wdg_handle.dev_fd);
#endif
    ret = ioctl(g_wdg_handle.dev_fd, WDIOC_SETSOFT, true);
    if(ret < 0){
    #if QM_WDG_DEBUG_LOG
        syslog(LOG_INFO, "Failed to WDIOC_SETSOFT %s\n", g_wdg_handle.devpath);
    #endif
        goto __error;
    }

    /* Set the watchdog timeout */

    ret = ioctl(g_wdg_handle.dev_fd, WDIOC_SETTIMEOUT, wdg->config.timeout);
    if (ret < 0) {
    #if QM_WDG_DEBUG_LOG
        syslog(LOG_INFO, "Failed to WDIOC_SETTIMEOUT %s\n", g_wdg_handle.devpath);
    #endif
        goto __error;
    }

    ret = ioctl(g_wdg_handle.dev_fd, WDIOC_GETSTATUS, &status);
    if (ret < 0) {
    #if QM_WDG_DEBUG_LOG
        syslog(LOG_INFO, "Failed to WDIOC_GETSTATUS %s\n", g_wdg_handle.devpath);
    #endif
        goto __error;
    }
#if QM_WDG_DEBUG_LOG
    syslog(LOG_INFO, "WDIOC_GETSTATUS status.flags=%d\n", status.flags);
#endif

    /* Then start the watchdog timer. */

    ret = ioctl(g_wdg_handle.dev_fd, WDIOC_START, 0);
    if (ret < 0) {
    #if QM_WDG_DEBUG_LOG
        syslog(LOG_INFO, "Failed to WDIOC_START %s\n", g_wdg_handle.devpath);
    #endif
    }

    return QM_EOK;

__error:

    close(g_wdg_handle.dev_fd);
    g_wdg_handle.dev_fd = -1;

    return -QM_ERROR;
}

/**
 * Reload watchdog counter.
 *
 * @param[in]  wdg  the watch dog device
 */
void qm_wdg_reload(qm_wdg_dev_t *wdg)
{
    int ret = QM_EOK;
    struct watchdog_status_s status = {0};

    ret = ioctl(g_wdg_handle.dev_fd, WDIOC_KEEPALIVE, 0);
    if (ret < 0) {
    #if QM_WDG_DEBUG_LOG
        syslog(LOG_INFO, "Failed to WDIOC_SETTIMEOUT %s\n", g_wdg_handle.devpath);
    #endif
        
        return ;
    }

    ret = ioctl(g_wdg_handle.dev_fd, WDIOC_GETSTATUS, &status);
    if(ret < 0){
    #if QM_WDG_DEBUG_LOG
        syslog(LOG_INFO, "Failed to WDIOC_GETSTATUS %s\n", g_wdg_handle.devpath);
    #endif
    }
#if QM_WDG_DEBUG_LOG
    syslog(LOG_INFO, "test_case_wdog ioctl status.timeout=%d\n", status.timeout);
#endif
}

/**
 * @brief 暂不实现
 *  
 * This function performs any platform-specific cleanup needed for hardware watch dog.
 *
 * @param[in]  wdg  the watch dog device
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_wdg_deinit(qm_wdg_dev_t *wdg)
{
    return QM_EOK;
}
