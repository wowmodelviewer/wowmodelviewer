// WDC3 text fields across sections. The production WDC3File::open() and get() (wow.dll) read small synthetic tables
// held in memory: no game data, no game storage. Every table's record is a uint32 id (field 0, the key, stored inline)
// and a uint32 text value (field 1, at byte 4); no table is sparse.
//
// The rules the expected strings follow:
// - A text field's value is an offset counted from the field's own position, as if every section's records came
//   first, back to back in section order, followed by every section's string block in section order. Records are
//   numbered across sections by the section headers' record counts; their total is the header's record_count.
// - A string must start inside some section's string block, and inside the bytes the file holds of it, and ends at
//   the first NUL inside both. A string with no NUL before that end reads as an empty string.
// - A string in a section whose bytes were not read (zero-filled because its key is missing, or a section with no
//   records) reads empty. An offset that lands in the records, or past every block, reads empty.
// Two of these are this reader's choices, pinned here rather than taken from the format: the record total is the
// header's record_count, not the sum of the sections' counts (f2f), and a section with no records is not read (f2b).
// - A copy-table row reads its source row's string.
// So for record g (counted across sections), its field at byte 4 and a stored value v, the string starts at byte
//   o = 8 * g + 4 + v - 8 * record_count
// of the string blocks laid end to end. Each fixture below works o out by hand from the stored values.
//
// Rows are listed in the order the reader returns them: section 0's records, section 0's copy-table rows, then each
// later section's records (a zero-filled section has none).
#include "wdc3file.h"
#include "WoWDatabase.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Record { uint32_t id, text; };
struct Copy { uint32_t id, source; };
struct Section
{
  std::vector<Record> records;
  std::string block;              // the string block's bytes as the file holds them
  long long declared = -1;        // the block size the section header declares (-1: the bytes above)
  std::vector<Copy> copies;       // copy table
  uint64_t key = 0;               // tact_key_hash: the key the section is encrypted with (0: none)
  bool zeroFilled = false;        // records and block as the storage returns them without the key: zeros
  uint32_t declaredBlock() const
  {
    return static_cast<uint32_t>(declared >= 0 ? declared : static_cast<long long>(block.size()));
  }
};
struct Row { uint32_t id; std::string text; };
struct Fixture
{
  std::string name;
  std::vector<Section> sections;
  long long headerRecords = -1;   // the header's record_count (-1: the sections' records)
  std::vector<Row> rows;          // expected, in the reader's record order
  size_t lockedRows = 0;
  std::vector<unsigned long long> lockedKeys;
};

template <size_t N> static std::string bytes(const char (&text)[N]) { return std::string(text, N - 1); }
static Section section(std::vector<Record> records, std::string block, std::vector<Copy> copies = {})
{
  Section s;
  s.records = std::move(records);
  s.block = std::move(block);
  s.copies = std::move(copies);
  return s;
}

