// WmvMain.cs
//
// Bootstrap and load orchestration for the WMV Unity viewport player -- WMV's new renderer
// foundation and intended primary viewport (the OpenGL canvas is the legacy/fallback viewport
// during the migration; see docs/unity-renderer/README.md).
//
// Add this component to one empty GameObject in an otherwise-empty scene; at runtime it builds
// the camera rig, a light, the status overlay and the IPC client that connects back to WMV.
//
// LOAD PIPELINE (all bytes arrive over IPC; nothing is read from or written to disk):
//
//   loadWoWModel(path, fileDataID)
//     -> getAsset(path)                     the .m2 itself
//     -> M2Parser                           header, vertices, textures, materials, SFID/TXID
//     -> getAssetByFileDataID(SFID[0])      the .skin profile (LOD 0)
//     -> M2SkinParser                       lookup, triangles, submeshes, batches
//     -> textures: TXID entry when the M2 names one, otherwise getModelTextures so WMV can
//        resolve the replaceable creature skin from the client database
//     -> getAssetByFileDataID(texture)      the .blp
//     -> BlpDecoder                         RGBA32 in memory
//     -> WmvModelBuilder                    Mesh + Materials + Texture2D + GameObject
//     -> frame the camera on the mesh bounds
//
// This milestone renders a STATIC model: bone data is parsed and preserved but no animation,
// no skinning, no attachments, no particles.

using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow;

public class WmvMain : MonoBehaviour
{
    WmvIpcClient ipc;
    GameObject placeholder;
    WmvStatusOverlay status;
    WmvOrbitCamera orbit;

    WmvRuntimeModel current;          // the model on screen (disposed when replaced)

    /// <summary>
    /// The .m2 bytes of the model on screen. Kept because only ONE animation's keyframes are
    /// parsed at a time -- a track on disk is an array of per-sequence arrays -- so following the
    /// app to a different animation means parsing these again with a different sequence in mind.
    /// A creature .m2 is a few hundred KB; re-fetching it over IPC for every dropdown change
    /// would cost a round-trip to save that.
    /// </summary>
    byte[] currentM2Bytes;

    /// <summary>
    /// The sequence the app says it is showing, or -1 when it has not said. Remembered because the
    /// push can arrive before the model finishes loading -- it is sent right after loadWoWModel --
    /// and then it decides which animation the model is built playing, rather than the model
    /// starting on its own idle and being corrected a frame later.
    /// </summary>
    int selectedSequence = -1;
    LoadJob job;                      // in-flight load, if any

    // Kept after a successful load so the SKIN can change without reloading anything. A creature
    // normally has several skins -- chicken2 has seven -- and WMV lets the user pick among them;
    // when they do, only the textures behind the existing materials need re-uploading.
    M2ParsedModel currentModel;
    string currentName = "WoWModel";
    int currentFileDataID;
    readonly Dictionary<int, BlpImage> currentTextures = new Dictionary<int, BlpImage>();
    readonly Dictionary<int, int> currentTextureIds = new Dictionary<int, int>();   // slot -> FileDataID
    SkinJob skinJob;                  // in-flight skin change, if any

    /// <summary>
    /// Bone tracks already read, keyed by sequence index.
    ///
    /// Reading a sequence's keyframes allocates its track arrays, and doing that again every time
    /// the user returns to an animation they have already watched is both wasted work and -- more
    /// to the point -- wasted garbage, which is what a switch is felt as. Cached, going back to a
    /// sequence costs one dictionary lookup and no allocation at all.
    ///
    /// Bounded by what the user actually plays, and strictly lighter than the legacy viewport,
    /// which reads EVERY sequence's tracks at load and holds them for the model's lifetime.
    /// </summary>
    readonly Dictionary<int, M2BoneDef[]> boneTrackCache = new Dictionary<int, M2BoneDef[]>();

    /// <summary>
    /// External .anim files already fetched, keyed by FileDataID. A sequence whose keyframes are
    /// not in the .m2 needs its .anim bytes; fetching them again on every visit would put a
    /// network round trip in front of an animation the renderer already has.
    /// </summary>
    readonly Dictionary<int, byte[]> animFileCache = new Dictionary<int, byte[]>();

    /// <summary>The .anim fetch in flight, if any: requestId -> the sequence waiting on it.</summary>
    readonly Dictionary<string, int> pendingAnimFetch = new Dictionary<string, int>();

    /// <summary>
    /// The last playback state the app sent, whether or not it could be applied when it arrived.
    ///
    /// A state message names the sequence it is about, and it used to be dropped whenever the
    /// renderer was not already on that sequence. That is exactly the moment it matters most: a
    /// selection that fell back to the idle, or one still waiting on its .anim file, would lose
    /// the app's play/pause, speed and position entirely and keep whatever the previous animation
    /// happened to be doing. Kept here, it can be applied to whatever ends up playing.
    /// </summary>
    WmvIpcClient.AnimationState lastAppState;
    bool haveAppState;

