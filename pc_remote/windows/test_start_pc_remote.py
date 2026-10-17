import asyncio
import os
import socket
import unittest
import urllib.error
import urllib.request
from start_pc_remote import run
import server


class LauncherTests(unittest.TestCase):
    def test_shared_token_and_shutdown(self):
        original = server.TOKEN
        original_env = os.environ.get('PC_REMOTE_TOKEN')
        port = []
        # Reserve a free port, then use it for the service lifecycle check.
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            chosen = probe.getsockname()[1]

        async def bridge():
            token = os.environ['PC_REMOTE_TOKEN']
            self.assertEqual(token, server.TOKEN)
            self.assertGreaterEqual(len(token), 24)
            req = urllib.request.Request(f'http://127.0.0.1:{chosen}/catalog',
                                         headers={'X-PC-Remote-Token': token})
            with urllib.request.urlopen(req, timeout=3) as response:
                self.assertEqual(response.status, 200)
            with self.assertRaises(urllib.error.HTTPError) as err:
                urllib.request.urlopen(f'http://127.0.0.1:{chosen}/catalog', timeout=3)
            self.assertEqual(err.exception.code, 401)
            port.append(chosen)
        try:
            run(bridge, port=chosen)
            with socket.socket() as probe:
                self.assertNotEqual(probe.connect_ex(('127.0.0.1', chosen)), 0)
        finally:
            server.TOKEN = original
            if original_env is None:
                os.environ.pop('PC_REMOTE_TOKEN', None)
            else:
                os.environ['PC_REMOTE_TOKEN'] = original_env
        self.assertEqual(port, [chosen])

    def test_occupied_port_leaves_listener_alive(self):
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            listener.listen()
            port = listener.getsockname()[1]
            async def bridge():
                self.fail('Bridge must not start if binding failed')
            with self.assertRaises(OSError):
                run(bridge, port=port)
            with socket.socket() as client:
                self.assertEqual(client.connect_ex(('127.0.0.1', port)), 0)
