# PC Remote dynamic menu

Windows owns the task catalog. Cardputer receives it over BLE, displays three rows at a time, and sends the selected index when Enter is pressed. Up/Down select; G0 returns by rebooting, matching the existing BLE lifecycle.

## Windows setup after flashing the new firmware

Download the complete `pc_remote/windows` directory from `feature/pc-remote-generic`; replacing only a test file is insufficient. Keep your own tasks.json if customized. Python 3.10+ is required.

In PowerShell, in this directory:

```powershell
python -m pip install bleak
$env:PC_REMOTE_TOKEN = (python -c "import secrets; print(secrets.token_hex(24))")
Start-Process python -ArgumentList 'server.py' -WorkingDirectory $PWD
python ble_bridge.py
```

The child server inherits the token. Keep the bridge terminal open. Open PC Remote on Cardputer; the bridge reconnects automatically and prints `Menu sent: 2 tasks`. The device should display Open Notepad and WSL Python Demo. Use Up/Down (Fn arrow keys on the keyboard) and Enter. `Result: OK` means the Windows server started the process, not that a long-running task finished successfully.

If an old server is already listening on port 8765, close that server's terminal before starting this one. The token must match the running server. On reconnection the bridge fetches its current catalog; restart server and reconnect bridge after editing tasks.json.

Only IDs from the displayed server catalog are submitted to `/run`; arguments and commands remain owned by the server's allowlist. The prototype BLE service is not authenticated or bonded yet. Keep this as a local prototype; do not add sensitive tasks before implementing pairing.

## Limits and diagnostics

64 tasks, 16 pages, 95 UTF-8 bytes per label; an item must also fit the negotiated BLE page size. Oversized catalogs fail explicitly in the bridge rather than silently truncating. The existing screen font determines which Unicode glyphs render. Default English labels are the hardware acceptance target.

`ble_client.py` remains a PING/PONG diagnostic. Use `ble_bridge.py` for menus.

Automated Linux checks (requires gcc for the real C receiver test):

```sh
python -m unittest discover -p 'test_*.py' -v
```

Hardware acceptance still required: menu arrival, both task selections, disconnect/reconnect, G0, and the original BLE keyboard after reboot.
