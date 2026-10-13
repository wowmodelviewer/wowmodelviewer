"""Orientation proof of the terrain mesh against the game's own minimap (stage 03 design, section 2.3).

Every structural check of the mesh (winding, Cross.y, the reference compare) is unchanged if the horizontal map is
mirrored and the index order reversed at the same time. The minimap tile is an outside anchor: a 512x512 DXT1 BLP
named by MAID word 7, one 4x4 block per terrain cell, rendered by the game with its own lighting. Its luminance is
fitted (least squares, with an intercept) against the terrain's per-cell slope normals under all 8 symmetries of the
128x128 cell grid; only the true orientation explains it well. The minimap's row grows with the vertex row r (south),
its column with the vertex column c (east).

  (a) python -I minimap_orientation.py dump <mesh dump> [<minimap blp>] [--corpus <audit data>]
      The mesh dump (runner, player or reference; TerrainDump format): each cell is placed by its inner vertex's
      Unity position (column from +X = east, row from -Z = south, relative to the dump's own extent) and described by
      that vertex's normal (E, N, up) = (x, z, y). Cells whose inner vertex no triangle references (holes) are left
      out. With --corpus and no minimap, the minimap is found from the dump's meta (client, minimapFileDataID) and
      checked against the WDT's MAID word 7.
  (b) python -I minimap_orientation.py capture <capture png> <minimap blp>
      A top-down orthographic capture of the tile in the 'normals' view (screen up = north = Unity +Z, screen right =
      east = Unity +X, the tile filling a square viewport) is reduced to 128x128 and the minimap luminance is fitted
      against its R, G, B under the 8 symmetries of the capture.
  render: python -I minimap_orientation.py render <mesh dump> <out png> [--size N] [--mirror]
      Writes the capture (b) expects from a dump's normals (n * 0.5 + 0.5, hole cells black), optionally mirrored
      east-west; used to test (b) before the player produces real captures.

Pass: the identity symmetry's R^2 is at least --min-ratio (default 2) times every other symmetry's. Exit code 0 on
PASS, 1 on FAIL, 2 on a usage or input error. Standard library only.
"""
import json
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import terrain_reference as R  # noqa: E402

GRID = 128
U = R.U

SYMMETRIES = [('identity', lambda v, u: (v, u)), ('transpose', lambda v, u: (u, v)),
              ('mirror rows (north-south)', lambda v, u: (GRID - 1 - v, u)),
              ('mirror columns (east-west)', lambda v, u: (v, GRID - 1 - u)),
              ('rotate 180', lambda v, u: (GRID - 1 - v, GRID - 1 - u)),
              ('rotate 90 (transpose + mirror rows)', lambda v, u: (GRID - 1 - u, v)),
              ('rotate 270 (transpose + mirror columns)', lambda v, u: (u, GRID - 1 - v)),
              ('anti-transpose', lambda v, u: (GRID - 1 - u, GRID - 1 - v))]


# ------------------------------------------------------------------------------------------------- minimap BLP

def _rgb565(c):
    return (((c >> 11) & 31) * 255 / 31.0, ((c >> 5) & 63) * 255 / 63.0, (c & 31) * 255 / 31.0)


