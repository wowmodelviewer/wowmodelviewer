"""Compares the viewer's world map catalogue (wowmodelviewer -mapcatalogtest) with this audit's own results.

  python -I compare_map_catalog.py <out.json> <audit data folder> [<more out.json> ...] [--report <report.json>]

Every client step of every run is compared with what the audit's Python readers found in the same client's bytes
(docs/world-viewer/00-audit.md, 3.1 and 4): catalog/maps_<client>.json (Map.db2, read schema-free) and
catalog/wdt_<client>.json (each WDT opened by the client's own rule). Nothing of the viewer's side is read from the
audit, and nothing of the audit's from the viewer: they meet only here.

Checked per client:
  - the maps: every readable Map.db2 row, no more, no fewer (Classic Beta's key-locked rows are counted, not listed);
  - directory and name;
  - the WDT reference: Map.db2's FileDataID where the client's layout has the column (Retail, Classic Beta), the path
    world/maps/<Directory>/<Directory>.wdt where it does not (Classic Era, MoP Classic); unresolved exactly where
    Map.db2 gives no WDT;
  - the availability: available where the audit read the WDT (same size and SHA-256), not in the client's storage
    where the audit found it NOT_IN_ROOT (and then no storage FileDataID), unsupported where there is no reference;
    a path the storage root has no name for is "not in client storage" only when that root names every file it lists
    (the catalogue's client.rootNameless is 0), else "unsupported";
  - for a path client, the FileDataID the storage root gives the WDT, against the FileDataID whose bytes the audit
    found equal to the path's (wdt_fdid; the listfile only proposed it);
  - the classification codes, against the raw Map.db2 values the audit decoded (pallet columns read as the int16 of
    their low 16 bits), column positions per layout as Map.dbd lists them;
  - the stated WDT form (MapType) against the form of the WDT the audit read: a tile grid with no global world model
    (MapType 1), a world model only, named by FileDataID (2) or also by path (4); and no WDT stated readable (3) is
    never available;
  - the probe files a run names (-mapcatalogprobe), where PROBE_EXPECTED gives this computer's state for them.
Across the steps of one run: a client loaded twice yields the same catalogue both times, and each step belongs to its
own client (product, build, its own storage).

Exit status 1 when anything differs; the report lists every difference.
"""
import json
import os
import sys

# Map.db2 raw field indexes per layout (the same positions dbd/Map.dbd lists; 02-map-catalog.md section 3).
LAYOUT_FIELDS = {
    '75863E23': {'mapType': 7, 'instanceType': 8, 'parentMapId': 13, 'worldCopyOfMapId': 14, 'locationMapId': 17, 'wdt': 21},
    # Classic Beta's field 14 is not WorldCopyOfMapID (a 2-bit code, BetaCode14): the catalogue has no such column there.
    'D43AFAC3': {'mapType': 7, 'instanceType': 8, 'parentMapId': 13, 'worldCopyOfMapId': None, 'locationMapId': 17, 'wdt': 21},
    'B47C6F05': {'mapType': 6, 'instanceType': 7, 'parentMapId': 12, 'worldCopyOfMapId': 13, 'locationMapId': 17, 'wdt': None},
}
MAP_REFS = ('parentMapId', 'worldCopyOfMapId', 'locationMapId')
ABSENT_NAMES = {'worldCopyOfMapId': 'WorldCopyOfMapID', 'wdt': 'WdtFileDataID'}
ALL = ('wow', 'wow_classic_beta', 'wow_classic_era', 'wow_classic')
ABSENT_BY_PATH = 'absent by path'   # see absent_by_path()


def absent_by_path(cat):
    """The state of a path the storage root has no name for: absent only if the root names every file it lists."""
    return 'not in client storage' if cat['client'].get('rootNameless') == 0 else 'unsupported'


