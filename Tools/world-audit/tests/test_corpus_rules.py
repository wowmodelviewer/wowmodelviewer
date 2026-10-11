"""The report's format rules, asserted on the extracted game bytes (not on synthetic files): run only where the audit
data has been extracted and pinned (WMV_WORLD_AUDIT_CORPUS, checked against tests/corpus_hashes.json by
test_wowfmt.Corpus). Each rule is also checked against its competing readings, so a test fails if the data stops
telling them apart, not only if the chosen reading breaks.

  set WMV_WORLD_AUDIT_CORPUS=F:/wowmodelviewer/build/world-audit-data
  python -I -m unittest discover -s Tools/world-audit/tests
"""
import contextlib
import io
import json
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
from wowfmt import adt as A  # noqa: E402
from wowfmt.wdt import WDT  # noqa: E402

ROOT = os.environ.get('WMV_WORLD_AUDIT_CORPUS')
CLIENTS = ('wow', 'wow_classic_beta', 'wow_classic_era', 'wow_classic')
T = 1600.0 / 3
C = T / 16
U = C / 8


def need_data(test):
    return unittest.skipUnless(ROOT, 'WMV_WORLD_AUDIT_CORPUS not set (the audit data is extracted locally)')(test)


def corpus_roots(client):
    folder = os.path.join(ROOT, 'corpus', client, 'azeroth')
    cj = json.load(open(os.path.join(folder, 'corpus.json')))
    return {(f['x'], f['y']): os.path.join(folder, '%d.bin' % f['fdid']) for f in cj['files']
            if f.get('what') == 'root' and f['status'] == 'OK'}


def run_script(main, *args):
    """Runs one of the audit scripts' main() into a temporary file and returns its JSON."""
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, 'out.json')
        with contextlib.redirect_stdout(io.StringIO()):
            main(*args, out)
        return json.load(open(out))


@need_data
class MainAndMaid(unittest.TestCase):
    def test_main_bit_1_iff_maid_names_files(self):
        """MAIN flag bit 0x1 is set exactly where MAID names files, in every extracted WDT of every client; MAIN
        flag 0x2 never comes with MAID words."""
        for client in CLIENTS:
            folder = os.path.join(ROOT, 'wdt', client)
            wdts = mismatches = flag2_with_words = 0
            for fn in os.listdir(folder):
                if not (fn.endswith('.bin') and fn[:-4].isdigit()):
                    continue
                w = WDT(open(os.path.join(folder, fn), 'rb').read())
                if not w.main or not w.maid:
                    continue
                wdts += 1
                for m, words in zip(w.main, w.maid):
                    mismatches += bool(m[0] & 1) != any(words)
                    flag2_with_words += bool(m[0] & 2) and any(words)
            with self.subTest(client=client):
                self.assertGreater(wdts, 0)
                self.assertEqual(mismatches, 0)
                self.assertEqual(flag2_with_words, 0)

    def test_maid_index_is_y_times_64_plus_x(self):
        """The corpus extracted each tile's files by MAID index y*64+x; every root's MCNK 0 must sit at that tile."""
        for client in CLIENTS:
            for (x, y), path in corpus_roots(client).items():
                b = open(path, 'rb').read()
                _, h, _ = A.SplitFile(b, root=True).mcnk[0]
                with self.subTest(client=client, tile=(x, y)):
                    self.assertAlmostEqual(h['position'][0], (32 - y) * T, delta=0.01)
                    self.assertAlmostEqual(h['position'][1], (32 - x) * T, delta=0.01)


