// WmvMainTerrain.cs
//
// The TERRAIN TILE in the viewport player (protocol 12, stage 03) -- the part of WmvMain that wires
// WmvTerrainSession (the load, its report and the scene of tiles) and WmvTerrainBuilder (the Unity
// objects) into the player:
//
//   loadTerrainTile  -> the latest command wins: the LOADS in flight are dropped as a new model load drops
//                       them (the world-model job answered "superseded", a character being loaded answered
//                       "superseded" or, before its scene came, "rejected"); the model ON SCREEN stays,
//                       untouched and hidden by the camera mask -- a scene or mount its dresser is still
//                       preparing keeps being prepared and commits while hidden (nothing of it moves the camera
//                       in the terrain view), so it comes back as the host last described it; then
//                       WmvTerrainSession.Load
//   clearTerrain     -> WmvTerrainSession.Clear
//   loadWoWModel     -> before anything else of HandleLoadWoWModel: the terrain load in flight is answered
//                       "superseded", the tile disposed and the terrain view left
//   assetResponse    -> a map file's answer (id "map<n>", or a role) comes through WmvIpcClient.OnMapAssetResponse to
//                       HandleMapAssetResponse, the session's alone: applied to its load, or dropped as late or
//                       unowned; it never reaches HandleAssetResponse, which drops one that did before any model,
//                       character or world-model path
//   Update           -> the session's Pump (worker results, the timeout -- 30 s, or -wmvTerrainTimeoutMs -- and the
//                       two-frame report)
//
// THE VIEW MODE (SetViewMode, the one owner of the main camera's culling mask). Model view: ModelViewMask (every
// layer but 30), set in Awake. Terrain view: TerrainViewMask (30 only), entered when a tile is committed: the orbit
// camera's whole state (WmvOrbitCamera.Snapshot) and the capture's framing record (lastFramed and friends) are kept,
// the tile is framed as a world model (FrameMapObject on its bounds, or chunk N's with -wmvTerrainFocus), then
// WMV_VIEWPORT_ORBIT ("terrain: "), then -wmvTerrainTopDown. Leaving restores both exactly, puts ModelViewMask back
// and owes the next load a framing (framingOwed). While the terrain view is on, nothing of the hidden model moves
// the camera: FrameRiddenScene, KeepFramed, a model's RefitLastFramed and every other ApplyViewportOrbitOverride stay
// off it, and RefitLastFramed (a WMV_VIEWPORT_SHOT resize) frames the TILE again. A fit FrameRiddenScene could not do
// (the hidden character's mount changed) is kept, uncounted, and done when clearTerrain leaves the view; a model load
// leaving it drops it (that load frames what it shows).
//
// THE CAPTURE. WMV_VIEWPORT_SHOT is armed after the load is reported "built" (RequestViewportShot: its 40-frame
// settle, ViewportBusy counting the terrain load); the log line names the tile, its view and the camera, and every
// such picture is a geometry diagnostic, not WoW material or lighting. The host refuses the IPC Save-As capture while
// only terrain is shown, so that path is not reached in the terrain view.
//
// -wmvTerrainDump=<bare name>: the meshes are kept readable and, when the load is reported built, read BACK from Unity
// (positions, normals, indices, sub-meshes, topology, index format, bounds, layers, activity, transforms) into
// <player folder>/<name>.txt in the TerrainDump format, for Tools/world-audit/compare_terrain_mesh.py.

using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow.World;

public partial class WmvMain
{
    WmvTerrainSession terrain;
    WmvTerrainViewMode terrainViewMode;
    GameObject terrainSceneRoot;          // "_WmvTerrain": every tile root hangs under it, at the identity
    Bounds terrainFrame;                  // the box the terrain view was last framed on
    bool haveTerrainFrame;

    /// <summary>The ApplyViewportOrbitOverride tag of the terrain framing: the only one honoured in the terrain view.</summary>
    const string TerrainOverrideTag = "terrain: ";

    /// <summary>The terrain view is on: the main camera shows the terrain layer only.</summary>
    bool TerrainViewActive { get { return terrainViewMode != null && terrainViewMode.Active; } }

