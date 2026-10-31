#include "boot_sound.h"

#include <string.h>

#include "opus.h"
#include "qm.h"
#include "qm_log.h"
#include "qm_audio.h"
#include "voice.h"
#include "agent_app.h"      /* 对话中先停掉 agent 输出，再自己播 */

#include "esp_heap_caps.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define LOG_TAG "boot_snd"

/*
 * 开机音效 / 闹铃提示音：16 kHz / 单声道 / 16 bit PCM，已嵌进固件。qm_audio 线上格式是
 * Opus，所以先把整段编好缓存。播放优先走 agent 的本地提示音通路，没有 agent 会话
 * （开机阶段）才自己申请 qm_audio 播放会话。
 */
#define BS_RATE_HZ          (16000)
#define BS_FRAME_SAMP       (320)   /* 20 ms */
#define BS_FRAME_BYTES      (BS_FRAME_SAMP * 2)
#define BS_OPUS_MAX         (512)

#define BS_FRAME_MAX_NUM    (256)

/* opus_encode 内部 alloca，参考 pcm_drv 的编码任务：栈从 PSRAM 拿 */
#define BS_STACK_BYTES      (30 * 1024)
#define BS_TASK_PRIO        (14)

#define BS_ACQUIRE_MS       (800)   /* 单次申请播放权的等待上限 */
#define BS_ACQUIRE_RETRY    (4)     /* 对端释放通路有延迟，失败重试几次 */
#define BS_ACQUIRE_GAP_MS   (250)
#define BS_WRITE_MS         (1000)
#define BS_DRAIN_MS         (7000)

/* target_add_binary_data(... RENAME_TO boot_sound_pcm / hurt_sound_N_pcm / death_sound_pcm / start_sound_pcm) 生成的符号 */
extern const uint8_t boot_sound_pcm_start[] asm("_binary_boot_sound_pcm_start");
extern const uint8_t boot_sound_pcm_end[] asm("_binary_boot_sound_pcm_end");
extern const uint8_t hurt_sound_0_pcm_start[] asm("_binary_hurt_sound_0_pcm_start");
extern const uint8_t hurt_sound_0_pcm_end[] asm("_binary_hurt_sound_0_pcm_end");
extern const uint8_t hurt_sound_1_pcm_start[] asm("_binary_hurt_sound_1_pcm_start");
extern const uint8_t hurt_sound_1_pcm_end[] asm("_binary_hurt_sound_1_pcm_end");
extern const uint8_t hurt_sound_2_pcm_start[] asm("_binary_hurt_sound_2_pcm_start");
extern const uint8_t hurt_sound_2_pcm_end[] asm("_binary_hurt_sound_2_pcm_end");
extern const uint8_t hurt_sound_3_pcm_start[] asm("_binary_hurt_sound_3_pcm_start");
extern const uint8_t hurt_sound_3_pcm_end[] asm("_binary_hurt_sound_3_pcm_end");
extern const uint8_t death_sound_pcm_start[] asm("_binary_death_sound_pcm_start");
extern const uint8_t death_sound_pcm_end[] asm("_binary_death_sound_pcm_end");
extern const uint8_t start_sound_pcm_start[] asm("_binary_start_sound_pcm_start");
extern const uint8_t start_sound_pcm_end[] asm("_binary_start_sound_pcm_end");

typedef enum {
    LOCAL_SOUND_BOOT = 0,
    LOCAL_SOUND_HURT_0 = 1,
    LOCAL_SOUND_HURT_1 = 2,
    LOCAL_SOUND_HURT_2 = 3,
    LOCAL_SOUND_HURT_3 = 4,
    LOCAL_SOUND_DEATH = 5,
    LOCAL_SOUND_START = 6,
    LOCAL_SOUND_COUNT
} local_sound_id_t;

#define LOCAL_HURT_COUNT    (4U)

