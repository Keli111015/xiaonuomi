# QUMI 显示控制器服务端

`qm_display_server` 是显示控制器通信协议 V1.0 的屏端实现。它从有序字节流接收
请求，完成校验、分发和响应，并维护两个逻辑屏幕的协议状态。

协议正文以
[`../qm_display_client/PROTOCOL.md`](../qm_display_client/PROTOCOL.md) 为准；服务端只引用该文件，不维护重复副本。

组件负责协议行为；产品相关的 RAM、Flash、图片格式校验、解码和物理屏幕切换通过
后端回调注入。

`qm_display_server_create()` 直接返回服务端句柄；配置非法或内存分配失败时返回
`NULL`。不再使用时调用 `qm_display_server_destroy()` 释放句柄。

## 架构

```text
QUMI 主控 / qm_display_client
              |
              | 协议帧字节流
              v
        read/write 回调
              |
              v
       qm_display_server
       | 大端组帧与 CRC
       | 参数校验与状态机
       | 32 项 / 5 秒事务缓存
              v
         业务后端回调
       |-- RAM / Flash 临时写入
       |-- 格式校验与原子提交
       |-- 单屏或双屏原子切换
       `-- 屏幕开关和亮度控制
```

`qm_display_server_process()` 同步处理一条有效请求，产品可在串口任务或事件循环中
反复调用。损坏帧会被丢弃，当前调用会继续搜索下一帧直到超时。

## 后端约定

- 服务端会先对被替换的临时任务调用 `asset_cancel`；`asset_begin` 从偏移 0 创建新任务。
- `asset_data` 对双屏分片必须原子处理，只有两屏数据都可靠后才能返回 `OK`。
- 调用 `asset_end` 前，服务端先检查接收长度和增量 CRC；`asset_end` 再校验素材格式并
  完成对应的 RAM 或 Flash 操作。
- `asset_cancel` 在任务覆盖或校验失败时释放临时数据，应实现为幂等操作。
- `display` 必须先完成所有素材加载，只有单屏或双屏切换全部完成后才返回 `OK`。
- `set_screen_power` 关闭输出时必须保留当前画面和素材，重复设置当前状态应返回 `OK`。
- `set_brightness` 将 0～100 映射为产品亮度且不改变开关状态，重复设置应返回 `OK`。

回调中的载荷指针仅在回调期间有效。组件会检查传输任务身份、停止等待偏移、接收
长度、整包 CRC、RAM 就绪状态和显示请求结构，再推进协议状态。

## 接入

启用 `CONFIG_QM_DISPLAY_SERVER_SUPPORT`。使用显式源码列表的目标还需加入：

- `components/qm_display_server/src/qm_display_server.c`
- `components/qm_display_server/include`
- `utility/crc/qm_crc.c` 和 `utility/crc`
- `utility/timer/qm_utils_timer.c` 和 `utility/timer`

Kconfig 默认按协议要求缓存至少 32 个事务、保留至少 5 秒。每个有效缓存项持有一份
请求载荷副本；如果 32 项全部是最大素材分片，缓存最多约占用 32 KiB。

## 互测

`tests/test_client_server.c` 通过内存中的分段双向字节流连接真实
`qm_display_client` 和 `qm_display_server`，覆盖完整素材与显示流程、响应丢失后的
重复事务缓存返回、屏幕开关和亮度控制，以及服务端整包 CRC 拒绝。

服务端组件不创建任务、不配置 UART/USB、不维护产品的持久素材索引、不解码图片，
也不直接驱动物理屏幕。

可运行的 UART 服务端示例位于 `examples/qm_display`。
