"""Coordinate and layout tests on an extracted corpus -- each question answered by trying candidate rules against the
bytes and reporting how well each fits, so the winner is measured, not assumed.

  python coords.py <corpus folder> <out.json>

1. MCVT outer vertex order: shared points of neighbouring chunks/tiles must carry the same height (all 8 symmetries
   of the 9x9 grid are tried). Seam x/y exactness is checked separately, without rounding: vertex x/y from the stored
   MCNK position against x/y from the integer vertex index.
2. MCVT inner vertices: the inner vertex of each cell against the mean of its four corners, for the claimed index and
   its transposed/mirrored alternatives (a wrong index pairs an inner value with the wrong cell).
3. Placement position: doodads must sit on the terrain under them -- the exact surface (four triangles per cell fanned
   to the inner vertex), signed residual. (Alternatives that put doodads outside their own tile are listed too.)
4. High-resolution hole bits: all 8 symmetries of the 8x8 grid and the whole-word MSB-first order, scored (a) by hole
   cells inside a WMO's stored world box (MODF extents, which reproduce the placed model exactly: rotation.py), over
   all cells and over the cells that can discriminate (chunks not wholly inside a box), and (b) without WMOs, by how
   many hole cells continue into the neighbouring chunk across a chunk or tile border. The whole-word MSB-first order
   reads bit 63-(r*8+c), which is the same cell as 'rotate 180' (bit (7-r)*8+(7-c)), so the list holds 8 distinct
   orders, not 9; the two score identically by construction.
5. MCAL orientation: the alpha of the same texture along both kinds of chunk edge (all 8 symmetries); 4-bit nibble
   order tested where it can show: across the column edge and across byte boundaries inside a row.
"""
import collections
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from wowfmt import adt as A  # noqa: E402

T = 1600.0 / 3.0
C = T / 16.0
U = C / 8.0


def f32(v):
    return struct.unpack('<f', struct.pack('<f', v))[0]


def sym(n):
    """The 8 symmetries of an n x n grid: (name, f(r, c) -> (r', c'))."""
    m = n - 1
    return [('identity', lambda r, c: (r, c)), ('transpose', lambda r, c: (c, r)),
            ('mirror rows', lambda r, c: (m - r, c)), ('mirror columns', lambda r, c: (r, m - c)),
            ('rotate 180', lambda r, c: (m - r, m - c)), ('transpose + mirror rows', lambda r, c: (m - c, r)),
            ('transpose + mirror columns', lambda r, c: (c, m - r)), ('anti-transpose', lambda r, c: (m - c, m - r))]


def load(folder):
    corpus = json.load(open(os.path.join(folder, 'corpus.json')))
    tiles = collections.defaultdict(dict)
    for f in corpus['files']:
        if 'x' in f and f['status'] in ('OK', 'ENCRYPTED_PARTIAL'):
            tiles[(f['x'], f['y'])][f['what']] = os.path.join(folder, '%d.bin' % f['fdid'])
    return tiles


FOUR_BIT = [0]


def decode_layers(buf, sub, nibble_low_first=True):
    """MCLY layers and their 64x64 alpha (None where there is none). 4-bit maps: 2048 bytes, two texels per byte."""
    layers = A.mcly(buf, sub.one('MCLY'))
    mcal = sub.one('MCAL')
    alphas = []
    for k, L in enumerate(layers[1:], 1):
        if not mcal:
            alphas.append(None)
            continue
        start = mcal.data_offset + L['alpha_offset']
        if L['flags'] & 0x200:
            vals, _ = A.rle_alpha(buf, start, mcal.data_offset + mcal.size)
        else:
            end = mcal.data_offset + (layers[k + 1]['alpha_offset'] if k + 1 < len(layers) else mcal.size)
            raw = buf[start:end]
            if len(raw) == 4096:
                vals = raw
            elif len(raw) == 2048:
                FOUR_BIT[0] += 1
                if nibble_low_first:
                    vals = bytes(v for b in raw for v in (b & 15, b >> 4))
                else:
                    vals = bytes(v for b in raw for v in (b >> 4, b & 15))
            else:
                vals = None
        alphas.append(vals)
    return layers, alphas


