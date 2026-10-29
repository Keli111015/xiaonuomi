#ifndef __QM_IOT_DEV_INTERNAL_H__
#define __QM_IOT_DEV_INTERNAL_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include "qm.h"

/**
 * @brief 经 device/up 向云端回复下行 JSON（method / 可选 subMethod / id / code / 可选 params）。
 *
 * @details
 * 发布 Topic 为 `/{pid}/{did}/device/up`。JSON 的 method 由 @p param 自动拼接
 * `"Reply"` 后缀（若 @p param 已以 `"Reply"` 结尾则不再重复拼接），例如
 * `"bindDevNotify"` → `"bindDevNotifyReply"`。
 * 仅供 @ref qm_iot_down_reply 等 API 层内部转调，不对外部应用暴露。
 *
 * @param[in] handle      dev 会话句柄（需已配置 MQTT 且挂载 IOT 会话）。
 * @param[in] param       下行原始 method 字符串；写入 JSON 前自动追加 `"Reply"` 后缀。
 * @param[in] submethod   可选顶层 subMethod；NULL 或空字符串时不写入。
 * @param[in] msg_id      报文 id，通常与下行请求 id 一致；0 表示由 dev 层自增。
 * @param[in] code        业务结果码，写入 JSON 的 code 字段。
 * @param[in] payload     可选 params JSON 片段；可为 NULL。
 * @param[in] payload_len @p payload 字节长度；为 0 或与 @p payload 为 NULL 时不带 params 字段。
 *
 * @return int32_t
 * @retval QM_EOK     MQTT 发布请求已发起。
 * @retval -QM_EINVAL 参数非法、method 过长或会话未就绪。
 * @retval -QM_ENOMEM JSON 组装内存分配失败。
 */
int32_t qm_iot_dev_reply_publish(void *handle,
                                 char *param,
                                 char *submethod,
                                 uint32_t msg_id,
                                 int code,
                                 char *payload,
                                 int payload_len);

#if defined(__cplusplus)
}
#endif

#endif /* __QM_IOT_DEV_INTERNAL_H__ */
