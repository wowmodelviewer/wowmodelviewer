// WmvLifecycleSelfTestMapsViewer.cs
//
// The player's part of the Maps viewer (protocol 14, docs/world-viewer/05-maps-viewer-ui.md), checked in the lifecycle
// self-test (-wmvLifecycleTest) beside the terrain tests:
//   - the tile table (WmvMapTiles.Inspect): the MAIN flags and MAID root ids of a synthetic WDT at the indices
//     y * 64 + x, never transposed; a WDT without MAID; a hash that is not the host's content key, no bytes and a
//     malformed WDT refused with their reasons; the mapTilesInspected line;
//   - the view chosen at run time (WmvTerrainDebug.SetRuntimeView): the names it takes and refuses;
//   - the tile on screen built again in another view (WmvTerrainSession.RebuildShown): its uploaded textures handed
//     over, never uploaded twice, nothing asked of the host, no framing; the data of the textures a view did not upload
//     kept and uploaded by a textured rebuild; a rebuild that throws leaves the old tile as it was; nothing on screen;
//     every tile, texture and alpha array released by clearTerrain;
//   - protocol 15: the minimap ids (MAID word 7) at y * 64 + x and a world-model-only WDT's global WMO in the tile table;
//     the camera fields of loadTerrainTile and setWorldCamera; the worldCameraApplied line; the ground under a point
//     (TerrainHeight) against ramps known everywhere, holes and points off the tile; the world-to-Unity conversion the
//     camera uses, against the mesh's own vertices; the free camera's step (WmvFlyCamera.Step) from synthetic input, and
//     its keys (GateKeys, ApplyKeys): counted only with the keyboard, never a key held from before it came, opposite keys
//     cancelling, the same distance at any frame rate. (Whether the keys reach the player is not a self-test's to say:
//     section 22.)

using System;
using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow.World;

public static partial class WmvLifecycleSelfTest
{
    static void TerrainMapsViewerTests(Action<string> log)
    {
        try
        {
            MapTilesTableTests(log);
            TerrainRuntimeViewTests(log);
            TerrainRebuildTests(log);
            OverheadMapTableTests(log);
            WorldCameraIpcTests(log);
            TerrainHeightTests(log);
            FlyCameraStepTests(log);
            FlyCameraKeyTests(log);
        }
        catch (Exception e)
        {
            Check(false, "maps viewer: the tests threw " + e.GetType().Name + ": " + e.Message + " at " + e.StackTrace, log);
        }
        finally
        {
            WmvTerrainDebug.SetRuntimeView("auto");
        }
    }

    // ---- protocol 15: the overhead map's table
    static void OverheadMapTableTests(Action<string> log)
    {
        // Minimap ids on (30, 48) and (2, 63); (63, 2) is not one, so a transposed table cannot pass.
        var maid = new byte[4096 * 32];
        int a = 48 * 64 + 30, b = 63 * 64 + 2, t = 2 * 64 + 63;
        AdtSynthetic.PutU32(maid, a * 32, 1001);
        AdtSynthetic.PutU32(maid, a * 32 + 28, 5001);
        AdtSynthetic.PutU32(maid, b * 32, 1002);
        AdtSynthetic.PutU32(maid, b * 32 + 28, 5002);
        byte[] wdt = AdtSynthetic.Cat(AdtSynthetic.Mver(), AdtSynthetic.Mphd(0), AdtSynthetic.MainTable(a, b), AdtSynthetic.Chunk("MAID", maid));
        WmvIpcClient.MapTilesReport r = WmvMapTiles.Inspect(TilesRequest(wdt));
        int nonZero = 0;
        if (r.MinimapIds != null)
            foreach (uint id in r.MinimapIds)
                if (id != 0) nonZero++;
        Check(r.Status == "inspected" && r.MinimapIds != null && r.MinimapIds.Length == 4096 && r.MinimapIds[a] == 5001 && r.MinimapIds[b] == 5002 &&
              r.MinimapIds[t] == 0 && nonZero == 2 && r.RootIds[a] == 1001 && r.GlobalWmoRecords == 0,
              "maps viewer: the minimap ids are MAID word 7 at y * 64 + x, never transposed (" + nonZero + " non-zero)", log);
        string line = WmvIpcClient.MapTilesInspectedLine(r);
        string key = "\"minimapIds\":\"";
        int at = line.IndexOf(key, StringComparison.Ordinal);
        bool decoded = false;
        if (at >= 0)
        {
            int start = at + key.Length, end = line.IndexOf('"', start);
            byte[] bytes = Convert.FromBase64String(line.Substring(start, end - start));
            decoded = bytes.Length == 4096 * 4 && BitConverter.ToUInt32(bytes, a * 4) == 5001 && BitConverter.ToUInt32(bytes, b * 4) == 5002;
        }
        Check(decoded && line.Contains("\"globalWmoRecords\":0"), "maps viewer: mapTilesInspected carries the minimap ids (little-endian u32, base64)", log);

        // A world-model-only WDT, modern form: no tile, MAID, one MODF naming FileDataID 108631 (flag 0x8).
        byte[] wmoOnly = AdtSynthetic.Cat(AdtSynthetic.Mver(), AdtSynthetic.Mphd(1), AdtSynthetic.MainTable(), AdtSynthetic.Chunk("MAID", new byte[4096 * 32]),
                                          AdtSynthetic.Chunk("MODF", AdtSynthetic.ModfRecord(108631, 0xFFFFFFFF, 0x8, 1024)));
        WmvIpcClient.MapTilesReport w = WmvMapTiles.Inspect(TilesRequest(wmoOnly));
        string wl = WmvIpcClient.MapTilesInspectedLine(w);
        Check(w.Status == "inspected" && w.TilesInMain == 0 && w.GlobalWmoRecords == 1 && w.GlobalWmoFileDataID == 108631 && w.GlobalWmoPath == "" &&
              wl.Contains("\"globalWmoRecords\":1") && wl.Contains("\"globalWmoFileDataID\":108631"),
              "maps viewer: a world-model-only WDT's global WMO is reported (" + w.Kind + ", " + w.GlobalWmoRecords + " record, file " +
              w.GlobalWmoFileDataID + ")", log);
    }

