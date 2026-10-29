# QM Provision Example

This example shows how to start an independent `qm_provision` BLE provisioning
session and receive parsed provisioning data from the callback.

## Files

```text
qumi/examples/qm_provision/
├── qm_kconfig
└── qm_provision_example.c
```

## Configuration

Set product values before starting the example:

- `CONFIG_QM_PROVISION_EXAMPLE_PRODUCT_ID`
- `CONFIG_QM_PROVISION_EXAMPLE_PRODUCT_SECRET`
- `CONFIG_QM_PROVISION_EXAMPLE_ADV_CID`, optional; leave empty to use the
  component default CID

The component buffer lengths, default timezone, and BLE deinit delay are
configured by the `qm_provision` component Kconfig.

## Behavior

The example only starts BLE provisioning and logs callback data. It does not
own network connection, cloud registration, or token handling.
