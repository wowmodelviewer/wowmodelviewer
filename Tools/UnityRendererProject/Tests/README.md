# Parser tests

Framework-free tests of the Unity project's pure parsing layer (`Assets/Scripts/Wow`, no UnityEngine):

- `WowParserTests.cs`: M2, skin, BLP, WMO and the model-space coordinate conversion.
- `WorldParserTests.cs`: the world parsers (`Assets/Scripts/Wow/World`) on hand-built bytes: WDT, split ADT,
  terrain coordinates and vertex layout, holes, alpha storage, placements, tile assembly, and truncated or
  randomly corrupted input.
- `WorldCorpusValidation.cs`: the world parsers on the real client bytes of the world-format audit
  (`docs/world-viewer/00-audit.md`). It first checks every input against
  `Tools/world-audit/tests/corpus_hashes.json` (243 files, 19 file sets). It then recomputes the audit's tables and
  verdicts from the C# parse results and writes them to `<corpus>/results/csharp_parsers.json`. It runs only when
  `WMV_WORLD_AUDIT_CORPUS` names the extracted audit data; otherwise it reports SKIPPED, which is not a pass.

## Run

From the repository root (.NET SDK installed; output goes to the git-ignored `build/unity-parser-tests`):

```
set WMV_WORLD_AUDIT_CORPUS=F:/wowmodelviewer/build/world-audit-data
dotnet run -c Release --project Tools/UnityRendererProject/Tests/Runner/WowParserTestRunner.csproj
```

The exit code is the number of failures.

```
dotnet build -c Release Tools/UnityRendererProject/Tests/Runner/WowParsingNetStandard.csproj
```

This compiles `Assets/Scripts/Wow` as a .NET Standard 2.1 library at C# 9: the API profile and language level of the
Unity player. The Unity Editor compile remains the authoritative check (see `docs/world-viewer/01-parsers.md`).

Compare the corpus results with the audit's independent Python readers:

```
python -I Tools/world-audit/compare_csharp.py <corpus>/results/csharp_parsers.json <python results folder> <corpus>/catalog <out.json>
```

The Python results come from the audit scripts run on the same pinned bytes; see `Tools/world-audit/README.md`.
