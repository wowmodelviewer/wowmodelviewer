"""MDDF (doodad) placement transform and scale, measured against obj1: MLDD repeats the tile's MDDF records and MLDX
holds one 28-byte record per MLDD record -- six floats that this test treats as a box, plus a seventh value. The M2's
own box (MD20 header, offset 0xA0) is placed by each candidate transform and compared with the six floats; the
candidate that reproduces them names the convention (and shows what MLDX is).

  python mldx.py <corpus folder> <assets folder with the M2s> <out.json>

Also checks that MLDD equals obj0 MDDF record by record (by uniqueId). Doodads are counted once per uniqueId.
"""
import itertools
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from wowfmt import adt as A  # noqa: E402
from wowfmt.chunks import walk  # noqa: E402
from rotation import IDENTITY, apply, mul, rot  # noqa: E402

T = 1600.0 / 3.0


def m2_box(path):
    b = open(path, 'rb').read()
    if b[:4] == b'MD21':
        base = 8
    elif b[:4] == b'MD20':
        base = 0
    else:
        return None
    if len(b) < base + 0xA0 + 24:
        return None
    return struct.unpack_from('<6f', b, base + 0xA0)


def main(corpus, assets, out_path):
    cj = json.load(open(os.path.join(corpus, 'corpus.json')))
    tiles = {}
    for f in cj['files']:
        if 'x' in f and f['status'] == 'OK' and f['what'] in ('obj0', 'obj1'):
            tiles.setdefault((f['x'], f['y']), {})[f['what']] = os.path.join(corpus, '%d.bin' % f['fdid'])
    same = differ = missing = 0
    flag_diff = {}
    records = {}
    for (x, y), parts in tiles.items():
        if 'obj0' not in parts or 'obj1' not in parts:
            continue
        b0 = open(parts['obj0'], 'rb').read()
        o0 = A.SplitFile(b0, root=False)
        mddf = {d['unique_id']: d for d in A.mddf(b0, o0.walk.one('MDDF'))}
        b1 = open(parts['obj1'], 'rb').read()
        w1 = walk(b1)
        mldd = A.mddf(b1, w1.one('MLDD'))
        mldx = w1.one('MLDX')
        if not mldx or mldx.size != 28 * len(mldd):
            continue
        for i, d in enumerate(mldd):
            o = mddf.get(d['unique_id'])
            if o is None:
                missing += 1
            elif all(o[k] == d[k] for k in ('name_id', 'position', 'rotation', 'scale')):
                same += 1
                key = '0x%X->0x%X' % (o['flags'], d['flags'])
                flag_diff[key] = flag_diff.get(key, 0) + 1
            else:
                differ += 1
            box = struct.unpack_from('<7f', b1, mldx.data_offset + 28 * i)
            records.setdefault(d['unique_id'], (d, box))
    boxes = {}
    for uid, (d, box) in records.items():
        if d['name_id'] not in boxes:
            path = os.path.join(assets, '%d.bin' % d['name_id'])
            boxes[d['name_id']] = m2_box(path) if os.path.exists(path) else None
    items = [(d, box) for d, box in records.values() if boxes.get(d['name_id'])]

    frames = {
        'world (X, Y, Z)': lambda W: W,
        'placement (32T-Y, Z, 32T-X)': lambda W: (32 * T - W[1], W[2], 32 * T - W[0]),
    }
    results = []
    for yaw_sign, yaw_offset in itertools.product((1, -1), (0.0, 180.0)):
        for s0, s2 in itertools.product((1, -1), repeat=2):
            for axes in ((1, 0), (0, 1)):                    # (axis of rotation[0], axis of rotation[2])
                for scale_rule in ('u16/1024', 'ignored'):
                    for fname, frame in frames.items():
                        within = 0
                        tilted_within = 0
                        tilted = 0
                        for d, box in items:
                            m = mul(mul(rot(2, yaw_sign * d['rotation'][1] + yaw_offset), rot(axes[0], s0 * d['rotation'][0])),
                                    rot(axes[1], s2 * d['rotation'][2]))
                            s = d['scale'] / 1024.0 if scale_rule == 'u16/1024' else 1.0
                            lo, hi = boxes[d['name_id']][:3], boxes[d['name_id']][3:]
                            p = d['position']
                            W0 = (32 * T - p[2], 32 * T - p[0], p[1])
                            pts = []
                            for c in itertools.product(*zip(lo, hi)):
                                v = apply(m, tuple(s * q for q in c))
                                pts.append(frame((W0[0] + v[0], W0[1] + v[1], W0[2] + v[2])))
                            got = [min(q[i] for q in pts) for i in range(3)] + [max(q[i] for q in pts) for i in range(3)]
                            err = max(abs(g - x) for g, x in zip(got, box[:6]))
                            ok = err <= 0.5
                            within += ok
                            if abs(d['rotation'][0]) > 0.01 or abs(d['rotation'][2]) > 0.01:
                                tilted += 1
                                tilted_within += ok
                        results.append({'yaw': '%+d*a1%+d' % (yaw_sign, yaw_offset), 'pitch_roll_signs': [s0, s2],
                                        'rotation0_about': 'XY'[axes[0]], 'rotation2_about': 'XY'[axes[1]],
                                        'scale': scale_rule, 'mldx_frame': fname, 'doodads': len(items),
                                        'within_0_5_yard': within, 'tilted': tilted, 'tilted_within_0_5_yard': tilted_within})
    results.sort(key=lambda r: -r['within_0_5_yard'])
    out = {'mldd_equals_mddf': {'same': same, 'differ': differ, 'not_in_mddf': missing, 'flags_obj0_to_obj1': flag_diff},
           'unique_doodads_with_mldx': len(records), 'with_m2_box': len(items), 'best': results[:6],
           'best_scale_ignored': next(r for r in results if r['scale'] == 'ignored'),
           'best_without_yaw_offset': next(r for r in results if r['yaw'].endswith('+0'))}
    json.dump(out, open(out_path, 'w'), indent=1)
    print(json.dumps({k: out[k] for k in out if k != 'best'}, indent=1))
    for r in results[:6]:
        print(r)


if __name__ == '__main__':
    main(*sys.argv[1:4])
