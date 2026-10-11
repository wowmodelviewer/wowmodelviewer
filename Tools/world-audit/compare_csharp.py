"""Compares the C# world parsers' corpus results with this audit's Python results, key by key.

  python compare_csharp.py <csharp_parsers.json> <python results folder> <python catalog folder> <out.json>

Both sides parse the same pinned bytes independently: the C# side (Tools/UnityRendererProject/Tests/
WorldCorpusValidation.cs) never reads a Python result, and this script only compares the two afterwards.
Integers and strings must be equal; floats must agree within a stated tolerance (1e-9 for values computed the
same way, 6e-4 for means the Python side rounds to 3 decimals). Every difference is listed; nothing is adjusted.
Fields that depend on the listfile (names) exist only on the Python side and are not compared.
"""
import json
import os
import sys

CLIENTS = ('wow', 'wow_classic_beta', 'wow_classic_era', 'wow_classic')


class Cmp:
    def __init__(self):
        self.compared = 0
        self.mismatches = []
        self.sections = {}

    def same(self, where, py, cs, tol=None):
        self.compared += 1
        sec = where.split(' ')[0] + ' ' + where.split(' ')[1] if ' ' in where else where
        self.sections.setdefault(sec, [0, 0])
        self.sections[sec][0] += 1
        ok = equal(py, cs, tol)
        if not ok:
            self.sections[sec][1] += 1
            self.mismatches.append({'where': where, 'python': short(py), 'csharp': short(cs)})
        return ok


LEAVES = [0]


def equal(a, b, tol):
    if not isinstance(a, (dict, list, tuple)):
        LEAVES[0] += 1
    if isinstance(a, bool) or isinstance(b, bool):
        return a == b
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        if tol is None or (isinstance(a, int) and isinstance(b, int)):
            return a == b
        return abs(a - b) <= tol
    if isinstance(a, dict) and isinstance(b, dict):
        return set(a) == set(b) and all(equal(a[k], b[k], tol) for k in a)
    if isinstance(a, (list, tuple)) and isinstance(b, (list, tuple)):
        return len(a) == len(b) and all(equal(x, y, tol) for x, y in zip(a, b))
    if a is None or b is None:
        return a is None and b is None
    return a == b


def short(v):
    s = json.dumps(v, sort_keys=True)
    return s if len(s) <= 600 else s[:600] + '...'


def pairs(v):
    """[[key, count], ...] (Counter.most_common) or {key: count} -> {str(key): count}."""
    if isinstance(v, dict):
        return {str(k): c for k, c in v.items()}
    return {str(k): c for k, c in v}


def load(path):
    return json.load(open(path)) if os.path.exists(path) else None