    /// <summary>
    /// The geoset numbers the displayed creature variant switches on, or null when the host has
    /// not reported any. Two variants of the same creature can differ by GEOMETRY rather than
    /// texture -- one horse's mane instead of another -- and this is what decides which submeshes
    /// are drawn. See WmvModelBuilder.GeosetVisible.
    /// </summary>
    HashSet<int> currentGeosets;

    /// <summary>Textures still on their way for a skin change. Requests are keyed to the M2
    /// texture slot they will land in.</summary>
    class SkinJob
    {
        public readonly Dictionary<string, int> Pending = new Dictionary<string, int>();
        public int Applied;
    }

    /// <summary>State for one in-flight model load.</summary>
    class LoadJob
    {
        public string Path;
        public int FileDataID;
        public System.Diagnostics.Stopwatch Clock = System.Diagnostics.Stopwatch.StartNew();
        public long M2Ms, SkinMs, TextureMs, ParseMs, BuildMs;

        public byte[] M2Bytes;
        public M2ParsedModel Model;
        public M2ParsedSkin Skin;
        public readonly Dictionary<int, BlpImage> Textures = new Dictionary<int, BlpImage>();

        public string PendingM2, PendingSkin, PendingTextureList;
        public readonly Dictionary<string, int> PendingTextures = new Dictionary<string, int>(); // requestId -> slot
        public int TexturesExpected;
    }

    void Awake()
    {
        // The player is embedded in a host app whose window normally has focus; without this it
        // would pause immediately. (Also set Run In Background in Player Settings.)
        Application.runInBackground = true;

        // Every Debug.Log otherwise walks and formats a full managed stack trace before writing
        // the line -- for an ORDINARY log, on the main thread. This renderer logs what it decided
        // about every batch, material, skin and animation, so that cost lands in the middle of the
        // work it is describing. The traces stay on for warnings and errors, where something has
        // actually gone wrong and the trace is the point.
        Application.SetStackTraceLogType(LogType.Log, StackTraceLogType.None);

        var cam = Camera.main;
        if (cam == null)
        {
            var camGo = new GameObject("Main Camera");
            cam = camGo.AddComponent<Camera>();
            camGo.tag = "MainCamera";
        }
        cam.clearFlags = CameraClearFlags.SolidColor;
        cam.backgroundColor = new Color(0.10f, 0.10f, 0.12f);
        cam.nearClipPlane = 0.01f;
        orbit = cam.gameObject.GetComponent<WmvOrbitCamera>() ?? cam.gameObject.AddComponent<WmvOrbitCamera>();

        var lightGo = new GameObject("Directional Light");
        var light = lightGo.AddComponent<Light>();
        light.type = LightType.Directional;
        light.intensity = 1.1f;
        lightGo.transform.rotation = Quaternion.Euler(50f, -30f, 0f);
        RenderSettings.ambientLight = new Color(0.35f, 0.35f, 0.4f);

        // Proof-of-life until a real model arrives -- OFF unless asked for (-wmvPlaceholder).
        // It answered "is the embedded player alive?", which stopped being the open question a
        // long time ago; what is left is a grey box spinning in the middle of the viewer before
        // the user has chosen anything. An empty viewport should look empty.
        //
        // Its default material comes from the built-in resources, which a player build may have
        // stripped (that is what makes an untouched primitive render magenta), so it still gets
        // the shader the model builder resolved rather than rendering magenta when it IS asked for.
        if (WmvModelBuilder.Debug_.Placeholder)
        {
            placeholder = GameObject.CreatePrimitive(PrimitiveType.Cube);
            placeholder.name = "WMV Placeholder";
            placeholder.AddComponent<WmvSpin>();
            var placeholderShader = WmvModelBuilder.ResolveShader(s => Debug.Log("WMV: " + s));
            if (placeholderShader != null)
                placeholder.GetComponent<MeshRenderer>().material = new Material(placeholderShader);
        }

        status = gameObject.AddComponent<WmvStatusOverlay>();
        status.Set("Starting ...");

        ipc = gameObject.AddComponent<WmvIpcClient>();
        ipc.OnStatus = s => status.Set(s);
        ipc.OnLoadWoWModel = HandleLoadWoWModel;
        ipc.OnAssetResponse = HandleAssetResponse;
        ipc.OnModelTextures = HandleModelTextures;
        ipc.OnModelSkin = HandleModelSkin;
        ipc.OnModelAnimation = HandleModelAnimation;
        ipc.OnModelAnimationState = HandleModelAnimationState;
    }

    // ---------------------------------------------------------------- load pipeline

