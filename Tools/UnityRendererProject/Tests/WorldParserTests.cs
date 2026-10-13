// WorldParserTests.cs
//
// Unit tests for the world-file parsers (Assets/Scripts/Wow/World): WDT, split ADT, terrain
// coordinates, holes, alpha storage, placements -- on hand-built byte arrays only (format structure,
// no game content), including truncated and malformed input. Framework-free like WowParserTests:
// `WorldParserTests.RunAll()` returns the failure count. The checks against real client bytes are
// in WorldCorpusValidation.cs.

using System;
using System.Collections.Generic;
using Wmv.Wow;
using Wmv.Wow.World;
using static Wmv.Wow.World.AdtSynthetic;

namespace Wmv.Wow.Tests
{
    public static class WorldParserTests
    {
        static int failures;
        static readonly List<string> log = new List<string>();

        static void Check(bool condition, string name)
        {
            if (condition) log.Add("  PASS  " + name);
            else { log.Add("  FAIL  " + name); failures++; }
        }

        static void Throws<T>(Action action, string name, string mustContain = null) where T : Exception
        {
            try
            {
                action();
                log.Add("  FAIL  " + name + " (expected " + typeof(T).Name + ", nothing was thrown)");
                failures++;
            }
            catch (T e)
            {
                if (mustContain != null && e.Message.IndexOf(mustContain, StringComparison.Ordinal) < 0)
                {
                    log.Add("  FAIL  " + name + " (message lacks '" + mustContain + "': " + e.Message + ")");
                    failures++;
                }
                else
                    log.Add("  PASS  " + name);
            }
            catch (Exception e)
            {
                log.Add("  FAIL  " + name + " (expected " + typeof(T).Name + ", got " + e.GetType().Name + ": " + e.Message + ")");
                failures++;
            }
        }

        // ------------------------------------------------------------------------- fixture kit

        // The byte builders (Chunk, Cat, Mver, MainTable, Maid, RootMcnk, Root, Tex, RleFill, ...) are
        // Wmv.Wow.World.AdtSynthetic, shared with the terrain-mesh tests and the player self-test.

        // ------------------------------------------------------------------------- tests

        static void ChunkWalkTests()
        {
            byte[] two = Cat(Chunk("MVER", U32Payload(18)), Chunk("MPHD", new byte[32]));
            List<WorldChunk> w = WorldChunkWalker.Walk(two, 0, two.Length, "t", "file");
            Check(w.Count == 2 && w[0].Tag == "MVER" && w[1].Tag == "MPHD" && w[1].DataOffset == 20 && w[1].Size == 32,
                  "chunk walk: reversed tags, offsets and sizes");
            Throws<WorldParseException>(() => WorldChunkWalker.Walk(Cat(two, new byte[3]), 0, two.Length + 3, "t", "file"),
                                        "chunk walk: 3 trailing bytes cannot hold a header", "trailing");
            byte[] past = Cat(Mver(), new byte[] { (byte)'D', (byte)'H', (byte)'P', (byte)'M', 0xE8, 0x03, 0, 0, 1, 2, 3 });
            Throws<WorldParseException>(() => WorldChunkWalker.Walk(past, 0, past.Length, "t", "file"),
                                        "chunk walk: size past the end is reported, not read", "claims 1000");
            byte[] huge = Cat(Mver(), new byte[] { (byte)'D', (byte)'H', (byte)'P', (byte)'M', 0xFF, 0xFF, 0xFF, 0xFF });
            Throws<WorldParseException>(() => WorldChunkWalker.Walk(huge, 0, huge.Length, "t", "file"),
                                        "chunk walk: size 0xFFFFFFFF does not overflow", "claims 4294967295");
            Throws<WorldParseException>(() => WorldChunkWalker.Walk(two, 4, two.Length + 1, "t", "file"),
                                        "chunk walk: a range past the buffer is rejected", "outside");
            try
            {
                WorldChunkWalker.Walk(past, 0, past.Length, "my.wdt", "file");
                Check(false, "chunk walk: exception carries file, chunk and offset");
            }
            catch (WorldParseException e)
            {
                Check(e.File == "my.wdt" && e.Chunk == "MPHD" && e.Offset == 12 && e.Message.Contains("0xC"),
                      "chunk walk: exception carries file, chunk and offset (" + e.Message + ")");
            }
            Check(WorldChunkWalker.Walk(new byte[0], 0, 0, "t", "file").Count == 0, "chunk walk: empty range has no chunks");
        }

