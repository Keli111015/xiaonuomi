/*
 * qm_audio_decoder.c
 * 平台硬件解码器实现: adapter/qm_audio_codec.h 具名函数接口的 ac7911bx
 * 实现, 基于杰理 audio_server 硬件解码。应用层只 include 组件公共头
 * adapter/qm_audio_codec.h, 本文件由 CONFIG_QM_AUDIO_DECODER_SUPPORT
 * 决定是否链接生效。
 *
 * 线程模型:
 * - Pipeline 任务: 串行调用 open/push/flush/close;
 * - worker 任务("qma_dec"): 唯一发起 vendor 请求(OPEN/START/STOP), 并作为
 *   PCM 桥从 virtual_audio cbuf 读数据、经 emit 回调交付 PCM;
 * - 杰理解码器任务: 通过 VFS 回调(fread/fseek/...)从 push 环形缓冲拉码流,
 *   可能阻塞等待新数据;
 * - 应用任务("app_core"): 接收 AUDIO_SERVER_EVENT_END/ERR 事件, 只做置位与唤醒。
 *
 * 关键设计:
 * 1. push 与 VFS 之间用环形缓冲做 FIFO; 独立保留码流头部供 MP3 探针回退,
 *    避免生产者补满环时覆盖刚被探针读过的文件头。环内历史也可回放。
 * 2. vendor OPEN 推迟到首批码流入环之后, 保证 OPEN 探针读有数据可拉, 避免
 *    Pipeline 任务与解码器拉流互相等待而死锁; START 携带 NO_WAIT_READY,
 *    不等待首包 PCM 缓存, 保证 START 请求不被 virtual cbuf 消费阻塞。
 * 3. flush 仅封口输入(eof), 天然 EOF 由 worker 观察 virtual_audio->end 或
 *    VFS EOF 宽限计时兜底判定, 全部尾部 PCM 排空后才返回成功。
 * 4. data_sem 同时作为"新数据可读/环空间可写/EOF/停止"的统一唤醒信号,
 *    各方醒来后按各自条件重查, 因此虚假唤醒无害; 消费者释放环空间后也会
 *    signal, 保证环满时阻塞的 push 能及时恢复写入。
 */
#define BOOL_DEFINE_CONFLICT
#define CONFIG_QM_WITHOUT_STDIO_H_SUPPORT 1

#include "adapter/qm_audio_codec.h"

#include "generic/circular_buf.h"
#include "os/os_api.h"
#include "qm_kernel.h"
#include "qm_log.h"
#include "qm_string.h"
#include "qm_utils_timer.h"
#include "server/audio_server.h"
#include "server/server_core.h"

/* 解码输出缓冲长度 / 字节(maps to kconfig QM_AUDIO_DECODER_HW_OUTPUT_BUF_LEN). */
#ifndef CONFIG_QM_AUDIO_DECODER_HW_OUTPUT_BUF_LEN
#define CONFIG_QM_AUDIO_DECODER_HW_OUTPUT_BUF_LEN (12U * 1024U)
#endif
/* worker 任务栈大小 / 字节(maps to kconfig QM_AUDIO_DECODER_HW_TASK_STACK_SIZE). */
#ifndef CONFIG_QM_AUDIO_DECODER_HW_TASK_STACK_SIZE
#define CONFIG_QM_AUDIO_DECODER_HW_TASK_STACK_SIZE 8192
#endif
/* worker 任务优先级(maps to kconfig QM_AUDIO_DECODER_HW_TASK_PRIORITY). */
#ifndef CONFIG_QM_AUDIO_DECODER_HW_TASK_PRIORITY
#define CONFIG_QM_AUDIO_DECODER_HW_TASK_PRIORITY 22
#endif
/* push 环形缓冲默认容量 / 字节(maps to kconfig QM_AUDIO_DECODER_HW_FIFO_BYTES). */
#ifndef CONFIG_QM_AUDIO_DECODER_HW_FIFO_BYTES
#define CONFIG_QM_AUDIO_DECODER_HW_FIFO_BYTES 16384U
#endif
/* close 等待 worker 退出的有界时长 / ms(maps to kconfig QM_AUDIO_DECODER_HW_STOP_TIMEOUT_MS). */
#ifndef CONFIG_QM_AUDIO_DECODER_HW_STOP_TIMEOUT_MS
#define CONFIG_QM_AUDIO_DECODER_HW_STOP_TIMEOUT_MS 1000U
#endif

/* VFS 已判 EOF 但 SDK 未抬 virtual_audio->end 时, 兜底判定解码结束的宽限时长。
 * 需小于 Pipeline flush 的默认超时(1000ms), 保证 flush 一次即可等到结果。 */
#ifndef DEC_VFS_EOF_FALLBACK_GRACE_MS
#define DEC_VFS_EOF_FALLBACK_GRACE_MS 600U
#endif

/* Keep the same bounded header replay window as the legacy JL decoder. */
#define DEC_VFS_REWIND_CACHE_BYTES (16U * 1024U)

#define LOG_TAG "qm_audio_dec"

/* worker 终态: 由 worker 写入、flush/close 读取。 */
typedef enum {
    DEC_RESULT_RUNNING = 0, /* worker 存活, 解码进行中 */
    DEC_RESULT_END,         /* 自然结束, 尾部 PCM 已全部交付 */
    DEC_RESULT_ERROR,       /* vendor/emit 失败 */
    DEC_RESULT_STOPPED,     /* 被 close 取消, 未排空 */
} decoder_result_t;

typedef struct {
    /* ---- 输入侧: push 环形缓冲(绝对偏移寻址, 容量内历史可回放)。
     * 偏移均为"流内绝对偏移", 基准为 base_offset: 续播会话中最早写入的字节位于
     * base_offset, 其余会话为 0, 因此两条路径共用同一套寻址算术。 ---- */
    uint8_t *fifo;
    uint32_t fifo_capacity;
    uint8_t *rewind_cache;
    uint32_t rewind_cache_capacity;
    uint32_t rewind_cache_len;
    volatile uint32_t input_pos;  /* VFS 消费位置 */
    volatile uint32_t source_pos; /* push 生产位置 */
    qm_mutex_t fifo_lock;
    qm_bool fifo_lock_valid;
    /* 新数据/空间释放/eof/stop 统一唤醒信号: fread/fseek/worker/push 等待使用 */
    qm_sem_t data_sem;
    qm_bool data_sem_valid;
    /* flush 等待 worker 终态的信号 */
    qm_sem_t done_sem;
    qm_bool done_sem_valid;
    /* close 等待 worker 到达退出点的信号(worker 退出前发出) */
    qm_sem_t exit_sem;
    qm_bool exit_sem_valid;
    /* ---- pipeline 接口状态 ---- */
    volatile qm_bool eof_set;   /* flush 已封口输入 */
    volatile qm_bool stopping;  /* close 已请求退出 */
    /* ---- 输出侧(open 时拷贝) ---- */
    qm_audio_pcm_format_t output_format;
    qm_audio_decoder_emit_pcm_fn emit_pcm;
    void *emit_ctx;
    uint8_t decoder_channel_num; /* OPEN 后必须明确源声道。 */
    /* ---- 杰理会话(vendor 请求仅 worker 任务发起) ---- */
    struct server *server;
    volatile struct audio_cbuf_t *virtual_audio;
    volatile qm_bool opened;     /* AUDIO_DEC_OPEN 成功 */
    volatile qm_bool vfs_eof_seen;
    volatile uint32_t eof_empty_since_ms; /* VFS EOF 兜底计时起点(decoder/worker 任务读写) */
    /* ---- 终态与错误 ---- */
    volatile decoder_result_t result;
    volatile int server_error;   /* 事件回调置位(EVENT_ERR) */
    qm_task_t worker_task;
    /* ---- 续播(断点)支持 ---- */
    /* 码流逻辑偏移基准: VFS 的绝对偏移 0 对应流内 base_offset。非续播会话恒为 0,
     * 此时 input_pos/source_pos 的语义与普通播放完全一致。 */
    uint32_t base_offset;
    /* 断点恢复数据: OPEN 请求借用, 故由 ctx 持有直到 close。 */
    uint32_t resume_offset;      /* 断点处的绝对码流偏移(SDK 视角) */
    uint32_t resume_state_size;
    uint8_t resume_state[QM_AUDIO_RESUME_STATE_BYTES];
    struct audio_dec_breakpoint vendor_bp;
    /* 捕获请求(worker 处理)与其回传; scratch 归 ctx 所有, 消除超时后写入
     * 调用方缓冲的竞争。 */
    volatile qm_bool resume_req;
    /* 请求序号与完成序号: 调用方等待超时后可能立刻发起新请求, 此时上一次请求
     * 仍可能被 worker 处理完并回传。用序号配对, 使调用方只接受自己那次的结果,
     * 忽略迟到的旧回传。 */
    volatile uint32_t resume_gen;
    volatile uint32_t resume_done_gen;
    volatile int resume_result;
    qm_audio_resume_point_t resume_scratch;
    qm_sem_t resume_sem;
    qm_bool resume_sem_valid;
    /* ---- 契约外配置(借用) ---- */
    const char *codec_hint;
    uint32_t input_size;
    uint32_t duration_ms;
    qm_bool duration_valid;
    qm_audio_decoder_framing_t framing;
    uint32_t packet_bytes;
    uint32_t packet_duration_ms;
    uint8_t opus_toc;
    qm_bool opus_toc_valid;
} decoder_ctx_t;

