// WmvMapTiles.cs
//
// THE MAPS VIEWER'S TILE TABLE (protocol 14, docs/world-viewer/05-maps-viewer-ui.md): what a map's WDT says of its
// 64 x 64 tiles, for the host's tile grid. The host never parses a WDT (WorldMapCatalog.h): it reads the bytes on its
// map asset worker, checks them against their content key, and hands them here with inspectMapTiles; the verified
// parser (Wow/World/WdtParser.cs, docs/world-viewer/01-parsers.md) reads them, exactly as a terrain load reads its WDT.
//
//   per tile (index y * 64 + x; x the column, east; y the row, south; the root ADT is <map>_<x>_<y>.adt):
//     bit 0  the MAIN flag (WdtMainEntry.HasTile: MAID names this tile's files)
//     bit 1  MAID word 0 names a root ADT (WdtTileFileRole.RootAdt; the root a terrain load asks for, WmvTerrainSession)
//   rootIds, tex0Ids: MAID words 0 and 3 of every tile (0: none); absent without MAID
//   minimapIds (protocol 15): MAID word 7 of every tile, the tile's minimap image (00-audit.md 4.2, 4.6); absent without MAID
//   the global WMO (protocol 15): how many MODF records the WDT has, and the first one's file (a FileDataID, or the old form's
//     MWMO path when that is unambiguous) -- what a world-model-only map is. Protocol 16 adds the first record itself, as
//     stored, for the footprint (InspectWorldModel below)
//
// THE FOOTPRINT (protocol 16, docs/world-viewer/06-world-models-in-world.md section 5): a WMO-only map's world model --
// its root's bytes, which the host read and checked, and the WDT's record -- placed by the stage 06 rule (the WDT's
// Global frame, WorldPlacements.cs) and reduced to what the overhead map draws: the placed root box, the record's
// extents, and every group's placed MOGI box with its flags. Nothing but the root is read: the groups' boxes are the
// root's MOGI.
//
// What the table does not say: whether the client has a tile's files. That is the host's to answer from its storage
// root (listed, on this computer), never assumed from the flag (01-parsers.md 2: a Beta root is listed but not local).
// Pure C#: no Unity object, no state; the self-test calls it directly.

using System;
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using Wmv.Wow.World;

public static class WmvMapTiles
{
    public const int Tiles = WdtFile.Tiles;
    public const byte FlagMain = 1, FlagRoot = 2;

