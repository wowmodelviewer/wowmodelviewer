"""Header summary of the environment tables (Light*, Liquid*, ZoneLight*, GroundEffect*, Terrain*, Weather, AreaTable,
Map, MapDifficulty) per client -- format, record count, field count, layout hash, and how many sections are under an
encryption key (readable with the keys given to casc_extract, or zero-filled without one). Nothing is interpreted: the
layouts differ by client, and no field is named.

  python env_tables.py <audit data folder> <listfile.csv> <out.json>

Reads <data>/envdb/<client>/<FileDataID>.bin for every client folder there (extracted with casc_extract from
ids_envdb.txt); names come from the listfile (a lookup aid).
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from wowfmt.db2 import DB2  # noqa: E402
from wowfmt.listfile import Listfile  # noqa: E402


def main(data, listfile_path, out_path):
    lf = Listfile(listfile_path)
    out = {}
    root = os.path.join(data, 'envdb')
    for client in sorted(os.listdir(root)):
        folder = os.path.join(root, client)
        if not os.path.isdir(folder):
            continue
        tables = {}
        for fn in sorted(os.listdir(folder)):
            if not (fn.endswith('.bin') and fn[:-4].isdigit()):
                continue
            name = os.path.basename(lf.path_of(int(fn[:-4])) or fn)
            db = DB2(open(os.path.join(folder, fn), 'rb').read())
            keyed = [s for s in db.sections if s['tact_key_hash']]
            locked = sum(1 for s in keyed if s['zero_filled'])
            note = ''
            if keyed:
                note = ' (%d of %d sections under a key: %d readable, %d without the key)' % (
                    len(keyed), len(db.sections), len(keyed) - locked, locked)
            tables[name] = '%s %d rec, %d fields, layout %08X%s' % (db.magic, db.record_count, db.field_count, db.layout_hash,
                                                                    note)
        out[client] = tables
    json.dump(out, open(out_path, 'w'), indent=1)
    print(json.dumps({k: len(v) for k, v in out.items()}))


if __name__ == '__main__':
    main(*sys.argv[1:4])
