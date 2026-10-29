/**
 * @file qm_agent_event.c
 * @brief Event ownership, public notice construction, and callback dispatch.
 *
 * 事件所有权、公共通知构造和回调分发。适配器上报的指针载荷只在回调期间有效；本文件在
 * 需要跨队列或跨任务传递时深拷贝数据，并将低频公共事件转换为无指针通知。
 */

#include "qm_agent_internal.h"

QM_EVENT_DEFINE_BASE(QM_AGENT_EVENT_BASE);

/* The identifier is copied into queued notices, so it must not be derived
 * from a session pointer that can be reused after destruction.
 * 标识符会被复制到排队通知中，因此不能由销毁后可能复用的会话指针推导。 */
static volatile uint32_t g_next_agent_id = 1U;

/**
 * @brief Map a known expression to its stable top-level category. 将已知表情映射为稳定的一级分类。
 *
 * @param[in] expression Provider-neutral facial expression. 供应商无关的面部表情。
 * @return Expression category, or UNKNOWN when no mapping exists. 表情一级分类；无映射时返回 UNKNOWN。
 */
qm_agent_expression_category_t qm_agent_expression_classify(
    qm_agent_expression_t expression)
{
    switch (expression) {
        case QM_AGENT_EXPRESSION_NEUTRAL:
        case QM_AGENT_EXPRESSION_CALM:
            return QM_AGENT_EXPR_CATEGORY_FACIAL_STATE;
        case QM_AGENT_EXPRESSION_HAPPY:
        case QM_AGENT_EXPRESSION_BORED:
        case QM_AGENT_EXPRESSION_SAD:
        case QM_AGENT_EXPRESSION_ANGRY:
        case QM_AGENT_EXPRESSION_FEAR:
        case QM_AGENT_EXPRESSION_SURPRISED:
        case QM_AGENT_EXPRESSION_ANXIOUS:
        case QM_AGENT_EXPRESSION_EXCITED:
        case QM_AGENT_EXPRESSION_DISAPPOINTED:
        case QM_AGENT_EXPRESSION_DISGUSTED:
            return QM_AGENT_EXPR_CATEGORY_AFFECT;
        case QM_AGENT_EXPRESSION_EXPECTANT:
        case QM_AGENT_EXPRESSION_CURIOUS:
        case QM_AGENT_EXPRESSION_CONFUSED:
        case QM_AGENT_EXPRESSION_HESITANT:
        case QM_AGENT_EXPRESSION_SHY:
        case QM_AGENT_EXPRESSION_AFFECTIONATE:
            return QM_AGENT_EXPR_CATEGORY_SOCIAL_COGNITIVE;
        case QM_AGENT_EXPRESSION_UNWELL:
            return QM_AGENT_EXPR_CATEGORY_PHYSICAL_STATE;
        case QM_AGENT_EXPRESSION_UNKNOWN:
        default:
            return QM_AGENT_EXPR_CATEGORY_UNKNOWN;
    }
}

/**
 * @brief Resolve the main category carried by an expression event. 解析表情事件的主分类。
 *
 * An adapter-provided category takes precedence so a future provider code can
 * retain its known broad meaning without first adding a concrete enum. When
 * absent, a gesture or known visual expression supplies the fallback category.
 * 适配层显式填写的分类优先，使未来供应商编码无需先增加具体枚举也能保留语义；
 * 未填写时由动作或已知面部表情推导回退分类。
 *
 * @param[in] event Normalized expression event. 标准化后的表情事件。
 * @return Resolved main category. 解析后的主分类。
 */
static qm_agent_expression_category_t expression_category(
    const qm_agent_event_t *event)
{
    qm_agent_expression_category_t category;

    if (event->data.expression.category != QM_AGENT_EXPR_CATEGORY_UNKNOWN) {
        return event->data.expression.category;
    }
    if (event->data.expression.gesture != QM_AGENT_GESTURE_NONE) {
        return QM_AGENT_EXPR_CATEGORY_GESTURE;
    }
    category = qm_agent_expression_classify(
        event->data.expression.visual);
    return category;
}

