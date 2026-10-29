#include "qm.h"
#include "qm_timer.h"

#include "bl616_tim.h"

#if CONFIG_QM_TIMER_SUPPORT

#define BL616_USER_TIMER_INDEX  (1)
#define BL616_USER_TIMER_NUM    (1)


static hal_timer_cb_t _timer_cb = NULL;
static void * _timer_arg = NULL;

static int _timer_isr(int irq, FAR void *context, FAR void *arg)
{
    qm_timer_dev_t *tim = (qm_timer_dev_t *)arg;
    if (NULL == arg)
    {
        return 0;
    }
    struct bl616_tim_dev_s *dev = (struct bl616_tim_dev_s*)tim->priv;
    if (dev == NULL)
    {
        //TODO: 错误上报
        return 0;
    }

    bool status = dev->ops->getint(dev);
    if (status)
    {
        dev->ops->clearint(dev);
        if (_timer_cb != NULL)
        {
            _timer_cb(_timer_arg);
        }
        if (tim->config.reload_mode == TIMER_RELOAD_MANU)
        {
            dev->ops->stop(dev);
        }
    }

    return 0;
}


int32_t qm_timer_dev_init(qm_timer_dev_t *tim)
{
    if ((tim == NULL)  || (tim->port != BL616_USER_TIMER_INDEX) || (tim->config.cb == NULL))
    {
        return -QM_EINVAL; // Invalid parameter
    }

    struct bl616_tim_dev_s *dev = bl616_tim_init(tim->port);
    if (dev == NULL) 
    {
        return -QM_ENOENT; // No such device
    }
    tim->priv = dev;

    //清除底层isuse标记位
    // bl616_tim_deinit(dev);
    // dev = bl616_tim_init(tim->port);

    //firt stop timer
    dev->ops->stop(dev);

    //set timeout
    dev->ops->setcmp(dev, tim->config.period);

    //set callback
    _timer_cb = tim->config.cb;
    _timer_arg = tim->config.arg;

    //disable int, to avoid unexcepted isr
    dev->ops->disableint(dev);
    dev->ops->clearint(dev);


    return 0; // Success
}

int32_t qm_timer_dev_start(qm_timer_dev_t *tim)
{
    //param check
    if ((tim == NULL)  || (tim->port != BL616_USER_TIMER_INDEX) || (tim->priv == NULL))
    {
        return -QM_EINVAL; // Invalid parameter
    }

    struct bl616_tim_dev_s *dev = (struct bl616_tim_dev_s*)tim->priv;

    //start timer
    dev->ops->start(dev);
    dev->ops->clearint(dev);
    dev->ops->attach(dev, _timer_isr, tim);

    return 0;
}

void qm_timer_dev_stop(qm_timer_dev_t *tim)
{
    if ((tim == NULL) || (tim->port != BL616_USER_TIMER_INDEX) || (tim->priv == NULL))
    {
        return -QM_EINIT;
    }

    struct bl616_tim_dev_s *dev = (struct bl616_tim_dev_s*)tim->priv;

    dev->ops->stop(dev);
}

int32_t qm_timer_dev_para_change(qm_timer_dev_t *tim, qm_timer_dev_config_t para)
{
    if ((tim == NULL) || (tim->port != BL616_USER_TIMER_INDEX) || (tim->priv == NULL))
    {
        return -QM_EINVAL;
    }
    if (para.cb == NULL)
    {
        return -QM_EINVAL;
    }

    _timer_cb = para.cb;
    _timer_arg = para.arg;

    struct bl616_tim_dev_s *dev = (struct bl616_tim_dev_s*)tim->priv;

    dev->ops->setcmp(dev, para.period);

    return 0;

}

int32_t qm_timer_dev_deinit(qm_timer_dev_t *tim)
{
    if ((tim == NULL) || (tim->port != BL616_USER_TIMER_INDEX) || (tim->priv == NULL))
    {
        return -QM_EINVAL;
    }

    struct bl616_tim_dev_s *dev = (struct bl616_tim_dev_s*)tim->priv;

    dev->ops->stop(dev);
    dev->ops->disableint(dev);

    bl616_tim_deinit(dev);

    return 0;
}



#endif