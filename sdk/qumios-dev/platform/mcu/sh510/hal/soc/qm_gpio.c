#include "qm_gpio.h"
#include "qm_log.h"
#include "qm_errno.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LOG_TAG "linux gpio"

#define LINUX_GPIO_MAX_PINS 32

typedef struct {
    int exported;
    int value_fd;
    qm_gpio_irq_handler_t handler;
    void *arg;
    qm_gpio_intr_type_t intr_type;
    pthread_t irq_thread;
    int irq_running;
    uint8_t last_level;
} linux_gpio_ctx_t;

static linux_gpio_ctx_t s_gpio_ctx[LINUX_GPIO_MAX_PINS];
static int s_gpio_ctx_inited = 0;

static void linux_gpio_ctx_init_once(void)
{
    int i;

    if (s_gpio_ctx_inited) {
        return;
    }

    for (i = 0; i < LINUX_GPIO_MAX_PINS; i++) {
        s_gpio_ctx[i].value_fd = -1;
    }
    s_gpio_ctx_inited = 1;
}

static linux_gpio_ctx_t *linux_gpio_ctx_get(uint8_t port)
{
    if (port >= LINUX_GPIO_MAX_PINS) {
        return NULL;
    }
    return &s_gpio_ctx[port];
}

static int linux_gpio_write_sysfs(uint8_t port, const char *attr, const char *value)
{
    char path[64];
    int fd;
    int len;

    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/%s", port, attr);
    fd = open(path, O_WRONLY);
    if (fd < 0) {
        return -1;
    }

    len = (int)strlen(value);
    if (write(fd, value, len) != len) {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

static int linux_gpio_export(uint8_t port)
{
    char buf[16];
    int fd;

    fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) {
        return -1;
    }

    snprintf(buf, sizeof(buf), "%u", port);
    if (write(fd, buf, strlen(buf)) < 0 && errno != EBUSY) {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

static int linux_gpio_unexport(uint8_t port)
{
    char buf[16];
    int fd;

    fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd < 0) {
        return -1;
    }

    snprintf(buf, sizeof(buf), "%u", port);
    write(fd, buf, strlen(buf));
    close(fd);
    return 0;
}

static int linux_gpio_open_value(uint8_t port)
{
    char path[64];

    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/value", port);
    return open(path, O_RDWR);
}

static int linux_gpio_level_from_fd(int fd)
{
    char ch;

    if (lseek(fd, 0, SEEK_SET) < 0) {
        return -1;
    }
    if (read(fd, &ch, 1) != 1) {
        return -1;
    }
    return (ch == '1') ? 1 : 0;
}

static int linux_gpio_set_level_fd(int fd, uint8_t level)
{
    const char *val = level ? "1" : "0";

    if (lseek(fd, 0, SEEK_SET) < 0) {
        return -1;
    }
    if (write(fd, val, 1) != 1) {
        return -1;
    }
    return 0;
}

static void *linux_gpio_irq_thread(void *arg)
{
    linux_gpio_ctx_t *ctx = (linux_gpio_ctx_t *)arg;
    int level;

    while (ctx->irq_running) {
        level = linux_gpio_level_from_fd(ctx->value_fd);
        if (level >= 0) {
            if ((ctx->intr_type == QM_GPIO_INTR_POSEDGE && ctx->last_level == 0 && level == 1) ||
                (ctx->intr_type == QM_GPIO_INTR_NEGEDGE && ctx->last_level == 1 && level == 0) ||
                (ctx->intr_type == QM_GPIO_INTR_ANYEDGE && ctx->last_level != (uint8_t)level) ||
                (ctx->intr_type == QM_GPIO_INTR_LOW_LEVEL && level == 0) ||
                (ctx->intr_type == QM_GPIO_INTR_HIGH_LEVEL && level == 1)) {
                if (ctx->handler) {
                    ctx->handler(ctx->arg);
                }
            }
            ctx->last_level = (uint8_t)level;
        }
        usleep(10000);
    }

    return NULL;
}

int32_t qm_gpio_init(qm_gpio_dev_t *gpio)
{
    linux_gpio_ctx_t *ctx;
    const char *direction;

    if (gpio == NULL) {
        return -QM_EINVAL;
    }

    linux_gpio_ctx_init_once();
    ctx = linux_gpio_ctx_get(gpio->port);
    if (ctx == NULL) {
        return -QM_EINVAL;
    }

    if (!ctx->exported) {
        if (linux_gpio_export(gpio->port) != 0) {
            QM_LOGW(LOG_TAG, "export gpio%u failed", gpio->port);
            return -QM_EIO;
        }
        ctx->exported = 1;
    }

    switch (gpio->config.mode) {
    case QM_GPIO_MODE_OUTPUT:
    case QM_GPIO_MODE_OUTPUT_OD:
        direction = "out";
        break;
    case QM_GPIO_MODE_INPUT:
    case QM_GPIO_MODE_INPUT_OUTPUT:
    case QM_GPIO_MODE_INPUT_OUTPUT_OD:
    default:
        direction = "in";
        break;
    }

    if (linux_gpio_write_sysfs(gpio->port, "direction", direction) != 0) {
        return -QM_EIO;
    }

    if (ctx->value_fd >= 0) {
        close(ctx->value_fd);
    }
    ctx->value_fd = linux_gpio_open_value(gpio->port);
    if (ctx->value_fd < 0) {
        return -QM_EIO;
    }

    ctx->last_level = (uint8_t)linux_gpio_level_from_fd(ctx->value_fd);
    gpio->priv = ctx;
    return QM_EOK;
}

int32_t qm_gpio_set_level(qm_gpio_dev_t *gpio, uint8_t level)
{
    linux_gpio_ctx_t *ctx;

    if (gpio == NULL || gpio->priv == NULL) {
        return -QM_EINVAL;
    }

    ctx = (linux_gpio_ctx_t *)gpio->priv;
    if (linux_gpio_set_level_fd(ctx->value_fd, level) != 0) {
        return -QM_EIO;
    }

    ctx->last_level = level;
    return QM_EOK;
}

uint8_t qm_gpio_get_level(qm_gpio_dev_t *gpio)
{
    linux_gpio_ctx_t *ctx;
    int level;

    if (gpio == NULL || gpio->priv == NULL) {
        return 0;
    }

    ctx = (linux_gpio_ctx_t *)gpio->priv;
    level = linux_gpio_level_from_fd(ctx->value_fd);
    if (level < 0) {
        return 0;
    }

    ctx->last_level = (uint8_t)level;
    return (uint8_t)level;
}

int32_t qm_gpio_enable_irq(qm_gpio_dev_t *gpio, qm_gpio_intr_type_t intr_type,
                           qm_gpio_irq_handler_t handler, void *arg)
{
    linux_gpio_ctx_t *ctx;

    if (gpio == NULL || gpio->priv == NULL || handler == NULL ||
        intr_type == QM_GPIO_INTR_DISABLE) {
        return -QM_EINVAL;
    }

    ctx = (linux_gpio_ctx_t *)gpio->priv;
    if (ctx->irq_running) {
        return QM_EOK;
    }

    ctx->handler = handler;
    ctx->arg = arg;
    ctx->intr_type = intr_type;
    ctx->irq_running = 1;
    if (pthread_create(&ctx->irq_thread, NULL, linux_gpio_irq_thread, ctx) != 0) {
        ctx->irq_running = 0;
        return -QM_EIO;
    }

    return QM_EOK;
}

int32_t qm_gpio_disable_irq(qm_gpio_dev_t *gpio)
{
    linux_gpio_ctx_t *ctx;

    if (gpio == NULL || gpio->priv == NULL) {
        return -QM_EINVAL;
    }

    ctx = (linux_gpio_ctx_t *)gpio->priv;
    if (!ctx->irq_running) {
        return QM_EOK;
    }

    ctx->irq_running = 0;
    pthread_join(ctx->irq_thread, NULL);
    ctx->handler = NULL;
    ctx->arg = NULL;
    return QM_EOK;
}

int32_t qm_gpio_clear_irq(qm_gpio_dev_t *gpio)
{
    (void)gpio;
    return QM_EOK;
}

int32_t qm_gpio_deinit(qm_gpio_dev_t *gpio)
{
    linux_gpio_ctx_t *ctx;

    if (gpio == NULL) {
        return -QM_EINVAL;
    }

    ctx = linux_gpio_ctx_get(gpio->port);
    if (ctx == NULL) {
        return -QM_EINVAL;
    }

    if (ctx->irq_running) {
        qm_gpio_disable_irq(gpio);
    }

    if (ctx->value_fd >= 0) {
        close(ctx->value_fd);
        ctx->value_fd = -1;
    }

    if (ctx->exported) {
        linux_gpio_unexport(gpio->port);
        ctx->exported = 0;
    }

    gpio->priv = NULL;
    return QM_EOK;
}
