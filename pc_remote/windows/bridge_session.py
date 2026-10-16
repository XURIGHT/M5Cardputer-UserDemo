"""Hardware-independent menu upload and allowlisted task request session."""
import json
import re
from ble_framing import fragment
from catalog import catalog_pages


def frames_for_catalog(entries, chunk_size):
    if len(entries) > 64:
        raise ValueError('Device supports at most 64 tasks')
    for item in entries:
        if not isinstance(item.get('label'), str) or len(item['label'].encode('utf-8')) > 95:
            raise ValueError('Task label exceeds device limit (95 UTF-8 bytes)')
    # Reserve the JSON page envelope, even at the minimum ATT MTU (23).
    pages = catalog_pages(entries, max_bytes=min(900, chunk_size * 16 - 80))
    if len(pages) > 16:
        raise ValueError('Device supports at most 16 menu pages')
    frames = []
    for i, page in enumerate(pages):
        payload = json.dumps({'type': 'MENU_RESPONSE', 'page': i, 'pages': len(pages),
                              'menu': page}, ensure_ascii=False, separators=(',', ':')).encode()
        frames.extend(fragment(2, i, payload, chunk_size))
    return frames


class BridgeSession:
    def __init__(self, entries, execute):
        self.entries = entries
        self.execute = execute
        self.replies = {}

    async def handle(self, data):
        match = re.fullmatch(rb'RUN:(\d{1,2}):(\d{1,6})', data)
        if not match:
            return None
        index, seq = map(int, match.groups())
        if seq in self.replies:
            return self.replies[seq]
        ok = False
        if 0 <= index < len(self.entries):
            ok = await self.execute(self.entries[index]['id'])
        reply = f'ACK:{seq}:{"OK" if ok else "ERR"}'.encode('ascii')
        self.replies[seq] = reply
        return reply
