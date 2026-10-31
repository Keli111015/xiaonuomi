# 项目文档导航

更新日期：2026-10-31。本轮整理依据工作树源码和已有记录，只做静态核对，未执行编译、CMake 配置生成、烧录或硬件测试。

## 推荐阅读顺序

| 文档 | 职责 |
| --- | --- |
| [项目首页](../README.md) | 项目用途、核心能力、工程入口 |
| [当前状态与交接](CURRENT_HANDOFF.md) | 当前进度、已知问题、待验收事项的统一入口 |
| [完整工程使用说明](FULL_REPOSITORY.md) | 环境、产品配置、开发入口、构建与恢复说明 |
| [常见指令](../常见指令.md) | PowerShell、串口、烧录及 CLI 操作速查 |
| [应用架构](../sdk/apps/README_CN.md) | 启动顺序、模块边界、云端调用链 |
| [学习规则与事件流](STUDY_RUNTIME.md) | 番茄钟、HP、聊天、坐姿与提醒的源码规则 |

## 专题与工具

- [坐姿识别](POSE_RECOGNITION.md)：校准、识别、采样、已知限制和验收清单。
- [舵机 P6 接线](SERVO_P6_WIRING.md)：供电、引脚冲突、手动调试。
- [专注结束摆头提醒](FOCUS_SERVO_REMINDER.md)：事件条件、动作序列和历史验证。
- [摄像头网页工具](../tools/camera/README.md)：安装、串口设置、JPG2 和校准兼容性。
- [SDK 本机辅助脚本](../tools/sdk-local-helpers/README.md)：本机路径依赖及副作用。
- [主机测试说明](../tools/tests/README.md)：哪些测试会调用 C/C++ 编译器。
- [早期 CAD 原型](../cad/morphcube-v1a/README.md)：结构参数和装配边界。

## SDK 业务模块

- [番茄钟接入](../sdk/apps/main/lj_components/pomodoro/README.md)
- [应用层闹钟](../sdk/apps/main/lj_components/alarm/README_CN.md)
- [物模型与音量](../sdk/apps/main/lj_components/iot_control/README_CN.md)
- [课程表](../sdk/apps/main/lj_components/course/README_CN.md)
- [资源播放](../sdk/apps/main/lj_components/playback/README_CN.md)及[接口使用示例](../sdk/apps/main/lj_components/playback/resource_playback.md)

## 历史与来源

- [早期板端验证记录](HANDOFF.md)：2026-10-24 / 25 的历史结果，不作为当前行为定义。
- [SDK 来源元数据](SDK_PROVENANCE.json)、[第三方说明](../THIRD_PARTY_NOTICES.md)：保留上游基线、来源和许可。
- [SDK 导航](../sdk/README.md)、[Windows 旧入口](../sdk/WINDOWS_BUILD_CN.md)、[dev.bat 说明](../sdk/DEV_BAT_CN.md)：兼容原路径，统一链接至维护中的说明。

## 维护规则

1. 当前行为以 `sdk/apps/main/` 源码为依据；“代码已接入”“历史测试通过”“当前实机验收通过”分别记录。
2. 进度和阻塞项集中在 `CURRENT_HANDOFF.md`，规则集中在专题文档，操作集中在使用说明与速查表，避免重复复制整段说明。
3. 历史记录保留日期、测试范围和限制，不将旧固件大小、哈希或测试结论写成当前保证。
4. 项目入口与应用层说明由本项目维护；ESP-IDF、第三方组件、模型内部文档以及 LICENSE / NOTICE / COPYING 保留其来源语义。
5. 文档中出现命令不代表已执行。特别是 `-ConfigureOnly` 仍运行 CMake 配置，主机测试也可能调用编译器；禁止编译时不能运行这些命令。
