#include "qm.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>

/* SH510: persist under /data so reboot/cwd 不影响 wifi_cfg 等 */
#define QM_KV_STORAGE_NAMESPACE "/data/qm_kv"

int qm_hal_kv_init(void)
{
    if (access("/data", F_OK) != 0) {
        mkdir("/data", 0755);
    }
    if (access(QM_KV_STORAGE_NAMESPACE, F_OK) != 0) {
        mkdir(QM_KV_STORAGE_NAMESPACE, 0755);
    }

    return QM_EOK;
}

int qm_hal_kv_get(const char *key, void *buffer, int *buffer_len)
{
    int kv_fd;
    char key_path[128];

    (void)qm_hal_kv_init();
    sprintf(key_path, "%s/%s", QM_KV_STORAGE_NAMESPACE, key);

    kv_fd = open(key_path, O_RDONLY);
    if (kv_fd < 0) {
        return QM_ERROR;
    }

    read(kv_fd, buffer, *buffer_len);

    close(kv_fd);

    return QM_EOK;
}

int qm_hal_kv_set(const char *key, const void *value, int len, int sync)
{
    int kv_fd;
    char key_path[128];

    (void)sync;
    (void)qm_hal_kv_init();
    sprintf(key_path, "%s/%s", QM_KV_STORAGE_NAMESPACE, key);

    if (access(key_path, F_OK)) {
        unlink(key_path);
    }

    unlink(key_path);
    kv_fd = open(key_path, O_RDWR | O_CREAT, 0644);
    if (kv_fd < 0) {
        return QM_ERROR;
    }

    write(kv_fd, value, len);

    close(kv_fd);

    return QM_EOK;
}

int qm_hal_kv_del(const char *key)
{
    char key_path[128];
    sprintf(key_path, "%s/%s", QM_KV_STORAGE_NAMESPACE, key);
    if (access(key_path, F_OK) != 0) {
        return QM_EINVAL;
    }

    unlink(key_path);

    return QM_EOK;
}

int qm_hal_kv_clean(void)
{
    if (access(QM_KV_STORAGE_NAMESPACE, F_OK) != 0) {
        return QM_EOK;
    }
    rmdir(QM_KV_STORAGE_NAMESPACE);
    return QM_EOK;
}
