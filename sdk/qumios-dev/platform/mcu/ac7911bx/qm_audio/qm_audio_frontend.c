#define BOOL_DEFINE_CONFLICT
#define CONFIG_QM_WITHOUT_STDIO_H_SUPPORT 1

/*
 * qm_audio_frontend.c
 * 语音前端(唤醒词检测)平台实现：当前绑定 Huwen 厂商算法(编译期)。
 *
 * 对外只实现 adapter/qm_audio_frontend.h 的四个命名接口；厂商库、采集、
 * 模型路径均为平台内部细节，应用不可见。
 *
 * 数据流：
 * - 编码回调(audio_server "enc" vfs fwrite) 经异步队列喂给厂商算法；
 * - 厂商回调 flag=0 输出采集 PCM，经平台统一 capture_submit 入口进入
 *   PUSH/PULL 采集链路，应用经 Capture 订阅拉流；
 * - 厂商回调 flag=1 为唤醒成功，经 qm_audio_report_wake() 进入统一
 *   Control 事件链路，应用通过 qm_audio_wake_subscribe() 接收。
 *
 * MODE 只控制唤醒检测启停；采集数据放行/上行门控在消费侧，不在此处。
 */
#include "adapter/qm_audio_frontend.h"
#include "qm_config.h"
#include "qm_audio.h"
#include "qm_string.h"
#include "app_config.h"
#include "server/audio_server.h"
#include "server/server_core.h"
#include "generic/circular_buf.h"
#include "os/os_api.h"
#include "spinlock.h"

#include "qm_audio_device_priv.h"

/* 厂商库接口(静态链接) */
extern int HuwenWakeupTaskInit(int mpo_dp, void *callback, char *filename);
extern int HuwenWakeupTaskUnInit(void);
extern int huwenSendRecordData(char *buffer, int length);
extern int huwenSendLineInData(char *buffer, int length);

#ifndef CONFIG_QM_AUDIO_FRONTEND_MODEL_PATH
#define CONFIG_QM_AUDIO_FRONTEND_MODEL_PATH "mnt/sdfile/res/cfg/words.txt"
#endif

/* ---------- 喂厂商库：异步队列 + 任务(避免编码回调阻塞) ---------- */
#define HUWEN_LIB_PCM_MAX_POINTS       1024
#define HUWEN_LIB_PCM_FRAME_POINTS     960
#define HUWEN_LIB_PCM_QUEUE_BYTES      (HUWEN_LIB_PCM_MAX_POINTS * 2 * sizeof(s16) * 4)
#define HUWEN_LIB_PCM_TASK_STACK_SIZE  1024
#define HUWEN_LIB_PCM_TASK_PRIO        CONFIG_QM_PRIO_HUWEN_FEED

static volatile int s_lib_pcm_inited = 0;
static volatile int s_lib_pcm_enabled = 0;
static volatile int s_lib_pcm_feeding = 0;
static cbuffer_t s_lib_pcm_cbuf;
static OS_SEM s_lib_pcm_sem;
static spinlock_t s_lib_pcm_lock;
static u8 s_lib_pcm_buf[HUWEN_LIB_PCM_QUEUE_BYTES] ALIGNE(4);
static s16 s_lib_pair_frame[HUWEN_LIB_PCM_MAX_POINTS * 2] ALIGNE(4);
static s16 s_lib_record_frame[HUWEN_LIB_PCM_MAX_POINTS] ALIGNE(4);
static s16 s_lib_linein_frame[HUWEN_LIB_PCM_MAX_POINTS] ALIGNE(4);

static void huwen_lib_pcm_clear(void)
{
    if (!s_lib_pcm_inited) {
        return;
    }
    spin_lock(&s_lib_pcm_lock);
    cbuf_clear(&s_lib_pcm_cbuf);
    spin_unlock(&s_lib_pcm_lock);
}

