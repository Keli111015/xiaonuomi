# 项目文档导航

此处收录产品构建、应用架构和业务规则文档；第三方组件内的许可和使用说明保留在各自目录。

## 推荐阅读顺序

| 文档 | 职责 |
| --- | --- |
| [项目首页](../README.md) | 项目用途、核心能力、工程入口 |
| [项目实践复盘](PROJECT_CASE_STUDY.md) | 产品场景、技术取舍、验证记录与团队项目经历 |
| [完整工程使用说明](FULL_REPOSITORY.md) | 环境、产品配置、开发入口、构建与恢复说明 |
| [应用架构](../sdk/apps/README_CN.md) | 启动顺序、模块边界、云端调用链 |
| [学习规则与事件流](STUDY_RUNTIME.md) | 番茄钟、HP、聊天、坐姿与提醒的源码规则 |

## 专题与工具

- [专注结束摆头提醒](FOCUS_SERVO_REMINDER.md)：事件条件和动作序列。
- [双板通信](BOARD_BRIDGE.md)：配对、指令转发及 ACK 语义。
- [主机测试说明](../tools/tests/README.md)：哪些测试会调用 C/C++ 编译器。

## SDK 业务模块

- [番茄钟接入](../sdk/apps/main/lj_components/pomodoro/README.md)
- [应用层闹钟](../sdk/apps/main/lj_components/alarm/README_CN.md)
- [物模型与音量](../sdk/apps/main/lj_components/iot_control/README_CN.md)
- [课程表](../sdk/apps/main/lj_components/course/README_CN.md)
- [资源播放](../sdk/apps/main/lj_components/playback/README_CN.md)及[接口使用示例](../sdk/apps/main/lj_components/playback/resource_playback.md)

## 来源

- [SDK 来源元数据](SDK_PROVENANCE.json)、[第三方说明](../THIRD_PARTY_NOTICES.md)：保留上游基线、来源和许可。
- [SDK 导航](../sdk/README.md)：工程目录和依赖说明。

## 展示素材

- [小诺米概念海报](assets/lil-noemie-poster.png)：README 项目封面，原图由项目维护者提供。
