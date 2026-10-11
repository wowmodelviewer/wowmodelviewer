// WdtParser.cs
//
// Bounds-safe reader for a map's WDT, from the bytes the caller supplies (it never opens CASC).
//
// Evidence (docs/world-viewer/00-audit.md 4.1-4.3, T2, T3; tables reproduced by WorldCorpusValidation):
//   - Forms: MVER(18) MPHD MAIN, then MAID [MODF] [MAI2|MANM] (every Classic-family WDT, 763 of 969
//     Retail), or MWMO MODF (193 Retail, old WMO-only form), or nothing (13 Retail). No WDT has
//     both tiles and a MODF.
//   - MPHD: 8 u32. Word 0 = flags (10 bits seen; bit 0x1 <=> WMO-only exactly; no other bit's
//     meaning established). Words 1-6 = FileDataIDs of the _lgt.wdt, _occ.wdt, _fogs.wdt, _mpv.wdt,
//     .tex and .wdl side files. Evidence: the six files of Azeroth, read in all four clients --
//     Retail's identified by their chunk ids; the 12-byte MVER-only files of Era (lgt, fogs, mpv),
//     MoP (fogs, mpv) and Beta (mpv) only by their MVER version (lgt 20, fogs 2, mpv 3) and the same
//     FileDataID; other maps by listfile name only. Word 7 is non-zero exactly on the modern
//     WMO-only form; its target was never read (listfile name only: .pd4) -- kept raw.
//   - MAIN: 4096 entries of 8 bytes, index y*64 + x. The corpus holds only the values 0, 1 and 2;
//     value 1 <=> MAID names files (every MAID-bearing WDT of every client), 2 => no MAID words; the
//     second word is 0 in every entry. A tile is "bit 0x1 set"; any other value or a non-zero
//     second word raises an Unexpected issue.
//   - MAID: 4096 entries of 8 u32 FileDataIDs: root ADT, obj0, obj1, tex0, lod ADT, map texture,
//     map texture _n, minimap. Evidence (window tiles): words 0-3 by chunk content in all four
//     clients, word 4 by content in Retail/Beta; words 5-7 by BLP2 size and mip count in
//     Retail/Beta (5: 512^2 with mips, terrain-only image; 6: 256^2; 7: 512^2 without mips, the
//     minimap); Era/MoP word 7 by BLP2 256^2; Era/MoP words 4-6 by listfile name only (no such file
//     is pinned). Words 4-6 are optional (zero on 21-93% of tiles). A named FileDataID says nothing
//     about whether the client has the file: availability belongs to the asset provider.
//   - MODF: the map's single WMO (WMO-only maps), 64-byte record (WorldPlacements.cs). Modern form:
//     name is a FileDataID (flag 0x8); old form: name is an MWMO entry (flag 0), scale 0.
//   - MAI2 / MANM: present on a few maps, layout/use not established: listed, not decoded.
//
// Ownership: the returned WdtFile holds its own arrays; it keeps no reference to `data`.

using System;
using System.Collections.Generic;

namespace Wmv.Wow.World
{
    /// <summary>What kind of map a WDT describes, from its own bytes.</summary>
    public enum WdtKind
    {
        /// <summary>At least one MAIN entry has flag bit 0x1 (terrain tiles named by MAID).</summary>
        TiledTerrain,
        /// <summary>No tile, one or more MODF records: a global-WMO (WMO-only) map.</summary>
        GlobalWmo,
        /// <summary>No tile and no MODF (the 13 bare Retail WDTs): nothing to show.</summary>
        Empty,
        /// <summary>Tiles AND a MODF: never seen in the audited clients; both are kept and an
        /// Unexpected issue is raised.</summary>
        TiledTerrainWithGlobalWmo,
    }

    /// <summary>The role of one MAID word, in the proven order.</summary>
    public enum WdtTileFileRole
    {
        RootAdt = 0,
        Obj0Adt = 1,
        Obj1Adt = 2,
        Tex0Adt = 3,
        LodAdt = 4,
        MapTexture = 5,
        MapTextureN = 6,
        Minimap = 7,
    }

