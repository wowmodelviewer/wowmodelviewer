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
//     every tile, texture and alpha array released by clearTerrain.

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
