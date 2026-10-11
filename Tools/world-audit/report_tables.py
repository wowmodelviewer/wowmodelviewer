"""Markdown tables for docs/world-viewer/00-audit.md, generated from the audit's result files (so every number in the
report can be regenerated from the bytes).

  python report_tables.py <audit data folder> > tables.md

A cell reads "not measured" where the audit did not run that measurement for a client, and "-" where the measurement
ran and found nothing.
"""
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from wowfmt import adt as A  # noqa: E402

CLIENTS = [('wow', 'Retail', '12.1.0.69933'), ('wow_classic_beta', 'Classic Beta', '1.60.1.70338'),
           ('wow_classic_era', 'Classic Era', '1.15.9.70003'), ('wow_classic', 'MoP Classic', '5.5.4.70268')]
NM = 'not measured'


def j(root, *p):
    path = os.path.join(root, *p)
    return json.load(open(path)) if os.path.exists(path) else None


def top_sets(adt, part):
    c = collections.Counter()
    for t in adt['tiles'].values():
        if part in t:
            c[' '.join(x for x, n in t[part]['top'])] += 1
    return c


def sub_sets(adt, part):
    c = collections.Counter()
    for t in adt['tiles'].values():
        if part in t and 'subchunk_sets' in t[part]:
            for x, y in t[part]['subchunk_sets']:
                c[x] += y
    return c


def tiles_with(adt, part, chunk):
    return sum(1 for t in adt['tiles'].values() if part in t and any(x == chunk for x, n in t[part]['top']))


def chunks_with(adt, part, chunk):
    return sum(n for s, n in sub_sets(adt, part).items() if chunk in s.split())


def side_chunks(side, key):
    e = side['side'].get(key) if side else None
    if not e:
        return NM if side is None else '-'
    if e['status'] != 'OK':
        return e['status']
    ch = collections.Counter()
    for c, n in e.get('chunks', e.get('top', [])):
        ch[c] += n
    return '%s B: %s' % ('{:,}'.format(e['bytes']) if 'bytes' in e else '?',
                         ', '.join('%s%s' % (c, 'x%d' % n if n > 1 else '') for c, n in ch.items()))


def main_area(root, client, f, areas):
    """The AreaTable name of the area id most root MCNKs of a tile carry (header offset 52)."""
    if not f or f.get('status') != 'OK':
        return ''
    path = os.path.join(root, 'corpus', client, 'azeroth', '%d.bin' % f['fdid'])
    if not os.path.exists(path):
        return NM
    b = open(path, 'rb').read()
    ids = collections.Counter(h['area_id'] for _, h, _ in A.SplitFile(b, root=True).mcnk if h)
    if not ids:
        return '-'
    a = ids.most_common(1)[0][0]
    return areas.get(str(a), [None, 'area %d (not in AreaTable)' % a])[1]


