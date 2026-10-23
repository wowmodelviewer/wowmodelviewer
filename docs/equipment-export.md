# Equipment packages

## User workflow

`File > Export equipment...` is available in the Models viewer for a player
character with equipped items. Choose the containing folder first, then choose
the equipment and target bodies in the export dialog.

- Armor and weapons have independent master checkboxes and item checkboxes.
  All equipped items start selected.
- **As a set** puts the selected armor's individual item folders inside a folder
  named by the user. It also writes the rules resolved for those pieces worn
  together. **One folder per item** leaves the armor folders at the package root.
- Weapons always have individual folders outside the armor set folder.
- Importing a set through the Item Sets selector supplies an editable name
  suggestion. Changing its item/appearance alternatives preserves the suggestion.
  Changing the armor outside that selection invalidates it. This is session state;
  ordinary saved character files do not gain a new set-name field.
- The right column lists the loaded client's non-NPC race entries, ordered by
  expansion, starting with the eight original races. There are no expansion
  headings. Male/female checkboxes are enabled where that body is available;
  the current body starts selected. The list scrolls vertically.
- Export requires at least one selected item and one selected body. An armor set
  also requires a valid Windows folder name. Spaces, underscores and accents are
  preserved. Invalid names disable Export and show a floating English notice next to the name field.
  Clicking the name field or the notice dismisses it without moving or resizing
  any controls. Export remains disabled until the name is valid; editing the
  name can show the notice again if the new value is still invalid.
  Forbidden characters show the complete list: `Set name contains invalid characters: < > : " / \ | ? *`.
  Empty names, reserved Windows device names, trailing spaces/periods and names over
  80 characters have separate messages. User-entered names are never silently rewritten.

The selected destination is a containing folder, for example `Druid`. Each set
and weapon is published directly inside it, using its name:

```text
Druid/
  Oathclaw_Wargarb/                  exact name entered by the user
    catalog.json                   set index and combined rules
    README.txt
    textures/                      shared within this set only
    Head_<item name>_<id>_<display>/
      catalog.json
      race-4/male/
        catalog.json
        equipment.fbx
        equipment.fbx.wmvmat.json
      race-4/female/...
    ...                            one folder per armor piece
    rules/race-4/male/catalog.json
    rules/race-4/female/catalog.json
  Kaldorei Moon Bow/                weapon name
    catalog.json
    README.txt
    textures/                      this weapon's resources
    race-4/male/...
    race-4/female/...
```

There is no `Equipment.equipment` wrapper and no logs/status files in `Druid`.
Diagnostic files live under WMV's local application-data `export-logs` directory.
Automatic item names are made safe for Windows; duplicate automatic names receive
an item/slot suffix. Existing destination folders or files are never overwritten.
In individual mode, each armor piece also goes directly under the destination.

Move or copy the **complete set or weapon folder** independently. Its catalogs,
FBX material references and shared textures stay inside that folder. Individual
pieces within a set still share the set's resources and must travel with the set.

## Contents and consumer contract

This is a reusable equipment library, not another character export. Each selected
race/sex resolves the captured item IDs, display IDs and levels through WMV's
normal item loader. Race/sex fallbacks come from the game's data and existing
viewer rules. There is no guessed cross-race scaling or substitute race table.

Each top-level folder has an `armor-set`, `armor` or `weapon` catalog.
Set indexes list the pieces and combined rules; item indexes list the selected body variants. A variant contains:

- The race, sex and reference body model FileDataID.
- The exact captured item identity.
- `groupOverrides`, `hideGroups`, texture `bodyLayers`, suppressed customization
  regions and the body's atlas layout.
- FBX geometry and the material sidecar, addressed by node/material/submesh.
- Reference bone indexes and node names for binding to the corresponding body.

The FBX contains attached/merged equipment and supplemental clothing geosets on
the matching body's reference skeleton. It contains no character surface or
animations. Unused body vertices are removed as well. A texture-only piece can
have an empty geometry catalog; its layers and rules are still meaningful.
Rigid pieces remain parented at their resolved attachment bone/offset. Skinned
parts retain their weights; unresolved or incompatible bone mappings are rejected.

Apply equipment rules after character customization. Apply `groupOverrides`, then
`hideGroups`; add `bodyLayers` in stable layer order and suppress the indicated
customization regions. A whole set uses its **combined** rules rather than applying
isolated pieces' overrides sequentially. Supplemental base clothing geometry must
obey those same group selections. Recompute visibility and textures when removing
or changing equipment.

Atlas coordinates use a top-left origin. Blend mode 4 is multiply, 6 is overlay,
and the other supported modes use source-over, as in WMV. Material texture types
1/8 need the consumer's composed body texture; the FBX's fallback material is not a
replacement for that composition. The sidecar supplies shader, UV and render-state
information that a generic FBX import does not preserve.

Source layer textures are shared by FileDataID. Raw material PNGs are shared by
SHA-256 across pieces and bodies within each independently movable folder and are not redundantly embedded in each FBX.

**Unity integration still requires a consumer of these catalogs.** This change
does not add an importer/runtime to the target project WowBattlegrounds.
The exported target contexts are ordinary player bodies; this is not a catalog
of every class/form-specific body or every possible item combination. Mirrored
equipment, absent assets, unresolved textures and invalid attachments fail the
export explicitly rather than publishing an incomplete successful package.

