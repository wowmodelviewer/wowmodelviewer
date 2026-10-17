"""Stage 08: where the 3 x 3 city region is (docs/world-viewer/08-streaming.md).

  python -I world08_density.py <WoW install root> <audit data folder> [client ...]

For each client (default: wow wow_classic_era wow_classic wow_classic_beta) it extracts, with bin/casc_extract.exe, the
Azeroth WDT (775971) and the obj0 ADT of every tile in columns 28-34, rows 44-51 (Stormwind and Elwynn Forest), counts
per tile the MODF world-model placements (flag 0x8: by FileDataID) and the MDDF doodad records (by uniqueId), and per
3 x 3 window the placements, the unique world models and the doodads. Writes <audit data>/world08/density.json and
prints the windows by doodad count.
"""
import collections
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wowfmt import adt as A  # noqa: E402
from wowfmt.chunks import walk  # noqa: E402
from wowfmt.wdt import WDT  # noqa: E402

AZEROTH = 775971
COLS, ROWS = range(28, 35), range(44, 52)


def extract(exe, data, product, out, ids):
    ids = sorted(set(i for i in ids if i))
    os.makedirs(out, exist_ok=True)
    lst = os.path.join(out, '_ids.txt')
    with open(lst, 'w') as f:
        f.write('\n'.join(str(i) for i in ids) + '\n')
    subprocess.run([exe, data, product, out, '-', lst], check=True, capture_output=True)


def main():
    install, audit = sys.argv[1], sys.argv[2]
    clients = sys.argv[3:] or ['wow', 'wow_classic_era', 'wow_classic', 'wow_classic_beta']
    exe = os.path.join(audit, 'bin', 'casc_extract.exe')
    data = os.path.join(install, 'Data')
    result = {}
    for c in clients:
        out = os.path.join(audit, 'world08', c)
        extract(exe, data, c, out, [AZEROTH])
        wdt = WDT(open(os.path.join(out, '%d.bin' % AZEROTH), 'rb').read())
        obj0 = {}
        for (x, y, main_words, maid) in wdt.tiles():
            if x in COLS and y in ROWS and len(maid) > 1 and maid[1]:
                obj0[(x, y)] = maid[1]
        extract(exe, data, c, out, list(obj0.values()))
        tiles = {}
        for (x, y), fid in obj0.items():
            p = os.path.join(out, '%d.bin' % fid)
            if not os.path.exists(p):
                continue
            b = open(p, 'rb').read()
            w = walk(b)
            mddf = A.mddf(b, w.one('MDDF')) if w.one('MDDF') else []
            modf = A.modf(b, w.one('MODF')) if w.one('MODF') else []
            tiles['%d_%d' % (x, y)] = {
                'obj0': fid,
                'mddf_uids': sorted(set(r['unique_id'] for r in mddf)),
                'modf_uids': sorted(set(r['unique_id'] for r in modf)),
                'wmo_fids': sorted(set(r['name_id'] for r in modf if r['flags'] & 8)),
            }
        windows = []
        for cx in COLS:
            for cy in ROWS:
                names = ['%d_%d' % (cx + dx, cy + dy) for dy in (-1, 0, 1) for dx in (-1, 0, 1)]
                if not all(n in tiles for n in names):
                    continue
                mddf, modf, wmos = set(), set(), set()
                for n in names:
                    mddf.update(tiles[n]['mddf_uids'])
                    modf.update(tiles[n]['modf_uids'])
                    wmos.update(tiles[n]['wmo_fids'])
                windows.append({'centre': '%d_%d' % (cx, cy), 'mddf': len(mddf), 'modf': len(modf), 'wmos': len(wmos)})
        windows.sort(key=lambda w: -w['mddf'])
        result[c] = {'tiles': tiles, 'windows': windows}
        print(c, 'tiles', len(tiles))
        for w in windows[:6]:
            print('   centre %-6s MDDF uniqueIds %5d  MODF uniqueIds %4d  world models %3d' % (w['centre'], w['mddf'], w['modf'], w['wmos']))
    with open(os.path.join(audit, 'world08', 'density.json'), 'w') as f:
        json.dump(result, f, indent=1, sort_keys=True)


if __name__ == '__main__':
    main()
