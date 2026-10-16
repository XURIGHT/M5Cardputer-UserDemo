"""Windows-side dynamic menu transport, independent of real BLE hardware."""
import json
from ble_framing import fragment, Reassembler
from catalog import load_catalog, catalog_pages

KIND_MENU = 2

def menu_payloads(entries=None):
    entries = load_catalog() if entries is None else entries
    pages = catalog_pages(entries, max_bytes=900)
    total = len(pages)
    return [json.dumps({"type": "MENU_RESPONSE", "page": i,
                        "pages": total, "menu": page},
                       ensure_ascii=False, separators=(",", ":")).encode("utf-8")
            for i, page in enumerate(pages)]

def menu_frames(entries=None, chunk_size=120):
    for i, payload in enumerate(menu_payloads(entries)):
        yield from fragment(KIND_MENU, i, payload, chunk_size)

def simulate_transfer(entries=None):
    receiver = Reassembler()
    messages = []
    for frame in menu_frames(entries):
        completed = receiver.feed(frame)
        if completed is not None:
            kind, msg_id, payload = completed
            messages.append((kind, msg_id, json.loads(payload)))
    return messages

if __name__ == "__main__":
    result = simulate_transfer()
    print(f"Reassembled {len(result)} menu page(s)")
    for kind, msg_id, page in result:
        print(f"page {page['page'] + 1}/{page['pages']}:", [x["label"] for x in page["menu"]])
