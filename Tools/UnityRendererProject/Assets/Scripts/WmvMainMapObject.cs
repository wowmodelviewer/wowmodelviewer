// WmvMainMapObject.cs
//
// The WORLD-MODEL (WMO) load job of the viewport player -- the half of WmvMain that loadWoWModel with
// kind "wmo" runs. It sits alongside the M2 job and follows the same rules:
//
//   loadWoWModel(kind "wmo", root path, root fileDataID, load)
//     -> getAssetByFileDataID(root)          the root .wmo
//     -> WmoParser.ParseRoot                  MOHD, MOMT, MOGI, GFID, ...
//     -> getAssetByFileDataID(GFID[i])        for i in 0 .. MOHD group count - 1, all requested at once.
//                                             LOD0 only: the later GFID blocks are LOD groups and would
//                                             draw the same building twice. No group file name is ever
//                                             constructed -- GFID is the relationship.
//     -> WmoParser.ParseGroup                 on worker threads, as each group file lands
//     -> getAssetByFileDataID(texture)        once the groups are parsed: every non-zero material texture
//                                             FileDataID of the whole WMO, once each -- the tail slots of
//                                             shader 22/23 included
//     -> BlpDecoder                           on worker threads, at most a few at a time, and ONLY for the
//                                             textures a material some LOD0 batch draws actually SAMPLES,
//                                             as its plan (Wow.WmoMaterialSemantics) binds them -- once per
//                                             FileDataID however many slots or materials name it. Every other
//                                             file is fetched and its header checked, not decoded
//                                             (HandleMapObjectAsset has the measurements that decided it)
//     -> WmvWmoBuilder                        staged: built inactive, then swapped in within one frame
//     -> mapObjectLoaded                      the outcome, once per load
//
// A new load of EITHER kind supersedes a world-model load in flight ("superseded" is reported, and its
// late asset answers and worker results are ignored), and a world model replaces the model on screen
// -- M2, character or another WMO -- only when it is built, destroying the old runtime and every mesh,
// material and texture it owned. A world model has no animator: animation, skin and geoset pushes about
// it are ignored.

using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow;

public partial class WmvMain
{
    WmvRuntimeMapObject currentMapObject;   // the world model on screen, if any
    int currentMapObjectFileDataID;
    MapObjectJob wmoJob;                     // the world-model load in flight, if any

    /// <summary>Results from the worker threads, applied on the main thread in PumpMapObjectWork.</summary>
    readonly Queue<System.Action> mapObjectWork = new Queue<System.Action>();

    class MapObjectJob
    {
        public string Path;
        public int FileDataID;
        public int Load;
        public readonly System.Diagnostics.Stopwatch Clock = System.Diagnostics.Stopwatch.StartNew();
        /// <summary>Set when the job is superseded or finished; a worker result for it is dropped.</summary>
        public volatile bool Dead;

        public string PendingRoot;
        public WmoRoot Root;
        public uint[] GroupIds = new uint[0];
        public WmoGroup[] Groups = new WmoGroup[0];
        public string[] GroupErrors = new string[0];
        public readonly Dictionary<string, int> PendingGroups = new Dictionary<string, int>();   // requestId -> group
        public readonly Dictionary<string, uint> PendingTextures = new Dictionary<string, uint>(); // requestId -> fileDataID
        public readonly Dictionary<uint, WmvWmoTexture> Textures = new Dictionary<uint, WmvWmoTexture>();
        /// <summary>The files the drawn materials' plans sample: the only ones decoded, each once.</summary>
        public readonly HashSet<uint> SampledTextures = new HashSet<uint>();
        public int GroupsOutstanding, TexturesOutstanding;   // requested or decoding, not yet landed
        public bool TexturesRequested;                        // after the groups: see RequestMapObjectTextures
        public readonly Queue<KeyValuePair<WmvWmoTexture, byte[]>> DecodeQueue = new Queue<KeyValuePair<WmvWmoTexture, byte[]>>();
        public int DecodesRunning;
        public int GroupFilesRequested;

        public double RootMs, GroupsStartMs, GroupsDoneMs, TexturesStartMs, TexturesDoneMs, BuildMs;
        public double GroupParseCpuMs, TextureDecodeCpuMs, RootParseMs;
        public long GroupBytes, TextureBytes;
        public long HeapAtStart;
        public int Gen0AtStart;

        public bool Owns(string requestId)
        {
            return requestId == PendingRoot || PendingGroups.ContainsKey(requestId) ||
                   PendingTextures.ContainsKey(requestId);
        }
    }

    static bool IsMapObjectKind(string kind)
    {
        return kind == WmvIpcClient.KindMapObject;
    }

    // ---------------------------------------------------------------- start / supersede

