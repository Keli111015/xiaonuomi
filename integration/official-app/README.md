# MorphCube 官方语音工程适配

> 2026-10-29：完整SDK现已内置在仓库`sdk/`，新接手请按[完整工程说明](../../docs/FULL_REPOSITORY.md)直接构建。下方overlay接入保留供外部SDK及历史快照参考，不要对内置SDK再次应用。

## 当前接入方式（2026-10-29）

使用`apply_overlay.py`和`sdk-overlay/`，覆盖全部当前SDK改动。下方“阶段1”及旧patch仅为历史记录，**不要在当前overlay上重复应用旧patch**。

在干净的官方SDK基线`89020f5e04bf777ae1672b8f3d320bc6e0ef3e0c`上：

```powershell
python integration/official-app/apply_overlay.py D:\dev\ljyd-sdk
python integration/official-app/apply_overlay.py D:\dev\ljyd-sdk --apply
```

默认只检查基线和文件哈希；`--apply`才复制代码，且拒绝覆盖已有本地改动。不会编译、擦除或烧录。

应用后，将SDK的`apps/main/cloud/ljyd_cloud_local.example.h`复制为同目录`ljyd_cloud_local.h`，填自己的产品PID和密钥（上游宏拼写`SECERT`需保留）。工具会在SDK本地Git排除列表忽略真实配置。示例含编译阻断，未填写不能直接编译。

然后在官方ESP-IDF环境中进入SDK的`apps`目录执行`idf.py build`。实际云端Agent和权限不在本仓库内，需各开发者自行准备。摄像头/舵机冲突和测试边界见[当前交接](../../docs/CURRENT_HANDOFF.md)。

## 以下为阶段1历史说明

这一层把已验证的 MorphCube 学习状态机接到灵机一动官方 Pomodoro 生命周期中。它暂不接摄像头，目的是先把“专注计时、开小差扣血、血量归零自动暂停”这一条链路做稳。

## 版本基线

- 官方仓库：`xj-open/ljyd_xjwb03_agent_public_developer`
- 已验证基线提交：`89020f5e0`（完整 SHA 以官方仓库 `git rev-parse HEAD` 为准）
- ESP-IDF：官方仓库自带版本
- 路径要求：请使用纯英文、无空格目录；中文路径会导致当前工具链出现编码错误。

## 已实现行为

- 官方 Pomodoro 是唯一计时来源，不另起一套倒计时。
- 开始新一轮专注时 HP 重置为 100。
- 收到一次“确认开小差”事件扣 10 HP，同一事件 60 秒冷却，避免连续误扣。
- HP 归零后自动暂停官方 Pomodoro。
- 暂停、休息、结束期间不扣 HP。
- 姿态提醒不扣 HP；摄像头输出将在下一阶段通过队列接入。
- 已提供 `morphcube_study_runtime_submit_pose()` 新帧入口：正常、歪斜、低头、未知四种输入都通过同一队列串行处理。
- SDK 定时回调只做非阻塞入队，所有 HP 状态由单独任务串行修改。

## 接入官方工程

1. 确认官方仓库处于基线提交，且下面三个文件没有未提交修改：
   - `apps/main/CMakeLists.txt`
   - `apps/main/lj_components/pomodoro/pomodoro.c`
   - `apps/main/qm_config/qm_config.h`
2. 将本目录的 `morphcube/` 整个复制为：
   `apps/main/lj_components/morphcube/`
3. 在官方仓库根目录执行：

   ```powershell
   git apply --check <MorphCube仓库>\integration\official-app\official-89020f5e0.patch
   git apply <MorphCube仓库>\integration\official-app\official-89020f5e0.patch
   ```

4. 仅编译验证：进入官方 `apps` 工程目录并运行 `idf.py build`。不要运行根目录中会自动擦除、烧录或打开监视器的批处理。

## 联调命令

官方云端 `pomodoro` 下行方法支持：

- `start`：新一轮专注，HP=100
- `pause`：暂停
- `resume`：继续
- `stop`：停止
- `distracted`：模拟一次“已确认开小差”

串口日志关键字：`morphcube_study`。示例结果：

```text
runtime ready hp=100
distraction accepted hp=90 penalties=1
distraction ignored active=1 hp=90
```

## 当前边界

- 还没有把 HP 和阶段状态上报到控制面板；状态读取接口已经保留。
- 还没有把摄像头三姿态结果接入；姿态提醒规则仍由 `mc_study` 保存。
- 每次真实的新推理结果只能调用一次姿态入口；无人物、关键点不足或断帧必须提交 `UNKNOWN`，不能复用上一帧。
- 本段为阶段1记录；当前版本完整应用已烧录，详见上方当前交接。新目标设备仍需先备份并核对分区。
