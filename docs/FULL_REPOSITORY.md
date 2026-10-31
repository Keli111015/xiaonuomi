# 完整工程使用说明

适用工作树：MorphCube / 小诺米，2026-10-31 文档静态整理。所有示例从项目根目录执行，除非另有说明。本文保留构建操作供后续使用；本轮没有执行任何编译、配置生成、烧录或擦除。

## 工程与前置条件

仓库内置 `sdk/apps/`、`sdk/qumios-dev/`、`sdk/esp-idf/`（5.4.1 源码）、组件、模型、音频及原始配件资料。已有源码不代表已安装 Xtensa 编译器、CMake、Ninja 和 IDF Python 环境。首次安装及组件求解可能联网。

- Windows 下准备 Git for Windows 和 ESP-IDF 5.4.1 所需的 Python；确认不是 Microsoft Store 的空应用别名。
- 项目、用户工具链目录优先使用英文、无空格的短路径，例如 `D:\dev\xiaonuomi`、`D:\esp-tools`。
- 目标是 ESP32-S3；历史板卡为 16 MB Flash、8 MB PSRAM。
- 日常业务修改 `sdk/apps/main/`；无需另克隆 Gitee，也无需 overlay。
- 先阅读[当前交接](CURRENT_HANDOFF.md)：摄像头及相关业务修复尚未编译或实测，本说明不保证原样构建成功。

## 首次配置

以下是供未来操作的步骤，本轮不执行。

```powershell
# 仅首次安装工具链时使用
.\sdk\esp-idf\install.bat esp32s3

# 每个新终端导出环境；该 SDK 脚本还会应用 ADF 补丁
. .\sdk\export.ps1

# 仅在本地配置文件尚不存在时复制，避免覆盖已有身份
Copy-Item sdk\apps\main\cloud\ljyd_cloud_local.example.h sdk\apps\main\cloud\ljyd_cloud_local.h
```

编辑 `ljyd_cloud_local.h`，填写自己的产品 PID 和密钥。`SECERT` 是上游宏名的既有拼写，不随意更名。该文件被 Git 忽略；示例占位值会被源码检查拒绝，根构建脚本本身只先检查文件是否存在。

克隆代码不提供云端 Agent 配置、账号权限或设备绑定。不要改回历史文档中的旧产品，也不要把真实身份填到 README、共享配置或提交记录中。

## 构建入口及副作用

| 入口 | 实际作用 |
| --- | --- |
| `.\build_full_sdk.ps1` | 设 ADF / IDF 路径并调用 IDF 构建，只构建、不烧录 |
| `.\build_full_sdk.ps1 -ConfigureOnly` | 调用 `idf.py reconfigure`；会生成配置和构建文件，不是只读检查 |
| `.\sdk\export.ps1` | 导出环境并运行 ADF 补丁应用脚本，可能修改 SDK 文件 |
| `.\sdk\build.bat` | 导出环境、构建、`flash monitor`；当前脚本没有显式整片擦除命令，但会写设备 |
| `.\sdk\dev.bat` | 旧快捷入口，可构建、删除缓存或烧录；有本机 Python 环境路径假设 |
| `tools/tests/test_*_host.py`、语音 guard 测试 | 调用主机 C/C++ 编译器，不是纯 Python 静态检查 |

在允许构建、源码问题已处理且环境已导出的任务中，完整应用构建命令为：

```powershell
.\build_full_sdk.ps1
```

产物目录为 `sdk/apps/build/`，应用镜像名 `vb701n.bin`。当前大小、余量及哈希必须从那次实际构建重新记录，不沿用旧值。

## VS Code 与常见环境问题

ESP-IDF 工程目录是 `sdk/apps/`，而 ADF 根目录是 `sdk/`，IDF 根目录是 `sdk/esp-idf/`。插件应指向这份 IDF，目标 `esp32s3`。可在已导出环境的终端中打开工程：

```powershell
code .\sdk\apps
```

| 问题 | 排查方向 |
| --- | --- |
| 找不到 `idf.py` 或工具链 | 当前终端是否导出正确环境，Python / Git 是否可执行 |
| `Activation script failed` | 核对工具版本和 `IDF_TOOLS_PATH`，不要靠删除锁文件或改业务编码掩盖问题 |
| SDK 源码缺失 | 完整恢复仓库；安装工具链不会补回缺失的源码目录 |
| 中文路径解码错误 | 使用英文短路径，包含工具目录；不要批量转换源码编码 |
| 依赖里有其他机器绝对路径 | 检查依赖声明与锁文件差异；不把删除 `dependencies.lock` 作为默认步骤 |
| `dev.bat` 找不到 Python 环境 | 该脚本含 `idf5.4_py3.14_env` 假设；按实际安装配置或使用标准导出环境 |

## 烧录、分区与恢复

具体命令集中在[常见指令](../常见指令.md)。执行前确认镜像、板卡、串口、接线和现有固件备份。`idf.py flash` / `app-flash` 可能先触发构建，禁止编译时也不能运行。

分区来源为 `sdk/apps/partitions.csv`；下表为该表的静态布局，实际操作仍核对生成的分区表与 `flash_args`。

| 分区 | 偏移 | 大小 |
| --- | --- | --- |
| nvs | 0x11000 | 48 KB |
| otadata | 0x1D000 | 8 KB |
| phy_init | 0x1F000 | 4 KB |
| ota_0 / ota_1 | 0x20000 / 0x410000 | 各 4032 KB |
| model | 0x800000 | 64 KB |
| coco_pose | 0x810000 | 3200 KB |
| spiffs_data | 0xB30000 | 1024 KB |
| lvgl | 0xC30000 | 3072 KB |

已有匹配分区和模型时，单独更新应用通常只写应用镜像；首次上板、改分区、擦除后恢复需要核对完整镜像集合。清 NVS 会影响配网、绑定及其他 KV，不能当成无损重连。原始 Flash、日志、照片和已配置固件不上传仓库。

## 日常开发与资料

- 当前宏开关、启动顺序、业务分层见[应用架构](../sdk/apps/README_CN.md)。
- 摄像头设置和已知限制见[网页工具](../tools/camera/README.md)，舵机接线见[P6 接线](SERVO_P6_WIRING.md)。
- 历史板端验证不替代完整应用；独立测试工程见[说明](../firmware/README.md)。
- SDK 来源及许可见[第三方说明](../THIRD_PARTY_NOTICES.md)和[来源元数据](SDK_PROVENANCE.json)。