        static void WdtTests()
        {
            // x = 5, y = 40: index 40*64 + 5 -- an asymmetric tile, so a swapped x/y fails.
            int idx = 40 * 64 + 5;
            byte[] tiled = Cat(Mver(), Mphd(0x3CA, 11, 12, 13, 14, 15, 16, 0), MainTable(idx), Maid(idx, 101, 102, 103, 104, 105, 106, 107, 108));
            WdtFile w = WdtParser.Parse(tiled, "tiled.wdt");
            Check(w.Kind == WdtKind.TiledTerrain && w.TileCount == 1, "wdt: one tile, tiled terrain");
            Check(w.TileExists(5, 40) && !w.TileExists(40, 5), "wdt: MAIN index is y*64 + x (no x/y swap)");
            WdtTileFiles f;
            Check(w.TryGetTileFiles(5, 40, out f) && f[WdtTileFileRole.RootAdt] == 101 && f[WdtTileFileRole.Obj0Adt] == 102 &&
                  f[WdtTileFileRole.Obj1Adt] == 103 && f[WdtTileFileRole.Tex0Adt] == 104 && f[WdtTileFileRole.LodAdt] == 105 &&
                  f[WdtTileFileRole.MapTexture] == 106 && f[WdtTileFileRole.MapTextureN] == 107 && f[WdtTileFileRole.Minimap] == 108,
                  "wdt: MAID words in the proven order root, obj0, obj1, tex0, lod, map texture, _n, minimap");
            Check(w.TryGetTileFiles(40, 5, out f) && !f.Names(WdtTileFileRole.RootAdt), "wdt: the swapped tile names nothing");
            Check(w.Header.Flags == 0x3CA && w.Header.LgtWdtFileDataId == 11 && w.Header.OccWdtFileDataId == 12 &&
                  w.Header.FogsWdtFileDataId == 13 && w.Header.MpvWdtFileDataId == 14 && w.Header.TexFileDataId == 15 &&
                  w.Header.WdlFileDataId == 16 && w.Header.Word7Raw == 0 && !w.Header.WmoOnlyFlag,
                  "wdt: MPHD flags and side-file words 1-6, word 7 raw");
            Check(w.Issues.Count == 0 && w.Version == 18, "wdt: a consistent file raises no issue");

            Throws<WorldParseException>(() => WdtParser.Parse(Cat(Mver(), Mphd(0), Chunk("MAIN", new byte[4096 * 8 - 8]))),
                                        "wdt: MAIN of the wrong size", "MAIN");
            Throws<WorldParseException>(() => WdtParser.Parse(Cat(Mver(), Mphd(0), MainTable(), Chunk("MAID", new byte[100]))),
                                        "wdt: MAID of the wrong size", "MAID");
            Throws<WorldParseException>(() => WdtParser.Parse(Cat(Mver(), Chunk("MPHD", new byte[28]), MainTable())),
                                        "wdt: MPHD shorter than 8 words", "MPHD");
            Throws<WorldParseException>(() => WdtParser.Parse(Cat(Mver(), MainTable())), "wdt: missing MPHD", "MPHD");
            Throws<WorldParseException>(() => WdtParser.Parse(Cat(Mver(), Mphd(0))), "wdt: missing MAIN", "MAIN");
            Throws<WorldParseException>(() => WdtParser.Parse(Cat(Mphd(0), Mver(), MainTable())), "wdt: MVER not first", "MVER");
            Throws<WorldParseException>(() => WdtParser.Parse(Cat(Mver(), Mphd(0), MainTable(), MainTable())), "wdt: MAIN twice", "more than once");
            Throws<WorldParseException>(() => WdtParser.Parse(new byte[0]), "wdt: empty file", "MVER");
            Throws<WorldParseException>(() => WdtParser.Parse(null), "wdt: no data");
            Throws<WorldParseException>(() => WdtParser.Parse(Cat(Mver(), Mphd(1), MainTable(), Chunk("MODF", new byte[63]))),
                                        "wdt: MODF not a whole number of 64-byte records", "64-byte");

            WdtFile longer = WdtParser.Parse(Cat(Mver(), Chunk("MPHD", new byte[36]), MainTable()));
            Check(longer.Issues.Count == 1 && longer.Issues[0].Kind == WorldIssueKind.Unexpected, "wdt: MPHD longer than 32 bytes is flagged, still read");

            // Modern WMO-only: MAID present but empty, one MODF named by FileDataID.
            WdtFile wmo = WdtParser.Parse(Cat(Mver(), Mphd(0x249), MainTable(), Maid(0), Chunk("MODF", ModfRecord(108631, 0xFFFFFFFF, 0x8, 1024))));
            WorldFileReference r;
            Check(wmo.Kind == WdtKind.GlobalWmo && wmo.GlobalWmos.Length == 1 && wmo.TryGetGlobalWmoReference(0, out r) &&
                  !r.IsPath && r.FileDataId == 108631 && wmo.Issues.Count == 0, "wdt: modern WMO-only map names its WMO by FileDataID");
            WmoPlacement p = wmo.GlobalWmos[0];
            Check(p.UniqueId == 0xFFFFFFFF && p.Position.A == 100f && p.Rotation.C == 105f && p.ExtentsMin.A == 10f && p.ExtentsMax.C == 60f &&
                  p.Word58 == 3 && p.Word60 == 4 && p.ScaleRaw == 1024 && p.Source.TileX == -1 && p.Source.Container == "WDT MODF",
                  "wdt: MODF record fields at their offsets");

            // Old WMO-only form: MWMO path, MODF name 0, flag 0.
            byte[] mwmo = Chunk("MWMO", new byte[] { (byte)'a', (byte)'\\', (byte)'b', (byte)'.', (byte)'w', (byte)'m', (byte)'o', 0 });
            WdtFile old = WdtParser.Parse(Cat(Mver(), Mphd(0x41), MainTable(), mwmo, Chunk("MODF", ModfRecord(0, 1, 0, 0))));
            Check(old.Kind == WdtKind.GlobalWmo && !old.HasMaid && old.TryGetGlobalWmoReference(0, out r) && r.IsPath && r.Path == "a\\b.wmo",
                  "wdt: old WMO-only form names its WMO by MWMO path");

            WdtFile bare = WdtParser.Parse(Cat(Mver(), Mphd(0), MainTable()));
            Check(bare.Kind == WdtKind.Empty && bare.Issues.Count == 0 && !bare.HasMaid, "wdt: bare MVER MPHD MAIN is an empty map");

            WdtFile noWords = WdtParser.Parse(Cat(Mver(), Mphd(0), MainTable(idx), Maid(0)));
            Check(noWords.Issues.Count == 1 && noWords.Issues[0].Kind == WorldIssueKind.Unexpected,
                  "wdt: MAIN bit 0x1 without MAID words is flagged, not hidden");
            WdtFile noMaid = WdtParser.Parse(Cat(Mver(), Mphd(0), MainTable(idx)));
            Check(noMaid.Issues.Count == 1 && !noMaid.TryGetTileFiles(5, 40, out f), "wdt: a tile without any MAID is flagged");
            WdtFile flagged = WdtParser.Parse(Cat(Mver(), Mphd(1), MainTable(idx), Maid(idx, 1, 2, 3, 4, 0, 0, 0, 5)));
            Check(flagged.Issues.Count == 1 && flagged.Issues[0].Chunk == "MPHD", "wdt: MPHD bit 0x1 on a tiled map is flagged");
            WdtFile mai2 = WdtParser.Parse(Cat(Mver(), Mphd(0), MainTable(idx), Maid(idx, 1, 2, 3, 4), Chunk("MAI2", new byte[4096 * 32])));
            Check(mai2.Unsupported.Length == 1 && mai2.Unsupported[0].Tag == "MAI2" && mai2.Issues.Count == 0,
                  "wdt: MAI2 is listed as unsupported, not decoded");
            Throws<ArgumentOutOfRangeException>(() => w.TileExists(64, 0), "wdt: tile index out of range");
        }

        static void CoordinateTests()
        {
            double x, y, x2, y2;
            TerrainCoordinates.ChunkOrigin(32, 32, 0, 0, out x, out y);
            Check(x == 0 && y == 0, "coords: tile 32,32 chunk 0,0 is the map centre");
            TerrainCoordinates.ChunkOrigin(31, 30, 3, 5, out x, out y);
            Check(Math.Abs(x - (2 * TerrainCoordinates.TileSize - 5 * TerrainCoordinates.ChunkSize)) < 1e-9 &&
                  Math.Abs(y - (TerrainCoordinates.TileSize - 3 * TerrainCoordinates.ChunkSize)) < 1e-9,
                  "coords: x = (32-ty)T - iy C, y = (32-tx)T - ix C");
            bool seams = true;
            for (int c = 0; c <= 8; c++)
            {
                TerrainCoordinates.OuterVertex(30, 47, 4, 7, 8, c, out x, out y);
                TerrainCoordinates.OuterVertex(30, 47, 4, 8, 0, c, out x2, out y2);
                seams &= x == x2 && y == y2;
                TerrainCoordinates.OuterVertex(30, 47, 15, 9, c, 8, out x, out y);
                TerrainCoordinates.OuterVertex(31, 47, 0, 9, c, 0, out x2, out y2);
                seams &= x == x2 && y == y2;
                TerrainCoordinates.OuterVertex(30, 47, 6, 15, 8, c, out x, out y);
                TerrainCoordinates.OuterVertex(30, 48, 6, 0, 0, c, out x2, out y2);
                seams &= x == x2 && y == y2;
            }
            Check(seams, "coords: shared seam vertices are bit-identical inside a tile and across tile edges");
            TerrainCoordinates.OuterVertex(30, 47, 4, 7, 0, 0, out x, out y);
            TerrainCoordinates.InnerVertex(30, 47, 4, 7, 0, 0, out x2, out y2);
            Check(Math.Abs(x - x2 - TerrainCoordinates.UnitSize / 2) < 1e-9 && Math.Abs(y - y2 - TerrainCoordinates.UnitSize / 2) < 1e-9,
                  "coords: inner vertex sits half a cell inside (x and y both fall)");
            Throws<ArgumentOutOfRangeException>(() => TerrainCoordinates.OuterVertex(0, 0, 0, 0, 9, 0, out x, out y), "coords: outer row 9 rejected");
            Throws<ArgumentOutOfRangeException>(() => TerrainCoordinates.InnerVertex(0, 0, 0, 0, 0, 8, out x, out y), "coords: inner column 8 rejected");
            Throws<ArgumentOutOfRangeException>(() => TerrainCoordinates.ChunkOrigin(64, 0, 0, 0, out x, out y), "coords: tile 64 rejected");
            Check(TerrainVertexLayout.Outer(8, 8) == 144 && TerrainVertexLayout.Inner(7, 7) == 135 && TerrainVertexLayout.Outer(1, 0) == 17 &&
                  TerrainVertexLayout.Inner(0, 0) == 9 && !TerrainVertexLayout.IsOuter(9) && TerrainVertexLayout.IsOuter(17) &&
                  TerrainVertexLayout.IsOuter(8) && !TerrainVertexLayout.IsOuter(16),
                  "layout: outer 17r + c, inner 17r + 9 + c");
            int outer = 0, inner = 0;
            for (int i = 0; i < 145; i++) { if (TerrainVertexLayout.IsOuter(i)) outer++; else inner++; }
            Check(outer == 81 && inner == 64, "layout: 81 outer and 64 inner vertices");
        }

