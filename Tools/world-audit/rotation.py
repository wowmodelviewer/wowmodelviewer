"""MODF placement transform, measured against the box the game stored for each placed WMO (MODF extents).

  python rotation.py <corpus folder> <assets folder with the WMO roots> <out.json>

The model's own box (MOHD, offset 36) is turned, scaled and moved by each candidate transform and its axis-aligned box
is compared with the stored extents -- all six numbers, offsets included (a box's size alone cannot tell a yaw a from
-a or a+180). Candidates: which world axis each of the three angles turns about, their signs, the order of the turns,
a constant yaw offset of 0/90/180/270 degrees, and (to test handedness) a mirror of one model axis before the turn.
Placements are counted once per uniqueId (a placement is listed in every tile it overlaps, identically).

Frames (coords.py): world W = (32T - p[2], 32T - p[0], p[1]) for a placement position p; extents are in the
placement frame and compared there.
"""
import itertools
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from wowfmt import adt as A  # noqa: E402
from wowfmt.chunks import walk  # noqa: E402

T = 1600.0 / 3.0


def rot(axis, deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    if axis == 0:
        return ((1, 0, 0), (0, c, -s), (0, s, c))
    if axis == 1:
        return ((c, 0, s), (0, 1, 0), (-s, 0, c))
    return ((c, -s, 0), (s, c, 0), (0, 0, 1))


def mul(a, b):
    return tuple(tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)) for i in range(3))


def apply(m, v):
    return tuple(sum(m[i][k] * v[k] for k in range(3)) for i in range(3))


IDENTITY = ((1, 0, 0), (0, 1, 0), (0, 0, 1))
MIRRORS = {'none': IDENTITY, 'mirror model x': ((-1, 0, 0), (0, 1, 0), (0, 0, 1)),
           'mirror model y': ((1, 0, 0), (0, -1, 0), (0, 0, 1)), 'mirror model z': ((1, 0, 0), (0, 1, 0), (0, 0, -1))}


def placed_box(p, box, m):
    s = p['scale'] / 1024.0 if p['flags'] & 0x4 else 1.0
    W0 = (32 * T - p['position'][2], 32 * T - p['position'][0], p['position'][1])
    pts = []
    for corner in itertools.product(*zip(box[:3], box[3:])):
        v = apply(m, tuple(s * c for c in corner))
        W = (W0[0] + v[0], W0[1] + v[1], W0[2] + v[2])
        pts.append((32 * T - W[1], W[2], 32 * T - W[0]))      # back to the placement frame
    return [min(q[i] for q in pts) for i in range(3)] + [max(q[i] for q in pts) for i in range(3)]


def transform(p, axis_of, signs, order, yaw_offset, mirror):
    """Turns applied right to left: m = R[order[0]] * R[order[1]] * R[order[2]] * mirror (order[2] acts first)."""
    angles = list(p['rotation'])
    m = IDENTITY
    for k in order:
        extra = yaw_offset if axis_of[k] == 2 else 0.0
        m = mul(m, rot(axis_of[k], signs[k] * angles[k] + extra))
    return mul(m, MIRRORS[mirror])


def main(corpus, assets, out_path):
    cj = json.load(open(os.path.join(corpus, 'corpus.json')))
    by_uid = {}
    entries = 0
    for f in cj['files']:
        if f.get('what') == 'obj0' and f['status'] == 'OK':
            b = open(os.path.join(corpus, '%d.bin' % f['fdid']), 'rb').read()
            o = A.SplitFile(b, root=False)
            for p in A.modf(b, o.walk.one('MODF')):
                entries += 1
                by_uid.setdefault(p['unique_id'], p)
    boxes = {}
    for w in {p['name_id'] for p in by_uid.values()}:
        path = os.path.join(assets, '%d.bin' % w)
        if os.path.exists(path):
            b = open(path, 'rb').read()
            mohd = walk(b).one('MOHD')
            if mohd and mohd.size >= 64:
                boxes[w] = struct.unpack_from('<6f', b, mohd.data_offset + 36)
    items = [p for p in by_uid.values() if p['name_id'] in boxes]
    tilted = [p for p in items if abs(p['rotation'][0]) > 0.01 or abs(p['rotation'][2]) > 0.01]
    results = []
    for axis_of in itertools.permutations(range(3)):
        for signs in itertools.product((1, -1), repeat=3):
            for order in itertools.permutations(range(3)):
                for yaw_offset in (0.0, 90.0, 180.0, 270.0):
                    for mirror in MIRRORS:
                        if mirror != 'none' and (yaw_offset not in (0.0, 180.0)):
                            continue
                        errs = []
                        for p in items:
                            got = placed_box(p, boxes[p['name_id']], transform(p, axis_of, signs, order, yaw_offset, mirror))
                            errs.append(max(abs(g - x) for g, x in zip(got, p['extents'])))
                        terr = [e for p, e in zip(items, errs) if p in tilted]
                        errs_sorted = sorted(errs)
                        results.append({'angle_k_turns_about_world_axis': list(axis_of), 'signs': list(signs),
                                        'order_left_to_right': list(order), 'yaw_offset': yaw_offset, 'mirror': mirror,
                                        'within_0_1_yard': sum(1 for e in errs if e <= 0.1),
                                        'tilted_within_0_1_yard': sum(1 for e in terr if e <= 0.1),
                                        'median_error': errs_sorted[len(errs_sorted) // 2]})
    results.sort(key=lambda r: (-r['within_0_1_yard'], r['median_error']))
    best = results[0]
    proper = [r for r in results if r['mirror'] == 'none']
    improper = [r for r in results if r['mirror'] != 'none']
    out = {'modf_entries': entries, 'unique_placements': len(by_uid), 'tested': len(items), 'tilted': len(tilted),
           'wmos': len(boxes), 'candidates': len(results), 'best': results[:8],
           'best_improper': improper[0] if improper else None,
           'best_without_yaw_offset': next(r for r in proper if r['yaw_offset'] == 0.0)}
    # Misfits under the winner: which placements, by how much.
    misfits = []
    for p in items:
        got = placed_box(p, boxes[p['name_id']], transform(p, best['angle_k_turns_about_world_axis'], best['signs'],
                                                           best['order_left_to_right'], best['yaw_offset'], best['mirror']))
        e = max(abs(g - x) for g, x in zip(got, p['extents']))
        if e > 0.1:
            misfits.append({'unique_id': p['unique_id'], 'wmo': p['name_id'], 'error': e})
    out['misfits_of_best'] = misfits
    json.dump(out, open(out_path, 'w'), indent=1)
    print('MODF entries %d, unique placements %d (tested %d, tilted %d), %d candidates'
          % (entries, len(by_uid), len(items), len(tilted), len(results)))
    for r in results[:5]:
        print(r)
    print('best without a yaw offset:', out['best_without_yaw_offset'])
    print('best with a mirrored model axis:', out['best_improper'])
    print('misfits of the best:', misfits)


if __name__ == '__main__':
    main(*sys.argv[1:4])
