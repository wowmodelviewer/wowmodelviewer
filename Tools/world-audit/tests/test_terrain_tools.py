"""Tests of the stage 03 terrain tools: terrain_reference.py (the independent mesh reference and the dump format),
compare_terrain_mesh.py (reference against a C# or player dump) and minimap_orientation.py (the orientation proof).

Synthetic roots are built here from format structure only (like the C# AdtSynthetic kit); the corpus tests run when
WMV_WORLD_AUDIT_CORPUS names the extracted audit data, and the runner-dump comparison when WMV_TERRAIN_DUMP_DIR also
holds the dumps the C# runner wrote (Tools/UnityRendererProject/Tests, TerrainCorpusTests).

  python -I -m unittest discover -s Tools/world-audit/tests
"""
import contextlib
import io
import math
import os
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
import compare_terrain_mesh as CMP  # noqa: E402
import minimap_orientation as MO  # noqa: E402
import terrain_reference as R  # noqa: E402
from wowfmt.wdt import WDT  # noqa: E402

ROOT = os.environ.get('WMV_WORLD_AUDIT_CORPUS')
DUMPS = os.environ.get('WMV_TERRAIN_DUMP_DIR')
T = R.T
U = R.U


def need_data(test):
    return unittest.skipUnless(ROOT, 'WMV_WORLD_AUDIT_CORPUS not set (the audit data is extracted locally)')(test)


# ------------------------------------------------------------------------------------------------- synthetic roots

def chunk(cid, data):
    return cid[::-1].encode('latin-1') + struct.pack('<I', len(data)) + data