// The file: header, section headers, field structure, field storage info (both fields uncompressed 32-bit values),
// then each section's records, string block and copy table, back to back.
static std::vector<unsigned char> build(const Fixture & f)
{
  std::vector<unsigned char> out;
  auto put = [&](uint64_t value, int size) {
    for (int i = 0; i < size; ++i)
      out.push_back(static_cast<unsigned char>(value >> (8 * i)));
  };
  uint32_t records = 0, blocks = 0, minId = 0xFFFFFFFF, maxId = 0;
  for (const auto & s : f.sections)
  {
    records += static_cast<uint32_t>(s.records.size());
    blocks += s.declaredBlock();
    for (const auto & r : s.records)
    {
      minId = std::min(minId, r.id);
      maxId = std::max(maxId, r.id);
    }
  }
  const char magic[] = "WDC3";
  out.insert(out.end(), magic, magic + 4);
  put(f.headerRecords >= 0 ? static_cast<uint64_t>(f.headerRecords) : records, 4); // record_count
  put(2, 4);                     // field_count
  put(8, 4);                     // record_size
  put(blocks, 4);                // string_table_size, every section's
  put(0, 4);                     // table_hash
  put(0x12345678, 4);            // layout_hash
  put(minId, 4);
  put(maxId, 4);
  put(0, 4);                     // locale
  put(0, 2);                     // flags: not sparse, ids inline
  put(0, 2);                     // id_index
  put(2, 4);                     // total_field_count
  put(0, 4);                     // bitpacked_data_offset
  put(0, 4);                     // lookup_column_count
  put(2 * 24, 4);                // field_storage_info_size
  put(0, 4);                     // common_data_size
  put(0, 4);                     // pallet_data_size
  put(f.sections.size(), 4);     // section_count
  size_t at = 72 + 40 * f.sections.size() + 2 * 4 + 2 * 24;
  for (const auto & s : f.sections)
  {
    put(s.key, 8);
    put(at, 4);                  // file_offset
    put(s.records.size(), 4);
    put(s.declaredBlock(), 4);
    put(0, 4);                   // offset_records_end
    put(0, 4);                   // id_list_size
    put(0, 4);                   // relationship_data_size
    put(0, 4);                   // offset_map_id_count
    put(s.copies.size(), 4);
    at += 8 * s.records.size() + s.block.size() + 8 * s.copies.size();
  }
  for (uint32_t position : {0u, 4u})
  {
    put(0, 2);                   // size
    put(position, 2);
  }
  for (uint32_t offsetBits : {0u, 32u})
  {
    put(offsetBits, 2);
    put(32, 2);                  // field_size_bits
    for (int i = 0; i < 5; ++i)
      put(0, 4);                 // additional_data_size, storage_type 0 (none), val1..3
  }
  for (const auto & s : f.sections)
  {
    for (const auto & r : s.records)
    {
      put(s.zeroFilled ? 0 : r.id, 4);
      put(s.zeroFilled ? 0 : r.text, 4);
    }
    for (char c : s.block)
      out.push_back(s.zeroFilled ? 0 : static_cast<unsigned char>(c));
    for (const auto & c : s.copies)
    {
      put(c.id, 4);
      put(c.source, 4);
    }
  }
  if (out.size() != at)
    throw std::logic_error("fixture " + f.name + ": section offsets do not match its bytes");
  return out;
}