typedef struct {
    const char *name;
    const uint8_t *pcm_start;
    const uint8_t *pcm_end;
    uint8_t *opus;
    uint32_t *opus_off;     /* frames + 1 个，末元素是总字节数 */
    uint16_t opus_frames;
} local_sound_cache_t;

static local_sound_cache_t s_sounds[LOCAL_SOUND_COUNT] = {
    [LOCAL_SOUND_BOOT] = {
        .name = "boot",
        .pcm_start = boot_sound_pcm_start,
        .pcm_end = boot_sound_pcm_end,
    },
    [LOCAL_SOUND_HURT_0] = {
        .name = "hurt_0",
        .pcm_start = hurt_sound_0_pcm_start,
        .pcm_end = hurt_sound_0_pcm_end,
    },
    [LOCAL_SOUND_HURT_1] = {
        .name = "hurt_1",
        .pcm_start = hurt_sound_1_pcm_start,
        .pcm_end = hurt_sound_1_pcm_end,
    },
    [LOCAL_SOUND_HURT_2] = {
        .name = "hurt_2",
        .pcm_start = hurt_sound_2_pcm_start,
        .pcm_end = hurt_sound_2_pcm_end,
    },
    [LOCAL_SOUND_HURT_3] = {
        .name = "hurt_3",
        .pcm_start = hurt_sound_3_pcm_start,
        .pcm_end = hurt_sound_3_pcm_end,
    },
    [LOCAL_SOUND_DEATH] = {
        .name = "death",
        .pcm_start = death_sound_pcm_start,
        .pcm_end = death_sound_pcm_end,
    },
    [LOCAL_SOUND_START] = {
        .name = "start",
        .pcm_start = start_sound_pcm_start,
        .pcm_end = start_sound_pcm_end,
    },
};

/* Protect the one-slot request mailbox and lazy task creation. The lock is
 * held only while publishing a request; encoding and playback stay unlocked. */
static portMUX_TYPE s_request_init_mux = portMUX_INITIALIZER_UNLOCKED;
static StaticSemaphore_t s_request_lock_buf;
static SemaphoreHandle_t s_request_lock;

static StaticTask_t s_tcb;
static StaticSemaphore_t s_sem_buf;
static SemaphoreHandle_t s_sem;
static uint8_t s_task_up;
static volatile local_sound_id_t s_req_sound = LOCAL_SOUND_BOOT;

static void boot_sound_task(void *arg);