static void huwen_lib_pcm_task(void *priv)
{
    (void)priv;
    while (1) {
        os_sem_pend(&s_lib_pcm_sem, 10);

        while (s_lib_pcm_enabled) {
            u32 avail;
            u32 read_bytes;
            u32 points;
            u32 i;

            spin_lock(&s_lib_pcm_lock);
            avail = cbuf_get_data_size(&s_lib_pcm_cbuf);
            if (avail < (HUWEN_LIB_PCM_FRAME_POINTS * 2 * sizeof(s16))) {
                spin_unlock(&s_lib_pcm_lock);
                break;
            }
            read_bytes = cbuf_read(&s_lib_pcm_cbuf, s_lib_pair_frame,
                                   HUWEN_LIB_PCM_FRAME_POINTS * 2 * sizeof(s16));
            spin_unlock(&s_lib_pcm_lock);
            if (read_bytes == 0) {
                break;
            }

            points = read_bytes / (2 * sizeof(s16));
            for (i = 0; i < points; i++) {
                s_lib_record_frame[i] = s_lib_pair_frame[i * 2];
                s_lib_linein_frame[i] = s_lib_pair_frame[i * 2 + 1];
            }
            /* 保持厂商库要求的顺序：麦克风帧先、匹配的 AEC 参考帧紧跟。 */
            s_lib_pcm_feeding = 1;
            if (s_lib_pcm_enabled) {
                (void)huwenSendRecordData((char *)s_lib_record_frame,
                                          points * sizeof(s16));
                (void)huwenSendLineInData((char *)s_lib_linein_frame,
                                          points * sizeof(s16));
            }
            s_lib_pcm_feeding = 0;
        }

        if (!s_lib_pcm_enabled) {
            huwen_lib_pcm_clear();
        }
    }
}

static int huwen_lib_pcm_init(void)
{
    int ret;

    if (s_lib_pcm_inited) {
        return 0;
    }
    cbuf_init(&s_lib_pcm_cbuf, s_lib_pcm_buf, sizeof(s_lib_pcm_buf));
    spin_lock_init(&s_lib_pcm_lock);
    if (os_sem_create(&s_lib_pcm_sem, 0) != 0) {
        printf("[FRONTEND] lib pcm sem create failed\n");
        return -1;
    }
    s_lib_pcm_inited = 1;
    ret = thread_fork("frontend_lib_tx", HUWEN_LIB_PCM_TASK_PRIO,
                      HUWEN_LIB_PCM_TASK_STACK_SIZE, 0, NULL,
                      huwen_lib_pcm_task, NULL);
    if (ret != 0) {
        s_lib_pcm_inited = 0;
        (void)os_sem_del(&s_lib_pcm_sem, OS_DEL_ALWAYS);
        printf("[FRONTEND] lib pcm task create failed ret=%d\n", ret);
        return -1;
    }
    return 0;
}

static void huwen_lib_pcm_set_enable(int enable)
{
    if (enable) {
        if (!s_lib_pcm_inited) {
            (void)huwen_lib_pcm_init();
        }
        huwen_lib_pcm_clear();
        s_lib_pcm_enabled = s_lib_pcm_inited ? 1 : 0;
        if (s_lib_pcm_inited) {
            os_sem_post(&s_lib_pcm_sem);
        }
        return;
    }

    s_lib_pcm_enabled = 0;
    if (s_lib_pcm_inited) {
        os_sem_post(&s_lib_pcm_sem);
    }
    /* 让任务消化掉已出队的一帧后再清队列，避免半帧拆分。 */
    while (s_lib_pcm_feeding) {
        os_time_dly(1);
    }
    huwen_lib_pcm_clear();
}

