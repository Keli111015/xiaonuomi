# QM Pomodoro Example

Chinese documentation: [README_CN.md](README_CN.md)

This example shows how to use `qm_pomodoro` from the `qumi/examples` tree.

## Files

```text
qumi/examples/qm_pomodoro/
├── README.md
├── README_CN.md
├── qm_kconfig
├── qm_pomodoro_example.c
└── qm_config/
    └── qm_config.h
```

## Features

- Create and destroy a pomodoro timer
- Start, pause, resume, skip, and stop flows
- Focus, short break, long break, and finished states
- Event callback logging
- Status query with remaining time, elapsed time, and completed focus count

## Example Configuration

```text
CONFIG_QM_POMODORO_EXAMPLE_FOCUS_SEC=4
CONFIG_QM_POMODORO_EXAMPLE_SHORT_BREAK_SEC=2
CONFIG_QM_POMODORO_EXAMPLE_LONG_BREAK_SEC=3
CONFIG_QM_POMODORO_EXAMPLE_LONG_BREAK_INTERVAL=2
CONFIG_QM_POMODORO_EXAMPLE_FOCUS_TARGET=3
CONFIG_QM_POMODORO_EXAMPLE_TICK_MS=700
```

## Entry Points

```c
qm_application_start();
qm_pomodoro_example_start();
qm_pomodoro_example_stop();
```

`qm_application_start()` calls `qm_pomodoro_example_start()`.

## Integration

This example lives under `qumi/examples/qm_pomodoro/`. Add
`qumi/examples/qm_pomodoro/qm_pomodoro_example.c` to your project sources and
merge `qumi/examples/qm_pomodoro/qm_config/qm_config.h` into your project
configuration.