    // ---- protocol 15: the camera on the wire
    static void WorldCameraIpcTests(Action<string> log)
    {
        var go = new GameObject("WorldCameraIpcTest");
        try
        {
            var ipc = go.AddComponent<WmvIpcClient>();
            ipc.SentForTest = s => { };
            WmvIpcClient.TerrainTileCommand got = null;
            WmvIpcClient.WorldCameraRequest asked = null;
            ipc.OnLoadTerrainTile = c => got = c;
            ipc.OnSetWorldCamera = q => asked = q;
            ipc.FeedLineForTest("{\"type\":\"loadTerrainTile\",\"load\":7,\"assetEpoch\":2,\"mapId\":0,\"tileX\":32,\"tileY\":48," +
                                "\"wdtFileDataID\":775971,\"maxAssetBytes\":4194304,\"cameraRequest\":9,\"cameraMode\":\"fly\"," +
                                "\"cameraX\":-9449.25,\"cameraY\":64.5,\"cameraZ\":0,\"cameraHasZ\":false,\"cameraYaw\":135,\"cameraPitch\":-10," +
                                "\"cameraEyeHeight\":2}", null);
            WmvIpcClient.WorldCameraRequest c0 = got != null ? got.Camera : null;
            Check(c0 != null && c0.Request == 9 && c0.Mode == "fly" && c0.X == -9449.25 && c0.Y == 64.5 && !c0.HasZ && c0.Yaw == 135f &&
                  c0.Pitch == -10f && c0.EyeHeight == 2f,
                  "maps viewer: loadTerrainTile carries the world camera, read whole (" + (c0 != null ? c0.ToString() : "none") + ")", log);
            got = null;
            ipc.FeedLineForTest("{\"type\":\"loadTerrainTile\",\"load\":8,\"assetEpoch\":2,\"mapId\":0,\"tileX\":32,\"tileY\":48," +
                                "\"wdtFileDataID\":775971,\"maxAssetBytes\":4194304}", null);
            Check(got != null && got.Camera == null, "maps viewer: a loadTerrainTile without a camera carries none (the tile is framed as before)", log);
            ipc.FeedLineForTest("{\"type\":\"setWorldCamera\",\"cameraRequest\":11,\"cameraMode\":\"fly\",\"cameraX\":100.5," +
                                "\"cameraY\":-20.25,\"cameraZ\":55.5,\"cameraHasZ\":true,\"cameraYaw\":270,\"cameraPitch\":5}", null);
            Check(asked != null && asked.Request == 11 && asked.Mode == "fly" && asked.X == 100.5 && asked.Y == -20.25 && asked.HasZ && asked.Z == 55.5 &&
                  asked.Yaw == 270f && asked.Pitch == 5f, "maps viewer: setWorldCamera is read whole (" + (asked != null ? asked.ToString() : "none") + ")", log);
            asked = null;
            ipc.FeedLineForTest("{\"type\":\"setWorldCamera\",\"cameraRequest\":12}", null);
            Check(asked != null && asked.Request == 12 && asked.Mode == "",
                  "maps viewer: a setWorldCamera without a mode still reaches the player (to be answered refused)", log);
            string line = WmvIpcClient.WorldCameraAppliedLine(new WmvIpcClient.WorldCameraReport
            {
                Request = 11, Mode = "fly", Status = "applied", Reason = "on the ground", X = 1.5, Y = -2.25, Z = 60.75, Ground = true,
                GroundZ = 58.75, Yaw = 90f, Pitch = -5f, Load = 7,
            });
            string none = WmvIpcClient.WorldCameraAppliedLine(new WmvIpcClient.WorldCameraReport { Request = 12, Reason = "no world is on screen" });
            Check(line == "{\"type\":\"worldCameraApplied\",\"request\":11,\"mode\":\"fly\",\"status\":\"applied\",\"reason\":\"on the ground\"," +
                          "\"x\":1.5,\"y\":-2.25,\"z\":60.75,\"ground\":true,\"hasGroundZ\":true,\"groundZ\":58.75,\"yaw\":90,\"pitch\":-5,\"load\":7}" &&
                  none.Contains("\"status\":\"refused\"") && none.Contains("\"hasGroundZ\":false"),
                  "maps viewer: the worldCameraApplied line (" + line + ")", log);
        }
        finally
        {
            UnityEngine.Object.DestroyImmediate(go);
        }
    }

