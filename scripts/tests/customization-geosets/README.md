# Customization geosets

The harness runs the production `CharDetails::applyCustomizationGeosets()`, `WoWModel::setGeosetDisplayById()`, `WoWModel::showGeoset()` and the default visibility rule of `WoWModel::refresh()`, cut out of `CharDetails.cpp` and `WoWModel.cpp` at configure time, over real client records. It needs no game install and no WMV build.

`fixtures.h` holds 22 character models of four clients (Retail 12.1.5.70391, MoP Classic 5.5.4.70268, Classic Era 1.15.9.70003, Classic Beta 1.60.1.70374): the geoset id of every submesh of the model's first `.skin`, its customization options and choices in client order, and every geoset element of those choices with its `GeosetType` and related choice. They were read from each client's own DB2 files and model files by a reader independent of the viewer, and the rows were compared with the viewer's imported database for the same clients: equal. The models are the Undead male and female of every client (on Classic Beta also the Classic models, ChrModel 265 and 266), and for the cases the rule touches elsewhere a Human male, Blood Elf female, Draenei female, Vulpera male, Gilnean female, Mag'har Orc male, Dwarf male and Night Elf male (Retail), a Draenei female and Pandaren female (MoP Classic), a Troll male (Classic Era) and a Human female (Classic Beta).

What a selection must show is worked out in `regression.cpp` from the records alone: a geoset element sets the value of its geoset group (`GeosetType`; a `GeosetID` of 0 is none) and the model draws one variant per group, so every variant an active choice sets shows (an element applies when its related choice is 0 or active) and nothing else in that group does; a variant some choice names, in a group no active choice sets, is hidden; everything else keeps the default rule (geoset 0, ids ending in 01 and 32xx shown; 17xx, 35xx and 12xx hidden). The cases:

- the Undead male's jaw for each jaw choice on every client, written out (Retail Slackjawed shows 103, Bonejawed the two submeshes of 202, and so on), and the Undead female's, which the rule does not change;
- every choice of every option of every model, from the first-choice selection, against the records' picture;
- other races, each a geoset the old rule left on beside the chosen variant: the base ear 701 beside the chosen ear (Human, Blood Elf), the base face 3201 beside a face shape, the base horns 2401 (Draenei), the snout 4101 (Vulpera, a copy of Short), the sideburns 201 under Sideburns None (Mag'har Orc, whose None sets 200), the eyes under Eye Color Sockets (Undead, 3300), the eye geoset an Eyesight choice gates, the stray hair 32 beside the chosen hair (Gilnean female; Human female on Classic Beta), the base horns 101 (MoP Classic Draenei);
- an element whose `GeosetID` is -1 (Retail and Classic Beta `ChrCustomizationGeoset` 12028, type 14, the Dracthyr drake's Breechcloth None) is none for its own group, 14, and leaves group 13 alone (a synthetic model);
- the transition sequence on the Retail Undead male: jaw A, B, C, A again (the picture A gave), another facial option (it stays), another jaw, the start selection again (the start picture), another character and back, and the Classic Beta HD and Classic models each from their own records; every step equal to a fresh character given that selection and to the records. The production refresh rewrites every flag before the customization pass, so these steps show the pass is a function of the selection; `CharDetails::reset`, saving and loading and `ModelViewer::SwitchCharacterVariant` themselves are run by `-customizationtest`;
- every order of four jaws followed by a fifth: one final picture;
- on every model, 2000 changes of a random option to a random choice (a fixed-seed generator) with a reset every 250, each picture equal to a fresh character's and the records'.

Run against develop 3e6a566c's `applyCustomizationGeosets` (its elements were plain (id, related) pairs, served to it in that form) it fails 77 of 98 checks: every Undead male jaw shows 101 beside the chosen one, and each other-race case keeps its default variant; the Classic Era Undead and every Undead female pass, as before. With the group rule, all 98 pass.

Configure and build this directory as a standalone CMake project outside the source tree, passing `WMV_ROOT` (the checkout whose sources are tested) and `QT_ROOT` (the Qt the viewer builds with; only Qt5Core is used). Put Qt's `bin` folder on `PATH` to run it.

```powershell
cmake -S <this-directory> -B <build-directory> -A x64 -DWMV_ROOT=<checkout> -DQT_ROOT=<qt>
cmake --build <build-directory> --config Release
<build-directory>/Release/customization-geosets.exe
```

Each case prints `[customization-geosets-test] PASS|FAIL <case> -- <detail>`, and the run ends with `[customization-geosets-test] RESULT: PASS|FAIL (<n> passed, <m> failed)`. The exit code is 0 when every case passes, 1 when any fails and 2 when a fixture is missing.

Not covered here: the database import, requirements (which choices are offered), the model refresh's later passes (equipment, ears, helmets) and the Unity viewport. `-customizationtest` checks the geoset rule on the loaded client's own characters through the import, the requirements and the whole refresh; what the viewport draws was checked by eye on captures (see `docs/character-customization/undead-jaw-overlap.md`).