/**
 * @brief Resolve the optional speech-expression category. 解析可选语音表情的分类。
 *
 * @param[in] event Normalized expression event. 标准化后的表情事件。
 * @return Resolved speech category. 解析后的语音表情分类。
 */
static qm_agent_expression_category_t expression_speech_category(
    const qm_agent_event_t *event)
{
    if (event->data.expression.speech_category !=
        QM_AGENT_EXPR_CATEGORY_UNKNOWN) {
        return event->data.expression.speech_category;
    }
    return qm_agent_expression_classify(event->data.expression.speech_expression);
}

static uint32_t next_agent_id(void)
{
    uint32_t agent_id;

    do {
        agent_id = __sync_fetch_and_add(&g_next_agent_id, 1U);
    } while (agent_id == 0U);
    return agent_id;
}

/**
 * @brief Event ownership helpers.
 * 事件所有权辅助函数。
 *
 * Synchronous callbacks borrow provider payloads until dispatch returns.
 * Applications may explicitly clone an event when retaining it is necessary.
 * 同步回调仅在分发返回前借用供应商载荷；需要跨越回调保留时，必须显式克隆事件。
 */

static void clear_payload(qm_agent_event_t *event)
{
    if (event == NULL) {
        return;
    }

    switch (event->id) {
        case QM_AGENT_EVENT_AUDIO:
            qm_free((void *)event->data.audio.data);
            event->data.audio.data = NULL;
            break;
        case QM_AGENT_EVENT_VIDEO:
            qm_free((void *)event->data.video.data);
            event->data.video.data = NULL;
            break;
        case QM_AGENT_EVENT_ASR:
            qm_free((void *)event->data.asr.text);
            event->data.asr.text = NULL;
            break;
        case QM_AGENT_EVENT_TTS:
            qm_free((void *)event->data.tts.text);
            event->data.tts.text = NULL;
            break;
        case QM_AGENT_EVENT_AGENT_TEXT:
            qm_free((void *)event->data.agent_text.text);
            event->data.agent_text.text = NULL;
            break;
        case QM_AGENT_EVENT_COMMAND:
            /* Command events cross the transport/control-task boundary. Both
             * strings are independently owned by the clone. 指令事件跨越传输与
             * 控制任务边界，克隆后两个字符串均由事件独立持有。 */
            qm_free((void *)event->data.command.domain);
            qm_free((void *)event->data.command.json);
            event->data.command.domain = NULL;
            event->data.command.json = NULL;
            break;
        case QM_AGENT_EVENT_EXPRESSION:
            qm_free((void *)event->data.expression.provider_code);
            qm_free((void *)event->data.expression.provider_speech_code);
            event->data.expression.provider_code = NULL;
            event->data.expression.provider_speech_code = NULL;
            break;
        case QM_AGENT_EVENT_AGENT_STATE:
            qm_free((void *)event->data.agent_state.text);
            event->data.agent_state.text = NULL;
            break;
        case QM_AGENT_EVENT_ERROR:
            qm_free((void *)event->data.error.message);
            event->data.error.message = NULL;
            break;
        default:
            break;
    }
}

static int clone_string(const char *src, const char **dst)
{
    char *copy;

    if (dst == NULL) {
        return -QM_EINVAL;
    }

    *dst = NULL;
    if (src == NULL) {
        return QM_EOK;
    }

    /** @brief qm_strdup keeps text-event cloning aligned with QUMI memory hooks. */
    copy = qm_strdup(src);
    if (copy == NULL) {
        return -QM_ENOMEM;
    }

    *dst = copy;
    return QM_EOK;
}