    void StartMapObjectLoad(string path, int fileDataID, int load)
    {
        wmoJob = new MapObjectJob
        {
            Path = path, FileDataID = fileDataID, Load = load,
            HeapAtStart = System.GC.GetTotalMemory(false), Gen0AtStart = System.GC.CollectionCount(0),
        };
        status.Set("Requested world model " + (string.IsNullOrEmpty(path) ? ("fileDataID " + fileDataID) : path));
        Debug.Log(string.Format("WMV: wmo: load {0} of root {1} ({2}) begins", load, fileDataID, path));
        // By FileDataID when the host named one: the root's identity is its id, and the path is only the
        // listfile's name for it.
        wmoJob.PendingRoot = fileDataID > 0 ? ipc.RequestAssetByFileDataID(fileDataID) : ipc.RequestAsset(path);
    }

    /// <summary>A world-model load in flight will never be shown: say so once, and forget it.</summary>
    void SupersedeMapObjectJob(string reason)
    {
        if (wmoJob == null)
            return;
        MapObjectJob old = wmoJob;
        wmoJob = null;
        old.Dead = true;
        Debug.Log(string.Format("WMV: wmo: load {0} of root {1} superseded after {2} ms ({3})", old.Load,
                                old.FileDataID, old.Clock.ElapsedMilliseconds, reason));
        ReportMapObject(old, null, "superseded", reason);
    }

    /// <summary>
    /// Pushes that must not act while a world model is loading or on screen: a WMO has no animation,
    /// skin or geosets, so a push naming it -- or naming nothing, with nothing else to apply it to -- is
    /// dropped. While an M2 is loading the push may be about that model, and the M2 path decides.
    /// </summary>
    bool PushIsAboutMapObject(int fileDataID)
    {
        if (job != null)
            return false;
        if (wmoJob != null && (fileDataID == 0 || fileDataID == wmoJob.FileDataID))
            return true;
        return currentSlot.Runtime == null && currentMapObject != null &&
               (fileDataID == 0 || fileDataID == currentMapObjectFileDataID);
    }

    // ---------------------------------------------------------------- assets

    /// <summary>Claim an asset answer for the world-model job. False when it is not the job's.</summary>
    bool HandleMapObjectAsset(WmvIpcClient.AssetResponse r)
    {
        MapObjectJob j = wmoJob;
        if (j == null || !j.Owns(r.requestId))
            return false;

        if (r.requestId == j.PendingRoot)
        {
            j.PendingRoot = null;
            OnMapObjectRoot(j, r);
            return true;
        }

        int groupIndex;
        if (j.PendingGroups.TryGetValue(r.requestId, out groupIndex))
        {
            j.PendingGroups.Remove(r.requestId);
            if (!r.ok || r.data == null)
            {
                j.GroupErrors[groupIndex] = "request failed: " + (r.error ?? "no data");
                GroupLanded(j);
                return true;
            }
            j.GroupBytes += r.data.Length;
            byte[] bytes = r.data;
            string what = string.Format("wmo group {0} (fileDataID {1})", groupIndex, j.GroupIds[groupIndex]);
            QueueWorker(j, () =>
            {
                var sw = System.Diagnostics.Stopwatch.StartNew();
                WmoGroup parsed = null;
                string error = null;
                try { parsed = WmoParser.ParseGroup(bytes, what, groupIndex); }
                catch (System.Exception e) { error = "parse failed: " + e.Message; }
                double ms = sw.Elapsed.TotalMilliseconds;
                return () =>
                {
                    j.GroupParseCpuMs += ms;
                    j.Groups[groupIndex] = parsed;
                    j.GroupErrors[groupIndex] = error;
                    GroupLanded(j);
                };
            });
            return true;
        }

        uint textureId;
        if (j.PendingTextures.TryGetValue(r.requestId, out textureId))
        {
            j.PendingTextures.Remove(r.requestId);
            var tex = new WmvWmoTexture { FileDataID = textureId };
            j.Textures[textureId] = tex;
            if (!r.ok || r.data == null)
            {
                tex.Error = "request failed: " + (r.error ?? "no data");
                TextureLanded(j);
                return true;
            }
            j.TextureBytes += r.data.Length;
            if (!j.SampledTextures.Contains(textureId))
            {
                // FETCHED, CHECKED, NOT DECODED. No drawn material's plan samples this file (a slot the
                // material's permutation does not read -- shader 23's env map in +0x0C, whose emissive is
                // not drawn -- or any texture of a material no LOD0 batch uses), so decoding it would
                // only burn time and memory: decoding every referenced file took the modern tower 67 s of
                // CPU and 380 MB of heap for the 19 images it draws, and an 86-group cave 2,550 s of CPU and
                // 2.2 GB for one. The header proves the id is a real texture; a material stage that samples
                // it fetches it again.
                string headerError;
                if (WmvWmoTexture.ReadHeader(r.data, tex, out headerError))
                    tex.HeaderOnly = true;
                else
                    tex.Error = headerError;
                TextureLanded(j);
                return true;
            }
            j.DecodeQueue.Enqueue(new KeyValuePair<WmvWmoTexture, byte[]>(tex, r.data));
            PumpDecodes(j);
            return true;
        }
        return false;
    }

