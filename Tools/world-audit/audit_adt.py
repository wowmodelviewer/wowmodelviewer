"""ADT audit of an extracted corpus (corpus.py): structure proofs, field census and content hashes, per tile.

  python audit_adt.py <corpus folder> <listfile.csv> <out.json> [<WDT file>]

What it tests against the bytes, for every tile (and reports as counts, so a reader can see how strong the proof is):
  - which top-level chunks each split part (root/tex0/obj0/obj1/lod) has, in order;
  - root MCNKs carry a 128-byte header (IndexX/IndexY = chunk order n%16, n//16; sub-chunks walk cleanly after it),
    tex0/obj0/obj1 MCNKs do not (sub-chunks from their first byte);
  - the header's offsets: ofs_height/ofs_normal/... equal the sub-chunk positions, measured from the MCNK chunk start,
    unless flag 0x10000 is set (then bytes 20..27 are not offsets: the 64-bit high-resolution hole mask);
  - the root header's layer count (its value) against the MCLY count in tex0;
  - MCAL: every layer after the first decoded by its MCLY flag 0x200 (compressed: the RLE must end exactly where the
    next layer's data starts, a real test) or as raw data (then only the spacing of the offsets is measured: 2048 or
    4096, first offset 0); MCLY flag 0x100 is counted against the layer's position (first or later);
  - MDDF/MODF name ids: flag 0x40 (MDDF) / 0x8 (MODF) = a FileDataID (checked: listfile extension .m2/.wmo),
    else an index into MMID/MWID;
  - MH2O instance bounds;
  - MCNK position against the tile/chunk numbers (several candidate formulas; the fitting one is reported).
"""
import collections
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from wowfmt import adt as A  # noqa: E402
from wowfmt.chunks import sha256, walk  # noqa: E402
from wowfmt.listfile import Listfile  # noqa: E402
from wowfmt.wdt import WDT  # noqa: E402

TILE = 1600.0 / 3.0          # 533.333.. yards: the conventional tile size, TESTED below against MCNK positions
CHUNK = TILE / 16.0


def load_corpus(folder):
    c = json.load(open(os.path.join(folder, 'corpus.json')))
    tiles = collections.defaultdict(dict)
    side = {}
    for f in c['files']:
        path = os.path.join(folder, '%d.bin' % f['fdid'])
        if 'x' in f:
            tiles[(f['x'], f['y'])][f['what']] = (f['fdid'], path if f['status'] in ('OK', 'ENCRYPTED_PARTIAL') else None, f['status'])
        else:
            side[f['what']] = (f['fdid'], path if f['status'] in ('OK', 'ENCRYPTED_PARTIAL') else None, f['status'])
    return c, tiles, side


