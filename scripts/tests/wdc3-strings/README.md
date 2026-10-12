# WDC3 text fields across sections

The harness runs the production `WDC3File::open()` and `WDC3File::get()` from wow.dll over seven small synthetic tables built in memory. It needs no game data and no game storage. A subclass of `WDC3File` serves the bytes through `read()` and `seek()`: the reader opens a table without the memory buffer, so every header and section byte comes through them. Each record is a uint32 id (the key, stored inline) and a uint32 text value; no table is sparse.

The expected strings follow the format rules, worked out by hand in `regression.cpp` from each record's stored value:

- A text value is an offset counted from the field's own position, as if every section's records came first, back to back in section order, followed by every section's string block in section order. Records are numbered across sections by the section headers' record counts; their total is the header's `record_count`.
- A string starts inside some section's string block, and inside the bytes the file holds of it, and ends at the first NUL inside both. A string with no NUL before that end reads empty.
- A string in a section that was not read (zero-filled because its key is missing, or a section with no records) reads empty, and so does an offset that lands in the records or past every block.
- A copy-table row reads its source row's string.

Two expectations pin this reader's choices rather than the format: the record total is the header's `record_count` (`f2f`), and a section with no records is not read (`f2b`).

The tables: `f1_multisection` (strings in the record's own, an earlier and a later section, one starting mid-string, offsets into the records and past every block, a copy row), `f2a_zeroed_section` (an encrypted section without its key: its rows are left out, counted as 2 locked records with the key's name), `f2b_unread_section` (a string block in a section with no records), `f2c_block_past_buffer` (a declared string block longer than the file), `f2d_unterminated_eof` and `f2e_unterminated_before_copytable` (strings with no NUL before their block ends, at the end of the file and before a copy table) and `f2f_header_count_mismatch` (a header `record_count` that is not the sum of the sections' counts). A copy row whose source id does not exist is not covered.

A reader that bounds a string by the end of its section's buffer instead of its string block fails the two unterminated cases: it reads `tail`, and `cd` followed by the copy table's first byte.

What no case can show: the reader finds a record's section from integers kept when the records are listed, not by comparing the record's pointer with each section's buffer. Pointers into separate allocations have no defined order, but on the platforms the viewer is built for such a comparison happens to work, so a reader that went back to it would still pass every case here. That part is protected by review and by the comment at `WDC3File::m_recordLocations`. Likewise `f2c` passes on a reader that drops the check against the bytes the buffer holds only when the memory past the buffer happens to start with a NUL; a memory checker would catch it, a plain run may not.

Configure and build this directory as a standalone CMake project outside the source tree, passing `WMV_SOURCE_DIR` (the checkout whose headers match the build) and `WMV_BUILD_DIR` (that checkout's build directory: the Release `core.lib` and `wow.lib` are linked from it, and Qt is taken from its cache unless `Qt5Core_DIR` is given). Use the generator and x64 platform of the WMV build and build Release. The build copies that build's `core.dll` and `wow.dll` and the Qt DLLs beside `wdc3-strings.exe`, so the reader tested is the one whose headers it was compiled against (a `wow.dll` with another `WDC3File` layout would corrupt the test's own members); the C++ runtime comes from the system.

```powershell
cmake -S <this-directory> -B <build-directory> -A x64 -DWMV_SOURCE_DIR=<checkout> -DWMV_BUILD_DIR=<wmv-build>
cmake --build <build-directory> --config Release
<build-directory>/Release/wdc3-strings.exe
```

Each case prints `[wdc3-strings-test] PASS|FAIL <case> -- <detail>`, and the run ends with `[wdc3-strings-test] RESULT: PASS|FAIL (<n> passed, <m> failed)`. The exit code is 0 when every case passes, 1 when any fails and 2 on an exception.