    void OnMapObjectRoot(MapObjectJob j, WmvIpcClient.AssetResponse r)
    {
        if (!r.ok || r.data == null)
        {
            FailMapObject(j, "root request failed: " + (r.error ?? "no data"));
            return;
        }
        if (j.FileDataID <= 0) j.FileDataID = r.fileDataID;
        var sw = System.Diagnostics.Stopwatch.StartNew();
        try
        {
            j.Root = WmoParser.ParseRoot(r.data, string.Format("wmo root {0}", j.FileDataID));
        }
        catch (WowParseException e)
        {
            FailMapObject(j, "root parse failed: " + e.Message);
            return;
        }
        j.RootParseMs = sw.Elapsed.TotalMilliseconds;
        j.RootMs = j.Clock.Elapsed.TotalMilliseconds;

        WmoRoot root = j.Root;
        j.GroupIds = root.Lod0GroupFileDataIDs;
        Debug.Log(string.Format(
            "WMV: wmo: root {0} ({1} bytes, parsed in {2:F1} ms): version {3}, {4} group(s), {5} material(s), " +
            "LOD count {6} (GFID holds {7} entries; only the first {4} -- LOD0 -- are fetched), {8} doodad set(s), " +
            "{9} doodad(s), {10} light(s), {11} fog(s), {12} portal(s), MOTX {13}, MODN {14}, {15} chunk(s), {16} warning(s)",
            j.FileDataID, r.data.Length, j.RootParseMs, root.Version, root.GroupCount, root.Materials.Length,
            root.LodCount, root.GroupFileDataIDs.Length, root.DoodadSets.Length, root.DoodadDefs.Length,
            root.Lights.Count, root.Fogs.Count, root.Portals.Length, root.HasMotx ? "present" : "absent",
            root.HasModn ? "present" : "absent", root.Chunks.Length, root.Warnings.Length));
        foreach (string w in root.Warnings)
            Debug.Log("WMV: wmo: root warning: " + w);

        if (root.GroupCount <= 0)
        {
            FailMapObject(j, "the root declares no groups");
            return;
        }

        int n = j.GroupIds.Length;
        j.Groups = new WmoGroup[n];
        j.GroupErrors = new string[n];
        j.GroupsStartMs = j.Clock.Elapsed.TotalMilliseconds;
        for (int i = 0; i < n; i++)
        {
            if (j.GroupIds[i] == 0)
            {
                // The parser warns about a zero LOD0 entry rather than failing the root; a group with no
                // file is a missing group, never a reason to guess a file name.
                j.GroupErrors[i] = "GFID entry is 0";
                continue;
            }
            j.PendingGroups[ipc.RequestAssetByFileDataID((int)j.GroupIds[i])] = i;
            j.GroupsOutstanding++;
            j.GroupFilesRequested++;
        }
        status.Set(string.Format("World model root parsed: {0} group file(s) requested", j.GroupFilesRequested));
        if (j.GroupsOutstanding == 0)
        {
            j.GroupsDoneMs = j.GroupsStartMs;
            RequestMapObjectTextures(j);
            BuildMapObjectIfReady(j);
        }
    }

    /// <summary>A group file arrived, failed, or finished parsing. The last one starts the textures.</summary>
    void GroupLanded(MapObjectJob j)
    {
        j.GroupsOutstanding--;
        if (j.GroupsOutstanding > 0)
            return;
        j.GroupsDoneMs = j.Clock.Elapsed.TotalMilliseconds;
        RequestMapObjectTextures(j);
        BuildMapObjectIfReady(j);
    }

