// Program.cs -- console entry for the parser tests (see WowParserTestRunner.csproj).
//
// Exit code: the number of failures (0 = everything that ran passed). The corpus validation runs
// only when WMV_WORLD_AUDIT_CORPUS names the extracted audit data; otherwise it reports SKIPPED,
// which is not a pass. Optional argument: the path of the corpus results JSON
// (default: <corpus>/results/csharp_parsers.json). The world-map contract check runs only when WMV_MAP_CONTRACT_DIR
// names a folder of WDTs the viewer's map catalogue wrote (MapContractCheck.cs).

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

        string corpus = Environment.GetEnvironmentVariable("WMV_WORLD_AUDIT_CORPUS");
        Console.WriteLine("== WorldCorpusValidation (real client bytes)");
        if (string.IsNullOrEmpty(corpus))
            Console.WriteLine("SKIPPED: WMV_WORLD_AUDIT_CORPUS is not set (the audit data is extracted locally, not committed)");
        else
            failures += WorldCorpusValidation.Run(corpus, args.Length > 0 ? args[0] : null, Console.WriteLine);

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
