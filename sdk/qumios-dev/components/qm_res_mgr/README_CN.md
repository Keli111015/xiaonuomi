# qm_res_mgr

For the English version, see [README.md](README.md).

`qm_res_mgr` 基于 `qm_http_client` 实现，用于获取资源列表/详情元数据，并通过数据回调同步下载资源 URL 对应的文件内容。

组件本身只负责“查资源 + 下资源”。文件是否命中、本地保存、缓存校验、临时文件清理等逻辑都由业务层负责。

## 架构

```text
业务应用
    | 查询元数据 / 消费下载字节
    v
qm_res_mgr
    |-- HTTP 或 MQTT 元数据通道
    `-- qm_http_client 下载通道
    v
资源服务 / 签名资源 URL
```

组件只返回元数据并通过调用方回调流式交付字节；不创建文件、不保留下载 payload，也不决定缓存策略。

## 配置归属

可以按这个原则理解：跟请求、设备、账号、选中资源有关的值放到接口入参；固定行为、默认值、资源上限放到 `qm_config`/Kconfig。

### API 入参

这些参数会随着运行时场景变化，所以放在 API 入参里，由调用方按次传入：

- `qm_res_mgr_set_env()`：HTTP 元数据通道使用的当前资源 API 环境。默认值来自 Kconfig；只有编译了 HTTP 元数据通道时，产品代码和 example 才能在运行时切换。MQTT-only 构建会返回 `-QM_ENOSYS`。
- `qm_res_mgr_get_list()`：设备 `token`、`DID`、资源包名、起始页码、页大小，以及可选的逐页回调。
- `qm_res_mgr_get_category()`：产品 `pid`、设备 `DID`，以及当前编译元数据通道所需的 HTTP 鉴权信息或 MQTT 会话句柄，用于获取一级目录及其二级目录。
- `qm_res_mgr_get_children_category()`：产品 `pid`、设备 `DID`、可选 `categoryId`、可选 `resourceBundleName`，以及当前编译元数据通道所需的 HTTP 鉴权信息或 MQTT 会话句柄，用于获取子分类。
- `qm_res_mgr_get_detail()`：设备 `DID`、资源 `id`，以及当前编译元数据通道所需的 HTTP 鉴权信息或 MQTT 会话句柄。
- `qm_res_mgr_download()`：选中的资源元数据、数据回调、回调用户指针。

由于元数据通道是编译期二选一，`qm_res_mgr.h` 里的请求结构体字段也会随
`CONFIG_QM_RES_MGR_HTTP_SUPPORT` / `CONFIG_QM_RES_MGR_MQTT_SUPPORT` 裁剪：

- HTTP 构建只暴露 HTTP 相关字段，例如 `token`、分类接口里的 `product_id`
- MQTT 构建只暴露 MQTT 相关字段，例如 `iot_handle`、`timeout_ms`

不要把这些值放进 `qm_config`：`token`、`DID`、`bundle`、`resource_id`、资源 URL、服务端返回的文件元数据、回调上下文指针、文件存储策略。当前实现里 `token` 只用于 HTTP 元数据接口，请求文件资源时直接使用资源 URL。

当前版本没有运行时 config 结构体，也没有事件回调层。接口成功/失败、HTTP 状态码、下载结果都直接通过同步返回值和输出参数给调用方。

HTTPS 默认走 `qm_http_client` 的传输配置，即单向 TLS；`qm_res_mgr` 自己不额外指定 CA 证书或客户端证书。

### `qm_config` / Kconfig

组件固定开关、限制和调优项放在 `qm_config`/Kconfig：

- 组件使能开关：`CONFIG_QM_RES_MGR_SUPPORT`
- 元数据通道二选一：`CONFIG_QM_RES_MGR_HTTP_SUPPORT` 或 `CONFIG_QM_RES_MGR_MQTT_SUPPORT`
- 生成的 `qm_config.h` 只会定义当前选中的元数据通道宏，另一个通道宏保持未定义
- 组件使能后始终显示的通用下载配置：
  `CONFIG_QM_RES_MGR_SEGMENT_SIZE`、`CONFIG_QM_RES_MGR_RETRY_MAX`
- 选定元数据通道后显示的共享元数据配置：
  `CONFIG_QM_RES_MGR_PAGE_SIZE`
- 仅 HTTP 元数据通道显示的配置：
  `CONFIG_QM_RES_MGR_TIMEOUT_MS`、`CONFIG_QM_RES_MGR_HTTP_RX_BUF_SIZE`、
  `CONFIG_QM_RES_MGR_HTTP_HEADER_MAX`、`CONFIG_QM_RES_MGR_API_BODY_MAX`、
  `CONFIG_QM_RES_MGR_ENV_DEV`、`CONFIG_QM_RES_MGR_ENV_TEST`、
  `CONFIG_QM_RES_MGR_ENV_PROD`
- 仅 MQTT 元数据通道显示的配置：
  `CONFIG_QM_RES_MGR_MQTT_TIMEOUT_MS_DEFAULT`

日志配置独立于 `qm_http_client` 以及 MQTT/cloud 组件。总开关是
`CONFIG_QM_RES_MGR_LOG_CONFIG`；只有打开总开关后，menuconfig 才显示分类选项：
`CONFIG_QM_RES_MGR_LOG_ERROR`、`CONFIG_QM_RES_MGR_LOG_LIFECYCLE`、
`CONFIG_QM_RES_MGR_LOG_CONTROL`、`CONFIG_QM_RES_MGR_LOG_METADATA`、
`CONFIG_QM_RES_MGR_LOG_DOWNLOAD`、`CONFIG_QM_RES_MGR_LOG_PAYLOAD` 和
`CONFIG_QM_RES_MGR_LOG_STATS`。

总开关关闭时只启用错误和生命周期日志。运行时可以通过
`qm_res_mgr_set_log_flags(QM_RES_MGR_LOG_ERROR | QM_RES_MGR_LOG_DOWNLOAD)`
修改 mask，并用 `qm_res_mgr_get_log_flags()` 读取当前值。错误日志只报告
状态和长度，不再直接打印响应 body。设备 DID、资源包名、ETag、签名和时间字段
只记录长度或是否存在；下载 URL 在输出前会被截短脱敏。

这些固定调优项不要再重复塞进请求结构体：例如超时、分段大小、重试次数、HTTP buffer/header/body 限制等。

当编译选择 MQTT 元数据通道时，资源文件下载内部仍然会走 HTTP，但这些
HTTP 超时 / header / 读缓冲配置不再通过 `qm_kconfig` 暴露；该路径会回退到
组件默认值：15000 ms、2048 字节 header 上限、1024 字节读缓冲。

`CONFIG_QM_RES_MGR_API_BODY_MAX` 表示 list/detail 接口响应 body 的上限。它不是一次性预分配的内存大小，而是动态扩容时允许增长到的最大值；超过这个值就返回失败。

资源 API 的 base URL 固定在组件内部：

- `QM_RES_MGR_ENV_DEV`：`https://api-dev.ljyd.cn`
- `QM_RES_MGR_ENV_TEST`：`https://api-test.ljyd.cn`
- `QM_RES_MGR_ENV_PROD`：`https://api.ljyd.cn`