    /// <summary>Awake: the model view's mask on the main camera, the session, and the host's terrain messages.</summary>
    void SetUpTerrain(Camera cam)
    {
        cam.cullingMask = WmvTerrainBuilder.ModelViewMask;
        terrainViewMode = new WmvTerrainViewMode(orbit, cam);
        terrain = new WmvTerrainSession(new MainTerrainView(this), ipc.RequestMapAsset, ipc.ReportTerrainTileLoaded,
                                        s => Debug.Log("WMV: " + s),
                                        a => System.Threading.ThreadPool.QueueUserWorkItem(_ => a()), WmvTerrainDebug.TimeoutMs);
        WireTerrainMessages();
        foreach (string note in WmvTerrainDebug.Notes)
            Debug.LogWarning("WMV: [terrain] " + note);
        Debug.Log("WMV: [terrain] ready: layer " + WmvTerrainBuilder.Layer + ", model view mask " + WmvTerrainBuilder.ModelViewMask +
                  ", terrain view mask " + WmvTerrainBuilder.TerrainViewMask + ", load timeout " + terrain.TimeoutMs + " ms; " +
                  WmvTerrainDebug.Describe());
    }

    /// <summary>The host's terrain messages: loadTerrainTile, clearTerrain, and every map file's answer.</summary>
    void WireTerrainMessages()
    {
        ipc.OnLoadTerrainTile = HandleLoadTerrainTile;
        ipc.OnClearTerrain = HandleClearTerrain;
        ipc.OnMapAssetResponse = HandleMapAssetResponse;
    }

    void HandleClearTerrain(string reason)
    {
        if (terrain != null)
            terrain.Clear(reason);
    }

    /// <summary>A map file's answer (getMapAsset): the terrain session's, applied to its load or dropped there as late or
    /// unowned; with no session (shut down) it is dropped and said. Never handed on to any model path.</summary>
    void HandleMapAssetResponse(WmvIpcClient.AssetResponse r)
    {
        if (terrain != null)
        {
            terrain.ClaimMapAssetResponse(r);
            return;
        }
        Debug.LogWarning("WMV: [terrain] a map answer (request " + (r != null ? r.requestId : "none") + ") with no terrain session -- dropped");
    }

    void HandleLoadTerrainTile(WmvIpcClient.TerrainTileCommand cmd)
    {
        status.Set(string.Format("Terrain tile requested: map {0} tile {1}_{2} (load {3})", cmd.MapId, cmd.TileX, cmd.TileY, cmd.Load));
        DropModelWorkForTerrain("superseded by terrain load " + cmd.Load);
        terrain.Load(cmd);
    }

    /// <summary>
    /// The model work a terrain load drops: the LOADS in flight, as HandleLoadWoWModel drops them for a new model load --
    /// the world-model job (answered "superseded"), a pending geoset push of the load in flight ("rejected"), and the
    /// load itself: a character whose scene had come is answered "superseded" (AbandonCharacterJob, which disposes the
    /// load's own dresser and mount), one whose scene had not yet come "rejected" (the Fail path), so each character load
    /// gets exactly one answer.
    /// NOT the character ON SCREEN: unlike a model load, a terrain load does not replace it -- it stays, hidden by the
    /// terrain view's mask, and comes back when the view is left. So the scene its dresser is still preparing and the
    /// mount being prepared for it are left to finish and commit while it is hidden (HandleLoadWoWModel cancels them
    /// because that character is about to go; cancelling them here answered the host's newest revision "superseded",
    /// which the host does not send again, and the character came back without it). Committing while hidden is safe:
    /// in the terrain view nothing of the hidden model moves the camera (FrameRiddenScene keeps its fit for when the
    /// view is left, KeepFramed and every non-terrain override stay off it).
    /// </summary>
    void DropModelWorkForTerrain(string reason)
    {
        SupersedeMapObjectJob(reason);
        if (job == null)
            return;
        bool characterLoad = job.Character || (job.Scene != null && job.Scene.attachmentsOnly);
        Debug.Log(string.Format("WMV: [terrain] the model load {0} of {1} in flight is dropped ({2})", job.Load,
                                string.IsNullOrEmpty(job.Path) ? job.FileDataID.ToString() : job.Path, reason));
        if (characterLoad && job.Scene == null)
        {
            Fail(reason);                       // "rejected", the pending geoset push too; job = null
            return;
        }
        if (characterLoad)
            AbandonCharacterJob(reason);        // its scene is answered "superseded"
        if (haveLoadSubmeshVisible && loadGeosetRevision > 0)
            ipc.ReportGeosetsApplied(job.FileDataID, loadGeosetRevision, "rejected", reason, null, 0, 0.0);
        loadSubmeshVisible = null;
        haveLoadSubmeshVisible = false;
        loadGeosetRevision = 0;
        job = null;
    }

