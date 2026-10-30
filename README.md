# MorphCube XJWB03

MorphCube（小诺米）桌面学习陪伴机器人的黑客松板端原型仓库，面向灵机一动 XJWB03（ESP32-S3）开发板。

当前已同步截至2026-10-29的SDK改动：番茄钟/血条适配、连云唤醒、舵机控制、BF3901摄像头、JPG2校验预览和实验性坐姿校准。完整应用已烧录实板；仍有未验收功能，详见[当前交接](docs/CURRENT_HANDOFF.md)。

## 同学从这里开始

1. 克隆本仓库：`git clone https://github.com/Keli111015/xiaonuomi.git`，官方完整源码已在`sdk/`，不用再克隆Gitee。
2. 按[完整工程构建说明](docs/FULL_REPOSITORY.md)安装工具链、导出ESP-IDF环境。
3. 填写自己的`ljyd_cloud_local.h`，运行`.\build_full_sdk.ps1`。日常代码直接修改`sdk/apps/main/`，不用再应用overlay或旧patch。
4. 摄像头网页工具见[tools/camera](tools/camera/README.md)，SDK本机辅助脚本见[tools/sdk-local-helpers](tools/sdk-local-helpers/README.md)。

完整工程包含官方应用、QUMIOS、ESP-IDF、现有组件依赖、模型和资源。来源及许可证见[第三方说明](THIRD_PARTY_NOTICES.md)。`integration/official-app/sdk-overlay/`保留为上次同步快照，新开发以`sdk/`为准。真实产品密钥在独立的`ljyd_cloud_local.h`中，不提交；个人设备的固件、Flash备份、照片和日志也不上传。

已有早期CAD脚本和参数保存在[cad/morphcube-v1a](cad/morphcube-v1a/README.md)，属于未完成真实装配验收的历史原型。

摄像头新版连续20帧校验通过，无人识别返回无法判断约1.78秒；**真人坐姿准确率尚未验收，用户已决定暂缓该功能。** 当前主工程已将舵机信号迁到P6，摄像头保留P4，接线见[舵机P6说明](docs/SERVO_P6_WIRING.md)；servo face是预设角度，不是自动追踪。

```powershell
python -m unittest discover -s tools/camera -p "test_*.py"
.\build_full_sdk.ps1 -ConfigureOnly
```

## 核心状态机与早期验证

- 25 分钟专注计时：开始、暂停、继续、停止、新一轮。
- 初始 100 HP；确认分心扣 10 HP；60 秒内重复事件不重复扣血。
- HP 归零后进入强制休息，新一轮才能恢复。
- 异常姿态连续 15 秒才提醒；提醒间隔 60 秒；姿态提醒不扣血。
- 纯主板串口验证程序，不启用 Wi-Fi、云端、摄像头、麦克风、电机或灯板。
- 官方 Pomodoro 适配：事件回调只入队，独立任务管理 HP；HP 归零自动暂停官方计时器。
- 官方完整固件编译通过：`vb701n.bin` 约 3.10 MB，4,032 KB 应用分区剩余约 984 KB（24%）。

## 目录

```text
firmware/
  mc_study.c/.h          核心状态机
  test_mc_study.c        C 断言测试
  board_bench/           ESP-IDF 实板串口测试工程
integration/official-app 官方语音工程适配源码、补丁与接入说明
docs/HANDOFF.md          硬件结果、已知边界与后续任务
```

## 依赖

1. 官方仓库：`https://gitee.com/xj-open/ljyd_xjwb03_agent_public_developer`
2. 使用官方仓库自带的 ESP-IDF 5.4.x 环境。
3. 目标芯片为 ESP32-S3，实测板载 8 MB PSRAM、16 MB Flash。

本仓库已内置官方SDK源码及依赖快照；每位开发者仍需安装编译工具链、配置自己的本地产品身份。工具链和组件求解可能需要联网。

> Windows 路径要求：官方随包 ESP-IDF 的部分脚本在中文用户目录下存在编码问题。请把官方仓库、本仓库和 `IDF_TOOLS_PATH` 都放在不含中文或空格的短路径，例如 `D:\dev\ljyd_xjwb03_agent_public_developer`、`D:\dev\MorphCube-XJWB03` 和 `D:\esp-tools`，不要直接在 `C:\Users\中文用户名\...` 中安装工具或构建。

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
- 摄像头取流及坐姿实验代码已加入完整应用，但尚未接入HP姿态事件队列。场景口语、学习总结、控制面板、灯板显示及持久化仍需开发或验收。
- 原厂固件备份不进入 Git 仓库，避免发布设备数据。

官方整包接入见 [integration/official-app/README.md](integration/official-app/README.md)，详细交接见 [docs/HANDOFF.md](docs/HANDOFF.md)。

## 团队协作

- 所有人都可以克隆或 Fork 这个公开仓库。
- 未被仓库所有者邀请为 collaborator 的同学不能直接向 `main` 推送；应从自己的 Fork 提交 Pull Request。
- 需要直接推送时，由仓库所有者在 GitHub 的 Collaborators 设置中邀请对应账号，并建议使用功能分支，不直接改 `main`。
