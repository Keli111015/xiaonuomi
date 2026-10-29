# QM Provision 示例

这个示例演示如何启动独立的 `qm_provision` BLE 配网会话，并在回调中接收解析后的
配网数据。

## 文件

```text
qumi/examples/qm_provision/
├── qm_kconfig
└── qm_provision_example.c
```

## 配置

示例启动前需要配置产品信息：

- `CONFIG_QM_PROVISION_EXAMPLE_PRODUCT_ID`
- `CONFIG_QM_PROVISION_EXAMPLE_PRODUCT_SECRET`
- `CONFIG_QM_PROVISION_EXAMPLE_ADV_CID`，可选，留空时使用组件默认 CID

`qm_provision` 组件自身的 buffer 长度、默认时区和 BLE 释放延迟通过组件 Kconfig 配置。

## 行为

示例只负责启动 BLE 配网并打印回调数据，不负责连接网络、云端注册或 token 管理。
