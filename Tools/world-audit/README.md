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
  - map catalogue: `compare_map_catalog.py` compares the viewer's world map catalogue (its headless test,
    `wowmodelviewer -mapcatalogtest`) with `catalog/maps_<client>.json` and `catalog/wdt_<client>.json`, map by map
    (`docs/world-viewer/02-map-catalog.md`).
  - terrain mesh (stage 03): `terrain_reference.py` builds one root ADT tile as Unity mesh data by the design's pinned
    rules from the `wowfmt` reading (parser-independent, design-dependent) and reads/writes the terrain mesh dump format
    (`Tools/UnityRendererProject/Assets/Scripts/Wow/World/TerrainDump.cs`); `compare_terrain_mesh.py` compares a C#
    runner dump (`WMV_TERRAIN_DUMP_DIR`) or a player dump (`-wmvTerrainDump`) with it: positions within 1e-5, normals
    within 1e-6, indices, counts and bounds exactly, plus the player's layer, matrix and mesh format;
    `minimap_orientation.py` proves the orientation against the game's minimap tile (MAID word 7): mode `dump` fits it
    against a dump's cell normals, mode `capture` against a top-down `normals` capture, mode `render` writes the
    capture a dump implies -- the identity symmetry must explain the minimap at least twice as well as the other 7.
  - terrain materials (stage 04): `terrain_material_reference.py` is the independent CPU reference of the material
    design: per-chunk material states and layers from the `wowfmt` reading with its own alpha and BLP decoders (DXT1/3/5
    with bit replication, palettised, BGRA), the height-mix composite rule (the cutoff with the MHID height textures'
    alpha and the MTXP height values on MPHD 0x80 maps, default heights elsewhere), the composite of one chunk from
    given texels in validation sampling (V1), and the map-texture mode (each layer's and height texture's own stored
    mip, alpha at pixel centres, x MCCV) that reproduces the game's baked map texture (MAID word 5); mode `golden`
    prints the golden table, `converter` the offline 4-bit converter's byte test, `maptexture`/`chunkdump` write its
    own composites in the dump format (an id used as a diffuse and a height texture gets one texel-dump entry per
    role). An MTXP texture scale nibble n gives 8 / 2^n repeats per chunk, a float (0.5 at n = 4, real on Retail map
    2923), with the UV continuous across chunks (each chunk adds the phase frac(ix * repeats), frac(iy * repeats) to
    its layers' diffuse and height texture coordinates; 0 for whole repeats); chunks of up to 8 layers are drawn (both
    proven for the game's baked map texture on map 2923, inferred for draw time). States beyond the plain cases: a
    scale nibble above 4 (never observed; no repeats: the reference, like the C# compositor, draws such a layer as
    the checker where asked to and refuses to read its height texture; chunkdump refuses the chunk) and MTXP height
    values that let H = h*s + o fall below 0 or overflow float32 (MPHD 0x80 only) are LayerEffectUnresolved; a height
    texture whose BLP has no alpha channel fails for the height role only (HeightTextureMissing); a LayerEffectStatic
    chunk whose texture failed keeps its state but, like every chunk drawing the checker, is masked from the golden
    and V2 comparison. A texture with no stored level of one texel per pixel is sampled with a note, never silently. The
    height texture's repeats and channel are gated by the golden tests (8 repeats, 1 repeat and the red channel fit
    worse on 30_49's height chunks); its LOD (stored mip) is INFERRED: no fit separates one mip finer.
    The block decode comes in three named models: `exact` (the format), `hardware` (this GPU's interpolants,
    measured on the player's texel dumps: colour from the player self-test's model, confirmed on real DXT3/DXT5
    interpolants -- no real DXT1 interpolant exists -- and DXT5 alpha from per-selector weights that match every
    interpolated alpha of every real texel dump; a DXT1 three-colour midpoint has none) and `rounded` (the format
    rounded to 8 bits, the DXT1 midpoint's ties to even: the player's CPU decode of a block texture it uploads as
    RGBA32). `compare_terrain_material.py` compares the player's dumps with it: `v1` (chunk composites against the reference
    composed from the dump's own GPU texels, diffuse and height, 0.5/255; saturated production dumps against the
    clamped reference, with their clippedPixels; production sampling checked per (id, repeats) a layer binds), `v2`
    (the 512-px production composite against the verified map texture: 4 x 4-block RMSE threshold, identity symmetry,
    zero phase, and every kept pixel within 0.26/255 of the reference composed under the better-fitting named decode,
    named in the result -- V2's own error budget doubled: the float16 dump's truncating store, below 2^-11 =
    0.1245/255 in the compared range 0..1 (both sides clamped, 1.0 exact), plus the difference measured before the
    store, measured at least 0.0045/255 (a lower bound; source INFERRED), about 0.13/255; measured 0.1235-0.1259 on the nine golden
    tiles, while a player dividing MCCV by 127.25 measures 0.425 and fails; the documented 8-bit `<base>.png` input
    is a weaker check, 0.76/255 (its 0.5/255 rounding on top) and no coverage or range check, the limit named in the
    check; `--reference-tolerance` replaces either limit), `texels` (GPU texels against
    the decode: textures whose BLP is not block-compressed and block endpoints exact; uploaded as blocks, every
    four-colour interpolant and every interpolated DXT5 alpha equal to one named decode per texture -- hardware model
    or exact ramp -- named, and DXT1 three-colour midpoints between their endpoints, counted as unmodelled; uploaded
    as R8G8B8A8_UNorm, every interpolated value equal to the rounded decode); every input is verified against its pin
    or manifest and the pinned client build, and the dumps' build and md5 fields against those files. The dump
    formats are documented at the top of `compare_terrain_material.py`. Inputs beyond the pinned corpus are in `materials/<client>/stage04_reference`,
    `stage04_golden` and `stage04_nibble4` (extracted read-only, `manifest_sha256.tsv` with the build).