    /// <summary>
    /// Every non-zero texture FileDataID of the whole WMO, requested once each -- after the groups,
    /// because only the groups say which materials are drawn, and only the textures a drawn material's
    /// plan samples are decoded (see HandleMapObjectAsset). Every other file is still fetched.
    /// </summary>
    void RequestMapObjectTextures(MapObjectJob j)
    {
        if (j.TexturesRequested)
            return;
        j.TexturesRequested = true;
        WmoRoot root = j.Root;
        var usedMaterials = new HashSet<int>();
        foreach (WmoGroup g in j.Groups)
        {
            if (g == null) continue;
            foreach (WmoBatch b in g.Batches)
                if (b.TriangleIndexCount > 0 && b.MaterialId >= 0 && b.MaterialId < root.Materials.Length)
                    usedMaterials.Add(b.MaterialId);
        }
        // The same plan the builder draws from, so what is decoded and what is sampled cannot drift
        // apart: a material decodes the registers its permutation reads (a four-layer one its layers and
        // height maps, not its env map; a two-layer one +0x0C and +0x18, not id 7's env map +0x24; id 5 only
        // +0x0C, not its env map +0x18), a provisional one only the +0x0C
        // texture its baseline draws. The set keeps one entry per FileDataID.
        foreach (int id in usedMaterials)
            WmoMaterialSemantics.CollectSampledTextures(WmoMaterialSemantics.Plan(root.Materials[id]), j.SampledTextures);

        uint[] textureIds = root.GetAllTextureFileDataIDs();
        j.TexturesStartMs = j.Clock.Elapsed.TotalMilliseconds;
        foreach (uint id in textureIds)
        {
            j.PendingTextures[ipc.RequestAssetByFileDataID((int)id)] = id;
            j.TexturesOutstanding++;
        }
        if (j.TexturesOutstanding == 0) j.TexturesDoneMs = j.TexturesStartMs;
        status.Set(string.Format("World model groups parsed: {0} texture(s) requested", textureIds.Length));
        Debug.Log(string.Format("WMV: wmo: {0} of {1} material(s) drawn by LOD0 batches; requested {2} texture(s): " +
                                "{3} sampled by the drawn materials' plans (decoded once each, at most {4} at a time), {5} fetched " +
                                "and header-checked only", usedMaterials.Count, root.Materials.Length, textureIds.Length,
                                j.SampledTextures.Count, MaxParallelDecodes, textureIds.Length - j.SampledTextures.Count));
    }

    /// <summary>A texture was decoded, checked or failed.</summary>
    void TextureLanded(MapObjectJob j)
    {
        j.TexturesOutstanding--;
        if (j.TexturesOutstanding == 0) j.TexturesDoneMs = j.Clock.Elapsed.TotalMilliseconds;
        BuildMapObjectIfReady(j);
    }

    /// <summary>
    /// How many textures decode at once. The decoder allocates a full RGBA image per file, and with every
    /// pool thread allocating at once the collector, not the decoding, becomes the cost; the viewer also
    /// runs beside a game. Half the cores, one to four.
    /// </summary>
    static int MaxParallelDecodes
    {
        get { return System.Math.Max(1, System.Math.Min(4, System.Environment.ProcessorCount / 2)); }
    }

    /// <summary>Start queued decodes up to the limit; each finished one starts the next.</summary>
    void PumpDecodes(MapObjectJob j)
    {
        while (j.DecodesRunning < MaxParallelDecodes && j.DecodeQueue.Count > 0)
        {
            KeyValuePair<WmvWmoTexture, byte[]> item = j.DecodeQueue.Dequeue();
            WmvWmoTexture tex = item.Key;
            byte[] bytes = item.Value;
            j.DecodesRunning++;
            QueueWorker(j, () =>
            {
                var sw = System.Diagnostics.Stopwatch.StartNew();
                BlpImage img = null;
                string error = null;
                try { img = BlpDecoder.Decode(bytes); }
                catch (System.Exception e) { error = "decode failed: " + e.Message; }
                double ms = sw.Elapsed.TotalMilliseconds;
                return () =>
                {
                    j.DecodesRunning--;
                    j.TextureDecodeCpuMs += ms;
                    tex.DecodeMs = ms;
                    tex.Image = img;
                    tex.Error = error;
                    tex.Decoded = img != null;
                    if (img != null)
                    {
                        tex.Width = img.Width;
                        tex.Height = img.Height;
                        tex.Encoding = img.Encoding;
                    }
                    PumpDecodes(j);
                    TextureLanded(j);
                };
            });
        }
    }

    // ---------------------------------------------------------------- worker threads

    /// <summary>
    /// Run work off the main thread. The work returns what to apply on the main thread, which
    /// PumpMapObjectWork does -- unless the job died in the meantime, when the result is dropped.
    /// Parsing and decoding touch no Unity API, so they are safe on a pool thread.
    /// </summary>
    void QueueWorker(MapObjectJob j, System.Func<System.Action> work)
    {
        System.Threading.ThreadPool.QueueUserWorkItem(_ =>
        {
            if (j.Dead) return;
            System.Action apply;
            try { apply = work(); }
            catch (System.Exception e)
            {
                // The work catches its own parse and decode failures, so this is a bug; fail the load
                // rather than leave it waiting for a result that will never land.
                string message = e.GetType().Name + ": " + e.Message;
                apply = () => FailMapObject(j, "worker failed: " + message);
            }
            lock (mapObjectWork)
                mapObjectWork.Enqueue(() => { if (!j.Dead && wmoJob == j) apply(); });
        });
    }

