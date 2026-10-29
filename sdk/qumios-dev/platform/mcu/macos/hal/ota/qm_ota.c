#include "qm_ota.h"
#include "qm_errno.h"

#include <stdio.h>
#include <stdlib.h>

#define QM_MACOS_OTA_PATH_ENV "QM_MACOS_OTA_PATH"
#define QM_MACOS_OTA_DEFAULT "/tmp/qumi-macos-ota.bin"

static FILE *g_ota_file;

static const char *ota_path(void)
{
    const char *path = getenv(QM_MACOS_OTA_PATH_ENV);
    return path != NULL && path[0] != '\0' ? path : QM_MACOS_OTA_DEFAULT;
}

int qm_ota_start(void *arg)
{
    (void)arg;
    if (g_ota_file != NULL) {
        return -QM_EBUSY;
    }
    g_ota_file = fopen(ota_path(), "w+b");
    return g_ota_file == NULL ? -QM_EIO : QM_EOK;
}

int qm_ota_write(uint32_t *offset, uint8_t *in_buf, uint32_t in_buf_len)
{
    if (g_ota_file == NULL || offset == NULL || (in_buf == NULL && in_buf_len != 0)) {
        return -QM_EINVAL;
    }
    if (fseek(g_ota_file, (long)*offset, SEEK_SET) != 0 ||
        (in_buf_len != 0 && fwrite(in_buf, 1, in_buf_len, g_ota_file) != in_buf_len)) {
        return -QM_EIO;
    }
    *offset += in_buf_len;
    return QM_EOK;
}

int qm_ota_read(uint32_t *offset, uint8_t *out_buf, uint32_t out_buf_len)
{
    if (g_ota_file == NULL || offset == NULL || (out_buf == NULL && out_buf_len != 0)) {
        return -QM_EINVAL;
    }
    if (fseek(g_ota_file, (long)*offset, SEEK_SET) != 0 ||
        (out_buf_len != 0 && fread(out_buf, 1, out_buf_len, g_ota_file) != out_buf_len)) {
        return -QM_EIO;
    }
    *offset += out_buf_len;
    return QM_EOK;
}

int qm_ota_abort(void *arg)
{
    (void)arg;
    if (g_ota_file != NULL) {
        fclose(g_ota_file);
        g_ota_file = NULL;
    }
    return QM_EOK;
}

int qm_ota_end(void *arg)
{
    int rc = QM_EOK;
    (void)arg;
    if (g_ota_file == NULL) {
        return -QM_EINVAL;
    }
    if (fflush(g_ota_file) != 0 || fclose(g_ota_file) != 0) {
        rc = -QM_EIO;
    }
    g_ota_file = NULL;
    return rc;
}

