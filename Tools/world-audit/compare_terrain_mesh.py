"""Compare a terrain mesh dump with the independent Python reference (terrain_reference.py).

  python -I compare_terrain_mesh.py <dump> (--reference <reference dump> | --root <root adt> | --corpus <audit data>)
                                    [--json <out.json>]

<dump> is either the C# builder's output written by the console runner (Tools/UnityRendererProject/Tests,
WMV_TERRAIN_DUMP_DIR, source "runner") or the player's dump of what its Unity meshes hold (-wmvTerrainDump, source
"player"). The reference is a reference dump, or is built here from the root ADT file (--root), or from the audit
corpus using the dump's own meta lines (client, rootFileDataID: <corpus>/corpus/<client>/azeroth/<id>.bin).

Both files use the TerrainDump format, version 1 (documented at the top of
Tools/UnityRendererProject/Assets/Scripts/Wow/World/TerrainDump.cs): ASCII lines; every float as its IEEE-754 bit
pattern (8 hex digits, doubles 16), so nothing is lost to formatting:
  wmv-terrain-dump 1 / source / tile x y / origin X Y Z / counts chunks built skipped unsupported vertices triangles
  holeCells / bounds (6 floats or none) / meshbytes / meta key value / root layer active m00..m33 (player only) /
  per chunk: chunk n ix iy state holeMask holeCells triangles, reason, pos (435 floats), nrm (435), idx (decimal),
  cbounds (6 or none), unity layer active subMeshCount topology indexFormat bmin bmax m00..m33 (player only) / end

Acceptance (stage 03 design, section 4):
  - tile, origin, counts and mesh bytes exactly equal; tile and chunk bounds exactly equal (numerically: -0.0 == 0.0);
  - per chunk: state, hole mask, hole cells and triangle count equal; index lists identical;
  - positions within 1e-5 yd (the number of bit-exact values is reported; exact is expected), normals within 1e-6;
  - runner dumps carry arrays for every Built chunk; player dumps exactly for the chunks with triangles (a chunk
    without triangles has no GameObject);
  - player dumps also: every chunk object on layer 30 and active in the hierarchy; root and chunk localToWorld the
    identity; one sub-mesh; topology Triangles; index format UInt16; mesh bounds equal to the reference bounds
    within 1e-4 yd plus 2 float32 ulps of the coordinate (Unity keeps bounds as centre and extents, so min/max can
    move by float rounding);
  - array lengths: 145 positions and 145 normals per chunk, as in the reference, every index inside them; origin and
    bounds three values each (a short or long record never compares over just its overlap);
  - every tolerance test fails on NaN and infinity (a value is out of tolerance unless |difference| <= tolerance).
Exit code: the number of failed checks.
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import terrain_reference as R  # noqa: E402

POSITION_TOLERANCE = 1e-5
NORMAL_TOLERANCE = 1e-6
# Unity keeps a mesh's bounds as centre and extents (float32), each from one rounded sum or difference of min and max,
# and gives min/max back as centre -/+ extents: the round trip moves a value by at most 2 float32 ulps of the larger
# |min|, |max| on that axis (half an ulp each for centre and extents, one for the final rounding; 1 ulp seen in
# simulation). That is 1.22e-4 yd in [1024, 2048) and 2.44e-4 in [2048, 4096), more than a fixed 1e-4, so the
# tolerance is 1e-4 yd plus MESH_BOUNDS_ULPS ulps of that magnitude (mesh_bounds_tolerance).
MESH_BOUNDS_TOLERANCE = 1e-4
MESH_BOUNDS_ULPS = 2
VERTICES = 145
TERRAIN_LAYER = 30
IDENTITY = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]


class Report:
    def __init__(self):
        self.checks = []

    def check(self, ok, name, detail=''):
        self.checks.append({'name': name, 'result': 'PASS' if ok else 'FAIL', 'detail': detail})
        print('  %s  %s%s' % ('PASS' if ok else 'FAIL', name, (' (%s)' % detail) if detail else ''))
        return ok

    @property
    def failed(self):
        return sum(1 for c in self.checks if c['result'] == 'FAIL')


def f32_ulp(v):
    """The gap between adjacent float32 values at magnitude |v| (the subnormal step near 0)."""
    if v == 0:
        return math.ldexp(1.0, -149)
    return math.ldexp(1.0, max(math.frexp(abs(v))[1] - 24, -149))


def mesh_bounds_tolerance(lo, hi):
    """How far Unity's centre/extents round trip may move a bound whose axis spans lo..hi (see MESH_BOUNDS_TOLERANCE)."""
    return MESH_BOUNDS_TOLERANCE + MESH_BOUNDS_ULPS * f32_ulp(max(abs(lo), abs(hi)))


def _worse(m, d):
    """Running maximum for reporting that keeps a NaN once one is seen (max() would skip it)."""
    return d if (d != d or d > m) and m == m else m


def _same_vec(a, b, n=3):
    """Both None, or both exactly n values and equal (numerically: -0.0 == 0.0; NaN never equals)."""
    if a is None or b is None:
        return a is None and b is None
    return len(a) == len(b) == n and all(x == y for x, y in zip(a, b))


