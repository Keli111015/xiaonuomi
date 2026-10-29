# QM Timer Manager Example

This example shows how to use `qm_timer_mgr` from the `qumi/examples` tree.

## Files

```text
qumi/examples/qm_timer_mgr/
├── README.md
├── qm_kconfig
├── qm_timer_mgr_example.c
└── qm_config/
    └── qm_config.h
```

## Features

- Create and destroy a timer manager
- Countdown: start, pause, resume, stop, restart, and finish flows
- Stopwatch: start, pause, resume, and stop flows
- Event callback logging (STARTED / TICK / PAUSED / RESUMED / RESTARTED / STOPPED / FINISHED)
- Status query with mode, state, duration, elapsed, and remaining

## Example Configuration

```text
CONFIG_QM_TIMER_MGR_EXAMPLE_COUNTDOWN_SEC=5
CONFIG_QM_TIMER_MGR_EXAMPLE_STOPWATCH_SEC=2
CONFIG_QM_TIMER_MGR_EXAMPLE_TICK_MS=1000
```

## Entry Points

```c
qm_application_start();
qm_timer_mgr_example_start();
qm_timer_mgr_example_stop();
```

`qm_application_start()` calls `qm_timer_mgr_example_start()`.

## Integration

This example lives under `qumi/examples/qm_timer_mgr/`. Add
`qumi/examples/qm_timer_mgr/qm_timer_mgr_example.c` to your project sources and
merge `qumi/examples/qm_timer_mgr/qm_config/qm_config.h` into your project
configuration.