def minimap_luminance(path):
    """128x128 mean luminance (Rec. 709 weights on the stored 0..255 values) of a square BLP2 DXT1 minimap."""
    buf = R.read_bytes(path)
    if buf[:4] != b'BLP2' or len(buf) < 148:
        raise ValueError('%s: not a BLP2 file' % path)
    encoding, alpha_depth, alpha_type = buf[8], buf[9], buf[10]
    w, h = struct.unpack_from('<II', buf, 12)
    ofs, size = struct.unpack_from('<I', buf, 20)[0], struct.unpack_from('<I', buf, 84)[0]
    if encoding != 2 or alpha_type != 0 or w != h or w % GRID or w < GRID:
        raise ValueError('%s: expected a square DXT1 BLP2 of at least 128 px (encoding %d, alpha type %d, %dx%d)'
                         % (path, encoding, alpha_type, w, h))
    bw = w // 4
    if ofs + 8 * bw * (h // 4) > len(buf) or size < 8 * bw * (h // 4):
        raise ValueError('%s: mip 0 runs past the file' % path)
    per = w // GRID                       # pixels per cell side
    acc = [[0.0] * GRID for _ in range(GRID)]
    for by in range(h // 4):
        for bx in range(bw):
            c0, c1, bits = struct.unpack_from('<HHI', buf, ofs + 8 * (by * bw + bx))
            p0, p1 = _rgb565(c0), _rgb565(c1)
            if c0 > c1:
                pal = [p0, p1, tuple((2 * a + b) / 3 for a, b in zip(p0, p1)), tuple((a + 2 * b) / 3 for a, b in zip(p0, p1))]
            else:
                pal = [p0, p1, tuple((a + b) / 2 for a, b in zip(p0, p1)), (0.0, 0.0, 0.0)]
            lum = [0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2] for p in pal]
            for k in range(16):
                y, x = by * 4 + k // 4, bx * 4 + k % 4
                acc[y // per][x // per] += lum[(bits >> (2 * k)) & 3]
    n = per * per
    return [[v / n for v in row] for row in acc]


def write_dxt1_minimap(path, lum):
    """A 512x512 BLP2 DXT1 file whose 4x4 block (v, u) is the solid grey nearest lum[v][u] (0..255): test fixtures."""
    header = bytearray(148 + 1024)
    header[0:4] = b'BLP2'
    struct.pack_into('<I', header, 4, 1)
    header[8], header[9], header[10], header[11] = 2, 0, 0, 0
    struct.pack_into('<II', header, 12, 512, 512)
    blocks = bytearray()
    for v in range(GRID):
        for u in range(GRID):
            g = max(0, min(255, int(round(lum[v][u]))))
            c = ((g >> 3) << 11) | ((g >> 2) << 5) | (g >> 3)
            blocks += struct.pack('<HHI', c, c, 0)
    struct.pack_into('<I', header, 20, len(header))
    struct.pack_into('<I', header, 84, len(blocks))
    with open(path, 'wb') as f:
        f.write(bytes(header) + bytes(blocks))


# ------------------------------------------------------------------------------------------------- PNG

def read_png(path):
    """(width, height, rows of (r, g, b) tuples) for 8/16-bit grey, grey+alpha, RGB, RGBA or palette PNGs, not interlaced."""
    data = R.read_bytes(path)
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('%s: not a PNG' % path)
    pos, idat, plte = 8, [], None
    w = h = depth = ctype = None
    while pos + 8 <= len(data):
        n, tag = struct.unpack_from('>I4s', data, pos)
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if tag == b'IHDR':
            w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', body)
            if interlace:
                raise ValueError('%s: interlaced PNGs are not supported' % path)
        elif tag == b'PLTE':
            plte = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif tag == b'IDAT':
            idat.append(body)
        elif tag == b'IEND':
            break
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    if depth not in (8, 16) or (ctype == 3 and depth != 8):
        raise ValueError('%s: bit depth %d not supported' % (path, depth))
    bpp = channels * depth // 8
    raw = zlib.decompress(b''.join(idat))
    stride = w * bpp
    prev = bytearray(stride)
    rows = []
    p = 0
    for _ in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        if f == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        elif f != 0:
            raise ValueError('%s: unknown filter %d' % (path, f))
        prev = line
        step = depth // 8
        px = []
        for x in range(w):
            s = [line[x * bpp + ch * step] for ch in range(channels)]
            if ctype == 3:
                px.append(plte[s[0]])
            elif ctype in (0, 4):
                px.append((s[0], s[0], s[0]))
            else:
                px.append((s[0], s[1], s[2]))
        rows.append(px)
    return w, h, rows


def write_png(path, rows):
    """An 8-bit RGB PNG from rows of (r, g, b) tuples (0..255)."""
    h, w = len(rows), len(rows[0])
    raw = bytearray()
    for row in rows:
        raw.append(0)
        for r, g, b in row:
            raw += bytes((r, g, b))

    def chunk(tag, body):
        return struct.pack('>I', len(body)) + tag + body + struct.pack('>I', zlib.crc32(tag + body) & 0xFFFFFFFF)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


# ------------------------------------------------------------------------------------------------- cells

def cells_from_dump(tile):
    """{(row v, column u): (nEast, nNorth, nUp)} from the inner vertices that a triangle references, placed by their
    Unity position: u from +X (east), v from -Z (south), relative to the smallest x / largest z among the dump's inner
    vertices. Raises when the positions do not fall on the 128x128 cell grid."""
    found = []
    xs, zs = [], []
    for ch in tile['chunks']:
        if ch['pos'] is None or ch['nrm'] is None or ch['idx'] is None:
            continue
        used = set(ch['idx'])
        for r in range(8):
            for c in range(8):
                k = R.inner(r, c)
                x, _, z = ch['pos'][k]
                xs.append(x)
                zs.append(z)
                if k in used:
                    found.append((x, z, ch['nrm'][k]))
    if not found:
        raise ValueError('the dump has no referenced inner vertex')
    x0, z0 = min(xs), max(zs)
    cells = {}
    for x, z, n in found:
        uf, vf = (x - x0) / U, (z0 - z) / U
        u, v = int(round(uf)), int(round(vf))
        if abs(uf - u) > 1e-3 or abs(vf - v) > 1e-3 or not (0 <= u < GRID and 0 <= v < GRID) or (v, u) in cells:
            raise ValueError('inner vertex at x %r, z %r is not on the 128x128 cell grid of this dump' % (x, z))
        cells[(v, u)] = (n[0], n[2], n[1])
    return cells


def cells_from_capture(path):
    """{(row v, column u): (R, G, B) mean} of a capture reduced to 128x128 (square captures expected)."""
    w, h, rows = read_png(path)
    acc = [[[0.0, 0.0, 0.0, 0] for _ in range(GRID)] for _ in range(GRID)]
    for y in range(h):
        v = y * GRID // h
        row = rows[y]
        accv = acc[v]
        for x in range(w):
            a = accv[x * GRID // w]
            p = row[x]
            a[0] += p[0]
            a[1] += p[1]
            a[2] += p[2]
            a[3] += 1
    return {(v, u): (a[0] / a[3], a[1] / a[3], a[2] / a[3]) for v in range(GRID) for u, a in enumerate(acc[v]) if a[3]}, (w, h)


# ------------------------------------------------------------------------------------------------- fit

def _solve(m, y):
    n = len(y)
    a = [row[:] + [y[i]] for i, row in enumerate(m)]
    for i in range(n):
        p = max(range(i, n), key=lambda k: abs(a[k][i]))
        a[i], a[p] = a[p], a[i]
        if a[i][i] == 0:
            raise ZeroDivisionError('singular fit')
        for k in range(n):
            if k != i:
                f = a[k][i] / a[i][i]
                for j in range(i, n + 1):
                    a[k][j] -= f * a[i][j]
    return [a[i][n] / a[i][i] for i in range(n)]


def r_squared(xs, ys):
    """R^2 and coefficients of y ~ b0 + b1 x1 + ... (ordinary least squares)."""
    k = len(xs[0]) + 1
    xtx = [[0.0] * k for _ in range(k)]
    xty = [0.0] * k
    for x, y in zip(xs, ys):
        row = (1.0,) + tuple(x)
        for i in range(k):
            ri = row[i]
            xty[i] += ri * y
            xi = xtx[i]
            for j in range(k):
                xi[j] += ri * row[j]
    try:
        beta = _solve(xtx, xty)
    except ZeroDivisionError:
        return 0.0, [0.0] * k
    my = sum(ys) / len(ys)
    ss_tot = sum((y - my) ** 2 for y in ys)
    ss_res = sum((y - sum(b * xi for b, xi in zip(beta, (1.0,) + tuple(x)))) ** 2 for x, y in zip(xs, ys))
    return (1 - ss_res / ss_tot if ss_tot > 0 else 0.0), beta


def fit(lum, cells, min_ratio=2.0):
    """Fit minimap luminance lum[v][u] against cells[s(v, u)] for the 8 symmetries s. Returns a result dict."""
    table = []
    for name, s in SYMMETRIES:
        xs, ys = [], []
        for v in range(GRID):
            for u in range(GRID):
                f = cells.get(s(v, u))
                if f is not None:
                    xs.append(f)
                    ys.append(lum[v][u])
        q, beta = r_squared(xs, ys)
        table.append({'symmetry': name, 'r2': q, 'cells': len(xs), 'coefficients': beta})
    ident = table[0]['r2']
    best_other = max(t['r2'] for t in table[1:])
    ratio = ident / best_other if best_other > 0 else float('inf')
    return {'table': table, 'identity_r2': ident, 'best_other_r2': best_other, 'ratio': ratio, 'min_ratio': min_ratio,
            'pass': ident > 0 and ratio >= min_ratio}


def print_fit(res, what):
    print(what)
    for t in res['table']:
        c = t['coefficients']
        print('   %-42s R^2 %.4f   cells %5d   fit %s' % (t['symmetry'], t['r2'], t['cells'], ' '.join('%+.3g' % v for v in c[1:])))
    print('   identity %.4f, best other %.4f, ratio %.2f (needs >= %.1f): %s'
          % (res['identity_r2'], res['best_other_r2'], res['ratio'], res['min_ratio'], 'PASS' if res['pass'] else 'FAIL'))


# ------------------------------------------------------------------------------------------------- render

def render(tile, size=512, mirror=False):
    """The top-down 'normals' view a capture (b) should show for this dump: rows of (r, g, b)."""
    cells = cells_from_dump(tile)
    rows = []
    for y in range(size):
        v = y * GRID // size
        row = []
        for x in range(size):
            u = x * GRID // size
            n = cells.get((v, GRID - 1 - u if mirror else u))
            if n is None:
                row.append((0, 0, 0))
            else:
                e, nn, up = n            # Unity (x, y, z) = (east, up, north) -> (R, G, B)
                row.append(tuple(max(0, min(255, int(round((c * 0.5 + 0.5) * 255)))) for c in (e, up, nn)))
        rows.append(row)
    return rows


# ------------------------------------------------------------------------------------------------- main

def find_minimap(tile, corpus):
    """The minimap of a dump's tile from the audit corpus, checked against the client's Azeroth WDT MAID word 7."""
    from wowfmt.wdt import WDT
    client, fdid, wdt_id = R.meta(tile, 'client'), R.meta(tile, 'minimapFileDataID'), R.meta(tile, 'wdtFileDataID', '775971')
    if not client or not fdid:
        raise ValueError('the dump names no client/minimapFileDataID; give the minimap path')
    if not corpus:
        raise ValueError('give the minimap path or --corpus <audit data>')
    tx, ty = tile['tile']
    wdt = WDT(R.read_bytes(os.path.join(corpus, 'wdt', client, '%s.bin' % wdt_id)))
    word7 = wdt.maid[ty * 64 + tx][7]
    if str(word7) != fdid:
        raise ValueError('the dump says minimap %s, the WDT MAID word 7 says %d' % (fdid, word7))
    return os.path.join(corpus, 'corpus', client, 'azeroth', '%s.bin' % fdid)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    mode, rest = argv[1], argv[2:]
    opts = {'min-ratio': '2', 'size': '512'}
    flags = set()
    pos = []
    while rest:
        a = rest.pop(0)
        if a in ('--corpus', '--min-ratio', '--size', '--json') and rest:
            opts[a[2:]] = rest.pop(0)
        elif a == '--mirror':
            flags.add('mirror')
        elif a.startswith('--'):
            print('unknown option %s' % a)
            return 2
        else:
            pos.append(a)
    try:
        min_ratio = float(opts['min-ratio'])
        if mode == 'render':
            tile = R.read_dump(pos[0])
            write_png(pos[1], render(tile, int(opts['size']), 'mirror' in flags))
            print('wrote %s (%s px%s)' % (pos[1], opts['size'], ', mirrored east-west' if 'mirror' in flags else ''))
            return 0
        if mode == 'dump':
            tile = R.read_dump(pos[0])
            minimap = pos[1] if len(pos) > 1 else find_minimap(tile, opts.get('corpus'))
            cells = cells_from_dump(tile)
            what = 'dump %s (source %s, tile %d_%d) against minimap %s' % ((pos[0], tile.get('source')) + tuple(tile['tile']) + (minimap,))
        elif mode == 'capture':
            cells, (w, h) = cells_from_capture(pos[0])
            minimap = pos[1]
            what = 'capture %s (%dx%d) against minimap %s' % (pos[0], w, h, minimap)
            if w != h:
                print('note: the capture is not square; the tile is expected to fill a square viewport')
        else:
            print('unknown mode %s' % mode)
            return 2
        lum = minimap_luminance(minimap)
    except (ValueError, IndexError, OSError, TypeError, KeyError) as e:
        # usage and input errors (a missing argument, a malformed dump, PNG or WDT) are exit 2, never the FAIL code 1
        print('error: %s: %s' % (type(e).__name__, e))
        return 2
    res = fit(lum, cells, min_ratio)
    print_fit(res, what)
    print('RESULT: %s' % ('PASS' if res['pass'] else 'FAIL'))
    if opts.get('json'):
        with open(opts['json'], 'w') as f:
            json.dump(dict(res, what=what), f, indent=1)
    return 0 if res['pass'] else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
