# QUMI

QUMI 是项目内的组件、驱动、平台适配和示例集合。当前 README 先作为组件入口索引，
详细设计和接入说明放在各组件自己的 README 中维护。

## 目录入口

| 路径 | 说明 |
| --- | --- |
| `components/` | 可复用业务组件和协议组件 |
| `drivers/` | 外设驱动 |
| `platform/` | MCU/平台适配 |
| `board/` | 板级适配 |
| `apps/` | 产品或业务应用 |
| `examples/` | 组件示例 |
| `utility/` | 通用工具库 |

## 近期组件

- `components/qm_agent`: 智能体会话框架，统一封装 transport、音频驱动和 service provider。
  详见 [components/qm_agent/README.md](components/qm_agent/README.md)。

## 配置使用方式

1. 在仓库根目录执行 `source export.sh`，初始化 `PATH` 和 `QDF_PATH`。
2. 在目标示例或应用目录执行 `python qdf.py menuconfig`，进入配置界面。
3. `qdf.py` 会先加载仓库根 `qm_kconfig`，再动态追加当前目录下主树未显式包含的 `qm_kconfig*` 配置项。
4. 配置结果默认输出到当前目录下的 `qm_config/` 中：
   - `qm_config/.config`：menuconfig 保存的配置结果
   - `qm_config/qm_config.h`：供代码编译使用的配置头文件
   - `qm_config/qm_kconfig.dynamic`：自动生成的临时 Kconfig 入口文件，无需提交到仓库

## README 维护约定

- 组件目录优先维护自己的 README，说明当前能力、目录结构、依赖边界、配置项和最小使用方式。
- README 应描述已经落地的行为；未完成能力放到“后续重点”或 TODO 中。
- 示例代码优先使用组件公开头文件中的 API，避免依赖内部实现细节。