### Orc posture variants (exporter `wmv-fbx-posture-2`)

Selecting Orc male now exports both authored posture models when the client
declares them. Hunched retains `race-2/male/catalog.json`; Upright uses
`race-2/male/postures/choice-439/catalog.json`. Combined set rules follow the
same suffix beneath `rules/`. Each variant loads the actual body model, applies
the posture selection, resolves the items again, and exports its geometry,
reference hierarchy, pivots, attachment transforms and binding metadata.
The model ID is never relabelled on another body's geometry.

The version-1 equipment index retains one `variants` entry per race/sex and
its existing base `catalog`. An additive `bodyVariants` array on that entry
lists `{option, choice, modelFileId, catalog}` for both bodies. These paths are
relative to the index containing `variants`, just like its base `catalog`.
Variant catalogs also record `option`, `choice`, `exporter`, `build` and
`product`. Character customization version 2 is unchanged.

Consumers supporting Upright must select the `bodyVariants` entry whose
`modelFileId` matches the active character, for **every piece and the combined
set rules**. The legacy base path continues to mean Hunched. Consumers without
this additive support can keep using Hunched, but must retain their model
compatibility rejection for Upright. Do not fall back to the first race/sex
entry or mix a body's pieces with another body's rules. No consumer project is
modified by this exporter correction.

## Process and failure behavior

The export uses the existing separate-process FBX worker and a captured character
snapshot. It does not reconfigure the character in the visible viewer. Progress
counts the piece/body variants and combined set-rule variants. Cancellation stops
the worker. All selections are built in a private staging directory before any
final folder is published. Existing outputs are checked before generation and
again by non-overwriting renames during publication. If a rename fails, folders
already published by this job are moved back; any rollback failure is reported.
An abrupt process termination can leave hidden staging data or interrupt the short
multi-folder publication step. No existing output is overwritten.

For automation, the existing `-fbxexport <path>` accepts
`-fbxequipmentpackage <base64-UTF8-JSON>`:

```json
{
  "version": 1,
  "slots": [0, 1, 2, 3, 5, 6, 7, 8, 10],
  "bodies": [{"race": 4, "sex": 0}, {"race": 4, "sex": 1}],
  "asSet": true,
  "setName": "Battlelord's Plate",
  "destination": "C:/exports/Druid"
}
```

Slots are WMV `CharSlots`, not inventory types. The worker requires those slots
to be equipped in the loaded snapshot. `destination` must be an existing folder;
set/item folders are created directly inside it. `<path>` is the separate diagnostic
prefix for status/log sidecars. Omitting `destination` retains the legacy
`<path>.equipment` layout for older automation callers.

## Verification

`scripts/tests/equipment-export` compiles the real dialog with fixture game data
and exercises it without showing a window. It checks both master toggles,
per-item selections, the editable suggestion, empty selections, body availability,
expansion order, scrolling, column bounds and resizing. The Item Sets integration
tests also verify that the name follows set/item/appearance selection and that an
external armor change invalidates the suggestion.

The optional `equipment-inspect` target independently reimports FBX using the SDK.
Run `verify-package.py <package> --inspector <equipment-inspect.exe>` to verify
catalog references, materials, bone links, normalized skin weights, absence of
unused vertices/body surfaces and animation takes. It also verifies the combined
set's piece list and shared texture references.

Local real-asset evidence and installation manifest are recorded separately in
`AGENTS.md`. Automated structure/reimport checks are not visual validation in
WoW or Unity.

`named-folders.py --runtime <runtime> --character <battlelord.chr> --output <new-evidence-folder> --inspector <equipment-inspect.exe>` exercises the new layout with the documented Battlelord armor/bow fixture, independently relocates sets and weapons, and checks collisions and invalid names. The dialog tests cover every forbidden character, reserved names, trailing characters, spaces and underscores.

## Orc posture dependency and validation

The Orc variants depend on PR #128 for conditional-model data and skeleton source
resolution. Merge #128 first; this branch contains that prerequisite.

Retail `wow` build `12.1.5.70391`, exporter `wmv-fbx-posture-2`: Oathclaw Wargarb
exports seven pieces for both postures (14 variants), and Battlelords Plate eight
(16 variants), each with combined rules for both models. Native FBX reimport checks
resources, geometry and weights. Independent skin evaluation covers 18 cases
(body / each outfit, both postures, Stand/Walk/Run), with 11,928 matching bind and
attachment matrices and 99,600 finite posed points. These are sampled frames, not
an exhaustive collision test. Hidden WMV captures and native geometry renders
were reviewed on the integrated build; the user confirmed the installed fix works.
No Blizzard live-client capture or WowCharacterViewer test is claimed.

Build `scripts/tests/animation-sources` to obtain `equipment-pose`, then run:

```text
equipment-pose <body-package> <set-package-or-dash> <take> <milliseconds> <pose.json>
```

It selects piece and combined-rule catalogs by the actual body model ID and
evaluates skin weights on that body's animation. Keep original fixtures, generated
FBX, screenshots and pose outputs in the local asset deposit; none belong in Git.
