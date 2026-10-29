# Windows 编译说明

本仓库包含 ESP-ADF、ESP-IDF 和 `apps` 应用。Windows 下从主仓库直接获得内置的 `esp-idf` 源码，再用它安装工具环境并编译 `apps`。

## 新机器前置条件

两个根目录脚本不能替代操作系统级依赖。运行它们前请确认：

- 建议用 `git clone` 获取仓库，便于后续 `git pull` 更新代码；解压 ZIP 包也可用，但需保证 `esp-idf` 等目录完整。
- 已安装 Git for Windows，并且在新开的终端中可以执行 `git --version`。
- 已安装 ESP-IDF 支持的 Python 版本，并且 `python.exe` 在 PATH 中可以执行。
  如果 `python` 指向 Microsoft Store 的 WindowsApps 别名，请在“应用执行别名”中关闭该别名，或调整 PATH 顺序。
- 首次安装时可以访问主仓库，以及本地 ESP-IDF 安装脚本所需的工具包下载地址（如 dl.espressif.com）；公司代理或防火墙需要允许 Git、Python 和工具下载。

Python 是安装脚本运行前必须存在的系统命令（Git 仅用于获取和更新代码）；CMake、Ninja、Xtensa 工具链和 Python 环境由本仓库 `esp-idf` 目录里的安装脚本处理。

## 一键安装和编译（推荐）

首次使用或更换 ESP-IDF 工具链时，必须先执行根目录的 `install.bat`，再执行
`build.bat`。`install.bat` 会直接使用仓库内置的 `esp-idf` 源码，调用 `esp-idf\install.bat` 安装编译工具；`build.bat`
会自动导出环境、编译 `apps`、擦除芯片、烧录固件并启动串口监视器。

在 PowerShell 中从仓库根目录执行：

```powershell
Set-Location D:\Code\esp32_combo
.\install.bat
.\build.bat
```

在 `cmd.exe` 中执行：

```bat
cd /d D:\Code\esp32_combo
call install.bat
call build.bat
```

`build.bat` 已经会自动调用 `export.bat`，所以一键流程不需要先手动执行
`export.ps1` 或 `export.bat`。如果只想编译、不擦除或烧录，可以使用下面的手动流程。

## PowerShell（推荐）

在仓库根目录执行。每个新开的 PowerShell 窗口都需要初始化一次：

```powershell
Set-Location D:\Code\esp32_combo
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
.\export.ps1
```

`export.ps1` 会自动设置 `ADF_PATH` 和 `IDF_PATH`，加载 ESP-IDF 环境，并应用仓库中的 IDF 补丁。成功后应看到：

```text
ESP-IDF environment setup completed successfully.
```

然后在同一个 PowerShell 窗口中编译：

```powershell
Set-Location .\apps
idf.py build
```

脚本需要在当前窗口中执行，不能在一个窗口初始化环境后换另一个窗口运行 `idf.py`。`Set-ExecutionPolicy -Scope Process` 只对当前窗口生效，不会修改系统策略。

如果当前 PowerShell 已允许本地脚本运行，可以省略 `Set-ExecutionPolicy`。

## cmd.exe

在仓库根目录打开 `cmd.exe`，执行：

```bat
cd /d D:\Code\esp32_combo
call export.bat
cd /d apps
idf.py build
```

在批处理脚本中调用 `export.bat` 时应使用 `call`，以便返回后继续执行编译命令。PowerShell 中应使用 `export.ps1`；直接运行 `export.bat` 时，环境变量只存在于它启动的子 `cmd.exe` 进程中。

## VS Code ESP-IDF 插件

可以使用 VS Code 的 ESP-IDF 插件编译和烧录：

1. 用 VS Code 打开 `D:\Code\esp32_combo\apps`，该目录是 ESP-IDF 工程目录。
2. 确保 ESP-IDF 插件已完成工具链配置，并使用本仓库的 ESP-IDF 目录：`D:\Code\esp32_combo\esp-idf`。新版插件通过 Espressif Install Manager 配置，旧版可使用 `ESP-IDF: Configure ESP-IDF extension`。
3. 当前 `apps\sdkconfig` 已配置目标芯片为 `esp32s3`；串口选择实际连接的 `COM` 端口。
4. 使用插件的 Build、Flash 和 Monitor 命令或状态栏按钮完成编译、烧录和串口监视。