def main(cs_path, py_results, py_catalog, out_path):
    cs = json.load(open(cs_path))
    m = Cmp()
    for c in CLIENTS:
        cc = cs['clients'][c]

        # T2 / T3: WDT census (Python counts per Map.db2 row that names a readable WDT, C# per extracted file).
        wc = load(os.path.join(py_results, 'wdt_%s.json' % c)) or load(os.path.join(py_catalog, 'wdt_%s.json' % c))
        cen = cc['wdt_census']
        m.same('%s T2 wdts-read' % c, wc['wdt_read'], cen['wdts_parsed'])
        m.same('%s T2 chunk-orders' % c, pairs(wc['chunk_orders']), cen['chunk_orders'])
        m.same('%s T2 mphd-flags' % c, pairs(wc['mphd_flag_values']), cen['mphd_flag_values'])
        for k in range(8):
            cls = wc['maid_word_classes'].get(str(k), {})
            py = {'nonzero': sum(v for n, v in cls.items() if n != 'zero'), 'zero': cls.get('zero', 0)}
            m.same('%s T3 maid-word-%d' % (c, k), py, cen['maid_words'][str(k)])

        # wdt_flags.py
        wf = load(os.path.join(py_results, 'wdt_flags_%s.json' % c))
        for key in ('main_vs_maid', 'mphd_bits_seen', 'mphd_bit_0x1', 'mphd_word_7', 'wdt_modf'):
            m.same('%s flags %s' % (c, key), wf[key], cen[key])
        m.same('%s flags wdts' % c, wf['wdts'], cen['wdts_parsed'])

        # maid_content.py roles by content
        mc = load(os.path.join(py_results, 'maid_content.json'))
        m.same('%s roles maid' % c, mc['maid_roles'][c], cc['maid_roles'])

        # audit_adt.py: per tile and the stats behind T6
        pa = load(os.path.join(py_results, 'adt_%s.json' % c))
        ca = cc['adt']
        m.same('%s adt tiles' % c, sorted(pa['tiles']), sorted(ca['tiles']))
        for key, t in pa['tiles'].items():
            ct = ca['tiles'].get(key, {})
            for part in ('root', 'tex0', 'obj0', 'obj1', 'lod'):
                if part not in t and part not in ct:
                    continue
                if (part in t) != (part in ct):
                    m.same('%s adt %s %s present' % (c, key, part), part in t, part in ct)
                    continue
                m.same('%s adt %s %s top' % (c, key, part), [list(x) for x in t[part]['top']], [list(x) for x in ct[part]['top']])
            if 'root' in t:
                r, cr = t['root'], ct['root']
                m.same('%s adt %s root subchunk_sets' % (c, key), pairs(r['subchunk_sets']), cr['subchunk_sets'])
                for k in ('holes', 'header_offsets', 'position_fit'):
                    m.same('%s adt %s root %s' % (c, key, k), r[k], cr[k])
                pm = r.get('mh2o')
                if pm or cr.get('mh2o'):
                    cm = cr.get('mh2o') or {}
                    for k in ('size', 'chunks_with_liquid', 'instances'):
                        m.same('%s adt %s mh2o %s' % (c, key, k), pm.get(k) if pm else None, cm.get(k))
                    m.same('%s adt %s mh2o liquid_types' % (c, key), pairs(pm['liquid_types']), cm.get('liquid_types'))
                    m.same('%s adt %s mh2o object_or_lvf' % (c, key), pairs(pm['object_or_lvf']), cm.get('object_or_lvf'))
            if 'tex0' in t:
                x, cx = t['tex0'], ct['tex0']
                m.same('%s adt %s tex0 subchunk_sets' % (c, key), pairs(x['subchunk_sets']), cx['subchunk_sets'])
                m.same('%s adt %s tex0 alpha' % (c, key), x['alpha'], cx['alpha'])
                m.same('%s adt %s tex0 layers_per_chunk' % (c, key), pairs(x['layers_per_chunk']), cx['layers_per_chunk'])
                m.same('%s adt %s tex0 textures' % (c, key), {k: x['textures'][k] for k in ('mdid', 'mhid', 'mtxp')}, cx['textures'])
            if 'obj0' in t:
                x, cx = t['obj0'], ct['obj0']
                m.same('%s adt %s obj0 subchunk_sets' % (c, key), pairs(x['subchunk_sets']), cx['subchunk_sets'])
                m.same('%s adt %s obj0 counts' % (c, key), (x['mddf'], x['modf']), (cx['mddf'], cx['modf']))
        for k, v in pa['stats'].items():
            if k in ('area', 'mddf_name', 'modf_name', 'mcly_texture_out_of_range'):
                continue
            m.same('%s T6 stats %s' % (c, k), pairs(v), ca['stats'].get(k))

        # coords.py verdict inputs
        pc = load(os.path.join(py_results, 'coords_%s.json' % c))
        co = cc['coords']
        m.same('%s coords chunks' % c, pc['chunks'], co['chunks'])
        m.same('%s coords outer_vertex_order' % c, pc['outer_vertex_order'], co['outer_vertex_order'], 1e-9)
        m.same('%s coords seam_xy' % c, pc['seam_xy'], co['seam_xy'], 1e-9)
        m.same('%s coords inner_vertex_index' % c, pc['inner_vertex_index'], co['inner_vertex_index'], 1e-9)
        pp = {k: v for k, v in pc['placement'].items() if k != 'rule'}
        m.same('%s coords placement' % c, pp, co['placement'], 1e-9)
        m.same('%s coords holes' % c, pc['holes'], co['holes'])
        m.same('%s coords alpha_orientation' % c, pc['alpha_orientation'], co['alpha_orientation'], 6e-4)
        if 'nibble_order_4bit' in pc or 'nibble_order_4bit' in co:
            m.same('%s coords nibble_order_4bit' % c, pc.get('nibble_order_4bit'), co.get('nibble_order_4bit'), 6e-4)

        # alpha_edges.py
        pe = load(os.path.join(py_results, 'alpha_edges_%s.json' % c))
        ce = cc['alpha_edges']
        for k, v in pe.items():
            m.same('%s alpha_edges %s' % (c, k), v, ce.get(k))

    # rotation.py and mldx.py (Retail)
    cr = cs['clients']['wow']
    pr = load(os.path.join(py_results, 'rotation_wow.json'))
    rot = cr['rotation']
    for k in ('modf_entries', 'unique_placements', 'tested', 'tilted', 'wmos', 'candidates'):
        m.same('wow rotation %s' % k, pr[k], rot[k])
    m.same('wow rotation best', pr['best'], rot['best'], 1e-9)
    m.same('wow rotation best_improper', pr['best_improper'], rot['best_improper'], 1e-9)
    m.same('wow rotation best_without_yaw_offset', pr['best_without_yaw_offset'], rot['best_without_yaw_offset'], 1e-9)
    m.same('wow rotation misfits', [(x['unique_id'], x['wmo']) for x in pr['misfits_of_best']],
           [(x['unique_id'], x['wmo']) for x in rot['placement_transform']['misfits']])
    m.same('wow rotation misfit errors', [x['error'] for x in pr['misfits_of_best']],
           [x['error'] for x in rot['placement_transform']['misfits']], 1e-6)
    pm = load(os.path.join(py_results, 'mldx_wow.json'))
    ml = cr['mldx']
    m.same('wow mldx mldd_equals_mddf', pm['mldd_equals_mddf'], ml['mldd_equals_mddf'])
    for k in ('unique_doodads_with_mldx', 'with_m2_box', 'best', 'best_scale_ignored', 'best_without_yaw_offset'):
        m.same('wow mldx %s' % k, pm[k], ml[k])

    out = {'csharp': cs_path, 'python_results': py_results, 'compared': m.compared, 'scalar_values_compared': LEAVES[0],
           'mismatches': len(m.mismatches),
           'by_section': {k: {'compared': v[0], 'mismatched': v[1]} for k, v in sorted(m.sections.items())},
           'details': m.mismatches}
    json.dump(out, open(out_path, 'w'), indent=1)
    print('%d comparisons (%d scalar values), %d mismatches' % (m.compared, LEAVES[0], len(m.mismatches)))
    for d in m.mismatches[:40]:
        print('  MISMATCH %s\n    python: %s\n    csharp: %s' % (d['where'], d['python'][:300], d['csharp'][:300]))
    return 1 if m.mismatches else 0


if __name__ == '__main__':
    sys.exit(main(*sys.argv[1:5]))
