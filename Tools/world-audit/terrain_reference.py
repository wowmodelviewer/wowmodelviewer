"""Independent CPU reference of the stage 03 terrain mesh: one root ADT tile as Unity mesh data, and the reader and
writer of the terrain mesh dump format.

PARSER-INDEPENDENT, DESIGN-DEPENDENT. The bytes are read with the audit's own `wowfmt` readers, never with the C#
parsers, so a parser fault in C# shows up as a difference. But this script applies the same design rules as the C#
builder (Assets/Scripts/Wow/World/TerrainMesh.cs): the same axis map, winding and normal rule. It therefore cannot
detect a design error; the orientation tests (minimap_orientation.py, the player's coverage and top-down captures)
do that.

The rules applied, as pinned by the design (section 2.1-3), with the skip rule spelled out as the C# parser applies
it ("not Decoded" there):
  T = 1600/3, U = T/128
  outer vertex (r, c) of chunk (ix, iy) of tile (tx, ty): R = 128 ty + 8 iy + r, Cc = 128 tx + 8 ix + c,
      X = (4096 - R) U, Y = (4096 - Cc) U;  inner (r, c): X = (4095.5 - R) U, Y = (4095.5 - Cc) U
  Z = float(stored C) + float(MCVT[k]), k = 17r + c (outer) or 17r + 9 + c (inner)
  origin (the tile's north-west corner) X0 = (4096 - 128 ty) U, Y0 = (4096 - 128 tx) U, Z0 = 0
  Unity: ux = f32(-(Y - Y0)), uy = f32(Z - Z0), uz = f32(X - X0)
  cell (r, c): (m, a, b), (m, b, d), (m, d, cc), (m, cc, a) with a, b, cc, d its corners and m its inner vertex;
      a hole cell (64-bit mask, bit r*8 + c, LSB first, when MCNK flag 0x10000 is set) has no triangles
  normals: Unity-space double cross products in mesh order, summed per vertex over the kept triangles (outer vertices
      keyed tile-wide on the 129 x 129 grid), normalised, cast to float32; (0, 1, 0) without a kept triangle
  bounds: from the vertices the index list references; none without a triangle
  tile identity: every usable chunk's stored x/y within 0.01 yd of its corner, else the whole build fails
  a chunk is Skipped when its header, index, stored position or MCVT is unusable, its MCNR is shorter than 435
      bytes or repeated, its MCCV is not 580 bytes or repeated (the mesh uses neither, but AdtParser.cs marks such a
      chunk Malformed, so the builder skips it; a missing MCNR or MCCV is fine), or a height is beyond 65,536 yd;
      Unsupported when flag 0x10000 is clear and the 16-bit hole field is non-zero

  python -I terrain_reference.py <root adt> <tileX> <tileY> <out dump> [--meta key=value ...]
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wowfmt import adt as A  # noqa: E402

T = 1600.0 / 3.0
U = T / 128.0
IDENTITY_TOLERANCE = 0.01
MAX_ABS_HEIGHT = 65536.0
GRID = 129
VERTICES = 145
MCNR_MIN_BYTES = 3 * VERTICES       # 145 normal triplets
MCCV_BYTES = 4 * VERTICES           # 145 colours
DUMP_MAGIC = 'wmv-terrain-dump'
DUMP_VERSION = 1
# Values per record of the dump format (TerrainDump.cs); bounds/cbounds are 6 floats or 'none', idx 3 per triangle.
DUMP_RECORD_VALUES = {'tile': 2, 'origin': 3, 'counts': 7, 'meshbytes': 1, 'root': 2 + 16, 'chunk': 7,
                      'pos': 3 * VERTICES, 'nrm': 3 * VERTICES, 'unity': 5 + 6 + 16}
CHUNK_RECORDS = ('reason', 'pos', 'nrm', 'idx', 'cbounds', 'unity')


class TileMismatch(Exception):
    """The root's stored chunk positions do not belong to the requested tile."""


class BuildFailed(Exception):
    """No part of the tile is usable."""


def f32(v):
    return struct.unpack('<f', struct.pack('<f', v))[0]


def f32_bits(v):
    return struct.unpack('<I', struct.pack('<f', v))[0]


def bits_f32(b):
    return struct.unpack('<f', struct.pack('<I', b))[0]


def d_bits(v):
    return struct.unpack('<Q', struct.pack('<d', v))[0]


def bits_d(b):
    return struct.unpack('<d', struct.pack('<Q', b))[0]


def outer(r, c):
    return 17 * r + c


