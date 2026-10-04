# Load Item Set: armor categories and transmog alternatives

The set picker reuses `CategoryChoiceDialog`, including its appearance, checked-by-default categories, multiple selection and double-click-to-isolate behavior. Only categories represented in the loaded data appear, as in individual item pickers. Checked categories are combined with OR, then intersected with the existing name search. The synthetic `None` row stays first and remains visible even with no matching sets; choosing it explicitly clears equipment. No class restrictions are applied.

Classification uses `Item.ClassID`, `SubclassID` and `InventoryType`, joined to every nonzero `ItemSet.ItemID1` through `ItemID17`. The supported 9.2, 10.0, 10.1 and 12.0 database profiles all expose these fields. It does not depend on localized names or the displayable-item cache, which excludes unnamed items.

- Cloth, Leather, Mail and Plate correspond to armor class 4, subclasses 1â€“4, in head, shoulder, chest/robe, waist, legs, feet, wrist and hand inventory slots.
- Cloaks, jewelry, weapons, shields, held items, shirts, tabards and other accessories do not add a material, even if their subclass is Cloth.
- Mixed means at least two distinct principal materials are positively identified.
- Other means all referenced pieces have classification data, but none is armor in those principal materials and slots.
- Unknown means an empty set or missing classification data for any referenced nonzero item. A partially known single-material set remains Unknown because a missing piece could introduce a second material. Two known materials still establish Mixed even if other data is missing. Zero IDs are empty positions, not missing records.

The catalog combines `ItemSet` and `TransmogSet` / `TransmogSetItem`. Source and ID are kept distinct even when IDs or localized names coincide; the list suffix identifies the source record. Unnamed sets are omitted. Redundant ungrouped fragments are omitted only under the source-coverage rule below; other named partial/technical records remain available. Sets are never merged by name. Different ItemIDs are always retained, even when their appearances happen to match.

The right column shows each supported equipment slot as a static label. One ItemID has a static item name; multiple ItemIDs have a closed dropdown. The selected item's distinct `ItemAppearanceID` values appear in a second selector only when there is more than one. Transmog choices are restricted to sources explicitly referenced by that set; appearances from other sets are not introduced. Traditional ItemSets have no appearance membership, so they offer the appearances of their referenced items. Chest and robe use the same character slot; weapons, cloaks, shirts and tabards use their normal equipment slots. Jewelry and other inventory types with no rendered WMV slot are not equipped.

Initial transmog choices prefer `TransmogSetItem.Flags & 1`, then ascending modifier/source ID and ItemID. This is an explicit deterministic heuristic, not a claim that WMV reproduces the native `GetSetPrimaryAppearances` API. Nonpreferred sources remain selectable. For traditional ItemSets, `OrderIndex == 0` is preferred. Duplicate appearance IDs within an ItemID are collapsed. The selected appearance is applied without replacing the ItemID with a display-only NPC item.

Missing item metadata, missing appearance/display records or unnamed items that the equipment cache cannot load make the catalog entry incomplete. A warning appears in the panel and unresolved options are omitted; known options remain usable. Such entries are Unknown unless two known materials already establish Mixed. Empty/unresolved entries with no usable slots do not clear current equipment. The explicit None row clears equipment.

Opening runs at most four queries (three bulk reads and one schema-column check) and builds all categories/options in memory. There are no SQL queries when typing or changing checkboxes. Filtering restores the same selection and slot choices if still visible; otherwise it clears the details panel and selection without changing equipment or selecting a replacement. Explicitly selecting a set clears previous equipment and applies one initial option per supported slot, examining all 17 legacy positions. Changing an item or appearance affects only that slot. The individual-item picker and main equipment UI are unchanged.

All four shipped database profiles include the two new tables. Their bundled WoWDBDefs layouts resolve fields by the actual client layout hash; schema version 15 invalidates older caches. A database without transmog tables still supplies traditional sets. No class filter or character-class restriction is applied.

## Automated verification

`scripts/tests/item-set-filters` builds two standalone hidden wxWidgets harnesses. Configure with `WMV_ROOT` and `QT_ROOT`, build Release x64, and put the matching wxWidgets/Qt DLL directories on PATH:

```powershell
cmake -S scripts/tests/item-set-filters -B <test-build> -A x64 -DWMV_ROOT=<checkout> -DQT_ROOT=<Qt-installation>
cmake --build <test-build> --config Release
<test-build>/Release/item-set-filters.exe
<test-build>/Release/item-set-integration.exe <runtime>/wowdb.sqlite
```

The first compiles the production dialog methods and classifier, replacing only application dependencies and unused network importer callbacks. It tests the seven categories, relevant slots, accessories, incomplete and empty sets, combinations, search, zero matches, restoration, selection preservation, no equipment callbacks during filtering, filtered ID mapping, and individual-item picker behavior.

The integration harness compiles the production catalog and two-column dialog. It reads SQLite without modifying it, selects every usable set through a category-filtered row, checks all emitted item/appearance/slot requests, exercises both alternative selectors, zero results, restoration and callback suppression. Filtering uses no queries. Run it both against a database containing transmog tables and against a legacy-only database. Known real cases include Hateful Chain (shoulders 41215), Sabellian (chest alternatives) and Cosmic Gladiator (same item, different appearances).

