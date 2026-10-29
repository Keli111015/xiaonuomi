#if 0 //未验证
#include "qm.h"
#include "qm_adc.h"

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <nuttx/analog/ioctl.h>
#include <nuttx/analog/adc.h>

#define CONFIG_EXAMPLES_ADC_GROUPSIZE 4 

int32_t qm_adc_init(qm_adc_dev_t *adc)
{
    int fd = 0;
    char dev_name[15] = {0};
    
    if(adc == NULL){
        return -QM_EINVAL;
    }

    snprintf(dev_name, sizeof(dev_name), "/dev/adc%u", adc->port);

    fd = open(dev_name, O_RDONLY);
    if (fd < 0){
        return fd;
    }

    adc->priv = (void *)fd;
    return QM_EOK;
}

int32_t qm_adc_value_get(qm_adc_dev_t *adc, void *output, uint32_t timeout)
{
    int fd = 0;
    int ret = QM_EOK;
    int32_t *value = NULL; 
    struct adc_msg_s data = {0};
    if(adc == NULL || output == NULL || adc->priv == NULL){
        return -QM_EINVAL;
    }

    fd = (int)adc->priv;
    value = (int32_t *)output;

    ret = ioctl(fd, ANIOC_TRIGGER, 0);
    if (ret < 0){
        return ret;
    }    

    ret = read(fd, &data, sizeof(struct adc_msg_s));
    if (ret < 0){
        return ret;
    }

    *value = data.am_data;

    return QM_EOK;
}

int32_t qm_adc_deinit(qm_adc_dev_t *adc)
{
    int fd = 0;

    if(adc == NULL || adc->priv == NULL){
        return -QM_EINVAL;
    }

    fd = (int)adc->priv;

    return close(fd);
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
#endif 