/* Validate the declared duration of fixed-size raw Opus packets before the
 * vendor decoder sees them.  The CBR framing contract is byte-oriented, so
 * the TOC may straddle two push calls; callers retain the first TOC byte in
 * decoder_ctx_t until the second byte is available for code 3. */
static int opus_toc_duration(const uint8_t *data, uint32_t size,
                             uint32_t *duration_ms)
{
    uint32_t frame_units, count, total;

    if (data == NULL || size == 0U || duration_ms == NULL) return -QM_EINVAL;
    if (data[0] & 0x80U) frame_units = 1U << ((data[0] >> 3U) & 3U);
    else if ((data[0] & 0x60U) == 0x60U) frame_units = (data[0] & 0x08U) ? 8U : 4U;
    else {
        uint32_t mode = (data[0] >> 3U) & 3U;
        frame_units = mode == 3U ? 24U : (4U << mode);
    }
    count = data[0] & 3U;
    if (count == 0U) count = 1U;
    else if (count < 3U) count = 2U;
    else {
        if (size < 2U) return -QM_ETIMEOUT;
        count = data[1] & 0x3fU;
    }
    total = frame_units * count;
    if (count == 0U || total > 48U || (total & 1U) != 0U) return -QM_ENOSYS;
    *duration_ms = total * 5U / 2U;
    return QM_EOK;
}

static int opus_cbr_validate_bytes(decoder_ctx_t *ctx, const uint8_t *data,
                                   uint32_t size)
{
    uint32_t base, i;

    if (ctx == NULL || ctx->framing != QM_AUDIO_DECODER_FRAMING_OPUS_CBR ||
        ctx->packet_duration_ms == 0U || ctx->packet_bytes == 0U) return QM_EOK;
    base = ctx->source_pos;
    for (i = 0U; i < size; ++i) {
        uint32_t absolute = base + i;
        int ret;
        if ((absolute % ctx->packet_bytes) == 0U) {
            ctx->opus_toc = data[i];
            ctx->opus_toc_valid = QM_TRUE;
            if ((ctx->opus_toc & 3U) != 3U) {
                uint32_t duration;
                ret = opus_toc_duration(data + i, 1U, &duration);
                if (ret != QM_EOK) return ret;
                if (duration != ctx->packet_duration_ms) return -QM_ENOSYS;
            }
            /* Code 3 carries its frame count in the next byte.  Do not turn
             * a valid split PUSH into a timeout: the following invocation
             * validates it once that byte has actually been accepted. */
        } else if ((absolute % ctx->packet_bytes) == 1U && ctx->opus_toc_valid &&
                   (ctx->opus_toc & 3U) == 3U) {
            uint8_t toc[2] = {ctx->opus_toc, data[i]};
            uint32_t duration;
            ret = opus_toc_duration(toc, sizeof(toc), &duration);
            if (ret != QM_EOK) return ret;
            if (duration != ctx->packet_duration_ms) return -QM_ENOSYS;
        }
        if (((absolute + 1U) % ctx->packet_bytes) == 0U)
            ctx->opus_toc_valid = QM_FALSE;
    }
    return QM_EOK;
}

static void decoder_worker(void *arg);

/* 幂等资源释放: 仅释放 open 期间已创建的资源, 供 open 失败路径与 close 复用。 */
static void decoder_destroy_resources(decoder_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->fifo != NULL) {
        qm_free(ctx->fifo);
        ctx->fifo = NULL;
    }
    if (ctx->rewind_cache != NULL) {
        qm_free(ctx->rewind_cache);
        ctx->rewind_cache = NULL;
    }
    if (ctx->done_sem_valid) {
        (void)qm_sem_free(&ctx->done_sem);
        ctx->done_sem_valid = QM_FALSE;
    }
    if (ctx->exit_sem_valid) {
        (void)qm_sem_free(&ctx->exit_sem);
        ctx->exit_sem_valid = QM_FALSE;
    }
    if (ctx->data_sem_valid) {
        (void)qm_sem_free(&ctx->data_sem);
        ctx->data_sem_valid = QM_FALSE;
    }
    if (ctx->resume_sem_valid) {
        (void)qm_sem_free(&ctx->resume_sem);
        ctx->resume_sem_valid = QM_FALSE;
    }
    if (ctx->fifo_lock_valid) {
        (void)qm_mutex_free(&ctx->fifo_lock);
        ctx->fifo_lock_valid = QM_FALSE;
    }
    qm_free(ctx);
}

static void decoder_wake_rd(decoder_ctx_t *ctx)
{
    /* virtual_audio 为 volatile 指针: OPEN 成功前为空, close 阶段会复位,
     * 事件回调可能晚到, 判空即可安全跳过。 */
    if (ctx != NULL && ctx->virtual_audio != NULL &&
        ctx->virtual_audio->rd_sem != NULL) {
        os_sem_post((OS_SEM *)ctx->virtual_audio->rd_sem);
    }
}

/*
 * 已知长度的流在封口后全部消费完毕时, SDK 可能不会再发一次长度为 0 的
 * fread。此时按 VFS 到达 EOF 处理, 唤醒 worker 进入兜底宽限计时。
 */
static void decoder_maybe_infer_eof(decoder_ctx_t *ctx)
{
    if (ctx == NULL || ctx->stopping || ctx->vfs_eof_seen || !ctx->eof_set ||
        ctx->input_size == 0U) {
        return;
    }
    if (ctx->source_pos < ctx->input_size || ctx->input_pos < ctx->input_size) {
        return;
    }
    ctx->vfs_eof_seen = QM_TRUE;
    QM_LOGI(LOG_TAG, "vfs eof inferred input_pos=%u source_pos=%u",
            (unsigned int)ctx->input_pos, (unsigned int)ctx->source_pos);
    decoder_wake_rd(ctx);
}

/* ============================ VFS 回调(杰理解码器任务上下文) ============================ */

