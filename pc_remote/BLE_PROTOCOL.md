# BLE PING/PONG protocol draft

The Windows BLE client is implemented in `ble_client.py`. **The Cardputer-side GATT server is not implemented yet**, so scanning a stock device will not discover `Cardputer-PCRemote`.

- Peripheral: Cardputer ADV; central: Windows with Bleak/WinRT.
- Advertised name: `Cardputer-PCRemote`.
- Service: `7cbf1000-8c22-4b2d-9c2e-ae8f2bc4a100`.
- Write characteristic: `7cbf1001-8c22-4b2d-9c2e-ae8f2bc4a100`, write with response.
- Notify characteristic: `7cbf1002-8c22-4b2d-9c2e-ae8f2bc4a100`, notify.
- Probe: Windows writes ASCII `PING`, Cardputer notifies ASCII `PONG`.

Before adding the peripheral to the stock firmware, audit the HID helper's NimBLE host initialization and shutdown; do not initialize a second NimBLE host. Keep BLE task callbacks separate from the UI event loop. Later control messages require authentication and explicit allowlisted actions; PING/PONG alone does not grant command execution.

## Windows diagnostic

```powershell
py -m pip install bleak
py ble_client.py
```

An expected 'Not found' result is not a failure of Windows Bluetooth when the custom peripheral firmware has not been flashed.