首次使用新克隆的仓库时，先在 PowerShell 中初始化一次环境，确保 ADF 补丁已经应用：

```powershell
Set-Location D:\Code\esp32_combo
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
.\export.ps1
```

插件构建需要能读取 `ADF_PATH`。可从上面已经初始化的 PowerShell 窗口启动 VS Code：

```powershell
code .\apps
```

如果 VS Code 已经在运行，请先完全退出后再从该终端启动，确保它继承 `ADF_PATH` 等环境变量。也可以在插件的自定义环境变量设置 `idf.customExtraVars` 中添加 `ADF_PATH`，值为仓库根目录的绝对路径。

也可以在 `apps\.vscode\settings.json` 中写入：

```json
{
    "idf.customExtraVars": {
        "ADF_PATH": "D:\\Code\\esp32_combo",
        "IDF_PATH": "D:\\Code\\esp32_combo\\esp-idf"
    }
}
```

如果仓库路径不同，只替换 `ADF_PATH` 的值即可。

## 手动烧录和监视

先按前文初始化当前 PowerShell 窗口，再执行以下命令。将 `COM7` 替换成实际串口：

```powershell
Set-Location D:\Code\esp32_combo\apps
idf.py -p COM7 flash monitor
```

退出串口监视器使用 `Ctrl+]`。

## 常见问题

### PowerShell 提示无法识别 idf.py

一键流程不依赖系统对 `.py` 文件的关联，也不直接使用 PATH 中的 `idf.py`；`build.bat` 会在导出环境后调用 ESP-IDF 虚拟环境里的 Python 和仓库内的 `tools\idf.py`。如果仍然报错，请重新执行 `install.bat`。

### install.bat 提示找不到 Python 或 Git

这两个命令需要先安装到 Windows，并加入 PATH。安装后必须关闭并重新打开终端，再从仓库根目录执行 `install.bat`。如果 `where python.exe` 的第一项是 `WindowsApps`，请关闭 Microsoft Store 的 Python 执行别名。

### install.bat 提示 esp-idf 目录缺失或不完整

`esp-idf` 和 `qumios-dev` 都是主仓库内的普通源码目录，仓库不再有子模块。如果 `esp-idf` 目录缺失或不完整（例如从裁剪过的压缩包解压），请重新完整 clone 主仓库。`install.bat` 只负责调用 `esp-idf\install.bat` 装工具链，需要能访问工具下载地址。

### 工具下载地址和 ESP-IDF 源码的区别

`install.bat` 不会从其他仓库重新获取 ESP-IDF 源码。ESP-IDF 源码固定来自本仓库内置的 `esp-idf` 源码目录；本地安装脚本只负责准备编译器、CMake、Ninja 和 Python 虚拟环境等构建工具。

### build.bat 提示 ESP-IDF 或 qumios-dev 未初始化

先确认 `install.bat` 已完整执行成功。不要只创建空的 `esp-idf` 或 `qumios-dev` 目录；脚本需要实际存在 ESP-IDF 的 `tools\idf.py` 和 QUMI 的 `components\qm_agent\include\qm_agent.h`。

通常是当前窗口尚未初始化环境，或者在 PowerShell 中运行了 `export.bat`。回到仓库根目录执行 `.\export.ps1`，然后在同一窗口进入 `apps` 编译。

### 仓库不在 D:\Code\esp32_combo

将命令中的仓库路径替换成实际路径。路径包含空格时使用引号，例如：

```powershell
Set-Location "D:\My Projects\esp32_combo"
```

### 已设置其他 IDF_PATH

根目录的 `export.ps1` 会强制使用本仓库内置的 `esp-idf`，不会沿用当前窗口中其他项目的 `IDF_PATH`。如果需要确认路径，可以执行：

```powershell
Set-Location D:\Code\esp32_combo
.\export.ps1
```

## 当前工程信息

- 目标芯片：`esp32s3`（来自 `apps\sdkconfig`）
- 工程目录：`apps`
- 构建输出目录：`apps\build`
- 应用名：`vb701n`
- Opus 组件：`78/esp-opus`（managed component）