static std::vector<Fixture> fixtures()
{
  std::vector<Fixture> all;

  // Three readable sections: strings in the record's own section, an earlier and a later one, one starting
  // mid-string, one offset back into the records, one past every block, and a copy row. record_count 8 (3 + 3 + 2);
  // blocks 11 + 12 + 11 (section 0 from 0, section 1 from 11, section 2 from 23, end 34): o = 8g + 4 + v - 64.
  Fixture f1;
  f1.name = "f1_multisection";
  f1.sections = {section({{1, 89}, {2, 58}, {3, 43}}, bytes("alpha\0beta\0"), {{100, 2}}),
                 section({{10, 47}, {11, 28}, {12, 39}}, bytes("gamma\0delta\0")),
                 section({{20, 29}, {21, 38}}, bytes("omega\0zeta\0"))};
  f1.rows = {{1, "zeta"},    // g 0: 0 + 4 + 89 - 64 = 29 = 23 + 6, section 2 byte 6
             {2, "beta"},    // g 1: 8 + 4 + 58 - 64 = 6, section 0 byte 6
             {3, ""},        // g 2: 16 + 4 + 43 - 64 = -1, in the records
             {100, "beta"},  // copy of id 2
             {10, "gamma"},  // g 3: 24 + 4 + 47 - 64 = 11, section 1 byte 0
             {11, "alpha"},  // g 4: 32 + 4 + 28 - 64 = 0, section 0 byte 0
             {12, "lta"},    // g 5: 40 + 4 + 39 - 64 = 19 = 11 + 8, section 1 byte 8 (inside "delta")
             {20, "delta"},  // g 6: 48 + 4 + 29 - 64 = 17 = 11 + 6, section 1 byte 6
             {21, ""}};      // g 7: 56 + 4 + 38 - 64 = 34, past every block (34 bytes)
  all.push_back(f1);

  // Section 1 is encrypted with a key this computer does not have: the storage zero-fills it, so its records are left
  // out (counted as locked, with the key's name) and a string in its block reads empty. Its records still count in
  // g. record_count 5 (2 + 2 + 1); blocks 4 + 6 + 3 (from 0, 4, 10; end 13): o = 8g + 4 + v - 40.
  Fixture f2a;
  f2a.name = "f2a_zeroed_section";
  Section locked = section({{5, 0}, {6, 0}}, bytes("secret"));
  locked.key = 0xA1B2C3D4E5F60718ull;
  locked.zeroFilled = true;
  f2a.sections = {section({{1, 40}, {2, 38}}, bytes("s0a\0")), locked, section({{30, 4}}, bytes("s2\0"))};
  f2a.rows = {{1, ""},       // g 0: 0 + 4 + 40 - 40 = 4, section 1 byte 0: not read
              {2, "s2"},     // g 1: 8 + 4 + 38 - 40 = 10, section 2 byte 0
              {30, "s0a"}};  // g 4: 32 + 4 + 4 - 40 = 0, section 0 byte 0
  f2a.lockedRows = 2;
  f2a.lockedKeys = {0xA1B2C3D4E5F60718ull};
  all.push_back(f2a);

  // Section 1 has a string block but no records: it is not read, so a string there reads empty (though the file
  // holds "never" there). A choice of this reader (record-less sections are skipped), pinned, not a format rule. record_count 3 (2 + 0 + 1); blocks 2 + 6 + 2 (from 0, 2, 8; end 10): o = 8g + 4 + v - 24.
  Fixture f2b;
  f2b.name = "f2b_unread_section";
  f2b.sections = {section({{1, 22}, {2, 20}}, bytes("x\0")), section({}, bytes("never\0")),
                  section({{3, 4}}, bytes("y\0"))};
  f2b.rows = {{1, ""},       // g 0: 0 + 4 + 22 - 24 = 2, section 1 byte 0: not read
              {2, "y"},      // g 1: 8 + 4 + 20 - 24 = 8, section 2 byte 0
              {3, "x"}};     // g 2: 16 + 4 + 4 - 24 = 0, section 0 byte 0
  all.push_back(f2b);

  // The last section declares a 64-byte block, but the file ends after its first 3 bytes ("ok\0"). record_count 3
  // (1 + 2); blocks 2 + 64 (from 0, 2; end 66): o = 8g + 4 + v - 24.
  Fixture f2c;
  f2c.name = "f2c_block_past_buffer";
  Section cut = section({{2, 54}, {3, 7}}, bytes("ok\0"));
  cut.declared = 64;
  f2c.sections = {section({{1, 22}}, bytes("a\0")), cut};
  f2c.rows = {{1, "ok"},     // g 0: 0 + 4 + 22 - 24 = 2, section 1 byte 0
              {2, ""},       // g 1: 8 + 4 + 54 - 24 = 42, section 1 byte 40: past the bytes the file has
              {3, "k"}};     // g 2: 16 + 4 + 7 - 24 = 3, section 1 byte 1
  all.push_back(f2c);

  // The file ends with an unterminated string: section 1's block is "tail" with no NUL. record_count 2 (1 + 1);
  // blocks 2 + 4 (from 0, 2; end 6): o = 8g + 4 + v - 16.
  Fixture f2d;
  f2d.name = "f2d_unterminated_eof";
  f2d.sections = {section({{1, 14}}, bytes("a\0")), section({{2, 4}}, bytes("tail"))};
  f2d.rows = {{1, ""},       // g 0: 0 + 4 + 14 - 16 = 2, section 1 byte 0: no NUL before its block ends
              {2, "a"}};     // g 1: 8 + 4 + 4 - 16 = 0, section 0 byte 0
  all.push_back(f2d);

  // Section 0's block ends with an unterminated string ("ab\0cd"), and its copy table follows it in the file.
  // record_count 3 (2 + 1); blocks 5 + 2 (from 0, 5; end 7): o = 8g + 4 + v - 24.
  Fixture f2e;
  f2e.name = "f2e_unterminated_before_copytable";
  f2e.sections = {section({{1, 20}, {2, 15}}, bytes("ab\0cd"), {{7, 1}}), section({{3, 9}}, bytes("z\0"))};
  f2e.rows = {{1, "ab"},     // g 0: 0 + 4 + 20 - 24 = 0, section 0 byte 0
              {2, ""},       // g 1: 8 + 4 + 15 - 24 = 3, section 0 byte 3: "cd" reaches its block's end with no NUL
              {7, "ab"},     // copy of id 1
              {3, "z"}};     // g 2: 16 + 4 + 9 - 24 = 5, section 1 byte 0
  all.push_back(f2e);

  // The header's record_count (9) is not the sections' sum (3 + 3 + 2 = 8): the string blocks start after the
  // header's total, so o = 8g + 4 + v - 72 (not - 64). Which total governs is this reader's choice (the header's),
  // pinned, not a format rule; real tables have the two equal. Blocks as f1 (from 0, 11, 23; end 34).
  Fixture f2f;
  f2f.name = "f2f_header_count_mismatch";
  f2f.headerRecords = 9;
  f2f.sections = {section({{1, 97}, {2, 66}, {3, 43}}, bytes("alpha\0beta\0"), {{100, 2}}),
                  section({{10, 55}, {11, 36}, {12, 47}}, bytes("gamma\0delta\0")),
                  section({{20, 37}, {21, 38}}, bytes("omega\0zeta\0"))};
  f2f.rows = {{1, "zeta"},   // g 0: 0 + 4 + 97 - 72 = 29 = 23 + 6, section 2 byte 6
              {2, "beta"},   // g 1: 8 + 4 + 66 - 72 = 6, section 0 byte 6
              {3, ""},       // g 2: 16 + 4 + 43 - 72 = -9, in the records
              {100, "beta"}, // copy of id 2
              {10, "gamma"}, // g 3: 24 + 4 + 55 - 72 = 11, section 1 byte 0
              {11, "alpha"}, // g 4: 32 + 4 + 36 - 72 = 0, section 0 byte 0
              {12, "lta"},   // g 5: 40 + 4 + 47 - 72 = 19 = 11 + 8, section 1 byte 8
              {20, "delta"}, // g 6: 48 + 4 + 37 - 72 = 17 = 11 + 6, section 1 byte 6
              {21, "ga"}};   // g 7: 56 + 4 + 38 - 72 = 26 = 23 + 3, section 2 byte 3 (from - 64: 34, past every block)
  all.push_back(f2f);

  return all;
}