如果 Kconfig 没有显式改动，默认环境就是生产环境；当编译选择 HTTP 元数据通道时，业务或 example 也可以在运行时切换：

```c
qm_res_mgr_set_env(QM_RES_MGR_ENV_TEST);
```

接口不支持外部传任意 `api_base_url`。组件现在是“静态即用”模式，不需要 `qm_res_mgr_init()` / `qm_res_mgr_deinit()`。

这里的“二选一”只针对资源元数据通道，也就是 `get_list()`、`get_category()`、`get_children_category()`。
资源文件下载仍然始终基于服务端返回的 HTTP URL。

## 下载边界

`qm_res_mgr_download()` 在调用线程里同步执行。它会基于选中的资源 URL 发起下载，并在每次收到数据块时调用 `on_data`。它不会检查本地是否已有文件，也不会读本地文件、写文件落盘、维护缓存元数据。

围绕下载流的存储逻辑需要业务层自己处理：

- 调用 `qm_res_mgr_download()` 之前：决定是否可以直接使用本地已有文件
- 在 `on_data` 内部：把收到的数据写入目标文件、解码器或其他业务接收端
- 下载完成或失败之后：根据业务策略提交、删除或重试处理临时文件

`qm_res_mgr` 内部的重试只覆盖网络分段重试；重试时会用当前已经成功交付给 `on_data` 的字节数作为下一次 `Range` 起点。如果 `on_data` 返回非 0，下载会立即中止，并把该返回值继续返回给调用方。

