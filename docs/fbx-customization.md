# FBX customization packages

The FBX dialog's **Appearance** row is a capability extension point. **Current
appearance** is the default and uses the existing export path. **All customization
options** writes `<chosen-name>.fbx.customization/`, containing the FBX geometry
library, PNG textures, the existing material sidecar, `catalog.json`, and `README.txt`.
FBX alone is not a character creator. A consumer must interpret the catalog.

The complete mode requires mesh, skeleton, and skinning. Their previous values are
restored on returning to Current appearance. Animation selection is independent.
**Include equipment** is initially off in complete mode and remembers its choice
within the dialog; in current mode it is fixed on to reflect the existing exporter.
Unsupported models keep both appearance radio buttons disabled and explain why.

## Scope and transaction

The catalog covers the loaded player body and its current class context. It includes
conditionally reachable choices, not just the choices currently shown by the UI. It
excludes other class contexts, NPC-only choices, and unlock requirements unavailable
in that context, recording excluded IDs. The captured default must be in this scope.
This does not export every body of a race or enumerate complete combinations.

Unknown effect semantics, missing resources, unresolved variants, invalid bone sets,
and unsupported equipment fail the complete export instead of advertising a partial
package. A temporary sibling directory is renamed only after every required file is
written and checked. An existing final package is never overwritten. A killed worker
can leave a temporary directory, but does not publish a successful final catalog.

The worker receives a character snapshot and the selected animation indices from
the same target model. Its own export model is modified; the viewport character is
not used as the geometry union. Optional equipment is the captured current outfit,
not a database of arbitrary items. The base resources remain available when that
outfit is removed. Outfit rules describe the pieces worn together; arbitrary
cross-outfit mixing is not promised.

## Catalog

`schema: wmv.customization`, `version: 1` identifies the package contract. All paths
are relative to the package root. Keep the whole folder together when moving it.

- `options` contains choice IDs, names, requirements, and conditional effects.
- `requirements.anyChoice` is OR; option and choice requirements are AND.
- `defaultSelection` records the starting choice IDs; `context` records exclusions.
- `geometry` addresses each exported component by node, material and material index,
  its source geoset, logical owner, default visibility, and texture units.
  Geoset effects carry `geosetGroups` separately from their positive `geosets`.
  Every group set by an active effect hides its other base variants, including
  default variants no choice names; the union of active variants wins. Body geoset
  0 is preserved. A None effect has an empty `geosets` array but retains its
  authored group, including source variants 0 and -1. This matches the current
  viewer's group selection and avoids showing overlapping default geometry.
- `textures` contains source images; `textureLayout` defines body-atlas regions.
  `README.txt` specifies composition order, blend modes, and geometry visibility.
- `equipment` distinguishes outfit pieces, additional layers, group overrides, and
  suppressions from the base customization data.
- `bones` maps source bone indices to unique FBX node names and parents.
- `boneSets` links each facial choice to its matrices and those exported nodes.
- `boneCorrectionContract` specifies matrix conventions, units, and application.

Texture variants with several files are selected by their component race, sex and
class metadata, with the body's declared texture fallback. Equal-priority unresolved
variants fail. A file's path/name is not a substitute for this metadata.

## Combining facial shape with animation

`ChrCustomizationElement.ChrCustomizationBoneSetID` resolves through
`ChrCustomizationBoneSet` to `BoneFileDataID` and `ModelFileDataID`. The latter must
match the exported model. A version-1 `.bone` contains paired BIDA bone indices and
BOMT affine matrices. Read the original matrices; do not infer them from FacePose
clips, transpose them, or swap quaternion components based on a visual resemblance.

