# 完整工程使用说明

从仓库根目录运行以下命令。产品工程是 `sdk/apps/`，应用源码在 `sdk/apps/main/`；仓库附带 ESP-IDF 5.4.1、QUMIOS、模型和第三方组件，但需要另行安装 ESP32-S3 工具链与 Python 构建环境。

## 首次配置

```powershell
.\sdk\esp-idf\install.bat esp32s3
. .\sdk\export.ps1
Copy-Item sdk\apps\main\cloud\ljyd_cloud_local.example.h sdk\apps\main\cloud\ljyd_cloud_local.h
```

只在本地身份文件不存在时复制模板；编辑 `ljyd_cloud_local.h` 填写自己的产品 PID 和密钥，不要提交真实密钥。`SECERT` 是现有宏名，不能自行更名。新终端需重新运行 `. .\sdk\export.ps1`，它会配置 IDF 环境并应用 ADF 补丁。导出后用 `python -c "import sys, click; print(sys.executable)"` 确认使用 IDF 的 Python 环境；若仍指向 Conda 环境且缺少 `click`，先修复 IDF 安装/导出，而非只安装单个包。

## 构建与烧录

```powershell
.\build_full_sdk.ps1
```

此命令只构建、不烧录；`-ConfigureOnly` 会执行 `idf.py reconfigure`，也会写入构建目录。应用产物在 `sdk/apps/build/`，镜像名 `vb701n.bin`。`sdk/build.bat` 会构建、烧录并启动监视器；`sdk/dev.bat` 有清理缓存、擦除及烧录设备的子命令，运行前核对目标串口和副作用。

在已导出环境的终端中可打开 ESP-IDF 工程：`code .\sdk\apps`。不要将其他 IDF 安装覆盖仓库内的 `sdk/esp-idf/`。

## 分区与设备数据

分区布局以 `sdk/apps/partitions.csv` 为准。首次上板、擦除或修改分区后，核对构建输出的全部镜像与模型分区；只更新应用镜像并不等于已恢复模型。擦除或清 NVS 会删除配网和绑定数据；不要将设备密钥、Flash 备份或日志提交到仓库。

应用架构见[应用说明](../sdk/apps/README_CN.md)，学习规则见[事件流](STUDY_RUNTIME.md)，第三方来源与许可见[第三方说明](../THIRD_PARTY_NOTICES.md)和[来源元数据](SDK_PROVENANCE.json)。