/* 整段 PCM → Opus，每个音效只构建一次 */
static int boot_sound_build_opus(local_sound_cache_t *snd)
{
    static int16_t frame[BS_FRAME_SAMP];
    const uint8_t *pcm = snd->pcm_start;
    uint32_t total = (uint32_t)(snd->pcm_end - snd->pcm_start);
    uint32_t frames = total / BS_FRAME_BYTES;
    OpusEncoder *enc;
    uint32_t pcm_off = 0;
    uint32_t opus_off = 0;
    uint16_t i;
    int err = 0;

    if (snd->opus != NULL) {
        return QM_EOK;
    }
    if (frames == 0U || frames > BS_FRAME_MAX_NUM) {
        QM_LOGW(LOG_TAG, "%s pcm=%u bytes frames=%u out of range",
                snd->name, (unsigned)total, (unsigned)frames);
        return -QM_EINVAL;
    }

    snd->opus = (uint8_t *)heap_caps_malloc(frames * BS_OPUS_MAX,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    snd->opus_off = (uint32_t *)heap_caps_malloc((frames + 1U) * sizeof(uint32_t),
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (snd->opus == NULL || snd->opus_off == NULL) {
        QM_LOGW(LOG_TAG, "%s opus cache alloc failed", snd->name);
        goto fail;
    }

    enc = opus_encoder_create(BS_RATE_HZ, 1, OPUS_APPLICATION_VOIP, &err);
    if (enc == NULL) {
        QM_LOGW(LOG_TAG, "encoder create failed err=%d", err);
        goto fail;
    }

    for (i = 0U; i < (uint16_t)frames; i++) {
        int n;

        /* 嵌进 flash 的数据不保证 2 字节对齐，先拷到对齐缓冲 */
        memcpy(frame, pcm + pcm_off, BS_FRAME_BYTES);
        n = opus_encode(enc, frame, BS_FRAME_SAMP, snd->opus + opus_off, BS_OPUS_MAX);
        if (n <= 0) {
            QM_LOGW(LOG_TAG, "encode failed n=%d frame=%u", n, (unsigned)i);
            opus_encoder_destroy(enc);
            goto fail;
        }
        snd->opus_off[i] = opus_off;
        opus_off += (uint32_t)n;
        pcm_off += BS_FRAME_BYTES;
    }
    opus_encoder_destroy(enc);

    snd->opus_off[frames] = opus_off;
    snd->opus_frames = (uint16_t)frames;
    QM_LOGI(LOG_TAG, "%s encoded %u frames / %u bytes opus", snd->name,
            (unsigned)frames, (unsigned)opus_off);
    return QM_EOK;

fail:
    heap_caps_free(snd->opus);
    heap_caps_free(snd->opus_off);
    snd->opus = NULL;
    snd->opus_off = NULL;
    return -QM_ERROR;
}

/* 自己的 Control，按包写已编好的 Opus。不占用 agent 那份 Control。 */
static void boot_sound_play_local(const local_sound_cache_t *snd)
{
    qm_audio_control_t *control = NULL;
    qm_audio_control_play_request_t req;
    qm_audio_operation_id_t operation = 0;
    uint16_t i;
    int ret;
    int attempt;

    if (!voice_audio_ready()) {
        QM_LOGW(LOG_TAG, "audio not ready, skip");
        return;
    }

    ret = qm_audio_control_create(&control);
    if (ret != QM_EOK || control == NULL) {
        QM_LOGW(LOG_TAG, "control create failed ret=%d", ret);
        return;
    }

    memset(&req, 0, sizeof(req));
    req.source.type = QM_AUDIO_SOURCE_PUSH;
    req.source.value.push.buffer_bytes = 4096;
    req.codec.codec = QM_AUDIO_CODEC_OPUS;
    req.codec.framing = QM_AUDIO_CODEC_FRAMING_PACKET;
    req.codec.packet_duration_ms = 20;
    req.codec.packet_bytes = 0;
    req.codec.pcm.sample_rate_hz = BS_RATE_HZ;
    req.codec.pcm.channels = 1;
    req.codec.pcm.sample = QM_AUDIO_PCM_S16;
    req.priority = QM_AUDIO_PRIORITY_CRITICAL;
    req.admission = QM_AUDIO_ADMISSION_PREEMPT_LOWER;
    req.admission_timeout_ms = BS_ACQUIRE_MS;

    ret = qm_audio_control_play(control, &req, &operation);
    for (attempt = 1; ret != QM_EOK && attempt < BS_ACQUIRE_RETRY; attempt++) {
        QM_LOGW(LOG_TAG, "play request failed ret=%d, retry %d", ret, attempt);
        vTaskDelay(pdMS_TO_TICKS(BS_ACQUIRE_GAP_MS));
        ret = qm_audio_control_play(control, &req, &operation);
    }
    if (ret != QM_EOK) {
        QM_LOGW(LOG_TAG, "play request failed ret=%d (give up)", ret);
        (void)qm_audio_control_shutdown(control, 1000);
        (void)qm_audio_control_destroy(control);
        return;
    }

    for (i = 0U; i < snd->opus_frames; i++) {
        qm_audio_data_t packet;
        uint32_t off = snd->opus_off[i];
        uint32_t len = snd->opus_off[i + 1U] - off;

        memset(&packet, 0, sizeof(packet));
        packet.data = snd->opus + off;
        packet.size = len;
        packet.duration_ms = 20;
        packet.flags = QM_AUDIO_DATA_DURATION_VALID;
        ret = qm_audio_control_stream_write_packet(control, operation, &packet, BS_WRITE_MS);
        if (ret != QM_EOK) {
            QM_LOGW(LOG_TAG, "write failed ret=%d frame=%u", ret, (unsigned)i);
            break;
        }
    }

    if (ret == QM_EOK) {
        ret = qm_audio_control_stream_finish(control, operation);
    }
    if (ret == QM_EOK) {
        ret = qm_audio_control_wait(control, operation, BS_DRAIN_MS);
    }
    QM_LOGI(LOG_TAG, "%s local play done ret=%d frames=%u/%u",
            snd->name, ret, (unsigned)i, (unsigned)snd->opus_frames);
    /* A terminal operation can still have a short-lived event/API reference.
     * Let that reference drain before tearing down this private Control. */
    for (attempt = 0; attempt < 10; attempt++) {
        ret = qm_audio_control_release(control, operation, 1000);
        if (ret == QM_EOK) break;
        if (ret != -QM_EBUSY) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (ret != QM_EOK) {
        /* Give the Control event worker a little more time, but never shut down
         * before the operation is released: shutdown intentionally rejects
         * further release attempts. */
        for (attempt = 0; attempt < 4 && ret == -QM_EBUSY; attempt++) {
            vTaskDelay(pdMS_TO_TICKS(50));
            ret = qm_audio_control_release(control, operation, 1000);
        }
    }
    if (ret == QM_EOK) {
        (void)qm_audio_control_shutdown(control, 1000);
        (void)qm_audio_control_destroy(control);
    } else {
        QM_LOGW(LOG_TAG, "%s release deferred ret=%d", snd->name, ret);
    }
}

/* 播一遍；编不出来就直接放弃，不再去抢通路 */
static void boot_sound_play_once(local_sound_id_t id)
{
    local_sound_cache_t *snd;

    if (id < 0 || id >= LOCAL_SOUND_COUNT) {
        return;
    }
    snd = &s_sounds[id];
    if (boot_sound_build_opus(snd) != QM_EOK) {
        QM_LOGW(LOG_TAG, "%s no opus cache, skip", snd->name);
        return;
    }

    if (agent_app_is_active()) {
        (void)agent_app_prepare_for_local_sound(600U);
    }

    QM_LOGI(LOG_TAG, "play %s (%u ms local)", snd->name,
            (unsigned)((uint32_t)snd->opus_frames * 20U));
    boot_sound_play_local(snd);
}

/* 常驻任务：等信号量 → 播一遍；任务不退出，避免反复建/删任务的资源风险 */
static void boot_sound_task(void *arg)
{
    (void)arg;

    for (;;) {
        if (xSemaphoreTake(s_sem, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        /* Snapshot the request: another producer can update s_req_sound
         * while this sound is playing. */
        local_sound_id_t id;
        if (xSemaphoreTake(s_request_lock, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        id = s_req_sound;
        (void)xSemaphoreGive(s_request_lock);
        boot_sound_play_once(id);
        if (xSemaphoreTake(s_request_lock, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        local_sound_id_t pending = s_req_sound;
        (void)xSemaphoreGive(s_request_lock);
        if (id == LOCAL_SOUND_DEATH || pending != LOCAL_SOUND_DEATH) {
            agent_app_set_local_feedback_mute(QM_FALSE);
        }
        /* The single pending request can merge/replace a death sound. Run
         * recovery for every completion, including encoding/playback failure,
         * so cleanup does not depend on which sound survived the merge.
         * The agent helper preserves the mute during active focus. */
        agent_app_restore_idle_pomodoro_tts();
    }
}

static qm_err_t local_sound_request(local_sound_id_t id)
{
    static StackType_t *stack;

    if (s_request_lock == NULL) {
        portENTER_CRITICAL(&s_request_init_mux);
        if (s_request_lock == NULL) {
            s_request_lock = xSemaphoreCreateMutexStatic(&s_request_lock_buf);
        }
        portEXIT_CRITICAL(&s_request_init_mux);
    }
    if (s_request_lock == NULL) {
        return -QM_ENOMEM;
    }
    if (xSemaphoreTake(s_request_lock, portMAX_DELAY) != pdTRUE) {
        return -QM_EBUSY;
    }

    if (s_task_up == 0) {
        stack = (StackType_t *)heap_caps_malloc(BS_STACK_BYTES,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (stack == NULL) {
            QM_LOGE(LOG_TAG, "stack alloc failed");
            (void)xSemaphoreGive(s_request_lock);
            return -QM_ENOMEM;
        }
        s_sem = xSemaphoreCreateBinaryStatic(&s_sem_buf);
        if (s_sem == NULL) {
            QM_LOGE(LOG_TAG, "sem create failed");
            heap_caps_free(stack);
            (void)xSemaphoreGive(s_request_lock);
            return -QM_ENOMEM;
        }
        if (xTaskCreateStatic(boot_sound_task, "boot_snd",
                              BS_STACK_BYTES / sizeof(StackType_t),
                              NULL, BS_TASK_PRIO, stack, &s_tcb) == NULL) {
            QM_LOGE(LOG_TAG, "task create failed");
            heap_caps_free(stack);
            (void)xSemaphoreGive(s_request_lock);
            return -QM_ENOMEM;
        }
        s_task_up = 1;
    }

    s_req_sound = id;
    SemaphoreHandle_t sem = s_sem;
    (void)xSemaphoreGive(s_request_lock);
    /* 二值信号量已满时合并请求，待播项已更新为最新 id；不打断当前播放。 */
    if (xSemaphoreGive(sem) != pdTRUE) {
        QM_LOGD(LOG_TAG, "busy, request merged");
    }
    return QM_EOK;
}

qm_err_t boot_sound_start(void)
{
    return local_sound_request(LOCAL_SOUND_BOOT);
}

qm_err_t hurt_sound_start(void)
{
    static uint8_t s_last_idx = 0xFF;
    uint32_t r = esp_random();
    uint8_t idx = (uint8_t)(r % LOCAL_HURT_COUNT);

    /* 伪随机：尽量避免连续两次播放相同的扣血音效，提升听感丰富度 */
    if (idx == s_last_idx) {
        idx = (uint8_t)((idx + 1U + ((r >> 8) % (LOCAL_HURT_COUNT - 1U))) % LOCAL_HURT_COUNT);
    }
    s_last_idx = idx;

    QM_LOGI(LOG_TAG, "hurt sound pick variant=%u", (unsigned)idx);
    return local_sound_request((local_sound_id_t)(LOCAL_SOUND_HURT_0 + idx));
}

qm_err_t hurt_sound_start_variant(uint8_t variant)
{
    if (variant >= LOCAL_HURT_COUNT) {
        variant = 0;
    }
    QM_LOGI(LOG_TAG, "hurt sound direct variant=%u", (unsigned)variant);
    return local_sound_request((local_sound_id_t)(LOCAL_SOUND_HURT_0 + variant));
}

qm_err_t death_sound_start(void)
{
    qm_err_t ret;
    QM_LOGI(LOG_TAG, "death sound requested");
#if defined(CONFIG_QM_POMODORO_SUPPORT) && CONFIG_QM_POMODORO_SUPPORT
    /* 暂时静音云端下行；音效任务退出播放后恢复非专注状态的语音。 */
    agent_app_set_local_feedback_mute(QM_TRUE);
    agent_app_set_pomodoro_tts_mute(QM_TRUE);
#endif
    ret = local_sound_request(LOCAL_SOUND_DEATH);
    if (ret != QM_EOK) {
        agent_app_set_local_feedback_mute(QM_FALSE);
    }
    return ret;
}

qm_err_t pomodoro_start_sound_start(void)
{
    QM_LOGI(LOG_TAG, "pomodoro start sound requested");
    return local_sound_request(LOCAL_SOUND_START);
}
