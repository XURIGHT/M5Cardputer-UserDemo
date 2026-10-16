"""Cardputer PC Remote BLE bridge: allowlisted Windows tasks only."""
import asyncio
import json
import os
import urllib.error
import urllib.request
from bleak import BleakClient, BleakScanner

SERVICE_UUID = "7cbf1000-8c22-4b2d-9c2e-ae8f2bc4a100"
WRITE_UUID = "7cbf1001-8c22-4b2d-9c2e-ae8f2bc4a100"
NOTIFY_UUID = "7cbf1002-8c22-4b2d-9c2e-ae8f2bc4a100"
TOKEN = os.environ.get("PC_REMOTE_TOKEN", "")
BASE = "http://127.0.0.1:8765"
SEEN = set()

def execute_notepad():
    req = urllib.request.Request(
        BASE + "/run",
        data=json.dumps({"task": "open_notepad"}).encode("utf-8"),
        headers={"Content-Type": "application/json", "X-PC-Remote-Token": TOKEN},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=5) as res:
            return bool(json.load(res).get("ok"))
    except (urllib.error.URLError, ValueError, TimeoutError) as exc:
        print("Windows task failed:", exc)
        return False

async def run_once():
    print("Scanning for Cardputer-PCRemote...")
    found = await BleakScanner.discover(timeout=10, return_adv=True)
    devices = [dev for dev, adv in found.values()
               if dev.name == "Cardputer-PCRemote" or
               SERVICE_UUID.lower() in [u.lower() for u in adv.service_uuids]]
    if not devices:
        print("Device not found")
        return
    async with BleakClient(devices[0], timeout=20) as client:
        print("Connected. Press 1 on Cardputer.")
        queue = asyncio.Queue()
        def on_notify(_, data):
            queue.put_nowait(bytes(data).decode("ascii", errors="replace"))
        await client.start_notify(NOTIFY_UUID, on_notify)
        while client.is_connected:
            try:
                frame = await asyncio.wait_for(queue.get(), timeout=2)
            except asyncio.TimeoutError:
                continue
            print("Received:", frame)
            if not frame.startswith("RUN:NOTEPAD:"):
                continue
            seq = frame.removeprefix("RUN:NOTEPAD:")
            if not seq.isdecimal() or len(seq) > 6:
                continue
            if seq in SEEN:
                print("Duplicate request ignored:", seq)
                continue
            SEEN.add(seq)
            success = await asyncio.to_thread(execute_notepad)
            response = f"ACK:{seq}:{'OK' if success else 'ERR'}"
            await client.write_gatt_char(WRITE_UUID, response.encode("ascii"), response=True)
            print("Sent:", response)

async def main():
    if not TOKEN:
        raise SystemExit("Set PC_REMOTE_TOKEN before running BLE bridge.")
    while True:
        try:
            await run_once()
        except Exception as exc:
            print("BLE disconnected or failed:", repr(exc))
        print("Retrying discovery in 3 seconds...")
        await asyncio.sleep(3)

if __name__ == "__main__":
    asyncio.run(main())