当组件已经知道文件总长度时，只有 `downloaded_size == total_size` 才算下载成功；如果服务端提前断流、短响应，或者最终字节数对不上，接口会返回失败，不再把半截文件当成功。

如果首次下载时服务端不支持 `Range` 且响应里也没有 `Content-Length`，组件会把该次实际读到的总字节数作为最终大小；这种场景下是否接受该结果，由业务层结合文件格式或校验值进一步判断。

## 列表分页

`qm_res_mgr_get_list()` 会把分页结果填到 `qm_res_mgr_list_t`。当 `page_num=0` 时默认按 `pageNum=1` 请求；当 `page_size=0` 时默认使用 `CONFIG_QM_RES_MGR_PAGE_SIZE`。如果服务端返回的 `pages` 大于当前页，组件会继续把后续页拉完，并直接写入最终的调用方持有 `items` 数组。

如果设置了 `req->on_page`，那么 `qm_res_mgr_get_list()` 每成功解析完一页，都会在继续请求下一页之前，同步回调一次。回调里拿到的是当前页的 `qm_res_mgr_list_page_t` 借用视图，适合立刻透传到应用层做展示，不需要等全部分页结束。`page.items` 指向最终 `qm_res_mgr_list_t.items` 中当前页对应的那一段，所以这段页数据会一直有效，直到调用方执行 `qm_res_mgr_list_free()` 释放整份列表。

当编译选择 MQTT 元数据通道时，组件会为每个请求分配内部 `msg_id`，并按 reply 的 `method + id` 匹配进行中的请求。`qm_res_mgr_get_list()`、`qm_res_mgr_get_category()` 和 `qm_res_mgr_get_children_category()` 可以同时存在多个进行中的 MQTT 请求；每个调用方仍然需要根据自己的业务场景设置合适的超时时间。

拿到列表项后，如果该项已经带了 `url`，应直接调用 `qm_res_mgr_download()` 下载文件资源，不要只是为了拿下载地址再多调一次 `qm_res_mgr_get_detail()`。

`qm_res_mgr_get_detail()` 也会跟随编译期元数据通道：HTTP 模式走 `/resource/detail`，MQTT 模式走 `getResourceDetail/getResourceDetailReply`。如果列表项已经带了 `url`，仍然建议优先直接下载，不必再为了拿地址多调一次 detail。

资源元数据里的字符串字段都是按服务端返回值动态分配的。列表结果用 `qm_res_mgr_list_free()` 释放；单个 detail 结果用 `qm_res_mgr_resource_free()` 释放。`qm_res_mgr_download_result_t` 现在只包含状态码和大小信息，不需要额外释放。

鉴权 `token` 只会传给 HTTP 模式下的 `qm_res_mgr_get_list()` 和 `qm_res_mgr_get_detail()`；下载文件时直接使用选中资源的 `url`，不会额外补业务侧 `Authorization` 请求头。如果文件服务需要鉴权，应该由业务层提供已经携带访问策略的可下载 URL，例如签名 URL。

`qm_res_mgr_set_env()` 修改的是 HTTP 元数据通道下的组件级全局环境。组件内部已经保证单次 list/detail 请求会固定使用进入接口时拍下来的环境快照，但如果多个业务任务并发切换环境，后续请求仍然可能互相影响；产品代码应避免把不同环境混在同一个运行实例里并发使用。MQTT-only 元数据构建不会使用这个设置，并会返回 `-QM_ENOSYS`。

- `total`：资源总数
- `page_num`：首次请求页码
- `page_size`：服务端返回的当前页大小
- `pages`：总页数
- `count`：最终解析出的资源条目数量
- `status_code`：最后一次 list API 的 HTTP 状态码，即使 `qm_res_mgr_get_list()` 返回失败也会保留

## 分类接口

`qm_res_mgr_get_category()` 走编译期选中的元数据通道：

- HTTP：调用 `/agent-device/resource/getCategory?pid=...&did=...`
- MQTT：上报 `geCategory`，解析 `geCategoryReply`

`qm_res_mgr_get_children_category()` 也走编译期选中的元数据通道：

- HTTP：调用 `/agent-device/resource/getChildrenCategory`，带 `pid`、`did`，以及可选的 `categoryId` / `resourceBundleName`
- MQTT：上报 `geChildrenCategory`，解析 `geChildrenCategoryReply`

