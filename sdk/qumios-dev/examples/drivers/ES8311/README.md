# ES8311 Example

中文文档：[README_CN.md](README_CN.md)

This example shows a minimal `ES8311` bring-up flow from the `qumi/examples/drivers` tree.

## Files

```text
qumi/examples/drivers/ES8311/
├── README.md
├── README_CN.md
├── application.c
├── qm_kconfig
└── qm_config/
    └── qm_config.h
```

## What It Shows

- Initialize a `qm_i2c_dev_t` for the codec control bus
- Fill an `es8311_dev_t` device descriptor with board-level clock and address settings
- Call `es8311_init()`
- Configure microphone path and analog gain
- Set DAC volume and unmute output
- Optionally dump codec registers for board bring-up

## Example Configuration

The example configuration defaults live in `qm_config/qm_config.h`. A matching
`qm_kconfig` is also provided when the example is integrated into a menuconfig-based project:

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

`CONFIG_ES8311_EXAMPLE_MCLK_FROM_PIN` selects how the codec clock is derived:

- `1`: use external MCLK from the codec MCLK pin
- `0`: derive the clock from serial bit clock according to the current driver behavior

## Entry Point

```c
void qm_application_start(void);
```

`qm_application_start()` calls the internal example flow directly.

## Integration

Add `qumi/examples/drivers/ES8311/application.c` to the target project source list,
merge `qumi/examples/drivers/ES8311/qm_config/qm_config.h` into the project
configuration header, include `qumi/examples/drivers/ES8311/qm_kconfig` in the
target Kconfig tree if needed, and make sure the `ES8311` driver sources are
compiled into the same target.
