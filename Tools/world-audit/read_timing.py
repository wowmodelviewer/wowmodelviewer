"""How long the client's storage takes to hand over each load set's files (load_pressure.py's request_ids): every
file read whole through CascLib, as the viewer's host reads them, into a scratch folder that is emptied first. Each set
is read `runs` times; casc_extract's timing.txt splits storage start-up (open + index) from the reads. Nothing is
decoded here: this is the read floor a world view starts from, not a frame time.

  python read_timing.py <casc_extract.exe> <game> <product> <keys.csv> <load.json> <scratch folder> <out.json> [runs]

The first run of a set may already find the files in the operating system's cache (the audit read them before);
the report labels these warm-cache figures.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import time


def main(exe, game, product, keys, load_path, scratch, out_path, runs='3'):
    load = json.load(open(load_path))
    out = {'product': product, 'runs': {}}
    for name, ids in load['request_ids'].items():
        rows = []
        for k in range(int(runs)):
            if os.path.isdir(scratch):
                shutil.rmtree(scratch)
            os.makedirs(scratch)
            lst = os.path.join(scratch, 'ids.txt')
            with open(lst, 'w') as f:
                f.write(''.join('%d\n' % i for i in ids))
            t0 = time.perf_counter()
            subprocess.run([exe, game, product, scratch, keys, lst], check=True, stderr=subprocess.DEVNULL)
            wall = (time.perf_counter() - t0) * 1000
            line = open(os.path.join(scratch, 'timing.txt')).read().strip().splitlines()[-1]
            m = re.search(r'open (\d+) ms, index (\d+) ms, read (\d+) ms \((\d+) files, (\d+) bytes\)', line)
            o, ix, rd, files, nbytes = map(int, m.groups())
            rows.append({'run': k + 1, 'open_ms': o, 'index_ms': ix, 'read_ms': rd, 'files': files, 'bytes': nbytes,
                         'process_wall_ms': round(wall), 'mb_per_s': round(nbytes / 1e6 / (rd / 1000.0), 1) if rd else None,
                         'files_per_s': round(files / (rd / 1000.0)) if rd else None})
        out['runs'][name] = {'requested': len(ids), 'runs': rows}
        print(name, rows)
    shutil.rmtree(scratch, ignore_errors=True)
    json.dump(out, open(out_path, 'w'), indent=1)


if __name__ == '__main__':
    main(*sys.argv[1:9])