static int clone_bytes(const void *src, uint32_t len, const uint8_t **dst)
{
    uint8_t *copy;

    if (dst == NULL) {
        return -QM_EINVAL;
    }

    *dst = NULL;
    if (len == 0U) {
        return QM_EOK;
    }
    if (src == NULL) {
        return -QM_EINVAL;
    }

    /** @brief Media payloads are byte buffers, so copy the exact reported length.
     * 媒体载荷是字节缓冲区，必须按上报的精确长度复制。 */
    copy = (uint8_t *)qm_malloc(len);
    if (copy == NULL) {
        return -QM_ENOMEM;
    }

    qm_memcpy(copy, src, len);
    *dst = copy;
    return QM_EOK;
}

int qm_agent_event_clone(qm_agent_event_t *dst, const qm_agent_event_t *src)
{
    int ret;

    if (dst == NULL || src == NULL || dst == src) {
        return -QM_EINVAL;
    }

    qm_memset(dst, 0, sizeof(*dst));
    qm_memcpy(dst, src, sizeof(*dst));

    /** @brief Normalize event ownership by copying pointer payloads for callbacks.
     * 通过复制指针载荷统一回调事件的所有权。 */
    switch (src->id) {
        case QM_AGENT_EVENT_AUDIO:
            ret = clone_bytes(src->data.audio.data,
                              src->data.audio.len,
                              &dst->data.audio.data);
            break;
        case QM_AGENT_EVENT_VIDEO:
            ret = clone_bytes(src->data.video.data,
                              src->data.video.len,
                              &dst->data.video.data);
            break;
        case QM_AGENT_EVENT_ASR:
            ret = clone_string(src->data.asr.text, &dst->data.asr.text);
            break;
        case QM_AGENT_EVENT_TTS:
            ret = clone_string(src->data.tts.text, &dst->data.tts.text);
            break;
        case QM_AGENT_EVENT_AGENT_TEXT:
            ret = clone_string(src->data.agent_text.text,
                               &dst->data.agent_text.text);
            break;
        case QM_AGENT_EVENT_COMMAND:
            /* Keep the full provider document for module execution. The public
             * event loop intentionally receives only a bounded snapshot later.
             * 为模块执行保留完整供应商文档；公开事件循环随后仅接收有界快照。 */
            dst->data.command.domain = NULL;
            dst->data.command.json = NULL;
            ret = clone_string(src->data.command.domain, &dst->data.command.domain);
            if (ret == QM_EOK) ret = clone_string(src->data.command.json, &dst->data.command.json);
            break;
        case QM_AGENT_EVENT_EXPRESSION:
            /* The initial struct copy leaves both members borrowed. Clear them
             * before cloning so a partial allocation failure cannot free a
             * pointer still owned by the source event.
             * 初始结构体复制会让两个成员仍是借用指针；克隆前清空它们，避免部分分配失败时释放
             * 仍由源事件持有的指针。 */
            dst->data.expression.provider_code = NULL;
            dst->data.expression.provider_speech_code = NULL;
            ret = clone_string(src->data.expression.provider_code,
                               &dst->data.expression.provider_code);
            if (ret == QM_EOK) {
                ret = clone_string(src->data.expression.provider_speech_code,
                                   &dst->data.expression.provider_speech_code);
            }
            break;
        case QM_AGENT_EVENT_AGENT_STATE:
            ret = clone_string(src->data.agent_state.text,
                               &dst->data.agent_state.text);
            break;
        case QM_AGENT_EVENT_ERROR:
            ret = clone_string(src->data.error.message,
                               &dst->data.error.message);
            break;
        default:
            ret = QM_EOK;
            break;
    }

    if (ret != QM_EOK) {
        qm_agent_event_free(dst);
    }

    return ret;
}

void qm_agent_event_free(qm_agent_event_t *event)
{
    if (event == NULL) {
        return;
    }

    clear_payload(event);
    qm_memset(event, 0, sizeof(*event));
}

static void copy_text_notice(qm_agent_event_text_notice_t *notice,
                             const char *text,
                             qm_bool final,
                             uint32_t duration_ms)
{
    uint32_t index = 0U;
    uint32_t capacity = QM_AGENT_EVENT_TEXT_BYTES - 1U;

    qm_memset(notice, 0, sizeof(*notice));
    notice->final = final;
    notice->duration_ms = duration_ms;
    if (text == NULL) {
        return;
    }

    while (index < capacity && text[index] != '\0') {
        notice->text[index] = text[index];
        index++;
    }
    notice->text[index] = '\0';
    notice->text_length = index;
    notice->truncated = text[index] != '\0' ? QM_TRUE : QM_FALSE;
}