    /// <summary>The tile table of request's WDT: "inspected", or "failed" with the reason (no bytes, a hash that is not the
    /// host's content key, the parser's exception). Never throws.</summary>
    public static WmvIpcClient.MapTilesReport Inspect(WmvIpcClient.MapTilesRequest request)
    {
        var r = new WmvIpcClient.MapTilesReport();
        if (request == null)
        {
            r.Reason = "no request";
            return r;
        }
        r.Inspection = request.Inspection;
        r.MapId = request.MapId;
        r.WdtFileDataID = request.WdtFileDataID;
        r.AssetEpoch = request.AssetEpoch;
        byte[] bytes = request.Bytes;
        if (bytes == null || bytes.Length == 0)
        {
            r.Reason = "the WDT's bytes did not come with the request" + (string.IsNullOrEmpty(request.Encoding) ? "" : " (" + request.Encoding + ")");
            return r;
        }
        r.Bytes = bytes.Length;
        var sw = Stopwatch.StartNew();
        r.Md5 = Md5Hex(bytes);
        if (!string.Equals(r.Md5, request.Md5 ?? "", StringComparison.OrdinalIgnoreCase))
        {
            r.Reason = "the WDT's bytes hash to " + r.Md5 + ", not to the content key the host checked them against (" + request.Md5 + ")";
            r.ParseMs = sw.Elapsed.TotalMilliseconds;
            return r;
        }
        WdtFile wdt;
        try
        {
            wdt = WdtParser.Parse(bytes, "WDT " + request.WdtFileDataID);
        }
        catch (Exception e)
        {
            r.Reason = "the WDT could not be parsed: " + e.GetType().Name + ": " + e.Message;
            r.ParseMs = sw.Elapsed.TotalMilliseconds;
            return r;
        }
        r.Kind = wdt.Kind.ToString();
        r.MphdFlags = wdt.Header.Words != null ? wdt.Header.Flags : 0;
        r.HasMain = wdt.Main != null && wdt.Main.Length == Tiles * Tiles;
        r.HasMaid = wdt.HasMaid;
        r.TileFlags = new byte[Tiles * Tiles];
        if (r.HasMaid)
        {
            r.RootIds = new uint[Tiles * Tiles];
            r.Tex0Ids = new uint[Tiles * Tiles];
            r.MinimapIds = new uint[Tiles * Tiles];
        }
        for (int i = 0; i < Tiles * Tiles; i++)
        {
            byte f = 0;
            if (r.HasMain && wdt.Main[i].HasTile)
            {
                f |= FlagMain;
                r.TilesInMain++;
            }
            if (r.HasMaid)
            {
                r.RootIds[i] = wdt.Maid[i * WdtFile.MaidWordsPerTile + (int)WdtTileFileRole.RootAdt];
                r.Tex0Ids[i] = wdt.Maid[i * WdtFile.MaidWordsPerTile + (int)WdtTileFileRole.Tex0Adt];
                r.MinimapIds[i] = wdt.Maid[i * WdtFile.MaidWordsPerTile + (int)WdtTileFileRole.Minimap];
                if (r.RootIds[i] != 0)
                {
                    f |= FlagRoot;
                    r.TilesWithRoot++;
                }
            }
            r.TileFlags[i] = f;
        }
        r.GlobalWmoRecords = wdt.GlobalWmos != null ? wdt.GlobalWmos.Length : 0;
        if (r.GlobalWmoRecords > 0)
        {
            WorldFileReference reference;
            if (wdt.TryGetGlobalWmoReference(0, out reference))
            {
                if (!reference.IsPath)
                    r.GlobalWmoFileDataID = reference.FileDataId;
                else
                    r.GlobalWmoPath = reference.Path ?? "";
            }
            WmoPlacement g = wdt.GlobalWmos[0];
            r.HasGlobalRecord = true;
            r.GlobalPosition = new[] { g.Position.A, g.Position.B, g.Position.C };
            r.GlobalRotation = new[] { g.Rotation.A, g.Rotation.B, g.Rotation.C };
            r.GlobalExtentsMin = new[] { g.ExtentsMin.A, g.ExtentsMin.B, g.ExtentsMin.C };
            r.GlobalExtentsMax = new[] { g.ExtentsMax.A, g.ExtentsMax.B, g.ExtentsMax.C };
            r.GlobalFlags = g.Flags;
            r.GlobalScaleRaw = g.ScaleRaw;
            r.GlobalUniqueId = g.UniqueId;
        }
        r.Status = "inspected";
        r.ParseMs = sw.Elapsed.TotalMilliseconds;
        return r;
    }

    /// <summary>The most groups a footprint carries (no audited root has more than 512; the line stays small).</summary>
    public const int FootprintGroupsMax = 1024;

