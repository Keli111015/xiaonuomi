# ES8311

The `ES8311` driver provides codec control on top of `qumi` component capabilities and is intended to work together with a PCM transport such as `qm_i2s`.

Current scope:

- codec initialization and base power-up sequence
- clock and sample-rate configuration
- serial-port resolution setup
- DAC volume and mute control
- ADC microphone gain and digital-mic selection
- ADC / DAC fade control
- register dump

Design boundary:

- this driver only handles `ES8311` register access over I2C
- it does not handle I2S data transport
- it does not manage board-specific `PA`, jack detect, or routing logic
- the interface depends on `qumi` I2C, OS, memory, and logging facilities

Directory layout:

- `include/` public headers
- `src/` driver implementation
- `priv_include/` private register and internal definitions
- `qm_kconfig` Kconfig entry

Minimal usage:

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