    // ---- protocol 15: the ground under a point
    static void TerrainHeightTests(Action<string> log)
    {
        const int tx = 32, ty = 48;
        WorldOrigin o = WorldToUnity.TileOrigin(tx, ty);
        double sx = 0.05, sy = -0.03;
        TerrainTileMesh ramp = TerrainFromRoot(AdtSynthetic.RampRoot(tx, ty, sx, sy), tx, ty);
        var rng = new System.Random(15);
        double T = TerrainCoordinates.TileSize, C = TerrainCoordinates.ChunkSize;
        int found = 0;
        double worst = 0;
        for (int k = 0; k < 400; k++)
        {
            // Inside the tile: x from o.X - T to o.X, y from o.Y - T to o.Y; every 20th point on a chunk border exactly.
            double south = rng.NextDouble() * T, east = rng.NextDouble() * T;
            if (k % 20 == 0)
                south = Math.Floor(south / C) * C;
            double x = o.X - south, y = o.Y - east;
            double h = TerrainHeight.At(ramp, x, y);
            if (double.IsNaN(h))
                continue;
            found++;
            worst = Math.Max(worst, Math.Abs(h - (100.0 + sx * (x - o.X) + sy * (y - o.Y))));
        }
        Check(found == 400 && worst < 0.01,
              string.Format(System.Globalization.CultureInfo.InvariantCulture,
                            "maps viewer: the ground under 400 points of a ramp is the ramp's height (found {0}, worst {1:G3} yd)", found, worst), log);
        double off1 = TerrainHeight.At(ramp, o.X + 1.0, o.Y - 10.0), off2 = TerrainHeight.At(ramp, o.X - 10.0, o.Y - T - 1.0);
        Check(double.IsNaN(off1) && double.IsNaN(off2), "maps viewer: a point off the tile has no ground (NaN)", log);
        // Chunk 64 is all holes: its cell (3, 4) has no ground; the same cell of chunk 65 has.
        TerrainTileMesh holed = TerrainFromRoot(AdtSynthetic.RootWith(tx, ty, n => new RootChunkSpec { Holes = n == 64 ? ulong.MaxValue : 0UL }), tx, ty);
        double U = TerrainCoordinates.UnitSize;
        int hx = 64 % 16, hy = 64 / 16;
        double inHole = TerrainHeight.At(holed, o.X - (hy * C + 3.5 * U), o.Y - (hx * C + 4.5 * U));
        double beside = TerrainHeight.At(holed, o.X - (hy * C + 3.5 * U), o.Y - ((hx + 1) * C + 4.5 * U));
        Check(double.IsNaN(inHole) && !double.IsNaN(beside), "maps viewer: a hole cell has no ground, the cell beside it has", log);
        // The conversion the camera uses: a vertex's world point turns into that vertex's Unity position.
        TerrainChunkMesh ch = ramp.Chunks[17];
        double vx, vy;
        TerrainCoordinates.OuterVertex(tx, ty, 17 % 16, 17 / 16, 0, 0, out vx, out vy);
        double vz = 100.0 + sx * (vx - o.X) + sy * (vy - o.Y);
        double ux, uy, uz;
        WorldToUnity.PositionD(vx, vy, vz, ramp.Origin, out ux, out uy, out uz);
        Check(Math.Abs(ux - ch.Positions[0]) < 1e-3 && Math.Abs(uy - ch.Positions[1]) < 1e-3 && Math.Abs(uz - ch.Positions[2]) < 1e-3,
              string.Format(System.Globalization.CultureInfo.InvariantCulture,
                            "maps viewer: a world point converts to the Unity position of its vertex ({0:0.###}, {1:0.###}, {2:0.###} vs {3}, {4}, {5})",
                            ux, uy, uz, ch.Positions[0], ch.Positions[1], ch.Positions[2]), log);
    }

