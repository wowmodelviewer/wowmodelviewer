# Load Item Set: armor categories

The set picker reuses `CategoryChoiceDialog`, including its appearance, checked-by-default categories, multiple selection and double-click-to-isolate behavior. Only categories represented in the loaded data appear, as in individual item pickers. Checked categories are combined with OR, then intersected with the existing name search. The synthetic `None` row stays first and remains visible even with no matching sets; its existing loading behavior is unchanged. No class restrictions are applied.

Classification uses `Item.ClassID`, `SubclassID` and `InventoryType`, joined to every nonzero `ItemSet.ItemID1` through `ItemID17`. The supported 9.2, 10.0, 10.1 and 12.0 database profiles all expose these fields. It does not depend on localized names or the displayable-item cache, which excludes unnamed items.

- Cloth, Leather, Mail and Plate correspond to armor class 4, subclasses 1–4, in head, shoulder, chest/robe, waist, legs, feet, wrist and hand inventory slots.
- Cloaks, jewelry, weapons, shields, held items, shirts, tabards and other accessories do not add a material, even if their subclass is Cloth.
- Mixed means at least two distinct principal materials are positively identified.
- Other means all referenced pieces have classification data, but none is armor in those principal materials and slots.
- Unknown means an empty set or missing classification data for any referenced nonzero item. A partially known single-material set remains Unknown because a missing piece could introduce a second material. Two known materials still establish Mixed even if other data is missing. Zero IDs are empty positions, not missing records.

Opening performs one indexed bulk query and computes categories once. Typing and changing checkboxes operate only on the dialog's in-memory choices. Filtering restores the same original selection if still visible, otherwise clears the selection; it suppresses equipment callbacks during rebuilding and never selects a replacement. Explicit selection continues through the original-index-to-set-ID mapping and existing equipment loader. Loading now visits all 17 positions instead of truncating at eight, clears previous equipment and refreshes through the existing path.

## Automated verification

`scripts/tests/item-set-filters` builds two standalone hidden wxWidgets harnesses. Configure with `WMV_ROOT` and `QT_ROOT`, build Release x64, and put the matching wxWidgets/Qt DLL directories on PATH:

```powershell
cmake -S scripts/tests/item-set-filters -B <test-build> -A x64 -DWMV_ROOT=<checkout> -DQT_ROOT=<Qt-installation>
cmake --build <test-build> --config Release
<test-build>/Release/item-set-filters.exe
<test-build>/Release/item-set-integration.exe <runtime>/wowdb.sqlite
```

The first compiles the production dialog methods and classifier, replacing only application dependencies and unused network importer callbacks. It tests the seven categories, relevant slots, accessories, incomplete and empty sets, combinations, search, zero matches, restoration, selection preservation, no equipment callbacks during filtering, filtered ID mapping, and individual-item picker behavior.

The second additionally compiles production `selectSet()` and the `UPDATE_SET` branch, omitting screen placement and showing the window. It opens the supplied SQLite database read-only, selects every real set through its category-filtered row and checks its ID, all 17 equipment requests, clearing and refresh flow. It records requests at `tryToEquipItem` instead of loading game assets. It also asserts one opening query and no queries or equipment callbacks from filtering.

These tests do not validate visual layout, rendered clothing or actual game-asset loading. Manual in-app inspection remains a separate check; no visible application is launched by the harnesses.

Validation on 2026-10-03: Release x64 build passed; 194 synthetic/dialog checks passed; all 1,008 sets in the local read-only database passed ID and piece-request checks. Categories were Cloth 231, Leather 240, Mail 202, Plate 236, Mixed 0, Other 63, Unknown 36. Mixed was exercised with synthetic data. There were 36 nonzero piece references after position eight. Opening took approximately 183 ms on this machine. All four shipped schemas were checked. The local installation was updated and all seven copied C++ binaries verified by SHA-256, with a backup retained outside the repository. Visual verification and actual game-asset loading remain pending.
