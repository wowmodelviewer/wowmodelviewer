"""Read-only WDC3/WDC4/WDC5 (DB2) reader for the world-format audit.

It decodes what the file itself says -- the header, sections, field storage (plain, bitpacked, common data, pallet,
pallet array, signed bitpacked), ID lists, copy tables and strings -- without a schema: fields are numbered, not
named. Naming a field is the caller's job, and the audit only names one when the bytes prove it (e.g. a column whose
values are the FileDataIDs of world/maps/<Directory>/<Directory>.wdt in the listfile).

Every offset is checked against the file size; a malformed file raises FormatError instead of reading garbage.
"""
import struct


class FormatError(ValueError):
    pass


def _need(data, off, size, what):
    if off < 0 or size < 0 or off + size > len(data):
        raise FormatError('%s: %d bytes at %d past the end (%d)' % (what, size, off, len(data)))


class Field:
    __slots__ = ('index', 'bit_offset', 'bit_size', 'storage', 'v1', 'v2', 'v3', 'extra_size', 'element_bits', 'count',
                 'pallet_base', 'common')

    def __repr__(self):
        return 'Field(%d storage=%d bits@%d+%d count=%d)' % (self.index, self.storage, self.bit_offset, self.bit_size,
                                                             self.count)


class DB2:
    def __init__(self, data):
        self.data = data
        _need(data, 0, 4, 'magic')
        self.magic = data[0:4].decode('ascii', 'replace')
        off = 4
        if self.magic == 'WDC5':
            _need(data, 4, 132, 'WDC5 version/schema')
            self.version_num = struct.unpack_from('<I', data, 4)[0]
            self.schema = data[8:136].split(b'\0', 1)[0].decode('ascii', 'replace')
            off = 136
        elif self.magic in ('WDC3', 'WDC4'):
            self.version_num = int(self.magic[3])
            self.schema = ''
        else:
            raise FormatError('not a WDC3/4/5 file: %r' % data[0:4])
        _need(data, off, 72, 'header')
        (self.record_count, self.field_count, self.record_size, self.string_table_size, self.table_hash,
         self.layout_hash, self.min_id, self.max_id, self.locale) = struct.unpack_from('<9I', data, off)
        off += 36
        self.flags, self.id_index = struct.unpack_from('<HH', data, off)
        off += 4
        (self.total_field_count, self.bitpacked_data_offset, self.lookup_column_count, self.field_storage_info_size,
         self.common_data_size, self.pallet_data_size, self.section_count) = struct.unpack_from('<7I', data, off)
        off += 28
        self.sections = []
        for _ in range(self.section_count):
            _need(data, off, 40, 'section header')
            s = dict(zip(('tact_key_hash', 'file_offset', 'record_count', 'string_table_size', 'offset_records_end',
                          'id_list_size', 'relationship_data_size', 'offset_map_id_count', 'copy_table_count'),
                         struct.unpack_from('<Q8I', data, off)))
            self.sections.append(s)
            off += 40
        _need(data, off, 4 * self.field_count, 'field structures')
        structures = [struct.unpack_from('<hH', data, off + 4 * i) for i in range(self.field_count)]
        off += 4 * self.field_count
        n_info = self.field_storage_info_size // 24
        _need(data, off, self.field_storage_info_size, 'field storage info')
        self.fields = []
        pallet_base = 0
        common_base = 0
        for i in range(n_info):
            f = Field()
            f.index = i
            (f.bit_offset, f.bit_size, f.extra_size, f.storage, f.v1, f.v2, f.v3) = struct.unpack_from('<HHIIIII', data,
                                                                                                         off + 24 * i)
            size_field = structures[i][0] if i < len(structures) else 0
            f.element_bits = 32 - size_field
            f.count = 1
            if f.storage == 0 and f.element_bits > 0 and f.bit_size % f.element_bits == 0:
                f.count = max(1, f.bit_size // f.element_bits)
            if f.storage == 4:
                f.count = f.v3
            f.pallet_base = None
            f.common = None
            if f.storage in (3, 4):
                f.pallet_base = pallet_base
                pallet_base += f.extra_size
            if f.storage == 2:
                f.common = (common_base, f.extra_size)
                common_base += f.extra_size
            self.fields.append(f)
        off += self.field_storage_info_size
        _need(data, off, self.pallet_data_size + self.common_data_size, 'pallet/common data')
        self.pallet = data[off:off + self.pallet_data_size]
        off += self.pallet_data_size
        self.common_block = data[off:off + self.common_data_size]
        off += self.common_data_size
        self.after_common = off
        self._common_maps = {}
        for f in self.fields:
            if f.common is not None:
                base, size = f.common
                m = {}
                for k in range(0, size - size % 8, 8):
                    rid, val = struct.unpack_from('<II', self.common_block, base + k)
                    m[rid] = val
                self._common_maps[f.index] = m
        if self.flags & 1:
            raise FormatError('sparse (offset map) tables are not needed by the audit and are not decoded')
        self._layout_sections()

    def _layout_sections(self):
        data = self.data
        total_records = 0
        for s in self.sections:
            off = s['file_offset']
            s['records_at'] = off
            rec_bytes = s['record_count'] * self.record_size
            s['strings_at'] = off + rec_bytes
            s['ids_at'] = s['strings_at'] + s['string_table_size']
            s['copy_at'] = s['ids_at'] + s['id_list_size']
            s['rel_at'] = s['copy_at'] + 8 * s['copy_table_count'] + 6 * s['offset_map_id_count']
            s['end'] = s['rel_at'] + s['relationship_data_size'] + 4 * s['offset_map_id_count']
            s['zero_filled'] = s['tact_key_hash'] != 0 and data[off:off + rec_bytes].count(0) == rec_bytes and rec_bytes > 0
            if s['record_count']:
                _need(data, off, s['end'] - off, 'section at %d' % off)
            s['first_record'] = total_records
            total_records += s['record_count']

    # ------------------------------------------------------------------ values

    def _bits(self, rec_off, bit_off, bit_size):
        start = rec_off + bit_off // 8
        nbytes = (bit_off % 8 + bit_size + 7) // 8
        _need(self.data, start, nbytes, 'bit field')
        v = int.from_bytes(self.data[start:start + nbytes], 'little') >> (bit_off % 8)
        return v & ((1 << bit_size) - 1) if bit_size else 0

    def _field(self, f, rec_off, rid):
        if f.storage == 0:
            n = f.element_bits // 8 if f.element_bits % 8 == 0 and f.element_bits else 4
            vals = []
            for k in range(f.count):
                p = rec_off + f.bit_offset // 8 + k * n
                _need(self.data, p, n, 'field %d' % f.index)
                vals.append(int.from_bytes(self.data[p:p + n], 'little'))
            return vals
        if f.storage in (1, 5):
            v = self._bits(rec_off, f.bit_offset, f.bit_size)
            if f.storage == 5 and f.bit_size and v >> (f.bit_size - 1):
                v -= 1 << f.bit_size
            return [v]
        if f.storage == 2:
            return [self._common_maps[f.index].get(rid, f.v1)]
        if f.storage in (3, 4):
            idx = self._bits(rec_off, f.bit_offset, f.bit_size)
            count = f.count if f.storage == 4 else 1
            out = []
            for k in range(count):
                p = f.pallet_base + 4 * (idx * count + k)
                _need(self.pallet, p, 4, 'pallet of field %d' % f.index)
                out.append(struct.unpack_from('<I', self.pallet, p)[0])
            return out
        raise FormatError('field %d: unknown storage type %d' % (f.index, f.storage))

    def string_at(self, rec_off, f, value):
        """A string field's value is an offset from the field's own position, counted as if the records of all
        sections came first, one after another, and then all the sections' string tables, one after another."""
        rec_index = None
        for s in self.sections:
            if s['records_at'] <= rec_off < s['records_at'] + s['record_count'] * self.record_size:
                rec_index = s['first_record'] + (rec_off - s['records_at']) // self.record_size
                break
        if rec_index is None:
            return None
        records_total = sum(s['record_count'] for s in self.sections) * self.record_size
        k = rec_index * self.record_size + f.bit_offset // 8 + value - records_total
        if k < 0:
            return None
        for s in self.sections:
            if k < s['string_table_size']:
                p = s['strings_at'] + k
                end = self.data.find(b'\0', p, s['strings_at'] + s['string_table_size'])
                if end < 0:
                    return None
                return self.data[p:end].decode('utf-8', 'replace')
            k -= s['string_table_size']
        return None

    def rows(self):
        """Yields (id, record offset, [values per field], section index); copy-table rows repeat their source."""
        out = []
        for si, s in enumerate(self.sections):
            if not s['record_count']:
                continue
            ids = None
            if s['id_list_size']:
                ids = struct.unpack_from('<%dI' % (s['id_list_size'] // 4), self.data, s['ids_at'])
            for i in range(s['record_count']):
                rec_off = s['records_at'] + i * self.record_size
                rid = ids[i] if ids else None
                if rid is None:
                    rid = self._field(self.fields[self.id_index], rec_off, 0)[0]
                vals = [self._field(f, rec_off, rid) for f in self.fields]
                out.append((rid, rec_off, vals, si))
            for k in range(s['copy_table_count']):
                new_id, src = struct.unpack_from('<II', self.data, s['copy_at'] + 8 * k)
                for row in list(out):
                    if row[0] == src:
                        out.append((new_id, row[1], row[2], si))
                        break
        return out