def matrix(root, w):
    w('### T0. Format matrix by client\n')
    w('Map 0 "Azeroth" unless the row says otherwise. Window = the tiles extracted for that client (Retail and Classic Era '
      '56 tiles x 29-36, y 45-51; Classic Beta and MoP Classic 9 tiles x 31-33, y 47-49).\n')
    rows = collections.OrderedDict()
    for p, name, build in CLIENTS:
        maps, wdt = j(root, 'catalog', 'maps_%s.json' % p), j(root, 'catalog', 'wdt_%s.json' % p)
        adt, side = j(root, 'results', 'adt_%s.json' % p), j(root, 'results', 'side_%s.json' % p)
        coords, avail = j(root, 'results', 'coords_%s.json' % p), j(root, 'results', 'avail_%s.json' % p)
        tex1 = os.path.join(root, 'tex1', p, 'manifest.tsv')
        pr = maps['proof']
        rows.setdefault('Map.db2 WDT FileDataID column', []).append(
            'field %d (%d rows agree)' % (pr['wdt_fdid_field'], pr['wdt_fdid_matches_listfile']) if pr['wdt_fdid_field'] is not None
            else 'none (no column matches)')
        same = wdt['name_vs_fdid_same_bytes']
        rows.setdefault('Open WDT by path', []).append(
            'yes, same bytes as by FileDataID (%d/%d)' % (sum(1 for s in same if s['same_bytes']), len(same)) if same else 'no (root lists no names)')
        orders = dict((o, n) for o, n in wdt['chunk_orders'])
        rows.setdefault('WDTs read / with MAID / old MWMO form', []).append('%d / %d / %d' % (
            wdt['wdt_read'], sum(n for o, n in orders.items() if 'MAID' in o.split()), sum(n for o, n in orders.items() if 'MWMO' in o.split())))
        mc = wdt['maid_word_classes']
        tiles = sum(mc['0'].values())
        rows.setdefault('MAID tiles; words 4-6 (lod, map textures) zero', []).append(
            '%s; %s' % ('{:,}'.format(tiles), '{:,}'.format(mc['4'].get('zero', 0))))
        rows.setdefault('Azeroth MPHD flags', []).append('0x%X' % adt['mphd_flags'] if adt else NM)
        if not adt:
            continue
        rows.setdefault('Window tiles / MCNKs', []).append('%d / %s' % (len(adt['tiles']), '{:,}'.format(sum(adt['stats']['root_header_index_ok'].values()))))
        rows.setdefault('`_lod.adt` in MAID (window tiles)', []).append(str(sum(1 for t in adt['tiles'].values() if 'lod' in t)) or '-')
        if os.path.exists(tex1):
            st = collections.Counter(l.split('\t')[1] for l in open(tex1) if '\t' in l)
            rows.setdefault('`_tex1.adt` (5 scenario tiles, by path and by listfile FileDataID)', []).append(
                'absent (%s)' % ', '.join('%s %d' % kv for kv in st.items()) if set(st) <= {'NOT_IN_ROOT'} else str(dict(st)))
        al = collections.Counter()
        for t in adt['tiles'].values():
            if 'tex0' in t:
                for k, v in t['tex0']['alpha'].items():
                    al[k.split(':')[0]] += v
        rows.setdefault('MCAL per chunk (chunks with alpha)', []).append(', '.join('%s %s' % (k, '{:,}'.format(v)) for k, v in al.most_common()))
        rows.setdefault('MCSH chunks', []).append('{:,}'.format(chunks_with(adt, 'tex0', 'MCSH')))
        rows.setdefault('MCCV chunks', []).append('{:,}'.format(chunks_with(adt, 'root', 'MCCV')))
        rows.setdefault('MCBB / MPTX chunks', []).append('%d / %d' % (chunks_with(adt, 'root', 'MCBB'), chunks_with(adt, 'root', 'MPTX')))
        rows.setdefault('Tiles with MH2O / MFBO / blend mesh (MBMH)', []).append('%d / %d / %d' % (
            tiles_with(adt, 'root', 'MH2O'), tiles_with(adt, 'root', 'MFBO'), tiles_with(adt, 'root', 'MBMH')))
        rows.setdefault('Tiles with MTXP / MLMB (obj0) / MLDB (obj1)', []).append('%d / %d / %d' % (
            tiles_with(adt, 'tex0', 'MTXP'), tiles_with(adt, 'obj0', 'MLMB'), tiles_with(adt, 'obj1', 'MLDB')))
        lod = collections.Counter(c for s, n in top_sets(adt, 'lod').items() for c in set(s.split()) for _ in range(n))
        rows.setdefault('`_lod.adt` chunk ids seen', []).append(', '.join(sorted(lod)) or '-')
        rows.setdefault('Chunks with holes (high-resolution mask)', []).append(str(coords['holes']['chunks_with_holes']) if coords else NM)
        for key, label in (('side:lgt', '`_lgt.wdt`'), ('side:fogs', '`_fogs.wdt`'), ('side:mpv', '`_mpv.wdt`'),
                           ('side:occ', '`_occ.wdt`'), ('side:tex', '`.tex`')):
            s = side_chunks(side, key)
            rows.setdefault(label, []).append(s if len(s) < 90 else s[:87] + '...')
        rows.setdefault('WDL tiles with heights (MARE)', []).append('{:,}'.format(side['wdl_tiles_with_heights']) if side else NM)
        rows.setdefault('Files MAID names: not listed by the root / listed but not on this computer', []).append(
            '%s / %s' % ('{:,}'.format(avail['status_counts'].get('NOT_IN_ROOT', 0)), '{:,}'.format(avail['status_counts'].get('NOT_LOCAL', 0)))
            if avail else NM)
    w('| | %s |' % ' | '.join('%s %s' % (n, b) for _, n, b in CLIENTS))
    w('|---|%s|' % '|'.join('---' for _ in CLIENTS))
    for k, v in rows.items():
        w('| %s | %s |' % (k, ' | '.join(v + [NM] * (len(CLIENTS) - len(v)))))
    w('')


