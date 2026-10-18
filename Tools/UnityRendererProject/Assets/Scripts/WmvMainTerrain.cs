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
//   inspectMapTiles  -> (protocol 14, the Maps viewer) WmvMapTiles.Inspect: the WDT the host read, its tile table by the
//                       verified parser, answered mapTilesInspected; nothing of the terrain session is touched
//   setTerrainView   -> (protocol 14) the view every tile is drawn in from now on, and the tile on screen built again in
//                       it from what the player holds (WmvTerrainSession.RebuildShown; no framing), answered
//                       terrainViewApplied
//   loadWorld        -> (protocol 16) a world: the tile and its world models, or a WMO-only map's world model with no tile
//                       (WmvTerrainSession, WmvTerrainSessionWorld.cs); the world models built and placed by WmvWorldModels
//                       through MainWorldView, on the terrain layer under the same scene root, culled per group every frame;
//                       (protocol 17) and their doodads, built and placed by WmvWorldDoodads, culled per instance every frame
//   inspectWorldModel-> (protocol 16) a WMO-only map's footprint (WmvMapTiles.InspectWorldModel), answered at once
//   setWorldCamera   -> (protocol 15) the world view's camera: "fly" puts the free camera (WmvFlyCamera) at a position of
//                       the game's world frame -- on the ground under it (TerrainHeight on the shown tile's own mesh) plus
//                       an eye height when no z is given -- converted with the tile's floating origin; "overview" frames
//                       the tile as before. Answered worldCameraApplied. A loadTerrainTile may carry the same camera: it
//                       is placed when that load's tile goes on screen, after its framing, in the same frame, so the
//                       overview is never seen first.
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
// settle, ViewportBusy counting the terrain load); the log line names the tile, its view and the camera, and says what
// the picture is (WhatTheViewShows, the status line's words too), from what was DRAWN, not from the view asked for: in
// the stage 03 views a geometry diagnostic; in the textured views the stage 04 texture composite (no lighting, shadow
// map, water or objects on its chunks; texturedlit under the diagnostic light) with the counts of chunks drawn textured
// and -- when any -- of chunks drawn NEUTRAL in the geometry diagnostic's shading; a geometry diagnostic when no chunk
// was drawn textured (none drawable, the chunk -wmvTerrainChunk shows not drawn textured, or the textured shader
// missing); in the materials view the chunk states; and
// "VALIDATION sampling" when -wmvTerrainValidate is set. The host refuses the IPC Save-As capture while only terrain is
// shown, so that path is not reached in the terrain view.
//
// THE VIEW (stage 04): -wmvTerrainView picks one; without it a tile whose material is textured or partial is drawn
// textured and any other in the stage 03 shaded view (WmvTerrainDebug.EffectiveView). -wmvTerrainValidate sets the
// textured shader's validation sampling for every frame.
//
// -wmvTerrainDump=<bare name>: the meshes are kept readable and, when the load is reported built, read BACK from Unity
// (positions, normals, indices, sub-meshes, topology, index format, bounds, layers, activity, transforms) into
// <player folder>/<name>.txt in the TerrainDump format, for Tools/world-audit/compare_terrain_mesh.py. The stage 04
// fixtures -wmvTerrainCompositeDump, -wmvTerrainCompositeChunk and -wmvTerrainTexelDump are written then too
// (WmvTerrainComposite), before the capture.

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
    WmvFlyCamera fly;                     // the world view's free camera (protocol 15), on the main camera, off until asked for
    WmvIpcClient.WorldCameraRequest owedCamera;   // a loadTerrainTile's camera, placed when that load's tile is shown
    int owedCameraLoad;
    // Protocol 16: a tile's world load places its camera once its world models are settled (they do not exist at the tile's
    // commit, and the camera stands on their floors): from TerrainTileBuilt, not ShowTile.
    bool owedCameraAtBuilt;
    // The tile world load whose world models are not settled yet (0: none): a camera asked for meanwhile waits for them too
    // (it stands on their floors), as the load's own camera does.
    int settlingLoad;
    // Streaming (protocol 18): a fly request with no height over a tile the session had not loaded yet -- the camera went
    // there at its own height and stands on the ground once that tile is built, unless it has moved since.
    WmvIpcClient.WorldCameraRequest groundOwed;
    Vector3 groundOwedAt;
    float groundOwedYaw, groundOwedPitch;
    WmvWorldModels worldModels;           // the world scene's world models (protocol 16)
    WmvWorldDoodads worldDoodads;         // the world scene's doodads (protocol 17)
    // Protocol 16: the world view's frame times (unscaled, milliseconds), the last WorldFrameWindow frames since it was
    // last entered, for runtimeState's distribution.
    public const int WorldFrameWindow = 600;
    readonly float[] worldFrameMs = new float[WorldFrameWindow];
    int worldFrames, worldFrameNext;

    /// <summary>The ApplyViewportOrbitOverride tag of the terrain framing: the only one honoured in the terrain view.</summary>
    const string TerrainOverrideTag = "terrain: ";

    /// <summary>The terrain view is on: the main camera shows the terrain layer only.</summary>
    bool TerrainViewActive { get { return terrainViewMode != null && terrainViewMode.Active; } }

    /// <summary>Awake: the model view's mask on the main camera, the session, and the host's terrain messages.</summary>
    void SetUpTerrain(Camera cam)
    {
        cam.cullingMask = WmvTerrainBuilder.ModelViewMask;
        fly = cam.gameObject.GetComponent<WmvFlyCamera>();
        if (fly == null)
            fly = cam.gameObject.AddComponent<WmvFlyCamera>();
        fly.enabled = false;
        if (WmvInputTrace.Requested())
        {
            gameObject.AddComponent<WmvInputTrace>();
            WmvInputTrace.CameraLine = TraceCameraLine;
        }
        terrainViewMode = new WmvTerrainViewMode(orbit, cam) { Fly = fly };
        terrain = new WmvTerrainSession(new MainTerrainView(this), ipc.RequestMapAsset, ipc.ReportTerrainTileLoaded,
                                        s => Debug.Log("WMV: " + s),
                                        a => System.Threading.ThreadPool.QueueUserWorkItem(_ => a()), WmvTerrainDebug.TimeoutMs);
        worldModels = new WmvWorldModels(TerrainSceneRoot());
        worldDoodads = new WmvWorldDoodads(TerrainSceneRoot());
        terrain.WorldView = new MainWorldView(this);
        // Protocol 18: a streaming session's requests, its tile loads and its state, and where the free camera is.
        terrain.RequestStreamMapAsset = ipc.RequestStreamMapAsset;
        terrain.SendStreamTile = ipc.SendStreamTile;
        terrain.ReportStreamState = ipc.ReportStreamState;
        terrain.StreamCamera = StreamCameraPoint;
        ipc.OnStreamTileAnswer = a =>
        {
            if (terrain != null)
                terrain.OnStreamTileAnswer(a);
        };
        UnityEngine.Rendering.RenderPipelineManager.beginCameraRendering += CullWorldModels;
        WireTerrainMessages();
        foreach (string note in WmvTerrainDebug.Notes)
            Debug.LogWarning("WMV: [terrain] " + note);
        // Validation sampling for every frame (the dumps set it for their own renders either way).
        Shader.SetGlobalFloat(WmvTerrainMaterials.ValidateId, WmvTerrainDebug.Validate ? 1f : 0f);
        Shader.SetGlobalFloat(WmvTerrainMaterials.UnclampedId, 0f);
        Debug.Log("WMV: [terrain] ready: layer " + WmvTerrainBuilder.Layer + ", model view mask " + WmvTerrainBuilder.ModelViewMask +
                  ", terrain view mask " + WmvTerrainBuilder.TerrainViewMask + ", load timeout " + terrain.TimeoutMs + " ms, at most " +
                  WmvTerrainSession.MaxOutstanding + " map requests out per load, " + WmvTerrainSession.RetiredCapacity +
                  " ended requests remembered; block-compressed sampling " + WmvTerrainFormatSupport.Query() + ", terrain layer anisotropy " +
                  WmvTerrainMaterials.LayerAnisoLevel + "; " + WmvTerrainDebug.Describe());
    }

    /// <summary>The host's terrain messages: loadTerrainTile, clearTerrain, and every map file's answer.</summary>
    void WireTerrainMessages()
    {
        ipc.OnLoadTerrainTile = HandleLoadTerrainTile;
        ipc.OnClearTerrain = HandleClearTerrain;
        ipc.OnMapAssetResponse = HandleMapAssetResponse;
        ipc.OnInspectMapTiles = HandleInspectMapTiles;
        ipc.OnSetTerrainView = HandleSetTerrainView;
        ipc.OnSetWorldCamera = HandleSetWorldCamera;
        ipc.OnInspectWorldModel = HandleInspectWorldModel;
        ipc.OnTestFlight = HandleTestFlight;
    }

    // ---------------------------------------------------------------- the test flight (protocol 18, the harnesses only)

    WmvIpcClient.TestFlight testFlight;
    double testFlightStartS, testFlightLength, testFlightLastS, testFlightFrameMaxMs;
    int testFlightFrames;

    /// <summary>testFlight: the free camera flies the path itself, frame by frame, as a user's keys would move it (the viewer
    /// sends nothing on the way); answered testFlightDone at its end, or refused, or stopped (a new flight, the world view
    /// left).</summary>
    void HandleTestFlight(WmvIpcClient.TestFlight f)
    {
        string why = null;
        if (f == null || f.Points == null || f.Points.Length < 6 || f.Points.Length % 3 != 0)
            why = "the path needs two points or more (x, y and z each)";
        else if (!(f.Speed > 0))
            why = "the speed must be above 0";
        else if (!TerrainViewActive || terrain == null || terrain.World == null)
            why = "no world is on screen";
        if (why != null)
        {
            Debug.Log("WMV: [terrain] test flight refused: " + why);
            ipc.ReportTestFlightDone(f != null ? f.Request : 0, "refused", why, 0, 0, 0, 0);
            return;
        }
        if (testFlight != null)
            FinishTestFlight("stopped", "a new flight began");
        testFlight = f;
        testFlightLength = 0;
        for (int k = 3; k + 2 < f.Points.Length; k += 3)
            testFlightLength += Distance3(f.Points, k - 3, k);
        testFlightStartS = testFlightLastS = WmvIpcClient.NowSeconds;
        testFlightFrames = 0;
        testFlightFrameMaxMs = 0;
        terrainViewMode.ApplyFly(UnityOfWorld(f.Points[0], f.Points[1], f.Points[2]), YawAlong(f.Points, 0), f.Pitch);
        Debug.Log(string.Format(System.Globalization.CultureInfo.InvariantCulture, "WMV: [terrain] test flight {0}: {1} point(s), {2:0.#} yards at {3:0.#} " +
                                "yards a second, pitch {4:0.#}", f.Request, f.Points.Length / 3, testFlightLength, f.Speed, f.Pitch));
    }

    static double Distance3(double[] p, int a, int b)
    {
        double dx = p[b] - p[a], dy = p[b + 1] - p[a + 1], dz = p[b + 2] - p[a + 2];
        return System.Math.Sqrt(dx * dx + dy * dy + dz * dz);
    }

    /// <summary>The free camera's yaw along the segment from point i (the world's (dx, dy) is Unity's (-dy, dx) in x and z;
    /// yaw 0 faces Unity +z).</summary>
    static float YawAlong(double[] p, int i)
    {
        int a = 3 * i, b = a + 3;
        if (b + 2 >= p.Length)
        {
            a -= 3;
            b -= 3;
        }
        double ux = -(p[b + 1] - p[a + 1]), uz = p[b] - p[a];
        return (float)(System.Math.Atan2(ux, uz) * 180.0 / System.Math.PI);
    }

    Vector3 UnityOfWorld(double x, double y, double z)
    {
        double ux, uy, uz;
        WorldToUnity.PositionD(x, y, z, terrain.World.Origin, out ux, out uy, out uz);
        return TerrainSceneRoot().position + new Vector3((float)ux, (float)uy, (float)uz);
    }

    /// <summary>One frame of the test flight (PumpTerrain, before the session's frame: the region sees where the camera is now).</summary>
    void PumpTestFlight()
    {
        WmvIpcClient.TestFlight f = testFlight;
        if (f == null)
            return;
        if (!TerrainViewActive || terrain == null || terrain.World == null || !terrainViewMode.Flying)
        {
            FinishTestFlight("stopped", "the world view or the free camera was left");
            return;
        }
        double now = WmvIpcClient.NowSeconds;
        testFlightFrameMaxMs = System.Math.Max(testFlightFrameMaxMs, (now - testFlightLastS) * 1000.0);
        testFlightLastS = now;
        testFlightFrames++;
        double along = (now - testFlightStartS) * f.Speed;
        int seg = 0;
        double from = 0;
        for (; 3 * seg + 5 < f.Points.Length; seg++)
        {
            double len = Distance3(f.Points, 3 * seg, 3 * seg + 3);
            if (along <= from + len)
                break;
            from += len;
        }
        if (3 * seg + 5 >= f.Points.Length)
        {
            int last = f.Points.Length - 3;
            fly.Place(UnityOfWorld(f.Points[last], f.Points[last + 1], f.Points[last + 2]), YawAlong(f.Points, f.Points.Length / 3 - 2), f.Pitch);
            FinishTestFlight("done", "");
            return;
        }
        double segLen = Distance3(f.Points, 3 * seg, 3 * seg + 3);
        double t = segLen > 0 ? (along - from) / segLen : 1.0;
        int a = 3 * seg, b = a + 3;
        fly.Place(UnityOfWorld(f.Points[a] + (f.Points[b] - f.Points[a]) * t, f.Points[a + 1] + (f.Points[b + 1] - f.Points[a + 1]) * t,
                               f.Points[a + 2] + (f.Points[b + 2] - f.Points[a + 2]) * t), YawAlong(f.Points, seg), f.Pitch);
    }

    void FinishTestFlight(string status, string reason)
    {
        WmvIpcClient.TestFlight f = testFlight;
        testFlight = null;
        if (f == null)
            return;
        double seconds = WmvIpcClient.NowSeconds - testFlightStartS;
        Debug.Log(string.Format(System.Globalization.CultureInfo.InvariantCulture, "WMV: [terrain] test flight {0} {1}{2} after {3:0.###} s, {4} frame(s), " +
                                "the longest {5:0.###} ms", f.Request, status, string.IsNullOrEmpty(reason) ? "" : " (" + reason + ")", seconds,
                                testFlightFrames, testFlightFrameMaxMs));
        ipc.ReportTestFlightDone(f.Request, status, reason, seconds, testFlightFrames, testFlightFrameMaxMs, testFlightLength);
    }

    /// <summary>inspectWorldModel: a WMO-only map's footprint (WmvMapTiles.InspectWorldModel), answered at once.</summary>
    void HandleInspectWorldModel(WmvIpcClient.WorldModelRequest request)
    {
        WmvIpcClient.WorldModelInspectReport r = WmvMapTiles.InspectWorldModel(request);
        Debug.Log(string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                "WMV: [terrain] inspectWorldModel {0}: map {1}, WMO {2} ({3} bytes): {4}{5}; {6} group(s) ({7} interior); root box " +
                                "({8:0.##}, {9:0.##}, {10:0.##})..({11:0.##}, {12:0.##}, {13:0.##}); {14:0.###} ms", r.Inspection, r.MapId, r.WmoFileDataID,
                                r.Bytes, r.Status, string.IsNullOrEmpty(r.Reason) ? "" : " (" + r.Reason + ")", r.Groups, r.InteriorGroups,
                                r.RootBox[0], r.RootBox[1], r.RootBox[2], r.RootBox[3], r.RootBox[4], r.RootBox[5], r.ParseMs));
        ipc.ReportWorldModelInspected(r);
    }

    /// <summary>setWorldCamera: the camera placed or switched now, answered. The user's latest choice wins: a camera a load
    /// in flight still carries is not placed when that load's tile comes.</summary>
    void HandleSetWorldCamera(WmvIpcClient.WorldCameraRequest request)
    {
        // A tile's world models still coming (the tile on screen, its load not reported): the camera waits for them, as the
        // load's own did -- placed now it would stand on the terrain under a floor not built yet.
        if (settlingLoad != 0 && terrain != null && terrain.Loading && terrain.ShownLoad == settlingLoad && request != null && request.Mode == "fly")
        {
            owedCamera = request;
            owedCameraLoad = settlingLoad;
            Debug.Log("WMV: [terrain] world camera " + request + " waits for the world models of load " + settlingLoad);
            return;
        }
        owedCamera = null;
        WmvIpcClient.WorldCameraReport r = PlaceWorldCamera(request);
        ipc.ReportWorldCameraApplied(r);
    }

    /// <summary>
    /// The world view's camera as asked: "fly" at a world position (the free camera), or "overview" (the tile framed as
    /// Stage 03 frames it). Refused, and said, when no tile is on screen or the mode is not one of the two. The position
    /// is the game's world frame; the tile's mesh origin and root offset turn it into Unity space exactly as its vertices
    /// were (WorldToUnity), so the same position stands at the same place whatever the scene's origin is.
    /// </summary>
    WmvIpcClient.WorldCameraReport PlaceWorldCamera(WmvIpcClient.WorldCameraRequest q)
    {
        var r = new WmvIpcClient.WorldCameraReport { Request = q != null ? q.Request : 0, Mode = q != null ? q.Mode ?? "" : "" };
        WmvRuntimeTerrainTile tile = terrain != null ? terrain.ShownTile : null;
        TerrainTileMesh mesh = terrain != null ? terrain.ShownMesh : null;
        WmvWorldScene scene = terrain != null ? terrain.World : null;
        // Streaming (protocol 18): the point stands on whichever resident tile of the session is under it.
        if (q != null && q.Mode == "fly" && terrain != null && terrain.Streaming)
        {
            WmvRuntimeTerrainTile at;
            TerrainTileMesh atMesh;
            if (terrain.ResidentTileAt(q.X, q.Y, out at, out atMesh))
            {
                tile = at;
                mesh = atMesh;
            }
            else if (scene != null && TerrainViewActive)
            {
                // A tile the session has not loaded yet: the point stands where asked, in the scene's frame -- at its height, or
                // with none at the free camera's present height, stood on the ground once the tile is built (PumpGroundOwed).
                double z = q.Z;
                double fx, fy, fz;
                if (!q.HasZ)
                    z = FlyWorldPosition(out fx, out fy, out fz) ? fz : scene.Origin.Z;
                terrainViewMode.ApplyFly(UnityOfWorld(q.X, q.Y, z), q.Yaw, q.Pitch);
                groundOwed = q.HasZ ? null : q;
                groundOwedAt = fly.transform.position;
                groundOwedYaw = fly.Current.Yaw;
                groundOwedPitch = fly.Current.Pitch;
                r.Status = "applied";
                r.Reason = q.HasZ ? "at the height asked for, over a tile the streaming session has not loaded yet"
                                  : "over a tile the streaming session has not loaded yet: at the camera's height until that tile is built, " +
                                    "then on its ground";
                r.X = q.X;
                r.Y = q.Y;
                r.Z = z;
                r.Yaw = fly.Current.Yaw;
                r.Pitch = fly.Current.Pitch;
                Debug.Log("WMV: [terrain] world camera " + q + ": applied -- " + r.Reason);
                return r;
            }
        }
        r.Load = tile != null ? tile.Load : 0;
        if (tile == null && scene != null && scene.Kind == "globalWmo")
            return PlaceWorldModelCamera(q, r, scene);
        if (q == null || (q.Mode != "fly" && q.Mode != "overview"))
            r.Reason = "\"" + r.Mode + "\" is not a camera mode (fly, overview)";
        else if (tile == null || tile.Root == null || !TerrainViewActive)
            r.Reason = "no world is on screen";
        else if (q.Mode == "overview")
        {
            FrameTerrainTile(tile);
            r.Status = "applied";
            r.Reason = "the tile framed";
        }
        else if (mesh == null)
            r.Reason = "the tile on screen keeps no mesh data to stand the camera on";
        else if (!OnTile(q.X, q.Y, tile.TileX, tile.TileY))
            r.Reason = string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                     "({0:0.###}, {1:0.###}) is not on the tile on screen ({2}_{3})", q.X, q.Y, tile.TileX, tile.TileY);
        else
        {
            double eye = q.EyeHeight > 0f ? q.EyeHeight : 2.0;
            r.GroundZ = TerrainHeight.At(mesh, q.X, q.Y);
            double z, modelZ, modelFloor;
            string modelWhy;
            if (q.HasZ)
            {
                z = q.Z;
                r.Reason = "at the height asked for";
            }
            else if (WorldModelStand(q.X, q.Y, eye, r.GroundZ, out modelZ, out modelFloor, out modelWhy))
            {
                // Protocol 16: a world model's floor at or above the terrain's ground there (a street, a room) is where the point
                // stands, never the terrain under it.
                z = modelZ;
                r.Ground = true;
                r.Reason = modelWhy + (double.IsNaN(r.GroundZ) ? "" : string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                                                                      "; the terrain's ground there {0:0.##}", r.GroundZ));
                r.GroundZ = modelFloor;
            }
            else if (!double.IsNaN(r.GroundZ))
            {
                z = r.GroundZ + eye;
                r.Ground = true;
                r.Reason = string.Format(System.Globalization.CultureInfo.InvariantCulture, "on the ground ({0:0.##}) plus {1:0.##}", r.GroundZ, eye);
            }
            else
            {
                // No triangle under the point (a hole, or off the tile): above the tile's highest point instead, and said.
                double top = mesh.BoundsMax != null ? mesh.BoundsMax[1] + mesh.Origin.Z : mesh.Origin.Z;
                z = top + eye;
                r.Reason = "no ground under the point (a hole, or outside the tile's triangles): placed above the tile's highest point";
            }
            double ux, uy, uz;
            WorldToUnity.PositionD(q.X, q.Y, z, mesh.Origin, out ux, out uy, out uz);
            Vector3 position = tile.Root.transform.position + new Vector3((float)ux, (float)uy, (float)uz);
            terrainViewMode.ApplyFly(position, q.Yaw, q.Pitch);
            r.Status = "applied";
            r.X = q.X;
            r.Y = q.Y;
            r.Z = z;
            r.Yaw = fly.Current.Yaw;
            r.Pitch = fly.Current.Pitch;
        }
        Debug.Log(string.Format(System.Globalization.CultureInfo.InvariantCulture, "WMV: [terrain] world camera {0}: {1}{2}{3}",
                                q != null ? q.ToString() : "(none)", r.Status, string.IsNullOrEmpty(r.Reason) ? "" : " -- " + r.Reason,
                                r.Status == "applied" && r.Mode == "fly" ? string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                    "; at ({0:0.###}, {1:0.###}, {2:0.###}), Unity {3}", r.X, r.Y, r.Z, fly.transform.position) : ""));
        return r;
    }

    /// <summary>
    /// The world models' floor to stand on at (x, y) in a tile's scene (protocol 16): over every world model of the scene, its
    /// lowest floor at or above the terrain's ground there less half a yard (WorldModelGeometry.GroundFloor: the street, or a
    /// building's ground floor -- not a cellar under the ground, not a tower room or a roof), the lowest of them; z is it plus
    /// the eye height. False when no world model has a floor there (the terrain's ground then).
    /// </summary>
    bool WorldModelStand(double x, double y, double eye, double ground, out double z, out double floor, out string reason)
    {
        z = floor = double.NaN;
        reason = "";
        WmvWorldScene scene = terrain != null ? terrain.World : null;
        if (scene == null || scene.Geometries.Count == 0)
            return false;
        double minFloor = double.IsNaN(ground) ? double.NegativeInfinity : ground - 0.5;
        bool found = false;
        foreach (WorldModelGeometry g in scene.Geometries)
        {
            double sz, sf;
            bool covered;
            string why;
            if (!g.GroundFloor(x, y, eye, minFloor, out sz, out sf, out covered, out why))
                continue;
            if (!found || sf < floor)
            {
                found = true;
                floor = sf;
                z = sz;
                reason = "world model " + g.Placement.FileDataID + " (uniqueId " + g.Placement.UniqueId + "): " + why;
            }
        }
        if (!found)
            return false;
        // The world models together, as GroundFloor does within one: another model's floor layered over the one chosen (a
        // platform on a street) is climbed onto, and the nearest surface above of any model limits the eye.
        double stand = floor;
        for (int k = 0; k < 16; k++)
        {
            double next = double.PositiveInfinity;
            foreach (WorldModelGeometry g in scene.Geometries)
            {
                double f;
                if (g.FloorAbove(x, y, stand + 0.05, out f) && f < next)
                    next = f;
            }
            if (double.IsInfinity(next) || next - stand > eye + 0.5)
                break;
            stand = next;
        }
        double ceiling = double.PositiveInfinity;
        foreach (WorldModelGeometry g in scene.Geometries)
        {
            double c;
            if (g.CeilingAbove(x, y, stand + 0.05, out c) && c < ceiling)
                ceiling = c;
        }
        if (stand != floor)
            reason += string.Format(System.Globalization.CultureInfo.InvariantCulture, "; on another world model's floor over it, {0:0.##}", stand);
        floor = stand;
        z = stand + (double.IsInfinity(ceiling) ? eye : System.Math.Min(eye, System.Math.Max(0.5, ceiling - stand - 0.5)));
        return true;
    }

    /// <summary>
    /// The camera in a WMO-only map's scene (protocol 16; no tile): "overview" frames the world model; "fly" with Start puts
    /// the free camera at the world model's start point (its own geometry: WorldModelGeometry.StartPoint); "fly" at (X, Y)
    /// refuses a point outside the scene's box and stands on the highest covered floor under it (WorldModelGeometry.StandOn)
    /// plus the eye height, or at the Z asked for. Converted with the scene's floating origin, as its instances were.
    /// </summary>
    WmvIpcClient.WorldCameraReport PlaceWorldModelCamera(WmvIpcClient.WorldCameraRequest q, WmvIpcClient.WorldCameraReport r, WmvWorldScene scene)
    {
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        r.Load = scene.Load;
        double x = 0, y = 0, z = 0;
        float yaw = q != null ? q.Yaw : 0f, pitch = q != null ? q.Pitch : 0f;
        bool place = false;
        if (q == null || (q.Mode != "fly" && q.Mode != "overview"))
            r.Reason = "\"" + r.Mode + "\" is not a camera mode (fly, overview)";
        else if (!TerrainViewActive)
            r.Reason = "no world is on screen";
        else if (q.Mode == "overview")
        {
            FrameWorldScene(scene);
            r.Status = "applied";
            r.Reason = "the world model framed";
        }
        else if (q.Start)
        {
            WorldPoint start;
            double startYaw;
            string why;
            if (terrain.WorldStart(out start, out startYaw, out why))
            {
                x = start.X;
                y = start.Y;
                z = start.Z;
                yaw = (float)startYaw;   // along the start room's longer side, level
                pitch = 0f;
                r.Reason = "at the world model's start point: " + why;
                place = true;
            }
            else
                r.Reason = "the world model's geometry offers no start point: " + why;
        }
        else if (!scene.Contains(q.X, q.Y))
            r.Reason = string.Format(inv, "({0:0.###}, {1:0.###}) is not inside the world model's box (({2:0.##}, {3:0.##})..({4:0.##}, {5:0.##}))",
                                     q.X, q.Y, scene.BoxMin.X, scene.BoxMin.Y, scene.BoxMax.X, scene.BoxMax.Y);
        else
        {
            x = q.X;
            y = q.Y;
            double eye = q.EyeHeight > 0f ? q.EyeHeight : 2.0;
            if (q.HasZ)
            {
                z = q.Z;
                r.Reason = "at the height asked for";
                place = true;
            }
            else
            {
                // The highest covered floor under the point, over every world model of the scene.
                bool found = false;
                double bestFloor = double.NegativeInfinity, bestZ = 0;
                string bestWhy = "no floor of the world model under the point";
                foreach (WorldModelGeometry g in scene.Geometries)
                {
                    double sz, sf;
                    string why;
                    if (g.StandOn(x, y, eye, out sz, out sf, out why) && sf > bestFloor)
                    {
                        found = true;
                        bestFloor = sf;
                        bestZ = sz;
                        bestWhy = why;
                    }
                }
                if (found)
                {
                    z = bestZ;
                    r.Ground = true;
                    r.GroundZ = bestFloor;
                    r.Reason = bestWhy;
                    place = true;
                }
                else
                    r.Reason = bestWhy;
            }
        }
        if (place)
        {
            double ux, uy, uz;
            WorldToUnity.PositionD(x, y, z, scene.Origin, out ux, out uy, out uz);
            Vector3 position = TerrainSceneRoot().position + new Vector3((float)ux, (float)uy, (float)uz);
            terrainViewMode.ApplyFly(position, yaw, pitch);
            r.Status = "applied";
            r.X = x;
            r.Y = y;
            r.Z = z;
            r.Yaw = fly.Current.Yaw;
            r.Pitch = fly.Current.Pitch;
        }
        Debug.Log(string.Format(inv, "WMV: [terrain] world camera {0} (world model of load {1}): {2}{3}{4}", q != null ? q.ToString() : "(none)",
                                scene.Load, r.Status, string.IsNullOrEmpty(r.Reason) ? "" : " -- " + r.Reason,
                                r.Status == "applied" && r.Mode == "fly" ? string.Format(inv, "; at ({0:0.###}, {1:0.###}, {2:0.###}), Unity {3}", r.X, r.Y,
                                                                                         r.Z, fly.transform.position) : ""));
        return r;
    }

    /// <summary>The world view frames a scene with no tile: its instances' box, as a tile is framed (FrameMapObject).</summary>
    void FrameWorldScene(WmvWorldScene scene)
    {
        terrainViewMode.StopFly();
        bool have = false;
        var frame = new Bounds();
        foreach (WmvWorldInstance i in scene.Instances)
        {
            if (!i.HasBounds) continue;
            if (!have) { frame = i.Bounds; have = true; }
            else frame.Encapsulate(i.Bounds);
        }
        if (!have)
            frame = new Bounds(TerrainSceneRoot().position, Vector3.one * 10f);
        terrainFrame = frame;
        haveTerrainFrame = true;
        orbit.FrameMapObject(frame);
        Camera cam = Camera.main;
        lastFramed = frame;
        lastFramedAspect = cam != null ? cam.aspect : 1f;
        haveLastFramed = true;
        lastFramedKept = false;
        ApplyViewportOrbitOverride(TerrainOverrideTag);
        Debug.Log(string.Format(System.Globalization.CultureInfo.InvariantCulture,
            "WMV: [terrain] framed the world model of load {0} on centre {1} radius {2:F1}: distance {3:F1}, yaw {4} pitch {5}, clip planes {6:F3} .. {7:F1}",
            scene.Load, frame.center, frame.extents.magnitude, orbit.distance, orbit.yaw, orbit.pitch, cam != null ? cam.nearClipPlane : 0f,
            cam != null ? cam.farClipPlane : 0f));
    }

    /// <summary>The free camera for the input trace (WmvInputTrace): "" while it is off; else its state (the keys, the input
    /// it read), "|", and where it is -- in the game's world frame when a tile is on screen.</summary>
    string TraceCameraLine()
    {
        if (fly == null || !fly.enabled)
            return "";
        var ci = System.Globalization.CultureInfo.InvariantCulture;
        WmvFlyCamera.FlyInput k = fly.LastKeys;
        string state = string.Format(ci, "free camera; the keyboard {0}; keys held {1}, counted {2}; input forward {3} right {4} up {5} speed x{6}",
                                     fly.HasKeyboard ? "is the player's" : "is not the player's", WmvFlyCamera.KeyNames(fly.HeldKeys),
                                     WmvFlyCamera.KeyNames(fly.CountedKeys), k.Forward, k.Right, k.Up, k.SpeedFactor);
        double x, y, z;
        string where = FlyWorldPosition(out x, out y, out z) ? string.Format(ci, "world ({0:0.00}, {1:0.00}, {2:0.00})", x, y, z)
                                                             : "Unity " + fly.transform.position.ToString("F2");
        return state + "|" + where + string.Format(ci, " yaw {0:0.0} pitch {1:0.0}", fly.Current.Yaw, fly.Current.Pitch);
    }

    /// <summary>The free camera's position in the game's world frame: the inverse of WorldToUnity on the shown tile's mesh
    /// origin, relative to its root ((ux, uy, uz) = (-(y - o.Y), z - o.Z, x - o.X)).</summary>
    bool FlyWorldPosition(out double x, out double y, out double z)
    {
        x = y = z = 0.0;
        WmvRuntimeTerrainTile tile = terrain != null ? terrain.ShownTile : null;
        TerrainTileMesh mesh = terrain != null ? terrain.ShownMesh : null;
        WmvWorldScene scene = terrain != null ? terrain.World : null;
        // (A streaming session: every tile is built against the scene's origin, so the scene root answers whether or not a
        // tile is shown -- every built tile may have closed behind a camera that flew on.)
        if (((tile == null || tile.Root == null || mesh == null) && scene != null && scene.Kind == "globalWmo") ||
            (scene != null && terrain.Streaming))
        {
            // A scene with no tile: relative to the scene root, with the scene's origin.
            Vector3 v = fly.transform.position - TerrainSceneRoot().position;
            x = v.z + scene.Origin.X;
            y = scene.Origin.Y - v.x;
            z = v.y + scene.Origin.Z;
            return true;
        }
        if (tile == null || tile.Root == null || mesh == null)
            return false;
        Vector3 u = fly.transform.position - tile.Root.transform.position;
        x = u.z + mesh.Origin.X;
        y = mesh.Origin.Y - u.x;
        z = u.y + mesh.Origin.Z;
        return true;
    }

    /// <summary>Streaming (protocol 18): where the free camera is in the game's world frame and which way it looks (x, y, z,
    /// then the forward x, y), or null when it is not flying (the session then keeps the tiles around its entry's).</summary>
    double[] StreamCameraPoint()
    {
        if (!TerrainViewActive || fly == null || !terrainViewMode.Flying)
            return null;
        double x, y, z;
        if (!FlyWorldPosition(out x, out y, out z))
            return null;
        // Unity (ux, uy, uz) = (-(y - O.Y), z - O.Z, x - O.X): a world step (dx, dy) is Unity (-dy, dz, dx).
        Vector3 f = fly.transform.forward;
        return new[] { x, y, z, (double)f.z, (double)-f.x };
    }

    /// <summary>Is world point (x, y) on tile (tileX, tileY)? Its north-west corner and the tile size (TerrainLayout.cs).</summary>
    static bool OnTile(double x, double y, int tileX, int tileY)
    {
        WorldOrigin o = WorldToUnity.TileOrigin(tileX, tileY);
        double t = TerrainCoordinates.TileSize;
        return x <= o.X && x > o.X - t && y <= o.Y && y > o.Y - t;
    }

    /// <summary>The tile of `tile`'s load went on screen: the camera its loadTerrainTile carried, placed now (after the
    /// framing, in the same frame) and answered.</summary>
    void PlaceOwedCamera(WmvRuntimeTerrainTile tile)
    {
        if (tile == null)
            return;
        if (owedCameraAtBuilt && owedCamera != null && tile.Load == owedCameraLoad)
        {
            Debug.Log("WMV: [terrain] load " + tile.Load + ": its camera (" + owedCamera + ") waits for the world models, the tile framed meanwhile");
            return;
        }
        PlaceOwedCamera(tile.Load);
    }

    /// <summary>The camera a load carried, placed now that its scene is shown (a tile, or a WMO-only map's world model).</summary>
    void PlaceOwedCamera(int load)
    {
        if (owedCamera == null || load != owedCameraLoad)
            return;
        WmvIpcClient.WorldCameraRequest q = owedCamera;
        owedCamera = null;
        HandleSetWorldCamera(q);
    }

    /// <summary>inspectMapTiles: the tile table of the WDT the host read (WmvMapTiles), answered at once.</summary>
    void HandleInspectMapTiles(WmvIpcClient.MapTilesRequest request)
    {
        WmvIpcClient.MapTilesReport r = WmvMapTiles.Inspect(request);
        Debug.Log(string.Format("WMV: [terrain] inspectMapTiles {0}: map {1}, WDT {2} ({3} bytes): {4}{5}; {6} tile(s) in MAIN, {7} with a root " +
                                "ADT; {8:0.###} ms", r.Inspection, r.MapId, r.WdtFileDataID, r.Bytes, r.Status,
                                string.IsNullOrEmpty(r.Reason) ? "" : " (" + r.Reason + ")", r.TilesInMain, r.TilesWithRoot, r.ParseMs));
        ipc.ReportMapTilesInspected(r);
    }

    /// <summary>setTerrainView: the view from now on, and the tile on screen built again in it (no framing: the camera stays
    /// where the user left it).</summary>
    void HandleSetTerrainView(string name)
    {
        var r = new WmvIpcClient.TerrainViewReport { View = name ?? "" };
        if (!WmvTerrainDebug.SetRuntimeView(r.View))
        {
            r.Status = "refused";
            r.Reason = "\"" + r.View + "\" is not a view the host can choose (auto, or one of " +
                       string.Join("|", WmvTerrainDebug.ViewNames) + " but texturedplainweights)";
        }
        else if (terrain == null)
        {
            r.Status = "noTile";
            r.Reason = "no terrain session";
        }
        else
        {
            string reason;
            double ms;
            int load;
            r.Status = terrain.RebuildShown("setTerrainView " + r.View, out reason, out ms, out load);
            r.Reason = reason;
            r.RebuildMs = ms;
            r.Load = load;
            WmvRuntimeTerrainTile t = terrain.ShownTile;
            if (r.Status == "applied" && t != null)
            {
                int textured = 0, objects = 0;
                for (int n = 0; n < t.ChunkObjects.Length; n++)
                {
                    if (t.ChunkObjects[n] == null) continue;
                    objects++;
                    if (t.ChunkTextured[n]) textured++;
                }
                r.TexturedChunks = textured;
                r.NeutralChunks = objects - textured;
                r.Textures = t.Textures.Count;
                if (r.Reason.Length == 0)
                    r.Reason = "drawn " + WmvTerrainDebug.ViewNames[(int)t.View];
            }
            r.RetainedTextureBytes = terrain.RetainedTextureBytes;
        }
        Debug.Log(string.Format("WMV: [terrain] setTerrainView {0} (in effect: {1}): {2}{3}", r.View, WmvTerrainDebug.RuntimeViewName, r.Status,
                                string.IsNullOrEmpty(r.Reason) ? "" : " -- " + r.Reason));
        ipc.ReportTerrainViewApplied(r);
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
        status.Set(cmd.GlobalWmo ? string.Format("World model requested: map {0} (load {1})", cmd.MapId, cmd.Load)
                                 : string.Format("Terrain tile requested: map {0} tile {1}_{2} (load {3})", cmd.MapId, cmd.TileX, cmd.TileY, cmd.Load));
        DropModelWorkForTerrain("superseded by terrain load " + cmd.Load);
        // The latest load's camera only: an earlier load's, never placed, goes with that load.
        owedCamera = cmd.Camera;
        owedCameraLoad = cmd.Load;
        groundOwed = null;
        owedCameraAtBuilt = cmd.World && !cmd.GlobalWmo;
        settlingLoad = owedCameraAtBuilt ? cmd.Load : 0;
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

    // STREAMING (stage 08): a closed tile's objects -- its terrain's, its doodads' -- destroyed a few hundred a frame (at least
    // LaterDestroyMin, then until LaterDestroyBudgetMs), never thousands in one frame; all at once when the terrain goes.
    internal readonly List<Object> laterDestroy = new List<Object>();
    int laterDestroyNext;
    public const int LaterDestroyMin = 100;
    public const double LaterDestroyBudgetMs = 1.5;
    public static long LaterDestroyed, LaterDestroyFrames;

    void PumpLaterDestroy(bool all)
    {
        if (laterDestroyNext >= laterDestroy.Count)
            return;
        var clock = System.Diagnostics.Stopwatch.StartNew();
        int n = 0;
        while (laterDestroyNext < laterDestroy.Count && (all || n < LaterDestroyMin || clock.Elapsed.TotalMilliseconds < LaterDestroyBudgetMs))
        {
            Object o = laterDestroy[laterDestroyNext++];
            if (o != null)
                Destroy(o);
            n++;
        }
        LaterDestroyed += n;
        LaterDestroyFrames++;
        if (laterDestroyNext >= laterDestroy.Count)
        {
            laterDestroy.Clear();
            laterDestroyNext = 0;
        }
    }

    void PumpTerrain()
    {
        PumpLaterDestroy(false);
        PumpTestFlight();
        if (terrain != null)
            terrain.Pump(WmvIpcClient.NowSeconds * 1000.0);
        PumpGroundOwed();
        // The world view's frame times (the view off: none kept, the next entry starts over).
        if (TerrainViewActive)
        {
            worldFrameMs[worldFrameNext] = Time.unscaledDeltaTime * 1000f;
            worldFrameNext = (worldFrameNext + 1) % WorldFrameWindow;
            if (worldFrames < WorldFrameWindow) worldFrames++;
        }
        else
            worldFrames = worldFrameNext = 0;
    }

    /// <summary>Streaming: the camera a fly request with no height left over a tile not loaded then, stood on the ground once
    /// that tile is built (PlaceWorldCamera, answered already) -- dropped when the camera has moved or the view has gone.</summary>
    void PumpGroundOwed()
    {
        WmvIpcClient.WorldCameraRequest q = groundOwed;
        if (q == null)
            return;
        if (!TerrainViewActive || terrain == null || !terrain.Streaming || fly == null || !terrainViewMode.Flying ||
            (fly.transform.position - groundOwedAt).sqrMagnitude > 1e-4f || fly.Current.Yaw != groundOwedYaw || fly.Current.Pitch != groundOwedPitch)
        {
            groundOwed = null;
            return;
        }
        WmvRuntimeTerrainTile t;
        TerrainTileMesh m;
        if (!terrain.ResidentTileAt(q.X, q.Y, out t, out m))
            return;
        groundOwed = null;
        WmvIpcClient.WorldCameraReport r = PlaceWorldCamera(q);
        Debug.Log("WMV: [terrain] world camera " + q + ": its tile is built -- " + r.Status + ", " + r.Reason);
    }

    /// <summary>The world models' group culling and the doodads' instance culling, for the main camera as it renders (after
    /// every camera of the frame has moved, and for a capture's own aspect): WmvWorldModels.Cull, WmvWorldDoodads.Cull.</summary>
    void CullWorldModels(UnityEngine.Rendering.ScriptableRenderContext context, Camera cam)
    {
        if (cam == null || cam != Camera.main || !TerrainViewActive)
            return;
        if (worldModels != null && worldModels.InstanceCount > 0)
            worldModels.Cull(cam);
        if (worldDoodads != null && worldDoodads.InstanceCount > 0)
            worldDoodads.Cull(cam);
    }

    /// <summary>A tile world load's world models are settled (protocol 17: before its doodads): its camera now -- it stands on
    /// their floors -- and a camera asked for meanwhile no longer waits.</summary>
    void WorldModelsSettled(int load)
    {
        if (settlingLoad == load)
            settlingLoad = 0;
        PlaceOwedCamera(load);
    }

    void DisposeTerrain()
    {
        UnityEngine.Rendering.RenderPipelineManager.beginCameraRendering -= CullWorldModels;
        if (terrain != null)
            terrain.Dispose();
        PumpLaterDestroy(true);
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
        terrainViewMode.StopFly();
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
        if (!haveTerrainFrame || terrainViewMode.Flying)
            return;   // (the free camera stays where the user flew it)
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

    /// <summary>The load was reported built (its tile on screen two frames): the dumps, then the capture.</summary>
    void TerrainTileBuilt(WmvRuntimeTerrainTile tile, TerrainTileMesh mesh, WmvTerrainTileContent content, WmvIpcClient.TerrainTileCommand cmd,
                          int rootFileDataID)
    {
        TerrainTileMaterial mat = content != null ? content.Material : null;
        status.Set(string.Format("Terrain tile {0}_{1} of map {2}: {3} vertices, {4} triangles, {5} hole cell(s); material {6} -- {7} ({8})",
                                 tile.TileX, tile.TileY, tile.MapId, mesh.Vertices, mesh.Triangles, mesh.HoleCells,
                                 mat != null ? TerrainTileMaterial.TileStateName(mat.State) : "none", WhatTheViewShows(tile),
                                 WmvTerrainDebug.ViewNames[(int)tile.View]) +
                   (terrain.World != null && terrain.World.Instances.Count > 0
                        ? string.Format("; {0} world model instance(s) of {1} world model(s)", terrain.World.Instances.Count,
                                        terrain.World.Prototypes.Count + terrain.World.PrivatePrototypes.Count)
                        : "") +
                   (terrain.World != null && terrain.World.Doodads.Count > 0
                        ? string.Format("; {0} doodad(s) of {1} M2(s)", terrain.World.Doodads.Count,
                                        terrain.World.DoodadPrototypes.Count + terrain.World.PrivateDoodadPrototypes.Count)
                        : ""));
        // A tile's world load: its camera now that its world models are settled (owedCameraAtBuilt), before the capture.
        if (settlingLoad == tile.Load)
            settlingLoad = 0;
        PlaceOwedCamera(tile.Load);
        if (WmvTerrainDebug.DumpName != null)
            WriteTerrainDump(tile, mesh, cmd, rootFileDataID, WmvTerrainDebug.DumpName);
        System.Action<string> log = s => Debug.Log("WMV: [terrain] " + s);
        if (WmvTerrainDebug.TexelDumpName != null)
            WmvTerrainComposite.WriteTexelDump(tile, content, cmd, WmvTerrainDebug.TexelDumpName, log);
        if (WmvTerrainDebug.CompositeDumpName != null)
        {
            WmvTerrainComposite.WriteTileComposite(tile, mesh, content, cmd, rootFileDataID, WmvTerrainDebug.CompositeDumpName,
                                                   WmvTerrainDebug.CompositeDumpSize, WmvTerrainDebug.Validate, WmvTerrainDebug.TexelDumpName, log);
            if (WmvTerrainDebug.CompositeChunks.Count > 0)
                WmvTerrainComposite.WriteChunkComposites(tile, mesh, content, cmd, rootFileDataID, WmvTerrainDebug.CompositeDumpName,
                                                         WmvTerrainDebug.CompositeChunks, WmvTerrainDebug.Validate, WmvTerrainDebug.TexelDumpName, log);
        }
        RequestViewportShot();
    }

    /// <summary>What a picture of the shown tile is, for the status line and the capture's log line.</summary>
    static string WhatTheViewShows(WmvRuntimeTerrainTile tile)
    {
        return WhatTheViewShows(tile, WmvTerrainDebug.Validate, () => WmvTerrainMaterials.ResolveShader(null) != null);
    }

    public const string GeometryDiagnosticLabel = "GEOMETRY DIAGNOSTIC (not WoW material or lighting)";

    /// <summary>
    /// What a picture of `tile` is, from what was DRAWN (the chunk objects shown, and which of them Wmv/TerrainTextured
    /// draws), not from the view asked for. The stage 03 views (and no tile): a geometry diagnostic. The textured views
    /// and the materials view: their own words with the count of chunks drawn by Wmv/TerrainTextured and, when any chunk
    /// was drawn NEUTRAL (Wmv/Terrain's lit grey), that count named a geometry-diagnostic shading; when no chunk was drawn
    /// textured, the geometry diagnostic's words with why: -- `texturedShader` false -- the textured shader missing or
    /// unsupported; with -wmvTerrainChunk=N (the tile's OnlyChunk) while other, hidden chunks ARE drawn textured, that the
    /// chunk shown is not drawn textured (or has no triangles); otherwise that no chunk of the tile is drawn textured.
    /// `validation` (-wmvTerrainValidate) adds that the textured chunks are sampled for validation, not the production
    /// look. `texturedShader` is asked only when no chunk was drawn textured.
    /// </summary>
    public static string WhatTheViewShows(WmvRuntimeTerrainTile tile, bool validation, System.Func<bool> texturedShader)
    {
        if (tile == null)
            return GeometryDiagnosticLabel;
        WmvTerrainView v = tile.View;
        bool texturedView = v == WmvTerrainView.Textured || v == WmvTerrainView.TexturedLit || v == WmvTerrainView.TexturedPlainWeights;
        if (!texturedView && v != WmvTerrainView.Materials)
            return GeometryDiagnosticLabel;
        int drawnTextured = 0, drawnNeutral = 0, hiddenTextured = 0;
        for (int n = 0; n < tile.ChunkObjects.Length; n++)
        {
            GameObject go = tile.ChunkObjects[n];
            if (go == null)
                continue;                                    // no triangles: no object
            if (!go.activeSelf)
            {
                if (tile.ChunkTextured[n]) hiddenTextured++; // hidden by -wmvTerrainChunk
                continue;
            }
            if (tile.ChunkTextured[n]) drawnTextured++;
            else drawnNeutral++;
        }
        string viewName = WmvTerrainDebug.ViewNames[(int)v];
        if (drawnTextured == 0)
        {
            string why;
            if (texturedShader != null && !texturedShader())
                why = "Resources/" + WmvTerrainMaterials.ShaderResource + ".shader is missing or not supported";
            else if (tile.OnlyChunk >= 0 && hiddenTextured > 0)
                why = "the chunk shown (-wmvTerrainChunk " + tile.OnlyChunk + ") " +
                      (tile.OnlyChunk < tile.ChunkObjects.Length && tile.ChunkObjects[tile.OnlyChunk] != null ? "is not drawn textured"
                                                                                                              : "has no triangles");
            else
                why = "no chunk of the tile is drawn textured";
            return string.Format("{0} -- the {1} view drew no chunk with Wmv/TerrainTextured ({2}): all {3} chunk(s) shown NEUTRAL",
                                 GeometryDiagnosticLabel, viewName, why, drawnNeutral);
        }
        string what;
        switch (v)
        {
            case WmvTerrainView.Textured:
                what = "TEXTURE COMPOSITE (stage 04: texture layers, the height mix of the alpha weights, MCCV; layer animation and " +
                       "overbright not applied; no lighting, shadow map, water or objects on the composite chunks)";
                break;
            case WmvTerrainView.TexturedLit:
                what = "TEXTURE COMPOSITE under the DIAGNOSTIC light (not the game's lighting)";
                break;
            case WmvTerrainView.TexturedPlainWeights:
                what = "PLAIN-WEIGHTS COMPARISON, hatched (no height mix: NOT the composite; layer effects ignored; a layer of " +
                       "unresolved repeats drawn as the checker)";
                break;
            default:
                what = "MATERIAL STATES (one colour per chunk state; the legend is in the log)";
                break;
        }
        var sb = new System.Text.StringBuilder(what);
        sb.Append(" -- ").Append(drawnTextured).Append(" chunk(s) drawn ").Append(v == WmvTerrainView.Materials ? "in their state colour" : "textured");
        if (drawnNeutral > 0)
            sb.Append("; NEUTRAL: ").Append(drawnNeutral).Append(" of ").Append(drawnTextured + drawnNeutral)
              .Append(" chunk(s) drawn in GEOMETRY DIAGNOSTIC shading (lit grey, not WoW material)");
        if (validation && texturedView)
            sb.Append("; VALIDATION sampling (mip 0 by Load, unfiltered -- not the production look)");
        return sb.ToString();
    }

    /// <summary>runtimeState's protocol-12 fields. What the terrain holds (terrainMeshes, terrainMaterials,
    /// terrainGameObjects) is the tiles' own tally, never a scan of the scene: it counts a tile still being built, never
    /// the persistent scene root _WmvTerrain, and drops the moment a tile is released, not when Unity's deferred
    /// Object.Destroy lands -- so it is 0 with no tile, right after a clear, a model load or a failed build. Then the
    /// session's request bookkeeping (pending at most WmvTerrainSession.MaxOutstanding, retired at most RetiredCapacity,
    /// retired in all and the last one retired, map answers dropped -- late plus unowned, and each apart), its timeout,
    /// and (protocol 13) the tiles' layer textures, their GPU bytes and their alpha arrays, by the same tallies.</summary>
    void FillTerrainRuntimeState(ref WmvIpcClient.RuntimeReport r)
    {
        Camera cam = Camera.main;
        r.LiveTerrainTiles = WmvRuntimeTerrainTile.Live;
        r.TerrainLoad = terrain != null ? terrain.ShownLoad : 0;
        r.TerrainLoading = terrain != null && terrain.Loading;
        r.TerrainView = TerrainViewActive;
        r.CameraMask = cam != null ? cam.cullingMask : 0;
        // Light (protocol 18): the whole-player scans skipped (-1), so the harness can ask while the world streams.
        bool light = WmvIpcClient.RuntimeStateLight;
        r.Light = light;
        r.MeshObjects = light ? -1 : Resources.FindObjectsOfTypeAll<Mesh>().Length;
        r.MaterialObjects = light ? -1 : Resources.FindObjectsOfTypeAll<Material>().Length;
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
        // (Streaming: the scene's terrain textures too -- the tiles bind them, off their own tallies.)
        r.TerrainTextures = WmvRuntimeTerrainTile.LiveTextures + (terrain != null ? terrain.SceneTerrainTextures : 0);
        r.TerrainTextureBytes = WmvRuntimeTerrainTile.LiveTextureBytes + (terrain != null ? terrain.SceneTerrainTextureBytes : 0);
        r.TerrainAlphaArrays = WmvRuntimeTerrainTile.LiveAlphaArrays;
        // Protocol 16: the world models (their own tallies, never WmvRuntimeMapObject.Live).
        r.WorldInstances = WmvWorldModels.LiveInstances;
        r.WorldModels = WmvWorldModels.LivePrototypes;
        r.WorldMeshes = WmvWorldModels.LiveMeshes;
        r.WorldMaterials = WmvWorldModels.LiveMaterials;
        r.WorldTextures = WmvWorldModels.LiveTextures;
        r.WorldGpuGeometryBytes = WmvWorldModels.LiveGpuGeometryBytes;
        r.WorldGpuTextureBytes = WmvWorldModels.LiveGpuTextureBytes;
        // The culling of the last frame drawn, while world models are on screen (none: 0, never the last scene's counts).
        bool culling = worldModels != null && worldModels.InstanceCount > 0 && TerrainViewActive;
        r.WorldGroups = culling ? worldModels.Groups : 0;
        r.WorldGroupsVisible = culling ? worldModels.GroupsVisible : 0;
        r.WorldGroupsCulled = culling ? worldModels.GroupsCulled : 0;
        r.WorldInstancesCulled = culling ? worldModels.InstancesCulled : 0;
        WmvWorldScene scene = terrain != null ? terrain.World : null;
        r.WorldLoad = scene != null ? scene.Load : 0;
        r.WorldKind = scene != null ? scene.Kind : "";
        // Protocol 17: the doodads (their own tallies), their culling as of the last frame drawn, and every object alive.
        r.DoodadInstances = WmvWorldDoodads.LiveInstances;
        r.DoodadPrototypes = WmvWorldDoodads.LivePrototypes;
        r.DoodadMeshes = WmvWorldDoodads.LiveMeshes;
        r.DoodadMaterials = WmvWorldDoodads.LiveMaterials;
        r.DoodadTextures = WmvWorldDoodads.LiveTextures;
        r.DoodadGpuGeometryBytes = WmvWorldDoodads.LiveGpuGeometryBytes;
        r.DoodadGpuTextureBytes = WmvWorldDoodads.LiveGpuTextureBytes;
        bool doodadCulling = worldDoodads != null && worldDoodads.InstanceCount > 0 && TerrainViewActive;
        r.DoodadVisible = doodadCulling ? worldDoodads.Visible : 0;
        r.DoodadCulled = doodadCulling ? worldDoodads.Culled : 0;
        r.LiveGameObjects = light ? -1 : Resources.FindObjectsOfTypeAll<GameObject>().Length;
        r.LiveTexture2D = light ? -1 : Resources.FindObjectsOfTypeAll<Texture2D>().Length;
        // Protocol 18: the streaming session.
        r.Streaming = terrain != null && terrain.Streaming;
        if (terrain != null)
        {
            r.StreamTiles = terrain.StreamTiles;
            r.StreamJobs = terrain.StreamJobs;
            r.StreamRequestsOut = terrain.StreamRequestsOut;
            r.StreamPlacements = terrain.StreamPlacements;
            r.StreamDoodads = terrain.StreamDoodads;
            r.SceneTerrainTextures = terrain.SceneTerrainTextures;
            r.StreamPrototypesEvicted = terrain.StreamPrototypesEvicted;
            r.StreamTexturesEvicted = terrain.StreamTexturesEvicted;
            r.StreamFilesAskedAgain = terrain.StreamFilesAskedAgain;
            r.StreamImagesShared = terrain.StreamImagesShared;
        }
        r.LiveMeshes = r.MeshObjects;
        r.LiveMaterials = r.MaterialObjects;
        r.WorldFrames = worldFrames;
        if (worldFrames > 0)
        {
            var sorted = new float[worldFrames];
            System.Array.Copy(worldFrameMs, sorted, worldFrames);
            System.Array.Sort(sorted);
            r.WorldFrameMsMedian = sorted[worldFrames / 2];
            r.WorldFrameMsP95 = sorted[System.Math.Min(worldFrames - 1, (int)System.Math.Ceiling(0.95 * worldFrames) - 1)];
            r.WorldFrameMsMax = sorted[worldFrames - 1];
        }
    }

    /// <summary>The capture's log line in the terrain view: the tile and the world models placed with it (or a WMO-only
    /// map's world model), not the hidden model.</summary>
    void LogTerrainShot(string name, Texture2D tex, string busy)
    {
        WmvRuntimeTerrainTile tile = terrain != null ? terrain.ShownTile : null;
        WmvWorldScene scene = terrain != null ? terrain.World : null;
        Camera cam = Camera.main;
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        string what = WhatTheViewShows(tile);
        if (scene != null && (scene.Instances.Count > 0 || scene.Doodads.Count > 0))
        {
            string models = string.Format(inv, "{0} world model instance(s) of {1} world model(s), {2} of {3} group(s) drawn ({4} culled); " +
                                          "{5} doodad(s) of {6} M2(s), {7} drawn ({8} culled) -- {9}",
                                          scene.Instances.Count, scene.Prototypes.Count + scene.PrivatePrototypes.Count,
                                          worldModels.GroupsVisible, worldModels.Groups, worldModels.GroupsCulled, scene.Doodads.Count,
                                          scene.DoodadPrototypes.Count + scene.PrivateDoodadPrototypes.Count, worldDoodads.Visible,
                                          worldDoodads.Culled, WorldModelsLabel);
            what = tile == null ? string.Format(inv, "map {0} world model (load {1}): {2}", scene.MapId, scene.Load, models) : what + "; plus " + models;
        }
        Debug.Log(string.Format(inv,
            "WMV: viewport shot {0} #{1} -- {2}x{3}: {15}, terrain {4}, view {5}{6}; " +
            "camera at ({7:F3}, {8:F3}, {9:F3}), {10}, field of view {11:0.##}, aspect {12:F3}, mask {13}{14}",
            name, viewportShots, tex.width, tex.height,
            tile != null ? string.Format(inv, "map {0} tile {1}_{2} (load {3})", tile.MapId, tile.TileX, tile.TileY, tile.Load) : "none shown",
            tile != null ? WmvTerrainDebug.ViewNames[(int)tile.View] : WmvTerrainDebug.ViewName,
            WmvTerrainDebug.OnlyChunk >= 0 ? ", only chunk " + WmvTerrainDebug.OnlyChunk : "",
            cam != null ? cam.transform.position.x : 0f, cam != null ? cam.transform.position.y : 0f, cam != null ? cam.transform.position.z : 0f,
            terrainViewMode.TopDown ? string.Format(inv, "top-down orthographic half-size {0:F3}", cam != null ? cam.orthographicSize : 0f)
                                    : string.Format(inv, "pivot ({0:F3}, {1:F3}, {2:F3}) yaw {3:0.##} pitch {4:0.##} distance {5:F3}",
                                                    orbit.pivot.x, orbit.pivot.y, orbit.pivot.z, orbit.yaw, orbit.pitch, orbit.distance),
            cam != null ? cam.fieldOfView : 0f, cam != null ? cam.aspect : 0f, cam != null ? cam.cullingMask : 0,
            busy == null ? "" : "; taken while " + busy, what));
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
            d.Meta.Add(new KeyValuePair<string, string>("view", WmvTerrainDebug.ViewNames[(int)tile.View]));
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

        public void BuildTile(WmvRuntimeTerrainTile tile, TerrainTileMesh mesh, WmvTerrainTileContent content)
        {
            var o = new WmvTerrainBuildOptions
            {
                View = WmvTerrainDebug.EffectiveView(content), OnlyChunk = WmvTerrainDebug.OnlyChunk,
                KeepReadable = WmvTerrainDebug.DumpName != null,
            };
            WmvTerrainBuilder.Build(tile, mesh, content, main.TerrainSceneRoot(), o, s => Debug.Log("WMV: [terrain] " + s));
        }

        public bool DrawsPlainDiagnostic { get { return WmvTerrainDebug.DrawsPlainDiagnostic; } }

        public void ShowTile(WmvRuntimeTerrainTile tile)
        {
            tile.Root.SetActive(true);
            main.SetViewMode(true, "tile " + tile.TileX + "_" + tile.TileY + " of map " + tile.MapId + ", load " + tile.Load);
            main.FrameTerrainTile(tile);
            main.PlaceOwedCamera(tile);
        }

        public void LeaveTerrainView(string why, bool forModelLoad) { main.SetViewMode(false, why, forModelLoad); }

        public void DisposeTile(WmvRuntimeTerrainTile tile) { tile.Dispose(); }

        public void DisposeTileLater(WmvRuntimeTerrainTile tile) { tile.DisposeLater(main.laterDestroy); }

        public void Sample(WmvIpcClient.TerrainTileReport report) { WmvTerrainBuilder.SampleCounters(report); }

        public void TileBuilt(WmvRuntimeTerrainTile tile, TerrainTileMesh mesh, WmvTerrainTileContent content,
                              WmvIpcClient.TerrainTileCommand command, int rootFileDataID)
        {
            main.TerrainTileBuilt(tile, mesh, content, command, rootFileDataID);
        }
    }

    // ---------------------------------------------------------------- the world models' Unity side (protocol 16)

    sealed class MainWorldView : IWmvWorldModelView
    {
        readonly WmvMain main;

        public MainWorldView(WmvMain main) { this.main = main; }

        public WmvWorldPrototype BuildPrototype(uint fileDataID, Wmv.Wow.WmoRoot root, Wmv.Wow.WmoGroup[] groups,
                                                Dictionary<uint, WmvWmoTexture> textures)
        {
            return main.worldModels.BuildPrototype(fileDataID, root, groups, textures);
        }

        public WmvWorldInstance PlaceInstance(WmvWorldPrototype prototype, WorldModelPlacement placement, WorldOrigin origin, int load)
        {
            return main.worldModels.Place(prototype, placement, origin, load);
        }

        public void DisposeInstance(WmvWorldInstance instance) { main.worldModels.DisposeInstance(instance); }

        public void DisposePrototype(WmvWorldPrototype prototype) { main.worldModels.DisposePrototype(prototype); }

        public void ShowWorld(WmvWorldScene scene)
        {
            main.SetViewMode(true, "the world model of map " + scene.MapId + ", load " + scene.Load);
            main.FrameWorldScene(scene);
            main.PlaceOwedCamera(scene.Load);
        }

        public void WorldBuilt(WmvWorldScene scene)
        {
            main.status.Set(string.Format("World model of map {0}: {1} instance(s), {2} world model(s) -- {3}", scene.MapId, scene.Instances.Count,
                                          scene.Prototypes.Count, WorldModelsLabel));
            main.RequestViewportShot();
        }

        public void ReportWorldShown(WmvIpcClient.WorldShownReport report) { main.ipc.ReportWorldShown(report); }

        public void ReportWorldProgress(WmvIpcClient.WorldProgressReport report) { main.ipc.ReportWorldProgress(report); }

        public void WorldModelsSettled(WmvWorldScene scene, int load)
        {
            if (scene != null && scene.Kind == "tile")
                main.WorldModelsSettled(load);
        }

        public WmvDoodadPrototype BuildDoodadPrototype(WmvWorldScene scene, uint fileDataID, Wmv.Wow.M2ParsedModel model, Wmv.Wow.M2ParsedSkin skin,
                                                       Dictionary<int, Wmv.Wow.BlpImage> texturesBySlot, Dictionary<int, uint> slotFiles)
        {
            if (scene.DoodadTextures == null)
                scene.DoodadTextures = new WmvDoodadTextures();
            return main.worldDoodads.BuildPrototype(fileDataID, model, skin, texturesBySlot, slotFiles, scene.DoodadTextures);
        }

        public WmvDoodadInstance PlaceDoodad(WmvDoodadPrototype prototype, WorldDoodad doodad, WorldOrigin origin, int load)
        {
            return main.worldDoodads.Place(prototype, doodad, origin, load);
        }

        public void DisposeDoodadPrototype(WmvWorldScene scene, WmvDoodadPrototype prototype)
        {
            main.worldDoodads.DisposePrototype(prototype, scene != null ? scene.DoodadTextures : null);
        }

        public void DisposeDoodadInstances(List<WmvDoodadInstance> instances) { main.worldDoodads.DisposeInstances(instances, main.laterDestroy); }

        public void DisposeDoodads(WmvWorldScene scene)
        {
            main.worldDoodads.DisposeInstances(scene.Doodads);
            scene.Doodads.Clear();
            foreach (WmvDoodadPrototype p in scene.DoodadPrototypes.Values)
                main.worldDoodads.DisposePrototype(p, scene.DoodadTextures);
            scene.DoodadPrototypes.Clear();
            if (scene.DoodadTextures != null)
                scene.DoodadTextures.Dispose();
            scene.DoodadTextures = null;
        }
    }

    /// <summary>What a picture of placed world models and doodads is (the status line and the capture's log line).</summary>
    public const string WorldModelsLabel = "WORLD MODELS AND DOODADS (the Buildings renderer's WMO materials and the Models renderer's M2 materials " +
                                           "under its preview key and fill, without shadow maps; doodads static in their rest pose, no particles; " +
                                           "no WoW lighting, liquids, sky or fog)";
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
    bool active, topDown, flying;
    float savedNear, savedFar;
    /// <summary>The world view's free camera (protocol 15), on the same camera; null in a self-test that drives no flight.</summary>
    public WmvFlyCamera Fly;
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
    /// <summary>The free camera holds the main camera (ApplyFly) until the overview, a new tile's framing or Leave.</summary>
    public bool Flying { get { return flying; } }
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
        StopFly();
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

    /// <summary>
    /// The free camera takes the main camera (protocol 15): the orbit stops taking input (its Update would put its own
    /// placement back), the projection is perspective, the clip planes are the flight's, and the camera stands at `position`
    /// (Unity space) facing yaw and pitch (degrees). False (nothing done) outside the terrain view or without a free camera.
    /// StopFly, the overview's framing and Leave give the camera back.
    /// </summary>
    public bool ApplyFly(Vector3 position, float yaw, float pitch)
    {
        if (!active || Fly == null)
            return false;
        if (!flying)
        {
            savedNear = cam.nearClipPlane;
            savedFar = cam.farClipPlane;
        }
        orbit.enabled = false;
        cam.orthographic = false;
        topDown = false;
        cam.nearClipPlane = WmvFlyCamera.NearPlane;
        cam.farClipPlane = WmvFlyCamera.FarPlane;
        Fly.Place(position, yaw, pitch);
        Fly.enabled = true;
        flying = true;
        return true;
    }

    /// <summary>The free camera lets go: the orbit takes input again as it did before the flight, and the clip planes are
    /// put back (a framing that follows sets its own).</summary>
    public void StopFly()
    {
        if (!flying)
            return;
        flying = false;
        if (Fly != null)
            Fly.enabled = false;
        orbit.enabled = savedOrbitEnabled;
        cam.nearClipPlane = savedNear;
        cam.farClipPlane = savedFar;
    }
}
