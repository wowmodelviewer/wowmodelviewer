"""Which of the files a client's WDTs name (every nonzero MAID word of every extracted WDT) are listed by its root and
on this computer: the status-only census casc_extract writes (lines "s <id>"), attributed back to map, tile and MAID
word.

  python availability.py <audit data folder> <client> <out.json>

Reads <data>/avail/<client>/manifest.tsv, <data>/wdt/<client>/*.bin and <data>/catalog/maps_<client>.json.
"""
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from wowfmt.wdt import WDT  # noqa: E402

WORDS = ['root', 'obj0', 'obj1', 'tex0', 'lod', 'maptexture', 'maptexture_n', 'minimap']


def main(data, client, out_path):
    status = {}
    for line in open(os.path.join(data, 'avail', client, 'manifest.tsv')):
        p = line.rstrip('\n').split('\t')
        if len(p) >= 2 and p[0].isdigit():
            status[int(p[0])] = p[1]
    names = {}
    mp = os.path.join(data, 'catalog', 'maps_%s.json' % client)
    if os.path.exists(mp):
        for m in json.load(open(mp))['maps']:
            if m.get('wdt_fdid'):
                names[m['wdt_fdid']] = '%d %s' % (m['id'], m['directory'])
    by_word = collections.defaultdict(collections.Counter)
    by_map = collections.defaultdict(collections.Counter)
    examples = collections.defaultdict(list)
    named = 0
    folder = os.path.join(data, 'wdt', client)
    for fn in sorted(os.listdir(folder)):
        if not fn.endswith('.bin') or not fn[:-4].isdigit():
            continue
        fdid = int(fn[:-4])
        w = WDT(open(os.path.join(folder, fn), 'rb').read())
        if not w.maid:
            continue
        label = names.get(fdid, 'WDT %d' % fdid)
        for i, words in enumerate(w.maid):
            for k, v in enumerate(words):
                if not v:
                    continue
                named += 1
                s = status.get(v, 'NOT_CENSUSED')
                role = WORDS[k] if k < len(WORDS) else 'word%d' % k
                by_word[role][s] += 1
                by_map[label][s] += 1
                if s != 'OK' and len(examples[label]) < 4:
                    examples[label].append({'tile': '%d_%d' % (i % 64, i // 64), 'word': role, 'fdid': v, 'status': s})
    out = {'client': client, 'maid_words_named': named, 'distinct_ids_censused': len(status),
           'status_counts': dict(collections.Counter(status.values())),
           'by_maid_word': {k: dict(v) for k, v in by_word.items()},
           'maps_with_files_not_ok': {k: dict(v) for k, v in sorted(by_map.items()) if set(v) - {'OK'}},
           'examples': dict(examples)}
    json.dump(out, open(out_path, 'w'), indent=1)
    print(json.dumps({k: out[k] for k in ('client', 'maid_words_named', 'distinct_ids_censused', 'status_counts',
                                          'by_maid_word')}, indent=1))
    print('maps with files not OK:', len(out['maps_with_files_not_ok']))
    for k, v in list(out['maps_with_files_not_ok'].items())[:12]:
        print('  ', k, v)


if __name__ == '__main__':
    main(*sys.argv[1:4])
