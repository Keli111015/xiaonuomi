# 完整工程使用说明

本仓库已经内置官方基线SDK及我们的现有改动。**不再需要单独克隆Gitee仓库，也不用应用overlay。**

## 已包含

`sdk/apps/`完整应用、`sdk/qumios-dev/`、`sdk/components/`、`sdk/esp-idf/`（5.4.1源码）、`sdk/apps/managed_components/`现有依赖快照、模型/音频资源、原始配件文档、官方工具及许可证。共29899个受控文件，源文件约835MiB。Git下载体积取决于压缩。

模型和第三方预编译库是构建输入，随SDK保留；个人设备固件、Flash备份、编译输出、照片、原始串口日志和真实产品密钥不在仓库中。已有ESP-IDF源码不代表编译器已安装，首次使用仍需下载工具链，组件管理器也可能访问注册表求解依赖。

## Windows快速开始

在英文、无空格短路径克隆，例如`D:\dev\xiaonuomi`：

```powershell
git clone https://github.com/Keli111015/xiaonuomi.git
cd xiaonuomi
```

首次安装ESP-IDF工具链（已经装好相同工具版本可复用）：

```powershell
.\sdk\esp-idf\install.bat esp32s3
. .\sdk\esp-idf\export.ps1
```

然后复制产品配置模板，填写各自产品的PID和密钥。宏名`SECERT`是上游拼写，不能随意改名。

```powershell
Copy-Item sdk\apps\main\cloud\ljyd_cloud_local.example.h sdk\apps\main\cloud\ljyd_cloud_local.h
notepad sdk\apps\main\cloud\ljyd_cloud_local.h
.\build_full_sdk.ps1
```

本地真实配置已被`.gitignore`排除。示例未填写会主动阻止编译。自己的云平台Agent配置和权限仍需自行准备。

构建入口只编译，不擦除、不烧录。检查CMake配置可运行`.\build_full_sdk.ps1 -ConfigureOnly`。编译产物在`sdk/apps/build/`，不会提交。

不要把官方原有`build.bat`当作单纯编译命令，它可能包含擦除、烧录和串口监视。向新板烧录前先备份并核对`sdk/apps/partitions.csv`；不要对已有设备执行全片擦除。

## 摄像头工具

```powershell
python -m pip install -r tools/camera/requirements.txt
python tools/camera/reliable_preview.py
```

打开 http://127.0.0.1:8765，默认COM5，修改端口见工具说明。串口不能同时被监视器占用。

## 开发与来源

直接修改`sdk/apps/main/`即可，原overlay现在只作为上次同步的历史快照。不要同时修改两份代码后期待自动同步。新的日常开发以`sdk/`为准。

官方来源、基线和文件规模见[SDK_PROVENANCE.json](SDK_PROVENANCE.json)。原始SDK说明保留在[sdk/README.md](../sdk/README.md)，其中获取代码的路径是官方原文，以本文的当前工程入口为准。

许可说明见[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)，保留各组件原有许可证和版权声明。功能验收状态见[CURRENT_HANDOFF.md](CURRENT_HANDOFF.md)。
