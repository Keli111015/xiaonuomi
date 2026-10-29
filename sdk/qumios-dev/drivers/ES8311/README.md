# ES8311

`ES8311` 驱动提供基于 `qumi` 组件能力的音频 codec 控制能力，适合和 `qm_i2s` 这类数据通道驱动配合使用。

当前实现范围：

- codec 初始化与基础上电时序
- 时钟与采样率配置
- 串口分辨率配置
- DAC 音量与静音控制
- ADC 麦克风增益与数字麦选择
- ADC / DAC fade 配置
- 寄存器 dump

设计边界：

- 本驱动只负责 `ES8311` 的 I2C 寄存器控制
- 不负责 I2S 数据传输
- 不负责板级 `PA`、耳机检测、路由切换等外围控制
- 接口依赖 `qumi` 的基础 I2C、OS、内存与日志能力

目录结构：

- `include/` 对外头文件
- `src/` 驱动实现
- `priv_include/` 私有寄存器与内部定义
- `qm_kconfig` Kconfig 入口

最小使用方式：

```c
qm_i2c_dev_t i2c = {0};
es8311_dev_t codec = {0};

i2c.port = 0;
i2c.config.mode = QM_I2C_MODE_MASTER;
i2c.config.address_width = QM_I2C_ADDRESS_WIDTH_7BIT;
i2c.config.freq = QM_I2C_BUS_BIT_RATES_400K;
i2c.pins.sda_pin = 17;
i2c.pins.sck_pin = 18;

qm_i2c_init(&i2c);

codec.i2c_dev = &i2c;
codec.dev_addr = ES8311_ADDRESS_0;
codec.clock_config.mclk_from_mclk_pin = 0;
codec.clock_config.sample_frequency = 16000;
codec.input_resolution = ES8311_RESOLUTION_16;
codec.output_resolution = ES8311_RESOLUTION_16;

es8311_init(&codec);
es8311_set_voice_volume(&codec, 70, NULL);
```
