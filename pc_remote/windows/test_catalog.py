import unittest
from catalog import load_catalog, catalog_pages
from ble_framing import MAX_MESSAGE
import json

class CatalogTests(unittest.TestCase):
    def test_existing_tasks(self):
        ids = [x["id"] for x in load_catalog()]
        self.assertIn("open_notepad", ids)
        self.assertIn("wsl_python_demo", ids)

    def test_pagination(self):
        entries = [{"id": str(i), "label": "Task " + str(i), "category": "Demo"} for i in range(200)]
        pages = catalog_pages(entries)
        self.assertEqual(sum(map(len, pages)), 200)
        for page in pages:
            self.assertLessEqual(len(json.dumps({"menu": page}, separators=(",", ":")).encode()), MAX_MESSAGE)

    def test_unicode(self):
        entries = [{"id": str(i), "label": "中文任务" * 6, "category": "工具"} for i in range(50)]
        self.assertEqual(sum(map(len, catalog_pages(entries))), 50)

if __name__ == "__main__":
    unittest.main()