def main(root):
    out = []
    w = out.append
    matrix(root, w)

    w('### T1. Clients and Map.db2\n')
    w('| Product | Client | Build | Map.db2 schema | Layout | Rows | WDT FileDataID column | WDTs read | Not read |')
    w('|---|---|---|---|---|---|---|---|---|')
    for p, name, build in CLIENTS:
        c = j(root, 'catalog', 'maps_%s.json' % p)
        d = j(root, 'catalog', 'wdt_%s.json' % p)
        pr = c['proof']
        if pr['wdt_fdid_field'] is not None:
            col = 'field %d: %d rows match the listfile WDT, %d mismatch, %d zero' % (
                pr['wdt_fdid_field'], pr['wdt_fdid_matches_listfile'], pr['wdt_fdid_mismatches'], pr['wdt_fdid_zero'])
        else:
            col = 'no column identified (best candidate matches 0 rows)'
        zero = sum(1 for m in c['maps'] if m['zero_filled'])
        rows = '%d (%d key-locked)' % (len(c['maps']), zero) if zero else str(len(c['maps']))
        st = d['status_counts']
        w('| %s | %s | %s | %s | %s | %s | %s | %d | %s |' % (p, name, build, c['schema'], c['layout_hash'], rows, col, d['wdt_read'],
                                                           ', '.join('%s %d' % (k, v) for k, v in st.items() if k != 'OK') or '0'))
    w('')

    w('### T2. WDT structure census (every WDT each client has)\n')
    w('| Client | Chunk orders (count) | MPHD flag words seen (count) |')
    w('|---|---|---|')
    for p, name, build in CLIENTS:
        d = j(root, 'catalog', 'wdt_%s.json' % p)
        orders = '; '.join('%s (%d)' % (o, n) for o, n in d['chunk_orders'])
        flags = ', '.join('0x%X (%d)' % (int(f), n) for f, n in d['mphd_flag_values'])
        w('| %s | %s | %s |' % (name, orders, flags))
    w('')

    w('### T3. MAID words, named through the listfile (all tiles of all WDTs)\n')
    w('| Word | Retail | Classic Beta | Classic Era | MoP Classic |')
    w('|---|---|---|---|---|')
    data = {p: j(root, 'catalog', 'wdt_%s.json' % p) for p, _, _ in CLIENTS}
    for k in range(8):
        cells = []
        for p, _, _ in CLIENTS:
            cls = data[p]['maid_word_classes'].get(str(k), {})
            xy = data[p]['maid_name_tile_vs_index'].get(str(k), {})
            cells.append('%s; tile in name = (i%%64, i//64): %s' % (', '.join('%s %d' % (a, b) for a, b in sorted(cls.items(), key=lambda t: -t[1])),
                                                                  xy.get('name x,y = i%64,i//64', 0)))
        w('| %d | %s |' % (k, ' | '.join(cells)))
    w('')
    mc = j(root, 'results', 'maid_content.json')
    w('The same words identified without the listfile, from the bytes of each extracted file (window tiles):\n')
    w('| Word | %s |' % ' | '.join(mc['maid_roles']))
    w('|---|%s|' % '|'.join('---' for _ in mc['maid_roles']))
    for k in range(8):
        w('| %d | %s |' % (k, ' | '.join(', '.join('%s %d' % kv for kv in mc['maid_roles'][p].get(str(k), {}).items())
                                          for p in mc['maid_roles'])))
    w('')
    w('MPHD words 1..7, named through the listfile:\n')
    w('| Word | Retail | Classic Beta | Classic Era | MoP Classic |')
    w('|---|---|---|---|---|')
    for k in range(1, 8):
        w('| %d | %s |' % (k, ' | '.join(', '.join('%s %d' % (a, b) for a, b in data[p]['mphd_word_classes'].get(str(k), {}).items()) or '-'
                                      for p, _, _ in CLIENTS)))
    w('')

    sel = {'wow': j(root, 'results', 'select_wow.json'), 'wow_classic_era': j(root, 'results', 'select_wow_classic_era.json')}
    retail_picks = {'%d_%d' % (v['x'], v['y']): k for k, v in sel['wow']['scenarios'].items()}
    retail_picks['30_49'] = 'modern terrain material (MHID, MTXP)'
    era_picks = {k: v + ' (Retail pick)' for k, v in retail_picks.items()}
    era_picks.update({'%d_%d' % (v['x'], v['y']): k + ' (Era rule pick)' for k, v in sel['wow_classic_era']['scenarios'].items()})
    picks = {'wow': retail_picks, 'wow_classic_era': era_picks, 'wow_classic': None, 'wow_classic_beta': None}
    w('### T4. Corpus: the scenario tiles (map 0 "Azeroth", Eastern Kingdoms)\n')
    w('FileDataIDs of each tile\'s MAID words; every one of these files is pinned by SHA-256 in '
      '`Tools/world-audit/tests/corpus_hashes.json`. "-" = MAID word zero.\n')
    w('| Client | Tile | Scenario | Main area | root | obj0 | obj1 | tex0 | lod | map texture | map texture _n | minimap | root bytes | root SHA-256 (first 12) |')
    w('|---|---|---|---|---|---|---|---|---|---|---|---|---|---|')
    for p, name, _ in CLIENTS:
        adt = j(root, 'results', 'adt_%s.json' % p)
        areas = j(root, 'catalog', 'areas_%s.json' % p) or {}
        tiles = sel[p]['tiles'] if p in sel else {}
        for key in sorted(adt['tiles'], key=lambda k: tuple(map(int, k.split('_')))):
            if picks[p] is not None and key not in picks[p]:
                continue
            t = adt['tiles'][key]
            f = t['files']
            area = main_area(root, p, f.get('root'), areas)
            ids =['%d' % f[k]['fdid'] if k in f else '-' for k in ('root', 'obj0', 'obj1', 'tex0', 'lod', 'maptexture',
                                                                     'maptexture_n', 'minimap')]
            r = f.get('root', {})
            w('| %s | %s | %s | %s | %s | %s | %s |' % (name, key, (picks[p] or {}).get(key, 'window'), area, ' | '.join(ids),
                                                     '{:,}'.format(r['bytes']) if 'bytes' in r else r.get('status', '-'),
                                                     (r.get('sha256') or '-')[:12]))
    w('')

    w('### T5. Per-tile content of the scenario tiles (Retail)\n')
    w('| Tile | Area | Hole cells | Chunks with holes | Liquid chunks / instances | Terrain textures (MDID) | Height textures (MHID) | Chunks by layer count | MDDF (unique) | MODF (unique) |')
    w('|---|---|---|---|---|---|---|---|---|---|')
    for key, scen in retail_picks.items():
        m = sel['wow']['tiles'][key]
        w('| %s %s | %s | %d | %d | %d / %d | %s | %s | %s | %d (%d) | %d (%d) |' % (
            key, scen, m['main_area_name'], m['hole_cells'], m['chunks_with_holes'], m['liquid_chunks'],
            m['liquid_instances'], m.get('textures'), m.get('height_textures'), m.get('chunks_by_layers'), m['mddf'],
            m['m2_unique'], m['modf'], m['wmo_unique']))
    w('')

    w('### T6. ADT structure, measured on each client\'s window\n')
    for p, name, _ in CLIENTS:
        adt = j(root, 'results', 'adt_%s.json' % p)
        agg = collections.defaultdict(collections.Counter)
        for k, t in adt['tiles'].items():
            for part in ('root', 'tex0', 'obj0', 'obj1', 'lod'):
                if part in t:
                    agg[part + ' top-level'][' '.join('%s%s' % (c, ('x%d' % n if n > 1 else '')) for c, n in t[part]['top'])] += 1
            if 'root' in t:
                for a, b in t['root']['holes'].items():
                    agg['holes'][a] += b
                for a, b in t['root']['subchunk_sets']:
                    agg['root MCNK sub-chunks'][a] += b
                for a, b in t['root']['header_offsets'].items():
                    agg['root header offsets'][a] += b
                for a, b in t['root']['position_fit'].items():
                    agg['MCNK position formula'][a] += b
            if 'tex0' in t:
                for a, b in t['tex0']['alpha'].items():
                    agg['MCAL decode'][a] += b
                for a, b in t['tex0']['subchunk_sets']:
                    agg['tex0 MCNK sub-chunks'][a] += b
            if 'obj0' in t:
                for a, b in t['obj0']['subchunk_sets']:
                    agg['obj0 MCNK sub-chunks'][a or '(none)'] += b
        w('**%s** (%d tiles, MPHD flags 0x%X)\n' % (name, len(adt['tiles']), adt['mphd_flags']))
        w('| Measure | Result (count) |')
        w('|---|---|')
        for k, v in agg.items():
            w('| %s | %s |' % (k, '; '.join('%s (%d)' % (a, b) for a, b in v.most_common(8))))
        for k, v in adt['stats'].items():
            if k in ('area',):
                continue
            label = {'flags': 'MCNK flags values'}.get(k, k)
            items = sorted(v.items(), key=lambda t: -t[1])[:10]
            w('| %s | %s |' % (label, '; '.join('%s (%d)' % (a, b) for a, b in items)))
        w('')

    w('### T7. MH2O in each window\n')
    w('| Client | Tiles with MH2O | liquid_type values (count) | liquid object-or-vertex-format word (count) |')
    w('|---|---|---|---|')
    for p, name, _ in CLIENTS:
        adt = j(root, 'results', 'adt_%s.json' % p)
        lt, lo, n = collections.Counter(), collections.Counter(), 0
        for k, t in adt['tiles'].items():
            m = (t.get('root') or {}).get('mh2o')
            if m:
                n += 1
                for a, b in m['liquid_types'].items():
                    lt[a] += b
                for a, b in m['object_or_lvf'].items():
                    lo[a] += b
        w('| %s | %d of %d | %s | %s |' % (name, n, len(adt['tiles']), ', '.join('%s (%d)' % (a, b) for a, b in lt.most_common()),
                                         ', '.join('%s (%d)' % (a, b) for a, b in lo.most_common())))
    w('')

    w('### T8. Side files of Azeroth (listed, not interpreted)\n')
    sides = {p: j(root, 'results', 'side_%s.json' % p) for p, _, _ in CLIENTS}
    w('| File | %s |' % ' | '.join(n for _, n, _ in CLIENTS))
    w('|---|%s|' % '|'.join('---' for _ in CLIENTS))
    for k in sorted({k for s in sides.values() if s for k in s['side']}):
        cells = []
        for p, _, _ in CLIENTS:
            s = side_chunks(sides[p], k)
            cells.append(s if len(s) < 160 else s[:157] + '...')
        w('| %s | %s |' % (k.replace('side:', ''), ' | '.join(cells)))
    w('')

    lp = j(root, 'results', 'load_wow.json')
    w('### T9. Load pressure (Retail, measured from the files; LOD0 only, placements counted once per uniqueId)\n')
    keys = ['tiles', 'adt_files', 'adt_bytes', 'mcnk', 'terrain_vertices', 'terrain_triangles', 'terrain_alpha_layers',
            'mddf_entries_before_dedup', 'modf_entries_before_dedup', 'm2_unique', 'm2_instances',
            'm2_instances_from_wmo_doodad_sets', 'm2_bytes', 'm2_vertices_unique', 'm2_vertices_instanced', 'skin_bytes',
            'skin_indices', 'm2_dependency_files', 'm2_dependency_bytes', 'm2_dependency_not_read', 'wmo_unique',
            'wmo_instances', 'wmo_root_bytes', 'wmo_groups', 'wmo_group_bytes', 'wmo_vertices', 'wmo_indices',
            'wmo_groups_further_lods', 'wmo_group_bytes_further_lods', 'textures_unique', 'texture_file_bytes',
            'texture_mip_bytes', 'texture_rgba32_bytes', 'requests', 'bytes_total', 'gpu_geometry_bytes',
            'gpu_bytes_estimate', 'gpu_bytes_with_rgba32_textures']
    names = list(lp['totals'])
    w('| Measure | %s |' % ' | '.join(names))
    w('|---|%s|' % '|'.join('---' for _ in names))
    for k in keys:
        w('| %s | %s |' % (k, ' | '.join('{:,}'.format(lp['totals'][n][k]) for n in names)))
    w('| not read (m2, wmo, textures) | %s |' % ' | '.join(str(lp['totals'][n]['not_read']) for n in names))
    w('| textures by size (top 6) | %s |' % ' | '.join(str(lp['totals'][n]['textures_by_size']) for n in names))
    w('')

    env = j(root, 'results', 'envdb.json')
    w('### T10. Environment tables present (header only, not interpreted)\n')
    w('| Table | Retail | Classic Era | MoP Classic | Classic Beta |')
    w('|---|---|---|---|---|')
    for n in sorted({n for r in env.values() for n in r}):
        w('| %s | %s |' % (n, ' | '.join(env[p].get(n, '-') for p in ('wow', 'wow_classic_era', 'wow_classic', 'wow_classic_beta'))))
    w('')

    w('### T11. Local availability of every file the WDTs name (status-only census of all MAID words)\n')
    w('| Client | Root lists / on this computer / listed but not here (whole storage) | MAID words censused | OK | NOT_IN_ROOT | NOT_LOCAL | Not OK, by MAID word | Maps affected |')
    w('|---|---|---|---|---|---|---|---|')
    for p, name, _ in CLIENTS:
        a = j(root, 'results', 'avail_%s.json' % p)
        idx = os.path.join(root, 'avail', p, 'index.txt')
        line = open(idx).read().strip().splitlines()[-1].split(': ', 1)[1] if os.path.exists(idx) else NM
        sc = a['status_counts']
        bad = collections.Counter()
        for word, c in a['by_maid_word'].items():
            for s, n in c.items():
                if s != 'OK':
                    bad['%s %s' % (word, s)] += n
        w('| %s | %s | %s | %s | %s | %s | %s | %d |' % (
            name, line, '{:,}'.format(a['distinct_ids_censused']), '{:,}'.format(sc.get('OK', 0)),
            '{:,}'.format(sc.get('NOT_IN_ROOT', 0)), '{:,}'.format(sc.get('NOT_LOCAL', 0)),
            ', '.join('%s %d' % kv for kv in bad.most_common()) or '-', len(a['maps_with_files_not_ok'])))
    w('')
    beta = j(root, 'results', 'avail_wow_classic_beta.json')
    w('Classic Beta files that are listed but not on this computer (all of them):\n')
    w('| Map | Tile | MAID word | FileDataID |')
    w('|---|---|---|---|')
    for m, ex in beta['examples'].items():
        for e in ex:
            if e['status'] == 'NOT_LOCAL':
                w('| %s | %s | %s | %d |' % (m, e['tile'], e['word'], e['fdid']))
    w('')

    rt = j(root, 'results', 'read_timing_wow.json')
    w('### T12. Read time of the load sets (Retail, CascLib, one thread, warm operating-system cache)\n')
    w('| Set | Files | Bytes | Storage open (ms) | Index (ms) | Read (ms), runs 1/2/3 | MB/s (best) | Files/s (best) |')
    w('|---|---|---|---|---|---|---|---|')
    for n, v in rt['runs'].items():
        r = v['runs']
        w('| %s | %s | %s | %s | %s | %s | %s | %s |' % (
            n, '{:,}'.format(r[0]['files']), '{:,}'.format(r[0]['bytes']), '/'.join(str(x['open_ms']) for x in r),
            '/'.join(str(x['index_ms']) for x in r), '/'.join(str(x['read_ms']) for x in r),
            max(x['mb_per_s'] for x in r), '{:,}'.format(max(x['files_per_s'] for x in r))))
    w('')
    print('\n'.join(out))


if __name__ == '__main__':
    main(sys.argv[1])
