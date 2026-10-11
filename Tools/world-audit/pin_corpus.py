"""Writes tests/corpus_hashes.json: the SHA-256 of every file the report cites by name, and a digest of every file set a
reported count depends on (all extracted .bin files of a folder: SHA-256 over the sorted "<name> <sha256>" lines).

  python pin_corpus.py <audit data folder> <out.json>

The extracted files are copies; a client update shows up only after re-extracting (corpus.py, audit steps) and then
running the hash test.
"""
import hashlib
import json
import os
import sys

SCENARIOS = {
    'wow': {'32_50': 'ordinary outdoor', '34_46': 'holes/layers', '29_48': 'water', '30_47': 'dense structures',
            '30_49': 'modern terrain material (MHID/MTXP)'},
    'wow_classic_era': {'32_50': 'ordinary outdoor (Retail pick)', '34_46': 'holes/layers (Retail pick)',
                        '29_48': 'water (Retail pick)', '30_47': 'dense structures (Retail pick)',
                        '30_49': 'Retail modern-material tile', '31_48': 'ordinary outdoor (Era rule pick)',
                        '30_48': 'holes/layers (Era rule pick)', '36_49': 'water (Era rule pick)',
                        '34_51': 'dense structures (Era rule pick)'},
    'wow_classic': None,          # all 9 tiles of the window
    'wow_classic_beta': None,     # all 9 tiles of the window
}
SETS = ['corpus/wow/azeroth', 'corpus/wow_classic_era/azeroth', 'corpus/wow_classic/azeroth',
        'corpus/wow_classic_beta/azeroth', 'wdt/wow', 'wdt/wow_classic_beta', 'wdt/wow_classic_era', 'wdt/wow_classic',
        'db/wow', 'db/wow_classic_beta', 'db/wow_classic_era', 'db/wow_classic', 'envdb/wow', 'envdb/wow_classic_beta',
        'envdb/wow_classic_era', 'envdb/wow_classic', 'assets/wow', 'assets/wow_blp', 'wmoonly']


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def set_digest(folder):
    names = sorted(n for n in os.listdir(folder) if n.endswith('.bin'))
    lines = ''.join('%s %s\n' % (n, sha(os.path.join(folder, n))) for n in names)
    return len(names), hashlib.sha256(lines.encode('ascii')).hexdigest()


def main(root, out_path):
    files, about = {}, {}

    def add(rel, what):
        p = os.path.join(root, rel)
        if os.path.exists(p):
            files[rel] = sha(p)
            about[rel] = what

    for client, picks in SCENARIOS.items():
        folder = 'corpus/%s/azeroth' % client
        cj = json.load(open(os.path.join(root, folder, 'corpus.json')))
        for f in cj['files']:
            if f['status'] != 'OK':
                continue
            if 'x' in f:
                key = '%d_%d' % (f['x'], f['y'])
                if picks is None or key in picks:
                    add('%s/%d.bin' % (folder, f['fdid']), '%s Azeroth %s %s%s' % (
                        client, key, f['what'], ' (%s)' % picks[key] if picks else ''))
            else:
                add('%s/%d.bin' % (folder, f['fdid']), '%s Azeroth %s' % (client, f['what']))
    for client in ('wow', 'wow_classic_era', 'wow_classic', 'wow_classic_beta'):
        add('db/%s/1349477.bin' % client, '%s Map.db2' % client)
        add('db/%s/1353545.bin' % client, '%s AreaTable.db2' % client)
        add('wdt/%s/775971.bin' % client, '%s azeroth.wdt' % client)
    add('wmoonly/108631.bin', 'wow stormwindjail.wmo (WMO-only map 34, MODF by FileDataID)')
    add('wmoonly/948620.bin', 'wow 6hu_garrison_blacksmithing_hub.wmo (WMO-only map 1212, MWMO by path)')
    add('wdt/wow/791060.bin', 'wow WDT of map 34 StormwindJail')
    add('wdt/wow/952203.bin', 'wow WDT of map 1212')
    add('listfile.csv', 'the community listfile the audit used (names are lookups, not proof)')
    sets = {}
    for s in SETS:
        p = os.path.join(root, s)
        if os.path.isdir(p):
            n, d = set_digest(p)
            sets[s] = {'files': n, 'digest': d}
    out = {'root': 'the audit data folder (WMV_WORLD_AUDIT_CORPUS), here F:/wowmodelviewer/build/world-audit-data',
           'client_builds': {'wow': '12.1.0.69933', 'wow_classic_era': '1.15.9.70003', 'wow_classic': '5.5.4.70268',
                             'wow_classic_beta': '1.60.1.70338'},
           'files': files, 'about': about, 'sets': sets}
    json.dump(out, open(out_path, 'w'), indent=1)
    print('%d files, %d sets (%d files in sets)' % (len(files), len(sets), sum(v['files'] for v in sets.values())))


if __name__ == '__main__':
    main(*sys.argv[1:3])
