# PC Remote v0.1 — Windows host (first slice)

This is a **Windows-side prototype**, not yet a Cardputer firmware app. No firmware was flashed or modified by this commit.

## Start (PowerShell on Windows)

Install Python 3.10+ if needed. From this directory:

```powershell
$env:PC_REMOTE_TOKEN = (python -c "import secrets; print(secrets.token_hex(24))")
python server.py
```

By default the server binds to `127.0.0.1:8765` (local-only). In a second PowerShell:

```powershell
$headers = @{ 'X-PC-Remote-Token' = $env:PC_REMOTE_TOKEN }
Invoke-RestMethod http://127.0.0.1:8765/status -Headers $headers
Invoke-RestMethod http://127.0.0.1:8765/tasks -Headers $headers
Invoke-RestMethod http://127.0.0.1:8765/run -Method Post -Headers $headers -ContentType 'application/json' -Body '{"task":"open_notepad"}'
```

Environment variables are per-terminal: set the same token in the second terminal, or run the requests in the same terminal using `Start-Process` to launch the server.

## Tasks

Edit `tasks.json` to add explicit allowed tasks with `argv` arrays, and optionally `cwd`. There is **no arbitrary shell endpoint** and requests cannot override arguments. `wsl_python_demo` is an example of launching WSL Python. Codex tasks will be added only after defining explicit project directory and lifecycle semantics.

## Network / security

For first testing, keep the server on loopback. For LAN pairing later, set `PC_REMOTE_HOST=0.0.0.0` only on a trusted private network, allow Windows Firewall only on the private profile, and use a unique secret. HTTP plus a static token is **not encrypted**, so this is not suitable for hostile Wi-Fi or public networks. Do not forward port 8765 to the internet. A pairing and stronger transport mechanism is required before general deployment.

## API

- `GET /status`: service and job status
- `GET /tasks`: task names
- `POST /run`: JSON `{"task":"open_notepad"}`
- All endpoints require `X-PC-Remote-Token`.

This slice does not implement Codex pause/resume, interactive terminal streaming, device discovery, or Cardputer menu yet.
