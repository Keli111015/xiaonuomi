# qm_event

`qm_event` provides event loops and the default dispatch channel used by
`qm_work` delayed tasks. Events are identified by an `event_base` and
`event_id`; handlers receive the event payload only for the duration of the
callback.

## Architecture

```text
producer: qm_event_post / qm_work
                v
          event queue
                v
       event loop dispatcher
                v
          registered handlers
```

The component owns queueing and dispatch. Applications own event payload
storage, handler work, and lifecycle serialization. In no-task mode, the
application must call the run/process API from its main loop.

## Responsibility Boundary

Handlers must not retain callback payload pointers unless they copy them;
application code also owns long-running work and event-loop shutdown ordering.

## Basic Usage

Create an event loop, register a handler, and post events with the APIs in
`include/qm_event.h`. Use the default loop when a shared application dispatch
channel is sufficient. Use `qm_work` for delayed or periodic callbacks.

## Configuration

`QM_WORK_SUPPORT` and the related `QM_WORKQUEUE_*` and `QM_EVENT_NUM` settings
control the default queue, handler capacity, task mode, and task parameters.
See `README_CN.md` for the complete API examples and return-code notes.
