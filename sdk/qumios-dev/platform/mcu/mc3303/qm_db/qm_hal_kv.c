#include "qm.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>

#define QM_KV_STORAGE_NAMESPACE "/start/qm_kv"

int qm_hal_kv_init(void)
{
    // 检查命名空间文件夹是否存在
    if (access(QM_KV_STORAGE_NAMESPACE, F_OK) != 0) {
        mkdir(QM_KV_STORAGE_NAMESPACE, 0755);
    }

    return QM_EOK;
}

int qm_hal_kv_get(const char *key, void *buffer, int *buffer_len)
{
    int kv_fd;
    char key_path[128];
    sprintf(key_path, "%s/%s", QM_KV_STORAGE_NAMESPACE, key);

    kv_fd = open(key_path, O_RDONLY);
    if (kv_fd < 0) {
        return QM_ERROR;
    }

    read(kv_fd, buffer, *buffer_len);

    close(kv_fd);

    return QM_EOK;
}

static void kv_ensure_parent_dir(const char *path)
{
    char tmp[128];
    char *p;
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
}

int qm_hal_kv_set(const char *key, const void *value, int len, int sync)
{
    int kv_fd;
    char key_path[128];

    sprintf(key_path, "%s/%s", QM_KV_STORAGE_NAMESPACE, key);

    kv_ensure_parent_dir(key_path);

    unlink(key_path);
    kv_fd = open(key_path, O_RDWR | O_CREAT, 0644);
    if (kv_fd < 0) {
        return QM_ERROR;
    }

    write(kv_fd, value, len);

    if (sync) {
        fsync(kv_fd);
    }

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