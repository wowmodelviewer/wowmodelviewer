# Classic Beta: the empty equipment list and the Blood Elf models

Branch `fix/classic-beta-equipment-and-blood-elves`, from `develop` at `9da5343c` (2026-10-13). Two problems were
reported for Classic Beta:

- **A.** With a Classic Beta character loaded, every equipment selection window lists no items.
- **B.** Blood Elf male and female appear under "NPC Races" instead of "Playable Races", and their SD (classic) models
  cannot be chosen.

Both were investigated on the installed client's own data before anything was changed. They have **separate causes**
(A: two table layouts the viewer had no definition for; B: the client's own race data). A cannot depend on race: the
item list is built before any character exists and never reads the race.

**Push status at delivery: NOT PUSHED.**

Labels: **PROVEN** = shown on the client's own bytes, the viewer's logs or captures, with the numbers given;
**INFERRED** = follows from proven facts and a stated assumption; **UNKNOWN** = not established. Code is cited as
`file:line` against `9da5343c`. Classic Beta means build 1.60.1.70338, the build installed while this work was done,
unless 1.60.1.70374 is named: the update the game launcher installed on 2026-10-14, on which the final build was
verified again (section 9).

## 0. Result in brief

| Item | Result |
|---|---|
| A. Equipment list on Classic Beta | **Fixed** (1.60.1.70338 and 1.60.1.70374). 0 items -> 18,082 items, every slot populated (Chest 1,730 rows with "None"). Equip, replace, remove, search, categories and View > Item work; Load Item Set works for sets whose pieces are named (section 10). |
| A. Cause | Schema: Classic Beta's `Item` (layout `9A2A4834`) and `ItemSparse` (layout `6FCC3191`) had no table definition, so the viewer read neither table. |
| A. Change | Two layout definitions, keyed by layout hash, in `bin_support/dbd/Item.dbd` and `ItemSparse.dbd`: the structure from the client's own file headers, the fields the viewer reads identified on the client's own rows, the other ItemSparse columns named after Retail's by position and the unidentified Item columns given neutral names (section 2.4). No build number; the viewer reads no other client's data. |
| A. Replacing an item | Found while verifying on Classic Beta, Classic Era and MoP Classic: choosing an item that has no appearance in the client kept the previous item's look under the new name (Retail runs the same code: INFERRED). `WoWItem::setId` now starts each item from its own appearances only; a hand emptied that way opens again (section 2.5). |
| B. Blood Elf under NPC Races | **Unchanged, by the client's own data.** Classic Beta 1.60.1.70338 marks race 10 not playable, and every other playability table in the client leaves it out (section 3). Moving it would contradict the client. |
| B. Blood Elf SD (classic) model | **Not in this client.** Classic Beta defines only the High Definition Blood Elf models; it has no classic Blood Elf character model, variant pair, options or readable display (section 4). The Classic/HD selector correctly offers HD only. |
| Tests | New `-customizationtest` checks for the item list, the equipment dialogs, the race categories and the Blood Elf's options. The final build passes on all four clients: Classic Beta 1.60.1.70374, Classic Era, MoP Classic and Retail (sections 8, 9). |
| Other clients | Retail and Classic Era read exactly what they read before (per-table states compared); MoP Classic too, except one more decrypted row in four tables (an encryption key the viewer learned meanwhile). Item counts unchanged. |

## 1. The client and how the viewer reads it

| Fact | Value | Source |
|---|---|---|
| Product | `wow_classic_beta`, folder `_classic_beta_`, `WowB.exe` 1.60.1.70338 | `.build.info`, exe version resource (PROVEN) |
| Build | 1.60.1.70338, build config name `WOW-70338patch1.60.1_ForeverBeta`; DB2 build string `wowstatic_camelot_1_60_1` | build config, DB2 headers (PROVEN) |
| Storage | `E:\World of Warcraft\Data*wow_classic_beta` through CascLib; 29 viewer files not installed, no table among them | viewer log (PROVEN) |
| Schema | `games/wow/12.0/database.xml` + `dbd/*.dbd` (no 1.x schema ships; `ClientInstallations::resolveSchema` takes the newest) | log line `[clientload] schema: "games/wow/12.0 as the base (no 1.60 schema ships) ..."` (PROVEN) |
| Table checks | Classic Beta is not the schema's own generation (`WoWDatabase.cpp:164`), so a table is read only when a definition matches the file's own layout hash or build, or database.xml records the file's layout (`WoWDatabase.cpp:208-257`) | code (PROVEN) |
| Before the change | `[schema] "12.0" for "1.60.1.70338" : 52 tables verified ... 2 not read`; the two are Item and ItemSparse | log (PROVEN) |
| After the change | `54 tables verified, 0 kept at schema positions, 0 not read, 0 not installed, 0 not in this client, 1 absent fields` (the absent field is `Map.WorldCopyOfMapID`, as before) | log (PROVEN) |

No build number is hard-coded by the change: both definitions are keyed by layout hash only (no `BUILD` line), so they
apply to any build whose files have exactly that layout and to nothing else.

## 2. Problem A: the empty equipment list

### 2.1 The workflow

1. `ModelViewer::InitDatabase` (`modelviewer.cpp:607`) builds the item list once per client load: `items` starts with
   "---- None ----" (`database.cpp:90-97`), then the SQL at `modelviewer.cpp:693-698` reads
   `Item LEFT JOIN ItemSparse` for every item with an inventory type and either a name or a display chain
   (ItemModifiedAppearance -> ItemAppearance -> ItemDisplayInfo).
2. A slot button calls `CharControl::selectItem` (`charcontrol.cpp:628-711`), which lists the in-memory items whose
   inventory type fits the slot (`correctType`, `util.cpp:189-215`). There is no race, class, quality or client filter.
3. The dialog (`itemselection.cpp`) filters by name or item ID and, when the slot's items fall in more than one
   category, by category. Choosing a row calls `CharControl::OnUpdateItem`, then `WoWItem::setId`, which resolves the
   display through ItemModifiedAppearance and ItemAppearance and loads the item's models and textures.

### 2.2 Where the count went to zero

| Stage | Classic Beta before | Classic Beta after | Classic Era | MoP Classic | Retail 12.1.0.69933 |
|---|---|---|---|---|---|
| Item.db2 layout | 9A2A4834, **not read** | 9A2A4834, verified | 39911A3F | 39911A3F | 996192AA |
| ItemSparse.db2 layout | 6FCC3191, **not read** | 6FCC3191, verified | B51F7C79 | B51F7C79 | 1C17D17F |
| Item rows read | **0** | 31,847 | 24,973 | 89,489 | 213,367 |
| ItemSparse rows read | **0** | 19,245 | 24,442 | 88,605 | 175,105 |
| Items with an inventory type (independent reader) | 19,233 | 19,233 | 15,472 | 65,155 | 141,079 |
| ...named | 8,044 | 8,044 | 15,174 | 64,439 | 111,463 |
| ...unnamed, with a display chain ("Unnamed item") | 10,038 | 10,038 | 259 | 644 | 26,038 |
| **Item list (`Found N items`)** | **0** (`Error during Item detection from database.`) | **18,082** | 15,433 | 65,083 | 137,501 |
| Chest dialog rows (with "None") | **1** | **1,730** | 1,427 | 6,385 | 12,285 |
| `[clientcaps] items` | 0 ("Items: this client's item tables could not be read") | 18,082 | 15,433 | 65,083 | 137,501 |

The 18,082 is exactly what an independent reader of the client's bytes predicted before the change (the same SQL and
slot filter run on Beta's own Item and ItemSparse); the same reader reproduces the viewer's counts on the three working
clients exactly. Per slot (items, "None" apart): Head 1,311, Shoulder 1,285, Shirt 83, Chest 1,729, Belt 1,345, Legs
1,531, Boots 1,569, Bracers 1,238, Gloves 1,523, Right hand 3,569, Left hand 3,575, Cape 731, Tabard 53, Quiver 155.
Retail updated to 12.1.5.70391 during this work; there it lists 137,665 items with the same two layouts.

`items 0` is not a recent regression: every Classic Beta load logged since Classic support arrived (builds 70205,
70235, 70245, 70291 and 70338) shows it.

### 2.3 Classification of the cause

| Candidate | Verdict |
|---|---|
| Reading (the DB2 reader) | Ruled out. Both files are standard WDC5 with Retail's structure (id list, copy table, encrypted sections; ItemSparse sparse). Once defined, they read correctly (counts above). |
| **Schema (table definitions)** | **The cause.** Neither layout was in `Item.dbd` / `ItemSparse.dbd`, no BUILD line or range covers 1.60.1, and database.xml gives these tables no `layoutHash`; `WoWDatabase.cpp:247-252` then skips both tables. |
| Relationship (display chain) | Ruled out: ItemModifiedAppearance, ItemAppearance and ItemDisplayInfo were already read and verified (16,754 / 9,029 / 42,455 rows); 16,635 of the 19,233 equippable items resolve to a display. |
| Filter | Ruled out: `correctType` uses InventoryType only; Beta uses the same values. |
| Display | Ruled out: all 57 model and texture files behind the eight representative items are installed. |
| UI | Ruled out: the dialog opened normally and listed what it was given (one row). |

### 2.4 The change

`bin_support/dbd/Item.dbd` gains layout `9A2A4834` (16 fields) and `bin_support/dbd/ItemSparse.dbd` gains layout
`6FCC3191` (68 fields), each with COLUMNS entries for the new neutral names. What each name rests on:

| Table, field | Evidence on Classic Beta's own bytes |
|---|---|
| Item f0 ClassID, f1 SubclassID | values are ItemClass IDs; the (f0, f1) pair exists in Beta's ItemSubClass for 31,842 of 31,844 rows |
| Item f2 Material, f3 InventoryType, f4 SheatheType | each equals the same column of Beta's own ItemSparse for 19,245 of 19,245 items both tables hold |
| Item f7 IconFileDataID | equals Classic Era's for 24,859 of the 24,960 items both clients hold (named; the viewer does not read it) |
| Item f5, f6, f8-f15 | not identified; named `BetaCode5` ... `BetaCode15` and placed by order only (the viewer reads none of them) |
| ItemSparse f0-f66 | the same 67 field storage entries (sizes, offsets) as Retail's `1C17D17F`, so Retail's names by position; `Display_lang` (f4) holds the names ("Worn Shortsword", "Shining Silver Breastplate"), and Material, InventoryType and SheatheType equal Beta's Item |
| ItemSparse f67 | a trailing 1-byte field Retail does not have (values 0, 2, 3); `BetaCode67`, not identified |

No code, database.xml or data from another client was needed for the list. A `.dbd` change is part of every client's
database cache key (`WoWDatabase::cacheInputs`), so each client's cached database is rebuilt once on its first load
after the update (section 7). The `.dbd` files are data: an update must copy `dbd/Item.dbd` and `dbd/ItemSparse.dbd`
next to the program, or Classic Beta keeps its old cached database with both tables unread.

### 2.5 Replacing an item that has no appearance

With the list filled, replacing an item could show the wrong look. `WoWItem::setId` (`WoWItem.cpp:58-127` at
`9da5343c`) cleared the item's appearance list and display only when the new item had appearance rows; for an item with
none (Classic Beta lists named items without one, for example 1020 "Leather Helmet D (Test)" under Head) it kept the
previous item's display, so the Wolfshead Helm stayed on the head under the new name. The self-test of the branch build
before this fix recorded it: `0:1020/28987` (item 1020 with the Wolfshead Helm's display) on Classic Beta and Classic
Era, `0:1020/18392` on MoP Classic. Retail lists such items too (166786 under Head) and runs the same code (INFERRED).

The changes, in `WoWItem.cpp` unless named:

- `WoWItem::setId` resets the per-item state (levels, appearance and modifier maps, display) before reading the new
  item's appearances, so an item without one shows none. With the final build the self-test shows `item 1020, display 0,
  0 models` on Classic Era, MoP Classic and Classic Beta 1.60.1.70374 and `item 166786, display 0, 0 models` on Retail
  (section 9).
- `WoWItem::refresh` opens the hand before deciding it again from the hand's model, so replacing a weapon by such an
  item leaves the hand open, not closed around nothing.
- A saved display of 0 (an item with no appearance where it was saved) is no longer forced on load by
  `WoWItem::restore` and the `.chr` loader, so the item's own look in the client that loads it stays (INFERRED from
  code; a same-client round trip gives the same 0 either way).
- `ModelViewer` FBX equipment restore (`modelviewer.cpp`) requires a model in a hand only when the item has a display,
  as the viewport does.

## 3. Problem B: Blood Elf under NPC Races

### 3.1 How the categories are made

`RaceInfos::init` (`RaceInfos.cpp:38-166`) reads every ChrRaceXChrModel row with its ChrRaces, ChrModel and creature
model. The only Playable/NPC decision in the code is `RaceInfos.cpp:101`:
`isNPC = ((Flags & 1) != 0) && raceID != 23 && raceID != 75` (ChrRaces.Flags bit 0; races 23 Gilnean and 75 Visage are
forced playable). `filecontrol.cpp:330-381` puts each race of `RaceInfos::getRaceMenu()` under Playable Races or NPC
Races by that flag.

### 3.2 What Classic Beta's own data says about race 10

| Source in Classic Beta 1.60.1.70338 | Races it makes playable | Blood Elf |
|---|---|---|
| ChrRaces.Flags bit 0 clear | 1-8, 95, 96 | **set** (Flags 0x8D; Retail 0x60008C and MoP Classic 0x60000C have it clear) |
| CharBaseInfo (race/class pairs offered at creation) | 1-8, 95, 96 (56 rows) | absent |
| CharacterLoadout (starting outfits) | 1-8, 95, 96 (114 rows) | absent |
| ChrRaceRacialAbility | 1-8, 95, 96 | absent |
| ChrRacesCreateScreenIcon | 1-9, 95, 96 | absent |
| ChrRaces.CinematicSequenceID / FactionID | set for 1-8 | 0 / 0 (Retail and MoP Classic: 162 / 1610; faction 1610 does not exist in Beta) |
| Racial skills naming race 10 with a narrow race mask | - | none |
| Server hotfix cache (`DBCache.bin`, build 70338) | no race, model, customization, class or loadout row | - |

What Blood Elf keeps (PlayableRaceBit 9, model and customization rows) Beta also keeps for all 21 other Retail races it
does not make playable, and for NPC-only races; neither is a playability marker. **Every source agrees: Classic Beta
1.60.1.70338 does not make the Blood Elf playable.** The viewer shows exactly that. (PROVEN)

Of the candidate rules evaluated, the only ones that would move Blood Elf to Playable (PlayableRaceBit >= 0, or that
rule combined with the current one) also move 21 more Beta races, Retail's two "TBD NPC Race" rows and Classic Era's
Goblin, against every source above. No race exception was added, as the evidence does not support one.

## 4. Blood Elf SD and HD models

| FileDataID | File | Generation (from the M2: type-19 eye texture = HD) | Used by Classic Beta |
|---|---|---|---|
| 1100087 | `character/bloodelf/male/bloodelfmale_hd.m2` | HD, 253,309 vertices, Beta's own bytes | ChrModel 19 (race 10, male), texture layout 121, 101 displays on 8102/8103 |
| 1100258 | `character/bloodelf/female/bloodelffemale_hd.m2` | HD, 193,115 vertices | ChrModel 20 (race 10, female), texture layout 122 |
| 117170 | `character/bloodelf/male/bloodelfmale.m2` | classic, 7,381 vertices; **byte-identical to Retail's copy** | creature model 2208 only; no readable display (15 CreatureDisplayInfo rows sit in sections this computer cannot decrypt: whether one uses it is UNKNOWN), no ChrModel, no options, no variant pair |
| 116921 | `character/bloodelf/female/bloodelffemale.m2` | classic, 8,061 vertices; byte-identical to Retail's | creature model 2209 only; same |

- Classic Beta's ChrModelAltVariant (18 rows) pairs ChrModel 1-18 (races 1-9) with their classic models 257-272, 277
  and 278. **There is no row for ChrModel 19 or 20**, no ChrModel 273-276, no option or choice pair and no customization
  material for a classic Blood Elf. Beta rebuilt the classic models of races 1-8 (for example `humanmale.m2` is
  version 272 with 16,924 vertices in Beta, version 274 with 3,501 in Retail); it did not rebuild the Blood Elf's.
- So the existing variant system (PR #84) has nothing to offer: a loaded Blood Elf shows the Model selector with Classic
  disabled and the note "The Blood Elf male has only a High Definition model in this client." (female likewise). That
  matches the client. Offering the leftover classic files would need a ChrModel, options and materials the client does
  not have; taking them from MoP Classic or Retail is ruled out.
- Other clients: Retail's Blood Elf is HD (the same leftover classic files, used by one and two creature displays but by
  no ChrModel); MoP Classic's is classic (117170/116921 with its own bytes, layout 1); Classic Era has no Blood Elf.

## 5. Blood Elf customization (Classic Beta)

The Blood Elf HD models use Retail's High Definition option set (texture layouts 121 and 122, 2048 x 1024). The
develop requirement rules, re-run on Classic Beta's own tables with the client's classes (no Demon Hunter), give in
client order (option: offered / total choices; INFERRED from that simulation - the option IDs, their order as the
Appearance panel showed them in the captures, and the totals are PROVEN against the viewer's Beta database):

- **Male (ChrModel 19):** Face 10/20, Skin Color 14/31, Hair Style 21/21, Hair Color 20/21, Facial Hair 13/13, Ears 3/3,
  Horns 1/7, Blindfold 1/12, Tattoo 1/7, Tattoo Color 1/6, Eye Color 11/34, Eyesight 4/4. Eye Style is no player's
  option.
- **Female (ChrModel 20):** Face 10/20, Skin Color 14/30, Hair Style 25/25, Hair Color 20/22, Horns 1/7, Eye Color 11/34,
  Blindfold 1/12, Ears 3/3, Earrings 14/14, Necklace 4/4, Tattoo 1/7, Tattoo Color 1/7, Armbands 4/4, Jewelry Color 2/2,
  Bracelets 7/7, Eyesight 4/4.

The Demon Hunter options offer only their first choice in a client without Demon Hunters. No choice is refused for
race. There is no SD model, so no HD record can be applied to one. The viewer's own check of these lists
(`checkRaceCategories`), run with the final build on Classic Beta 1.60.1.70374, gives exactly these options and counts
for both sexes (PROVEN on 1.60.1.70374; section 9).

## 6. Results after the change

All runs below were made on Classic Beta 1.60.1.70338 with the branch build made before the final review edits
(`wowmodelviewer.exe` SHA-256 `234E2EC0...`: the `.dbd` fix, without the section 2.5 changes), on a hidden desktop,
driving the viewer as a user does (File > Load World of Warcraft, the Browse tree, the slot buttons, the item dialogs'
list and filter box, the remove buttons and the Model selector), and photographed at full resolution. Single categories
were chosen from the keyboard on the category list (the same `OnCheck` / `DoFilter` path); the list's double click did
not reach the viewer on the hidden desktop and was not verified. The final build was driven the same way on Classic
Beta 1.60.1.70374 afterwards (section 9). The pictures named here are in this folder; the full sets of captures and
driver logs stayed outside the repository.

### 6.1 Browsing (Classic Beta, Human male High Definition)

| Action | Result |
|---|---|
| Open each slot | Head 1,312 rows, Shoulder 1,286, Chest 1,730, Gloves 1,524, Legs 1,532, Boots 1,570, Right hand 3,570, Left hand 3,576 (each with "None"); before the change the Chest dialog listed 1 row, "None" (captured), as every slot must with an empty list |
| Search | `Shining` in the Chest dialog: 8 rows, "Shining Silver Breastplate" first; `2870`: 2 rows (the breastplate, and an unnamed item whose ID contains 2870) |
| Switch categories | Chest dialog: Mail alone 391 rows, Plate alone 242 rows; categories offered: NewItem (class 0, subclass 0, where "None" sits), Miscellaneous, Cloth, Leather, Mail, Plate, Cosmetic |
| Select / equip | the eight representative items, one per slot, each through its dialog: every one worn, the log's `Current Equipment` line grows item by item |
| Replace | Head: Lionheart Helm (12640) replaces Wolfshead Helm (8345), its look replacing the wolf head, the other seven kept |
| Remove | Head's remove button: helm off, the other seven kept |
| Classic/HD switch | the dressed Human male to its Classic model (`humanmale.m2`) and back: all eight items kept both ways |
| Load Item Set | 544 rows listed (the 536 item sets, the client's 7 transmog sets and "None"); categories Cloth, Leather, Mail, Plate, Other, Unknown (before: Unknown only); "Sorcerer's Regalia" equips its eight pieces. 121 of the 536 item sets resolve fully, 32 partly and 383 not at all (section 10) |
| View > Item | opens with 5,926 rows (before: refused, "has no item data this viewer can read") |
| A second File > Load World of Warcraft of Classic Beta in the same session | loads again (`Found 18082 items`); the Chest dialog lists 1,730 rows again |

Pictures: `after-chest-dialog.png` (the populated Chest dialog; compare `before-chest-dialog.png`),
`after-human-dressed.png` (the Human male in the eight items), `after-human-classic-dressed.png` (the same on his
Classic model).

### 6.2 Rendering

Each item's display chain and files were read from the client's own tables; every file is installed in Classic Beta's
storage. "Renders" is what the capture after each pick shows.

| ItemID | Slot | ItemModifiedAppearance -> ItemAppearance -> ItemDisplayInfo | Model (Human male) | Textures | Renders |
|---|---|---|---|---|---|
| 8345 Wolfshead Helm | Head | 159273 -> 65843 -> 28987 | 141721 `helm_robe_horde_b_03earthenring_hum.m2` (Blood Elf: 141711 `_bem`, 141710 `_bef`) | 141709 | yes: wolf-head helm, textured |
| 12625 Dawnbringer Shoulders | Shoulder | 160589 -> 66732 -> 25827 | 143246 / 143548 `l/rshoulder_plate_d_02.m2` | 143966 | yes: gold pauldrons on both shoulders |
| 2870 Shining Silver Breastplate | Chest | 157016 -> 64064 -> 9695 | none (body textures) | 151975, 164076, 161396 (female 151974, 164075, 161395) | yes: silver plate chest and upper-arm bands; the belly between plate and shorts is bare skin, as on Retail with the same display (whether the game shows it so: UNSURE) |
| 7938 Truesilver Gauntlets | Gloves | 159058 -> 65664 -> 13172 | none; geoset group 1 | 150085, 154341 | yes: gauntlets with flared cuffs |
| 12619 Enchanted Thorium Leggings | Legs | 160585 -> 66729 -> 22882 | none | 152051, 150201, 164198, 161517, 158844, 156257 (female 158843, 156256) | yes: plate legs with gold ornaments and a copper belly band (that display's own textures) |
| 12021 Shieldplate Sabatons | Boots | 160423 -> 66627 -> 17814 | none; geoset group 1 | 156484, 153434 | yes: plate boots, bare feet hidden |
| 810 Hammer of the Northern Wind | Right hand | 156124 -> 63307 -> 19726 | 145686 `hammer_1h_standard_b_03.m2` | 145692 | in the right hand, yes: a near-white/silver head with a red-brown diamond; its texture holds wood, red, grey and silver-white areas, and Retail renders it the same (whether the game looks this bright: UNSURE) |
| 6898 Orb of Soran'ruk | Left hand | 158710 -> 64086 -> 21597 | 147027 `misc_1h_orb_c_01.m2` | 147014 | yes: a dark purple orb in the left hand |

No asset was taken from another client. For comparison only (not used by Classic Beta), the same eight item IDs were
also worn on Retail.

### 6.3 Blood Elf (Classic Beta)

Characters > NPC Races > Blood Elf > Male / Female load `bloodelfmale_hd.m2` (1100087) and `bloodelffemale_hd.m2`
(1100258). The Model selector shows HD checked and Classic disabled, with "The Blood Elf male has only a High Definition
model in this client." (female likewise); clicking the disabled Classic does nothing. Both wear all eight items, the
helm on each one's own model. Pictures: `after-blood-elf-male.png`, `after-blood-elf-female.png`,
`after-blood-elf-male-dressed.png`, `after-blood-elf-female-dressed.png`, `blood-elf-selector.png`.

The Characters tree is unchanged: Playable Races (12): Dwarf, Gilnean, Gnome, High Order Skyborne, Human, Night Elf,
Orc, Tauren, Troll, Undead, Visage, Windshaper Skyborne; NPC Races (41 nodes) with Blood Elf. Pictures:
`playable-races.png`, `npc-races.png`.

## 7. Performance

| Measure (Classic Beta unless stated) | Before | After |
|---|---|---|
| Item list stage at load (`[clientload] item list`) | 0.1 ms (nothing to read) | 40-65 ms |
| Database stage, cached database | 21-34 ms | 59-86 ms |
| Database stage, first load after the update (cache rebuilt) | 3,616 ms | 4,181 ms |
| Whole client load, cached database | 4,472 ms headless; 5,330-6,616 ms in GUI runs | 4,645 ms headless; 4,710-5,650 ms in GUI runs |
| Opening a slot dialog until its list is complete | - | 89-154 ms (Right hand, 3,570 rows: 127-130 ms); Retail 12.1.0.69933 for comparison: Right hand 25,690 rows 714-751 ms |
| Filtering a dialog (one keystroke's work) | - | 9-31 ms; Retail 12.1.0.69933 Right hand 179 ms |
| Choosing an item until the scene is applied | - | 65-139 ms |
| Viewer working set: after load / character loaded / Right hand dialog open / after 8 items | - | 1,651 / 1,840 / 1,840 / 1,841 MB |

The fix's only load-time cost is the item list stage (+40-65 ms); whole-load totals of the two builds overlap on this
busy machine. The item list is built once per client load from the cached database, as it is for every client; nothing
is decoded by scanning at startup. A `.dbd` change rebuilds each client's cached database once: Classic Beta about 4 s, Classic
Era about 2 s, MoP Classic about 6-7 s, Retail about 17-21 s on this machine.

## 8. Tests

`-customizationtest` (headless, against the installed client) gains two groups of checks in
`Source/wowmodelviewer/app.cpp`, run for every client. Their expected values were read from each client's DB2 files by
a reader apart from the viewer's loader.

- **`checkItemCatalogue`**
  - The client's Item and ItemSparse were read; a table on this computer that comes back empty fails (Classic Beta's
    did).
  - The list starts with "None", holds each item once, every one with an inventory type.
  - The list's size and each of the 14 slots' counts, for the build the expectation names (another build: SKIP).
  - Eight representative items per client: name, inventory type, class/subclass, sheathe.
  - On the Human male, through the dialogs (`CharControl::selectItem` as the slot button calls it, the filter box's
    text event, the list's selection event; for the category list, the double-click handler's steps are run directly:
    check states set, then `DoFilter`): every slot's dialog lists "None" and as many rows as the in-memory list holds for
    that slot; the filter finds each representative item by name and by ID; its category alone, named as the independent
    reader names it, lists exactly that category's items; choosing it wears it with the expected display (and loads its
    models).
  - Replacing: another row with one appearance replaces the item with that row's display and models; a row the client
    gives no appearance shows no look (where the slot lists one); "None" takes it off. The other slots keep theirs
    throughout. A weapon replaced by a Right hand item without an appearance leaves the hand empty and open.
  - Classic Beta only: the Blood Elf male and female wear all eight pieces, the helm on each one's own model (141711 /
    141710), and the Human male's Classic model keeps them across the Classic|HD switch (helm 141721).
- **`checkRaceCategories`**
  - The Blood Elf is listed once by name, with its category, model files and texture layouts per client (Retail and
    MoP Classic Playable, Classic Beta NPC with no variant pair, Classic Era none).
  - Classic Beta's Playable list: the races its data makes playable (1-8, 95, 96), checked apart from the forms 23 and
    75, which a separate line reports as listed by the viewer's rule against the client's data (section 10).
  - Classic Beta's Blood Elf male and female options and offered choices (section 5).
- The Retail test names its client at the start and counts its skips in the result line, as the Classic one does.

The list size and per-slot counts are judged only on the build their expectation names; on a later build they are
reported as a SKIP line. The other expectations follow this test's existing convention: a failure on a later build
means the client's data moved and is to be looked at. No existing assertion or expected value was changed.

With develop's definitions, Classic Beta's Item and ItemSparse are left unread (both empty in every develop load), which
the first check rejects; this was not run as a separate experiment with the final build.

| Client | develop `9da5343c` | branch build before the final review edits (`234E2EC0...`) | final build |
|---|---|---|---|
| Classic Beta | PASS 192 / 0 failed / 1 skipped (1.60.1.70338) | **PASS 251 / 0 / 1** (1.60.1.70338) | **PASS 254 / 0 / 2** (1.60.1.70374; the counts pinned to 1.60.1.70338 skipped, with the same numbers) |
| Classic Era 1.15.9.70003 | PASS 37 / 0 / 3 | PASS 92 / 0 / 3 | PASS 93 / 0 / 3 |
| MoP Classic 5.5.4.70268 | PASS 49 / 0 / 2 | PASS 104 / 0 / 2 | PASS 105 / 0 / 2 |
| Retail | PASS 92 / 0 (12.1.0.69933) | PASS 147 / 0 (12.1.0.69933) | PASS 147 / 0 / 1 (12.1.5.70391; the item count pinned to 12.1.0.69933 skipped) |

The other skips are pre-existing (data the client does not have, such as a Demon Hunter NPC). Final build: the
probe build (`wowmodelviewer.exe` SHA-256 `E6B9B21E6B74D0D0...`, `wow.dll` `CFF44FDB32D8CD73...`) for Classic Era, MoP
Classic and Retail, and the deployed build of the same commit (`9D4C771F7E6D03F4...`, `wow.dll` `54E6A24ACF028D2A...`)
for Classic Beta 1.60.1.70374, Classic Era and MoP Classic; `core.dll` and the plugins are develop's.

## 9. Regression and client switching

**Other clients read what they read before.** Each client's per-table reading state (layout, rows, locked rows) in a
freshly built database was compared with develop's: Retail and Classic Era identical; MoP Classic identical except one
more decrypted row in four tables (an encryption key the viewer learned in the meantime: environment, not code);
Classic Beta differs only in Item and ItemSparse, now verified. Item lists: Classic Era 15,433, MoP Classic 65,083,
Retail 137,501 (12.1.0.69933), unchanged.

**The race lists are unchanged.** The Characters tree's Playable and NPC lists, read from the running viewer, are
identical with develop and with the branch for all four clients: Retail 28 / 25, Classic Era 8 / 1, MoP Classic 14 / 10,
Classic Beta 12 / 41 (nodes). Blood Elf: Playable in Retail and MoP Classic, NPC in Classic Beta, absent in Classic Era.

**Client switching** (one session each, File > Load World of Warcraft between clients, branch build):

| Session | First client | Classic Beta | Back on the first client | New error kinds |
|---|---|---|---|---|
| Retail -> Classic Beta -> Retail | items 137,501, Chest 12,285 rows, Blood Elf Playable | items 18,082, Chest 1,730, Blood Elf NPC | items 137,501, Chest 12,285, Blood Elf Playable | none |
| Classic Era -> Classic Beta -> Classic Era | items 15,433, Chest 1,427, no Blood Elf | 18,082, 1,730, NPC | 15,433, 1,427, no Blood Elf | none |
| MoP Classic -> Classic Beta -> MoP Classic | items 65,083, Chest 6,385, Blood Elf Playable | 18,082, 1,730, NPC | items 65,083 on the return load; the Chest and tree steps of the return were not recorded (the run was cut off) | not compared |

"New error kinds" compares the session's log ERROR lines with the same clients' single loads. The viewer's working set
returned to its single-client level after each switch (for example Classic Era 808 MB, 1,844 MB on Classic Beta,
878 MB back on Classic Era). A second load of the same client (Classic Beta twice in one session) also worked.

**The final build** (with the section 2.5 changes and the last test edits) passes `-customizationtest` on Classic Era
(93 passed, 0 failed, 3 skipped), MoP Classic (105 / 0 / 2) and Retail - which the game launcher updated overnight to
12.1.5.70391 (147 / 0 / 1: the item-count check is pinned to 12.1.0.69933 and is skipped, everything else passes,
items 137,665). On all three, choosing a Head item without an appearance shows no helm (before the fix the replaced
helm's display stayed: `0:1020/28987` on Classic Era, `0:1020/18392` on MoP Classic), and a Right hand item without
one, chosen over the hammer, leaves the hand empty and open.

**Classic Beta 1.60.1.70374.** On 2026-10-14 the game launcher updated Classic Beta to 1.60.1.70374
(`WOW-70374patch1.60.1_ForeverBeta`; for the update's duration it was not in `.build.info`). The final build was then
run on it:

- Its Item.db2 and ItemSparse.db2 keep layouts `9A2A4834` and `6FCC3191` (9,117 and 19,273 records, as in 70338), so
  the definitions apply unchanged: `[schema] "12.0" for "1.60.1.70374" : 54 tables verified ... 0 not read`,
  `Found 18082 items`, the same per-slot counts.
- `-customizationtest`: PASS 254 passed, 0 failed, 2 skipped (no Demon Hunter NPC; the list-size check pinned to
  1.60.1.70338, whose numbers are nevertheless equal). The log names the client (`[clientprofile] ... product=
  wow_classic_beta version=1.60.1.70374`). This includes the no-appearance replace (`item 1020, display 0, 0 models`),
  the open hand (`item 751, display 0, 0 models, hand closed 0`), both Blood Elves dressed with their own helm models,
  the Classic model keeping the items, the race checks and the Blood Elf option lists.
- A GUI run (the same driver as section 6): 119 checks passed, 0 failed - Chest 1,730 rows, the eight items worn, Head
  1020 and Right hand 751 ("Deprecated Wolf Femur") chosen over the helm and the hammer, the remove button, the Blood
  Elf male's selector (Classic disabled, the note) and the Blood Elf male dressed. `after-no-appearance.png`: the Human
  male dressed; then with 1020 on the head (no helm); then with 751 in the right hand (no hammer, the hand open).

## 10. Known limitations and pre-existing findings

None of these is changed by this branch; each was found during the investigation and is reported for a decision.

- **Unnamed items.** 10,038 of Classic Beta's 18,082 listed items have a display but no name in the client's own
  ItemSparse (8,425 of them are copy-table rows of Item). The viewer lists them as "Unnamed item", as it already does
  for Retail (26,038), MoP Classic (644) and Classic Era (259); they are found by item ID in the filter. Names from
  another client are not used. The client's server hotfix cache (`_classic_beta_\Cache\ADB\enUS\DBCache.bin`, 4,754
  Item and 4,900 ItemSparse records) might name some of them; the viewer does not read hotfix caches for any client.
- **Item sets with unnamed pieces.** Load Item Set takes only pieces that have a name (`ItemSetCatalog.cpp:50`; the
  comment at :54 says "unnamed sources cannot be loaded safely"). In Classic Beta 121 of the 536 item sets resolve fully, 32 partly and 383
  not at all, because their pieces have no ItemSparse row in this client (for example "Arcanist Regalia", items
  16795-16802). The rule is the same for every client and was not changed.
- **Rows in encrypted sections.** 71 Item records sit in encrypted sections; 3 of them (IDs 285352-285354) use keys
  this viewer has and are read, the other 68 are left out. The 80 ItemSparse records in sections 1-10 are never read,
  because the loader reads only section 0 of a sparse table (`wdc3file.cpp:494`), which also reports 0 locked rows for
  it; so the 3 decrypted Item rows have no name. Pre-existing, as in every client; unchanged.
- **Gilnean and Visage under Playable Races on Classic Beta.** The hard-coded exception at `RaceInfos.cpp:101` lists
  races 23 and 75 as playable in every client, although Classic Beta's data marks them (and their base races Worgen
  and Dracthyr) not playable. A data rule (a form race is playable when a playable race names it as its
  UnalteredVisualRaceID) would fix Beta but move Gilnean to NPC Races and Visage 76 to Playable Races in Retail, so it
  was not applied here.
- **A classic character file opened through a Models search.** `WoWModel::initRaceInfos` (`WoWModel.cpp:924-944`)
  gives a `character/<race>/<sex>/` file that is no ChrModel's model the race's High Definition row by its path. For
  Classic Beta's (and Retail's) leftover `bloodelfmale.m2` this would put the HD options and texture layout on the
  classic mesh. INFERRED from code; not run. Unchanged.
- **Same-name races share one tree node** (`filecontrol.h:70-76`): Pandaren 24/25/26, Dracthyr 52/70, Earthen 84/85,
  Haranir 86/91.
- **Item quality colours are never loaded** in any client (`ItemRecord` sets quality 0, `database.cpp:16-37`).
- **Character > Load Start Outfit** queries a `CharStartOutfit` table no `database.xml` declares, so its class list is
  empty in every client; Classic Beta does not ship that table (its starting gear is CharacterLoadout).

## 11. Acceptance

| # | Requirement | Result | Evidence |
|---|---|---|---|
| 1 | Environment established (product, build, schema, table reads, storage, code) | PASS | section 1 |
| 2 | Equipment workflow traced, counts per stage, compared with working clients | PASS | section 2.2 |
| 3 | Cause classified | PASS: schema | section 2.3 |
| 4 | Fixed without lifting item restrictions, fabricated items or another client's data | PASS | section 2.4 |
| 5 | Open, list, search, select, equip, remove, replace, switch categories; all slots | PASS (Classic Beta 1.60.1.70338) | sections 6.1, 8 |
| 6 | Rendering of representative items, per-item table, no cross-client assets | PASS | section 6.2 |
| 7 | Playable categories traced from authoritative race data | PASS | section 3 |
| 8 | Blood Elf Male/Female under Playable Races | **UNSUPPORTED by the client's data**: Classic Beta 1.60.1.70338 marks race 10 not playable in every source; no exception added | section 3.2 |
| 9 | SD/HD verified from metadata and model data | PASS | section 4 |
| 10 | Blood Elf SD (classic) model selectable | **UNSUPPORTED**: Classic Beta defines no classic Blood Elf character model | section 4 |
| 11 | Blood Elf HD model | PASS (both sexes load; the selector offers HD only) | section 6.3 |
| 12 | Customization (skin, face, hair, colours, facial hair, textures, geosets), no HD records on SD | PASS for HD (options and offered choices checked); no SD model exists | section 5 |
| 13 | Equipment on both Blood Elf sexes and on a Classic (SD) variant | PASS (Blood Elf HD both sexes; Human male's Classic model) | sections 6.3, 8 |
| 14 | Regression: Retail, Classic Era, MoP Classic, Classic Beta | PASS | sections 8, 9 |
| 15 | Client switching without stale data | PASS for Retail and Classic Era round trips; MoP Classic round trip partly recorded | section 9 |
| 16 | Performance: no full-scan decoding at startup; times and memory measured | PASS | section 7 |
| 17 | Deterministic tests, no weakened assertions | PASS on all four clients with the final build | section 8 |
| 18 | Final build on Classic Beta | PASS on 1.60.1.70374 (self-test and GUI run); its item layouts are unchanged | section 9 |
| 19 | Scope: no World Viewer, terrain, ADT/WDT, map streaming, unrelated M2/WMO rendering, installer or UI appearance change | PASS | section 12 |

## 12. Files

| File | Change |
|---|---|
| `bin_support/dbd/Item.dbd` | Classic Beta layout `9A2A4834` and its COLUMNS entries |
| `bin_support/dbd/ItemSparse.dbd` | Classic Beta layout `6FCC3191` and its COLUMNS entry |
| `Source/games/wow/WoWItem.cpp` | `setId` starts each item from its own appearances only; `refresh` reopens an emptied hand; a saved display 0 is not forced (section 2.5) |
| `Source/wowmodelviewer/modelviewer.cpp` | FBX equipment restore: a hand item without a display needs no model |
| `Source/wowmodelviewer/app.cpp` | `-customizationtest`: `checkItemCatalogue`, `checkRaceCategories`; the Retail test names its client and counts its skips |
| `docs/classic-beta/equipment-and-blood-elf-fixes.md` | this report |
| `docs/classic-beta/*.png` | the pictures named above, full resolution (2576 x 1408 frames; dialog and tree windows at their own size) |

`before-chest-dialog.png` was taken with develop `9da5343c`; `after-no-appearance.png` with the final build on Classic
Beta 1.60.1.70374 (three 420 x 650 crops of full frames, side by side, unscaled); every other picture with the branch
build before the final review edits, on Classic Beta 1.60.1.70338; all on a hidden desktop. `playable-races.png` is byte-identical to the same
picture taken with develop.