    /// <summary>A reference to a file, as the map's own data states it. A FileDataID or a path;
    /// resolving it (by FileDataID or by path, per client) and reading it are the host's job.</summary>
    public struct WorldFileReference
    {
        public bool IsPath;
        public uint FileDataId;     // when !IsPath
        public string Path;         // when IsPath (old WMO-only WDT: MWMO string)

        public static WorldFileReference ById(uint id) { return new WorldFileReference { FileDataId = id }; }
        public static WorldFileReference ByPath(string path) { return new WorldFileReference { IsPath = true, Path = path }; }

        public override string ToString() { return IsPath ? "path:" + Path : "fdid:" + FileDataId; }
    }

    public struct WdtHeader
    {
        public uint[] Words;        // the 8 MPHD words as stored
        public uint Flags { get { return Words[0]; } }
        public uint LgtWdtFileDataId { get { return Words[1]; } }
        public uint OccWdtFileDataId { get { return Words[2]; } }
        public uint FogsWdtFileDataId { get { return Words[3]; } }
        public uint MpvWdtFileDataId { get { return Words[4]; } }
        public uint TexFileDataId { get { return Words[5]; } }
        public uint WdlFileDataId { get { return Words[6]; } }
        /// <summary>Raw: non-zero exactly on modern WMO-only WDTs; target never read (.pd4 by name only).</summary>
        public uint Word7Raw { get { return Words[7]; } }
        /// <summary>MPHD bit 0x1: exactly the WMO-only WDTs in all four clients (a 100% correlation).</summary>
        public bool WmoOnlyFlag { get { return (Words[0] & 1) != 0; } }
    }

    public struct WdtMainEntry
    {
        public uint Flags;          // bit 0x1 <=> MAID names this tile's files
        public uint Word1Raw;       // 0 in every audited entry
        public bool HasTile { get { return (Flags & 1) != 0; } }
    }

    /// <summary>A tile's eight MAID words, in the proven order. 0 = the client names no file.</summary>
    public struct WdtTileFiles
    {
        public int TileX, TileY;
        public uint[] FileDataIds;  // 8 words, index = (int)WdtTileFileRole

        public uint this[WdtTileFileRole role] { get { return FileDataIds[(int)role]; } }
        public bool Names(WdtTileFileRole role) { return FileDataIds[(int)role] != 0; }
    }

    public sealed class WdtFile
    {
        public const int Tiles = 64;
        public const int MaidWordsPerTile = 8;

        public string What;
        public uint Version;
        public WorldChunk[] Chunks;
        public WdtHeader Header;
        public WdtMainEntry[] Main;             // 4096, index y*64 + x
        public uint[] Maid;                     // 4096 * 8 words, or null when the WDT has no MAID
        public string[] MwmoNames;              // old WMO-only form, or null
        public int[] MwmoNameOffsets;           // byte offset of each name inside MWMO
        public WmoPlacement[] GlobalWmos;       // MODF records (empty when none)
        public WorldUnsupportedChunk[] Unsupported;
        public WdtKind Kind;
        public List<WorldIssue> Issues = new List<WorldIssue>();

        public bool HasMaid { get { return Maid != null; } }

        public static int TileIndex(int x, int y)
        {
            if (x < 0 || x >= Tiles || y < 0 || y >= Tiles)
                throw new ArgumentOutOfRangeException("x/y", "tile x/y must be 0..63");
            return y * Tiles + x;
        }

        /// <summary>MAIN flag bit 0x1 at (x, y).</summary>
        /// <remarks>Only what the WDT declares: it says nothing about whether the client has the tile's
        /// files (one audited Beta root, Kalimdor 44_24, is listed but not on the computer). Availability
        /// is the asset provider's, reported per part as AdtPartState.NotSupplied. Throws for x/y
        /// outside 0..63.</remarks>
        public bool TileExists(int x, int y) { return Main[TileIndex(x, y)].HasTile; }

        /// <summary>The MAID words of (x, y); false when the WDT has no MAID or x/y is outside 0..63.</summary>
        public bool TryGetTileFiles(int x, int y, out WdtTileFiles files)
        {
            files = default(WdtTileFiles);
            if (Maid == null || x < 0 || x >= Tiles || y < 0 || y >= Tiles)
                return false;
            int i = y * Tiles + x;
            var w = new uint[MaidWordsPerTile];
            Array.Copy(Maid, i * MaidWordsPerTile, w, 0, MaidWordsPerTile);
            files = new WdtTileFiles { TileX = x, TileY = y, FileDataIds = w };
            return true;
        }