    /// <summary>Apply finished worker results. Called from Update.</summary>
    void PumpMapObjectWork()
    {
        while (true)
        {
            System.Action a;
            lock (mapObjectWork)
            {
                if (mapObjectWork.Count == 0) return;
                a = mapObjectWork.Dequeue();
            }
            try { a(); }
            catch (System.Exception e)
            {
                Debug.LogWarning("WMV: wmo: applying a worker result failed: " + e.GetType().Name + ": " + e.Message);
                if (wmoJob != null) FailMapObject(wmoJob, "internal error: " + e.Message);
            }
        }
    }

    // ---------------------------------------------------------------- build and adopt

    void BuildMapObjectIfReady(MapObjectJob j)
    {
        if (j != wmoJob || j.Dead || j.Root == null || j.GroupsOutstanding > 0 || !j.TexturesRequested ||
            j.TexturesOutstanding > 0)
            return;

        int missing = 0;
        var reasons = new List<string>();
        for (int i = 0; i < j.Groups.Length; i++)
            if (j.Groups[i] == null)
            {
                missing++;
                reasons.Add("group " + i + " (fileDataID " + j.GroupIds[i] + "): " + (j.GroupErrors[i] ?? "missing"));
                Debug.LogWarning("WMV: wmo: " + reasons[reasons.Count - 1]);
            }
            else
                foreach (string w in j.Groups[i].Warnings)
                    Debug.Log("WMV: wmo: group " + i + " warning: " + w);
        if (missing == j.Groups.Length)
        {
            FailMapObject(j, "no group file could be read: " + string.Join("; ", reasons.ToArray()));
            return;
        }

        WmvRuntimeMapObject built;
        double t0 = j.Clock.Elapsed.TotalMilliseconds;
        try
        {
            string name = System.IO.Path.GetFileNameWithoutExtension(string.IsNullOrEmpty(j.Path) ? "" : j.Path);
            built = WmvWmoBuilder.Build(j.Root, j.Groups, j.Textures,
                                        "WMO " + (string.IsNullOrEmpty(name) ? j.FileDataID.ToString() : name),
                                        s => Debug.Log("WMV: " + s));
        }
        catch (System.Exception e)
        {
            FailMapObject(j, "build failed: " + e.GetType().Name + ": " + e.Message);
            return;
        }
        j.BuildMs = j.Clock.Elapsed.TotalMilliseconds - t0;

        // The pixels were uploaded (or were never going to be); what describes them stays.
        foreach (var kv in j.Textures) kv.Value.ReleasePixels();

        // ADOPTION IS GUARDED LIKE THE BUILD, as the M2 path guards its own (WmvMain.BuildIfReady). This can
        // run straight from an asset answer, and an exception out of framing, the shadow rig, the capture or
        // a log line used to escape to the IPC client's handler catch: the job stayed current and alive, no
        // mapObjectLoaded went out (the host waited for one that never came), and the next load reported
        // this one "superseded" although it could be on screen. Exactly one report goes out either way.
        bool reported = false;
        try
        {
            AdoptMapObject(j, built);
            string reason = missing > 0 ? missing + " group file(s) missing: " + string.Join("; ", reasons.ToArray()) : "";
            LogMapObjectPerformance(j, built);
            ReportMapObject(j, built, "built", reason);
            reported = true;
        }
        catch (System.Exception e)
        {
            string what = e.GetType().Name + ": " + e.Message;
            Debug.LogError("WMV: wmo: load " + j.Load + " of root " + j.FileDataID + ": adoption failed: " + what);
            if (!reported)
            {
                if (currentMapObject == built)
                {
                    // It is on screen and owned as the current world model: say so, with what went wrong.
                    try { ReportMapObject(j, built, "built", "adopted, but finishing the adoption failed: " + what); }
                    catch (System.Exception e2) { Debug.LogError("WMV: wmo: reporting failed: " + e2.Message); }
                }
                else
                {
                    // Never shown: release it (its meshes, materials and textures) and report the failure.
                    built.Dispose();
                    try { FailMapObject(j, "adoption failed: " + what); }
                    catch (System.Exception e2) { Debug.LogError("WMV: wmo: reporting failed: " + e2.Message); }
                }
            }
        }
        finally
        {
            // Never left half-finished: a dead job's late answers and worker results are ignored.
            j.Dead = true;
            if (wmoJob == j) wmoJob = null;
        }
    }

