# MorphCube XJWB03

MorphCube（小诺米）桌面学习陪伴机器人的板端原型仓库，面向灵机一动 XJWB03（ESP32-S3）开发板。

当前版本完成了可在实板运行的番茄钟与血条状态机，用于团队继续集成语音、摄像头姿态识别和灯效。它不是最终产品固件。

## 当前已经可用

- 25 分钟专注计时：开始、暂停、继续、停止、新一轮。
- 初始 100 HP；确认分心扣 10 HP；60 秒内重复事件不重复扣血。
- HP 归零后进入强制休息，新一轮才能恢复。
- 异常姿态连续 15 秒才提醒；提醒间隔 60 秒；姿态提醒不扣血。
- 纯主板串口验证程序，不启用 Wi-Fi、云端、摄像头、麦克风、电机或灯板。

## 目录

```text
firmware/
  mc_study.c/.h          核心状态机
  test_mc_study.c        C 断言测试
  board_bench/           ESP-IDF 实板串口测试工程
docs/HANDOFF.md          硬件结果、已知边界与后续任务
```

## 依赖

1. 官方仓库：`https://gitee.com/xj-open/ljyd_xjwb03_agent_public_developer`
2. 使用官方仓库自带的 ESP-IDF 5.4.x 环境。
3. 目标芯片为 ESP32-S3，实测板载 8 MB PSRAM、16 MB Flash。

本仓库不复制官方 SDK、设备凭证或产品配置。请每位开发者自行克隆官方仓库并设置 `IDF_PATH`。

## 编译（不会自动烧录）

在已经导出 ESP-IDF 环境的终端中：

```text
cd firmware/board_bench
idf.py build
```

不要直接运行官方根目录的 `build.bat`；该脚本包含擦除、烧录和串口监视步骤。烧录前应备份目标开发板现有固件并核对分区。

## 串口命令

UART0：115200、8N1，每条命令以换行结束。

| 命令 | 作用 |
|---|---|
| `status` | 查询当前状态 |
| `start` | 开始 25 分钟专注 |
| `pause` / `resume` | 暂停或继续 |
| `distracted` | 人工确认分心，按规则扣血 |
| `stop` | 停止并保留本轮统计 |
| `new` | 非运行状态下新建一轮并恢复 100 HP |

## 重要边界

- 当前 `distracted` 是人工测试命令，不代表已经实现自动开小差识别。
- 摄像头坐姿识别、口语练习、学习锐评、灯板显示和持久化尚未接入。
- 原厂固件备份不进入 Git 仓库，避免发布设备数据。

详细交接见 [docs/HANDOFF.md](docs/HANDOFF.md)。
