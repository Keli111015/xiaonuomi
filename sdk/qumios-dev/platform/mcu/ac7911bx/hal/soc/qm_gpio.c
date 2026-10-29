#ifndef BOOL_DEFINE_CONFLICT
#define BOOL_DEFINE_CONFLICT
#endif

#include "qm.h"
#include "qm_gpio.h"
#include "qm_errno.h"

#include "asm/port_waked_up.h"
#include "device/gpio.h"

#include <string.h>

#define QM_AC7911BX_GPIO_IRQ_SLOTS 3

typedef struct {
    qm_gpio_irq_handler_t handler;
    void *handler_arg;
    void *irq_handle;
    PORT_EVENT_E irq_event;
    int8_t irq_slot;
} qm_ac7911bx_gpio_ctx_t;

static qm_ac7911bx_gpio_ctx_t *s_irq_contexts[QM_AC7911BX_GPIO_IRQ_SLOTS];

static int gpio_validate(const qm_gpio_dev_t *gpio)
{
    if (gpio == NULL || gpio->port > IO_PORT_USB_DMB) {
        return -QM_EINVAL;
    }
    if (gpio->config.mode > QM_GPIO_MODE_INPUT_OUTPUT ||
        gpio->config.pull_en > QM_GPIO_FLOATING) {
        return -QM_EINVAL;
    }
    return QM_EOK;
}

static int gpio_apply_pull(const qm_gpio_dev_t *gpio)
{
    unsigned int pull_up;
    unsigned int pull_down;

    switch (gpio->config.pull_en) {
    case QM_GPIO_PULLUP_ONLY:
        pull_up = 1;
        pull_down = 0;
        break;
    case QM_GPIO_PULLDOWN_ONLY:
        pull_up = 0;
        pull_down = 1;
        break;
    case QM_GPIO_PULLUP_PULLDOWN:
        pull_up = 1;
        pull_down = 1;
        break;
    case QM_GPIO_FLOATING:
        pull_up = 0;
        pull_down = 0;
        break;
    default:
        return -QM_EINVAL;
    }

    if (gpio_set_pull_up(gpio->port, pull_up) != 0 ||
        gpio_set_pull_down(gpio->port, pull_down) != 0) {
        return -QM_EIO;
    }
    return QM_EOK;
}

static int gpio_set_digital_input(const qm_gpio_dev_t *gpio, int enabled)
{
    if (gpio_set_die(gpio->port, enabled) != 0 ||
        gpio_set_dieh(gpio->port, enabled) != 0) {
        return -QM_EIO;
    }
    return QM_EOK;
}

static int gpio_configure(const qm_gpio_dev_t *gpio)
{
    int ret;

    gpio_latch_en(gpio->port, 0);
    ret = gpio_apply_pull(gpio);
    if (ret != QM_EOK) {
        return ret;
    }

    switch (gpio->config.mode) {
    case QM_GPIO_MODE_DISABLE:
        if (gpio_direction_input(gpio->port) != 0) {
            return -QM_EIO;
        }
        if (gpio_set_pull_up(gpio->port, 0) != 0 ||
            gpio_set_pull_down(gpio->port, 0) != 0) {
            return -QM_EIO;
        }
        return gpio_set_digital_input(gpio, 0);

    case QM_GPIO_MODE_INPUT:
        if (gpio_direction_input(gpio->port) != 0) {
            return -QM_EIO;
        }
        return gpio_set_digital_input(gpio, 1);

    case QM_GPIO_MODE_OUTPUT:
        if (gpio_direction_output(gpio->port, 0) != 0) {
            return -QM_EIO;
        }
        return gpio_set_digital_input(gpio, 0);

    case QM_GPIO_MODE_OUTPUT_OD:
        /* A released open-drain output is represented by input mode. */
        if (gpio_direction_input(gpio->port) != 0) {
            return -QM_EIO;
        }
        return gpio_set_digital_input(gpio, 0);

    case QM_GPIO_MODE_INPUT_OUTPUT_OD:
        if (gpio_direction_input(gpio->port) != 0) {
            return -QM_EIO;
        }
        return gpio_set_digital_input(gpio, 1);

    case QM_GPIO_MODE_INPUT_OUTPUT:
        if (gpio_direction_output(gpio->port, 0) != 0) {
            return -QM_EIO;
        }
        return gpio_set_digital_input(gpio, 1);

    default:
        return -QM_EINVAL;
    }
}

