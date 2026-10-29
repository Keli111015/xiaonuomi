#include "qm_pwm.h"
#include "qm_types.h"
#include "qm_errno.h"
#include "qm_kernel.h"

#include "pwmout_api.h"
#include "pwmout_ex_api.h"

int32_t qm_pwm_init(qm_pwm_dev_t *pwm)
{
    if(pwm->port < 0 || pwm->port > 7){
        return -QM_EINVAL;
    }

    pwmout_t *pwm_conf = (pwmout_t *)qm_malloc(sizeof(pwmout_t));
    if(pwm_conf == NULL){
        return -QM_ENOMEM;
    }
    memset(pwm_conf, 0, sizeof(pwmout_t));

    pwmout_init(pwm_conf, pwm->pin.pwm_pin);

    pwm_conf->period =  pwm->config.freq;
    pwm_conf->pulse =  pwm->config.pulse;

    pwmout_period(pwm_conf, pwm_conf->period);
    pwmout_pulsewidth(pwm_conf, pwm_conf->pulse);

    int ret = QM_EOK;

    pwm->priv = (void *)pwm_conf;
    qm_printf("qm_pwm_init success\r\n");

    return ret;
}


int32_t qm_pwm_start(qm_pwm_dev_t *pwm)
{
    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }

    pwmout_t *pwm_conf = (pwmout_t *)pwm->priv;
    pwmout_start(pwm_conf);
    qm_printf("qm_pwm_start success\r\n");
    return QM_EOK;
}

int32_t qm_pwm_stop(qm_pwm_dev_t *pwm)
{
    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }

    pwmout_t *pwm_conf = (pwmout_t *)pwm->priv;
    pwmout_stop(pwm_conf);
    qm_printf("qm_pwm_stop success\r\n");
    return QM_EOK;
}

int32_t qm_pwm_para_change(qm_pwm_dev_t *pwm, qm_pwm_config_t para)
{
    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }
    pwmout_t *pwm_conf = (pwmout_t *)pwm->priv;
    pwmout_period(pwm_conf, para.freq);
    pwmout_pulsewidth(pwm_conf, para.pulse);
    return QM_EOK;
}

//0 <= pulsewidth <= 1
int32_t qm_pwm_pulsewidth_percentage(qm_pwm_dev_t *pwm, float pulsewidth)
{
    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }
    pwmout_t *pwm_conf = (pwmout_t *)pwm->priv;
    pwmout_write(pwm_conf, pulsewidth);
    qm_printf("qm_pwm_pulsewidth_percentage success\r\n");
    return QM_EOK;
}

int32_t qm_pwm_fade_start(qm_pwm_dev_t *pwm, uint32_t start_pulse, uint32_t end_pulse, uint32_t time)
{
    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }
    int ret ;

    return -QM_EINVAL;
}

int32_t qm_pwm_deinit(qm_pwm_dev_t *pwm)
{
    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }
    int ret ;

    pwmout_t *pwm_conf = (pwmout_t *)pwm->priv;
    pwmout_free(pwm_conf);

    return QM_EOK;
}