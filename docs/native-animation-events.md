# Native animation events

Whenever the FBX exporter writes selected animation takes, it also writes
`<filename>.fbx.wmvevents.json`. This is automatic in ordinary FBX exports,
Current appearance and All customization options, including each conditional
body model. No new export option is required. Equipment-only exports contain no
animation takes and do not acquire an event file.

For customization packages, the **body's own** `catalog.json` contains:

```json
"animationEvents": {
  "schema": "wmv.animation-events",
  "version": 1,
  "file": "character.fbx.wmvevents.json"
}
```

The file path is relative to that catalog. Follow `bodyVariants` first, as for
geometry and animations: Hunched and Upright have their own event files. Never
reuse another body's file. Ordinary FBX exports use the filename convention
above and also record its basename in the FBX document's `WMVAnimationEvents`
string property. Move the FBX and sidecars together. Existing catalog versions,
`animations` and `animationTakes` remain unchanged; this is an additive contract.

## Version 1 contract

The JSON root has `schema: "wmv.animation-events"`, `version: 1`,
`status: "complete"`, `modelFileId`, `product`, `build`, `eventDefinitions` and
`clips`. Product/build come from the loaded client, not a constant in the
exporter. Missing provenance fails an animated export.

Each clip has:

| Field | Meaning |
| --- | --- |
| `take` | Exact, de-duplicated FBX take name, including variation suffixes |
| `animationId`, `subAnimationId` | Native identity of the selected sequence |
| `durationMs` | Native duration in milliseconds |
| `sequenceOwnerFileId` | Original model/skeleton containing that sequence |
| `sourceSequenceIndex` | Sequence index local to that owner, before alias resolution |
| `resolvedSequenceIndex` | Source-local index actually used for event timestamps |
| `events` | All nonempty native event tracks associated with this clip |

An empty `events: []` means the valid source supplied no timestamps for the
clip. A failed read never produces a successful file containing an empty
replacement. Every exported take has a clip entry, including event-free takes.
There is no filtering by event code, spell, race, sex or consumer policy.

Each event track contains:

| Field | Meaning |
| --- | --- |
| `eventIndex` | Index in the source model's event definitions; distinguishes duplicate codes |
| `code` | Four original bytes represented as Latin-1 characters, including any NUL |
| `codeHex` | Eight hex digits preserving those bytes unambiguously |
| `payload` | Original unsigned 32-bit payload bits; no event-specific interpretation |
| `bone` | Native bone index; native `0xffffffff` is represented as `-1` |
| `position` | Three original float32 values, without coordinate or unit conversion |
| `interpolationType` | Original track header value; events are timestamp-only |
| `timestampsMs` | All original timestamps, including duplicates and zero |
| `globalSequence` | Source-owner-local global sequence index, or `-1` for a clip clock |
| `clock` | `clip` or `global` |
| `periodMs` | Global-loop duration; present only for `clock: "global"` |
| `trackChunk` | `MD21` or `PEDC`, containing the event-track descriptors |
| `timestampChunk` | `MD21`, `PEDC` or `AFM2`, containing the timestamp values |
| `timestampFileId` | File containing the timestamp chunk |

`eventDefinitions` preserves the model's entire event-definition table (index,
code/codeHex, payload, bone, position), including definitions with no timestamps
in the selected clips. Interpret signed payloads only according to the event's
own native semantics; the exporter preserves the bit pattern.

Root unit declarations are `timeUnit: "millisecond"`,
`positionUnit: "WoW-model-unit"`,
`positionCoordinates: "M2-native-right-handed-Z-up"`, and
`positionSpace: "native-event-bone-relative"`. Positions retain the native event
offset attached to the named bone, not a sampled world position or a Unity-space
vector. `sourceUnitsToFbx` records the existing FBX unit factor (91.44 cm per
source unit). The exporter does not apply that factor to event positions.
Consumers must use the same skeleton, coordinate conversion and unit conversion
as for the associated model. The exporter does not choose a projectile origin.

For `clock: "clip"`, timestamps are within `[0, durationMs]`. For `global`,
timestamps are within `[0, periodMs]` and refer to the independent, repeating
global clock, not to the take's duration. The consumer owns clock phase and
boundary handling; do not stretch global timestamps to fit a clip or expand
them into guessed per-take occurrences. The file preserves source information,
not a gameplay dispatch schedule. The existing FBX baker uses a 1 ms span for
a native zero-duration sequence; the sidecar keeps its native duration of zero.

## Source resolution and failure behavior

The reader snapshots whole resources without moving or closing an already-open
GameFile. It parses event data separately from the animation bone sampler, so
adding events does not change skeletons, pivots, mesh data or sampled motion.

Sequences are matched by AnimationID/SubAnimationID, using each skeleton's
local table. Child identities override matching parent identities; missing
identities inherit the parent. Partial skeletons with a different bone-count
index space follow the existing loader's parent path. Alias resolution retains
the requested identity and uses the owning table's alias target. A variation is
exported only if selected; no random variation is selected for its events.

For direct/child sequences, the event definitions and track descriptors come
from MD21. Non-resident event timestamps use the event AFM2 chunk selected by
the source AFID (a model AFID takes priority for its own events). They never use
AFSB, which contains bone keys. Global tracks remain resident in their track
buffer and resolve their period against the sequence owner's global table.