def root_mcnk(n, tx, ty, flags=0x10000, holes=0, low=0, base=50.0, height=None, dx=0.0, dy=0.0, short=False, mcnr=(448,), mccv=()):
    """mcnr / mccv: the byte sizes of the MCNR / MCCV sub-chunks to write (one well-formed MCNR, no MCCV by default)."""
    if short:
        return chunk('MCNK', bytes(100))
    h = bytearray(128)
    struct.pack_into('<III', h, 0, flags, n % 16, n // 16)
    struct.pack_into('<Q', h, 20, holes)
    struct.pack_into('<H', h, 60, low)
    x, y = R.chunk_corner(tx, ty, n % 16, n // 16)
    struct.pack_into('<3f', h, 104, x + dx, y + dy, base)
    mcvt = struct.pack('<145f', *[(height(n, k) if height else k * 0.25) for k in range(145)])
    return chunk('MCNK', bytes(h) + chunk('MCVT', mcvt) + b''.join(chunk('MCCV', bytes(s)) for s in mccv)
                 + b''.join(chunk('MCNR', bytes(s)) for s in mcnr) + chunk('MCSE', b''))


def root(tx, ty, **per_chunk):
    """MVER + 256 MCNKs; keyword values are functions of n giving root_mcnk's argument (or constants)."""
    parts = [chunk('MVER', struct.pack('<I', 18))]
    for n in range(256):
        kw = {k: (v(n) if callable(v) and k != 'height' else v) for k, v in per_chunk.items()}
        parts.append(root_mcnk(n, tx, ty, **kw))
    return b''.join(parts)


def world_height(tx, ty, z, base):
    def h(n, k):
        x, y = R.vertex_xy(tx, ty, n % 16, n // 16, k)
        return z(x, y) - base
    return h


def bumpy(tx, ty, base=60.0):
    return world_height(tx, ty, lambda x, y: 60 + 20 * math.sin(x / 37.0) + 15 * math.cos(y / 23.0) + 0.05 * x
                        + 3 * math.sin((x + y) / 7.0), base)


def quiet(fn, *args):
    with contextlib.redirect_stdout(io.StringIO()):
        return fn(*args)


class Reference(unittest.TestCase):
    def test_origin_is_exact(self):
        for t in range(64):
            X0, Y0, Z0 = R.tile_origin(t, t)
            self.assertEqual(X0, (32 - t) * T)
            self.assertEqual(Y0, (32 - t) * T)
            self.assertEqual(Z0, 0.0)

    def test_flat_tile(self):
        tile = R.build_tile(root(32, 50, height=lambda n, k: 0.0), 32, 50)
        c = tile['counts']
        self.assertEqual((c['built'], c['vertices'], c['triangles'], c['hole_cells']), (256, 37120, 65536, 0))
        self.assertEqual(tile['meshbytes'], 256 * (145 * 24 + 768 * 2))
        for ch in tile['chunks']:
            self.assertTrue(all(n == (0.0, 1.0, 0.0) for n in ch['nrm']))
            self.assertTrue(all(p[1] == 50.0 for p in ch['pos']))
        # first row (z) and column (x) exactly 0; east = +X, north = +Z
        for i in range(16):
            for j in range(9):
                self.assertEqual(tile['chunks'][i]['pos'][R.outer(0, j)][2], 0.0)
                self.assertEqual(tile['chunks'][i * 16]['pos'][R.outer(j, 0)][0], 0.0)
        self.assertEqual(R.f32_bits(tile['chunks'][0]['pos'][0][0]), 0x80000000)      # -(Y - Y0) on column 0 is -0.0
        (mn, mx) = tile['bounds']
        self.assertAlmostEqual(mx[0], T, places=3)
        self.assertAlmostEqual(mn[2], -T, places=3)

    def test_cell_order_and_winding(self):
        self.assertEqual(R.cell_indices(0, 0), [9, 0, 1, 9, 1, 18, 9, 18, 17, 9, 17, 0])
        tile = R.build_tile(root(30, 47, height=bumpy(30, 47)), 30, 47)
        half = U * U / 2
        for ch in tile['chunks']:
            p, idx = ch['pos'], ch['idx']
            for t in range(0, len(idx), 3):
                a, b, c = p[idx[t]], p[idx[t + 1]], p[idx[t + 2]]
                cy = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2])
                self.assertLess(abs(cy - half), 1e-3)

    def test_single_hole_bits(self):
        tile = R.build_tile(root(31, 48, holes=lambda n: (1 << n) if n < 64 else 0), 31, 48)
        full = [i for r in range(8) for c in range(8) for i in R.cell_indices(r, c)]
        for bit in range(64):
            ch = tile['chunks'][bit]
            m = R.inner(bit // 8, bit % 8)
            want = [i for t in range(0, len(full), 3) if full[t] != m for i in full[t:t + 3]]
            self.assertEqual(ch['idx'], want)
            self.assertEqual((ch['triangles'], ch['hole_cells'], ch['hole_mask']), (252, 1, 1 << bit))
            self.assertEqual(ch['nrm'][m], (0.0, 1.0, 0.0))
        self.assertEqual(tile['counts']['triangles'], 65536 - 256)

    def test_full_mask_has_no_triangles_or_bounds(self):
        tile = R.build_tile(root(31, 48, holes=lambda n: 0xFFFFFFFFFFFFFFFF if n == 37 else 0), 31, 48)
        ch = tile['chunks'][37]
        self.assertEqual((ch['state'], ch['triangles'], ch['idx'], ch['cbounds']), ('Built', 0, [], None))
        self.assertEqual(tile['counts']['vertices'], 255 * 145)

    def test_ramps_keep_orientation(self):
        def ramp(sx, sy):
            X0, Y0, _ = R.tile_origin(30, 47)
            return world_height(30, 47, lambda x, y: 100 + sx * (x - X0) + sy * (y - Y0), 100.0)
        north = R.build_tile(root(30, 47, base=100.0, height=ramp(0.25, 0)), 30, 47)
        east = R.build_tile(root(30, 47, base=100.0, height=ramp(0, -0.25)), 30, 47)
        for ch in north['chunks']:
            for x, y, z in ch['pos']:
                self.assertLess(abs(y - (100 + 0.25 * z)), 1e-3)      # rises toward Unity +Z
        for ch in east['chunks']:
            for x, y, z in ch['pos']:
                self.assertLess(abs(y - (100 + 0.25 * x)), 1e-3)      # rises toward Unity +X

    def test_identity_and_failures(self):
        good = root(30, 47)
        with self.assertRaises(R.TileMismatch):
            R.build_tile(good, 31, 47)
        with self.assertRaises(R.TileMismatch):
            R.build_tile(root(30, 47, dx=lambda n: 0.011 if n == 3 else 0.0), 30, 47)
        self.assertEqual(R.build_tile(root(30, 47, dx=0.009, dy=-0.009), 30, 47)['counts']['built'], 256)
        with self.assertRaises(R.BuildFailed):
            R.build_tile(root(30, 47, short=True), 30, 47)
        tall = R.build_tile(root(30, 47, base=lambda n: 1e30 if n == 10 else 50.0), 30, 47)
        self.assertEqual((tall['chunks'][10]['state'], tall['chunks'][10]['reason']), ('Skipped', 'height out of range'))
        low = R.build_tile(root(30, 47, flags=lambda n: 0 if n in (12, 13) else 0x10000, low=lambda n: 0x8001 if n == 12 else 0), 30, 47)
        self.assertEqual((low['chunks'][12]['state'], low['chunks'][13]['state']), ('Unsupported', 'Built'))
        bad = R.build_tile(root(30, 47, short=lambda n: n == 9), 30, 47)
        self.assertEqual((bad['chunks'][9]['state'], bad['counts']['built']), ('Skipped', 255))

    def test_malformed_mcnr_and_mccv_skip_the_chunk_as_in_csharp(self):
        # The C# parser marks a chunk Malformed for an MCNR shorter than 435 bytes or repeated and an MCCV that is not
        # 580 bytes or repeated, so the builder skips it although the mesh uses neither; a missing MCNR is only
        # Unexpected there. TerrainMeshTests (FailureTests) asserts the same states on the C# side; with
        # WMV_TERRAIN_DUMP_DIR, test_runner_dump_of_side_chunk_root compares the builder's dump of such a root.
        side = {9: {'mcnr': (400,)}, 10: {'mccv': (100,)}, 11: {'mcnr': (448, 448)}, 12: {'mccv': (580, 580)},
                13: {'mcnr': ()}, 14: {'mcnr': (435,)}, 15: {'mccv': (580,)}}
        tile = R.build_tile(root(32, 50, mcnr=lambda n: side.get(n, {}).get('mcnr', (448,)),
                                 mccv=lambda n: side.get(n, {}).get('mccv', ())), 32, 50)
        states = {n: tile['chunks'][n]['state'] for n in side}
        self.assertEqual(states, {9: 'Skipped', 10: 'Skipped', 11: 'Skipped', 12: 'Skipped', 13: 'Built', 14: 'Built', 15: 'Built'})
        self.assertIn('MCNR', tile['chunks'][9]['reason'])
        self.assertIn('MCCV', tile['chunks'][10]['reason'])
        self.assertEqual((tile['counts']['built'], tile['counts']['skipped']), (252, 4))
        # Skipped chunks take no part in the tile identity: a far-off stored position there does not fail the build.
        R.build_tile(root(32, 50, mcnr=lambda n: (400,) if n == 9 else (448,), dx=lambda n: 50.0 if n == 9 else 0.0), 32, 50)


class DumpAndCompare(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.tile = R.build_tile(root(32, 50, height=bumpy(32, 50), holes=lambda n: 0xC0C0 if n == 3 else
                                      (0xFFFFFFFFFFFFFFFF if n == 4 else 0)), 32, 50)
        self.path = os.path.join(self.tmp.name, 'ref.txt')
        R.write_dump(self.tile, self.path)

    def tearDown(self):
        self.tmp.cleanup()

    def test_round_trip_is_exact(self):
        back = R.read_dump(self.path)
        self.assertEqual(back['counts'], self.tile['counts'])
        self.assertEqual(back['origin'], self.tile['origin'])
        for a, b in zip(back['chunks'], self.tile['chunks']):
            self.assertEqual(a['pos'], b['pos'])
            self.assertEqual(a['nrm'], b['nrm'])
            self.assertEqual(a['idx'], b['idx'])
        self.assertEqual(back['chunks'][0]['pos_bits'][0], 0x80000000)
        text = R.read_bytes(self.path)
        self.assertTrue(text.endswith(b'end\n') and b'\r' not in text and max(text) < 128)

    def test_truncated_dump_is_refused(self):
        text = R.read_bytes(self.path).decode('ascii')
        bad = os.path.join(self.tmp.name, 'bad.txt')
        with open(bad, 'w', newline='\n') as f:
            f.write(text[:len(text) // 2])
        with self.assertRaises(ValueError):
            R.read_dump(bad)

    def test_compare_passes_identical_and_catches_mutations(self):
        ref = R.read_dump(self.path)
        self.assertEqual(quiet(CMP.compare, R.read_dump(self.path), ref).failed, 0)

        def mutated(fn):
            d = R.read_dump(self.path)
            fn(d)
            return quiet(CMP.compare, d, ref)

        def nudge_pos(d):
            p = d['chunks'][77]['pos']
            p[5] = (p[5][0], p[5][1] + 1e-4, p[5][2])

        def nudge_nrm(d):
            n = d['chunks'][77]['nrm']
            n[5] = (n[5][0] + 1e-5, n[5][1], n[5][2])

        def swap_idx(d):
            i = d['chunks'][77]['idx']
            i[1], i[2] = i[2], i[1]

        def drop_hole(d):
            d['chunks'][3]['hole_mask'] = 0

        def bounds(d):
            d['chunks'][9]['cbounds'] = ((0.0, 0.0, 0.0), d['chunks'][9]['cbounds'][1])

        # NaN and infinity: a running max() never rises on NaN, so each value must be tested on its own.
        def nan_pos(d):
            p = d['chunks'][77]['pos']
            p[5] = (p[5][0], float('nan'), p[5][2])

        def nan_nrm(d):
            d['chunks'][77]['nrm'] = [(float('nan'),) * 3 for _ in range(145)]

        def inf_pos(d):
            p = d['chunks'][78]['pos']
            p[0] = (float('inf'), p[0][1], p[0][2])

        # Lengths: arrays never compare over just their overlap with the reference.
        def drop_vertex(d):
            c = d['chunks'][77]
            c['pos'], c['nrm'] = c['pos'][:144], c['nrm'][:144]
            c['pos_bits'], c['nrm_bits'] = c['pos_bits'][:432], c['nrm_bits'][:432]

        def extra_vertex(d):
            c = d['chunks'][77]
            c['pos'] = c['pos'] + [(float('inf'),) * 3]
            c['nrm'] = c['nrm'] + [(0.0, 1.0, 0.0)]

        def short_triple(d):
            c = d['chunks'][5]
            c['pos'] = c['pos'][:144] + [c['pos'][144][:2]]

        def index_past_vertices(d):
            c = d['chunks'][77]
            c['idx'] = c['idx'][:-1] + [145]

        def short_origin(d):
            d['origin'] = d['origin'][:2]

        def short_cbounds(d):
            d['chunks'][9]['cbounds'] = (d['chunks'][9]['cbounds'][0], d['chunks'][9]['cbounds'][1][:2])

        def short_tile_bounds(d):
            d['bounds'] = (d['bounds'][0], d['bounds'][1][:1])
        for fn, check in ((nudge_pos, 'positions'), (nudge_nrm, 'normals'), (swap_idx, 'index lists'),
                          (drop_hole, 'chunk states'), (bounds, 'chunk bounds'),
                          (nan_pos, 'positions'), (nan_nrm, 'normals'), (inf_pos, 'positions'),
                          (drop_vertex, 'array lengths'), (extra_vertex, 'array lengths'), (short_triple, 'array lengths'),
                          (index_past_vertices, 'array lengths'), (short_origin, 'origin'), (short_cbounds, 'chunk bounds'),
                          (short_tile_bounds, 'tile bounds')):
            rep = mutated(fn)
            failed = [c['name'] for c in rep.checks if c['result'] == 'FAIL']
            self.assertTrue(failed and all(check in f for f in failed), (fn.__name__, failed))

    def test_nan_report_is_not_a_zero(self):
        d = R.read_dump(self.path)
        d['chunks'][77]['pos'] = [(float('nan'),) * 3 for _ in range(145)]
        rep = quiet(CMP.compare, d, R.read_dump(self.path))
        pos = [c for c in rep.checks if 'positions' in c['name']][0]
        self.assertEqual(pos['result'], 'FAIL')
        self.assertIn('435 out of tolerance, max difference nan', pos['detail'])

    def edit_record(self, path, key, edit, nth=0):
        """Rewrite the nth line starting with `key ` through edit(tokens) -> tokens; returns the new file's path."""
        lines = R.read_bytes(path).decode('ascii').split('\n')
        hits = [i for i, l in enumerate(lines) if l == key or l.startswith(key + ' ')]
        i = hits[nth]
        tok = lines[i].split(' ')[1:]
        lines[i] = ' '.join([key] + edit(tok))
        out = os.path.join(self.tmp.name, 'edited.txt')
        with open(out, 'w', encoding='ascii', newline='\n') as f:
            f.write('\n'.join(lines))
        return out

    def test_short_or_long_records_are_refused(self):
        player = os.path.join(self.tmp.name, 'player.txt')
        self.player_dump()
        cases = [('origin', lambda t: t[:2], 0), ('counts', lambda t: t[:6], 0), ('bounds', lambda t: t[:5], 0),
                 ('tile', lambda t: t[:1], 0), ('meshbytes', lambda t: t + ['1'], 0), ('chunk', lambda t: t[:6], 7),
                 ('pos', lambda t: t[:432], 77), ('nrm', lambda t: t + ['00000000'] * 3, 77), ('cbounds', lambda t: t[:5], 9),
                 ('idx', lambda t: t[:-1], 77), ('idx', lambda t: t[:-3] + ['145', '0', '1'], 77), ('idx', lambda t: ['-1'] + t[1:], 77)]
        for key, edit, nth in cases:
            with self.subTest(record=key, nth=nth):
                with self.assertRaises(ValueError):
                    R.read_dump(self.edit_record(self.path, key, edit, nth))
        for key, edit in (('root', lambda t: t[:17]), ('unity', lambda t: t[:26]), ('unity', lambda t: t + ['00000000'])):
            with self.subTest(record=key):
                with self.assertRaises(ValueError):
                    R.read_dump(self.edit_record(player, key, edit))
        orphan = os.path.join(self.tmp.name, 'orphan.txt')
        text = R.read_bytes(self.path).decode('ascii')
        first = text.index('\nchunk ')
        with open(orphan, 'w', encoding='ascii', newline='\n') as f:
            f.write(text[:first] + '\nreason before any chunk' + text[first:])
        with self.assertRaises(ValueError):
            R.read_dump(orphan)
        # The untouched files still read, and a chunk without triangles keeps its empty idx record.
        self.assertEqual(R.read_dump(self.path)['chunks'][4]['idx'], [])
        self.assertIsNotNone(R.read_dump(player)['root'])

    def player_dump(self):
        d = R.read_dump(self.path)
        d['source'] = 'player'
        d['root'] = {'layer': 30, 'active': True, 'local_to_world': list(CMP.IDENTITY)}
        for ch in d['chunks']:
            if ch['triangles'] == 0:
                ch['pos'] = ch['nrm'] = ch['idx'] = ch['cbounds'] = None
                continue
            ch['unity'] = {'layer': 30, 'active': True, 'sub_mesh_count': 1, 'topology': 'Triangles', 'index_format': 'UInt16',
                           'mesh_bounds': ch['cbounds'], 'local_to_world': list(CMP.IDENTITY)}
        out = os.path.join(self.tmp.name, 'player.txt')
        R.write_dump(d, out)
        return R.read_dump(out)

    def test_player_dump_checks(self):
        ref = R.read_dump(self.path)
        good = self.player_dump()
        self.assertEqual(quiet(CMP.compare, good, ref).failed, 0)
        for change in ('layer', 'topology', 'index_format', 'inactive', 'matrix', 'bounds', 'mesh for a holed-out chunk',
                       'nan bounds', 'one nan bound', 'height bounds 2e-4 off', 'dropped vertex', 'short bounds'):
            d = self.player_dump()
            u = d['chunks'][100]['unity']
            if change == 'nan bounds':
                u['mesh_bounds'] = ((float('nan'),) * 3, (float('nan'),) * 3)
            elif change == 'one nan bound':
                u['mesh_bounds'] = (u['mesh_bounds'][0], (u['mesh_bounds'][1][0], float('nan'), u['mesh_bounds'][1][2]))
            elif change == 'height bounds 2e-4 off':
                lo = u['mesh_bounds'][0]
                self.assertLess(max(abs(v) for v in lo + u['mesh_bounds'][1]), 512)
                u['mesh_bounds'] = ((lo[0], lo[1] + 2e-4, lo[2]), u['mesh_bounds'][1])
            elif change == 'dropped vertex':
                c = d['chunks'][101]
                c['pos'], c['nrm'] = c['pos'][:144], c['nrm'][:144]
            elif change == 'short bounds':
                u['mesh_bounds'] = (u['mesh_bounds'][0], u['mesh_bounds'][1][:2])
            elif change == 'layer':
                u['layer'] = 0
            elif change == 'topology':
                u['topology'] = 'Lines'
            elif change == 'index_format':
                u['index_format'] = 'UInt32'
            elif change == 'inactive':
                u['active'] = False
            elif change == 'matrix':
                u['local_to_world'][3] = 5.0
            elif change == 'bounds':
                u['mesh_bounds'] = ((0.0, 0.0, 0.0), u['mesh_bounds'][1])
            else:
                d['chunks'][4]['pos'] = d['chunks'][5]['pos']
                d['chunks'][4]['nrm'] = d['chunks'][5]['nrm']
                d['chunks'][4]['idx'] = []
            self.assertGreater(quiet(CMP.compare, d, ref).failed, 0, change)

    def test_mesh_bounds_tolerance_covers_the_unity_round_trip_far_from_the_origin(self):
        # Unity keeps mesh bounds as centre and extents (float32); min/max read back move by up to 2 ulps of the
        # coordinate: 2.44e-4 yd per ulp in [2048, 4096), more than a fixed 1e-4. An origin 2600-3000 yd away puts
        # every bound of the tile there.
        X0, Y0, Z0 = R.tile_origin(32, 50)
        ref = R.build_tile(root(32, 50, height=bumpy(32, 50)), 32, 50, origin=(X0 + 3000.0, Y0 - 3000.0, Z0 - 2600.0))
        f = R.f32
        player = {k: v for k, v in ref.items()}
        player['source'] = 'player'
        player['root'] = {'layer': 30, 'active': True, 'local_to_world': list(CMP.IDENTITY)}
        player['chunks'] = []
        drift = 0.0
        for rc in ref['chunks']:
            ch = dict(rc)
            if rc['triangles'] == 0:
                ch['pos'] = ch['nrm'] = ch['idx'] = ch['cbounds'] = None
            else:
                mn, mx = rc['cbounds']
                self.assertTrue(all(2048 <= abs(v) < 4096 for v in mn + mx), (mn, mx))
                c = [f(f(a + b) * 0.5) for a, b in zip(mn, mx)]
                e = [f(f(b - a) * 0.5) for a, b in zip(mn, mx)]
                back = (tuple(f(x - y) for x, y in zip(c, e)), tuple(f(x + y) for x, y in zip(c, e)))
                drift = max([drift] + [abs(back[i][a] - rc['cbounds'][i][a]) for i in range(2) for a in range(3)])
                ch['unity'] = {'layer': 30, 'active': True, 'sub_mesh_count': 1, 'topology': 'Triangles', 'index_format': 'UInt16',
                               'mesh_bounds': back, 'local_to_world': list(CMP.IDENTITY)}
            player['chunks'].append(ch)
        self.assertGreater(drift, CMP.MESH_BOUNDS_TOLERANCE)          # the fixed 1e-4 alone would fail this tile
        self.assertEqual(quiet(CMP.compare, player, ref).failed, 0)
        saved = CMP.MESH_BOUNDS_ULPS
        try:
            CMP.MESH_BOUNDS_ULPS = 0
            self.assertGreater(quiet(CMP.compare, player, ref).failed, 0)
        finally:
            CMP.MESH_BOUNDS_ULPS = saved
        # 1e-4 yd plus 2 ulps of the larger magnitude on the axis: 2^-14 yd per ulp on a tile-local 533 yd axis, 2^-18 at 50 yd.
        self.assertEqual(CMP.mesh_bounds_tolerance(-533.4, 0.0), 1e-4 + 2 * 2.0 ** -14)
        self.assertEqual(CMP.mesh_bounds_tolerance(40.0, 50.0), 1e-4 + 2 * 2.0 ** -18)
        self.assertEqual(CMP.f32_ulp(1024.0), 2.0 ** -13)
        self.assertEqual(CMP.f32_ulp(-3000.0), 2.0 ** -12)


@unittest.skipUnless(DUMPS, 'WMV_TERRAIN_DUMP_DIR not set (no runner dumps to compare)')
class RunnerSyntheticDump(unittest.TestCase):
    def test_runner_dump_of_side_chunk_root(self):
        """The C# runner's dump of TerrainMeshTests.SideChunkRoot (written by TerrainCorpusTests next to the corpus
        dumps, with its bytes) against the reference built here from the same bytes: both sides skip exactly the chunks
        whose MCNR or MCCV the parser calls malformed, so every check passes."""
        dump = R.read_dump(os.path.join(DUMPS, 'runner_synthetic_sidechunks_32_50.txt'))
        ref = R.build_tile(R.read_bytes(os.path.join(DUMPS, 'synthetic_sidechunks_32_50.bin')), 32, 50)
        self.assertEqual(quiet(CMP.compare, dump, ref).failed, 0)
        self.assertEqual([c['n'] for c in dump['chunks'] if c['state'] == 'Skipped'], [9, 10, 11, 12])
        self.assertEqual([c['n'] for c in ref['chunks'] if c['state'] == 'Skipped'], [9, 10, 11, 12])


class Png(unittest.TestCase):
    def encode(self, rows, filt):
        """A PNG whose every row uses filter `filt` (0..4), to exercise the reader's unfiltering."""
        h, w = len(rows), len(rows[0])
        prev = bytes(3 * w)
        raw = bytearray()
        for row in rows:
            line = bytes(v for p in row for v in p)
            out = bytearray()
            for i, x in enumerate(line):
                a = line[i - 3] if i >= 3 else 0
                b = prev[i]
                c = prev[i - 3] if i >= 3 else 0
                if filt == 0:
                    pred = 0
                elif filt == 1:
                    pred = a
                elif filt == 2:
                    pred = b
                elif filt == 3:
                    pred = (a + b) >> 1
                else:
                    pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                    pred = a if pa <= pb and pa <= pc else b if pb <= pc else c
                out.append((x - pred) & 255)
            raw.append(filt)
            raw += out
            prev = line

        def ch(tag, body):
            return struct.pack('>I', len(body)) + tag + body + struct.pack('>I', zlib.crc32(tag + body) & 0xFFFFFFFF)
        return (b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                + ch(b'IDAT', zlib.compress(bytes(raw))) + ch(b'IEND', b''))

    def test_filters_round_trip(self):
        rows = [[((x * 37 + y * 11) % 256, (x * y) % 256, (255 - x * 5) % 256) for x in range(9)] for y in range(7)]
        with tempfile.TemporaryDirectory() as tmp:
            for filt in range(5):
                p = os.path.join(tmp, 'f%d.png' % filt)
                with open(p, 'wb') as f:
                    f.write(self.encode(rows, filt))
                w, h, back = MO.read_png(p)
                self.assertEqual((w, h, back), (9, 7, rows), filt)
            p = os.path.join(tmp, 'w.png')
            MO.write_png(p, rows)
            self.assertEqual(MO.read_png(p)[2], rows)


class MinimapSynthetic(unittest.TestCase):
    """The orientation test on a synthetic tile and a synthetic minimap lit from the north-west."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.tile = R.build_tile(root(32, 50, height=bumpy(32, 50)), 32, 50)
        cls.dump = os.path.join(cls.tmp.name, 'tile.txt')
        R.write_dump(cls.tile, cls.dump)
        cells = MO.cells_from_dump(R.read_dump(cls.dump))
        lum = [[128 + 120 * (-0.6 * cells[(v, u)][0] + 0.6 * cells[(v, u)][1] + 0.3 * cells[(v, u)][2]) for u in range(128)]
               for v in range(128)]
        cls.minimap = os.path.join(cls.tmp.name, 'minimap.blp')
        MO.write_dxt1_minimap(cls.minimap, lum)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_identity_wins_on_the_dump(self):
        res = MO.fit(MO.minimap_luminance(self.minimap), MO.cells_from_dump(R.read_dump(self.dump)))
        self.assertTrue(res['pass'], res['ratio'])
        self.assertGreater(res['identity_r2'], 0.9)

    def test_a_mirrored_mesh_fails(self):
        d = R.read_dump(self.dump)
        for ch in d['chunks']:
            ch['pos'] = [(-x, y, z) for x, y, z in ch['pos']]          # ux = +(Y - Y0): the mirror the test exists for
        res = MO.fit(MO.minimap_luminance(self.minimap), MO.cells_from_dump(d))
        self.assertFalse(res['pass'])
        self.assertEqual(max(res['table'], key=lambda t: t['r2'])['symmetry'], 'mirror columns (east-west)')

    def test_capture_mode(self):
        lum = MO.minimap_luminance(self.minimap)
        for mirror in (False, True):
            png = os.path.join(self.tmp.name, 'cap%d.png' % mirror)
            MO.write_png(png, MO.render(R.read_dump(self.dump), 256, mirror))
            cells, size = MO.cells_from_capture(png)
            self.assertEqual(size, (256, 256))
            self.assertEqual(MO.fit(lum, cells)['pass'], not mirror)

    def test_exit_codes(self):
        """0 PASS, 1 FAIL, 2 for every usage or input error (never a traceback, whose exit code 1 reads as FAIL)."""
        def run(*args):
            return quiet(MO.main, ['minimap_orientation.py'] + list(args))

        def write(name, data):
            p = os.path.join(self.tmp.name, name)
            with open(p, 'wb') as f:
                f.write(data)
            return p
        self.assertEqual(run('dump', self.dump, self.minimap), 0)
        mirrored = R.read_dump(self.dump)
        for ch in mirrored['chunks']:
            ch['pos'] = [(-x, y, z) for x, y, z in ch['pos']]
        mirrored['meta'] = [('client', 'wow'), ('minimapFileDataID', '204495')]
        named = os.path.join(self.tmp.name, 'named.txt')
        R.write_dump(mirrored, named)
        self.assertEqual(run('dump', named, self.minimap), 1)
        # 'dump' with neither a minimap path nor --corpus (was os.path.join(None, ...): TypeError, exit 1)
        self.assertEqual(run('dump', named), 2)
        # a corpus whose WDT has no MAID (TypeError in find_minimap)
        os.makedirs(os.path.join(self.tmp.name, 'corpus', 'wdt', 'wow'))
        write(os.path.join('corpus', 'wdt', 'wow', '775971.bin'), chunk('MVER', struct.pack('<I', 18)))
        self.assertEqual(run('dump', named, '--corpus', os.path.join(self.tmp.name, 'corpus')), 2)
        # a dump without its tile record (KeyError)
        text = R.read_bytes(self.dump).decode('ascii')
        no_tile = write('notile.txt', '\n'.join(l for l in text.split('\n') if not l.startswith('tile ')).encode('ascii'))
        self.assertEqual(run('dump', no_tile, self.minimap), 2)
        # a PNG without IHDR (KeyError), a missing argument (IndexError), a bad --min-ratio (ValueError)
        no_ihdr = write('noihdr.png', b'\x89PNG\r\n\x1a\n' + struct.pack('>I', 0) + b'IEND' + struct.pack('>I', zlib.crc32(b'IEND')))
        self.assertEqual(run('capture', no_ihdr, self.minimap), 2)
        self.assertEqual(run('dump', '--min-ratio', '3'), 2)
        self.assertEqual(run('dump', self.dump, self.minimap, '--min-ratio', 'two'), 2)


# ------------------------------------------------------------------------------------------------- corpus

PINNED = [  # stage 03 design section 7: (client, x, y, root, hole cells, triangles, {chunk: mask})
    ('wow', 32, 50, 778037, 0, 65536, {}),
    ('wow_classic_era', 32, 50, 778037, 0, 65536, {}),
    ('wow', 34, 46, 778417, 256, 64512, {3: 0xC0C0, 4: 2 ** 64 - 1, 5: None, 19: None, 20: 2 ** 64 - 1, 21: 0x3F3F3F3F3F3F3F3F,
                                         36: None, 37: None, 145: None, 146: None}),
    ('wow', 29, 50, 777437, 24, 65440, {164: 0x3F3F000000000000, 180: 0x3F3F}),
] + [(c, 32, 48, 778027, 16, 65472, {51: 0xF0F0000000000000, 67: 0xF0F0})
     for c in ('wow', 'wow_classic_era', 'wow_classic', 'wow_classic_beta')]


def wdt_words(client, x, y):
    w = WDT(R.read_bytes(os.path.join(ROOT, 'wdt', client, '775971.bin')))
    return w.main[y * 64 + x][0], w.maid[y * 64 + x]


@need_data
class Corpus(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.built = {}
        for client, x, y, rid, _, _, _ in PINNED:
            cls.built[(client, x, y)] = R.build_tile(R.read_bytes(os.path.join(ROOT, 'corpus', client, 'azeroth', '%d.bin' % rid)), x, y)

    def test_pinned_ids_come_from_the_wdt(self):
        for client, x, y, rid, _, _, _ in PINNED:
            main, maid = wdt_words(client, x, y)
            with self.subTest(client=client, tile=(x, y)):
                self.assertTrue(main & 1)
                self.assertEqual(maid[0], rid)
        for x, y, mini in ((32, 50, 204495), (34, 46, 204571)):
            self.assertEqual(wdt_words('wow', x, y)[1][7], mini)
            self.assertTrue(os.path.exists(os.path.join(ROOT, 'corpus', 'wow', 'azeroth', '%d.bin' % mini)))

    def test_pinned_counts(self):
        for client, x, y, rid, holes, tris, masks in PINNED:
            t = self.built[(client, x, y)]
            with self.subTest(client=client, tile=(x, y)):
                c = t['counts']
                self.assertEqual((c['built'], c['skipped'], c['unsupported'], c['triangles'], c['hole_cells']), (256, 0, 0, tris, holes))
                self.assertEqual(sorted(ch['n'] for ch in t['chunks'] if ch['hole_cells']), sorted(masks))
                for n, m in masks.items():
                    if m is not None:
                        self.assertEqual(t['chunks'][n]['hole_mask'], m)
        self.assertEqual(self.built[('wow', 32, 50)]['counts']['vertices'], 37120)

    def test_minimap_orientation_on_the_reference(self):
        for x, y, mini in ((32, 50, 204495), (34, 46, 204571)):
            res = MO.fit(MO.minimap_luminance(os.path.join(ROOT, 'corpus', 'wow', 'azeroth', '%d.bin' % mini)),
                         MO.cells_from_dump(self.built[('wow', x, y)]))
            with self.subTest(tile=(x, y)):
                self.assertTrue(res['pass'], '%r' % [(t['symmetry'], round(t['r2'], 4)) for t in res['table']])

    def test_capture_mode_on_a_rendered_capture(self):
        lum = MO.minimap_luminance(os.path.join(ROOT, 'corpus', 'wow', 'azeroth', '204495.bin'))
        with tempfile.TemporaryDirectory() as tmp:
            for mirror in (False, True):
                png = os.path.join(tmp, 'cap.png')
                MO.write_png(png, MO.render(self.built[('wow', 32, 50)], 512, mirror))
                cells, _ = MO.cells_from_capture(png)
                with self.subTest(mirror=mirror):
                    self.assertEqual(MO.fit(lum, cells)['pass'], not mirror)

    @unittest.skipUnless(DUMPS, 'WMV_TERRAIN_DUMP_DIR not set (no runner dumps to compare)')
    def test_runner_dumps_equal_the_reference(self):
        found = 0
        for client, x, y, rid, _, _, _ in PINNED:
            p = os.path.join(DUMPS, 'runner_%s_%d_%d.txt' % (client, x, y))
            if not os.path.exists(p):
                continue
            found += 1
            d = R.read_dump(p)
            with self.subTest(dump=os.path.basename(p)):
                self.assertEqual(R.meta(d, 'rootFileDataID'), str(rid))
                self.assertEqual(quiet(CMP.compare, d, self.built[(client, x, y)]).failed, 0)
        self.assertEqual(found, len(PINNED))


if __name__ == '__main__':
    unittest.main()