static int huwen_lib_pcm_enqueue(const s16 *pair_pcm, u32 points)
{
    u32 bytes;
    u32 written;
    u32 was_empty;

    if (!s_lib_pcm_inited || !s_lib_pcm_enabled ||
        pair_pcm == NULL || points == 0) {
        return 0;
    }
    bytes = points * 2 * sizeof(s16);
    spin_lock(&s_lib_pcm_lock);
    was_empty = (cbuf_get_data_size(&s_lib_pcm_cbuf) == 0);
    if (cbuf_is_write_able(&s_lib_pcm_cbuf, bytes) == 0) {
        cbuf_clear(&s_lib_pcm_cbuf);
        was_empty = 1;
    }
    written = cbuf_write(&s_lib_pcm_cbuf, (void *)pair_pcm, bytes);
    spin_unlock(&s_lib_pcm_lock);
    if (written > 0 && was_empty) {
        os_sem_post(&s_lib_pcm_sem);
    }
    return (int)(written / (2 * sizeof(s16)));
}

/* ---------- 采集：audio_server "enc" + vfs 回调 ---------- */
static struct server *s_enc_server = NULL;

static s16 s_mic_frame[1024] ALIGNE(4);
static s16 s_ref_frame[1024] ALIGNE(4);
static s16 s_pair_frame[2048] ALIGNE(4);

static int huwen_vfs_fwrite(void *file, void *data, u32 len)
{
    s16 *data0 = (s16 *)data;
    u32 points = len / (2 * 2);
    u32 i;

    (void)file;
    if (points > 1024) {
        points = 1024;
    }
    for (i = 0; i < points; i++) {
        /*
         * JL 音频编码器按 MIC 索引升序交织。本板：
         *   MIC1 (LINEIN, AEC 回采) → 偶数位；MIC3 (MIC3P 真麦) → 奇数位。
         * s_mic_frame 必须是真麦(送 KWS/AEC)，s_ref_frame 是 AEC 参考。
         */
        s_mic_frame[i] = data0[i * 2 + 1];  /* MIC3P (real mic) */
        s_ref_frame[i] = data0[i * 2];      /* MIC1  (AEC ref)  */
        s_pair_frame[i * 2] = s_mic_frame[i];
#if CONFIG_HUWEN_AEC_REF_ZERO_ENABLE
        s_pair_frame[i * 2 + 1] = 0;
#else
        s_pair_frame[i * 2 + 1] = s_ref_frame[i];
#endif
    }
    /* 异步喂厂商库；回调保持浅。 */
    (void)huwen_lib_pcm_enqueue(s_pair_frame, points);
    return len;
}

static int huwen_vfs_fclose(void *file)
{
    (void)file;
    return 0;
}

static const struct audio_vfs_ops s_frontend_vfs_ops = {
    .fwrite = huwen_vfs_fwrite,
    .fclose = huwen_vfs_fclose,
};

static int huwen_capture_start(void)
{
    union audio_req req;

    if (s_enc_server != NULL) {
        return QM_EOK;
    }
    s_enc_server = server_open("audio_server", "enc");
    if (s_enc_server == NULL) {
        printf("[FRONTEND] encoder open failed\n");
        return -QM_EIO;
    }
    qm_memset(&req, 0, sizeof(req));
    req.enc.cmd = AUDIO_ENC_OPEN;
    req.enc.channel = 2;
    req.enc.volume = CONFIG_AISP_MIC_ADC_GAIN;
    req.enc.output_buf = NULL;
    req.enc.output_buf_len = 4 * 1024 * req.enc.channel;
    req.enc.sample_rate = 16000;
    req.enc.format = "pcm";
    req.enc.frame_size = 1920 * req.enc.channel;
    req.enc.sample_source = "mic";
    req.enc.vfs_ops = &s_frontend_vfs_ops;
    req.enc.msec = 0;
    req.enc.channel_bit_map = BIT(CONFIG_AISP_MIC0_ADC_CHANNEL) |
                              BIT(CONFIG_AISP_LINEIN_ADC_CHANNEL);
    if (server_request(s_enc_server, AUDIO_REQ_ENC, &req) != 0) {
        printf("[FRONTEND] encoder open request failed\n");
        server_close(s_enc_server);
        s_enc_server = NULL;
        return -QM_EIO;
    }
    extern void adc_multiplex_set_gain(const char *source, u8 channel_bit_map, u8 gain);
    adc_multiplex_set_gain("mic", BIT(CONFIG_AISP_MIC0_ADC_CHANNEL),
                           CONFIG_AISP_MIC_ADC_GAIN);
    adc_multiplex_set_gain("mic", BIT(CONFIG_AISP_LINEIN_ADC_CHANNEL),
                           CONFIG_AISP_LINEIN_ADC_GAIN);
    return QM_EOK;
}

