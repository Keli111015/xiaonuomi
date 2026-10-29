# TinyCBOR QUMI Component

This component provides the public TinyCBOR encoder and parser used by QUMI
components. It keeps the upstream CBOR API stable while giving the build
system a first-class component boundary.

## Responsibilities

- Encode and decode CBOR data without platform or RTOS dependencies.
- Expose `cbor.h` through the component include directory.
- Provide the implementation sources through the QUMI component build.

## Non-goals

- No sockets, storage, task, timer, or logging policy.
- No protocol schema validation; protocol components own their schemas.
- No ownership transfer beyond the TinyCBOR API contract.

## Architecture

```text
QUMI component / application
            |
         cbor.h
            |
 TinyCBOR encoder + parser
            |
     platform-independent C
```

## Configuration

`QM_TINYCBOR_SUPPORT` is enabled by default because protocol and cloud
components may share this implementation.

## Usage

Include `cbor.h` from a QUMI component and use the upstream TinyCBOR encoder
or parser contract. The component does not retain caller buffers after an API
call; ownership and lifetime follow the upstream API documentation.