@need_data
class ChunkGeometry(unittest.TestCase):
    def test_mcnk_position_and_index(self):
        """Root MCNK n has index_x = n % 16, index_y = n // 16 and position (32-ty)T - iy*C, (32-tx)T - ix*C."""
        for client in CLIENTS:
            worst = 0.0
            for (tx, ty), path in corpus_roots(client).items():
                b = open(path, 'rb').read()
                for n, (_, h, _) in enumerate(A.SplitFile(b, root=True).mcnk):
                    ix, iy = h['index_x'], h['index_y']
                    self.assertEqual((ix, iy), (n % 16, n // 16))
                    worst = max(worst, abs(h['position'][0] - ((32 - ty) * T - iy * C)),
                                abs(h['position'][1] - ((32 - tx) * T - ix * C)))
            with self.subTest(client=client):
                self.assertLess(worst, 0.002)

    def test_outer_vertex_order_seams(self):
        """Outer vertex (r, c) = MCVT[17r + c] at (pos.x - rU, pos.y - cU): the shared edges of neighbouring chunks
        (inside a tile and across tile seams) have the same height; reading MCVT transposed breaks them."""
        for client in CLIENTS:
            roots = corpus_roots(client)
            heights = {}
            for (tx, ty), path in roots.items():
                b = open(path, 'rb').read()
                for _, h, sub in A.SplitFile(b, root=True).mcnk:
                    heights[(tx, ty, h['index_x'], h['index_y'])] = (h['position'][2], A.mcvt(b, sub.one('MCVT')))

            def outer(key, r, c, transposed=False):
                z, v = heights[key]
                return z + (v[17 * c + r] if transposed else v[17 * r + c])

            pairs = bad = bad_t = 0
            for (tx, ty, ix, iy) in heights:
                # the chunk to the -Y side (next ix, possibly in the next tile) and to the -X side (next iy)
                for nkey, edge in ((((tx, ty, ix + 1, iy) if ix < 15 else (tx + 1, ty, 0, iy)), 'c'),
                                   (((tx, ty, ix, iy + 1) if iy < 15 else (tx, ty + 1, ix, 0)), 'r')):
                    if nkey not in heights:
                        continue
                    for k in range(9):
                        a = (k, 8) if edge == 'c' else (8, k)
                        n = (k, 0) if edge == 'c' else (0, k)
                        pairs += 1
                        bad += abs(outer((tx, ty, ix, iy), *a) - outer(nkey, *n)) > 0.001
                        bad_t += abs(outer((tx, ty, ix, iy), *a, True) - outer(nkey, *n, True)) > 0.001
            with self.subTest(client=client):
                self.assertGreater(pairs, 10000)
                self.assertEqual(bad, 0)
                self.assertGreater(bad_t, pairs // 2)


@need_data
class ScriptVerdicts(unittest.TestCase):
    """The audit scripts' own comparisons, re-run on the pinned bytes: the reported reading must win, strictly."""

    @classmethod
    def setUpClass(cls):
        import coords
        cls.coords = {c: run_script(coords.main, os.path.join(ROOT, 'corpus', c, 'azeroth')) for c in CLIENTS}

    def test_outer_and_inner_vertex_order(self):
        """Outer MCVT[17r + c] is the only one of the 8 symmetries with equal heights at every shared point; inner
        MCVT[17r + 9 + c] sits at least 5 times closer to its corners' mean than under any other symmetry."""
        for client, r in self.coords.items():
            with self.subTest(client=client):
                o = r['outer_vertex_order']
                self.assertEqual(o['identity']['differing_over_1mm'], 0)
                self.assertTrue(all(v['differing_over_1mm'] > 0 for k, v in o.items() if k != 'identity'))
                inner = r['inner_vertex_index']
                ident = inner['identity']['median_yd']
                self.assertTrue(all(v['median_yd'] > 5 * ident for k, v in inner.items() if k != 'identity'))

    def test_seam_xy_from_indices(self):
        """Stored MCNK positions are within 0.002 yd of the index formula, but not bit-consistent: seam vertices built
        from the stored float32 position never match exactly -- the reason vertex x/y must come from the indices."""
        for client, r in self.coords.items():
            with self.subTest(client=client):
                s = r['seam_xy']
                self.assertLess(s['stored_position_vs_formula_max_yd'], 0.002)
                self.assertGreater(s['seams_tested'], 0)
                self.assertEqual(s['seams_bit_equal_when_built_from_stored_position_in_float32'], 0)

    def test_placement_position(self):
        """MDDF position p -> world (32T - p2, 32T - p0, p1): doodads over the window's terrain sit on the exact fan
        surface (median |dz| under 0.05 yd)."""
        for client, r in self.coords.items():
            with self.subTest(client=client):
                p = r['placement']
                self.assertGreater(p['over_terrain_in_corpus'], 0.98 * p['unique_doodads'])
                self.assertLess(p['median_abs_yd'], 0.05)

    def test_holes(self):
        """Hole bit = row*8 + col, LSB first: every hole cell of the discriminating chunks lies inside a WMO's box, and
        holes continue across chunk edges most often; every other bit order does worse on both."""
        for client, r in self.coords.items():
            orders = r['holes']['orders']
            ident = orders['identity']
            with self.subTest(client=client):
                self.assertGreater(ident['cells_in_discriminating_chunks'], 0)
                self.assertEqual(ident['discriminating_cells_inside'], ident['cells_in_discriminating_chunks'])
                for k, v in orders.items():
                    if k != 'identity':
                        self.assertLess(v['discriminating_cells_inside'], ident['discriminating_cells_inside'], k)
                        self.assertLess(v['cross_chunk_hole_adjacencies'], ident['cross_chunk_hole_adjacencies'], k)

    def test_alpha_orientation(self):
        """Alpha texel (row, col) = byte row*64 + col: it continues across chunk edges in both directions, and no
        other orientation does as well."""
        for client, r in self.coords.items():
            a = r['alpha_orientation']

            def score(k):
                return sum(e['mean'] for e in a[k].values())
            with self.subTest(client=client):
                self.assertEqual(min(a, key=score), 'identity')
                self.assertTrue(all(score(k) > score('identity') * 1.5 for k in a if k != 'identity'))

    def test_placement_rotation(self):
        """MODF: world = (32T - p2, 32T - p0, p1) + Rz(a1 + 180) Ry(a0) Rx(a2) applied to the WMO's box corners, against
        the stored extents; MDDF/obj1 MLDX the same with scale u16/1024."""
        import mldx
        import rotation
        corpus = os.path.join(ROOT, 'corpus', 'wow', 'azeroth')
        assets = os.path.join(ROOT, 'assets', 'wow')
        rot = run_script(rotation.main, corpus, assets)
        best = rot['best'][0]
        self.assertEqual(best['yaw_offset'], 180.0)
        self.assertEqual(best['mirror'], 'none')
        self.assertEqual(best['signs'], [1, 1, 1])
        self.assertEqual(best['within_0_1_yard'], rot['tested'] - len(rot['misfits_of_best']))
        self.assertEqual(best['tilted_within_0_1_yard'], rot['tilted'])
        self.assertGreater(best['within_0_1_yard'], rot['best_without_yaw_offset']['within_0_1_yard'])
        self.assertGreater(best['within_0_1_yard'], rot['best_improper']['within_0_1_yard'])
        m = run_script(mldx.main, corpus, assets)
        self.assertEqual(m['mldd_equals_mddf']['differ'], 0)
        b = m['best'][0]
        self.assertEqual((b['yaw'], b['scale'], b['pitch_roll_signs']), ('+1*a1+180', 'u16/1024', [1, 1]))
        self.assertGreater(b['within_0_5_yard'] / b['doodads'], 0.99)
        self.assertGreater(b['within_0_5_yard'], m['best'][1]['within_0_5_yard'])


if __name__ == '__main__':
    unittest.main()
