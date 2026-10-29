#include "qm_pwm.h"
#include "qm_types.h"
#include "qm_errno.h"
#include "qm_kernel.h"

#include <fcntl.h>
#include <syslog.h>
#include <unistd.h>
#include <applet.h>
#include <sys/ioctl.h>
#include <nuttx/timers/pwm.h>
#include "../../chip/esp32c3/pwm.h"

typedef struct {
    int start;
    int fd;   /**< priv data */
    struct pwm_info_s info;
} _conifg_t;

static int pwm_port_count = 0;



/*********************** 初始化pwm ********************/

/* WARNING for ESP32C3:
    * 1.channel must be in range of [0, 5]
    * 2.channel must be in strictly increasing order, from 0 to your max channel number
    * 3.channel id must be continuous, that is, channel id 0, 1, 3 will not work properly,
    *   if you want 3 channels, their id must be like 0,1,2
    */

int32_t qm_pwm_init(qm_pwm_dev_t *pwm)
{
    if(pwm->port < 0 || pwm->port > 4){
        return -QM_EINVAL;
    }
    int ret;
    _conifg_t   *pwm_fd    = NULL;
    pwm_config_t config    = {0};

    pwm->port = pwm_port_count;

    config.id = pwm->port;
    
    config.pins_count = 1;
    config.pins[0].pin = pwm->pin.pwm_pin;
    config.pins[0].channel = pwm->port;

    ret = boardctl(APP_BOARDIOC_PWM, &config);
    if (ret < QM_EOK){
        return -QM_ERROR;
    }
    
    pwm_fd = (_conifg_t *)qm_malloc(sizeof(_conifg_t));
    if(pwm_fd == NULL){
        return -QM_ENOMEM;
    }
    memset(pwm_fd, 0, sizeof(_conifg_t));

    pwm_fd->info.frequency = pwm->config.freq;  // 频率 (1 / frequency)
    pwm_fd->info.channels[0].channel  = pwm->port;
    pwm_fd->info.channels[0].duty     = pwm->config.pulse;

    pwm->priv = (void *)pwm_fd;
   
    pwm_port_count++;

    return ret;
}


int32_t qm_pwm_start(qm_pwm_dev_t *pwm)
{
    int ret ;
    char dev_name[20] = {0};
    _conifg_t *pwm_fd  = (_conifg_t *)pwm->priv;

    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }    

    if(pwm_fd->start){
        return QM_EOK;
    }

    snprintf(dev_name,20, "%s%d", "/dev/pwm", pwm->port);
    pwm_fd->fd = open(dev_name, O_RDONLY);
    if (pwm_fd->fd < QM_EOK)
    {
        return pwm_fd->fd;
    }

    ret = ioctl(pwm_fd->fd, PWMIOC_SETCHARACTERISTICS, &pwm_fd->info);
    if (ret < QM_EOK)
    {
        close(pwm_fd->fd);
        return ret;
    }
    ret = ioctl(pwm_fd->fd, PWMIOC_START, 0);
    if (ret < 0)
    {
        close(pwm_fd->fd);
        return ret;
    }

    pwm_fd->start = QM_TRUE;
    return QM_EOK;
}

int32_t qm_pwm_stop(qm_pwm_dev_t *pwm)
{
    int ret ;
    _conifg_t *pwm_fd  = (_conifg_t *)pwm->priv;

    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }

    if(!pwm_fd->start){
        return QM_EOK;
    }

    ret = ioctl(pwm_fd->fd, PWMIOC_STOP, 0);
    if (ret < 0){
       goto _exit;
    }

_exit:
    close(pwm_fd->fd);
    pwm_fd->start  = QM_FALSE;
    return QM_EOK;
}

int32_t qm_pwm_para_change(qm_pwm_dev_t *pwm, qm_pwm_config_t para)
{
    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }
    int ret ;
    _conifg_t *pwm_fd  = (_conifg_t *)pwm->priv;
    pwm_fd->info.frequency = para.freq;  // 频率 (1 / frequency)
    pwm_fd->info.channels[0].channel  = pwm->port;
    pwm_fd->info.channels[0].duty     = (uint16_t)para.pulse;
    
    qm_pwm_stop(pwm);
    qm_pwm_start(pwm);

    return QM_EOK;
}

int32_t qm_pwm_fade_start(qm_pwm_dev_t *pwm, uint32_t start_pulse, uint32_t end_pulse, uint32_t time)
{
    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }
    int ret ;
    _conifg_t *pwm_fd  = (_conifg_t *)pwm->priv;
    qm_pwm_stop(pwm);
    boatdctl_pwm_character_t pwm_character = {
        .dev_id = pwm->port,
        .channel = pwm->port,
    };  

    pwm_character.value = QM_TRUE;
    pwm_character.character_id = ESP32C3_PWM_FADEENABLE;
    ret = boardctl(APP_BOARDIOC_PWM_SET_CHARACTER, &pwm_character);
    if (ret < 0){
        return ret;
    }

    pwm_character.value = time;  //100ms
    pwm_character.character_id = ESP32C3_PWM_FADEDURATION;
    ret = boardctl(APP_BOARDIOC_PWM_SET_CHARACTER, &pwm_character);
    if (ret < 0){
        return ret;
    }  

    pwm_character.value = end_pulse; 
    pwm_character.character_id = ESP32C3_PWM_FADETARGET;
    ret = boardctl(APP_BOARDIOC_PWM_SET_CHARACTER, &pwm_character);
    if (ret < 0){
        return ret;
    }
    pwm_fd->info.channels[0].duty = start_pulse;
    ret = qm_pwm_start(pwm);
    if (ret != 0){
        goto __stop;
    }

    return ret;

__stop:

    pwm_fd->info.channels[0].duty = end_pulse;
    pwm_character.value = QM_FALSE;
    pwm_character.character_id = ESP32C3_PWM_FADEENABLE;
    ret = boardctl(APP_BOARDIOC_PWM_SET_CHARACTER, &pwm_character);
    if (ret < 0){
        return ret;
    }
    qm_pwm_start(pwm);
    return ret;
}

int32_t qm_pwm_deinit(qm_pwm_dev_t *pwm)
{
    int ret ;
    _conifg_t *pwm_fd  = (_conifg_t *)pwm->priv;

    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }

    qm_pwm_stop(pwm);
    if(pwm_fd->fd){
        close(pwm_fd->fd);
    }
    if(pwm_fd){
        qm_free(pwm_fd);
    }
    
    pwm->priv = NULL;
    pwm_port_count--;
    
    return QM_EOK;
}