def audit_root(buf, tx, ty, stats):
    r = A.SplitFile(buf, root=True)
    out = {'top': r.top(), 'problems': r.problems, 'mcnk': len(r.mcnk)}
    pos_fit = collections.Counter()
    holes = collections.Counter()
    sub_sets = collections.Counter()
    offsets = collections.Counter()
    heights = []
    for n, (c, h, sub) in enumerate(r.mcnk):
        if h is None:
            continue
        stats['root_header_index_ok'][(h['index_x'], h['index_y']) == (n % 16, n // 16)] += 1
        sub_sets[' '.join(i for i, k in sub.ids())] += 1
        hi = bool(h['flags'] & 0x10000)
        holes['flag_0x10000' if hi else 'no_flag_0x10000'] += 1
        if hi:
            holes['high_mask_nonzero' if h['holes_high'] else 'high_mask_zero'] += 1
            holes['low_nonzero_with_flag' if h['holes_low'] else 'low_zero_with_flag'] += 1
        else:
            holes['low_nonzero' if h['holes_low'] else 'low_zero'] += 1
        # Header offsets vs the sub-chunks' real positions (from the MCNK chunk's own start, header included).
        pos = {s.id: s.offset - c.offset for s in sub.chunks}
        for field, sid in (('ofs_height', 'MCVT'), ('ofs_normal', 'MCNR'), ('ofs_mccv', 'MCCV'), ('ofs_mclv', 'MCLV'),
                           ('ofs_snd', 'MCSE'), ('ofs_liquid', 'MCLQ')):
            if field in ('ofs_height', 'ofs_normal') and hi:
                continue
            v = h[field]
            if sid in pos:
                offsets['%s=%s start' % (field, sid) if v == pos[sid] else '%s!=%s start (%d vs %d)' % (field, sid, v, pos[sid])] += 1
            elif v:
                offsets['%s=%d but no %s' % (field, v, sid)] += 1
        # Position formulas: which of them reproduce every MCNK's position?
        px, py, pz = h['position']
        cand = {
            'x=(32-ty)*T-iy*C, y=(32-tx)*T-ix*C': ((32 - ty) * TILE - h['index_y'] * CHUNK, (32 - tx) * TILE - h['index_x'] * CHUNK),
            'x=(32-tx)*T-ix*C, y=(32-ty)*T-iy*C': ((32 - tx) * TILE - h['index_x'] * CHUNK, (32 - ty) * TILE - h['index_y'] * CHUNK),
            'x=(tx-32)*T+ix*C, y=(ty-32)*T+iy*C': ((tx - 32) * TILE + h['index_x'] * CHUNK, (ty - 32) * TILE + h['index_y'] * CHUNK),
        }
        for name, (ex, ey) in cand.items():
            pos_fit[name + (' fits' if abs(ex - px) < 0.01 and abs(ey - py) < 0.01 else ' does not fit')] += 1
        mv = sub.one('MCVT')
        hv = A.mcvt(buf, mv)
        if hv:
            heights.append((n, h['position'], hv))
        stats['flags'][h['flags']] += 1
        stats['area'][h['area_id']] += 1
    out.update({'subchunk_sets': sub_sets.most_common(), 'holes': dict(holes), 'header_offsets': dict(offsets),
                'position_fit': dict(pos_fit)})
    out['mh2o'] = None
    mh = r.walk.one('MH2O')
    if mh:
        m = A.mh2o(buf, mh)
        inst = [i for c in m['chunks'] for i in c['instances']]
        out['mh2o'] = {'size': mh.size, 'chunks_with_liquid': sum(1 for c in m['chunks'] if c['layers']),
                       'instances': len(inst), 'liquid_types': collections.Counter(i['liquid_type'] for i in inst),
                       'object_or_lvf': collections.Counter(i['object_or_lvf'] for i in inst),
                       'problems': m['problems']}
    return out, r, heights


def audit_tex(buf, root_split, mphd_flags, stats):
    t = A.SplitFile(buf, root=False)
    out = {'top': t.top(), 'problems': t.problems, 'mcnk': len(t.mcnk)}
    mdid = A.u32s(buf, t.walk.one('MDID'))
    mhid = A.u32s(buf, t.walk.one('MHID'))
    mtex = A.strings(buf, t.walk.one('MTEX'))
    out['textures'] = {'mdid': len(mdid), 'mhid': len(mhid), 'mtex': len(mtex),
                       'mtxp': (t.walk.one('MTXP').size // 16) if t.walk.one('MTXP') else 0,
                       'mtxf': (t.walk.one('MTXF').size // 4) if t.walk.one('MTXF') else 0}
    out['mdid_fdids'] = mdid
    mtxp = t.walk.one('MTXP')
    out['mtxp'] = [struct.unpack_from('<Iffi', buf, mtxp.data_offset + 16 * k) for k in range(mtxp.size // 16)] if mtxp else []
    layer_hist = collections.Counter()
    sub_sets = collections.Counter()
    alpha = collections.Counter()
    for n, (c, _, sub) in enumerate(t.mcnk):
        sub_sets[' '.join(i for i, k in sub.ids())] += 1
        layers = A.mcly(buf, sub.one('MCLY'))
        layer_hist[len(layers)] += 1
        for k, L in enumerate(layers):
            stats['mcly_0x100_by_position'][('first' if k == 0 else 'later', bool(L['flags'] & 0x100))] += 1
        if root_split and n < len(root_split.mcnk) and root_split.mcnk[n][1]:
            rh = root_split.mcnk[n][1]
            stats['root_n_layers_value'][rh['n_layers']] += 1
            stats['root_alpha_shadow_offsets_all_zero'][not any(rh[k] for k in ('ofs_alpha', 'size_alpha', 'ofs_shadow', 'size_shadow'))] += 1
            stats['mcsh_vs_root_flag1'][('MCSH' in [s.id for s in sub.chunks], bool(rh['flags'] & 1))] += 1
        for L in layers:
            if L['texture'] >= max(len(mdid), len(mtex)):
                stats['mcly_texture_out_of_range'][True] += 1
        mcal = sub.one('MCAL')
        if not mcal:
            if len(layers) > 1:
                alpha['layers>1 but no MCAL'] += 1
            continue
        # Decode every layer after the first from its offset; lengths must tile MCAL exactly.
        total = 0
        ok = True
        kinds = []
        alpha_layers = [L for L in layers[1:]]
        for k, L in enumerate(alpha_layers):
            start = mcal.data_offset + L['alpha_offset']
            if L['alpha_offset'] != total:
                ok = False
            if L['flags'] & 0x200:
                vals, used = A.rle_alpha(buf, start, mcal.data_offset + mcal.size)
                kinds.append('rle')
                if vals is None:
                    ok = False
            else:
                nxt = alpha_layers[k + 1]['alpha_offset'] if k + 1 < len(alpha_layers) else mcal.size
                used = nxt - L['alpha_offset']
                kinds.append('raw%d' % used)
                stats['raw_alpha_spacing'][used] += 1
                if used not in (2048, 4096):
                    ok = False
            total += used
        kind = '+'.join(sorted(set(kinds))) if kinds else 'none'
        if ok and total == mcal.size:
            verdict = ('RLE ends where the next layer starts, raw spacing 2048/4096' if 'rle' in kind
                       else 'raw spacing 2048/4096, first offset 0')
        else:
            verdict = 'MISMATCH %d of %d' % (total, mcal.size)
        alpha['%s: %s' % (kind, verdict)] += 1
        for L in layers:
            stats['mcly_flags'][L['flags']] += 1
    out.update({'subchunk_sets': sub_sets.most_common(), 'layers_per_chunk': dict(layer_hist), 'alpha': dict(alpha),
                'mphd_flags': mphd_flags})
    return out


def audit_obj0(buf, lf, stats):
    o = A.SplitFile(buf, root=False)
    out = {'top': o.top(), 'problems': o.problems, 'mcnk': len(o.mcnk)}
    md = A.mddf(buf, o.walk.one('MDDF'))
    mo = A.modf(buf, o.walk.one('MODF'))
    mmid = A.u32s(buf, o.walk.one('MMID'))
    mwid = A.u32s(buf, o.walk.one('MWID'))
    out['mddf'] = len(md)
    out['modf'] = len(mo)
    out['mmdx_names'] = len(A.strings(buf, o.walk.one('MMDX')))
    out['mwmo_names'] = len(A.strings(buf, o.walk.one('MWMO')))
    for d in md:
        isfid = bool(d['flags'] & 0x40)
        p = lf.path_of(d['name_id']) if isfid else None
        stats['mddf_name'][('fdid' if isfid else 'index') + (':' + (p.rsplit('.', 1)[-1] if p else 'unnamed') if isfid else
                                                             (':in MMID' if d['name_id'] < len(mmid) else ':OUT OF RANGE'))] += 1
        stats['mddf_flags'][d['flags']] += 1
    for w in mo:
        isfid = bool(w['flags'] & 0x8)
        p = lf.path_of(w['name_id']) if isfid else None
        stats['modf_name'][('fdid' if isfid else 'index') + (':' + (p.rsplit('.', 1)[-1] if p else 'unnamed') if isfid else
                                                             (':in MWID' if w['name_id'] < len(mwid) else ':OUT OF RANGE'))] += 1
        stats['modf_flags'][w['flags']] += 1
    refs = collections.Counter()
    for n, (c, _, sub) in enumerate(o.mcnk):
        refs[' '.join(i for i, k in sub.ids())] += 1
    out['subchunk_sets'] = refs.most_common()
    return out, md, mo


def main(folder, listfile_path, out_path, wdt_path=None):
    lf = Listfile(listfile_path)
    corpus, tiles, side = load_corpus(folder)
    wdt = WDT(open(wdt_path or corpus['wdt'], 'rb').read())
    mphd_flags = wdt.mphd_words[0] if wdt.mphd_words else None
    stats = collections.defaultdict(collections.Counter)
    out_tiles = {}
    placements = {}
    for (tx, ty), parts in sorted(tiles.items()):
        t = {'files': {}}
        root_split = None
        for part, (fid, path, status) in sorted(parts.items()):
            entry = {'fdid': fid, 'status': status, 'listfile': lf.path_of(fid)}
            if not path:
                t['files'][part] = entry
                continue
            buf = open(path, 'rb').read()
            entry.update({'bytes': len(buf), 'sha256': sha256(buf)})
            t['files'][part] = entry
        if 'root' in parts and parts['root'][1]:
            buf = open(parts['root'][1], 'rb').read()
            t['root'], root_split, heights = audit_root(buf, tx, ty, stats)
        if 'tex0' in parts and parts['tex0'][1]:
            t['tex0'] = audit_tex(open(parts['tex0'][1], 'rb').read(), root_split, mphd_flags, stats)
        if 'obj0' in parts and parts['obj0'][1]:
            t['obj0'], md, mo = audit_obj0(open(parts['obj0'][1], 'rb').read(), lf, stats)
            placements[(tx, ty)] = (md, mo)
        for part in ('obj1', 'lod'):
            if part in parts and parts[part][1]:
                b = open(parts[part][1], 'rb').read()
                w = walk(b)
                t[part] = {'top': w.ids(), 'problems': w.problems,
                           'sizes': {c.id: c.size for c in w.chunks if c.id != 'MCNK'}}
        out_tiles['%d_%d' % (tx, ty)] = t
    result = {
        'corpus': folder, 'mphd_flags': mphd_flags, 'tiles': out_tiles,
        'side_files': {k: {'fdid': v[0], 'status': v[2], 'listfile': lf.path_of(v[0]),
                           'sha256': sha256(open(v[1], 'rb').read()) if v[1] else None,
                           'top': walk(open(v[1], 'rb').read()).ids() if v[1] else None} for k, v in side.items()},
        'stats': {k: {str(a): b for a, b in v.items()} for k, v in stats.items()},
    }
    json.dump(result, open(out_path, 'w'), indent=1, default=lambda o: dict(o) if isinstance(o, collections.Counter) else list(o))
    print('%s: %d tiles' % (folder, len(out_tiles)))
    for k in sorted(stats):
        if k in ('area', 'flags'):
            continue
        print('  %s: %s' % (k, dict(stats[k].most_common(8))))


if __name__ == '__main__':
    main(*sys.argv[1:])