    /// <summary>Put a built world model on screen in place of whatever is there.</summary>
    void AdoptMapObject(MapObjectJob j, WmvRuntimeMapObject built)
    {
        // The model on screen goes first -- a mount it rides after taking the character off, a character
        // with its parts -- then any earlier world model, then the new one is shown: all in this frame, so
        // nothing flickers and nothing is ever drawn twice.
        if (mounted != null) { mounted.Dispose(); mounted = null; }
        if (dresser != null) { dresser.Dispose(); dresser = null; }
        if (currentSlot.Runtime != null) { currentSlot.Runtime.Dispose(); currentSlot.Runtime = null; }
        currentSlot.Model = null;
        currentSlot.M2Bytes = null;
        currentSlot.Name = "WoWModel";
        currentSlot.FileDataID = 0;
        currentSlot.Textures.Clear();
        currentSlot.TextureIds.Clear();
        // -wmvAllocCheck measures the frames after a world model is adopted as it does after a model.
        allocProbe = WmvModelBuilder.Debug_.AllocCheck ? new AllocProbe { StartFrame = Time.frameCount + 10 } : null;
        if (currentMapObject != null) currentMapObject.Dispose();
        currentMapObject = built;
        currentMapObjectFileDataID = j.FileDataID;
        if (placeholder != null) placeholder.SetActive(false);
        built.Root.SetActive(true);

        Bounds frame = built.Bounds;
        if (WmvModelBuilder.Debug_.HasFrameBounds)
        {
            Debug.Log(string.Format("WMV: wmo: bounds pinned by -wmvFrameBounds (world model's own: centre {0} extents {1})",
                                    built.Bounds.center, built.Bounds.extents));
            frame = WmvModelBuilder.Debug_.FrameBounds;
        }
        ViewFramings++;
        orbit.FrameMapObject(frame);
        haveLastFramed = false;              // a model capture waiting does not frame the model's box again
        ApplyViewportOrbitOverride("wmo: ");
        if (shadowRig != null)
            shadowRig.SetBounds(frame);
        Camera cam = Camera.main;
        Debug.Log(string.Format(
            "WMV: wmo: framed centre {0} radius {1:F1}: distance {2:F1} (zoom {3:F2} .. {4:F1}), yaw {5} pitch {6}, " +
            "clip planes {7:F3} .. {8:F1}; the shadow window spans {9:F1} units over a {10}-texel map",
            frame.center, frame.extents.magnitude, orbit.distance, orbit.MinDistance, orbit.MaxDistance, orbit.yaw,
            orbit.pitch, cam != null ? cam.nearClipPlane : 0f, cam != null ? cam.farClipPlane : 0f,
            frame.extents.magnitude * 1.4f * 2f, 4096));

        {   // SCRATCH: capture the REAL viewport, as the M2 path does.
            string shot = System.Environment.GetEnvironmentVariable("WMV_VIEWPORT_SHOT");
            if (!string.IsNullOrEmpty(shot))
                StartCoroutine(CaptureViewport(shot));
        }

        status.Set("Loaded world model " + (string.IsNullOrEmpty(j.Path) ? j.FileDataID.ToString() : j.Path));
        status.Set(string.Format("Groups {0}  Submeshes {1}  Vertices {2}  Triangles {3}", built.GroupCount,
                                 built.Submeshes, built.VertexCount, built.TriangleCount));
    }

    void FailMapObject(MapObjectJob j, string reason)
    {
        if (j != wmoJob)
            return;
        status.Set("FAILED: world model: " + reason);
        Debug.LogError("WMV: wmo: load " + j.Load + " of root " + j.FileDataID + " failed: " + reason);
        j.Dead = true;
        wmoJob = null;
        ReportMapObject(j, null, "failed", reason);
    }

