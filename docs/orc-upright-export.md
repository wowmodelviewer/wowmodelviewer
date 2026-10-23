# Orc Upright animation export correction

Validated on 22-23 October 2026, Retail product `wow`, build **12.1.5.70391**.
Exporter revision: **`wmv-fbx-posture-2`**, recorded in new character/equipment
catalogs. The plugin's existing public version is 0.5.
This correction updates PR #128 (`codex/fbx-orc-postures`); equipment variants
are delivered separately by PR #129, which depends on this loader correction.

## Cause and correction

| Body | Model FileDataID | Skeleton FileDataID | Relationship |
| --- | --- | --- | --- |
| Hunched, option 24 / choice 438 | 917116 | 1964411 | Parent/base |
| Upright, option 24 / choice 439 | 1968587 | 1969462 | SKPD parent 1964411, plus its own SKS1/SKB1/AFID |

The loader replaced the child's sequences and bones with the parent's whenever
SKPD existed. This discarded **118 authored Upright sequences**, their track
headers, external animation mappings, bind pivots and global clocks. The FBX
exporter therefore sampled Hunched movement on the Upright mesh. The integrated
Unity parser duplicated the same assumption.

Both now compose complete compatible rigs by AnimationID/SubAnimationID. Child
identities override the parent; missing identities inherit it. Track data is read
using the selected source's own indices, aliases, interpolation and global-loop
periods. Child bind hierarchy/pivots remain associated with its geometry. The
existing parent-only handling of partial skeletons with another bone-index space
is retained. Complete rigs with incompatible bone identities are rejected.

The 0–225 selection contains 151 clips on each body. Upright uses 73 child clips
and 78 inherited clips in that selection. In particular:

| Animation | Hunched | Upright | Upright source |
| --- | --- | --- | --- |
| Stand / 0 | 2467 ms | 2467 ms | 1969462, sequence 0 |
| Walk / 4 | 1066 ms | 1000 ms | 1969462, sequence 1 |
| Run / 5 | 667 ms | 667 ms | 1969462, sequence 2 |

The WMV Appearance conditional-model toggle itself remains unimplemented. Preview
validation loaded each actual model, including `character/orc/male/orcmaleupright.m2`,
rather than treating the toggle on the base model as a reference. Saved character
snapshots can now use the existing hidden `-unityipctest` path as well as `-mo`.

## Consumer contract

Customization remains `wmv.customization` version 2 with
`conditional-body-models-v1`. Its alternate path remains exactly
`postures/choice-439/character.fbx.customization/catalog.json`.
The additive `animations` array records take/AnimationID/SubAnimationID/duration;
existing `animationTakes` and correction semantics remain intact.
No consumer project was modified. The companion equipment changes are in PR #129.

## Validation and evidence

The independent C++ oracle reads original local client bytes and compares every
30 Hz baked frame after FBX SDK reimport. It does not call the production skeleton
resolver or its animation sampler for expected values.

| Check | Result |
| --- | --- |
| Supplied pre-fix Upright FBX | 525,728 pose/duration discrepancies detected |
| Corrected Hunched, 151 clips | 1,564,860 bone/time samples; 0 discrepancies |
| Corrected Upright, 151 clips | 1,568,600 bone/time samples; 0 discrepancies |
| Maximum translation/scale difference | 0 in these exported samples |
| Maximum quaternion orientation error metric | 4.79e-14 |
| Integrated parser, both bodies | 199,320 native-track comparisons passed |
| Facial correction catalogs | All 18 face/posture mappings match the client database; 9 bone sets per body |
| Hidden WMV runtime | Six complete body/outfit runs passed; 18 front captures plus six body captures |
| Night Elf female regression | 137 choices, 9 bone sets, 239 bones, 276 textures and 3 clips retained |

Geometry review samples are Stand at 0 ms, Walk at 250 ms and Run at 167 ms.
The animation oracle checks the complete selected clips; the geometry renders
are selected frames, not an exhaustive collision test for every animation and
every outfit combination. Native FBX SDK evaluation and WMV captures are the
visual evidence; the generic Blender draft harness is not an acceptance oracle.
The native render uses neutral materials and does not implement the full texture
compositor or facial correction consumer. Face ownership, resources and matrices
were checked separately against the client's authored tables.

Builds: Release x64 C++ and Unity 6000.3.25f1 passed. Parser synthetic tests and the
real Orc corpus passed. No new IPC protocol is required. No visual capture of the
running Blizzard client or test inside WowCharacterViewer is claimed; the source
comparison uses the original installed files of the exact requested build.

Validation above was performed on the integrated development build. The isolated
PR branch is additionally compiled and its synthetic/native-corpus tests rerun
before publication. Local fixtures, FBX packages and screenshots remain exclusively
in the shared `WowBattlegrounds/LocalAssets` deposit; none are included in Git.
The user confirmed the installed correction works on 23 October 2026.