static int dec_vfs_read(void *file, void *buf, u32 len)
{
    decoder_ctx_t *ctx = (decoder_ctx_t *)file;
    u32 total = 0U;

    if (ctx == NULL || buf == NULL || len == 0U) {
        return 0;
    }

    /* MP3 解码对短读敏感: 流仍活跃时填满整个请求长度, 仅在 EOF/停止时提前返回。 */
    while (total < len) {
        uint32_t n;
        uint32_t idx;

        qm_mutex_lock(&ctx->fifo_lock, QM_WAIT_FOREVER);
        n = ctx->source_pos - ctx->input_pos;
        if (n > len - total) {
            n = len - total;
        }
        if (n > 0U) {
            /* 头部缓存按"距流起点"的相对偏移寻址: 缓存保存的是本会话最早写入的
             * rewind_cache_capacity 个字节, 续播会话中即从 base_offset 起算。 */
            uint32_t cached_off = ctx->input_pos - ctx->base_offset;

            if (cached_off < ctx->rewind_cache_len) {
                uint32_t cached = ctx->rewind_cache_len - cached_off;

                if (n > cached) {
                    n = cached;
                }
                qm_memcpy((u8 *)buf + total,
                          ctx->rewind_cache + cached_off, n);
            } else {
                uint32_t base = ctx->source_pos > ctx->fifo_capacity ?
                                ctx->source_pos - ctx->fifo_capacity : 0U;

                if (base < ctx->base_offset) {
                    base = ctx->base_offset;
                }
                if (ctx->input_pos < base) {
                    qm_mutex_unlock(&ctx->fifo_lock);
                    QM_LOGE(LOG_TAG, "vfs read outside retained history");
                    return -1;
                }
                idx = ctx->input_pos % ctx->fifo_capacity;
                if (idx + n <= ctx->fifo_capacity) {
                    qm_memcpy((u8 *)buf + total, ctx->fifo + idx, n);
                } else {
                    uint32_t first = ctx->fifo_capacity - idx;

                    qm_memcpy((u8 *)buf + total, ctx->fifo + idx, first);
                    qm_memcpy((u8 *)buf + total + first, ctx->fifo, n - first);
                }
            }
            ctx->input_pos += n;
            total += n;
            qm_mutex_unlock(&ctx->fifo_lock);
            /* 消费释放了环空间, 唤醒可能阻塞在环满背压中的 push。 */
            (void)qm_sem_signal(&ctx->data_sem);
            decoder_maybe_infer_eof(ctx);
            continue;
        }
        /* Observe EOF together with the empty FIFO. The producer may append
         * its last bytes and set eof_set as soon as this lock is released. */
        if (ctx->stopping) {
            qm_mutex_unlock(&ctx->fifo_lock);
            return 0;
        }
        if (ctx->eof_set) {
            qm_mutex_unlock(&ctx->fifo_lock);
            /* 输入封口且缓冲已排空: 解码器侧确定的 VFS EOF。 */
            if (!ctx->vfs_eof_seen) {
                ctx->vfs_eof_seen = QM_TRUE;
                decoder_wake_rd(ctx);
            }
            return (int)total;
        }
        qm_mutex_unlock(&ctx->fifo_lock);
        if (qm_sem_wait(&ctx->data_sem, QM_WAIT_FOREVER) != QM_EOK) {
            return (int)total;
        }
    }
    return (int)total;
}

static int dec_vfs_fseek(void *file, u32 offset, int seek_mode)
{
    decoder_ctx_t *ctx = (decoder_ctx_t *)file;
    uint32_t target;

    if (ctx == NULL) {
        return -2;
    }
    switch (seek_mode) {
        case 0: /* SEEK_SET */
            target = offset;
            break;
        case 1: /* SEEK_CUR */
            target = ctx->input_pos + offset;
            break;
        case 2: /* SEEK_END */
            if (ctx->input_size == 0U) {
                return -2;
            }
            target = ctx->input_size + offset;
            break;
        default:
            return -2;
    }
    if (ctx->input_size != 0U && target > ctx->input_size) {
        return -2;
    }

    for (;;) {
        uint32_t base;
        uint32_t src;
        uint32_t in;

        qm_mutex_lock(&ctx->fifo_lock, QM_WAIT_FOREVER);
        src = ctx->source_pos;
        in = ctx->input_pos;
        if (target <= src) {
            /* 头部缓存与环形保留窗口内的历史都可回放; 两者都以 base_offset 为
             * 下界, 低于该点的字节本会话从未获得。 */
            base = src > ctx->fifo_capacity ? src - ctx->fifo_capacity : 0U;
            if (base < ctx->base_offset) {
                base = ctx->base_offset;
            }
            if (target < base) {
                uint32_t rel = target >= ctx->base_offset ?
                               target - ctx->base_offset : UINT32_MAX;

                if (rel >= ctx->rewind_cache_len) {
                    qm_mutex_unlock(&ctx->fifo_lock);
                    QM_LOGW(LOG_TAG, "vfs rewind unavailable target=%u base=%u",
                            (unsigned int)target, (unsigned int)base);
                    return -2;
                }
            }
            if (target < in && ctx->vfs_eof_seen) {
                /* 回退移动使旧 EOF 失效, 允许解码器继续拉后续字节。 */
                ctx->vfs_eof_seen = QM_FALSE;
                ctx->eof_empty_since_ms = 0U;
            }
            ctx->input_pos = target;
            qm_mutex_unlock(&ctx->fifo_lock);
            /* 丢弃或回退都释放了环空间, 唤醒阻塞在环满背压中的 push。 */
            if (target > in) {
                (void)qm_sem_signal(&ctx->data_sem);
            }
            decoder_maybe_infer_eof(ctx);
            return 0;
        }
        /* Only reject EOF after checking the latest source_pos under lock. */
        if (ctx->stopping || ctx->eof_set) {
            qm_mutex_unlock(&ctx->fifo_lock);
            return -2;
        }
        /* 目标在未来数据区: 丢弃已缓冲字节, 等待 push 推进 source_pos。 */
        ctx->input_pos = src;
        qm_mutex_unlock(&ctx->fifo_lock);
        (void)qm_sem_signal(&ctx->data_sem);
        decoder_maybe_infer_eof(ctx);
        if (qm_sem_wait(&ctx->data_sem, QM_WAIT_FOREVER) != QM_EOK) {
            return -2;
        }
    }
}

static int dec_vfs_ftell(void *file)
{
    decoder_ctx_t *ctx = (decoder_ctx_t *)file;

    return ctx == NULL ? -1 : (int)ctx->input_pos;
}

static int dec_vfs_flen(void *file)
{
    decoder_ctx_t *ctx = (decoder_ctx_t *)file;

    return ctx == NULL ? 0 : (int)ctx->input_size;
}

static int dec_vfs_fclose(void *file)
{
    QM_UNUSED_ARG(file);
    return 0;
}

static const struct audio_vfs_ops s_dec_vfs = {
    .fread  = dec_vfs_read,
    .fseek  = dec_vfs_fseek,
    .ftell  = dec_vfs_ftell,
    .flen   = dec_vfs_flen,
    .fclose = dec_vfs_fclose,
};

/* ============================ PCM 交付 ============================ */

/* 经 emit 回调交付一帧 PCM(借用语义, 回调返回即失效)。 */
static int dec_emit(decoder_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    /* 未使用的元数据字段必须为零: 渲染引擎会以 duration_ms/sequence 非零为由
     * 拒绝该帧(-QM_EINVAL), 导致解码 worker 以 emit failed 退出。 */
    qm_audio_data_t block = {0};

    block.data = data;
    block.size = len;
    return ctx->emit_pcm(&block, ctx->emit_ctx);
}

/* 解码器实际输出声道数: 杰理虚拟解码对 mp3 等压缩源固定输出 2ch(单声道文件为
 * 左右同相复制), req.dec.channel 仅作提示并会被 SDK 回填, 故按回填值处理。 */
static uint32_t dec_source_channels(const decoder_ctx_t *ctx)
{
    return ctx->decoder_channel_num != 0U ? (uint32_t)ctx->decoder_channel_num :
           (uint32_t)ctx->output_format.channels;
}

/* 把解码器输出就地和解为调用方声明的声道数。解码输出固定为 16bit 交织 PCM,
 * 若把 2ch 数据原样交给声明为单声道的播放端, 播放端会把每个立体声帧当作两个
 * 单声道样本, 语速只剩一半。降混时同帧各源声道取均值; 声道一致时零拷贝。
 * 返回和解后的字节数, 0 表示该组合无法就地完成。 */
