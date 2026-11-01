# 产品主机测试

这些测试针对当前 `sdk/apps/main/` 的业务代码或规则，不等于完整 ESP-IDF 构建和实机验收。运行时可能调用主机 C/C++ 编译器并在临时目录生成可执行文件。

```powershell
python tools/tests/test_pose_host.py --cxx <g++路径>
python tools/tests/test_servo_host.py --cc <gcc路径>
python tools/tests/test_board_bridge.py --cc <gcc路径>
python -m unittest discover -s tools/tests -p "test_study_voice_guard.py"
```

测试范围包括姿态逻辑、舵机事件、双板桥接和语音计时规则；不能替代真实摄像头、云端、接线或电机动作验收。