def inner(r, c):
    return 17 * r + 9 + c


def cell_indices(r, c):
    a, b, cc, d, m = outer(r, c), outer(r, c + 1), outer(r + 1, c), outer(r + 1, c + 1), inner(r, c)
    return [m, a, b, m, b, d, m, d, cc, m, cc, a]


def tile_origin(tx, ty):
    return ((4096 - 128 * ty) * U, (4096 - 128 * tx) * U, 0.0)


def vertex_xy(tx, ty, ix, iy, k):
    r, j = divmod(k, 17)
    if j < 9:
        return (4096 - (128 * ty + 8 * iy + r)) * U, (4096 - (128 * tx + 8 * ix + j)) * U
    return (4095.5 - (128 * ty + 8 * iy + r)) * U, (4095.5 - (128 * tx + 8 * ix + (j - 9))) * U


def chunk_corner(tx, ty, ix, iy):
    return (4096 - (128 * ty + 8 * iy)) * U, (4096 - (128 * tx + 8 * ix)) * U


def _finite(v):
    return not (math.isnan(v) or math.isinf(v))


def read_chunks(buf):
    """Per MCNK: (header dict or None, heights tuple or None, problem text or None), with the rules of this reference."""
    sf = A.SplitFile(buf, root=True)
    if len(sf.mcnk) != 256:
        raise BuildFailed('a root ADT has 256 MCNKs, this one has %d' % len(sf.mcnk))
    out = []
    for n, (c, h, sub) in enumerate(sf.mcnk):
        if h is None:
            out.append((None, None, 'MCNK %d is smaller than its 128-byte header' % n))
            continue
        if sub.problems:
            out.append((h, None, 'MCNK %d sub-chunks: %s' % (n, sub.problems[0])))
            continue
        if h['index_x'] != n % 16 or h['index_y'] != n // 16:
            out.append((h, None, 'MCNK %d header index (%d, %d) is not its position' % (n, h['index_x'], h['index_y'])))
            continue
        if not all(_finite(v) for v in h['position']):
            out.append((h, None, 'MCNK %d stored position is not finite' % n))
            continue
        mcvt = sub.all('MCVT')
        if len(mcvt) != 1 or mcvt[0].size != 580:
            out.append((h, None, 'MCNK %d has no usable MCVT' % n))
            continue
        hv = A.mcvt(buf, mcvt[0])
        if not all(_finite(v) for v in hv):
            out.append((h, None, 'MCNK %d has a non-finite height' % n))
            continue
        # The C# parser's Malformed rules for the sub-chunks the mesh does not use (AdtParser.cs, ParseRootChunk).
        mcnr = sub.all('MCNR')
        if len(mcnr) > 1 or any(s.size < MCNR_MIN_BYTES for s in mcnr):
            out.append((h, None, 'MCNK %d MCNR is repeated or shorter than 435 bytes (%s)' % (n, [s.size for s in mcnr])))
            continue
        mccv = sub.all('MCCV')
        if len(mccv) > 1 or any(s.size != MCCV_BYTES for s in mccv):
            out.append((h, None, 'MCNK %d MCCV is repeated or not 580 bytes (%s)' % (n, [s.size for s in mccv])))
            continue
        out.append((h, hv, None))
    return out