static uint32_t dec_reconcile_channels(decoder_ctx_t *ctx, uint8_t *data, uint32_t len)
{
    uint32_t src_ch = dec_source_channels(ctx);
    uint32_t dst_ch = (uint32_t)ctx->output_format.channels;
    uint32_t frames = len / (2U * src_ch);
    uint32_t i;
    uint32_t c;

    if (src_ch == dst_ch) {
        return len;
    }
    if (dst_ch > src_ch) {
        /* 升混需要比源更大的缓冲, 本平台各调用方一律声明单声道, 不支持该组合。 */
        return 0U;
    }
    if (dst_ch == 1U) {
        for (i = 0U; i < frames; ++i) {
            const int16_t *src = (const int16_t *)data + i * src_ch;
            int32_t sum = 0;

            for (c = 0U; c < src_ch; ++c) {
                sum += src[c];
            }
            ((int16_t *)data)[i] = (int16_t)(sum / (int32_t)src_ch);
        }
    } else {
        /* 取前 dst_ch 个源声道; 目标写位置始终不晚于源读位置, 可就地完成。 */
        for (i = 0U; i < frames; ++i) {
            const int16_t *src = (const int16_t *)data + i * src_ch;
            int16_t *dst = (int16_t *)data + i * dst_ch;

            for (c = 0U; c < dst_ch; ++c) {
                dst[c] = src[c];
            }
        }
    }
    return frames * 2U * dst_ch;
}

static int dec_pcm_submit(decoder_ctx_t *ctx, uint8_t *data, uint32_t len)
{
    uint32_t converted;

    if (data == NULL || len == 0U) {
        return -QM_EINVAL;
    }
    if (len % (2U * dec_source_channels(ctx)) != 0U) {
        QM_LOGE(LOG_TAG, "unaligned PCM len=%u ch=%u",
                (unsigned int)len, (unsigned int)dec_source_channels(ctx));
        return -QM_EIO;
    }
    converted = dec_reconcile_channels(ctx, data, len);
    if (converted == 0U) {
        QM_LOGE(LOG_TAG, "unsupported channel mix source_ch=%u output_ch=%u",
                (unsigned int)dec_source_channels(ctx),
                (unsigned int)ctx->output_format.channels);
        return -QM_ENOSYS;
    }
    return dec_emit(ctx, data, converted);
}

/* ============================ 事件回调(app_core 任务上下文) ============================ */

static void dec_server_event(void *priv, int argc, int *argv)
{
    decoder_ctx_t *ctx = (decoder_ctx_t *)priv;

    if (ctx == NULL || argc <= 0 || argv == NULL) {
        return;
    }
    if (argv[0] == AUDIO_SERVER_EVENT_END) {
        /* 只唤醒 PCM 桥去检查 virtual_audio->end; 该事件本身不当作 EOF 事实。 */
        decoder_wake_rd(ctx);
    } else if (argv[0] == AUDIO_SERVER_EVENT_ERR) {
        ctx->server_error = -QM_EIO;
        decoder_wake_rd(ctx);
    }
}

/* ============================ worker: vendor 请求与 PCM 桥 ============================ */

/* 幂等地把 worker 推到终态并唤醒 flush 等待者。 */
static void dec_worker_terminate(decoder_ctx_t *ctx, decoder_result_t result)
{
    if (ctx->result == DEC_RESULT_RUNNING) {
        ctx->result = result;
    }
    (void)qm_sem_signal(&ctx->done_sem);
    decoder_wake_rd(ctx);
}

/* 发起 AUDIO_DEC_STOP(幂等, 仅在 OPEN 成功后); 解码器任务可能在 fread 阻塞,
 * close 已置 stopping 并唤醒 data_sem, fread 返回 0 后解码器即可处理 STOP。 */
static void dec_vendor_stop(decoder_ctx_t *ctx)
{
    union audio_req req;

    if (!ctx->opened || ctx->server == NULL) {
        return;
    }
    qm_memset(&req, 0, sizeof(req));
    req.dec.cmd = AUDIO_DEC_STOP;
    if (server_request(ctx->server, AUDIO_REQ_DEC, &req) != 0) {
        QM_LOGW(LOG_TAG, "AUDIO_DEC_STOP failed");
    }
}

/* 取当前解码断点(仅 worker 任务调用)。成功时 *bp 由 SDK 分配 data, 调用方负责
 * qm_free; len 为 0 表示无状态可携带。返回 QM_EOK 或负错误码。 */
static int dec_vendor_get_breakpoint(decoder_ctx_t *ctx, struct audio_dec_breakpoint *bp)
{
    union audio_req req;

    if (!ctx->opened || ctx->server == NULL) {
        return -QM_ENOSYS;
    }
    qm_memset(bp, 0, sizeof(*bp));
    qm_memset(&req, 0, sizeof(req));
    req.dec.bp = bp;
    req.dec.cmd = AUDIO_DEC_GET_BREAKPOINT;
    if (server_request(ctx->server, AUDIO_REQ_DEC, &req) != 0) {
        QM_LOGW(LOG_TAG, "AUDIO_DEC_GET_BREAKPOINT failed");
        return -QM_EIO;
    }
    if (req.dec.status == AUDIO_DEC_STOP) {
        /* 解码已停止: 断点无法再续, 回退给调用方走完整重启。 */
        if (bp->data != NULL) {
            qm_free(bp->data);
            bp->data = NULL;
        }
        bp->len = 0;
        return -QM_ENOSYS;
    }
    if (bp->len < 0) {
        bp->len = 0;
    }
    return QM_EOK;
}

/* worker 内处理一次续播点捕获: 结果写入 ctx 自有的 scratch, 再经 resume_sem 回传。
 * scratch 归 ctx 所有, 因此调用方等待超时后迟到的写入不会触及已失效的调用方缓冲。 */
static void dec_worker_capture_resume(decoder_ctx_t *ctx)
{
    /* 零初始化: dec_vendor_get_breakpoint 在未打开时提前返回不 memset, 保证
     * 下方无条件 qm_free(bp.data) 不会拿到垃圾指针。 */
    struct audio_dec_breakpoint bp = {0};
    qm_audio_resume_point_t *out = &ctx->resume_scratch;
    uint32_t gen = ctx->resume_gen;
    int result = -QM_ENOSYS;

    ctx->resume_req = QM_FALSE;
    qm_memset(out, 0, sizeof(*out));
    if (dec_vendor_get_breakpoint(ctx, &bp) == QM_EOK &&
        bp.len > 0 && bp.fptr != 0U) {
        if ((uint32_t)bp.len <= QM_AUDIO_RESUME_STATE_BYTES && bp.data != NULL) {
            out->offset = bp.fptr;
            out->state_size = (uint32_t)bp.len;
            qm_memcpy(out->state, bp.data, out->state_size);
            result = QM_EOK;
        } else {
            /* 状态超过容量是理论不可达(平台最大 2036 字节), 拒绝而非截断。 */
            QM_LOGW(LOG_TAG, "breakpoint too large len=%d", bp.len);
        }
    }
    if (bp.data != NULL) {
        qm_free(bp.data);
    }
    ctx->resume_result = result;
    /* 先置完成序号再发信号: 等待方据此判定回传是否属于自己发起的请求, 从而
     * 忽略上一次超时请求遗留的旧回传。 */
    ctx->resume_done_gen = gen;
    (void)qm_sem_signal(&ctx->resume_sem);
}

