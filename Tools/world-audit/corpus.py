"""Extracts a reproducible corpus of one map: its WDT, the MPHD side files (lgt, occ, fogs, mpv, tex, wdl, pd4) and,
for the tiles asked for, every MAID part (root, obj0, obj1, tex0, lod, map texture, map texture _n, minimap) -- all by
the FileDataIDs the client's own files give, never by a guessed name.

  python corpus.py <casc_extract.exe> <game folder> <product> <keys.csv> <WDT file> <out folder> <x0> <y0> <x1> <y1> [parts]

parts: comma list of MAID word indices to take (default all 8); the side files are always taken.
Writes <out>/manifest.tsv (from casc_extract) and <out>/corpus.json (which file is which: tile, part, FileDataID).
"""
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
from wowfmt.wdt import WDT  # noqa: E402

MAID_PARTS = ['root', 'obj0', 'obj1', 'tex0', 'lod', 'maptexture', 'maptexture_n', 'minimap']   # proven: audit_wdt.py
MPHD_SIDE = [None, 'lgt', 'occ', 'fogs', 'mpv', 'tex', 'wdl', 'pd4']                               # proven: audit_wdt.py


def main(exe, game, product, keys, wdt_path, out, x0, y0, x1, y1, parts=None):
    x0, y0, x1, y1 = int(x0), int(y0), int(x1), int(y1)
    take = [int(p) for p in parts.split(',')] if parts else list(range(8))
    w = WDT(open(wdt_path, 'rb').read())
    os.makedirs(out, exist_ok=True)
    items = []
    for k, word in enumerate(w.mphd_words[1:8], 1):
        if word:
            items.append({'fdid': word, 'what': 'side:' + MPHD_SIDE[k]})
    for x, y, main, maid in w.tiles():
        if x0 <= x <= x1 and y0 <= y <= y1 and maid:
            for k in take:
                if maid[k]:
                    items.append({'fdid': maid[k], 'what': MAID_PARTS[k], 'x': x, 'y': y})
    ids = os.path.join(out, 'ids.txt')
    with open(ids, 'w') as f:
        for it in items:
            f.write('%d\n' % it['fdid'])
    man = os.path.join(out, 'manifest.tsv')
    if os.path.exists(man):
        os.remove(man)
    subprocess.run([exe, game, product, out, keys, ids], check=True, stderr=subprocess.DEVNULL)
    status = {}
    with open(man) as f:
        for line in f:
            p = line.rstrip('\n').split('\t')
            status[p[0]] = (p[1], int(p[2]))
    for it in items:
        it['status'], it['bytes'] = status.get(str(it['fdid']), ('MISSING_FROM_MANIFEST', 0))
    json.dump({'product': product, 'wdt': wdt_path, 'window': [x0, y0, x1, y1], 'files': items},
              open(os.path.join(out, 'corpus.json'), 'w'), indent=1)
    counts = {}
    for it in items:
        counts[it['status']] = counts.get(it['status'], 0) + 1
    print('%s: %d files %s' % (out, len(items), counts))


if __name__ == '__main__':
    main(*sys.argv[1:])
