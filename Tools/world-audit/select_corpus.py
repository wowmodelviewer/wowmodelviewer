"""Per-tile metrics of an audited window, and the scenario tiles chosen from them by fixed rules (reproducible).

  python select_corpus.py <corpus folder> <areas_<client>.json> <out.json>

Rules (ties broken by the smaller x, then y):
  ordinary outdoor -- the tile whose main zone is the most common zone of the window's tiles, with the fewest of
                      (hole cells + liquid chunks + WMO placements), and at least 200 doodads;
  holes/layers     -- the most high-resolution hole cells (then the most 4-layer chunks);
  water            -- the most chunks with MH2O liquid among tiles with at least 100 doodads (a shore, not open sea);
  dense structures -- the most WMO placements (MODF), then the most doodads (MDDF).
"""
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from wowfmt import adt as A  # noqa: E402


def popcount(v):
    return bin(v).count('1')


def main(folder, areas_path, out_path):
    areas = json.load(open(areas_path))
    corpus = json.load(open(os.path.join(folder, 'corpus.json')))
    tiles = collections.defaultdict(dict)
    for f in corpus['files']:
        if 'x' in f and f['status'] == 'OK':
            tiles[(f['x'], f['y'])][f['what']] = (os.path.join(folder, '%d.bin' % f['fdid']), f['fdid'], f['bytes'])
    metrics = {}
    for (x, y), parts in sorted(tiles.items()):
        m = {'x': x, 'y': y, 'bytes': {k: v[2] for k, v in parts.items()}, 'fdids': {k: v[1] for k, v in parts.items()}}
        if 'root' in parts:
            buf = open(parts['root'][0], 'rb').read()
            r = A.SplitFile(buf, root=True)
            area = collections.Counter(h['area_id'] for c, h, s in r.mcnk if h)
            m['main_area'] = area.most_common(1)[0][0]
            m['main_area_name'] = (areas.get(str(m['main_area'])) or [None, None])[1]
            m['areas'] = {str(a): n for a, n in area.most_common()}
            m['hole_cells'] = sum(popcount(h['holes_high']) for c, h, s in r.mcnk if h and h['flags'] & 0x10000)
            m['chunks_with_holes'] = sum(1 for c, h, s in r.mcnk if h and h['flags'] & 0x10000 and h['holes_high'])
            mh = r.walk.one('MH2O')
            liq = A.mh2o(buf, mh) if mh else None
            m['liquid_chunks'] = sum(1 for c in liq['chunks'] if c['layers']) if liq else 0
            m['liquid_instances'] = sum(len(c['instances']) for c in liq['chunks']) if liq else 0
            m['mccv_chunks'] = sum(1 for c, h, s in r.mcnk if s and s.one('MCCV'))
        if 'tex0' in parts:
            buf = open(parts['tex0'][0], 'rb').read()
            t = A.SplitFile(buf, root=False)
            m['textures'] = len(A.u32s(buf, t.walk.one('MDID'))) or len(A.strings(buf, t.walk.one('MTEX')))
            m['height_textures'] = len([v for v in A.u32s(buf, t.walk.one('MHID')) if v])
            layers = collections.Counter(len(A.mcly(buf, s.one('MCLY'))) for c, h, s in t.mcnk)
            m['chunks_by_layers'] = {str(k): v for k, v in sorted(layers.items())}
        if 'obj0' in parts:
            buf = open(parts['obj0'][0], 'rb').read()
            o = A.SplitFile(buf, root=False)
            md = A.mddf(buf, o.walk.one('MDDF'))
            mo = A.modf(buf, o.walk.one('MODF'))
            m['mddf'] = len(md)
            m['m2_unique'] = len({d['name_id'] for d in md})
            m['modf'] = len(mo)
            m['wmo_unique'] = len({w['name_id'] for w in mo})
            m['m2_fdids'] = sorted({d['name_id'] for d in md})
            m['wmo_fdids'] = sorted({w['name_id'] for w in mo})
        metrics['%d_%d' % (x, y)] = m

    def key_xy(m):
        return (m['x'], m['y'])
    ms = [m for m in metrics.values() if 'root' in m['bytes'] and 'mddf' in m]
    zone = collections.Counter(m['main_area'] for m in ms).most_common(1)[0][0]
    ordinary = min([m for m in ms if m['main_area'] == zone and m['mddf'] >= 200] or ms,
                   key=lambda m: (m['hole_cells'] + m['liquid_chunks'] + m['modf'], key_xy(m)))
    holes = max(ms, key=lambda m: (m['hole_cells'], m['chunks_by_layers'].get('4', 0), -m['x'], -m['y']))
    water = max([m for m in ms if m['mddf'] >= 100] or ms, key=lambda m: (m['liquid_chunks'], -m['x'], -m['y']))
    dense = max(ms, key=lambda m: (m['modf'], m['mddf'], -m['x'], -m['y']))
    pick = {'ordinary_outdoor': ordinary, 'holes_layers': holes, 'water': water, 'dense_structures': dense}
    out = {'corpus': folder, 'window_main_zone': zone,
           'scenarios': {k: {'x': v['x'], 'y': v['y'], 'main_area': v['main_area'], 'main_area_name': v['main_area_name']}
                         for k, v in pick.items()},
           'tiles': metrics}
    json.dump(out, open(out_path, 'w'), indent=1)
    for k, v in pick.items():
        print('%-17s %d_%d  %-22s holes %4d  liquid chunks %3d  textures %3s  MDDF %5d (%3d unique)  MODF %3d (%2d unique)' % (
            k, v['x'], v['y'], v['main_area_name'], v['hole_cells'], v['liquid_chunks'], v.get('textures'), v['mddf'],
            v['m2_unique'], v['modf'], v['wmo_unique']))


if __name__ == '__main__':
    main(*sys.argv[1:4])
