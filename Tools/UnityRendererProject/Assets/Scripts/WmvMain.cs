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
    WmvShadowRig shadowRig;           // renders the cast-shadow depth map (see WmvShadowRig.cs)

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

        // A scene light and ambient, for the FALLBACK shaders only.
        //
        // The renderer's own shader ignores both -- it carries its own preview rig, because URP and
        // HDRP do not feed the built-in light uniforms and a shader that read them would look
        // different per pipeline. These exist so that a build which somehow has to fall back to a
        // pipeline Lit shader (-wmvLitShader, or a player with no Resources) is not lit by nothing
        // at all. Their angle matches the shader's key so the two do not disagree wildly.
        // -wmvRig=N draws the viewport with a different preview light rig (0 shipped,
        // 1 legacy). The global is the only thing that changes; every material and every
        // combiner stays exactly as it was.
        Shader.SetGlobalFloat("_WmvRig", WmvModelBuilder.Debug_.Rig);

        // Cast shadows: the model occluding its own key light (a rein across the mount's body).
        // The rig renders a depth map from the key's viewpoint each frame; the shader attenuates
        // the key term where the map says something stands in the way. See WmvShadowRig.cs.
        shadowRig = gameObject.AddComponent<WmvShadowRig>();

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
            if (shadowRig != null)
                shadowRig.SetBounds(built.Bounds);

            // The light check brings its own camera and frames the model itself, so it no longer
            // depends on this call having happened -- an earlier version measured before framing
            // and reported a few stray pixels as a reading.
            if (WmvModelBuilder.Debug_.LightCheck)
                ReportLighting();

            // Independent of the model just loaded -- it brings its own geometry, materials and
            // camera -- but hung off the same hook so one headless run produces both readings.
            if (WmvModelBuilder.Debug_.QueueProof)
                ReportQueueOrder();

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
    /// Render the model offscreen and report what the light rig did to it -- measured on a set of
    /// pixels that the light rig cannot move.
    ///
    /// WHY THE OLD VERSION WAS NOT TRUSTWORTHY. It picked the model out of the frame with
    /// "brighter than the background plus 0.02", which defines the sample in terms of the very
    /// quantity being measured. Brighten the shader and dim model pixels -- silhouette
    /// antialiasing, the shadow side, dark texture regions -- cross that line and join the sample.
    /// They join it at the bottom, so the reported mean can FALL while every single pixel got
    /// brighter. That is a selection effect, not a lighting result, and it is why a rig carrying
    /// strictly more light measured darker on the pale models.
    ///
    /// WHAT REPLACES IT. The model is separated from the background geometrically: the same frame
    /// is drawn twice, once cleared to black and once to white, and a pixel counts as model where
    /// the two agree exactly. Only geometry that fully covers a pixel with opaque, depth-writing
    /// material can be independent of what was cleared behind it, so the mask depends on the mesh,
    /// the blend states and the camera -- and on nothing the light rig does. Additive and blended
    /// batches, and antialiased silhouette pixels, fall outside it by construction, which is the
    /// point: those are exactly the pixels whose value is part background.
    ///
    /// Everything else is pinned too: a camera of its own (the viewport orbit cannot leak in),
    /// framing derived from the bounds alone, and -wmvNoAnim for a fixed pose. Every rig is then
    /// measured through that one camera, over that one mask, in one process, so the only thing
    /// that differs between the numbers is the rig.
    ///
    /// The old threshold number is still printed beside the new one, because the gap between them
    /// IS the artefact, and it is worth being able to see it rather than take it on trust.
    ///
    /// Diagnostics only: its own camera and its own render texture, nothing written to disk, the
    /// viewport untouched.
    /// </summary>
    void ReportLighting()
    {
        Camera src = Camera.main;
        if (src == null || current == null)
        {
            Debug.Log("WMV: lightcheck: no camera or no model -- nothing measured");
            return;
        }

        const int W = 512, H = 512;
        const float Fov = 60f;
        float Yaw = WmvModelBuilder.Debug_.LightYaw;
        float Pitch = WmvModelBuilder.Debug_.LightPitch;

        // FRAMING, from the bounds and two fixed angles. Not from the orbit: the viewport camera
        // may have been moved by a Frame() call, a drag or a wheel, and a measurement that moves
        // with it compares two different pictures.
        Bounds b = current.Bounds;
        Quaternion rot = Quaternion.Euler(Pitch, Yaw, 0f);
        Vector3 up = rot * Vector3.up, right = rot * Vector3.right, fwd = rot * Vector3.forward;
        Vector3 e = b.extents;
        float halfUp = Mathf.Abs(e.x * up.x) + Mathf.Abs(e.y * up.y) + Mathf.Abs(e.z * up.z);
        float halfRt = Mathf.Abs(e.x * right.x) + Mathf.Abs(e.y * right.y) + Mathf.Abs(e.z * right.z);
        float halfFw = Mathf.Abs(e.x * fwd.x) + Mathf.Abs(e.y * fwd.y) + Mathf.Abs(e.z * fwd.z);
        // Square frame, so the vertical field of view governs both axes.
        float need = Mathf.Max(Mathf.Max(halfUp, halfRt), 0.01f);
        float dist = need / Mathf.Tan(Fov * 0.5f * Mathf.Deg2Rad) * 1.08f + halfFw;

        var go = new GameObject("WmvLightCheckCamera");
        Camera cam = go.AddComponent<Camera>();
        cam.enabled = false;                         // only ever rendered by hand, below
        cam.clearFlags = CameraClearFlags.SolidColor;
        cam.fieldOfView = Fov;
        cam.cullingMask = src.cullingMask;
        cam.allowHDR = false;
        cam.allowMSAA = false;
        cam.transform.rotation = rot;
        cam.transform.position = b.center - fwd * dist;
        cam.nearClipPlane = Mathf.Max(0.01f, dist * 0.01f);
        cam.farClipPlane = dist * 10f + 100f;

        RenderTexture rt = RenderTexture.GetTemporary(W, H, 24, RenderTextureFormat.ARGB32,
                                                      RenderTextureReadWrite.Default);
        RenderTexture prevActive = RenderTexture.active;
        float restoreRig = WmvModelBuilder.Debug_.Rig;
        try
        {
            // The shadow map must belong to THIS camera, not to wherever the viewport was
            // pointing: the key is view-relative, so the map's light direction follows the
            // camera it was rendered for, and a measurement taken with someone else's map
            // would move when the viewport moved.
            if (shadowRig != null)
                shadowRig.RenderFor(cam);

            // ---- the mask: geometry decides, not brightness ---------------------------------
            //
            // Built under the LEGACY rig specifically. Byte-equality between the two clears is a
            // near-perfect stand-in for "opaque geometry covered this pixel", but not a perfect
            // one: where an additive batch is bright enough to saturate the result over BOTH
            // clears, the two agree for a reason that has nothing to do with coverage, and the
            // pixel joins the mask. Which pixels those are depends on how bright the rig is --
            // so a mask built under the rig being measured would drift as the rig is tuned, and
            // it did, by a few pixels on the two models with big additive passes. The legacy rig
            // is the one rig that is finished and will not be tuned again, so building the mask
            // under it pins the pixel set across every candidate and every build.
            Shader.SetGlobalFloat("_WmvRig", 1);
            Color32[] onBlack = GrabFrame(cam, rt, Color.black, W, H);
            Color32[] onWhite = GrabFrame(cam, rt, Color.white, W, H);
            var mask = new bool[W * H];
            int covered = 0, saturated = 0;
            for (int i = 0; i < mask.Length; i++)
            {
                Color32 p = onBlack[i], q = onWhite[i];
                bool same = p.r == q.r && p.g == q.g && p.b == q.b;
                // ...but not if it agreed only because it ran out of range. An additive batch
                // bright enough to saturate over a black clear saturates over a white one too,
                // so the two agree for a reason that has nothing to do with coverage -- valkier,
                // which is 99 % additive, smuggled in 1676 such pixels and then reported 12 %
                // of its own mask as clipped. A pixel pinned at the top of the range carries no
                // luminance to measure either way, so it is not part of the sample.
                bool pinned = p.r >= 255 || p.g >= 255 || p.b >= 255;
                if (same && pinned) { saturated++; same = false; }
                mask[i] = same;
                if (same) covered++;
            }

            // Determinism is a claim, so check it rather than assert it: build the mask a second
            // time and report whether the two agree pixel for pixel.
            Color32[] onBlack2 = GrabFrame(cam, rt, Color.black, W, H);
            Color32[] onWhite2 = GrabFrame(cam, rt, Color.white, W, H);
            int maskDrift = 0;
            for (int i = 0; i < mask.Length; i++)
            {
                bool same2 = onBlack2[i].r == onWhite2[i].r && onBlack2[i].g == onWhite2[i].g
                             && onBlack2[i].b == onWhite2[i].b;
                if (same2 && (onBlack2[i].r >= 255 || onBlack2[i].g >= 255 || onBlack2[i].b >= 255))
                    same2 = false;
                if (same2 != mask[i]) maskDrift++;
            }

            Color bg = src.backgroundColor;
            float bgLum = 0.2126f * bg.r + 0.7152f * bg.g + 0.0722f * bg.b;
            // WHICH SKIN WAS MEASURED, spelled out. WMV picks a random skin per load unless
            // Session/RandomLooks is off, and a creature can carry skins that differ in brightness
            // by more than any light rig ever will -- chicken2 spans 2.4x across its four. A
            // before/after pair taken in two processes is therefore not comparable unless this
            // line matches, and until it was printed there was no way to notice that it did not.
            var skinIds = new List<string>();
            foreach (var kv in currentTextureIds) skinIds.Add(kv.Key + ":" + kv.Value);
            skinIds.Sort();
            Debug.Log(string.Format(
                "WMV: lightcheck: {0} | camera yaw {1} pitch {2} fov {3} dist {4:F3} "
                + "| bounds extents ({5:F2},{6:F2},{7:F2}) | background {8:F4} | textures {9}",
                currentName, Yaw, Pitch, Fov, dist, e.x, e.y, e.z, bgLum,
                skinIds.Count > 0 ? string.Join(",", skinIds.ToArray()) : "(none recorded)"));
            Debug.Log(string.Format(
                "WMV: lightcheck: mask {0} px ({1:P2} of frame), {2} px dropped as saturated, "
                + "drift on rebuild {3} px -- {4}",
                covered, covered / (float)(W * H), saturated, maskDrift,
                maskDrift == 0 ? "deterministic" : "NOT DETERMINISTIC, numbers below are suspect"));

            // A model that is nearly all additive or alpha-blended has almost no opaque core
            // to measure, so say so rather than quietly reporting a number from a sliver of it.
            if (covered > 0 && covered < (W * H) / 100)
                Debug.Log(string.Format(
                    "WMV: lightcheck: WARNING -- only {0:P2} of the frame is opaque geometry; this "
                    + "model is mostly blended/additive and the figures below describe a small "
                    + "opaque core, not what the viewer shows", covered / (float)(W * H)));

            if (covered == 0)
            {
                Debug.Log("WMV: lightcheck: no opaque depth-writing geometry covered a pixel -- "
                          + "nothing to measure (a fully blended model, or nothing drawn)");
                return;
            }

            // ---- every rig, same camera, same mask, same process -----------------------------
            for (int rig = 0; rig <= 1; rig++)
            {
                Shader.SetGlobalFloat("_WmvRig", rig);
                Color32[] shot = GrabFrame(cam, rt, bg, W, H);
                ReportOneRig(rig, shot, mask, covered, bgLum, W, H);

                // CAST SHADOWS, measured by subtraction -- and PER CONTRIBUTOR, because the
                // map and the contact march compose with min() and a joint measurement hides
                // whichever one the other already covers (an early version disabled only the
                // map in the reference frame and reported the contact march's entire output as
                // a REDUCTION in shadow). Three frames: everything on, map only, neither.
                if (rig == 0 && shadowRig != null)
                {
                    Shader.SetGlobalFloat("_WmvContactValid", 0f);
                    Color32[] mapOnly = GrabFrame(cam, rt, bg, W, H);
                    Shader.SetGlobalFloat("_WmvShadowValid", 0f);
                    Color32[] unshadowed = GrabFrame(cam, rt, bg, W, H);
                    shadowRig.RenderFor(cam);            // restores both maps and both flags
                    ReportShadow("map", mapOnly, unshadowed, mask, W, H);
                    ReportShadow("map+contact", shot, unshadowed, mask, W, H);
                    ReportShadow("contact adds", shot, mapOnly, mask, W, H);
                }

                // ...and again with the texture taken out of it. On a real model, paint and light
                // arrive as one number: a rig that models form and a rig that lights everything
                // flat can produce the same mean, the same percentiles and the same contrast,
                // because the spread being measured is mostly the texture. White albedo leaves
                // only the rig, so "does this have shadows in it" becomes a ratio instead of an
                // opinion. The shipped rig measured FLATTER than the legacy one here while every
                // other number said it was better, which is exactly the failure this catches.
                const float FlatAlbedo = 0.25f;   // linear, ~sRGB 0.54: a typical WoW texel
                Shader.SetGlobalFloat("_WmvFlatAlbedo", FlatAlbedo);
                Color32[] flat = GrabFrame(cam, rt, bg, W, H);
                Shader.SetGlobalFloat("_WmvFlatAlbedo", 0f);
                ReportShading(rig, flat, mask, covered);
            }
        }
        catch (System.Exception ex)
        {
            Debug.LogWarning("WMV: lightcheck could not render: " + ex.Message);
        }
        finally
        {
            Shader.SetGlobalFloat("_WmvRig", restoreRig);
            RenderTexture.active = prevActive;
            RenderTexture.ReleaseTemporary(rt);
            Destroy(go);
        }
    }

    /// <summary>
    /// Render one frame of the measurement camera into rt with the given clear colour and read it
    /// back. Its own texture each call, so the caller can hold several frames side by side.
    /// </summary>
    Color32[] GrabFrame(Camera cam, RenderTexture rt, Color clear, int w, int h)
    {
        cam.backgroundColor = clear;
        cam.targetTexture = rt;
        cam.Render();
        cam.targetTexture = null;
        RenderTexture prev = RenderTexture.active;
        RenderTexture.active = rt;
        var tex = new Texture2D(w, h, TextureFormat.RGBA32, false);
        tex.ReadPixels(new Rect(0, 0, w, h), 0, 0);
        tex.Apply(false);
        RenderTexture.active = prev;
        Color32[] px = tex.GetPixels32();
        Destroy(tex);
        return px;
    }

    /// <summary>
    /// Does a per-material render queue actually order the SUBMESHES OF ONE RENDERER?
    /// (-wmvQueueProof)
    ///
    /// The transparent draw order in WmvModelBuilder rests entirely on this. A model is one
    /// renderer with one submesh per batch, so the only place an order can be expressed without
    /// rebuilding the mesh is the material's render queue -- and whether Unity honours that
    /// BETWEEN SUBMESHES OF THE SAME RENDERER, rather than drawing them in submesh order
    /// regardless, is a question about the engine. Reading how the render loop sorts draw calls
    /// suggests an answer; it does not establish one, and a real model cannot settle it either:
    /// additive blending is commutative, so a model whose blended batches are additive looks
    /// identical whichever order they draw in, and a null result there would mean nothing.
    ///
    /// So the case is synthetic and fully determined. Two coplanar quads in one mesh, one submesh
    /// each, both with depth writes off and an output alpha of exactly 1, blended SrcAlpha /
    /// OneMinusSrcAlpha. Nothing about the geometry, the depth buffer or the winding can decide
    /// what survives: the pixel that comes out IS the identity of whichever material drew SECOND.
    /// Submesh 0 is red, submesh 1 is blue, and the queues are set both ways round --
    ///
    ///   equal queues     : records what the engine does with no queue to go on (submesh order)
    ///   A 3000, B 3001   : expect BLUE, which agrees with submesh order
    ///   A 3001, B 3000   : expect RED, which CONTRADICTS submesh order
    ///
    /// -- and it is the third case that carries the proof. Both renderer types are measured,
    /// because models take both paths (WmvModelBuilder builds a SkinnedMeshRenderer when the file
    /// can be skinned and a MeshRenderer when it cannot) and a skinned draw is submitted
    /// differently.
    /// </summary>
    void ReportQueueOrder()
    {
        // Same 512 square the light check uses, so a saved frame from here sits beside a saved
        // frame of a model at the same size and neither has to be scaled to be looked at.
        const int W = 512, H = 512;
        Shader sh = WmvModelBuilder.ResolveShader(null);
        if (sh == null)
        {
            Debug.Log("WMV: queueproof: no render shader resolved -- nothing to measure");
            return;
        }

        // Far from anything the viewport can see. The proof objects are destroyed before this
        // method returns, but Destroy is deferred to the end of the frame, and 10 km away with a
        // 20-unit far plane on the measuring camera means neither camera can see the other's
        // subject even for that one frame.
        Vector3 origin = new Vector3(10000f, 10000f, 10000f);
        Color clear = new Color(0.25f, 0.25f, 0.25f, 1f);   // neither red- nor blue-dominant

        var camGo = new GameObject("WmvQueueProofCamera");
        Camera cam = camGo.AddComponent<Camera>();
        cam.enabled = false;                        // only ever rendered by hand, below
        cam.clearFlags = CameraClearFlags.SolidColor;
        cam.orthographic = true;
        cam.orthographicSize = 0.5f;                // the quads are 2 units across: they overfill
        cam.nearClipPlane = 0.1f;
        cam.farClipPlane = 20f;
        cam.allowHDR = false;
        cam.allowMSAA = false;
        cam.transform.position = origin + new Vector3(0f, 0f, -5f);
        cam.transform.rotation = Quaternion.identity;

        RenderTexture rt = RenderTexture.GetTemporary(W, H, 24, RenderTextureFormat.ARGB32,
                                                      RenderTextureReadWrite.Default);

        // WHAT THIS DIAGNOSTIC BORROWS FROM GLOBAL SHADER STATE, AND HOW IT GIVES IT BACK.
        //
        // Two globals decide how the proof materials shade. _WmvRig, because the emissive bypass
        // the proof depends on is gated on the shipped rig and the legacy rig deliberately
        // ignores it; and _WmvFlatAlbedo, because the light check's white-albedo override would
        // repaint both quads the same colour and destroy the very reading being taken.
        //
        // Both are READ FIRST and put back exactly as they were FOUND -- not put back to the
        // value they ought to have had. An earlier version of this method restored _WmvRig to
        // Debug_.Rig and did not restore _WmvFlatAlbedo at all, reasoning that ReportLighting is
        // its only other writer and always leaves it at 0. That reasoning happens to be true of
        // today's code and is still the wrong rule: it makes the diagnostic's cleanup depend on
        // a fact about a different method, and it leaves the switch able to change what the
        // viewport draws after it has finished measuring. Ask, do not assume -- and say in the
        // log what was found and what was left, so the claim is checkable instead of asserted.
        float prevRig = Shader.GetGlobalFloat("_WmvRig");
        float prevFlatAlbedo = Shader.GetGlobalFloat("_WmvFlatAlbedo");
        // THE RENDER TARGET, AND WHY RECORDING IT IS NOT ENOUGH.
        //
        // GrabFrame does put RenderTexture.active back -- but only on the path where nothing
        // throws. It sets cam.targetTexture = rt, renders, clears it, then sets
        // RenderTexture.active = rt and restores it after ReadPixels. A throw anywhere between
        // those pairs leaves the process pointing at THIS method's temporary, and the release
        // below would then hand the rest of the frame a freed render target. Depending on
        // GrabFrame's internal cleanup is therefore fine for the happy path and not good enough
        // for a finally block, so both are put back here as well, before the release.
        //
        // GrabFrame is left alone: twelve of its thirteen callers are the light check's, and
        // rewriting it to be exception-safe is a change to that measurement, not to this
        // diagnostic's cleanup. (The Texture2D it allocates does leak on a throw; that is
        // GrabFrame's own business and not shared state.) cam.backgroundColor is also written
        // and never restored, and that one does not matter: the camera belongs to this method
        // and is destroyed below.
        RenderTexture prevActive = RenderTexture.active;
        try
        {
            Shader.SetGlobalFloat("_WmvRig", 0f);
            Shader.SetGlobalFloat("_WmvFlatAlbedo", 0f);

            bool allProven = true;
            for (int p = 0; p < 2; p++)
            {
                bool skinned = p == 1;
                string path = skinned ? "SkinnedMeshRenderer" : "MeshRenderer";
                Material matA = QueueProofMaterial(sh, new Color(1f, 0f, 0f, 1f), "queueProofA");
                Material matB = QueueProofMaterial(sh, new Color(0f, 0f, 1f, 1f), "queueProofB");
                Mesh mesh;
                GameObject go = BuildQueueProofCase(sh, origin, skinned,
                                                    new[] { matA, matB }, out mesh);

                char wEqual, wBlast, wAlast;
                matA.renderQueue = 3000; matB.renderQueue = 3000;
                Color32[] fEqual = GrabFrame(cam, rt, clear, W, H);
                string sEqual = QueueProofRead(fEqual, out wEqual);
                matA.renderQueue = 3000; matB.renderQueue = 3001;
                Color32[] fBlast = GrabFrame(cam, rt, clear, W, H);
                string sBlast = QueueProofRead(fBlast, out wBlast);
                matA.renderQueue = 3001; matB.renderQueue = 3000;
                Color32[] fAlast = GrabFrame(cam, rt, clear, W, H);
                string sAlast = QueueProofRead(fAlast, out wAlast);

                // Repeat the decisive case to show the reading is not a one-off.
                char wAlast2;
                string sAlast2 = QueueProofRead(GrabFrame(cam, rt, clear, W, H), out wAlast2);

                // The frames themselves, under the switch that already saves the light check's
                // pictures. A count of red and blue pixels is the measurement; the picture is
                // what a reviewer can check the measurement against without running anything.
                if (WmvModelBuilder.Debug_.LightDump)
                {
                    string tag = skinned ? "skinned" : "static";
                    DumpPng(fEqual, W, H, "wmv-queueproof-" + tag + "-equal-3000-3000.png");
                    DumpPng(fBlast, W, H, "wmv-queueproof-" + tag + "-A3000-B3001.png");
                    DumpPng(fAlast, W, H, "wmv-queueproof-" + tag + "-A3001-B3000.png");
                }

                bool proven = wBlast == 'B' && wAlast == 'A' && wAlast2 == 'A';
                if (!proven) allProven = false;

                Debug.Log(string.Format(
                    "WMV: queueproof: {0}: submesh 0 = red material A, submesh 1 = blue material B, "
                    + "coplanar, ZWrite off, alpha 1 -- the frame shows whichever drew last", path));
                Debug.Log(string.Format(
                    "WMV: queueproof: {0}: queues equal (3000/3000), no queue to go on -> {1}",
                    path, sEqual));
                Debug.Log(string.Format(
                    "WMV: queueproof: {0}: A=3000 B=3001, expect B/blue last -> {1} [{2}]",
                    path, sBlast, wBlast == 'B' ? "as expected" : "NOT AS EXPECTED"));
                Debug.Log(string.Format(
                    "WMV: queueproof: {0}: A=3001 B=3000, expect A/red last, which CONTRADICTS "
                    + "submesh order -> {1} [{2}]",
                    path, sAlast, wAlast == 'A' ? "as expected" : "NOT AS EXPECTED"));
                Debug.Log(string.Format(
                    "WMV: queueproof: {0}: same case rendered again -> {1} [{2}]",
                    path, sAlast2, wAlast2 == wAlast ? "repeatable" : "NOT REPEATABLE"));
                Debug.Log(string.Format("WMV: queueproof: {0}: {1}", path, proven
                    ? "PROVEN -- the frame follows the render queue, including when the queue "
                      + "contradicts submesh order"
                    : "NOT PROVEN -- the frame did not follow the render queue; the transparent "
                      + "draw order has no effect and should be removed"));

                go.SetActive(false);
                Destroy(go);
                Destroy(mesh);
                Destroy(matA);
                Destroy(matB);
            }

            Debug.Log("WMV: queueproof: verdict -- " + (allProven
                ? "a per-material render queue orders the submeshes of one renderer on BOTH "
                  + "renderer paths, so ranking transparent batches by queue does what it claims"
                : "the render queue did NOT order the submeshes of one renderer on at least one "
                  + "path -- see the lines above"));
        }
        finally
        {
            // ORDER MATTERS HERE. The render target goes back FIRST, so the temporary is neither
            // the active target nor a camera's target at the moment it is released -- releasing
            // one that is still bound is how a freed render target reaches the next frame.
            RenderTexture.active = prevActive;
            if (cam != null)
                cam.targetTexture = null;
            Shader.SetGlobalFloat("_WmvRig", prevRig);
            Shader.SetGlobalFloat("_WmvFlatAlbedo", prevFlatAlbedo);
            RenderTexture.ReleaseTemporary(rt);
            Destroy(camGo);

            // Read the globals back rather than trust the writes, and print entry and exit side
            // by side. A diagnostic that quietly changes what the viewport draws after it has
            // finished measuring is worse than no diagnostic, so this line exists to be looked at.
            float nowRig = Shader.GetGlobalFloat("_WmvRig");
            float nowFlatAlbedo = Shader.GetGlobalFloat("_WmvFlatAlbedo");
            // Compared by REFERENCE, not by name: "the same render target object", not "a
            // render target that happens to be called the same thing".
            bool restored = nowRig == prevRig && nowFlatAlbedo == prevFlatAlbedo
                            && ReferenceEquals(RenderTexture.active, prevActive);
            Debug.Log(string.Format(
                "WMV: queueproof: global state -- on entry _WmvRig {0} _WmvFlatAlbedo {1} "
                + "RenderTexture.active {2}; on exit _WmvRig {3} _WmvFlatAlbedo {4} "
                + "RenderTexture.active {5} -- {6}",
                prevRig, prevFlatAlbedo, prevActive == null ? "none" : prevActive.name,
                nowRig, nowFlatAlbedo,
                RenderTexture.active == null ? "none" : RenderTexture.active.name,
                restored ? "every borrowed global restored to the value it was found at"
                         : "NOT RESTORED -- the diagnostic changed global state it did not put back"));
        }
    }

    /// <summary>
    /// A flat, self-lit, fully opaque-in-alpha blended material: the light rig, the texture, the
    /// combiner and the alpha channel are all taken out of the reading, so the only thing the
    /// frame can report is which material drew last.
    /// </summary>
    static Material QueueProofMaterial(Shader sh, Color c, string name)
    {
        var m = new Material(sh);
        m.name = name;
        m.SetColor("_Color", c);
        m.SetFloat("_CombinerMode", 0f);      // single texture; _MainTex defaults to white
        m.SetFloat("_Unit0UV", 0f);
        m.SetFloat("_Unit1UV", 2f);
        m.SetFloat("_AlphaMode", 0f);         // alpha 1 out of the combiner
        m.SetFloat("_AlphaScale", 1f);
        m.SetFloat("_OpaqueAlpha", 1f);       // ...and 1 out of the shader: the later draw wins
        m.SetFloat("_Emissive", 1f);          // no light rig in the colour
        m.SetFloat("_Cull", 0f);              // Off: winding cannot decide the outcome either
        m.SetFloat("_ZWrite", 0f);            // as a transparent batch: depth cannot decide it
        m.SetFloat("_SrcBlend", (float)UnityEngine.Rendering.BlendMode.SrcAlpha);
        m.SetFloat("_DstBlend", (float)UnityEngine.Rendering.BlendMode.OneMinusSrcAlpha);
        return m;
    }

    /// <summary>
    /// One renderer, one mesh, two submeshes that occupy exactly the same pixels -- built the two
    /// ways WmvModelBuilder builds a model, so the answer covers both.
    /// </summary>
    static GameObject BuildQueueProofCase(Shader sh, Vector3 origin, bool skinned,
                                          Material[] mats, out Mesh mesh)
    {
        Vector3[] corner = { new Vector3(-1f, -1f, 0f), new Vector3(-1f, 1f, 0f),
                             new Vector3( 1f,  1f, 0f), new Vector3( 1f, -1f, 0f) };
        var verts = new Vector3[8];
        var norms = new Vector3[8];
        for (int q = 0; q < 2; q++)
            for (int c = 0; c < 4; c++)
            {
                verts[q * 4 + c] = corner[c];
                norms[q * 4 + c] = new Vector3(0f, 0f, -1f);
            }
        mesh = new Mesh();
        mesh.vertices = verts;
        mesh.normals = norms;
        mesh.subMeshCount = 2;
        mesh.SetTriangles(new[] { 0, 1, 2, 0, 2, 3 }, 0, false);
        mesh.SetTriangles(new[] { 4, 5, 6, 4, 6, 7 }, 1, false);
        mesh.bounds = new Bounds(Vector3.zero, new Vector3(2f, 2f, 0.1f));

        var go = new GameObject(skinned ? "WmvQueueProofSkinned" : "WmvQueueProofStatic");
        go.transform.position = origin;
        if (skinned)
        {
            var bone = new GameObject("WmvQueueProofBone");
            bone.transform.SetParent(go.transform, false);
            var bw = new BoneWeight[8];
            for (int i = 0; i < bw.Length; i++) { bw[i].boneIndex0 = 0; bw[i].weight0 = 1f; }
            mesh.boneWeights = bw;
            mesh.bindposes = new[] { Matrix4x4.identity };
            var smr = go.AddComponent<SkinnedMeshRenderer>();
            smr.sharedMesh = mesh;
            smr.bones = new[] { bone.transform };
            smr.rootBone = bone.transform;
            smr.sharedMaterials = mats;
            smr.localBounds = mesh.bounds;
            smr.updateWhenOffscreen = false;
        }
        else
        {
            go.AddComponent<MeshFilter>().sharedMesh = mesh;
            go.AddComponent<MeshRenderer>().sharedMaterials = mats;
        }
        return go;
    }

    /// <summary>
    /// Which material owns the frame: 'A' red, 'B' blue, '?' neither. Counted over every pixel
    /// rather than sampled at the centre, so a partial draw cannot be read as a whole one.
    /// </summary>
    static string QueueProofRead(Color32[] px, out char winner)
    {
        int red = 0, blue = 0, neither = 0;
        for (int i = 0; i < px.Length; i++)
        {
            Color32 q = px[i];
            if (q.r > q.b + 40 && q.r > q.g + 40) red++;
            else if (q.b > q.r + 40 && q.b > q.g + 40) blue++;
            else neither++;
        }
        winner = red > blue && red > neither ? 'A'
               : blue > red && blue > neither ? 'B' : '?';
        return string.Format("{0} ({1} red px, {2} blue px, {3} neither, of {4})",
                             winner == 'A' ? "A/red" : winner == 'B' ? "B/blue" : "INDETERMINATE",
                             red, blue, neither, px.Length);
    }

    /// <summary>
    /// Statistics for one rig over the geometric mask, plus -- deliberately -- the number the old
    /// threshold rule would have printed for the same frame, so the two can be compared directly.
    /// </summary>
    void ReportOneRig(int rig, Color32[] shot, bool[] mask, int covered, float bgLum, int w, int h)
    {
        var lums = new List<float>(covered);
        int clipped = 0;
        double chroma = 0.0;
        int threshCount = 0;
        double threshSum = 0.0;
        float threshold = bgLum + 0.02f;

        for (int i = 0; i < shot.Length; i++)
        {
            Color32 c = shot[i];
            float r = c.r / 255f, g = c.g / 255f, bl = c.b / 255f;
            float lum = 0.2126f * r + 0.7152f * g + 0.0722f * bl;

            // The old rule, over the WHOLE frame, exactly as it used to be applied.
            if (lum > threshold) { threshCount++; threshSum += lum; }

            if (!mask[i]) continue;
            lums.Add(lum);
            if (c.r >= 253 || c.g >= 253 || c.b >= 253) clipped++;

            // Chroma IN LINEAR LIGHT, not on the sRGB bytes. (max-min)/max is invariant under a
            // scale factor only in the space the scaling happens in; computed on sRGB values it
            // falls whenever the picture gets brighter, because the encoding curve is concave.
            // Measured on the bytes, a pure exposure change looked like a loss of saturation --
            // which is exactly the question this number is supposed to answer, so it has to be
            // free of it. Linearised, a rig that only changes exposure leaves it untouched and
            // only a real desaturation moves it.
            float lr = Srgb2Linear(r), lg = Srgb2Linear(g), lb = Srgb2Linear(bl);
            float mx = Mathf.Max(lr, Mathf.Max(lg, lb)), mn = Mathf.Min(lr, Mathf.Min(lg, lb));
            if (mx > 0.0001f) chroma += (mx - mn) / mx;      // saturation, a colour-pop proxy
        }

        lums.Sort();
        double sum = 0.0;
        for (int i = 0; i < lums.Count; i++) sum += lums[i];
        float mean = (float)(sum / lums.Count);
        // Michelson contrast of the model against the background it sits on.
        float contrast = (mean + bgLum) > 0.0001f ? (mean - bgLum) / (mean + bgLum) : 0f;

        Debug.Log(string.Format(
            "WMV: lightcheck rig={0} ({1}) | mask {2} px | mean {3:F4} p05 {4:F4} p50 {5:F4} "
            + "p95 {6:F4} max {7:F4} | clipped {8:F3} % | chroma {9:F4} | contrast {10:F4} "
            + "|| old-threshold mask: {11} px mean {12:F4}",
            rig,
            rig == 0 ? "shipped" : "legacy",
            lums.Count, mean,
            Pct(lums, 0.05f), Pct(lums, 0.50f), Pct(lums, 0.95f), lums[lums.Count - 1],
            100f * clipped / lums.Count, (float)(chroma / lums.Count), contrast,
            threshCount, threshCount > 0 ? (float)(threshSum / threshCount) : 0f));
    }

    /// <summary>sRGB byte value (0..1) to linear light.</summary>
    static float Srgb2Linear(float c)
    {
        return c <= 0.04045f ? c / 12.92f : Mathf.Pow((c + 0.055f) / 1.055f, 2.4f);
    }

    /// <summary>
    /// What the cast shadows did: how much of the model they touch, how hard they darken it,
    /// and where their centre of mass sits. The centroid line is DESCRIPTIVE, not a verdict.
    ///
    /// The full story is a cautionary one. The first shadow build had a vertically mirrored
    /// map (the render-into-texture flip counted twice -- see WmvShadowRig), and every check
    /// here let it through: coverage looked plausible, darkening looked plausible, and when
    /// the centroid flagged "shadows sit ABOVE the model" on two of three models, that was
    /// explained away as receiver geometry. Even the dumped difference images were misread as
    /// sensible. What caught it was someone LOOKING AT THE VIEWPORT and saying the shadow
    /// looked like a whole copy of the model stamped across itself -- which is precisely what
    /// a mirrored map does. The lesson stands in both directions: -wmvLightDump plus the
    /// criterion "with a high key, darkening belongs on UNDER-surfaces and the top of the back
    /// must be clean" is what verified the fix, and no summary statistic here should ever be
    /// trusted over that picture.
    /// </summary>
    void ReportShadow(string what, Color32[] withShadow, Color32[] without, bool[] mask,
                      int w, int h)
    {
        int touched = 0, maskCount = 0;
        double deltaSum = 0.0;
        double ySum = 0.0, yShadowSum = 0.0;
        for (int i = 0; i < withShadow.Length; i++)
        {
            if (!mask[i]) continue;
            maskCount++;
            int y = i / w;                              // ReadPixels rows run bottom-up
            ySum += y;
            float on = 0.2126f * withShadow[i].r + 0.7152f * withShadow[i].g
                       + 0.0722f * withShadow[i].b;
            float off = 0.2126f * without[i].r + 0.7152f * without[i].g
                        + 0.0722f * without[i].b;
            float delta = (off - on) / 255f;            // positive where the shadow darkened
            if (delta > 1.5f / 255f)
            {
                touched++;
                deltaSum += delta;
                yShadowSum += y;
            }
        }
        if (WmvModelBuilder.Debug_.LightDump)
        {
            DumpPng(withShadow, w, h, "wmv-lightcheck-" + what + "-on.png");
            DumpPng(without, w, h, "wmv-lightcheck-" + what + "-off.png");
            // The difference, amplified: white = where the shadow darkened the frame.
            var diff = new Color32[withShadow.Length];
            for (int i = 0; i < diff.Length; i++)
            {
                float on = 0.2126f * withShadow[i].r + 0.7152f * withShadow[i].g
                           + 0.0722f * withShadow[i].b;
                float off = 0.2126f * without[i].r + 0.7152f * without[i].g
                            + 0.0722f * without[i].b;
                byte v = (byte)Mathf.Clamp((off - on) * 8f, 0f, 255f);
                diff[i] = new Color32(v, v, v, 255);
            }
            DumpPng(diff, w, h, "wmv-lightcheck-" + what + "-diff.png");
        }

        if (maskCount == 0) return;
        if (touched == 0)
        {
            Debug.Log("WMV: lightcheck shadows [" + what + "]: touch 0.0 % of the model");
            return;
        }
        float centroidDy = (float)(yShadowSum / touched - ySum / maskCount);
        Debug.Log(string.Format(
            "WMV: lightcheck shadows [{4}]: touch {0:P1} of the model | mean darkening {1:F4} "
            + "over touched px | shadow centroid {2:F1} px {3} the model centroid",
            touched / (float)maskCount, deltaSum / touched,
            Mathf.Abs(centroidDy), centroidDy < 0f ? "below" : "above", what));
    }

    /// <summary>Write one readback frame as a PNG next to the player (-wmvLightDump).</summary>
    void DumpPng(Color32[] px, int w, int h, string name)
    {
        try
        {
            var tex = new Texture2D(w, h, TextureFormat.RGBA32, false);
            tex.SetPixels32(px);
            tex.Apply(false);
            string path = System.IO.Path.Combine(Application.dataPath, "../" + name);
            System.IO.File.WriteAllBytes(path, ImageConversion.EncodeToPNG(tex));
            Destroy(tex);
            Debug.Log("WMV: lightcheck dump: " + path);
        }
        catch (System.Exception e)
        {
            Debug.LogWarning("WMV: lightcheck dump failed: " + e.Message);
        }
    }

    /// <summary>
    /// How much light-to-dark range the rig itself puts on a model, with no texture in the way.
    /// The ratio of the bright end to the dark end is the number that says whether the thing has
    /// shadows: around 1.2 is a flat, evenly-lit figure; 1.8 upwards reads as modelled form.
    /// </summary>
    void ReportShading(int rig, Color32[] shot, bool[] mask, int covered)
    {
        var lums = new List<float>(covered);
        for (int i = 0; i < shot.Length; i++)
        {
            if (!mask[i]) continue;
            Color32 c = shot[i];
            lums.Add(0.2126f * c.r / 255f + 0.7152f * c.g / 255f + 0.0722f * c.b / 255f);
        }
        if (lums.Count == 0) return;
        lums.Sort();
        float p05 = Pct(lums, 0.05f), p50 = Pct(lums, 0.50f), p95 = Pct(lums, 0.95f);
        double sum = 0.0;
        for (int i = 0; i < lums.Count; i++) sum += lums[i];
        Debug.Log(string.Format(
            "WMV: lightcheck rig={0} SHADING (flat albedo) | mean {1:F4} p05 {2:F4} p50 {3:F4} "
            + "p95 {4:F4} min {5:F4} max {6:F4} | range p95/p05 {7:F2}x | full {8:F2}x",
            rig, sum / lums.Count, p05, p50, p95, lums[0], lums[lums.Count - 1],
            p05 > 0.0001f ? p95 / p05 : 0f,
            lums[0] > 0.0001f ? lums[lums.Count - 1] / lums[0] : 0f));
    }

    static float Pct(List<float> sorted, float q)
    {
        int i = Mathf.Clamp(Mathf.RoundToInt((sorted.Count - 1) * q), 0, sorted.Count - 1);
        return sorted[i];
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