        static void HoleTests()
        {
            var none = new TerrainHoles { Kind = HoleMaskKind.HighResolution, HighResolutionBits = 0 };
            bool all = true;
            for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) all &= none.IsHole(r, c) == false;
            Check(all && none.HasAnyHole == false, "holes: zero mask has no hole");
            var one = new TerrainHoles { Kind = HoleMaskKind.HighResolution, HighResolutionBits = 1UL << 21 };
            Check(one.IsHole(2, 5) == true && one.IsHole(5, 2) == false && one.IsHole(2, 4) == false, "holes: bit 21 is row 2, column 5 (not transposed)");
            var corner = new TerrainHoles { Kind = HoleMaskKind.HighResolution, HighResolutionBits = 1UL << 63 };
            Check(corner.IsHole(7, 7) == true && corner.IsHole(0, 0) == false, "holes: bit 63 is row 7, column 7 (LSB first, not MSB first)");
            var first = new TerrainHoles { Kind = HoleMaskKind.HighResolution, HighResolutionBits = 1UL };
            Check(first.IsHole(0, 0) == true && first.IsHole(7, 7) == false, "holes: bit 0 is row 0, column 0");
            var row = new TerrainHoles { Kind = HoleMaskKind.HighResolution, HighResolutionBits = 0xFFUL << 24 };
            bool rowOk = true;
            for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) rowOk &= row.IsHole(r, c) == (r == 3);
            Check(rowOk, "holes: 0xFF << 24 is exactly row 3");
            var full = new TerrainHoles { Kind = HoleMaskKind.HighResolution, HighResolutionBits = ulong.MaxValue };
            bool fullOk = true;
            for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) fullOk &= full.IsHole(r, c) == true;
            Check(fullOk, "holes: full mask is all holes");
            var low = new TerrainHoles { Kind = HoleMaskKind.LowResolutionUnsupported, LowResolutionRaw = 0x0010 };
            Check(low.IsHole(0, 0) == null && low.HasAnyHole == true, "holes: a non-zero low-resolution mask is not decoded (null)");
            var lowZero = new TerrainHoles { Kind = HoleMaskKind.LowResolutionUnsupported, LowResolutionRaw = 0 };
            Check(lowZero.IsHole(3, 3) == false, "holes: a zero low-resolution mask has no hole");
            Throws<ArgumentOutOfRangeException>(() => full.IsHole(8, 0), "holes: row 8 rejected");
            Throws<ArgumentOutOfRangeException>(() => full.IsHole(0, -1), "holes: column -1 rejected");

            ulong mask = (1UL << 21) | (1UL << 63);
            AdtRoot root = AdtParser.ParseRoot(Root(30, 47, i => i == 17 ? RootMcnk(17, 0x10000, mask, false, 30, 47) : null), 30, 47);
            Check(root.Mcnk[17].Holes.Kind == HoleMaskKind.HighResolution && root.Mcnk[17].Holes.HighResolutionBits == mask &&
                  root.Mcnk[17].Holes.IsHole(2, 5) == true && root.Mcnk[16].Holes.HasAnyHole == false,
                  "holes: root parser reads the 64-bit mask at header +20 when flag 0x10000 is set");
            AdtRoot lowRoot = AdtParser.ParseRoot(Root(30, 47, i => i == 3 ? LowHoleMcnk(30, 47) : null), 30, 47);
            Check(lowRoot.Mcnk[3].Holes.Kind == HoleMaskKind.LowResolutionUnsupported && lowRoot.Mcnk[3].Holes.LowResolutionRaw == 0x8001 &&
                  lowRoot.Issues.Exists(i => i.Kind == WorldIssueKind.Unsupported && i.Chunk == "MCNK 3"),
                  "holes: a low-resolution mask without flag 0x10000 is reported unsupported");
        }

        static byte[] LowHoleMcnk(int tileX, int tileY)
        {
            byte[] c = RootMcnk(3, 0, 0, false, tileX, tileY);
            PutU16(c, 8 + 60, 0x8001);
            return c;
        }

        static void RootTests()
        {
            byte[] data = Root(30, 47, i => i == 5 ? RootMcnk(5, 0x10000, 0, true, 30, 47) : null);
            AdtRoot r = AdtParser.ParseRoot(data, 30, 47, "root 30_47");
            int decoded = 0;
            foreach (AdtRootChunk c in r.Mcnk) if (c.Status == WorldDataStatus.Decoded) decoded++;
            Check(r.Version == 18 && r.Mcnk.Length == 256 && decoded == 256 && r.Issues.Count == 0, "root: 256 MCNKs decoded, no issue");
            AdtRootChunk m = r.Mcnk[37];
            double x, y;
            TerrainCoordinates.ChunkOrigin(30, 47, 37 % 16, 37 / 16, out x, out y);
            Check(m.IndexX == 5 && m.IndexY == 2 && m.AreaId == 12 && m.StoredPosition.A == (float)x && m.StoredPosition.B == (float)y &&
                  m.StoredPosition.C == 50f && m.RawHeader.Length == 128, "root: header fields at their offsets");
            Check(m.Heights.Length == 145 && m.Heights[0] == 0f && m.Heights[144] == 36f && m.NormalsRaw.Length == 435 &&
                  m.NormalsTrailingRaw.Length == 13 && m.McseSize == 0 && !m.HasVertexColors, "root: MCVT heights, raw MCNR, MCSE");
            Check(r.Mcnk[5].HasVertexColors && r.Mcnk[5].VertexColorsRaw.Length == 580, "root: MCCV kept raw with flag 0x40");

            Throws<WorldParseException>(() => AdtParser.ParseRoot(Cat(Mver(), RootMcnk(0, 0x10000, 0, false, 30, 47)), 30, 47),
                                        "root: fewer than 256 MCNKs", "256");
            Throws<WorldParseException>(() => AdtParser.ParseRoot(Root(30, 47).AsSpanTrimmed(9), 30, 47), "root: truncated file");

            AdtRoot small = AdtParser.ParseRoot(Root(30, 47, i => i == 9 ? Chunk("MCNK", new byte[100]) : null), 30, 47);
            Check(small.Mcnk[9].Status == WorldDataStatus.Malformed && small.Mcnk[10].Status == WorldDataStatus.Decoded &&
                  small.Issues.Exists(i => i.Chunk == "MCNK 9" && i.Reason.Contains("smaller than its 128-byte header")),
                  "root: an MCNK smaller than its header is malformed alone");

            byte[] swapped = RootMcnk(20, 0x10000, 0, false, 30, 47);
            PutU32(swapped, 8 + 4, 9);
            AdtRoot sw = AdtParser.ParseRoot(Root(30, 47, i => i == 20 ? swapped : null), 30, 47);
            Check(sw.Mcnk[20].Status == WorldDataStatus.Malformed && sw.Issues.Exists(i => i.Reason.Contains("header index")),
                  "root: header index that is not the chunk's position is malformed");

            byte[] shortMcvt = Chunk("MCNK", Cat(new byte[128], Chunk("MCVT", new byte[576]), Chunk("MCNR", new byte[448])));
            PutU32(shortMcvt, 8 + 0, 0x10000);
            PutU32(shortMcvt, 8 + 4, 0); PutU32(shortMcvt, 8 + 8, 0);
            AdtRoot sm = AdtParser.ParseRoot(Root(30, 47, i => i == 0 ? shortMcvt : null), 30, 47);
            Check(sm.Mcnk[0].Status == WorldDataStatus.Malformed && sm.Mcnk[0].Heights == null && sm.Mcnk[1].Status == WorldDataStatus.Decoded,
                  "root: MCVT of the wrong size is malformed");

            byte[] overflowSub = Chunk("MCNK", Cat(new byte[128], new byte[] { (byte)'T', (byte)'V', (byte)'C', (byte)'M', 0x00, 0x10, 0, 0 }));
            PutU32(overflowSub, 8 + 4, 1); PutU32(overflowSub, 8 + 8, 0);
            AdtRoot ov = AdtParser.ParseRoot(Root(30, 47, i => i == 1 ? overflowSub : null), 30, 47);
            Check(ov.Mcnk[1].Status == WorldDataStatus.Malformed && ov.Issues.Exists(i => i.Chunk == "MCVT" && i.Reason.Contains("claims 4096")),
                  "root: a sub-chunk running past its MCNK is malformed, with offset");

            AdtRoot nan = AdtParser.ParseRoot(Root(30, 47, i => i == 2 ? RootMcnk(2, 0x10000, 0, false, 30, 47, k => k == 7 ? float.NaN : 1f) : null), 30, 47);
            Check(nan.Mcnk[2].Status == WorldDataStatus.Malformed && nan.Issues.Exists(i => i.Reason.Contains("non-finite")),
                  "root: a non-finite height is malformed");

            byte[] mismatched = RootMcnk(4, 0x10000, 0, false, 30, 47);
            PutU32(mismatched, 8, 0x10040);  // flag 0x40 but no MCCV
            AdtRoot mm = AdtParser.ParseRoot(Root(30, 47, i => i == 4 ? mismatched : null), 30, 47);
            Check(mm.Issues.Exists(i => i.Kind == WorldIssueKind.Unexpected && i.Reason.Contains("flag 0x40")), "root: flag 0x40 without MCCV is flagged");

            // MH2O: header table + one instance in chunk 3; then counts that run past the chunk.
            var mh2o = new byte[256 * 12 + 24];
            PutU32(mh2o, 3 * 12, 256 * 12);
            PutU32(mh2o, 3 * 12 + 4, 1);
            PutU16(mh2o, 256 * 12, 5);
            PutU16(mh2o, 256 * 12 + 2, 42);
            PutF32(mh2o, 256 * 12 + 4, 1.5f);
            mh2o[256 * 12 + 14] = 8;
            AdtRoot liq = AdtParser.ParseRoot(Root(30, 47, null, Chunk("MH2O", mh2o)), 30, 47);
            Check(liq.Liquids != null && liq.Liquids.Status == WorldDataStatus.PartiallyDecoded && liq.Liquids.Instances[3].Length == 1 &&
                  liq.Liquids.Instances[3][0].Word0 == 5 && liq.Liquids.Instances[3][0].Word2 == 42 && liq.Liquids.Instances[3][0].Float4 == 1.5f &&
                  liq.Liquids.Instances[3][0].Byte14 == 8 && liq.Liquids.Instances[0].Length == 0,
                  "liquids: MH2O header table and instance kept raw, partially decoded");
            var bad = (byte[])mh2o.Clone();
            PutU32(bad, 3 * 12 + 4, 0xFFFFFFFF);
            AdtRoot badLiq = AdtParser.ParseRoot(Root(30, 47, null, Chunk("MH2O", bad)), 30, 47);
            Check(badLiq.Liquids.Status == WorldDataStatus.Malformed && badLiq.Issues.Exists(i => i.Chunk == "MH2O") &&
                  badLiq.Mcnk[0].Status == WorldDataStatus.Decoded, "liquids: an impossible instance count is malformed, terrain unaffected");
            var past = (byte[])mh2o.Clone();
            PutU32(past, 3 * 12, 256 * 12 + 8);
            AdtRoot pastLiq = AdtParser.ParseRoot(Root(30, 47, null, Chunk("MH2O", past)), 30, 47);
            Check(pastLiq.Liquids.Status == WorldDataStatus.Malformed, "liquids: an instance past the chunk is malformed");
            AdtRoot shortLiq = AdtParser.ParseRoot(Root(30, 47, null, Chunk("MH2O", new byte[100])), 30, 47);
            Check(shortLiq.Liquids.Status == WorldDataStatus.Malformed, "liquids: MH2O shorter than its header table is malformed");
            AdtRoot extra = AdtParser.ParseRoot(Root(30, 47, null, Chunk("MFBO", new byte[36])), 30, 47);
            Check(extra.Unsupported.Length == 1 && extra.Unsupported[0].Tag == "MFBO" && extra.Issues.Count == 0, "root: MFBO listed as unsupported");
        }

        static void AlphaTests()
        {
            string err;
            byte[] fill = RleFill(200);
            AlphaMap m = AdtParser.DecodeRle(fill, 0, fill.Length, out err);
            bool allv = m != null;
            if (m != null) foreach (byte b in m.Samples) allv &= b == 200;
            Check(allv && m.Storage == AlphaStorage.Compressed && m.StoredBytes == fill.Length && m.BitDepth == 8, "alpha: RLE fill runs");
            var copy = new List<byte>();
            for (int run = 0; run < 33; run++)
            {
                int n = run < 32 ? 127 : 32;
                copy.Add((byte)n);
                for (int k = 0; k < n; k++) copy.Add((byte)((run * 127 + k) & 0xFF));
            }
            m = AdtParser.DecodeRle(copy.ToArray(), 0, copy.Count, out err);
            bool seq = m != null;
            if (m != null) for (int i = 0; i < 4096; i++) seq &= m.Samples[i] == (byte)(i & 0xFF);
            Check(seq && m.Sample(1, 0) == 64 && m.Sample(0, 5) == 5, "alpha: RLE copy runs, row-major addressing (row*64 + col)");
            Check(AdtParser.DecodeRle(fill, 0, fill.Length - 1, out err) == null && err.Contains("no value byte"), "alpha: RLE missing value byte");
            Check(AdtParser.DecodeRle(fill, 0, 10, out err) == null && err.Contains("ends after"), "alpha: RLE truncated");
            byte[] cross = Cat(RleFill(1, 4095), new byte[] { 0x82, 5 });
            Check(AdtParser.DecodeRle(cross, 0, cross.Length, out err) == null && err.Contains("crosses"), "alpha: RLE run crossing 4096 is rejected");
            byte[] zeros = new byte[64];
            Check(AdtParser.DecodeRle(zeros, 0, zeros.Length, out err) == null, "alpha: zero-count runs terminate (no infinite loop)");
            byte[] copyPast = new byte[] { 0x7F, 1, 2 };
            Check(AdtParser.DecodeRle(copyPast, 0, copyPast.Length, out err) == null && err.Contains("passes the end"), "alpha: RLE copy past MCAL");

            var raw4 = new byte[2048];
            raw4[0] = 0x21;
            raw4[2047] = 0xF0;
            m = AdtParser.DecodeRaw(raw4, 0, 2048, 0, 2048, out err);
            Check(m != null && m.Storage == AlphaStorage.Uncompressed4 && m.BitDepth == 4 && m.Samples[0] == 1 && m.Samples[1] == 2 &&
                  m.Samples[4094] == 0 && m.Samples[4095] == 15, "alpha: 4-bit maps are low nibble first, samples kept 0..15");
            byte[] eight;
            string reason;
            Check(!m.TryGetEightBit(out eight, out reason) && eight == null && reason.Contains("U19"), "alpha: no 4-bit to 8-bit expansion is guessed");
            var raw8 = new byte[4096];
            raw8[4095] = 9;
            m = AdtParser.DecodeRaw(raw8, 0, 4096, 0, 4096, out err);
            Check(m != null && m.Storage == AlphaStorage.Uncompressed8 && m.Samples[4095] == 9 && m.TryGetEightBit(out eight, out reason) && eight[4095] == 9,
                  "alpha: 8-bit uncompressed map");
            Check(AdtParser.DecodeRaw(raw8, 0, 4096, 0, 3000, out err) == null && err.Contains("3000"), "alpha: an uncompressed gap of 3000 bytes is rejected");
            Check(AdtParser.DecodeRaw(raw8, 0, 4096, 100, 50, out err) == null, "alpha: a negative gap is rejected");

            // Through tex0: layer 0 base (no alpha), layer 1 RLE at +0, layer 2 raw 8-bit at +rleLen to the MCAL end -- mixed storage.
            byte[] mcal = Cat(RleFill(7), raw8);
            int rleLen = RleFill(7).Length;
            byte[] tex = Tex(i => i == 0 ? TexMcnk(Mcly(0, 0, 0, 0, 1, 0x300, 0, 0, 2, 0x100, (uint)rleLen, 0), Chunk("MCAL", mcal)) : null);
            AdtTex t = AdtParser.ParseTex(tex, "tex");
            AdtTexChunk c0 = t.Mcnk[0];
            Check(c0.Status == WorldDataStatus.Decoded && c0.Alpha[0] == null && c0.Alpha[1].Storage == AlphaStorage.Compressed &&
                  c0.Alpha[1].Samples[100] == 7 && c0.Alpha[2].Storage == AlphaStorage.Uncompressed8 && c0.Alpha[2].Samples[4095] == 9 &&
                  t.Issues.Count == 0 && t.DiffuseTextureIds.Length == 4 && t.DiffuseTextureIds[2] == 1002,
                  "alpha: mixed compressed and uncompressed layers back to back");
            Check(c0.Layers.Length == 3 && c0.Layers[1].Flags == 0x300 && c0.Layers[2].AlphaOffset == (uint)rleLen && c0.Layers[2].TextureIndex == 2,
                  "layers: MCLY texture index, flags, alpha offset");

            byte[] gap = Tex(i => i == 0 ? TexMcnk(Mcly(0, 0, 0, 0, 1, 0x100, 8, 0), Chunk("MCAL", new byte[4104])) : null);
            AdtTex tg = AdtParser.ParseTex(gap);
            Check(tg.Mcnk[0].Status == WorldDataStatus.Malformed && tg.Mcnk[0].Alpha == null && tg.Issues.Exists(i => i.Reason.Contains("expected +0")),
                  "alpha: maps not starting at 0 are malformed");
            byte[] trailing = Tex(i => i == 0 ? TexMcnk(Mcly(0, 0, 0, 0, 1, 0x300, 0, 0), Chunk("MCAL", Cat(RleFill(3), new byte[5]))) : null);
            Check(AdtParser.ParseTex(trailing).Mcnk[0].Status == WorldDataStatus.Malformed, "alpha: trailing MCAL bytes after an RLE map are malformed");
            byte[] flag0 = Tex(i => i == 0 ? TexMcnk(Mcly(0, 0x100, 0, 0, 1, 0x100, 0, 0), Chunk("MCAL", raw8)) : null);
            AdtTex tf = AdtParser.ParseTex(flag0);
            Check(tf.Mcnk[0].Status == WorldDataStatus.Malformed && tf.Issues.Exists(i => i.Kind == WorldIssueKind.Unexpected && i.Reason.Contains("0x100")),
                  "alpha: flag 0x100 on the first layer breaks the proven correlation: flagged, not decoded");
            byte[] noAlphaFlag = Tex(i => i == 0 ? TexMcnk(Mcly(0, 0, 0, 0, 1, 0, 0, 0), Chunk("MCAL", raw8)) : null);
            Check(AdtParser.ParseTex(noAlphaFlag).Mcnk[0].Status == WorldDataStatus.Malformed, "alpha: a second layer without 0x100 is flagged, not decoded");
            byte[] badTex = Tex(i => i == 0 ? TexMcnk(Mcly(9, 0, 0, 0), null) : null);
            AdtTex tb = AdtParser.ParseTex(badTex);
            Check(tb.Mcnk[0].Status == WorldDataStatus.Malformed && tb.Issues.Exists(i => i.Reason.Contains("MDID has 4")),
                  "layers: a texture index outside MDID is an invalid reference");
            byte[] badMcly = Tex(i => i == 0 ? TexMcnk(Chunk("MCLY", new byte[20]), null) : null);
            Check(AdtParser.ParseTex(badMcly).Mcnk[0].Status == WorldDataStatus.Malformed, "layers: MCLY of 20 bytes is malformed");
            byte[] noMcal = Tex(i => i == 0 ? TexMcnk(Mcly(0, 0, 0, 0, 1, 0x100, 0, 0), null) : null);
            Check(AdtParser.ParseTex(noMcal).Mcnk[0].Status == WorldDataStatus.Malformed, "alpha: two layers but no MCAL is malformed");
            AdtTex one = AdtParser.ParseTex(Tex(i => null));
            Check(one.Mcnk[200].Status == WorldDataStatus.Decoded && one.Mcnk[200].Layers.Length == 1 && one.Mcnk[200].Alpha[0] == null && one.Issues.Count == 0,
                  "alpha: a single layer needs no MCAL");
            byte[] fourBit = Tex(i => i == 1 ? TexMcnk(Mcly(0, 0, 0, 0, 1, 0x100, 0, 0, 2, 0x100, 2048, 0), Chunk("MCAL", Cat(raw4, raw4))) : null);
            AdtTex t4 = AdtParser.ParseTex(fourBit);
            Check(t4.Mcnk[1].Status == WorldDataStatus.Decoded && t4.Mcnk[1].Alpha[1].BitDepth == 4 && t4.Mcnk[1].Alpha[2].BitDepth == 4,
                  "alpha: 2048-byte spacing decodes as 4-bit (bit depth from spacing, not MPHD)");
            var badMdid = new List<byte[]> { Mver(), Chunk("MDID", new byte[5]) };
            for (int i = 0; i < 256; i++) badMdid.Add(TexMcnk(Mcly(0, 0, 0, 0), null));
            AdtTex md = AdtParser.ParseTex(Cat(badMdid.ToArray()));
            Check(md.DiffuseTextureIds.Length == 0 && md.Issues.Exists(i => i.Kind == WorldIssueKind.Malformed && i.Chunk == "MDID") &&
                  md.Mcnk[0].Status == WorldDataStatus.Malformed, "tex0: an MDID that is not whole u32s is reported, not fatal; its layers cannot resolve");
        }

        static void PlacementTests()
        {
            byte[] mddf = Cat(MddfRecord(189678, 7, 100f, 20f, 300f, 1f, 2f, 3f, 1024, 0x40), MddfRecord(5, 8, 0f, 0f, 0f, 0f, 0f, 0f, 512, 0));
            byte[] modf = ModfRecord(112871, 9, 0x0C, 1024);
            var parts = new List<byte[]> { Mver(), Chunk("MDDF", mddf), Chunk("MODF", modf) };
            for (int i = 0; i < 256; i++) parts.Add(Chunk("MCNK", i == 3 ? Cat(Chunk("MCRD", U32Payload(0, 1)), Chunk("MCRW", U32Payload(0))) : new byte[0]));
            AdtObj0 o = AdtParser.ParseObj0(Cat(parts.ToArray()), 31, 48);
            DoodadPlacement d = o.Doodads[0];
            Check(o.Doodads.Length == 2 && d.NameId == 189678 && d.UniqueId == 7 && d.Position.A == 100f && d.Position.C == 300f &&
                  d.Rotation.B == 2f && d.ScaleRaw == 1024 && d.Scale == 1.0 && d.Flags == 0x40 && d.NameKind == PlacementNameKind.FileDataId &&
                  d.Source.TileX == 31 && d.Source.TileY == 48 && d.Source.RecordIndex == 0 && d.Source.Container == "obj0 MDDF",
                  "placements: MDDF fields, scale u16/1024, source kept");
            Check(o.Doodads[1].Scale == 0.5 && o.Doodads[1].NameKind == PlacementNameKind.NameTableEntry, "placements: without flag 0x40 the name is not called a FileDataID");
            Check(o.Wmos.Length == 1 && o.Wmos[0].NameKind == PlacementNameKind.FileDataId && o.Wmos[0].ExtentsMax.A == 40f && o.Mcnk.Length == 256 &&
                  o.Mcnk[3].McrdRaw.Length == 2 && o.Mcnk[3].McrwRaw.Length == 1, "placements: MODF and MCNK reference lists");
            Check(o.Issues.Count == 1 && o.Issues[0].Kind == WorldIssueKind.Unexpected && o.Issues[0].Chunk == "MDDF" && o.Issues[0].Offset == 12 + 8 + 36,
                  "placements: a record without flag 0x40 and no name table is flagged, at that record");
            Throws<WorldParseException>(() => AdtParser.ParseObj0(Cat(Mver(), Chunk("MDDF", new byte[37])), 0, 0), "placements: MDDF of 37 bytes", "36-byte");
            Throws<WorldParseException>(() => AdtParser.ParseObj0(Cat(Mver(), Chunk("MDDF", MddfRecord(1, 2, float.PositiveInfinity, 0, 0, 0, 0, 0, 0, 0))), 0, 0),
                                        "placements: a non-finite position is rejected", "non-finite");

            // obj1: MLDD repeats MDDF; MLDX one 28-byte box per record.
            var box = new byte[28];
            PutF32(box, 0, -1f); PutF32(box, 12, 1f); PutF32(box, 24, 3.5f);
            AdtObj1 o1 = AdtParser.ParseObj1(Cat(Mver(), Chunk("MLDD", MddfRecord(189678, 7, 100f, 20f, 300f, 1f, 2f, 3f, 1024, 0x60)), Chunk("MLDX", box),
                                                 Chunk("MLMD", new byte[8])), 32, 48);
            Check(o1.Doodads.Length == 1 && o1.DoodadBoxes.Length == 1 && o1.DoodadBoxes[0].Min.A == -1f && o1.DoodadBoxes[0].Max.A == 1f &&
                  o1.DoodadBoxes[0].Value6Raw == 3.5f && o1.Unsupported.Length == 1 && o1.Doodads[0].SamePlacementAs(d) && o1.Doodads[0].Flags != d.Flags,
                  "placements: MLDD repeats MDDF (flags differ), MLDX boxes");
            AdtObj1 bad = AdtParser.ParseObj1(Cat(Mver(), Chunk("MLDD", MddfRecord(1, 1, 0, 0, 0, 0, 0, 0, 1024, 0x40)), Chunk("MLDX", new byte[56])), 0, 0);
            Check(bad.DoodadBoxes == null && bad.Issues.Exists(i => i.Chunk == "MLDX"), "placements: MLDX not 28 bytes per MLDD record is malformed");

            // Transform: W = (32T - p2, 32T - p0, p1); R = Rz(a1 + 180) Ry(a0) Rx(a2), Rx first.
            double half = 32 * TerrainCoordinates.TileSize;
            WorldPoint w = PlacementTransform.ToWorld(new WorldFloat3(10f, 5f, 20f));
            Check(Math.Abs(w.X - (half - 20)) < 1e-9 && Math.Abs(w.Y - (half - 10)) < 1e-9 && w.Z == 5, "transform: file position to world");
            var zero = new WorldFloat3(0, 0, 0);
            WorldPoint q = PlacementTransform.Transform(zero, zero, 1.0, new WorldPoint(1, 0, 0));
            Check(Near(q, half - 1, half, 0), "transform: zero rotation is a 180-degree yaw (model +x -> world -x)");
            q = PlacementTransform.Transform(zero, new WorldFloat3(0, 90, 0), 1.0, new WorldPoint(1, 0, 0));
            Check(Near(q, half, half - 1, 0), "transform: a1 = 90 turns model +x to world -y");
            q = PlacementTransform.Transform(zero, new WorldFloat3(90, -180, 0), 1.0, new WorldPoint(1, 0, 0));
            Check(Near(q, half, half, -1), "transform: a0 = 90 turns about world Y (model +x -> world -z)");
            q = PlacementTransform.Transform(zero, new WorldFloat3(0, -180, 90), 1.0, new WorldPoint(0, 1, 0));
            Check(Near(q, half, half, 1), "transform: a2 = 90 turns about world X (model +y -> world +z)");
            q = PlacementTransform.Transform(zero, new WorldFloat3(90, -180, 90), 2.0, new WorldPoint(0, 1, 0));
            Check(Near(q, half + 2, half, 0), "transform: Rx acts first, then Ry; scale multiplies (model +y -> world +2x)");
            WorldPoint mn, mx;
            PlacementTransform.ExtentsToWorld(new WorldFloat3(1, 2, 3), new WorldFloat3(4, 5, 6), out mn, out mx);
            Check(Near(mn, half - 6, half - 4, 2) && Near(mx, half - 3, half - 1, 5), "transform: MODF extents to the world box");
            PlacementTransform.PlacedBox(zero, zero, 1.0, new WorldPoint(-1, -2, -3), new WorldPoint(1, 2, 3), out mn, out mx);
            Check(Near(mn, half - 1, half - 2, -3) && Near(mx, half + 1, half + 2, 3), "transform: placed box of all 8 corners");
        }

        static bool Near(WorldPoint p, double x, double y, double z)
        {
            return Math.Abs(p.X - x) < 1e-9 && Math.Abs(p.Y - y) < 1e-9 && Math.Abs(p.Z - z) < 1e-9;
        }

        static void TileTests()
        {
            byte[] root = Root(30, 47);
            byte[] brokenObj0 = Cat(Mver(), Chunk("MDDF", new byte[37]));
            AdtTile t = AdtParser.ParseTile(30, 47, new uint[] { 1, 2, 3, 0, 5, 6, 7, 8 }, root, null, brokenObj0, null, Cat(Mver(), Chunk("MLHD", new byte[4])));
            Check(t.Root.State == AdtPartState.Parsed && t.Root.Value.Mcnk.Length == 256, "tile: root parsed");
            Check(t.Tex0.State == AdtPartState.NotNamed, "tile: tex0 not named by MAID is NotNamed, not an error");
            Check(t.Obj0.State == AdtPartState.Malformed && t.Obj0.Error.Contains("MDDF"), "tile: malformed obj0 reported on its own");
            Check(t.Obj1.State == AdtPartState.NotSupplied, "tile: named but missing obj1 is NotSupplied");
            Check(t.Lod.State == AdtPartState.Parsed && t.Lod.Value.Issues.Count == 1 && t.Lod.Value.Issues[0].Kind == WorldIssueKind.Unsupported,
                  "tile: lod is an inventory, reported unsupported");
            AdtTile u = AdtParser.ParseTile(30, 47, null, null, null, null, null, null);
            Check(u.Root.State == AdtPartState.NotSupplied && u.Tex0.State == AdtPartState.NotSupplied, "tile: with unknown MAID every absent part is NotSupplied");
        }

        static void SafetyTests()
        {
            // Every prefix of a valid WDT and ADT root either parses or fails with a WorldParseException.
            byte[] wdt = Cat(Mver(), Mphd(0), MainTable(5), Maid(5, 1, 2, 3, 4));
            byte[] root = Root(30, 47, i => i == 0 ? RootMcnk(0, 0x10000, 1, true, 30, 47) : null);
            bool ok = true;
            string bad = null;
            foreach (int cut in new[] { 0, 1, 7, 8, 11, 12, 20, 40, 1000, 32788, wdt.Length - 1 })
            {
                try { WdtParser.Parse(wdt.AsSpanTrimmed(wdt.Length - cut)); }
                catch (WorldParseException) { }
                catch (Exception e) { ok = false; bad = "wdt cut " + cut + ": " + e.GetType().Name; }
            }
            var rnd = new Random(12345);
            for (int n = 0; n < 300; n++)
            {
                byte[] b = (byte[])root.Clone();
                for (int k = 0; k < 8; k++) b[rnd.Next(b.Length)] = (byte)rnd.Next(256);
                try { AdtParser.ParseRoot(b, 30, 47); }
                catch (WorldParseException) { }
                catch (Exception e) { ok = false; bad = "root mutation " + n + ": " + e.GetType().Name + " " + e.Message; break; }
            }
            byte[] tex = Tex(i => i == 0 ? TexMcnk(Mcly(0, 0, 0, 0, 1, 0x300, 0, 0), Chunk("MCAL", RleFill(9))) : null);
            for (int n = 0; n < 300; n++)
            {
                byte[] b = (byte[])tex.Clone();
                for (int k = 0; k < 8; k++) b[rnd.Next(b.Length)] = (byte)rnd.Next(256);
                try { AdtParser.ParseTex(b); }
                catch (WorldParseException) { }
                catch (Exception e) { ok = false; bad = "tex mutation " + n + ": " + e.GetType().Name + " " + e.Message; break; }
            }
            Check(ok, "safety: truncated and randomly corrupted files only ever raise WorldParseException" + (bad != null ? " (" + bad + ")" : ""));
        }

        /// <summary>The adversarial review's findings (docs/world-viewer/01-parsers.md section 13), one check each.</summary>
        static void ReviewFindingTests()
        {
            int idx = 7 * 64 + 36;
            // MAIN values never seen (only 0, 1, 2 occur) and a non-zero second word are flagged at their entry.
            byte[] main = MainTable(idx);
            PutU32(main, 8 + 8 * idx, 3);
            PutU32(main, 8 + 8 * 100 + 4, 0xDEAD);
            WdtFile odd = WdtParser.Parse(Cat(Mver(), Mphd(0), main, Maid(idx, 1, 2, 3, 4)));
            Check(odd.Issues.Exists(i => i.Kind == WorldIssueKind.Unexpected && i.Chunk == "MAIN" && i.Offset == 52 + 8 + 8 * 100) &&
                  odd.TileExists(36, 7), "review: unseen MAIN values are flagged at their entry; bit 0x1 still makes a tile");
            WdtTileFiles f;
            Check(!odd.TryGetTileFiles(64, 0, out f) && !odd.TryGetTileFiles(0, -1, out f), "review: TryGetTileFiles returns false outside 0..63");
            WdtFile mismatch = WdtParser.Parse(Cat(Mver(), Mphd(0), MainTable(idx), Maid(0)));
            Check(mismatch.Issues.Exists(i => i.Chunk == "MAIN" && i.Offset == 52 + 8 + 8 * idx), "review: the MAIN/MAID mismatch cites the MAIN entry");
            WdtFile badBit = WdtParser.Parse(Cat(Mver(), Mphd(1), MainTable(idx), Maid(idx, 1, 2, 3, 4)));
            Check(badBit.Issues.Exists(i => i.Chunk == "MPHD" && i.Offset == 20), "review: the MPHD bit 0x1 issue cites MPHD");

            // Global WMO references: FileDataID 0 names nothing; MWMO must start with a name at byte 0.
            WorldFileReference r;
            WdtFile zero = WdtParser.Parse(Cat(Mver(), Mphd(0x249), MainTable(), Maid(0), Chunk("MODF", ModfRecord(0, 1, 0x8, 1024))));
            Check(!zero.TryGetGlobalWmoReference(0, out r) && zero.Issues.Exists(i => i.Chunk == "MODF"), "review: a MODF FileDataID of 0 is no reference, flagged");
            byte[] lead = Chunk("MWMO", new byte[] { 0, (byte)'a', (byte)'.', (byte)'w', (byte)'m', (byte)'o', 0 });
            WdtFile led = WdtParser.Parse(Cat(Mver(), Mphd(0x41), MainTable(), lead, Chunk("MODF", ModfRecord(0, 1, 0, 0))));
            Check(!led.TryGetGlobalWmoReference(0, out r) && led.Issues.Exists(i => i.Chunk == "MWMO"), "review: an MWMO name not at byte 0 is ambiguous: refused, flagged");
            byte[] two = Chunk("MWMO", new byte[] { (byte)'a', 0, (byte)'b', 0 });
            WdtFile twoNames = WdtParser.Parse(Cat(Mver(), Mphd(0x41), MainTable(), two, Chunk("MODF", ModfRecord(0, 1, 0, 0))));
            Check(twoNames.TryGetGlobalWmoReference(0, out r) && r.Path == "a", "review: name 0 with the first name at byte 0 resolves under both readings");
            Check(old0Scale(), "review: WmoPlacement.Scale is 1.0 without flag 0x4, ScaleRaw/1024 with it");

            // Holes: an MCNK without a header has an Unknown mask, not "no holes"; a clear 0x10000 flag is flagged.
            AdtRoot small = AdtParser.ParseRoot(Root(30, 47, i => i == 9 ? Chunk("MCNK", new byte[100]) : null), 30, 47);
            Check(small.Mcnk[9].Holes.Kind == HoleMaskKind.Unknown && small.Mcnk[9].Holes.IsHole(1, 1) == null && small.Mcnk[9].Holes.HasAnyHole == null,
                  "review: a malformed MCNK's hole mask is Unknown (null), not 'no holes'");
            AdtRoot clear = AdtParser.ParseRoot(Root(30, 47, i => i == 4 ? RootMcnk(4, 0, 0, false, 30, 47) : null), 30, 47);
            Check(clear.Issues.Exists(i => i.Kind == WorldIssueKind.Unexpected && i.Chunk == "MCNK 4" && i.Reason.Contains("0x10000")),
                  "review: flag 0x10000 clear is flagged (set on every audited chunk)");

            // MCSE: non-empty or repeated is listed and flagged; MCNR/MCCV rejection marks the chunk.
            byte[] mcse = RootMcnkOnly(5, 30, 47, Chunk("MCVT", new byte[580]), Chunk("MCNR", new byte[448]), Chunk("MCSE", new byte[28]));
            AdtRoot se = AdtParser.ParseRoot(Root(30, 47, i => i == 5 ? mcse : null), 30, 47);
            Check(se.Mcnk[5].McseSize == 28 && se.Mcnk[5].Unsupported.Length == 1 && se.Issues.Exists(i => i.Chunk == "MCNK 5 MCSE"),
                  "review: a non-empty MCSE is listed as unsupported and flagged");
            byte[] mcse2 = RootMcnkWith(6, 30, 47, Chunk("MCSE", new byte[0]));
            AdtRoot se2 = AdtParser.ParseRoot(Root(30, 47, i => i == 6 ? mcse2 : null), 30, 47);
            Check(se2.Mcnk[6].McseSize == 0 && se2.Mcnk[6].Unsupported.Length == 1 && se2.Issues.Exists(i => i.Reason.Contains("more than once")),
                  "review: a repeated MCSE keeps the first and is flagged");
            byte[] badCv = RootMcnkWith(7, 30, 47, Chunk("MCCV", new byte[579]));
            AdtRoot cv = AdtParser.ParseRoot(Root(30, 47, i => i == 7 ? badCv : null), 30, 47);
            Check(cv.Mcnk[7].Status == WorldDataStatus.Malformed && cv.Mcnk[8].Status == WorldDataStatus.Decoded, "review: a rejected MCCV marks its chunk Malformed");

            // MH2O: instance tables must follow the header table and must not overlap (no allocation amplification).
            var over = new byte[256 * 12 + 24 * 10];
            for (int n = 0; n < 256; n++) { PutU32(over, 12 * n, 256 * 12); PutU32(over, 12 * n + 4, 10); }
            AdtRoot ov = AdtParser.ParseRoot(Root(30, 47, null, Chunk("MH2O", over)), 30, 47);
            Check(ov.Liquids.Status == WorldDataStatus.Malformed && ov.Liquids.Instances[0].Length == 0 && ov.Issues.Exists(i => i.Reason.Contains("overlap")),
                  "review: overlapping MH2O instance tables are rejected before allocating");
            var inTable = new byte[256 * 12 + 24];
            PutU32(inTable, 0, 24);
            PutU32(inTable, 4, 1);
            AdtRoot it = AdtParser.ParseRoot(Root(30, 47, null, Chunk("MH2O", inTable)), 30, 47);
            Check(it.Liquids.Status == WorldDataStatus.Malformed, "review: an instance table inside the header table is rejected");
            AdtRoot dup = AdtParser.ParseRoot(Root(30, 47, null, Cat(Chunk("MH2O", new byte[256 * 12]), Chunk("MH2O", new byte[256 * 12]))), 30, 47);
            Check(dup.Liquids != null && dup.Issues.Exists(i => i.Chunk == "MH2O" && i.Reason.Contains("more than once")), "review: a repeated MH2O is reported, the first used");

            // Optional tex0 chunks and limits.
            var parts = new List<byte[]> { Mver(), Chunk("MAMP", new byte[4]), Chunk("MAMP", new byte[4]), Chunk("MDID", U32Payload(1000)) };
            for (int i = 0; i < 256; i++) parts.Add(TexMcnk(Mcly(0, 0, 0, 0), null));
            AdtTex amp = AdtParser.ParseTex(Cat(parts.ToArray()));
            Check(amp.MampRaw.Length == 4 && amp.Issues.Exists(i => i.Chunk == "MAMP"), "review: a repeated MAMP is reported, not fatal");
            var nine = new uint[36];
            for (int k = 0; k < 9; k++) { nine[4 * k + 1] = k == 0 ? 0u : 0x100u; }
            AdtTex many = AdtParser.ParseTex(Tex(i => i == 0 ? TexMcnk(Mcly(nine), Chunk("MCAL", new byte[8 * 4096])) : null));
            Check(many.Mcnk[0].Status == WorldDataStatus.Malformed && many.Issues.Exists(i => i.Reason.Contains("layers")), "review: more than 8 layers are rejected");
            var five = new uint[20];
            for (int k = 1; k < 5; k++) { five[4 * k + 1] = 0x100; five[4 * k + 2] = (uint)(4096 * (k - 1)); }
            AdtTex fiveLayers = AdtParser.ParseTex(Tex(i => i == 0 ? TexMcnk(Mcly(five), Chunk("MCAL", new byte[4 * 4096])) : null));
            Check(fiveLayers.Mcnk[0].Status == WorldDataStatus.Decoded && fiveLayers.Mcnk[0].Alpha[4] != null, "review: 5 layers (seen in Classic Beta) decode");
            AdtTex mixed = AdtParser.ParseTex(Tex(i => i == 0 ? TexMcnk(Mcly(0, 0, 0, 0, 1, 0x100, 0, 0, 2, 0x100, 4096, 0), Chunk("MCAL", new byte[4096 + 2048])) : null));
            Check(mixed.Mcnk[0].Status == WorldDataStatus.Decoded && mixed.Issues.Exists(i => i.Kind == WorldIssueKind.Unexpected && i.Reason.Contains("4-bit and 8-bit")),
                  "review: 4-bit and 8-bit maps in one chunk are decoded and flagged");
            byte[] flagTex = Tex(i => i == 0 ? TexMcnk(Mcly(0, 0x100, 0, 0, 1, 0x100, 0, 0), Chunk("MCAL", new byte[4096])) : null);
            AdtTex ft = AdtParser.ParseTex(flagTex);
            WorldIssue fi = ft.Issues.Find(i => i.Reason.Contains("0x100"));
            int mclyData = FindTag(flagTex, "MCLY") + 8;
            Check(fi != null && fi.Offset == mclyData + 4, "review: the flag-0x100 issue cites layer 0's flags in MCLY");
            byte[] noMcal = Tex(i => i == 0 ? TexMcnk(Mcly(0, 0, 0, 0, 1, 0x100, 0, 0), null) : null);
            WorldIssue nm = AdtParser.ParseTex(noMcal).Issues.Find(i => i.Reason.Contains("no MCAL"));
            Check(nm != null && nm.Offset == FindTag(noMcal, "MCNK") + 8, "review: 'layers but no MCAL' cites the MCNK");

            // obj1: a non-finite MLDX is an issue; MLDD survives.
            var nanBox = new byte[28];
            PutF32(nanBox, 4, float.NaN);
            AdtObj1 o1 = AdtParser.ParseObj1(Cat(Mver(), Chunk("MLDD", MddfRecord(1, 1, 0, 0, 0, 0, 0, 0, 1024, 0x40)), Chunk("MLDX", nanBox)), 0, 0);
            Check(o1.Doodads.Length == 1 && o1.DoodadBoxes == null && o1.Issues.Exists(i => i.Chunk == "MLDX" && i.Offset == 12 + 8 + 36 + 8 + 4),
                  "review: a non-finite MLDX value is an issue at its offset; MLDD is kept");

            // Empty reads, hostile helper arguments.
            AdtTile empty = AdtParser.ParseTile(1, 2, new uint[] { 9, 9, 9, 9, 9, 0, 0, 0 }, new byte[0], null, null, null, null);
            Check(empty.Root.State == AdtPartState.NotSupplied && empty.Root.Error.Contains("empty read"), "review: an empty buffer is an empty read, not a malformed file");
            string err;
            var buf = new byte[8192];
            Check(AdtParser.DecodeRaw(buf, 4096, 4096, -100, 3996, out err) == null && AdtParser.DecodeRaw(buf, 8000, 4096, 0, 4096, out err) == null &&
                  AdtParser.DecodeRaw(null, 0, 0, 0, 0, out err) == null && AdtParser.DecodeRle(null, 0, 0, out err) == null &&
                  AdtParser.DecodeRle(buf, 10, 20000, out err) == null,
                  "review: the decoding helpers return null with a reason for arguments outside the buffer");
            AlphaMap[] maps;
            var issues = new List<WorldIssue>();
            Check(!AdtParser.DecodeAlpha(buf, new WorldChunk { Tag = "MCAL", DataOffset = 8000, Size = 4096 },
                                          new[] { new TerrainLayer(), new TerrainLayer { Flags = 0x100 } }, "t", "c", issues, out maps) &&
                  issues.Count == 1, "review: DecodeAlpha refuses an MCAL outside the buffer");
            Throws<WorldParseException>(() => WorldChunkWalker.TagAt(new byte[2], 0), "review: TagAt outside the buffer is a WorldParseException");

            // Seams by construction over a whole map row and column of tiles (every chunk edge, both directions).
            bool rowOk = true;
            double x1, y1, x2, y2;
            for (int t = 0; t < 63 && rowOk; t++)
                for (int k = 0; k < 16 && rowOk; k++)
                    for (int c = 0; c <= 8; c++)
                    {
                        TerrainCoordinates.OuterVertex(t, 31, 15, k, c, 8, out x1, out y1);
                        TerrainCoordinates.OuterVertex(t + 1, 31, 0, k, c, 0, out x2, out y2);
                        TerrainCoordinates.OuterVertex(31, t, k, 15, 8, c, out double x3, out double y3);
                        TerrainCoordinates.OuterVertex(31, t + 1, k, 0, 0, c, out double x4, out double y4);
                        rowOk &= x1 == x2 && y1 == y2 && x3 == x4 && y3 == y4;
                    }
            Check(rowOk, "review: tile-edge vertices are bit-identical along a whole map row and column (a property of the integer-index formula)");
            TerrainCoordinates.ChunkOrigin(31, 47, 5, 9, out x1, out y1);
            Check(Math.Abs(x1 - ((32 - 47) * TerrainCoordinates.TileSize - 9 * TerrainCoordinates.ChunkSize)) < 1e-9 &&
                  Math.Abs(y1 - ((32 - 31) * TerrainCoordinates.TileSize - 5 * TerrainCoordinates.ChunkSize)) < 1e-9,
                  "review: the integer-index formula equals the audit's (32-ty)T - iy C form (independent expression)");
        }

        static bool old0Scale()
        {
            var a = new WmoPlacement { Flags = 0, ScaleRaw = 0 };
            var b = new WmoPlacement { Flags = 0x4, ScaleRaw = 512 };
            return a.Scale == 1.0 && b.Scale == 0.5;
        }

        /// <summary>Header offset of the first chunk with `tag` (walking from 0, then into the first MCNK).</summary>
        static int FindTag(byte[] b, string tag)
        {
            for (int p = 0; p + 4 <= b.Length; p++)
                if (b[p] == tag[3] && b[p + 1] == tag[2] && b[p + 2] == tag[1] && b[p + 3] == tag[0])
                    return p;
            return -1;
        }

        public static int RunAll(Action<string> output = null)
        {
            failures = 0;
            log.Clear();
            log.Add("World chunk walk");
            ChunkWalkTests();
            log.Add("WDT");
            WdtTests();
            log.Add("Terrain coordinates and vertex layout");
            CoordinateTests();
            log.Add("Holes");
            HoleTests();
            log.Add("ADT root");
            RootTests();
            log.Add("Alpha maps and layers");
            AlphaTests();
            log.Add("Placements");
            PlacementTests();
            log.Add("Tile assembly");
            TileTests();
            log.Add("Malformed input safety");
            SafetyTests();
            log.Add("Adversarial review findings");
            ReviewFindingTests();
            log.Add(string.Format("WorldParserTests: {0} failure(s)", failures));
            if (output != null)
                foreach (string line in log)
                    output(line);
            return failures;
        }
    }

    static class ByteArrayTestExtensions
    {
        /// <summary>The first `length` bytes (a truncated copy).</summary>
        public static byte[] AsSpanTrimmed(this byte[] b, int length)
        {
            if (length < 0) length = 0;
            var r = new byte[Math.Min(length, b.Length)];
            Buffer.BlockCopy(b, 0, r, 0, r.Length);
            return r;
        }
    }
}
