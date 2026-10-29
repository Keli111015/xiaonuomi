#ifndef QM_FS_H
#define QM_FS_H

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup qm_fs Generic file I/O
 *  Platform-neutral file I/O abstraction. Components such as qm_audio depend
 *  only on this interface instead of a specific file-system back end.
 *
 *  @{
 */

#include "qm_types.h"

/** @brief Opaque file handle; each platform binds it to its own stream type. */
typedef void *qm_fs_file_t;

/** @brief Seek base used by qm_fs_seek. */
typedef enum {
    QM_FS_SEEK_SET = 0, /**< Offset from the beginning of the file. */
    QM_FS_SEEK_CUR,     /**< Offset from the current position. */
    QM_FS_SEEK_END,     /**< Offset from the end of the file. */
} qm_fs_whence_t;

/** @brief Open-mode flags combined with qm_fs_open. */
enum {
    QM_FS_OPEN_READ   = (1u << 0), /**< Open for reading. */
    QM_FS_OPEN_WRITE  = (1u << 1), /**< Open for writing. */
    QM_FS_OPEN_CREATE = (1u << 2), /**< Create if the path does not exist. */
    QM_FS_OPEN_TRUNC  = (1u << 3), /**< Truncate to zero length on open. */
};

/**
 * @brief Open a file at path with the given flag combination.
 * @param[in]  path      NUL-terminated path, borrowed for this call only.
 * @param[in]  flags     Combination of QM_FS_OPEN_*; READ or WRITE required.
 * @param[out] out_file  Required handle output, set to NULL on failure.
 * @return QM_EOK on success; -QM_EINVAL for NULL/bad flags; -QM_ENOENT when
 *         a read-only open cannot find the file; -QM_ENOSYS for an
 *         unsupported mode combination; -QM_EIO on other failure.
 */
int32_t qm_fs_open(const char *path, uint32_t flags, qm_fs_file_t *out_file);

/**
 * @brief Read up to len bytes into caller-owned storage.
 * @param[in]  file      Handle from qm_fs_open.
 * @param[out] buf       Borrowed writable destination, not retained.
 * @param[in]  len       Capacity in bytes; zero is invalid.
 * @param[out] read_size Required count; zero on entry and valid on return.
 *                       Zero with QM_EOK means end-of-file.
 * @return QM_EOK on success or EOF; -QM_EINVAL for invalid arguments;
 *         -QM_EIO on a read error.
 */
int32_t qm_fs_read(qm_fs_file_t file, void *buf, uint32_t len, uint32_t *read_size);

/**
 * @brief Write up to len bytes from caller-owned storage.
 * @param[in]  file    Writable handle from qm_fs_open.
 * @param[in]  buf     Borrowed readable source, not retained.
 * @param[in]  len     Byte count; zero is invalid.
 * @param[out] written Required count; valid on return, may be less than len.
 * @return QM_EOK on success; -QM_EINVAL for invalid arguments; -QM_EIO on a
 *         write error.
 */
int32_t qm_fs_write(qm_fs_file_t file, const void *buf, uint32_t len, uint32_t *written);

/**
 * @brief Reposition the file pointer.
 * @param[in] file   Handle from qm_fs_open.
 * @param[in] offset Signed offset relative to whence.
 * @param[in] whence Seek base; negative offsets are platform-dependent.
 * @return QM_EOK on success; -QM_EINVAL for invalid arguments; -QM_EIO on
 *         failure.
 */
int32_t qm_fs_seek(qm_fs_file_t file, int32_t offset, qm_fs_whence_t whence);

/**
 * @brief Read the current file position.
 * @param[in]  file Handle from qm_fs_open.
 * @param[out] pos  Required output for the position.
 * @return QM_EOK on success; -QM_EINVAL for invalid arguments; -QM_EIO on
 *         failure.
 */
int32_t qm_fs_tell(qm_fs_file_t file, uint32_t *pos);

/**
 * @brief Read the file size without disturbing the current position.
 * @param[in]  file Handle from qm_fs_open.
 * @param[out] size Required output for the size in bytes.
 * @return QM_EOK on success; -QM_EINVAL for invalid arguments; -QM_EIO on
 *         failure.
 * @note Unlike the native flen() helper, this must NOT move the file pointer.
 */
int32_t qm_fs_size(qm_fs_file_t file, uint32_t *size);

/**
 * @brief Release a file handle.
 * @param[in] file Handle from qm_fs_open; NULL is a no-op.
 * @return QM_EOK on success; -QM_EIO if the close fails.
 */
int32_t qm_fs_close(qm_fs_file_t file);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* QM_FS_H */