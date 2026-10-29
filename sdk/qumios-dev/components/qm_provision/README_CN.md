# QM Provision

For the English version, see [README.md](README.md).

`qm_provision` 是独立的 BLE 配网组件。它保留原 `qm_cloud` 的 BLE 配网行为，
但不再依赖 `qm_cloud`。

## 功能范围

`qm_provision` 只负责 BLE 配网会话：

- 启动和停止 BLE 配网广播
- 接收原有 BLE spec 配网 payload，并可选兼容位置编码格式
- 将 WiFi 配网数据解析成 `qm_provision_recv_t`
- 通过 `qm_provision_recv_handler_t` 回调上报解析结果

组件不负责网络连接、网络类型选择、云端连接、动态注册、token 管理或数据持久化。
应用层收到回调后自行决定后续处理方式。

## 典型流程

1. 通过 `qm_provision_create()` 创建配网会话。
2. 通过 `qm_provision_start()` 启动 BLE 配网广播。
3. BLE App 按原有 spec payload 下发 WiFi 配网数据。
4. `qm_provision_recv_handler_t` 接收 `ssid`、`password`、`dynreg_host`、
   `timezone`、`audio_uri` 和可选鉴权数据。
5. 应用层处理收到的配网数据。
6. 通过 `qm_provision_stop()` 停止会话，或通过 `qm_provision_destroy()`
   销毁会话。

## 配置项

运行时产品参数通过 `qm_provision_config_t` 传入：

- `product_id`：BLE 鉴权和广播 payload 使用的产品 ID
- `product_secret`：BLE 鉴权使用的产品密钥
- `adv_cid`：BLE 广播 CID 前缀，传 `NULL` 时使用默认值
- `recv_handler`：收到配网数据时触发的回调
- `userdata`：回调用户上下文

固定默认值和限制通过 Kconfig 配置：

- `CONFIG_QM_PROVISION_BLE_ADV_CID`：默认 BLE 广播 CID
- `CONFIG_QM_PROVISION_LOG_CONFIG`：启用组件日志类别配置
- `CONFIG_QM_PROVISION_NTP_TIMEZONE`：BLE payload 未携带时使用的默认时区
- `CONFIG_QM_PROVISION_BLE_DEINIT_DELAY_MS`：BLE 断连到释放之间的延迟时间
- `CONFIG_QM_PROVISION_APINFO_ALT_FORMAT`：标准 spec 解码未得到 SSID 时，
  启用位置编码 APINFO 的兼容解析
- `CONFIG_QM_PROVISION_BLE_LOCAL_NAME_MAX_LEN`：BLE 广播本地名最大内容长度
- `CONFIG_QM_PROVISION_SSID_MAX_LEN`：`ssid` 最大内容长度
- `CONFIG_QM_PROVISION_PASSWORD_MAX_LEN`：`password` 最大内容长度
- `CONFIG_QM_PROVISION_DYNREG_HOST_MAX_LEN`：`dynreg_host` 最大内容长度
- `CONFIG_QM_PROVISION_AUDIO_URI_MAX_LEN`：`audio_uri` 最大内容长度
- `CONFIG_QM_PROVISION_AUTHCODE_MAX_LEN`：鉴权码最大内容长度

产品身份和回调相关参数属于运行时会话输入，不建议放入 Kconfig。

公开接收 buffer 会额外预留一个字节给字符串结尾 `\0`：
`QM_PROVISION_SSID_SIZE`、`QM_PROVISION_PASSWORD_SIZE`、
`QM_PROVISION_DYNREG_HOST_SIZE`、`QM_PROVISION_AUDIO_URI_SIZE` 和
`QM_PROVISION_AUTHCODE_SIZE`。

## 数据字段

`qm_provision_recv_t` 包含从 BLE payload 解析出的配网数据：

- `ssid`：WiFi SSID
- `password`：WiFi 密码
- `dynreg_host`：BLE payload 携带的动态注册 host
- `timezone`：BLE payload 携带的时区，未携带时使用配置的默认时区
- `audio_uri`：BLE payload 携带的音频资源 URI
- `authcode` / `authcode_len`：App Key 或 Network Key 及其有效长度
- `token_type`：BLE v2 协议携带的 token 类型
- `env_type`：BLE APINFO 属性 `0xEC`（v2）或标准服务 `siid=1, piid=12`（v3）携带的环境类型；`0x00` 为开发、`0x01` 为测试、`0x02` 为生产
- `env_type_valid`：是否实际收到 `env_type`，未携带时不可将默认值当作生产环境

接收对象及其中字段只在回调执行期间有效；需要在回调结束后使用的数据必须自行复制。

## 架构

```text
BLE 客户端
    |
    v
qm_ble 传输与鉴权
    |
    v
qm_provision payload 解析与会话生命周期
    |
    v
应用回调 -> WiFi / 云连接 / 持久化策略
```

`qm_provision` 只依赖 QUMI 的 BLE、spec、工作队列、内存和 OS 抽象；
厂商 BLE 与 RTOS 细节仍由平台适配层承接。

## 基本用法

```c
#include "qm_provision.h"

static void provision_recv_handler(qm_provision_handle_t handle,
                                   const qm_provision_recv_t *recv,
                                   void *userdata)
{
    (void)handle;
    (void)userdata;

    /* 在这里处理 recv 中的配网数据。 */
}

void app_provision_start(void)
{
    qm_provision_config_t config = {
        .product_id = 123456,
        .product_secret = "product-secret",
        .adv_cid = NULL,
        .recv_handler = provision_recv_handler,
        .userdata = NULL,
    };
    qm_provision_handle_t handle;

    handle = qm_provision_create(&config);
    if (handle == NULL) {
        return;
    }

    (void)qm_provision_start(handle);
}
```

## 生命周期

- `qm_provision_create()` 创建一个 BLE 配网会话句柄。
- `qm_provision_start()` 启动 BLE 广播；同一个已启动句柄重复调用会直接返回成功；
  上一次停止仍在等待 BLE 断连时返回 `-QM_EBUSY`。
- 当前只支持一个 active BLE 配网会话。
- `qm_provision_stop()` 停止广播并断开当前 BLE central，但保留会话句柄，
  后续可以再次启动。
- `qm_provision_destroy()` 停止会话、释放 BLE 并销毁句柄；若 central 仍连接，
  销毁会在断连后完成。

## 错误码

- `QM_EOK`：操作成功
- `-QM_EINVAL`：句柄或配置无效
- `-QM_EBUSY`：已经存在另一个 active BLE 配网会话

## 相关示例

- `qumi/examples/qm_provision/qm_provision_example.c`
- `apps/opencpu/app/qm_provision_example.c`
