#define BOOL_DEFINE_CONFLICT
#define CONFIG_QM_WITHOUT_STDIO_H_SUPPORT 1

#include "qm_fs.h"
#include "qm_errno.h"

#include "fs/fs.h"

int32_t qm_fs_open(const char *path, uint32_t flags, qm_fs_file_t *out_file)
{
    const char *mode;
    FILE *f;

    if (path == NULL || out_file == NULL) {
        return -QM_EINVAL;
    }
    *out_file = NULL;

    if ((flags & QM_FS_OPEN_READ) && (flags & QM_FS_OPEN_WRITE)) {
        mode = "w+";
    } else if (flags & QM_FS_OPEN_WRITE) {
        mode = "w";
    } else if (flags & QM_FS_OPEN_READ) {
        mode = "r";
    } else {
        return -QM_ENOSYS;
    }

    f = fopen(path, mode);
    if (f == NULL) {
        return ((flags & QM_FS_OPEN_READ) && !(flags & QM_FS_OPEN_WRITE))
                   ? -QM_ENOENT : -QM_EIO;
    }
    *out_file = (qm_fs_file_t)f;
    return QM_EOK;
}

int32_t qm_fs_read(qm_fs_file_t file, void *buf, uint32_t len, uint32_t *read_size)
{
    FILE *f = (FILE *)file;
    int count;

    if (read_size != NULL) {
        *read_size = 0;
    }
    if (f == NULL || buf == NULL || read_size == NULL || len == 0U) {
        return -QM_EINVAL;
    }
    count = fread(buf, 1U, len, f);
    if (count < 0) {
        return -QM_EIO;
    }
    *read_size = (uint32_t)count;
    return QM_EOK;
}

int32_t qm_fs_write(qm_fs_file_t file, const void *buf, uint32_t len, uint32_t *written)
{
    FILE *f = (FILE *)file;
    int count;

    if (written != NULL) {
        *written = 0;
    }
    if (f == NULL || buf == NULL || written == NULL || len == 0U) {
        return -QM_EINVAL;
    }
    count = fwrite((void *)buf, 1U, len, f);
    if (count < 0) {
        return -QM_EIO;
    }
    *written = (uint32_t)count;
    return QM_EOK;
}

int32_t qm_fs_seek(qm_fs_file_t file, int32_t offset, qm_fs_whence_t whence)
{
    FILE *f = (FILE *)file;
    int orig;

    if (f == NULL) {
        return -QM_EINVAL;
    }
    /* Jieli fseek takes an unsigned offset; negative seeks are unsupported. */
    if (offset < 0) {
        return -QM_EINVAL;
    }
    switch (whence) {
    case QM_FS_SEEK_SET:
        orig = SEEK_SET;
        break;
    case QM_FS_SEEK_CUR:
        orig = SEEK_CUR;
        break;
    case QM_FS_SEEK_END:
        orig = SEEK_END;
        break;
    default:
        return -QM_EINVAL;
    }
    if (fseek(f, (u32)offset, orig) != 0) {
        return -QM_EIO;
    }
    return QM_EOK;
}

int32_t qm_fs_tell(qm_fs_file_t file, uint32_t *pos)
{
    FILE *f = (FILE *)file;
    u32 cur;

    if (f == NULL || pos == NULL) {
        return -QM_EINVAL;
    }
    cur = ftell(f);
    *pos = (uint32_t)cur;
    return QM_EOK;
}

int32_t qm_fs_size(qm_fs_file_t file, uint32_t *size)
{
    FILE *f = (FILE *)file;
    u32 cur, end;

    if (f == NULL || size == NULL) {
        return -QM_EINVAL;
    }
    cur = ftell(f);
    if (fseek(f, 0U, SEEK_END) != 0) {
        return -QM_EIO;
    }
    end = ftell(f);
    if (fseek(f, cur, SEEK_SET) != 0) {
        return -QM_EIO;
    }
    *size = (uint32_t)end;
    return QM_EOK;
}

int32_t qm_fs_close(qm_fs_file_t file)
{
    FILE *f = (FILE *)file;

    if (f == NULL) {
        return QM_EOK;
    }
    if (fclose(f) != 0) {
        return -QM_EIO;
    }
    return QM_EOK;
}