    void HandleLoadWoWModel(string path, int fileDataID, string client)
    {
        status.Set("Active client received (" + client + ")");
        if (string.IsNullOrEmpty(path) && fileDataID <= 0)
        {
            status.Set("loadWoWModel without path or fileDataID -- ignored");
            return;
        }

        // A sequence index means nothing across models -- entry 14 is a different animation in
        // each -- so forget the previous one. The app pushes its selection for the NEW model right
        // after this message, so the value is refilled before the .m2 arrives to be parsed.
        selectedSequence = -1;
        currentM2Bytes = null;
        // Sequence indices and file ids mean nothing across models.
        boneTrackCache.Clear();
        animFileCache.Clear();
        pendingAnimFetch.Clear();
        haveAppState = false;

        job = new LoadJob { Path = path, FileDataID = fileDataID };
        status.Set("Requested " + (string.IsNullOrEmpty(path) ? ("fileDataID " + fileDataID) : path));
        job.PendingM2 = string.IsNullOrEmpty(path)
            ? ipc.RequestAssetByFileDataID(fileDataID)
            : ipc.RequestAsset(path);
    }

    void HandleAssetResponse(WmvIpcClient.AssetResponse r)
    {
        // A skin change is answered by the same assetResponse messages as a load, so claim ours
        // before the load path sees them.
        if (skinJob != null && skinJob.Pending.ContainsKey(r.requestId))
        {
            OnSkinTextureBytes(r);
            return;
        }
        // An external .anim answer belongs to an animation change, not to a load.
        int waitingSequence;
        if (pendingAnimFetch.TryGetValue(r.requestId, out waitingSequence))
        {
            OnAnimFileBytes(r, waitingSequence);
            return;
        }
        if (job == null)
            return;

        if (!r.ok)
        {
            // Attribute the failure to the stage that asked for it.
            if (r.requestId == job.PendingM2) Fail("M2 request failed: " + r.error);
            else if (r.requestId == job.PendingSkin) Fail("skin request failed: " + r.error);
            else if (job.PendingTextures.ContainsKey(r.requestId)) TextureFailed(r.requestId, r.error);
            else Debug.LogWarning("WMV: unexpected failed response " + r.requestId + ": " + r.error);
            return;
        }

        if (r.requestId == job.PendingM2) OnM2Bytes(r);
        else if (r.requestId == job.PendingSkin) OnSkinBytes(r);
        else if (job.PendingTextures.ContainsKey(r.requestId)) OnTextureBytes(r);
    }

    void OnM2Bytes(WmvIpcClient.AssetResponse r)
    {
        job.M2Ms = job.Clock.ElapsedMilliseconds;
        status.Set("Received " + r.byteLength + " bytes for " + job.Path);
        try
        {
            long t0 = job.Clock.ElapsedMilliseconds;
            job.M2Bytes = r.data;
            job.Model = M2Parser.Parse(r.data, selectedSequence);
            job.ParseMs += job.Clock.ElapsedMilliseconds - t0;
            if (job.FileDataID <= 0) job.FileDataID = r.fileDataID;
        }
        catch (WowParseException e) { Fail("M2 parse failed: " + e.Message); return; }

        if (job.Model.SkinFileDataIDs.Length == 0)
        {
            Fail("model has no skin profile (SFID chunk missing) -- nothing to render");
            return;
        }
        // SFID[0] is the highest-detail profile.
        job.PendingSkin = ipc.RequestAssetByFileDataID(job.Model.SkinFileDataIDs[0]);
    }

    void OnSkinBytes(WmvIpcClient.AssetResponse r)
    {
        job.SkinMs = job.Clock.ElapsedMilliseconds - job.M2Ms;
        try
        {
            long t0 = job.Clock.ElapsedMilliseconds;
            job.Skin = M2SkinParser.Parse(r.data);
            job.ParseMs += job.Clock.ElapsedMilliseconds - t0;
        }
        catch (WowParseException e) { Fail("skin parse failed: " + e.Message); return; }

        RequestTextures();
    }

    /// <summary>
    /// Textures the M2 names itself (TXID) are fetched directly; replaceable ones (creature
    /// skins) have no id in the file, so WMV resolves them from the client database.
    /// </summary>
    void RequestTextures()
    {
        var direct = new List<KeyValuePair<int, int>>();   // slot -> fileDataID
        bool needsHost = false;
        for (int i = 0; i < job.Model.Textures.Length; i++)
        {
            var t = job.Model.Textures[i];
            if (t.FileDataID > 0)
            {
                direct.Add(new KeyValuePair<int, int>(i, t.FileDataID));
                currentTextureIds[i] = t.FileDataID;   // the M2 named this one itself
            }
            else if (t.IsReplaceable) needsHost = true;
        }

        job.TexturesExpected = direct.Count;
        foreach (var d in direct)
            job.PendingTextures[ipc.RequestAssetByFileDataID(d.Value)] = d.Key;

        if (needsHost)
            job.PendingTextureList = ipc.RequestModelTextures(job.FileDataID);
        else if (direct.Count == 0)
            BuildIfReady();
    }

