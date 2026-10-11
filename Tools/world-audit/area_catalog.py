"""Area names per client from AreaTable.db2, for labelling tiles by the area id their MCNK headers carry (offset 52):

  python area_catalog.py <AreaTable.db2 file> <out.json>

Writes {area id: [first string column, second string column]} -- the internal zone name and the display name. The
columns are not named from a schema: they are the first two columns whose values decode as strings in at least 90% of
the readable rows. The names only label tiles in the report; no rule depends on them. Rows in sections whose key is
missing (zero-filled) are left out.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from wowfmt.db2 import DB2  # noqa: E402


def main(db_path, out_path):
    db = DB2(open(db_path, 'rb').read())
    rows = [r for r in db.rows() if not db.sections[r[3]]['zero_filled']]
    columns = []
    for f in db.fields:
        if f.storage == 0 and f.element_bits == 32 and f.count == 1:
            vals = [db.string_at(ro, f, v[f.index][0]) for rid, ro, v, si in rows]
            if sum(1 for s in vals if s is not None) >= 0.9 * len(rows):
                columns.append(vals)
        if len(columns) == 2:
            break
    out = {str(rid): [columns[0][k], columns[1][k]] for k, (rid, ro, v, si) in enumerate(rows)}
    json.dump(out, open(out_path, 'w'), indent=1)
    print('%d areas' % len(out))


if __name__ == '__main__':
    main(*sys.argv[1:3])
