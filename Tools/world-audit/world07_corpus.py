"""Stage 07 corpus and census: the real client files the doodad placement tests read, and the numbers the doodad budgets
are chosen from (docs/world-viewer/07-doodads.md).

  python -I world07_corpus.py <WoW install root> <audit data folder> [client ...]

For each client (default: wow wow_classic_era wow_classic wow_classic_beta) it extracts, with bin/casc_extract.exe, into
<audit data>/world07/<client>/<FileDataID>.bin:
  - the WDTs of map 0 (Azeroth, 775971), map 34 (Stormwind Stockade, 791060) and map 429 (Dire Maul, 780814);
  - for tiles 30_47 (Stormwind) and 31_48 (Elwynn Forest) the root, obj0 and obj1 ADTs named by the WDT's MAID words 0-2;
  - every world model those place (the obj0 MODF records with flag 0x8, the WDTs' global MODF records): the root and its
    LOD0 group files (the first MOHD group-count entries of GFID);
  - every doodad model: the M2 named by each obj0 MDDF record (flag 0x40) and by every MODI entry of every extracted
    world model root (every doodad set, not only the ones a placement selects);
  - for every M2: its first skin profile (SFID[0]), every non-zero texture FileDataID (TXID), and any skeleton (SKID),
    bone (BFID) or animation (AFID) file it names.
It writes <audit data>/world07/manifest.json (per client: build, per file its role, size and SHA-256) and
<audit data>/world07/census.json (per client: per tile and per WMO the doodad counts by set rule, the unique M2s, M2
versions and chunk sets, skin profile counts, texture slot types, the largest files and the bytes a load would read).
The C# corpus tests check every file against the manifest before they use it.
"""
import collections
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
TILES = [(30, 47), (31, 48)]
PARTS = {0: 'root', 1: 'obj0', 2: 'obj1'}   # MAID words (docs/world-viewer/00-audit.md 4.2)


def build_of(install, product):
    info = os.path.join(install, '.build.info')
    with open(info, encoding='utf-8') as f:
        names = [h.split('!')[0] for h in f.readline().rstrip('\n').split('|')]
        for line in f:
            row = dict(zip(names, line.rstrip('\n').split('|')))
            if row.get('Product') == product and row.get('Active') == '1':
                return row.get('Version', '')
    return ''


def extract(exe, data, product, out, ids):
    ids = sorted(set(i for i in ids if i))
    if not ids:
        return {}
    os.makedirs(out, exist_ok=True)
    lst = os.path.join(out, '_ids.txt')
    with open(lst, 'w') as f:
        f.write('\n'.join(str(i) for i in ids) + '\n')
    subprocess.run([exe, data, product, out, '-', lst], check=True, capture_output=True)
    status = {}
    with open(os.path.join(out, 'manifest.tsv')) as f:
        for line in f:
            p = line.rstrip('\n').split('\t')
            status[int(p[0])] = p[1]
    return status