    // ---- protocol 15: the free camera's step
    static void FlyCameraStepTests(Action<string> log)
    {
        var s = new WmvFlyCamera.State { Position = Vector3.zero, Yaw = 0f, Pitch = 0f, Speed = 30f };
        WmvFlyCamera.State n = WmvFlyCamera.Step(s, new WmvFlyCamera.FlyInput { Forward = 1f, SpeedFactor = 1f }, 0.5f);
        WmvFlyCamera.State e = WmvFlyCamera.Step(new WmvFlyCamera.State { Yaw = 90f, Speed = 30f }, new WmvFlyCamera.FlyInput { Forward = 1f, SpeedFactor = 1f }, 0.5f);
        WmvFlyCamera.State r = WmvFlyCamera.Step(s, new WmvFlyCamera.FlyInput { Right = 1f, SpeedFactor = 1f }, 0.5f);
        WmvFlyCamera.State u = WmvFlyCamera.Step(s, new WmvFlyCamera.FlyInput { Up = 1f, SpeedFactor = 1f }, 0.5f);
        Check(Close(n.Position, new Vector3(0f, 0f, 15f)) && Close(e.Position, new Vector3(15f, 0f, 0f)) && Close(r.Position, new Vector3(15f, 0f, 0f)) &&
              Close(u.Position, new Vector3(0f, 15f, 0f)),
              "maps viewer: the free camera flies north at yaw 0, east at yaw 90, strafes east, climbs straight up (" + n.Position + ", " + e.Position +
              ", " + r.Position + ", " + u.Position + ")", log);
        WmvFlyCamera.State look = WmvFlyCamera.Step(s, new WmvFlyCamera.FlyInput { LookX = 1800f, LookY = 1000f, SpeedFactor = 1f }, 0.016f);
        WmvFlyCamera.State down = WmvFlyCamera.Step(s, new WmvFlyCamera.FlyInput { LookY = -1000f, SpeedFactor = 1f }, 0.016f);
        Check(Mathf.Abs(look.Yaw) < 1e-3f && look.Pitch == WmvFlyCamera.MaxPitch && down.Pitch == -WmvFlyCamera.MaxPitch && Close(look.Position, Vector3.zero),
              "maps viewer: a drag turns the free camera (360 degrees back to north) and holds its pitch within 89 degrees, without moving it", log);
        WmvFlyCamera.State wheel = WmvFlyCamera.Step(s, new WmvFlyCamera.FlyInput { WheelNotches = 2f, SpeedFactor = 1f }, 0.016f);
        WmvFlyCamera.State fast = WmvFlyCamera.Step(s, new WmvFlyCamera.FlyInput { Forward = 1f, SpeedFactor = WmvFlyCamera.FastFactor }, 0.5f);
        WmvFlyCamera.State up = WmvFlyCamera.Step(new WmvFlyCamera.State { Pitch = 30f, Speed = 30f }, new WmvFlyCamera.FlyInput { Forward = 1f, SpeedFactor = 1f }, 1f);
        Check(Close(wheel.Position, new Vector3(0f, 0f, 15f)) && Close(fast.Position, new Vector3(0f, 0f, 60f)) &&
              Close(up.Position, new Vector3(0f, 15f, 25.98076f)),
              "maps viewer: two wheel notches fly half a second forwards, Shift four times as fast, and forwards follows the pitch (" + wheel.Position +
              ", " + fast.Position + ", " + up.Position + ")", log);
    }

    // ---- the free camera's keys (section 22): only with the keyboard, never a key held from before it came
    static void FlyCameraKeyTests(Action<string> log)
    {
        const uint W = WmvFlyCamera.KeyForward, D = WmvFlyCamera.KeyRight, Shift = WmvFlyCamera.KeyFast;
        var g = new WmvFlyCamera.KeyGate();
        uint without = WmvFlyCamera.GateKeys(ref g, false, W);
        uint atTake = WmvFlyCamera.GateKeys(ref g, true, W);            // W already held when the keyboard comes
        uint stillHeld = WmvFlyCamera.GateKeys(ref g, true, W);
        uint released = WmvFlyCamera.GateKeys(ref g, true, 0);
        uint pressed = WmvFlyCamera.GateKeys(ref g, true, W);
        Check(without == 0 && atTake == 0 && stillHeld == 0 && released == 0 && pressed == W,
              "maps viewer: keys count only while the player's window has the keyboard, and a key held when it came only after it was released (" +
              without + " " + atTake + " " + stillHeld + " " + released + " " + pressed + ")", log);
        uint lost = WmvFlyCamera.GateKeys(ref g, false, W);             // the keyboard goes while W is held
        uint stale = WmvFlyCamera.GateKeys(ref g, true, W);             // back, the backends still reporting W held
        uint staleAgain = WmvFlyCamera.GateKeys(ref g, true, W | D);    // D pressed meanwhile counts, the stale W does not
        uint freed = WmvFlyCamera.GateKeys(ref g, true, D);
        uint both = WmvFlyCamera.GateKeys(ref g, true, W | D);
        Check(lost == 0 && stale == 0 && staleAgain == D && freed == D && both == (W | D),
              "maps viewer: a key whose release went elsewhere never moves the camera; a key pressed since counts at once (" + lost + " " + stale + " " +
              staleAgain + " " + freed + " " + both + ")", log);
        var i = new WmvFlyCamera.FlyInput();
        WmvFlyCamera.ApplyKeys(ref i, W | D | Shift);
        var j = new WmvFlyCamera.FlyInput();
        WmvFlyCamera.ApplyKeys(ref j, WmvFlyCamera.KeyBack | WmvFlyCamera.KeyLeft | WmvFlyCamera.KeyDown);
        var k = new WmvFlyCamera.FlyInput();
        WmvFlyCamera.ApplyKeys(ref k, W | WmvFlyCamera.KeyBack | WmvFlyCamera.KeyUp | WmvFlyCamera.KeyDown);
        var none = new WmvFlyCamera.FlyInput();
        WmvFlyCamera.ApplyKeys(ref none, 0);
        Check(i.Forward == 1f && i.Right == 1f && i.Up == 0f && i.SpeedFactor == WmvFlyCamera.FastFactor &&
              j.Forward == -1f && j.Right == -1f && j.Up == -1f && j.SpeedFactor == 1f &&
              k.Forward == 0f && k.Up == 0f && none.Forward == 0f && none.Right == 0f && none.Up == 0f && none.SpeedFactor == 1f,
              "maps viewer: W+D+Shift fly forwards and right four times as fast, S+A+Q back, left and down, opposite keys cancel, no key stops", log);
        // Frame-rate independent: the same second of W at 30 or 240 frames a second ends at the same place.
        var s = new WmvFlyCamera.State { Speed = WmvFlyCamera.DefaultSpeed };
        WmvFlyCamera.State at30 = s, at240 = s;
        var fw = new WmvFlyCamera.FlyInput { Forward = 1f, SpeedFactor = 1f };
        for (int n = 0; n < 30; n++) at30 = WmvFlyCamera.Step(at30, fw, 1f / 30f);
        for (int n = 0; n < 240; n++) at240 = WmvFlyCamera.Step(at240, fw, 1f / 240f);
        Check(Close(at30.Position, at240.Position) && Close(at30.Position, new Vector3(0f, 0f, WmvFlyCamera.DefaultSpeed)),
              "maps viewer: a second of W moves the free camera its speed in yards at 30 and at 240 frames a second (" + at30.Position + ", " + at240.Position + ")", log);
    }