- `tests/` -- `python -I -m unittest discover -s Tools/world-audit/tests`:
  - `test_wowfmt.py`: the parsers on malformed and synthetic input, and the pinned hashes;
  - `test_corpus_rules.py`: the report's rules asserted on the extracted bytes, each against its competing readings;
  - `test_terrain_tools.py`: the terrain reference, the dump format, the comparison (with planted mutations), PNG
    reading and the minimap fit on synthetic tiles; on the corpus the pinned tiles' counts and ids, and the
    orientation of 32_50 and 34_46.
  - `test_terrain_material.py`: the BLP decoders (all three named models) and the alpha decoder, every material
    state and the precedence, the height mix and its value domain, the composition rules (nibble 4 at 0.5 repeats
    with tile-continuous UV, 6..8 layers) and the comparison (with planted mutations, both-role ids, ids bound at two
    repeats, an alpha-less height texture, the named-decode texel check by upload format, the V2 gate at 0.26/255
    and at 0.76/255 for a float16-like composite's 8-bit PNG, a crafted scale nibble 5 drawn as the checker with its
    height texture and its V1 dump refused, and unverified inputs) on synthetic tiles; on the corpus the pinned chunk
    states and alpha hashes, the golden tests against the map textures of nine tiles (the height mix within its
    measured own-mip RMSE + 0.25; plain weights, sharpening controls, sequential, linear, no-MCCV, default heights
    where height textures exist, heights at 8 or 1 repeats and the height texture's red channel, the full formula on
    an MPHD 0x80-clear map, shifted or i/63 alpha, other symmetries and phases all worse; on Retail map 2923 32_24
    the nibble-4 layers chunk-local, at 1, 8 or 0.25 repeats worse too) and the converter byte test (about 200 s).

  The data-dependent tests run when `WMV_WORLD_AUDIT_CORPUS` names the extracted data folder (here
  `F:/wowmodelviewer/build/world-audit-data`, git-ignored); otherwise they are skipped with a message. With
  `WMV_TERRAIN_DUMP_DIR` also set to the folder the C# runner dumped into, the runner's dumps of the pinned tiles are
  compared with the reference too.

The full reproduce sequence is in section 10 of the report. Python 3.10+, standard library only. Run scripts with
`python -I`.
