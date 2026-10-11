"""How a 4-bit alpha value relates to 8 bits, measured: the same tile, chunk and texture (by FileDataID) in Classic Era
(4-bit maps) and in Retail (8-bit maps of the same terrain). For each 4-bit value n, the Retail values at the same
texels are tallied; candidate expansions (17n, 16n, 16n+15) are scored. This shows how Blizzard re-encoded these
maps; how the Era client itself expands 4-bit values at draw time is not measurable from files.

  python scale4.py <era corpus folder> <retail corpus folder> <out.json>
"""
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from wowfmt import adt as A  # noqa: E402
from coords import decode_layers  # noqa: E402


def tex0_by_tile(folder):
    cj = json.load(open(os.path.join(folder, 'corpus.json')))
    return {(f['x'], f['y']): os.path.join(folder, '%d.bin' % f['fdid']) for f in cj['files']
            if f.get('what') == 'tex0' and f['status'] == 'OK'}


def alphas_by_texture(path):
    b = open(path, 'rb').read()
    t = A.SplitFile(b, root=False)
    mdid = A.u32s(b, t.walk.one('MDID'))
    out = []
    for c, _, sub in t.mcnk:
        layers, alphas = decode_layers(b, sub, True)
        out.append({mdid[L['texture']]: a for L, a in zip(layers[1:], alphas) if a and L['texture'] < len(mdid)})
    return out


def main(era, retail, out_path):
    e_t, r_t = tex0_by_tile(era), tex0_by_tile(retail)
    tally = collections.defaultdict(collections.Counter)
    pairs = 0
    for key in sorted(set(e_t) & set(r_t)):
        ea, ra = alphas_by_texture(e_t[key]), alphas_by_texture(r_t[key])
        for n in range(min(len(ea), len(ra))):
            for fid, a4 in ea[n].items():
                a8 = ra[n].get(fid)
                if not a8 or len(a4) != 4096 or len(a8) != 4096 or max(a4) > 15:
                    continue
                pairs += 1
                for v4, v8 in zip(a4, a8):
                    tally[v4][v8] += 1
    out = {'map_pairs': pairs, 'per_value': {}}
    score = collections.Counter()
    total = 0
    for n in range(16):
        c = tally[n]
        if not c:
            continue
        cnt = sum(c.values())
        out['per_value'][n] = {'texels': cnt, 'retail_mode': c.most_common(1)[0][0],
                               'share_17n': c[17 * n] / cnt, 'share_16n': c[16 * n] / cnt,
                               'share_16n_plus_15': c[min(255, 16 * n + 15)] / cnt}
        if 1 <= n <= 14:
            total += cnt
            score['17n'] += c[17 * n]
            score['16n'] += c[16 * n]
            score['16n+15'] += c[16 * n + 15]
    out['share_over_values_1_to_14'] = {k: v / total for k, v in score.items()} if total else {}
    json.dump(out, open(out_path, 'w'), indent=1)
    print(json.dumps(out, indent=1))


if __name__ == '__main__':
    main(*sys.argv[1:4])