    static bool Close(Vector3 a, Vector3 b)
    {
        return (a - b).magnitude < 1e-3f;
    }

    static WmvIpcClient.MapTilesRequest TilesRequest(byte[] wdt, string md5 = null)
    {
        return new WmvIpcClient.MapTilesRequest
        {
            Inspection = 5, MapId = 1, WdtFileDataID = 4242, AssetEpoch = 3, Bytes = wdt,
            Md5 = md5 ?? (wdt != null ? WmvTerrainSession.Md5Hex(wdt) : ""), Encoding = "binary",
        };
    }

    static void MapTilesTableTests(Action<string> log)
    {
        // Three tiles: (30, 48) with a root, (31, 48) MAIN only (MAID word 0 is 0), (2, 63) with a root -- (63, 2) is not
        // one, so a transposed table cannot pass.
        byte[] wdt = TerrainWdt(true, 30, 48, 12345, 31, 48, 0, 2, 63, 777);
        WmvIpcClient.MapTilesReport r = WmvMapTiles.Inspect(TilesRequest(wdt));
        int i30 = 48 * 64 + 30, i31 = 48 * 64 + 31, i2 = 63 * 64 + 2, iT = 2 * 64 + 63;
        int others = 0;
        if (r.TileFlags != null)
            for (int i = 0; i < r.TileFlags.Length; i++)
                if (i != i30 && i != i31 && i != i2 && r.TileFlags[i] != 0) others++;
        Check(r.Status == "inspected" && r.Inspection == 5 && r.MapId == 1 && r.WdtFileDataID == 4242 && r.AssetEpoch == 3 &&
              r.HasMain && r.HasMaid && r.TilesInMain == 3 && r.TilesWithRoot == 2 && r.TileFlags != null && r.TileFlags.Length == 4096 &&
              r.TileFlags[i30] == (WmvMapTiles.FlagMain | WmvMapTiles.FlagRoot) && r.TileFlags[i31] == WmvMapTiles.FlagMain &&
              r.TileFlags[i2] == (WmvMapTiles.FlagMain | WmvMapTiles.FlagRoot) && r.TileFlags[iT] == 0 && others == 0 &&
              r.RootIds[i30] == 12345 && r.RootIds[i31] == 0 && r.RootIds[i2] == 777 && r.Bytes == wdt.Length &&
              r.Md5 == WmvTerrainSession.Md5Hex(wdt) && r.Kind == "TiledTerrain",
              string.Format("maps viewer: the tile table of a synthetic WDT is the parser's, at y * 64 + x (status {0} {1}; {2} in MAIN, {3} with a root; " +
                            "flags 30_48 {4}, 31_48 {5}, 2_63 {6}, the transposed 63_2 {7}, {8} other tile(s) flagged; root ids {9}/{10}/{11}; kind {12})",
                            r.Status, r.Reason, r.TilesInMain, r.TilesWithRoot, r.TileFlags != null ? r.TileFlags[i30] : -1,
                            r.TileFlags != null ? r.TileFlags[i31] : -1, r.TileFlags != null ? r.TileFlags[i2] : -1,
                            r.TileFlags != null ? r.TileFlags[iT] : -1, others, r.RootIds != null ? r.RootIds[i30] : 0,
                            r.RootIds != null ? r.RootIds[i31] : 0, r.RootIds != null ? r.RootIds[i2] : 0, r.Kind), log);

        WmvIpcClient.MapTilesReport noMaid = WmvMapTiles.Inspect(TilesRequest(TerrainWdt(false, 30, 48, 12345)));
        Check(noMaid.Status == "inspected" && !noMaid.HasMaid && noMaid.RootIds == null && noMaid.Tex0Ids == null && noMaid.TilesInMain == 1 &&
              noMaid.TilesWithRoot == 0 && noMaid.TileFlags[i30] == WmvMapTiles.FlagMain,
              "maps viewer: a WDT without MAID: its MAIN flag, no root ids, no tile with a root (" + noMaid.Status + ", " + noMaid.TilesInMain +
              " in MAIN, " + noMaid.TilesWithRoot + " with a root)", log);

        WmvIpcClient.MapTilesReport badMd5 = WmvMapTiles.Inspect(TilesRequest(wdt, "00000000000000000000000000000000"));
        WmvIpcClient.MapTilesReport none = WmvMapTiles.Inspect(TilesRequest(null, "00"));
        WmvIpcClient.MapTilesReport bad = WmvMapTiles.Inspect(TilesRequest(AdtSynthetic.Cat(AdtSynthetic.Mver())));
        Check(badMd5.Status == "failed" && badMd5.Reason.Contains("not to the content key") && badMd5.TileFlags == null &&
              none.Status == "failed" && none.Reason.Contains("did not come") && bad.Status == "failed" && bad.Reason.Contains("could not be parsed"),
              "maps viewer: refused, with the reason: bytes that do not hash to the host's content key (" + badMd5.Reason + "), no bytes (" +
              none.Reason + "), a malformed WDT (" + bad.Reason + ")", log);

        string line = WmvIpcClient.MapTilesInspectedLine(r);
        bool lineOk = false;
        string why = "";
        try
        {
            var p = JsonUtility.FromJson<TilesLine>(line);
            byte[] flags = Convert.FromBase64String(p.tileFlags);
            byte[] roots = Convert.FromBase64String(p.rootIds);
            uint root30 = (uint)(roots[4 * i30] | roots[4 * i30 + 1] << 8 | roots[4 * i30 + 2] << 16 | roots[4 * i30 + 3] << 24);
            lineOk = p.type == "mapTilesInspected" && p.inspection == 5 && p.status == "inspected" && flags.Length == 4096 &&
                     flags[i30] == 3 && roots.Length == 4 * 4096 && root30 == 12345 && p.tilesInMain == 3 && p.tilesWithRoot == 2;
            why = string.Format("{0} {1} flags {2} roots {3} root 30_48 {4}", p.type, p.status, flags.Length, roots.Length, root30);
        }
        catch (Exception e) { why = e.Message; }
        Check(lineOk, "maps viewer: the mapTilesInspected line carries the table (base64 flags and little-endian ids): " + why, log);
    }

