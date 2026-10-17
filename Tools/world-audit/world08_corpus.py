"""Stage 08 corpus and census: the 3 x 3 city region the streaming acceptance test flies through, per client
(docs/world-viewer/08-streaming.md).

  python -I world08_corpus.py <WoW install root> <audit data folder> [client ...]

For each client (default: wow wow_classic_era wow_classic wow_classic_beta) and the tiles 30-32 x 47-49 of Azeroth
(the region centred on 31_48, Stormwind) it extracts with bin/casc_extract.exe, into <audit data>/world08/<client>/:
  - whole: the WDT, each tile's root, obj0 and tex0 ADT (MAID words 0, 1, 3), the world models the obj0 files place
    (MODF flag 0x8) and every M2 a doodad would load (MDDF flag 0x40 names; MODI entries of set 0 and the placement's set);
  - status and size only: every LOD0 group of those world models and the first skin of every M2;
  - the first 148 bytes (the BLP2 header with its mip table) of every texture: the tiles' MDID and MHID entries, the
    world models' MOMT texture FileDataIDs, the M2s' TXID entries.
It writes <audit data>/world08/census.json: per client the build, the tiles' counts (placements by uniqueId, unique
models), the files and bytes a region load reads by role, and per texture family the BLP encodings, alpha depths,
sizes, stored mip levels, the bytes as stored and as RGBA32 with a full mip chain, and how many could be uploaded as
their own blocks (DXT, whole 4x4 blocks at the top level, every level height a multiple of 4 or below 4, a stored mip
chain) -- the block-compression question of section 1.
"""
import collections
import json
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wowfmt import adt as A  # noqa: E402
from wowfmt.chunks import walk  # noqa: E402
from wowfmt.wdt import WDT  # noqa: E402

AZEROTH = 775971
TILES = [(x, y) for y in (47, 48, 49) for x in (30, 31, 32)]
MAID = {0: 'root', 1: 'obj0', 3: 'tex0'}


def build_of(install, product):
    with open(os.path.join(install, '.build.info'), encoding='utf-8') as f:
        names = [h.split('!')[0] for h in f.readline().rstrip('\n').split('|')]
        for line in f:
            row = dict(zip(names, line.rstrip('\n').split('|')))
            if row.get('Product') == product and row.get('Active') == '1':
                return row.get('Version', '')
    return ''


class Store:
    def __init__(self, exe, data, product, out):
        self.exe, self.data, self.product, self.out = exe, data, product, out
        os.makedirs(out, exist_ok=True)
        self.status = {}
        man = os.path.join(out, 'manifest.tsv')
        if os.path.exists(man):
            self._read(man)

    def _read(self, man):
        for line in open(man):
            p = line.rstrip('\n').split('\t')
            if len(p) >= 3 and p[0].isdigit():
                self.status[int(p[0])] = (p[1], int(p[2]), p[3] if len(p) > 3 else '')

    def get(self, ids, mode='whole'):
        """mode: 'whole', 'header' (first 148 bytes) or 'status'. Fetches the ids not known yet."""
        todo = sorted(set(i for i in ids if i and i not in self.status))
        if todo:
            lst = os.path.join(self.out, '_ids.txt')
            with open(lst, 'w') as f:
                for i in todo:
                    f.write('s %d\n' % i if mode == 'status' else ('%d 148\n' % i if mode == 'header' else '%d\n' % i))
            subprocess.run([self.exe, self.data, self.product, self.out, '-', lst], check=True, capture_output=True)
            self._read(os.path.join(self.out, 'manifest.tsv'))
        return {i: self.status.get(i, ('NOT_EXTRACTED', 0, '')) for i in ids if i}

    def size(self, i):
        st, got, detail = self.status.get(i, ('', 0, ''))
        if detail.startswith('first bytes of '):
            return int(detail.split()[-1])
        return got if st in ('OK', 'ENCRYPTED_PARTIAL') else 0

    def ok(self, i):
        return self.status.get(i, ('',))[0] == 'OK'

    def read(self, i):
        return open(os.path.join(self.out, '%d.bin' % i), 'rb').read()