/* EVENT_IO_0/1 are shared remap channels used by touch and LCD TE. */
static int gpio_irq_event_get(uint8_t port, PORT_EVENT_E *event)
{
    if (event == NULL) {
        return -QM_EINVAL;
    }

    switch (port) {
    case IO_PORTA_00:
        *event = EVENT_PA0;
        break;
    case IO_PORTA_07:
        *event = EVENT_PA7;
        break;
    case IO_PORTA_08:
        *event = EVENT_PA8;
        break;
    case IO_PORTB_01:
        *event = EVENT_PB1;
        break;
    case IO_PORTB_06:
        *event = EVENT_PB6;
        break;
    case IO_PORTC_00:
        *event = EVENT_PC0;
        break;
    case IO_PORTC_01:
        *event = EVENT_PC1;
        break;
    case IO_PORTH_00:
        *event = EVENT_PH0;
        break;
    case IO_PORTH_07:
        *event = EVENT_PH7;
        break;
    default:
        return -QM_ENOSYS;
    }
    return QM_EOK;
}

static void gpio_irq_dispatch(unsigned int slot)
{
    qm_ac7911bx_gpio_ctx_t *ctx = s_irq_contexts[slot];
    qm_gpio_irq_handler_t handler;
    void *handler_arg;

    if (ctx == NULL || ctx->handler == NULL) {
        return;
    }

    /* The callback may disable and deinitialise this GPIO. */
    handler = ctx->handler;
    handler_arg = ctx->handler_arg;
    handler(handler_arg);
}

static void gpio_irq_slot_0(void)
{
    gpio_irq_dispatch(0);
}

static void gpio_irq_slot_1(void)
{
    gpio_irq_dispatch(1);
}

static void gpio_irq_slot_2(void)
{
    gpio_irq_dispatch(2);
}

static void (*const s_irq_trampolines[QM_AC7911BX_GPIO_IRQ_SLOTS])(void) = {
    gpio_irq_slot_0,
    gpio_irq_slot_1,
    gpio_irq_slot_2,
};

int32_t qm_gpio_init(qm_gpio_dev_t *gpio)
{
    qm_ac7911bx_gpio_ctx_t *ctx;
    int ret;

    ret = gpio_validate(gpio);
    if (ret != QM_EOK) {
        return ret;
    }
    if (gpio->priv != NULL) {
        return -QM_EBUSY;
    }

    ctx = qm_malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return -QM_ENOMEM;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->irq_slot = -1;

    ret = gpio_configure(gpio);
    if (ret != QM_EOK) {
        qm_free(ctx);
        return ret;
    }

    gpio->priv = ctx;
    return QM_EOK;
}

int32_t qm_gpio_set_level(qm_gpio_dev_t *gpio, uint8_t level)
{
    if (gpio == NULL || gpio->priv == NULL) {
        return -QM_EINVAL;
    }

    level = level != 0;
    switch (gpio->config.mode) {
    case QM_GPIO_MODE_OUTPUT:
    case QM_GPIO_MODE_INPUT_OUTPUT:
        return gpio_direction_output(gpio->port, level) == 0 ?
               QM_EOK : -QM_EIO;

    case QM_GPIO_MODE_OUTPUT_OD:
    case QM_GPIO_MODE_INPUT_OUTPUT_OD:
        if (level != 0) {
            return gpio_direction_input(gpio->port) == 0 ?
                   QM_EOK : -QM_EIO;
        }
        return gpio_direction_output(gpio->port, 0) == 0 ?
               QM_EOK : -QM_EIO;

    default:
        return -QM_EINVAL;
    }
}

uint8_t qm_gpio_get_level(qm_gpio_dev_t *gpio)
{
    if (gpio == NULL || gpio->priv == NULL) {
        return 0;
    }

    switch (gpio->config.mode) {
    case QM_GPIO_MODE_INPUT:
    case QM_GPIO_MODE_INPUT_OUTPUT_OD:
    case QM_GPIO_MODE_INPUT_OUTPUT:
        return gpio_read(gpio->port) != 0;
    default:
        return 0;
    }
}

