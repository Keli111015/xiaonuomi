# 早期板端验证记录

本文件归档 2026-10-24 / 25 的交接信息，不再描述当前功能。当前状态见[当前交接](CURRENT_HANDOFF.md)，当前规则见[学习规则](STUDY_RUNTIME.md)。

## 2026-10-24：独立串口验证固件

当时实测开发板为 XJWB03 / ESP32-S3 revision 0.2，8 MB PSRAM、16 MB Flash、DIO 80 MHz，串口芯片为 CH343。

旧测试固件完成启动及串口交互，三个写入分区通过 esptool 哈希校验。历史流程：

1. 上电 `idle`，25 分钟、100 HP；`start` 后进入 `focus`，倒计时递减。
2. 人工 `distracted` 从 100 扣到 90；当时 60 秒内重复事件忽略。
3. `pause` 冻结倒计时，`resume` 继续；`stop` 保留统计，`new` 恢复一轮。
4. 当时坐姿只提醒、不扣 HP；固件未启用摄像头和云端。

这些都是旧版行为，不能移用为当前 SDK 的验收结论。当前暂停流血、聊天去抖及姿态扣血逻辑已变化。

历史串口输出示例：

```json
{"result":"ok","mode":"board_bench","phase":"focus","remaining_ms":1499499,"hp":90,"distractions":1,"camera":false,"cloud":false}
```

`camera:false` 和 `cloud:false` 表示旧独立固件没有这些能力，不是当前完整应用的能力标志。

## 2026-10-25：官方工程早期适配

当时基于上游 `89020f5e04bf777ae1672b8f3d320bc6e0ef3e0c` 接入学习状态任务，官方 Pomodoro 保持唯一时钟；生命周期事件通过非阻塞队列交给独立任务。原记录称整包编译 0 错误，新增 MorphCube 文件无警告，当时尚未烧录。

旧适配资料曾位于 `integration/official-app/`，该目录已不在当前工作树中。源码已整合到 `sdk/`，不再按旧路径操作。

## 如何解释这些记录

- 后续的实板写入、舵机和坐姿变更见当前交接及对应专题文档。
- 历史构建成功不能证明当前版本成功；2026-10-31 文档整理未进行任何编译或硬件验证。
- 独立 `firmware/board_bench` 保留为早期验证工程，其接口与当前 `mc_study` 副本需另行同步确认。
