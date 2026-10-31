# MorphCube 内置 SDK

本目录是小诺米项目整合后的 XJWB03 / ESP32-S3 SDK，包含 ESP-IDF 5.4.1、QUMIOS、应用源码、模型与资源。当前应用在 `apps/main/`，工程在 `apps/`。

2026-10-31 已将重复的安装、构建和调试说明统一到项目文档；原有 SDK 入口保留导航。当前功能不是上游默认示例，勿重新克隆上游覆盖本目录。

| 需要做什么 | 文档 |
| --- | --- |
| 了解小诺米 | [项目首页](../README.md) |
| 查看进度与阻塞 | [当前交接](../docs/CURRENT_HANDOFF.md) |
| 配置环境和本地产品身份 | [完整工程使用说明](../docs/FULL_REPOSITORY.md) |
| 查构建、烧录、串口命令 | [常见指令](../常见指令.md) |
| 查看应用结构与宏开关 | [应用架构](apps/README_CN.md) |
| 查第三方许可和基线 | [第三方说明](../THIRD_PARTY_NOTICES.md)、[来源元数据](../docs/SDK_PROVENANCE.json) |
| 查全部项目文档 | [文档导航](../docs/README.md) |

注意：`build.bat` 会构建后烧录并启动监视器；`export.ps1` 会应用 ADF 补丁；`dev.bat` 部分命令会删除缓存或擦除设备。具体副作用以统一使用说明和实际脚本为准。本轮文档整理未执行这些入口。

官方及第三方 LICENSE、NOTICE、组件内部说明保留在对应目录，各自许可继续适用。
