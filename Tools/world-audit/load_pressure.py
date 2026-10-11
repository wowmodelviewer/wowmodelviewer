"""Load pressure of a set of tiles, measured from the real files (Retail corpus): what a world view would have to
fetch, decode and upload for one tile, a 3x3 area and a dense city.

  python load_pressure.py <casc_extract.exe> <game> <product> <keys.csv> <corpus folder> <select.json> <assets folder> <out.json>

It extracts what the tiles reference -- M2s (and their first skin), WMO roots and groups, and the first 148 bytes of
every texture (the BLP header with mip sizes) -- and counts:
  requests  -- files a viewer has to read (ADT parts, M2 + skin, WMO root + groups, textures);
  bytes     -- their sizes as stored after CASC decoding;
  gpu       -- vertex/index bytes (terrain 145 vertices and 256 triangles per MCNK; M2 vertices 48 B; WMO MOVT/MOVI)
               and texture bytes (the sum of all mip sizes of each BLP, as stored: block-compressed data uploads as is).
Everything is a count or a sum of sizes read from the files; nothing is timed here.
"""
import collections
import json
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
from wowfmt import adt as A  # noqa: E402
from wowfmt.chunks import walk  # noqa: E402


def walk_plain(buf):
    """M2 chunks keep their ids in reading order ('MD21', 'SFID'...), unlike ADT/WMO chunks."""
    out = {}
    off = 0
    while off + 8 <= len(buf):
        cid = buf[off:off + 4].decode('latin-1')
        size = struct.unpack_from('<I', buf, off + 4)[0]
        if off + 8 + size > len(buf):
            break
        out.setdefault(cid, (off + 8, size))
        off += 8 + size
    return out


def extract(exe, game, product, keys, folder, items, status_only=False):
    """items: [(fdid, byte limit or 0)]; only those not already extracted are fetched. Returns {fdid: (status, bytes)}.
    status_only: nothing written, only the status and the file's size."""
    os.makedirs(folder, exist_ok=True)
    man = os.path.join(folder, 'manifest.tsv')
    done = {}
    if os.path.exists(man):
        for line in open(man):
            p = line.rstrip('\n').split('\t')
            done[p[0]] = (p[1], int(p[2]), p[3] if len(p) > 3 else '')
    todo = [(f, lim) for f, lim in items if str(f) not in done]
    if todo:
        ids = os.path.join(folder, 'ids_more.txt')
        with open(ids, 'w') as f:
            for fid, lim in todo:
                if status_only:
                    f.write('s %d\n' % fid)
                else:
                    f.write('%d %d\n' % (fid, lim) if lim else '%d\n' % fid)
        subprocess.run([exe, game, product, folder, keys, ids], check=True, stderr=subprocess.DEVNULL)
        for line in open(man):
            p = line.rstrip('\n').split('\t')
            done[p[0]] = (p[1], int(p[2]), p[3] if len(p) > 3 else '')
    return done


def full_size(entry):
    status, got, detail = entry
    if detail.startswith('first bytes of '):
        return int(detail.split()[-1])
    return got