def u32s(b, c):
    return list(struct.unpack_from('<%dI' % (c.size // 4), b, c.data_offset)) if c else []


def wmo_root(b):
    w = walk(b)
    mohd = w.one('MOHD')
    n_groups = struct.unpack_from('<I', b, mohd.data_offset + 4)[0]
    gfid = u32s(b, w.one('GFID'))[:n_groups]
    momt, motx = w.one('MOMT'), w.one('MOTX')
    tex, named = set(), 0
    for k in range((momt.size if momt else 0) // 64):
        base = momt.data_offset + 64 * k
        for off in (12, 24, 36):
            v = struct.unpack_from('<I', b, base + off)[0]
            if not v:
                continue
            if motx and motx.size > 0:
                named += 1      # the material names its texture by MOTX offset (a path), not by FileDataID
            else:
                tex.add(v)
    mods, modd, modi = w.one('MODS'), w.one('MODD'), w.one('MODI')
    sets = []
    for k in range((mods.size if mods else 0) // 32):
        o = mods.data_offset + 32 * k
        sets.append(struct.unpack_from('<II', b, o + 20))
    ids = u32s(b, modi)
    names = [struct.unpack_from('<I', b, modd.data_offset + 40 * k)[0] & 0xFFFFFF for k in range((modd.size if modd else 0) // 40)]
    return {'gfid': gfid, 'textures': tex, 'texturesNamedByPath': named, 'sets': sets, 'modi': ids, 'moddNames': names}


def m2_chunks(b):
    out, p = {}, 0
    while p + 8 <= len(b):
        cid = b[p:p + 4].decode('latin-1')
        size = struct.unpack_from('<I', b, p + 4)[0]
        if p + 8 + size > len(b):
            break
        out.setdefault(cid, (p + 8, size))
        p += 8 + size
    return out


def m2_deps(b):
    if b[:4] != b'MD21':
        return [], []
    ch = m2_chunks(b)

    def ids(cid):
        if cid not in ch:
            return []
        o, s = ch[cid]
        return list(struct.unpack_from('<%dI' % (s // 4), b, o))
    return ids('SFID')[:1], [t for t in ids('TXID') if t]


def blp(head):
    """The BLP2 header: encoding, alpha depth, alpha type, has-mips flag, size, stored levels and their sizes."""
    if len(head) < 148 or head[:4] != b'BLP2':
        return None
    enc, adepth, atype, hasmips = head[8], head[9], head[10], head[11]
    w, h = struct.unpack_from('<II', head, 12)
    sizes = struct.unpack_from('<16I', head, 84)
    levels = 0
    for s in sizes:
        if s == 0:
            break
        levels += 1
    full = max(w, h).bit_length()
    levels = min(levels, full)
    rgba = sum(max(1, w >> k) * max(1, h >> k) * 4 for k in range(full))
    stored = sum(sizes[:levels])
    fmt = {1: 'palettized', 2: 'dxt', 3: 'bgra'}.get(enc, 'enc%d' % enc)
    if enc == 2:
        fmt = 'dxt1' if (atype == 0 or adepth <= 1) else ('dxt3' if atype == 1 else 'dxt5')
    whole = w % 4 == 0 and h % 4 == 0
    heights_ok = all(((h >> k) % 4 == 0) or (h >> k) < 4 for k in range(levels))
    return {'format': fmt, 'alphaDepth': adepth, 'width': w, 'height': h, 'hasMips': hasmips, 'levels': levels,
            'fullLevels': full, 'stored': stored, 'rgba32': rgba,
            'blockUpload': enc == 2 and whole and heights_ok and levels == full,
            'blockUploadPartialMips': enc == 2 and whole and heights_ok and 1 <= levels < full}


def texture_census(store, ids):
    by = collections.Counter()
    out = {'files': 0, 'notRead': 0, 'bytesStored': 0, 'rgba32': 0, 'formats': collections.Counter(),
           'alphaDepths': collections.Counter(), 'sizes': collections.Counter(), 'blockUpload': 0, 'blockUploadBytes': 0,
           'blockUploadRgba32Equivalent': 0, 'partialMips': 0, 'notWholeBlocks': 0, 'decodeOnly': 0, 'decodeOnlyRgba32': 0}
    for i in sorted(ids):
        if not store.ok(i):
            out['notRead'] += 1
            continue
        h = blp(store.read(i))
        if not h:
            out['notRead'] += 1
            continue
        out['files'] += 1
        out['bytesStored'] += h['stored']
        out['rgba32'] += h['rgba32']
        out['formats'][h['format']] += 1
        out['alphaDepths'][str(h['alphaDepth'])] += 1
        out['sizes']['%dx%d' % (h['width'], h['height'])] += 1
        if h['blockUpload']:
            out['blockUpload'] += 1
            out['blockUploadBytes'] += h['stored']
            out['blockUploadRgba32Equivalent'] += h['rgba32']
        else:
            if h['blockUploadPartialMips']:
                out['partialMips'] += 1
            elif h['format'].startswith('dxt'):
                out['notWholeBlocks'] += 1
            out['decodeOnly'] += 1
            out['decodeOnlyRgba32'] += h['rgba32']
    for k in ('formats', 'alphaDepths'):
        out[k] = dict(out[k])
    out['sizes'] = dict(out['sizes'].most_common(8))
    out['gpuWithBlocks'] = out['blockUploadBytes'] + out['decodeOnlyRgba32']
    return out


def main():
    install, audit = sys.argv[1], sys.argv[2]
    clients = sys.argv[3:] or ['wow', 'wow_classic_era', 'wow_classic', 'wow_classic_beta']
    exe = os.path.join(audit, 'bin', 'casc_extract.exe')
    data = os.path.join(install, 'Data')
    census = {'region': ['%d_%d' % t for t in TILES], 'clients': {}}
    for c in clients:
        s = Store(exe, data, c, os.path.join(audit, 'world08', 'corpus', c))
        s.get([AZEROTH])
        wdt = WDT(s.read(AZEROTH))
        parts = {}
        for (x, y, main_words, maid) in wdt.tiles():
            if (x, y) in TILES:
                parts[(x, y)] = {MAID[k]: maid[k] for k in MAID if k < len(maid) and maid[k]}
        s.get([f for p in parts.values() for f in p.values()])
        mddf, modf, terrain_tex = {}, {}, set()
        tiles_out = {}
        for (x, y) in TILES:
            p = parts.get((x, y), {})
            t = {'root': s.size(p.get('root', 0)), 'obj0': s.size(p.get('obj0', 0)), 'tex0': s.size(p.get('tex0', 0))}
            if s.ok(p.get('tex0', 0)):
                b = s.read(p['tex0'])
                w = walk(b)
                tt = [v for v in u32s(b, w.one('MDID')) + u32s(b, w.one('MHID')) if v]
                t['terrainTextures'] = len(set(tt))
                terrain_tex.update(tt)
            if s.ok(p.get('obj0', 0)):
                b = s.read(p['obj0'])
                w = walk(b)
                md = A.mddf(b, w.one('MDDF')) if w.one('MDDF') else []
                mo = A.modf(b, w.one('MODF')) if w.one('MODF') else []
                t['mddfRecords'], t['modfRecords'] = len(md), len(mo)
                t['mddfUnique'] = len(set(r['unique_id'] for r in md))
                t['modfUnique'] = len(set(r['unique_id'] for r in mo))
                for r in md:
                    mddf.setdefault(r['unique_id'], r)
                for r in mo:
                    modf.setdefault(r['unique_id'], r)
            tiles_out['%d_%d' % (x, y)] = t
        # Placements shared by several tiles (the same uniqueId in more than one obj0).
        seen = collections.Counter()
        for (x, y) in TILES:
            p = parts.get((x, y), {})
            if s.ok(p.get('obj0', 0)):
                b = s.read(p['obj0'])
                w = walk(b)
                for r in set(r['unique_id'] for r in (A.mddf(b, w.one('MDDF')) if w.one('MDDF') else [])):
                    seen[('mddf', r)] += 1
                for r in set(r['unique_id'] for r in (A.modf(b, w.one('MODF')) if w.one('MODF') else [])):
                    seen[('modf', r)] += 1
        wmo_ids = sorted(set(r['name_id'] for r in modf.values() if r['flags'] & 8 and r['name_id']))
        s.get(wmo_ids)
        roots = {f: wmo_root(s.read(f)) for f in wmo_ids if s.ok(f)}
        groups = sorted(set(g for r in roots.values() for g in r['gfid'] if g))
        s.get(groups, 'status')
        wmo_tex = set(t for r in roots.values() for t in r['textures'])
        # Doodad models: MDDF (flag 0x40) and the active sets (set 0 plus the placement's) of every placed world model.
        m2_ids = set(r['name_id'] for r in mddf.values() if r['flags'] & 0x40 and r['name_id'])
        wmo_doodads = 0
        for r in modf.values():
            root = roots.get(r['name_id'])
            if not root:
                continue
            chosen = r['doodad_set']
            active = [0] + ([chosen] if 0 < chosen < len(root['sets']) else [])
            for si in active:
                if si >= len(root['sets']):
                    continue
                start, count = root['sets'][si]
                for k in range(start, min(start + count, len(root['moddNames']))):
                    n = root['moddNames'][k]
                    if n < len(root['modi']) and root['modi'][n]:
                        m2_ids.add(root['modi'][n])
                        wmo_doodads += 1
        m2_ids = sorted(m2_ids)
        s.get(m2_ids)
        skins, m2_tex = set(), set()
        for m in m2_ids:
            if s.ok(m):
                sk, tx = m2_deps(s.read(m))
                skins.update(sk)
                m2_tex.update(tx)
        s.get(sorted(skins), 'status')
        s.get(sorted(terrain_tex | wmo_tex | m2_tex), 'header')
        files = {
            'adt': sum(1 for p in parts.values() for f in p.values() if s.ok(f)),
            'adtBytes': sum(s.size(f) for p in parts.values() for f in p.values()),
            'wmoRoots': len(roots), 'wmoRootBytes': sum(s.size(f) for f in roots),
            'wmoGroups': sum(1 for g in groups if s.ok(g)), 'wmoGroupBytes': sum(s.size(g) for g in groups),
            'm2': sum(1 for m in m2_ids if s.ok(m)), 'm2Bytes': sum(s.size(m) for m in m2_ids),
            'skins': sum(1 for k in skins if s.ok(k)), 'skinBytes': sum(s.size(k) for k in skins),
            'textures': len(terrain_tex | wmo_tex | m2_tex),
            'textureBytes': sum(s.size(t) for t in terrain_tex | wmo_tex | m2_tex),
        }
        files['total'] = files['adt'] + files['wmoRoots'] + files['wmoGroups'] + files['m2'] + files['skins'] + files['textures'] + 1
        files['totalBytes'] = (files['adtBytes'] + files['wmoRootBytes'] + files['wmoGroupBytes'] + files['m2Bytes']
                               + files['skinBytes'] + files['textureBytes'] + s.size(AZEROTH))
        census['clients'][c] = {
            'build': build_of(install, c), 'tiles': tiles_out,
            'placements': {'mddfUnique': len(mddf), 'modfUnique': len(modf), 'wmoDoodadInstancesActive': wmo_doodads,
                           'mddfInSeveralTiles': sum(1 for k, n in seen.items() if k[0] == 'mddf' and n > 1),
                           'modfInSeveralTiles': sum(1 for k, n in seen.items() if k[0] == 'modf' and n > 1),
                           'wmoUnique': len(wmo_ids), 'm2Unique': len(m2_ids),
                           'wmoTexturesNamedByPath': sum(r['texturesNamedByPath'] for r in roots.values())},
            'files': files,
            'textures': {'terrain': texture_census(s, terrain_tex), 'wmo': texture_census(s, wmo_tex),
                         'm2': texture_census(s, m2_tex), 'all': texture_census(s, terrain_tex | wmo_tex | m2_tex)},
            'notRead': {k: sum(1 for i in v if not s.ok(i)) for k, v in
                        (('wmo', wmo_ids), ('groups', groups), ('m2', m2_ids), ('skins', sorted(skins)))},
        }
        cc = census['clients'][c]
        print('== %s %s' % (c, cc['build']))
        print('   placements', json.dumps(cc['placements']))
        print('   files', json.dumps(files))
        for fam in ('terrain', 'wmo', 'm2', 'all'):
            t = cc['textures'][fam]
            print('   textures %-7s files %4d stored %7.1f MB rgba32 %7.1f MB | block %4d (%6.1f MB, as rgba32 %7.1f MB) decode %4d (%6.1f MB rgba32; partial mips %d, not whole %d) -> GPU %7.1f MB | %s' % (
                fam, t['files'], t['bytesStored'] / 1e6, t['rgba32'] / 1e6, t['blockUpload'], t['blockUploadBytes'] / 1e6,
                t['blockUploadRgba32Equivalent'] / 1e6, t['decodeOnly'], t['decodeOnlyRgba32'] / 1e6, t['partialMips'],
                t['notWholeBlocks'], t['gpuWithBlocks'] / 1e6, json.dumps(t['formats'])))
    with open(os.path.join(audit, 'world08', 'census.json'), 'w') as f:
        json.dump(census, f, indent=1, sort_keys=True)


if __name__ == '__main__':
    main()