static qm_bool is_tts_related(qm_agent_event_id_t id)
{
    switch (id) {
        case QM_AGENT_EVENT_TTS:
        case QM_AGENT_EVENT_TTS_INPUT_COMPLETE:
        case QM_AGENT_EVENT_TTS_INPUT_BEGIN:
        case QM_AGENT_EVENT_TTS_PLAYBACK_STARTED:
        case QM_AGENT_EVENT_TTS_PLAYBACK_COMPLETE:
        case QM_AGENT_EVENT_TTS_PLAYBACK_FAILED:
        case QM_AGENT_EVENT_AGENT_ABORTED:
        case QM_AGENT_EVENT_DISCONNECTED:
            return QM_TRUE;
        default:
            return QM_FALSE;
    }
}

static void copy_tts_context(qm_agent_handle_t handle,
                             const qm_agent_event_t *event,
                             qm_agent_event_notice_t *notice)
{
    uint32_t request_id = 0U;
    uint32_t generation = 0U;
    qm_bool active = QM_FALSE;

    if (!is_tts_related(event->id) || !handle->tts_request.lock_valid) {
        return;
    }

    (void)qm_mutex_lock(&handle->tts_request.lock, QM_WAIT_FOREVER);
    active = handle->tts_request.active;
    if (active) {
        request_id = handle->tts_request.request_id;
        generation = handle->tts_request.generation;
    }
    (void)qm_mutex_unlock(&handle->tts_request.lock);

    if (event->id == QM_AGENT_EVENT_TTS_PLAYBACK_STARTED ||
        event->id == QM_AGENT_EVENT_TTS_PLAYBACK_COMPLETE ||
        event->id == QM_AGENT_EVENT_TTS_PLAYBACK_FAILED) {
        notice->generation = event->data.tts_playback.generation;
        if (active && generation == notice->generation) {
            notice->request_id = request_id;
        }
        return;
    }

    if (active) {
        notice->request_id = request_id;
        notice->generation = generation;
    }
}

