# QM HTTP OTA

`qm_http_ota` 提供基于 `qm_http_client` 的流式 HTTP OTA 能力。默认目标是本 SoC，
通过 `qm_ota` 写入；也支持下载后边收边转发给外部 MCU。

## 能力概览

## 架构

```text
应用 OTA 策略与事件回调
          v
      qm_http_ota
       |       |
       v       v
qm_http_client  qm_ota 或 MCU 回调
       |       |
       +-- 镜像服务 / 目标存储
```

- 一键升级：`qm_http_ota()`
- 分阶段升级：`qm_http_ota_begin()` / `qm_http_ota_perform()` / `qm_http_ota_finish()` / `qm_http_ota_abort()`
- 进度查询：`qm_http_ota_is_complete_data_received()` / `qm_http_ota_get_image_len_read()` / `qm_http_ota_get_result()`
- 进度通知：`QM_HTTP_OTA_EVENT_PROGRESS`
- 必填文件长度入参：`expected_total_size`
- 可选断点续传：`Range` + `If-Range`
- 断点持久化：基于 `qm_kv`
- 断点管理接口：按 URL 查询/清理续传记录
- 可选文件完整性校验：下载完成后校验 MD5 或 HMAC-SHA256
- 可选 MCU OTA：通过调用方提供的 `begin/write/read/finish/abort` 回调转发镜像

## 依赖边界

组件只依赖现有 QUMI 平台抽象能力：

- `qm_http_client`
- `qm_ota`（仅 `QM_HTTP_OTA_TARGET_SOC` 目标使用）
- `qm_kv`
- `qm_kernel`
- `qm_log`
- `qm_string`
- `qm_utils_crc32`
- `qm_utils_md5`
- `qm_utils_hmac`
- `qm_utils_sha256`（由 HMAC-SHA256 工具使用）
- `qm_reboot()`（仅 SoC 目标且 `auto_restart == QM_TRUE` 时用于重启）

## 默认配置项

`qm_kconfig` 提供以下默认项：

- `QM_HTTP_OTA_TIMEOUT_MS`：默认 OTA 超时
- `QM_HTTP_OTA_HEADER_MAX`：默认响应头大小上限
- `QM_HTTP_OTA_RX_BUFFER_SIZE`：默认 OTA 接收缓冲区大小

当调用方未在 `qm_http_client_request_t` 中显式设置超时、头大小或 `rx_buf_size` 时，组件会回退到这些默认值。
如果设置了 `http_config->rx_buf_size`，OTA 会使用该值作为本次任务的下载缓冲区大小和底层
HTTP client 的单次读缓冲大小。
`User-Agent` 属于 HTTP 请求属性，如需定制应通过 `qm_http_client_request_t.user_agent` 传入；
未传入时组件使用内部兜底值。
`expected_total_size`、`progress_step`、`resume_enable`、`auto_restart` 等本次 OTA 任务相关参数保持为
`qm_http_ota_config_t` 接口入参，不放入 `qm_config`。

## OTA 目标

`qm_http_ota_config_t.target` 用于选择写入目标：

- `QM_HTTP_OTA_TARGET_SOC`：默认值，保持原有行为，使用 `qm_ota_start/write/read/end/abort`
- `QM_HTTP_OTA_TARGET_MCU`：不调用 `qm_ota`，由 `mcu_ota` 回调完成外部 MCU 镜像传输

MCU 目标必须配置 `qm_http_ota_mcu_config_t`，并提供全部回调：

- `begin(user, info)`：准备 MCU OTA 传输或存储；此时已经拿到 HTTP 响应头，`info` 中包含 URL、总大小和最终续传偏移
- `write(user, offset, data, len)`：把当前下载块写入/发送给 MCU
- `read(user, offset, data, len)`：从 MCU 侧读回已保存镜像，仅在选择 read-back 摘要校验时需要
- `finish(user)`：在下载完成且可选摘要校验通过后提交 MCU 镜像
- `abort(user, error_code)`：中止 MCU OTA，释放调用方资源

`auto_restart` 只对 SoC 目标生效；MCU 目标是否重启或切换应用由回调内部决定。

### MCU read 能力

MCU 目标默认不要求 `read` 回调。摘要校验
支持两种校验模式：

- `QM_HTTP_OTA_DIGEST_VERIFY_STREAM`：默认模式，使用 HTTP 下载缓冲区边下载边计算摘要，不依赖 `read`
- `QM_HTTP_OTA_DIGEST_VERIFY_READBACK`：写完后从目标按 offset 读回镜像再计算摘要，必须提供 `read`

