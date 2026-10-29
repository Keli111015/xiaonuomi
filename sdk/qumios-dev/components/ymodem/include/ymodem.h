#ifndef YMODEM_H
#define YMODEM_H

#include "qm_errno.h"
#include "qm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup ymodem YMODEM receiver
 * @brief Synchronous, transport-independent YMODEM receiver for firmware OTA.
 * @brief 同步、与传输介质无关的固件 OTA YMODEM 接收器。
 * @{
 */

/** @brief Opaque YMODEM handle. / 不透明的 YMODEM 句柄。 */
typedef struct ymodem *ymodem_handle_t;

/**
 * @brief Write bytes to an ordered byte-stream transport.
 * @brief 向有序字节流传输层写入数据。
 *
 * The callback may perform a partial write. The component repeats the call
 * until all bytes are written or the deadline expires. The buffer is borrowed
 * for the duration of the callback and must not be retained.
 *
 * 回调允许部分写入。组件会持续调用，直到全部写完或 deadline 到期。
 * 缓冲区只在回调期间借用，不得保存。
 *
 * Return a positive number of bytes written, zero for no progress, or a
 * negative error. `-QM_ETIMEOUT` and zero are treated as timeout; any other
 * negative value is treated as a transport error.
 *
 * 返回实际写入的正数字节数；返回 0 表示没有进展；返回负值表示错误。
 * `-QM_ETIMEOUT` 和 0 按超时处理，其他负值按传输错误处理。
 */
typedef int32_t (*ymodem_write_fn)(void *user, const uint8_t *data,
                                   uint32_t length, uint32_t timeout_ms);

/**
 * @brief Read bytes from an ordered byte-stream transport.
 * @brief 从有序字节流传输层读取数据。
 *
 * Partial reads are supported. The component carries one deadline across
 * repeated reads for a frame. data is borrowed for the callback and must not
 * be retained for asynchronous use.
 *
 * 支持部分读取。组件在读取一个数据帧时会在多次回调之间保持同一个
 * deadline。data 仅在回调期间借用，不得保存用于异步操作。
 *
 * Return a positive number of bytes read, zero for no progress, or a negative
 * error. `-QM_ETIMEOUT` and zero are treated as timeout; any other negative
 * value is treated as a transport error.
 * After a frame start byte has been received, a timed-out partial frame may
 * be drained with additional reads before the receiver sends NAK.
 *
 * 返回实际读取的正数字节数；返回 0 表示没有进展；返回负值表示错误。
 * `-QM_ETIMEOUT` 和 0 按超时处理，其他负值按传输错误处理。
 * 接收到帧起始字节后，如果帧只收到一部分就超时，接收器可能继续读取并
 * 丢弃该帧的剩余字节，然后才发送 NAK。
 */
typedef int32_t (*ymodem_read_fn)(void *user, uint8_t *data,
                                  uint32_t length, uint32_t timeout_ms);

/**
 * @brief Metadata received in block 0.
 * @brief 从 block 0 接收的文件元数据。
 *
 * The metadata pointer and name are borrowed for the storage callback.
 * Copy any information needed after the callback returns. name is a
 * NUL-terminated basename owned by the receiver.
 *
 * 元数据指针和 name 只在存储回调期间借用。需要在回调返回后使用的信息
 * 必须复制。name 是接收器持有的、以 NUL 结尾的文件名。
 */
typedef struct {
    const char *name; /**< Nonempty printable basename without path separators. / 非空可打印文件名，不含路径分隔符。 */
    uint32_t total_size; /**< Advertised bytes, at most max_file_size; may be zero. / 声明字节数，不超过 max_file_size，允许为 0。 */
} ymodem_file_info_t;

/**
 * @brief Prepare storage after block 0 is validated.
 * @brief block 0 校验通过后准备存储后端。
 *
 * The callback runs before the first ACK for block 0 and must synchronously
 * prepare the target before returning QM_EOK.
 * On failure it must release any resources it acquired; abort is only called
 * after a successful begin.
 *
 * 回调在 block 0 的首次 ACK 之前执行，必须同步准备目标并返回 QM_EOK。
 * 失败时必须自行释放已申请的资源；只有 begin 成功后才会调用 abort。
 */
typedef qm_err_t (*ymodem_storage_begin_fn)(const ymodem_file_info_t *file,
                                            void *user);

/**
 * @brief Write one validated firmware fragment.
 * @brief 写入一段已经校验通过的固件数据。
 *
 * offset is the zero-based firmware offset. data is borrowed for the callback
 * and contains only bytes within the advertised file size; protocol padding
 * is never passed to this callback. QM_EOK means all length bytes were written
 * synchronously; return a negative error on failure, not a byte count.
 *
 * offset 是固件的零基址偏移。data 只在回调期间借用，并且只包含声明
 * 文件长度以内的有效字节；协议填充不会传入。返回 QM_EOK 表示已同步写入
 * 全部 length 字节；失败返回负错误码，不返回字节数。
 */
typedef qm_err_t (*ymodem_storage_write_fn)(uint32_t offset,
                                            const uint8_t *data,
                                            uint32_t length, void *user);

/**
 * @brief Commit the received image after the final block-0 handshake.
 * @brief 完成末尾 block 0 握手后提交接收镜像。
 *
 * The callback runs before the final ACK. It must not reboot; the application
 * decides when to reboot after ymodem_receive() returns QM_EOK.
 * Return QM_EOK after committing; on failure return a negative error and leave
 * the backend safe for the subsequent abort call.
 *
 * 回调在最终 ACK 之前执行，不应重启；应用在 ymodem_receive() 返回
 * QM_EOK 后决定重启时机。
 * 提交成功返回 QM_EOK；失败返回负错误码，并保证后续 abort 可安全调用。
 */
typedef qm_err_t (*ymodem_storage_finish_fn)(const ymodem_file_info_t *file,
                                             void *user);

/**
 * @brief Abort and release storage after a failed transfer.
 * @brief 传输失败后取消并释放存储会话。
 *
 * Called at most once after begin succeeds and before finish commits the
 * image. It must release the in-progress storage session and must not switch
 * the boot image. It is not called after finish succeeds, including when the
 * final transport ACK fails.
 *
 * begin 成功且 finish 尚未提交镜像时最多调用一次。必须释放正在进行的
 * 存储会话；不得切换启动镜像。finish 成功后，即使最终传输 ACK 失败，
 * 也不会调用该回调。
 */
typedef void (*ymodem_storage_abort_fn)(void *user);

/**
 * @brief Storage callbacks used by one receiver session.
 * @brief 一次接收会话使用的存储回调。
 */
typedef struct {
    ymodem_storage_begin_fn begin; /**< Required. / 必填。 */
    ymodem_storage_write_fn write; /**< Required. / 必填。 */
    ymodem_storage_finish_fn finish; /**< Required. / 必填。 */
    ymodem_storage_abort_fn abort; /**< Required. / 必填。 */
    void *user; /**< Shared storage context, borrowed until destroy; may be NULL. / 存储回调共用上下文，借用至销毁，可为 NULL。 */
} ymodem_storage_t;

/**
 * @brief Progress snapshot supplied after each accepted data block.
 * @brief 每个数据块接收并写入后提供的进度快照。
 *
 * transferred counts bytes successfully written by storage.write, excluding
 * padding and retransmissions. Equality with total_size means data writing
 * is complete; it does not indicate a successful finish or final ACK.
 *
 * transferred 统计 storage.write 成功写入的字节数，不含填充和重传数据。
 * 与 total_size 相等表示数据写入完成，不表示 finish 或最终 ACK 已成功。
 */
typedef struct {
    uint32_t transferred; /**< Valid bytes written, <= total_size. / 已写入的有效字节数，不超过 total_size。 */
    uint32_t total_size; /**< Advertised firmware size in bytes. / 固件声明长度，单位为字节。 */
} ymodem_progress_t;

/**
 * @brief Progress/cancellation callback.
 * @brief 进度及取消回调。
 *
 * Returning QM_EOK continues. Any other return aborts the session, sends a
 * best-effort CAN sequence, and returns -QM_EINTR. The callback runs on the
 * caller's task and must be short and non-blocking.
 * It runs after each successful storage.write and before that block's ACK.
 * Duplicate blocks, failed writes, and zero-length files produce no progress
 * callback. progress is borrowed only during this call; copy values to retain
 * them. user is the configured progress_user.
 *
 * 返回 QM_EOK 继续。其他返回值会取消会话、尽力发送 CAN 序列并返回
 * -QM_EINTR。回调运行在调用任务中，必须短小且不可阻塞。
 * 每次 storage.write 成功后、该块 ACK 之前调用；重复块、写入失败及
 * 零长度文件不触发进度回调。progress 仅在本次回调期间有效，如需保留
 * 应复制字段值。user 为配置中的 progress_user。
 */
typedef qm_err_t (*ymodem_progress_fn)(const ymodem_progress_t *progress,
                                       void *user);

/**
 * @brief YMODEM receiver configuration. The structure is copied at create.
 * @brief YMODEM 接收器配置。创建句柄时会复制该结构。
 *
 * A handle supports one synchronous receive at a time. Callers must serialize
 * ymodem_receive(), callbacks, and ymodem_destroy(). The UART must be owned by
 * this session and application logs must not share the YMODEM byte stream.
 * The three user pointers are independent borrowed contexts; each may be NULL
 * if its callbacks need no context. Referenced objects must outlive the handle.
 *
 * 一个句柄同一时间只支持一次同步接收。调用方必须串行化
 * ymodem_receive()、回调和 ymodem_destroy()。会话期间 UART 必须独占，
 * 应用日志不得写入同一 YMODEM 字节流。
 * 三个 user 指针分别借用各自的上下文；回调无需上下文时允许为 NULL。
 * 所引用对象的有效期必须覆盖句柄生命周期。
 */
typedef struct {
    ymodem_write_fn write; /**< Required ordered transport write callback. / 必填的有序传输写回调。 */
    ymodem_read_fn read; /**< Required ordered transport read callback. / 必填的有序传输读回调。 */
    void *transport_user; /**< Borrowed until destroy; passed to transport callbacks. / 借用至销毁，传给传输回调。 */
    ymodem_storage_t storage; /**< OTA/storage backend. / OTA/存储后端。 */
    uint32_t handshake_timeout_ms; /**< Header/final frame wait, 1..UINT32_MAX/2 ms; 0 uses Kconfig. / 首部及末尾帧等待时限，1..UINT32_MAX/2 ms；0 使用 Kconfig。 */
    uint32_t packet_timeout_ms; /**< Data frame wait including start byte, 1..UINT32_MAX/2 ms; 0 uses Kconfig. / 数据帧等待时限，含等待起始字节，1..UINT32_MAX/2 ms；0 使用 Kconfig。 */
    uint8_t retry_max; /**< Retries after the first attempt; 0 uses Kconfig, not zero retries. / 首次尝试后的重试次数；0 使用 Kconfig，并非禁用重试。 */
    uint32_t max_file_size; /**< Maximum image bytes; 0 uses Kconfig, not unlimited. / 最大镜像字节数；0 使用 Kconfig，并非无限制。 */
    ymodem_progress_fn progress; /**< Optional progress/cancel callback. / 可选进度及取消回调。 */
    void *progress_user; /**< Borrowed until destroy; ignored when progress is NULL. / 借用至销毁，progress 为 NULL 时忽略。 */
} ymodem_config_t;

/**
 * @brief Create a YMODEM receiver.
 * @brief 创建 YMODEM 接收器。
 *
 * The configuration is copied. The receiver owns one packet buffer of at most
 * 1029 bytes and a bounded file-name buffer; both live until destroy.
 *
 * 配置会被复制。接收器拥有一个最大 1029 字节的数据包缓冲区和一个有界
 * 文件名缓冲区，二者都在销毁前有效。
 *
 * @return A handle, or NULL for invalid configuration/out of memory.
 *         / 配置无效或内存不足时返回 NULL。
 */
ymodem_handle_t ymodem_create(const ymodem_config_t *config);

/**
 * @brief Destroy an idle receiver.
 * @brief 销毁空闲接收器。
 *
 * The caller must ensure no receive or callback is active. Destroy does not
 * abort an active storage backend.
 *
 * 调用方必须确保没有正在执行的接收或回调。销毁函数不会替活动存储后端
 * 执行 abort。
 */
void ymodem_destroy(ymodem_handle_t handle);

/**
 * @brief Receive one PC-sent YMODEM firmware image synchronously.
 * @brief 同步接收 PC 发送的一份 YMODEM 固件镜像。
 *
 * The receiver requests CRC16 with 'C', validates block 0 and SOH/STX data
 * frames, writes only the advertised number of bytes, and completes the
 * standard EOT/empty-block-0 termination handshake. Duplicate data blocks are
 * ACKed without being written again.
 *
 * 接收器用 'C' 请求 CRC16，校验 block 0 及 SOH/STX 数据帧，只写入声明的
 * 有效长度，并完成标准 EOT/空 block 0 结束握手。重复数据块只 ACK，不会
 * 再次写入。
 *
 * @return QM_EOK on completion; -QM_EINVAL for bad arguments or metadata,
 * -QM_EBUSY for concurrent receive, -QM_ETIMEOUT for exhausted timeouts,
 * -QM_EIO for transport errors, the negative error returned by a storage
 * callback, or -QM_EINTR for cancellation.
 * / 完成返回 QM_EOK；参数或元数据错误返回 -QM_EINVAL，并发接收返回
 * -QM_EBUSY，超时耗尽返回 -QM_ETIMEOUT，传输错误返回 -QM_EIO；存储
 * 回调的负错误码会原样返回；取消返回 -QM_EINTR。
 */
qm_err_t ymodem_receive(ymodem_handle_t handle);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
