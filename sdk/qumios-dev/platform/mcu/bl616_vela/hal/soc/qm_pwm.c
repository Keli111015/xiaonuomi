/****************************************************************************
 * arch/risc-v/src/bl602/bl602_pwm_lowerhalf.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include "qm.h"

#if 1

#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>
#include <nuttx/timers/pwm.h>

#include "qm_pwm.h"

//bl616 only support one pwm device with max 4 channels
//port: 0 -> channel 0
//port: 1 -> channel 1
//port: 2 -> channel 2
//port: 3 -> channel 3
#define QM_PWM_PORT_MAX 4


/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
//fd for /dev/pwm0
// static int g_pwm_fd = -1;
static struct pwm_lowerhalf_s *g_pwm_dev = NULL;
static struct pwm_info_s g_info;


//use pwm lowerhalf driver.
//使用上层的file接口(open, ioctl)有个弊端，多个task同时使用pwm时，需要open多个fd

/****************************************************************************
 * Private Types
 ****************************************************************************/


int32_t qm_pwm_init(qm_pwm_dev_t *pwm)
{
    if (pwm == NULL)
    {
        return -QM_EINVAL;
    }
    if (pwm->port >= QM_PWM_PORT_MAX)
    {
        return -QM_EINVAL;
    }
    //TODO: add mutex
    if (NULL == g_pwm_dev)
    {
        // Open the PWM device
        extern struct pwm_lowerhalf_s *bl616_pwm_initialize(void);
        g_pwm_dev = bl616_pwm_initialize();

        memset(&g_info, 0, sizeof(g_info));

        g_pwm_dev->ops->setup(g_pwm_dev);
    }
    //setup the pwm device
    g_info.frequency = pwm->config.freq;
    g_info.channels[pwm->port].duty = pwm->config.pulse;
    g_info.channels[pwm->port].channel = pwm->port; // channel 1 to 4

    return QM_EOK;
}


int32_t qm_pwm_start(qm_pwm_dev_t *pwm)
{
    if(pwm == NULL) 
    {
        return -QM_EINVAL;
    }
    if (g_pwm_dev == NULL)
    {
        return -QM_EINVAL;
    }
    //setup pwm device
    {
        //TODO: add mutex
        g_info.frequency = pwm->config.freq;
        g_info.channels[pwm->port].duty = pwm->config.pulse;
        g_info.channels[pwm->port].channel = pwm->port; // channel 1 to 4
    }

    //bl616 start always return OK
    g_pwm_dev->ops->start(g_pwm_dev, &g_info);

    return QM_EOK;
}



int32_t qm_pwm_para_change(qm_pwm_dev_t *pwm, qm_pwm_config_t para)
{
    if (pwm == NULL) 
    {
        return -QM_EINVAL;
    }

    if (g_pwm_dev == NULL)
    {
        return -QM_EINVAL;
    }

    //setup pwm device
    {
        //TODO: add mutex
        g_info.frequency = para.freq;
        g_info.channels[pwm->port].duty = para.pulse;
        g_info.channels[pwm->port].channel = pwm->port; // channel 1 to 4
    }

    g_pwm_dev->ops->start(g_pwm_dev, &g_info);

    return QM_EOK;
}

int32_t qm_pwm_stop(qm_pwm_dev_t *pwm)
{
    if (pwm == NULL) 
    {
        return -QM_EINVAL;
    }

    if (g_pwm_dev == NULL)
    {
        return -QM_EINVAL;
    }
    //stop pwm
    g_pwm_dev->ops->stop(g_pwm_dev);

    return QM_EOK;
}

int32_t qm_pwm_deinit(qm_pwm_dev_t *pwm)
{
    if (g_pwm_dev == NULL)
    {
        return -QM_EINVAL;
    }

    //close the pwm device
    g_pwm_dev->ops->shutdown(g_pwm_dev);
    g_pwm_dev = NULL;

    //reset the pwm info
    memset(&g_info, 0, sizeof(g_info));

	  return QM_EOK;
}
#endif
