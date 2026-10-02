# Animation source regressions

The C++ harness uses the production `Animated<>`, alias resolver and animation snapshot helper. Synthetic cases cover duplicate animation IDs with distinct variants, external and internal alias chains, cycles, invalid targets, primary sequences, globals, unavailable external buffers, owned/borrowed files and legacy/chunked layouts. It runs without game fixtures when invoked without arguments.

For HumanMale, supply the read-only raw fixtures from the diagnosis (build 12.1.0.69933). Expected poses are read and sampled independently of `Animated<>`, the production alias resolver and snapshot helper, using the diagnosed sequence/file pairs. The harness imports the new FBX with the FBX SDK and checks the 12 affected clips and the fifth dance / EmoteDanceSpecial controls: names, duration, source key finiteness, monotonic times, motion and translation/scale/quaternion samples at every baked frame on all 216 bones. Quaternion comparison is orientation-based, so Euler unrolling and quaternion sign changes do not create false failures. Translation/scale properties preserve signed scales; decomposing a matrix would incorrectly turn negative scales into rotation differences.

Configure and build this directory as a standalone CMake project, passing `WMV_ROOT`, `WMV_BUILD`, `QT_ROOT` and `FBX_ROOT`. Build Release x64 after building WMV. Put `animation-sources.exe` beside a complete matching WMV runtime, or supply its dependency directory in PATH.

```powershell
./Test-AnimationSources.ps1 -Runtime <isolated-runtime> -RegressionExe <regression-executable> -RawDirectory <diagnosis/raw> -OutputDirectory <new-output-directory>
```

The script runs WMV invisibly, pins the game build, avoids the optional listfile refresh, exports a **skeleton and 14 animation takes** (without mesh/material baking), compares them, and exercises all selected sequences in the integrated Unity renderer. It also checks the second SitGround variant. It preserves the original character FBX and refuses to overwrite an existing export. `-ViewerOnly` reruns the viewer phase against an existing evidence directory.

Check `comparison.json`, `export.log`, `regression.stdout.txt`, `unity.log` and `unity.unity.log`. The automatic renderer test verifies loading, selected/resolved sequences, bone movement, notices and asset access; it does not constitute visual review of poses. The default test renderer remains unchanged; no protocol change or Unity rebuild is needed for this correction.

On 2026-10-02 the full mesh/material attempt hit a PixelBuffer allocation failure before writing the animation takes. Review reproduced that failure with the pre-fix binaries as well. The skeleton-only export validates the animation path independently; full mesh/material export and visual review remain unverified.
