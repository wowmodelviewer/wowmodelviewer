# World-format audit tools

Read-only probes and parsers behind `docs/world-viewer/00-audit.md`. They are not part of the viewer, its build or
its installer, and never write to a game folder.

- `casc_extract.cpp` / `build_casc_extract.cmd` -- copies files out of a locally installed client by FileDataID (or by
  path with `n <path>`; `s <id>` records only the status), recording per file `OK`, `NOT_IN_ROOT`, `NOT_LOCAL`
  (decided from one enumeration of the storage, counts in `index.txt`), `ENCRYPTED_PARTIAL` (blocks under a missing
  key read as zeros, and marked) or `ENCRYPTED`/`READ_ERROR`; `timing.txt` splits storage open, enumeration and read
  time. Build it against a viewer build's CascLib:
  `build_casc_extract.cmd <build>\ThirdParty\casclib\Release\casc.lib <out folder>`.
- `wowfmt/` -- bounds-checked Python readers (`chunks`, `db2`, `wdt`, `adt`, `listfile`). Malformed input is reported,
  never read past. The listfile is a lookup aid, never proof.
- Scripts, roughly in the order the audit ran them:
  - catalog and WDTs: `map_catalog.py` (Map.db2 columns), `audit_wdt.py` (WDT census), `wdt_flags.py` (MAIN/MPHD
    flag counts), `maid_content.py` (MAID roles and Map.db2 field 21 without the listfile);
  - corpus: `corpus.py` (extract a tile window by MAID), `select_corpus.py` (scenario tiles by fixed rules),
    `availability.py` (status of every file the WDTs name);
  - ADT: `audit_adt.py` (split-file structure), `subchunk_sizes.py`, `coords.py` (positions, vertex order, seams,
    placement height, holes, alpha orientation, nibble order), `alpha_edges.py`, `scale4.py` (4-bit vs 8-bit values);
  - placements: `rotation.py` (MODF against stored extents), `mldx.py` (MDDF against obj1 MLDX boxes);
  - WDL and side files: `audit_side.py`;
  - renderer and load: `terrain_needs.py`, `load_pressure.py`, `read_timing.py`;
  - report: `report_tables.py` (the report's appendix), `pin_corpus.py` (hashes every cited file and file set);
  - C# parsers: `compare_csharp.py` compares the C# world parsers' corpus results
    (`Tools/UnityRendererProject/Tests/WorldCorpusValidation.cs`) with these scripts' results, key by key
    (`docs/world-viewer/01-parsers.md`).
- `tests/` -- `python -I -m unittest discover -s Tools/world-audit/tests`:
  - `test_wowfmt.py`: the parsers on malformed and synthetic input, and the pinned hashes;
  - `test_corpus_rules.py`: the report's rules asserted on the extracted bytes, each against its competing readings.

  The data-dependent tests run when `WMV_WORLD_AUDIT_CORPUS` names the extracted data folder (here
  `F:/wowmodelviewer/build/world-audit-data`, git-ignored); otherwise they are skipped with a message.

The full reproduce sequence is in section 10 of the report. Python 3.10+, standard library only. Run scripts with
`python -I`.
