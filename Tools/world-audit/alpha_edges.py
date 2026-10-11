"""Whether alpha maps repeat their last row and column (row 63 = row 62, column 63 = column 62), per client:

  python alpha_edges.py <corpus folder> <out.json>

Counted over every decoded alpha layer of the corpus tiles (coords.decode_layers, the proven layout row*64 + col).
A map whose last row/column always equals the one before holds 63x63 distinct texels; this matters for how texels map
to positions on the chunk and for any code that copies row 62 into row 63.
"""
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from wowfmt import adt as A  # noqa: E402
from coords import decode_layers  # noqa: E402


def main(corpus, out_path):
    cj = json.load(open(os.path.join(corpus, 'corpus.json')))
    c = collections.Counter()
    for f in cj['files']:
        if f.get('what') != 'tex0' or f['status'] != 'OK':
            continue
        b = open(os.path.join(corpus, '%d.bin' % f['fdid']), 'rb').read()
        for _, _, sub in A.SplitFile(b, root=False).mcnk:
            layers, alphas = decode_layers(b, sub, True)
            for L, a in zip(layers[1:], alphas):
                if not a or len(a) != 4096:
                    continue
                c['maps'] += 1
                if not L['flags'] & 0x200 and max(a) > 15:
                    # uncompressed 8-bit layers: where code that copies row 62 into row 63 would change the data
                    c['raw 8-bit maps'] += 1
                    c['raw 8-bit maps with row 63 != row 62'] += a[63 * 64:] != a[62 * 64:63 * 64]
                if all(v == 0 for v in a) or all(v == a[0] for v in a):
                    c['uniform maps'] += 1
                    continue
                row = a[63 * 64:] == a[62 * 64:63 * 64]
                col = all(a[r * 64 + 63] == a[r * 64 + 62] for r in range(64))
                c['non-uniform maps'] += 1
                c['row 63 = row 62'] += row
                c['column 63 = column 62'] += col
                c['both'] += row and col
    json.dump(dict(c), open(out_path, 'w'), indent=1)
    print(json.dumps(dict(c)))


if __name__ == '__main__':
    main(*sys.argv[1:3])