// The production reader over a table held in memory. WDC3File::open() opens without the memory buffer, so every byte
// it reads (header, section headers, field information, then each section) comes through read() and seek(), as from
// the game storage: a read past the end returns the bytes that are there.
class MemoryTable : public WDC3File
{
public:
  MemoryTable(const std::string & name, std::vector<unsigned char> data)
    : WDC3File(QString::fromStdString(name)), m_bytes(std::move(data)) {}
  size_t read(void * dest, size_t count) override
  {
    const size_t n = m_at < m_bytes.size() ? std::min(count, m_bytes.size() - m_at) : 0;
    if (n)
      memcpy(dest, m_bytes.data() + m_at, n);
    m_at += n;
    return n;
  }
  void seek(size_t offset) override { m_at = offset; }

protected:
  bool openFile() override { m_open = true; m_at = 0; return true; }
  bool isAlreadyOpened() override { return m_open; }
  bool getFileSize(unsigned long long & s) override { s = m_bytes.size(); return true; }
  unsigned long readFile() override
  {
    if (!m_bytes.empty())
      memcpy(buffer, m_bytes.data(), m_bytes.size());
    return static_cast<unsigned long>(m_bytes.size());
  }
  void doPostOpenOperation() override {}
  bool doPostCloseOperation() override { m_open = false; return true; }

private:
  std::vector<unsigned char> m_bytes;
  size_t m_at = 0;
  bool m_open = false;
};