# What the storage of each client says about some files on this computer, as found on 2026-10-12 (the clients'
# builds pinned in 00-audit.md): a state per product. A probe not listed, or a product not listed for it, is reported
# but not checked. These are this installation's facts (a download or a new key changes them), not rules.
PROBE_EXPECTED = {
    '775971': {'wow': 'available', 'wow_classic_beta': 'available'},          # Azeroth's WDT by FileDataID
    '787450': {'wow': 'available', 'wow_classic_beta': 'not installed locally'},
    '1349477': {'wow': 'available', 'wow_classic_beta': 'unreadable'},     # Map.db2: Beta's has key-locked sections
    '546628': {'wow': 'unreadable'},                                         # encrypted from its first byte
    '921760': {'wow': 'unreadable', 'wow_classic_era': 'available'},        # Mount.db2: partly encrypted in Retail
    '1467245': {'wow': 'unreadable'},                                        # larger than a WDT read allows
    '2976775': {'wow': 'not installed locally', 'wow_classic_era': 'not installed locally'},
    '8345027': {'wow': 'not in client storage'},                             # past every root id: CascLib's own number
    '0': {p: 'unsupported' for p in ALL},
    '-5': {p: 'unsupported' for p in ALL},
    'INSTALL': {p: ABSENT_BY_PATH for p in ALL},                            # a storage file, not a root entry
    'world/maps/azeroth/azeroth.wdt': {'wow': ABSENT_BY_PATH, 'wow_classic_beta': ABSENT_BY_PATH,  # no name in their roots
                                       'wow_classic_era': 'available', 'wow_classic': 'available'},
    'WORLD\\MAPS\\AZEROTH\\AZEROTH.WDT': {'wow_classic_era': 'available', 'wow_classic': 'available'},
}


def int16_low(v):
    """A pallet column stores an int16 as its low 16 bits (-1 as 65535; Era/MoP add 0x0001 in the upper half)."""
    v &= 0xFFFF
    return v - 0x10000 if v >= 0x8000 else v


def load(path):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


class Diff:
    def __init__(self):
        self.items = []
        self.checks = 0

    def check(self, ok, where, what):
        self.checks += 1
        if not ok:
            self.items.append({'where': where, 'what': what})
        return ok