static int dec_vendor_start(decoder_ctx_t *ctx)
{
    union audio_req req;
    qm_bool raw_pcm;
    int ret;

    ctx->server = server_open("audio_server", "dec");
    if (ctx->server == NULL) {
        QM_LOGE(LOG_TAG, "server_open audio_server failed");
        return -QM_EIO;
    }
    /* 事件投递到带消息队列的 app_core 任务(与平台既有约定一致)。 */
    server_register_event_handler_to_task(ctx->server, ctx, dec_server_event,
                                          "app_core");

    qm_memset(&req, 0, sizeof(req));
    req.dec.cmd = AUDIO_DEC_OPEN;
    req.dec.volume = 100U;
    req.dec.priority = 1U;
    req.dec.output_buf_len = CONFIG_QM_AUDIO_DECODER_HW_OUTPUT_BUF_LEN;
    req.dec.file = (FILE *)ctx;
    req.dec.vfs_ops = &s_dec_vfs;
    req.dec.dec_type = ctx->codec_hint;
    req.dec.sample_source = "virtual";
    raw_pcm = ctx->codec_hint != NULL && qm_strcmp(ctx->codec_hint, "pcm") == 0;
    /* 裸 PCM 无容器头可探, 直接给定采样率; 其余类型交给 SDK 探测采样率。
     * req.dec.channel 对压缩源只是"期望输出声道"的提示, SDK 会按解码结果回填,
     * 因此不能据此判定播放是否可行(见下方校验)。 */
    req.dec.sample_rate = raw_pcm ? ctx->output_format.sample_rate_hz : 0U;
    req.dec.channel = (u8)ctx->output_format.channels;
    req.dec.force_sr = 0U; /* Preserve the source rate; reject incompatible PCM below. */
    switch (ctx->framing) {
        case QM_AUDIO_DECODER_FRAMING_OPUS_OGG:
            req.dec.attr |= AUDIO_ATTR_OPUS_OGG_TYPE;
            break;
        case QM_AUDIO_DECODER_FRAMING_OPUS_RAW_HEADER:
            req.dec.attr |= AUDIO_ATTR_OPUS_RAWDTF_TYPE;
            break;
        case QM_AUDIO_DECODER_FRAMING_OPUS_CBR:
            req.dec.attr |= AUDIO_ATTR_OPUS_CBR_PKTLEN_TYPE;
            req.dec.opus_cbr_pktlen = ctx->packet_bytes;
            break;
        case QM_AUDIO_DECODER_FRAMING_STREAM:
        default:
            break;
    }

    if (ctx->resume_state_size != 0U && ctx->resume_offset != 0U) {
        /* 断点恢复: 让 SDK 从记录的码流偏移与编解码状态续解。vendor_bp.data
         * 指向 ctx 自有副本, 生命周期覆盖本次 OPEN 之后 SDK 的使用。 */
        ctx->vendor_bp.len = (int)ctx->resume_state_size;
        ctx->vendor_bp.fptr = ctx->resume_offset;
        ctx->vendor_bp.data = ctx->resume_state;
        req.dec.bp = &ctx->vendor_bp;
        QM_LOGI(LOG_TAG, "resume open fptr=%u state=%u base=%u",
                (unsigned int)ctx->resume_offset,
                (unsigned int)ctx->resume_state_size,
                (unsigned int)ctx->base_offset);
    }

    /* OPEN 的探针读从环形缓冲取数, 故 worker 在首批 push 之后才发起, 此处
     * 拉流阻塞由并发运行的 Pipeline push 满足, 不会死锁。 */
    ret = server_request(ctx->server, AUDIO_REQ_DEC, &req);
    if (ret != 0 || req.dec.virtual_audio == NULL) {
        QM_LOGE(LOG_TAG, "AUDIO_DEC_OPEN failed ret=%d", ret);
        return -QM_EIO;
    }
    qm_mutex_lock(&ctx->fifo_lock, QM_WAIT_FOREVER);
    if (req.dec.total_time != 0U && req.dec.total_time != UINT32_MAX) {
        ctx->duration_ms = req.dec.total_time > UINT32_MAX / 1000U ? UINT32_MAX : req.dec.total_time * 1000U;
        ctx->duration_valid = QM_TRUE;
    }
    qm_mutex_unlock(&ctx->fifo_lock);
    ctx->decoder_channel_num = req.dec.channel;
    ctx->virtual_audio = req.dec.virtual_audio;
    /* OPEN 已成功: 先置 opened, 使后续任何退出路径(wrapper 失败/通道不支持)
     * 都能补发 AUDIO_DEC_STOP, 避免遗留解码会话。 */
    ctx->opened = QM_TRUE;
    /* 仅对裸 PCM 做强校验: 裸 PCM 按声明参数直接输出, 采样率/声道不匹配即无法
     * 交付, 必须拒绝。压缩源(mp3 等)的 sample_rate/channel 是 SDK 解码后的回填
     * 结果(如单声道 mp3 被回填为 16000/2ch), 与请求值不等属正常现象, 强校验会
     * 把本可播放的提示音误判为"不支持的 PCM"而拒绝播放。 */
    if (raw_pcm &&
        (req.dec.sample_rate != ctx->output_format.sample_rate_hz ||
         ctx->decoder_channel_num != ctx->output_format.channels)) {
        QM_LOGE(LOG_TAG, "unsupported PCM source_rate=%u source_ch=%u output_ch=%u",
                (unsigned int)req.dec.sample_rate,
                (unsigned int)ctx->decoder_channel_num,
                (unsigned int)ctx->output_format.channels);
        return -QM_ENOSYS;
    }
    QM_LOGI(LOG_TAG, "opened codec=%s sr=%u source_ch=%u output_ch=%u",
            ctx->codec_hint != NULL ? ctx->codec_hint : "auto",
            (unsigned int)ctx->output_format.sample_rate_hz,
            (unsigned int)ctx->decoder_channel_num,
            (unsigned int)ctx->output_format.channels);

    qm_memset(&req, 0, sizeof(req));
    req.dec.cmd = AUDIO_DEC_START;
    /* 码流由 Pipeline 异步推入, 开始解码时不等待首包 PCM 缓存; 否则 START
     * 的同步等待与 virtual cbuf 消费(本 worker)互相等待会死锁。 */
    req.dec.attr = AUDIO_ATTR_NO_WAIT_READY;
    if (server_request(ctx->server, AUDIO_REQ_DEC, &req) != 0) {
        QM_LOGE(LOG_TAG, "AUDIO_DEC_START failed");
        return -QM_EIO;
    }
    return QM_EOK;
}

/* 排空 virtual cbuf 残余 PCM; 返回 QM_EOK 或首个失败错误码。 */
static int dec_drain_tail(decoder_ctx_t *ctx, uint8_t *pcm_buf, uint32_t cap)
{
    int len;

    while ((len = (int)cbuf_get_data_size((cbuffer_t *)ctx->virtual_audio->cbuf)) > 0) {
        int ret;

        if (len > (int)cap) {
            len = (int)cap;
        }
        len = (int)cbuf_read((cbuffer_t *)ctx->virtual_audio->cbuf, pcm_buf,
                             (u32)len);
        if (len <= 0) {
            break;
        }
        ret = dec_pcm_submit(ctx, pcm_buf, (uint32_t)len);
        if (ret != QM_EOK) {
            return ret;
        }
    }
    return QM_EOK;
}