def main(exe, game, product, keys, corpus, select_path, assets, out_path):
    sel = json.load(open(select_path))
    metrics = sel['tiles']
    o = sel['scenarios']['ordinary_outdoor']
    d = sel['scenarios']['dense_structures']
    sets = {
        'one ADT (%d_%d)' % (o['x'], o['y']): [(o['x'], o['y'])],
        '3x3 around %d_%d' % (o['x'], o['y']): [(o['x'] + i, o['y'] + j) for j in (-1, 0, 1) for i in (-1, 0, 1)],
        'dense city 3x3 around %d_%d' % (d['x'], d['y']): [(d['x'] + i, d['y'] + j) for j in (-1, 0, 1) for i in (-1, 0, 1)],
    }
    corpus_files = json.load(open(os.path.join(corpus, 'corpus.json')))['files']
    tile_parts = collections.defaultdict(dict)
    for f in corpus_files:
        if 'x' in f:
            tile_parts[(f['x'], f['y'])][f['what']] = f

    # Everything the sets reference, per tile: terrain textures, M2 and WMO placements (with WMO doodad sets).
    per_tile = {}
    m2_all, wmo_all, tex_all = set(), set(), set()
    for tiles in sets.values():
        for (x, y) in tiles:
            if (x, y) in per_tile or (x, y) not in tile_parts:
                continue
            parts = tile_parts[(x, y)]
            info = {'parts': {k: v['bytes'] for k, v in parts.items() if k in ('root', 'tex0', 'obj0', 'obj1', 'lod')},
                    'mcnk': 0, 'terrain_textures': [], 'mddf': [], 'modf': [], 'alpha_layers': 0}
            if 'root' in parts:
                r = A.SplitFile(open(os.path.join(corpus, '%d.bin' % parts['root']['fdid']), 'rb').read(), root=True)
                info['mcnk'] = len(r.mcnk)
            if 'tex0' in parts:
                b = open(os.path.join(corpus, '%d.bin' % parts['tex0']['fdid']), 'rb').read()
                t = A.SplitFile(b, root=False)
                info['terrain_textures'] = [v for v in A.u32s(b, t.walk.one('MDID')) + A.u32s(b, t.walk.one('MHID')) if v]
                info['alpha_layers'] = sum(max(0, len(A.mcly(b, s.one('MCLY'))) - 1) for c, _, s in t.mcnk)
            if 'obj0' in parts:
                b = open(os.path.join(corpus, '%d.bin' % parts['obj0']['fdid']), 'rb').read()
                ob = A.SplitFile(b, root=False)
                # (uniqueId, ...): a placement is listed in every tile it overlaps, identically; counted once by id.
                info['mddf'] = [(m['unique_id'], m['name_id']) for m in A.mddf(b, ob.walk.one('MDDF'))]
                info['modf'] = [(w['unique_id'], w['name_id'], w['doodad_set']) for w in A.modf(b, ob.walk.one('MODF'))]
            per_tile[(x, y)] = info
            m2_all.update(n for u, n in info['mddf'])
            wmo_all.update(w for u, w, s in info['modf'])
            tex_all.update(info['terrain_textures'])

    # WMO roots, then their groups and doodads.
    got = extract(exe, game, product, keys, assets, [(w, 0) for w in sorted(wmo_all)])
    wmo = {}
    group_ids = set()
    for w in sorted(wmo_all):
        path = os.path.join(assets, '%d.bin' % w)
        if got.get(str(w), ('', 0, ''))[0] != 'OK':
            wmo[w] = {'status': got.get(str(w), ('NOT_EXTRACTED',))[0]}
            continue
        b = open(path, 'rb').read()
        wk = walk(b)
        mohd = wk.one('MOHD')
        n_tex, n_groups, n_portals, n_lights, n_dnames, n_ddefs, n_dsets = struct.unpack_from('<7I', b, mohd.data_offset)
        gfid = A.u32s(b, wk.one('GFID'))
        momt = wk.one('MOMT')
        textures = set()
        if momt:
            for k in range(momt.size // 64):
                base = momt.data_offset + 64 * k
                for off in (12, 24, 36):
                    v = struct.unpack_from('<I', b, base + off)[0]
                    if v:
                        textures.add(v)
        modi = A.u32s(b, wk.one('MODI'))
        modd = wk.one('MODD')
        mods = wk.one('MODS')
        sets_ = []
        if mods:
            for k in range(mods.size // 32):
                start, count = struct.unpack_from('<II', b, mods.data_offset + 32 * k + 20)
                sets_.append((start, count))
        dd = []
        if modd:
            for k in range(modd.size // 40):
                word = struct.unpack_from('<I', b, modd.data_offset + 40 * k)[0]
                dd.append(word & 0xFFFFFF)
        # GFID: the LOD0 group files first (MOHD nGroups of them), then each further LOD level's (the player loads LOD0).
        wmo[w] = {'status': 'OK', 'bytes': len(b), 'groups': n_groups, 'gfid': gfid[:n_groups],
                  'gfid_more_lods': gfid[n_groups:], 'textures': sorted(textures),
                  'doodad_sets': sets_, 'modd_name_index': dd, 'modi': modi, 'portals': n_portals, 'lights': n_lights}
        group_ids.update(v for v in gfid if v)
    got = extract(exe, game, product, keys, assets, [(g, 0) for g in sorted(group_ids)])
    groups = {}
    for g in sorted(group_ids):
        e = got.get(str(g))
        if not e or e[0] != 'OK':
            groups[g] = {'status': e[0] if e else 'NOT_EXTRACTED'}
            continue
        b = open(os.path.join(assets, '%d.bin' % g), 'rb').read()
        top = walk(b)
        mogp = top.one('MOGP')
        verts = idx = 0
        if mogp:
            sub = walk(b, mogp.data_offset + 68, mogp.data_offset + mogp.size)
            mv, mi = sub.one('MOVT'), sub.one('MOVI')
            verts = mv.size // 12 if mv else 0
            idx = mi.size // 2 if mi else 0
        groups[g] = {'status': 'OK', 'bytes': len(b), 'vertices': verts, 'indices': idx}

    # WMO doodads join the M2 list (set 0 and the set the placement asks for).
    for (x, y), info in per_tile.items():
        info['wmo_doodads'] = {}
        for uid, w, dset in info['modf']:
            W = wmo.get(w)
            if not W or W['status'] != 'OK':
                continue
            extra = []
            for sidx in {0, dset}:
                if sidx < len(W['doodad_sets']):
                    start, count = W['doodad_sets'][sidx]
                    for k in range(start, min(start + count, len(W['modd_name_index']))):
                        ni = W['modd_name_index'][k]
                        if ni < len(W['modi']) and W['modi'][ni]:
                            extra.append(W['modi'][ni])
            info['wmo_doodads'][uid] = extra
            m2_all.update(extra)

    # M2s and their first skin.
    got = extract(exe, game, product, keys, assets, [(m, 0) for m in sorted(m2_all)])
    m2 = {}
    skins = set()
    for m in sorted(m2_all):
        e = got.get(str(m))
        if not e or e[0] != 'OK':
            m2[m] = {'status': e[0] if e else 'NOT_EXTRACTED'}
            continue
        b = open(os.path.join(assets, '%d.bin' % m), 'rb').read()
        ch = walk_plain(b)
        info = {'status': 'OK', 'bytes': len(b), 'vertices': 0, 'skin': None, 'textures': [], 'deps': []}
        if 'MD21' in ch:
            md20 = ch['MD21'][0]
            info['vertices'] = struct.unpack_from('<I', b, md20 + 60)[0]
        if 'SFID' in ch:
            o_, s_ = ch['SFID']
            if s_ >= 4:
                info['skin'] = struct.unpack_from('<I', b, o_)[0]
                skins.add(info['skin'])
        if 'TXID' in ch:
            o_, s_ = ch['TXID']
            info['textures'] = [v for v in struct.unpack_from('<%dI' % (s_ // 4), b, o_) if v]
        # Files the player may also fetch for the model: its skeleton (SKID), external animations (AFID, 8-byte
        # entries ending in a FileDataID) and bone files (BFID).
        if 'SKID' in ch:
            info['deps'].append(struct.unpack_from('<I', b, ch['SKID'][0])[0])
        if 'AFID' in ch:
            o_, s_ = ch['AFID']
            info['deps'] += [struct.unpack_from('<I', b, o_ + 8 * k + 4)[0] for k in range(s_ // 8)]
        if 'BFID' in ch:
            o_, s_ = ch['BFID']
            info['deps'] += list(struct.unpack_from('<%dI' % (s_ // 4), b, o_))
        info['deps'] = [v for v in info['deps'] if v]
        m2[m] = info
        tex_all.update(info['textures'])
    for W in wmo.values():
        tex_all.update(W.get('textures', []))
    dep_ids = sorted({d for i in m2.values() for d in i.get('deps', [])})
    dep_got = extract(exe, game, product, keys, assets + '_deps', [(d, 0) for d in dep_ids], status_only=True)
    got = extract(exe, game, product, keys, assets, [(s, 0) for s in sorted(skins)])
    skin = {}
    for s in sorted(skins):
        e = got.get(str(s))
        if not e or e[0] != 'OK':
            skin[s] = {'status': e[0] if e else 'NOT_EXTRACTED'}
            continue
        b = open(os.path.join(assets, '%d.bin' % s), 'rb').read()
        skin[s] = {'status': 'OK', 'bytes': len(b), 'indices': struct.unpack_from('<I', b, 12)[0] if len(b) >= 16 else 0}

    # Texture headers (148 bytes): size, format, mip bytes.
    got = extract(exe, game, product, keys, assets + '_blp', [(t, 148) for t in sorted(tex_all)])
    tex = {}
    for t in sorted(tex_all):
        e = got.get(str(t))
        if not e or e[0] != 'OK':
            tex[t] = {'status': e[0] if e else 'NOT_EXTRACTED'}
            continue
        b = open(os.path.join(assets + '_blp', '%d.bin' % t), 'rb').read()
        if b[:4] != b'BLP2' or len(b) < 148:
            tex[t] = {'status': 'NOT_BLP2', 'bytes': full_size(e)}
            continue
        comp, adepth, atype, mips = b[8], b[9], b[10], b[11]
        w, h = struct.unpack_from('<II', b, 12)
        sizes = struct.unpack_from('<16I', b, 84)
        tex[t] = {'status': 'OK', 'bytes': full_size(e), 'width': w, 'height': h, 'compression': comp,
                  'alpha_depth': adepth, 'alpha_type': atype, 'mip_bytes': sum(sizes),
                  # what the player uploads today: RGBA32 with a full mip chain (2.3)
                  'rgba32_bytes': sum(max(1, w >> k) * max(1, h >> k) * 4 for k in range(max(w, h).bit_length()))}

    # Totals per set.
    totals, request_ids = {}, {}
    for name, tiles in sets.items():
        present = [t for t in tiles if t in per_tile]
        m2_inst = collections.Counter()
        wmo_inst = collections.Counter()
        texs = set()
        adt_bytes = 0
        adt_files = 0
        mcnk = 0
        mddf_by_uid, modf_by_uid, doodads_by_modf = {}, {}, {}
        mddf_entries = modf_entries = 0
        for t in present:
            info = per_tile[t]
            adt_bytes += sum(info['parts'].values())
            adt_files += len(info['parts'])
            mcnk += info['mcnk']
            texs.update(info['terrain_textures'])
            mddf_entries += len(info['mddf'])
            modf_entries += len(info['modf'])
            for uid, n in info['mddf']:
                mddf_by_uid[uid] = n
            for uid, w, s in info['modf']:
                modf_by_uid[uid] = w
                doodads_by_modf[uid] = info['wmo_doodads'].get(uid, [])
        m2_inst.update(mddf_by_uid.values())
        for extra in doodads_by_modf.values():
            m2_inst.update(extra)
        wmo_inst.update(modf_by_uid.values())
        m2u = [m2[m] for m in m2_inst if m2.get(m, {}).get('status') == 'OK']
        wmou = [wmo[w] for w in wmo_inst if wmo.get(w, {}).get('status') == 'OK']
        for i in m2u:
            texs.update(i['textures'])
        for i in wmou:
            texs.update(i['textures'])
        grp = [groups[g] for W in wmou for g in W['gfid'] if g in groups and groups[g]['status'] == 'OK']
        grp_more = [groups[g] for W in wmou for g in W['gfid_more_lods'] if g in groups and groups[g]['status'] == 'OK']
        deps = {d for i in m2u for d in i['deps']}
        deps_ok = [d for d in deps if dep_got.get(str(d), ('',))[0] == 'OK']
        alpha_layers = sum(per_tile[t]['alpha_layers'] for t in present)
        sk = [skin[i['skin']] for i in m2u if i['skin'] in skin and skin[i['skin']]['status'] == 'OK']
        tx = [tex[t] for t in texs if tex.get(t, {}).get('status') == 'OK']
        totals[name] = {
            'tiles': len(present), 'adt_files': adt_files, 'adt_bytes': adt_bytes, 'mcnk': mcnk,
            'terrain_vertices': mcnk * 145, 'terrain_triangles': mcnk * 256,
            'm2_unique': len(m2_inst), 'm2_instances': sum(m2_inst.values()),
            'm2_instances_from_wmo_doodad_sets': sum(len(v) for v in doodads_by_modf.values()),
            'mddf_entries_before_dedup': mddf_entries, 'modf_entries_before_dedup': modf_entries,
            'm2_bytes': sum(i['bytes'] for i in m2u), 'm2_vertices_unique': sum(i['vertices'] for i in m2u),
            'm2_vertices_instanced': sum(m2[m]['vertices'] * n for m, n in m2_inst.items() if m2.get(m, {}).get('status') == 'OK'),
            'skin_bytes': sum(s['bytes'] for s in sk), 'skin_indices': sum(s['indices'] for s in sk),
            'm2_dependency_files': len(deps), 'm2_dependency_bytes': sum(dep_got[str(d)][1] for d in deps_ok),
            'm2_dependency_not_read': len(deps) - len(deps_ok),
            'terrain_alpha_layers': alpha_layers,
            'wmo_unique': len(wmo_inst), 'wmo_instances': sum(wmo_inst.values()),
            'wmo_root_bytes': sum(i['bytes'] for i in wmou), 'wmo_groups': len(grp),
            'wmo_groups_further_lods': len(grp_more), 'wmo_group_bytes_further_lods': sum(g['bytes'] for g in grp_more),
            'wmo_group_bytes': sum(g['bytes'] for g in grp), 'wmo_vertices': sum(g['vertices'] for g in grp),
            'wmo_indices': sum(g['indices'] for g in grp),
            'textures_unique': len(texs), 'texture_file_bytes': sum(t['bytes'] for t in tx),
            'texture_mip_bytes': sum(t['mip_bytes'] for t in tx),
            'texture_rgba32_bytes': sum(t['rgba32_bytes'] for t in tx),
            'textures_by_size': dict(collections.Counter('%dx%d' % (t['width'], t['height']) for t in tx).most_common(6)),
            'not_read': {
                'm2': sum(1 for m in m2_inst if m2.get(m, {}).get('status') != 'OK'),
                'wmo': sum(1 for w in wmo_inst if wmo.get(w, {}).get('status') != 'OK'),
                'textures': sum(1 for t in texs if tex.get(t, {}).get('status') != 'OK'),
            },
        }
        tt = totals[name]
        # LOD0 only (what the player loads): ADT parts, M2s + first skin + their dependency files, WMO roots + LOD0
        # groups, textures.
        tt['requests'] = (tt['adt_files'] + tt['m2_unique'] + len(sk) + tt['m2_dependency_files'] + tt['wmo_unique']
                          + tt['wmo_groups'] + tt['textures_unique'])
        tt['bytes_total'] = (tt['adt_bytes'] + tt['m2_bytes'] + tt['skin_bytes'] + tt['m2_dependency_bytes']
                             + tt['wmo_root_bytes'] + tt['wmo_group_bytes'] + tt['texture_file_bytes'])
        # GPU (INFERRED sizes: 32 B per terrain/WMO vertex, 48 B per M2 vertex, 2 B per index, a 64x64 byte map per alpha
        # layer); textures once as stored (block-compressed upload) and once as today's RGBA32 upload (measured).
        geometry = (tt['terrain_vertices'] * 32 + tt['terrain_triangles'] * 6 + tt['m2_vertices_unique'] * 48
                    + tt['skin_indices'] * 2 + tt['wmo_vertices'] * 32 + tt['wmo_indices'] * 2
                    + tt['terrain_alpha_layers'] * 4096)
        tt['gpu_geometry_bytes'] = geometry
        tt['gpu_bytes_estimate'] = geometry + tt['texture_mip_bytes']
        tt['gpu_bytes_with_rgba32_textures'] = geometry + tt['texture_rgba32_bytes']
        # The readable files of the request list above, by FileDataID (read_timing.py reads them back from the client).
        ok = lambda table, k: table.get(k, {}).get('status') == 'OK'  # noqa: E731
        request_ids[name] = sorted(
            {tile_parts[t][p]['fdid'] for t in present for p in per_tile[t]['parts']}
            | {m for m in m2_inst if ok(m2, m)} | {i['skin'] for i in m2u if ok(skin, i['skin'])} | set(deps_ok)
            | {w for w in wmo_inst if ok(wmo, w)} | {g for W in wmou for g in W['gfid'] if ok(groups, g)}
            | {t for t in texs if ok(tex, t)})
    json.dump({'sets': {k: [list(t) for t in v] for k, v in sets.items()}, 'totals': totals, 'request_ids': request_ids},
              open(out_path, 'w'), indent=1)
    for name, t in totals.items():
        print('== %s' % name)
        for k, v in t.items():
            print('   %-36s %s' % (k, v))


if __name__ == '__main__':
    main(*sys.argv[1:9])