static qm_bool build_notice(qm_agent_handle_t handle,
                            const qm_agent_event_t *event,
                            qm_agent_event_notice_t *notice)
{
    qm_bool active;

    if (event->id == QM_AGENT_EVENT_NONE ||
        event->id == QM_AGENT_EVENT_AUDIO ||
        event->id == QM_AGENT_EVENT_VIDEO) {
        return QM_FALSE;
    }

    qm_memset(notice, 0, sizeof(*notice));
    (void)qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    active = handle->events.active;
    notice->agent_id = handle->events.agent_id;
    (void)qm_mutex_unlock(&handle->network_lock);
    if (!active || notice->agent_id == 0U) {
        return QM_FALSE;
    }

    notice->source = event->source;
    notice->code = event->code;
    copy_tts_context(handle, event, notice);

    switch (event->id) {
        case QM_AGENT_EVENT_STATE_CHANGED:
            notice->data.state.old_state = event->data.state.old_state;
            notice->data.state.new_state = event->data.state.new_state;
            break;
        case QM_AGENT_EVENT_ASR:
            copy_text_notice(&notice->data.text,
                             event->data.asr.text,
                             event->data.asr.final,
                             0U);
            break;
        case QM_AGENT_EVENT_TTS:
            copy_text_notice(&notice->data.text,
                             event->data.tts.text,
                             event->data.tts.final,
                             event->data.tts.duration_ms);
            break;
        case QM_AGENT_EVENT_AGENT_TEXT:
            copy_text_notice(&notice->data.text,
                             event->data.agent_text.text,
                             event->data.agent_text.final,
                             0U);
            notice->data.text.finish_reason = event->data.agent_text.finish_reason;
            break;
        case QM_AGENT_EVENT_COMMAND:
            /* Notices have fixed storage and are observability-only. Consumers
             * must inspect truncated before using these fields. 公开通知使用固定
             * 存储，仅用于观察；使用前必须检查 truncated。 */
            copy_text_notice(&notice->data.command.domain, event->data.command.domain, QM_FALSE, 0U);
            copy_text_notice(&notice->data.command.json, event->data.command.json, QM_FALSE, 0U);
            break;
        case QM_AGENT_EVENT_EXPRESSION:
            notice->data.expression.visual = event->data.expression.visual;
            notice->data.expression.gesture = event->data.expression.gesture;
            notice->data.expression.category = expression_category(event);
            notice->data.expression.speech_expression =
                event->data.expression.speech_expression;
            notice->data.expression.speech_category =
                expression_speech_category(event);
            copy_text_notice(&notice->data.expression.provider_code,
                             event->data.expression.provider_code,
                             QM_FALSE,
                             0U);
            copy_text_notice(&notice->data.expression.provider_speech_code,
                             event->data.expression.provider_speech_code,
                             QM_FALSE,
                             0U);
            break;
        case QM_AGENT_EVENT_AGENT_STATE:
            notice->data.agent_state.state = event->data.agent_state.state;
            copy_text_notice(&notice->data.agent_state.detail,
                             event->data.agent_state.text,
                             QM_FALSE,
                             0U);
            break;
        case QM_AGENT_EVENT_ERROR:
            copy_text_notice(&notice->data.text,
                             event->data.error.message,
                             QM_FALSE,
                             0U);
            break;
        case QM_AGENT_EVENT_DOA:
            notice->data.doa = event->data.doa.info;
            break;
        case QM_AGENT_EVENT_REMOTE_USER_JOINED:
        case QM_AGENT_EVENT_REMOTE_USER_LEFT:
        case QM_AGENT_EVENT_REMOTE_AUDIO_MUTED:
        case QM_AGENT_EVENT_REMOTE_AUDIO_UNMUTED:
            notice->data.remote_user.uid = event->data.remote_user.uid;
            notice->data.remote_user.reason = event->data.remote_user.reason;
            notice->data.remote_user.muted = event->data.remote_user.muted;
            break;
        default:
            break;
    }

    return QM_TRUE;
}

int qm_agent_event_runtime_init(qm_agent_handle_t handle)
{
    if (handle == NULL) {
        return -QM_EINVAL;
    }

    /* The default loop is created once during the system initialization stage
     * (qm_work_on_init() inside qm_init()); qm_agent only reuses it and never
     * races to create a second loop. qm_event_post() reports -QM_ENOENT when
     * the loop is missing, and a dropped notice is a recoverable loss by
     * design.
     * 默认循环只在系统初始化阶段创建一次；qm_agent 只复用它，不会竞争创建第二个循环。若循环
     * 缺失，qm_event_post() 返回 -QM_ENOENT；按设计，丢失公开通知可由后续状态快照恢复。 */
    (void)qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    if (!handle->events.active) {
        handle->events.agent_id = next_agent_id();
        handle->events.active = QM_TRUE;
    }
    (void)qm_mutex_unlock(&handle->network_lock);
    return QM_EOK;
}

void qm_agent_event_runtime_deinit(qm_agent_handle_t handle)
{
    if (handle == NULL) {
        return;
    }

    /* No unregister or loop deletion is needed: posted payloads carry no
     * session pointer and the default loop belongs to the process.
     * 无需注销或删除循环：已投递载荷不含会话指针，默认循环归进程所有。 */
    (void)qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    handle->events.active = QM_FALSE;
    (void)qm_mutex_unlock(&handle->network_lock);
}

qm_bool qm_agent_callback_enter(qm_agent_handle_t handle)
{
    qm_bool allowed;

    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    allowed = !handle->destroying;
    if (allowed) handle->callback_depth++;
    qm_mutex_unlock(&handle->network_lock);
    return allowed;
}