def compare_step(step, data, d, where):
    product = step['product']
    cat = step.get('catalog')
    if not d.check(cat is not None, where, 'no catalogue in this step'):
        return None
    d.check(cat['client']['product'] == product, where, 'catalogue of %s, not %s' % (cat['client']['product'], product))
    d.check(cat['status'] == 'ready', where, 'status %s: %s' % (cat['status'], cat.get('statusDetail')))

    maps_json = load(os.path.join(data, 'catalog', 'maps_%s.json' % product))
    wdt_json = load(os.path.join(data, 'catalog', 'wdt_%s.json' % product))
    layout = maps_json['layout_hash']
    d.check(cat['mapTable']['layout'] == layout, where, 'Map.db2 layout %s, the audit read %s' % (cat['mapTable']['layout'], layout))
    fields = LAYOUT_FIELDS.get(layout)
    if not d.check(fields is not None, where, 'layout %s has no field positions here' % layout):
        return None

    rule = 'by FileDataID' if fields['wdt'] is not None else 'by directory path'
    d.check(cat['wdtRule'] == rule, where, 'WDT rule %r, expected %r for layout %s' % (cat['wdtRule'], rule, layout))
    absent = set(cat['mapTable']['absentColumns'])
    for key, column in ABSENT_NAMES.items():
        d.check((column in absent) == (fields[key] is None), where, 'absent columns %s (%s)' % (sorted(absent), column))

    readable = [m for m in maps_json['maps'] if not m['zero_filled']]
    locked = len(maps_json['maps']) - len(readable)
    d.check(cat['mapTable']['lockedRows'] == locked, where,
            'Map.db2 records in key-locked sections: %s, the audit counts %d' % (cat['mapTable']['lockedRows'], locked))
    audit = {m['id']: m for m in readable}
    wdts = {m['id']: m for m in wdt_json['maps']}
    mine = {m['id']: m for m in cat['maps']}
    d.check(set(mine) == set(audit), where, 'map IDs differ: only in the viewer %s, only in the audit %s'
            % (sorted(set(mine) - set(audit))[:20], sorted(set(audit) - set(mine))[:20]))
    d.check(len(mine) == len(cat['maps']), where, 'a map ID is listed twice')

    counts = {}
    for mid in sorted(set(mine) & set(audit)):
        m, a = mine[mid], audit[mid]
        w = '%s map %d (%s)' % (where, mid, a['directory'])
        d.check(m['directory'] == a['directory'], w, 'directory %r, audit %r' % (m['directory'], a['directory']))
        d.check(m['name'] == a.get('name'), w, 'name %r, audit %r' % (m['name'], a.get('name')))
        raw = a['raw']
        for key in ('mapType', 'instanceType') + MAP_REFS:
            if fields[key] is None:
                d.check(m[key] is None, w, '%s %r, but the layout has no such column' % (key, m[key]))
                continue
            v = raw[fields[key]][0]
            expect = int16_low(v) if key in MAP_REFS else v
            d.check(m[key] == expect, w, '%s %r, raw Map.db2 value %r (as %r)' % (key, m[key], v, expect))

        ref = m['wdt']
        wdt = wdts.get(mid, {})
        if fields['wdt'] is not None:
            fdid = raw[fields['wdt']][0]
            if fdid:
                d.check(ref.get('kind') == 'FileDataID' and ref.get('fileDataId') == fdid, w, 'reference %s, Map.db2 says %d' % (ref, fdid))
            else:
                d.check(ref.get('kind') == 'unresolved', w, 'reference %s, Map.db2 gives no WDT' % ref)
        else:
            path = 'world/maps/%s/%s.wdt' % (a['directory'], a['directory'])
            d.check(ref.get('kind') == 'path' and ref.get('path') == path, w, 'reference %s, the rule gives %s' % (ref, path))

        ins = m['inspection']
        state = ins['state']
        counts[state] = counts.get(state, 0) + 1
        status = wdt.get('status')
        if ref.get('kind') == 'unresolved':
            d.check(state == 'unsupported', w, 'state %s for a map with no WDT reference' % state)
        elif status == 'OK':
            d.check(state == 'available', w, 'state %s (%s), the audit read the WDT' % (state, ins.get('detail')))
            if state == 'available':
                d.check(ins.get('bytes') == wdt.get('bytes'), w, 'size %s, audit %s' % (ins.get('bytes'), wdt.get('bytes')))
                d.check(ins.get('sha256') == wdt.get('sha256'), w, 'SHA-256 differs from the audit\'s copy')
                if ref.get('kind') == 'path':
                    d.check(ins.get('storageFileDataId') == wdt.get('wdt_fdid'), w,
                            'storage root FileDataID %s, the audit\'s same-bytes FileDataID %s' % (ins.get('storageFileDataId'), wdt.get('wdt_fdid')))
        else:
            # NOT_IN_ROOT, or no audit reference at all (a path the listfile does not name, probed by path).
            expect = absent_by_path(cat) if ref.get('kind') == 'path' else 'not in client storage'
            d.check(state == expect, w, 'state %s (%s), the audit status %s, expected %s' % (state, ins.get('detail'), status, expect))
        if state in ('not in client storage', 'unsupported'):
            d.check(ins.get('storageFileDataId') == -1, w, 'storage FileDataID %s for a file the root does not list' % ins.get('storageFileDataId'))

        form = m.get('statedWdtForm')
        if state == 'available':
            tiles = wdt.get('tiles') or 0
            gw = wdt.get('global_wmo') or {}
            modf, mwmo = bool(gw.get('modf')), bool(gw.get('mwmo'))
            if not modf and not mwmo:
                actual = 'tile grid'
            elif modf and not tiles:
                actual = 'WMO only'
            else:
                actual = 'tiles and a world model'
            d.check(form == actual, w, 'stated WDT form %s, the WDT read is %s (tiles %d, MODF %s, MWMO %s)' % (form, actual, tiles, modf, mwmo))
            if m['mapType'] in (2, 4):
                d.check(mwmo == (m['mapType'] == 4), w, 'MapType %d, the WDT %s an MWMO path' % (m['mapType'], 'has' if mwmo else 'has no'))
        if form == 'no WDT':
            d.check(state != 'available', w, 'Map.db2 states no readable WDT, yet it was read')
    for p in step.get('probes', []):
        expect = PROBE_EXPECTED.get(p['probe'], {}).get(product)
        if expect == ABSENT_BY_PATH:
            expect = absent_by_path(cat)
        if expect is not None:
            d.check(p['state'] == expect, '%s probe %s' % (where, p['probe']), 'state %s (%s), expected %s' % (p['state'], p.get('detail'), expect))
    d.check(isinstance(cat['client'].get('rootNames'), (int, float)) and isinstance(cat['client'].get('rootNameless'), (int, float)),
            where, 'the catalogue does not give its storage root\'s name counts')
    return {'product': product, 'build': cat['client']['build'], 'layout': layout, 'maps': len(mine),
            'rootNames': cat['client'].get('rootNames'), 'rootNameless': cat['client'].get('rootNameless'),
            'locked': cat['mapTable']['lockedRows'], 'rule': cat['wdtRule'], 'availability': counts,
            'storageGeneration': cat['client']['storageGeneration']}