    void HandleModelTextures(WmvIpcClient.ModelTexturesResponse r)
    {
        if (job == null || r.requestId != job.PendingTextureList)
            return;
        job.PendingTextureList = null;
        AdoptGeosets(r);

        if (!r.ok || r.textures.Length == 0)
        {
            // Not fatal: the mesh still renders, untextured, and the reason is visible.
            status.Set("No texture resolved (" + (r.error ?? "none") + ") -- rendering untextured");
            BuildIfReady();
            return;
        }

        foreach (var t in r.textures)
        {
            if (t.fileDataID <= 0) continue;
            foreach (int slot in SlotsForTexture(job.Model, t))
            {
                job.TexturesExpected++;
                job.PendingTextures[ipc.RequestAssetByFileDataID(t.fileDataID)] = slot;
                currentTextureIds[slot] = t.fileDataID;
                Debug.Log("WMV: texture slot " + slot + " (type " + t.type + ") -> fileDataID " +
                          t.fileDataID + " (" + t.source + ")");
            }
        }
        if (job.TexturesExpected == 0)
            BuildIfReady();
    }

    void OnTextureBytes(WmvIpcClient.AssetResponse r)
    {
        int slot = job.PendingTextures[r.requestId];
        job.PendingTextures.Remove(r.requestId);
        long t0 = job.Clock.ElapsedMilliseconds;
        try
        {
            BlpImage img = BlpDecoder.Decode(r.data);
            job.Textures[slot] = img;
            Debug.Log("WMV: decoded texture slot " + slot + ": " + img.Width + "x" + img.Height +
                      " " + img.Encoding + " (" + img.Rgba.Length + " bytes RGBA)");
        }
        catch (WowParseException e)
        {
            status.Set("Texture decode failed: " + e.Message);   // keep going: mesh without texture
        }
        job.TextureMs += job.Clock.ElapsedMilliseconds - t0;
        BuildIfReady();
    }

    void TextureFailed(string requestId, string error)
    {
        job.PendingTextures.Remove(requestId);
        status.Set("Texture request failed: " + error);
        BuildIfReady();
    }

    void BuildIfReady()
    {
        if (job == null || job.Model == null || job.Skin == null) return;
        if (job.PendingTextureList != null || job.PendingTextures.Count > 0) return;

        try
        {
            long t0 = job.Clock.ElapsedMilliseconds;
            var built = WmvModelBuilder.Build(job.Model, job.Skin, job.Textures,
                                              string.IsNullOrEmpty(job.Model.Name) ? "WoWModel" : job.Model.Name,
                                              s => Debug.LogWarning("WMV: " + s),
                                              currentGeosets);
            job.BuildMs = job.Clock.ElapsedMilliseconds - t0;

            if (current != null) current.Dispose();      // never leak the previous model
            current = built;
            if (placeholder != null) placeholder.SetActive(false);

            // Keep what a later skin change needs: the parsed model (to map a texture type onto
            // slots) and the decoded textures (so untouched slots are not re-fetched).
            currentModel = job.Model;
            currentM2Bytes = job.M2Bytes;
            currentName = string.IsNullOrEmpty(job.Model.Name) ? "WoWModel" : job.Model.Name;
            currentFileDataID = job.FileDataID;
            currentTextures.Clear();
            foreach (var kv in job.Textures) currentTextures[kv.Key] = kv.Value;
            skinJob = null;

            // Ask for this creature's .anim files now rather than when one is first played, so no
            // animation switch ever waits on a round trip. They arrive while the user is looking
            // at the model, not while they are waiting for the animation they just picked.
            PrefetchAnimFiles();

            orbit.Frame(built.Bounds);

            status.Set("Loaded " + job.Path);
            status.Set(string.Format("Vertices {0}  Triangles {1}  Submeshes {2}  Textures {3}",
                                     built.VertexCount, built.TriangleCount, built.SubmeshCount, job.Textures.Count));
            if (currentGeosets != null)
                status.Set("Geosets " + (currentGeosets.Count == 0 ? "none" : string.Join(",",
                           new List<int>(currentGeosets).ConvertAll(x => x.ToString()).ToArray())));
            status.Set(string.Format("Bounds {0} size {1}", built.Bounds.center, built.Bounds.size));
            status.Set(string.Format("Load {0} ms (m2 {1}, skin {2}, tex {3}, parse {4}, build {5})",
                                     job.Clock.ElapsedMilliseconds, job.M2Ms, job.SkinMs, job.TextureMs,
                                     job.ParseMs, job.BuildMs));
            Debug.Log(string.Format(
                "WMV: loaded {0} -- {1} vertices, {2} triangles, {3} submeshes, {4} texture(s), bounds {5}, {6} ms",
                job.Path, built.VertexCount, built.TriangleCount, built.SubmeshCount, job.Textures.Count,
                built.Bounds, job.Clock.ElapsedMilliseconds));
        }
        catch (WowParseException e) { Fail("mesh creation failed: " + e.Message); }
        finally { job = null; }
    }

