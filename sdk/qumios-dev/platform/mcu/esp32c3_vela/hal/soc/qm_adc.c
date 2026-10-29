#include "qm.h"
#include "qm_adc.h"

#include <applet.h>
#include <fcntl.h>
#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <syslog.h>

#define LOG_TAG "adc"
#define ADC_MAX_CHANNEL    (5)

static uint8_t g_fd_use[ADC_MAX_CHANNEL] = {0};

/**ADC最多检测端口为5个**/
int32_t qm_adc_init(qm_adc_dev_t *adc)
{
    int fd = 0;
    uint8_t fd_use = 0;
    char dev_name[20] = {0};
    adc_config_t config = {0};

    QM_RETURN_ON_FALSE(adc, -QM_EINVAL, LOG_TAG, "adc invail");
    
    config.id = 1;
    config.ranglevel = 3;
    config.pin = adc->pin.adc_pin;
    config.channel = adc->pin.adc_pin;

    if( adc->pin.adc_pin < 0 || 
        adc->pin.adc_pin > 4){
        return -QM_EINVAL;
    }
    
    fd_use = g_fd_use[adc->pin.adc_pin - 1];
    if(!fd_use){
        boardctl(APP_BOARDIOC_ADC, &config);/* adc config */
    }
    snprintf(dev_name, 20, "%s%d", "/dev/adc", config.pin);

    fd = open(dev_name, O_RDONLY);
    if(fd < 0){
        return -QM_EINVAL;
    }
  
    adc->priv = (void *)fd;
    g_fd_use[adc->pin.adc_pin - 1] = QM_TRUE;

    return QM_EOK;
}


int32_t qm_adc_value_get(qm_adc_dev_t *adc, void *output, uint32_t timeout)
{
    int fd = (int)adc->priv;
    struct adc_msg_s data = {0};

    QM_RETURN_ON_FALSE(adc, -QM_EINVAL, LOG_TAG, "adc invail");
    QM_RETURN_ON_FALSE(output, -QM_EINVAL, LOG_TAG, "output invail");
    QM_RETURN_ON_FALSE(adc->priv, -QM_EINVAL, LOG_TAG, "adc->priv invail");

    ioctl(fd, ANIOC_TRIGGER, 0);
    ioctl(fd, ANIOC_APPLET_ADCMSG, &data);

    *((int32_t *)output) = data.am_data;

    return QM_EOK;
}

int32_t qm_adc_deinit(qm_adc_dev_t *adc)
{
    int fd = (int)adc->priv;

    QM_RETURN_ON_FALSE(adc, -QM_EINVAL, LOG_TAG, "adc invail");
    QM_RETURN_ON_FALSE(adc->priv, -QM_EINVAL, LOG_TAG, "adc->priv invail");
    close(fd);
    adc->priv = NULL;

    return QM_EOK;
}

#if 0 //暂不支持
int32_t  qm_adc_start(qm_adc_dev_t *adc, void *data, uint32_t size)
{
    return -QM_EINVAL;
}

int32_t qm_adc_stop(qm_adc_dev_t *adc)
{
    return -QM_EINVAL;
}
#endif