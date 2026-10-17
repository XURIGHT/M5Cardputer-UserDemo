"""One console and one process for the local HTTP service and BLE bridge."""
import asyncio
import importlib.util
import os
import secrets
import socket
import sys
import threading
from pathlib import Path


def run(bridge_main=None, host='127.0.0.1', port=8765):
    import server
    class LocalServer(server.ThreadingHTTPServer):
        allow_reuse_address = os.name != 'nt'

        def server_bind(self):
            if os.name == 'nt':
                self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            super().server_bind()

    if bridge_main is None:
        from ble_bridge import main as bridge_main
    # Binding first rejects an occupied port without touching another process.
    httpd = LocalServer((host, port), server.Handler)
    token = secrets.token_hex(24)
    server.TOKEN = token
    os.environ['PC_REMOTE_TOKEN'] = token
    thread = threading.Thread(target=httpd.serve_forever, name='PC Remote HTTP', daemon=True)
    thread.start()
    print('PC Remote - service + Bluetooth', flush=True)
    print(f'Local service ready: {httpd.server_address[0]}:{httpd.server_address[1]}', flush=True)
    print('Open PC Remote on your Cardputer. Keep this window open.', flush=True)
    print('Press Ctrl+C or close this window to stop PC Remote.', flush=True)
    try:
        asyncio.run(bridge_main())
    finally:
        httpd.shutdown()
        httpd.server_close()
        thread.join(timeout=3)


def main():
    if os.name == 'nt':
        import ctypes
        ctypes.windll.kernel32.SetConsoleTitleW('PC Remote - Bluetooth + Tasks')
    os.chdir(Path(__file__).resolve().parent)
    if importlib.util.find_spec('bleak') is None:
        print('Bluetooth dependency missing. Run: python -m pip install bleak')
        return 1
    try:
        run()
    except KeyboardInterrupt:
        print('\nPC Remote stopped.')
        return 0
    except OSError as exc:
        if getattr(exc, 'winerror', None) == 10048 or getattr(exc, 'errno', None) == 98:
            print('Port 8765 is already in use. PC Remote may already be running.')
            print('Use the existing PC Remote window, or close the old server.py window first.')
            print('No existing process was stopped.')
        else:
            print('PC Remote could not start:', exc)
        return 1
    except Exception as exc:
        print('PC Remote could not start:', exc)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