qm_bool qm_agent_callback_try_enter(qm_agent_handle_t handle)
{
    qm_bool allowed;

    if (handle == NULL ||
        qm_mutex_lock(&handle->network_lock, QM_NO_WAIT) != QM_EOK) {
        return QM_FALSE;
    }
    allowed = !handle->destroying;
    if (allowed) handle->callback_depth++;
    (void)qm_mutex_unlock(&handle->network_lock);
    return allowed;
}

void qm_agent_callback_leave(qm_agent_handle_t handle)
{
    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    if (handle->callback_depth != 0U) handle->callback_depth--;
    qm_mutex_unlock(&handle->network_lock);
}

void qm_agent_emit(qm_agent_handle_t handle, const qm_agent_event_t *event)
{
    qm_agent_event_notice_t notice;
    qm_bool publish_notice;
    int ret;

    if (handle == NULL || event == NULL) {
        return;
    }

    /* Media buffers are handled by their direct data paths. They do not carry
     * a public lifecycle transition and must not take the event lock or enter
     * the shared default loop.
     * 媒体缓冲走直连数据路径，不承载公开生命周期转换，因此不得获取事件锁或进入共享默认循环。 */
    if (event->id == QM_AGENT_EVENT_AUDIO ||
        event->id == QM_AGENT_EVENT_VIDEO) {
        return;
    }

    /* Hold the delivery guard while copying event fields and resolving TTS.
     * A queued notice has no session pointer, so it remains valid after this
     * guard is released and a later destroy succeeds.
     * 复制事件字段和解析 TTS 时保持投递保护；排队通知不含会话指针，因此保护释放后即使随后销毁
     * 成功也仍然有效。 */
    if (!qm_agent_callback_enter(handle)) {
        return;
    }
    publish_notice = build_notice(handle, event, &notice);
    qm_agent_tts_runtime_on_event(handle, event);

    if (publish_notice) {
        ret = qm_event_post(QM_AGENT_EVENT_BASE,
                            (int32_t)event->id,
                            &notice,
                            sizeof(notice),
                            QM_NO_WAIT);
        if (ret != QM_EOK) {
            /* Public notification loss cannot roll back a state transition or
             * retain a session. Applications recover from the next snapshot.
             * 丢失公开通知不会回滚状态转换或持有会话；应用可从下一次快照恢复。 */
            QM_AGENT_LOGW("qm_agent_event", QM_AGENT_LOG_CONTROL,
                          "drop public event id=%d agent=%u ret=%d",
                          (int)event->id,
                          (unsigned int)notice.agent_id,
                          ret);
        }
    }
    qm_agent_callback_leave(handle);
}

uint32_t qm_agent_get_id(qm_agent_handle_t handle)
{
    uint32_t agent_id = 0U;

    if (handle == NULL) {
        return 0U;
    }

    (void)qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    agent_id = handle->events.agent_id;
    (void)qm_mutex_unlock(&handle->network_lock);
    return agent_id;
}

void qm_agent_set_state(qm_agent_handle_t handle, qm_agent_state_t new_state)
{
    qm_agent_event_t event = {0};
    qm_agent_state_t old_state;

    if (handle == NULL) {
        return;
    }

    (void)qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    old_state = handle->state;
    if (old_state == new_state) {
        (void)qm_mutex_unlock(&handle->network_lock);
        return;
    }
    handle->state = new_state;
    (void)qm_mutex_unlock(&handle->network_lock);

    /* Publish after releasing network_lock because qm_agent_emit() enters the
     * callback lifetime guard through that same lock.
     * 释放 network_lock 后才发布，因为 qm_agent_emit() 会通过同一把锁进入回调生命周期保护。 */
    event.id = QM_AGENT_EVENT_STATE_CHANGED;
    event.source = QM_AGENT_EVENT_SOURCE_CORE;
    event.data.state.old_state = old_state;
    event.data.state.new_state = new_state;
    qm_agent_emit(handle, &event);
}
