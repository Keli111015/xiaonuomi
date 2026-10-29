# QM IoT Cloud SDK

`qm_cloud` manages an embedded device's cloud session, property traffic,
device topics, dynamic registration, and optional OTA-related cloud flows.
Applications retain control of network bring-up, credentials, and product
policy.

## Architecture

```text
application
    | credentials, network updates, callbacks
    v
qm_cloud public API
    |-- MQTT session and property/device topics
    |-- dynamic registration, OTA, NTP, weather, subdevices
    `-- QUMI TCP/TLS and OS abstractions
    v
cloud service
```

The component does not configure Wi-Fi, cellular, or Ethernet. Bring the
network up first, call `qm_iot_network_update(handle, QM_IOT_NETWORK_UP)`, and
process receive and event callbacks in product code. Secrets and certificates
must come from product configuration or secure storage, never generic docs.

## Responsibility Boundary

`qm_cloud` owns cloud protocol/session behavior; applications own networking,
credential sourcing, retry policy above the session, and product decisions.

## Basic Flow

1. Create a session with `qm_iot_init()`.
2. Set product identity, credential, callback, and user-context options with
   `qm_iot_setopt()`.
3. Call `qm_iot_start()`, then report network up or down through
   `qm_iot_network_update()`.
4. Report properties and handle `qm_iot_recv_t` and `qm_iot_event_t` callbacks.
5. Stop and release the session when the application no longer needs it.

## Configuration

`qm_kconfig` controls optional features such as MQTT, Spec properties, OTA,
dynamic registration, device management, NTP, and weather. The runtime log
mask is component-wide and only affects `qm_cloud`. The detailed Chinese
integration reference is in `README_CN.md`; public contracts are in
`api/qm_iot_api.h`.