    void ReportMapObject(MapObjectJob j, WmvRuntimeMapObject built, string statusText, string reason)
    {
        var r = new WmvIpcClient.MapObjectReport
        {
            FileDataID = j.FileDataID,
            Load = j.Load,
            Status = statusText,
            Reason = reason ?? "",
            Groups = j.Root != null ? j.Root.GroupCount : 0,
            GroupFilesRequested = j.GroupFilesRequested,
            Materials = j.Root != null ? j.Root.Materials.Length : 0,
            RootMs = j.RootMs,
            GroupsMs = j.GroupsDoneMs > 0 ? j.GroupsDoneMs - j.GroupsStartMs : 0,
            TexturesMs = j.TexturesDoneMs > 0 ? j.TexturesDoneMs - j.TexturesStartMs : 0,
            BuildMs = j.BuildMs,
            TotalMs = j.Clock.Elapsed.TotalMilliseconds,
            LiveMapObjects = WmvRuntimeMapObject.Live,
            LiveModels = WmvRuntimeModel.Live,
        };
        if (j.Root != null)
        {
            r.TexturesReferenced = j.Root.GetAllTextureFileDataIDs().Length;
            int missingGroups = 0;
            foreach (WmoGroup g in j.Groups) if (g == null) missingGroups++;
            // Only a finished fetch can call a group missing; one superseded in flight is not.
            r.GroupFilesMissing = statusText == "superseded" ? 0 : missingGroups;
        }
        // texturesDecoded counts pixels decoded (the sampled files); a later-slot file that was fetched and
        // whose header checked out is neither decoded nor missing. Missing is a failed fetch, decode or header.
        foreach (var kv in j.Textures)
        {
            if (kv.Value.Decoded) r.TexturesDecoded++;
            else if (kv.Value.Error != null) r.TexturesMissing++;
        }
        if (built != null)
        {
            r.Groups = built.GroupCount;
            r.Batches = built.Batches;
            r.Submeshes = built.Submeshes;
            r.Renderers = built.Renderers;
            // provisionalMaterials: drawn provisionally -- by the archived baseline or by a labelled fallback
            // (id 23 missing a height map, an empty register the draw weights, a blend of 2 or above on ids
            // 4/5/7/13/23). unresolvedMaterials: drawn materials with ANY open question -- resolved-partial
            // and unresolved -- so the host's count only reaches 0 when every drawn material is fully
            // established.
            r.ProvisionalMaterials = built.ProvisionalMaterials;
            r.UnresolvedMaterials = built.PartialMaterials + built.UnresolvedMaterials;
            r.BlendedMaterials = built.BlendedMaterials;
            r.Vertices = built.VertexCount;
            r.Triangles = built.TriangleCount;
            r.HasBounds = built.HasBounds;
            r.BoundsMin = built.Bounds.min;
            r.BoundsMax = built.Bounds.max;
        }
        Debug.Log(string.Format(
            "WMV: wmo: mapObjectLoaded load {0} root {1} {2}{3}: groups {4} (requested {5}, missing {6}), batches {7}, " +
            "submeshes {8}, renderers {9}, materials {10} (provisional {11}, not fully resolved {12}, non-zero blend {13}), " +
            "textures {14} referenced / {15} decoded / {16} missing, vertices {17}, triangles {18}, " +
            "timings root {19:F0} groups {20:F0} textures {21:F0} build {22:F0} total {23:F0} ms, live world models {24}, live models {25}",
            r.Load, r.FileDataID, r.Status, string.IsNullOrEmpty(r.Reason) ? "" : " (" + r.Reason + ")", r.Groups,
            r.GroupFilesRequested, r.GroupFilesMissing, r.Batches, r.Submeshes, r.Renderers, r.Materials,
            r.ProvisionalMaterials, r.UnresolvedMaterials, r.BlendedMaterials, r.TexturesReferenced, r.TexturesDecoded,
            r.TexturesMissing, r.Vertices, r.Triangles, r.RootMs, r.GroupsMs, r.TexturesMs, r.BuildMs, r.TotalMs,
            r.LiveMapObjects, r.LiveModels));
        ipc.ReportMapObjectLoaded(r);
    }

    void LogMapObjectPerformance(MapObjectJob j, WmvRuntimeMapObject built)
    {
        long largest = 0, decodedBytes = 0;
        int decoded = 0, headerOnly = 0;
        foreach (var kv in j.Textures)
        {
            if (kv.Value.HeaderOnly) headerOnly++;
            if (!kv.Value.Decoded) continue;
            decoded++;
            largest = System.Math.Max(largest, (long)kv.Value.Width * kv.Value.Height);
            decodedBytes += (long)kv.Value.Width * kv.Value.Height * 4;
        }
        Debug.Log(string.Format(
            "WMV: wmo: performance -- root fetch+parse {0:F1} ms (parse {1:F1}); {2} group file(s) {3} KB fetched " +
            "and parsed in {4:F1} ms wall ({5:F1} ms parse CPU on workers); {6} texture(s) {7} KB fetched and decoded " +
            "in {8:F1} ms wall ({9:F1} ms decode CPU on workers, {10} decoded into {22} KB of RGBA -- released after the " +
            "upload --, {23} file(s) nothing drawn samples, header-checked only, largest decoded {11} px); build {12:F1} ms, " +
            "{13} KB of managed arrays allocated by the build (geometry {24} KB uploaded to the GPU, textures " +
            "{25} KB with mips); {14} group object(s), {15} mesh(es), {16} submesh(es) = " +
            "draw calls per camera render (the shadow and view-depth passes render the scene again), {17} material(s), " +
            "{18} Texture2D(s); managed heap {19:+#;-#;0} KB and {20} gen-0 collection(s) since the load began; " +
            "total {21:F1} ms",
            j.RootMs, j.RootParseMs, j.GroupFilesRequested, j.GroupBytes / 1024, j.GroupsDoneMs - j.GroupsStartMs,
            j.GroupParseCpuMs, j.Textures.Count, j.TextureBytes / 1024, j.TexturesDoneMs - j.TexturesStartMs,
            j.TextureDecodeCpuMs, decoded, largest, j.BuildMs, built.ManagedBytes / 1024, built.GroupObjects.Length,
            CountMeshes(built), built.Submeshes, CountMaterials(built), built.Textures.Length,
            (System.GC.GetTotalMemory(false) - j.HeapAtStart) / 1024, System.GC.CollectionCount(0) - j.Gen0AtStart,
            j.Clock.Elapsed.TotalMilliseconds, decodedBytes / 1024, headerOnly, built.GpuGeometryBytes / 1024,
            built.GpuTextureBytes / 1024));
    }