The independently implemented reference is the public
[Wowhead viewer](https://wow.zamimg.com/modelviewer/live/viewer/viewer.min.js),
inspected on 2026-10-17. In the inspected bundle, `Ql.j` resolves the selected bone
file, `bl`/`C`/`ba` load and assign its corrections, and `Eo.A` applies each correction
after its animation matrix and before parent accumulation. Function names are
minified and can change. The inspected UTF-8 bundle's SHA-256 was
`ce4ceffb220830b806a9ec9483118e84471a4a4b47edfbb3c672914e96cdd241`.
No viewer implementation or game assets are vendored into this repository.

For column vectors, let `A` be a native M2 bone's animated local skin matrix, `p`
its model-space bind pivot, and `C` its correction. The reference computes:

```text
q  = A * [p, 1]
A' = T(q) * C * T(-q) * A
world = correctedParentWorld * A'
```

The pivot is moved by the animation **before** applying the correction. Multiplying
in the reverse order or using the bind pivot instead produces different results.

FBX joints have their origins at their pivots. For an uncorrected sampled FBX local
joint matrix `L`, the equivalent rule is:

```text
t  = translation(L)
L' = T(t) * C_fbx * T(-t) * L
```

The package already expresses `C_fbx` in FBX-native Z-up right-handed axes and
centimeters, including the exporter's 91.44 scale on its translation. An importer
that changes axes or units must convert both matrices consistently; for basis change
`B`, use `B * C_fbx * inverse(B)`. Do not scale the translation twice.

Apply this to a fresh animation/rest sample each frame, after animation blending,
then accumulate the corrected hierarchy and skin with the **original** bind pose.
Unlisted bones use identity corrections; a selection with no bone set clears the
previous set. Corrections are not a second looping animation. The package does not
bake a selected face into shared clips or bind poses, and a consumer must not layer
the corresponding raw FacePose clip on top of the same correction.

Preserve the full affine matrix. A correction's nonuniform scale combined with
animation rotation can produce shear; decomposing to approximate Unity Transform
TRS can lose it. This contract supports a matrix-based evaluator; it does not claim
that assigning only `localPosition/localRotation/localScale` is always equivalent.
`CustomizationBoneTransform.h` provides the two reference-equivalent operations.

The current catalog rejects ambiguous multiple bone-set owners/combinations and
correction targets with special inheritance or billboard flags. These need their
own verified semantics rather than an arbitrary multiplication order.

## Technical checks and evidence

`scripts/tests/customization-bones.cpp` is a standalone C++17 test for the reader and
matrix evaluator. It rejects malformed versions/chunks, truncation, duplicate and
out-of-range indices, non-finite and non-affine matrices. Optional file arguments
exercise locally extracted `.bone` files; `--reference <fixture>` compares against
an independent renderer fixture (assets/fixtures are not committed).

The research corpus uses client 12.1.5.70391: 30 Night Elf male/female bone files,
1,000 corrections, and 16,000 animated samples across IDs 0, 4, 5 and 804. The
reference fixture executes the inspected arithmetic branch from Wowhead, while the
WMV evaluator is independently written. Maximum C++ matrix-component difference:
`2.27e-7` in source units. An independent FBX-local coordinate derivation differs
by at most `1.62e-5` centimeters. Reversed-order and fixed-pivot alternatives differ
in 7,864 and 2,893 samples respectively. Two remote reference bone files (1060751
and 1053759) are byte-identical to the local client files.

This validates agreement with an independent renderer, **not** a numeric capture
of Blizzard's proprietary client. Mauricio visually inspected every male Night Elf
face in the local client and confirmed small nose/chin changes and paired mouth
expressions for faces 7–8, 9–10 and 11–12. That is visual evidence of shape changes,
not a per-vertex verification of the matrix formula.

`scripts/tests/customization-dialog.cpp` exercises supported/unsupported models,
with/without clips, required-content locking, restoration and equipment memory in
hidden dialog instances. It never shows or focuses the application.

The schema adds `ChrCustomizationBoneSet` with verified-layout-only reading and
bumps the cache schema to 19. Old database caches rebuild once. Complete-mode
support is decided from actual data; a new race/build is not assumed compatible.

Integration checks on that client exported the male Night Elf context with 20
options, 127 choices, 201 source textures, 80 geometry entries, 218 bones, and 11
bone sets. A bow adds one geometry entry; a captured helmet/pants/robe/bow outfit
exports four items, 209 source textures and 84 geometry entries. Exporting an
equipped character without equipment preserves the base coverage. Four explicitly
selected clips (WoW IDs 4, 5, 0, 804) survive independent FBX SDK reimport.

The reimport check resolves catalog node/material indices, skin clusters, bone-set
targets, choice resources and relative texture paths, including after copying and
renaming the package folder. Existing destinations are rejected without changing
the previous catalog. Interrupting texture extraction does not publish a final
package. These are technical integration checks, not visual validation or a
production Unity consumer test.

An additional class-context audit matched all 127 choice identities, option/choice
requirements and effect ownership to the source database. None fails the ordinary
class mask (0x77DF for this client, excluding DK and DH). All 37 DH-only choices
and all 17 DK-only choices are absent. The `Horns` option contains only `None`
(782); it exports no horn choice. `Blindfold` contains `None` (789) and `Linen`
(795), both using requirement 141, whose ClassMask is -1 (all classes). Category
names alone therefore do not establish class exclusivity. The 127 count includes
neutral `None` choices and fixed single-choice categories, not 127 visible variants.

The initial female Night Elf export was blocked by bone file 1053758. Investigation
identified a neutral-face reference rather than an unavailable nonneutral shape:
choice 825/set 580 names this absent file, while the owning skeleton omits it from
BFID and inline FacePose 808/subanimation 1 contains no per-sequence transforms on
any bone. The same pattern holds for choice 834/set 581/file 1053760/subanimation
10, which remains outside the ordinary class context. The independent Wowhead
renderer clears all previous corrections before attempting either absent file and
keeps the base pose on the failed request. Its actual method was executed with a
failed request and pre-existing corrections to check that transition.

The exporter recognizes only these verified model/choice/set/file tuples, only
when CASC reports NotListed and the loaded inline, non-aliased FacePose sequence
still has no per-sequence bone transforms. It emits an explicit empty transform
set with `resolution: verified-neutral-face`; selecting this set must clear the
previous correction. This does not infer a general FacePose-to-BONE conversion,
accept arbitrary missing files, or substitute another face. NotLocal, corrupt,
unknown references and changed nonneutral poses still fail. This compatibility
rule is based on the source skeleton and independent renderer, not a numeric
capture of Blizzard's client.

The female model also carries an auxiliary geoset-0 surface using replaceable
texture type 9, with no binding in the ordinary class context. The package excludes
this verified model's dormant type-9 single-texture base surface only when it has no available source
texture or customization binding; it never discards body skin, named textures or
nonzero variants this way. `excludedGeometry` records each exclusion and reason.

The corrected female package passes independent FBX reimport with 22 options, 137
choices (including all nine ordinary faces), 276 source textures, 74 geometry
entries, 239 bones, nine bone sets (one neutral) and three animation takes. A
negative test replacing a nonneutral face's file reference with an absent one
still fails without publishing a package. All 14 DK-only and 38 DH-only choices
remain excluded, along with three other class-restricted choices.

The export dialog displays capability failures as readable text below Appearance;
disabled-control tooltips alone are insufficient. The conventional Current appearance raw-component path passes
with four clips. Its baked-texture path hits the pre-existing hidden OpenGL
PixelBuffer failure, reproduced with both the changed and previous daily binaries.

### Independent contribution validation — 18 October 2026

The contribution was rebuilt on official develop 8f4e791f in Release x64. Catalog geoset group rules follow this base's customization behavior. A normal code review corrected export snapshot preservation of sheathed weapons, helmet suppression preferences and attached-model visibility. Hidden equipment resources remain reusable with their captured `defaultVisible` value.

Hidden regression exports and independent FBX SDK reimports passed for male and female Night Elves, optional equipment, relocated packages and Current appearance with raw textures. Existing-target rejection, missing non-neutral bone resources and cancellation were checked. The final snapshot regressions verified the bow's changed parent bone, hidden helmet metadata and helmet suppression preferences, followed by female and Current appearance exports with three clips. Bone validation covered 30 files, 1,000 corrections and 16,000 independent reference samples. No new visual validation or formal QA is claimed.