static void decoder_worker(void *arg)
{
    decoder_ctx_t *ctx = (decoder_ctx_t *)arg;
    uint8_t pcm_buf[1024];
    const char *exit_reason = "stopped";

    if (ctx == NULL) {
        return;
    }
    while (!ctx->stopping) {
        if (ctx->resume_req) {
            /* 捕获请求优先于数据搬运: 命令来自应用任务, 有界等待。 */
            dec_worker_capture_resume(ctx);
            continue;
        }
        if (!ctx->opened) {
            uint32_t pending;

            /* 未发起 vendor 前等待首批码流(或 EOF)。 */
            qm_mutex_lock(&ctx->fifo_lock, QM_WAIT_FOREVER);
            pending = ctx->source_pos - ctx->input_pos;
            qm_mutex_unlock(&ctx->fifo_lock);
            if (pending == 0U && ctx->eof_set) {
                /* 空输入: 无码流可解, 视为自然结束。 */
                exit_reason = "empty input";
                dec_worker_terminate(ctx, DEC_RESULT_END);
                goto exit;
            }
            if (pending == 0U) {
                if (qm_sem_wait(&ctx->data_sem, QM_WAIT_FOREVER) != QM_EOK) {
                    if (ctx->stopping) {
                        goto exit;
                    }
                    exit_reason = "data wait failed";
                    dec_worker_terminate(ctx, DEC_RESULT_ERROR);
                    goto exit;
                }
                continue;
            }
            if (dec_vendor_start(ctx) != QM_EOK) {
                exit_reason = "vendor start failed";
                dec_worker_terminate(ctx, DEC_RESULT_ERROR);
                goto exit;
            }
            continue;
        }

        if (ctx->server_error != 0) {
            exit_reason = "server error event";
            dec_worker_terminate(ctx, DEC_RESULT_ERROR);
            goto exit;
        }
        if (ctx->virtual_audio == NULL) {
            exit_reason = "virtual audio missing";
            dec_worker_terminate(ctx, DEC_RESULT_ERROR);
            goto exit;
        }
        if (ctx->virtual_audio->end) {
            int ret;

            /* 解码端自然结束: 确认标记并排空残余 PCM。 */
            exit_reason = "decoder eof";
            ctx->virtual_audio->end = 2;
            if (ctx->virtual_audio->wr_sem != NULL) {
                os_sem_post((OS_SEM *)ctx->virtual_audio->wr_sem);
            }
            ret = dec_drain_tail(ctx, pcm_buf, sizeof(pcm_buf));
            if (ret != QM_EOK) {
                exit_reason = "tail drain failed";
                dec_worker_terminate(ctx, DEC_RESULT_ERROR);
                goto exit;
            }
            dec_worker_terminate(ctx, DEC_RESULT_END);
            goto exit;
        }

        /* 虚拟解码器等待该确认后才继续写 cbuffer。 */
        if (ctx->virtual_audio->wr_sem != NULL) {
            os_sem_post((OS_SEM *)ctx->virtual_audio->wr_sem);
        }
        {
            int len = (int)cbuf_read((cbuffer_t *)ctx->virtual_audio->cbuf,
                                     pcm_buf, sizeof(pcm_buf));

            if (len > 0) {
                int ret;

                ctx->eof_empty_since_ms = 0U;
                ret = dec_pcm_submit(ctx, pcm_buf, (uint32_t)len);
                if (ret != QM_EOK) {
                    QM_LOGW(LOG_TAG, "pcm submit failed ret=%d", ret);
                    exit_reason = "emit failed";
                    dec_worker_terminate(ctx, DEC_RESULT_ERROR);
                    goto exit;
                }
                continue;
            }
        }
        if (ctx->vfs_eof_seen) {
            /* VFS 已 EOF 但 SDK 未抬 end 标记时兜底判定结束。 */
            uint32_t now = qm_now_ms();

            if (ctx->eof_empty_since_ms == 0U) {
                ctx->eof_empty_since_ms = now;
            }
            if ((uint32_t)(now - ctx->eof_empty_since_ms) >=
                DEC_VFS_EOF_FALLBACK_GRACE_MS) {
                exit_reason = "vfs eof fallback";
                QM_LOGW(LOG_TAG, "decoder end fallback after VFS EOF");
                dec_worker_terminate(ctx, DEC_RESULT_END);
                goto exit;
            }
        }
        if (ctx->virtual_audio->rd_sem != NULL) {
            os_sem_pend((OS_SEM *)ctx->virtual_audio->rd_sem, 20);
        } else {
            qm_msleep(5);
        }
    }

exit:
    dec_vendor_stop(ctx);
    if (ctx->result == DEC_RESULT_RUNNING) {
        /* 被 close 取消: 未排空, 终端结果标记为 STOPPED。 */
        dec_worker_terminate(ctx, DEC_RESULT_STOPPED);
    }
    QM_LOGI(LOG_TAG, "worker exit reason=%s result=%d",
            exit_reason, (int)ctx->result);
    /* 通知 close: 已到达退出点, 无需轮询端口标志。 */
    (void)qm_sem_signal(&ctx->exit_sem);
    /* 任务自删除由 QMOS 任务封装拥有; close 侧通过 join 确认删除完成。 */
    qm_task_exit(NULL);
}

/* ============================ adapter/qm_audio_codec.h 解码器接口实现 ============================ */

int qm_audio_decoder_check_format(const qm_audio_pcm_format_t *output,
                                  const qm_audio_decoder_config_t *config)
{
    if (output == NULL || output->sample_rate_hz == 0U ||
        (output->channels != 1U && output->channels != 2U) ||
        output->sample != QM_AUDIO_PCM_S16) return -QM_ENOSYS;
    if (config == NULL) return QM_EOK;
    if ((unsigned int)config->framing > QM_AUDIO_DECODER_FRAMING_OPUS_CBR ||
        ((config->framing == QM_AUDIO_DECODER_FRAMING_OPUS_CBR) !=
         (config->packet_bytes != 0U))) return -QM_EINVAL;
    if (config->packet_duration_ms > 120U ||
        (config->framing == QM_AUDIO_DECODER_FRAMING_OPUS_CBR &&
         config->packet_duration_ms != 0U && config->packet_bytes < 2U)) return -QM_EINVAL;
    /* This provider can verify duration from the TOC only for fixed CBR
     * packets. Reject other non-zero declarations instead of accepting a
     * profile whose duration would be silently ignored. */
    if (config->packet_duration_ms != 0U &&
        (config->codec_hint == NULL || qm_strcmp(config->codec_hint, "opus") != 0 ||
         config->framing != QM_AUDIO_DECODER_FRAMING_OPUS_CBR)) return -QM_ENOSYS;
    if (config->codec_hint != NULL && qm_strcmp(config->codec_hint, "opus") == 0)
        return config->framing == QM_AUDIO_DECODER_FRAMING_STREAM ? -QM_EINVAL : QM_EOK;
    return config->framing == QM_AUDIO_DECODER_FRAMING_STREAM ? QM_EOK : -QM_EINVAL;
}

