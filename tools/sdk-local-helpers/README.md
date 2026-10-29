# SDK本机辅助脚本

保留现有开发环境的构建/串口代码。`_idf.py`、`_build_cam.py`默认引用`D:/heikesong/...`工具路径，其他同学需修改路径或使用官方导出的ESP-IDF终端。

`_serial_monitor.py`默认COM5，包含硬件重启，不能与预览同时占用串口。串口日志可能含设备信息，不要直接上传。

`legacy/_cam_snap.py`仅供历史参考，不适配JPG2固件；当前请用`tools/camera/reliable_preview.py`。