子分类请求里可以只传 `categoryId`，也可以只传 `resourceBundleName`，也可以两者同时传；两者都不传时，按云端对当前设备可见的子分类范围处理。

`qm_res_mgr_get_category()` 的结果通过调用方持有的 `qm_res_mgr_category_list_t` 导出，
使用 `qm_res_mgr_category_list_free()` 释放。一级目录节点里的 `children` 只承载二级目录。

`qm_res_mgr_get_children_category()` 的结果通过调用方持有的
`qm_res_mgr_child_category_list_t` 导出，使用
`qm_res_mgr_child_category_list_free()` 释放。返回条目本身不再包含 `children` 字段。

`qm_res_mgr_get_category()` 当前已解析的一级目录字段包括：

- `id`
- `name`
- `resource_bundle_name`
- `parent_id`
- `level`
- `is_leaf`
- `sort_order`
- `create_time`
- `description`
- `icon`
- `background_image`
- `child_count`
- `children`

`qm_res_mgr_get_children_category()` 返回的二级目录字段包括上面除 `child_count` /
`children` 之外的公共字段。服务端即使继续返回更深层的 `children`，当前组件也会忽略。

如果编译选择 HTTP 元数据通道，`req->product_id` 和 `req->did` 必填；`req->token` 可选，传入时会作为 HTTP 鉴权头透传。如果编译选择 MQTT 元数据通道，`req->iot_handle` 必填；当 `req->timeout_ms == 0` 时，会回退到 Kconfig 默认的 MQTT 应答超时时间 `CONFIG_QM_RES_MGR_MQTT_TIMEOUT_MS_DEFAULT`。

## 资源元数据扩展字段

除了原来的 list/detail 字段，`qm_res_mgr_resource_t` 现在还会按服务端返回解析以下可选字段：

- `audio_type`：来自 `audioType`
- `total_duration`：来自 `totalDuration`，单位秒

## opencpu 示例

opencpu 示例位于：

```text
apps/opencpu/app/qm_res_mgr_example.c
```

通过下面的宏选择该示例：

```c
#define APP_DEMO_SELECT APP_DEMO_RES_MGR
```

常用示例宏如下：

```c
#define QM_RES_MGR_EXAMPLE_TOKEN        "device-token"
#define QM_RES_MGR_EXAMPLE_DID          "device-did"
#define QM_RES_MGR_EXAMPLE_BUNDLE       "resource-bundle-name"
#define QM_RES_MGR_EXAMPLE_RESOURCE_ID  0
#define QM_RES_MGR_EXAMPLE_ENV          QM_RES_MGR_ENV_DEV
#define QM_RES_MGR_EXAMPLE_ENABLE_GET_CATEGORY          1
#define QM_RES_MGR_EXAMPLE_ENABLE_GET_CHILDREN_CATEGORY 1
#define QM_RES_MGR_EXAMPLE_ENABLE_GET_LIST              1
#define QM_RES_MGR_EXAMPLE_ENABLE_GET_DETAIL            1
#define QM_RES_MGR_EXAMPLE_ENABLE_DOWNLOAD              1
```

这些宏用于按功能模块开关 example：

- `QM_RES_MGR_EXAMPLE_ENABLE_GET_CATEGORY`：是否测试 `qm_res_mgr_get_category()`
- `QM_RES_MGR_EXAMPLE_ENABLE_GET_CHILDREN_CATEGORY`：是否测试 `qm_res_mgr_get_children_category()`
- `QM_RES_MGR_EXAMPLE_ENABLE_GET_LIST`：是否测试 `qm_res_mgr_get_list()`
- `QM_RES_MGR_EXAMPLE_ENABLE_GET_DETAIL`：是否单独测试 `qm_res_mgr_get_detail()`，以及在列表项缺少 `url` 时是否允许 detail 补全
- `QM_RES_MGR_EXAMPLE_ENABLE_DOWNLOAD`：是否测试 `qm_res_mgr_download()`，也控制 direct URL 下载分支

当前 example 在 `on_data` 里只做字节数统计，不做文件保存。如果要真正落盘或做本地文件命中判断，需要在业务代码里补上。

如果想跳过 list/detail，直接测试文件下载，可以这样配置：

```c
#define QM_RES_MGR_EXAMPLE_DIRECT_URL "https://example.com/resource.bin"
```