`item-set-appearance.exe` compiles the actual `WoWItem::setAppearanceId` method with mocked asset loading. It tests exact appearance lookup, retained ItemID, repeated choices, a stale legacy level after modifier changes and rejection of invalid/missing appearances. These tests do not validate rendered clothing or actual game-asset loading. No visible application is launched; visual verification remains a separate check.

### Previous filter-only validation

Validation on 2026-10-03: Release x64 build passed; 194 synthetic/dialog checks passed; all 1,008 sets in the local read-only database passed ID and piece-request checks. Categories were Cloth 231, Leather 240, Mail 202, Plate 236, Mixed 0, Other 63, Unknown 36. Mixed was exercised with synthetic data. There were 36 nonzero piece references after position eight. Opening took approximately 183 ms on this machine. All four shipped schemas were checked. The local installation was updated and all seven copied C++ binaries verified by SHA-256, with a backup retained outside the repository. Visual verification and actual game-asset loading remain pending.


### Transmog panel validation — 2026-10-03

- Both the isolated PR branch and daily checkout built successfully in Release x64.
- The new XML/DBD definitions extracted 5,141 TransmogSet records and 74,760 TransmogSetItem records directly from local client 12.1.0.69933, with Hateful Chain membership checked.
- A disposable snapshot combining those extracted tables with the existing game database produced 1,008 named traditional sets and 5,095 named transmog records. Catalog construction took about 1.0 seconds and three queries; unnamed transmog records were omitted.
- The complete hidden UI run passed 192,891 assertions: 5,838 usable filtered set selections, 18,697 item changes and 1,196 appearance changes. Unresolved/nonrendered sets remained visible without clearing equipment. A final smoke run after batching panel layout also passed.
- The legacy-only database passed 24,076 assertions, with 947 usable set selections and two opening queries. The 194 existing category/individual-picker regressions, catalog fixtures and 11 production equipment/appearance-method scenarios also passed.
- The daily installation was updated with seven C++ binaries, four database profiles and two DBD files; all 13 files were SHA-256 verified and prior files backed up outside the repository. Unity's protocol and binaries are unchanged. The application was closed for replacement and not reopened.
- Actual rendered game assets and visual layout still require user inspection. Hidden tests record equipment requests and mock asset loading; they do not claim visual validation or confirmation against the native WoW primary-appearance API.

Compact panel correction: the original picker client width and height are retained; a second equal-width column doubles the client width. Both item and appearance alternatives use closed dropdowns. Hidden layout checks measured 223x635 -> 446x635 and verified unchanged list width after selections, with 31 item and 11 appearance changes passing after the control replacement. The daily executable was rebuilt and installed with hash verification.


Picker layout follow-up: Select all / Select none below the categories use the existing checkbox filtering and never equip. The search has extra separation; the item heading uses the same 10px gap above its list as the category heading. The existing separator and OK button are now in a full-width footer. Initial columns remain 223px each on the test system; resizing shares extra width equally, and the item list expands vertically. Hidden tests verified 446x635 -> 646x755, the left list 203x303 -> 303x423, right-aligned OK, bulk category actions and alternative callbacks. Both Release builds and installed executable hash verification passed.

User visual verification: Mauricio confirmed Sabellian's Battlegear alternatives work in Unity. Southsea Cruise exposes an existing chest/legs shared-geoset override; that renderer issue remains unresolved and is separate from these layout changes.


### Redundant transmog fragments — 2026-10-04

`TransmogSetGroupID` and `ClassMask` are now imported in all client profiles. An ungrouped record is omitted only if it lacks at least one of head/shoulder/chest/legs/feet, has complete resolvable data, and all its exact ItemModifiedAppearance source IDs occur among complete variants in one catalog group with the same localized name and class mask. Every donor must itself have all five core slots and complete data. Coverage may span variants of that one group; sources from different groups are never combined. ClassMask is used only to establish the family relationship, not to filter by the current character's class. No guessed flag values or name-only merging are used.

Grouped records, full outfits, fragments with exclusive sources, incomplete records, and records with different class/name relationships remain available. Appearance IDs alone do not establish redundancy: all original source IDs are compared before per-item appearance deduplication. When metadata columns are unavailable in an older standalone database, the catalog falls back to retaining its fragments. Filtering/search still perform no database work.

The local Astral Gladiator's Chain Armor case keeps complete records 4102/4103/4109/4115/4116/4122 and omits fragments 4069–4083. Cosmic Dreadplate keeps 2352/2353 and omits 2420–2424. The real-data catalog contains 4,360 named transmog records, down from 5,095, plus all 1,008 traditional sets. Synthetic coverage fixtures verify same-group union, exclusive sources, different class/name/group relationships, incomplete sources and full ungrouped variants. Schema 15 requires a one-time cache rebuild after installation.

Full hidden validation after fragment filtering passed 233,795 assertions: 5,103 usable filtered selections, 17,597 item changes and 1,089 appearance changes. Both Release x64 builds passed. Shipped XML/DBD extraction from client 12.1.0.69933 confirmed Astral fragment 4074 has class mask 4/group 0 and variants 4103/4116 have class mask 4/group 284. Eleven installed files were backed up and SHA-256 verified. No application was reopened and no commit or push was made.
