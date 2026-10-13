"""Checks one headless terrain run (the viewer's `-terraintile` harness, its out.json) against values that do not come
from the run itself: the audit corpus bytes, the independent CPU reference (terrain_reference.py) and the pinned
expectations of the stage 03 design.

  python -I check_terrain_run.py <out.json> [<out.json> ...] [--corpus <dir>] [--pins <corpus_hashes.json>]

For every load the player reported "built":
  - the counts it reported (chunks built/skipped/unsupported, vertices, triangles, hole cells) and its bounds equal
    the reference's for the same root bytes, when the corpus holds that client's root and the client's build is the
    corpus build (otherwise SKIP, with the reason);
  - the root FileDataID is the WDT's MAID word 0 for the tile (corpus WDT, same build rule);
  - the MD5 the player computed of the WDT and root it parsed equals the corpus file's MD5 (same build rule) and the
    MD5 the host logged for the bytes it served for that load;
  - the pinned expectations of the design (root ids, hole cells, triangles, the worst-case root's size) hold;
  - the chunk states add up to 256.
Across all runs given: the same tile built by two clients whose corpus roots differ must report different root MD5s.
Host serving: handler and read times per (role, FileDataID), median / p95 / max, the first request of each process
apart (the host flags the first read of each FileDataID in its process: the cold read; a WDT's first serve is warm,
the host read it whole just before sending the load); median and p95 must be <= 20 ms for every group of >= 10
requests (the design's blocking budget).
Prints "[check] PASS|FAIL|SKIP <name>: <detail>" lines and "RESULT: PASS|FAIL (p passed, f failed, s skipped)";
the exit code is the number of failures. A SKIP is never counted as a pass.
"""
import hashlib
import json
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import terrain_reference as R  # noqa: E402
from wowfmt.wdt import WDT  # noqa: E402

# Design section 7 (each id verified against MAID word 0 of the corpus WDT by the C# corpus tests).
PINNED = {
    (0, 32, 50): {'root': 778037, 'holeCells': 0, 'triangles': 65536},
    (0, 34, 46): {'root': 778417, 'holeCells': 256, 'triangles': 64512},
    (0, 29, 50): {'root': 777437, 'holeCells': 24, 'triangles': 65440},
    (0, 32, 48): {'root': 778027, 'holeCells': 16, 'triangles': 65472},
    (1051, 31, 29): {'root': 811303, 'rootBytes': 1462916},
}
BUDGET_MS = 20.0


class Checker:
    def __init__(self):
        self.passed = self.failed = self.skipped = 0

    def result(self, ok, name, detail=''):
        if ok is None:
            self.skipped += 1
            tag = 'SKIP'
        elif ok:
            self.passed += 1
            tag = 'PASS'
        else:
            self.failed += 1
            tag = 'FAIL'
        print('[check] %s %s%s' % (tag, name, (': ' + detail) if detail else ''))


def md5_file(path):
    with open(path, 'rb') as f:
        return hashlib.md5(f.read()).hexdigest()


def pct(values, p):
    v = sorted(values)
    if not v:
        return float('nan')
    k = (len(v) - 1) * p / 100.0
    lo = int(k)
    hi = min(lo + 1, len(v) - 1)
    return v[lo] + (v[hi] - v[lo]) * (k - lo)


def close(a, b, tol):
    return a is not None and b is not None and len(a) == len(b) and all(abs(x - y) <= tol for x, y in zip(a, b))


