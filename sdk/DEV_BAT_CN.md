# dev.bat 使用说明

SDK 子目录中的旧快捷入口。以下写法假定当前目录为 `sdk/`；从项目根目录使用 `.\sdk\dev.bat ...`。统一环境说明见[完整工程使用说明](../docs/FULL_REPOSITORY.md)，操作速查见[常见指令](../常见指令.md)。本页仅说明脚本，不代表本轮执行过任何命令。

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
- 脚本含本机 `idf5.4_py3.14_env` 环境名假设，使用前核对实际工具路径。优先使用统一说明中的标准环境流程。
- `flash` / `flashall` 会写设备；`erase` 会清空整片 Flash。重置网络不必默认全擦，见速查表的独立说明。
- `rebuild` / `clean` 会删除构建缓存。禁止编译时不要运行构建或可能间接触发构建的命令。
- 编译产物在 `apps\build\`，应用固件为 `vb701n.bin`。
