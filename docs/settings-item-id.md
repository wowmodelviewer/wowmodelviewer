# Settings layout and item-ID selection

## Behavior

- General uses a vertically scrolling window. Its natural content size determines
  the initial floating size, bounded by the active display's work area.
- The Settings pane is resizable, the notebook fills it, and path/URL fields grow
  with the available width. A minimum width keeps labels and buttons readable.
  A restored old fixed-size pane is migrated to the new size and resize behavior.
- Item pickers search their stable ItemIDs as well as displayed labels. Showing
  IDs changes presentation only. Existing name/wildcard, slot and category filters
  still apply; a numeric match does not bypass a category or slot restriction.
- The catalog retains existing named items. Empty, whitespace or missing names
  are admitted when ItemModifiedAppearance resolves through ItemAppearance to a
  nonzero, existing ItemDisplayInfo. Duplicate modifiers do not duplicate items.
  Such items are labelled `Unnamed item`, optionally followed by `[ItemID]`.
- Standalone item loading accepts a valid model without an external replacement
  skin. This supports weapons whose M2 material definitions name their own textures.

## Repeatable hidden tests

Configure and build `scripts/tests/settings-item-id` with CMake and the same
wxWidgets and Qt installations used by WMV:

```text
cmake -S scripts/tests/settings-item-id -B <test-build> -A x64 -DWMV_ROOT=<checkout> -DQT_ROOT=<Qt-installation>
cmake --build <test-build> --config Release
<test-build>/Release/settings-item-id.exe <runtime>/dbcache/wowdb-wow-<build>-<hash>.sqlite
```

Place wxWidgets and Qt DLLs on the test process's PATH. The SQLite database is
opened read-only. The harness compiles production layout, catalog query, item
labels, slot filtering, picker construction and dialog filtering/selection code.
Settings persistence/folder actions, network imports and the final equipment
receiver and theme choice are stubbed. The picker's final Show and Move are suppressed. No window
is shown or focused. A separate in-memory fixture covers invalid appearance links,
missing/blank names, duplicate modifiers and non-equipment inventory types.

The native hidden AUI frame is checked for a resize border and sufficient initial
space including window decorations. Size events exercise the real layout and
scroll handlers. Tests shrink the panel, scroll to Apply, and enlarge it again.

## Reproduction and validation

The isolated current-develop branch compiled successfully in Windows Release x64
with wxWidgets 3.3.3.

With a Retail database containing ItemID `153575`, search either hand's equipment
picker for that ID with **Display Items/NPCs' IDs in lists** both off and on.
The row must be `Unnamed item` / `Unnamed item [153575]`. Disabling its category
must hide it, and the head slot must not admit it. Name and wildcard searches
remain available. A picker with a single category retains its existing None row.

The regression case is Jaina's staff: ItemID `153575`, DisplayID `185141`,
FileDataID `1717765`, model
`item/objectcomponents/weapon/staff_2h_jaina_d_01.m2`. Its ItemSparse name is empty
in Retail `12.1.0.69933`; the model supplies intrinsic textures rather than an
external replacement skin. It should load both as equipment and as a standalone
item.

On 10 October 2026, the hidden tests passed 137,570 assertions against 137,501
catalog rows, including the synthetic invalid/missing appearance cases. With
wxWidgets 3.3.3 the initial Settings window was 440 × 684 on the test display;
Apply and the ID checkbox fit. Enlarging expanded the notebook and text fields;
shrinking to height 350 made Apply reachable by scrolling. The native AUI frame
had a resize border.

The original integration build (wxWidgets 3.2) also passed these tests and its
existing transmog regressions. With real game files, it loaded the staff alone
and equipped on Tyrande. An independent FBX reader verified 1,072 staff triangles,
three materials, texture references and attachment to Tyrande's hand bone.
The reporter installed that build and confirmed that the fixes worked.

The isolated branch's test fixture was copied from the same Retail cache, with
only `Item.SheathType` renamed to the current schema's `Item.SheatheType`.
The source cache was not modified. No game assets or database are included here.
Alternate DPI/monitor configurations, the new upstream UI's rendered appearance,
and animation of the equipped staff were not covered by the hidden tests.