def _same_bounds(a, b):
    if a is None or b is None:
        return a is None and b is None
    return len(a) == len(b) == 2 and _same_vec(a[0], b[0]) and _same_vec(a[1], b[1])


def _array_problem(dc, rc):
    """Why a chunk's arrays cannot be compared value for value with the reference's, or None."""
    for key in ('pos', 'nrm'):
        a, b = dc[key], rc[key]
        if len(a) != VERTICES or len(b) != VERTICES or any(len(v) != 3 for v in a) or any(len(v) != 3 for v in b):
            return '%s: %d entries (%d values), reference %d; 145 x 3 expected' % (key, len(a), sum(len(v) for v in a), len(b))
        bits = dc.get(key + '_bits')
        if bits is not None and len(bits) != 3 * VERTICES:
            return '%s: %d raw values, 435 expected' % (key, len(bits))
    bad = [i for i in dc['idx'] if not 0 <= i < len(dc['pos'])]
    if bad:
        return 'index %d outside the %d vertices' % (bad[0], len(dc['pos']))
    return None


def compare(dump, ref, report=None):
    """Compare a parsed dump with a reference tile (both in read_dump's shape). Returns the Report."""
    rep = report or Report()
    player = dump.get('source') == 'player'
    label = 'tile %d_%d (%s)' % (tuple(dump['tile']) + (dump.get('source'),))
    rep.check(tuple(dump['tile']) == tuple(ref['tile']), label + ': tile', '%s vs reference %s' % (dump['tile'], ref['tile']))
    rep.check(len(dump['origin']) == len(ref['origin']) == 3 and all(R.d_bits(a) == R.d_bits(b) for a, b in zip(dump['origin'], ref['origin'])),
              label + ': origin bit-equal', '%r' % (dump['origin'],))
    rep.check(dump['counts'] == ref['counts'], label + ': counts', 'dump %s, reference %s' % (dump['counts'], ref['counts']))
    rep.check(dump['meshbytes'] == ref['meshbytes'], label + ': mesh bytes', '%d vs %d' % (dump['meshbytes'], ref['meshbytes']))
    rep.check(_same_bounds(dump['bounds'], ref['bounds']), label + ': tile bounds equal', '%s' % (dump['bounds'],))
    rep.check(len(dump['chunks']) == 256 and [c['n'] for c in dump['chunks']] == list(range(256)), label + ': 256 chunk records in order')

    by_n = {c['n']: c for c in dump['chunks']}
    state_bad, idx_bad, bounds_bad, presence_bad, length_bad = [], [], [], [], []
    pos_n = pos_exact = pos_bits = pos_bad = nrm_n = nrm_exact = nrm_bad = 0
    pos_max = nrm_max = 0.0
    unity_bad = []
    for rc in ref['chunks']:
        n = rc['n']
        dc = by_n.get(n)
        if dc is None:
            state_bad.append('%d missing' % n)
            continue
        if (dc['state'], dc['hole_mask'], dc['hole_cells'], dc['triangles'], dc['ix'], dc['iy']) != \
                (rc['state'], rc['hole_mask'], rc['hole_cells'], rc['triangles'], rc['ix'], rc['iy']):
            state_bad.append('%d: %s/%x/%d/%d vs %s/%x/%d/%d' % (n, dc['state'], dc['hole_mask'], dc['hole_cells'], dc['triangles'],
                                                              rc['state'], rc['hole_mask'], rc['hole_cells'], rc['triangles']))
            continue
        has_arrays = dc['pos'] is not None and dc['nrm'] is not None and dc['idx'] is not None
        want_arrays = rc['triangles'] > 0 if player else rc['state'] == 'Built'
        if has_arrays != want_arrays or (not has_arrays and (dc['pos'] is not None or dc['nrm'] is not None or dc['idx'] is not None)):
            presence_bad.append('%d: arrays %s, expected %s' % (n, 'present' if has_arrays else 'absent', 'present' if want_arrays else 'absent'))
            continue
        if not has_arrays:
            if player and dc['unity'] is not None:
                presence_bad.append('%d: a unity record for a chunk without a mesh' % n)
            continue
        problem = _array_problem(dc, rc)
        if problem is not None:
            length_bad.append('%d: %s' % (n, problem))
            continue
        if dc['idx'] != rc['idx']:
            idx_bad.append(n)
        if not _same_bounds(dc['cbounds'], rc['cbounds']):
            bounds_bad.append(n)
        # Out of tolerance unless |difference| <= tolerance: NaN and infinity count as bad (a running max skips NaN).
        bits = dc.get('pos_bits')
        for i, (a, b) in enumerate(zip([v for p in dc['pos'] for v in p], [v for p in rc['pos'] for v in p], strict=True)):
            d = abs(a - b)
            pos_max = _worse(pos_max, d)
            pos_n += 1
            pos_bad += not (d <= POSITION_TOLERANCE)
            pos_exact += d == 0
            pos_bits += bits is not None and bits[i] == R.f32_bits(b)
        for a, b in zip([v for p in dc['nrm'] for v in p], [v for p in rc['nrm'] for v in p], strict=True):
            d = abs(a - b)
            nrm_max = _worse(nrm_max, d)
            nrm_n += 1
            nrm_bad += not (d <= NORMAL_TOLERANCE)
            nrm_exact += d == 0
        if player:
            u = dc['unity']
            if u is None:
                unity_bad.append('%d: no unity record' % n)
                continue
            problems = []
            if u['layer'] != TERRAIN_LAYER:
                problems.append('layer %d' % u['layer'])
            if not u['active']:
                problems.append('inactive')
            if u['sub_mesh_count'] != 1:
                problems.append('%d sub-meshes' % u['sub_mesh_count'])
            if u['topology'] != 'Triangles':
                problems.append('topology ' + u['topology'])
            if u['index_format'] != 'UInt16':
                problems.append('index format ' + u['index_format'])
            if u['local_to_world'] != IDENTITY:
                problems.append('localToWorld not identity')
            if rc['cbounds'] is not None:
                mb, cb = u['mesh_bounds'], rc['cbounds']
                if len(mb) != 2 or any(len(v) != 3 for v in mb):
                    problems.append('mesh bounds of %s values' % [len(v) for v in mb])
                else:
                    err, off = 0.0, 0
                    for a in range(3):
                        tol = mesh_bounds_tolerance(cb[0][a], cb[1][a])
                        for i in range(2):
                            d = abs(mb[i][a] - cb[i][a])
                            err = _worse(err, d)
                            off += not (d <= tol)
                    if off:
                        problems.append('mesh bounds off by %g (%d value(s) beyond 1e-4 yd + %d ulps)' % (err, off, MESH_BOUNDS_ULPS))
            if problems:
                unity_bad.append('%d: %s' % (n, ', '.join(problems)))

    rep.check(not state_bad, label + ': chunk states, hole masks, hole cells and triangle counts equal',
              '; '.join(state_bad[:5]) if state_bad else '256 chunks')
    rep.check(not presence_bad, label + ': arrays present exactly for the chunks that ' + ('have triangles' if player else 'are Built'),
              '; '.join(presence_bad[:5]))
    rep.check(not length_bad, label + ': array lengths (145 x 3 values per chunk array, every index inside them)',
              '; '.join(length_bad[:5]))
    rep.check(not idx_bad, label + ': index lists identical', 'chunks %s' % idx_bad[:10] if idx_bad else '')
    rep.check(not bounds_bad, label + ': chunk bounds equal (referenced vertices)', 'chunks %s' % bounds_bad[:10] if bounds_bad else '')
    rep.check(pos_n > 0 and pos_bad == 0, label + ': positions within 1e-5 yd',
              '%d values, %d out of tolerance, max difference %g, %d numerically exact, %d bit-exact'
              % (pos_n, pos_bad, pos_max, pos_exact, pos_bits))
    rep.check(nrm_n > 0 and nrm_bad == 0, label + ': normals within 1e-6',
              '%d values, %d out of tolerance, max difference %g, %d exact' % (nrm_n, nrm_bad, nrm_max, nrm_exact))
    if player:
        root = dump.get('root')
        rep.check(root is not None and root['local_to_world'] == IDENTITY and root['active'] and root['layer'] == TERRAIN_LAYER,
                  label + ': root on layer 30, active, localToWorld identity', '%s' % (root,) if root is None or root['local_to_world'] != IDENTITY else '')
        rep.check(not unity_bad, label + ': every chunk object on layer 30, active, identity, 1 sub-mesh, Triangles, UInt16, mesh bounds = reference',
                  '; '.join(unity_bad[:5]))
    return rep