    /// <summary>A loadWoWModel arrived (any kind): the terrain goes first.</summary>
    void LeaveTerrainForModelLoad()
    {
        if (terrain != null)
            terrain.LeaveForModelLoad("superseded by a model load");
    }

    void PumpTerrain()
    {
        if (terrain != null)
            terrain.Pump(WmvIpcClient.NowSeconds * 1000.0);
    }

    void DisposeTerrain()
    {
        if (terrain != null)
            terrain.Dispose();
        if (terrainSceneRoot != null)
            Destroy(terrainSceneRoot);
        terrainSceneRoot = null;
    }

    /// <summary>The scene root every tile root hangs under: at the identity, on the terrain layer.</summary>
    Transform TerrainSceneRoot()
    {
        if (terrainSceneRoot == null)
        {
            terrainSceneRoot = new GameObject("_WmvTerrain");
            terrainSceneRoot.layer = WmvTerrainBuilder.Layer;
        }
        return terrainSceneRoot.transform;
    }

    // ---------------------------------------------------------------- the view mode

    /// <summary>The one owner of the main camera's mask: terrain on (snapshot, TerrainViewMask) or off (restore,
    /// ModelViewMask, framingOwed). Off for clearTerrain (forModelLoad false), a fit the hidden character's mount change
    /// asked for while the view held the camera (FrameRiddenScene) is done then, after the restore, and counted; off for
    /// a model load it is dropped -- that load frames what it shows.</summary>
    void SetViewMode(bool terrainOn, string why, bool forModelLoad = false)
    {
        if (terrainOn)
        {
            var current = new WmvTerrainViewMode.Framing { Box = lastFramed, Aspect = lastFramedAspect, Have = haveLastFramed, Kept = lastFramedKept };
            if (terrainViewMode.Enter(current))
                Debug.Log("WMV: [terrain] terrain view on (" + why + "): the model view is kept -- " + terrainViewMode.Saved +
                          (haveLastFramed ? ", framed on " + lastFramed.center + " extents " + lastFramed.extents : ", nothing framed"));
            return;
        }
        WmvTerrainViewMode.Framing f;
        if (!terrainViewMode.Leave(out f))
            return;
        lastFramed = f.Box;
        lastFramedAspect = f.Aspect;
        haveLastFramed = f.Have;
        lastFramedKept = f.Kept;
        haveTerrainFrame = false;
        // What is on screen now was never framed for the load that comes next: it frames (see AdoptBuilt).
        framingOwed = true;
        Debug.Log("WMV: [terrain] terrain view off (" + why + "): the model view is back as it was -- " + orbit.Snapshot());
        bool fit = haveRiddenFitPending;
        haveRiddenFitPending = false;
        if (!fit)
            return;
        WmvRuntimeModel body = dresser != null ? dresser.Body : null;
        if (forModelLoad || body == null || body.Root == null)
        {
            Debug.Log("WMV: [terrain] the fit a mount change asked for in the terrain view is dropped (" +
                      (forModelLoad ? "a model load frames what it shows" : "no character on screen") + ")");
            return;
        }
        ViewFramings++;
        orbit.Frame(riddenFitPending);
        KeepFramed(riddenFitPending);
        ApplyViewportOrbitOverride("mount: ");
        Debug.Log(string.Format("WMV: [terrain] the fit a mount change asked for in the terrain view is done now: centre {0} extents {1}, " +
                                "distance {2:F2}, yaw {3} pitch {4}", riddenFitPending.center, riddenFitPending.extents, orbit.distance,
                                orbit.yaw, orbit.pitch));
    }

