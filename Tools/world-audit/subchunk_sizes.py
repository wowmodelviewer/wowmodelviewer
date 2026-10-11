"""Sizes of the root MCNK sub-chunks (MCVT, MCNR, MCCV, MCSE, MCBB, MPTX) and the tex0 MCSH, per client:

  python subchunk_sizes.py <corpus folder> <out.json>
"""
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from wowfmt import adt as A  # noqa: E402


def main(corpus, out_path):
    cj = json.load(open(os.path.join(corpus, 'corpus.json')))
    sizes = collections.defaultdict(collections.Counter)
    for f in cj['files']:
        if f['status'] != 'OK' or f.get('what') not in ('root', 'tex0'):
            continue
        b = open(os.path.join(corpus, '%d.bin' % f['fdid']), 'rb').read()
        for _, _, sub in A.SplitFile(b, root=f['what'] == 'root').mcnk:
            for c in sub.chunks:
                if c.id in ('MCVT', 'MCNR', 'MCCV', 'MCSE', 'MCBB', 'MPTX', 'MCSH'):
                    sizes[c.id][c.size] += 1
    out = {k: {str(s): n for s, n in v.most_common()} for k, v in sorted(sizes.items())}
    json.dump(out, open(out_path, 'w'), indent=1)
    print(json.dumps(out))


if __name__ == '__main__':
    main(*sys.argv[1:3])
