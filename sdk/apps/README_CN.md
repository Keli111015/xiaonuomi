# 应用架构与开发入口

2026-10-31 按 `main/main.c`、配置及业务源码静态整理；未编译或实机验证。当前问题见[交接](../../docs/CURRENT_HANDOFF.md)，完整文档见[导航](../../docs/README.md)。

## 目录

```text
main/
  main.c                       app_main → qm_main；qm_application_start 组装模块
  qm_config/qm_config.h        功能、语音、串口与业务参数
  cloud/                       动态注册、MQTT、BLE 配网、方法路由、对时
  voice/                       VB701N 音频与本地音效
  agent/                       Agora Agent、唤醒、聊天/计时指令识别
  peripheral/                  GPIO、舵机、摄像头与调试抓拍
  pose/                        YOLO11 推理、校准、视觉服务
  lj_components/
    morphcube/                 学习状态机及独立运行任务
    pomodoro/                  官方计时器接入与云端控制
    alarm/、course/             闹钟与课程表
    playback/、iot_control/     资源播放与音量等属性控制
```

产品身份由 `cloud/ljyd_cloud_local.h` 提供，使用同目录 example 模板，不在公共 README 内放产品密钥。配置步骤见[完整工程说明](../../docs/FULL_REPOSITORY.md)。

## 当前启动顺序

`app_main()` 进入 `qm_main()`，由框架调用 `qm_application_start()`：

1. 姿态功能开启时预分配缓冲、初始化 YOLO11 和视觉服务，以便在网络前处理内存分配。
2. 注册舵机 CLI，此时不启动 PWM；初始化工作机制及板级 IO。
3. 摄像头功能开启时早期初始化并注册 CLI；MPU6050 因 GPIO8 冲突保持未启用。
4. 启动 voice；资源播放开启时调用无参 `playback_init()`。
5. 初始化 cloud、物模型控制、课程表、配网及 Agent。
6. 按功能开关初始化闹钟与番茄钟。

旧文档中的“必须先语音芯片唤醒才 start”已不完整：当前 `agent_app.c` 在云连接事件后直接调用 `on_wakeup()` 自动唤醒，也保留语音唤醒和串口 `wake` 路径。云断开会清理会话状态；实际可用性依赖云端配置。

## 当前宏开关

来源为 `main/qm_config/qm_config.h`；这里只记录静态值，不表示模块已通过验收。

| 宏 | 值 | 作用 |
| --- | --- | --- |
| `CONFIG_VOICE_BACKEND_AGORA` | 1 | Agora 语音后端 |
| `CONFIG_QM_ALARM_SUPPORT` | 1 | 闹钟 |
| `CONFIG_LJYD_PLAYBACK_SUPPORT` | 1 | 应用资源播放 |
| `CONFIG_QM_POMODORO_SUPPORT` | 1 | 番茄钟 |
| `CONFIG_APP_POMODORO_AUTO_START` | 0 | 上电不自动开始计时 |
| `CONFIG_QM_POSE_SUPPORT` | 1 | 姿态推理 |
| `CONFIG_APP_CAMERA_TEST` | 1 | 摄像头 |
| `CONFIG_QM_IOT_OTA_SUPPORT` | 0 | 当前不启用 IoT OTA；有双分区不等于已实现升级流程 |

CMake 读取配置宏决定部分源码是否加入，调用点也有条件编译。新增功能应同步源文件列表、头文件路径、初始化和路由，不能只改一个调用点。

## 云端与学习链路

- `ljyd_provision` 负责 Wi-Fi / BLE，网络可用后云模块处理注册、MQTT 和连接事件。
- 属性控制经 `iot_control` 的 siid / piid 表，当前主要是音量；method 命令经业务路由，二者不要混用。
- `pomodoro` 单独注册 method，下发支持 start / stop / pause / resume。Agent 说“开始了”并不证明设备方法已执行。
- 计时事件、聊天事件与姿态结果通过队列交由学习任务处理 HP、音效和结束提醒，详见[学习规则](../../docs/STUDY_RUNTIME.md)。

## device/up 与 device/down 方法

实现见 `main/cloud/ljyd_method.c`。本地主动上报可调用 `ljyd_method_up(method, submethod, params_json)`；空参数会使用带 `source=local` 的对象。报文 id 由 SDK 生成。

路由表维护 method、submethod、处理函数及应答标记。精确子方法优先；标记为需要应答时以处理返回值形成 Reply，已有 Reply 不再次应答；本地回环按 source 判断。MQTT 回调只解析和提交，不阻塞等待语音、下载或同步查询。

详细模块说明见[闹钟](main/lj_components/alarm/README_CN.md)、[物模型](main/lj_components/iot_control/README_CN.md)、[课表](main/lj_components/course/README_CN.md)、[资源播放](main/lj_components/playback/README_CN.md)和[番茄钟](main/lj_components/pomodoro/README.md)。
