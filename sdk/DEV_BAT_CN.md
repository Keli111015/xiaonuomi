# dev.bat 使用说明

仓库根目录的编译烧录入口，常用命令：

| 命令 | 作用 |
|---|---|
| `dev.bat build` | 增量编译 |
| `dev.bat rebuild` | 全量编译（删掉 `apps\build` 后重新编译） |
| `dev.bat clean` | 清理编译缓存（删掉整个 `apps\build`） |
| `dev.bat clean obj` | 只清对象文件，保留 `sdkconfig` |
| `dev.bat flash -p COMx` | 只烧应用分区，改完代码快速验证用 |
| `dev.bat flashall -p COMx` | 全烧：`flash_args` 里的全部镜像（含 `0x810000` 姿态模型） |
| `dev.bat flashall -p COMx erase` | 全烧前先整片擦除，会清掉 `nvs` 与已保存的配网信息 |
| `dev.bat help` | 显示帮助 |

- 串口必填 `-p COMx`，也可以先用环境变量 `DEV_PORT` 指定。查本机串口（PowerShell）：
  `[System.IO.Ports.SerialPort]::GetPortNames()`
- 日常改代码后用 `flash`；换分区表、首次烧录或要清配网时用 `flashall`（加 `erase`）。
- 编译产物在 `apps\build\`，应用固件为 `vb701n.bin`。
