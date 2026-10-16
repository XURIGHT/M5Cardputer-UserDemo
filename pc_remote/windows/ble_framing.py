"""Bounded, ordered BLE framing for generic PC Remote v0.2.

Wire format: 8-byte header, big-endian: version:u8, kind:u8,
message_id:u16, index:u8, count:u8, payload_length:u16; followed by payload.
The receiver validates all fields and reassembles one message at a time.
"""
import struct

VERSION = 2
HEADER = struct.Struct("!BBHBBH")
MAX_MESSAGE = 1024
MAX_CHUNK = 120
MAX_FRAGMENTS = 16

class ProtocolError(ValueError):
    pass

def fragment(kind: int, message_id: int, payload: bytes, chunk_size: int = MAX_CHUNK):
    if not isinstance(payload, bytes) or not 0 < len(payload) <= MAX_MESSAGE:
        raise ProtocolError("invalid payload length")
    if not 1 <= kind <= 255 or not 0 <= message_id <= 65535:
        raise ProtocolError("invalid kind or id")
    if not 1 <= chunk_size <= MAX_CHUNK:
        raise ProtocolError("invalid chunk size")
    count = (len(payload) + chunk_size - 1) // chunk_size
    if count > MAX_FRAGMENTS:
        raise ProtocolError("too many fragments")
    return [
        HEADER.pack(VERSION, kind, message_id, i, count, len(payload)) +
        payload[i * chunk_size:(i + 1) * chunk_size]
        for i in range(count)
    ]

class Reassembler:
    def __init__(self):
        self.reset()

    def reset(self):
        self.key = None
        self.parts = []
        self.expected_length = 0

    def feed(self, frame: bytes):
        if len(frame) < HEADER.size:
            self.reset()
            raise ProtocolError("truncated header")
        version, kind, msg_id, index, count, length = HEADER.unpack_from(frame)
        chunk = frame[HEADER.size:]
        if (version != VERSION or not 1 <= kind <= 255 or
                not 1 <= count <= MAX_FRAGMENTS or not 0 <= index < count or
                not 1 <= length <= MAX_MESSAGE or not 1 <= len(chunk) <= MAX_CHUNK):
            self.reset()
            raise ProtocolError("invalid frame")
        key = (kind, msg_id, count)
        if index == 0:
            if self.key is not None:
                self.reset()
                raise ProtocolError("new message before previous completed")
            self.key = key
            self.expected_length = length
        if self.key != key or index != len(self.parts) or length != self.expected_length:
            self.reset()
            raise ProtocolError("out of order or inconsistent fragment")
        self.parts.append(chunk)
        if index != count - 1:
            return None
        data = b"".join(self.parts)
        self.reset()
        if len(data) != length:
            raise ProtocolError("length mismatch")
        return kind, msg_id, data