def build_tile(buf, tx, ty, origin=None):
    """The reference mesh of root `buf` as tile (tx, ty): a dict in the dump's shape (see read_dump)."""
    if not (0 <= tx < 64 and 0 <= ty < 64):
        raise ValueError('tile x/y must be 0..63')
    X0, Y0, Z0 = origin if origin is not None else tile_origin(tx, ty)
    chunks_in = read_chunks(buf)
    usable = 0
    for n, (h, hv, problem) in enumerate(chunks_in):
        if problem is not None:
            continue
        usable += 1
        ex, ey = chunk_corner(tx, ty, n % 16, n // 16)
        sx, sy = h['position'][0], h['position'][1]
        if not (abs(sx - ex) <= IDENTITY_TOLERANCE and abs(sy - ey) <= IDENTITY_TOLERANCE):
            raise TileMismatch('root ADT does not belong to tile %d_%d: MCNK %d is stored at (%r, %r), its corner is (%r, %r)'
                               % (tx, ty, n, sx, sy, ex, ey))
    if usable == 0:
        raise BuildFailed('no decodable chunk')

    chunks = []
    dpos = {}
    for n, (h, hv, problem) in enumerate(chunks_in):
        ch = {'n': n, 'ix': n % 16, 'iy': n // 16, 'state': 'Skipped', 'reason': problem, 'hole_mask': 0, 'hole_cells': 0,
              'triangles': 0, 'pos': None, 'nrm': None, 'idx': None, 'cbounds': None, 'unity': None}
        chunks.append(ch)
        if problem is not None:
            continue
        base = h['position'][2]
        if not abs(base) <= MAX_ABS_HEIGHT or not all(abs(base + v) <= MAX_ABS_HEIGHT for v in hv):
            ch['reason'] = 'height out of range'
            continue
        if h['flags'] & 0x10000:
            mask = h['holes_high']
        elif h['holes_low'] == 0:
            mask = 0
        else:
            ch['state'] = 'Unsupported'
            ch['reason'] = 'hole mask not decodable: flag 0x10000 clear, low-resolution mask 0x%04X' % h['holes_low']
            continue
        ch['state'] = 'Built'
        ch['reason'] = None
        ix, iy = n % 16, n // 16
        pd = []
        pf = []
        for k in range(145):
            x, y = vertex_xy(tx, ty, ix, iy, k)
            z = base + hv[k]
            ux, uy, uz = -(y - Y0), z - Z0, x - X0
            pd.append((ux, uy, uz))
            pf.append((f32(ux), f32(uy), f32(uz)))
        dpos[n] = pd
        ch['pos'] = pf
        idx = []
        holes = 0
        for r in range(8):
            for c in range(8):
                if (mask >> (r * 8 + c)) & 1:
                    holes += 1
                    continue
                idx.extend(cell_indices(r, c))
        ch['idx'] = idx
        ch['triangles'] = len(idx) // 3
        ch['hole_cells'] = holes
        ch['hole_mask'] = mask

    # tile-wide normals, in the builder's order: chunks, triangles, the triangle's three vertices
    acc_outer = [[0.0, 0.0, 0.0] for _ in range(GRID * GRID)]
    acc_inner = {}

    def slot(ch, k):
        r, j = divmod(k, 17)
        if j < 9:
            return acc_outer[(8 * ch['iy'] + r) * GRID + 8 * ch['ix'] + j]
        return acc_inner[ch['n']][r * 8 + j - 9]

    for ch in chunks:
        if ch['state'] != 'Built':
            continue
        acc_inner[ch['n']] = [[0.0, 0.0, 0.0] for _ in range(64)]
        p = dpos[ch['n']]
        idx = ch['idx']
        for t in range(0, len(idx), 3):
            i0, i1, i2 = idx[t], idx[t + 1], idx[t + 2]
            p0, p1, p2 = p[i0], p[i1], p[i2]
            e1x, e1y, e1z = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
            e2x, e2y, e2z = p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]
            nx = e1y * e2z - e1z * e2y
            ny = e1z * e2x - e1x * e2z
            nz = e1x * e2y - e1y * e2x
            for v in (i0, i1, i2):
                a = slot(ch, v)
                a[0] += nx
                a[1] += ny
                a[2] += nz
    for ch in chunks:
        if ch['state'] != 'Built':
            continue
        nrm = []
        for k in range(145):
            ax, ay, az = slot(ch, k)
            ln = math.sqrt(ax * ax + ay * ay + az * az)
            nrm.append((f32(ax / ln), f32(ay / ln), f32(az / ln)) if ln > 0 else (0.0, 1.0, 0.0))
        ch['nrm'] = nrm

    counts = {'chunks': 256, 'built': 0, 'skipped': 0, 'unsupported': 0, 'vertices': 0, 'triangles': 0, 'hole_cells': 0}
    bounds = None
    meshbytes = 0
    for ch in chunks:
        counts[ch['state'].lower()] += 1
        if ch['state'] != 'Built':
            continue
        counts['hole_cells'] += ch['hole_cells']
        if ch['triangles'] == 0:
            continue
        used = sorted(set(ch['idx']))
        mn = tuple(min(ch['pos'][k][a] for k in used) for a in range(3))
        mx = tuple(max(ch['pos'][k][a] for k in used) for a in range(3))
        ch['cbounds'] = (mn, mx)
        bounds = (mn, mx) if bounds is None else (tuple(min(bounds[0][a], mn[a]) for a in range(3)),
                                                  tuple(max(bounds[1][a], mx[a]) for a in range(3)))
        counts['vertices'] += 145
        counts['triangles'] += ch['triangles']
        meshbytes += 145 * 24 + 2 * len(ch['idx'])
    return {'version': DUMP_VERSION, 'source': 'reference', 'tile': (tx, ty), 'origin': (X0, Y0, Z0), 'counts': counts,
            'bounds': bounds, 'meshbytes': meshbytes, 'meta': [], 'root': None, 'chunks': chunks}