    /// <summary>The box the terrain view frames: chunk N's with -wmvTerrainFocus (refused, and said, when it has no
    /// triangles), else the tile's -- or its footprint at height 0 when it has no triangle at all.</summary>
    Bounds TerrainFrameBox(WmvRuntimeTerrainTile tile)
    {
        float t = (float)TerrainCoordinates.TileSize;
        Bounds frame = tile.HasBounds ? tile.Bounds : new Bounds(new Vector3(t * 0.5f, 0f, -t * 0.5f), new Vector3(t, 0f, t));
        int focus = WmvTerrainDebug.Focus;
        if (focus >= 0)
        {
            Bounds b;
            if (tile.TryGetChunkBounds(focus, out b))
            {
                Debug.Log("WMV: [terrain] -wmvTerrainFocus=" + focus + ": the camera frames chunk " + focus + " (centre " + b.center +
                          " extents " + b.extents + ")");
                frame = b;
            }
            else
                Debug.Log("WMV: [terrain] -wmvTerrainFocus=" + focus + " refused: chunk " + focus + " has no triangles -- the tile is framed");
        }
        return frame;
    }

    /// <summary>Frame the committed tile: as a world model, then WMV_VIEWPORT_ORBIT, then -wmvTerrainTopDown.</summary>
    void FrameTerrainTile(WmvRuntimeTerrainTile tile)
    {
        Bounds frame = TerrainFrameBox(tile);
        terrainFrame = frame;
        haveTerrainFrame = true;
        orbit.FrameMapObject(frame);
        Camera cam = Camera.main;
        // The capture's framing record is the tile's while the view is on (a resize refits it, RefitLastFramed).
        lastFramed = frame;
        lastFramedAspect = cam != null ? cam.aspect : 1f;
        haveLastFramed = true;
        lastFramedKept = false;
        ApplyViewportOrbitOverride(TerrainOverrideTag);
        if (WmvTerrainDebug.TopDown)
            ApplyTerrainTopDown(tile);
        Debug.Log(string.Format(System.Globalization.CultureInfo.InvariantCulture,
            "WMV: [terrain] framed tile {0}_{1} (load {2}) on centre {3} radius {4:F1}: {5}, clip planes {6:F3} .. {7:F1}, mask {8}",
            tile.TileX, tile.TileY, tile.Load, frame.center, frame.extents.magnitude,
            terrainViewMode.TopDown ? string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                                    "top-down orthographic, half-size {0:F3}", cam != null ? cam.orthographicSize : 0f)
                                    : string.Format(System.Globalization.CultureInfo.InvariantCulture, "distance {0:F1}, yaw {1} pitch {2}",
                                                    orbit.distance, orbit.yaw, orbit.pitch),
            cam != null ? cam.nearClipPlane : 0f, cam != null ? cam.farClipPlane : 0f, cam != null ? cam.cullingMask : 0));
    }

    void ApplyTerrainTopDown(WmvRuntimeTerrainTile tile)
    {
        float lo = tile.HasBounds ? tile.Bounds.min.y : 0f, hi = tile.HasBounds ? tile.Bounds.max.y : 0f;
        terrainViewMode.ApplyTopDown(lo, hi);
        Camera cam = Camera.main;
        if (cam != null && Mathf.Abs(cam.aspect - 1f) > 1e-3f)
            Debug.LogWarning(string.Format("WMV: [terrain] -wmvTerrainTopDown: the viewport is {0}x{1}, not square -- the capture is " +
                                           "not the tile alone (set WMV_VIEWPORT_SIZE with WMV_VIEWPORT_SHOT)", Screen.width, Screen.height));
    }

    /// <summary>RefitLastFramed in the terrain view: the TILE is framed again for the new aspect, never the hidden model.</summary>
    void RefitTerrain(Camera cam, string what)
    {
        lastFramedAspect = cam.aspect;
        if (!haveTerrainFrame)
            return;
        orbit.FrameMapObject(terrainFrame);
        ApplyViewportOrbitOverride(TerrainOverrideTag);
        WmvRuntimeTerrainTile tile = terrain != null ? terrain.ShownTile : null;
        if (WmvTerrainDebug.TopDown && tile != null)
            ApplyTerrainTopDown(tile);
        Debug.Log("WMV: [terrain] " + what + "the tile framed again for aspect " + cam.aspect.ToString("F3"));
    }

    /// <summary>Does the terrain view hold the camera against this ApplyViewportOrbitOverride caller?</summary>
    bool TerrainHoldsCamera(string what)
    {
        return TerrainViewActive && what != TerrainOverrideTag;
    }

    /// <summary>The load was reported built (its tile on screen two frames): the dump, then the capture.</summary>
    void TerrainTileBuilt(WmvRuntimeTerrainTile tile, TerrainTileMesh mesh, WmvIpcClient.TerrainTileCommand cmd, int rootFileDataID)
    {
        status.Set(string.Format("Terrain tile {0}_{1} of map {2}: {3} vertices, {4} triangles, {5} hole cell(s) -- geometry diagnostic ({6})",
                                 tile.TileX, tile.TileY, tile.MapId, mesh.Vertices, mesh.Triangles, mesh.HoleCells, WmvTerrainDebug.ViewName));
        if (WmvTerrainDebug.DumpName != null)
            WriteTerrainDump(tile, mesh, cmd, rootFileDataID, WmvTerrainDebug.DumpName);
        RequestViewportShot();
    }

    /// <summary>runtimeState's protocol-12 fields. What the terrain holds (terrainMeshes, terrainMaterials,
    /// terrainGameObjects) is the tiles' own tally, never a scan of the scene: it counts a tile still being built, never
    /// the persistent scene root _WmvTerrain, and drops the moment a tile is released, not when Unity's deferred
    /// Object.Destroy lands -- so it is 0 with no tile, right after a clear, a model load or a failed build. Then the
    /// session's request bookkeeping (pending 0 or 1, retired at most 32, retired in all and the last one retired, map
    /// answers dropped -- late plus unowned, and each apart) and its timeout.</summary>
    void FillTerrainRuntimeState(ref WmvIpcClient.RuntimeReport r)
    {
        Camera cam = Camera.main;
        r.LiveTerrainTiles = WmvRuntimeTerrainTile.Live;
        r.TerrainLoad = terrain != null ? terrain.ShownLoad : 0;
        r.TerrainLoading = terrain != null && terrain.Loading;
        r.TerrainView = TerrainViewActive;
        r.CameraMask = cam != null ? cam.cullingMask : 0;
        r.MeshObjects = Resources.FindObjectsOfTypeAll<Mesh>().Length;
        r.MaterialObjects = Resources.FindObjectsOfTypeAll<Material>().Length;
        r.TerrainMeshes = WmvRuntimeTerrainTile.LiveMeshes;
        r.TerrainMaterials = WmvRuntimeTerrainTile.LiveMaterials;
        r.TerrainGameObjects = WmvRuntimeTerrainTile.LiveGameObjects;
        r.TerrainPendingRequests = terrain != null ? terrain.PendingRequests : 0;
        r.TerrainRetiredRequests = terrain != null ? terrain.RetiredRequests : 0;
        r.MapAnswersDropped = terrain != null ? terrain.AnswersDropped : 0;
        r.TerrainTimeoutMs = (int)(terrain != null ? terrain.TimeoutMs : WmvTerrainDebug.TimeoutMs);
        r.TerrainRetiredTotal = terrain != null ? terrain.RetiredTotal : 0;
        r.TerrainLastRetiredRequest = terrain != null ? terrain.LastRetiredRequest : "";
        r.MapAnswersLate = terrain != null ? terrain.LateAnswersDropped : 0;
        r.MapAnswersUnowned = terrain != null ? terrain.UnownedAnswersDropped : 0;
    }

    /// <summary>The capture's log line in the terrain view: the tile, not the hidden model.</summary>
    void LogTerrainShot(string name, Texture2D tex, string busy)
    {
        WmvRuntimeTerrainTile tile = terrain != null ? terrain.ShownTile : null;
        Camera cam = Camera.main;
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        Debug.Log(string.Format(inv,
            "WMV: viewport shot {0} #{1} -- {2}x{3}: GEOMETRY DIAGNOSTIC (not WoW material or lighting), terrain {4}, view {5}{6}; " +
            "camera at ({7:F3}, {8:F3}, {9:F3}), {10}, field of view {11:0.##}, aspect {12:F3}, mask {13}{14}",
            name, viewportShots, tex.width, tex.height,
            tile != null ? string.Format(inv, "map {0} tile {1}_{2} (load {3})", tile.MapId, tile.TileX, tile.TileY, tile.Load) : "none shown",
            WmvTerrainDebug.ViewName, WmvTerrainDebug.OnlyChunk >= 0 ? ", only chunk " + WmvTerrainDebug.OnlyChunk : "",
            cam != null ? cam.transform.position.x : 0f, cam != null ? cam.transform.position.y : 0f, cam != null ? cam.transform.position.z : 0f,
            terrainViewMode.TopDown ? string.Format(inv, "top-down orthographic half-size {0:F3}", cam != null ? cam.orthographicSize : 0f)
                                    : string.Format(inv, "pivot ({0:F3}, {1:F3}, {2:F3}) yaw {3:0.##} pitch {4:0.##} distance {5:F3}",
                                                    orbit.pivot.x, orbit.pivot.y, orbit.pivot.z, orbit.yaw, orbit.pitch, orbit.distance),
            cam != null ? cam.fieldOfView : 0f, cam != null ? cam.aspect : 0f, cam != null ? cam.cullingMask : 0,
            busy == null ? "" : "; taken while " + busy));
    }

    // ---------------------------------------------------------------- -wmvTerrainDump

    /// <summary>The tile's meshes read back from Unity into the TerrainDump format (source "player"): the builder's
    /// counts and states (TerrainDump.FromMesh), every array of a chunk with a mesh replaced by what the mesh holds, the
    /// chunks without triangles stripped of theirs (they have no object), and the Unity facts of the root and of each
    /// chunk object.</summary>
    void WriteTerrainDump(WmvRuntimeTerrainTile tile, TerrainTileMesh mesh, WmvIpcClient.TerrainTileCommand cmd, int rootFileDataID,
                          string name)
    {
        var clock = System.Diagnostics.Stopwatch.StartNew();
        string path = System.IO.Path.GetFullPath(System.IO.Path.Combine(Application.dataPath, "../" + name + ".txt"));
        try
        {
            TerrainDumpTile d = TerrainDump.FromMesh(mesh, "player");
            d.Meta.Add(new KeyValuePair<string, string>("client", string.IsNullOrEmpty(cmd.Product) ? "unknown" : cmd.Product));
            d.Meta.Add(new KeyValuePair<string, string>("build", string.IsNullOrEmpty(cmd.Build) ? "unknown" : cmd.Build));
            d.Meta.Add(new KeyValuePair<string, string>("mapId", cmd.MapId.ToString(System.Globalization.CultureInfo.InvariantCulture)));
            d.Meta.Add(new KeyValuePair<string, string>("load", cmd.Load.ToString(System.Globalization.CultureInfo.InvariantCulture)));
            d.Meta.Add(new KeyValuePair<string, string>("rootFileDataID", rootFileDataID.ToString(System.Globalization.CultureInfo.InvariantCulture)));
            d.Meta.Add(new KeyValuePair<string, string>("view", WmvTerrainDebug.ViewName));
            d.Root = UnityFacts(tile.Root, null);
            int read = 0;
            for (int n = 0; n < d.Chunks.Length; n++)
            {
                TerrainDumpChunk c = d.Chunks[n];
                Mesh m = tile.ChunkMeshes[n];
                GameObject go = tile.ChunkObjects[n];
                if (m == null || go == null)
                {
                    c.Positions = null;
                    c.Normals = null;
                    c.Indices = null;
                    c.BoundsMin = null;
                    c.BoundsMax = null;
                    continue;
                }
                Vector3[] v = m.vertices, nrm = m.normals;
                int[] idx = m.GetIndices(0);
                c.Positions = Flatten(v);
                c.Normals = Flatten(nrm);
                c.Indices = idx;
                // The referenced-vertex bounds, from what the mesh holds.
                var mn = new[] { float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity };
                var mx = new[] { float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity };
                foreach (int i in idx)
                    for (int a = 0; a < 3; a++)
                    {
                        float x = v[i][a];
                        if (x < mn[a]) mn[a] = x;
                        if (x > mx[a]) mx[a] = x;
                    }
                c.BoundsMin = idx.Length > 0 ? mn : null;
                c.BoundsMax = idx.Length > 0 ? mx : null;
                c.Unity = UnityFacts(go, m);
                read++;
            }
            using (var w = new System.IO.StreamWriter(path, false, new System.Text.UTF8Encoding(false)))
                TerrainDump.Write(d, w);
            Debug.Log(string.Format("WMV: [terrain] dump {0}: {1} chunk mesh(es) read back from Unity, written in {2:F1} ms", path, read,
                                    clock.Elapsed.TotalMilliseconds));
        }
        catch (System.Exception e)
        {
            Debug.LogWarning("WMV: [terrain] dump " + path + " failed: " + e.GetType().Name + ": " + e.Message);
            throw;
        }
    }

    static TerrainDumpUnity UnityFacts(GameObject go, Mesh m)
    {
        var u = new TerrainDumpUnity { Layer = go.layer, ActiveInHierarchy = go.activeInHierarchy, LocalToWorld = RowMajor(go.transform.localToWorldMatrix) };
        if (m != null)
        {
            u.SubMeshCount = m.subMeshCount;
            u.Topology = m.GetTopology(0).ToString();
            u.IndexFormat = m.indexFormat.ToString();
            Bounds b = m.bounds;
            u.MeshBoundsMin = new[] { b.min.x, b.min.y, b.min.z };
            u.MeshBoundsMax = new[] { b.max.x, b.max.y, b.max.z };
        }
        return u;
    }

    static float[] RowMajor(Matrix4x4 m)
    {
        var a = new float[16];
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                a[r * 4 + c] = m[r, c];
        return a;
    }

    static float[] Flatten(Vector3[] v)
    {
        var a = new float[v.Length * 3];
        for (int i = 0; i < v.Length; i++)
        {
            a[3 * i] = v[i].x;
            a[3 * i + 1] = v[i].y;
            a[3 * i + 2] = v[i].z;
        }
        return a;
    }

    // ---------------------------------------------------------------- the session's Unity side

    sealed class MainTerrainView : IWmvTerrainView
    {
        readonly WmvMain main;

        public MainTerrainView(WmvMain main) { this.main = main; }

        public void BuildTile(WmvRuntimeTerrainTile tile, TerrainTileMesh mesh)
        {
            var o = new WmvTerrainBuildOptions
            {
                View = WmvTerrainDebug.View, OnlyChunk = WmvTerrainDebug.OnlyChunk, KeepReadable = WmvTerrainDebug.DumpName != null,
            };
            WmvTerrainBuilder.Build(tile, mesh, main.TerrainSceneRoot(), o, s => Debug.Log("WMV: [terrain] " + s));
        }

        public void ShowTile(WmvRuntimeTerrainTile tile)
        {
            tile.Root.SetActive(true);
            main.SetViewMode(true, "tile " + tile.TileX + "_" + tile.TileY + " of map " + tile.MapId + ", load " + tile.Load);
            main.FrameTerrainTile(tile);
        }

        public void LeaveTerrainView(string why, bool forModelLoad) { main.SetViewMode(false, why, forModelLoad); }

        public void DisposeTile(WmvRuntimeTerrainTile tile) { tile.Dispose(); }

        public void Sample(WmvIpcClient.TerrainTileReport report) { WmvTerrainBuilder.SampleCounters(report); }

        public void TileBuilt(WmvRuntimeTerrainTile tile, TerrainTileMesh mesh, WmvIpcClient.TerrainTileCommand command,
                              int rootFileDataID)
        {
            main.TerrainTileBuilt(tile, mesh, command, rootFileDataID);
        }
    }
}