如果业务只需要确认 HTTP 下载内容与服务端签名一致，使用 stream 模式即可。如果业务需要确认
MCU 侧实际保存的数据也一致，使用 read-back 模式并实现 `read`。

## 基本流程

### 一键模式

1. 配置 `qm_http_client_request_t`
2. 配置 `qm_http_ota_config_t`
3. 调用 `qm_http_ota()`

### 分阶段模式

1. 调用 `qm_http_ota_begin()`
2. 循环调用 `qm_http_ota_perform()`
3. 若返回 `-QM_ERR_HTTP_OTA_IN_PROGRESS`，继续调用
4. 若返回 `QM_EOK`，调用 `qm_http_ota_finish()`
5. 若返回其他负值，可调用 `qm_http_ota_abort()`

## 文件长度

`qm_http_ota_config_t.expected_total_size` 用于传入本次 OTA 的完整文件长度，单位为字节。
该字段为必填项，不能为 `0`；未配置时 `qm_http_ota_begin()` 返回 `-QM_EINVAL`。

校验行为：

- `expected_total_size` 会被视为精确文件长度
- 如果响应头中能拿到 `Content-Length` 或 `Content-Range` total，且该 total 与
  `expected_total_size` 不一致，组件会在写入目标前失败
- 如果服务端未提供 total size，组件会用 `expected_total_size` 计算进度并做完整性校验
- 当 `http_config->max_body_len == 0` 时，OTA 会把底层 `qm_http_client` 的响应体上限设置为
  `expected_total_size`，避免流式 OTA 被 HTTP client 默认 body 上限提前截断
- 如果调用方显式设置了 `http_config->max_body_len`，该值仍作为 HTTP client 层的额外限制
- `http_config->rx_buf_size` 只控制单次下载/写入块大小，不改变完整文件长度校验
- 组件会拒绝超过 total 的数据块；连接结束时也会检查 `downloaded_size == total_size`，
  避免短包被当成成功

## 进度事件

`QM_HTTP_OTA_EVENT_PROGRESS` 会在数据块成功写入目标后按 `progress_step` 策略发出。事件中：

- `downloaded_size`：当前已写入字节数
- `total_size`：完整镜像大小，来自 `expected_total_size`
- `progress_percent`：`0` 到 `100`

`qm_http_ota_config_t.progress_step` 可在组件内部节流进度事件：

- `progress_step == 0`：每写入一个数据块都发出 `QM_HTTP_OTA_EVENT_PROGRESS`
- `progress_step > 0`：按百分比步长发出进度事件，例如 `5` 表示每 5% 通知一次
- 进度到 `100%` 时一定会发出通知

`QM_HTTP_OTA_EVENT_DATA` 仍然表示每个数据块已写入；如果只需要 UI 或日志进度，建议优先使用
`QM_HTTP_OTA_EVENT_PROGRESS`。

## 错误事件

`QM_HTTP_OTA_EVENT_ERROR` 的 `event.error_code` 使用负错误码，和对应 API 返回值保持一致。

常见取值：

- `-QM_ERR_HTTP_OTA_MD5_MISMATCH`：`qm_http_ota_finish()` 阶段 MD5 校验失败
- `-QM_ERR_HTTP_OTA_HMAC_SHA256_MISMATCH`：`qm_http_ota_finish()` 阶段 HMAC-SHA256 校验失败
- `-QM_EINVAL`：接口参数非法、响应头格式非法或回调配置不完整
- `-QM_EIO`：HTTP 状态、续传响应、长度不匹配、短包等协议或 IO 错误
- `-QM_EFULL`：下载数据超过 `expected_total_size`
- `-QM_ENOMEM`：内存不足；如果失败发生在回调建立前，可能只有 API 返回值而没有事件
- 其他负值：来自 `qm_http_client`、`qm_ota`、MCU 回调、`qm_kv` 或 HMAC 工具的底层错误

`QM_ERR_HTTP_OTA_*` 宏本身是正数；事件和 API 返回时使用负值，例如 HMAC 校验失败是
`-QM_ERR_HTTP_OTA_HMAC_SHA256_MISMATCH`，MD5 校验失败是 `-QM_ERR_HTTP_OTA_MD5_MISMATCH`。

## 断点续传

断点续传是 `qm_http_ota` 的可选增强能力，不是基础必选能力。
即使不启用续传，组件仍然可以完整执行全量 OTA。