    static int CountMeshes(WmvRuntimeMapObject rt)
    {
        int n = 0;
        foreach (var m in rt.Meshes) if (m != null) n++;
        return n;
    }

    static int CountMaterials(WmvRuntimeMapObject rt)
    {
        int n = rt.FallbackMaterial != null ? 1 : 0;
        foreach (var m in rt.Materials) if (m != null) n++;
        return n;
    }

    /// <summary>Destroy the world model on screen (a model replacing it, or shutdown).</summary>
    void DisposeMapObject()
    {
        if (currentMapObject == null)
            return;
        currentMapObject.Dispose();
        currentMapObject = null;
        currentMapObjectFileDataID = 0;
    }

    /// <summary>
    /// The host's runtimeState question (protocol 4): what the player holds right now. A lifecycle step that
    /// ends on a model has no mapObjectLoaded to carry live counts, and only these can show a world model
    /// left alive under that model -- the next world-model adoption would dispose it before counting.
    /// Answered in message order on the main thread, so a question sent after an answer about a build sees
    /// that build adopted.
    /// </summary>
    void HandleRuntimeState(int query)
    {
        var r = new WmvIpcClient.RuntimeReport
        {
            LiveMapObjects = WmvRuntimeMapObject.Live,
            LiveModels = WmvRuntimeModel.Live,
            ModelFileDataID = currentSlot.Runtime != null ? currentSlot.FileDataID : 0,
            MapObjectFileDataID = currentMapObject != null ? currentMapObjectFileDataID : 0,
            Loading = job != null || wmoJob != null,
            // The mount the model on screen rides (protocol 5); a mount still being built for a load is not on screen.
            MountFileDataID = currentSlot.Runtime != null && mounted != null ? mounted.RiddenFileDataID : 0,
            MountKey = "",
            LiveMounts = WmvMountedScene.LiveMounts,
            MountsBuilt = WmvMountedScene.MountsBuilt,
            MountSeat = -1,
            MountSeatBone = -1,
            ModelSequence = currentSlot.Runtime != null && currentSlot.Runtime.Animator != null
                            ? currentSlot.Runtime.Animator.SequenceIndex : -1,
            MountSequence = -1,
            BodyRebinds = dresser != null ? dresser.BodyRebinds : 0,
            ViewFramings = ViewFramings,
        };
        WmvRuntimeModel mount = r.MountFileDataID != 0 ? mounted.Mount.Runtime : null;
        if (mount != null)
        {
            r.MountKey = mounted.Key;
            r.MountSeat = mounted.SeatCase;
            r.MountSeatBone = mounted.SeatBone;
            r.MountSequence = mount.Animator != null ? mount.Animator.SequenceIndex : -1;
            r.MountEmitters = mount.Emitters != null ? mount.Emitters.ParticleEmitterCount : 0;
            r.MountRibbons = mount.Emitters != null ? mount.Emitters.RibbonEmitterCount : 0;
            r.MountParticles = mount.Emitters != null ? mount.Emitters.LiveParticleCount : 0;
        }
        Debug.Log(string.Format("WMV: runtimeState {0}: live world models {1}, live models {2}, model {3}, world model {4}, " +
                                "mount {5}{6}", query, r.LiveMapObjects, r.LiveModels, r.ModelFileDataID, r.MapObjectFileDataID,
                                r.MountFileDataID, r.Loading ? ", a load in flight" : "") +
                  (mount == null && r.LiveMounts == 0 ? "" : string.Format(
                      " ({0}, {1} mount runtime(s) alive, {2} built; seat {3} on bone {4}; sequences {5} / {6}; emitters {7} " +
                      "particle + {8} ribbon, {9} live particle(s))", r.MountKey.Length > 0 ? r.MountKey : "none", r.LiveMounts,
                      r.MountsBuilt, r.MountSeat, r.MountSeatBone, r.ModelSequence, r.MountSequence, r.MountEmitters,
                      r.MountRibbons, r.MountParticles)) +
                  string.Format("; the view has been fitted {0} time(s)", r.ViewFramings));
        ipc.ReportRuntimeState(query, r);
    }
}