# ------------------------------------------------------------------------------------------------- dump format

def _hex_floats(values):
    return ' '.join('%08x' % f32_bits(v) for v in values)


def write_dump(tile, path):
    """Write `tile` (build_tile's or read_dump's shape) in the TerrainDump format, version 1 (TerrainDump.cs)."""
    c = tile['counts']
    lines = ['%s %d' % (DUMP_MAGIC, DUMP_VERSION), 'source %s' % tile['source'], 'tile %d %d' % tuple(tile['tile']),
             'origin ' + ' '.join('%016x' % d_bits(v) for v in tile['origin']),
             'counts %d %d %d %d %d %d %d' % (c['chunks'], c['built'], c['skipped'], c['unsupported'], c['vertices'],
                                              c['triangles'], c['hole_cells'])]
    b = tile['bounds']
    lines.append('bounds none' if b is None else 'bounds ' + _hex_floats(list(b[0]) + list(b[1])))
    lines.append('meshbytes %d' % tile['meshbytes'])
    for k, v in tile['meta']:
        lines.append('meta %s %s' % (k, v))
    if tile.get('root'):
        r = tile['root']
        lines.append('root %d %d %s' % (r['layer'], 1 if r['active'] else 0, _hex_floats(r['local_to_world'])))
    for ch in tile['chunks']:
        lines.append('chunk %d %d %d %s %016x %d %d' % (ch['n'], ch['ix'], ch['iy'], ch['state'], ch['hole_mask'],
                                                        ch['hole_cells'], ch['triangles']))
        if ch.get('reason') is not None:
            lines.append('reason ' + ch['reason'])
        arrays = ch['pos'] is not None or ch['nrm'] is not None or ch['idx'] is not None
        if ch['pos'] is not None:
            lines.append('pos ' + _hex_floats([v for p in ch['pos'] for v in p]))
        if ch['nrm'] is not None:
            lines.append('nrm ' + _hex_floats([v for p in ch['nrm'] for v in p]))
        if ch['idx'] is not None:
            lines.append(('idx ' + ' '.join(str(i) for i in ch['idx'])).rstrip())
        if arrays:
            cb = ch['cbounds']
            lines.append('cbounds none' if cb is None else 'cbounds ' + _hex_floats(list(cb[0]) + list(cb[1])))
        u = ch.get('unity')
        if u:
            lines.append('unity %d %d %d %s %s %s' % (u['layer'], 1 if u['active'] else 0, u['sub_mesh_count'], u['topology'],
                                                      u['index_format'], _hex_floats(list(u['mesh_bounds'][0]) + list(u['mesh_bounds'][1])
                                                                                     + list(u['local_to_world']))))
    lines.append('end')
    with open(path, 'w', encoding='ascii', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


def _floats(tokens):
    return [bits_f32(int(t, 16)) for t in tokens]


def _triples(values):
    return [tuple(values[i:i + 3]) for i in range(0, len(values), 3)]


def _check_record(path, key, tok, ch):
    """Raise ValueError unless a record has the number of values the format gives it (a short record would otherwise
    be compared over just its overlap with the reference)."""
    want = DUMP_RECORD_VALUES.get(key)
    if want is not None and len(tok) != want:
        raise ValueError('%s: %s record with %d value(s), the format has %d' % (path, key, len(tok), want))
    if key in ('bounds', 'cbounds') and tok != ['none'] and len(tok) != 6:
        raise ValueError('%s: %s record with %d value(s), the format has 6 or none' % (path, key, len(tok)))
    if key in CHUNK_RECORDS and ch is None:
        raise ValueError('%s: %s record before the first chunk record' % (path, key))
    if key == 'idx':
        if len(tok) % 3:
            raise ValueError('%s: chunk %d idx record with %d values, not whole triangles' % (path, ch['n'], len(tok)))
        bad = [t for t in tok if not 0 <= int(t) < VERTICES]
        if bad:
            raise ValueError('%s: chunk %d idx record holds %s, outside 0..%d' % (path, ch['n'], bad[0], VERTICES - 1))


def read_dump(path):
    """Parse a TerrainDump file. Floats come back as Python floats holding the exact float32 values; `*_bits` keep the
    raw patterns (so -0.0 and 0.0 stay distinguishable where that matters). Every record must have the number of values
    the format gives it (DUMP_RECORD_VALUES; ValueError otherwise)."""
    with open(path, 'r', encoding='ascii') as f:
        lines = f.read().split('\n')
    if not lines or lines[0] != '%s %d' % (DUMP_MAGIC, DUMP_VERSION):
        raise ValueError('%s: not a version %d terrain dump (first line %r)' % (path, DUMP_VERSION, lines[0] if lines else ''))
    tile = {'version': DUMP_VERSION, 'meta': [], 'root': None, 'chunks': [], 'bounds': None}
    ch = None
    ended = False
    for line in lines[1:]:
        if not line:
            continue
        key, _, rest = line.partition(' ')
        tok = rest.split(' ') if rest else []
        _check_record(path, key, tok, ch)
        if key == 'source':
            tile['source'] = rest
        elif key == 'tile':
            tile['tile'] = (int(tok[0]), int(tok[1]))
        elif key == 'origin':
            tile['origin'] = tuple(bits_d(int(t, 16)) for t in tok)
        elif key == 'counts':
            v = [int(t) for t in tok]
            tile['counts'] = dict(zip(('chunks', 'built', 'skipped', 'unsupported', 'vertices', 'triangles', 'hole_cells'), v))
        elif key == 'bounds':
            if tok != ['none']:
                v = _floats(tok)
                tile['bounds'] = (tuple(v[:3]), tuple(v[3:]))
        elif key == 'meshbytes':
            tile['meshbytes'] = int(tok[0])
        elif key == 'meta':
            k, _, v = rest.partition(' ')
            tile['meta'].append((k, v))
        elif key == 'root':
            tile['root'] = {'layer': int(tok[0]), 'active': tok[1] == '1', 'local_to_world': _floats(tok[2:18])}
        elif key == 'chunk':
            ch = {'n': int(tok[0]), 'ix': int(tok[1]), 'iy': int(tok[2]), 'state': tok[3], 'hole_mask': int(tok[4], 16),
                  'hole_cells': int(tok[5]), 'triangles': int(tok[6]), 'reason': None, 'pos': None, 'nrm': None, 'idx': None,
                  'cbounds': None, 'unity': None, 'pos_bits': None, 'nrm_bits': None}
            tile['chunks'].append(ch)
        elif key == 'reason':
            ch['reason'] = rest
        elif key == 'pos':
            ch['pos_bits'] = [int(t, 16) for t in tok]
            ch['pos'] = _triples([bits_f32(b) for b in ch['pos_bits']])
        elif key == 'nrm':
            ch['nrm_bits'] = [int(t, 16) for t in tok]
            ch['nrm'] = _triples([bits_f32(b) for b in ch['nrm_bits']])
        elif key == 'idx':
            ch['idx'] = [int(t) for t in tok]
        elif key == 'cbounds':
            if tok != ['none']:
                v = _floats(tok)
                ch['cbounds'] = (tuple(v[:3]), tuple(v[3:]))
        elif key == 'unity':
            v = _floats(tok[5:])
            ch['unity'] = {'layer': int(tok[0]), 'active': tok[1] == '1', 'sub_mesh_count': int(tok[2]), 'topology': tok[3],
                           'index_format': tok[4], 'mesh_bounds': (tuple(v[0:3]), tuple(v[3:6])), 'local_to_world': v[6:22]}
        elif key == 'end':
            ended = True
            break
        else:
            raise ValueError('%s: unknown record %r' % (path, key))
    if not ended:
        raise ValueError('%s: no end record (truncated dump)' % path)
    return tile


def read_bytes(path):
    with open(path, 'rb') as f:
        return f.read()


def meta(tile, key, default=None):
    for k, v in tile['meta']:
        if k == key:
            return v
    return default


def main(argv):
    if len(argv) < 5:
        print(__doc__)
        return 2
    path, tx, ty, out = argv[1], int(argv[2]), int(argv[3]), argv[4]
    buf = read_bytes(path)
    tile = build_tile(buf, tx, ty)
    for a in argv[5:]:
        if a.startswith('--meta') or '=' not in a:
            continue
        k, _, v = a.partition('=')
        tile['meta'].append((k, v))
    write_dump(tile, out)
    c = tile['counts']
    print('%s %d_%d: built %d, skipped %d, unsupported %d, vertices %d, triangles %d, hole cells %d -> %s'
          % (os.path.basename(path), tx, ty, c['built'], c['skipped'], c['unsupported'], c['vertices'], c['triangles'],
             c['hole_cells'], out))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
