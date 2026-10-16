import unittest
from ble_framing import fragment, Reassembler, ProtocolError, MAX_MESSAGE

class FramingTests(unittest.TestCase):
    def roundtrip(self, payload):
        frames = fragment(2, 123, payload)
        receiver = Reassembler()
        outputs = [receiver.feed(frame) for frame in frames]
        self.assertEqual(outputs[-1], (2, 123, payload))
        self.assertTrue(all(x is None for x in outputs[:-1]))

    def test_short(self):
        self.roundtrip(b"hello")

    def test_menu(self):
        self.roundtrip(("menu " * 130).encode())

    def test_max(self):
        self.roundtrip(b"x" * MAX_MESSAGE)

    def test_empty_and_oversize(self):
        for payload in (b"", b"x" * (MAX_MESSAGE + 1)):
            with self.assertRaises(ProtocolError):
                fragment(2, 1, payload)

    def test_out_of_order(self):
        frames = fragment(2, 4, b"x" * 400)
        with self.assertRaises(ProtocolError):
            Reassembler().feed(frames[1])

    def test_truncated(self):
        with self.assertRaises(ProtocolError):
            Reassembler().feed(b"bad")

    def test_mismatch(self):
        frames = fragment(2, 4, b"x" * 400)
        r = Reassembler()
        r.feed(frames[0])
        with self.assertRaises(ProtocolError):
            r.feed(fragment(2, 5, b"hello")[0])

if __name__ == "__main__":
    unittest.main()