def reference_for(dump, args):
    if args.get('reference'):
        return R.read_dump(args['reference'])
    root = args.get('root')
    if not root:
        corpus = args.get('corpus')
        client, fdid = R.meta(dump, 'client'), R.meta(dump, 'rootFileDataID')
        if not corpus or not client or not fdid:
            raise SystemExit('no reference: give --reference, --root, or --corpus with a dump that names client and rootFileDataID')
        root = os.path.join(corpus, 'corpus', client, 'azeroth', '%s.bin' % fdid)
    tx, ty = dump['tile']
    return R.build_tile(R.read_bytes(root), tx, ty)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    args = {}
    rest = argv[2:]
    while rest:
        k = rest.pop(0)
        if k in ('--reference', '--root', '--corpus', '--json') and rest:
            args[k[2:]] = rest.pop(0)
        else:
            print('unknown argument %r' % k)
            return 2
    dump = R.read_dump(argv[1])
    ref = reference_for(dump, args)
    print('compare %s (source %s) with the reference' % (argv[1], dump.get('source')))
    rep = compare(dump, ref)
    print('RESULT: %s (%d checks, %d failed)' % ('PASS' if rep.failed == 0 else 'FAIL', len(rep.checks), rep.failed))
    if args.get('json'):
        with open(args['json'], 'w') as f:
            json.dump({'dump': argv[1], 'source': dump.get('source'), 'checks': rep.checks}, f, indent=1)
    return rep.failed


if __name__ == '__main__':
    sys.exit(main(sys.argv))
