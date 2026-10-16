"""Stage 06 corpus: the real client files the world-model placement tests read (docs/world-viewer/06-world-models-in-world.md).

  python -I world06_corpus.py <WoW install root> <audit data folder> [client ...]

For each client (default: wow wow_classic_era wow_classic wow_classic_beta) it extracts, with bin/casc_extract.exe:
  - the WDTs of map 0 (Azeroth, 775971), map 34 (Stormwind Stockade, 791060) and map 429 (Dire Maul, 780814);
  - tile 30_47's obj0 ADT (777623);
  - every world model those name (the obj0's MODF records, the WDTs' global MODF records): the root and its LOD0 group
    files (the first MOHD group-count entries of GFID);
and writes <audit data>/world06/<client>/<FileDataID>.bin plus <audit data>/world06/manifest.json: per client its build
(from the install's .build.info) and per file its role, size and SHA-256. The C# corpus tests check every file against the
manifest before they use it, so a client that updated is noticed, never read as a regression.
"""
import hashlib
import json
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wowfmt import adt as A  # noqa: E402
from wowfmt.chunks import walk  # noqa: E402
from wowfmt.wdt import WDT  # noqa: E402

WDTS = {775971: 'wdt map 0 (Azeroth)', 791060: 'wdt map 34 (StormwindJail)', 780814: 'wdt map 429 (DireMaul)'}
OBJ0 = {777623: 'obj0 azeroth_30_47'}


def build_of(install, product):
    info = os.path.join(install, '.build.info')
    with open(info, encoding='utf-8') as f:
        head = f.readline().rstrip('\n').split('|')
        names = [h.split('!')[0] for h in head]
        for line in f:
            row = dict(zip(names, line.rstrip('\n').split('|')))
            if row.get('Product') == product and row.get('Active') == '1':
                return row.get('Version', '')
    return ''


def extract(exe, data, product, out, ids):
    os.makedirs(out, exist_ok=True)
    lst = os.path.join(out, '_ids.txt')
    with open(lst, 'w') as f:
        f.write('\n'.join(str(i) for i in sorted(set(ids))) + '\n')
    subprocess.run([exe, data, product, out, '-', lst], check=True, capture_output=True)
    status = {}
    with open(os.path.join(out, 'manifest.tsv')) as f:
        for line in f:
            p = line.rstrip('\n').split('\t')
            status[int(p[0])] = p[1]
    return status


def lod0_groups(path):
    b = open(path, 'rb').read()
    w = walk(b)
    mohd, gfid = w.one('MOHD'), w.one('GFID')
    n = struct.unpack_from('<I', b, mohd.data_offset + 4)[0]
    return list(struct.unpack_from('<%dI' % (gfid.size // 4), b, gfid.data_offset))[:n] if gfid else []


def main():
    install, audit = sys.argv[1], sys.argv[2]
    clients = sys.argv[3:] or ['wow', 'wow_classic_era', 'wow_classic', 'wow_classic_beta']
    exe = os.path.join(audit, 'bin', 'casc_extract.exe')
    data = os.path.join(install, 'Data')
    root = os.path.join(audit, 'world06')
    manifest = {'clients': {}}
    for c in clients:
        out = os.path.join(root, c)
        roles = {}
        st = extract(exe, data, c, out, list(WDTS) + list(OBJ0))
        for fid, what in list(WDTS.items()) + list(OBJ0.items()):
            if st.get(fid) == 'OK':
                roles[fid] = what
        wmos = set()
        if 777623 in roles:
            b = open(os.path.join(out, '777623.bin'), 'rb').read()
            for r in A.modf(b, walk(b).one('MODF')):
                if r['flags'] & 8:
                    wmos.add(r['name_id'])
        for fid in (791060, 780814):
            if fid in roles:
                for r in WDT(open(os.path.join(out, '%d.bin' % fid), 'rb').read()).modf:
                    if r['flags'] & 8:
                        wmos.add(r['name_id'])
        st = extract(exe, data, c, out, wmos)
        groups = {}
        for fid in sorted(wmos):
            if st.get(fid) != 'OK':
                continue
            roles[fid] = 'wmo root'
            for g in lod0_groups(os.path.join(out, '%d.bin' % fid)):
                if g:
                    groups[g] = fid
        st = extract(exe, data, c, out, groups)
        for g, owner in groups.items():
            if st.get(g) == 'OK':
                roles[g] = 'wmo group of %d' % owner
        files = {}
        for fid, what in sorted(roles.items()):
            p = os.path.join(out, '%d.bin' % fid)
            b = open(p, 'rb').read()
            files[str(fid)] = {'role': what, 'bytes': len(b), 'sha256': hashlib.sha256(b).hexdigest()}
        manifest['clients'][c] = {'build': build_of(install, c), 'files': files}
        print('%s %s: %d files (%d world models, %d groups)' % (c, manifest['clients'][c]['build'], len(files),
                                                              sum(1 for v in roles.values() if v == 'wmo root'),
                                                              sum(1 for v in roles.values() if v.startswith('wmo group'))))
    with open(os.path.join(root, 'manifest.json'), 'w') as f:
        json.dump(manifest, f, indent=1, sort_keys=True)


if __name__ == '__main__':
    main()