def check_run(ck, path, corpus, builds, roots_seen):
    with open(path, encoding='utf-8') as f:
        run = json.load(f)
    name = os.path.basename(path)
    if not isinstance(run, dict) or not isinstance(run.get('reports'), list) or not isinstance(run.get('checks'), list):
        ck.result(False, '%s is a terrain test result' % name, 'no reports/checks lists: not a -terraintile out.json')
        return
    ck.result(run.get('failed', 1) == 0, '%s harness' % name,
              'harness %s passed, %s failed, %s skipped' % (run.get('passed'), run.get('failed'), run.get('skipped')))
    for c in run.get('checks', []):
        if c.get('result') == 'SKIP':   # the harness's own skips stay skips here: never folded into the pass above
            ck.result(None, '%s harness: %s' % (name, c.get('check')), c.get('detail', ''))
    loads = {l['load']: l for l in run.get('loads', [])}
    served = run.get('served', [])
    for rep in run.get('reports', []):
        if rep.get('status') != 'built':
            continue
        load = loads.get(rep.get('load'), {})
        product, build = load.get('product'), load.get('build')
        mp, tx, ty = rep.get('mapId'), rep.get('tileX'), rep.get('tileY')
        tag = '%s load %s %s %s %s_%s' % (name, rep.get('load'), product, mp, tx, ty)
        corpus_build = builds.get(product)
        same_build = corpus_build is not None and corpus_build == build
        # served bytes for this load, by role
        srv = {s['role']: s for s in served if s.get('load') == rep.get('load') and s.get('state') == 'available'}
        for role, key in (('wdt', 'wdtMd5'), ('rootAdt', 'rootMd5')):
            s = srv.get(role)
            ck.result(s is not None and s.get('md5', '').lower() == str(rep.get(key, '')).lower(),
                      '%s %s md5 parsed == served' % (tag, role),
                      'player %s, host %s' % (rep.get(key), s.get('md5') if s else None))
        pin = PINNED.get((mp, tx, ty))
        if pin:
            ck.result(rep.get('rootFileDataID') == pin['root'], '%s pinned root id' % tag,
                      'reported %s, pinned %s' % (rep.get('rootFileDataID'), pin['root']))
            for k in ('holeCells', 'triangles', 'rootBytes'):
                if k in pin:
                    ck.result(rep.get(k) == pin[k], '%s pinned %s' % (tag, k), 'reported %s, pinned %s' % (rep.get(k), pin[k]))
        ck.result(rep.get('chunksBuilt', 0) + rep.get('chunksSkipped', 0) + rep.get('chunksUnsupported', 0) == 256,
                  '%s chunk states add up to 256' % tag)
        if mp != 0:
            ck.result(None, '%s corpus comparison' % tag, 'the corpus holds Azeroth (map 0) only')
            continue
        if not same_build:
            ck.result(None, '%s corpus comparison' % tag,
                      'client build %s differs from the corpus build %s' % (build, corpus_build))
            continue
        wdt_path = os.path.join(corpus, 'wdt', product, '%s.bin' % rep.get('wdtFileDataID'))
        if os.path.isfile(wdt_path):
            ck.result(md5_file(wdt_path) == str(rep.get('wdtMd5', '')).lower(), '%s WDT md5 == corpus' % tag)
            wdt = WDT(R.read_bytes(wdt_path))
            word0 = wdt.maid[ty * 64 + tx][0] if wdt.maid else None
            ck.result(word0 == rep.get('rootFileDataID'), '%s root id == MAID word 0' % tag,
                      'MAID %s, reported %s' % (word0, rep.get('rootFileDataID')))
        else:
            ck.result(None, '%s WDT md5 == corpus' % tag, 'no corpus WDT %s' % wdt_path)
        root_path = os.path.join(corpus, 'corpus', product, 'azeroth', '%s.bin' % rep.get('rootFileDataID'))
        if not os.path.isfile(root_path):
            ck.result(None, '%s reference comparison' % tag, 'no corpus root %s' % root_path)
            continue
        buf = R.read_bytes(root_path)
        md5 = hashlib.md5(buf).hexdigest()
        ck.result(md5 == str(rep.get('rootMd5', '')).lower(), '%s root md5 == corpus' % tag,
                  'corpus %s, player %s' % (md5, rep.get('rootMd5')))
        roots_seen.setdefault((mp, tx, ty), {})[product] = (md5, str(rep.get('rootMd5', '')).lower())
        ref = R.build_tile(buf, tx, ty)
        c = ref['counts']
        for rk, ok_ in (('chunksBuilt', 'built'), ('chunksSkipped', 'skipped'), ('chunksUnsupported', 'unsupported'),
                        ('vertices', 'vertices'), ('triangles', 'triangles'), ('holeCells', 'hole_cells')):
            ck.result(rep.get(rk) == c[ok_], '%s %s == reference' % (tag, rk), 'reported %s, reference %s' % (rep.get(rk), c[ok_]))
        ck.result(rep.get('meshBytes') == ref['meshbytes'], '%s meshBytes == reference' % tag,
                  'reported %s, reference %s' % (rep.get('meshBytes'), ref['meshbytes']))
        rb = ref.get('bounds')
        if rb:
            ok = close(rep.get('boundsMin'), list(rb[0]), 1e-4) and close(rep.get('boundsMax'), list(rb[1]), 1e-4)
            ck.result(ok, '%s bounds == reference (1e-4 yd)' % tag,
                      'reported %s..%s, reference %s..%s' % (rep.get('boundsMin'), rep.get('boundsMax'), rb[0], rb[1]))