int qm_audio_decoder_open(void **backend_ctx,
                          const qm_audio_pcm_format_t *output_format,
                          qm_audio_decoder_emit_pcm_fn emit_pcm,
                          void *emit_ctx,
                          void *user_ctx)
{
    const qm_audio_decoder_config_t *cfg =
        (const qm_audio_decoder_config_t *)user_ctx;
    decoder_ctx_t *ctx;
    uint32_t fifo_bytes = CONFIG_QM_AUDIO_DECODER_HW_FIFO_BYTES;
    int ret = -QM_EIO;

    if (backend_ctx == NULL || output_format == NULL || emit_pcm == NULL) {
        return -QM_EINVAL;
    }
    *backend_ctx = NULL;
    if (cfg != NULL) {
        if ((unsigned int)cfg->framing > QM_AUDIO_DECODER_FRAMING_OPUS_CBR ||
            ((cfg->framing == QM_AUDIO_DECODER_FRAMING_OPUS_CBR) !=
             (cfg->packet_bytes != 0U)) ||
            cfg->packet_duration_ms > 120U ||
            (cfg->framing == QM_AUDIO_DECODER_FRAMING_OPUS_CBR &&
             cfg->packet_duration_ms != 0U && cfg->packet_bytes < 2U) ||
            (cfg->framing != QM_AUDIO_DECODER_FRAMING_STREAM &&
             cfg->codec_hint != NULL && qm_strcmp(cfg->codec_hint, "opus") != 0)) {
            return -QM_EINVAL;
        }
        if (cfg->packet_duration_ms != 0U &&
            (cfg->codec_hint == NULL || qm_strcmp(cfg->codec_hint, "opus") != 0 ||
             cfg->framing != QM_AUDIO_DECODER_FRAMING_OPUS_CBR)) {
            return -QM_ENOSYS;
        }
        /* A codec name alone cannot distinguish Ogg, framed raw, and bare
         * Opus. Never concatenate arbitrary transport packets and guess. */
        if (cfg->framing == QM_AUDIO_DECODER_FRAMING_STREAM &&
            cfg->codec_hint != NULL && qm_strcmp(cfg->codec_hint, "opus") == 0) {
            return -QM_EINVAL;
        }
        /* offset 为零表示从流头开始, 无需续播。其余记录必须带可用的编解码状态;
         * 无法满足时明确失败, 由 Pipeline 回退为从字节零重启。 */
        if (cfg->resume != NULL && cfg->resume->offset != 0U &&
            (cfg->resume->state_size == 0U ||
             cfg->resume->state_size > QM_AUDIO_RESUME_STATE_BYTES)) {
            QM_LOGW(LOG_TAG, "unsupported resume offset=%u state=%u",
                    (unsigned int)cfg->resume->offset,
                    (unsigned int)cfg->resume->state_size);
            return -QM_ENOSYS;
        }
    }
    /* 杰理虚拟解码输出固定为 16bit 交织 PCM; 支持单/双声道输出。 */
    if (output_format->sample_rate_hz == 0U ||
        (output_format->channels != 1U && output_format->channels != 2U) ||
        output_format->sample != QM_AUDIO_PCM_S16) {
        QM_LOGE(LOG_TAG, "unsupported pcm format rate=%u ch=%u sample=%d",
                (unsigned int)output_format->sample_rate_hz,
                (unsigned int)output_format->channels,
                (int)output_format->sample);
        return -QM_ENOSYS;
    }
    if (cfg != NULL && cfg->fifo_bytes != 0U) {
        fifo_bytes = cfg->fifo_bytes;
    }
    if (fifo_bytes < 1024U) {
        fifo_bytes = 1024U;
    }

    ctx = (decoder_ctx_t *)qm_calloc(1U, sizeof(*ctx));
    if (ctx == NULL) {
        return -QM_ENOMEM;
    }
    ctx->output_format = *output_format;
    ctx->emit_pcm = emit_pcm;
    ctx->emit_ctx = emit_ctx;
    if (cfg != NULL) {
        ctx->codec_hint = cfg->codec_hint;
        ctx->input_size = cfg->input_size;
        ctx->framing = cfg->framing;
        ctx->packet_bytes = cfg->packet_bytes;
        ctx->packet_duration_ms = cfg->packet_duration_ms;
        if (ctx->framing != QM_AUDIO_DECODER_FRAMING_STREAM) {
            ctx->codec_hint = "opus";
        }
        if (cfg->resume != NULL && cfg->resume->offset != 0U) {
            /* 续播会话: 记录断点, 把 VFS 基址前移一段(QM_AUDIO_RESUME_BACKOFF_BYTES,
             * 与 Pipeline 抓取起点同源), 使 SDK 恢复断点后仍能回退读取容器上下文。
             * 随后 input_pos/source_pos 均以该基址为起点。 */
            ctx->resume_offset = cfg->resume->offset;
            ctx->resume_state_size = cfg->resume->state_size;
            qm_memcpy(ctx->resume_state, cfg->resume->state,
                      ctx->resume_state_size);
            ctx->base_offset = ctx->resume_offset > QM_AUDIO_RESUME_BACKOFF_BYTES ?
                               ctx->resume_offset - QM_AUDIO_RESUME_BACKOFF_BYTES : 0U;
            ctx->input_pos = ctx->base_offset;
            ctx->source_pos = ctx->base_offset;
        }
    }
    ctx->fifo_capacity = fifo_bytes;
    ctx->fifo = (uint8_t *)qm_malloc(fifo_bytes);
    if (ctx->fifo == NULL) {
        ret = -QM_ENOMEM;
        goto err;
    }
    ctx->rewind_cache_capacity = DEC_VFS_REWIND_CACHE_BYTES;
    if (ctx->input_size != 0U &&
        ctx->input_size < ctx->rewind_cache_capacity) {
        ctx->rewind_cache_capacity = ctx->input_size;
    }
    ctx->rewind_cache = (uint8_t *)qm_malloc(ctx->rewind_cache_capacity);
    if (ctx->rewind_cache == NULL) {
        ret = -QM_ENOMEM;
        goto err;
    }
    if (qm_mutex_new(&ctx->fifo_lock) != QM_EOK) {
        goto err;
    }
    ctx->fifo_lock_valid = QM_TRUE;
    if (qm_sem_new(&ctx->data_sem, 0) != QM_EOK) {
        goto err;
    }
    ctx->data_sem_valid = QM_TRUE;
    if (qm_sem_new(&ctx->done_sem, 0) != QM_EOK) {
        goto err;
    }
    ctx->done_sem_valid = QM_TRUE;
    if (qm_sem_new(&ctx->exit_sem, 0) != QM_EOK) {
        goto err;
    }
    ctx->exit_sem_valid = QM_TRUE;
    if (qm_sem_new(&ctx->resume_sem, 0) != QM_EOK) {
        goto err;
    }
    ctx->resume_sem_valid = QM_TRUE;
    /* close() may be called immediately after open(): exit_sem starts at zero,
     * so close blocks until the newly scheduled worker reaches its exit point
     * instead of freeing ctx while the worker has not run yet. / close 可能紧跟
     * open 调用: exit_sem 初值为 0, close 会一直等到新建的 worker 到达退出点,
     * 不会在 worker 尚未运行时释放 ctx。 */
    ret = qm_task_new(&ctx->worker_task, "qma_dec", decoder_worker, ctx,
                      CONFIG_QM_AUDIO_DECODER_HW_TASK_STACK_SIZE,
                      CONFIG_QM_AUDIO_DECODER_HW_TASK_PRIORITY);
    if (ret != QM_EOK) {
        ret = -QM_EBUSY;
        goto err;
    }
    *backend_ctx = ctx;
    return QM_EOK;

err:
    decoder_destroy_resources(ctx);
    return ret;
}

int qm_audio_decoder_push(void *backend_ctx, const void *data, uint32_t size,
                          uint32_t timeout_ms, uint32_t *consumed)
{
    decoder_ctx_t *ctx = (decoder_ctx_t *)backend_ctx;
    qm_utils_time_t deadline;
    uint32_t done = 0U;

    if (consumed == NULL) {
        return -QM_EINVAL;
    }
    *consumed = 0U;
    if (ctx == NULL || (size != 0U && data == NULL)) {
        return -QM_EINVAL;
    }
    if (size == 0U) {
        /* 零长度不是 EOS 信号。 */
        return QM_EOK;
    }
    if (ctx->stopping) {
        return -QM_EINTR;
    }
    if (ctx->eof_set || ctx->result != DEC_RESULT_RUNNING) {
        return -QM_EIO;
    }
    /* 进入即开始计时: 后续背压等待与复制共用一个总预算。 */
    qm_utils_time_countdown_ms(&deadline, timeout_ms);

    /* 环满时背压等待解码器消费(fread/fseek 释放空间后会 signal data_sem);
     * 超时返回已接收前缀的字节数。 */
    while (done < size) {
        uint32_t avail;
        uint32_t space;
        uint32_t n;
        uint32_t idx;

        qm_mutex_lock(&ctx->fifo_lock, QM_WAIT_FOREVER);
        avail = ctx->source_pos - ctx->input_pos;
        /* A rewind into the prefix can put input_pos before the FIFO window.
         * Keep the producer blocked until replay catches up; never underflow. */
        space = avail < ctx->fifo_capacity ? ctx->fifo_capacity - avail : 0U;
        n = size - done;
        if (n > space) {
            n = space;
        }
        if (n > 0U) {
            /* Validate only the prefix that will actually be committed to
             * the FIFO. A push may be accepted partially under backpressure;
             * advancing the TOC cursor for an unaccepted suffix would make a
             * retry interpret the packet at the wrong byte offset. */
            if (ctx->framing == QM_AUDIO_DECODER_FRAMING_OPUS_CBR) {
                int ret = opus_cbr_validate_bytes(ctx,
                                                   (const uint8_t *)data + done,
                                                   n);
                if (ret != QM_EOK) {
                    qm_mutex_unlock(&ctx->fifo_lock);
                    return ret;
                }
            }
            {
                /* 头部缓存按"距流起点"的相对偏移写入(见 dec_vfs_read)。 */
                uint32_t cached_off = ctx->source_pos - ctx->base_offset;

                if (cached_off < ctx->rewind_cache_capacity) {
                    uint32_t cached = ctx->rewind_cache_capacity - cached_off;

                    if (cached > n) {
                        cached = n;
                    }
                    qm_memcpy(ctx->rewind_cache + cached_off,
                              (const u8 *)data + done, cached);
                    ctx->rewind_cache_len = cached_off + cached;
                }
            }
            idx = ctx->source_pos % ctx->fifo_capacity;
            if (idx + n <= ctx->fifo_capacity) {
                qm_memcpy(ctx->fifo + idx, (const u8 *)data + done, n);
            } else {
                uint32_t first = ctx->fifo_capacity - idx;

                qm_memcpy(ctx->fifo + idx, (const u8 *)data + done, first);
                qm_memcpy(ctx->fifo, (const u8 *)data + done + first, n - first);
            }
            ctx->source_pos += n;
            done += n;
            *consumed = done;
        }
        qm_mutex_unlock(&ctx->fifo_lock);
        if (n > 0U) {
            (void)qm_sem_signal(&ctx->data_sem);
        }
        if (done == size) {
            return QM_EOK;
        }
        if (ctx->stopping) {
            return -QM_EINTR;
        }
        {
            uint32_t left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
                            qm_utils_time_left(&deadline);

            if (left == 0U || qm_sem_wait(&ctx->data_sem, left) != QM_EOK) {
                return -QM_ETIMEOUT;
            }
        }
    }
    return QM_EOK;
}