/// <summary>
/// The main camera in and out of the terrain view (stage 03 design 5.6): on the way in, the orbit camera's whole state
/// (WmvOrbitCamera.Snapshot), the capture's framing record, the projection and whether the orbit takes input are kept and
/// the mask becomes TerrainViewMask; on the way out all of it is put back exactly and the mask is ModelViewMask again.
/// The top-down orthographic camera of design 2.3b is applied here too. A plain class, so the self-test drives it with
/// a camera of its own.
/// </summary>
public sealed class WmvTerrainViewMode
{
    /// <summary>The capture's framing record (WmvMain.lastFramed, lastFramedAspect, haveLastFramed, lastFramedKept).</summary>
    public struct Framing
    {
        public Bounds Box;
        public float Aspect;
        public bool Have, Kept;
    }

    readonly WmvOrbitCamera orbit;
    readonly Camera cam;
    bool active, topDown;
    WmvOrbitCamera.State saved;
    Framing savedFraming;
    bool savedOrthographic, savedOrbitEnabled;
    float savedOrthographicSize;

    public WmvTerrainViewMode(WmvOrbitCamera orbit, Camera cam)
    {
        this.orbit = orbit;
        this.cam = cam;
    }

    public bool Active { get { return active; } }
    public bool TopDown { get { return topDown; } }
    /// <summary>The model view's orbit state, kept while the terrain view is on.</summary>
    public WmvOrbitCamera.State Saved { get { return saved; } }

