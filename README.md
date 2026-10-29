# MorphCube XJWB03

MorphCube（小诺米）桌面学习陪伴机器人的黑客松板端原型仓库，面向灵机一动 XJWB03（ESP32-S3）开发板。

当前已同步截至2026-10-29的SDK改动：番茄钟/血条适配、连云唤醒、舵机控制、BF3901摄像头、JPG2校验预览和实验性坐姿校准。完整应用已烧录实板；仍有未验收功能，详见[当前交接](docs/CURRENT_HANDOFF.md)。

## 同学从这里开始

1. 克隆本仓库：`git clone https://github.com/Keli111015/xiaonuomi.git`。
2. 单独克隆官方SDK，检出基线`89020f5e04bf777ae1672b8f3d320bc6e0ef3e0c`，按官方文档准备依赖和ESP-IDF。
3. 使用[integration/official-app/README.md](integration/official-app/README.md)中的**当前overlay**，填写自己的本地产品配置，再编译。不要再叠加旧patch。
4. 摄像头网页工具见[tools/camera](tools/camera/README.md)，SDK本机辅助脚本见[tools/sdk-local-helpers](tools/sdk-local-helpers/README.md)。

当前源码覆盖保存在`integration/official-app/sdk-overlay/`，文件清单和哈希见`overlay-manifest.json`。真实产品密钥改为独立的`ljyd_cloud_local.h`，不提交。固件二进制可能包含产品配置，本仓库提供源码而不上传个人设备的固件、Flash备份、照片和日志。

已有早期CAD脚本和参数保存在[cad/morphcube-v1a](cad/morphcube-v1a/README.md)，属于未完成真实装配验收的历史原型。

摄像头新版连续20帧校验通过，无人识别返回无法判断约1.78秒；**真人坐姿准确率尚未验收，用户已决定暂缓该功能。** 舵机GPIO4与摄像头冲突，不要同时启用；servo face是预设角度，不是自动追踪。

```powershell
python -m unittest discover -s tools/camera -p "test_*.py"
python integration/official-app/apply_overlay.py D:\dev\ljyd-sdk
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

本仓库仅复制修改过的SDK文件，不附带完整官方依赖或真实产品密钥。请每位开发者自行克隆官方仓库、配置本地产品身份并设置 `IDF_PATH`。

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
