"""PC Remote BLE diagnostic client (Windows). Requires: pip install bleak.

Device firmware must expose the matching custom GATT service first.
This script does not pair with or control the stock HID keyboard.
"""
import asyncio
import sys
from bleak import BleakScanner, BleakClient

SERVICE_UUID = "7cbf1000-8c22-4b2d-9c2e-ae8f2bc4a100"
WRITE_UUID = "7cbf1001-8c22-4b2d-9c2e-ae8f2bc4a100"
NOTIFY_UUID = "7cbf1002-8c22-4b2d-9c2e-ae8f2bc4a100"
NAME = "Cardputer-PCRemote"

async def main():
    print("Scanning for", NAME)
    found = await BleakScanner.discover(timeout=10, return_adv=True)
    matches = [(dev, adv) for dev, adv in found.values()
               if dev.name == NAME or SERVICE_UUID.lower() in
               [u.lower() for u in adv.service_uuids]]
    if not matches:
        print("Not found. This is expected until PC Remote firmware is installed.")
        return 2
    device = matches[0][0]
    print("Found:", device.name, device.address)
    async with BleakClient(device, timeout=20) as client:
        services = client.services
        if services.get_service(SERVICE_UUID) is None:
            print("Device has no PC Remote service")
            return 3
        reply = asyncio.Event()
        def notify(_, data):
            print("Received:", bytes(data).decode("utf-8", errors="replace"))
            if bytes(data).strip() == b"PONG":
                reply.set()
        await client.start_notify(NOTIFY_UUID, notify)
        await client.write_gatt_char(WRITE_UUID, b"PING", response=True)
        try:
            await asyncio.wait_for(reply.wait(), timeout=5)
        except asyncio.TimeoutError:
            print("Timeout waiting for PONG")
            return 4
        print("BLE PING/PONG PASS")
        return 0

if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
