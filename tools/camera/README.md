# 摄像头预览与实验性坐姿校准

先将当前SDK overlay编译烧录，再运行本工具；旧`JPEG <n>`裸数据固件与本工具的JPG2协议不兼容。

```powershell
python -m pip install -r tools/camera/requirements.txt
python tools/camera/reliable_preview.py
```

打开 http://127.0.0.1:8765。默认COM5、115200；修改端口请编辑`reliable_preview.py`的`uart.port`，不要同时开其他串口工具。

先固定摄像头，选择人物正立的方向，使脸、双眼、双肩完整入镜。方向同时用于显示和识别。坐正后点击“坐正校准”，等待3个稳定合格样本，再点击“检测一次坐姿”。无人、未校准或关键点不可靠时无法判断。

校准在板端RAM保存，重启或识别方向变化会清除；相机位置、距离、使用者变化需重新校准。**真人准确率尚未验收，用户已决定暂缓继续开发。** 未接入自动扣血或语音提醒。

图片仅在本机显示，不上传、不持续录制。识别日志在本地保存且被Git忽略。关闭网页不停止服务，用Ctrl+C停止Python后再烧录。串口抓拍不是流畅视频，过期画面会变淡。

协议按帧编号/偏移重组Base64片段，验证长度及FNV-1a值；校验用于检错，不是安全认证。

测试：`python -m unittest discover -s tools/camera -p "test_*.py"`。
