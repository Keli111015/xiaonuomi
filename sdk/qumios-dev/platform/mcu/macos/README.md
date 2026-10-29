# macOS Platform Adapter

This directory is the host counterpart of the ESP32 platform layout. Public QUMI
interfaces remain C; Objective-C is isolated to Apple framework adapters.

## Implemented Layout

- `qmos/`: pthread-based task, mutex, semaphore, event, queue, timer, memory and
  monotonic time primitives. Core pinning is intentionally ignored on a host.
- `hal/ble/`: CoreBluetooth GAP and the generic QMS GATT peripheral adapter.
- `qm_ble/`: QUMI BLE OS, AES-CBC (CommonCrypto), OTA bridge and GATT layout
  compatibility wrapper.
- `qm_audio/`: bidirectional AudioQueue linear-PCM driver with microphone
  authorization, timed capture, a bounded ring, fixed playback buffers,
  volume/mute/stop, and capture-policy controls.
- `hal/net/`: BSD TCP and UDP plus repository-bundled mbedTLS 1.2. The TLS
  adapter supports system or caller-provided CAs, hostname verification, SNI,
  ALPN, client certificates and server certificates.
- `hal/wifi/`: station connect/disconnect, scan, interface MAC read and event
  translation using macOS `networksetup`, the legacy `airport` scanner and
  `ifconfig` tools. These tools may be unavailable in sandboxed or newer macOS
  installations; the adapter returns the corresponding error instead of
  claiming that a scan or connection succeeded.
- `hal/lpc/`: host-safe initialization; sleep and wake GPIO controls return
  `-QM_ENOSYS` because a macOS process cannot emulate MCU low-power states.
- `hal/modem/`: compile-compatible modem/PPP stubs returning `-QM_ENOSYS`.
- `qm_db/`: file-backed KV store. Set `QM_MACOS_KV_DIR` to control its location.
- `hal/ota/`: file-backed OTA staging (`QM_MACOS_OTA_PATH`, default
  `/tmp/qumi-macos-ota.bin`); it never changes the host boot target or reboots.
- `hal/soc/`: system-clock-backed RTC. GPIO, UART, SPI, I2C, flash and other
  MCU-specific peripherals are not emulated by this host port.

## BLE Limits

CoreBluetooth hides peer and local Bluetooth MAC addresses. The adapter therefore
persists a host-only, randomly generated six-byte protocol identity in
NSUserDefaults for the advertised device ID and QMS read characteristic. It is
not a real Bluetooth MAC and does not bypass BLE provisioning authentication.
Raw scan-response bytes and scan interval/window controls are retained at the C
API boundary but are not directly controllable by CoreBluetooth. GAP callbacks
are delivered on the main dispatch queue; an application must keep its run loop
alive.

## Dependencies

- macOS 10.15 or later
- CoreBluetooth.framework and Foundation.framework
- AudioToolbox.framework and AVFoundation.framework
- CommonCrypto (provided by macOS)
- repository-bundled mbedTLS
- Bluetooth usage description in the application bundle
- user-granted Bluetooth permission
- microphone usage description and user permission for voice applications

Do not expose CoreBluetooth types from reusable QUMI components. The Objective-C
implementation is confined to this platform directory and communicates through
the existing QUMI C callbacks.
