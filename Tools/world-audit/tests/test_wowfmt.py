"""Tests of the audit's read-only parsers: malformed and truncated input must be reported, never read past; small
synthetic files must decode exactly. The corpus test runs only where the corpus has been extracted (corpus.py) and
checks every file against the hashes the audit report was written from.

  python -m unittest discover -s Tools/world-audit/tests
  (set WMV_WORLD_AUDIT_CORPUS to the audit data folder, e.g. F:/wowmodelviewer/build/world-audit-data, for the
  corpus tests here and in test_corpus_rules.py)
"""
import hashlib
import json
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
from wowfmt import adt as A  # noqa: E402
from wowfmt.chunks import walk  # noqa: E402
from wowfmt.db2 import DB2, FormatError  # noqa: E402
from wowfmt.wdt import WDT  # noqa: E402


def chunk(cid, data):
    return cid[::-1].encode('latin-1') + struct.pack('<I', len(data)) + data


class ChunkWalk(unittest.TestCase):
    def test_clean(self):
        w = walk(chunk('MVER', struct.pack('<I', 18)) + chunk('MPHD', bytes(32)))
        self.assertEqual(w.ids(), [('MVER', 1), ('MPHD', 1)])
        self.assertEqual(w.problems, [])
        self.assertEqual(w.one('MPHD').data_offset, 12 + 8)

    def test_size_past_end_is_reported_not_read(self):
        buf = chunk('MVER', struct.pack('<I', 18)) + b'NIAM' + struct.pack('<I', 1000) + bytes(10)
        w = walk(buf)
        self.assertEqual(w.ids(), [('MVER', 1)])
        self.assertTrue(any('claims 1000 bytes' in p for p in w.problems))

    def test_trailing_bytes(self):
        w = walk(chunk('MVER', struct.pack('<I', 18)) + b'\x01\x02\x03')
        self.assertTrue(any('trailing 3 bytes' in p for p in w.problems))

    def test_empty(self):
        w = walk(b'')
        self.assertEqual(w.chunks, [])
        self.assertEqual(w.problems, [])


class Wdt(unittest.TestCase):
    def make(self, main_size=4096 * 8, maid_size=4096 * 32):
        main = bytearray(main_size)
        struct.pack_into('<I', main, (48 * 64 + 32) * 8, 1)
        maid = bytearray(maid_size)
        if maid_size >= 4096 * 32:
            struct.pack_into('<8I', maid, (48 * 64 + 32) * 32, 11, 12, 13, 14, 15, 16, 17, 18)
        return (chunk('MVER', struct.pack('<I', 18)) + chunk('MPHD', struct.pack('<8I', 0x3CA, 1, 2, 3, 4, 5, 6, 0))
                + chunk('MAIN', bytes(main)) + chunk('MAID', bytes(maid)))

    def test_tiles_and_maid_words(self):
        w = WDT(self.make())
        self.assertEqual(w.version, 18)
        self.assertEqual(w.mphd_words[:2], [0x3CA, 1])
        self.assertEqual([(x, y) for x, y, _, _ in w.tiles()], [(32, 48)])
        self.assertEqual(w.tiles()[0][3], (11, 12, 13, 14, 15, 16, 17, 18))

    def test_main_wrong_size_is_reported(self):
        w = WDT(self.make(main_size=4096 * 8 + 4))
        self.assertTrue(any('MAIN size' in p for p in w.problems))

    def test_truncated_file(self):
        buf = self.make()
        w = WDT(buf[:100])
        self.assertTrue(w.problems)
        self.assertIsNone(w.maid)


class Db2(unittest.TestCase):
    def test_bad_magic(self):
        with self.assertRaises(FormatError):
            DB2(b'WDBC' + bytes(100))

    def test_truncated_header(self):
        with self.assertRaises(FormatError):
            DB2(b'WDC5' + struct.pack('<I', 5) + bytes(50))

    def test_minimal_table(self):
        # One section, two records of one plain 32-bit field, IDs in an ID list, one string field.
        fields = 2
        rec_size = 8
        strings = b'\0Azeroth\0Kalimdor\0'
        records = bytearray()
        ids = struct.pack('<II', 0, 1)
        header_len = 136 + 68 + 40 + 4 * fields + 24 * fields
        file_offset = header_len
        # field 0: string offset (from the field's own position), field 1: a number
        for i, (s_off, val) in enumerate(((1, 775971), (9, 782779))):
            pos_field = i * rec_size
            records += struct.pack('<II', (2 * rec_size - pos_field) + s_off, val)
        buf = bytearray()
        buf += b'WDC5' + struct.pack('<I', 5) + b'TEST'.ljust(128, b'\0')
        buf += struct.pack('<9I', 2, fields, rec_size, len(strings), 0, 0, 0, 1, 0)
        buf += struct.pack('<HH', 4, 0)
        buf += struct.pack('<7I', fields, 0, 0, 24 * fields, 0, 0, 1)
        buf += struct.pack('<Q8I', 0, file_offset, 2, len(strings), 0, len(ids), 0, 0, 0)
        buf += struct.pack('<hH', 0, 0) + struct.pack('<hH', 0, 4)
        buf += struct.pack('<HHIIIII', 0, 32, 0, 0, 0, 0, 0) + struct.pack('<HHIIIII', 32, 32, 0, 0, 0, 0, 0)
        self.assertEqual(len(buf), header_len)
        buf += records + strings + ids
        db = DB2(bytes(buf))
        rows = db.rows()
        self.assertEqual([r[0] for r in rows], [0, 1])
        self.assertEqual([r[2][1][0] for r in rows], [775971, 782779])
        self.assertEqual([db.string_at(r[1], db.fields[0], r[2][0][0]) for r in rows], ['Azeroth', 'Kalimdor'])
        with self.assertRaises(FormatError):
            DB2(bytes(buf[:-20]))