    /// <summary>
    /// WMV's displayed animation changed. The renderer draws the same model from the same data, so
    /// it plays what the app plays rather than the idle it would pick for itself.
    ///
    /// Only one sequence's keyframes are held at a time, so this re-parses the .m2 kept from the
    /// load with the new sequence in mind and re-binds the animator to the result. Nothing else
    /// moves: the mesh, its materials, its textures and its geoset selection are all untouched by
    /// which animation is playing.
    /// </summary>
    void HandleModelAnimation(WmvIpcClient.AnimationSelection a)
    {
        if (a.fileDataID != 0 && currentFileDataID != 0 && a.fileDataID != currentFileDataID)
            return;                                     // about a different model
        if (a.sequenceIndex < 0)
            return;

        bool changed = a.sequenceIndex != selectedSequence;
        selectedSequence = a.sequenceIndex;

        if (current == null || currentM2Bytes == null)
        {
            // Still loading. The parse below will use this selection when it gets there.
            status.Set("Animation " + a.sequenceIndex + " selected (model still loading)");
            return;
        }
        if (!changed && current.Animator != null)
        {
            status.Set("Animation unchanged");
            return;
        }

        SwitchToSequence(a.sequenceIndex);
    }

    /// <summary>
    /// Show a different animation of the model already loaded.
    ///
    /// NOTHING is rebuilt here. The .m2 is not re-requested or re-parsed, the mesh, materials,
    /// textures, geoset selection and skeleton are untouched, and the animator is re-bound to the
    /// new tracks rather than recreated. Only loadWoWModel builds anything.
    ///
    /// Three ways this can go, cheapest first:
    ///   1. the sequence has been played before  -> its tracks come from the cache, no allocation
    ///   2. its keyframes are in the .m2         -> read them, cache them
    ///   3. its keyframes are in a .anim file    -> fetch that once, then as (2)
    /// The previous animation stays on screen throughout, including while a fetch is in flight.
    /// </summary>
    void SwitchToSequence(int sequenceIndex)
    {
        // -wmvNoAnim means nothing is going to be played, so nothing is worth reading or -- more
        // to the point -- fetching. ApplySequence would refuse this anyway, but only after a .anim
        // round trip had already been spent on a sequence that will not move a bone.
        if (WmvModelBuilder.Debug_.NoAnim)
            return;

        // 1. Already read once. This becomes the common case as soon as the user goes back and
        //    forth between a few animations, and it is deliberately the cheapest path there is:
        //    no parse, no fetch, no allocation.
        M2BoneDef[] cached;
        if (boneTrackCache.TryGetValue(sequenceIndex, out cached))
        {
            long heapBefore = System.GC.GetTotalMemory(false);
            int gcBefore = System.GC.CollectionCount(0);
            var swc = System.Diagnostics.Stopwatch.StartNew();
            currentModel.Bones = cached;
            currentModel.AnimatedSequence = sequenceIndex;
            currentModel.AnimationSkipReason = null;
            ApplyResolvedSequence(sequenceIndex, "cached");
            if (WmvModelBuilder.Debug_.AnimCheck)
                Debug.Log(string.Format(
                    "WMV: anim switch timing (cached): read 0 ms, total {0} ms, allocated {1} KB, "
                    + "gen0 collections {2} (no reload, no fetch, no parse)",
                    swc.ElapsedMilliseconds,
                    (System.GC.GetTotalMemory(false) - heapBefore) / 1024,
                    System.GC.CollectionCount(0) - gcBefore));
            return;
        }

        // 3. Keys in a .anim file. Fetch it once; the switch completes when the bytes arrive.
        int animFileId = M2Parser.ExternalAnimFileId(currentModel, sequenceIndex);
        byte[] external = null;
        if (animFileId != 0 && !animFileCache.TryGetValue(animFileId, out external))
        {
            foreach (int waiting in pendingAnimFetch.Values)
                if (waiting == sequenceIndex)
                    return;                          // already on its way
            string req = ipc.RequestAssetByFileDataID(animFileId);
            pendingAnimFetch[req] = sequenceIndex;
            status.Set(string.Format("Animation {0}: fetching its .anim file ({1})",
                                     sequenceIndex, animFileId));
            return;
        }

        ReadAndApplySequence(sequenceIndex, external, animFileId == 0 ? "in-file" : "from .anim");
    }

    /// <summary>
    /// Fetch every external .anim file this model names, as soon as it is built.
    ///
    /// A sequence whose keyframes are in a .anim used to fetch them the first time it was played,
    /// which meant the FIRST switch to each such animation was deferred: the previous animation
    /// stayed on screen until the bytes landed. Measured at 16-18 ms each -- about a frame, so not
    /// a stall in itself, but it is the one part of a switch that is not instant, and there is no
    /// reason for it to be on the interactive path at all. A creature names a handful of these
    /// (Agronn 8, ~300 KB in total) and they are what its animations ARE.
    ///
    /// Everything else about it is unchanged: the bytes land in the same cache the on-demand path
    /// fills, a sequence still falls back gracefully if its file never arrives, and nothing is
    /// parsed until an animation actually asks for it.
    /// </summary>
    void PrefetchAnimFiles()
    {
        if (currentModel == null || WmvModelBuilder.Debug_.NoAnim)
            return;
        var wanted = new HashSet<int>();
        foreach (var e in currentModel.AnimFileIds)
            if (e.FileDataID > 0)
                wanted.Add(e.FileDataID);
        if (wanted.Count == 0)
            return;
        foreach (int fileId in wanted)
        {
            if (animFileCache.ContainsKey(fileId))
                continue;
            string req = ipc.RequestAssetByFileDataID(fileId);
            pendingAnimFetch[req] = -1;          // -1: nothing is waiting on it, just fill the cache
        }
        Debug.Log(string.Format("WMV: anim: fetching {0} .anim file(s) up front so switching to "
                                + "one never waits", wanted.Count));
    }

