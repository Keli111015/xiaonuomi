#ifndef __NTPDATE_H__
#define __NTPDATE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_config.h"
#include "qm_types.h"

#if CONFIG_NTPDATE_SUPPORT

#ifndef CONFIG_NTPDATE_TASK_SIZE  
#define CONFIG_NTPDATE_TASK_SIZE                 4*1024    
#endif

#ifndef CONFIG_NTPDATE_TASK_PRIO  
#define CONFIG_NTPDATE_TASK_PRIO                 21    
#endif

#ifndef CONFIG_NTPDATE_PERIODIC_SYNC_SUPPORT  
#define CONFIG_NTPDATE_PERIODIC_SYNC_SUPPORT              1    
#endif

#if CONFIG_NTPDATE_PERIODIC_SYNC_SUPPORT

#ifndef CONFIG_NTPDATE_PERIODIC_SYNC_INTERVAL 
#define CONFIG_NTPDATE_PERIODIC_SYNC_INTERVAL           12*3600   
#endif

#endif

typedef enum {
    NTPDATE_RES_SUCCESS,
    NTPDATE_RES_FAIL,
}ntpdate_res_t;

typedef void (*ntpdate_complete_cb)(ntpdate_res_t res);

//start notify time synchronization from ntp server
int ntpdate_start(ntpdate_complete_cb cb);


#endif

#ifdef __cplusplus
}
#endif

#endif