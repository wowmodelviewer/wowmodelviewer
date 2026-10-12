# World viewer 02: the client-aware map catalogue

Branch `world/02-map-catalog`, from `develop` at `bedc74cc` (2026-10-11, after `world/01-parsers`, PR #102). The work
is **map discovery, WDT resolution and the runtime contract only**. There is no Maps viewer mode, no map-selection UI,
no terrain, no ADT streaming, no global-WMO rendering, no IPC message and no protocol change. The audit
(`docs/world-viewer/00-audit.md`) and the parsers (`docs/world-viewer/01-parsers.md`) are the specification.

**Push status at delivery: NOT PUSHED.**

## 0. Result in brief

Every figure below comes from the final binaries (`wowmodelviewer.exe` SHA-256 `a789e2d7...`, `wow.dll` `bf4a3ea7...`,
`core.dll` `667ca7ee...`; each test JSON records the three hashes it was produced by), in one final pass of every test
(`F:\wmv-build-fix\mapcat\run_all.ps1 -Tag g`; test rigs outside the repository). They include the follow-up of section 20
(section-pointer safety and content-key verification); every figure is the same as before it.

| Check | Result |
|---|---|
| Catalogue on the four installed clients (headless viewer runs, hidden desktop) | **PASS**: Retail 1,184 maps, Classic Beta 71 (+8 in encrypted sections, counted), Classic Era 59, MoP Classic 285 |
| WDT references by each client's own mechanism | **PASS**: Retail/Beta by Map.db2 FileDataID (1,079 / 71; 105 Retail rows name none), Era/MoP by `world/maps/<Dir>/<Dir>.wdt` through the storage root's own names (59 / 285) |
| Against the audit's independent decode of the same bytes (`Tools/world-audit/compare_map_catalog.py`) | **PASS**: 104,333 checks over 10 client steps in 4 runs, 0 differences; 19 of 19 planted errors caught |
| The same without the listfile's map paths (383,761 `world/maps/` lines removed) | **PASS**: identical catalogues, 44,125 checks, 0 differences |
| Every readable WDT read through the catalogue | **PASS**: byte-identical (SHA-256) to the audit's copy, every one checked against its content key |
| Client switching: Retail -> MoP -> Retail, Retail -> Beta -> Retail, Era -> Retail, Retail -> Retail | **PASS**: 44/44, 44/44, 27/27, 27/27 viewer checks; every earlier catalogue refuses to read after each switch |
| Error handling (section 15) | **PASS**: each case reported with its reason, no crash, no fallback to another client |
| Runtime contract at the data level (catalogue -> WDT bytes -> verified C# parser -> tile parts -> storage) | **PASS**: 36 WDTs of 4 clients, SHA-256 and stated form checked, parsed with 0 failures; 497 parts resolved; the audit's known gaps reproduced (section 14) |
| Database cache: reuse, tampered states, a newly known key, old schema, changed definition | **PASS**: 8 of 8 cases as designed (section 12) |
| Regression against unchanged `develop` (customization x4 clients, Models and Buildings through the Unity player, a texture decode, the clients' capability lines, the C# parser tests) | **PASS**: identical results, identical PNG (section 16) |
| Performance | catalogue about 0.7-2.4 ms per load (a process's first Retail query up to 5.9 ms, once 18.7 ms on a cold database file), one SQL query, no file opened; Retail retains ~333 KB |
| Adversarial review (ten claims, 55 agents) | all ten **WEAKENED**, none refuted; every finding fixed or recorded (section 17) |

Clients (unchanged since the audit; `E:\World of Warcraft\.build.info`, 2026-10-12): Retail 12.1.0.69933, Classic Beta
1.60.1.70338, Classic Era 1.15.9.70003, MoP Classic 5.5.4.70268. Map.db2 is byte-identical to the audit's pinned copy in
all four.

## 1. Files

| File | What |
|---|---|
| `Source/games/wow/WorldMapCatalog.{h,cpp}` (new) | The catalogue: client identity, Map.db2 rows, WDT references, availability, JSON for the tests |
| `bin_support/dbd/Map.dbd` (new) | Map.db2's three layouts, every column in file order, named from evidence (section 4) |
| `bin_support/wow/12.0/database.xml` | The `Map` table, `verifiedLayoutOnly="yes"` |
| `Source/games/wow/dbdfile.{h,cpp}` | `getStructureByLayout`: a definition by exact layout hash only |
| `Source/games/wow/WoWDatabase.{h,cpp}` | Per-table readings in the schema check (state, layout, absent columns, rows, encrypted rows and their keys, the client), kept with the cache and checked when it is restored; `verifiedLayoutOnly`; `fileLayoutHash` |
| `Source/core/GameDatabase.{h,cpp}`, `Source/core/dbfile.h` | Rows read and rows in encrypted sections per table; `clientDatabaseStarting`, `tableBuilt`; `SCHEMA_VERSION` 18 |
| `Source/games/wow/wdc3file.{h,cpp}` | Records in encrypted sections counted with their key names; **text fields of records in sections after the first are now read** (they read as empty before, section 17) |
| `Source/games/wow/CASCFolder.{h,cpp}`, `WoWFolder.h` | The storage root's entry for a file (by FileDataID or by path), the root's named and nameless counts, a whole-file read with its outcome (the frame hashes the data carries and the content key checked; no key, no read: section 20), the storage generation, `knowsEncryptionKey` |
| `ThirdParty/casclib/src/{CascLib.h, CascRootFile_WoW.cpp, common/RootHandler.{h,cpp}, common/FileTree.h}` | `CascWowFindRootEntry`, `CascWowRootNameCount`; CascLib's own storage files kept out of root lookups. Marked `WMV:` like the existing patches |
| `Source/wowmodelviewer/modelviewer.{h,cpp}` | The catalogue built in the load (stage "map catalogue"), replaced in `ResetClientState` |
| `Source/wowmodelviewer/app.cpp` | `-mapcatalogtest`: the headless catalogue test |
| `Tools/UnityRendererProject/Tests/MapContractCheck.cs` (new), `Tests/Runner/*`, `Tests/README.md` | The contract test's C# step |
| `Tools/world-audit/compare_map_catalog.py` (new), `Tools/world-audit/README.md` | Catalogue vs the audit's results |
| `docs/world-viewer/02-map-catalog.md` | This report |

## 2. Architecture

```
 Map.db2 (the client's own)          dbd/Map.dbd: every column of each verified layout
        |                                   |
        v                                   v
 database.xml <table name="Map" verifiedLayoutOnly="yes">  --refreshStructures-->  per-table reading
        |  (existing DB2 reader, existing schema check, existing dbcache)           (state, layout, absent
        v                                                                             columns, rows, encrypted
 SQLite table Map ---- one query ----> wow::WorldMapCatalog::build()                 rows + keys, the client)
                                              ^           <---- client identity, storage generation, root counts
                                              |
                     MapEntry: id, directory, name, classification codes, WdtReference
                                              |   (no file opened)
                         inspect() / readWdt() / inspectFile(), on request
                                              |
              CASCFolder::rootEntry / rootEntryByPath   (CascWowFindRootEntry: listed? local? its FileDataID)
              CASCFolder::readWholeFile                  (read / not listed / not local / encrypted / corrupt / ...)
                                              |
                       WDT bytes  ----(Stage 03+: existing asset path)---->  C# WdtParser (01-parsers)
```

- **Owner.** `ModelViewer::m_mapCatalog` (`modelviewer.h:754-755`). The viewer always holds one: "no client is loaded"
  from construction (`modelviewer.cpp:270`), "a client is being loaded" from the start of a client change
  (`ResetClientState`, `modelviewer.cpp:4127`), the loaded client's after the database, Browse and the character
  controls (`LoadWoW`, stage "map catalogue", `modelviewer.cpp:4459-4462`). A legacy MPQ load builds an `Unsupported`
  one (`modelviewer.cpp:4003`).
- **Identity.** A catalogue records the client's product, build, locale, data path and schema, the storage generation,
  and its storage root's named and nameless counts. The generation (`CASCFolder.cpp:30, 122, 177, 322`) comes from a
  process-wide counter: every folder object gets its own at construction and a new one whenever a storage opens or
  closes. `belongsToActiveClient()` (`WorldMapCatalog.cpp:264`) compares generation, product, build and data path.
  Every read (`inspect`, `inspectAll`, `readWdt`, `inspectFile`) goes through `canRead()` (`WorldMapCatalog.cpp:301`):
  the catalogue must be `Ready` and still the loaded client's, else it refuses and says why.
- **No second database system.** Map.db2 goes through the existing table declaration, DB2 reader, definition check and
  cache. The catalogue is one `SELECT` (`WorldMapCatalog.cpp:177`).
- **No second WDT parser.** No C++ code reads a WDT's chunks. The catalogue reads WDT *bytes* (to prove they can be
  read), checks the first four bytes are the MVER chunk tag (`WorldMapCatalog.cpp:377`; a wrong column would name
  another kind of file) and hands them on. Their contents are the C# parser's (section 14).

## 3. Source evidence: the existing infrastructure, and what this branch adds

**Audited existing code (develop `bedc74cc`).**

| Concern | Where | What it does / what this branch uses |
|---|---|---|
| Loading the selected client | `modelviewer.cpp` `LoadWoW` (from `:4124`) | Folder reuse or a fresh one, `setConfig` opens the storage into a new handle (`CASCFolder.cpp:93-127`), teardown is the atomic switch, then schema, file index, database. The catalogue is one more stage after the characters |
| Selecting the schema/profile | `ClientInstallations.cpp:267-302` (`resolveSchema`), `ClientProfile.cpp:34-48, 81-105` | All four clients use `games/wow/12.0/` |
| Reading Map.db2 | `WoWDatabase.cpp` (`createDBFile`), `wdc3file.cpp` | `DBFilesClient\Map.db2` through the listfile name (as every table), WDC5 read by `WDC3File` |
| Validating the layout | `WoWDatabase.cpp:146-310` (`refreshStructures`) | A definition places the columns by name. This branch adds `verifiedLayoutOnly` (`:216, 224, 237`): such a table takes only the definition of its file's own layout hash, never a BUILD match, never the schema's positions or its `layoutHash` |
| DB rows from C++ | `GameDatabase.cpp` (`sqlQuery`) | One `SELECT` per catalogue |
| Resolving a FileDataID | `WoWFolder.cpp` (`getFile(int)`) | Not used by the catalogue: it adds file objects to the folder. The catalogue asks the storage root directly (`CASCFolder.cpp:185`) |
| Resolving a named file | `WoWFolder.cpp` (listfile -> FileDataID) | Not used for WDTs: the Era/MoP rule goes through the root's own name hashes (`CASCFolder.cpp:199`) |
| Local availability | `CASCFolder.cpp` (one enumeration into present/remote bits) | The catalogue reads the same `CASC_CE_FILE_IS_LOCAL` flag per file, through `CascWowFindRootEntry` (`CascRootFile_WoW.cpp:698`) |
| Invalidation on a client switch | `modelviewer.cpp` (`ResetClientState`), `UnityAssetAccess::noteClientReplaced` | The catalogue is replaced there; the storage generation catches any kept copy |
| The database cache | `GameDatabase.cpp:186-381` (key, reuse, build, publish) | Unchanged mechanism; the Map table and the readings live in it (section 12) |

**Added by this branch** (beyond the catalogue itself): `GameDatabase.cpp:253` (`clientDatabaseStarting` first thing
in `initFromXML`), `:499` (`tableBuilt` per table); `WoWDatabase.cpp:134` (`fileLayoutHash`), `:411-421` (rows,
locked rows and key names per table), `:426-530` (the readings saved and restored, with the checks of section 12);
`wdc3file.cpp:520-534` (sections under a missing key counted with the key's name), `:767` (text across sections);
`CASCFolder.cpp:217-315` (`readWholeFile`), `:317` (`knowsEncryptionKey`); `RootHandler.cpp:53-56, 152` (CascLib's own
storage files flagged and excluded).

Legacy MPQ clients have no database; their catalogue is `Unsupported`.

## 4. Map.db2 field mapping per build

No definition of Map.db2 existed in the repository or on this computer, and the audit had proven three columns
(00-audit.md 3.1). The audit's risk R4 allows definitions from outside only with the owner's consent, so none was
fetched. Every column used here was established from the clients' own bytes, by five independent lines of evidence,
and each claim was re-derived by a separate skeptic who tried to refute it. Where a skeptic weakened a claim, the
weaker verdict is the one given here. Scripts and outputs: `F:\wowmodelviewer\build\world-audit-data\mapcols\`
(git-ignored, like the rest of the audit data). Evidence tables: Map, Difficulty, MapDifficulty, LFGDungeons,
JournalInstance, AreaTable, LoadingScreens, the WDT census, 1,160 root ADTs, the zone-map and preload files.

**Layouts.** Era and MoP share one layout hash, but they are not the same build.

| Client | Build | Layout | Fields | Rows | Sections |
|---|---|---|---|---|---|
| Retail | 12.1.0.69933 | 75863E23 | 25 | 1,184 | 1 |
| Classic Beta | 1.60.1.70338 | D43AFAC3 | 26 | 79 (71 readable + 8 in sections under keys this computer lacks) | 4 |
| Classic Era | 1.15.9.70003 | B47C6F05 | 23 | 59 | 1 |
| MoP Classic | 5.5.4.70268 | B47C6F05 | 23 | 285 | 1 |

**Column order is preserved across layouts** (every Map ID shared by two clients has the same Directory, in all six
client pairs: 36 to 271 shared rows per pair). Retail and Beta have a float pair at 6 and WdtFileDataID at 21; Beta has
one more column after WdtFileDataID. Era/MoP have neither the float pair nor WdtFileDataID nor Retail's last code
column (Retail's 25 - 3), and one column of their own at 16 (+ 1 = 23).

**The columns the catalogue reads**, positions per layout (`dbd/Map.dbd`):

| Column (database.xml) | Retail 75863E23 | Beta D43AFAC3 | Era/MoP B47C6F05 | Meaning, and how far it is established |
|---|---|---|---|---|
| `ID` | id list | id list | id list | The map's identity. **Proven** (audit) |
| `Directory` | 0 | 0 | 0 | The WDT's folder. **Proven** (audit 3.1) |
| `MapName_lang` | 1 | 1 | 1 | The map's name as the client stores it. **Supported** in every client (Retail: the Adventure Guide's name for the same map on 181/206 maps; MoP 62/82; Era and Beta through names shared with Retail/MoP and area names). It is **not** always the exact label the game shows: that stronger claim was refuted |
| `MapType` | 7 | 7 | 6 | The form of the map's WDT. On every readable WDT of all four clients: 1 = a tile grid with no global world model (Retail 741, of which 13 list no tile; Beta 48, Era 37, MoP 194); 2 = a world model only, named by FileDataID (Retail 35, Beta 21, Era 21, MoP 33); 4 = a world model only, also named by an MWMO path (Retail 193); 3 = no WDT the client can read (Retail: the 105 rows with WdtFileDataID 0 and 7 whose WDT the root does not list; MoP: 51 rows not in the root by path). The comparator checks this on every map (section 9) and the contract test against the C# parser (section 14). Other meanings once proposed ("instanced", "transport") are **not** established |
| `InstanceType` | 8 | 8 | 7 | The code Difficulty.db2 gives the map's difficulties. **Supported**: it equals Difficulty's code on all 1,113 joined MapDifficulty rows in Retail, and on Beta 104 of 107, Era 91 of 96, MoP 294 of 297. The exceptions are InstanceType-0 maps with a "Normal" difficulty of code 1 (maps 25, 35, 42; also 37 and 451 in Era; 35 and 369 in MoP) and MoP's 309 (code 1, one raid-size row of code 2). Codes 1, 2 and 5 are the ones the join exercises; the difficulty names that co-occur (1 normal/heroic dungeon difficulties, 2 raid sizes and raid finder, 5 scenario) are evidence, not an implemented taxonomy. 0 is mostly, not only, maps whose difficulties are all difficulty 0 (Retail 579 of 589); 3, 4, 6, 7, 8 are not labelled |
| `ParentMapID` | 13 | 13 | 12 | When set, a map whose world space this map shares (same tile coordinates and surface). **Supported** (Retail and MoP tile and surface tests; Beta and Era 1-4 rows). -1 is "not set", which is **not** "no parent" |
| `WorldCopyOfMapID` | 14 | absent | 13 | When set, a map part of whose world this map copies (same tiles and surface, zones of its own). **Supported** in Retail, Era and MoP. In Beta the column at this position is a 2-bit field whose meaning is **unknown**, so the catalogue reads it as absent there (`BetaCode14`) |
| `LocationMapID` | 17 | 17 | 17 | When set, the map an instance is located on: the row's own (X, Y) point lies on it. **Proven** in Classic Beta (28/28 points inside a world model on the named continent, 1/28 on the other); **supported** in Retail and MoP (equal on 93/93 shared maps) and Era. -1 is "not set"; **Classic Era and Beta also store 0 on rows with no point**, which reads like map 0 (Eastern Kingdoms) |
| `WdtFileDataID` | 21 | 21 | absent | The WDT's FileDataID. **Proven** (audit 3.1); **absent** from the Era/MoP layout, which is what selects the directory rule (section 6) |

Every other column of each layout is listed in `Map.dbd` under a neutral name (`Code9`, `Text2_lang`, `BetaCode23`,
...) or, where established, under its role (`AreaTableID`, `LoadingScreenID`, `ZoneMapFileDataID`, `Flags`). A neutral
name two layouts share means the same slot, not an established equal meaning; slots whose alignment the skeptics could
not confirm got layout-specific names (`BetaCode23`, `BetaCode24`, `EraMoPCode21`). The catalogue reads none of them.

**Pallet columns.** Pallet-stored 16-bit columns keep a constant upper half (0x0001 in Era/MoP) and store -1 as 65535.
The schema declares the map references `int16`, so the reader yields the low 16 bits, signed. The comparison checks
every value against the audit's raw decode (section 9).

**Not by name.** Nothing in this branch reads a map's kind from its directory or name: the form is the Map.db2 column,
checked against the WDT bytes; there is no "dungeon"/"raid" grouping. Grouping for a UI is left to the Maps viewer.

## 5. Catalogue data model (`WorldMapCatalog.h`)

| Type | Fields |
|---|---|
| `MapCatalogClient` | product, build, locale, dataPath, schema, storageGeneration, rootNames, rootNameless |
| `MapEntry` | `id`, `directory`, `name`; classification codes `mapType`, `instanceType`, `parentMapId`, `worldCopyOfMapId`, `locationMapId` (each a `MapCode {present, value}`: a column the layout lacks is *absent*, never 0); `wdt` (`WdtReference`); `inspection` (`WdtInspection`); `statedWdtForm()` (`Unknown` / `TileGrid` / `WmoOnly` / `NoWdt`) |
| `WdtReference` | `kind` (`Unresolved` / `FileDataId` / `Path`), `fileDataId`, `path`, `why` |
| `WdtInspection` | `state` (`WdtAvailability`), `detail`, `storageFileDataId` (-1 when the root does not list it), `bytes`, `sha256` (only with `setHashing(true)`), `milliseconds` |
| `WorldMapCatalog` | `status` (`NoClient` / `Unsupported` / `MapTableUnavailable` / `Ready`) + `statusDetail`; `client()`; `belongsToActiveClient()`; `wdtRule()` (`ByFileDataId` / `ByDirectoryPath`); Map.db2's layout, absent columns, rows and encrypted rows; `maps()`, `find()`; `inspect()`, `inspectAll()`, `readWdt()`, `inspectFile()`, `lastRefusal()`, `setHashing()`; `toJson()` |

A failure is never a FileDataID of 0: an unresolved reference says why (with the raw value), an unread table says why
(status detail), and an availability state other than Available says why (inspection detail).

## 6. WDT reference types

- **By FileDataID** (Retail, Classic Beta). Chosen because the client's layout *has* the `WdtFileDataID` column, as the
  schema check records for that layout (its absent columns), never because of the product. A value of 0 (or anything not
  a positive number) gives `Unresolved` with "Map.db2 names no WDT for this map (WdtFileDataID 0)" (Retail: 105 maps, all
  MapType 3). The FileDataID is looked up in the storage root, not in the listfile.
- **By path** (Classic Era, MoP Classic). The layout has no such column, so the audit's verified rule applies:
  `world/maps/<Directory>/<Directory>.wdt`. The path is looked up by the **storage root's own name hashes**
  (`CascWowFindRootEntry` by name), not by the listfile: removing every `world/maps/` line from the listfile leaves the
  catalogue identical. The root's FileDataID for the path is recorded as `storageFileDataId` once the map is inspected
  (it is the client's own number, never a manufactured one); it equals the audit's same-bytes FileDataID for every
  available path map (285: MoP 227 + Era 58).
- **Unresolved**: no reference, with the reason. Never guessed.

**What a path lookup can say depends on the root.** The root of each client, counted when its storage opens:

| Client | Files the root names | Files it lists by FileDataID only | A name it does not know means |
|---|---|---|---|
| Retail | 149,147 | 1,776,676 | nothing (the file may be one of the nameless) |
| Classic Beta | 140,840 | 1,101,423 | nothing |
| Classic Era | 157,020 | 0 | the file is not in the client |
| MoP Classic | 418,311 | 0 | the file is not in the client |

So a path the root has no name for is `NotInClientStorage` only in a root that names every file (Era, MoP), and
`Unsupported` ("cannot be told by path") in Retail and Beta. Retail and Beta never use the path rule for their maps; the
distinction matters for probes and for Retail's old-form global world models (section 13). CascLib's own storage files
(ENCODING, ROOT, INSTALL, ...) are neither names nor entries of the root for these lookups.

## 7. Availability states

| State | Decided by | Real example | Shown on real data? |
|---|---|---|---|
| NotChecked | nothing inspected yet (inspection is lazy); also a read refused, or no storage open | every map with a reference right after a load (checked: "loading inspected no WDT") | yes |
| Available | the root lists it, its data is local, every byte read, each frame hash the data carries and the whole content's MD5 (its content key) checked; without a content key nothing is Available | Retail Azeroth 775971, 294,988 bytes | yes |
| NotInClientStorage | the root does not list the FileDataID, or (Era/MoP) has no such name | Retail map 1534's WDT 2449139; MoP's 51 MapType-3 rows by path | yes |
| NotInstalledLocally | the root lists it, the local index has no copy (`CASC_CE_FILE_IS_LOCAL` clear) | Beta Kalimdor tile 44_24 root ADT 787450; a Retail shader 2976775 | yes (no WDT is missing locally in these four clients) |
| Unreadable | local, but: encrypted with a key this computer lacks / larger than the read allows (the detail names the size and the limit: 64 MiB for a WDT, lower where the caller sets one) / damaged (checksums do not match) / its content key could not be obtained (the bytes could not be checked) / data missing although indexed / an empty read / not a WDT (no MVER tag) | Beta Map.db2 1349477 and Retail 546628 (encrypted); Retail 1467245 (178 MB, too large) | encrypted and too large yes; the others are code paths only (no such file found on this computer) |
| Unsupported | no WDT reference; not a valid reference; a path the root cannot answer for | Retail's 105 maps with WdtFileDataID 0; `world/maps/azeroth/azeroth.wdt` asked of Retail | yes |

Map.db2 rows never disappear because their WDT is unavailable: every readable row is listed, whatever its state. Rows
in sections encrypted with keys this computer lacks cannot be read at all (their IDs are encrypted too): Beta's 8 are
*counted* (`lockedRows`, the status detail says so) together with the keys they wait for, not invented. Encrypted data
is refused, never read as zeros (CascLib's option to zero-fill missing keys is not used).

## 8. Sample map catalogue (representative maps, real IDs)

| Role | Client | Map ID | Name | Directory | WDT reference | Availability | Resolution |
|---|---|---|---|---|---|---|---|
| Large continent | Retail | 0 | Eastern Kingdoms | Azeroth | FileDataID 775971 | available | 294,988 B; parses TiledTerrain, 1,176 tiles |
| Large continent, newest | Retail | 2444 | Dragon Isles | 2444 | FileDataID 3694921 | available | TiledTerrain, 1,634 tiles |
| Ordinary small map | Retail | 33 | Shadowfang Keep | Shadowfang | FileDataID 790796 | available | TiledTerrain, 25 tiles |
| Global WMO (modern) | Retail | 34 | Stormwind Stockade | StormwindJail | FileDataID 791060 | available | MapType 2; GlobalWmo, WMO FileDataID 108631 (available) |
| Global WMO (old form) | Retail | 1212 | Garrison - Alliance - Blacksmith 3 | 6HU_GARRISON_Blacksmithing_hub | FileDataID 952203 | available | MapType 4; GlobalWmo, WMO named by MWMO path: Retail's root has no name for it ("cannot be told by path", section 18) |
| Empty WDT | Retail | 1232 | GOR Horde Garrison Level 0 | GorHordeGarrisonLevel0 | FileDataID 953470 | available | MapType 1, parses Empty (no tile); ParentMapID 1116 |
| Partly unavailable | Classic Beta | 1 | Kalimdor | Kalimdor | FileDataID 782779 | available | 988 tiles; tile 44_24 root 787450 and 37_19 minimap 208191 not installed locally |
| Partly unavailable | Retail | 723 | Stormwind | Stormwind | FileDataID 790924 | available | 25 tiles; the sampled tiles' minimaps not in client storage |
| Classic Era | Classic Era | 451 | Development Land | development | path world/maps/development/development.wdt | available | storage FileDataID 857684; 82 tiles; 3 sampled minimaps not in client storage |
| Classic Era, global WMO | Classic Era | 429 | Dire Maul | DireMaul | path world/maps/DireMaul/DireMaul.wdt | available | storage FileDataID 780814; GlobalWmo 109280; LocationMapID 1 |
| MoP Classic | MoP Classic | 870 | Pandaria | HawaiiMainLand | path world/maps/HawaiiMainLand/HawaiiMainLand.wdt | available | storage FileDataID 805681; 684 tiles |
| Classic Beta | Classic Beta | 34 | Stormwind Stockade | StormwindJail | FileDataID 791060 | available | GlobalWmo 108631 |

## 9. Real WDT resolution results

| Client | Maps | References | Available | Not in client storage | Unsupported | Encrypted rows | Audit (T1, 00-audit.md 3.1) |
|---|---|---|---|---|---|---|---|
| Retail | 1,184 | 1,079 FileDataID, 105 unresolved | 969 | 110 | 105 | 0 | 969 OK, 110 NOT_IN_ROOT, 105 without a WDT id |
| Classic Beta | 71 | 71 FileDataID | 69 | 2 (13 test, 3109) | 0 | 8 | 69 OK, 2 NOT_IN_ROOT, 8 key-locked |
| Classic Era | 59 | 59 path | 58 | 1 (13 test) | 0 | 0 | 58 OK, 1 NOT_IN_ROOT |
| MoP Classic | 285 | 285 path | 227 | 58 (7 + the 51 MapType-3 rows) | 0 | 0 | 227 OK, 7 NOT_IN_ROOT, 51 without a listfile id (all NOT_IN_ROOT by path) |

Era's and MoP's misses are conclusive (their roots name every file, section 6).

`compare_map_catalog.py` checks, against `catalog/maps_<client>.json` and `catalog/wdt_<client>.json`:
- every map ID, its directory and name;
- the reference (Map.db2's FileDataID, or the path rule);
- every classification code against the raw value at the layout's position, and a column the layout lacks as absent;
- the state against the audit's status, with the root's name counts deciding what a path miss must be;
- size and SHA-256 for every available WDT;
- the root's FileDataID for path maps, and none (-1) for a file the root does not list;
- the stated WDT form against the WDT read (tile grid, world model only, MWMO path for MapType 4; never "available" for
  MapType 3);
- the probe files against this computer's known states (`PROBE_EXPECTED`);
- the steps of a run against each other (the same client twice gives the same catalogue; no two steps share a storage).

Final run: **104,333 checks, 0 differences** over 10 client steps in 4 runs. The other final outputs compare clean too:
the listfile-free runs (44,125 checks), the decoy-definition run (14,930), and the contract, probe and cache runs
(67,152). Planting 19 kinds of
error caught 19 of 19, among them a leaked map, two steps sharing a storage, a changed SHA-256, a wrong rule, a wrong
stated form, a wrong root count and a wrong probe state.

Probe files, inspected through each client's storage (the same in every run; Beta and Era rows abridged):

| Probe | Retail | MoP Classic | Classic Era | Classic Beta |
|---|---|---|---|---|
| 775971 (Azeroth's WDT) | available, 294,988 B | available, 163,908 B | available | available |
| 787450 (Beta Kalimdor 44_24 root ADT) | available | available | available | not installed locally |
| 1349477 (Map.db2) | available | available | available | unreadable: encrypted |
| 546628 | unreadable: encrypted | available | available | available |
| 921760 (Mount.db2) | unreadable: encrypted | unreadable: encrypted | available | unreadable: encrypted |
| 1467245 | unreadable: too large | not in client storage | not in client storage | not in client storage |
| 2976775 (a shader) | not installed locally | not installed locally | not installed locally | not installed locally |
| 8345027, `INSTALL` (CascLib's own) | not in client storage; unsupported | not in client storage (both) | not in client storage (both) | not in client storage; unsupported |
| 0, -5 | unsupported | unsupported | unsupported | unsupported |
| `world/maps/azeroth/azeroth.wdt` | unsupported (no name) | available (775971) | available (775971) | unsupported (no name) |
| `WORLD\MAPS\AZEROTH\AZEROTH.WDT` | unsupported | available | available | unsupported |

## 10. Client-switch tests

One viewer process per sequence (`-mapcatalogtest <out> -mapcatalogclients <list>`), each switch a real `LoadWoW` of
the next product of the installation, as the chooser makes it:

| Sequence | Viewer checks | Notes |
|---|---|---|
| Retail -> MoP Classic -> Retail | 44 / 44 | The two Retail catalogues are identical (compare: no difference apart from timings and the storage number) |
| Retail -> Classic Beta -> Retail | 44 / 44 | Beta's 8 encrypted rows counted |
| Classic Era -> Retail | 27 / 27 | |
| Retail -> Retail | 27 / 27 | The same product loaded again: only the storage number tells the catalogues apart, and it does |

At every step:
- the catalogue is the loaded client's (product, build, a new storage number), its Map.db2 layout is the one in the
  client's own file header (read again by the test), and it is `Ready`;
- every map reference (ParentMapID, WorldCopyOfMapID, LocationMapID) is -1 or a map of the same catalogue;
- loading inspected no WDT (every map with a reference is NotChecked, every one without is Unsupported);
- **every earlier catalogue** of the run, kept by the test past the switches, reports it no longer belongs to the loaded
  client, refuses `inspect`, `inspectAll`, `readWdt` and `inspectFile`, and its entries stay unchanged;
- no ID, path, reference or state of one client appears in another's (the comparator checks each step against its own
  client's audit data).

The catalogue holds no file objects and adds nothing to the folder's file index, so it adds no long-lived index. A
client's database that fails to open leaves no stale reading: the schema check is cleared first thing
(`clientDatabaseStarting`) and carries its client's build and product, which the catalogue compares
(`WorldMapCatalog.cpp:114`).

## 11. Performance

Measured in the final runs (Release build, hidden desktop, the user's machine in normal use; switch, listfile-free and
contract runs):

| Client | "map catalogue" stage | Query | Build | Retained at load (approx.) | Inspect all WDTs, no hash | ... with SHA-256 (tests) |
|---|---|---|---|---|---|---|
| Retail (1,184 maps) | 1.8-2.4 ms; a process's first load 5.9 ms, once 18.7 ms | 1.3-1.6 ms; first 5.3 ms, once 18.2 ms | 1.6-1.9 ms | 333 KB | 0.34-0.35 s | 0.85-0.87 s |
| MoP (285) | 0.98-1.12 ms | 0.82 ms | 0.92 ms | 107 KB | 0.10 s | 0.24 s |
| Beta (71) | 0.69-0.91 ms | 0.64 ms | 0.66 ms | 17 KB | 0.03 s | 0.08 s |
| Era (59) | 0.73-0.79 ms | 0.70 ms | 0.74 ms | 20 KB | 0.02 s | 0.06 s |

The slow first loads are the SQL query reading a database file the system has not cached yet (the run before had just
rebuilt it); the catalogue's own work is the rest of "build" minus "query", under 0.4 ms.

- **No WDT is opened during a load.** References are resolved in memory; the test checks that the load left every map
  uninspected. Availability is inspected per map on request, or all at once by the test.
- **The progress display is not held up**: the stage is one synchronous query of a few milliseconds between Browse and
  the end of the load (`modelviewer.cpp:4459`); every load measured here was headless, so the GUI progress path itself was not
  timed.
- **Retained** is the catalogue's own estimate (`approximateBytes`): the structures plus the storage of every non-empty
  string. After a full inspection with hashing Retail holds ~492 KB (the reasons and hashes).
- **The Map table** adds about 9 ms to a Retail database build of about 14-16 s. Clients rebuild their cache once,
  because the schema version and the definitions changed (section 12).
- **Inspection cost.** WDTs are 32-295 KB. Inspecting one (root lookup, read, frame hashes, content-key MD5) costs about
  0.3 ms on average in Retail; hashing it with SHA-256 for the tests more than doubles that, so the catalogue hashes
  only when asked (`setHashing`).

## 12. Cache behaviour

The Map table lives in the existing per-client database cache. Its key (`GameDatabase.cpp:186-203`) holds:
- build and product;
- the schema folder;
- `SCHEMA_VERSION` (now 18);
- the locale and the format;
- a hash of the schema folder's files and of every `dbd/*.dbd`, `Map.dbd` included.

The per-table readings (and the client they are of) are kept with the cache and restored with it. Restoring now refuses
a state with no readings, readings that do not add up to the kept counts, or an unknown state name; and it rebuilds the
database when a key recorded as missing for some table's encrypted rows is now known to the storage (the key list is
not in the cache key; it is refreshed while the viewer runs). No new persistent cache was added: the catalogue is
rebuilt from the database at every load in about 2 ms.

| Test (copy of the runtime, Retail) | Result |
|---|---|
| warm: the client's own cache | reused; the Map reading restored ("verified", layout 75863E23, 1,184 rows); catalogue ready |
| tampered: the kept Map reading's state changed to a name no build writes | "not used: its schema check could not be read - rebuilding from DB2"; rebuilt; catalogue ready |
| counts: the Map reading set to another valid state, so the readings no longer add up to the kept counts | not used, rebuilt |
| notables: the per-table readings removed | not used, rebuilt |
| unknownkey: the Map reading records rows under a key no list has | reused (nothing new is readable) |
| knownkey: the Map reading records rows under a key this computer has (the first of `extraEncryptionKeys.csv`) | "Map has rows under key 0001C772CCC21F2D which this computer now has: the cached database is built again"; rebuilt |
| oldcache: develop's schema-17 Retail cache beside it | never selected: another key, another file name; the schema-18 cache is used |
| mapdbd: `Map.dbd` changed (one comment line) | a new key (the definitions' hash), a new build under a new file name |

Every case ended with the catalogue ready (13/13 viewer checks each).

A client update changes the build, which is in the key. A client whose Map.db2 is not on this computer yet leaves the
cache unstamped (the existing rule), so it is built again once the file arrives (not exercised: no such client here;
section 18).

## 13. Global-WMO support model

- **Before the WDT is read.** `MapType` states the form (`MapEntry::statedWdtForm()`): 1 a tile grid, 2 and 4 a world
  model only, 3 no readable WDT. It holds for every readable WDT, but it is still Map.db2's statement; the 13 empty
  Retail garrison WDTs are MapType 1 and list no tile.
- **After the WDT is read.** The C# parser decides: `WdtKind.GlobalWmo` and `TryGetGlobalWmoReference`, a FileDataID
  (modern form) or an MWMO path (Retail's 193 old-form maps). The contract test now asserts that the stated form and the
  parsed kind agree (tile grid: TiledTerrain or Empty; world model only: GlobalWmo) for every WDT it is given (section 14).
- **No grid assumption.** Nothing in the host assumes a 64x64 terrain grid; the host never looks inside a WDT.
- **What a runtime needs.** A scene descriptor of its own for a global-WMO map: the WMO reference plus the MODF
  placement the parser returns. Era's Dire Maul is the case where the yaw of 180 must be applied (audit 4.3).

## 14. Future runtime contract (Stage 03+)

**Ownership.**

| What | Owner | Lifetime |
|---|---|---|
| The selected client, its storage, its database | the host (`ModelViewer::LoadWoW`, `core::Game`) | until the next client load |
| Map identity (ID, directory, name, codes) | the host's `WorldMapCatalog` | the loaded client; replaced at the next load |
| The WDT reference (FileDataID or path) and whether it can be read | the host's catalogue | the same |
| WDT bytes | the host's storage, read on request | per request; the host keeps no copy |
| The tile manifest (kind, tiles, MAID FileDataIDs, global WMO reference) | the player's C# parser (`WdtParser`) | the map open in the player |
| Tile part bytes, loaded terrain tiles | the player, fetched through the host | the player's map session |
| Cached game bytes | the player's asset cache: emptied when the host's client epoch changes; today it keeps the files of the last three model loads within 256 MB, so a map session needs a generation of its own (Stage 03) | the client epoch |

**Flow.**
1. The user picks a map from the host's catalogue (a later Maps mode).
2. The host checks `belongsToActiveClient()` and the map's reference, and tells the player which map to open with a new
   message carrying the asset epoch. That message is a later branch's protocol change; none is added here.
3. The player fetches the WDT bytes through the existing asset messages.
   - **FileDataID reference:** `getAssetByFileDataID {fileDataID}`.
   - **Path reference:** once the map is inspected, the host has the root's FileDataID for it (`storageFileDataId`), so
     Era and MoP can fetch by FileDataID too; a freshly built catalogue has none yet (-1). Every MAID entry is a
     FileDataID in all four clients.
4. The player parses with `WdtParser.Parse` and branches on `Kind`. Tiles go through `TryGetTileFiles`, then
   `AdtParser.ParseTile` per tile. A global WMO goes through `TryGetGlobalWmoReference`.
5. Parts that come back `ok:false` become `NotSupplied`, with the host's reason kept.

**Two readers, one rule.** The catalogue reads through the storage root; the player's asset messages read through the
viewer's file index, where a custom folder (`bypassOriginalFiles`) can override a file. Stage 03 must either make the
asset path honour the catalogue's reference exactly, or report an override, so the bytes parsed are the bytes the
catalogue vouched for.

**The player never discovers installations.** The host stays the authority for game data (audit H7; 01-parsers 14).

**Tested at the data level now** (`run_contract.ps1`: one viewer run that reads the WDTs, the C# runner, then one probe
run per client, five viewer processes in all):
- **WDTs:** 36 WDTs of 14 map IDs in 4 clients were read through `readWdt` (114/114 viewer checks; a failed read or a
  requested map that no client has would fail the run). Each was written with its SHA-256 and its stated form.
- **Parsing:** `MapContractCheck.cs` checked every SHA-256 against the viewer's and parsed all 36 with the verified
  parser, 0 failures: TiledTerrain 26, GlobalWmo 9, Empty 1. The stated form agreed with the parsed kind on all 36.
- **Parts:** the sampled tiles' eight parts and every global world model reference (497 in all) were resolved through
  each client's storage: 484 available, 10 not in client storage, 2 not installed locally, 1 cannot be told by path.
- **The audit's gaps, reproduced exactly:**
  - Beta Kalimdor 44_24 root 787450 and 37_19 minimap 208191: not installed locally;
  - Retail/MoP Stormwind minimaps: not in client storage;
  - Era Development Land minimaps: not in client storage;
  - Retail's old-form MWMO path (map 1212): Retail's root has no name for it, so it cannot be told by path (section 18).

## 15. Error handling

| Case | Behaviour | Tested |
|---|---|---|
| No client loaded | the viewer's catalogue is NoClient ("no client is loaded"); every read refused with that reason | `-mapcatalogtest`, before the first load (`inspect`, `inspectAll`, `readWdt`, `inspectFile`) |
| A client is loading | NoClient "a client is being loaded" until the load finishes | code path (`ResetClientState`) |
| Map.db2 not on this computer | MapTableUnavailable "listed by this client but not on this computer yet" (and the cache stays unstamped) | code path (no such client here) |
| Map.db2's layout not in the definitions | MapTableUnavailable with the layout and build; the table is not read, Retail included | runtimes `nodbd` (both refused), `noretail` (Retail refused, Era read) |
| A definition block names the build but another layout | not used: the layout must match | runtime `buildkept` (Retail refused, Era read) |
| A decoy block for the same build placed first | ignored: the exact layout's block places the columns | runtime `earlier` (Retail ready; comparator 14,930 checks, 0 differences) |
| The schema has no Map table | MapTableUnavailable "the schema ... has no Map table" | runtime `noschema` (both) |
| The database could not be read for this client | MapTableUnavailable "this client's database was not read" (no stale reading) | code path |
| A legacy MPQ client | Unsupported (no Map.db2 is read for it); reads refused | code path |
| A row without a usable WDT reference | Unresolved + why (with the raw value); state Unsupported | Retail 105 rows |
| WDT not in the root | NotInClientStorage + what was looked up | Retail 110, Beta 2, Era 1, MoP 58 |
| A path the root cannot answer for | Unsupported "cannot be told by path" | probes in Retail and Beta |
| WDT not installed | NotInstalledLocally | Beta 787450 and others (a tile part; no WDT is missing locally in these clients) |
| WDT open/read fails | Unreadable + the reason: encrypted, too large, damaged (checksums), no content key, data missing, empty, not a WDT | encrypted and too large on real files; no content key in an isolated test (section 20); the rest code paths |
| Client changes while a catalogue is in use | belongsToActiveClient false; every read refused | every switch step, every earlier catalogue |
| A catalogue asked for before any load, or kept from an MPQ client | refused: not Ready | before-load check; code path |

In every negative runtime the viewer did not crash; its own checks report FAIL exactly where the catalogue is
(rightly) not ready.

No path is guessed beyond the audit-verified directory rule, and there is no Retail fallback: a client whose layout is
unknown gets no catalogue, with the reason.

## 16. Regression (`develop` baseline vs this branch, same commands, same machine, back to back)

| Feature | Command | develop | this branch |
|---|---|---|---|
| Character customization, Retail | `-customizationtest -product wow` | PASS 92/0 | PASS 92/0 |
| Classic Era | `... wow_classic_era` | PASS 37/0 (3 skipped) | PASS 37/0 (3 skipped) |
| MoP Classic | `... wow_classic` | PASS 49/0 (2 skipped) | PASS 49/0 (2 skipped) |
| Classic Beta | `... wow_classic_beta` | PASS 192/0 (1 skipped) | PASS 192/0 (1 skipped) |
| Models, drawn by the Unity player | `-mo creature/bear/bear.m2 -unityipctest` | PASS | PASS |
| Buildings, drawn by the Unity player | `-wmo 108631 -unityipctest` | PASS | PASS |
| Textures (BLP decode) | `-dumptex 134400` | PNG 24F661134C5F85B0 | identical |
| Client capabilities (models, textures, buildings, races, NPCs, items, files and tables not installed or not read), all four clients | `[clientcaps]` | | identical but for the line's own timing |

Plus:
- `WowParserTests` and `WorldParserTests`: 0 failures; `MapContractCheck`: 36 WDTs, 0 failures (the runner's
  "ALL RUN TESTS PASSED").
- No IPC message, protocol version or player script changed.
- The database each client builds differs from develop's reader only where text was empty before (section 17,
  claim 5): Retail 116 rows, MoP 37, Beta 1 regain their strings (for example Mount 2982 "Hearthkeeper's Wandering
  Caravan", ChrRaces 99 "Stoneforged Sentinel", 38 ChrCustomizationChoice names), and Beta's Map.WorldCopyOfMapID
  reads as absent.

## 17. Adversarial review findings

Ten reviewers each tried to break one claim of this report on the real clients and the code (workflow
`wf_7d6acb32-9f7`, 55 agents); every finding was then checked by an independent verifier (labels: REAL, PARTLY). All ten
claims came back **WEAKENED**: the core held, a part was overstated or a defect was shown. None was refuted. Each
finding, and what was done:

| Claim | Finding (verifier) | Done |
|---|---|---|
| 1 WdtFileDataID mapping | A definition was chosen by its BUILD line as well as its layout: a Map.dbd block for another layout listing Retail's build overrode Retail's positions (shown: Azeroth's "WDT" became a 64 KB zone map, the viewer's own test still passed) (PARTLY) | Fixed: `verifiedLayoutOnly` tables take only the definition of their exact layout hash (`getStructureByLayout`); negative tests `buildkept` and `earlier` added (section 15) |
| | The "layout not verified" test removed LAYOUT and BUILD together (REAL) | Covered by the two new cases |
| | Nothing in the viewer noticed a column naming a non-WDT file (PARTLY) | Fixed: an Available file must start with the MVER tag; the viewer's test also checks the layout against the file header |
| | A `layoutHash` attribute on the Map table would have bypassed the definitions (PARTLY) | Fixed: ignored for `verifiedLayoutOnly` tables |
| 2 Path rule | CascLib's own storage files (ENCODING, INSTALL, ...) answered root lookups, with made-up FileDataIDs (REAL) | Fixed: flagged in CascLib, excluded from lookups and name counts; probes `INSTALL` and 8345027 added |
| | "285 + 59" overstated the FileDataID equality (REAL) | Corrected: 285 (MoP 227 + Era 58) |
| | No check of `storageFileDataId` -1 when not in storage; listfile independence untested (REAL) | Comparator check added; listfile-free run added (identical) |
| 3 Classification | InstanceType "equals it on every row" and "0 = only difficulty-0 rows" overstated (REAL) | Corrected to the counts with exceptions (section 4) |
| | Beta and Era store LocationMapID 0 with no point (REAL) | Documented in the header, Map.dbd and section 4 |
| | MapType comments said "instanced" / "has no WDT" (PARTLY); the layout arithmetic did not add up (REAL) | Reworded; corrected |
| | The viewer's own test checks no classification code (PARTLY) | Recorded: the comparator is the check of column placement; the viewer adds the layout and map-reference checks |
| 4 Ownership | NoClient was decided by "a database object exists", wrong under `-dbfromfile` (PARTLY) | Fixed: a loaded client has a valid profile |
| | Folders that never opened a storage shared generation 0 (MPQ -> MPQ) (PARTLY) | Fixed: every folder gets its own number at construction; catalogues that are not Ready refuse reads |
| | The static file inspection had no ownership check (REAL) | Fixed: `inspectFile` on the catalogue, guarded like the rest |
| | The owner handed out nothing before a load (PARTLY) | Fixed: a NoClient catalogue from construction and during a load |
| | Only the previous catalogue was re-checked after a switch (REAL) | Fixed: every earlier one, and a Retail -> Retail run |
| 5 States | CascLib's own files (as claim 2) (REAL) | Fixed |
| | Rows in encrypted DB2 sections whose key IS known had empty text: a Map row there would lose its directory (REAL) | **Fixed in the DB2 reader**: text offsets now count across every section. All four databases rebuilt and compared with the old reader's: only previously empty strings changed (Retail 116 rows in ChrCustomizationChoice, ChrCustomizationOption, ChrRaces, Creature, Mount, TransmogSet; MoP 37; Beta 1; Era none), and all 156 recovered values equal the audit's independent decoder |
| | Damaged data could be "Available" (CascLib pads short reads) (REAL) | Fixed: reads check the frame hashes the data carries and the content key (MD5); a mismatch is Unreadable "damaged" (and a read whose key cannot be obtained is not Available either: section 20) |
| | Edge states mislabelled: no storage, invalid FileDataID, a path in a root without names (REAL) | Fixed: NotChecked / Unsupported, and the nameless-root rule (section 6) |
| | Evidence came from a build older than the final source (PARTLY) | Every figure regenerated with the final binaries, whose hashes each JSON records; code-path-only states marked (section 7) |
| 6 Cache | The key leaves out the encryption-key list (REAL) | Fixed: missing keys recorded per table; a cache is rebuilt when one becomes known (tested) |
| | Only an unknown state name was caught (REAL) | Fixed: missing readings and counts that do not add up are refused (tested) |
| | The "partly downloaded Map.db2" rule is untested and depends on the listfile naming the table (REAL) | Recorded (section 18) |
| 7 Switching | A database that failed to open left the previous client's reading for the catalogue (REAL) | Fixed: cleared first thing, and the reading carries its client |
| | The viewer's switch checks compared the catalogue with itself (PARTLY) | The layout is now read from the file header; the comparator remains the independent check |
| | MPQ catalogues shared a storage number (PARTLY) | Fixed (as claim 4) |
| | The fresh-folder and failed-open paths were never run (REAL) | Recorded (section 18) |
| 8 Performance | "Retained" was measured after the test's inspections; the estimator was biased; "loading progress" unstated; the stage range and first-read timing overstated; SHA-256 in every inspection (REAL/PARTLY) | Retained measured at load; estimator fixed; statement added; ranges corrected; hashing only on request; the load's laziness is a check |
| 9 Global WMO | A path in a root without names was "not in client storage" (PARTLY) | Fixed: the nameless-root rule; it also showed that Retail's and Beta's roots do carry names, contrary to this report's draft |
| | "Instanced" and "has no WDT" wording (REAL) | Corrected |
| | No test asserted stated form against the parser (REAL) | Fixed: the contract test asserts it, the comparator checks three forms and MapType 4's path |
| 10 Boundary | The contract test only checked that bytes parse; missing maps and failed reads were skipped (REAL) | Fixed: the viewer writes each WDT's SHA-256 and stated form, the C# step checks both, and a failed read or a missing map is a FAIL |
| | The catalogue and the player's asset path are two readers (custom-folder overrides) (PARTLY) | Recorded (section 14) |
| | The cache-lifetime row; `storageFileDataId` only after an inspection; "two viewer runs" (REAL/PARTLY) | Corrected (section 14) |

## 18. Unresolved limitations

- **Old-form global WMOs in Retail.** 193 Retail WMO-only WDTs name their world model by an MWMO path, and Retail's
  root has no name for those files (it names 149,147 of its files, not these). By path the catalogue answers
  "cannot be told"; only the community listfile resolves those paths to FileDataIDs (the host's existing by-path asset
  lookup uses it; audit R6). Stage 03 must keep that dependency visible.
- **Classification beyond the established codes.** The catalogue exposes codes, not a taxonomy. InstanceType 0, 3, 4,
  6, 7 and 8 are unlabelled; Beta's column 14 is unknown; expansion and group size are not exposed.
- **Beta and Era evidence.** The name and the map references are *supported*, not proven, in these clients: they have
  few rows the cross-table tests can use. Their LocationMapID 0 is ambiguous.
- **Encrypted Map.db2 rows.** Beta's 8 key-locked rows cannot be listed: their IDs are encrypted too. When a key arrives
  the database is rebuilt and they appear.
- **Definitions are per layout.** `Map.dbd` lists three layouts. A client update that changes Map.db2's layout gets no
  catalogue, with the layout named, until its columns are established and added. By design: no unverified read.
- **States shown only in code.** NotInstalledLocally for a WDT (no WDT is missing locally in these four clients; shown on
  tile parts and other files), and Unreadable for damaged, data-missing, empty and non-WDT files (none found on this
  computer). The "Map.db2 not on this computer yet" status is likewise a code path.
- **Untested switch paths.** Every switch here reuses the folder object of one installation. The fresh-folder path
  (another installation, or after an MPQ client) and a database that fails to open were reviewed in code, not run.
- **Partly downloaded clients and the cache.** Whether a table counts as "not installed" relies on the listfile naming
  its file (existing rule); a client update changes the key anyway.
- **Availability is a snapshot.** It reflects the local index as the storage was opened (as for the whole viewer); a
  download during the session is seen at the next load.
- **No UI.** Nothing shows the catalogue yet. The load logs one `[mapcatalog]` line.

## 19. Status

Branch `world/02-map-catalog`, base `develop` `bedc74cc`: the catalogue (`f7eab99d`) and the follow-up of section 20,
local commits only. **PUSH STATUS: NOT PUSHED.**

## 20. Follow-up: section-pointer safety and content-key verification

Two concerns were raised about the committed catalogue (`f7eab99d`) and examined on the same branch: how
`WDC3File::get()` finds a record's section and its string, and whether `CASCFolder::readWholeFile()` can report
`Read` without checking the content key. Both were confirmed and corrected. Nothing else changed: the Map.db2 column
mappings, `Map.dbd`, the classifications, the WDT rules, client selection, the Unity parsers, IPC and UI are untouched.
Method: three independent readers of the code first, the regression tests written and run against the unfixed build
before any fix, then the fix, then an adversarial review of the fix and the tests (section 20.4).

### 20.1 WDC3: which section a record lives in, and where its string is

**Original behaviour** (`f7eab99d` `wdc3file.cpp:748-799`).
- The record's section was found by testing `recordOffset >= m_sectionBuffers[s] && recordOffset <
  m_sectionBuffers[s] + size` for each section in turn (`:753`). Each section's buffer is its own `new[]`, so for a
  record of section *k* >= 1 the tests against sections 0 to *k*-1 compare pointers into unrelated allocations. C++
  leaves that result unspecified. The subtraction `recordOffset - m_sectionBuffers[s]` that followed is defined only if
  the test happened to pick the right buffer, and it was divided by the record size.
- The string pointer `strStart + offset` was formed (`:779`) before it was checked against the buffer, and nothing
  checked a section's declared string block against the bytes its buffer holds. A malformed or truncated file could
  therefore make the code form a pointer past its allocation (undefined behaviour), even though the check after it then
  returned an empty string.
- A string was read up to the end of its section's whole buffer (`:795`), not the end of its own string block.

**Reproducible?**
- The cross-allocation comparison runs on real data, for every text field of a record in a section after the first:
  Retail 116 rows, MoP 37, Beta 1, the rows that regained their text. It cannot be shown as a wrong value: on this
  platform (MSVC, x64) the comparison compiles to a plain address comparison and the separate buffers do not overlap,
  so it gave the right strings (all 156 matched the independent decoder). It is a reliance on unspecified behaviour,
  not an observed failure.
- The out-of-bounds pointer needs a malformed file. In the real tables no section's string block extends past its
  buffer, and every non-empty block ends with a NUL (the review scanned 1,361 unique WDC3/4/5 files of the four clients).
- Reading past a block's end is reproducible on synthetic files: a string with no NUL before its block ends read on
  into the next bytes (`tail` at the end of a file; `cd` plus the copy table's first byte).

**Correction** (`wdc3file.h`, `wdc3file.cpp`).
- When the record offsets are listed, each record's location is kept as integers, `(section, index within the
  section)`: section 0's records (`:268`), a copy-table row takes its source row's location, a later section's records
  (`:543`), and sparse rows (and a copy of a missing row) get none. `m_recordLocations` always has one entry per record.
- `get()` computes the string's offset from the location in unsigned 64-bit arithmetic, from the header's record count
  as before (`:775-807`). It forms a pointer only after checking that the string starts inside its section's string
  block **and** inside the bytes that section's buffer holds, and it reads to the first NUL inside both (`:797`). A
  string without one reads empty. No pointer is compared with another allocation's and nothing is divided.
- The shared text preamble no longer computes section 0's string-block pointer (the sparse path, unchanged, never used
  it).

**Regression test:** `scripts/tests/wdc3-strings`. It runs the production `WDC3File::open()` and `get()` from wow.dll
over seven synthetic tables held in memory: three sections with a copy table, a zero-filled section under a missing
key, a section with no records, a string block declared longer than the file, unterminated strings at the end of a file
and before a copy table, and a header record count that differs from the sections' sum. Each expected string is worked
out by hand in the source.

| Build | Result |
|---|---|
| `f7eab99d` (deployed build, same harness) | FAIL, 52 passed, 2 failed: the two unterminated strings read `tail` and `cd` + byte 0x07 |
| fixed | PASS, 54 passed, 0 failed |

No runtime test can tell the old pointer search from the integer lookup, because on this platform the unspecified
comparison happens to give the right answer. That part is protected by review and by the comment at
`WDC3File::m_recordLocations` (the test's README says so).

**Measured on the four clients.**
- The four clients' databases were rebuilt with the fixed reader and compared table by table, row by row, with those
  of `f7eab99d`'s reader: identical in every table, with the same cache key and the same per-table readings.
- Against develop's reader (before `f7eab99d`): the same 154 rows / 156 values regain their text as before (Retail
  116 rows, MoP 37, Beta 1, Era 0). All 156 values equal the audit's independent decoder; they come from records in
  sections 1 to 22.

### 20.2 Content key: Read only when the bytes were checked

**Original behaviour** (`f7eab99d` `CASCFolder.cpp:277-299`): `hasKey = done == size && CascGetFileInfo(...,
CascFileContentKey, ...) && keySize == 16`. When `hasKey` was false the MD5 comparison was skipped and the function
returned `Read`.

| Scenario | `f7eab99d` | now |
|---|---|---|
| content key unavailable (`ERROR_NOT_SUPPORTED`) | Read, bytes unchecked | Unverified, no bytes, error 50 |
| `CascGetFileInfo` fails for another reason | Read, bytes unchecked | Unverified, no bytes, its error |
| the query fails but leaves no error | Read, bytes unchecked | Unverified, no bytes, error 50 |
| the query succeeds with a length other than 16 (or none) | Read, bytes unchecked | Unverified, no bytes, error 50 |
| MD5 of the bytes differs from the content key | Corrupt, no bytes | Corrupt, no bytes (unchanged) |
| the read fails part way | Encrypted / Corrupt / NotLocal / Damaged, no bytes | the same (unchanged) |
| every byte read, key obtained and equal | Read | Read |

**Reproducible?** Yes, deterministically. An isolated test compiles the production `CASCFolder.cpp` into a test
program in which every CascLib call is a scripted stub. `f7eab99d` returned `Read`, with the unchecked bytes, in every
key-unavailable, failed-query and wrong-length case. With the bundled CascLib, a key of the wrong length cannot occur
(it writes 16 or fails). Through the catalogue the other cases cannot occur either today: every file the WoW root lists
comes with a content key. The public function could still report unchecked bytes as `Read`.

**What the real clients do.** For every WDT the catalogue reads (Retail 969, MoP 227, Beta 69, Era 58), plus three probe
files per client, a separate census opened each file as the viewer does:
- the content key was obtained, 16 bytes, for every file;
- the MD5 of the bytes equalled it for every map WDT;
- every map WDT is stored with a frame table whose frames all carry a hash, so CascLib checked the frame hashes of
  every one as well.

**Contract and correction** (`CASCFolder.h`, `CASCFolder.cpp`, `WorldMapCatalog.cpp`).
- `Read` now requires all three: every byte read, the content key obtained (16 bytes), and the MD5 of the bytes equal
  to it.
- Frame hashes are checked where the data carries them (`CASC_STRICT_DATA_CHECK`), but they are not a separate
  requirement. CascLib cannot report whether a frame was checked. A file stored as a single chunk without a frame table,
  or as plain data, carries none, so requiring frame hashes would refuse such files. The content key covers every
  decoded byte; a frame hash covers only encoded bytes.
- A whole read whose content key cannot be obtained returns the new `ReadOutcome::Unverified`
  (`CASCFolder.cpp:302-308`), with no bytes. It carries the query's error, or `ERROR_NOT_SUPPORTED` for a key of the
  wrong length or a failed query that left no error, so the error is never 0.
- The key is asked for while the file is still open (`:286`). Partial reads keep their previous outcomes.
- The catalogue maps `Unverified` to Unreadable: "... was read, but its content key could not be obtained, so its
  bytes could not be checked (error N)" (`WorldMapCatalog.cpp:503`). `readWholeFile` has no other caller.

**Regression test:** `scripts/tests/casc-whole-read`, 21 cases.
- Read with a matching key, by FileDataID and by path.
- No content key, by FileDataID and by path; a failed query; a query that fails without an error.
- Key lengths 0, 15, 32, and no length written.
- Three wrong keys.
- Content delivered in 7-byte and in 1-byte pieces.
- Reads failing part way with four different errors.
- An empty file.

Every case also requires the file handle to be closed exactly once. The stubs answer nothing through a handle that is
not open, so a key query made after closing the file fails the case.

| Build | Result |
|---|---|
| `f7eab99d` (the harness's first version, which asserted "not Read, no bytes") | FAIL, 13 passed, 7 failed |
| the final harness, `f7eab99d`'s `readWholeFile` compiled with the fixed header | FAIL, 13 passed, 8 failed (the same cases plus the no-error one) |
| fixed | PASS, 21 passed, 0 failed |
| a mutant that queries the key after closing the file | FAIL, 5 passed, 16 failed |

**Measured on the four clients:** no change. In all four switch sequences, every map's and every probe's state, size,
SHA-256, storage FileDataID and reason is identical to `f7eab99d`'s run.

### 20.3 Validation (final binaries; test pass `-Tag g`)

| Test | Result |
|---|---|
| Switch sequences: Retail -> MoP -> Retail, Retail -> Beta -> Retail, Era -> Retail, Retail -> Retail | PASS 44/44, 44/44, 27/27, 27/27 |
| Comparison with the audit (`compare_map_catalog.py`) | 104,333 checks, 0 differences; without the listfile's map paths 44,125, 0; decoy definition 14,930, 0; contract, probe and cache runs 67,152, 0; 19 of 19 planted errors caught |
| WDT contract (catalogue -> C# parser) | 36 WDTs, SHA-256 and stated form checked, 0 failures; 497 parts resolved, the same states as before |
| DB2 multi-section strings | the four clients' databases identical to `f7eab99d`'s; 156 of 156 recovered values equal the independent decoder |
| Database cache | 8 of 8 cases as designed (reuse; tampered state, contradictory counts, missing readings and a newly known key rebuild; an unknown key reuses; an old schema is never selected; a changed definition gives a new key) |
| Negative definitions and schema | refused with the reason where they must be; the decoy block is ignored |
| Regression against develop | customization Retail 92/0, Era 37/0, MoP 49/0, Beta 192/0; Models and Buildings through the Unity player PASS; texture PNG identical; capability lines identical |
| C# parser tests | WowParserTests and WorldParserTests 0 failures; MapContractCheck 0 failures |
| The two new regression tests | 54/54 and 21/21 |

### 20.4 Adversarial review, and what stays as it was

Three reviewers tried to break the fix and the tests (`wf_8bdd12f0-1a9`):
- **WDC3 fix:** HOLDS.
- **Content-key fix:** HOLDS.
- **The tests:** WEAKENED. Acted on:
  - the stubs now track the file handle;
  - the refused outcomes' error codes are asserted;
  - the case of a failed query that leaves no error was added;
  - the WDC3 test copies the DLLs it tests beside it;
  - the two expectations that pin this reader's choices rather than the format are labelled as such (the header's
    record count governs; a section with no records is not read);
  - the README states what no test can show.
- **This report:** stale wording on frame hashes and three line references were corrected.

Older problems the reviewers and readers found, all in code before this branch and not changed here (no general
rework was asked for). None occurs in the real tables checked:
- Section 0's records are not checked against section 0's buffer. A malformed section 0 that declares more records than
  its bytes hold gets record pointers past the buffer, and the field read (`readFieldValue`) reads through them. Later
  sections have that check.
- Section 0's buffer is not zero-filled before it is read. After a short read, text in the unread tail would be built
  from leftover memory, though always inside the buffer.
- A copy-table row whose source id is missing gets a null record pointer and crashes in `readFieldValue`.
- The sparse-table text path walks inline strings with pointer steps that are not bounds-checked first.
- Text arrays resolve every element from the field's first position, as before.