    /// <summary>The .anim bytes arrived. Cache them and finish the switch that was waiting.</summary>
    void OnAnimFileBytes(WmvIpcClient.AssetResponse r, int sequenceIndex)
    {
        pendingAnimFetch.Remove(r.requestId);
        if (sequenceIndex < 0 && (!r.ok || r.data == null))
        {
            pendingAnimFetch.Remove(r.requestId);
            return;                              // a prefetch that failed; the switch will retry
        }
        if (!r.ok || r.data == null || r.data.Length == 0)
        {
            // Graceful for the viewer -- the previous animation keeps running rather than the
            // model dropping to its rest pose -- but loud for whoever has to fix it. LogWarning
            // rather than a status line: ordinary logs no longer carry a stack trace, and this is
            // exactly the kind of thing worth having one for.
            Debug.LogWarning(string.Format(
                "WMV: anim: sequence {0} wanted .anim file {1}, which could not be read: {2}",
                sequenceIndex, M2Parser.ExternalAnimFileId(currentModel, sequenceIndex),
                r.error ?? "empty response"));
            status.Set(string.Format("Animation {0}: its .anim file could not be read",
                                     sequenceIndex));
            return;
        }
        if (sequenceIndex < 0)
        {
            // A prefetch: nothing is waiting on it, it just belongs in the cache. The reply names
            // the file, so it can be filed without a sequence to look it up from.
            animFileCache[r.fileDataID] = r.data;
            return;
        }
        int animFileId = M2Parser.ExternalAnimFileId(currentModel, sequenceIndex);
        if (animFileId != 0)
            animFileCache[animFileId] = r.data;
        // Only apply if this is still what the app wants: the user may have moved on while the
        // bytes were in flight.
        if (sequenceIndex != selectedSequence)
            return;
        ReadAndApplySequence(sequenceIndex, r.data, "from .anim");
    }

    /// <summary>Read one sequence's bone tracks, cache them, and put them on screen.</summary>
    void ReadAndApplySequence(int sequenceIndex, byte[] external, string source)
    {
        try
        {
            // Time AND bytes. The time is what the switch costs now; the allocation is what it
            // costs a frame or two later, when the collector runs -- and it is the collector, not
            // the reading, that a viewer feels as a stutter.
            long heapBefore = System.GC.GetTotalMemory(false);
            int gcBefore = System.GC.CollectionCount(0);
            var sw = System.Diagnostics.Stopwatch.StartNew();
            M2Parser.ReadAnimationInto(currentM2Bytes, sequenceIndex, currentModel, external);
            long readMs = sw.ElapsedMilliseconds;

            // Cache under the sequence that RESOLVED, not the one asked for: a request that fell
            // back to the idle must not be remembered as though it had played.
            if (currentModel.AnimatedSequence >= 0)
                boneTrackCache[currentModel.AnimatedSequence] = currentModel.Bones;

            ApplyResolvedSequence(sequenceIndex, source);
            if (WmvModelBuilder.Debug_.AnimCheck)
                Debug.Log(string.Format(
                    "WMV: anim switch timing ({0}): read {1} ms, total {2} ms, allocated {3} KB, "
                    + "gen0 collections {4} (no reload, no mesh/material/texture rebuild)",
                    source, readMs, sw.ElapsedMilliseconds,
                    (System.GC.GetTotalMemory(false) - heapBefore) / 1024,
                    System.GC.CollectionCount(0) - gcBefore));
        }
        catch (WowParseException e)
        {
            Debug.LogWarning("WMV: anim: reading sequence " + sequenceIndex + " (" + source +
                             ") failed: " + e.Message);
            status.Set("Animation change failed: " + e.Message);
        }
    }