        public int TileCount
        {
            get
            {
                int n = 0;
                for (int i = 0; i < Main.Length; i++)
                    if (Main[i].HasTile)
                        n++;
                return n;
            }
        }

        /// <summary>The global WMO's file: a FileDataID (modern form) or the MWMO path (old form). False
        /// when the record names nothing resolvable: FileDataID 0, or an MWMO reference that is not
        /// unambiguous.</summary>
        public bool TryGetGlobalWmoReference(int placement, out WorldFileReference reference)
        {
            reference = default(WorldFileReference);
            if (GlobalWmos == null || placement < 0 || placement >= GlobalWmos.Length)
                return false;
            WmoPlacement p = GlobalWmos[placement];
            if (p.NameKind == PlacementNameKind.FileDataId)
            {
                if (p.NameId == 0)
                    return false;      // 0 names no file, as in MAID
                reference = WorldFileReference.ById(p.NameId);
                return true;
            }
            // Old form: NameId is an index or a byte offset into MWMO -- the audited data cannot tell
            // which. Only NameId 0 with the first name starting at byte 0 means the same under both.
            if (MwmoNames == null || MwmoNames.Length == 0 || MwmoNameOffsets[0] != 0 || p.NameId != 0)
                return false;
            reference = WorldFileReference.ByPath(MwmoNames[0]);
            return true;
        }
    }

    public static class WdtParser
    {
        public const int MainEntrySize = 8;
        public const int MaidEntrySize = 32;

        public static WdtFile Parse(byte[] data, string what = null)
        {
            what = what ?? "wdt";
            List<WorldChunk> chunks = WorldChunkWalker.Walk(data, 0, data == null ? 0 : data.Length, what, "file");
            var wdt = new WdtFile { What = what, Chunks = chunks.ToArray() };
            var unsupported = new List<WorldUnsupportedChunk>();

            if (chunks.Count == 0 || chunks[0].Tag != "MVER")
                throw new WorldParseException(what, "MVER", 0, "the file does not start with an MVER chunk");
            var ver = new WorldSpan(data, chunks[0], what);
            if (ver.Size < 4)
                throw new WorldParseException(what, "MVER", chunks[0].DataOffset, "shorter than 4 bytes");
            wdt.Version = ver.U32(0);
            if (wdt.Version != 18)
                wdt.Issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, "MVER", chunks[0].DataOffset,
                                              "version " + wdt.Version + " (every audited WDT has 18)"));