def main(folder, out_path):
    tiles = load(folder)
    chunks = {}        # (tx, ty, ix, iy) -> (header, heights, layers, alphas low-first, alphas high-first, is4bit)
    placements = {}
    for (tx, ty), parts in tiles.items():
        if 'root' not in parts:
            continue
        buf = open(parts['root'], 'rb').read()
        r = A.SplitFile(buf, root=True)
        tex = A.SplitFile(open(parts['tex0'], 'rb').read(), root=False) if 'tex0' in parts else None
        for n, (c, h, sub) in enumerate(r.mcnk):
            hv = A.mcvt(buf, sub.one('MCVT'))
            layers, lo, hi4 = [], [], []
            if tex and n < len(tex.mcnk):
                tsub = tex.mcnk[n][2]
                layers, lo = decode_layers(tex.buf, tsub, True)
                _, hi4 = decode_layers(tex.buf, tsub, False)
            chunks[(tx, ty, h['index_x'], h['index_y'])] = (h, hv, layers, lo, hi4)
        if 'obj0' in parts:
            ob = open(parts['obj0'], 'rb').read()
            o = A.SplitFile(ob, root=False)
            placements[(tx, ty)] = (A.mddf(ob, o.walk.one('MDDF')), A.modf(ob, o.walk.one('MODF')))
    result = {'corpus': folder, 'tile_size': T, 'chunk_size': C, 'unit': U, 'chunks': len(chunks)}

    # Global outer-vertex grid: row R advances with index_y (x falls), column advances with index_x (y falls).
    def gkey(tx, ty, ix, iy, r, c):
        return (ty * 128 + iy * 8 + r, tx * 128 + ix * 8 + c)

    # 1. Outer vertex order: which symmetry of the 9x9 grid makes shared points agree.
    vres = {}
    for name, s in sym(9):
        pts = collections.defaultdict(list)
        for (tx, ty, ix, iy), (h, hv, _, _, _) in chunks.items():
            if not hv:
                continue
            for r in range(9):
                for c in range(9):
                    rr, cc = s(r, c)
                    pts[gkey(tx, ty, ix, iy, r, c)].append((h['position'][2] + hv[rr * 17 + cc], (tx, ty)))
        shared = [v for v in pts.values() if len(v) > 1]
        diffs = [max(z for z, _ in v) - min(z for z, _ in v) for v in shared]
        cross = [d for d, v in zip(diffs, shared) if len({t for _, t in v}) > 1]
        vres[name] = {'shared_points': len(shared), 'differing_over_1mm': sum(1 for d in diffs if d > 0.001),
                      'max_difference': max(diffs) if diffs else None, 'tile_seam_points': len(cross),
                      'tile_seam_differing_over_1mm': sum(1 for d in cross if d > 0.001)}
    result['outer_vertex_order'] = vres
    # Seam x/y: stored MCNK position vs the index formula; and float32 seam coincidence when built from positions.
    off = []
    pos_exact = 0
    for (tx, ty, ix, iy), (h, hv, _, _, _) in chunks.items():
        ex = (32 - ty) * T - iy * C
        ey = (32 - tx) * T - ix * C
        off.append(max(abs(h['position'][0] - ex), abs(h['position'][1] - ey)))
        pos_exact += (h['position'][0] == f32(ex)) + (h['position'][1] == f32(ey))
    seam_pairs = seam_bit_equal = 0
    for (tx, ty, ix, iy), (h, hv, _, _, _) in chunks.items():
        nb = chunks.get((tx, ty, ix, iy + 1))
        if nb:
            seam_pairs += 1
            seam_bit_equal += f32(f32(h['position'][0]) - f32(8 * U)) == f32(nb[0]['position'][0])
    result['seam_xy'] = {'stored_position_vs_formula_max_yd': max(off), 'stored_values_equal_float32_of_formula':
                         pos_exact, 'stored_values': 2 * len(chunks), 'seams_tested': seam_pairs,
                         'seams_bit_equal_when_built_from_stored_position_in_float32': seam_bit_equal}

    # 2. Inner vertices against the mean of their cell's corners.
    ires = {}
    for name, s in sym(8):
        d = []
        for (h, hv, _, _, _) in chunks.values():
            if not hv:
                continue
            for i in range(8):
                for j in range(8):
                    ii, jj = s(i, j)
                    corners = [hv[i * 17 + j], hv[i * 17 + j + 1], hv[(i + 1) * 17 + j], hv[(i + 1) * 17 + j + 1]]
                    d.append(abs(hv[ii * 17 + 9 + jj] - sum(corners) / 4.0))
        d.sort()
        ires[name] = {'median_yd': d[len(d) // 2], 'mean_yd': sum(d) / len(d)}
    result['inner_vertex_index'] = ires

    # Exact terrain surface: four triangles per cell, fanned to the inner vertex.
    def ground(x, y):
        gr, gc = (32 * T - x) / U, (32 * T - y) / U
        R, Cc = int(math.floor(gr)), int(math.floor(gc))
        fr, fc = gr - R, gc - Cc
        ty_, iy, r = R // 128, (R % 128) // 8, R % 8
        tx_, ix, c = Cc // 128, (Cc % 128) // 8, Cc % 8
        ch = chunks.get((tx_, ty_, ix, iy))
        if not ch or not ch[1]:
            return None
        h, hv = ch[0], ch[1]
        z0 = h['position'][2]
        a, b = z0 + hv[r * 17 + c], z0 + hv[r * 17 + c + 1]
        cc_, d = z0 + hv[(r + 1) * 17 + c], z0 + hv[(r + 1) * 17 + c + 1]
        m = z0 + hv[r * 17 + 9 + c]
        # triangle containing (fr, fc): split by the two diagonals of the cell
        u, v = fr - 0.5, fc - 0.5
        if abs(u) >= abs(v):
            p, q = ((a, b) if u < 0 else (cc_, d))
            t = (v / (2 * abs(u)) + 0.5) if u != 0 else 0.5
            edge = p + (q - p) * t
            return m + (edge - m) * (2 * abs(u))
        p, q = ((a, cc_) if v < 0 else (b, d))
        t = (u / (2 * abs(v)) + 0.5) if v != 0 else 0.5
        edge = p + (q - p) * t
        return m + (edge - m) * (2 * abs(v))

    # 3. Placement position (doodads counted once per uniqueId).
    seen = set()
    inside = total = 0
    signed = []
    for (tx, ty), (md, mo) in placements.items():
        for dd in md:
            if dd['unique_id'] in seen:
                continue
            seen.add(dd['unique_id'])
            p = dd['position']
            X, Y, Z = 32 * T - p[2], 32 * T - p[0], p[1]
            total += 1
            g = ground(X, Y)
            if g is not None:
                inside += 1
                signed.append(Z - g)
    absd = sorted(abs(v) for v in signed)
    signed.sort()
    result['placement'] = {'rule': 'X = 32T - p[2], Y = 32T - p[0], Z = p[1]', 'unique_doodads': total,
                           'over_terrain_in_corpus': inside, 'median_signed_yd': signed[len(signed) // 2],
                           'median_abs_yd': absd[len(absd) // 2], 'within_0_1_yd': sum(1 for v in absd if v <= 0.1),
                           'within_2_yd': sum(1 for v in absd if v <= 2.0), 'above': sum(1 for v in signed if v > 0.01),
                           'below': sum(1 for v in signed if v < -0.01)}

    # 4. Hole bits.
    boxes = collections.defaultdict(list)
    for (tx, ty), (md, mo) in placements.items():
        for w in mo:
            e = w['extents']
            # extents are in the placement frame; to world: X = 32T - e[2], Y = 32T - e[0]
            boxes[(tx, ty)].append((32 * T - e[5], 32 * T - e[2], 32 * T - e[3], 32 * T - e[0]))

    def in_box(t, x, y):
        return any(b[0] <= x <= b[1] and b[2] <= y <= b[3] for b in boxes[t])

    hole_chunks = {k: v for k, v in chunks.items() if v[0]['flags'] & 0x10000 and v[0]['holes_high']}
    orders = [(name, s) for name, s in sym(8)] + [('whole word MSB first', None)]

    def bit(word, r, c, s):
        if s is None:
            return (word >> (63 - (r * 8 + c))) & 1
        rr, cc = s(r, c)
        return (word >> (rr * 8 + cc)) & 1

    discriminating = set()
    for (tx, ty, ix, iy), ch in hole_chunks.items():
        px, py, _ = ch[0]['position']
        if not all(in_box((tx, ty), px - (r + 0.5) * U, py - (c + 0.5) * U) for r in range(8) for c in range(8)):
            discriminating.add((tx, ty, ix, iy))
    hres = {}
    for name, s in orders:
        cells = cells_in = dcells = dcells_in = 0
        hole_set = set()
        for key, ch in hole_chunks.items():
            tx, ty, ix, iy = key
            px, py, _ = ch[0]['position']
            for r in range(8):
                for c in range(8):
                    if bit(ch[0]['holes_high'], r, c, s):
                        x, y = px - (r + 0.5) * U, py - (c + 0.5) * U
                        ok = in_box((tx, ty), x, y)
                        cells += 1
                        cells_in += ok
                        if key in discriminating:
                            dcells += 1
                            dcells_in += ok
                        hole_set.add(gkey(tx, ty, ix, iy, r, c))
        # continuity across chunk borders (cells on a chunk edge whose neighbour cell is also a hole)
        cross = 0
        for (R, Cc) in hole_set:
            for dR, dC in ((1, 0), (0, 1)):
                n = (R + dR, Cc + dC)
                if n in hole_set and ((R % 8 == 7 and dR) or (Cc % 8 == 7 and dC)):
                    cross += 1
        hres[name] = {'hole_cells': cells, 'inside_wmo_box': cells_in, 'cells_in_discriminating_chunks': dcells,
                      'discriminating_cells_inside': dcells_in, 'cross_chunk_hole_adjacencies': cross}
    blocky = sum(1 for ch in hole_chunks.values() if all(
        ((ch[0]['holes_high'] >> (r * 8 + c)) & 1) == ((ch[0]['holes_high'] >> ((r - r % 2) * 8 + (c - c % 2))) & 1)
        for r in range(8) for c in range(8)))
    result['holes'] = {'chunks_with_holes': len(hole_chunks), 'discriminating_chunks': len(discriminating),
                       'masks_made_of_whole_2x2_blocks': blocky, 'orders': hres}

    # 5. Alpha orientation on both edges; 4-bit nibble order.
    def edge_stats(s, use_hi=False, edge='rows'):
        diffs = []
        for (tx, ty, ix, iy), ch in chunks.items():
            nb = chunks.get((tx, ty, ix, iy + 1) if edge == 'rows' else (tx, ty, ix + 1, iy))
            if not nb:
                continue
            here = {L['texture']: a for L, a in zip(ch[2][1:], ch[4] if use_hi else ch[3]) if a}
            there = {L['texture']: a for L, a in zip(nb[2][1:], nb[4] if use_hi else nb[3]) if a}
            for t, a in here.items():
                b = there.get(t)
                if not b:
                    continue
                for k in range(64):
                    if edge == 'rows':
                        r1, c1 = s(63, k)
                        r2, c2 = s(0, k)
                    else:
                        r1, c1 = s(k, 63)
                        r2, c2 = s(k, 0)
                    diffs.append(abs(a[r1 * 64 + c1] - b[r2 * 64 + c2]))
        diffs.sort()
        return {'samples': len(diffs), 'median': diffs[len(diffs) // 2] if diffs else None,
                'mean': round(sum(diffs) / len(diffs), 3) if diffs else None}

    ares = {}
    for name, s in sym(64):
        ares[name] = {'edge across index_y (vertex rows)': edge_stats(s, edge='rows'),
                      'edge across index_x (vertex columns)': edge_stats(s, edge='columns')}
    result['alpha_orientation'] = ares
    is4 = FOUR_BIT[0] > 0
    if is4:
        ident = sym(64)[0][1]
        nib = {}
        for label, hi in (('low nibble first', False), ('high nibble first', True)):
            within = []
            for ch in chunks.values():
                for a in (ch[4] if hi else ch[3]):
                    if not a:
                        continue
                    for r in range(64):
                        for c in range(1, 63, 2):          # pairs that straddle a byte boundary (c, c+1)
                            within.append(abs(a[r * 64 + c] - a[r * 64 + c + 1]))
            within.sort()
            nib[label] = {'column_edge': edge_stats(ident, use_hi=hi, edge='columns'),
                          'across_byte_boundaries_mean': round(sum(within) / len(within), 3)}
        result['nibble_order_4bit'] = nib
    json.dump(result, open(out_path, 'w'), indent=1)
    summary = {k: result[k] for k in ('seam_xy', 'placement')}
    summary['outer_vertex_order (differing_over_1mm)'] = {k: v['differing_over_1mm'] for k, v in vres.items()}
    summary['inner_vertex_index (median)'] = {k: round(v['median_yd'], 3) for k, v in ires.items()}
    summary['holes'] = {k: (v['inside_wmo_box'], v['hole_cells'], v['discriminating_cells_inside'],
                            v['cells_in_discriminating_chunks'], v['cross_chunk_hole_adjacencies'])
                        for k, v in hres.items()}
    summary['holes_meta'] = {k: result['holes'][k] for k in ('chunks_with_holes', 'discriminating_chunks',
                                                             'masks_made_of_whole_2x2_blocks')}
    summary['alpha (median/mean rows, median/mean columns)'] = {
        k: (v['edge across index_y (vertex rows)']['median'], v['edge across index_y (vertex rows)']['mean'],
            v['edge across index_x (vertex columns)']['median'], v['edge across index_x (vertex columns)']['mean'])
        for k, v in ares.items()}
    if is4:
        summary['nibble_order_4bit'] = result['nibble_order_4bit']
    print(json.dumps(summary, indent=1))


if __name__ == '__main__':
    main(*sys.argv[1:3])
