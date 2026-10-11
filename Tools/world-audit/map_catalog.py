"""Map catalog per client from Map.db2, with its columns identified from the bytes.

  python map_catalog.py <Map.db2 file> <listfile.csv> <out.json>

Columns are not named from a schema. A column is called:
  Directory  -- the string column whose value d gives world/maps/<d>/<d>.wdt in the listfile for most rows;
  MapName    -- the next string column (reported as is, not relied on);
  WdtFileDataID -- the integer column whose value, for most rows that have one, is the FileDataID the listfile
                   names world/maps/<Directory>/<Directory>.wdt.
The proof (rows checked, matches, mismatches) goes into the output, so the claim can be audited.
"""
import json
import sys

sys.path.insert(0, __import__('os').path.dirname(__file__))
from wowfmt.db2 import DB2  # noqa: E402
from wowfmt.listfile import Listfile  # noqa: E402


def main(db_path, listfile_path, out_path):
    db = DB2(open(db_path, 'rb').read())
    lf = Listfile(listfile_path)
    rows = db.rows()
    real_rows = [r for r in rows if not db.sections[r[3]]['zero_filled']]
    strings = {}
    for f in db.fields:
        if f.storage == 0 and f.element_bits == 32 and f.count == 1:
            vals = []
            for rid, ro, v, si in real_rows:
                vals.append(db.string_at(ro, f, v[f.index][0]))
            if sum(1 for s in vals if s is not None) >= 0.9 * len(real_rows):
                strings[f.index] = vals

    # Directory: the string column that names WDTs.
    best = None
    for fi, vals in strings.items():
        hits = sum(1 for d in vals if d and lf.id_of('world/maps/%s/%s.wdt' % (d.lower(), d.lower())))
        if best is None or hits > best[1]:
            best = (fi, hits)
    dir_field, dir_hits = best
    dirs = strings[dir_field]

    # WdtFileDataID: the integer column equal to the listfile's FileDataID of that WDT.
    candidates = []
    for f in db.fields:
        if f.index in strings or f.count != 1:
            continue
        match = mismatch = zero = 0
        for (rid, ro, v, si), d in zip(real_rows, dirs):
            val = v[f.index][0]
            if not val:
                zero += 1
                continue
            want = lf.id_of('world/maps/%s/%s.wdt' % (d.lower(), d.lower())) if d else None
            if want and want == val:
                match += 1
            else:
                mismatch += 1
        candidates.append((match, -mismatch, f.index, zero, mismatch))
    candidates.sort(reverse=True)
    match, _, wdt_field, zero, mismatch = candidates[0]
    if match == 0:
        wdt_field = None      # no integer column holds the listed WDTs' FileDataIDs (not "an all-zero column")

    name_field = min((fi for fi in strings if fi > dir_field), default=None)
    maps = []
    for (rid, ro, v, si), d in zip(rows, [None] * len(rows)):
        zero_filled = db.sections[si]['zero_filled']
        entry = {'id': rid, 'section': si, 'zero_filled': zero_filled}
        if not zero_filled:
            f = db.fields[dir_field]
            entry['directory'] = db.string_at(ro, f, v[dir_field][0])
            if name_field is not None:
                entry['name'] = db.string_at(ro, db.fields[name_field], v[name_field][0])
            entry['wdt_fdid'] = v[wdt_field][0] if wdt_field is not None else 0
            entry['wdt_listfile_path'] = lf.path_of(entry['wdt_fdid']) if entry['wdt_fdid'] else None
            entry['raw'] = v
        maps.append(entry)
    out = {
        'file': db_path, 'magic': db.magic, 'schema': db.schema, 'layout_hash': '%08X' % db.layout_hash,
        'records': db.record_count, 'fields': db.field_count,
        'sections': [{k: (hex(s[k]) if k == 'tact_key_hash' else s[k]) for k in ('tact_key_hash', 'record_count', 'zero_filled')}
                     for s in db.sections],
        'proof': {
            'directory_field': dir_field, 'directory_rows_naming_a_listed_wdt': dir_hits,
            'name_field': name_field,
            'wdt_fdid_field': wdt_field, 'wdt_fdid_matches_listfile': match, 'wdt_fdid_mismatches': mismatch,
            'wdt_fdid_zero': zero, 'rows_checked': len(real_rows),
            'runner_up': [{'field': c[2], 'matches': c[0], 'mismatches': c[4]} for c in candidates[1:3]],
        },
        'maps': maps,
    }
    json.dump(out, open(out_path, 'w'), indent=1)
    p = out['proof']
    print('%s %s: %d maps; Directory=field %d (%d name a listed WDT); WdtFileDataID=%s (%d match, %d mismatch, %d zero, of %d)'
          % (db.magic, db.schema, len(maps), dir_field, dir_hits,
             'field %d' % wdt_field if wdt_field is not None else 'no column identified', match, mismatch, zero, len(real_rows)))


if __name__ == '__main__':
    main(*sys.argv[1:4])