static void huwen_capture_stop(void)
{
    union audio_req req;

    if (s_enc_server == NULL) {
        return;
    }
    qm_memset(&req, 0, sizeof(req));
    req.enc.cmd = AUDIO_ENC_CLOSE;
    (void)server_request(s_enc_server, AUDIO_REQ_ENC, &req);
    server_close(s_enc_server);
    s_enc_server = NULL;
}

/* ---------- 厂商回调：flag=0 采集数据，flag=1 唤醒 ---------- */
static void huwen_frontend_event_cb(void *data, int len, int flag)
{
    if (flag == 0) {
        if (data != NULL && len >= (int)sizeof(int16_t) &&
            len % (int)sizeof(int16_t) == 0) {
            qm_audio_data_t input = {0};
            input.data = data;
            input.size = (uint32_t)len;
            (void)qm_audio_device_capture_submit(&input);
        }
        return;
    }
    if (flag == 1) {
        qm_audio_wake_info_t wake = {0};
        wake.source = 1U;
        wake.reason = 1U;
        (void)qm_audio_report_wake(&wake);
        return;
    }
    /* 未知事件：打印兜底，避免厂商状态变化被静默吞掉。 */
    printf("[FRONTEND] huwen event flag=%d\n", flag);
}

/* ---------- 对外接口 ---------- */
static volatile int s_opened = 0;
static qm_audio_frontend_mode_t s_mode = QM_AUDIO_FRONTEND_MODE_WAKEUP;

int qm_audio_frontend_open(void)
{
    if (s_opened) {
        return -QM_EBUSY;
    }
    if (huwen_lib_pcm_init() != 0) {
        return -QM_ENOMEM;
    }
    if (huwen_capture_start() != QM_EOK) {
        return -QM_EIO;
    }
    /* 采集稳定后再加载模型并启动唤醒任务，避免评估瞬态首帧。 */
    os_time_dly(10);
    if (HuwenWakeupTaskInit(6, huwen_frontend_event_cb,
                            CONFIG_QM_AUDIO_FRONTEND_MODEL_PATH) != 0) {
        huwen_capture_stop();
        return -QM_EIO;
    }
    os_time_dly(20);
    s_opened = 1;
    s_mode = QM_AUDIO_FRONTEND_MODE_WAKEUP;
    huwen_lib_pcm_set_enable(1); /* 默认 MODE_WAKEUP：唤醒检测运行 */
    return QM_EOK;
}

int qm_audio_frontend_close(void)
{
    if (!s_opened) {
        return QM_EOK;
    }
    huwen_lib_pcm_set_enable(0);
    (void)HuwenWakeupTaskUnInit();
    os_time_dly(5);
    huwen_capture_stop();
    s_opened = 0;
    s_mode = QM_AUDIO_FRONTEND_MODE_WAKEUP;
    return QM_EOK;
}

int qm_audio_frontend_set_mode(qm_audio_frontend_mode_t mode)
{
    if (mode != QM_AUDIO_FRONTEND_MODE_WAKEUP &&
        mode != QM_AUDIO_FRONTEND_MODE_DIALOG) {
        return -QM_EINVAL;
    }
    if (!s_opened) {
        return -QM_ENOSYS;
    }
    s_mode = mode;
    huwen_lib_pcm_set_enable(mode == QM_AUDIO_FRONTEND_MODE_WAKEUP ? 1 : 0);
    return QM_EOK;
}

qm_audio_frontend_mode_t qm_audio_frontend_get_mode(void)
{
    return s_mode;
}