int32_t qm_gpio_enable_irq(qm_gpio_dev_t *gpio,
                           qm_gpio_intr_type_t intr_type,
                           qm_gpio_irq_handler_t handler, void *arg)
{
    qm_ac7911bx_gpio_ctx_t *ctx;
    PORT_EDGE_E edge;
    PORT_EVENT_E event;
    unsigned int slot;
    void *irq_handle;
    int ret;

    if (gpio == NULL || gpio->priv == NULL || handler == NULL) {
        return -QM_EINVAL;
    }
    if (gpio->config.mode != QM_GPIO_MODE_INPUT &&
        gpio->config.mode != QM_GPIO_MODE_INPUT_OUTPUT &&
        gpio->config.mode != QM_GPIO_MODE_INPUT_OUTPUT_OD) {
        return -QM_EINVAL;
    }
    ret = gpio_irq_event_get(gpio->port, &event);
    if (ret != QM_EOK) {
        return ret;
    }

    switch (intr_type) {
    case QM_GPIO_INTR_POSEDGE:
        edge = EDGE_POSITIVE;
        break;
    case QM_GPIO_INTR_NEGEDGE:
        edge = EDGE_NEGATIVE;
        break;
    case QM_GPIO_INTR_ANYEDGE:
    case QM_GPIO_INTR_LOW_LEVEL:
    case QM_GPIO_INTR_HIGH_LEVEL:
        return -QM_ENOSYS;
    default:
        return -QM_EINVAL;
    }

    ctx = (qm_ac7911bx_gpio_ctx_t *)gpio->priv;
    if (ctx->irq_handle != NULL) {
        return -QM_EBUSY;
    }

    for (slot = 0; slot < QM_AC7911BX_GPIO_IRQ_SLOTS; ++slot) {
        if (s_irq_contexts[slot] != NULL &&
            s_irq_contexts[slot]->irq_event == event) {
            return -QM_EBUSY;
        }
    }
    for (slot = 0; slot < QM_AC7911BX_GPIO_IRQ_SLOTS; ++slot) {
        if (s_irq_contexts[slot] == NULL) {
            break;
        }
    }
    if (slot == QM_AC7911BX_GPIO_IRQ_SLOTS) {
        return -QM_EBUSY;
    }

    ctx->handler = handler;
    ctx->handler_arg = arg;
    ctx->irq_event = event;
    ctx->irq_slot = (int8_t)slot;
    s_irq_contexts[slot] = ctx;

    irq_handle = port_wakeup_reg(event, gpio->port, edge,
                                 s_irq_trampolines[slot]);
    if (irq_handle == NULL) {
        s_irq_contexts[slot] = NULL;
        ctx->handler = NULL;
        ctx->handler_arg = NULL;
        ctx->irq_event = EVENT_IO_0;
        ctx->irq_slot = -1;
        return -QM_EBUSY;
    }
    ctx->irq_handle = irq_handle;

    /* port_wakeup_reg() applies an edge-dependent pull; restore QUMI config. */
    ret = gpio_apply_pull(gpio);
    if (ret != QM_EOK) {
        qm_gpio_disable_irq(gpio);
        return ret;
    }
    return QM_EOK;
}

int32_t qm_gpio_disable_irq(qm_gpio_dev_t *gpio)
{
    qm_ac7911bx_gpio_ctx_t *ctx;
    void *irq_handle;
    int8_t slot;

    if (gpio == NULL || gpio->priv == NULL) {
        return -QM_EINVAL;
    }

    ctx = (qm_ac7911bx_gpio_ctx_t *)gpio->priv;
    if (ctx->irq_handle == NULL) {
        return QM_EOK;
    }

    irq_handle = ctx->irq_handle;
    slot = ctx->irq_slot;
    if (slot >= 0 && slot < QM_AC7911BX_GPIO_IRQ_SLOTS &&
        s_irq_contexts[(unsigned int)slot] == ctx) {
        s_irq_contexts[(unsigned int)slot] = NULL;
    }
    ctx->irq_handle = NULL;
    ctx->irq_slot = -1;
    ctx->handler = NULL;
    ctx->handler_arg = NULL;
    ctx->irq_event = EVENT_IO_0;
    port_wakeup_unreg(irq_handle);
    return QM_EOK;
}

int32_t qm_gpio_clear_irq(qm_gpio_dev_t *gpio)
{
    qm_ac7911bx_gpio_ctx_t *ctx;

    if (gpio == NULL || gpio->priv == NULL) {
        return -QM_EINVAL;
    }
    ctx = (qm_ac7911bx_gpio_ctx_t *)gpio->priv;
    if (ctx->irq_handle == NULL) {
        return -QM_EINVAL;
    }

    /* The SDK port-wakeup ISR clears the pending bit after each callback. */
    return QM_EOK;
}

int32_t qm_gpio_deinit(qm_gpio_dev_t *gpio)
{
    qm_ac7911bx_gpio_ctx_t *ctx;
    int ret;
    int cleanup_ret = QM_EOK;

    if (gpio == NULL || gpio->priv == NULL) {
        return -QM_EINVAL;
    }

    ctx = (qm_ac7911bx_gpio_ctx_t *)gpio->priv;
    ret = qm_gpio_disable_irq(gpio);
    if (ret != QM_EOK) {
        return ret;
    }

    gpio_latch_en(gpio->port, 0);
    if (gpio_direction_input(gpio->port) != 0 ||
        gpio_set_pull_up(gpio->port, 0) != 0 ||
        gpio_set_pull_down(gpio->port, 0) != 0 ||
        gpio_set_die(gpio->port, 0) != 0 ||
        gpio_set_dieh(gpio->port, 0) != 0) {
        cleanup_ret = -QM_EIO;
    }

    gpio->priv = NULL;
    qm_free(ctx);
    return cleanup_ret;
}
