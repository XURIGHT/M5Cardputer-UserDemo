"""Upload Windows-owned menus and execute explicit allowlisted task selections."""
import asyncio
import json
import os
import urllib.request
from bridge_session import BridgeSession, frames_for_catalog

SERVICE_UUID = '7cbf1000-8c22-4b2d-9c2e-ae8f2bc4a100'
WRITE_UUID = '7cbf1001-8c22-4b2d-9c2e-ae8f2bc4a100'
NOTIFY_UUID = '7cbf1002-8c22-4b2d-9c2e-ae8f2bc4a100'
BASE = 'http://127.0.0.1:8765'


def request(path, token, data=None):
    body = None if data is None else json.dumps(data).encode()
    req = urllib.request.Request(BASE + path, data=body, headers={
        'Content-Type': 'application/json', 'X-PC-Remote-Token': token})
    with urllib.request.urlopen(req, timeout=5) as response:
        return json.load(response)


async def run_once(token):
    from bleak import BleakClient, BleakScanner
    # Read from the running server, so displayed IDs and execution share one catalog.
    catalog = await asyncio.to_thread(request, '/catalog', token)
    entries = catalog['menu']
    print('Scanning for Cardputer-PCRemote...')
    device = await BleakScanner.find_device_by_filter(
        lambda dev, adv: dev.name == 'Cardputer-PCRemote' or
        SERVICE_UUID in [u.lower() for u in adv.service_uuids], timeout=10)
    if device is None:
        print('Device not found; open PC Remote on the Cardputer.')
        return
    disconnected = asyncio.Event()
    async with BleakClient(device, timeout=20,
                           disconnected_callback=lambda _: disconnected.set()) as client:
        queue = asyncio.Queue()
        await client.start_notify(NOTIFY_UUID, lambda _, data: queue.put_nowait(bytes(data)))
        chunk_size = min(120, client.mtu_size - 3 - 8)
        frames = frames_for_catalog(entries, chunk_size)
        for frame in frames:
            await client.write_gatt_char(WRITE_UUID, frame, response=True)
        print(f'Menu sent: {len(entries)} tasks. Use Up/Down and Enter on Cardputer.')

        async def execute(task_id):
            try:
                result = await asyncio.to_thread(request, '/run', token, {'task': task_id})
                print('Task:', task_id, result)
                return bool(result.get('ok'))
            except Exception as exc:
                print('Task failed:', exc)
                return False

        session = BridgeSession(entries, execute)
        while not disconnected.is_set():
            try:
                data = await asyncio.wait_for(queue.get(), timeout=1)
            except asyncio.TimeoutError:
                continue
            reply = await session.handle(data)
            if reply:
                await client.write_gatt_char(WRITE_UUID, reply, response=True)


async def main():
    token = os.environ.get('PC_REMOTE_TOKEN', '')
    if not token:
        raise SystemExit('Set PC_REMOTE_TOKEN to the same value used by server.py.')
    while True:
        try:
            await run_once(token)
        except Exception as exc:
            print('Bridge failed:', repr(exc))
        await asyncio.sleep(3)


if __name__ == '__main__':
    asyncio.run(main())
