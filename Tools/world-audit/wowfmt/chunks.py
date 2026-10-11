"""IFF-style chunk walking for WDT/ADT/WDL files (4-byte id stored byte-reversed, uint32 size, data).

walk() never reads past the end: a chunk whose size runs past the end of its container is reported as malformed
(and the walk stops there), not trusted. Every chunk keeps its absolute offset, so reports can cite bytes."""
import hashlib
import struct


class Chunk:
    __slots__ = ('id', 'offset', 'size', 'data_offset', 'buf')

    def __init__(self, cid, offset, size, buf):
        self.id = cid
        self.offset = offset
        self.size = size
        self.data_offset = offset + 8
        self.buf = buf

    @property
    def data(self):
        return self.buf[self.data_offset:self.data_offset + self.size]

    def __repr__(self):
        return '%s@%d+%d' % (self.id, self.offset, self.size)


class Walk:
    def __init__(self, chunks, problems):
        self.chunks = chunks
        self.problems = problems

    def all(self, cid):
        return [c for c in self.chunks if c.id == cid]

    def one(self, cid):
        found = self.all(cid)
        return found[0] if found else None

    def ids(self):
        out = []
        for c in self.chunks:
            if not out or out[-1][0] != c.id:
                out.append([c.id, 1])
            else:
                out[-1][1] += 1
        return [(i, n) for i, n in out]


def walk(buf, start=0, end=None):
    end = len(buf) if end is None else end
    chunks, problems = [], []
    off = start
    while off < end:
        if off + 8 > end:
            problems.append('trailing %d bytes at %d (no room for a chunk header)' % (end - off, off))
            break
        raw = buf[off:off + 4]
        cid = raw[::-1].decode('latin-1')
        size = struct.unpack_from('<I', buf, off + 4)[0]
        if off + 8 + size > end:
            problems.append('chunk %s at %d claims %d bytes, only %d left' % (cid, off, size, end - off - 8))
            break
        chunks.append(Chunk(cid, off, size, buf))
        off += 8 + size
    return Walk(chunks, problems)


def sha256(b):
    return hashlib.sha256(b).hexdigest()
