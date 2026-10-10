# FBX equipment on exclusive NPC models

## Cause and correction

`ModelViewer::OnExport` dispatches FBX work to a fresh WMV process. Previously only
`isChar` models were saved to a temporary `.chr`, which includes customization and
equipment. An exclusive NPC such as `creature/tyrande3/tyrande3.m2` instead produced
`-mo <model>` (or `-npc <id:display>`). That reloaded the body without the weapons
selected in the parent process. The FBX plugin already exports equipped item meshes
and parents them to skeleton bones; it cannot export an item the child never loaded.

`PrepareFbxAsset` now preserves the original model/NPC/skin command and appends
`-fbxequipment <temporary.chr>` for non-racial equipment owners. `SaveChar` stores
the actual slots, IDs, display IDs and appearance levels. An exclusive NPC has only
two equipment slots. Empty slots are included and replace previous/default equipment.
Racial characters retain their complete `.chr` route.

The export child calls `LoadFbxEquipment` after loading the body and before the FBX
plugin. It validates the XML, model identity and complete slot set, restores exact
item appearances using the existing item loader and refreshes the attachments only.
It does not call racial body customization or change the model's classification.
Invalid snapshots or unresolved hand models produce an error status rather than a
successful export with missing equipment. The manager removes the snapshot when the
job finishes/cancels; the caller also removes it when a job cannot start.

This is independent of Unity's protocol-11 `attachmentsOnly` correction. No Unity code
or protocol changes are part of this FBX fix.

## Repeatable regression

Build `scripts/tests/fbx-equipment` with CMake, passing `FBX_SDK_ROOT` pointing to
an SDK tree with `include/`, `lib/libfbxsdk.lib` and `lib/libfbxsdk.dll`. Run
`Test-FbxEquipment.ps1` with `-Runtime`, `-Inspector` and a new `-OutputDirectory`.
The runtime must already have access to the WoW client and its database/listfile.
Use `-Build` and `-Product` to pin the client and `-Clips` to supply the comma-separated animation
array indices from an existing export command. These are indices, not WoW animation IDs.

The test first restores a live model, then uses `WMV_FBX_DESCRIBE` to call the same
descriptor builder as the GUI. A second, fresh process exports that exact saved
descriptor. A separate FBX SDK executable imports the result and reports mesh triangle
counts, materials/textures, attachment parents and sampled world-space movement for
each clip. It checks that rigid attachments keep a constant transform relative to their
animated bone. This is a numerical check, not a visual review in Unity or a DCC tool.

Cases: Tyrande with bow, right-hand weapon, both hands, no equipment, a display-only
weapon, and a racial Night Elf female with bow. `-Case invalid-snapshots` checks malformed XML, wrong model,
missing slots and unavailable equipment. The harness uses the existing listfile instead
of downloading updates and cleans up temporary export descriptors. FBXs, snapshots,
logs and inspection JSON remain in the selected output directory for review.

The known example uses Tyrande FileDataID `4198151`, Kaldorei Moon Bow ItemID `213160`,
bow FileDataID `524474`. Expected body/bow triangle counts are `12054 / 1972`, and
the bow's parent is Tyrande bone `223`. For the racial Night Elf female, the example's
body has `7716` triangles and the bow's parent is bone `234`. The automated racial
fixture uses the runtime's current customization defaults, so its body triangle count
may differ; the test checks its full-character load route and the bow's geometry,
materials, bone parent and animation instead of imposing the example's appearance.

## Verification on 2026-09-28

Release x64 compiled successfully against client build `12.1.0.69933`. Six export
cases passed, along with all four invalid-snapshot rejection cases. Tyrande's output
contains the unchanged `12054`-triangle body and the `1972`-triangle bow under bone
`223`, with all `156` selected animation takes. The bow's raw PNG is byte-identical
to the supplied successful racial export (SHA-256
`D4153681E7C2AD47E27C900EFC05B909B9F8AC4907402E186153E925F7C3BA3D`).

The independent reader sampled the exported bow in `ReadyBow`, `HoldBow`, `LoadBow`
and `AttackBow`: its world position changes while its authored local transform stays
constant. The racial regression retains bone `234` and `156` takes. Tests created new
files outside Song of War; the supplied reference FBXs were only read. No visual
review of the newly exported FBX in Unity was performed.

The tested executable was copied to the local installation and its SHA-256 verified.
The existing Unity renderer and FBX plugin binaries were already identical to the test
runtime; the fix requires no changes to those binaries. The application was closed for
replacement and left closed.