def strip_volatile(cat):
    """A catalogue without what may differ between two loads of one client: timings and the storage number."""
    maps = []
    for m in cat['maps']:
        m = dict(m)
        ins = dict(m['inspection'])
        ins.pop('ms', None)
        m['inspection'] = ins
        maps.append(m)
    client = dict(cat['client'])
    client.pop('storageGeneration', None)
    return {'client': client, 'status': cat['status'], 'mapTable': cat['mapTable'], 'wdtRule': cat['wdtRule'], 'maps': maps}


def main(argv):
    report_path = None
    if '--report' in argv:
        i = argv.index('--report')
        report_path = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
    out_paths = [argv[0]] + argv[2:]
    data = argv[1]
    d = Diff()
    runs = []
    for out_path in out_paths:
        run = load(out_path)
        name = os.path.basename(out_path)
        summary = {'run': name, 'passed': run.get('passed'), 'failed': run.get('failed'), 'steps': []}
        d.check(run.get('failed') == 0, name, 'the viewer\'s own checks failed: %s' % run.get('failed'))
        before = run.get('beforeLoad', {})
        d.check(before.get('status') == 'no client', name, 'catalogue before any load: %s' % before.get('status'))
        seen = {}
        gens = []
        for i, step in enumerate(run['steps']):
            where = '%s step %d %s' % (name, i, step['product'])
            s = compare_step(step, data, d, where)
            summary['steps'].append(s)
            if s is None:
                continue
            gens.append(s['storageGeneration'])
            stripped = strip_volatile(step['catalog'])
            if step['product'] in seen:
                d.check(stripped == seen[step['product']], where, 'differs from the same client\'s catalogue earlier in the run')
            seen[step['product']] = stripped
        d.check(len(set(gens)) == len(gens), name, 'two steps share a storage: %s' % gens)
        runs.append(summary)
    report = {'checks': d.checks, 'differences': len(d.items), 'runs': runs, 'items': d.items[:500]}
    if report_path:
        with open(report_path, 'w', encoding='utf-8') as f:
            json.dump(report, f, indent=1)
    for r in runs:
        for s in r['steps']:
            if s:
                print('%-22s %-17s %-13s layout %s: %4d maps, %d locked, %s, root names %s / nameless %s, %s'
                      % (r['run'], s['product'], s['build'], s['layout'], s['maps'], s['locked'], s['rule'],
                         s['rootNames'], s['rootNameless'], json.dumps(s['availability'], sort_keys=True)))
    for item in d.items[:40]:
        print('DIFF', item['where'], '--', item['what'])
    print('%d checks, %d differences' % (d.checks, len(d.items)))
    return 1 if d.items else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
