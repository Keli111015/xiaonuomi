# Windows 开发说明（统一入口）

本页原有的重复安装和构建步骤已于 2026-10-31 合并至[完整工程使用说明](../docs/FULL_REPOSITORY.md)。所有命令从小诺米项目根目录出发，SDK 位于其 `sdk/` 子目录。

- 环境、产品身份、构建入口、VS Code、分区和常见问题：[完整工程使用说明](../docs/FULL_REPOSITORY.md)。
- 烧录、网络重置、终端和 CLI：[常见指令](../常见指令.md)。
- 当前源码问题和实机验收边界：[当前交接](../docs/CURRENT_HANDOFF.md)。

`build.bat` 会写入设备，`-ConfigureOnly` 会生成 CMake 配置；禁止编译或只读检查时不要运行。本轮文档更新未执行构建、配置生成或硬件操作。