    /// <summary>Into the terrain view: keep everything, then TerrainViewMask. False (nothing done) when already in it.</summary>
    public bool Enter(Framing current)
    {
        if (active)
            return false;
        saved = orbit.Snapshot();
        savedFraming = current;
        savedOrthographic = cam.orthographic;
        savedOrthographicSize = cam.orthographicSize;
        savedOrbitEnabled = orbit.enabled;
        cam.cullingMask = WmvTerrainBuilder.TerrainViewMask;
        active = true;
        topDown = false;
        return true;
    }

    /// <summary>Out of the terrain view: everything kept is put back, then ModelViewMask; `restored` is the framing record
    /// to put back. False (nothing done) when not in it -- true means the next load owes a framing.</summary>
    public bool Leave(out Framing restored)
    {
        restored = new Framing();
        if (!active)
            return false;
        active = false;
        topDown = false;
        cam.orthographic = savedOrthographic;
        cam.orthographicSize = savedOrthographicSize;
        orbit.enabled = savedOrbitEnabled;
        orbit.Restore(saved);
        cam.cullingMask = WmvTerrainBuilder.ModelViewMask;
        restored = savedFraming;
        return true;
    }

    /// <summary>
    /// The top-down orthographic camera of design 2.3b: rotation Euler(90, 0, 0), so screen up is Unity +Z (north) and
    /// screen right Unity +X (east); orthographic half-size T/2 centred on the tile's footprint (Unity x 0..T, z -T..0,
    /// exactly the tile in a square viewport); the planes bracket the tile's heights [minY, maxY]. The orbit stops taking
    /// input while it holds (its Update would put the perspective orbit back); Leave restores both.
    /// </summary>
    public void ApplyTopDown(float minY, float maxY)
    {
        if (!active)
            return;
        float half = (float)(TerrainCoordinates.TileSize * 0.5);
        orbit.enabled = false;
        cam.orthographic = true;
        cam.orthographicSize = half;
        cam.transform.rotation = Quaternion.Euler(90f, 0f, 0f);
        cam.transform.position = new Vector3(half, maxY + 1f, -half);
        cam.nearClipPlane = 0.5f;
        cam.farClipPlane = (maxY - minY) + 2f;
        topDown = true;
    }
}