相关配置字段：

- `resume_enable`：开启断点续传
- `resume_offset`：显式指定本地已写入偏移
- `resume_if_range`：可选 `If-Range` 覆盖值；通常保持 `NULL`，由组件自动使用响应头中的 `ETag` 或 `Last-Modified`

行为说明：

- 当 `resume_enable == QM_TRUE` 且 `resume_offset > 0` 时，组件自动追加 `Range: bytes=<offset>-`
- 当 `resume_enable == QM_TRUE` 且 `resume_offset == 0` 时，组件会尝试从 KV 自动恢复该 URL 的断点
- 自动恢复的断点记录必须匹配当前 `expected_total_size`，且 offset 必须大于 0 且小于完整文件长度
- 若服务端返回 `206`，组件校验 `Content-Range` 起始偏移必须等于 `resume_offset`
- 若服务端返回 `200`，组件自动清理旧断点并回退到全量下载
- 若响应与续传语义不匹配，则 OTA 失败

SoC 目标的断点续传还依赖具体平台 `qm_ota_start()` 不擦除或破坏已写入 OTA 分区数据；如果平台
每次 start 都重新初始化 OTA 写入区，应关闭 SoC 断点续传或改由平台提供可续写能力。

## 断点状态持久化

组件使用 `qm_kv` 以“按 URL CRC32 hash 分 key”的方式保存断点状态。

- key 形式：`qm_http_ota_<CRC32(url)>`
- value 中仍保存完整 `url`
- 读取后会再次校验记录中的 `url`，避免 hash 冲突导致误恢复

当前保存内容包括：

- `url`
- `offset`
- `total_size`
- `if_range`

组件会优先从响应头保存 `ETag` 作为后续续传的 `If-Range` 值；没有 `ETag` 时保存
`Last-Modified`。下一次自动续传时，如果断点记录中存在 `if_range`，组件会随 `Range`
一起发送 `If-Range`，让服务端在文件已变化时回退到全量下载。

持久化时机：

- 每写入一块数据后更新断点
- `qm_http_ota_finish()` 成功后清除该 URL 的断点
- `qm_http_ota_abort()` 不清理断点，便于后续继续同一 URL 的下载

## 文件校验

`digest_type` 决定校验算法，`expected_md5_hex` 和 `expected_hmac_sha256_hex`
提供对应算法的期望值。运行时只会启用 `digest_type` 选中的一种算法，不会同时
进行 MD5 和 HMAC-SHA256 校验。两个 expected 字段都为空时不做摘要校验。

- `QM_HTTP_OTA_DIGEST_TYPE_MD5`：使用 `expected_md5_hex`，32 字符十六进制字符串，不依赖 `product_secret`
- `QM_HTTP_OTA_DIGEST_TYPE_HMAC_SHA256`：使用 `expected_hmac_sha256_hex`，64 字符十六进制字符串，必须同时配置非空 `product_secret`
- `expected_md5_hex` 和 `expected_hmac_sha256_hex` 互斥，同时配置时返回 `-QM_EINVAL`
- `qm_http_ota_finish()` 会在提交目标镜像前校验完整镜像摘要
- MD5 校验失败返回 `-QM_ERR_HTTP_OTA_MD5_MISMATCH`
- HMAC-SHA256 校验失败返回 `-QM_ERR_HTTP_OTA_HMAC_SHA256_MISMATCH`

服务端应按以下方式生成期望值：

```text
expected_md5_hex = HEX(MD5(firmware_bin))
expected_hmac_sha256_hex = HEX(HMAC-SHA256(product_secret, firmware_bin))
```

校验实现使用 qumi 的 `qm_utils_md5` 和 `qm_utils_hmac`。`STREAM` 模式使用 HTTP 下载块
更新摘要，并在写入目标前完成该块的摘要更新；`READBACK` 模式下载过程中不更新摘要，只在 finish 阶段
通过当前目标的 read 能力重建摘要。

`product_secret` 必须是服务端和设备端共同持有的秘密值，不能公开、不能放在
可被未授权访问的接口或日志里。如果使用公开产品标识作为 HMAC key，HMAC 只能做
误传检测，不能防止固件被恶意替换。固件签名、回滚保护等仍应由平台的
`qm_ota_end()`/boot 流程或 MCU 自身安全启动流程负责。

## Finish 与 Abort 语义

### `qm_http_ota_finish()`

SoC 目标：

