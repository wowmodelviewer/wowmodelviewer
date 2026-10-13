"""Tests of the stage 04 material tools: terrain_material_reference.py (the independent CPU reference: BLP decoder
with its exact, measured hardware and rounded block decodes, alpha decoder, chunk states, the height mix, MTXP
scale nibble 4 at 0.5 repeats with tile-continuous UV, up to 8 drawn layers, unresolved repeats drawn as the checker,
map-texture mode, validation sampling) and compare_terrain_material.py (V1, V2 with its per-pixel named-decode gate at
V2's own 0.26/255 limit -- 0.76/255 for an 8-bit PNG input --, and the texel test with its named-decode check by
upload format, with planted mutations, and the verification of every input).

Synthetic tex0, root, WDT and BLP files are built here from format structure only. The corpus tests run when
WMV_WORLD_AUDIT_CORPUS names the extracted audit data: pinned chunk states and alpha hashes, the golden tests against
the game's own map textures (design v3 section 4: nine tiles, the height mix within its measured RMSE + 0.25 and
every competing rule worse, the height texture's sampling and the nibble-4 UV included), and the offline converter's
byte test.

  python -I -m unittest discover -s Tools/world-audit/tests
"""
import contextlib
import hashlib
import io
import json
import os
import shutil
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
import compare_terrain_material as CMP  # noqa: E402
import terrain_material_reference as R  # noqa: E402
import terrain_reference as TR  # noqa: E402

ROOT = os.environ.get('WMV_WORLD_AUDIT_CORPUS')
HEIGHT_MAP = 0x3CA          # Azeroth's MPHD: 8-bit alpha (0x84) and height textures (0x80)
NO_HEIGHT_MAP = 0x34E       # an 8-bit map without 0x80 (WDT 1923857)


def need_data(test):
    return unittest.skipUnless(ROOT, 'WMV_WORLD_AUDIT_CORPUS not set (the audit data is extracted locally)')(test)


def load_json(path):
    with open(path) as f:
        return json.load(f)


def save_json(path, value):
    with open(path, 'w') as f:
        json.dump(value, f)


def quiet(fn, *args):
    with contextlib.redirect_stdout(io.StringIO()):
        return fn(*args)


# ------------------------------------------------------------------------------------------------- synthetic files

def chunk(cid, data):
    return cid[::-1].encode('latin-1') + struct.pack('<I', len(data)) + data


def rle(values):
    """WoW's compressed alpha: a fill run for each uniform 64-value row, else one copy run of the row."""
    out = bytearray()
    for r in range(64):
        row = values[r * 64:(r + 1) * 64]
        if len(set(row)) == 1:
            out += bytes([0x80 | 64, row[0]])
        else:
            out += bytes([64]) + bytes(row)
    return bytes(out)


def encode_alpha(storage, values):
    if storage == 'rle':
        return rle(values)
    if storage == 'raw8':
        return bytes(values)
    return bytes((values[2 * i] & 15) | ((values[2 * i + 1] & 15) << 4) for i in range(2048))


def tex0_mcnk(layers):
    """layers: [(texture index, flags, storage or None, 4096 values or None)], storage 'rle', 'raw8' or 'raw4'.
    MCLY offsets are computed back to back; flags get 0x100 after the first layer and 0x200 for RLE unless the given
    flags already say otherwise (pass explicit flags | 0x10000 to suppress: bit 16 is stripped)."""
    mcly = b''
    mcal = b''
    for k, (tex, flags, storage, values) in enumerate(layers):
        if flags & 0x10000:
            flags &= 0xFFFF
        else:
            flags |= (0x100 if k else 0) | (0x200 if storage == 'rle' else 0)
        mcly += struct.pack('<IIIi', tex, flags, len(mcal) if k else 0, -1)
        if k:
            mcal += encode_alpha(storage, values)
    return chunk('MCNK', chunk('MCLY', mcly) + (chunk('MCAL', mcal) if len(layers) > 1 else b''))


def mtxp_entries(entries):
    """MTXP payload from (flags, scale, offset[, word 3]) tuples."""
    return b''.join(struct.pack('<IffI', e[0], e[1], e[2], e[3] if len(e) > 3 else 0) for e in entries)


def make_tex0(per_chunk, mdid, mhid=None, mtxp=None, raw=None, extra=()):
    """per_chunk(n) -> layers for tex0_mcnk, or bytes for a hand-made MCNK; raw: {n: bytes} overrides; mhid: a list
    of ids or a raw payload (bytes); mtxp: (flags, scale, offset[, word 3]) entries; extra: whole chunks placed after
    the tables (repeated tables)."""
    parts = [chunk('MVER', struct.pack('<I', 18)), chunk('MAMP', bytes(4)),
             chunk('MDID', struct.pack('<%dI' % len(mdid), *mdid))]
    if mhid is not None:
        parts.append(chunk('MHID', mhid if isinstance(mhid, bytes) else struct.pack('<%dI' % len(mhid), *mhid)))
    if mtxp is not None:
        parts.append(chunk('MTXP', mtxp_entries(mtxp)))
    parts.extend(extra)
    for n in range(256):
        if raw and n in raw:
            parts.append(raw[n])
            continue
        v = per_chunk(n)
        parts.append(v if isinstance(v, bytes) else tex0_mcnk(v))
    return b''.join(parts)


