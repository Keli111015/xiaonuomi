# ES8311 示例

English documentation: [README.md](README.md)

这个示例演示如何在 `qumi/examples/drivers` 目录下完成一个最小 `ES8311` 初始化流程。

## 文件结构

```text
qumi/examples/drivers/ES8311/
├── README.md
├── README_CN.md
├── application.c
├── qm_kconfig
└── qm_config/
    └── qm_config.h
```

## 演示内容

- 初始化 codec 控制总线使用的 `qm_i2c_dev_t`
- 填充带板级地址和时钟参数的 `es8311_dev_t` 设备描述
- 调用 `es8311_init()`
- 配置麦克风输入路径和模拟增益
- 设置 DAC 音量并取消静音
- 在板级联调时可选输出寄存器 dump

## 示例配置

示例默认配置位于 `qm_config/qm_config.h`。如果工程使用 `menuconfig`，也可以接入配套的 `qm_kconfig`：

```c
#define CONFIG_ES8311_EXAMPLE_I2C_PORT 0
#define CONFIG_ES8311_EXAMPLE_I2C_ADDR ES8311_ADDRESS_0
#define CONFIG_ES8311_EXAMPLE_I2C_SDA_PIN 17
#define CONFIG_ES8311_EXAMPLE_I2C_SCL_PIN 18

#define CONFIG_ES8311_EXAMPLE_SAMPLE_RATE 16000
#define CONFIG_ES8311_EXAMPLE_MCLK_FROM_PIN 0
#define CONFIG_ES8311_EXAMPLE_MCLK_FREQUENCY 12288000

#define CONFIG_ES8311_EXAMPLE_USE_DIGITAL_MIC 0
#define CONFIG_ES8311_EXAMPLE_MIC_GAIN ES8311_MIC_GAIN_12DB
#define CONFIG_ES8311_EXAMPLE_VOLUME 70
#define CONFIG_ES8311_EXAMPLE_DUMP_REGISTERS 0
```

`CONFIG_ES8311_EXAMPLE_MCLK_FROM_PIN` 用来选择 codec 时钟来源：

- `1`：使用外部 `MCLK` 引脚输入
- `0`：按当前驱动行为从串口位时钟推导

## 入口

```c
void qm_application_start(void);
```

`qm_application_start()` 会直接调用示例流程。

## 接入方式

将 `qumi/examples/drivers/ES8311/application.c` 加入目标工程源码列表，
并将 `qumi/examples/drivers/ES8311/qm_config/qm_config.h` 中相关配置合并到
项目配置头；如果需要，也可将 `qumi/examples/drivers/ES8311/qm_kconfig`
接入目标工程的 Kconfig 树。同时确保 `ES8311` 驱动源码已经加入同一个构建目标。