static int passed = 0, failed = 0;
static void report(bool ok, const std::string & name, const std::string & detail)
{
  (ok ? passed : failed)++;
  std::cout << "[wdc3-strings-test] " << (ok ? "PASS " : "FAIL ") << name << " -- " << detail << "\n";
}
static std::string quoted(const std::string & s)
{
  std::string out = "\"";
  for (unsigned char c : s)
  {
    if (c >= 0x20 && c < 0x7F && c != '"' && c != '\\')
      out += static_cast<char>(c);
    else
    {
      char hex[8];
      snprintf(hex, sizeof(hex), "\\x%02X", c);
      out += hex;
    }
  }
  return out + "\"";
}
static std::string idList(const std::vector<std::string> & ids)
{
  std::string out;
  for (const auto & id : ids)
    out += (out.empty() ? "" : " ") + id;
  return "[" + out + "]";
}
static std::string keys(size_t rows, const std::vector<unsigned long long> & names)
{
  std::ostringstream out;
  out << rows << " locked records, keys [";
  for (size_t i = 0; i < names.size(); ++i)
  {
    char hex[24];
    snprintf(hex, sizeof(hex), "%016llX", names[i]);
    out << (i ? " " : "") << hex;
  }
  out << "]";
  return out.str();
}

static void run(const Fixture & f)
{
  MemoryTable table(f.name + ".db2", build(f));
  if (!table.open())
  {
    report(false, f.name + ":open", "open() failed");
    return;
  }
  report(true, f.name + ":open", std::to_string(f.sections.size()) + " sections");

  // Key (field 0) and text (field 1, storage info 1); the structure deletes its fields.
  core::TableStructure structure;
  auto * key = new wow::FieldStructure;
  key->name = "ID";
  key->type = "uint32";
  key->isKey = true;
  key->pos = 0;
  structure.fields.push_back(key);
  auto * text = new wow::FieldStructure;
  text->name = "Name";
  text->type = "text";
  text->pos = 1;
  text->arraySize = 1;
  structure.fields.push_back(text);

  std::vector<std::vector<std::string>> rows;
  for (size_t i = 0; i < table.getRecordCount(); ++i)
    rows.push_back(table.get(static_cast<unsigned int>(i), &structure));

  std::vector<std::string> expectedIds, ids;
  for (const auto & r : f.rows)
    expectedIds.push_back(std::to_string(r.id));
  for (const auto & r : rows)
    ids.push_back(r.empty() ? "?" : r[0]);
  const std::string got = std::to_string(ids.size()) + " records, ids " + idList(ids);
  report(ids == expectedIds, f.name + ":records",
         ids == expectedIds ? got
                            : "expected " + std::to_string(expectedIds.size()) + " records, ids " +
                                idList(expectedIds) + "; got " + got);

  for (size_t i = 0; i < f.rows.size(); ++i)
  {
    const std::string name = f.name + ":id" + std::to_string(f.rows[i].id);
    if (i >= rows.size())
      report(false, name, "record " + std::to_string(i) + " missing");
    else if (rows[i].size() != 2)
      report(false, name,
             "record " + std::to_string(i) + " has " + std::to_string(rows[i].size()) + " values, expected 2");
    else if (rows[i][0] != std::to_string(f.rows[i].id))
      report(false, name, "record " + std::to_string(i) + " has id " + rows[i][0]);
    else if (rows[i][1] != f.rows[i].text)
      report(false, name, "expected " + quoted(f.rows[i].text) + " got " + quoted(rows[i][1]));
    else
      report(true, name, quoted(rows[i][1]));
  }

  const bool lockedOk = table.getLockedRecordCount() == f.lockedRows && table.getLockedKeyNames() == f.lockedKeys;
  report(lockedOk, f.name + ":locked",
         lockedOk ? keys(f.lockedRows, f.lockedKeys)
                  : "expected " + keys(f.lockedRows, f.lockedKeys) + "; got " +
                      keys(table.getLockedRecordCount(), table.getLockedKeyNames()));
  table.close();
}

int main()
{
  try
  {
    for (const auto & f : fixtures())
      run(f);
  }
  catch (const std::exception & e)
  {
    report(false, "exception", e.what());
    std::cout << "[wdc3-strings-test] RESULT: FAIL (" << passed << " passed, " << failed << " failed)\n";
    return 2;
  }
  std::cout << "[wdc3-strings-test] RESULT: " << (failed ? "FAIL" : "PASS") << " (" << passed << " passed, " << failed
            << " failed)\n";
  return failed ? 1 : 0;
}
