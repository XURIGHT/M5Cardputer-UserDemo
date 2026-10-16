"""Transport-neutral menu protocol with a fake BLE link.

This exercises the exact bytes the Windows side will send. Hardware GATT
integration is a separate step; this module never claims a real BLE connection.
"""
import json
from menu_transport import menu_frames, KIND_MENU
from ble_framing import Reassembler, ProtocolError

class FakeGattLink:
    def __init__(self):
        self.receiver = Reassembler()
        self.pages = {}
        self.total_pages = None

    def write(self, frame):
        complete = self.receiver.feed(frame)
        if complete is None:
            return
        kind, message_id, payload = complete
        if kind != KIND_MENU:
            raise ProtocolError("unexpected message kind")
        obj = json.loads(payload.decode("utf-8"))
        if obj.get("type") != "MENU_RESPONSE":
            raise ProtocolError("unexpected message type")
        page, pages = obj.get("page"), obj.get("pages")
        if (not isinstance(page, int) or not isinstance(pages, int)
                or not 1 <= pages <= 256 or not 0 <= page < pages
                or message_id != page or not isinstance(obj.get("menu"), list)):
            raise ProtocolError("invalid menu page")
        if self.total_pages is not None and pages != self.total_pages:
            raise ProtocolError("page count changed")
        if page in self.pages:
            raise ProtocolError("duplicate page")
        self.total_pages = pages
        self.pages[page] = obj["menu"]

    def catalog(self):
        if self.total_pages is None or len(self.pages) != self.total_pages:
            raise ProtocolError("incomplete catalog")
        return [item for index in range(self.total_pages) for item in self.pages[index]]

def deliver_menu(entries=None, chunk_size=120):
    link = FakeGattLink()
    for frame in menu_frames(entries, chunk_size):
        link.write(frame)
    return link.catalog()
