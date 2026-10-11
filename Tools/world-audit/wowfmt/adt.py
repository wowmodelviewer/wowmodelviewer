"""Split-ADT readers (root, tex0, obj0, obj1, lod) for the world-format audit.

Layouts used here are the ones the audit tests against the bytes (audit_adt.py): it checks, for example, that a root
MCNK starts with a 128-byte header whose IndexX/IndexY follow the chunk order and that its sub-chunks walk cleanly
after it, while tex/obj MCNKs hold sub-chunks from their first byte. Anything decoded is kept with its offset.
"""
import struct

from .chunks import walk

MCNK_HEADER = 128
MCNK_FIELDS = [  # (name, offset, struct format) -- tested by audit_adt.py, not taken on trust
    ('flags', 0, '<I'), ('index_x', 4, '<I'), ('index_y', 8, '<I'), ('n_layers', 12, '<I'),
    ('n_doodad_refs', 16, '<I'), ('ofs_height', 20, '<I'), ('ofs_normal', 24, '<I'), ('holes_high', 20, '<Q'),
    ('ofs_layer', 28, '<I'), ('ofs_refs', 32, '<I'), ('ofs_alpha', 36, '<I'), ('size_alpha', 40, '<I'),
    ('ofs_shadow', 44, '<I'), ('size_shadow', 48, '<I'), ('area_id', 52, '<I'), ('n_mapobj_refs', 56, '<I'),
    ('holes_low', 60, '<H'), ('unknown_62', 62, '<H'), ('low_quality_map', 64, '<16s'),
    ('no_effect_doodad', 80, '<Q'), ('ofs_snd', 88, '<I'), ('n_snd', 92, '<I'), ('ofs_liquid', 96, '<I'),
    ('size_liquid', 100, '<I'), ('position', 104, '<3f'), ('ofs_mccv', 116, '<I'), ('ofs_mclv', 120, '<I'),
    ('unused_124', 124, '<I'),
]


def mcnk_header(buf, off):
    h = {}
    for name, o, fmt in MCNK_FIELDS:
        v = struct.unpack_from(fmt, buf, off + o)
        h[name] = v if len(v) > 1 else v[0]
    return h


class SplitFile:
    """Any split ADT file: its top-level chunks, and its MCNKs with sub-chunks (header or not)."""

    def __init__(self, buf, root):
        self.buf = buf
        self.root = root
        w = walk(buf)
        self.walk = w
        self.problems = list(w.problems)
        self.mcnk = []
        for n, c in enumerate(w.all('MCNK')):
            if root:
                if c.size < MCNK_HEADER:
                    self.problems.append('MCNK %d at %d: %d bytes, smaller than its header' % (n, c.offset, c.size))
                    self.mcnk.append((c, None, None))
                    continue
                hdr = mcnk_header(buf, c.data_offset)
                sub = walk(buf, c.data_offset + MCNK_HEADER, c.data_offset + c.size)
            else:
                hdr = None
                sub = walk(buf, c.data_offset, c.data_offset + c.size)
            for p in sub.problems:
                self.problems.append('MCNK %d: %s' % (n, p))
            self.mcnk.append((c, hdr, sub))

    def top(self):
        return self.walk.ids()


# ------------------------------------------------------------------------------------------------- decoders

def mcvt(buf, chunk):
    return struct.unpack_from('<145f', buf, chunk.data_offset) if chunk and chunk.size >= 580 else None


def mcly(buf, chunk):
    out = []
    if not chunk:
        return out
    for k in range(chunk.size // 16):
        tex, flags, ofs, effect = struct.unpack_from('<IIIi', buf, chunk.data_offset + 16 * k)
        out.append({'texture': tex, 'flags': flags, 'alpha_offset': ofs, 'effect': effect})
    return out


def rle_alpha(buf, start, end):
    """WoW's compressed alpha: a byte n; top bit set = fill (the next byte, n&0x7f times), clear = copy n&0x7f bytes.
    Returns (4096 values, bytes consumed) or (None, consumed) when it does not reach 4096 inside [start, end)
    (a copy run that would pass `end` also gives None)."""
    out = bytearray()
    p = start
    while len(out) < 4096 and p < end:
        n = buf[p]
        p += 1
        count = n & 0x7F
        if n & 0x80:
            if p >= end:
                break
            out.extend(bytes([buf[p]]) * count)
            p += 1
        else:
            if p + count > end:
                return None, p - start      # a copy run may not read past MCAL into the next chunk
            out.extend(buf[p:p + count])
            p += count
    return (bytes(out[:4096]) if len(out) >= 4096 else None), p - start


def mddf(buf, chunk):
    out = []
    if not chunk:
        return out
    for k in range(chunk.size // 36):
        o = chunk.data_offset + 36 * k
        name_id, uid = struct.unpack_from('<II', buf, o)
        pos = struct.unpack_from('<3f', buf, o + 8)
        rot = struct.unpack_from('<3f', buf, o + 20)
        scale, flags = struct.unpack_from('<HH', buf, o + 32)
        out.append({'name_id': name_id, 'unique_id': uid, 'position': pos, 'rotation': rot, 'scale': scale, 'flags': flags})
    return out


def modf(buf, chunk):
    out = []
    if not chunk:
        return out
    for k in range(chunk.size // 64):
        o = chunk.data_offset + 64 * k
        name_id, uid = struct.unpack_from('<II', buf, o)
        pos = struct.unpack_from('<3f', buf, o + 8)
        rot = struct.unpack_from('<3f', buf, o + 20)
        ext = struct.unpack_from('<6f', buf, o + 32)
        flags, dset, nset, scale = struct.unpack_from('<4H', buf, o + 56)
        out.append({'name_id': name_id, 'unique_id': uid, 'position': pos, 'rotation': rot, 'extents': ext,
                    'flags': flags, 'doodad_set': dset, 'name_set': nset, 'scale': scale})
    return out


def u32s(buf, chunk):
    return list(struct.unpack_from('<%dI' % (chunk.size // 4), buf, chunk.data_offset)) if chunk else []


def strings(buf, chunk):
    if not chunk:
        return []
    return [s.decode('latin-1') for s in buf[chunk.data_offset:chunk.data_offset + chunk.size].split(b'\0') if s]


def mh2o(buf, chunk):
    """MH2O: 256 headers (offset of instances, layer count, offset of attributes), then the instances they point at.
    Offsets are from the start of MH2O's data. Vertex data is located, not decoded (its format needs LiquidType/
    LiquidMaterial from DB2, or a liquid-object-or-format word below 42)."""
    if not chunk:
        return None
    base = chunk.data_offset
    end = base + chunk.size
    chunks = []
    problems = []
    for n in range(256):
        ofs_inst, layers, ofs_attr = struct.unpack_from('<III', buf, base + 12 * n)
        inst = []
        for k in range(layers):
            o = base + ofs_inst + 24 * k
            if o + 24 > end:
                problems.append('chunk %d instance %d past MH2O end' % (n, k))
                break
            (ltype, lobj, hmin, hmax, xo, yo, w, h, ofs_bits, ofs_vert) = struct.unpack_from('<HHffBBBBII', buf, o)
            inst.append({'liquid_type': ltype, 'object_or_lvf': lobj, 'min_height': hmin, 'max_height': hmax,
                         'x': xo, 'y': yo, 'width': w, 'height': h, 'ofs_exists': ofs_bits, 'ofs_vertex': ofs_vert})
        chunks.append({'layers': layers, 'ofs_attributes': ofs_attr, 'instances': inst})
    return {'chunks': chunks, 'problems': problems, 'size': chunk.size}
