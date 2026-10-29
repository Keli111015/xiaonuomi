#include "qm_ota.h"
#include "qm_log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define LOG_TAG "linux ota"

#ifndef QM_LINUX_OTA_IMAGE_PATH
#define QM_LINUX_OTA_IMAGE_PATH "/data/qumi_ota.bin"
#endif

static int s_ota_fd = -1;

static void linux_ota_close(void)
{
    if (s_ota_fd >= 0) {
        close(s_ota_fd);
        s_ota_fd = -1;
    }
}

int qm_ota_start(void *arg)
{
    (void)arg;

    linux_ota_close();
    unlink(QM_LINUX_OTA_IMAGE_PATH);

    s_ota_fd = open(QM_LINUX_OTA_IMAGE_PATH, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (s_ota_fd < 0) {
        QM_LOGE(LOG_TAG, "open %s failed errno=%d", QM_LINUX_OTA_IMAGE_PATH, errno);
        return -QM_EIO;
    }

    QM_LOGI(LOG_TAG, "start writing %s", QM_LINUX_OTA_IMAGE_PATH);
    return QM_EOK;
}

int qm_ota_write(uint32_t *off_set, uint8_t *in_buf, uint32_t in_buf_len)
{
    ssize_t written;
    uint32_t offset;

    if (off_set == NULL || in_buf == NULL || in_buf_len == 0) {
        return -QM_EINVAL;
    }

    if (s_ota_fd < 0) {
        return -QM_EIO;
    }

    offset = *off_set;
    if (lseek(s_ota_fd, (off_t)offset, SEEK_SET) < 0) {
        QM_LOGE(LOG_TAG, "lseek failed offset=%u errno=%d", offset, errno);
        return -QM_EIO;
    }

    written = write(s_ota_fd, in_buf, in_buf_len);
    if (written < 0 || (uint32_t)written != in_buf_len) {
        QM_LOGE(LOG_TAG, "write failed offset=%u len=%u errno=%d", offset, in_buf_len, errno);
        return -QM_EIO;
    }

    *off_set = offset + in_buf_len;
    return QM_EOK;
}

int qm_ota_read(uint32_t *off_set, uint8_t *out_buf, uint32_t out_buf_len)
{
    ssize_t nread;
    uint32_t offset;

    if (off_set == NULL || out_buf == NULL || out_buf_len == 0) {
        return -QM_EINVAL;
    }

    if (s_ota_fd < 0) {
        return -QM_EIO;
    }

    offset = *off_set;
    if (lseek(s_ota_fd, (off_t)offset, SEEK_SET) < 0) {
        return -QM_EIO;
    }

    nread = read(s_ota_fd, out_buf, out_buf_len);
    if (nread < 0) {
        return -QM_EIO;
    }

    *off_set = offset + (uint32_t)nread;
    return (int)nread;
}

int qm_ota_abort(void *arg)
{
    (void)arg;

    linux_ota_close();
    unlink(QM_LINUX_OTA_IMAGE_PATH);
    QM_LOGI(LOG_TAG, "abort");
    return QM_EOK;
}

int qm_ota_end(void *arg)
{
    (void)arg;

    if (s_ota_fd < 0) {
        return -QM_EIO;
    }

    fsync(s_ota_fd);
    linux_ota_close();
    QM_LOGI(LOG_TAG, "image ready at %s, replace app manually then reboot", QM_LINUX_OTA_IMAGE_PATH);
    return QM_EOK;
}
