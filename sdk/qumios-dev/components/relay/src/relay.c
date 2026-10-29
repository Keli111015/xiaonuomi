#include "relay.h"
#include "qm_kernel.h"

typedef struct {
    qm_gpio_dev_t gpio_dev;
    relay_status_t state;
    relay_close_level_t close_level;
} relay_dev_t;

relay_handle_t relay_create(relay_io_t *relay_io, relay_close_level_t close_level)
{
    if(relay_io == NULL){
        return NULL;
    }
    relay_dev_t *relay_dev = (relay_dev_t*)qm_malloc(sizeof(relay_dev_t));
    if(relay_dev == NULL){
        return NULL;
    }

    relay_dev->gpio_dev.port = relay_io->port;
    relay_dev->gpio_dev.config.mode = QM_GPIO_MODE_OUTPUT;
    relay_dev->gpio_dev.config.pull_en = QM_GPIO_FLOATING;

    qm_gpio_init(&relay_dev->gpio_dev);

    relay_dev->state = RELAY_STATUS_CLOSE;
    relay_dev->close_level = close_level;

    return (relay_handle_t)relay_dev;
}

qm_err_t relay_state_write(relay_handle_t relay_handle, relay_status_t state)
{
    relay_dev_t *relay_dev = (relay_dev_t*)relay_handle;

    if(relay_handle == NULL){
        return -QM_EINVAL;
    }
    qm_gpio_set_level(&relay_dev->gpio_dev, (0x01 & state) ^ relay_dev->close_level);
    return QM_EOK;
}

relay_status_t relay_state_read(relay_handle_t relay_handle)
{
    relay_dev_t *relay_dev = (relay_dev_t*)relay_handle;

    if(relay_handle == NULL){
        return RELAY_STATUS_CLOSE;
    }
    return relay_dev->state;
}

qm_err_t relay_delete(relay_handle_t relay_handle)
{
    relay_dev_t *relay_dev = (relay_dev_t*)relay_handle;

    if(relay_handle == NULL){
        return -QM_EINVAL;
    }
    qm_gpio_deinit(&relay_dev->gpio_dev);
    qm_free(relay_dev);
    return QM_EOK;
}
