#include "qm_pwm.h"
#include "qm_types.h"
#include "qm_errno.h"
#include "qm_kernel.h"

#include <sys/ioctl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>


#include <nuttx/timers/pwm.h>

int32_t qm_pwm_init(qm_pwm_dev_t *pwm)
{   
    int fd = 0;
    qm_err_t ret = QM_EOK;
    char dev_name[15] = {0};
    
    if (pwm == NULL)
    {
        return -QM_EINVAL;
    }

    snprintf(dev_name, sizeof(dev_name), "/dev/pwm%u", pwm->port);

    fd = open(dev_name, O_RDONLY);
    if (fd < 0){
        return fd;
    }

    pwm->priv = (void *)fd;
    return ret;
}


int32_t qm_pwm_start(qm_pwm_dev_t *pwm)
{
    int fd = 0;
    int ret = QM_EOK;
    struct pwm_info_s info = {0};

    if (pwm == NULL || pwm->priv == NULL)
    {
        return -QM_EINVAL;
    }
    
    fd = (int)pwm->priv;
    info.frequency = pwm->config.freq;
    info.duty = (uint16_t)pwm->config.pulse; //占空比 高电平持续时间 (duty/65536)%
    ret = ioctl(fd, PWMIOC_SETCHARACTERISTICS, &info);
    if (ret < QM_EOK)
    {
        return ret;
    }

    return ioctl(fd, PWMIOC_START, 0);
}

int32_t qm_pwm_stop(qm_pwm_dev_t *pwm)
{
    int fd = 0;
    struct pwm_info_s info = {0};

    if (pwm == NULL || pwm->priv == NULL)
    {
        return -QM_EINVAL;
    }

    fd = (int)pwm->priv;
    return ioctl(fd, PWMIOC_STOP, 0);
}

int32_t qm_pwm_para_change(qm_pwm_dev_t *pwm, qm_pwm_config_t para)
{
    int fd = 0;
    struct pwm_info_s info = {0};

    if (pwm == NULL || pwm->priv == NULL)
    {
        return -QM_EINVAL;
    }

    fd = (int)pwm->priv;
    
    info.duty = para.pulse;
    info.frequency = para.freq;

    pwm->config.freq = para.freq;
    pwm->config.pulse = para.pulse;

    return ioctl(fd, PWMIOC_SETCHARACTERISTICS, (unsigned long)&info);
}

#if 0 //暂不支持
int32_t qm_pwm_fade_start(qm_pwm_dev_t *pwm, uint32_t start_pulse, uint32_t end_pulse, uint32_t time)
{
    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }
    return -QM_EINVAL;
}
#endif

int32_t qm_pwm_deinit(qm_pwm_dev_t *pwm)
{
    int fd = 0;

    if (pwm == NULL || pwm->priv == NULL)
    {
        return -QM_EINVAL;
    }

    fd = (int)pwm->priv;

    return close(fd);
}
