# Native animation event regression

Build with CMake (C++14, no WoW assets or runtime dependencies):

```text
cmake -S scripts/tests/native-animation-events -B <outside-repo-build>
cmake --build <outside-repo-build> --config Release
ctest --test-dir <outside-repo-build> -C Release --output-on-failure
```

`native-events` without arguments runs synthetic event/alias/variation/global
clock/AFM2/PEDC/PABC tests, including malformed and missing-resource rejection.
AFSB has different timestamps from AFM2 in the fixture: accidentally selecting
the bone buffer cannot pass. A parent clip with external bone flags still uses
resident PEDC timestamps.

For private source corpora named `<FileDataID>.bin`:

```text
native-events --all <model-id> <raw-directory> <report.tsv>
native-events <model-id> <raw-directory> <animation-id> <sub-id> <duration-ms> <report.tsv>
python scripts/tests/native-animation-events/verify-events.py <raw-directory> <sidecar> [<sidecar> ...]
```

`--all` tests native identities in 0–225 (the regression corpus scope, not an
exporter restriction). The Python oracle independently decodes original bytes
and compares every event field, owner/index, payload, position and timestamp
against the actual exported JSON. It also checks schema, complete status,
product/build and uniqueness. The real-data oracle intentionally requires the
verified Retail build 12.1.5.70391; do not relabel another build to pass it.

Keep corpora, exports, binaries and reports outside Git. For end-to-end checks,
use the existing hidden FBX CLI in an isolated runtime, with new output paths
and `WMV_FBX_SELFTEST=1`. No desktop interaction or simulated input is needed.
The current wxWidgets headless host can exit with code 255 even after success;
check its `.fbx.status` (`OK` or `ERROR`), the resulting files and reimport
results. A process return code alone is not evidence of a successful export.

Optional SDK reimport inspector: configure with `-DFBX_ROOT=<SDK-root>` to build
`inspect-event-fbx`. It returns each take's real duration/name and the FBX
document's `WMVAnimationEvents` property, independently of the sidecar writer.

The format and consumer contract are documented in
[`docs/native-animation-events.md`](../../../docs/native-animation-events.md).
