# 主机测试说明

2026-10-31 更新。已运行摄像头目录的 10 项纯 Python 测试；未运行调用 C/C++ 编译器的测试。

## 测试范围及副作用

| 测试 | 覆盖范围 | 是否调用编译器 |
| --- | --- | --- |
| `test_pose_host.py` | 从生产 C++ 抽取实际校准/分类逻辑，用合成关键点验证 | 是，`g++`，支持 `--cxx` |
| `test_servo_host.py` | 舵机代码、模拟 PWM/任务和事件组合 | 是，支持 `--cc` |
| `test_study_voice_guard.py` | 番茄钟相关文本与聊天、状态 TTS 识别规则 | 是，直接调用 `gcc` |
| `../../firmware/test_mc_study.c` | 学习状态机断言测试 | 是，C 源码需编译，链接哪份状态机以实际命令为准 |
| `../camera/test_frame_protocol.py` | Python JPG2 帧重组和检错 | 否，不连接硬件 |
| `../camera/test_preview_detection.py` | 提取实际网页请求函数，模拟返回值验证校准/重校准/失败提示 | 否，不导入串口或启动预览服务 |

禁止编译时不能运行前三类 Python 测试，也不要对本目录直接执行宽泛的 unittest discover。Python 测试文件并不意味着纯 Python 执行。

## 允许编译时的入口示例

以下只供后续经允许的测试任务参考；本轮未执行：

```powershell
python tools/tests/test_pose_host.py --cxx <g++路径>
python tools/tests/test_servo_host.py --cc <gcc路径>
python -m unittest discover -s tools/tests -p "test_study_voice_guard.py"
```

这些测试可能在临时目录生成并运行可执行文件，不等于完整 ESP-IDF 构建，也不覆盖真实相机关键点误差、RTOS 并发、云端行为、接线和物理转角。

`firmware/test_mc_study.c` 已整合冲突并补充 10 秒采样、25 秒间隔边界、时钟回退、正常/UNKNOWN、暂停及 60 秒冷却断言；尚未编译执行，后续应分别链接独立及 SDK 生产状态机验证。

历史“通过”记录保留在[坐姿说明](../../docs/POSE_RECOGNITION.md)和[摆头提醒](../../docs/FOCUS_SERVO_REMINDER.md)。旧测试结果不能当作当前 C/C++ 代码的复测结果。