            WorldChunk c;
            if (!WorldChunkWalker.FindSingle(chunks, "MPHD", what, out c))
                throw new WorldParseException(what, "MPHD", 0, "missing");
            var mphd = new WorldSpan(data, c, what);
            if (mphd.Size < 32)
                throw new WorldParseException(what, "MPHD", c.DataOffset, mphd.Size + " byte(s), expected 32 (8 words)");
            if (mphd.Size != 32)
                wdt.Issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, "MPHD", c.DataOffset,
                                              mphd.Size + " byte(s); the 8 audited words are read, the rest is ignored"));
            var words = new uint[8];
            for (int i = 0; i < 8; i++)
                words[i] = mphd.U32(4 * i);
            wdt.Header = new WdtHeader { Words = words };

            if (!WorldChunkWalker.FindSingle(chunks, "MAIN", what, out c))
                throw new WorldParseException(what, "MAIN", 0, "missing");
            var main = new WorldSpan(data, c, what);
            if (main.Size != WdtFile.Tiles * WdtFile.Tiles * MainEntrySize)
                throw new WorldParseException(what, "MAIN", c.DataOffset, string.Format(
                    "{0} byte(s), expected {1} (4096 entries of 8 bytes)", main.Size, WdtFile.Tiles * WdtFile.Tiles * MainEntrySize));
            wdt.Main = new WdtMainEntry[WdtFile.Tiles * WdtFile.Tiles];
            int unseen = 0, firstUnseen = -1;
            for (int i = 0; i < wdt.Main.Length; i++)
            {
                wdt.Main[i] = new WdtMainEntry { Flags = main.U32(MainEntrySize * i), Word1Raw = main.U32(MainEntrySize * i + 4) };
                if (wdt.Main[i].Flags > 2 || wdt.Main[i].Word1Raw != 0)
                {
                    unseen++;
                    if (firstUnseen < 0)
                        firstUnseen = i;
                }
            }
            if (unseen > 0)
                wdt.Issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, "MAIN", main.AbsoluteOffset(MainEntrySize * firstUnseen), string.Format(
                    "{0} entr(ies) with a value other than 0, 1, 2 or a non-zero second word (first: x {1}, y {2}, value 0x{3:X}, word 0x{4:X}); " +
                    "the audited WDTs hold only 0, 1 and 2 with a zero second word. A set bit 0x1 is read as a tile",
                    unseen, firstUnseen % 64, firstUnseen / 64, wdt.Main[firstUnseen].Flags, wdt.Main[firstUnseen].Word1Raw)));

            if (WorldChunkWalker.FindSingle(chunks, "MAID", what, out c))
            {
                var maid = new WorldSpan(data, c, what);
                if (maid.Size != WdtFile.Tiles * WdtFile.Tiles * MaidEntrySize)
                    throw new WorldParseException(what, "MAID", c.DataOffset, string.Format(
                        "{0} byte(s), expected {1} (4096 entries of 32 bytes)", maid.Size, WdtFile.Tiles * WdtFile.Tiles * MaidEntrySize));
                wdt.Maid = new uint[WdtFile.Tiles * WdtFile.Tiles * WdtFile.MaidWordsPerTile];
                for (int i = 0; i < wdt.Maid.Length; i++)
                    wdt.Maid[i] = maid.U32(4 * i);
                // MAIN bit 0x1 <=> MAID names files held in every audited WDT; a break is flagged, not fixed.
                int mismatches = 0, firstBad = -1;
                for (int i = 0; i < wdt.Main.Length; i++)
                {
                    bool named = false;
                    for (int k = 0; k < WdtFile.MaidWordsPerTile; k++)
                        named |= wdt.Maid[i * WdtFile.MaidWordsPerTile + k] != 0;
                    if (named != wdt.Main[i].HasTile)
                    {
                        mismatches++;
                        if (firstBad < 0)
                            firstBad = i;
                    }
                }
                if (mismatches > 0)
                    wdt.Issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, "MAIN", main.AbsoluteOffset(MainEntrySize * firstBad), string.Format(
                        "{0} tile(s) where MAIN bit 0x1 and 'MAID names files' disagree (first: x {1}, y {2}; its MAID entry at 0x{3:X})",
                        mismatches, firstBad % 64, firstBad / 64, maid.AbsoluteOffset(MaidEntrySize * firstBad))));
            }
            else
            {
                for (int i = 0; i < wdt.Main.Length; i++)
                    if (wdt.Main[i].HasTile)
                    {
                        wdt.Issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, "MAIN", main.AbsoluteOffset(MainEntrySize * i), string.Format(
                            "tile x {0}, y {1} has MAIN bit 0x1 but the WDT has no MAID: its files are not named", i % 64, i / 64)));
                        break;
                    }
            }

            WorldChunk mwmo, modf;
            bool haveMwmo = WorldChunkWalker.FindFirst(chunks, "MWMO", what, wdt.Issues, out mwmo);
            if (haveMwmo)
                ReadNames(data, mwmo, what, out wdt.MwmoNames, out wdt.MwmoNameOffsets);
            bool haveModf = WorldChunkWalker.FindSingle(chunks, "MODF", what, out modf);
            wdt.GlobalWmos = haveModf ? PlacementRecords.ReadWmos(new WorldSpan(data, modf, what), -1, -1, "WDT MODF") : new WmoPlacement[0];
            for (int i = 0; i < wdt.GlobalWmos.Length; i++)
            {
                WmoPlacement p = wdt.GlobalWmos[i];
                long at = (long)modf.DataOffset + PlacementRecords.WmoRecordSize * i;
                if (p.NameKind == PlacementNameKind.FileDataId && p.NameId == 0)
                    wdt.Issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, "MODF", at,
                                                  "record " + i + ": FileDataID flag 0x8 with name 0 (names no file)"));
                else if (p.NameKind == PlacementNameKind.NameTableEntry && !haveMwmo)
                    wdt.Issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, "MODF", at,
                                                  "record " + i + ": no FileDataID flag and no MWMO name table: the WMO cannot be resolved"));
                else if (p.NameKind == PlacementNameKind.NameTableEntry && (p.NameId != 0 || wdt.MwmoNames.Length == 0 || wdt.MwmoNameOffsets[0] != 0))
                    wdt.Issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, "MWMO", mwmo.DataOffset, string.Format(
                        "record {0}: name {1} into an MWMO whose first name starts at {2}: index and byte-offset readings disagree " +
                        "(every audited old-form WDT has name 0 and one name at offset 0)", i, p.NameId,
                        wdt.MwmoNames.Length > 0 ? wdt.MwmoNameOffsets[0].ToString() : "none")));
            }

            foreach (WorldChunk k in chunks)
            {
                switch (k.Tag)
                {
                    case "MVER": case "MPHD": case "MAIN": case "MAID": case "MWMO": case "MODF":
                        break;
                    case "MAI2":
                    case "MANM":
                        unsupported.Add(new WorldUnsupportedChunk { Tag = k.Tag, DataOffset = k.DataOffset, Size = k.Size,
                                                                    Reason = "layout and use not established (audit U4/U5)" });
                        break;
                    default:
                        unsupported.Add(new WorldUnsupportedChunk { Tag = WorldChunkWalker.Printable(k.Tag), DataOffset = k.DataOffset,
                                                                    Size = k.Size, Reason = "not seen in the audited WDTs" });
                        wdt.Issues.Add(new WorldIssue(WorldIssueKind.Unsupported, what, WorldChunkWalker.Printable(k.Tag), k.DataOffset,
                                                      "chunk not seen in the audited WDTs; kept undecoded"));
                        break;
                }
            }
            wdt.Unsupported = unsupported.ToArray();

            int tiles = wdt.TileCount;
            if (tiles > 0 && wdt.GlobalWmos.Length > 0)
            {
                wdt.Kind = WdtKind.TiledTerrainWithGlobalWmo;
                wdt.Issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, "MODF", modf.HeaderOffset,
                                              "tiles and a MODF in one WDT: never seen in the audited clients"));
            }
            else if (tiles > 0)
                wdt.Kind = WdtKind.TiledTerrain;
            else if (wdt.GlobalWmos.Length > 0)
                wdt.Kind = WdtKind.GlobalWmo;
            else
                wdt.Kind = WdtKind.Empty;
            if (wdt.Header.WmoOnlyFlag != (wdt.Kind == WdtKind.GlobalWmo))
                wdt.Issues.Add(new WorldIssue(WorldIssueKind.Unexpected, what, "MPHD", mphd.AbsoluteOffset(0), string.Format(
                    "MPHD bit 0x1 is {0} but the WDT is {1} (bit 0x1 <=> WMO-only held in every audited WDT)",
                    wdt.Header.WmoOnlyFlag ? "set" : "clear", wdt.Kind)));
            return wdt;
        }

        /// <summary>NUL-separated names with their byte offsets (latin-1: one byte per char).</summary>
        static void ReadNames(byte[] data, WorldChunk c, string what, out string[] names, out int[] offsets)
        {
            var s = new WorldSpan(data, c, what);
            var n = new List<string>();
            var o = new List<int>();
            int start = 0;
            for (int i = 0; i <= s.Size; i++)
            {
                if (i == s.Size || data[c.DataOffset + i] == 0)
                {
                    if (i > start)
                    {
                        var chars = new char[i - start];
                        for (int k = 0; k < chars.Length; k++)
                            chars[k] = (char)data[c.DataOffset + start + k];
                        n.Add(new string(chars));
                        o.Add(start);
                    }
                    start = i + 1;
                }
            }
            names = n.ToArray();
            offsets = o.ToArray();
        }
    }
}
