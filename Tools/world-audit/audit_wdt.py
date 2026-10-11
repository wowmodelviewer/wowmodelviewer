"""WDT census for one client: what every extracted WDT holds, and the proof of MAID's and MPHD's word order.

  python audit_wdt.py <client> <maps_<client>.json> <wdt folder with manifest.tsv> <listfile.csv> <out.json>

MAID: for every tile of every map, each MAID word that is a FileDataID is named through the listfile and classified
(root ADT, obj0, obj1, tex0, lod, map texture, map texture _n, minimap...). A word index is called e.g. "obj0" only if
the names of the FileDataIDs in it say so, and the tile numbers in those names are compared with the MAID index
(i % 64, i // 64). MPHD words are classified the same way (lgt/occ/fogs/mpv/tex/wdl/pd4 files).
"""
import collections
import hashlib
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
from wowfmt.listfile import Listfile  # noqa: E402
from wowfmt.wdt import WDT  # noqa: E402

ADT_RE = re.compile(r'^world/maps/([^/]+)/([^/]+?)_(\d+)_(\d+)(_obj0|_obj1|_tex0|_tex1|_lod|_occ|_lgt)?\.adt$')
MAPTEX_RE = re.compile(r'^world/maptextures/([^/]+)/([^/]+?)_(\d+)_(\d+)(_n|_h)?\.blp$')
MINIMAP_RE = re.compile(r'^world/minimaps/([^/]+)/map(\d+)_(\d+)\.blp$')


def classify(path):
    if path is None:
        return ('unnamed', None, None)
    m = ADT_RE.match(path)
    if m:
        return ('adt' + (m.group(5) or '_root'), int(m.group(3)), int(m.group(4)))
    m = MAPTEX_RE.match(path)
    if m:
        return ('maptexture' + (m.group(5) or ''), int(m.group(3)), int(m.group(4)))
    m = MINIMAP_RE.match(path)
    if m:
        return ('minimap', int(m.group(2)), int(m.group(3)))
    ext = path.rsplit('.', 1)[-1]
    stem = path.rsplit('/', 1)[-1]
    for tag in ('_lgt.wdt', '_occ.wdt', '_fogs.wdt', '_mpv.wdt', '_tex.wdt', '.wdl', '_preload.wdt', '_pd4'):
        if stem.endswith(tag) or tag in stem:
            return ('aux' + tag, None, None)
    return ('other.' + ext, None, None)


def manifest(folder):
    rows = {}
    with open(os.path.join(folder, 'manifest.tsv')) as f:
        for line in f:
            parts = line.rstrip('\n').split('\t')
            if len(parts) >= 3:
                rows[parts[0]] = (parts[1], int(parts[2]))
    return rows


