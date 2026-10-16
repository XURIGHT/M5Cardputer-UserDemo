"""Windows-owned dynamic catalog, independent of BLE transport."""
import json
from pathlib import Path
from ble_framing import MAX_MESSAGE

def load_catalog(path=None):
    path = Path(path) if path else Path(__file__).with_name("tasks.json")
    config = json.loads(path.read_text(encoding="utf-8"))
    tasks = config["tasks"]
    return [{"id": task_id, "label": spec.get("label", task_id),
             "category": spec.get("category", "Other")}
            for task_id, spec in tasks.items()]

def catalog_pages(entries, max_bytes=MAX_MESSAGE):
    if not 64 <= max_bytes <= MAX_MESSAGE:
        raise ValueError("invalid page size")
    pages, current = [], []
    for entry in entries:
        candidate = current + [entry]
        payload = json.dumps({"menu": candidate}, ensure_ascii=False, separators=(",", ":")).encode()
        if len(payload) > max_bytes:
            if not current:
                raise ValueError("single menu item too large")
            pages.append(current)
            current = [entry]
            if len(json.dumps({"menu": current}, ensure_ascii=False, separators=(",", ":")).encode()) > max_bytes:
                raise ValueError("single menu item too large")
        else:
            current = candidate
    if current or not pages:
        pages.append(current)
    return pages