    [Serializable]
    class TilesLine
    {
        public string type, status, tileFlags, rootIds;
        public int inspection, tilesInMain, tilesWithRoot;
    }

    static void TerrainRuntimeViewTests(Action<string> log)
    {
        bool takes = WmvTerrainDebug.IsRuntimeView("auto") && WmvTerrainDebug.IsRuntimeView("textured") && WmvTerrainDebug.IsRuntimeView("normals") &&
                     WmvTerrainDebug.IsRuntimeView("materials") && WmvTerrainDebug.IsRuntimeView("wire");
        bool refuses = !WmvTerrainDebug.IsRuntimeView("texturedplainweights") && !WmvTerrainDebug.IsRuntimeView("bogus") &&
                       !WmvTerrainDebug.IsRuntimeView("") && !WmvTerrainDebug.SetRuntimeView("bogus");
        bool normals = WmvTerrainDebug.SetRuntimeView("normals") && WmvTerrainDebug.EffectiveView(null) == WmvTerrainView.Normals &&
                       WmvTerrainDebug.RuntimeViewName == "normals";
        bool back = WmvTerrainDebug.SetRuntimeView("auto") && WmvTerrainDebug.RuntimeViewName == "auto";
        Check(takes && refuses && normals && back,
              "maps viewer: setTerrainView takes auto and every view but texturedplainweights, refuses others, and the view it sets is the one " +
              "tiles are drawn in (" + takes + "/" + refuses + "/" + normals + "/" + back + ")", log);
    }