- 调用 `qm_ota_end()` 完成 OTA 收尾
- 切换 boot partition
- 若配置了摘要校验，在 `qm_ota_end()` 前完成校验
- 清除该 URL 的断点记录
- 当 `auto_restart == QM_TRUE` 时调用 `qm_reboot()`

MCU 目标：

- 若配置了摘要校验，先按 `digest_verify_mode` 完成校验
- 调用 `mcu_ota.ops->finish()` 提交 MCU 镜像
- 清除该 URL 的断点记录
- 不自动调用 `qm_reboot()`

### `qm_http_ota_abort()`

- 仅中止 HTTP OTA 会话并释放上层资源
- SoC 目标调用 `qm_ota_abort()` 释放平台 OTA 资源，不调用 `qm_ota_end()`，不切换 boot partition
- MCU 目标调用 `mcu_ota.ops->abort(user, error_code)`
- 保留该 URL 的断点记录，供后续续传

## 断点管理接口

- `qm_http_ota_resume_info_get(url, ...)`：读取指定 URL 的断点信息
- `qm_http_ota_resume_info_clear(url)`：清除指定 URL 的断点信息

## 示例

参考：

- `apps/opencpu/app/qm_http_ota_example.c`
- `examples/qm_http_ota/qm_http_ota_example.c`

其中包含：

- 一键 OTA 示例
- 分阶段 OTA 示例
- `progress_step` 进度通知节流示例，默认每 5% 打印一次
- MCU OTA 回调示例：`qm_http_ota_example_mcu_start()`
- 断点查询示例
- 断点清理示例

可通过以下宏调整示例行为：

- `QM_HTTP_OTA_EXAMPLE_TARGET`：WiFi demo 默认升级目标，取值为 `QM_HTTP_OTA_TARGET_SOC` 或 `QM_HTTP_OTA_TARGET_MCU`
- `QM_HTTP_OTA_EXAMPLE_EXPECTED_TOTAL_SIZE`：示例配置的完整镜像大小，必须配置为非 0
- `QM_HTTP_OTA_EXAMPLE_EXPECTED_MD5`：可选 MD5 hex 期望值；配置后示例使用 MD5 校验
- `QM_HTTP_OTA_EXAMPLE_EXPECTED_HMAC_SHA256`：可选 HMAC-SHA256 hex 期望值；未配置 MD5 且配置该项时示例使用 HMAC-SHA256 校验
- `QM_HTTP_OTA_EXAMPLE_PRODUCT_SECRET`：HMAC-SHA256 校验使用的密钥
- `QM_HTTP_OTA_EXAMPLE_DIGEST_VERIFY_MODE`：校验数据源，默认 `QM_HTTP_OTA_DIGEST_VERIFY_STREAM`
- `QM_HTTP_OTA_EXAMPLE_PROGRESS_STEP`：组件内部进度通知节流步长，默认 `5`
- `QM_HTTP_OTA_EXAMPLE_MCU_URL`：MCU OTA 示例使用的 URL，默认复用 `QM_HTTP_OTA_EXAMPLE_URL`
- `QM_HTTP_OTA_EXAMPLE_MCU_RESUME_ENABLE`：MCU 示例是否开启断点续传，默认 `QM_FALSE`
- `QM_HTTP_OTA_EXAMPLE_MCU_MOCK_STORAGE_SIZE`：MCU read-back RAM 模拟存储大小，默认 `0`；仅 read-back 校验模式下动态申请缓存

WiFi demo 会调用 `qm_http_ota_example_selected_target_start()`，按 `QM_HTTP_OTA_EXAMPLE_TARGET`
选择 SoC 或 MCU 示例路径。也可以直接调用 `qm_http_ota_example_start()` 测试 SoC OTA，
或调用 `qm_http_ota_example_mcu_start()` 测试 MCU OTA。

MCU 示例默认只给出适配层模板，`write/read` 返回 `-QM_ENOSYS`，用于提醒调用方接入真实
MCU 传输协议。默认 stream 模式只依赖 `write`，不依赖 `read`，也不需要整包 RAM 缓存。
若想验证 read-back
校验链路，可在编译时定义
`QM_HTTP_OTA_EXAMPLE_MCU_MOCK_STORAGE_SIZE` 为一个大于固件大小的值，示例会用 RAM
模拟 MCU 侧存储，并支持 `read` 回读校验。该 RAM 缓存在 MCU OTA begin 时动态申请，
finish 或 abort 时释放。
