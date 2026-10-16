// Program.cs -- console entry for the parser tests (see WowParserTestRunner.csproj).
//
// Exit code: the number of failures (0 = everything that ran passed). The corpus validation runs
// only when WMV_WORLD_AUDIT_CORPUS names the extracted audit data; otherwise it reports SKIPPED,
// which is not a pass. Optional argument: the path of the corpus results JSON
// (default: <corpus>/results/csharp_parsers.json). The terrain corpus tests run under the same condition and
// write mesh dumps of the pinned tiles to WMV_TERRAIN_DUMP_DIR when it is set (TerrainCorpusTests.cs); the
// terrain material corpus tests (TerrainMaterialCorpusTests.cs) run under the same condition. The
// world-map contract check runs only when WMV_MAP_CONTRACT_DIR names a folder of WDTs the viewer's map
// catalogue wrote (MapContractCheck.cs).

using System;
using System.Globalization;
using System.Threading;
using Wmv.Wow.Tests;

static class Program
{
    static int Main(string[] args)
    {
        Thread.CurrentThread.CurrentCulture = CultureInfo.InvariantCulture;
        int failures = 0;
        Console.WriteLine("== WowParserTests (M2, skin, BLP, WMO, coordinates)");
        int f = WowParserTests.RunAll(Console.WriteLine);
        Console.WriteLine("WowParserTests: " + f + " failure(s)");
        failures += f;

        Console.WriteLine("== WorldParserTests (WDT, ADT, synthetic and malformed input)");
        failures += WorldParserTests.RunAll(Console.WriteLine);

        Console.WriteLine("== TerrainMeshTests (terrain mesh builder and mesh dump, synthetic roots)");
        failures += TerrainMeshTests.RunAll(Console.WriteLine);

        Console.WriteLine("== TerrainMaterialTests (terrain material states, alpha slices, compositor, BLP header; synthetic tex0)");
        failures += TerrainMaterialTests.RunAll(Console.WriteLine);

        string corpus = Environment.GetEnvironmentVariable("WMV_WORLD_AUDIT_CORPUS");
        Console.WriteLine("== WorldCorpusValidation (real client bytes)");
        if (string.IsNullOrEmpty(corpus))
            Console.WriteLine("SKIPPED: WMV_WORLD_AUDIT_CORPUS is not set (the audit data is extracted locally, not committed)");
        else
            failures += WorldCorpusValidation.Run(corpus, args.Length > 0 ? args[0] : null, Console.WriteLine);

        // The terrain mesh builder on the pinned corpus tiles; WMV_TERRAIN_DUMP_DIR also writes their mesh dumps.
        Console.WriteLine("== TerrainCorpusTests (terrain mesh builder on real client bytes)");
        if (string.IsNullOrEmpty(corpus))
            Console.WriteLine("SKIPPED: WMV_WORLD_AUDIT_CORPUS is not set (the audit data is extracted locally, not committed)");
        else
            failures += TerrainCorpusTests.Run(corpus, Environment.GetEnvironmentVariable("WMV_TERRAIN_DUMP_DIR"), Console.WriteLine);

        // The terrain material builder and the BLP header API on the pinned tiles and chunks of the real client bytes.
        Console.WriteLine("== TerrainMaterialCorpusTests (terrain materials on real client bytes)");
        if (string.IsNullOrEmpty(corpus))
            Console.WriteLine("SKIPPED: WMV_WORLD_AUDIT_CORPUS is not set (the audit data is extracted locally, not committed)");
        else
            failures += TerrainMaterialCorpusTests.Run(corpus, Console.WriteLine);

        // Stage 06: the world-model placement layer, synthetic, then on the real client bytes of the world06 corpus.
        Console.WriteLine("== WorldModelTests (placement frames, Unity rotation, plan, floor and start point; synthetic)");
        failures += WorldModelTests.RunAll(Console.WriteLine);
        Console.WriteLine("== WorldModelCorpusTests (world-model placement on real client bytes)");
        if (string.IsNullOrEmpty(corpus))
            Console.WriteLine("SKIPPED: WMV_WORLD_AUDIT_CORPUS is not set (the audit data is extracted locally, not committed)");
        else
            failures += WorldModelCorpusTests.Run(corpus, Console.WriteLine);

        // Stage 07: the doodad placement layer, synthetic, then on the real client bytes of the world07 corpus.
        Console.WriteLine("== DoodadTests (MDDF plan, doodad sets, MODD composition, R(q), MODR; synthetic)");
        failures += DoodadTests.RunAll(Console.WriteLine);
        Console.WriteLine("== DoodadCorpusTests (doodad placement on real client bytes)");
        if (string.IsNullOrEmpty(corpus))
            Console.WriteLine("SKIPPED: WMV_WORLD_AUDIT_CORPUS is not set (the audit data is extracted locally, not committed)");
        else
            failures += DoodadCorpusTests.Run(corpus, Console.WriteLine);

        string contract = Environment.GetEnvironmentVariable("WMV_MAP_CONTRACT_DIR");
        if (!string.IsNullOrEmpty(contract))
        {
            // The world-map runtime contract at the data level: WDTs the viewer read through its map catalogue.
            Console.WriteLine("== MapContractCheck (WDTs read by the viewer's map catalogue)");
            failures += MapContractCheck.Run(contract, Console.WriteLine);
        }

        Console.WriteLine(failures == 0 ? "ALL RUN TESTS PASSED" : "FAILURES: " + failures);
        return failures;
    }
}
