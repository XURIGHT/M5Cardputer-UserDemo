import asyncio
import ctypes
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from bridge_session import BridgeSession, frames_for_catalog
from ble_framing import Reassembler
from catalog import load_catalog


class SessionTests(unittest.IsolatedAsyncioTestCase):
    async def test_selection_and_duplicate(self):
        calls = []
        async def execute(task):
            calls.append(task)
            return True
        session = BridgeSession(load_catalog(), execute)
        self.assertEqual(await session.handle(b'RUN:1:7'), b'ACK:7:OK')
        self.assertEqual(await session.handle(b'RUN:1:7'), b'ACK:7:OK')
        self.assertEqual(calls, ['wsl_python_demo'])
        self.assertEqual(await session.handle(b'RUN:64:8'), b'ACK:8:ERR')
        self.assertIsNone(await session.handle(b'RUN:NOTEPAD:9'))
        self.assertEqual(len(calls), 1)

    async def test_failure_and_new_session(self):
        async def execute(task):
            return False
        session = BridgeSession(load_catalog(), execute)
        self.assertEqual(await session.handle(b'RUN:0:1'), b'ACK:1:ERR')
        self.assertEqual(await BridgeSession(load_catalog(), execute).handle(b'RUN:0:1'), b'ACK:1:ERR')


class WireTests(unittest.TestCase):
    def test_minimum_and_large_mtu(self):
        for chunk in (12, 120):
            receiver = Reassembler()
            received = []
            for frame in frames_for_catalog(load_catalog(), chunk):
                self.assertLessEqual(len(frame), chunk + 8)
                result = receiver.feed(frame)
                if result:
                    received.extend(json.loads(result[2])['menu'])
            self.assertEqual(received, load_catalog())

    def test_device_bounds(self):
        with self.assertRaises(ValueError):
            frames_for_catalog([{'id': 'x', 'label': 'x' * 96}], 120)
        with self.assertRaises(ValueError):
            frames_for_catalog([{'id': str(i), 'label': 'x'} for i in range(65)], 120)

    def test_actual_c_receiver(self):
        source = Path(__file__).resolve().parents[2] / 'main/apps/app_pc_remote'
        class Receiver(ctypes.Structure):
            _fields_ = [('buffer', ctypes.c_uint8 * 1024),
                        ('received', ctypes.c_uint16), ('expected', ctypes.c_uint16),
                        ('message_id', ctypes.c_uint16), ('kind', ctypes.c_uint8),
                        ('next_index', ctypes.c_uint8), ('part_count', ctypes.c_uint8),
                        ('active', ctypes.c_uint8)]
        with tempfile.TemporaryDirectory() as tmp:
            lib_path = Path(tmp) / 'receiver.so'
            subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-shared', '-fPIC', str(source / 'pc_frame.c'),
                            '-o', str(lib_path)], check=True)
            lib = ctypes.CDLL(str(lib_path))
            lib.pc_frame_feed.argtypes = [ctypes.POINTER(Receiver), ctypes.c_char_p, ctypes.c_size_t]
            lib.pc_frame_reset.argtypes = [ctypes.POINTER(Receiver)]
            for chunk in (12, 120):
                rx = Receiver()
                entries = []
                frames = frames_for_catalog(load_catalog(), chunk)
                for frame in frames:
                    result = lib.pc_frame_feed(ctypes.byref(rx), frame, len(frame))
                    self.assertGreaterEqual(result, 0)
                    if result == 1:
                        entries.extend(json.loads(bytes(rx.buffer[:rx.received]))['menu'])
                        lib.pc_frame_reset(ctypes.byref(rx))
                self.assertEqual(entries, load_catalog())
                self.assertLess(lib.pc_frame_feed(ctypes.byref(rx), frames[1], len(frames[1])), 0)
