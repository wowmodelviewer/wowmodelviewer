"""WDT reader: MVER, MPHD (as raw uint32 words), MAIN (per tile), MAID (per tile, raw uint32 words), MWMO/MODF (a
map's single global WMO), and every other chunk listed as found.

Field meanings are NOT assumed here. MAID is returned as words per tile; which word is the root ADT, obj0, tex0 and so on
is proven by the audit (audit_wdt.py) from the listfile names of the FileDataIDs in each word, per client."""
import struct

from .chunks import walk

TILES = 64


class WDT:
    def __init__(self, buf):
        self.buf = buf
        w = walk(buf)
        self.walk = w
        self.problems = list(w.problems)
        self.order = w.ids()
        mver = w.one('MVER')
        self.version = struct.unpack_from('<I', buf, mver.data_offset)[0] if mver and mver.size >= 4 else None
        mphd = w.one('MPHD')
        self.mphd_words = list(struct.unpack_from('<%dI' % (mphd.size // 4), buf, mphd.data_offset)) if mphd else []
        main = w.one('MAIN')
        self.main = None
        if main:
            per = main.size // (TILES * TILES)
            if main.size % (TILES * TILES):
                self.problems.append('MAIN size %d is not 64x64 entries' % main.size)
            self.main_entry_size = per
            self.main = [struct.unpack_from('<%dI' % (per // 4), buf, main.data_offset + i * per) for i in range(TILES * TILES)]
        maid = w.one('MAID')
        self.maid = None
        if maid:
            per = maid.size // (TILES * TILES)
            if maid.size % (TILES * TILES):
                self.problems.append('MAID size %d is not 64x64 entries' % maid.size)
            self.maid_entry_size = per
            self.maid = [struct.unpack_from('<%dI' % (per // 4), buf, maid.data_offset + i * per) for i in range(TILES * TILES)]
        # Other 64x64 tables (MAI2, MANM): kept as raw uint32 words per tile when they divide evenly, else as size.
        self.extra_tables = {}
        for cid in ('MAI2', 'MANM'):
            c = w.one(cid)
            if c:
                if c.size % (TILES * TILES) == 0 and (c.size // (TILES * TILES)) % 4 == 0:
                    per = c.size // (TILES * TILES)
                    self.extra_tables[cid] = [struct.unpack_from('<%dI' % (per // 4), buf, c.data_offset + i * per)
                                              for i in range(TILES * TILES)]
                else:
                    self.extra_tables[cid] = c.size
        mwmo = w.one('MWMO')
        self.mwmo = buf[mwmo.data_offset:mwmo.data_offset + mwmo.size].split(b'\0') if mwmo else None
        modf = w.one('MODF')
        self.modf = []
        if modf:
            for k in range(modf.size // 64):
                o = modf.data_offset + 64 * k
                name_id, unique_id = struct.unpack_from('<II', buf, o)
                pos = struct.unpack_from('<3f', buf, o + 8)
                rot = struct.unpack_from('<3f', buf, o + 20)
                ext = struct.unpack_from('<6f', buf, o + 32)
                flags, doodad_set, name_set, scale = struct.unpack_from('<4H', buf, o + 56)
                self.modf.append({'name_id': name_id, 'unique_id': unique_id, 'position': pos, 'rotation': rot,
                                  'extents': ext, 'flags': flags, 'doodad_set': doodad_set, 'name_set': name_set,
                                  'scale': scale})
            if modf.size % 64:
                self.problems.append('MODF size %d is not a multiple of 64' % modf.size)

    def tiles(self):
        """(x, y, MAIN words, MAID words) for every tile with MAIN flag bit 1 (the bit whose tiles have MAID words);
        index = y*64 + x (proven by the tile numbers in the MAID files' names)."""
        out = []
        for i in range(TILES * TILES):
            if self.main and self.main[i][0] & 1:
                out.append((i % TILES, i // TILES, self.main[i], self.maid[i] if self.maid else None))
        return out