def check_cross_client(ck, roots_seen):
    for key, per in sorted(roots_seen.items()):
        prods = sorted(per)
        for i in range(len(prods)):
            for j in range(i + 1, len(prods)):
                a, b = per[prods[i]], per[prods[j]]
                if a[0] != b[0]:
                    ck.result(a[1] != b[1], 'tile %s_%s_%s %s vs %s: different corpus roots give different reported md5s'
                              % (key + (prods[i], prods[j])))
                else:
                    ck.result(None, 'tile %s_%s_%s %s vs %s' % (key + (prods[i], prods[j])), 'the corpus roots are identical')


def check_serving(ck, paths):
    groups = {}
    first = []
    for p in paths:
        with open(p, encoding='utf-8') as f:
            run = json.load(f)
        if not isinstance(run, dict):
            continue
        loads = {l['load']: l for l in run.get('loads', [])}
        for s in run.get('served', []):
            if s.get('state') != 'available':
                continue
            if s.get('first'):
                first.append((os.path.basename(p), s))
                continue
            # per client: two clients' files of one id are other bytes (Retail and Beta WDT 775971 share a size)
            client = loads.get(s.get('load'), {}).get('product', '?')
            groups.setdefault((client, s['role'], s['fileDataID'], s.get('bytes')), []).append(s)
    for (client, role, fdid, size), recs in sorted(groups.items(), key=lambda kv: kv[0][:3]):
        h = [r['handlerMs'] for r in recs]
        rd = [r['readMs'] for r in recs]
        detail = 'n=%d bytes=%s handler median %.2f p95 %.2f max %.2f ms; read median %.2f p95 %.2f max %.2f ms' % (
            len(h), size, statistics.median(h), pct(h, 95), max(h), statistics.median(rd), pct(rd, 95), max(rd))
        if len(h) >= 10:
            ck.result(statistics.median(h) <= BUDGET_MS and pct(h, 95) <= BUDGET_MS,
                      'serving %s %s %s within %g ms' % (client, role, fdid, BUDGET_MS), detail)
        else:
            print('[info] serving %s %s %s: %s (fewer than 10 requests, no verdict)' % (client, role, fdid, detail))
    for name, s in first:
        print('[info] first read in its process (%s): %s %s %s bytes handler %.2f ms read %.2f ms' % (
            name, s['role'], s['fileDataID'], s.get('bytes'), s['handlerMs'], s['readMs']))


def main(argv):
    corpus = os.environ.get('WMV_WORLD_AUDIT_CORPUS', 'F:/wowmodelviewer/build/world-audit-data')
    pins = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tests', 'corpus_hashes.json')
    paths = []
    i = 1
    while i < len(argv):
        if argv[i] == '--corpus':
            corpus = argv[i + 1]
            i += 2
        elif argv[i] == '--pins':
            pins = argv[i + 1]
            i += 2
        else:
            paths.append(argv[i])
            i += 1
    if not paths:
        print(__doc__)
        return 2
    with open(pins, encoding='utf-8') as f:
        builds = json.load(f)['client_builds']
    ck = Checker()
    roots_seen = {}
    for p in paths:
        check_run(ck, p, corpus, builds, roots_seen)
    check_cross_client(ck, roots_seen)
    check_serving(ck, paths)
    print('RESULT: %s (%d passed, %d failed, %d skipped)' % ('PASS' if ck.failed == 0 else 'FAIL', ck.passed, ck.failed,
                                                            ck.skipped))
    return ck.failed


if __name__ == '__main__':
    sys.exit(main(sys.argv))