For inherited sequences, PEDC's array header addresses one 12-byte track per
model event. These tracks use the parent's local sequence indices. PEDC includes
its own resident timestamps **even when the parent's bone sequence is external**;
its offsets must not be applied to the parent's AFM2. This is verified against
Retail Orc Upright, including EmoteSalute's external parent sequences.

PABC excludes inherited AnimationIDs. If the existing bone selection requests a
blacklisted parent identity, export fails instead of pairing it with another
model's events. This change does not alter the bone loader's blacklist behavior.

Bounds, chunk sizes, duplicate identities, alias cycles, duration agreement,
bone references, finite positions, global references/periods, timestamp order
and clock ranges are checked. Missing external event data, required PEDC or an
AFM2 chunk is an error. The error includes model/take identity when available.
No fallback is selected according to whether an offset happens to fit a buffer.

Read validation finishes before animation baking. The sidecar uses an atomic
file replacement. A sidecar write failure fails the export; an ordinary FBX may
already have been written and must be treated as incomplete. Customization
packages retain their existing temporary-directory publication: failure in
either posture publishes no final package.
After a successful ordinary export without animations, any old event sidecar
for that same target is removed to prevent stale metadata from being reused.

## Consumer integration (WowSpellViewer)

1. Choose the body catalog, verify product/build/model, follow its
   `animationEvents.file`, and require the supported schema/version/status.
2. Match `take`, AnimationID and SubAnimationID to the loaded clip and validate
   duration (allow only the importer's documented rounding tolerance).
3. Inspect all event tracks. SpellViewer can select `$CSL`, `$CSR`, `$CST` or
   `$BWR` according to its existing release policy, then read `timestampsMs`.
   Code interpretation and per-spell dispatch remain the consumer's decision.
4. Keep clip and global clocks separate. Apply playback speed/pause to the
   appropriate clock and process timestamp crossings exactly once per occurrence.
5. Treat a valid empty list separately from a missing/incompatible file or an
   unavailable marker. Never replace missing data with a fixed race/sex delay.

This exporter does not modify WowSpellViewer or its existing
`wow.character-release-events` adapter. The consumer must add support for the
new schema; existing prepared character packages must be reexported to contain
the native sidecar. No post-export adapter is needed for new exports.

## Validation and scope

The automated harness is in `scripts/tests/native-animation-events/`. It includes
synthetic positive/negative cases and an independent Python byte-level oracle.
All real source validation in this change uses **Retail `wow` 12.1.5.70391**, M2
layout **274**. Other layouts fail explicitly. This is not a compatibility claim
for Classic/TBC, other builds or every possible skeleton structure.

Inherited events deeper than the immediate parent are rejected explicitly:
the current model's PEDC does not establish mappings to arbitrary ancestors.
Nested-parent exports with no definitions are also conservatively rejected.
Unverified inheritance layouts are not silently treated as empty.

The supplied Upright SpellCastDirected case is tested as source data: model
1968587, skeleton 1969462, parent 1964411, `$CSL` at 200 ms. No such time or
model ID is embedded in production logic.

Validation on 23 October 2026:

- 28 synthetic assertions cover timestamps at zero, non-release codes, original
  payload/position, event-free variations, aliases/cycles, separate global clock
  bounds, AFM2 versus AFSB, resident PEDC on external parent clips, PABC and
  missing/truncated/out-of-range resources and retained-parent bone-index validation.
- Independently checked raw source events for 13 bodies and 1,965 identities
  within the selected 0–225 range: 3,802 timestamps. Bodies: Human, Night Elf,
  Orc, Dwarf, Gnome and Troll, both sexes, plus Orc Upright.
- Exported and reimported a complete Orc customization package: 151 takes per
  posture, 403 timestamps in Hunched and 331 in Upright; 28 and 35 event-free
  takes respectively. All metadata fields match the independent source oracle.
- Exported ordinary skeleton/animation FBX for those same 13 bodies (103 takes),
  including movement, casts and variations; independently checked their event
  files and reimported exact FBX take names/durations and sidecar references.
- Reimported both complete posture FBX files and compared 3,133,460 bone samples
  with the existing independent animation oracle: no discrepancies. Geometry,
  hierarchy, weights, materials and texture-reference inspection matched prior
  posture exports; catalog default visibility follows the newly selected
  appearance, so the entire appearance-default catalog is not byte-identical.
- Tested the final binary with and without animations, stale-sidecar removal,
  and a sidecar destination that cannot be written: the latter reports ERROR.

No new visual validation or client-Blizzard capture was performed. The consumer
has not been changed or exercised with this schema. Real global-event clocks
and PABC-bearing models were not present in this corpus; those paths have
synthetic coverage only. Real resources, exports and reports remain local in
`LocalAssets/Prepared/WoWModelViewer/native-events-20261023` and
`C:/Users/mauri/WMVDev/native-events-20261023/`, outside Git. Existing shared
packages were not overwritten.

Format references consulted: [WhiteoutLib's M2 specification](https://github.com/sc2-arcade-watcher/WhiteoutLib/blob/libmodkit/docs/M2_FILE_FORMAT_SPECIFICATION.md)
(event records, parent-data chunks and external animation chunks). Actual binary
layouts and ownership were checked against the local Retail files; the
release-only adapter is a comparison reference, not the complete parser.