    /// <summary>Bind whatever currentModel now holds, and say what is actually playing.</summary>
    void ApplyResolvedSequence(int requested, string source)
    {
        if (!WmvModelBuilder.ApplySequence(current, currentModel, s => Debug.Log("WMV: " + s)))
        {
            status.Set("Animation " + requested + " could not be played");
            return;
        }
        // Report what is PLAYING, not what was asked for: a sequence whose keyframes cannot be
        // read falls back to the idle, and saying "animation 20" while the idle plays is the kind
        // of log that costs an hour later.
        int playing = currentModel.AnimatedSequence;
        status.Set(string.Format("Animation {0} (animID {1}, {2} ms, {3}){4}",
                                 playing, currentModel.Sequences[playing].AnimId,
                                 currentModel.Sequences[playing].Length, source,
                                 playing == requested ? "" : " -- fell back from " + requested));
        if (playing != requested && currentModel.AnimationSkipReason != null)
            Debug.Log("WMV: anim: " + currentModel.AnimationSkipReason);

        // The app's playback state applies to whatever is now on screen. Without this a switch
        // starts from the animator's own defaults -- playing, at 1x -- which is wrong whenever the
        // app is paused or the speed slider is not at 1, and is not corrected until the next
        // heartbeat. The heartbeat is meant to correct DRIFT, not to start the animation.
        if (haveAppState && current.Animator != null)
            current.Animator.SetPlaybackState(lastAppState.playing,
                                              lastAppState.sequenceIndex == playing
                                                  ? lastAppState.timeMs : 0f,
                                              lastAppState.speed);
        // Watch whether it actually starts moving; see WmvM2Animator.BeginAdvanceWatch.
        if (current.Animator != null)
            current.Animator.BeginAdvanceWatch();
    }

    /// <summary>
    /// WMV's playback state changed, or the heartbeat arrived. Hand it to the animator, which
    /// decides what to do with the time.
    ///
    /// Nothing here rebuilds or re-parses anything: play/pause, speed and position are the app's
    /// state, and the renderer already has everything needed to act on them. A state message for a
    /// sequence the renderer is not playing is ignored rather than acted on -- the selection push
    /// that switches to it will arrive with its own state.
    /// </summary>
    void HandleModelAnimationState(WmvIpcClient.AnimationState s)
    {
        if (s.fileDataID != 0 && currentFileDataID != 0 && s.fileDataID != currentFileDataID)
            return;                                     // about a different model
        if (current == null || current.Animator == null)
            return;                                     // nothing playing to apply it to
        // Remember it even when it cannot be applied yet -- a deferred or fallen-back switch
        // will ask for it as soon as it knows what is playing.
        lastAppState = s;
        haveAppState = true;

        if (s.sequenceIndex >= 0 && current.Animator.SequenceIndex != s.sequenceIndex)
        {
            // Not about what is on screen. The play/pause and speed still are, though: they are
            // the app's, not the sequence's, and dropping them here is what left a switch running
            // at the wrong speed or moving while the app was paused.
            current.Animator.SetTransportOnly(s.playing, s.speed);
            return;
        }

        bool wasPlaying = current.Animator.IsPlaying;
        float wasSpeed = current.Animator.Speed;
        current.Animator.SetPlaybackState(s.playing, s.timeMs, s.speed);

        // Only the changes worth reading are surfaced: the heartbeat would otherwise write a line
        // a second for the whole session.
        if (s.playing != wasPlaying)
            status.Set(s.playing ? "Playing" : "Paused");
        else if (System.Math.Abs(s.speed - wasSpeed) > 0.001f)
            status.Set(string.Format("Speed {0:0.##}x", s.speed));
    }

    /// <summary>
    /// WMV's displayed skin changed. Fetch whatever textures that actually changes and re-bind
    /// them onto the materials already on screen -- the mesh is unaffected by which image its
    /// materials sample.
    /// </summary>
    void HandleModelSkin(WmvIpcClient.ModelTexturesResponse r)
    {
        if (current == null || currentModel == null)
            return;                                     // nothing built yet; the load will pick it up
        if (r.fileDataID != 0 && currentFileDataID != 0 && r.fileDataID != currentFileDataID)
            return;                                     // about a different model
        if (!r.ok || r.textures.Length == 0)
            return;

        // Geometry first: a variant can change which submeshes are drawn as well as which texture
        // they wear, and the two are independent -- a variant that only swaps geosets has no
        // texture to fetch and would otherwise be dropped by the no-op check below.
        if (AdoptGeosets(r))
        {
            WmvModelBuilder.ApplyGeosets(current, currentGeosets, s => Debug.Log("WMV: " + s));
            status.Set(string.Format("Geosets applied ({0} triangles, mesh unchanged)",
                                     current.TriangleCount));
        }

        var wanted = new Dictionary<int, int>();         // slot -> FileDataID
        foreach (var t in r.textures)
        {
            if (t.fileDataID <= 0) continue;
            foreach (int slot in SlotsForTexture(currentModel, t))
                wanted[slot] = t.fileDataID;
        }

        var fetch = new List<KeyValuePair<int, int>>();
        foreach (var kv in wanted)
        {
            int have;
            if (currentTextureIds.TryGetValue(kv.Key, out have) && have == kv.Value &&
                currentTextures.ContainsKey(kv.Key))
                continue;                                // this slot already holds that texture
            fetch.Add(kv);
        }

        if (fetch.Count == 0)
        {
            status.Set("Skin unchanged");
            return;
        }

        skinJob = new SkinJob();
        foreach (var kv in fetch)
        {
            skinJob.Pending[ipc.RequestAssetByFileDataID(kv.Value)] = kv.Key;
            currentTextureIds[kv.Key] = kv.Value;
            Debug.Log("WMV: skin change -> slot " + kv.Key + " becomes fileDataID " + kv.Value);
        }
        status.Set("Skin changed (" + fetch.Count + " texture(s))");
    }

