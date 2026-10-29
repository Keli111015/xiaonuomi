#include "qm_hal_kv.h"
#include "qm_errno.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define QM_MACOS_KV_DIR_ENV "QM_MACOS_KV_DIR"
#define QM_MACOS_KV_DIR_NAME "QUMI/kv"
#define QM_MACOS_KV_KEY_MAX 96
#define QM_MACOS_KV_PATH_MAX 1024

static int key_valid(const char *key)
{
    size_t i;
    if (key == NULL || key[0] == '\0' || strlen(key) >= QM_MACOS_KV_KEY_MAX) {
        return 0;
    }
    for (i = 0; key[i] != '\0'; i++) {
        if ((key[i] >= 'a' && key[i] <= 'z') ||
            (key[i] >= 'A' && key[i] <= 'Z') ||
            (key[i] >= '0' && key[i] <= '9') || key[i] == '_' || key[i] == '-') {
            continue;
        }
        return 0;
    }
    return 1;
}

static int kv_dir(char *path, size_t path_len)
{
    const char *override = getenv(QM_MACOS_KV_DIR_ENV);
    const char *home = getenv("HOME");
    int rc;

    if (override != NULL && override[0] != '\0') {
        rc = snprintf(path, path_len, "%s", override);
    } else if (home != NULL && home[0] != '\0') {
        rc = snprintf(path, path_len, "%s/Library/Application Support/%s", home,
                      QM_MACOS_KV_DIR_NAME);
    } else {
        rc = snprintf(path, path_len, "/tmp/%s", QM_MACOS_KV_DIR_NAME);
    }
    if (rc < 0 || (size_t)rc >= path_len) {
        return -QM_EINVAL;
    }
    {
        char partial[QM_MACOS_KV_PATH_MAX];
        size_t i;
        size_t length = strlen(path);
        if (length >= sizeof(partial)) {
            return -QM_EINVAL;
        }
        memcpy(partial, path, length + 1);
        for (i = 1; i < length; i++) {
            if (partial[i] != '/') {
                continue;
            }
            partial[i] = '\0';
            if (partial[0] != '\0' && mkdir(partial, 0700) != 0 && errno != EEXIST) {
                return -QM_EIO;
            }
            partial[i] = '/';
        }
        if (mkdir(partial, 0700) != 0 && errno != EEXIST) {
            return -QM_EIO;
        }
    }
    return QM_EOK;
}

static int kv_path(const char *key, char *path, size_t path_len)
{
    char dir[QM_MACOS_KV_PATH_MAX];
    int rc;

    if (!key_valid(key) || path == NULL) {
        return -QM_EINVAL;
    }
    rc = kv_dir(dir, sizeof(dir));
    if (rc != QM_EOK) {
        return rc;
    }
    rc = snprintf(path, path_len, "%s/%s.bin", dir, key);
    return rc < 0 || (size_t)rc >= path_len ? -QM_EINVAL : QM_EOK;
}

int qm_hal_kv_init(void)
{
    char dir[QM_MACOS_KV_PATH_MAX];
    return kv_dir(dir, sizeof(dir));
}

int qm_hal_kv_set(const char *key, const void *value, int len, int sync)
{
    char path[QM_MACOS_KV_PATH_MAX];
    char temp[QM_MACOS_KV_PATH_MAX];
    FILE *file;
    int rc;

    (void)sync;
    if (value == NULL || len < 0) {
        return -QM_EINVAL;
    }
    rc = kv_path(key, path, sizeof(path));
    if (rc != QM_EOK) {
        return rc;
    }
    rc = snprintf(temp, sizeof(temp), "%s.tmp.%ld", path, (long)getpid());
    if (rc < 0 || (size_t)rc >= sizeof(temp)) {
        return -QM_EINVAL;
    }
    file = fopen(temp, "wb");
    if (file == NULL) {
        return -QM_EIO;
    }
    if ((len > 0 && fwrite(value, 1, (size_t)len, file) != (size_t)len) ||
        fflush(file) != 0) {
        fclose(file);
        unlink(temp);
        return -QM_EIO;
    }
    if (fclose(file) != 0 || rename(temp, path) != 0) {
        unlink(temp);
        return -QM_EIO;
    }
    return QM_EOK;
}

int qm_hal_kv_get(const char *key, void *buffer, int *buffer_len)
{
    char path[QM_MACOS_KV_PATH_MAX];
    FILE *file;
    long length;
    size_t copied;
    int rc;

    if (buffer_len == NULL || *buffer_len < 0) {
        return -QM_EINVAL;
    }
    rc = kv_path(key, path, sizeof(path));
    if (rc != QM_EOK) {
        return rc;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        return errno == ENOENT ? -QM_ENOENT : -QM_EIO;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0 || length > INT_MAX) {
        fclose(file);
        return -QM_EIO;
    }
    if (buffer == NULL || *buffer_len < length) {
        *buffer_len = (int)length;
        fclose(file);
        return -QM_EFULL;
    }
    copied = length == 0 ? 0 : fread(buffer, 1, (size_t)length, file);
    fclose(file);
    if (copied != (size_t)length) {
        return -QM_EIO;
    }
    *buffer_len = (int)length;
    return QM_EOK;
}

int qm_hal_kv_del(const char *key)
{
    char path[QM_MACOS_KV_PATH_MAX];
    int rc = kv_path(key, path, sizeof(path));
    if (rc != QM_EOK) {
        return rc;
    }
    if (unlink(path) != 0 && errno != ENOENT) {
        return -QM_EIO;
    }
    return QM_EOK;
}

int qm_hal_kv_clean(void)
{
    /* The host store is intentionally key-scoped; callers can delete known keys. */
    return QM_EOK;
}