def make_root(tx, ty, flags=lambda n: 0x8000 | 0x10000, holes=lambda n: 0, mccv=lambda n: None):
    """A root ADT of 256 flat MCNKs; mccv(n) -> 145 (B, G, R, A) tuples, raw bytes or None; flag 0x40 is not added
    for you."""
    parts = [chunk('MVER', struct.pack('<I', 18))]
    for n in range(256):
        h = bytearray(128)
        struct.pack_into('<III', h, 0, flags(n), n % 16, n // 16)
        struct.pack_into('<Q', h, 20, holes(n))
        x, y = TR.chunk_corner(tx, ty, n % 16, n // 16)
        struct.pack_into('<3f', h, 104, x, y, 10.0)
        body = chunk('MCVT', bytes(580))
        c = mccv(n)
        if c is not None:
            body += chunk('MCCV', c if isinstance(c, bytes) else b''.join(bytes(e) for e in c))
        body += chunk('MCNR', bytes(448))
        parts.append(chunk('MCNK', bytes(h) + body))
    return b''.join(parts)


def make_wdt(tx, ty, mphd, maid):
    main = bytearray(64 * 64 * 8)
    struct.pack_into('<I', main, (ty * 64 + tx) * 8, 1)
    words = bytearray(64 * 64 * 32)
    struct.pack_into('<8I', words, (ty * 64 + tx) * 32, *maid)
    return (chunk('MVER', struct.pack('<I', 18)) + chunk('MPHD', struct.pack('<8I', mphd, 0, 0, 0, 0, 0, 0, 0))
            + chunk('MAIN', bytes(main)) + chunk('MAID', bytes(words)))


def blp_header(encoding, alpha_depth, alpha_type, w, h, levels):
    """levels: list of level byte strings; returns the whole file."""
    head = bytearray(148 + 1024)
    head[0:4] = b'BLP2'
    struct.pack_into('<I', head, 4, 1)
    head[8], head[9], head[10], head[11] = encoding, alpha_depth, alpha_type, 1 if len(levels) > 1 else 0
    struct.pack_into('<II', head, 12, w, h)
    off = len(head)
    for k, data in enumerate(levels):
        struct.pack_into('<I', head, 20 + 4 * k, off)
        struct.pack_into('<I', head, 84 + 4 * k, len(data))
        off += len(data)
    return bytes(head) + b''.join(levels)


def blp_bgra(w, h, texel, mips=True):
    """An uncompressed BLP (encoding 3) whose level-0 texel (x, y) is texel(x, y) -> (r, g, b, a); smaller levels are
    2 x 2 means (rounded) so every stored level is meaningful."""
    level = [[texel(x, y) for x in range(w)] for y in range(h)]
    out = []
    while True:
        out.append(b''.join(bytes((p[2], p[1], p[0], p[3])) for row in level for p in row))
        if not mips or (len(level) == 1 and len(level[0]) == 1):
            break
        lh, lw = max(1, len(level) // 2), max(1, len(level[0]) // 2)
        level = [[tuple(int(round(sum(level[min(2 * y + j, len(level) - 1)][min(2 * x + i, len(level[0]) - 1)][c]
                                      for j in (0, 1) for i in (0, 1)) / 4.0)) for c in range(4))
                  for x in range(lw)] for y in range(lh)]
    return blp_header(3, 8, 8, w, h, out)


def pattern(seed):
    """An asymmetric texel pattern (no symmetry of the square maps it onto itself)."""
    return lambda x, y: ((37 * x + 11 * y + seed) % 200 + 20, (13 * x * x + 7 * y + 3 * seed) % 200 + 30,
                         (5 * x + 29 * y * y + 7 * seed) % 180 + 40, 255)


def height_pattern(seed):
    """A height texture: grey colour, an asymmetric alpha (the height value)."""
    return lambda x, y: (128, 128, 128, (53 * x + 17 * y * y + 31 * seed) % 256)


def ramp(n, k=0):
    """An 8-bit alpha map that grows east and south, different per chunk (asymmetric over the tile)."""
    return [min(255, (3 * c + 2 * r + 5 * ((n + k) % 7)) % 256 // 2) for r in range(64) for c in range(64)]


def mix_spec(ws, hs=None):
    """The design's height mix for one pixel, written out (w_k raw weights, hs the H_k)."""
    hs = hs or [1.0] * len(ws)
    p = [max(w, 0.0) * h for w, h in zip(ws, hs)]
    m = max(p)
    p = [x * (1.0 - min(max(m - x, 0.0), 1.0)) for x in p]
    s = sum(p)
    return [x / s for x in p] if s > 0 else [max(w, 0.0) for w in ws]


def float16_truncated(x):
    """x >= 0 through a float16 store that truncates toward zero (the player's V2 dump target, as V2's budget
    assumes): the largest float16 not above x."""
    h = struct.unpack('<e', struct.pack('<e', x))[0]            # round to nearest
    if h > x:
        h = struct.unpack('<e', struct.pack('<H', struct.unpack('<H', struct.pack('<e', x))[0] - 1))[0]
    return h


# ------------------------------------------------------------------------------------------------- BLP decoder

class BlpDecoder(unittest.TestCase):
    def test_dxt1_bit_replication_and_interpolation(self):
        # c0 = pure red (31, 0, 0), c1 = (16, 32, 1): 5-bit 16 -> 132, 6-bit 32 -> 130, 5-bit 1 -> 8 (x * 255 / 31
        # would give 131, 129, 8); four-colour block, indices 0, 1, 2, 3 in the first row
        c0, c1 = 31 << 11, (16 << 11) | (32 << 5) | 1
        block = struct.pack('<HHI', c0, c1, 0b11100100)
        b = R.Blp(blp_header(2, 0, 0, 4, 4, [block]))
        w, h, px = b.decode(0)
        self.assertEqual((w, h, b.format), (4, 4, 'DXT1'))
        self.assertEqual(px[0:4], [255.0, 0.0, 0.0, 255.0])
        self.assertEqual(px[4:8], [132.0, 130.0, 8.0, 255.0])
        self.assertEqual(px[8:12], [(2 * 255 + 132) / 3.0, 130 / 3.0, 8 / 3.0, 255.0])     # unrounded
        self.assertEqual(px[12:16], [(255 + 2 * 132) / 3.0, 260 / 3.0, 16 / 3.0, 255.0])

    def test_dxt1_three_colour_block(self):
        c0, c1 = 1 << 11, 31 << 11          # c0 <= c1: index 2 = mean, index 3 = transparent black
        b = R.Blp(blp_header(2, 1, 0, 4, 4, [struct.pack('<HHI', c0, c1, 0b11100100)]))
        px = b.decode(0)[2]
        self.assertEqual(px[8:12], [(8 + 255) / 2.0, 0.0, 0.0, 255.0])
        self.assertEqual(px[12:16], [0.0, 0.0, 0.0, 0.0])

    def test_dxt3_and_dxt5(self):
        c0, c1 = 1 << 11, 31 << 11          # c0 <= c1, but DXT3/DXT5 colour blocks are always four-colour
        colour = struct.pack('<HHI', c0, c1, 0b11100100)
        a3 = bytes([0x21, 0xF3] + [0] * 6)  # texels 0..3: 1, 2, 3, 15 -> x 17
        p3 = R.Blp(blp_header(2, 8, 1, 4, 4, [a3 + colour])).decode(0)[2]
        self.assertEqual([p3[4 * k + 3] for k in range(4)], [17.0, 34.0, 51.0, 255.0])
        self.assertEqual(p3[12:15], [(8 + 2 * 255) / 3.0, 0.0, 0.0])
        # DXT5, a0 > a1 (8 values) and a0 <= a1 (6 values + 0 + 255); indices 0..7 in the first 8 texels
        idx = sum(k << (3 * k) for k in range(8)).to_bytes(6, 'little')
        p5 = R.Blp(blp_header(2, 8, 7, 4, 4, [bytes([200, 60]) + idx + colour])).decode(0)[2]
        self.assertEqual([p5[4 * k + 3] for k in range(8)], [200.0, 60.0] + [((7 - i) * 200 + i * 60) / 7.0 for i in range(1, 7)])
        p6 = R.Blp(blp_header(2, 8, 7, 4, 4, [bytes([60, 200]) + idx + colour])).decode(0)[2]
        self.assertEqual([p6[4 * k + 3] for k in range(8)],
                         [60.0, 200.0] + [((5 - i) * 60 + i * 200) / 5.0 for i in range(1, 5)] + [0.0, 255.0])

    def test_rounded_decode(self):
        """The 'rounded' decode (the player's CPU decode of a block texture, BlpDecoder's exact-blocks mode): the exact
        decode rounded to nearest, endpoints unchanged; the DXT1 three-colour midpoint rounds ties to even (131.5 ->
        132, 16.5 -> 16); DXT5 alpha ramps round (no tie occurs for /7 and /5)."""
        c0, c1 = 31 << 11, (16 << 11) | (32 << 5) | 1
        px = R.Blp(blp_header(2, 0, 0, 4, 4, [struct.pack('<HHI', c0, c1, 0b11100100)]), model='rounded').decode(0)[2]
        self.assertEqual(px[0:16], [255.0, 0.0, 0.0, 255.0, 132.0, 130.0, 8.0, 255.0, 214.0, 43.0, 3.0, 255.0,
                                    173.0, 87.0, 5.0, 255.0])
        for lo, hi, want in ((1 << 11, 31 << 11, 132.0), (0, 4 << 11, 16.0)):      # (8 + 255) / 2, (0 + 33) / 2
            px = R.Blp(blp_header(2, 1, 0, 4, 4, [struct.pack('<HHI', lo, hi, 0b11100100)]), model='rounded').decode(0)[2]
            self.assertEqual(px[8:12], [want, 0.0, 0.0, 255.0])
            self.assertEqual(px[12:16], [0.0, 0.0, 0.0, 0.0])
        idx = sum(k << (3 * k) for k in range(8)).to_bytes(6, 'little')
        colour = struct.pack('<HHI', c0, c1, 0)
        for a0, a1 in ((200, 61), (61, 200)):
            p = R.Blp(blp_header(2, 8, 7, 4, 4, [bytes([a0, a1]) + idx + colour]), model='rounded').decode(0)[2]
            e = R.Blp(blp_header(2, 8, 7, 4, 4, [bytes([a0, a1]) + idx + colour])).decode(0)[2]
            self.assertEqual([p[4 * k + 3] for k in range(8)], [float(round(e[4 * k + 3])) for k in range(8)])
        self.assertEqual([R.nearest(n, 2) for n in (261, 263, 33, 35)], [130.0, 132.0, 16.0, 18.0])

    def test_palettised_and_bgra(self):
        for depth, alpha_bytes, want in ((0, b'', [255.0] * 4), (1, bytes([0b0101]), [255.0, 0.0, 255.0, 0.0]),
                                         (4, bytes([0x2F, 0x00]), [255.0, 34.0, 0.0, 0.0]), (8, bytes([1, 2, 3, 4]), [1.0, 2.0, 3.0, 4.0])):
            data = bytearray(blp_header(1, depth, 0, 2, 2, [bytes([0, 1, 2, 3]) + alpha_bytes]))
            for k in range(4):
                data[148 + 4 * k:148 + 4 * k + 4] = bytes((10 * k, 20 * k, 30 * k, 99))   # B, G, R, A
            px = R.Blp(bytes(data)).decode(0)[2]
            with self.subTest(depth=depth):
                self.assertEqual([px[4 * k + 3] for k in range(4)], want)
                self.assertEqual(px[4:7], [30.0, 20.0, 10.0])
        b = R.Blp(blp_bgra(2, 1, lambda x, y: (1 + x, 2, 3, 4), mips=False))
        self.assertEqual(b.decode(0)[2], [1.0, 2.0, 3.0, 4.0, 2.0, 2.0, 3.0, 4.0])

    def test_levels_small_blocks_and_rows_as_stored(self):
        # a 4x8 DXT1 texture: level 0 two blocks (rows 0-3 red, rows 4-7 blue), level 1 (2x4) and level 2 (1x2) in
        # one block each, cropped
        red, blue = struct.pack('<HHI', 31 << 11, 31 << 11, 0), struct.pack('<HHI', 31, 31, 0)
        b = R.Blp(blp_header(2, 0, 0, 4, 8, [red + blue, red, blue, red]))
        self.assertEqual(len(b.levels), 4)
        w, h, px = b.decode(0)
        self.assertEqual((w, h), (4, 8))
        self.assertEqual(px[0:3], [255.0, 0.0, 0.0])                     # row 0 first, no flip
        self.assertEqual(px[4 * 4 * 7:4 * 4 * 7 + 3], [0.0, 0.0, 255.0])
        self.assertEqual(b.decode(1)[:2], (2, 4))
        self.assertEqual(b.decode(3)[:2], (1, 1))

    def test_malformed(self):
        good = blp_header(2, 0, 0, 4, 4, [bytes(8)])
        for bad, why in ((b'BLP1' + good[4:], 'magic'), (good[:150], 'truncated level'),
                         (good[:8] + bytes([4]) + good[9:], 'encoding'), (good[:10] + bytes([5]) + good[11:], 'alpha type'),
                         (blp_header(2, 0, 0, 0, 4, [bytes(8)]), 'zero width')):
            with self.subTest(why=why):
                with self.assertRaises(R.BlpError):
                    R.Blp(bad).decode(0)
        with self.assertRaises(R.BlpError):
            R.Blp(good).decode(1)


# ------------------------------------------------------------------------------------------------- alpha + states

def fill(v):
    return [v] * 4096


DEFAULT_MTXP = (0, 0.0, 1.0)


class AlphaAndStates(unittest.TestCase):
    MDID = [101, 102, 103, 104, 105, 106, 0]

    def material(self, per_chunk, mphd=0x84, mdid=None, root=None, missing=(), **kw):
        return R.build_material(root or make_root(32, 50), make_tex0(per_chunk, mdid or self.MDID, **kw), mphd, missing)

    def state(self, layers, **kw):
        """Chunk 0's (state, reason) with `layers`; every other chunk has none (NoLayers, unless a tile-wide or
        root condition comes first)."""
        m = self.material(lambda n: layers if n == 0 else [], **kw)
        self.assertEqual(sum(m['counters'].values()), 256)
        if m['chunks'][0]['state'] == 'Malformed':
            self.assertEqual(m['counters']['noLayers'], 255)      # a tile-table problem spares chunks without layers
        return m['chunks'][0]['state'], m['chunks'][0]['reason']

    def test_decoders(self):
        vals = [(r * 64 + c) % 256 for r in range(64) for c in range(64)]
        for storage in ('rle', 'raw8'):
            m = self.material(lambda n: [(0, 0, None, None), (1, 0, storage, vals)])
            self.assertEqual(m['chunks'][5]['layers'][1]['alpha'], bytes(vals))
            self.assertEqual(m['chunks'][5]['layers'][1]['storage'], storage)
        four = [(r + c) % 16 for r in range(64) for c in range(64)]
        m = self.material(lambda n: [(0, 0, None, None), (1, 0, 'raw4', four)], mphd=0)
        self.assertEqual(m['chunks'][0]['layers'][1]['alpha'], bytes(four))          # low nibble first, 0..15 kept
        self.assertEqual(m['chunks'][0]['state'], 'AlphaUnsupported')
        good = bytes([0x80 | 100, 7, 3, 1, 2, 3] + [0xFF, 9] * 31 + [0x80 | 56, 9])     # 100 + 3 + 31 x 127 + 56
        values, used = R.decode_rle(good + bytes(10), 0, len(good) + 10)
        self.assertEqual((values[:104], values[-1], len(values), used), (bytes([7] * 100 + [1, 2, 3, 9]), 9, 4096, len(good)))
        self.assertIsNone(R.decode_rle(bytes([0x80 | 127, 1] * 33), 0, 66)[0])          # run past value 4096
        self.assertIsNone(R.decode_rle(bytes([0x80 | 127, 1] * 3), 0, 6)[0])            # ends early
        self.assertIsNone(R.decode_rle(bytes([5, 1, 2]), 0, 3)[0])                      # copy past the end

    def test_every_state_and_reason(self):
        ok = fill(100)
        two = [(0, 0, None, None), (1, 0, 'raw8', ok)]
        mtxp = [DEFAULT_MTXP] * 7
        mhid0 = [0] * 7
        cases = [
            ([(0, 0, None, None), (9, 0, 'raw8', ok)], {}, 'Malformed', 'MDID has'),
            ([(0, 0, None, None), (1, 0, 'raw8', fill(200)), (2, 0, 'raw8', fill(56))], {}, 'Malformed', 'alpha sum 256'),
            ([(0, 0, None, None), (1, 0, 'raw4', fill(3))], {}, 'Malformed', 'MPHD 0x84 says 8-bit'),
            (two, {'mphd': 0}, 'Malformed', 'says 4-bit'),
            ([(0, 0, None, None), (1, 0, 'rle', ok)], {'mphd': 0}, 'Malformed', 'says 4-bit'),
            ([(0, 0x100 | 0x10000, None, None)], {}, 'Malformed', 'flag 0x100 set'),
            ([(0, 0, None, None)], {'root': make_root(32, 50, mccv=lambda n: bytes(400) if n == 0 else None)}, 'Malformed',
             'MCCV is repeated or not 580 bytes'),
            (two, {'mtxp': [DEFAULT_MTXP] * 3}, 'Malformed', 'MTXP has 3 entries'),
            (two, {'mtxp': [DEFAULT_MTXP, (0, float('nan'), 1.0)] + [DEFAULT_MTXP] * 5}, 'Malformed', 'not finite'),
            (two, {'mtxp': [(0, 0.0, float('inf'))] + [DEFAULT_MTXP] * 6}, 'Malformed', 'not finite'),
            (two, {'mphd': HEIGHT_MAP, 'mtxp': [(0, float('nan'), -1.0)] + [DEFAULT_MTXP] * 6}, 'Malformed', 'not finite'),
            # tile tables (design v3): repeated, not whole u32, MHID count unlike MDID's; never read as "none"
            (two, {'mhid': mhid0, 'extra': [chunk('MHID', struct.pack('<7I', 2004, 0, 0, 0, 0, 0, 0))]}, 'Malformed',
             'MHID appears 2 times'),
            (two, {'extra': [chunk('MDID', struct.pack('<7I', *range(5000, 5007)))]}, 'Malformed', 'MDID appears 2 times'),
            (two, {'mtxp': mtxp, 'extra': [chunk('MTXP', mtxp_entries([(0x20, 5.0, 1.0)] + [DEFAULT_MTXP] * 6))]},
             'Malformed', 'MTXP appears 2 times'),
            (two, {'mhid': struct.pack('<I', 2004) + bytes(2)}, 'Malformed', 'MHID is 6 bytes'),
            (two, {'mhid': [0, 0]}, 'Malformed', 'MHID has 2 entries, MDID 7'),
            (two, {'mhid': b''}, 'Malformed', 'MHID has 0 entries'),
            ([(0, 0, None, None), (1, 0, 'raw4', fill(3))], {'mphd': 0}, 'AlphaUnsupported', '4-bit'),
            (two, {'root': make_root(32, 50, flags=lambda n: 0x10000)}, 'AlphaUnsupported', '0x8000'),
            # 6..8 layers are drawn (the map generator composites them by the same rule: map 2923); more than 8 is
            # beyond the parser's limit (Malformed), so LayerCountUnsupported is reached only by a crafted chunk
            # (test_layer_count_beyond_eight)
            ([(0, 0, None, None)] + [(k, 0, 'rle', fill(40)) for k in range(1, 6)], {}, 'Textured', None),
            ([(k % 6, 0, None, None) if k == 0 else (k % 6, 0, 'raw8', fill(30)) for k in range(8)], {}, 'Textured', None),
            ([(k % 6, 0, None, None) if k == 0 else (k % 6, 0, 'raw8', fill(25)) for k in range(9)], {}, 'Malformed',
             'MCLY of 144 bytes (whole 16-byte layers, at most 8)'),
            ([(0, 0x400, None, None)], {}, 'LayerEffectUnresolved', '0x400 (cube map)'),
            ([(0, 0, None, None), (1, 0x800, 'rle', ok)], {}, 'LayerEffectUnresolved', 'bit(s) 0x800 never observed'),
            ([(0, 0x1000, None, None)], {}, 'LayerEffectUnresolved', 'bit(s) 0x1000 never observed'),
            (two, {'mtxp': [(0x1, 0.0, 1.0)] + [DEFAULT_MTXP] * 6}, 'LayerEffectUnresolved', '0x1 (cube-map texture)'),
            (two, {'mtxp': [DEFAULT_MTXP, (0x110, 0.0, 1.0)] + [DEFAULT_MTXP] * 5}, 'LayerEffectUnresolved',
             'bit(s) 0x100 never observed'),
            (two, {'mtxp': [(0, 0.0, 1.0, 7)] + [DEFAULT_MTXP] * 6}, 'LayerEffectUnresolved', 'word 3 = 7'),
            # MTXP scale nibble 4 (real: Retail map 2923) is drawn at 0.5 repeats; nibbles above 4 were never observed
            (two, {'mtxp': [(0x40, 0.0, 1.0)] + [DEFAULT_MTXP] * 6}, 'Textured', None),
            (two, {'mtxp': [(0x50, 0.0, 1.0)] + [DEFAULT_MTXP] * 6}, 'LayerEffectUnresolved',
             'layer 0: MTXP texture scale nibble 5: never observed (0..4 verified)'),
            (two, {'mtxp': [DEFAULT_MTXP, (0xF0, 0.0, 1.0)] + [DEFAULT_MTXP] * 5}, 'LayerEffectUnresolved',
             'layer 1: MTXP texture scale nibble 15: never observed (0..4 verified)'),
            # MTXP height values where H = h*s + o can fall below 0 or overflow float32 (MPHD 0x80 only)
            (two, {'mphd': HEIGHT_MAP, 'mtxp': [(0, 0.0, 0.3), (0, 0.0, -1.0)] + [DEFAULT_MTXP] * 5},
             'LayerEffectUnresolved', 'layer 1: MTXP height values (scale 0.0, offset -1.0) let H = h*s + o fall below 0 '
             'or overflow float32; the height mix is not defined there'),
            (two, {'mphd': HEIGHT_MAP, 'mhid': [0, 555, 0, 0, 0, 0, 0], 'mtxp': [DEFAULT_MTXP, (0, 3e38, 3e38)] + [DEFAULT_MTXP] * 5},
             'LayerEffectUnresolved', 'fall below 0 or overflow float32'),
            (two, {'mphd': HEIGHT_MAP, 'mhid': [0, 555, 0, 0, 0, 0, 0], 'mtxp': [DEFAULT_MTXP, (0, -2.0, 1.0)] + [DEFAULT_MTXP] * 5},
             'LayerEffectUnresolved', 'scale -2.0, offset 1.0'),
            (two, {'mphd': HEIGHT_MAP, 'mhid': [0, 555, 0, 0, 0, 0, 0], 'missing': {555: 'not in client storage'}},
             'HeightTextureMissing', 'height texture 555 not in client storage'),
            ([], {}, 'NoLayers', 'no layers'),
            ([(0, 0x40, None, None)], {}, 'LayerEffectStatic', '0x40'),
            ([(0, 0, None, None), (1, 0x80, 'raw8', ok)], {}, 'LayerEffectStatic', '0x180'),
            ([(0, 0x3, None, None)], {}, 'LayerEffectStatic', '0x3'),
            ([(0, 0, None, None), (1, 0xea, 'rle', ok)], {}, 'LayerEffectStatic', '0x3ea'),
            ([(6, 0, None, None)], {}, 'MissingTexture', 'MDID 0'),
            ([(0, 0, None, None), (2, 0, 'raw8', ok)], {'missing': {103: 'not in client storage'}}, 'MissingTexture',
             'not in client storage'),
            ([(0, 0, None, None), (1, 0, 'rle', ok), (2, 0, 'raw8', fill(155))], {}, 'Textured', None),
            # height data is resolved: drawn Textured on an 0x80 map (the mix reads it) and on an 0x80-clear map (it
            # does not); a failed height texture matters only where 0x80 lets it in
            (two, {'mphd': HEIGHT_MAP, 'mhid': [0, 555, 0, 0, 0, 0, 0], 'mtxp': [DEFAULT_MTXP, (0x10, 5.0, 1.0)] + [DEFAULT_MTXP] * 5},
             'Textured', None),
            (two, {'mphd': NO_HEIGHT_MAP, 'mhid': [0, 555, 0, 0, 0, 0, 0], 'missing': {555: 'not in client storage'}},
             'Textured', None),
        ]
        for layers, kw, want, why in cases:
            with self.subTest(want=want, why=why):
                got, reason = self.state(layers, **kw)
                self.assertEqual(got, want, reason)
                if why:
                    self.assertIn(why, reason)
                else:
                    self.assertIsNone(reason)

    def test_precedence(self):
        """The first state of the table wins wherever several apply."""
        mhid = {'mphd': HEIGHT_MAP, 'mhid': [0, 7, 0, 0, 0, 0, 0], 'missing': {7: 'failed'}}
        cases = [
            ([(9, 0x40, None, None)], {}, 'Malformed'),                                         # Malformed > effect
            ([(0, 0x400, None, None), (1, 0, 'raw8', fill(9))], {'mhid': [0, 0]}, 'Malformed'),    # tables first
            ([(0, 0x400, None, None), (1, 0, 'raw4', fill(1))], {'mphd': 0}, 'AlphaUnsupported'),
            # six layers are drawn now, so the effect decides (LayerCountUnsupported > LayerEffectUnresolved:
            # test_layer_count_beyond_eight)
            ([(0, 0x400, None, None)] + [(k, 0, 'rle', fill(40)) for k in range(1, 6)], {}, 'LayerEffectUnresolved'),
            ([(0, 0x400, None, None), (1, 0, 'raw8', fill(9))], mhid, 'LayerEffectUnresolved'),
            ([(0, 0x40, None, None), (1, 0, 'raw8', fill(9))], mhid, 'HeightTextureMissing'),
            ([(6, 0x80, None, None)], {}, 'LayerEffectStatic'),                                 # static > missing
            ([(0, 0, None, None), (1, 0, 'raw8', fill(200)), (2, 0, 'raw4', fill(1))], {}, 'Malformed'),
            # a scale nibble above 4 is an unresolved effect, below the alpha states and above a missing height;
            # nibble 4 itself is drawn, so the missing height decides
            ([(0, 0, None, None), (1, 0, 'raw4', fill(1))], {'mphd': 0, 'mtxp': [(0x50, 0.0, 1.0)] + [DEFAULT_MTXP] * 6},
             'AlphaUnsupported'),
            ([(0, 0, None, None), (1, 0, 'raw8', fill(9))], dict(mhid, mtxp=[(0x50, 0.0, 1.0)] + [DEFAULT_MTXP] * 6),
             'LayerEffectUnresolved'),
            ([(0, 0, None, None), (1, 0, 'raw8', fill(9))], dict(mhid, mtxp=[(0x40, 0.0, 1.0)] + [DEFAULT_MTXP] * 6),
             'HeightTextureMissing'),
            ([(0, 0, None, None), (1, 0, 'raw8', fill(9))], dict(mhid, mtxp=[(0, 0.0, -1.0)] + [DEFAULT_MTXP] * 6),
             'LayerEffectUnresolved'),
        ]
        for layers, kw, want in cases:
            with self.subTest(want=want):
                self.assertEqual(self.state(layers, **kw)[0], want)

    def test_layer_count_beyond_eight(self):
        """At most 8 layers are drawn (8 is the most observed); a chunk with more is LayerCountUnsupported, above
        LayerEffectUnresolved. A parsed MCLY of more than 8 layers is Malformed first (the parser's limit), so the
        state is reached only by a chunk handed to chunk_state directly."""
        tex = {'tile_problem': None, 'mtxp_problem': None, 'mtxp': None, 'mdid': [101], 'mhid': [], 'mhid_present': False}
        rch = {'problem': None, 'flags': 0x8000}
        for flags, n in ((0, 9), (0x400, 9), (0, 8)):
            layers = [{'texture': 0, 'flags': (0x100 if k else 0) | (flags if k == 0 else 0), 'alpha_offset': 4096 * (k - 1)}
                      for k in range(n)]
            tch = {'problem': None, 'layers': layers, 'alpha': [None] + [('raw8', bytes([20]) * 4096)] * (n - 1),
                   'mcsh': False}
            with self.subTest(flags=flags, layers=n):
                got = R.chunk_state(tex, tch, rch, 0x84, set())
                if n == 9:
                    self.assertEqual(got, ('LayerCountUnsupported', '9 layers; at most 8 are drawn (8 is the most observed)'))
                else:
                    self.assertEqual(got, ('Textured', None))

    def test_height_value_domain(self):
        """With MPHD 0x80 and an MTXP entry, H = h * s + o (h in 0..1, h = 0 without a height texture) must stay >= 0
        and finite in float32, else LayerEffectUnresolved; H = 0 exactly (the S == 0 fallback) is still drawn; without
        0x80 the values are ignored (H = 1) and give no state."""
        two = [(0, 0, None, None), (1, 0, 'raw8', fill(128))]
        cases = [(HEIGHT_MAP, 0, (0, 0.0, 0.0), 'Textured'),            # H = 0 exactly
                 (HEIGHT_MAP, 555, (0, 0.0, 0.0), 'Textured'),
                 (HEIGHT_MAP, 0, (0, 0.0, -0.0), 'Textured'),           # -0.0 is not below 0
                 (HEIGHT_MAP, 0, (0, -2.0, 1.0), 'Textured'),           # no height texture: H = o
                 (HEIGHT_MAP, 555, (0, -1.0, 1.0), 'Textured'),         # s + o = 0: H in [0, 1]
                 (HEIGHT_MAP, 555, (0, -1.5, 1.0), 'LayerEffectUnresolved'),
                 (HEIGHT_MAP, 0, (0, 0.0, -1e-30), 'LayerEffectUnresolved'),
                 (HEIGHT_MAP, 0, (0, 3e38, 3e38), 'Textured'),          # no height texture: H = o, finite
                 (HEIGHT_MAP, 555, (0, 2e38, 2e38), 'LayerEffectUnresolved'),       # s + o is inf in float32
                 (HEIGHT_MAP, 555, (0, 2e38, 1.4e38), 'Textured'),      # 3.4e38 < the float32 maximum
                 (NO_HEIGHT_MAP, 555, (0, -1.5, -1.0), 'Textured'),     # 0x80 clear: ignored
                 (NO_HEIGHT_MAP, 555, (0, 2e38, 2e38), 'Textured')]
        for mphd, hid, entry, want in cases:
            with self.subTest(mphd=mphd, hid=hid, entry=entry):
                got, reason = self.state(two, mphd=mphd, mhid=[0, hid, 0, 0, 0, 0, 0],
                                         mtxp=[DEFAULT_MTXP, entry] + [DEFAULT_MTXP] * 5)
                self.assertEqual(got, want, reason)
        self.assertEqual(R.float32(2e38 + 2e38), float('inf'))
        self.assertLess(R.float32(2e38 + 1.4e38), float('inf'))

    def test_layers_repeats_colours_notes(self):
        mccv = [(10 + k % 50, 127, 200, 255) for k in range(145)]
        root = make_root(32, 50, flags=lambda n: 0x18000 | (0x40 if n in (1, 2, 5, 6) else 0) | (0x1 if n == 3 else 0),
                         mccv=lambda n: mccv if n in (1, 4, 5, 6) else None)
        m = self.material(lambda n: [] if n == 5 else [(0, 0x400, None, None)] if n == 6 else
                          [(n % 3, 0, None, None), (3, 0, 'rle', fill(50))],
                          mtxp=[(0, 0.0, 1.0), (0x10, 0.0, 1.0), (0x30, 0.0, 1.0)] + [(0, 0.0, 1.0)] * 4, root=root)
        self.assertEqual([l['repeats'] for l in m['chunks'][0]['layers']], [8, 8])
        self.assertEqual([l['repeats'] for l in m['chunks'][1]['layers']], [4, 8])
        self.assertEqual([l['repeats'] for l in m['chunks'][2]['layers']], [1, 8])
        self.assertEqual(m['chunks'][1]['vertexColors'][0], (200, 127, 10))                  # B, G, R stored
        self.assertIsNone(m['chunks'][2]['vertexColors'])                                    # flag without MCCV
        self.assertIsNone(m['chunks'][4]['vertexColors'])                                    # MCCV without flag
        self.assertEqual(m['chunks'][5]['state'], 'NoLayers')
        self.assertIsNone(m['chunks'][5]['vertexColors'])                                    # NoLayers: none
        self.assertEqual(m['chunks'][6]['state'], 'LayerEffectUnresolved')                   # kept for the
        self.assertIsNotNone(m['chunks'][6]['vertexColors'])                                 # diagnostic view
        self.assertEqual(m['notes']['mccvFlagMismatch'], 2)
        self.assertEqual(m['notes']['mccvApplied'], 2)
        self.assertEqual(m['uniqueTextures'], [101, 104, 102, 103])                          # first use
        self.assertEqual(m['layerReferences'], 508)
        mcsh = make_tex0(lambda n: [(0, 0, None, None)], self.MDID,
                         raw={7: chunk('MCNK', chunk('MCLY', struct.pack('<IIIi', 0, 0, 0, -1)) + chunk('MCSH', bytes(512)))})
        self.assertEqual(R.build_material(make_root(32, 50), mcsh, 0x84)['notes']['mcsh'], 1)

    def test_drawable_unique_ids_skip_neutral_chunks_and_zero(self):
        m = self.material(lambda n: [(6, 0, None, None)] if n == 0 else [(4, 0x400, None, None)] if n == 1 else
                          [(3, 0x40, None, None)] if n == 2 else [(5, 0, None, None)])
        self.assertEqual(m['uniqueTextures'], [104, 106])        # 0x400 is neutral, 0x40 (static) is drawn
        self.assertEqual(m['zeroIdReferences'], 1)
        self.assertEqual((m['counters']['missingTexture'], m['counters']['layerEffectStatic']), (1, 1))

    def test_static_chunk_with_a_checker_layer_is_masked(self):
        """LayerEffectStatic keeps its precedence over MissingTexture (no new state or counter), but a static chunk
        whose layer draws the checker (texture failed, or MDID 0) is masked by material_keep like a missing-texture
        chunk, whatever its state."""
        m = self.material(lambda n: [(0, 0x40, None, None), (2, 0, 'raw8', fill(90))] if n == 0 else
                          [(6, 0x80, None, None)] if n == 1 else [(6, 0, None, None)] if n == 2 else
                          [(0, 0x40, None, None)] if n == 3 else [(0, 0, None, None)], missing={103: 'not installed locally'})
        self.assertEqual([m['chunks'][n]['state'] for n in range(5)],
                         ['LayerEffectStatic', 'LayerEffectStatic', 'MissingTexture', 'LayerEffectStatic', 'Textured'])
        self.assertEqual([m['chunks'][n]['checkerLayers'] for n in range(5)], [[1], [0], [0], [], []])
        self.assertEqual((m['counters']['layerEffectStatic'], m['counters']['missingTexture']), (3, 1))
        keep = R.material_keep(m)
        per = R.MAP_SIZE // 4 // 16                        # blocks per chunk side
        kept = lambda n: keep[(n // 16) * per * R.MAP_SIZE // 4 + (n % 16) * per]
        self.assertEqual([kept(n) for n in range(5)], [False, False, False, True, True])

    def test_height_ids_and_layer_heights(self):
        """MHID ids of drawable chunks (first use) on an MPHD 0x80 map only; each layer carries its stored height
        data; layer_height lets it into the mix per the map flag ('design'), never ('default') or always ('full')."""
        per = lambda n: [(n % 2, 0, None, None), (2, 0, 'rle', fill(80))] if n != 9 else [(3, 0x400, None, None)]
        kw = dict(mdid=[101, 102, 103, 104], mhid=[0, 201, 203, 204],
                  mtxp=[DEFAULT_MTXP, (0, 0.0, 0.5), (0x10, 4.0, 1.0), DEFAULT_MTXP])
        m = self.material(per, mphd=HEIGHT_MAP, **kw)
        self.assertEqual(m['uniqueTextures'], [101, 103, 102])
        self.assertEqual(m['uniqueHeightTextures'], [203, 201])             # 204 is used only by a neutral chunk
        lay = m['chunks'][1]['layers']
        self.assertEqual([(l['heightTextureID'], l['heightScale'], l['heightOffset']) for l in lay],
                         [(201, 0.0, 0.5), (203, 4.0, 1.0)])
        self.assertEqual([R.layer_height(m, l) for l in lay], [(201, 0.0, 0.5), (203, 4.0, 1.0)])
        self.assertEqual([R.layer_height(m, l) for l in m['chunks'][0]['layers']], [None, (203, 4.0, 1.0)])
        self.assertIsNone(R.chunk_heights(m, m['chunks'][0], 'default'))
        self.assertEqual(R.needed_textures(m, m['chunks'][1]), [(102, 'diffuse'), (103, 'diffuse'), (201, 'height'),
                                                                (203, 'height')])
        clear = self.material(per, mphd=NO_HEIGHT_MAP, **kw)
        self.assertEqual(clear['uniqueHeightTextures'], [])
        self.assertIsNone(R.chunk_heights(clear, clear['chunks'][1]))
        self.assertEqual(R.chunk_heights(clear, clear['chunks'][1], 'full'), [(201, 0.0, 0.5), (203, 4.0, 1.0)])
        self.assertEqual(R.needed_textures(clear, clear['chunks'][1]), [(102, 'diffuse'), (103, 'diffuse')])
        # without MHID and MTXP every layer has H = 1 on either map
        plain = self.material(per, mphd=HEIGHT_MAP, mdid=[101, 102, 103, 104])
        self.assertEqual((plain['uniqueHeightTextures'], R.chunk_heights(plain, plain['chunks'][1])), ([], None))

    def test_unusable_tex0(self):
        with self.assertRaises(R.MaterialUnavailable):
            R.read_tex0(chunk('MVER', struct.pack('<I', 18)) + chunk('MCNK', b'') * 10)
        with self.assertRaises(R.MaterialUnavailable):
            R.read_tex0(make_tex0(lambda n: [], [1])[:-3])


# ------------------------------------------------------------------------------------------------- composition

class Composition(unittest.TestCase):
    def test_fan_interpolation(self):
        cols = [(k, 2 * k, 255 - k) for k in range(145)]
        self.assertEqual(R.mccv_fan(cols, 0.0, 0.0), (0.0, 0.0, 255.0))                   # outer (0, 0)
        self.assertEqual(R.mccv_fan(cols, 1 / 16.0, 1 / 16.0), (9.0, 18.0, 246.0))           # inner (0, 0)
        self.assertEqual(R.mccv_fan(cols, 1.0, 1.0), (144.0, 288.0, 111.0))                  # outer (8, 8)
        for u, v in ((0.03, 0.03), (0.09, 0.03), (0.4, 0.4 + 1e-12)):                        # continuous on the diagonals
            a = R.mccv_fan(cols, u, v)
            b = R.mccv_fan(cols, u + 1e-9, v - 1e-9)
            self.assertTrue(all(abs(x - y) < 1e-6 for x, y in zip(a, b)))

    def test_alpha_at_pixel_centres_is_the_2x2_mean(self):
        vals = [(7 * r + 3 * c * c) % 256 for r in range(64) for c in range(64)]
        got = R.resample_alpha(vals, R._axis(R.alpha_positions(32), 64, False))
        for y in range(32):
            for x in range(32):
                mean = sum(vals[(2 * y + j) * 64 + 2 * x + i] for j in (0, 1) for i in (0, 1)) / 4.0 / 255.0
                self.assertAlmostEqual(got[y * 32 + x], mean, places=12)
        self.assertEqual(R.alpha_texel_centre(0), 0.5 / 64)

    def test_height_mix_rule(self):
        """The cutoff drops layers far below the leader: (0.7, 0.3) -> (0.795, 0.205), (0.9, 0.1) -> (0.978, 0.022),
        (0.5, 0.5) unchanged; heights scale the competition; S = 0 falls back to the weights; negative weights
        count as 0; 'plain' leaves the weights alone; the sharpening controls normalise w^e."""
        def one(ws, hs=None, rule='heightmix'):
            return [c[0] for c in R.mix([[w] for w in ws], None if hs is None else [[h] for h in hs], rule)]
        for ws, want in (((0.7, 0.3), (0.7 / 0.88, 0.18 / 0.88)), ((0.9, 0.1), (0.9 / 0.92, 0.02 / 0.92)),
                         ((0.5, 0.5), (0.5, 0.5)), ((0.6, 0.3, 0.1), mix_spec([0.6, 0.3, 0.1]))):
            for g, w in zip(one(ws), want):
                self.assertAlmostEqual(g, w, places=12)
        self.assertEqual(one((0.5, 0.5), (1.0, 3.0)), [0.0, 1.0])            # p = (0.5, 1.5): layer 0 cut off
        for g, w in zip(one((0.6, 0.4), (1.0, 1.4)), mix_spec([0.6, 0.4], [1.0, 1.4])):
            self.assertAlmostEqual(g, w, places=12)
        self.assertEqual(one((0.6, 0.4), (0.0, 0.0)), [0.6, 0.4])            # S = 0: the weights themselves
        self.assertEqual(one((-1e-9, 1.0)), [0.0, 1.0])
        self.assertEqual(one((0.7, 0.3), rule='plain'), [0.7, 0.3])
        for g, w in zip(one((0.7, 0.3), rule='pow1.5'), (0.7 ** 1.5 / (0.7 ** 1.5 + 0.3 ** 1.5), 0.3 ** 1.5 / (0.7 ** 1.5 + 0.3 ** 1.5))):
            self.assertAlmostEqual(g, w, places=12)
        self.assertEqual(R.composite_weights([], 3, 'sequential'), [[1.0, 1.0, 1.0]])     # one layer: weight 1
        a = [0.4]
        self.assertEqual([round(c[0], 12) for c in R.composite_weights([a, a], 1, 'plain weights')], [0.2, 0.4, 0.4])
        self.assertEqual([round(c[0], 12) for c in R.composite_weights([a, a], 1, 'sequential')], [0.36, 0.24, 0.4])

    def setUp(self):
        self.tex = {101: R.Blp(blp_bgra(4, 4, pattern(1))), 102: R.Blp(blp_bgra(4, 4, pattern(2))),
                    103: R.Blp(blp_bgra(4, 4, height_pattern(3)))}

    def tile(self, mphd=0x84, mccv=None, mhid=None, mtxp=None):
        root = make_root(32, 50, flags=lambda n: 0x18000 | (0x40 if mccv else 0), mccv=lambda n: mccv)
        return R.build_material(root, make_tex0(lambda n: [(0, 0, None, None), (1, 0, 'rle', fill(64))], [101, 102],
                                                mhid=mhid, mtxp=mtxp), mphd)

    def test_map_mode_orientation_weights_and_clip(self):
        p1, p2 = pattern(1), pattern(2)
        a = 64 / 255.0
        cut = mix_spec([1 - a, a])
        for rule, wts in (('heightmix', cut), ('plain weights', (1 - a, a))):
            img, info = R.map_composite(self.tile(), self.tex.get, rule=rule)
            for y, x in ((0, 0), (1, 2), (3, 1), (37, 70), (511, 0), (5, 511)):
                for c in range(3):
                    want = wts[0] * p1(x % 4, y % 4)[c] + wts[1] * p2(x % 4, y % 4)[c]   # u east, v south, no flip
                    self.assertAlmostEqual(img[c][y * 512 + x], want, places=9)
            self.assertEqual(info['clipped'], 0)
        bright = [(255, 255, 255, 255)] * 145                                                # x 255/127
        img, info = R.map_composite(self.tile(mccv=bright), self.tex.get)
        self.assertEqual(max(img[0]), 255.0)
        self.assertGreater(info['clipped'], 0)
        raw, _ = R.map_composite(self.tile(mccv=bright), self.tex.get, clip=False)
        self.assertGreater(max(raw[0]), 255.0)

    def test_heights_in_map_mode(self):
        """MPHD 0x80: H = alpha(height texture at the layer's UV) / 255 * scale + offset; without 0x80 the same tile
        mixes with H = 1, and 'full' brings the heights back; a failed height texture leaves its chunks undrawn."""
        hp = height_pattern(3)
        kw = dict(mhid=[0, 103], mtxp=[(0, 0.0, 1.0), (0, 4.0, 0.5)])
        a = 64 / 255.0
        img, info = R.map_composite(self.tile(HEIGHT_MAP, **kw), self.tex.get)
        for y, x in ((0, 0), (2, 3), (37, 70), (511, 6)):
            h = hp(x % 4, y % 4)[3] / 255.0
            wts = mix_spec([1 - a, a], [1.0, h * 4.0 + 0.5])
            for c in range(3):
                self.assertAlmostEqual(img[c][y * 512 + x],
                                       wts[0] * pattern(1)(x % 4, y % 4)[c] + wts[1] * pattern(2)(x % 4, y % 4)[c], places=9)
        clear = self.tile(NO_HEIGHT_MAP, **kw)
        self.assertEqual(R.map_composite(clear, self.tex.get)[0], R.map_composite(self.tile(NO_HEIGHT_MAP), self.tex.get)[0])
        self.assertEqual(R.map_composite(clear, self.tex.get, heights='full')[0], img)
        self.assertNotEqual(R.map_composite(self.tile(HEIGHT_MAP, **kw), self.tex.get, heights='default')[0], img)
        gone = R.build_material(make_root(32, 50), make_tex0(lambda n: [(0, 0, None, None), (1, 0, 'rle', fill(64))],
                                                             [101, 102], **kw), HEIGHT_MAP, {103: 'failed'})
        self.assertEqual(gone['counters']['heightTextureMissing'], 256)
        self.assertEqual(max(R.map_composite(gone, self.tex.get)[0][0]), 0.0)

    def test_validation_equals_map_mode_at_one_texel_per_pixel(self):
        """Both samplings reduce to the texel itself at one texel per pixel, so a chunk alone in validation sampling
        equals map mode at that size: alpha positions, the fan, the heights and the mix agree."""
        mccv = [(90 + k % 60, 127 + k % 3, 160 - k % 50, 255) for k in range(145)]
        m = self.tile(HEIGHT_MAP, mccv=mccv, mhid=[0, 103], mtxp=[(0, 0.0, 1.0), (0, 4.0, 0.5)])
        tex = dict((k, v.decode(0)) for k, v in self.tex.items())
        ch = m['chunks'][0]
        self.assertEqual(R.validation_size(ch, lambda fd: tex[fd][:2], m), (32, None))
        rows = list(R.validation_rows(ch, tex.get, 32, mat=m))
        img, _ = R.map_composite(m, self.tex.get, clip=False)
        for y in range(32):
            for c in range(3):
                for x in range(32):
                    self.assertAlmostEqual(rows[y][1 + c][x], img[c][y * 512 + x], places=9)
        flat = list(R.validation_rows(ch, tex.get, 32))                       # no material: H = 1
        self.assertNotEqual(flat[3][1], rows[3][1])

    def test_sequential_and_linear_differ_from_weights(self):
        layers = [(0, 0, None, None), (1, 0, 'raw8', fill(100)), (0, 0, 'raw8', fill(100))]
        m = R.build_material(make_root(32, 50), make_tex0(lambda n: layers, [101, 102]), 0x84)
        w = R.map_composite(m, self.tex.get, rule='plain weights')[0]
        s = R.map_composite(m, self.tex.get, rule='sequential')[0]
        h = R.map_composite(m, self.tex.get)[0]
        lin = R.map_composite(m, self.tex.get, linear=True)[0]
        a = 100 / 255.0
        p1, p2 = pattern(1)(0, 0)[0], pattern(2)(0, 0)[0]
        self.assertAlmostEqual(w[0][0], (1 - 2 * a) * p1 + a * p2 + a * p1, places=9)
        self.assertAlmostEqual(s[0][0], (1 - a) * (1 - a) * p1 + a * (1 - a) * p2 + a * p1, places=9)
        k = mix_spec([1 - 2 * a, a, a])
        self.assertAlmostEqual(h[0][0], k[0] * p1 + k[1] * p2 + k[2] * p1, places=9)
        self.assertNotAlmostEqual(lin[0][0], h[0][0], places=3)
        self.assertAlmostEqual(R.linear_to_srgb(R.srgb_to_linear(77.0)), 77.0, places=9)

    def test_texture_level_is_the_footprint_lod(self):
        big = R.Blp(blp_bgra(256, 256, pattern(3)))
        self.assertEqual(R.texture_level(big, 8)[:2], (6, True))       # 256 x 8 / 32 = 64 texels per pixel
        self.assertEqual(R.texture_level(big, 2)[:2], (4, True))
        self.assertEqual(R.texture_level(R.Blp(blp_bgra(8, 8, pattern(3))), 8), (1, True, None))
        self.assertIn('not stored', R.texture_level(R.Blp(blp_bgra(256, 256, pattern(3), mips=False)), 8)[2])

    def test_inexact_levels_carry_a_note(self):
        """A texture with no stored level of one texel per pixel on both axes is never sampled silently: a non-square
        texture (256 x 512 at 8 repeats: level 6 is 4 x 8) and magnification (8 x 8 at 1 repeat) return a note, and
        layer_image, height_values and map_composite pass it on."""
        lv, exact, note = R.texture_level(R.Blp(blp_bgra(256, 512, pattern(3))), 8)
        self.assertEqual((lv, exact), (6, False))
        self.assertIn('no stored level of one texel per pixel (256x512 at 8 repeats on 32 px): level 6 (4x8)', note)
        lv, exact, note = R.texture_level(R.Blp(blp_bgra(8, 8, pattern(3))), 1)
        self.assertEqual((lv, exact), (0, False))
        self.assertIn('(8x8 at 1 repeats on 32 px): level 0 (8x8)', note)
        self.assertIn('no stored level', R.layer_image(R.Blp(blp_bgra(8, 8, pattern(3))), 1)[1])
        h = R.height_values((103, 4.0, 0.5), self.tex.get, 1, 32)[1]
        self.assertIn('height texture 103: no stored level of one texel per pixel (4x4 at 1 repeats', h)
        # a tile whose layer 0 is at 1 repeat (MTXP nibble 3) and whose height texture is magnified the same way
        m = self.tile(HEIGHT_MAP, mhid=[0, 103], mtxp=[(0x30, 0.0, 1.0), (0x30, 4.0, 0.5)])
        notes = R.map_composite(m, self.tex.get)[1]['notes']
        self.assertTrue(any(n.startswith('texture 101: no stored level') for n in notes), notes)
        self.assertTrue(any(n.startswith('height texture 103: no stored level') for n in notes), notes)

    def test_validation_rows(self):
        m = self.tile()
        ch = m['chunks'][0]
        tex = {101: self.tex[101].decode(0), 102: self.tex[102].decode(0)}
        size, why = R.validation_size(ch, lambda fd: tex[fd][:2])
        self.assertEqual((size, why), (32, None))
        rows = list(R.validation_rows(ch, tex.get, size))
        a = 64 / 255.0
        k = mix_spec([1 - a, a])
        for y, x in ((0, 0), (5, 7), (31, 31)):
            want = k[0] * pattern(1)(x % 4, y % 4)[0] + k[1] * pattern(2)(x % 4, y % 4)[0]
            self.assertAlmostEqual(rows[y][1][x], want, places=9)
        # a missing layer is the checker, one square per 1/8 cell
        rows = list(R.validation_rows(ch, lambda fd: tex[101] if fd == 101 else None, 128))
        self.assertAlmostEqual(rows[0][1][0], k[0] * pattern(1)(0, 0)[0] + k[1] * 255.0, places=9)
        self.assertAlmostEqual(rows[0][1][2], k[0] * pattern(1)(0, 0)[0], places=9)
        self.assertEqual(R.validation_size({'layers': [{'fileDataID': 1, 'repeats': 8}]}, lambda fd: (24, 24))[0], None)
        # a height texture larger than the diffuse ones sets N
        hm = self.tile(HEIGHT_MAP, mhid=[0, 103], mtxp=[(0, 0.0, 1.0), (0, 4.0, 0.5)])
        sizes = {101: (4, 4), 102: (4, 4), 103: (16, 16)}
        self.assertEqual(R.validation_size(hm['chunks'][0], sizes.get, hm)[0], 128)
        self.assertEqual(R.validation_size(hm['chunks'][0], sizes.get)[0], 32)

    def test_nibble4_half_repeat_continuous_uv(self):
        """MTXP scale nibble 4: 8 / 2^4 = 0.5 repeats per chunk (never the integer 8 >> 4 = 0), one texture repeat per
        16 cells with the UV running on across chunks: chunk (ix, iy) adds the phase (frac(0.5 ix), frac(0.5 iy)).
        A single nibble-4 layer of a 64 x 64 texture tiles the map texture continuously (map pixel X, Y -> texel
        X % 64, Y % 64); chunk-local UV would restart it in every chunk. Nibble 5 is LayerEffectUnresolved with no
        repeats; validation_size takes the float repeats (2048 x 0.5 = 1024) and texture_level picks the exact level."""
        t64 = R.Blp(blp_bgra(64, 64, pattern(4)))
        m = R.build_material(make_root(32, 50), make_tex0(lambda n: [(0, 0, None, None)], [101], mtxp=[(0x40, 0.0, 1.0)]),
                             0x84)
        lay = m['chunks'][17]['layers'][0]
        self.assertEqual((m['chunks'][17]['state'], lay['repeats'], lay['scaleNibble']), ('Textured', 0.5, 4))
        self.assertEqual([R.chunk_phase(n, 0.5) for n in (0, 1, 16, 17, 255)], [(0, 0), (0.5, 0), (0, 0.5), (0.5, 0.5),
                                                                                  (0.5, 0.5)])
        self.assertEqual([R.chunk_phase(n, 8) for n in (1, 17)], [(0, 0), (0, 0)])
        self.assertEqual([R.scale_repeats(n) for n in range(6)], [8, 4, 2, 1, 0.5, 0])
        img, info = R.map_composite(m, {101: t64}.get)
        local, _ = R.map_composite(m, {101: t64}.get, nibble4=(0.5, False))
        self.assertEqual(info['notes'], [])
        p = pattern(4)
        for y, x in ((0, 0), (0, 31), (0, 32), (40, 63), (64, 64), (300, 100), (511, 511), (33, 95)):
            for c in range(3):
                self.assertEqual(img[c][y * 512 + x], float(p(x % 64, y % 64)[c]))
                self.assertEqual(local[c][y * 512 + x], float(p(x % 32, y % 32)[c]))
        self.assertNotEqual(img, local)
        m5 = R.build_material(make_root(32, 50), make_tex0(lambda n: [(0, 0, None, None)], [101], mtxp=[(0x50, 0.0, 1.0)]),
                              0x84)
        self.assertEqual((m5['chunks'][0]['state'], m5['chunks'][0]['layers'][0]['repeats']), ('LayerEffectUnresolved', 0))
        self.assertEqual(R.validation_size(m['chunks'][17], lambda fd: (2048, 2048)), (1024, None))
        self.assertEqual(R.validation_size(m['chunks'][17], lambda fd: (1, 1))[0], None)       # 0.5 texels per chunk
        self.assertEqual(R.texture_level(R.Blp(blp_bgra(256, 256, pattern(3))), 0.5), (2, True, None))

    def test_nibble4_validation_equals_map_mode(self):
        """A nibble-4 layer and its height texture share the layer's texture coordinate, chunk phase included: chunk
        17 (ix 1, iy 1) alone in validation sampling equals map mode at one texel per pixel (64 x 64 textures at 0.5
        repeats, a 4 x 4 one at 8), and chunk-local UV for the nibble-4 layer differs there."""
        mccv = [(90 + k % 60, 127 + k % 3, 160 - k % 50, 255) for k in range(145)]
        root = make_root(32, 50, flags=lambda n: 0x18040, mccv=lambda n: mccv)
        m = R.build_material(root, make_tex0(lambda n: [(0, 0, None, None), (1, 0, 'raw8', ramp(n))], [101, 102],
                                             mhid=[0, 103], mtxp=[(0, 0.0, 1.0), (0x40, 4.0, 0.5)]), HEIGHT_MAP)
        blps = {101: self.tex[101], 102: R.Blp(blp_bgra(64, 64, pattern(2))), 103: R.Blp(blp_bgra(64, 64, height_pattern(3)))}
        tex = dict((k, v.decode(0)) for k, v in blps.items())
        ch = m['chunks'][17]
        self.assertEqual(R.validation_size(ch, lambda fd: tex[fd][:2], m), (32, None))
        rows = list(R.validation_rows(ch, tex.get, 32, mat=m))
        img, info = R.map_composite(m, blps.get, clip=False)
        local, _ = R.map_composite(m, blps.get, clip=False, nibble4=(0.5, False))
        self.assertEqual(info['notes'], [])
        same_local = True
        for y in range(32):
            for c in range(3):
                for x in range(32):
                    i = (32 + y) * 512 + 32 + x
                    self.assertAlmostEqual(rows[y][1 + c][x], img[c][i], places=9)
                    same_local = same_local and abs(local[c][i] - img[c][i]) < 1e-9
        self.assertFalse(same_local)

    def test_eight_layers_composite(self):
        """Chunks of 6..8 layers are drawn by the same height mix over every layer (up to 7 overlay alpha maps)."""
        a = [10, 20, 30, 40, 50, 60, 40]
        layers = [(0, 0, None, None)] + [(k % 2, 0, 'raw8', fill(v)) for k, v in enumerate(a, 1)]
        m = R.build_material(make_root(32, 50), make_tex0(lambda n: layers, [101, 102]), 0x84)
        self.assertEqual((m['counters']['textured'], len(m['chunks'][0]['layers'])), (256, 8))
        img, _ = R.map_composite(m, self.tex.get)
        ws = mix_spec([1.0 - sum(v / 255.0 for v in a)] + [v / 255.0 for v in a])
        for y, x in ((0, 0), (3, 6), (200, 37)):
            for c in range(3):
                want = sum(w * pattern(k % 2 + 1)(x % 4, y % 4)[c] for k, w in enumerate(ws))
                self.assertAlmostEqual(img[c][y * 512 + x], want, places=9)

    def test_unresolved_repeats(self):
        """A crafted MTXP scale nibble 5 on layer 1 (repeats 0, LayerEffectUnresolved; never observed) mirrors C#
        LayerTexel and HeightAt: validation_rows and map_composite (asked for that state) draw the layer as the
        checker, never its texel 0 stretched over the chunk, and read no texture for it; a height texture the height
        mix would read on it (MHID 103, MPHD 0x80) has no defined coordinate: ValueError, but not under plain weights,
        nor with MHID entry 0 or without MPHD 0x80; the V1 dump writer (chunkdump's) refuses the chunk."""
        mtxp = [(0, 0.0, 1.0), (0x50, 4.0, 0.5)]
        m = self.tile(HEIGHT_MAP, mhid=[0, 103], mtxp=mtxp)
        ch = m['chunks'][0]
        reason = 'layer 1: MTXP texture scale nibble 5: never observed (0..4 verified)'
        self.assertEqual((ch['state'], ch['reason'], ch['layers'][1]['repeats']), ('LayerEffectUnresolved', reason, 0))
        tex = dict((k, v.decode(0)) for k, v in self.tex.items())
        a = 64 / 255.0
        p1 = pattern(1)

        def at_128(rows, k):        # layer 0 (4 x 4 at 8 repeats) and the checker, one square per 1/8 cell = 2 px
            for y, x in ((0, 0), (0, 2), (3, 1), (5, 9), (64, 70), (127, 127)):
                sq = R.CHECKER_ON if (y // 2 + x // 2) % 2 == 0 else R.CHECKER_OFF
                for c in range(3):
                    self.assertAlmostEqual(rows[y][1 + c][x], k[0] * p1(x // 4 % 4, y // 4 % 4)[c] + k[1] * sq[c], places=9)
        at_128(list(R.validation_rows(ch, tex.get, 128, mat=m, rule='plain weights')), (1 - a, a))
        img, info = R.map_composite(m, self.tex.get, rule='plain weights', states=('LayerEffectUnresolved',))
        self.assertEqual((info['composited'], info['notes']), (list(range(256)), []))
        for y, x in ((0, 0), (17, 40), (511, 511)):          # at 32 px every pixel centre lies on a magenta square
            for c in range(3):
                self.assertAlmostEqual(img[c][y * 512 + x], (1 - a) * p1(x % 4, y % 4)[c] + a * R.CHECKER_ON[c], places=9)
        want = 'layer 1 has unresolved repeats (MTXP texture scale nibble 5): its height texture 103 has no defined coordinate'
        for name, fn in (('validation_rows', lambda: list(R.validation_rows(ch, tex.get, 128, mat=m))),
                         ('map_composite', lambda: R.map_composite(m, self.tex.get, states=('LayerEffectUnresolved',)))):
            with self.subTest(height=name):
                with self.assertRaises(ValueError) as cm:
                    fn()
                self.assertEqual(str(cm.exception), want)
        # no height texture on the layer (H = 0 * 4 + 0.5), or no MPHD 0x80 (H = 1): the height mix composes
        for other, hs in ((self.tile(HEIGHT_MAP, mtxp=mtxp), [1.0, 0.5]), (self.tile(NO_HEIGHT_MAP, mhid=[0, 103], mtxp=mtxp), None)):
            with self.subTest(heights=hs):
                self.assertEqual(other['chunks'][0]['state'], 'LayerEffectUnresolved')
                at_128(list(R.validation_rows(other['chunks'][0], tex.get, 128, mat=other)), mix_spec([1 - a, a], hs))
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(ValueError) as cm:
                R.write_chunk_dump(os.path.join(d, 'n5'), ch, tex.get, 128, {}, m)
            self.assertEqual(str(cm.exception), 'MCNK 0 is LayerEffectUnresolved: %s (not drawn in the textured view; no '
                                                'V1 dump)' % reason)
            self.assertEqual(os.listdir(d), [])


# ------------------------------------------------------------------------------------------------- compare tool

def md5_file(path):
    with open(path, 'rb') as f:
        return hashlib.md5(f.read()).hexdigest()


class CompareSynthetic(unittest.TestCase):
    """A synthetic MPHD 0x80 tile with asymmetric textures, a height texture, alpha and MCCV: the reference's own
    dumps pass V1, V2 and the texel test; every planted mutation fails, and so does every unverified input."""

    TX, TY = 32, 50

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        d = cls.tmp.name
        mccv = [(100 + k % 40, 127, 140 - k % 30, 255) for k in range(145)]
        bright = [(255, 255, 255, 255)] * 145
        cls.root = make_root(cls.TX, cls.TY, flags=lambda n: 0x18000 | (0x40 if n % 3 == 0 else 0),
                             holes=lambda n: 0x8001 if n == 17 else 0,
                             mccv=lambda n: (bright if n == 18 else mccv) if n % 3 == 0 else None)
        # chunk 18: three textures at one texel per pixel at N = 64 (the production and saturation case); the others
        # carry texture 203 at 4 repeats with the 16 x 16 height texture 205 (scale 6, offset 1), N = 64 as well
        per = lambda n: ([(1, 0, None, None), (3, 0, 'rle', ramp(n)), (0, 0, 'raw8', ramp(n, 3))] if n == 18 else
                         [(n % 2, 0, None, None), (2, 0, 'rle', ramp(n)), (3, 0, 'raw8', ramp(n, 3))])
        cls.tex0 = make_tex0(per, [201, 202, 203, 204], mhid=[0, 0, 205, 0],
                             mtxp=[DEFAULT_MTXP, DEFAULT_MTXP, (0x10, 6.0, 1.0), DEFAULT_MTXP])
        cls.wdt = make_wdt(cls.TX, cls.TY, HEIGHT_MAP, (9001, 0, 0, 9004, 0, 9006, 0, 0))
        cls.blps = {201 + k: blp_bgra(8, 8, pattern(k)) for k in range(4)}
        cls.blps[205] = blp_bgra(16, 16, height_pattern(5))
        for name, data in [('root.bin', cls.root), ('tex0.bin', cls.tex0), ('wdt.bin', cls.wdt)] + \
                [('%d.bin' % k, v) for k, v in cls.blps.items()]:
            with open(os.path.join(d, name), 'wb') as f:
                f.write(data)
        cls.mat = R.build_material(cls.root, cls.tex0, HEIGHT_MAP)
        cls.blp = {k: R.Blp(v) for k, v in cls.blps.items()}
        # the "game's" map texture: the reference composite stored as an uncompressed 512 x 512 BLP
        img, info = R.map_composite(cls.mat, cls.blp.get)
        cls.img = img
        cls.cover = R.tile_coverage(cls.mat, info['composited'])
        cls.map = blp_header(3, 8, 8, 512, 512, [b''.join(bytes((int(round(img[2][i])), int(round(img[1][i])),
                                                               int(round(img[0][i])), 255)) for i in range(512 * 512))])
        with open(os.path.join(d, 'map.bin'), 'wb') as f:
            f.write(cls.map)
        cls.args = ['--root', os.path.join(d, 'root.bin'), '--tex0', os.path.join(d, 'tex0.bin'), '--wdt', os.path.join(d, 'wdt.bin'),
                    '--textures', d]
        roles = dict((k, 'height' if k == 205 else 'diffuse') for k in cls.blps)
        R.write_texel_dump(os.path.join(d, 'texels'), cls.blp, 'wow', '12.1.0.69933', roles)
        cls.meta = {'source': 'reference', 'client': 'wow', 'tile': [cls.TX, cls.TY], 'mphdFlags': HEIGHT_MAP,
                    'texelDump': 'texels.json', 'build': '12.1.0.69933',
                    'wdtMd5': md5_file(os.path.join(d, 'wdt.bin')), 'rootMd5': md5_file(os.path.join(d, 'root.bin')),
                    'tex0Md5': md5_file(os.path.join(d, 'tex0.bin'))}
        texels = lambda fd: cls.blp[fd].decode(0)
        for n in (17, 18):
            ch = cls.mat['chunks'][n]
            R.write_chunk_dump(os.path.join(d, 'c%d' % n), ch, texels, 64, dict(cls.meta, chunks=[
                {'n': n, 'state': ch['state'],
                 'layers': [{'fileDataID': l['fileDataID'], 'repeats': l['repeats']} for l in ch['layers']]}]), cls.mat)
        ch = cls.mat['chunks'][17]
        R.write_chunk_dump(os.path.join(d, 'plain17'), ch, texels, 64, cls.meta, cls.mat, rule='plain weights')
        R.write_chunk_dump(os.path.join(d, 'noheight17'), ch, texels, 64, cls.meta, None)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def path(self, name):
        return os.path.join(self.tmp.name, name)

    def run_cmp(self, *args):
        return quiet(CMP.main, ['compare_terrain_material.py'] + list(args))

    def mutate_raw(self, src, dst, fn):
        """Copy composite dump `src` to `dst` with fn(float array, sidecar) applied."""
        side = load_json(self.path(src + '.json'))
        vals = CMP.read_floats(self.path(src + '.raw'), side['width'] * side['height'] * 4)
        fn(vals, side)
        R.write_floats(self.path(dst + '.raw'), vals)
        side['raw'] = dst + '.raw'
        side['texelDump'] = 'texels.json'
        save_json(self.path(dst + '.json'), side)
        return self.path(dst + '.json')

    def test_material_has_the_cases(self):
        self.assertEqual(self.mat['chunks'][17]['holes'], 0x8001)
        self.assertEqual(R.chunk_heights(self.mat, self.mat['chunks'][17]), [None, (205, 6.0, 1.0), None])
        self.assertIsNone(R.chunk_heights(self.mat, self.mat['chunks'][18]))
        self.assertEqual(self.mat['uniqueHeightTextures'], [205])

    def test_v1_reference_passes(self):
        self.assertEqual(self.run_cmp('v1', self.path('c17.json'), self.path('c18.json'), *self.args), 0)
        side = load_json(self.path('c18.json'))
        self.assertGreater(side['clippedPixels'], 0)                  # MCCV 255 doubles chunk 18

    def test_v1_mutations_fail(self):
        def bump(vals, side):
            vals[4 * (10 * 64 + 20)] += 0.6 / 255           # one channel of one pixel by 0.6/255
        def mirror(vals, side):
            n = side['width']
            rows = [vals[4 * n * y:4 * n * (y + 1)] for y in range(n)]
            for y, row in enumerate(rows):
                for x in range(n):
                    vals[4 * (n * y + x):4 * (n * y + x) + 4] = row[4 * (n - 1 - x):4 * (n - x)]
        def hole_drawn(vals, side):
            vals[3] = 1.0                                    # pixel (0, 0) lies in hole cell (0, 0)
        def uncovered(vals, side):
            vals[4 * (40 * 64 + 40) + 3] = 0.0
        def state(vals, side):
            side['chunks'][0]['state'] = 'LayerEffectStatic'
        def layers(vals, side):
            side['chunks'][0]['layers'][1]['repeats'] = 8
        def root_md5(vals, side):
            side['rootMd5'] = '0' * 32
        for fn in (bump, mirror, hole_drawn, uncovered, state, layers, root_md5):
            with self.subTest(mutation=fn.__name__):
                self.assertEqual(self.run_cmp('v1', self.mutate_raw('c17', 'm_' + fn.__name__, fn), *self.args), 1)
        for name in ('plain17', 'noheight17'):                          # plain weights; heights left out
            with self.subTest(dump=name):
                self.assertEqual(self.run_cmp('v1', self.path(name + '.json'), *self.args), 1)

    def test_v1_saturated_production(self):
        """A production dump of the MCCV-bright chunk, clamped by the shader: compared with the clamped reference and
        its clippedPixels with the reference's count; a moved clamped pixel or a wrong count fails."""
        clipped = load_json(self.path('c18.json'))['clippedPixels']

        def clamp(vals, side):
            side['sampling'] = 'production'
            side['saturated'] = True
            for i in range(len(vals)):
                if i % 4 != 3:
                    vals[i] = min(1.0, max(0.0, vals[i]))
        self.assertEqual(self.run_cmp('v1', self.mutate_raw('c18', 's_ok', clamp), *self.args), 0)

        def moved(vals, side):
            clamp(vals, side)
            i = next(i for i in range(0, len(vals), 4) if vals[i] == 1.0)
            vals[i] -= 3.0 / 255
        def count(vals, side):
            clamp(vals, side)
            side['clippedPixels'] = 0
        def unclamped(vals, side):
            side['sampling'] = 'production'
            side['saturated'] = True
        for fn in (moved, count, unclamped):
            with self.subTest(mutation=fn.__name__):
                self.assertEqual(self.run_cmp('v1', self.mutate_raw('c18', 's_' + fn.__name__, fn), *self.args), 1)
        self.assertGreater(clipped, 0)

        def off_by_one(vals, side):
            side['clippedPixels'] -= 1                       # an unclamped dump counts its own pixels exactly
        self.assertEqual(self.run_cmp('v1', self.mutate_raw('c18', 's_off_by_one', off_by_one), *self.args), 1)

        def lying(vals, side):
            side['saturated'] = True                         # validation sampling is never clamped
        def lit(vals, side):
            side['view'] = 'texturedlit'
        def domain(vals, side):
            side['colourDomain'] = 'linear'
        for fn in (lying, lit, domain):
            with self.subTest(refused=fn.__name__):
                self.assertEqual(self.run_cmp('v1', self.mutate_raw('c18', 'r_' + fn.__name__, fn), *self.args), 2)

    def test_v1_texel_dump_roles_and_md5(self):
        side = load_json(self.path('texels.json'))
        for name, fn in (('role', lambda e: e.update(role='diffuse') if e['fileDataID'] == 205 else None),
                         ('md5', lambda e: e.update(md5='0' * 32) if e['fileDataID'] == 203 else None)):
            s = json.loads(json.dumps(side))
            for e in s['textures']:
                fn(e)
            save_json(self.path('tx_%s.json' % name), s)
            with self.subTest(mutation=name):
                self.assertEqual(self.run_cmp('v1', self.path('c17.json'), '--texels', self.path('tx_%s.json' % name), *self.args), 1)
        s = json.loads(json.dumps(side))
        s['textures'] = [e for e in s['textures'] if e['fileDataID'] != 205]
        save_json(self.path('tx_noheight.json'), s)
        self.assertEqual(self.run_cmp('v1', self.path('c17.json'), '--texels', self.path('tx_noheight.json'), *self.args), 1)

    def test_v1_alpha_transposed_fails(self):
        ch = dict(self.mat['chunks'][18])
        ch['layers'] = [dict(l, alpha=bytes(R.transform_grid(l['alpha'], 64, 4)) if l['alpha'] else None) for l in ch['layers']]
        R.write_chunk_dump(self.path('t18'), ch, lambda fd: self.blp[fd].decode(0), 64, self.meta, self.mat)
        self.assertEqual(self.run_cmp('v1', self.path('t18.json'), *self.args), 1)

    def test_v1_inputs(self):
        self.assertEqual(self.run_cmp('v1', self.path('c17.json')), 2)                       # no reference inputs
        self.assertEqual(self.run_cmp('v1', self.path('texels.json'), *self.args), 2)        # not a composite
        side = load_json(self.path('c17.json'))
        side['rowOrder'] = 'south-first'
        save_json(self.path('bad.json'), side)
        self.assertEqual(self.run_cmp('v1', self.path('bad.json'), *self.args), 2)
        side['rowOrder'] = 'north-first'
        side['width'] = side['height'] = 32                                                  # raw holds 64 x 64
        save_json(self.path('bad.json'), side)
        self.assertEqual(self.run_cmp('v1', self.path('bad.json'), *self.args), 2)

    def test_texels(self):
        self.assertEqual(self.run_cmp('texels', self.path('texels.json'), '--textures', self.tmp.name), 0)
        side = load_json(self.path('texels.json'))
        ent = side['textures'][0]
        vals = CMP.read_floats(self.path(ent['raw']), ent['width'] * ent['height'] * 4)
        vals[5] += 0.5 / 255                                  # an uncompressed upload comes back exactly
        R.write_floats(self.path('t_bad.raw'), vals)
        side['textures'][0]['raw'] = 't_bad.raw'
        save_json(self.path('t.json'), side)
        self.assertEqual(self.run_cmp('texels', self.path('t.json'), '--textures', self.tmp.name), 1)
        side['textures'][0]['raw'] = ent['raw']
        side['textures'][1]['md5'] = 'f' * 32
        save_json(self.path('t.json'), side)
        self.assertEqual(self.run_cmp('texels', self.path('t.json'), '--textures', self.tmp.name), 1)

    def test_texels_block_compressed(self):
        # endpoints exact; every four-colour interpolant and every interpolated DXT5 alpha equal to ONE named decode
        # for the whole texture (the exact ramp, or the measured hardware model); a DXT1 three-colour midpoint (no
        # measured model) between its endpoints. Colour: c0 = (255, 0, 0), c1 = (132, 130, 8), indices 0, 1, 2, 3 in
        # row 0 (exact ramp texel 2 = (214, 43.3, 2.7), texel 3 = (173, 86.7, 5.3); hardware model (214, 41, 2) and
        # (173, 89, 5)); DXT5 alpha indices 0..7 in the first 8 texels, a0 > a1 (8 values, 301) and a0 <= a1 (6 values
        # + 0 + 255, 302); 303 a DXT1 three-colour block (index 2 the mean, index 3 transparent black)
        d = self.path('bc')
        os.makedirs(d, exist_ok=True)
        c0, c1 = 31 << 11, (16 << 11) | (32 << 5) | 1
        colour = struct.pack('<HHI', c0, c1, 0b11100100)
        idx = sum(k << (3 * k) for k in range(8)).to_bytes(6, 'little')
        files = {301: blp_header(2, 8, 7, 4, 4, [bytes([200, 60]) + idx + colour]),
                 302: blp_header(2, 8, 7, 4, 4, [bytes([60, 200]) + idx + colour]),
                 303: blp_header(2, 1, 0, 4, 4, [struct.pack('<HHI', 1 << 11, 31 << 11, 0b11100100)])}
        for fd, data in files.items():
            with open(os.path.join(d, '%d.bin' % fd), 'wb') as f:
                f.write(data)
        hw = R.Blp(files[301], model='hardware').decode(0)[2]
        self.assertEqual((hw[8:11], hw[12:15]), ([214.0, 41.0, 2.0], [173.0, 89.0, 5.0]))
        self.assertEqual([hw[4 * k + 3] for k in range(2, 8)], [180.0, 160.0, 139.0, 121.0, 100.0, 80.0])   # ramp 140, 120
        # the texture dumped by any decode passes, named as such: exact and hardware with the block format, rounded
        # (the player's CPU decode) with the upload format R8G8B8A8_UNorm, its DXT1 midpoint included
        for model in R.DECODE_MODELS:
            R.write_texel_dump(os.path.join(d, 'bc_' + model), dict((fd, R.Blp(v, model=model)) for fd, v in files.items()),
                               'wow', '12.1.0.69933')
            with self.subTest(model=model):
                self.assertEqual(self.run_cmp('texels', os.path.join(d, 'bc_%s.json' % model), '--textures', d,
                                              '--json', os.path.join(d, 'r.json')), 0)
                names = dict((r['fileDataID'], (r.get('colour_model'), r.get('alpha_model')))
                             for r in load_json(os.path.join(d, 'r.json'))['results'])
                want = {'exact': 'exact ramp', 'hardware': 'hardware model', 'rounded': 'rounded'}[model]
                self.assertEqual(names[301], (want, want))
                self.assertEqual(names[302][1], want)
                self.assertEqual(names[303], ('rounded' if model == 'rounded' else 'none interpolated', None))
        self.assertEqual(set(e['format'] for e in load_json(os.path.join(d, 'bc_rounded.json'))['textures']),
                         {R.DECODED_UPLOAD})
        # the fallback entry tagged with its block format, or a block decode tagged as the fallback: FAIL
        for model, fmt in (('rounded', 'DXT5'), ('rounded', 'RGBA_DXT5_UNorm'), ('hardware', R.DECODED_UPLOAD),
                           ('exact', R.DECODED_UPLOAD)):
            side = load_json(os.path.join(d, 'bc_%s.json' % model))
            save_json(os.path.join(d, 'f.json'), dict(side, textures=[dict(e, format=fmt) if e['fileDataID'] == 301 else e
                                                                      for e in side['textures']]))
            with self.subTest(dump=model, tagged=fmt):
                self.assertEqual(self.run_cmp('texels', os.path.join(d, 'f.json'), '--textures', d), 1)
        # (texture, decode the dump starts from, {flat index: delta /255 or ('swap', i, j)}, exit code); flat index =
        # 4 * texel + channel
        swap = ('swap', 2, 3)                            # colour texels 2 and 3 exchanged: selectors 2 <-> 3
        cases = [(301, 'exact', {9: 5.0, 10: -2.0}, 1),   # texel 2 G, B moved inside the endpoint hull: neither decode
                 (301, 'exact', {9: 90.0}, 1),
                 (301, 'exact', {0: -0.5}, 1),             # texel 0 R: endpoint c0
                 (301, 'exact', {5: 0.5}, 1),              # texel 1 G: endpoint c1
                 (301, 'exact', swap, 1),                  # swapped interpolants
                 (301, 'hardware', swap, 1),
                 (301, 'hardware', {9: 43.0 + 1 / 3.0 - 41.0, 10: 2.0 + 2 / 3.0 - 2.0}, 1),   # texel 2 exact, 3 hardware: mixed
                 (301, 'exact', {11: 6.0, 19: -6.0}, 1),   # interpolated alphas moved: neither decode
                 (301, 'hardware', {11: 1.0}, 1),
                 (301, 'exact', {3: 0.5}, 1),              # alpha texel 0 = a0
                 (302, 'exact', {11: 5.0}, 1),             # 6-value mode: alpha texel 2 interpolated
                 (302, 'exact', {27: 0.5}, 1),             # alpha texel 6 = 0
                 (302, 'exact', {31: -0.5}, 1),            # alpha texel 7 = 255
                 (303, 'exact', {8: 3.0}, 0),              # three-colour mean (unmodelled): inside [8, 255]
                 (303, 'exact', {9: -2.0}, 1),             # G below its endpoints' 0 - 1
                 (303, 'exact', {12: 0.5}, 1),             # index 3: transparent black
                 (303, 'exact', {15: 0.5}, 1),
                 (301, 'rounded', {9: 1.0}, 1),            # the fallback: texel 2 G 43 -> 44, off the rounded decode
                 (301, 'rounded', {11: -1.0}, 1),          # interpolated alpha 180 -> 179
                 (303, 'rounded', {8: -1.0}, 1),           # midpoint 131.5 rounded to 131 (not to even): FAIL
                 (303, 'rounded', {13: 0.5}, 1)]           # index 3: transparent black
        for fd, model, deltas, want in cases:
            side = load_json(os.path.join(d, 'bc_%s.json' % model))
            k = [e['fileDataID'] for e in side['textures']].index(fd)
            ent = dict(side['textures'][k])
            vals = CMP.read_floats(os.path.join(d, ent['raw']), 64)
            if deltas == swap:
                vals[8:11], vals[12:15] = vals[12:15], vals[8:11]
            else:
                for i, dv in deltas.items():
                    vals[i] += dv / 255.0
            R.write_floats(os.path.join(d, 'm.raw'), vals)
            mut = dict(side, textures=[dict(e, raw='m.raw') if e['fileDataID'] == fd else e for e in side['textures']])
            save_json(os.path.join(d, 'm.json'), mut)
            with self.subTest(texture=fd, model=model, deltas=deltas):
                self.assertEqual(self.run_cmp('texels', os.path.join(d, 'm.json'), '--textures', d), want)

    def v2_args(self, threshold='0.5'):
        return self.args + ['--map-texture', self.path('map.bin'), '--textures', self.tmp.name, '--threshold', threshold,
                            '--reference-tolerance', '0.01']

    def v2_dump(self, name, img, folder=None, **extra):
        meta = dict(self.meta, kind='tile', pixelsPerChunk=32, sampling='production', saturated=True, view='textured')
        meta.pop('texelDump')
        meta.update(extra)
        base = os.path.join(folder or self.tmp.name, name)
        R.write_composite_dump(base, img, 512, meta, self.cover)
        return base + '.json'

    def test_v2_reference_passes(self):
        self.assertEqual(self.run_cmp('v2', self.v2_dump('v2', self.img), *self.v2_args()), 0)
        self.assertEqual(self.run_cmp('v2', self.path('v2.png'), *self.v2_args()[:-2] + ['--tile', '32,50']), 0)

    def test_v2_mutations_fail(self):
        self.assertEqual(self.run_cmp('v2', self.v2_dump('v2m', R.transform_image(self.img, 512, 1)), *self.v2_args('100')), 1)
        self.assertEqual(self.run_cmp('v2', self.v2_dump('v2s', R.shift_image(self.img, 512, 1, 0)), *self.v2_args('100')), 1)
        dark = tuple([v * 0.97 for v in c] for c in self.img)
        self.assertEqual(self.run_cmp('v2', self.v2_dump('v2d', dark), *self.v2_args()), 1)
        plain, _ = R.map_composite(self.mat, self.blp.get, rule='plain weights')
        self.assertEqual(self.run_cmp('v2', self.v2_dump('v2p', plain), *self.v2_args()), 1)
        flat, _ = R.map_composite(self.mat, self.blp.get, heights='default')
        self.assertEqual(self.run_cmp('v2', self.v2_dump('v2h', flat), *self.v2_args()), 1)
        self.assertEqual(self.run_cmp('v2', self.v2_dump('v2t', self.img), *self.args + ['--map-texture', self.path('map.bin'),
                                                                                          '--textures', self.tmp.name]), 2)
        over = self.v2_dump('v2o', self.img)                         # saturated, yet a covered value above 1
        vals = CMP.read_floats(self.path('v2o.raw'), 512 * 512 * 4)
        vals[4 * (100 * 512 + 100)] = 1.2
        R.write_floats(self.path('v2o.raw'), vals)
        self.assertEqual(self.run_cmp('v2', over, *self.v2_args()), 1)
        self.assertEqual(self.run_cmp('v2', self.v2_dump('v2l', self.img, view='materials'), *self.v2_args()), 2)
        self.assertEqual(self.run_cmp('v2', self.v2_dump('v2x', self.img, tex0Md5='1' * 32), *self.v2_args()), 1)

    def test_v2_per_pixel_gate(self):
        """The dump against the reference composite is a check by default (0.26/255: V2's own budget of about
        0.13/255 -- the float16 store's step below 0.1245/255 plus the pre-store difference -- doubled; under the
        better-fitting named decode, named in the result); --reference-tolerance only changes the limit. One kept
        pixel 3/255 off, or 0.425/255 off (what a player dividing MCCV by 127.25 instead of 127 measures on a real
        tile), passes the block, symmetry and phase checks and fails this one alone; 0.13/255 off (the budget: what a
        correct player's float16 dump may show) passes."""
        args = self.args + ['--map-texture', self.path('map.bin'), '--threshold', '0.5']
        out = self.path('gate.json')
        i = 200 * 512 + 300                                  # chunk 105, no hole
        for delta, want in ((3.0, 1), (0.425, 1), (0.13, 0)):
            img = tuple(list(c) for c in self.img)
            img[0][i] += delta if img[0][i] <= 250.0 else -delta
            with self.subTest(delta=delta):
                self.assertEqual(self.run_cmp('v2', self.v2_dump('v2g', img), *args + ['--json', out]), want)
                failed = [c['name'] for c in load_json(out)['checks'] if c['result'] == 'FAIL']
                self.assertEqual(len(failed), want, failed)
                if want:
                    self.assertIn('every kept pixel within 0.26/255 (V2_REFERENCE_TOLERANCE: float dump) of the reference '
                                  'composite', failed[0])
                    self.assertEqual(self.run_cmp('v2', self.v2_dump('v2g', img), *args + ['--reference-tolerance', '4']), 0)
        self.assertEqual(self.run_cmp('v2', self.v2_dump('v2r', self.img), *args + ['--json', out]), 0)
        res = load_json(out)['results']
        self.assertEqual((res['reference_model'], res['reference_tolerance'], res['reference_limit']),
                         ('exact', 0.26, 'V2_REFERENCE_TOLERANCE: float dump'))
        self.assertEqual((CMP.V2_REFERENCE_TOLERANCE, CMP.V2_PNG_REFERENCE_TOLERANCE), (0.26, 0.26 + 0.5))
        self.assertLess(res['reference_max'], 0.001)                 # float32 storage of the same values

    def test_v2_png_input(self):
        """The 8-bit PNG input is the float composite rounded to 8 bits: a float16-like dump (the reference composite
        through a truncating float16 store: non-integer values) passes as a float dump below one float16 step, and
        its PNG, more than 0.5/255 off the reference somewhere (the rounding on top of the store), passes within
        V2_PNG_REFERENCE_TOLERANCE, named in the check; held to the float dump's limit it fails."""
        img16 = tuple([float16_truncated(v / 255.0) * 255.0 for v in c] for c in self.img)
        self.assertTrue(any(v != int(v) for v in img16[0]))
        dump = self.v2_dump('v2f16', img16)                  # .raw, .json and the 8-bit .png
        args = self.args + ['--map-texture', self.path('map.bin'), '--threshold', '0.5']
        out = self.path('png.json')
        self.assertEqual(self.run_cmp('v2', dump, *args + ['--json', out]), 0)
        res = load_json(out)['results']
        self.assertEqual((res['reference_tolerance'], res['reference_limit']), (0.26, 'V2_REFERENCE_TOLERANCE: float dump'))
        self.assertGreater(res['reference_max'], 0.0)
        self.assertLess(res['reference_max'], 2 ** -11 * 255)
        png = self.path('v2f16.png')
        self.assertEqual(self.run_cmp('v2', png, *args + ['--sidecar', dump, '--json', out]), 0)
        res = load_json(out)['results']
        gate = [c for c in load_json(out)['checks'] if 'of the reference composite' in c['name']]
        self.assertEqual([c['result'] for c in gate], ['PASS'])
        self.assertIn('every kept pixel within 0.76/255 (V2_PNG_REFERENCE_TOLERANCE: 8-bit PNG input, rounding '
                      'included) of the reference composite', gate[0]['name'])
        self.assertEqual(res['reference_tolerance'], CMP.V2_PNG_REFERENCE_TOLERANCE)
        self.assertGreater(res['reference_max'], 0.5)
        self.assertLessEqual(res['reference_max'], 0.5 + 2 ** -11 * 255)
        self.assertEqual(self.run_cmp('v2', png, *args + ['--sidecar', dump, '--reference-tolerance',
                                                          repr(CMP.V2_REFERENCE_TOLERANCE)]), 1)


def dxt5_blp(w, h, seed):
    """A DXT5 BLP with a full mip chain of pseudo-random (deterministic) blocks: every level has interpolated colours
    and alphas, so the exact and the hardware decodes differ."""
    levels = []
    lw, lh = w, h
    while True:
        n = ((lw + 3) // 4) * ((lh + 3) // 4)
        levels.append(b''.join(hashlib.sha256(b'%d %d %d' % (seed, lw, k)).digest()[:16] for k in range(n)))
        if lw == 1 and lh == 1:
            break
        lw, lh = max(1, lw // 2), max(1, lh // 2)
    return blp_header(2, 8, 7, w, h, levels)


class CompareDecodeModels(unittest.TestCase):
    """V2 names the block decode a dump agrees with: on a tile of block-compressed textures the reference composed
    under the exact decode, the hardware model and the rounded decode differ, and a dump made with any of them passes
    at 0.01/255 naming that decode, the others reported off."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        d = cls.tmp.name
        root = make_root(32, 50)
        tex0 = make_tex0(lambda n: [(0, 0, None, None), (1, 0, 'rle', ramp(n))], [401, 402])
        wdt = make_wdt(32, 50, 0x84, (9001, 0, 0, 9004, 0, 9006, 0, 0))
        blps = {401: dxt5_blp(8, 8, 1), 402: dxt5_blp(8, 8, 2)}
        mat = R.build_material(root, tex0, 0x84)
        cls.img = dict((m, R.map_composite(mat, lambda fd, m=m: R.Blp(blps[fd], model=m))[0]) for m in R.DECODE_MODELS)
        hw = cls.img['hardware']
        mp = blp_header(3, 8, 8, 512, 512, [b''.join(bytes((int(round(hw[2][i])), int(round(hw[1][i])),
                                                             int(round(hw[0][i])), 255)) for i in range(512 * 512))])
        for name, data in [('root.bin', root), ('tex0.bin', tex0), ('wdt.bin', wdt), ('map.bin', mp)] + \
                [('%d.bin' % k, v) for k, v in blps.items()]:
            with open(os.path.join(d, name), 'wb') as f:
                f.write(data)
        cls.cover = R.tile_coverage(mat, range(256))
        cls.meta = {'source': 'reference', 'client': 'wow', 'tile': [32, 50], 'mphdFlags': 0x84, 'build': '12.1.0.69933',
                    'wdtMd5': R.md5_hex(wdt), 'rootMd5': R.md5_hex(root), 'tex0Md5': R.md5_hex(tex0), 'kind': 'tile',
                    'pixelsPerChunk': 32, 'sampling': 'production', 'saturated': True, 'view': 'textured'}
        cls.args = ['--root', os.path.join(d, 'root.bin'), '--tex0', os.path.join(d, 'tex0.bin'), '--wdt',
                    os.path.join(d, 'wdt.bin'), '--textures', d, '--map-texture', os.path.join(d, 'map.bin'),
                    '--threshold', '10', '--reference-tolerance', '0.01']

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_the_named_decode(self):
        self.assertGreater(max(abs(a - b) for c in range(3) for a, b in zip(self.img['exact'][c], self.img['hardware'][c])), 1.0)
        for model in R.DECODE_MODELS:
            base = os.path.join(self.tmp.name, 'dump_' + model)
            R.write_composite_dump(base, self.img[model], 512, self.meta, self.cover, png=False)
            out = base + '_out.json'
            with self.subTest(model=model):
                self.assertEqual(quiet(CMP.main, ['compare_terrain_material.py', 'v2', base + '.json'] + self.args +
                                       ['--json', out]), 0)
                res = load_json(out)['results']
                self.assertEqual(res['reference_model'], model)
                self.assertLess(res['reference_models'][model]['max'], 0.01)
                for other in R.DECODE_MODELS:          # the rounded decode is at most 0.5/255 off the exact one
                    if other != model:
                        self.assertGreater(res['reference_models'][other]['max'],
                                           0.1 if 'hardware' not in (model, other) else 1.0, other)


class CompareRolesAndRepeats(unittest.TestCase):
    """An MPHD 0x80 tile where one id is a diffuse texture at 1 repeat and the next layer's height texture at 8
    (chunk 0), and where a DXT1 texture without an alpha channel is a height texture (chunk 1) and, elsewhere, a
    plain diffuse texture (chunk 2): texel dump roles, the design N and the production check per (id, repeats) pair,
    and the height role of an alpha-less texture failing alone."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        d = cls.tmp.name
        cls.root = make_root(32, 50)
        per = lambda n: ([(0, 0, None, None), (1, 0, 'raw8', ramp(n))] if n == 0 else
                         [(2, 0, None, None), (3, 0, 'raw8', ramp(n))] if n == 1 else
                         [(2, 0, None, None)] if n == 2 else [])
        cls.tex0 = make_tex0(per, [501, 502, 503, 504], mhid=[0, 501, 0, 503],
                             mtxp=[(0x30, 0.0, 1.0), (0, 4.0, 0.5), DEFAULT_MTXP, (0, 2.0, 1.0)])
        cls.wdt = make_wdt(32, 50, HEIGHT_MAP, (9001, 0, 0, 9004, 0, 0, 0, 0))
        dxt1 = struct.pack('<HHI', 31 << 11, 31, 0x1B1B1B1B)       # a four-colour block, no alpha channel
        cls.blps = {501: blp_bgra(8, 8, height_pattern(1)), 502: blp_bgra(8, 8, pattern(2)),
                    503: blp_header(2, 0, 0, 8, 8, [dxt1 * 4, dxt1, dxt1, dxt1]), 504: blp_bgra(8, 8, pattern(4))}
        for name, data in [('root.bin', cls.root), ('tex0.bin', cls.tex0), ('wdt.bin', cls.wdt)] + \
                [('%d.bin' % k, v) for k, v in cls.blps.items()]:
            with open(os.path.join(d, name), 'wb') as f:
                f.write(data)
        cls.blp = dict((k, R.Blp(v)) for k, v in cls.blps.items())
        cls.args = ['--root', os.path.join(d, 'root.bin'), '--tex0', os.path.join(d, 'tex0.bin'), '--wdt',
                    os.path.join(d, 'wdt.bin'), '--textures', d]
        cls.meta = {'source': 'reference', 'client': 'wow', 'tile': [32, 50], 'mphdFlags': HEIGHT_MAP,
                    'build': '12.1.0.69933', 'wdtMd5': R.md5_hex(cls.wdt), 'rootMd5': R.md5_hex(cls.root),
                    'tex0Md5': R.md5_hex(cls.tex0)}

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def path(self, name):
        return os.path.join(self.tmp.name, name)

    def dump(self, name, mat, n, roles, **extra):
        """The reference's own V1 dump of chunk n and its texel dump (roles as given), like chunkdump writes them."""
        ch = mat['chunks'][n]
        need = R.needed_textures(mat, ch)
        blps = dict((fd, self.blp[fd]) for fd, _ in need)
        size, why = R.validation_size(ch, lambda fd: (blps[fd].width, blps[fd].height), mat)
        self.assertIsNone(why)
        R.write_texel_dump(self.path(name + '_texels'), blps, 'wow', '12.1.0.69933', roles(need))
        meta = dict(self.meta, texelDump=name + '_texels.json', **extra)
        R.write_chunk_dump(self.path(name), ch, lambda fd: blps[fd].decode(0), size, meta, mat)
        if 'sampling' in extra:                     # write_chunk_dump records validation sampling
            side = load_json(self.path(name + '.json'))
            side.update(extra)
            save_json(self.path(name + '.json'), side)
        return self.path(name + '.json'), size

    def run_v1(self, path):
        out = path[:-5] + '_out.json'
        code = quiet(CMP.main, ['compare_terrain_material.py', 'v1', path] + self.args + ['--json', out])
        return code, [c['name'] for c in load_json(out)['checks'] if c['result'] == 'FAIL'] if code != 2 else None

    def test_both_roles(self):
        """501 is chunk 0's diffuse texture and its next layer's height texture: chunkdump's roles keep both, the texel
        dump lists it once per role (one raw), and V1 passes; one role per id (the old collapsing) fails V1."""
        mat = R.build_material(self.root, self.tex0, HEIGHT_MAP)
        need = R.needed_textures(mat, mat['chunks'][0])
        self.assertEqual(need, [(501, 'diffuse'), (502, 'diffuse'), (501, 'height')])
        self.assertEqual(R.texture_roles(need), {501: ['diffuse', 'height'], 502: ['diffuse']})
        path, size = self.dump('both', mat, 0, R.texture_roles)
        self.assertEqual(size, 64)
        side = load_json(self.path('both_texels.json'))
        self.assertEqual([(e['fileDataID'], e['role']) for e in side['textures']],
                         [(501, 'diffuse'), (501, 'height'), (502, 'diffuse')])
        self.assertEqual(len(set(e['raw'] for e in side['textures'] if e['fileDataID'] == 501)), 1)
        self.assertEqual(self.run_v1(path), (0, []))
        path, _ = self.dump('collapsed', mat, 0, lambda need: dict((fd, r) for fd, r in reversed(need)))
        code, failed = self.run_v1(path)
        self.assertEqual(code, 1)
        self.assertEqual([f for f in failed if 'roles' in f], [f for f in failed])

    def test_production_check_per_layer_repeats(self):
        """Chunk 0 binds 501 at 1 repeat (diffuse) and at 8 (height): N = 64, and a production dump fails the
        one-texel-per-pixel check for the 1-repeat binding (keyed by id, the 8 overwrote the 1 and it passed)."""
        mat = R.build_material(self.root, self.tex0, HEIGHT_MAP)
        self.assertEqual(R.bound_pairs(mat, mat['chunks'][0]), [(501, 1), (502, 8), (501, 8)])
        path, size = self.dump('prod', mat, 0, R.texture_roles, sampling='production')
        code, failed = self.run_v1(path)
        self.assertEqual(code, 1)
        self.assertEqual(len(failed), 1, failed)
        self.assertIn('production sampling with every texture at one texel per pixel', failed[0])

    def test_validation_size_per_layer(self):
        """One height texture shared by a layer at 8 repeats and one at 1: N = 512 x 8 in either layer order."""
        sizes = {601: (64, 64), 602: (64, 64), 699: (512, 512)}
        for order in ((0, 1), (1, 0)):
            tex0 = make_tex0(lambda n: [(order[0], 0, None, None), (order[1], 0, 'raw8', fill(100))], [601, 602],
                             mhid=[699, 699], mtxp=[(0, 2.0, 1.0), (0x30, 2.0, 1.0)])
            m = R.build_material(self.root, tex0, HEIGHT_MAP)
            with self.subTest(order=order):
                self.assertEqual(R.validation_size(m['chunks'][0], sizes.get, m), (4096, None))
                self.assertEqual(R.validation_size(m['chunks'][0], sizes.get), (512, None))     # diffuse only

    def test_height_texture_without_alpha(self):
        """503 (DXT1, BLP alpha depth 0) as a height texture fails for the height role only: chunk 1 is
        HeightTextureMissing, chunk 2 (503 as its diffuse texture) is drawn; height_values reads no alpha from it;
        a V1 dump of chunk 1 drawn with the alpha anyway fails as not drawable."""
        missing, height_missing = {}, {}
        mat = R.resolve_material(self.root, self.tex0, HEIGHT_MAP, self.blp.get, missing, height_missing)
        self.assertEqual((missing, height_missing), ({}, {503: R.HEIGHT_NO_ALPHA}))
        self.assertEqual([mat['chunks'][n]['state'] for n in range(3)], ['Textured', 'HeightTextureMissing', 'Textured'])
        self.assertEqual(mat['chunks'][1]['reason'], 'layer 1: height texture 503 ' + R.HEIGHT_NO_ALPHA)
        self.assertIn(503, mat['uniqueTextures'])
        v, note = R.height_values((503, 2.0, 1.0), self.blp.get, 8, 32)
        self.assertEqual(v, 1.0)
        self.assertIn(R.HEIGHT_NO_ALPHA, note)
        raw = R.build_material(self.root, self.tex0, HEIGHT_MAP)      # as if the alpha could be read
        self.assertEqual(raw['chunks'][1]['state'], 'Textured')
        path, _ = self.dump('noalpha', raw, 1, R.texture_roles)
        code, failed = self.run_v1(path)
        self.assertEqual(code, 1)
        self.assertTrue(any('MCNK 1 is drawable' in f for f in failed), failed)


class CompareNibble4(unittest.TestCase):
    """V1 on a nibble-4 layer (0.5 repeats, a 64 x 64 texture: N = 32, as 2048 x 0.5 = 1024 on map 2923) in chunk 17
    (ix 1, iy 1, phase (0.5, 0.5)): the reference's own dump passes in validation and in production sampling (one
    texel per pixel at the float repeats); the same chunk drawn with chunk-local UV, or a sidecar naming 1 repeat,
    fails."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        d = cls.tmp.name
        root = make_root(32, 50)
        tex0 = make_tex0(lambda n: [(0, 0, None, None), (1, 0, 'raw8', ramp(n))], [701, 702],
                         mtxp=[DEFAULT_MTXP, (0x40, 0.0, 1.0)])
        wdt = make_wdt(32, 50, 0x84, (9001, 0, 0, 9004, 0, 0, 0, 0))
        blps = {701: blp_bgra(4, 4, pattern(1)), 702: blp_bgra(64, 64, pattern(2))}
        for name, data in [('root.bin', root), ('tex0.bin', tex0), ('wdt.bin', wdt)] + \
                [('%d.bin' % k, v) for k, v in blps.items()]:
            with open(os.path.join(d, name), 'wb') as f:
                f.write(data)
        cls.blp = dict((k, R.Blp(v)) for k, v in blps.items())
        cls.mat = R.build_material(root, tex0, 0x84)
        cls.args = ['--root', os.path.join(d, 'root.bin'), '--tex0', os.path.join(d, 'tex0.bin'), '--wdt',
                    os.path.join(d, 'wdt.bin'), '--textures', d]
        R.write_texel_dump(os.path.join(d, 'texels'), cls.blp, 'wow', '12.1.0.69933')
        cls.meta = {'source': 'reference', 'client': 'wow', 'tile': [32, 50], 'mphdFlags': 0x84, 'build': '12.1.0.69933',
                    'wdtMd5': R.md5_hex(wdt), 'rootMd5': R.md5_hex(root), 'tex0Md5': R.md5_hex(tex0),
                    'texelDump': 'texels.json'}

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def dump(self, name, n=17, drawn_as=None, **extra):
        ch = self.mat['chunks'][n]
        side = dict(self.meta, chunks=[{'n': n, 'state': ch['state'],
                                        'layers': [{'fileDataID': l['fileDataID'], 'repeats': l['repeats']}
                                                   for l in ch['layers']]}])
        base = os.path.join(self.tmp.name, name)
        R.write_chunk_dump(base, dict(ch, n=n if drawn_as is None else drawn_as), lambda fd: self.blp[fd].decode(0), 32,
                           side, self.mat)
        s = load_json(base + '.json')
        s['chunk'] = n
        s.update(extra)
        if 'repeats' in extra:
            s['chunks'][0]['layers'][1]['repeats'] = s.pop('repeats')
        save_json(base + '.json', s)
        return base + '.json'

    def run_v1(self, path):
        return quiet(CMP.main, ['compare_terrain_material.py', 'v1', path] + self.args)

    def test_nibble4_chunk(self):
        self.assertEqual([l['repeats'] for l in self.mat['chunks'][17]['layers']], [8, 0.5])
        self.assertEqual(R.validation_size(self.mat['chunks'][17], lambda fd: (self.blp[fd].width, self.blp[fd].height),
                                           self.mat), (32, None))
        self.assertEqual(self.run_v1(self.dump('n17')), 0)
        self.assertEqual(self.run_v1(self.dump('n17p', sampling='production')), 0)
        self.assertEqual(self.run_v1(self.dump('n17local', drawn_as=0)), 1)        # chunk 0's phase: chunk-local UV
        self.assertEqual(self.run_v1(self.dump('n17r1', repeats=1)), 1)


class AuditDataCopies(unittest.TestCase):
    """AuditData.read takes the first copy, in folder order, that verifies: an unlisted copy in an earlier folder does
    not hide a listed one, a manifest's id column may be named "fdid", and with no verified copy every copy is named."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.build = R.load_hashes()['client_builds']['wow']
        self.data = b'BLP2' + bytes(range(60))

    def put(self, folder, data, header=None, digest=None):
        d = os.path.join(self.tmp.name, 'materials', 'wow', folder)
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, '123.bin'), 'wb') as f:
            f.write(data)
        if header:
            with open(os.path.join(d, 'manifest_sha256.tsv'), 'w', newline='\n') as f:
                f.write('\t'.join(header) + '\n')
                f.write('\t'.join(['123', str(len(data)), digest or hashlib.sha256(data).hexdigest(), self.build]) + '\n')

    def test_first_verified_copy(self):
        self.put('a_unlisted', b'BLP2 truncated')
        self.put('b_fdid', self.data, ('fdid', 'bytes', 'sha256', 'build'))
        data = R.AuditData(self.tmp.name)
        self.assertTrue(data.find('wow', 123).replace(os.sep, '/').endswith('a_unlisted/123.bin'))
        got, how = data.read_how('wow', 123)
        self.assertEqual(got, self.data)
        self.assertIn('b_fdid', how)

    def test_no_verified_copy(self):
        self.put('a_unlisted', b'BLP2 truncated')
        self.put('b_id', self.data, ('id', 'bytes', 'sha256', 'build', 'status'), digest='0' * 64)
        with self.assertRaises(ValueError) as e:
            R.AuditData(self.tmp.name).read('wow', 123)
        self.assertIn('a_unlisted', str(e.exception))
        self.assertIn('b_id', str(e.exception))
        self.assertEqual(R.AuditData(self.tmp.name).read('wow', 123, verify=False), b'BLP2 truncated')


class CompareVerification(unittest.TestCase):
    """--corpus verifies every input against its manifest row and the pinned client build, the map texture included:
    a synthetic audit-data folder with a manifest_sha256.tsv passes, and a map texture outside it, a row of another
    build, changed bytes or a sidecar of another build are refused or failed."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.build = R.load_hashes()['client_builds']['wow']
        src = CompareSynthetic
        if not hasattr(src, 'mat'):
            src.setUpClass()
            self.addCleanup(src.tearDownClass)
        self.src = src
        self.folder = os.path.join(self.tmp.name, 'materials', 'wow', 'stage04_reference')
        os.makedirs(self.folder)
        self.files = {9000: src.wdt, 9001: src.root, 9004: src.tex0, 9006: src.map}
        self.files.update(src.blps)
        for fd, data in self.files.items():
            with open(os.path.join(self.folder, '%d.bin' % fd), 'wb') as f:
                f.write(data)
        self.write_manifest()

    def tearDown(self):
        self.tmp.cleanup()

    def write_manifest(self, build=None, skip=(), folder=None):
        folder = folder or self.folder
        with open(os.path.join(folder, 'manifest_sha256.tsv'), 'w', newline='\n') as f:
            f.write('id\tbytes\tsha256\tbuild\tstatus\n')
            for fd in sorted(self.files):
                if fd in skip or not os.path.exists(os.path.join(folder, '%d.bin' % fd)):
                    continue
                data = self.files[fd]
                f.write('%d\t%d\t%s\t%s\tOK\n' % (fd, len(data), hashlib.sha256(data).hexdigest(), build or self.build))

    def dump(self, **extra):
        img = self.src.img
        meta = dict(self.src.meta, kind='tile', pixelsPerChunk=32, sampling='production', saturated=True,
                    view='textured', wdt='materials/wow/stage04_reference/9000.bin', rootFileDataID=9001,
                    tex0FileDataID=9004, mapTextureFileDataID=9006)
        meta.pop('texelDump')
        meta.update(extra)
        base = os.path.join(self.tmp.name, 'dump')
        R.write_composite_dump(base, img, 512, meta, self.src.cover, png=False)
        return base + '.json'

    def run_v2(self, dump):
        return quiet(CMP.main, ['compare_terrain_material.py', 'v2', dump, '--corpus', self.tmp.name, '--threshold', '0.5'])

    def test_verified_inputs_pass(self):
        self.assertEqual(self.run_v2(self.dump()), 0)

    def test_sidecar_of_another_build_fails(self):
        self.assertEqual(self.run_v2(self.dump(build='12.0.0.1')), 1)

    def test_map_texture_outside_the_manifest_is_refused(self):
        other = os.path.join(self.tmp.name, 'materials', 'wow', 'zz_other')
        os.makedirs(other)
        shutil.move(os.path.join(self.folder, '9006.bin'), os.path.join(other, '9006.bin'))
        self.write_manifest()
        self.assertEqual(self.run_v2(self.dump()), 2)

    def test_map_texture_of_another_build_or_changed_is_refused(self):
        self.write_manifest(build='12.0.0.1')
        self.assertEqual(self.run_v2(self.dump()), 2)
        self.write_manifest()
        with open(os.path.join(self.folder, '9006.bin'), 'r+b') as f:
            f.seek(-1, 2)
            f.write(b'\x01')
        self.assertEqual(self.run_v2(self.dump()), 2)


# ------------------------------------------------------------------------------------------------- corpus

# (client, tile, root, tex0, MCNK, expected state, [decoded-alpha SHA-256 (first 12) per layer after the first])
# The hashes are the census's (test cases 2, 4, 6, 7, 9, 13, 15, 17).
PINNED_CHUNKS = [
    ('wow', (32, 50), 778037, 778040, 87, 'Textured', ['2b3d72e4dbad', 'b78f6d4ba277', '0dd9dbfdb042']),
    ('wow', (32, 50), 778037, 778040, 246, 'Textured', ['7a5d7a080e8f']),
    ('wow', (34, 46), 778417, 778420, 48, 'Textured', ['a03dae85e4a1', '5c4c5fe69078', '2f8ffb156f27']),   # lava 186947, flags 0x100
    ('wow', (34, 46), 778417, 778420, 146, 'Textured', ['4dfda64661c4', 'b1103a5971f0']),
    ('wow', (34, 45), 778412, 778415, 3, 'LayerEffectStatic', ['8cd3b1ca7d38', '3ea74366c285', 'cb623dbfc30b']),
    ('wow', (30, 49), 777632, 777635, 52, 'Textured', ['4043a9d22dfd', 'c685beb2f56e', 'be3961467080']),    # _h 971415
    ('wow', (30, 49), 777632, 777635, 68, 'Textured', ['b5e3443d1994', '126ce3d3049a', '6425d5ec11ea']),
    ('wow_classic_beta', (31, 47), 777822, 777825, 48, 'Textured', ['9af2b6ce29cd', 'a6c978e12167', 'ceeb0c12f204', '9a1bdbb4e524']),
    ('wow_classic_era', (32, 50), 778037, 778040, 51, 'AlphaUnsupported', ['14b3f3d32f24', 'bf8786e54409', '6f03617f19f5']),
]
# (client, WDT, tile, root, tex0, counters that are not zero, MCCV chunks applied, MCSH chunks, unique drawable
# textures, unique height textures)
PINNED_TILES = [
    ('wow', 775971, (32, 50), 778037, 778040, {'textured': 256}, 0, 0, 10, []),
    ('wow', 775971, (34, 46), 778417, 778420, {'textured': 245, 'layerEffectStatic': 11}, 152, 0, 9, []),
    ('wow', 775971, (32, 48), 778027, 778030, {'textured': 256}, 57, 0, 10, []),
    ('wow', 775971, (30, 49), 777632, 777635, {'textured': 256}, 36, 0, 11, [971415, 878974]),
    ('wow_classic_beta', 775971, (32, 48), 778027, 778030, {'textured': 256}, 256, 0, 7, []),
    ('wow_classic_era', 775971, (32, 50), 778037, 778040, {'alphaUnsupported': 256}, 0, 256, 0, []),
    ('wow_classic_era', 775971, (32, 48), 778027, 778030, {'alphaUnsupported': 256}, 0, 210, 0, []),
    # every chunk has MCCV and flag 0x40 but no layer: no vertex colours on neutral chunks
    ('wow', 4914790, (41, 27), 4920553, 4920556, {'noLayers': 256}, 0, 0, 0, []),
    # MPHD 0x80 clear: MHID and MTXP height values on every chunk, none requested, all drawn
    ('wow', 1923857, (27, 19), 1923654, 1923657, {'textured': 256}, 223, 0, 10, []),
    # Retail map 2923: MTXP scale nibble 4 and chunks of 6..8 layers, all drawn (materials/wow/stage04_nibble4)
    ('wow', 6838755, (32, 24), 7473884, 7473887, {'textured': 256}, 256, 0, 16,
     [6717883, 6049364, 5928542, 6156541, 6021895, 5900004, 5876853, 5899999, 5900002, 6225353, 6339464, 6163584,
      5906216, 6717886, 6021898, 6224612]),
]


@need_data
class CorpusStates(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = R.AuditData(ROOT)
        cls.mats = {}

    def mat(self, client, tile, rid, tid, wdt=775971):
        key = (client, wdt, tile)
        if key not in self.mats:
            mphd, maid = R.tile_ids(ROOT, 'wdt/%s/%d.bin' % (client, wdt), *tile)
            self.assertEqual((maid[0], maid[3]), (rid, tid))
            self.mats[key] = R.build_material(self.data.read(client, rid), self.data.read(client, tid), mphd)
        return self.mats[key]

    def test_pinned_chunks(self):
        for client, tile, rid, tid, n, state, hashes in PINNED_CHUNKS:
            ch = self.mat(client, tile, rid, tid)['chunks'][n]
            with self.subTest(client=client, tile=tile, chunk=n):
                self.assertEqual(ch['state'], state, ch['reason'])
                self.assertEqual([R.sha12(l['alpha']) for l in ch['layers'][1:]], hashes)
        ch = self.mat('wow', (34, 46), 778417, 778420)['chunks'][146]
        self.assertEqual(ch['holes'], 0x303)
        self.assertEqual([l['fileDataID'] for l in ch['layers']], [186934, 186951, 188903])
        self.assertEqual(len(self.mat('wow_classic_beta', (31, 47), 777822, 777825)['chunks'][48]['layers']), 5)
        m = self.mat('wow', (30, 49), 777632, 777635)
        self.assertEqual([R.layer_height(m, l) for l in m['chunks'][68]['layers']],
                         [None, (971415, 5.0, 1.0), (878974, 4.0, 0.5), None])

    def test_pinned_tiles(self):
        for client, wdt, tile, rid, tid, counters, mccv, mcsh, unique, heights in PINNED_TILES:
            m = self.mat(client, tile, rid, tid, wdt)
            with self.subTest(client=client, wdt=wdt, tile=tile):
                self.assertEqual(dict((k, v) for k, v in m['counters'].items() if v), counters)
                self.assertEqual((m['notes']['mccvApplied'], m['notes']['mcsh'], m['notes']['mccvFlagMismatch']), (mccv, mcsh, 0))
                self.assertEqual(len(m['uniqueTextures']), unique)
                self.assertEqual(m['uniqueHeightTextures'], heights)
        self.assertEqual(self.mat('wow', (32, 50), 778037, 778040)['uniqueTextures'],
                         [187127, 187138, 187115, 187125, 186770, 187100, 187088, 187082, 187094, 187098])
        m = self.mat('wow', (27, 19), 1923654, 1923657, 1923857)
        self.assertEqual(len(R.height_chunks(m)), 256)
        self.assertTrue(all(R.chunk_heights(m, ch) is None for ch in m['chunks']))

    def test_map_2923_nibble4_and_eight_layers(self):
        """Retail map 2923 32_24: MTXP scale nibble 4 on MDID 5900005, 5900000, 5900003 and 5906217 (0.5 repeats),
        6..8 layers in 115 chunks; chunk 145 has 8 layers (7 overlay alpha maps) and two nibble-4 layers."""
        m = self.mat('wow', (32, 24), 7473884, 7473887, 6838755)
        self.assertEqual(sorted(m['mdid'][k] for k, e in enumerate(m['mtxp']) if (e[0] >> 4) & 0xF == 4),
                         [5900000, 5900003, 5900005, 5906217])
        hist = {}
        for ch in m['chunks']:
            hist[len(ch['layers'])] = hist.get(len(ch['layers']), 0) + 1
        self.assertEqual(hist, {2: 4, 3: 38, 4: 41, 5: 58, 6: 83, 7: 27, 8: 5})
        ch = m['chunks'][145]
        self.assertEqual([(l['fileDataID'], l['repeats']) for l in ch['layers']],
                         [(5928543, 2), (5876854, 1), (6225354, 1), (6156542, 4), (6021899, 2), (5900005, 0.5),
                          (6021896, 2), (5900000, 0.5)])
        self.assertEqual([R.sha12(l['alpha']) for l in ch['layers'][1:]],
                         ['1b1a6ed1c666', '0fc7a425f5d5', 'ec3a7de032ff', 'a2f8ebe2991b', '4d4c2896e8e6', 'b1201e329805',
                          'c76ffdab3e32'])

    def test_real_mdid_zero_in_drawable_chunks(self):
        """Era map 169 tile 24_27 (MPHD 0x248): its 228 single-layer chunks name MDID 0 and need no alpha, so they are
        drawable MissingTexture chunks; the 28 multi-layer chunks are 4-bit. 249604, used only in 4-layer chunks, is
        not in the client's storage (the extraction manifest says NOT_IN_ROOT)."""
        p = os.path.join(ROOT, 'materials', 'wow_classic_era', 'stage04_reference')
        mphd, maid = R.tile_ids(ROOT, 'wdt/wow_classic_era/780817.bin', 24, 27)
        self.assertEqual((mphd, maid[0], maid[3]), (0x248, 780828, 780831))
        m = R.build_material(self.data.read('wow_classic_era', 780828), self.data.read('wow_classic_era', 780831), mphd)
        self.assertEqual(dict((k, v) for k, v in m['counters'].items() if v), {'missingTexture': 228, 'alphaUnsupported': 28})
        self.assertEqual((m['uniqueTextures'], m['zeroIdReferences']), ([], 228))
        self.assertEqual(self.data.manifest(p)['249604']['status'], 'NOT_IN_ROOT')

    def test_texture_decoder_on_real_files(self):
        """The placeholder 188903 is transparent black at every level; the file mips of 187138 are box-filtered in
        stored values, so every level's mean equals level 0's (152/136/105) -- a check of the decoder, not of mips:
        within 1.5 down to 4 x 4 (the level the map-texture mode samples), within one 5-bit colour step (255/31) at
        2 x 2 and 1 x 1, where a single block's endpoints carry the mean."""
        b = R.Blp(self.data.read('wow', 188903))
        self.assertEqual((b.width, b.height, b.format, len(b.levels)), (8, 8, 'DXT1', 4))
        for lv in range(4):
            self.assertEqual(set(b.decode(lv)[2]), {0.0})
        b = R.Blp(self.data.read('wow', 187138))
        self.assertEqual((b.width, b.format, len(b.levels)), (256, 'DXT3', 9))
        for lv in range(9):
            w, h, px = b.decode(lv)
            means = [sum(px[c::4]) / (w * h) for c in range(3)]
            with self.subTest(level=lv):
                for m, want in zip(means, (152, 136, 105)):
                    self.assertLess(abs(m - want), 1.5 if w >= 4 else 255 / 31.0)
        m = R.Blp(self.data.read('wow', 1293881))
        self.assertEqual((m.width, m.height, m.format, len(m.levels)), (512, 512, 'DXT1', 10))
        h = R.Blp(self.data.read('wow', 971415))                  # 30_49's height texture: the mix reads its alpha
        self.assertEqual((h.width, h.height, h.format), (1024, 1024, 'DXT5'))


@need_data
class Golden(unittest.TestCase):
    """The map-texture mode against the game's own map texture (MAID word 5) on the nine golden tiles: the height
    mix within its measured RMSE + 0.25, and every competing rule worse (design v3 sections 1 and 4); on Retail map
    2923 32_24 also the nibble-4 UV (0.5 repeats, tile-continuous) against its alternatives and the 6..8-layer
    chunks."""

    @classmethod
    def setUpClass(cls):
        data = R.AuditData(ROOT)
        cls.res = dict((case[0], R.golden_table(data, case)) for case in R.GOLDEN)

    def each(self, *labels):
        for label, r in self.res.items():
            if labels and label not in labels:
                continue
            with self.subTest(tile=label):
                yield r

    def worse(self, r, alt, metric='block'):
        self.assertFalse(r[alt].get('same_as_design'), alt)
        self.assertGreater(r[alt][metric], r['design'][metric], alt)

    def test_ids_and_inputs_verified(self):
        """load_case checks MAID words 0/3/5 against the pinned ids and every file against its pin or manifest."""
        self.assertEqual(len(self.res), 9)
        for r in self.each():
            self.assertEqual((r['missing'], r['height_missing']), ({}, {}))
            self.assertGreater(r['blocks'], 15000)
            self.assertEqual(r['design']['notes'], [])          # every texture at one texel per pixel, nothing noted

    def test_within_threshold(self):
        for r in self.each():
            self.assertLessEqual(r['design']['block'], r['threshold'], r['design'])
            self.assertLess(abs(r['design']['block'] - r['measured']), 0.0005)       # the pinned value is current

    def test_plain_weights_and_sharpening_controls_worse(self):
        """Plain weights (the v2 rule) and generic sharpening (w^1.25, w^1.5 normalised) fit worse everywhere."""
        for r in self.each():
            for alt in ('plain weights', 'pow1.25', 'pow1.5'):
                self.worse(r, alt)

    def test_sequential_linear_worse(self):
        for r in self.each():
            for alt in ('sequential', 'sequential heightmix', 'linear'):
                self.worse(r, alt)

    def test_no_mccv_worse(self):
        """Where MCCV is applied (kept chunks), dropping it is worse on those chunks' blocks; a tile without MCCV is
        unchanged."""
        for r in self.each():
            if r['mccv_chunks_kept']:
                self.worse(r, 'no MCCV', 'mccv_block')
            else:
                self.assertEqual(r['no MCCV']['block'], r['design']['block'])
        self.assertEqual(sorted(r['mccv_chunks_kept'] for r in self.res.values()), [0, 0, 1, 36, 57, 152, 223, 256, 256])

    def test_static_effect_chunks(self):
        """34_46's 11 lava chunks (MCLY 0x40/0x80) drawn with the static composite fit like the rest; plain weights
        do not."""
        r = self.res['Retail 34_46']
        self.assertEqual((r['static_chunks_kept'], r['static_blocks']), (11, 704))
        self.assertLess(r['design']['static_block'], r['threshold'])
        self.worse(r, 'plain weights', 'static_block')

    def test_height_textures_on_an_0x80_map(self):
        """30_49: only the per-layer height textures explain its height-textured chunks (full formula against the
        cutoff with default heights and plain weights); on every 0x80 map the 'full formula' is the design itself.
        How the height texture is sampled is gated too: at its layer's repeats (8 repeats and 1 repeat fit worse) and
        its alpha (the red channel fits worse) -- measured 1.324, 1.645 and 2.606 against the design's 1.203 on the
        height chunks' blocks. Its LOD is INFERRED: one mip finer fits within 0.01, so no alternative gates it."""
        r = self.res['Retail 30_49']
        self.assertEqual((r['height_chunks_kept'], r['height_blocks']), (10, 640))
        self.worse(r, 'default heights', 'height_block')
        self.worse(r, 'plain weights', 'height_block')
        for alt in ('heights at 8 repeats', 'heights at 1 repeat', 'height red channel'):
            self.worse(r, alt, 'height_block')
        self.assertLess(r['design']['height_block'], r['threshold'])
        for r in self.each():
            if r['mphd'] & R.MPHD_HEIGHT:
                self.assertTrue(r['full formula'].get('same_as_design'))

    def test_nibble4_and_eight_layers_on_map_2923(self):
        """Retail map 2923 32_24 (every chunk drawn: 188 with a nibble-4 layer, 115 with 6..8 layers): the nibble-4
        layers at 0.5 repeats with tile-continuous UV (diffuse and height texture) fit the map texture like the rest
        (design 1.063 on the nibble-4 blocks, 1.079 on the 6..8-layer blocks); chunk-local UV at 0.5, 1 repeat (as
        nibble 3), 8 repeats (as nibble 0) and 0.25 repeats tile-continuous all fit worse on the nibble-4 blocks. No
        other golden tile has a nibble-4 layer, so there the alternatives are the design itself."""
        r = self.res['Retail map 2923 32_24']
        self.assertEqual(dict((k, v) for k, v in r['counters'].items() if v), {'textured': 256})
        self.assertEqual((r['nibble4_chunks_kept'], r['nibble4_blocks'], r['layers6to8_chunks_kept'],
                          r['layers6to8_blocks']), (188, 12032, 115, 7360))
        self.assertLess(r['design']['nibble4_block'], r['threshold'])
        self.assertLess(r['design']['layers6to8_block'], r['threshold'])
        for alt in ('nibble 4 chunk-local', 'nibble 4 as 1 repeat', 'nibble 4 as 8 repeats',
                    'nibble 4 at 0.25 tile-continuous'):
            self.worse(r, alt, 'nibble4_block')
            self.worse(r, alt)
            for other in self.res.values():
                if other is not r:
                    self.assertTrue(other[alt].get('same_as_design'), (other['label'], alt))

    def test_full_formula_worse_on_an_0x80_clear_map(self):
        """WDT 1923857 (MPHD 0x34E): its MHID and MTXP height values must not enter the mix."""
        r = self.res['Retail WDT 1923857 27_19']
        self.assertEqual(r['mphd'] & R.MPHD_HEIGHT, 0)
        self.assertEqual(r['height_chunks_kept'], 256)
        self.worse(r, 'full formula')
        self.assertTrue(r['default heights'].get('same_as_design'))

    def test_alpha_placement(self):
        """Alpha texel i centred at (i + 0.5) / 64 beats +-0.25 texel and the i/63 layout, per pixel."""
        for r in self.each():
            for alt in ('alpha +0.25 texel', 'alpha -0.25 texel', 'alpha i/63'):
                self.worse(r, alt, 'pixel')

    def test_zero_phase(self):
        for r in self.each():
            for alt in ('phase +1,+0', 'phase -1,+0', 'phase +0,+1', 'phase +0,-1'):
                self.worse(r, alt, 'pixel')

    def test_identity_best_of_8_symmetries(self):
        """Alpha maps (4 x 4 blocks) and textures (per pixel) under the 7 other symmetries of the square."""
        for r in self.each():
            for s in range(1, 8):
                self.worse(r, 'alpha ' + R.SYMMETRIES[s][0])
                self.worse(r, 'texture ' + R.SYMMETRIES[s][0], 'pixel')


@need_data
class CorpusCompare(unittest.TestCase):
    """The compare tool on the reference's own 512-px composite of a golden tile with height textures (30_49): V2
    passes with every input verified, and the plain-weights composite fails its threshold."""

    def test_v2_on_30_49(self):
        with tempfile.TemporaryDirectory() as d:
            base = os.path.join(d, 'ref_30_49')
            self.assertEqual(quiet(R.main, ['terrain_material_reference.py', 'maptexture', ROOT, 'wow', 'wdt/wow/775971.bin',
                                            '30', '49', base]), 0)
            self.assertEqual(quiet(CMP.main, ['compare_terrain_material.py', 'v2', base + '.json', '--corpus', ROOT,
                                              '--reference-tolerance', '0.0001']), 0)
            data = R.AuditData(ROOT)
            mphd, maid = R.tile_ids(ROOT, 'wdt/wow/775971.bin', 30, 49)
            mat, tex = R.tile_material(data, 'wow', mphd, maid[0], maid[3])
            plain, _ = R.map_composite(mat, tex, rule='plain weights')
            side = load_json(base + '.json')
            side.pop('png')
            R.write_composite_dump(base + '_plain', plain, 512, side, R.tile_coverage(mat, range(256)), png=False)
            self.assertEqual(quiet(CMP.main, ['compare_terrain_material.py', 'v2', base + '_plain.json', '--corpus', ROOT]), 1)


@need_data
class Converter(unittest.TestCase):
    """Blizzard's offline 4-bit -> 8-bit conversion (documents U19; not a draw-time rule)."""

    def test_byte_agreement(self):
        data = R.AuditData(ROOT)
        for label, client, fd, need in R.CONVERTER_CASES:
            r = R.converter_agreement(data.read('wow_classic_era', fd), data.read(client, fd))
            with self.subTest(tile=label):
                self.assertGreater(r['texels'], 2000000)
                self.assertGreaterEqual(r['within1'], need, r)


if __name__ == '__main__':
    unittest.main()
