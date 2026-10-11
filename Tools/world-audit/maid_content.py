"""MAID word roles and Map.db2's WdtFileDataID column, proven without the listfile (the community listfile partly
derives its map-file names from MAID positions and Map.db2 rows, so agreeing with it is not independent proof).

  python maid_content.py <data folder> <out.json>

1. MAID word roles by content: for every corpus tile, what the file each MAID word points at actually contains --
   its first chunks (MHDR = root, MDDF/MODF = obj0, MLDD/MLMD = obj1, MAMP/MDID = tex0, MLHD = lod) or, for images,
   the BLP2 header (size, compression). The tile number of word 0 is checked separately by coords.py (every MCNK
   position against the MAID index).
2. Map.db2 field 21: Classic Era and MoP Classic storages open WDTs by path (their root has name hashes); for those
   directories the FileDataID they resolve to is known without any listfile (same bytes opened by path and by id,
   audit_wdt.py). Retail and Classic Beta rows with the same directory must carry that FileDataID in field 21.
"""
import collections
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from wowfmt.chunks import walk  # noqa: E402
from wowfmt.wdt import WDT  # noqa: E402

ROLE_BY_CHUNKS = [('MHDR', 'root'), ('MLHD', 'lod'), ('MAMP', 'tex0'), ('MDID', 'tex0'), ('MLDD', 'obj1'),
                  ('MLMD', 'obj1'), ('MDDF', 'obj0'), ('MODF', 'obj0')]


def role_of(buf):
    if buf[:4] == b'BLP2':
        comp, alpha_depth, alpha_type, mips = buf[8], buf[9], buf[10], buf[11]
        w, h = struct.unpack_from('<II', buf, 12)
        return 'BLP2 %dx%d compression %d alpha type %d' % (w, h, comp, alpha_type)
    ids = [c.id for c in walk(buf).chunks[:12]]
    for cid, role in ROLE_BY_CHUNKS:
        if cid in ids:
            return role + ' (has %s)' % cid
    return 'unknown: ' + ' '.join(ids[:4])


def main(data, out_path):
    out = {'maid_roles': {}, 'field21': {}}
    for client in ('wow', 'wow_classic_beta', 'wow_classic_era', 'wow_classic'):
        folder = os.path.join(data, 'corpus', client, 'azeroth')
        cj = json.load(open(os.path.join(folder, 'corpus.json')))
        w = WDT(open(cj['wdt'], 'rb').read())
        index = {(x, y): maid for x, y, main, maid in w.tiles()}
        roles = collections.defaultdict(collections.Counter)
        tiles = {(f['x'], f['y']) for f in cj['files'] if 'x' in f}
        for (x, y) in tiles:
            for k, fid in enumerate(index[(x, y)]):
                if not fid:
                    roles[k]['(zero)'] += 1
                    continue
                path = os.path.join(folder, '%d.bin' % fid)
                if not os.path.exists(path):
                    roles[k]['(not extracted)'] += 1
                    continue
                roles[k][role_of(open(path, 'rb').read(512 * 1024))] += 1
        out['maid_roles'][client] = {k: dict(v) for k, v in sorted(roles.items())}

    # Field 21 against the name-hash resolution of the path-capable clients.
    by_name = {}
    for client in ('wow_classic_era', 'wow_classic'):
        cen = json.load(open(os.path.join(data, 'catalog', 'wdt_%s.json' % client)))
        for e in cen['name_vs_fdid_same_bytes']:
            if e['same_bytes']:
                d = e['path'].split('/')[2]
                by_name[d.lower()] = e['fdid']
    for client in ('wow', 'wow_classic_beta'):
        cat = json.load(open(os.path.join(data, 'catalog', 'maps_%s.json' % client)))
        agree = disagree = 0
        examples = []
        for m in cat['maps']:
            if m['zero_filled'] or not m.get('directory'):
                continue
            want = by_name.get(m['directory'].lower())
            if want is None:
                continue
            if m['wdt_fdid'] == want:
                agree += 1
            else:
                disagree += 1
                examples.append((m['id'], m['directory'], m['wdt_fdid'], want))
        out['field21'][client] = {'directories_resolved_by_name_hash_elsewhere': agree + disagree, 'field21_agrees': agree,
                                  'field21_disagrees': disagree, 'disagreements': examples[:10]}
    json.dump(out, open(out_path, 'w'), indent=1)
    print(json.dumps(out, indent=1))


if __name__ == '__main__':
    main(*sys.argv[1:3])
