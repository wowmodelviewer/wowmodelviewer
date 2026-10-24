// Program.cs -- console entry for the parser tests (see WowParserTestRunner.csproj).
//
// Exit code: the number of failures (0 = everything that ran passed). The corpus validation runs
// only when WMV_WORLD_AUDIT_CORPUS names the extracted audit data; otherwise it reports SKIPPED,
// which is not a pass. Optional argument: the path of the corpus results JSON
// (default: <corpus>/results/csharp_parsers.json). The terrain corpus tests run under the same condition and
// write mesh dumps of the pinned tiles to WMV_TERRAIN_DUMP_DIR when it is set (TerrainCorpusTests.cs); the
// terrain material corpus tests (TerrainMaterialCorpusTests.cs) run under the same condition. The
// world-map contract check runs only when WMV_MAP_CONTRACT_DIR names a folder of WDTs the viewer's map
// catalogue wrote (MapContractCheck.cs). The liquid corpus tests (LiquidCorpusTests.cs) run under the corpus condition
// and also read the MLIQ examples folder WMV_LIQUID_EXAMPLES names, when it is set. The outdoor light's fixture part
// (WorldLightTests.cs) runs when WMV_WORLD_LIGHT_FIXTURES names the offline fixtures, and reads the host's own objects
// from WMV_WORLD_LIGHT_HOST_DUMPS when that is set; the MCNR proof on the four Southfury tiles (SkyTerrainLightTests.cs) runs
// when WMV_MCNR_TILES names the folder holding them. The world-model context census (WorldModelLightTests.cs) runs under the
// corpus condition. The fog's census on every key of five clients (WorldFogTests.cs R9) runs when WMV_FOG_KEYS names the key
// export, its continuity on maps 0 and 1 when WMV_WORLD_LIGHT_FIXTURES is set, and WMV_FOG_PARITY_OUT has it write the rig's
// parity grid (R10).

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

        // Stage 08: the block-compressed upload's flip, synthetic, then on every whole BLP2 file of the world07 corpus.
        Console.WriteLine("== BlpBlockTests (DXT block flip for block-compressed uploads; synthetic)");
        failures += BlpBlockTests.RunAll(Console.WriteLine);
        Console.WriteLine("== BlpBlockCorpusTests (the flip on real client textures)");
        if (string.IsNullOrEmpty(corpus))
            Console.WriteLine("SKIPPED: WMV_WORLD_AUDIT_CORPUS is not set (the audit data is extracted locally, not committed)");
        else
            failures += BlpBlockTests.Run(corpus, Console.WriteLine);
        // Stage 08: the seams of the streaming region's tiles, built against one origin, on the four clients.
        Console.WriteLine("== StreamSeamTests (neighbouring tiles' shared edges against one origin)");
        if (string.IsNullOrEmpty(corpus))
            Console.WriteLine("SKIPPED: WMV_WORLD_AUDIT_CORPUS is not set (the audit data is extracted locally, not committed)");
        else
            failures += StreamSeamTests.Run(corpus, Console.WriteLine);

        // Stage 09: MH2O and MLIQ decode, the format rule and the liquid meshes, synthetic, then on the real client bytes:
        // the pinned corpus's roots and exit tiles, and the investigation's WMO liquid examples (WMV_LIQUID_EXAMPLES).
        Console.WriteLine("== LiquidTests (MH2O decode, format rule, terrain and MLIQ liquid meshes; synthetic)");
        failures += LiquidTests.RunAll(Console.WriteLine);
        Console.WriteLine("== LiquidCorpusTests (MH2O and MLIQ on real client bytes)");
        if (string.IsNullOrEmpty(corpus))
            Console.WriteLine("SKIPPED: WMV_WORLD_AUDIT_CORPUS is not set (the audit data is extracted locally, not committed)");
        else
            failures += LiquidCorpusTests.Run(corpus, Environment.GetEnvironmentVariable("WMV_LIQUID_EXAMPLES"), Console.WriteLine);

        // Stage 09 tuning: the zone light the water colours follow, its ease, and the depth and planar uv the shader reads.
        Console.WriteLine("== LiquidLightTests (zone lights: selection, point lights, ease, sky; depth in yards, planar uv)");
        failures += LiquidLightTests.RunAll(Console.WriteLine);
        Console.WriteLine("== LiquidHighTests (the High water look: formulas, the water tune, the ripple map's check)");
        failures += LiquidHighTests.RunAll(Console.WriteLine);

        // Stage 10 step 3: the outdoor light state -- the day, the selection, the clock, the ease, the switches, the graded model;
        // with WMV_WORLD_LIGHT_FIXTURES the host's sample() parity and the survey's spots on five clients, with
        // WMV_WORLD_LIGHT_HOST_DUMPS the host's own objects.
        Console.WriteLine("== WorldLightTests (outdoor light: day, selection, clock, ease, switches, model; fixtures when given)");
        failures += WorldLightTests.RunAll(Console.WriteLine);

        // Stage 10 steps 4 and 5: the sky's colour dome and program, the MCNR decode and the terrain light; with WMV_MCNR_TILES the
        // MCNR mapping proved again on the four Southfury tiles.
        Console.WriteLine("== SkyTerrainLightTests (the sky dome and program, MCNR and the terrain light; Southfury tiles when given)");
        failures += SkyTerrainLightTests.RunAll(Console.WriteLine);

        // Stage 10 step 6: the outdoor light on doodads and exterior world-model groups against the client's own programs, and the
        // context rule; with WMV_WORLD_AUDIT_CORPUS the census of the client's world models.
        Console.WriteLine("== WorldModelLightTests (doodad and world-model light, the client's programs, the contexts; census when given)");
        failures += WorldModelLightTests.RunAll(Console.WriteLine);

        // Stage 10 step 7: the client's per-pixel fog against its own terrain and M2 programs, the record from the client's keys, the
        // derived fog channels, the modes, arms and switches; with WMV_FOG_KEYS the census on every key, with WMV_WORLD_LIGHT_FIXTURES
        // the record's continuity on maps 0 and 1 of five clients.
        Console.WriteLine("== WorldFogTests (the fog: the client's programs, the record, the derived channels, modes and arms; census when given)");
        failures += WorldFogTests.RunAll(Console.WriteLine);

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