int qm_audio_decoder_flush(void *backend_ctx, uint32_t timeout_ms)
{
    decoder_ctx_t *ctx = (decoder_ctx_t *)backend_ctx;
    qm_utils_time_t deadline;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }
    if (ctx->framing == QM_AUDIO_DECODER_FRAMING_OPUS_CBR &&
        ctx->source_pos % ctx->packet_bytes != 0U) {
        return -QM_EINVAL;
    }
    /* 进入即开始计时: 后续等待/检查都在同一总预算内。 */
    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    if (!ctx->eof_set) {
        /* 封口输入: 唤醒 VFS 读等待者观察 EOF, 并触发已知长度的 EOF 推断。 */
        ctx->eof_set = QM_TRUE;
        (void)qm_sem_signal(&ctx->data_sem);
        decoder_maybe_infer_eof(ctx);
        decoder_wake_rd(ctx);
    }
    for (;;) {
        uint32_t left;

        if (ctx->result == DEC_RESULT_END) {
            return QM_EOK;
        }
        if (ctx->result == DEC_RESULT_ERROR) {
            return ctx->server_error != 0 ? ctx->server_error : -QM_EIO;
        }
        if (ctx->result == DEC_RESULT_STOPPED) {
            return -QM_EINTR;
        }
        left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
               qm_utils_time_left(&deadline);
        if (left == 0U || qm_sem_wait(&ctx->done_sem, left) != QM_EOK) {
            return -QM_ETIMEOUT;
        }
    }
}

int qm_audio_decoder_get_duration(void *backend_ctx, uint32_t *duration_ms)
{
    decoder_ctx_t *ctx = backend_ctx;
    int ret;
    if (ctx == NULL || duration_ms == NULL) return -QM_EINVAL;
    qm_mutex_lock(&ctx->fifo_lock, QM_WAIT_FOREVER);
    ret = ctx->duration_valid ? QM_EOK : -QM_ENOSYS;
    if (ret == QM_EOK) *duration_ms = ctx->duration_ms;
    qm_mutex_unlock(&ctx->fifo_lock);
    return ret;
}

int qm_audio_decoder_capture_resume(void *backend_ctx,
                                    qm_audio_resume_point_t *resume,
                                    uint32_t timeout_ms)
{
    decoder_ctx_t *ctx = (decoder_ctx_t *)backend_ctx;
    qm_utils_time_t deadline;
    uint32_t gen;
    uint32_t left;
    int ret;

    if (ctx == NULL || resume == NULL) {
        return -QM_EINVAL;
    }
    /* 仅在成功时填充调用方记录：失败/超时绝不写回，调用方因此可原地保留旧记录。 */
    if (!ctx->opened || ctx->result != DEC_RESULT_RUNNING) {
        /* 仅运行中的会话可捕获: 未打开或已进入终态时无断点可续。 */
        return -QM_ENOSYS;
    }
    /* 请求由 worker 任务串行执行(与 vendor 请求同线程), 避免多任务并发访问
     * 解码器。先占号再置请求, 使 worker 读到的序号与本次请求一致。 */
    qm_mutex_lock(&ctx->fifo_lock, QM_WAIT_FOREVER);
    gen = ++ctx->resume_gen;
    ctx->resume_result = -QM_ENOSYS;
    ctx->resume_req = QM_TRUE;
    qm_mutex_unlock(&ctx->fifo_lock);
    (void)qm_sem_signal(&ctx->data_sem);
    decoder_wake_rd(ctx);

    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    for (;;) {
        /* 只接受本人序号的结果; 旧的超时请求若迟到回传, 会因序号不符被忽略,
         * 继续等待直到本次请求完成或超时。 */
        if (ctx->resume_done_gen == gen) {
            break;
        }
        left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
               qm_utils_time_left(&deadline);
        if (left == 0U || qm_sem_wait(&ctx->resume_sem, left) != QM_EOK) {
            /* 超时: 撤销请求。worker 之后即便处理也只写 ctx 自有的 scratch,
             * 不会触碰调用方缓冲。 */
            ctx->resume_req = QM_FALSE;
            return -QM_ETIMEOUT;
        }
    }
    qm_mutex_lock(&ctx->fifo_lock, QM_WAIT_FOREVER);
    ret = ctx->resume_result;
    if (ret == QM_EOK) {
        *resume = ctx->resume_scratch;
    }
    qm_mutex_unlock(&ctx->fifo_lock);
    return ret;
}

int qm_audio_decoder_close(void *backend_ctx)
{
    decoder_ctx_t *ctx = (decoder_ctx_t *)backend_ctx;
    int ret;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }
    if (!ctx->stopping) {
        ctx->stopping = QM_TRUE;
        /* 唤醒阻塞中的 VFS fread/fseek 与 worker 数据等待, 使其退出拉流。 */
        (void)qm_sem_signal(&ctx->data_sem);
        decoder_wake_rd(ctx);
    }
    /* 有界等待 worker 到达退出点(STOP/清理由 worker 收敛完成后自行发起):
     * 由 worker 在退出前 signal, 取代按固定周期轮询端口标志。 */
    ret = qm_sem_wait(&ctx->exit_sem, CONFIG_QM_AUDIO_DECODER_HW_STOP_TIMEOUT_MS);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "worker stop timeout ret=%d", ret);
        /* 保留状态, 允许调用方重试 close; 禁止释放上下文。 */
        return -QM_ETIMEOUT;
    }
    if (ctx->worker_task.hdl != NULL) {
        (void)qm_task_exit(&ctx->worker_task);
        ctx->worker_task.hdl = NULL;
    }
    if (ctx->server != NULL) {
        int argv[2];

        /* 摘除仍排在 app_core 队列、尚未执行的 END/ERR 通知, 使其不再回调
         * 并引用 ctx。参数必须与投递时一致: argc=2 且 argv[1]=req.dec.file。 */
        argv[1] = (int)ctx;
        argv[0] = AUDIO_SERVER_EVENT_END;
        (void)server_event_handler_del(ctx->server, 2, argv);
        argv[0] = AUDIO_SERVER_EVENT_ERR;
        (void)server_event_handler_del(ctx->server, 2, argv);
        server_close(ctx->server);
        ctx->server = NULL;
    }
    ctx->virtual_audio = NULL;
    /* 上面只覆盖未执行的通知; 已在 app_core 上执行中的回调撤不掉, 事件回调
     * 只读 ctx 的 volatile 字段并于指针判空后安全跳过; 留一个派发窗口再释放,
     * 降低回调与释放之间的竞争窗口。 */
    qm_msleep(20);
    decoder_destroy_resources(ctx);
    return QM_EOK;
}