class Alpha(unittest.TestCase):
    def test_rle(self):
        stream = bytes([0x80 | 100, 7]) + bytes([3, 1, 2, 3])
        stream += bytes([0x80 | 127, 9]) * 31 + bytes([0x80 | 116, 9])
        vals, used = A.rle_alpha(stream, 0, len(stream))
        self.assertIsNotNone(vals)
        self.assertEqual(len(vals), 4096)
        self.assertEqual(vals[:103], bytes([7]) * 100 + bytes([1, 2, 3]))
        self.assertEqual(used, len(stream))

    def test_rle_truncated(self):
        vals, used = A.rle_alpha(bytes([0x80 | 100, 7]), 0, 2)
        self.assertIsNone(vals)


class Adt(unittest.TestCase):
    def test_root_mcnk_smaller_than_header(self):
        r = A.SplitFile(chunk('MVER', struct.pack('<I', 18)) + chunk('MCNK', bytes(40)), root=True)
        self.assertTrue(any('smaller than its header' in p for p in r.problems))

    def test_root_mcnk_header_and_subchunks(self):
        hdr = bytearray(128)
        struct.pack_into('<III', hdr, 0, 0x10000, 3, 2)
        struct.pack_into('<3f', hdr, 104, 1.0, 2.0, 3.0)
        body = bytes(hdr) + chunk('MCVT', struct.pack('<145f', *([0.5] * 145))) + chunk('MCSE', b'')
        r = A.SplitFile(chunk('MCNK', body), root=True)
        c, h, sub = r.mcnk[0]
        self.assertEqual((h['index_x'], h['index_y']), (3, 2))
        self.assertEqual(h['position'], (1.0, 2.0, 3.0))
        self.assertEqual(sub.ids(), [('MCVT', 1), ('MCSE', 1)])
        self.assertEqual(A.mcvt(r.buf, sub.one('MCVT'))[0], 0.5)

    def test_mh2o_instance_past_end(self):
        data = bytearray(256 * 12)
        struct.pack_into('<III', data, 0, 256 * 12, 3, 0)    # 3 instances claimed, none present
        r = A.SplitFile(chunk('MH2O', bytes(data)), root=True)
        m = A.mh2o(r.buf, r.walk.one('MH2O'))
        self.assertTrue(m['problems'])


class Corpus(unittest.TestCase):
    """Every file the report cites, and every file set a reported count depends on, against the SHA-256 the report was
    written from (tests/corpus_hashes.json, made by pin_corpus.py)."""

    @staticmethod
    def sha(path):
        return hashlib.sha256(open(path, 'rb').read()).hexdigest()

    def setUp(self):
        self.root = os.environ.get('WMV_WORLD_AUDIT_CORPUS')
        if not self.root:
            self.skipTest('WMV_WORLD_AUDIT_CORPUS not set (the audit data is extracted locally, not committed)')
        self.pins = json.load(open(os.path.join(os.path.dirname(__file__), 'corpus_hashes.json')))

    def test_files(self):
        bad = []
        for rel, want in self.pins['files'].items():
            p = os.path.join(self.root, rel)
            if not os.path.exists(p):
                bad.append('missing ' + rel)
            elif self.sha(p) != want:
                bad.append('changed ' + rel)
        self.assertEqual(bad, [], '%d files differ from the audited bytes (a client update?)' % len(bad))

    def test_sets(self):
        bad = []
        for rel, want in self.pins['sets'].items():
            folder = os.path.join(self.root, rel)
            if not os.path.isdir(folder):
                bad.append('missing ' + rel)
                continue
            names = sorted(n for n in os.listdir(folder) if n.endswith('.bin'))
            lines = ''.join('%s %s\n' % (n, self.sha(os.path.join(folder, n))) for n in names)
            if len(names) != want['files'] or hashlib.sha256(lines.encode('ascii')).hexdigest() != want['digest']:
                bad.append('changed %s (%d files, pinned %d)' % (rel, len(names), want['files']))
        self.assertEqual(bad, [], 'file sets differ from the audited bytes')


if __name__ == '__main__':
    unittest.main()
