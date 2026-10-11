"""Measurements behind the renderer choice (custom mesh vs Unity Terrain), from the corpus:

  python terrain_needs.py <corpus folder> <out.json>

  - inner vertices: each chunk's 8x8 inner vertices against the mean of their four outer neighbours -- what a
    regular heightmap of the outer grid would put there (Unity Terrain has no place for them);
  - texture sets: distinct ordered layer lists per chunk, distinct textures per tile (a global layer list would
    need all of them);
  - vertex colours (MCCV): chunks that have them, and how many are not the neutral 0x7F;
  - a 257x257 heightmap per tile (the smallest that holds all 145 vertices: outer vertices on even nodes, inner on
    odd-odd nodes, the edge midpoints between filled by their two outer neighbours) still differs from the game's
    surface: each of its quarter-cells is split by one fixed diagonal, while the game fans every cell from its centre.
    At a quarter-cell's centre the difference is |2*inner - corner1 - corner2| / 4 for the quarter-cells whose
    diagonal disagrees; both fixed diagonal directions are measured.
"""
import collections
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from wowfmt import adt as A  # noqa: E402


def main(corpus, out_path):
    cj = json.load(open(os.path.join(corpus, 'corpus.json')))
    tiles = collections.defaultdict(dict)
    for f in cj['files']:
        if 'x' in f and f['status'] == 'OK':
            tiles[(f['x'], f['y'])][f['what']] = os.path.join(corpus, '%d.bin' % f['fdid'])
    dev = []
    grid257 = {'diagonal TL-BR': [], 'diagonal TR-BL': []}
    mccv_chunks = mccv_nonneutral = chunks = 0
    sets = collections.Counter()
    per_tile_textures = []
    for (x, y), parts in tiles.items():
        if 'root' not in parts:
            continue
        b = open(parts['root'], 'rb').read()
        r = A.SplitFile(b, root=True)
        for c, h, sub in r.mcnk:
            chunks += 1
            hv = A.mcvt(b, sub.one('MCVT'))
            if hv:
                for i in range(8):
                    for j in range(8):
                        o = [hv[i * 17 + j], hv[i * 17 + j + 1], hv[(i + 1) * 17 + j], hv[(i + 1) * 17 + j + 1]]
                        centre = hv[i * 17 + 9 + j]
                        dev.append(abs(centre - sum(o) / 4.0))
                        tl, tr, bl, br = o
                        # A fixed TL-BR diagonal disagrees with the fan in the TR and BL quarter-cells, and so on.
                        grid257['diagonal TL-BR'].append(abs(2 * centre - tl - br) / 4.0)
                        grid257['diagonal TR-BL'].append(abs(2 * centre - tr - bl) / 4.0)
            cv = sub.one('MCCV')
            if cv:
                mccv_chunks += 1
                vals = b[cv.data_offset:cv.data_offset + cv.size]
                if any(v not in (0x7F,) for k, v in enumerate(vals) if k % 4 != 3):
                    mccv_nonneutral += 1
        if 'tex0' in parts:
            tb = open(parts['tex0'], 'rb').read()
            t = A.SplitFile(tb, root=False)
            mdid = A.u32s(tb, t.walk.one('MDID'))
            used = set()
            for c, _, sub in t.mcnk:
                L = A.mcly(tb, sub.one('MCLY'))
                key = tuple(l['texture'] for l in L)
                sets[key] += 1
                used.update(key)
            per_tile_textures.append(len(used))
    dev.sort()
    out = {
        'chunks': chunks,
        'inner_vertex_vs_outer_mean': {'samples': len(dev), 'median': dev[len(dev) // 2], 'p95': dev[int(len(dev) * 0.95)],
                                       'max': dev[-1], 'over_0_5_yard': sum(1 for d in dev if d > 0.5),
                                       'over_2_yards': sum(1 for d in dev if d > 2.0)},
        'grid_257_vs_fan_at_quarter_cell_centres': {
            k: {'median': sorted(v)[len(v) // 2], 'p95': sorted(v)[int(len(v) * 0.95)], 'max': max(v),
                'over_0_25_yard': sum(1 for d in v if d > 0.25)} for k, v in grid257.items()},
        'mccv_chunks': mccv_chunks, 'mccv_not_neutral': mccv_nonneutral,
        'texture_sets_distinct_in_corpus': len(sets),
        'textures_used_per_tile': {'min': min(per_tile_textures), 'max': max(per_tile_textures),
                                   'median': sorted(per_tile_textures)[len(per_tile_textures) // 2]},
    }
    json.dump(out, open(out_path, 'w'), indent=1)
    print(json.dumps(out, indent=1))


if __name__ == '__main__':
    main(*sys.argv[1:3])
