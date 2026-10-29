# QM WebSocket Client

`qm_websocket_client` provides `ws://` and `wss://` connections through QUMI
TCP/TLS abstractions. It supports asynchronous send queues, fragmented frames,
PING/PONG, clean close, error events, and optional reconnect backoff.

## Architecture

```text
application events and send APIs
                v
      qm_websocket_client
                | handshake, frames, PING, reconnect
                v
      QUMI TCP or TLS transport
                v
          WebSocket service
```

The component owns the connection task and protocol state. Applications own
network readiness, event processing, payload reassembly, and credentials.
Receive data is delivered in fixed-buffer chunks; it must be copied if retained
beyond its callback.

Send timeout zero does not wait for admission locks or queue space. Full queues
return `-QM_EFULL`, while closed admission or lock contention returns `-QM_EBUSY`.
Finite waits share one admission deadline and wake on capacity changes. Accepted
frames are copied; disconnect cancels queued frames and resets fragmentation,
so old data is not uploaded on a new connection. Allocation and copying still
take execution time; this is not a preallocated hard-real-time API. Serialize
destroy with every application API call, including pending send waits.

## Responsibility Boundary

Network bring-up, credential handling, payload reassembly, and application
retry decisions remain outside the client.

## Basic Usage

1. Fill `qm_websocket_client_config_t` with `uri` and optional headers/TLS
   configuration.
2. Create a client with `qm_websocket_client_init()` and register callbacks.
3. Start it with `qm_websocket_client_start()`.
4. Send text or binary data after `WEBSOCKET_EVENT_CONNECTED`.
5. Close or stop, then destroy the client.

## Configuration

Enable `QM_WEBSOCKET_CLIENT_SUPPORT` in `qm_kconfig`. Connection fields are
instance-specific; zero-valued timing and buffer fields use Kconfig defaults.
`README_CN.md` documents event semantics, transport substitution, logging, and
the available examples.