    static void TerrainRebuildTests(Action<string> log)
    {
        int tx = TerrainTestTileX, ty = TerrainTestTileY;
        TerrainTileMaterial want = SettledMaterial(tx, ty, MatHeightsOn, null, new[] { MatMissingHeight });
        int uploadedWant = UploadedFor(want);

        // ---- a textured tile, drawn shaded, then textured again: the same textures, handed over, nothing asked
        {
            var h = new TerrainHarness();
            var host = new MaterialHost(tx, ty);
            h.Session.Load(TexturedCommand(701, tx, ty));
            host.ServeAll(h);
            h.Pump(3);
            WmvIpcClient.TerrainTileReport r = h.For(701);
            WmvRuntimeTerrainTile first = h.Session.ShownTile;
            int texturesBuilt = WmvRuntimeTerrainTile.LiveTextures, sent = h.Sent.Count, shown = h.View.Shown;
            long bytesBuilt = WmvRuntimeTerrainTile.LiveTextureBytes;
            string reason;
            double ms;
            int load;
            h.View.View = WmvTerrainView.Shaded;
            string s1 = h.Session.RebuildShown("test: shaded", out reason, out ms, out load);
            WmvRuntimeTerrainTile shaded = h.Session.ShownTile;
            int texturedShaded = 0;
            for (int n = 0; shaded != null && n < 256; n++) if (shaded.ChunkTextured[n]) texturedShaded++;
            bool shadedOk = s1 == "applied" && load == 701 && shaded != null && shaded != first && first.Disposed && !shaded.Disposed &&
                            shaded.View == WmvTerrainView.Shaded && texturedShaded == 0 && WmvRuntimeTerrainTile.Live == 1 &&
                            WmvRuntimeTerrainTile.LiveTextures == texturesBuilt && WmvRuntimeTerrainTile.LiveTextureBytes == bytesBuilt &&
                            shaded.Textures.Count == texturesBuilt && shaded.Root != null && shaded.Root.activeSelf &&
                            h.Sent.Count == sent && h.View.Shown == shown && h.Reports.Count == 1 && WmvRuntimeTerrainTile.LiveAlphaArrays == 0;
            h.View.View = WmvTerrainView.Textured;
            string s2 = h.Session.RebuildShown("test: textured", out reason, out ms, out load);
            WmvRuntimeTerrainTile again = h.Session.ShownTile;
            int texturedAgain = 0;
            for (int n = 0; again != null && n < 256; n++) if (again.ChunkTextured[n]) texturedAgain++;
            bool texturedOk = s2 == "applied" && again != null && again != shaded && shaded.Disposed && again.View == WmvTerrainView.Textured &&
                              texturedAgain == want.DrawableChunks && WmvRuntimeTerrainTile.LiveTextures == texturesBuilt &&
                              WmvRuntimeTerrainTile.LiveTextureBytes == bytesBuilt && WmvRuntimeTerrainTile.LiveAlphaArrays == 1 &&
                              h.Sent.Count == sent && h.View.Shown == shown && h.Reports.Count == 1 && WmvRuntimeTerrainTile.Live == 1;
            Check(r != null && r.Status == "built" && texturesBuilt == uploadedWant && shadedOk && texturedOk,
                  string.Format("maps viewer: the tile on screen drawn again in another view from what the player holds -- shaded: {0} ({1} chunks " +
                                "textured, {2} texture(s) kept, handed over), textured again: {3} ({4} chunks textured of {5} drawable); textures " +
                                "{6} -> {7} (want {8}), GPU bytes {9} -> {10}; requests sent {11} -> {12}, ShowTile calls {13} -> {14} (no framing), " +
                                "reports {15}", s1, texturedShaded, shaded != null ? shaded.Textures.Count : -1, s2, texturedAgain,
                                want.DrawableChunks, texturesBuilt, WmvRuntimeTerrainTile.LiveTextures, uploadedWant, bytesBuilt,
                                WmvRuntimeTerrainTile.LiveTextureBytes, sent, h.Sent.Count, shown, h.View.Shown, h.Reports.Count), log);

            // A rebuild that throws: the tile on screen stays as it was, with its textures.
            h.View.FailBuild = true;
            h.View.View = WmvTerrainView.Normals;
            string s3 = h.Session.RebuildShown("test: fails", out reason, out ms, out load);
            h.View.FailBuild = false;
            bool kept = s3 == "failed" && reason.Contains("failed") && h.Session.ShownTile == again && !again.Disposed &&
                        WmvRuntimeTerrainTile.Live == 1 && WmvRuntimeTerrainTile.LiveTextures == texturesBuilt &&
                        again.Textures.Count == texturesBuilt && again.Root != null;
            Check(kept, "maps viewer: a rebuild that throws disposes only the new tile; the one on screen keeps its objects and textures (" + s3 +
                  ": " + reason + "; live tiles " + WmvRuntimeTerrainTile.Live + ", textures " + WmvRuntimeTerrainTile.LiveTextures + ")", log);

            h.Session.Clear("test");
            string s4 = h.Session.RebuildShown("test: nothing", out reason, out ms, out load);
            Check(s4 == "noTile" && load == 0 && WmvRuntimeTerrainTile.Live == 0 && WmvRuntimeTerrainTile.LiveTextures == 0 &&
                  WmvRuntimeTerrainTile.LiveAlphaArrays == 0 && TerrainTallies() == "0/0/0" && h.Session.RetainedTextureBytes == 0,
                  "maps viewer: after clearTerrain nothing is rebuilt (" + s4 + ": " + reason + ") and every tile, texture and alpha array is gone " +
                  "(tallies " + TerrainTallies() + ", textures " + WmvRuntimeTerrainTile.LiveTextures + ")", log);
        }

        // ---- a textured tile loaded in a view the HOST chose (setTerrainView) that uploads nothing: the textures' data
        // kept, uploaded by a textured rebuild
        {
            var h = new TerrainHarness();
            var host = new MaterialHost(tx, ty);
            h.View.View = WmvTerrainView.Chunks;
            WmvTerrainDebug.SetRuntimeView("chunks");
            h.Session.Load(TexturedCommand(711, tx, ty));
            host.ServeAll(h);
            h.Pump(3);
            WmvIpcClient.TerrainTileReport r = h.For(711);
            long retained = h.Session.RetainedTextureBytes;
            int texturesChunks = WmvRuntimeTerrainTile.LiveTextures;
            int sent = h.Sent.Count;
            h.View.View = WmvTerrainView.Textured;
            string reason;
            double ms;
            int load;
            string s = h.Session.RebuildShown("test: textured", out reason, out ms, out load);
            WmvRuntimeTerrainTile t = h.Session.ShownTile;
            int textured = 0;
            for (int n = 0; t != null && n < 256; n++) if (t.ChunkTextured[n]) textured++;
            Check(r != null && r.Status == "built" && r.View == "chunks" && texturesChunks == 0 && retained > 0 && s == "applied" &&
                  WmvRuntimeTerrainTile.LiveTextures == uploadedWant && textured == want.DrawableChunks && h.Session.RetainedTextureBytes == 0 &&
                  h.Sent.Count == sent,
                  string.Format("maps viewer: a tile loaded in the chunks view (report view \"{0}\") uploads no texture and keeps their data ({1} bytes); " +
                                "a textured rebuild uploads them from it ({2}: {3} texture(s), want {4}; {5} chunks textured) and keeps none after " +
                                "({6} bytes); no request sent ({7} -> {8})", r != null ? r.View : "none", retained, s,
                                WmvRuntimeTerrainTile.LiveTextures, uploadedWant, textured, h.Session.RetainedTextureBytes, sent, h.Sent.Count), log);
            h.Session.Clear("test");
            WmvTerrainDebug.SetRuntimeView("auto");
            Check(WmvRuntimeTerrainTile.Live == 0 && WmvRuntimeTerrainTile.LiveTextures == 0 && h.Session.RetainedTextureBytes == 0 &&
                  TerrainTallies() == "0/0/0", "maps viewer: clearTerrain releases the rebuilt tile and its kept data", log);
        }

        // ---- the same view NOT chosen by the host (the command line's, or the material's): the data go at once, as before
        // protocol 14 (the -terraintile harness's loads keep their memory profile)
        {
            var h = new TerrainHarness();
            var host = new MaterialHost(tx, ty);
            h.View.View = WmvTerrainView.Chunks;
            h.Session.Load(TexturedCommand(712, tx, ty));
            host.ServeAll(h);
            h.Pump(3);
            WmvIpcClient.TerrainTileReport r = h.For(712);
            long retained = h.Session.RetainedTextureBytes;
            h.Session.Clear("test");
            Check(r != null && r.Status == "built" && retained == 0 && TerrainTallies() == "0/0/0",
                  "maps viewer: a tile in a view the host did not choose keeps no texture data (" + retained + " bytes), as before", log);
        }

        // ---- a view switch in the two frames between the adoption and the report: the report is "built" (the tile on screen
        // is the load's own, rebuilt), not "superseded"
        {
            var h = new TerrainHarness();
            var host = new MaterialHost(tx, ty);
            h.Session.Load(TexturedCommand(721, tx, ty));
            // Served frame by frame until the tile is adopted (on screen), and no further: its report waits two frames.
            for (int i = 0; i < 40 && h.Session.ShownTile == null; i++)
            {
                foreach (string id in h.Session.PendingIds()) host.Serve(h, id);
                h.Pump(1);
            }
            bool unreported = h.For(721) == null && h.Session.ShownTile != null;
            h.View.View = WmvTerrainView.Shaded;
            string reason;
            double ms;
            int load;
            string s = h.Session.RebuildShown("test: in the report window", out reason, out ms, out load);
            h.Pump(3);
            WmvIpcClient.TerrainTileReport r = h.For(721);
            Check(unreported && s == "applied" && load == 721 && r != null && r.Status == "built" && h.CountFor(721) == 1 &&
                  h.Session.ShownTile != null && !h.Session.ShownTile.Disposed && WmvRuntimeTerrainTile.Live == 1,
                  "maps viewer: a view switch between the adoption and the report leaves the load reported \"built\" once (" + s + "; " +
                  (r != null ? r.Status + " " + r.Reason : "no report") + "; reports " + h.CountFor(721) + ")", log);
            h.Session.Clear("test");
            Check(TerrainTallies() == "0/0/0" && WmvRuntimeTerrainTile.LiveTextures == 0, "maps viewer: and clearTerrain releases it all", log);
            h.View.View = null;
        }

        // ---- the report line names the view
        {
            var rep = new WmvIpcClient.TerrainTileReport { Load = 3, Status = "built", View = "normals" };
            string line = WmvIpcClient.TerrainTileLoadedLine(rep);
            var v = new WmvIpcClient.TerrainViewReport { View = "chunks", Status = "applied", Load = 3, RebuildMs = 1.5, TexturedChunks = 0, NeutralChunks = 256 };
            string vline = WmvIpcClient.TerrainViewAppliedLine(v);
            Check(line.Contains("\"view\":\"normals\"") && vline.Contains("\"type\":\"terrainViewApplied\"") && vline.Contains("\"status\":\"applied\"") &&
                  vline.Contains("\"neutralChunks\":256"),
                  "maps viewer: terrainTileLoaded names the view (protocol 14) and terrainViewApplied carries the rebuild: " + vline, log);
        }
    }
}
