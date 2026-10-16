import unittest
from menu_transport import simulate_transfer, menu_frames, menu_payloads, KIND_MENU
from ble_framing import Reassembler, ProtocolError

class MenuTransportTests(unittest.TestCase):
    def test_default_catalog(self):
        messages = simulate_transfer()
        labels = [entry["label"] for _, _, page in messages for entry in page["menu"]]
        self.assertTrue("Open Notepad" in labels or "open_notepad" in labels)
        self.assertTrue("WSL Python Demo" in labels or "wsl_python_demo" in labels)

    def test_large_catalog(self):
        entries = [{"id": str(i), "label": f"Task {i}", "category": "Test"} for i in range(80)]
        messages = simulate_transfer(entries)
        ids = [entry["id"] for _, _, page in messages for entry in page["menu"]]
        self.assertEqual(ids, [str(i) for i in range(80)])

    def test_fragmented_unicode(self):
        entries = [{"id": str(i), "label": "中文菜单" * 5, "category": "工具"} for i in range(15)]
        frames = list(menu_frames(entries, chunk_size=120))
        self.assertGreater(len(frames), 1)
        self.assertEqual(sum(len(x["menu"]) for _, _, x in simulate_transfer(entries)), 15)

    def test_out_of_order_rejected(self):
        entries = [{"id": "x", "label": "x" * 250, "category": "Test"}]
        frames = list(menu_frames(entries, chunk_size=120))
        self.assertGreater(len(frames), 2)
        with self.assertRaises(ProtocolError):
            Reassembler().feed(frames[1])

if __name__ == "__main__":
    unittest.main()