def lod0_groups(b):
    w = walk(b)
    mohd, gfid = w.one('MOHD'), w.one('GFID')
    n = struct.unpack_from('<I', b, mohd.data_offset + 4)[0]
    return list(struct.unpack_from('<%dI' % (gfid.size // 4), b, gfid.data_offset))[:n] if gfid else []


def wmo_doodads(b):
    """MODS sets (name, start, count), MODI ids and per MODD record its MODI index."""
    w = walk(b)
    mods, modd, modi = w.one('MODS'), w.one('MODD'), w.one('MODI')
    sets = []
    for k in range((mods.size if mods else 0) // 32):
        o = mods.data_offset + 32 * k
        sets.append((b[o:o + 20].split(b'\0')[0].decode('latin-1'),) + struct.unpack_from('<II', b, o + 20))
    ids = list(struct.unpack_from('<%dI' % (modi.size // 4), b, modi.data_offset)) if modi else []
    names = [struct.unpack_from('<I', b, modd.data_offset + 40 * k)[0] & 0xFFFFFF for k in range((modd.size if modd else 0) // 40)]
    return sets, ids, names, bool(w.one('MODN'))


def m2_chunks(b):
    """The chunk table of an M2 file: ids are stored in reading order ('MD21', 'SFID', ...), unlike ADT/WMO ids."""
    out, p = {}, 0
    while p + 8 <= len(b):
        cid = b[p:p + 4].decode('latin-1')
        size = struct.unpack_from('<I', b, p + 4)[0]
        if p + 8 + size > len(b):
            out.setdefault('_problem', 'chunk %s at %d runs past the end' % (cid, p))
            break
        out.setdefault(cid, (p + 8, size))
        p += 8 + size
    return out


def m2_info(b):
    """Version, chunk ids, skin/texture/skeleton/bone/animation FileDataIDs, texture slot types, vertex count, box."""
    info = {'first': b[:4].decode('latin-1')}
    if info['first'] == 'MD21':
        ch = m2_chunks(b)
        base = ch['MD21'][0]
        info['chunks'] = sorted(k for k in ch if not k.startswith('_'))
    elif info['first'] == 'MD20':
        ch, base = {}, 0
        info['chunks'] = []
    else:
        info['problem'] = 'neither MD21 nor MD20'
        return info
    if b[base:base + 4] != b'MD20':
        info['problem'] = 'no MD20 payload'
        return info
    info['version'] = struct.unpack_from('<I', b, base + 4)[0]
    info['vertices'] = struct.unpack_from('<I', b, base + 60)[0]
    info['skinProfiles'] = struct.unpack_from('<I', b, base + 68)[0]
    ntex, otex = struct.unpack_from('<II', b, base + 80)
    info['textureTypes'] = [struct.unpack_from('<I', b, base + otex + 16 * i)[0] for i in range(ntex)]
    info['box'] = list(struct.unpack_from('<6f', b, base + 0xA0))

    def ids(cid):
        if cid not in ch:
            return []
        o, s = ch[cid]
        return list(struct.unpack_from('<%dI' % (s // 4), b, o))
    info['sfid'] = ids('SFID')
    info['txid'] = ids('TXID')
    info['skid'] = ids('SKID')
    info['bfid'] = ids('BFID')
    info['afid'] = []
    if 'AFID' in ch:   # (u16 animation id, u16 sub id, u32 FileDataID) per entry
        o, s = ch['AFID']
        info['afid'] = [struct.unpack_from('<I', b, o + 8 * k + 4)[0] for k in range(s // 8)]
    return info


def skin_info(b):
    if b[:4] != b'SKIN':
        return {'problem': 'magic %r' % b[:4]}
    nv, = struct.unpack_from('<I', b, 4)
    ni, = struct.unpack_from('<I', b, 12)
    nsub, = struct.unpack_from('<I', b, 28)
    nbatch, = struct.unpack_from('<I', b, 36)
    return {'vertices': nv, 'indices': ni, 'submeshes': nsub, 'batches': nbatch}


def main():
    install, audit = sys.argv[1], sys.argv[2]
    clients = sys.argv[3:] or ['wow', 'wow_classic_era', 'wow_classic', 'wow_classic_beta']
    exe = os.path.join(audit, 'bin', 'casc_extract.exe')
    data = os.path.join(install, 'Data')
    root = os.path.join(audit, 'world07')
    manifest, census = {'clients': {}}, {'clients': {}}
    for c in clients:
        out = os.path.join(root, c)
        roles = {}

        def keep(st, fids, what):
            for fid in fids:
                if st.get(fid) == 'OK' and fid not in roles:
                    roles[fid] = what(fid) if callable(what) else what

        def read(fid):
            return open(os.path.join(out, '%d.bin' % fid), 'rb').read()
        st = extract(exe, data, c, out, list(WDTS))
        keep(st, WDTS, lambda f: WDTS[f])
        wdt = WDT(read(775971))
        tiles = {}
        for (x, y, main_words, maid) in wdt.tiles():
            if (x, y) in TILES:
                tiles[(x, y)] = {PARTS[i]: maid[i] for i in PARTS if i < len(maid) and maid[i]}
        st = extract(exe, data, c, out, [f for t in tiles.values() for f in t.values()])
        for (x, y), parts in tiles.items():
            keep(st, [parts[p] for p in parts], lambda f, x=x, y=y, parts=parts: '%s azeroth_%d_%d' % ([p for p in parts if parts[p] == f][0], x, y))
        # World models placed on the tiles and the WMO-only maps' global models
        wmos = collections.OrderedDict()
        placements = {}
        for (x, y), parts in tiles.items():
            fid = parts.get('obj0')
            if fid in roles:
                b = read(fid)
                placements[(x, y)] = {'mddf': A.mddf(b, walk(b).one('MDDF')), 'modf': A.modf(b, walk(b).one('MODF'))}
                for r in placements[(x, y)]['modf']:
                    if r['flags'] & 8:
                        wmos[r['name_id']] = 'tile %d_%d' % (x, y)
        globals_ = {}
        for fid in (791060, 780814):
            if fid in roles:
                g = [r for r in WDT(read(fid)).modf if r['flags'] & 8]
                globals_[fid] = g
                for r in g:
                    wmos[r['name_id']] = 'wdt %d' % fid
        st = extract(exe, data, c, out, wmos)
        keep(st, wmos, 'wmo root')
        groups = {}
        wmo_tables = {}
        for fid in wmos:
            if roles.get(fid) != 'wmo root':
                continue
            b = read(fid)
            wmo_tables[fid] = wmo_doodads(b)
            for gfid in lod0_groups(b):
                if gfid:
                    groups[gfid] = fid
        st = extract(exe, data, c, out, groups)
        keep(st, groups, lambda g: 'wmo group of %d' % groups[g])
        # Doodad models: every MDDF name, every MODI entry
        m2s = collections.OrderedDict()
        for (x, y), pl in placements.items():
            for r in pl['mddf']:
                if r['flags'] & 0x40 and r['name_id']:
                    m2s.setdefault(r['name_id'], 'm2 (mddf %d_%d)' % (x, y))
        for fid, (sets, ids, names, modn) in wmo_tables.items():
            for i in ids:
                if i:
                    m2s.setdefault(i, 'm2 (modi of %d)' % fid)
        st = extract(exe, data, c, out, m2s)
        keep(st, m2s, lambda f: m2s[f])
        m2 = {}
        deps = collections.OrderedDict()
        for fid in m2s:
            if roles.get(fid, '').startswith('m2'):
                b = read(fid)
                info = m2_info(b)
                info['bytes'] = len(b)
                m2[fid] = info
                if info.get('sfid'):
                    deps.setdefault(info['sfid'][0], ('skin', fid))
                for t in info.get('txid', []):
                    if t:
                        deps.setdefault(t, ('texture', fid))
                for kind in ('skid', 'bfid', 'afid'):
                    for d in info.get(kind, []):
                        if d:
                            deps.setdefault(d, (kind, fid))
        st = extract(exe, data, c, out, deps)
        keep(st, deps, lambda f: '%s of m2 %d' % deps[f])
        files = {}
        for fid, what in sorted(roles.items()):
            b = read(fid)
            files[str(fid)] = {'role': what, 'bytes': len(b), 'sha256': hashlib.sha256(b).hexdigest()}
        build = build_of(install, c)
        manifest['clients'][c] = {'build': build, 'files': files}

        # ---- census
        size = {int(k): v['bytes'] for k, v in files.items()}
        skins = {}
        for d, (kind, owner) in deps.items():
            if kind == 'skin' and d in size:
                skins[d] = skin_info(read(d))
        missing = {str(f): deps[f][0] for f in deps if f not in size}

        def m2_load(model_ids):
            """What a load of these unique M2s reads: models, first skins, unique textures, bytes."""
            mids = [m for m in model_ids if m in m2]
            sk = {m2[m]['sfid'][0] for m in mids if m2[m].get('sfid')}
            tx = {t for m in mids for t in m2[m].get('txid', []) if t}
            return {'models': len(mids), 'skins': len(sk), 'textures': len(tx),
                    'modelBytes': sum(size.get(m, 0) for m in mids), 'skinBytes': sum(size.get(s, 0) for s in sk),
                    'textureBytes': sum(size.get(t, 0) for t in tx),
                    'vertices': sum(m2[m].get('vertices', 0) for m in mids),
                    'triangles': sum(skins.get(m2[m]['sfid'][0], {}).get('indices', 0) // 3 for m in mids if m2[m].get('sfid'))}
        tiles_out = {}
        for (x, y), pl in placements.items():
            uniq = {}
            for r in pl['mddf']:
                uniq.setdefault(r['unique_id'], r)
            names = collections.Counter(r['name_id'] for r in uniq.values())
            wmo_inst = collections.Counter()
            per_wmo = []
            seen = set()
            for r in pl['modf']:
                if r['unique_id'] in seen or r['name_id'] not in wmo_tables:
                    continue
                seen.add(r['unique_id'])
                sets, ids, dnames, modn = wmo_tables[r['name_id']]
                chosen = r['doodad_set']
                active = [0] + ([chosen] if 0 < chosen < len(sets) else [])
                n_active = 0
                for si in active:
                    s = sets[si]
                    for k in range(s[1], s[1] + s[2]):
                        if k < len(dnames) and dnames[k] < len(ids) and ids[dnames[k]]:
                            wmo_inst[ids[dnames[k]]] += 1
                            n_active += 1
                per_wmo.append({'wmo': r['name_id'], 'uniqueId': r['unique_id'], 'set': chosen, 'sets': [list(s) for s in sets],
                                'doodadsAll': len(dnames), 'doodadsActive': n_active})
            all_models = set(names) | set(wmo_inst)
            tiles_out['%d_%d' % (x, y)] = {
                'mddfRecords': len(pl['mddf']), 'mddfUnique': len(uniq), 'mddfModels': len(names),
                'mddfMostOfOneModel': max(names.values()) if names else 0,
                'mddfScaleNotOne': sum(1 for r in uniq.values() if r['scale'] != 1024),
                'mddfTilted': sum(1 for r in uniq.values() if abs(r['rotation'][0]) > 0.01 or abs(r['rotation'][2]) > 0.01),
                'mddfFlags': dict(collections.Counter(str(r['flags']) for r in uniq.values())),
                'wmoPlacements': per_wmo, 'wmoDoodadInstancesActive': sum(wmo_inst.values()), 'wmoDoodadModelsActive': len(wmo_inst),
                'instancesTotal': len(uniq) + sum(wmo_inst.values()), 'load': m2_load(all_models)}
        maps_out = {}
        for wfid, recs in globals_.items():
            for r in recs:
                if r['name_id'] not in wmo_tables:
                    continue
                sets, ids, dnames, modn = wmo_tables[r['name_id']]
                chosen = r['doodad_set']
                active = [0] + ([chosen] if 0 < chosen < len(sets) else [])
                inst = collections.Counter()
                for si in active:
                    s = sets[si]
                    for k in range(s[1], s[1] + s[2]):
                        if k < len(dnames) and dnames[k] < len(ids) and ids[dnames[k]]:
                            inst[ids[dnames[k]]] += 1
                maps_out[str(wfid)] = {'wmo': r['name_id'], 'set': chosen, 'sets': [list(s) for s in sets], 'modn': modn,
                                       'instancesActive': sum(inst.values()), 'modelsActive': len(inst), 'load': m2_load(inst)}
        versions = collections.Counter(str(v.get('version')) for v in m2.values())
        firsts = collections.Counter(v.get('first') for v in m2.values())
        chunkset = collections.Counter(ch for v in m2.values() for ch in v.get('chunks', []))
        tex_types = collections.Counter(str(t) for v in m2.values() for t in v.get('textureTypes', []))
        replaceable_models = sorted(f for f, v in m2.items() if any(t != 0 for t in v.get('textureTypes', [])))
        largest = lambda kind: max(((size[f], f) for f, (k, o) in deps.items() if k == kind and f in size), default=(0, 0))
        census['clients'][c] = {
            'build': build, 'tiles': tiles_out, 'wmoOnlyMaps': maps_out,
            'm2': {'models': len(m2), 'versions': dict(versions), 'firstChunk': dict(firsts), 'chunks': dict(chunkset),
                   'skinProfiles': dict(collections.Counter(str(v.get('skinProfiles')) for v in m2.values())),
                   'withSkid': sum(1 for v in m2.values() if v.get('skid')), 'withBfid': sum(1 for v in m2.values() if v.get('bfid')),
                   'withAfid': sum(1 for v in m2.values() if v.get('afid')),
                   'noSfid': sorted(f for f, v in m2.items() if not v.get('sfid')),
                   'textureSlotTypes': dict(tex_types), 'modelsWithReplaceableSlots': replaceable_models[:50],
                   'modelsWithReplaceableSlotsCount': len(replaceable_models),
                   'problems': {str(f): v['problem'] for f, v in m2.items() if 'problem' in v},
                   'largestModel': max(((size[f], f) for f in m2 if f in size), default=(0, 0)),
                   'largestSkin': largest('skin'), 'largestTexture': largest('texture'),
                   'missingDependencies': missing}}
        print('%s %s: %d files; tiles %s; maps %s; m2 %d (versions %s)' % (
            c, build, len(files), {k: (v['mddfUnique'], v['wmoDoodadInstancesActive'], v['load']['models']) for k, v in tiles_out.items()},
            {k: (v['instancesActive'], v['modelsActive']) for k, v in maps_out.items()}, len(m2), dict(versions)))
    os.makedirs(root, exist_ok=True)
    with open(os.path.join(root, 'manifest.json'), 'w') as f:
        json.dump(manifest, f, indent=1, sort_keys=True)
    with open(os.path.join(root, 'census.json'), 'w') as f:
        json.dump(census, f, indent=1, sort_keys=True)


if __name__ == '__main__':
    main()