    /// <summary>The footprint of a WMO-only map's world model: "inspected", or "failed" with the reason (no bytes, a hash
    /// that is not the host's content key, the root parser's exception, more groups than FootprintGroupsMax). Never throws.</summary>
    public static WmvIpcClient.WorldModelInspectReport InspectWorldModel(WmvIpcClient.WorldModelRequest request)
    {
        var r = new WmvIpcClient.WorldModelInspectReport();
        if (request == null)
        {
            r.Reason = "no request";
            return r;
        }
        r.Inspection = request.Inspection;
        r.MapId = request.MapId;
        r.WmoFileDataID = request.WmoFileDataID;
        r.AssetEpoch = request.AssetEpoch;
        byte[] bytes = request.Bytes;
        if (bytes == null || bytes.Length == 0)
        {
            r.Reason = "the world model's bytes did not come with the request" + (string.IsNullOrEmpty(request.Encoding) ? "" : " (" + request.Encoding + ")");
            return r;
        }
        r.Bytes = bytes.Length;
        var sw = Stopwatch.StartNew();
        r.Md5 = Md5Hex(bytes);
        if (!string.Equals(r.Md5, request.Md5 ?? "", StringComparison.OrdinalIgnoreCase))
        {
            r.Reason = "the world model's bytes hash to " + r.Md5 + ", not to the content key the host checked them against (" + request.Md5 + ")";
            r.ParseMs = sw.Elapsed.TotalMilliseconds;
            return r;
        }
        Wmv.Wow.WmoRoot root;
        try
        {
            root = Wmv.Wow.WmoParser.ParseRoot(bytes, "WMO root " + request.WmoFileDataID);
        }
        catch (Exception e)
        {
            r.Reason = "the world model's root could not be parsed: " + e.GetType().Name + ": " + e.Message;
            r.ParseMs = sw.Elapsed.TotalMilliseconds;
            return r;
        }
        if (root.GroupInfos.Length > FootprintGroupsMax)
        {
            r.Reason = "the world model has " + root.GroupInfos.Length + " groups, more than a footprint carries (" + FootprintGroupsMax + ")";
            r.ParseMs = sw.Elapsed.TotalMilliseconds;
            return r;
        }
        var record = new WmoPlacement
        {
            NameId = (uint)request.WmoFileDataID, UniqueId = request.UniqueId,
            Position = new WorldFloat3(request.Position[0], request.Position[1], request.Position[2]),
            Rotation = new WorldFloat3(request.Rotation[0], request.Rotation[1], request.Rotation[2]),
            ExtentsMin = new WorldFloat3(request.ExtentsMin[0], request.ExtentsMin[1], request.ExtentsMin[2]),
            ExtentsMax = new WorldFloat3(request.ExtentsMax[0], request.ExtentsMax[1], request.ExtentsMax[2]),
            Flags = (ushort)request.Flags, ScaleRaw = (ushort)request.ScaleRaw,
        };
        WorldModelPlacement p = WorldModelPlan.Build(new[] { record }, PlacementFrame.Global, null)[0];
        WorldPoint rmin, rmax;
        WorldModelPlan.RootBox(root, p, out rmin, out rmax);
        r.RootBox = new[] { rmin.X, rmin.Y, rmin.Z, rmax.X, rmax.Y, rmax.Z };
        r.Extents = new[] { p.ExtentsMin.X, p.ExtentsMin.Y, p.ExtentsMin.Z, p.ExtentsMax.X, p.ExtentsMax.Y, p.ExtentsMax.Z };
        // A box that is not a number (a malformed root or record) is no footprint: said, never drawn or targeted.
        foreach (double v in r.RootBox)
            if (double.IsNaN(v) || double.IsInfinity(v))
            {
                r.Reason = "the world model's placed box is not finite (a malformed root or WDT record)";
                r.ParseMs = sw.Elapsed.TotalMilliseconds;
                return r;
            }
        WorldModelGroupBox[] boxes = WorldModelPlan.GroupBoxes(root, p);
        r.Groups = boxes.Length;
        r.GroupBoxes = new float[boxes.Length * 6];
        r.GroupFlags = new uint[boxes.Length];
        for (int i = 0; i < boxes.Length; i++)
        {
            WorldModelGroupBox b = boxes[i];
            r.GroupBoxes[6 * i] = (float)b.Min.X;
            r.GroupBoxes[6 * i + 1] = (float)b.Min.Y;
            r.GroupBoxes[6 * i + 2] = (float)b.Min.Z;
            r.GroupBoxes[6 * i + 3] = (float)b.Max.X;
            r.GroupBoxes[6 * i + 4] = (float)b.Max.Y;
            r.GroupBoxes[6 * i + 5] = (float)b.Max.Z;
            r.GroupFlags[i] = b.Flags;
            if (b.Interior)
                r.InteriorGroups++;
        }
        r.Status = "inspected";
        r.ParseMs = sw.Elapsed.TotalMilliseconds;
        return r;
    }

    static string Md5Hex(byte[] bytes)
    {
        using (MD5 md5 = MD5.Create())
        {
            byte[] h = md5.ComputeHash(bytes);
            var sb = new StringBuilder(32);
            foreach (byte b in h)
                sb.Append(b.ToString("x2"));
            return sb.ToString();
        }
    }
}
