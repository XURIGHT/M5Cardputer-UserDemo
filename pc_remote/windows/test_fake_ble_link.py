import unittest
from fake_ble_link import FakeGattLink, deliver_menu
from menu_transport import menu_frames
from ble_framing import ProtocolError

class FakeBLETests(unittest.TestCase):
    def test_menu_roundtrip(self):
        data = [{"id": "a", "label": "Open Notepad", "category": "Windows"},
                {"id": "b", "label": "WSL Python", "category": "WSL"}]
        self.assertEqual(deliver_menu(data), data)

    def test_large_menu(self):
        data = [{"id": str(i), "label": "菜单项目" + str(i), "category": "测试"}
                for i in range(100)]
        self.assertEqual(deliver_menu(data), data)

    def test_incomplete(self):
        frames = list(menu_frames([{"id": "x", "label": "long" * 60, "category": "Test"}]))
        link = FakeGattLink()
        for frame in frames[:-1]:
            link.write(frame)
        with self.assertRaises(ProtocolError):
            link.catalog()

if __name__ == "__main__":
    unittest.main()