    void OnSkinTextureBytes(WmvIpcClient.AssetResponse r)
    {
        int slot = skinJob.Pending[r.requestId];
        skinJob.Pending.Remove(r.requestId);

        if (!r.ok)
        {
            status.Set("Skin texture request failed: " + r.error);
        }
        else
        {
            try
            {
                currentTextures[slot] = BlpDecoder.Decode(r.data);
                skinJob.Applied++;
                Debug.Log("WMV: skin texture slot " + slot + ": " + currentTextures[slot].Width + "x" +
                          currentTextures[slot].Height + " " + currentTextures[slot].Encoding);
            }
            catch (WowParseException e)
            {
                status.Set("Skin texture decode failed: " + e.Message);   // keep the old texture
            }
        }

        if (skinJob.Pending.Count > 0)
            return;

        if (skinJob.Applied > 0)
        {
            WmvModelBuilder.RebindTextures(current, currentTextures, currentName,
                                           s => Debug.Log("WMV: " + s));
            status.Set("Skin applied (" + skinJob.Applied + " texture(s), mesh unchanged)");
        }
        skinJob = null;
    }

    /// <summary>
    /// Which M2 texture slots a resolved texture feeds. The host names a texture TYPE (11, 12, 13
    /// for the three creature skin slots) rather than a position, because a model's
    /// texture-variation order and its M2 texture-slot order need not agree. Falls back to the
    /// positional index when the host did not say -- an older host, or a texture with no type.
    /// </summary>
    static List<int> SlotsForTexture(M2ParsedModel model, WmvIpcClient.ModelTextureRef t)
    {
        var slots = new List<int>();
        if (model != null && t.type > 0)
        {
            for (int i = 0; i < model.Textures.Length; i++)
                if ((int)model.Textures[i].Type == t.type)
                    slots.Add(i);
        }
        if (slots.Count == 0 && t.index >= 0 &&
            (model == null || t.index < model.Textures.Length))
            slots.Add(t.index);
        return slots;
    }

    /// <summary>
    /// Take the geoset set out of a host message, if it reported one. Returns true when the set
    /// actually CHANGED, so the caller only touches the mesh when there is something to do.
    /// A message with hasGeosets false is silence, not an empty answer: the host had no creature
    /// selection to report, and whatever is on screen stays.
    /// </summary>
    bool AdoptGeosets(WmvIpcClient.ModelTexturesResponse r)
    {
        if (!r.hasGeosets)
            return false;

        var next = new HashSet<int>();
        foreach (int g in r.geosets)
            next.Add(g);

        if (currentGeosets != null && currentGeosets.Count == next.Count)
        {
            bool same = true;
            foreach (int g in next)
                if (!currentGeosets.Contains(g)) { same = false; break; }
            if (same)
                return false;
        }

        currentGeosets = next;
        return true;
    }

    void Fail(string reason)
    {
        status.Set("FAILED: " + reason);
        Debug.LogError("WMV: " + reason);
        job = null;
    }

    void OnDestroy()
    {
        if (current != null) current.Dispose();
    }
}

// Slow idle spin so the placeholder scene is visibly "live".
public class WmvSpin : MonoBehaviour
{
    void Update() { transform.Rotate(0f, 40f * Time.deltaTime, 0f); }
}

// Top-left status lines drawn with the immediate-mode GUI -- no scene/canvas setup needed.
public class WmvStatusOverlay : MonoBehaviour
{
    const int MaxLines = 8;
    readonly System.Collections.Generic.List<string> lines = new System.Collections.Generic.List<string>();
    GUIStyle style;

    public void Set(string line)
    {
        lines.Add(line);
        while (lines.Count > MaxLines) lines.RemoveAt(0);
        Debug.Log("WMV status: " + line);
    }

    void OnGUI()
    {
        // Silent unless asked for: see WmvModelBuilder.Debug_.Overlay. Set() still logs, so a run
        // can be read afterwards without the viewport having been written on during it.
        if (!WmvModelBuilder.Debug_.Overlay)
            return;

        if (style == null)
        {
            style = new GUIStyle(GUI.skin.label) { fontSize = 14, richText = false };
            style.normal.textColor = new Color(0.92f, 0.92f, 0.95f);
        }
        GUI.Label(new Rect(10, 8, Screen.width - 20, 22), "WMV Unity Renderer  (static M2 rendering)", style);
        for (int i = 0; i < lines.Count; i++)
            GUI.Label(new Rect(10, 30 + i * 20, Screen.width - 20, 22), lines[i], style);
    }
}
