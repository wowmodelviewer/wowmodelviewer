"""MAIN and MPHD flag facts over every extracted WDT of a client, as counts (no meaning assumed):

  python wdt_flags.py <audit data folder> <client> <out.json>

  - MAIN flag value against "MAID names files" for every entry, in WDTs with and without MAID;
  - MPHD flag bits seen, and bit 0x1 against "WMO-only" (a MODF and no MAIN tile with flag bit 0x1);
  - MPHD word 7 (the eighth word) against WMO-only;
  - the global MODF of WMO-only WDTs: rotation, scale and flags, by WDT form (MWMO path or FileDataID name).
"""
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from wowfmt.wdt import WDT  # noqa: E402


def main(data, client, out_path):
    folder = os.path.join(data, 'wdt', client)
    main_vs_maid = collections.Counter()
    bits = collections.Counter()
    bit1 = collections.Counter()
    word7 = collections.Counter()
    modf = collections.Counter()
    wdts = 0
    for fn in sorted(os.listdir(folder)):
        if not (fn.endswith('.bin') and fn[:-4].isdigit()):
            continue
        w = WDT(open(os.path.join(folder, fn), 'rb').read())
        if not w.main or not w.mphd_words:
            continue
        wdts += 1
        has_maid = w.maid is not None
        tiles = sum(1 for m in w.main if m[0] & 1)
        for i, m in enumerate(w.main):
            named = has_maid and any(w.maid[i])
            main_vs_maid['%s: flag 0x%X, %s' % ('WDT with MAID' if has_maid else 'WDT without MAID', m[0],
                                               'MAID names files' if named else 'no MAID words')] += 1
        flags = w.mphd_words[0]
        for b in range(32):
            if flags & (1 << b):
                bits['0x%X' % (1 << b)] += 1
        wmo_only = bool(w.modf) and tiles == 0
        bit1['bit 0x1 %s, %s' % ('set' if flags & 1 else 'clear', 'WMO-only' if wmo_only else 'has tiles or no MODF')] += 1
        if len(w.mphd_words) > 7:
            word7['word 7 %s, %s' % ('non-zero' if w.mphd_words[7] else 'zero', 'WMO-only' if wmo_only else 'not WMO-only')] += 1
        for m in w.modf:
            modf['%s; rotation %s; scale %d; flags 0x%X' % ('MWMO path' if w.mwmo else 'FileDataID name',
                                                         tuple(round(r, 1) for r in m['rotation']), m['scale'], m['flags'])] += 1
        if w.modf and tiles:
            modf['WDT with both MODF and tiles'] += 1
    out = {'client': client, 'wdts': wdts, 'main_vs_maid': dict(main_vs_maid.most_common()),
           'mphd_bits_seen': dict(sorted(bits.items(), key=lambda kv: int(kv[0], 16))), 'mphd_bit_0x1': dict(bit1),
           'mphd_word_7': dict(word7), 'wdt_modf': dict(modf.most_common())}
    json.dump(out, open(out_path, 'w'), indent=1)
    print(json.dumps(out, indent=1))


if __name__ == '__main__':
    main(*sys.argv[1:4])