def main(client, catalog_path, folder, listfile_path, out_path):
    lf = Listfile(listfile_path)
    cat = json.load(open(catalog_path))
    man = manifest(folder)
    maid_matrix = collections.defaultdict(collections.Counter)    # word index -> class -> count
    maid_xy = collections.defaultdict(collections.Counter)        # word index -> 'x=i%64,y=i//64' agreement
    mphd_matrix = collections.defaultdict(collections.Counter)
    mphd_flags = collections.Counter()
    chunk_orders = collections.Counter()
    maps_out = []
    files_by_sha = {}
    for m in cat['maps']:
        if m['zero_filled']:
            continue
        d = m['directory']
        key = str(m['wdt_fdid']) if m.get('wdt_fdid') else None
        fid = m.get('wdt_fdid') or lf.id_of('world/maps/%s/%s.wdt' % (d.lower(), d.lower()))
        path = os.path.join(folder, '%d.bin' % fid) if fid else None
        status = man.get(str(fid), ('NO_FDID', 0))[0] if fid else 'NO_FDID'
        entry = {'id': m['id'], 'directory': d, 'name': m.get('name'), 'wdt_fdid': fid, 'wdt_fdid_source':
                 'Map.db2' if m.get('wdt_fdid') else ('listfile' if fid else None), 'status': status}
        if not path or not os.path.exists(path):
            maps_out.append(entry)
            continue
        buf = open(path, 'rb').read()
        sha = hashlib.sha256(buf).hexdigest()
        entry['bytes'] = len(buf)
        entry['sha256'] = sha
        w = WDT(buf)
        entry['problems'] = w.problems
        entry['chunks'] = w.order
        chunk_orders[' '.join('%s' % c for c, k in w.order)] += 1
        entry['version'] = w.version
        entry['mphd'] = w.mphd_words
        if w.mphd_words:
            mphd_flags[w.mphd_words[0]] += 1
            for k, word in enumerate(w.mphd_words[1:], 1):
                if word:
                    cls = classify(lf.path_of(word))[0]
                    mphd_matrix[k][cls] += 1
        tiles = w.tiles()
        entry['tiles'] = len(tiles)
        entry['main_entry_size'] = getattr(w, 'main_entry_size', None)
        entry['maid_entry_size'] = getattr(w, 'maid_entry_size', None)
        entry['global_wmo'] = {'mwmo': [s.decode('latin-1') for s in (w.mwmo or []) if s], 'modf': w.modf}
        # MAID words, checked against the listfile names (tile numbers included).
        if w.maid:
            for i, words in enumerate(w.maid):
                if not any(words):
                    continue
                x, y = i % 64, i // 64
                for k, word in enumerate(words):
                    if not word:
                        maid_matrix[k]['zero'] += 1
                        continue
                    cls, a, b = classify(lf.path_of(word))
                    maid_matrix[k][cls] += 1
                    if a is not None:
                        maid_xy[k]['name x,y = i%64,i//64' if (a, b) == (x, y) else
                                   ('name x,y = i//64,i%64' if (a, b) == (y, x) else 'name x,y elsewhere')] += 1
            # Tiles flagged in MAIN without MAID words, and the reverse.
            main_set = {i for i in range(4096) if w.main and w.main[i][0]}
            maid_set = {i for i in range(4096) if any(w.maid[i])}
            entry['main_without_maid'] = len(main_set - maid_set)
            entry['maid_without_main'] = len(maid_set - main_set)
        if w.main:
            entry['main_flag_values'] = sorted({w.main[i][0] for i in range(4096) if w.main[i][0]})
            if w.maid:
                entry['main_flags_vs_maid'] = {str(v): {'tiles': sum(1 for i in range(4096) if w.main[i][0] == v),
                                                       'with_maid_words': sum(1 for i in range(4096) if w.main[i][0] == v and any(w.maid[i]))}
                                              for v in entry['main_flag_values']}
        for cid, table in w.extra_tables.items():
            if isinstance(table, int):
                entry['table_' + cid] = {'size': table}
                continue
            classes = collections.defaultdict(collections.Counter)
            for i, words in enumerate(table):
                for k, word in enumerate(words):
                    classes[k]['zero' if not word else classify(lf.path_of(word))[0]] += 1
            entry['table_' + cid] = {k: dict(v) for k, v in sorted(classes.items())}
        maps_out.append(entry)
        files_by_sha.setdefault(sha, []).append(m['id'])

    # Era/Classic: the same WDT opened by name and by FileDataID must be the same bytes.
    name_vs_id = []
    if any(k.startswith('name:') for k in man):
        listed = [l.strip() for l in open(os.path.join(folder, '..', '..', 'ids_wdt_%s.txt' % client)) if l.strip()]
        n = 0
        for line in listed:
            if line.startswith('n '):
                n += 1
                p = line[2:]
                fid = lf.id_of(p)
                a = os.path.join(folder, 'name_%d.bin' % n)
                b = os.path.join(folder, '%d.bin' % fid) if fid else None
                if os.path.exists(a) and b and os.path.exists(b):
                    name_vs_id.append({'path': p, 'fdid': fid, 'same_bytes': open(a, 'rb').read() == open(b, 'rb').read()})
    out = {
        'client': client,
        'maps_in_catalog': len(cat['maps']),
        'wdt_read': sum(1 for e in maps_out if 'sha256' in e),
        'status_counts': collections.Counter(e['status'] for e in maps_out),
        'chunk_orders': chunk_orders.most_common(),
        'mphd_flag_values': mphd_flags.most_common(),
        'mphd_word_classes': {k: dict(v) for k, v in sorted(mphd_matrix.items())},
        'maid_word_classes': {k: dict(v) for k, v in sorted(maid_matrix.items())},
        'maid_name_tile_vs_index': {k: dict(v) for k, v in sorted(maid_xy.items())},
        'name_vs_fdid_same_bytes': name_vs_id,
        'maps': maps_out,
    }
    json.dump(out, open(out_path, 'w'), indent=1, default=list)
    print('%s: %d WDTs read of %d maps; statuses %s' % (client, out['wdt_read'], len(cat['maps']), dict(out['status_counts'])))
    print('  chunk orders:', out['chunk_orders'][:6])
    print('  MPHD flag values:', out['mphd_flag_values'][:12])
    print('  MPHD words:', out['mphd_word_classes'])
    print('  MAID words:', out['maid_word_classes'])
    print('  MAID tile in name vs index:', out['maid_name_tile_vs_index'])
    if name_vs_id:
        print('  name vs FileDataID: %d compared, %d identical' % (len(name_vs_id), sum(1 for e in name_vs_id if e['same_bytes'])))


if __name__ == '__main__':
    main(*sys.argv[1:6])
