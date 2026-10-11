"""Side files of a map corpus: WDL (tested against the ADT heights), and the chunk inventory of _lgt, _occ, _fogs,
_mpv, .tex and .pd4 files (contents listed, not interpreted).

  python audit_side.py <corpus folder> <out.json>

WDL: MAOF holds 64x64 offsets; at each, MARE holds 17x17 + 16x16 int16 values. The tests, for every corpus tile:
the 17x17 grid against the ADT height at each chunk corner (outer vertex 0 of chunk (ix, iy)), for both orientations,
and its far row/column (index 16) against the last outer vertices of the chunks on the tile's far edges; the 16x16
grid against candidate chunk statistics (centre vertex, mean of all 145 heights). MAHO is listed with its size only.
"""
import collections
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from wowfmt import adt as A  # noqa: E402
from wowfmt.chunks import sha256, walk  # noqa: E402


def main(folder, out_path):
    corpus = json.load(open(os.path.join(folder, 'corpus.json')))
    side = {f['what']: f for f in corpus['files'] if 'x' not in f}
    tiles = collections.defaultdict(dict)
    for f in corpus['files']:
        if 'x' in f and f['status'] == 'OK':
            tiles[(f['x'], f['y'])][f['what']] = os.path.join(folder, '%d.bin' % f['fdid'])
    out = {'corpus': folder, 'side': {}}
    for what, f in sorted(side.items()):
        e = {'fdid': f['fdid'], 'status': f['status'], 'bytes': f['bytes']}
        if f['status'] == 'OK':
            b = open(os.path.join(folder, '%d.bin' % f['fdid']), 'rb').read()
            w = walk(b)
            e['sha256'] = sha256(b)
            e['chunks'] = w.ids()
            e['chunk_sizes'] = {c.id: c.size for c in w.chunks[:40]}
            e['problems'] = w.problems
        out['side'][what] = e

    wdl = side.get('side:wdl')
    if wdl and wdl['status'] == 'OK':
        b = open(os.path.join(folder, '%d.bin' % wdl['fdid']), 'rb').read()
        w = walk(b)
        maof = w.one('MAOF')
        offsets = struct.unpack_from('<4096I', b, maof.data_offset) if maof else []
        out['wdl_tiles_with_heights'] = sum(1 for v in offsets if v)
        fits = {'outer[r][c]: r along -x (index_y), c along -y (index_x)': collections.Counter(),
                'outer[r][c]: r along -y (index_x), c along -x (index_y)': collections.Counter()}
        resid = {k: [] for k in fits}
        side = collections.defaultdict(list)
        for (tx, ty), parts in tiles.items():
            if 'root' not in parts:
                continue
            ofs = offsets[ty * 64 + tx] if offsets else 0
            if not ofs:
                continue
            # MARE chunk at that offset (absolute in the file).
            cid = b[ofs:ofs + 4][::-1].decode('latin-1')
            if cid != 'MARE':
                out.setdefault('wdl_problems', []).append('tile %d_%d: %s at MAOF offset' % (tx, ty, cid))
                continue
            outer = struct.unpack_from('<289h', b, ofs + 8)
            buf = open(parts['root'], 'rb').read()
            r = A.SplitFile(buf, root=True)
            corner = {}
            for c, h, sub in r.mcnk:
                hv = A.mcvt(buf, sub.one('MCVT'))
                if hv:
                    corner[(h['index_x'], h['index_y'])] = h['position'][2] + hv[0]
            for (ix, iy), z in corner.items():
                a = outer[iy * 17 + ix]
                bb = outer[ix * 17 + iy]
                resid['outer[r][c]: r along -x (index_y), c along -y (index_x)'].append(abs(a - z))
                resid['outer[r][c]: r along -y (index_x), c along -x (index_y)'].append(abs(bb - z))
            inner = struct.unpack_from('<256h', b, ofs + 8 + 289 * 2)
            for c, h, sub in r.mcnk:
                hv = A.mcvt(buf, sub.one('MCVT'))
                if not hv:
                    continue
                ix, iy, z0 = h['index_x'], h['index_y'], h['position'][2]
                v = inner[iy * 16 + ix]
                centre = z0 + hv[4 * 17 + 4]
                mean = z0 + sum(hv) / 145.0
                side['16x16 = centre vertex'].append(abs(v - centre))
                side['16x16 = truncated mean of the 145 heights'].append(abs(v - int(mean)))
                # far edges: row 16 / column 16 against the last outer vertex of the edge chunks
                if iy == 15:
                    side['17x17 far row (index 16)'].append(abs(outer[16 * 17 + ix] - (z0 + hv[8 * 17 + 0])))
                if ix == 15:
                    side['17x17 far column (index 16)'].append(abs(outer[iy * 17 + 16] - (z0 + hv[0 * 17 + 8])))
        out['wdl_vs_adt'] = {}
        for k, v in resid.items():
            v.sort()
            out['wdl_vs_adt'][k] = {'corners': len(v), 'median_abs_difference': v[len(v) // 2] if v else None,
                                    'within_1_yard': sum(1 for d in v if d <= 1.0), 'max': v[-1] if v else None}
        for k, v in side.items():
            v.sort()
            out['wdl_vs_adt'][k] = {'samples': len(v), 'median_abs_difference': v[len(v) // 2] if v else None,
                                    'within_1_yard': sum(1 for d in v if d <= 1.0), 'max': v[-1] if v else None}
    json.dump(out, open(out_path, 'w'), indent=1)
    print(json.dumps({k: out[k] for k in out if k != 'side'}, indent=1))
    for k, v in out['side'].items():
        print('%-12s %s %s %s' % (k, v['status'], v.get('bytes'), v.get('chunks')))


if __name__ == '__main__':
    main(*sys.argv[1:3])
