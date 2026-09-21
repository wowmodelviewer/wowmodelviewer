// WmvMain.cs
//
// Bootstrap and load orchestration for the WMV Unity viewport player -- WMV's only viewport (the
// host's OpenGL viewport is archived and cannot be shown; content this player cannot draw yet gets
// a notice painted by the host instead; see docs/unity-renderer/README.md).
//
// Add this component to one empty GameObject in an otherwise-empty scene; at runtime it builds
// the camera rig, a light, the status overlay and the IPC client that connects back to WMV.
//
// LOAD PIPELINE (all bytes arrive over IPC; nothing is read from or written to disk):
//
//   loadWoWModel(path, fileDataID, character)
//     -> getAsset(path)                     the .m2 itself
//     -> M2Parser                           header, vertices, textures, materials, SFID/TXID/SKID
//     -> getAssetByFileDataID(SKID)         the .skel (and ITS parent, SKPD) when the model keeps its
//                                           bones, sequences and attachments there -- every playable
//                                           character does
//     -> getAssetByFileDataID(SFID[0])      the .skin profile (LOD 0)
//     -> M2SkinParser                       lookup, triangles, submeshes, batches
//     -> textures: TXID entry when the M2 names one, otherwise getModelTextures so WMV can
//        resolve the replaceable creature skin from the client database. A CHARACTER instead waits
//        for the host's characterScene, which names every body slot's texture (a file, or an image
//        the host composited), and is dressed by WmvCharacterDresser before it goes on screen
//     -> getAssetByFileDataID(texture)      the .blp
//     -> BlpDecoder                         RGBA32 in memory
//     -> WmvModelBuilder                    Mesh + Materials + Texture2D + GameObject
//     -> frame the camera on the mesh bounds
//
// What is built is then animated and dressed by other components: WmvM2Animator (bones, following the
// app's animation selection and transport), WmvMaterialAnimator and WmvEmitterRuntime (animated
// materials and particles), and for a character WmvCharacterDresser (the host's characterScene).
// What the player keeps about the model on screen -- the parsed model and its bytes, the selection,
// the sequence caches, the app's playback state, the display state -- is one WmvModelSlot
// (WmvModelSlot.cs), and the sequence code (WmvSlotAnimation.cs) takes the slot it acts on.
//
// A CHARACTER RIDING A MOUNT (protocol 5) is still the character: the model on screen and its dresser stay
// the rider's, and the mount its scene describes is a second model, held by WmvMountedScene
// (WmvMountedScene.cs) -- built beside the character, with the character's body hung from the mount's bone
// in the frame the scene is applied. A mount for a character still being loaded belongs to that load, as
// its dresser does, and replaces the mount on screen only when the character does. The two models animate
// on their own clocks, and the host's animation pushes say which one they are about (a role, and the rider
// nested in the mount's playback state): HandleModelAnimation and HandleModelAnimationState route them.
// In the frame the mount under the character changes, the camera and the shadow window are fitted to both
// models together, or to the character again once it is off (FrameRiddenScene).
//
// A WORLD MODEL (loadWoWModel kind "wmo") takes its own pipeline -- root, GFID LOD0 groups, material
// textures, WmvWmoBuilder -- which lives in WmvMainMapObject.cs. The two jobs supersede each other.

using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow;

public partial class WmvMain : MonoBehaviour
{
    WmvIpcClient ipc;
    GameObject placeholder;
    WmvStatusOverlay status;
    WmvOrbitCamera orbit;

    /// <summary>
    /// The model on screen: its runtime (disposed when replaced), the parsed model and bytes it was built
    /// from, the app's selection and playback state for it, the sequence caches and its display state
    /// (see WmvModelSlot). A load fills this same slot. Its selection, sequence caches, texture ids and
    /// display state are reset when loadWoWModel arrives and from then on describe the model being
    /// loaded; its runtime, parsed model, name, FileDataID and textures stay the previous model's until
    /// AdoptBuilt or AdoptMapObject replaces them.
    /// </summary>
    readonly WmvModelSlot currentSlot = new WmvModelSlot();
    WmvCharacterDresser dresser;      // what the character on screen wears, when it is one
    WmvMountedScene mounted;          // what the character on screen rides, or is about to (protocol 5)
    WmvSlotAnimation anim;            // the app's animation choice and playback, applied to a slot

    // A RIDDEN MOUNT'S ANIMATION PUSHES (protocol 5) that cannot be applied when they arrive, kept for the frame a
    // mount goes on or comes off (CommitMount). All of it is dropped with the load it belongs to.
    WmvIpcClient.AnimationState lastMountState;        // the newest ridden state: its top level is the host's mount
    bool haveLastMountState;
    WmvIpcClient.AnimationSelection keptMountPick;     // a selection for a mount still being prepared
    bool haveKeptMountPick;
    double riderPickAt = -1.0;        // when the newest role "rider" selection arrived (WmvIpcClient.NowSeconds)
    bool riderSelectionHeld;          // a selection with no role for the riding character: the host dismounted it
    Bounds riddenFrame;               // what FrameRiddenScene last fitted the camera to
    WmvMountProbe mountProbe;         // -wmvMountCheck, while it measures

    /// <summary>
    /// Host-composited images by the id scenes name them by, each with its kind ("body", "eyes"). The
    /// newest image of each kind is kept for the life of the connection, NEVER dropped with a model: the
    /// host sends a kind again only when its pixels change, so a character loaded a second time names the
    /// image it already sent. An older image of a kind is dropped when a newer one arrives, unless a scene
    /// not yet applied still names it: the host sends the new image as soon as it has composited it, and
    /// a scene still being prepared would otherwise lose its body texture and commit without it.
    /// </summary>
    readonly Dictionary<string, KeyValuePair<string, BlpImage>> characterImages =
        new Dictionary<string, KeyValuePair<string, BlpImage>>();
    WmvShadowRig shadowRig;           // renders the cast-shadow depth map (see WmvShadowRig.cs)

    LoadJob job;                      // in-flight load, if any
    SkinJob skinJob;                  // in-flight skin change of the model on screen, if any
    // The app's playback state for the model being LOADED, kept until its animator exists.
    WmvIpcClient.AnimationState loadState;
    bool haveLoadState;
    double loadStateAt;             // WmvIpcClient.NowSeconds when the push ARRIVED
    // The app's skin push (geosets, particle colour) for the model being LOADED, adopted at
    // build when the load did not ask the host for its textures itself.
    WmvIpcClient.ModelTexturesResponse loadSkin;
    bool haveLoadSkin;

    /// <summary>
    /// The host's per-submesh display state for the model being LOADED, the latest of whatever
    /// carried one (a modelGeosets push, the modelTextures reply, a modelSkin push), handed to
    /// Build. Latest wins and it is never dropped at build: each of those messages carries the
    /// host's whole state at the time it was sent, and they arrive in the order they were sent.
    /// loadGeosetRevision is the modelGeosets push it came from (0 = a reply or a skin push),
    /// acknowledged once the build has applied it.
    /// </summary>
    bool[] loadSubmeshVisible;
    bool haveLoadSubmeshVisible;
    int loadGeosetRevision;

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
        public int Load;            // the host's serial for this loadWoWModel, echoed in characterSceneApplied
        public int ParsedSequence = -1;   // the selection the sequence was resolved for (Parse, ApplySkeleton)
        public System.Diagnostics.Stopwatch Clock = System.Diagnostics.Stopwatch.StartNew();
        public long M2Ms, SkinMs, TextureMs, ParseMs, BuildMs;

        public byte[] M2Bytes;
        public M2ParsedModel Model;
        public M2ParsedSkin Skin;
        public readonly Dictionary<int, BlpImage> Textures = new Dictionary<int, BlpImage>();

        public string PendingM2, PendingSkin, PendingTextureList;
        public string PendingSkel, PendingParentSkel;
        public byte[] SkelBytes;

        // A playable character: textures come from the host's scene, and the body is dressed before it
        // replaces the model on screen.
        public bool Character;
        public WmvIpcClient.CharacterScene Scene;
        public WmvIpcClient.CharacterScene TexturesScene;   // the scene the body's textures were taken from
        public bool SceneTexturesRequested;
        public WmvRuntimeModel Staged;
        public WmvCharacterDresser Dresser;
        // The mount its scene describes (a character loaded while it rides one: a reconnect, a load while
        // mounted). Built for this load, never put in place of the mount on screen before the character is
        // adopted: AdoptBuilt disposes that one with the character it carries.
        public WmvMountedScene Mount;
        public bool AskedHost;      // getModelTextures was sent: the answer carries the display's geosets
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

        if (WmvModelBuilder.Debug_.LifecycleTest)
            WmvLifecycleSelfTest.RunAll(s => Debug.Log("WMV: " + s));

        {   // SCRATCH: give the harness player a real screen BEFORE anything frames to it.
            string sz = System.Environment.GetEnvironmentVariable("WMV_VIEWPORT_SIZE");
            string want = System.Environment.GetEnvironmentVariable("WMV_VIEWPORT_SHOT");
            int v = 1024;
            if (!string.IsNullOrEmpty(sz)) int.TryParse(sz, out v);
            if (!string.IsNullOrEmpty(want) && v >= 128 && v <= 4096)
                Screen.SetResolution(v, v, false);
        }

        var cam = Camera.main;
        if (cam == null)
        {
            var camGo = new GameObject("Main Camera");
            cam = camGo.AddComponent<Camera>();
            camGo.tag = "MainCamera";
        }
        cam.clearFlags = CameraClearFlags.SolidColor;
        cam.backgroundColor = ViewportClear;    // re-set in the composited domain by ConfigureDisplayTransform
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

        ConfigureDisplayTransform();

        // Cast shadows: the model occluding its own key light (a rein across the mount's body).
        // The rig renders a depth map from the key's viewpoint each frame; the shader attenuates
        // the key term where the map says something stands in the way. See WmvShadowRig.cs.
        shadowRig = gameObject.AddComponent<WmvShadowRig>();

        var lightGo = new GameObject("Directional Light");
        var light = lightGo.AddComponent<Light>();
        light.type = LightType.Directional;
        light.intensity = 1.1f;
        // Over the default camera's shoulder: the camera looks at the model's front from +Z
        // (WmvOrbitCamera.FrontYaw), so this light travels toward -Z. Only the fallback shaders
        // (-wmvLitShader, or a build without the WMV shader) read it; the WMV shader's key is
        // view-relative (WmvShadowRig.KeyDirView) and turns with the camera by itself.
        lightGo.transform.rotation = Quaternion.Euler(50f, 150f, 0f);
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
        anim = new WmvSlotAnimation(ipc.RequestAssetByFileDataID, s => status.Set(s));
        anim.SequenceApplied = OnSequenceApplied;
        ipc.OnStatus = s => status.Set(s);
        ipc.OnLoadWoWModel = HandleLoadWoWModel;
        ipc.OnAssetResponse = HandleAssetResponse;
        ipc.OnModelTextures = HandleModelTextures;
        ipc.OnModelSkin = HandleModelSkin;
        ipc.OnModelAnimation = HandleModelAnimation;
        ipc.OnModelAnimationState = HandleModelAnimationState;
        ipc.OnModelGeosets = HandleModelGeosets;
        ipc.OnCharacterImage = HandleCharacterImage;
        ipc.OnCharacterScene = HandleCharacterScene;
        ipc.OnRuntimeState = HandleRuntimeState;
        ipc.OnCaptureScreenshot = HandleCaptureScreenshot;
    }

    // ---------------------------------------------------------------- load pipeline

    void HandleLoadWoWModel(string path, int fileDataID, string client, bool character, int load, string kind)
    {
        status.Set("Active client received (" + client + ")");
        if (string.IsNullOrEmpty(path) && fileDataID <= 0)
        {
            status.Set("loadWoWModel without path or fileDataID -- ignored");
            return;
        }
        // A world model still loading will never be shown, whichever kind replaces it.
        SupersedeMapObjectJob("superseded by a new load");
        bool mapObject = IsMapObjectKind(kind);
        // A character still being dressed for the previous load will never be shown.
        AbandonCharacterJob("superseded by a new load");
        // Nor will a scene the character ON SCREEN is still preparing. Left running, it committed whenever
        // its last file landed, in the middle of the new load, and against images the new load's scene
        // may already have replaced. What it already wears stays until the new model takes its place.
        if (dresser != null)
            dresser.CancelTarget("superseded by a new load");
        // ... and the same for a mount being prepared for it. The mount it rides stays, with it.
        if (mounted != null)
            mounted.CancelTarget();

        // A sequence index means nothing across models -- entry 14 is a different animation in
        // each -- so forget the previous one. The app pushes its selection for the NEW model right
        // after this message, so the value is refilled before the .m2 arrives to be parsed.
        currentSlot.SelectedSequence = -1;
        currentSlot.M2Bytes = null;
        // Sequence indices and file ids mean nothing across models.
        currentSlot.BoneTrackCache.Clear();
        currentSlot.MaterialTrackCache.Clear();
        currentSlot.AnimFileCache.Clear();
        currentSlot.AbandonAnimFetches();
        currentSlot.HaveAppState = false;
        haveLoadState = false;
        haveLoadSkin = false;
        // Nor do a ridden mount's pushes kept for a mount going on or a character coming off (CommitMount).
        haveLastMountState = false;
        haveKeptMountPick = false;
        riderPickAt = -1.0;
        riderSelectionHeld = false;
        // Neither does the previous model's DISPLAY state, and it was being carried across: the
        // geoset set chosen for the last creature was handed to the next Build as its own, hiding
        // submeshes -- or the whole model -- on any file the host is never asked to describe
        // (one whose textures all name files). Same for the particle recolour, an in-flight skin
        // change and the texture ids the skin change reads.
        currentSlot.Geosets = null;
        currentSlot.ParticleColor = null;
        skinJob = null;
        currentSlot.TextureIds.Clear();
        // A push still waiting for the previous load is about a model that will never be built.
        if (haveLoadSubmeshVisible && loadGeosetRevision > 0 && job != null)
            ipc.ReportGeosetsApplied(job.FileDataID, loadGeosetRevision, "rejected",
                                     "superseded by a new load", null, 0, 0.0);
        loadSubmeshVisible = null;
        haveLoadSubmeshVisible = false;
        loadGeosetRevision = 0;

        if (mapObject)
        {
            // Everything above -- an M2 load in flight, a character being dressed, the previous model's
            // display state -- is over just the same; the model on screen stays until the world model
            // is built (AdoptMapObject).
            job = null;
            StartMapObjectLoad(path, fileDataID, load);
            return;
        }

        job = new LoadJob { Path = path, FileDataID = fileDataID, Character = character, Load = load };
        status.Set("Requested " + (string.IsNullOrEmpty(path) ? ("fileDataID " + fileDataID) : path));
        job.PendingM2 = string.IsNullOrEmpty(path)
            ? ipc.RequestAssetByFileDataID(fileDataID)
            : ipc.RequestAsset(path);
    }

    void HandleAssetResponse(WmvIpcClient.AssetResponse r)
    {
        // The world-model job's root, group files and textures.
        if (HandleMapObjectAsset(r))
            return;
        // The dresser's own requests: a part's .m2, skin, skeleton and textures.
        if (job != null && job.Dresser != null && job.Dresser.Owns(r.requestId))
        {
            job.Dresser.OnAsset(r);
            return;
        }
        if (dresser != null && dresser.Owns(r.requestId))
        {
            dresser.OnAsset(r);
            return;
        }
        // A mount's own requests: its .m2, skeleton, .anim, skin and textures, and the .anim of the riding sequence.
        if (job != null && job.Mount != null && job.Mount.Owns(r.requestId))
        {
            job.Mount.OnAsset(r);
            return;
        }
        if (mounted != null && mounted.Owns(r.requestId))
        {
            mounted.OnAsset(r);
            return;
        }
        // A skin change is answered by the same assetResponse messages as a load, so claim ours
        // before the load path sees them.
        if (skinJob != null && skinJob.Pending.ContainsKey(r.requestId))
        {
            OnSkinTextureBytes(r);
            return;
        }
        // An external .anim answer belongs to an animation change, not to a load -- of the model on screen, or of the
        // mount it rides.
        int waitingSequence;
        if (currentSlot.PendingAnimFetch.TryGetValue(r.requestId, out waitingSequence))
        {
            anim.OnAnimFileBytes(currentSlot, r, waitingSequence);
            return;
        }
        if (mounted != null && mounted.Mount.PendingAnimFetch.TryGetValue(r.requestId, out waitingSequence))
        {
            anim.OnAnimFileBytes(mounted.Mount, r, waitingSequence);
            return;
        }
        // A .anim answer for a slot that was given another model while it was out: claimed and dropped here, so it is
        // never read as an answer to the load in flight.
        if (currentSlot.AbandonedAnimFetch.Remove(r.requestId) ||
            (mounted != null && mounted.Mount.AbandonedAnimFetch.Remove(r.requestId)))
            return;
        if (job == null)
            return;

        if (r.requestId == job.PendingSkel || r.requestId == job.PendingParentSkel)
        {
            OnSkeletonBytes(r);
            return;
        }

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
            job.ParsedSequence = currentSlot.SelectedSequence;
            job.Model = M2Parser.Parse(r.data, currentSlot.SelectedSequence);
            job.ParseMs += job.Clock.ElapsedMilliseconds - t0;
            if (job.FileDataID <= 0) job.FileDataID = r.fileDataID;
        }
        catch (WowParseException e) { Fail("M2 parse failed: " + e.Message); return; }

        if (job.Model.SkinFileDataIDs.Length == 0)
        {
            Fail("model has no skin profile (SFID chunk missing) -- nothing to render");
            return;
        }
        // The bones, sequences and attachments of a model with a skeleton file are not in the .m2.
        if (job.Model.SkeletonFileDataID != 0)
        {
            job.PendingSkel = ipc.RequestAssetByFileDataID(job.Model.SkeletonFileDataID);
            return;
        }
        // SFID[0] is the highest-detail profile.
        job.PendingSkin = ipc.RequestAssetByFileDataID(job.Model.SkinFileDataIDs[0]);
    }

    /// <summary>
    /// The model's .skel, then the .skel's parent when it names one (SKPD). The skeleton is applied
    /// the way the host applies it (M2Parser.ApplySkeleton); a skeleton that cannot be read leaves the
    /// model drawn static, as before this existed, rather than failing the load.
    /// </summary>
    void OnSkeletonBytes(WmvIpcClient.AssetResponse r)
    {
        bool parentReply = r.requestId == job.PendingParentSkel;
        if (parentReply) job.PendingParentSkel = null; else job.PendingSkel = null;

        if (!r.ok || r.data == null)
        {
            Debug.LogWarning("WMV: skeleton file " + (parentReply ? "(parent) " : "") + "could not be read: " + r.error);
            if (!parentReply || job.SkelBytes == null)
            {
                job.PendingSkin = ipc.RequestAssetByFileDataID(job.Model.SkinFileDataIDs[0]);
                return;
            }
        }
        else if (!parentReply)
        {
            job.SkelBytes = r.data;
            int parent = M2Parser.ReadSkeletonParentId(r.data);
            if (parent > 0)
            {
                job.PendingParentSkel = ipc.RequestAssetByFileDataID(parent);
                return;
            }
        }

        long t0 = job.Clock.ElapsedMilliseconds;
        try
        {
            job.ParsedSequence = currentSlot.SelectedSequence;
            M2Parser.ApplySkeleton(job.Model, job.SkelBytes, parentReply && r.ok ? r.data : null,
                                   currentSlot.SelectedSequence);
            Debug.Log(string.Format("WMV: skeleton {0} applied: {1} bone(s), {2} sequence(s), {3} attachment(s){4}",
                                    job.Model.SkeletonFileDataID, job.Model.Bones.Length, job.Model.Sequences.Length,
                                    job.Model.Attachments.Length,
                                    job.Model.ParentSkeletonFileDataID > 0 ? ", parent " + job.Model.ParentSkeletonFileDataID : ""));
        }
        catch (WowParseException e)
        {
            Debug.LogWarning("WMV: skeleton " + job.Model.SkeletonFileDataID + " not applied -- drawn static: " + e.Message);
        }
        job.ParseMs += job.Clock.ElapsedMilliseconds - t0;
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
        if (job.Character)
        {
            RequestCharacterBodyTextures();
            return;
        }
        var direct = new List<KeyValuePair<int, int>>();   // slot -> fileDataID
        bool needsHost = false;
        for (int i = 0; i < job.Model.Textures.Length; i++)
        {
            var t = job.Model.Textures[i];
            if (t.FileDataID > 0)
            {
                direct.Add(new KeyValuePair<int, int>(i, t.FileDataID));
                currentSlot.TextureIds[i] = t.FileDataID;   // the M2 named this one itself
            }
            else if (t.IsReplaceable) needsHost = true;
        }

        job.TexturesExpected = direct.Count;
        foreach (var d in direct)
            job.PendingTextures[ipc.RequestAssetByFileDataID(d.Value)] = d.Key;

        if (needsHost)
        {
            job.AskedHost = true;
            job.PendingTextureList = ipc.RequestModelTextures(job.FileDataID);
        }
        else if (direct.Count == 0)
            BuildIfReady();
    }

    /// <summary>
    /// A character's body textures, as the host's scene binds them: files are fetched like any texture,
    /// composited images are already here. Waits for the scene when it has not arrived yet.
    /// </summary>
    void RequestCharacterBodyTextures()
    {
        if (job == null || job.Skin == null || job.SceneTexturesRequested)
            return;
        if (job.Scene == null)
        {
            status.Set("Waiting for the character's appearance");
            return;
        }
        job.SceneTexturesRequested = true;
        job.TexturesScene = job.Scene;
        List<KeyValuePair<int, int>> files;
        var missing = new List<string>();
        WmvCharacterDresser.BodyTexturesFrom(job.Scene, ImageByHash, out files, job.Textures, missing);
        foreach (string m in missing)
            Debug.LogWarning("WMV: character: " + m + " is not available -- that slot draws untextured");
        job.TexturesExpected = files.Count;
        foreach (var f in files)
        {
            job.PendingTextures[ipc.RequestAssetByFileDataID(f.Value)] = f.Key;
            currentSlot.TextureIds[f.Key] = f.Value;
        }
        if (files.Count == 0)
            BuildIfReady();
    }

    BlpImage ImageByHash(string hash)
    {
        KeyValuePair<string, BlpImage> entry;
        return hash != null && characterImages.TryGetValue(hash, out entry) ? entry.Value : null;
    }

    /// <summary>Does a scene that has not been applied yet -- the load's, or one either dresser is still
    /// preparing -- name this image?</summary>
    bool ImageStillNamed(string hash)
    {
        if (job != null && WmvCharacterDresser.SceneNamesImage(job.Scene, hash))
            return true;
        if (job != null && job.Dresser != null && job.Dresser.WantsImage(hash))
            return true;
        return dresser != null && dresser.WantsImage(hash);
    }

    void HandleCharacterImage(WmvIpcClient.CharacterImage img)
    {
        if (img.image == null)
        {
            Debug.LogWarning("WMV: character image " + img.hash + " rejected: " + img.error);
            return;
        }
        string kind = string.IsNullOrEmpty(img.kind) ? img.hash : img.kind;
        characterImages[img.hash] = new KeyValuePair<string, BlpImage>(kind, img.image);
        // The images this one replaces, unless a scene still waiting to be applied names them.
        List<string> replaced = null;
        foreach (var kv in characterImages)
        {
            if (kv.Key == img.hash || kv.Value.Key != kind || ImageStillNamed(kv.Key))
                continue;
            if (replaced == null) replaced = new List<string>();
            replaced.Add(kv.Key);
        }
        if (replaced != null)
            foreach (string h in replaced)
                characterImages.Remove(h);
        Debug.Log(string.Format("WMV: character image {0} ({1}) {2}x{3}{4}", img.hash, img.kind, img.image.Width,
                                img.image.Height, img.image.HasAlpha ? ", alpha" : ""));
        if (job != null && job.Dresser != null) job.Dresser.OnImage();
        if (dresser != null) dresser.OnImage();
    }

    /// <summary>
    /// The host's resolved character. Either it belongs to the character being loaded -- the body build
    /// waits for its textures, and the dresser for its parts -- or to the character on screen, which is
    /// re-dressed. A scene about neither is refused.
    /// </summary>
    void HandleCharacterScene(WmvIpcClient.CharacterScene scene)
    {
        // The mount first, in both cases: the dresser may apply the scene the moment it is retargeted, and a scene
        // with a mount is only applied once its mount is ready (MountReady).
        if (job != null && job.Character && scene.fileDataID == job.FileDataID)
        {
            job.Scene = scene;
            RetargetMount(ref job.Mount, scene);
            if (job.Dresser != null)
                job.Dresser.Retarget(scene);
            else if (job.Skin != null)
                RequestCharacterBodyTextures();
            return;
        }
        if (job == null && dresser != null && currentSlot.Runtime != null && scene.fileDataID == currentSlot.FileDataID)
        {
            RetargetMount(ref mounted, scene);
            dresser.Retarget(scene);
            return;
        }
        // Matches no load, so no load serial.
        ipc.ReportCharacterSceneApplied(scene.fileDataID, 0, scene.revision, "rejected",
                                        "not the character on screen or being loaded", 0, 0, null, 0,
                                        WmvIpcClient.MountKeyOf(scene), "none", "");
    }

    /// <summary>Hand a scene's mount to the mounted scene of the character it belongs to -- created for the first
    /// mount -- or, for a scene without one, drop the mount being prepared there. The mount on screen stays until the
    /// scene without it is applied: that is the dismount (CommitMount).</summary>
    void RetargetMount(ref WmvMountedScene holder, WmvIpcClient.CharacterScene scene)
    {
        if (WmvIpcClient.HasMount(scene))
        {
            if (holder == null)
                holder = new WmvMountedScene(ipc.RequestAssetByFileDataID, s => Debug.Log("WMV: " + s), RepumpMountWaiters);
            holder.Retarget(scene.mount, scene.receivedSeconds);
        }
        else if (holder != null)
        {
            holder.CancelTarget();
        }
    }

    /// <summary>A mount's file arrived: a dresser whose scene waits on it (CommitGate) looks again.</summary>
    void RepumpMountWaiters()
    {
        if (job != null && job.Dresser != null)
            job.Dresser.Repump();
        if (dresser != null)
            dresser.Repump();
    }

    /// <summary>
    /// The CommitGate of a character's dresser: may its scene be applied now? Always, for a scene without a mount --
    /// exactly as before mounts existed. For one with a mount, once the mount is built (or failed for good) and the
    /// keys of the character's riding sequence are here, so the character, the mount and the pose go on together.
    /// The load's dresser waits on the load's mount, the dresser on screen on the mount on screen.
    /// </summary>
    bool MountReady(WmvCharacterDresser from, WmvIpcClient.CharacterScene scene)
    {
        if (!WmvIpcClient.HasMount(scene))
            return true;
        if (job != null && job.Dresser == from)
            return job.Mount == null || job.Mount.ReadyFor(scene.mount, currentSlot, job.Model);
        if (from == dresser)
            return mounted == null || mounted.ReadyFor(scene.mount, currentSlot, currentSlot.Model);
        return true;
    }

    /// <summary>
    /// The OnCommitted of a character's dresser, in the frame its scene is applied (for a load, after AdoptStaged put
    /// the character on screen): seat the character on the scene's mount, swap mounts, or take it off the one it
    /// rides; start both clocks when a different mount went on, and play the character's own selection when it came
    /// off; fit the view to what the character is now when the mount under it changed (FrameRiddenScene); and say what
    /// became of the mount for the answer.
    /// </summary>
    WmvIpcClient.MountAnswer CommitMount(WmvCharacterDresser from, WmvIpcClient.CharacterScene scene)
    {
        if (from != dresser || dresser.Body == null)
            return WmvIpcClient.MountAnswer.None(scene);     // not the character on screen (its adoption failed)
        if (!WmvIpcClient.HasMount(scene))
        {
            string rode = mounted != null && mounted.Mount.Runtime != null ? mounted.Key : null;
            WmvIpcClient.MountAnswer none = mounted != null ? mounted.Dismount() : WmvIpcClient.MountAnswer.None(scene);
            haveKeptMountPick = false;
            if (riderSelectionHeld)
                ApplyHeldRiderSelection();
            if (rode != null)
                FrameRiddenScene("the character came off " + rode);
            string key = WmvIpcClient.MountKeyOf(scene);
            if (key.Length > 0)
            {
                // A key with no file: nothing the player can fetch. Said, rather than read as no mount.
                Debug.LogWarning("WMV: mount: " + key + " names no fileDataID -- the character is shown without it");
                return new WmvIpcClient.MountAnswer { Key = key, Status = "failed", Reason = "the mount names no fileDataID" };
            }
            return none;
        }
        if (mounted == null)
        {
            // Every scene with a mount reaches its mounted scene before its dresser (HandleCharacterScene).
            Debug.LogWarning("WMV: mount: " + scene.mount.key + " reached no mounted scene -- the character is shown without it");
            return new WmvIpcClient.MountAnswer { Key = scene.mount.key, Status = "failed", Reason = "the mount was never prepared" };
        }
        bool newMount;
        double describedAt = mounted.PreparingDescribedAt;   // read before the commit takes the prepared mount on screen
        WmvRuntimeModel mountBefore = mounted.Mount.Runtime;
        string keyBefore = mountBefore != null ? mounted.Key : "";
        WmvIpcClient.MountAnswer answer = mounted.Commit(scene.mount, dresser.Body, out newMount);
        if (newMount)
        {
            riderSelectionHeld = false;
            double mountRanMs = StartMountClock(scene.mount);
            StartRidingSequence(scene.mount.riderSequenceIndex, describedAt, scene.receivedSeconds, mountRanMs);
            PoseMountedAtPinnedTime();
        }
        // After the clocks and any pinned pose: the character's box is carried through the mount bone as it now stands.
        if (mounted.Mount.Runtime != mountBefore)
            FrameRiddenScene(mounted.Mount.Runtime != null
                             ? mounted.Key + " went on" + (keyBefore.Length > 0 ? " in place of " + keyBefore : "")
                             : scene.mount.key + " could not be built, and the character came off " + keyBefore);
        else if (mounted.RiddenFileDataID != 0)
            RequestViewportShot();                           // what the character wears changed on its mount: the view stays
        return answer;
    }

    /// <summary>A sequence switch completed on a slot (WmvSlotAnimation.SequenceApplied). While the character rides, a
    /// capture waits for a switch on either model.</summary>
    void OnSequenceApplied(WmvModelSlot slot)
    {
        if (mounted != null && mounted.RiddenFileDataID != 0 && (slot == currentSlot || slot == mounted.Mount))
            RequestViewportShot();
    }

    /// <summary>
    /// How many times the view has been fitted to what is on screen: a model or a world model adopted, and every change
    /// of the mount under a character (FrameRiddenScene). runtimeState reports it, which is how a lifecycle test tells
    /// a mount change -- which fits the view exactly once -- from an appearance change on a riding character, which must
    /// not move the camera at all. A capture that has to ask for its size again re-fits the SAME box
    /// (CaptureViewportWhenSettled) and is deliberately not counted: it follows the display, not what is on screen.
    /// </summary>
    public int ViewFramings { get; private set; }

    /// <summary>
    /// The frame the mount under the character changes -- a mount goes on, another replaces it, the character comes off
    /// (a dismount, or a new mount that could not be built taking it off the one it rode): the camera and the shadow
    /// window are fitted again, as they are for a model put on screen, to the mount and the character together while it
    /// rides (WmvMountedScene.UnionBounds) and to the character alone once it is off. Not for a newer description of the
    /// same mount -- an appearance change must not move the view -- and never per frame: an animation that later carries
    /// either model past the box is not followed. -wmvFrameBounds still pins the box; WMV_VIEWPORT_ORBIT re-aims the
    /// camera after it; -wmvLightCheck measures (in the next frame), -wmvAllocCheck counts and WMV_VIEWPORT_SHOT captures
    /// what is now on screen, and -wmvMountCheck measures the frames that follow while the character rides.
    /// </summary>
    void FrameRiddenScene(string what)
    {
        WmvRuntimeModel body = dresser != null ? dresser.Body : null;
        if (body == null || body.Root == null)
            return;
        ViewFramings++;
        Bounds own;
        bool riding = false;
        if (mounted != null)
            riding = mounted.UnionBounds(body, out own);     // the character's own box when it rides nothing
        else
            own = WmvMountedScene.WorldBounds(body.Bounds, body.Root.transform.localToWorldMatrix);
        Bounds frame = own;
        if (WmvModelBuilder.Debug_.HasFrameBounds)
        {
            Debug.Log(string.Format("WMV: mount: bounds pinned by -wmvFrameBounds for framing and the light rig ({0}: centre {1} extents {2})",
                                    riding ? "the mount and the character together" : "the character's own", own.center, own.extents));
            frame = WmvModelBuilder.Debug_.FrameBounds;
        }
        orbit.Frame(frame);
        KeepFramed(frame);
        ApplyViewportOrbitOverride("mount: ");
        if (shadowRig != null)
            shadowRig.SetBounds(frame);
        riddenFrame = frame;
        Debug.Log(string.Format("WMV: mount: {0} -- the view is fitted to {1}: centre {2} extents {3}, distance {4:F2}, yaw {5} pitch {6}",
                                what, riding ? "the mount and the character together" : "the character alone", frame.center,
                                frame.extents, orbit.distance, orbit.yaw, orbit.pitch));

        if (WmvModelBuilder.Debug_.AllocCheck)
            allocProbe = new AllocProbe { StartFrame = Time.frameCount + 10 };
        if (WmvModelBuilder.Debug_.LightCheck)
            StartCoroutine(ReportLightingNextFrame(own, riding, mounted != null ? mounted.Mount.Runtime : null));
        RequestViewportShot();
        if (riding)
            StartMountProbe(what);
    }

    /// <summary>
    /// -wmvLightCheck after the mount under the character changed, in the next frame rather than in the one the mount went
    /// on: the mount a swap replaces is disposed in that frame, and Object.Destroy removes its objects only at the end of
    /// it, so a check rendered then drew the old mount too (measured: the Highland Drake put on in place of the Brutosaur
    /// read a mask of 31,665 px when checked in its commit frame, and 2,451 px checked a frame later -- the same mounts,
    /// box and camera). A mount changed again in between is not measured.
    /// </summary>
    System.Collections.IEnumerator ReportLightingNextFrame(Bounds framed, bool riding, WmvRuntimeModel mountThen)
    {
        yield return null;
        WmvRuntimeModel mountNow = mounted != null ? mounted.Mount.Runtime : null;
        if (mountNow != mountThen || currentSlot.Runtime == null)
        {
            Debug.Log("WMV: lightcheck: the mount under the character changed again before the check ran -- not measured");
            yield break;
        }
        ReportLighting(framed, riding);
    }

    /// <summary>-wmvMountCheck: measure the frames that follow on the mount on screen and the character riding it
    /// (WmvMountProbe), in place of a window still running.</summary>
    void StartMountProbe(string what)
    {
        if (WmvModelBuilder.Debug_.MountCheckFrames <= 0 || mounted == null || mounted.Rider == null ||
            mounted.Mount.Runtime == null || dresser == null || dresser.Body == null)
            return;
        if (mountProbe != null)
            mountProbe.Stop("a new window begins: " + what);
        mountProbe = WmvMountProbe.Begin(gameObject, mounted.Key + " (" + what + ")", mounted.Mount.Runtime, dresser.Body,
                                         riddenFrame, WmvModelBuilder.Debug_.MountCheckFrames,
                                         WmvModelBuilder.Debug_.MountCheckMountFirst, s => Debug.Log("WMV: " + s));
    }

    /// <summary>
    /// The mount's clock in the frame it goes on. The commit started it at its first frame, playing at 1x, as the host
    /// starts the fresh model the mount choice loads; the host has run it since, and what it said about it is newer: the
    /// newest ridden state naming this mount's file (sampled after the host put that mount on) is where the clock starts,
    /// projected to now, and a selection made for it while it was being prepared is switched to, through the slot's own
    /// caches or a .anim fetched once, as any switch is. Its .anim files are then fetched up front, as the character's
    /// are. Returns how long that clock says the host has run the mount on the sequence the scene started it on -- the
    /// position it started at over its speed -- or -1 when it did not start from such a state.
    /// </summary>
    double StartMountClock(WmvIpcClient.SceneMount described)
    {
        WmvModelSlot mount = mounted.Mount;
        int pick = haveKeptMountPick && keptMountPick.load == dresser.Load && keptMountPick.fileDataID == mount.FileDataID
                   ? keptMountPick.sequenceIndex : -1;
        haveKeptMountPick = false;
        if (mount.Runtime == null || mount.Model == null)
            return -1.0;                                     // it could not be built
        if (haveLastMountState && lastMountState.load == dresser.Load && lastMountState.fileDataID == mount.FileDataID)
        {
            mount.LastAppState = lastMountState;
            mount.HaveAppState = true;
        }
        double ranMs = -1.0;
        if (pick >= 0 && pick < mount.Model.Sequences.Length && pick != mount.Model.AnimatedSequence)
        {
            Debug.Log("WMV: mount: " + mounted.Key + " was given sequence " + pick + " while it was prepared -- switching to it");
            mount.SelectedSequence = pick;
            try { anim.SwitchToSequence(mount, pick); }
            catch (System.Exception e)
            {
                Debug.LogWarning("WMV: mount: switching " + mounted.Key + " to sequence " + pick + " failed: " +
                                 e.GetType().Name + ": " + e.Message);
            }
        }
        else if (mount.HaveAppState)
        {
            float at = WmvSlotAnimation.StartClock(mount, 0.0, true, 1f);
            WmvIpcClient.AnimationState s = mount.LastAppState;
            if (s.sequenceIndex == described.sequenceIndex && s.sequenceIndex == mount.Model.AnimatedSequence && s.speed > 0f &&
                s.receivedSeconds > 0.0)
                ranMs = at / s.speed;
            Debug.Log(string.Format("WMV: mount: {0} plays sequence {1} from the app's state for it: {2} at {3:F0} ms, speed {4:0.##}",
                                    mounted.Key, mount.Model.AnimatedSequence, s.playing ? "playing" : "paused", at, s.speed));
        }
        anim.PrefetchAnimFiles(mount);
        return ranMs;
    }

    /// <summary>
    /// The character's clock when a different mount goes on: the host stops the rider, sets the sequence the mount
    /// choice selected for it (riderSequenceIndex -- the host's own result, applied as given) at its first frame, and
    /// lets the canvas tick run it with the mount's: playing, at the rider's own speed. Its keys are already here
    /// (MountReady), so the switch completes in this frame. A state for that sequence the host sent after the first
    /// scene describing this mount arrived (describedAt) is newer than that restart -- a heartbeat during a long build,
    /// or any state of a character loaded while it rides -- so the clock starts from it. Without one, the character is
    /// started in step with the mount: the host set both at their first frames in the one mount choice and has run both
    /// on the same ticks since, so it is as far into its sequence as the mount's clock says the mount is (mountRanMs,
    /// StartMountClock), at its own speed; a state from before that scene is about the clip before, and only its speed
    /// carries over. A selection the character was given after this scene arrived (sceneArrivedAt) is newer than
    /// riderSequenceIndex, and stays.
    /// </summary>
    void StartRidingSequence(int sequence, double describedAt, double sceneArrivedAt, double mountRanMs)
    {
        WmvModelSlot rider = currentSlot;
        if (sequence < 0 || rider.Model == null || rider.Runtime == null || sequence >= rider.Model.Sequences.Length)
            return;
        if (riderPickAt > sceneArrivedAt)
        {
            Debug.Log("WMV: mount: the character keeps sequence " + rider.SelectedSequence + ", selected for it after " +
                      "this scene was sent, rather than the riding sequence " + sequence);
            return;
        }
        rider.SelectedSequence = sequence;
        if (rider.Runtime.Animator == null || rider.Runtime.Animator.SequenceIndex != sequence ||
            rider.Model.AnimatedSequence != sequence)
        {
            try { anim.SwitchToSequence(rider, sequence); }
            catch (System.Exception e)
            {
                Debug.LogWarning("WMV: mount: switching the character to its riding sequence " + sequence + " failed: " +
                                 e.GetType().Name + ": " + e.Message);
            }
        }
        WmvM2Animator animator = rider.Runtime.Animator;
        if (animator != null && rider.Model.AnimatedSequence == sequence)
        {
            bool newer = rider.HaveAppState && rider.LastAppState.sequenceIndex == sequence &&
                         rider.LastAppState.receivedSeconds >= describedAt;
            float at;
            string from;
            if (!newer && mountRanMs >= 0.0 && mounted != null && mounted.Mount.HaveAppState)
            {
                at = WmvSlotAnimation.StartInStep(rider, mountRanMs, mounted.Mount.LastAppState.playing, animator.Speed);
                from = "in step with the mount, at " + at.ToString("F0") + " ms";
            }
            else
            {
                at = WmvSlotAnimation.StartClock(rider, describedAt, true, animator.Speed);
                from = newer ? "from the app's state for it, at " + at.ToString("F0") + " ms" : "from its first frame";
            }
            Debug.Log("WMV: mount: the character plays its riding sequence " + sequence + " (animID " +
                      rider.Model.Sequences[sequence].AnimId + ") " + from);
        }
    }

    /// <summary>
    /// The frame the character comes off its mount: the selection the host made for it as it took it off, held until
    /// now (HandleModelAnimation), is played, with the state the host sent for it -- so the character's own idle starts
    /// on the ground, not on the mount's back the frame before.
    /// </summary>
    void ApplyHeldRiderSelection()
    {
        riderSelectionHeld = false;
        int sequence = currentSlot.SelectedSequence;
        if (sequence < 0 || currentSlot.Runtime == null)
            return;
        Debug.Log("WMV: anim: the character is off its mount -- sequence " + sequence + ", selected by the host as it took it off");
        try { anim.SelectSequence(currentSlot, sequence, false); }
        catch (System.Exception e)
        {
            Debug.LogWarning("WMV: anim: switching the dismounted character to sequence " + sequence + " failed: " +
                             e.GetType().Name + ": " + e.Message);
        }
        PoseMountedAtPinnedTime();
    }

    /// <summary>
    /// -wmvAnimTime holds every clock at one instant. When a mount goes on or the character comes off one, the models are
    /// posed there in that frame, before anything measures them: the mount first, then the character, whose billboard
    /// bones take their facing under the bone the mount's pose has just placed (WmvSlotAnimation.PoseMountedAt).
    /// </summary>
    void PoseMountedAtPinnedTime()
    {
        float t = WmvModelBuilder.Debug_.AnimTime;
        if (t < 0f)
            return;
        WmvRuntimeModel mount = mounted != null ? mounted.Mount.Runtime : null;
        WmvSlotAnimation.PoseMountedAt(mount, currentSlot.Runtime, t);
        Debug.Log("WMV: anim: -wmvAnimTime " + t + " ms: " +
                  (mount != null ? "the mount posed at it, then the character" : "the character posed at it"));
    }

    /// <summary>Drop a character load that will not be shown, and everything it built. Its scene was
    /// replaced, not refused, so it is answered "superseded".</summary>
    void AbandonCharacterJob(string reason)
    {
        if (job == null || !job.Character)
            return;
        if (job.Mount != null) job.Mount.Dispose();
        if (job.Dresser != null) job.Dresser.Dispose();
        if (job.Staged != null) job.Staged.Dispose();
        if (job.Scene != null)
            ipc.ReportCharacterSceneApplied(job.FileDataID, job.Load, job.Scene.revision, "superseded", reason, 0, 0, null, 0,
                                            WmvIpcClient.MountKeyOf(job.Scene), "none", "");
        job.Mount = null;
        job.Dresser = null;
        job.Staged = null;
    }

    void HandleModelTextures(WmvIpcClient.ModelTexturesResponse r)
    {
        if (job == null || r.requestId != job.PendingTextureList)
            return;
        job.PendingTextureList = null;
        AdoptGeosets(currentSlot, r);
        AdoptParticleColor(currentSlot, r);
        if (r.hasSubmeshVisible)
            KeepLoadSubmeshVisible(r.submeshVisible, 0);

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
                int fdid = PinTexture(slot, t.fileDataID);
                job.TexturesExpected++;
                job.PendingTextures[ipc.RequestAssetByFileDataID(fdid)] = slot;
                currentSlot.TextureIds[slot] = fdid;
                Debug.Log("WMV: texture slot " + slot + " (type " + t.type + ") -> fileDataID " +
                          fdid + " (" + (fdid == t.fileDataID ? t.source
                                         : "-wmvSkinTexture, host offered " + t.fileDataID) + ")");
            }
        }
        if (job.TexturesExpected == 0)
            BuildIfReady();
    }

    /// <summary>
    /// The file a slot should actually load: whatever -wmvSkinTexture pinned to it, else the one
    /// the host offered. A model with several skin variants otherwise shows whichever the app
    /// selected, and "the purple one" is not something a controlled comparison can hold fixed.
    /// </summary>
    static int PinTexture(int slot, int offered)
    {
        int pinned = WmvModelBuilder.Debug_.PinnedTexture(slot);
        return pinned > 0 ? pinned : offered;
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
        if (job.Character && (!job.SceneTexturesRequested || job.Staged != null)) return;

        bool staging = false;
        try
        {
            // THE APP'S SKIN PUSH FOR THIS MODEL, when the load did not ask the host itself. A
            // model whose every texture names a file never sends getModelTextures, so the push
            // is the only word about which geosets its display switches on and what colour its
            // particles take; it used to be dropped, and every geoset was drawn.
            if (haveLoadSkin && !job.AskedHost)
            {
                AdoptGeosets(currentSlot, loadSkin);
                AdoptParticleColor(currentSlot, loadSkin);
            }
            haveLoadSkin = false;

            long t0 = job.Clock.ElapsedMilliseconds;
            bool[] buildFlags = haveLoadSubmeshVisible ? loadSubmeshVisible : null;
            if (job.Character && job.Scene != null)
                buildFlags = WmvIpcClient.Flags(job.Scene.body.submeshVisible);
            var built = WmvModelBuilder.Build(job.Model, job.Skin, job.Textures,
                                              string.IsNullOrEmpty(job.Model.Name) ? "WoWModel" : job.Model.Name,
                                              s => Debug.LogWarning("WMV: " + s),
                                              job.Character ? null : currentSlot.Geosets,
                                              buildFlags);
            job.BuildMs = job.Clock.ElapsedMilliseconds - t0;
            if (job.Character)
            {
                // A CHARACTER IS SHOWN DRESSED. The body stays hidden, and the model on screen stays
                // where it is, until the dresser has built every part the scene names; then the whole
                // character replaces it in one frame (AdoptStaged).
                haveLoadSubmeshVisible = false;
                loadSubmeshVisible = null;
                loadGeosetRevision = 0;
                built.Root.SetActive(false);
                job.Staged = built;
                job.Dresser = new WmvCharacterDresser(ipc, job.Load, ImageByHash, s => Debug.Log("WMV: " + s));
                // Before BeginStaged, which may apply the scene at once: a scene with a mount waits for it.
                WmvCharacterDresser dressedBy = job.Dresser;
                dressedBy.CommitGate = scene => MountReady(dressedBy, scene);
                dressedBy.OnCommitted = scene => CommitMount(dressedBy, scene);
                LoadJob staged = job;
                staging = true;
                Debug.Log(string.Format("WMV: character body built in {0} ms ({1} submeshes, {2} materials) -- dressing it",
                                        job.BuildMs, built.SubmeshCount, built.Materials.Length));
                job.Dresser.BeginStaged(built, job.Model, job.FileDataID, job.Scene, job.TexturesScene, job.Textures,
                                        () => AdoptStaged(staged));
                return;
            }
            if (haveLoadSubmeshVisible)
            {
                // Build ignores a list that does not fit the skin, and says so; report which it was.
                bool used = built.SubmeshVisible != null;
                ipc.ReportGeosetsApplied(job.FileDataID, loadGeosetRevision, used ? "applied" : "rejected",
                                         used ? "" : string.Format("the host listed {0} submeshes, the skin has {1}",
                                                                   loadSubmeshVisible.Length, built.SkinSubmeshCount),
                                         WmvModelBuilder.EffectiveSubmeshVisibility(built),
                                         WmvModelBuilder.DrawnTriangleCount(built), 0.0);
            }
            haveLoadSubmeshVisible = false;
            loadSubmeshVisible = null;
            loadGeosetRevision = 0;

            AdoptBuilt(built);
        }
        catch (WowParseException e) { Fail("mesh creation failed: " + e.Message); }
        catch (System.Exception e)
        {
            // Anything else out of Build used to escape to the IPC client's catch, which logged
            // "handler failed" and left the previous model and camera on screen with a job that
            // never completed. Same outcome as a parse failure: reported, and the load is over.
            Fail("mesh creation failed: " + e.GetType().Name + ": " + e.Message);
        }
        finally { if (!staging) job = null; }
    }

    /// <summary>The dresser has finished the character it was staged with: put it on screen.</summary>
    void AdoptStaged(LoadJob staged)
    {
        if (job != staged || staged.Staged == null)
            return;                                  // superseded while it was being dressed
        WmvRuntimeModel built = staged.Staged;
        WmvCharacterDresser dressedBy = staged.Dresser;
        WmvMountedScene mount = staged.Mount;
        staged.Staged = null;
        staged.Dresser = null;
        staged.Mount = null;
        try
        {
            built.Root.SetActive(true);
            AdoptBuilt(built);
            dresser = dressedBy;
            // The load's own mount replaces the one on screen only now, after AdoptBuilt disposed that one with the
            // character it carried; the dresser's commit, still in this frame, seats the new character on it.
            mounted = mount;
            mount = null;
            int renderers, materials, textures;
            dresser.Measure(out renderers, out materials, out textures);
            Debug.Log(string.Format("WMV: character on screen {0} ms after the load began: body {1} material(s), " +
                                    "{2} part(s) with {3} renderer(s), {4} material(s), {5} texture(s)",
                                    staged.Clock.ElapsedMilliseconds, built.Materials.Length, dresser.PartCount,
                                    renderers, materials, textures));
        }
        catch (System.Exception e)
        {
            if (mount != null) mount.Dispose();          // never adopted: nothing else holds it
            Fail("character adoption failed: " + e.GetType().Name + ": " + e.Message);
            return;
        }
        job = null;
    }

    /// <summary>Replace the model on screen with a freshly built one, and bring it up to the app's state.</summary>
    void AdoptBuilt(WmvRuntimeModel built)
    {
        {
            // A mount the previous character rode goes first, and it takes the character off before its root is
            // destroyed (WmvMountedScene.Dispose); a mount built for THIS load is not it (LoadJob.Mount).
            if (mounted != null) { mounted.Dispose(); mounted = null; }
            // What the previous character wore is parented to its body, which goes next.
            if (dresser != null) { dresser.Dispose(); dresser = null; }
            if (currentSlot.Runtime != null) currentSlot.Runtime.Dispose();   // never leak the previous model
            DisposeMapObject();                                               // ... nor a world model it replaces
            currentSlot.Runtime = built;
            // The emitters were created by that build; the override arrived with the textures.
            ApplyParticleColor(currentSlot);
            if (placeholder != null) placeholder.SetActive(false);

            // Keep what a later skin change needs: the parsed model (to map a texture type onto
            // slots) and the decoded textures (so untouched slots are not re-fetched).
            currentSlot.Model = job.Model;
            currentSlot.M2Bytes = job.M2Bytes;
            currentSlot.Name = string.IsNullOrEmpty(job.Model.Name) ? "WoWModel" : job.Model.Name;
            currentSlot.FileDataID = job.FileDataID;
            currentSlot.Textures.Clear();
            foreach (var kv in job.Textures) currentSlot.Textures[kv.Key] = kv.Value;
            skinJob = null;

            // THE APP'S PLAYBACK STATE FOR THIS MODEL, pushed while it was still loading. Without
            // it the fresh animator starts at 0 and playing at 1x whatever the app is doing, and
            // the first heartbeat -- a second later -- snaps it forward by however long the load
            // took: the visible restart on every switch. The position is projected by the time
            // the push has been waiting, at the app's own speed, so the two clocks meet.
            if (currentSlot.Runtime.Animator != null && haveLoadState)
            {
                currentSlot.LastAppState = loadState;
                currentSlot.HaveAppState = true;
                int playingSeq = currentSlot.Model.AnimatedSequence;
                if (loadState.sequenceIndex < 0 || loadState.sequenceIndex == playingSeq)
                {
                    float elapsed = loadState.playing && loadStateAt > 0.0
                        ? (float)((WmvIpcClient.NowSeconds - loadStateAt) * 1000.0) * Mathf.Max(loadState.speed, 0f)
                        : 0f;
                    currentSlot.Runtime.Animator.StartFromApp(loadState.playing, loadState.timeMs + elapsed, loadState.speed);
                }
                else
                {
                    currentSlot.Runtime.Animator.SetTransportOnly(loadState.playing, loadState.speed);
                }
            }

            // AN ANIMATION PICKED WHILE THE MODEL WAS LOADING. The sequence was resolved when the .m2 --
            // or a character's .skel -- was parsed, and a selection that arrived after that was only
            // stored: a character is fetched, built and dressed for seconds after its .skel, nothing
            // asked for the selection again, and the host played one animation while this viewport
            // played another until the user picked again. A selection whose keys are in a .anim had
            // the same fate, because the parse has no .anim to read and falls back to the idle. Both
            // go through the ordinary switch, which fetches the .anim when there is one and then
            // applies the app's playback state. A selection already playing is not touched, and one
            // the parse fell back from for a reason a second read cannot change is not read again.
            int selected = currentSlot.SelectedSequence;
            if (selected >= 0 && selected != currentSlot.Model.AnimatedSequence && !WmvModelBuilder.Debug_.NoAnim &&
                (selected != job.ParsedSequence || M2Parser.ExternalAnimFileId(currentSlot.Model, selected) != 0))
            {
                // The switch applies the app's state from LastAppState; the push kept for this load is the
                // newest word on it (the block above has already taken it when the build made an animator).
                if (haveLoadState)
                {
                    currentSlot.LastAppState = loadState;
                    currentSlot.HaveAppState = true;
                }
                Debug.Log("WMV: anim: sequence " + selected + " was selected while the model was loading -- switching to it");
                // Contained here, unlike a pick on a model already on screen: this runs in the middle of the
                // adoption, and anything escaping it would leave the model unframed and report a character
                // that IS on screen as a failed load. The load completes and the failed switch is logged.
                try { anim.SwitchToSequence(currentSlot, selected); }
                catch (System.Exception e)
                {
                    Debug.LogWarning("WMV: anim: switching to sequence " + selected + " failed: " +
                                     e.GetType().Name + ": " + e.Message);
                }
            }
            haveLoadState = false;

            // Ask for this creature's .anim files now rather than when one is first played, so no
            // animation switch ever waits on a round trip. They arrive while the user is looking
            // at the model, not while they are waiting for the animation they just picked. After
            // the switch above, so a file that switch is already fetching is not asked for twice.
            anim.PrefetchAnimFiles(currentSlot);

            // -wmvFrameBounds pins EVERYTHING that frames from the bounds -- the orbit camera, the
            // light rig and the light check -- so two builds that disagree about the bounds
            // (one keeps batches the other dropped) are lit and framed identically.
            if (WmvModelBuilder.Debug_.HasFrameBounds)
            {
                Debug.Log(string.Format("WMV: bounds pinned by -wmvFrameBounds for framing and the light rig (model's own: centre {0} extents {1})",
                                        built.Bounds.center, built.Bounds.extents));
                built.Bounds = WmvModelBuilder.Debug_.FrameBounds;
            }
            ViewFramings++;
            orbit.Frame(built.Bounds);
            KeepFramed(built.Bounds);
            ApplyViewportOrbitOverride("");
            if (shadowRig != null)
                shadowRig.SetBounds(built.Bounds);

            // -wmvAnimTime: pose the model at that instant BEFORE anything measures it. The
            // animator's own LateUpdate has not run yet at this point, so without this the light
            // check would capture the rest pose whatever time was asked for.
            // -wmvSeqPath: walk the sequences in order first, through the same switch the host
            // drives, so a capture can show the state AFTER a sequence change.
            int[] seqPath = WmvModelBuilder.Debug_.SeqPath;
            for (int i = 0; i < seqPath.Length; i++)
            {
                Debug.Log("WMV: seqpath: switching to sequence " + seqPath[i]);
                anim.SwitchToSequence(currentSlot, seqPath[i]);
            }
            if (WmvModelBuilder.Debug_.AnimTime >= 0f)
                WmvModelBuilder.PoseAt(currentSlot.Runtime, WmvModelBuilder.Debug_.AnimTime);

            if (WmvModelBuilder.Debug_.AllocCheck)
                allocProbe = new AllocProbe { StartFrame = Time.frameCount + 10 };

            // The light check brings its own camera and frames the model itself, so it no longer
            // depends on this call having happened -- an earlier version measured before framing
            // and reported a few stray pixels as a reading.
            if (WmvModelBuilder.Debug_.LightCheck)
                ReportLighting();

            // SCRATCH: capture the REAL viewport, post-processing included.
            RequestViewportShot();

            // Independent of the model just loaded -- it brings its own geometry, materials and
            // camera -- but hung off the same hook so one headless run produces both readings.
            if (WmvModelBuilder.Debug_.QueueProof)
                ReportQueueOrder();

            status.Set("Loaded " + job.Path);
            status.Set(string.Format("Vertices {0}  Triangles {1}  Submeshes {2}  Textures {3}",
                                     built.VertexCount, built.TriangleCount, built.SubmeshCount, job.Textures.Count));
            if (currentSlot.Geosets != null)
                status.Set("Geosets " + (currentSlot.Geosets.Count == 0 ? "none" : string.Join(",",
                           new List<int>(currentSlot.Geosets).ConvertAll(x => x.ToString()).ToArray())));
            status.Set(string.Format("Bounds {0} size {1}", built.Bounds.center, built.Bounds.size));
            status.Set(string.Format("Load {0} ms (m2 {1}, skin {2}, tex {3}, parse {4}, build {5})",
                                     job.Clock.ElapsedMilliseconds, job.M2Ms, job.SkinMs, job.TextureMs,
                                     job.ParseMs, job.BuildMs));
            Debug.Log(string.Format(
                "WMV: loaded {0} -- {1} vertices, {2} triangles, {3} submeshes, {4} texture(s), bounds {5}, {6} ms",
                job.Path, built.VertexCount, built.TriangleCount, built.SubmeshCount, job.Textures.Count,
                built.Bounds, job.Clock.ElapsedMilliseconds));
        }
    }

    /// <summary>
    /// Is a push about the model on screen, or about the one being loaded?
    ///
    /// While a switch is in flight the model on screen is still the PREVIOUS one, and the app's
    /// pushes for the new one -- its skin, its default animation, its playback state -- name the
    /// new file. Judging them by the on-screen id alone threw every one of them away, and the
    /// new model then started on the animator's own defaults and was snapped forward by the first
    /// heartbeat a second later. A push about neither model is still refused.
    /// </summary>
    bool AboutThisModel(int fileDataID)
    {
        if (fileDataID == 0)
            return true;
        if (job != null && job.FileDataID == fileDataID)
            return true;
        return currentSlot.FileDataID == 0 || fileDataID == currentSlot.FileDataID;
    }

    /// <summary>About the load in flight, specifically -- as opposed to the model on screen.</summary>
    bool AboutTheLoad(int fileDataID)
    {
        return job != null && (fileDataID == 0 || fileDataID == job.FileDataID);
    }

    /// <summary>
    /// WMV's displayed animation changed. The renderer draws the same model from the same data, so
    /// it plays what the app plays rather than the idle it would pick for itself.
    ///
    /// Only one sequence's keyframes are held at a time, so this re-parses the .m2 kept from the
    /// load with the new sequence in mind and re-binds the animator to the result. Nothing else
    /// moves: the mesh, its materials, its textures and its geoset selection are all untouched by
    /// which animation is playing.
    ///
    /// A push with a role is about one of a ridden mount's two models (HandleRoleSelection). One without is about the
    /// model it names, as always -- except while the character on screen rides: a host that seats characters sends
    /// those pushes with a role, so the character's own selection without one comes from the host taking it off its
    /// mount, a tick before the scene without the mount, and is held for the frame that scene is applied.
    /// </summary>
    void HandleModelAnimation(WmvIpcClient.AnimationSelection a)
    {
        if (!string.IsNullOrEmpty(a.role))
        {
            HandleRoleSelection(a);
            return;
        }
        if (PushIsAboutMapObject(a.fileDataID))
            return;                                     // a world model has no animation
        if (!AboutThisModel(a.fileDataID))
            return;                                     // about a different model
        if (a.sequenceIndex < 0)
            return;

        string why;
        if (WmvSlotAnimation.RouteSelection(a.role, a.load, a.fileDataID, Holding(), out why) ==
            WmvSlotAnimation.Route.HeldForDismount)
        {
            currentSlot.SelectedSequence = a.sequenceIndex;
            riderSelectionHeld = true;
            Debug.Log("WMV: anim: sequence " + a.sequenceIndex + " for the character, with no role while it rides -- " +
                      "the host has taken it off its mount; held until the scene without the mount is applied");
            return;
        }
        anim.SelectSequence(currentSlot, a.sequenceIndex, AboutTheLoad(a.fileDataID));
    }

    /// <summary>
    /// A selection about one of a ridden mount's two models (protocol 5), routed by WmvSlotAnimation.RouteSelection:
    /// role "rider" is the character, on screen or -- by its load serial -- being loaded, and plays or waits exactly as a
    /// selection for the model on screen or the load does; role "mount" is the mount the character rides on screen. A
    /// mount still being prepared, for either, keeps the selection for the frame it goes on (StartMountClock). Anything
    /// else -- a mount role while the character rides no mount of that file, a push about a character not here -- is
    /// ignored, and logged.
    /// </summary>
    void HandleRoleSelection(WmvIpcClient.AnimationSelection a)
    {
        if (a.sequenceIndex < 0)
            return;
        string why;
        switch (WmvSlotAnimation.RouteSelection(a.role, a.load, a.fileDataID, Holding(), out why))
        {
            case WmvSlotAnimation.Route.Rider:
                riderSelectionHeld = false;
                riderPickAt = a.receivedSeconds;
                Debug.Log("WMV: anim: role \"rider\" sequence " + a.sequenceIndex + " -> the character");
                anim.SelectSequence(currentSlot, a.sequenceIndex, false);
                break;
            case WmvSlotAnimation.Route.RiderOfLoad:
                riderPickAt = a.receivedSeconds;
                Debug.Log("WMV: anim: role \"rider\" sequence " + a.sequenceIndex + " -> the character being loaded");
                anim.SelectSequence(currentSlot, a.sequenceIndex, true);
                break;
            case WmvSlotAnimation.Route.Mount:
                Debug.Log("WMV: anim: role \"mount\" sequence " + a.sequenceIndex + " -> the mount the character rides (" +
                          mounted.Key + ")");
                anim.SelectSequence(mounted.Mount, a.sequenceIndex, false);
                StartMountProbe("the mount was given sequence " + a.sequenceIndex);
                break;
            case WmvSlotAnimation.Route.MountPreparing:
                keptMountPick = a;
                haveKeptMountPick = true;
                Debug.Log("WMV: anim: mount sequence " + a.sequenceIndex + " for fileDataID " + a.fileDataID +
                          ", still being prepared -- kept for the frame it goes on");
                break;
            default:
                Debug.Log("WMV: anim: role \"" + a.role + "\" sequence " + a.sequenceIndex + " for fileDataID " +
                          a.fileDataID + " (load " + a.load + ") ignored: " + why);
                break;
        }
    }

    /// <summary>What the player holds, as WmvSlotAnimation.RouteSelection routes a push by it.</summary>
    WmvSlotAnimation.Holding Holding()
    {
        var h = new WmvSlotAnimation.Holding();
        if (job != null || wmoJob != null)
        {
            h.Loading = true;
            h.LoadIsCharacter = job != null && job.Character;
            h.LoadSerial = job != null ? job.Load : 0;
            h.LoadMountPreparing = job != null && job.Mount != null ? job.Mount.PreparingFileDataID : 0;
        }
        if (dresser != null && currentSlot.Runtime != null)
        {
            h.RiderLoad = dresser.Load;
            h.RiderFileDataID = currentSlot.FileDataID;
            h.RiderMounted = mounted != null && mounted.Rider != null;
        }
        if (mounted != null)
        {
            h.MountFileDataID = mounted.RiddenFileDataID;
            h.MountPreparing = mounted.PreparingFileDataID;
        }
        return h;
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
        ReportLighting(currentSlot.Runtime != null ? currentSlot.Runtime.Bounds : new Bounds(), false);
    }

    /// <param name="framed">The box the check frames: the model's own, or, for a character riding its mount, both
    /// models together in the world (FrameRiddenScene). -wmvFrameBounds still takes its place.</param>
    /// <param name="riding">The character rides the mount on screen: the mount is named and its textures listed too.</param>
    void ReportLighting(Bounds framed, bool riding)
    {
        Camera src = Camera.main;
        WmvRuntimeModel onScreen = currentSlot.Runtime;
        if (src == null || onScreen == null)
        {
            Debug.Log("WMV: lightcheck: no camera or no model -- nothing measured");
            return;
        }
        riding = riding && mounted != null && mounted.Mount.Runtime != null;

        const int W = 512, H = 512;
        const float Fov = 60f;
        float Yaw = WmvModelBuilder.Debug_.LightYaw;
        float Pitch = WmvModelBuilder.Debug_.LightPitch;

        // FRAMING, from the bounds and two fixed angles. Not from the orbit: the viewport camera
        // may have been moved by a Frame() call, a drag or a wheel, and a measurement that moves
        // with it compares two different pictures.
        Bounds b = WmvModelBuilder.Debug_.HasFrameBounds ? WmvModelBuilder.Debug_.FrameBounds : framed;
        if (WmvModelBuilder.Debug_.HasFrameBounds)
            Debug.Log(string.Format("WMV: lightcheck: framing bounds pinned by -wmvFrameBounds (model's own: centre ({0:F2},{1:F2},{2:F2}) extents ({3:F2},{4:F2},{5:F2}))",
                                    framed.center.x, framed.center.y, framed.center.z,
                                    framed.extents.x, framed.extents.y, framed.extents.z));
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

            Color bg = ViewportClear;   // as displayed; GrabFrame converts for the current domain
            float bgLum = 0.2126f * bg.r + 0.7152f * bg.g + 0.0722f * bg.b;
            // WHICH SKIN WAS MEASURED, spelled out. WMV picks a random skin per load unless
            // Session/RandomLooks is off, and a creature can carry skins that differ in brightness
            // by more than any light rig ever will -- chicken2 spans 2.4x across its four. A
            // before/after pair taken in two processes is therefore not comparable unless this
            // line matches, and until it was printed there was no way to notice that it did not.
            var skinIds = new List<string>();
            foreach (var kv in currentSlot.TextureIds) skinIds.Add(kv.Key + ":" + kv.Value);
            skinIds.Sort();
            if (riding)
            {
                // The mount's slots after the character's: both models are in the frame and in the mask.
                var mountIds = new List<string>();
                foreach (var kv in mounted.Mount.TextureIds) mountIds.Add("mount " + kv.Key + ":" + kv.Value);
                mountIds.Sort();
                skinIds.AddRange(mountIds);
            }
            Debug.Log(string.Format(
                "WMV: lightcheck: {0} | camera yaw {1} pitch {2} fov {3} dist {4:F3} "
                + "| bounds extents ({5:F2},{6:F2},{7:F2}) | background {8:F4} | textures {9}",
                riding ? currentSlot.Name + " riding " + mounted.Mount.Name + " (" + mounted.Key + ")" : currentSlot.Name,
                Yaw, Pitch, Fov, dist, e.x, e.y, e.z, bgLum,
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
                // There is nothing to MEASURE, but there may still be something to LOOK at: an
                // all-additive model, or one additive submesh of one under -wmvOnlySubmesh. The
                // measurement needs an opaque mask; a picture does not, so write the frame under
                // the same name and rig the measuring path would have used.
                if (WmvModelBuilder.Debug_.LightDump)
                {
                    Shader.SetGlobalFloat("_WmvRig", 0f);
                    Color32[] blended = GrabFrame(cam, rt, bg, W, H);
                    Shader.SetGlobalFloat("_WmvRig", restoreRig);
                    DumpPng(blended, W, H, "wmv-lightcheck-map+contact-on.png");
                    Debug.Log("WMV: lightcheck: frame written anyway for -wmvLightDump");
                }
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
        cam.backgroundColor = ClearColour(clear);
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
    /// <summary>
    /// THE DISPLAY TRANSFORM. A model viewer shows what an artist painted; it does not photograph
    /// a scene, and it must not put a camera response curve between the two.
    ///
    /// The Unity project was created from the URP template, and the template's SampleSceneProfile
    /// ships with Tonemapping enabled in Neutral mode. Nobody chose that for WoW content. Combined
    /// with the pipeline asset's ColorGradingMode = LowDynamicRange, which clamps to [0,1] before
    /// the grading LUT, it is a hard ceiling on the whole renderer:
    ///
    ///     NeutralTonemap(1.0) = 0.5444 linear  ->  sRGB byte 195
    ///
    /// A pure white surface could not exceed 195, and nothing could reach 250 at all. Measured on
    /// item 207140 (Drakestalker's Trophy Pauldrons): with the tone curve enabled, 0.00 % of the
    /// model's pixels reached 250 in any channel and 5.0 % reached 200; with it disabled, 3.3 %
    /// and 14.2 %. The authored lava network -- the whole point of that material -- was being
    /// deleted after the material had drawn it correctly.
    ///
    /// Neither renderer of record applies a tone curve: retail's M2 path has none, and this
    /// application's OpenGL viewport saturates at the framebuffer. So it goes.
    ///
    /// The vignette goes with it, for the same reason: it is a photographic affectation, it is not
    /// in either reference, and it darkens the frame edges of a capture.
    ///
    /// BLOOM STAYS. Glowing WoW content does bleed light, the effect is authored for, and it now
    /// operates on genuine linear values above 1 rather than on a range that was clamped flat.
    ///
    /// WHERE THE FRAME IS DECODED. The authored working space needs one conversion to linear
    /// somewhere before the swapchain. It used to be the last line of each fragment, and that was
    /// exact for a lone fragment and wrong for every stack: the hardware blend then ran on linear
    /// numbers while Wowhead's viewer, the legacy OpenGL viewport and the game all blend the
    /// authored values. Two additive layers authored at 0.5 reach 255 in all three references and
    /// 175 summed in linear; a lone additive particle fading at alpha a displays a in the
    /// references and OETF(a) here. So the fragments now write authored values, the blend runs on
    /// them, and WmvFrameDecodePass converts the finished composite once, before post-processing
    /// (see that file). A lone fragment lands on exactly the same byte as before.
    ///
    /// The camera's clear colour has to be given in the same domain as the fragments: Unity
    /// treats backgroundColor as sRGB and linearises it on clear, so to leave the AUTHORED value
    /// in the buffer the colour is passed through its own OETF first (Color.gamma). The frame
    /// decode then brings it back to the linear value the swapchain expects and the background
    /// displays as (25,25,30), as it always has.
    ///
    /// WMV_DISPLAY selects, for A/B only:
    ///   full      (default) authored working space, decoded once per FRAME, no tone curve,
    ///                       no vignette
    ///   fragment            authored working space decoded per FRAGMENT: the behaviour before
    ///                       the frame decode, so the two can be differenced from one build
    ///   notonemap           tone curve and vignette off, old sampling domain
    ///   legacy              exactly what the renderer did before any of this
    /// </summary>
    static void ConfigureDisplayTransform()
    {
        string want = System.Environment.GetEnvironmentVariable("WMV_DISPLAY");
        if (string.IsNullOrEmpty(want)) want = "full";
        bool legacy = (want == "legacy");
        bool authored = (want == "full" || want == "fragment");
        bool frameDecode = (want == "full");

        WmvModelBuilder.AuthoredTextureDomain = authored;
        Shader.SetGlobalFloat("_WmvAuthoredDomain", authored ? 1f : 0f);

        if (frameDecode && !WmvFrameDecodePass.SetActive(true))
        {
            // Without the shader nothing would ever decode the frame: fall back to the fragment
            // encode rather than present authored values as linear.
            Debug.LogWarning("WMV: display transform 'full' needs Wmv/FrameDecode, which is "
                             + "missing -- decoding per fragment instead");
            frameDecode = false;
        }
        if (!frameDecode)
            WmvFrameDecodePass.SetActive(false);
        Shader.SetGlobalFloat("_WmvShaderEncode", authored && !frameDecode ? 1f : 0f);
        ClearInAuthoredDomain = frameDecode;

        // THE BUFFER HAS TO HOLD AUTHORED VALUES WITHOUT BANDING. The pipeline asset asks for a
        // 32-bit HDR buffer, which URP satisfies with B10G11R11: 6-bit mantissas in red and green,
        // 5 in blue. That was fine for linear light, where the swapchain's encode spreads the
        // top of the range out, but an authored value is already perceptual and the same format
        // quantises [0.5, 1) to ~1/128 in red and green and ~1/64 in blue -- two and four 8-bit
        // steps -- which bands in smooth bright gradients, blue worst. FP16 has a 10-bit mantissa
        // and does not. The asset is not in the repository, so the request is made here.
        if (frameDecode)
        {
            var rp = UnityEngine.Rendering.GraphicsSettings.currentRenderPipeline
                     as UnityEngine.Rendering.Universal.UniversalRenderPipelineAsset;
            if (rp != null)
            {
                var had = rp.hdrColorBufferPrecision;
                rp.hdrColorBufferPrecision =
                    UnityEngine.Rendering.Universal.HDRColorBufferPrecision._64Bits;
                Debug.Log(string.Format("WMV: HDR colour buffer precision {0} -> {1} (authored "
                                        + "values need the mantissa)", had, rp.hdrColorBufferPrecision));
            }
        }

        var mainCam = Camera.main;
        if (mainCam != null)
            mainCam.backgroundColor = ClearColour(ViewportClear);

        int touched = 0;
        var vols = UnityEngine.Object.FindObjectsByType<UnityEngine.Rendering.Volume>(
                       FindObjectsSortMode.None);
        foreach (var v in vols)
        {
            if (v == null || v.profile == null) continue;
            UnityEngine.Rendering.Universal.Tonemapping tm;
            UnityEngine.Rendering.Universal.Vignette vg;

            // Setting VolumeComponent.active alone does NOT take effect here -- measured: four
            // variants came back byte-identical to the baseline that way. Overriding the
            // PARAMETER is what the pipeline actually reads.
            if (v.profile.TryGet(out tm))
            {
                tm.mode.overrideState = true;
                tm.mode.value = legacy ? UnityEngine.Rendering.Universal.TonemappingMode.Neutral
                                       : UnityEngine.Rendering.Universal.TonemappingMode.None;
                touched++;
            }
            if (v.profile.TryGet(out vg))
            {
                vg.intensity.overrideState = true;
                vg.intensity.value = legacy ? 0.2f : 0f;
                touched++;
            }
        }
        Debug.Log(string.Format(
            "WMV: display transform '{0}' -- authored texture domain {1}, decode {2}, "
            + "{3} volume parameter(s) set across {4} volume(s)",
            want, WmvModelBuilder.AuthoredTextureDomain ? "ON" : "off",
            frameDecode ? "once per FRAME (blends in the authored domain)"
                        : (authored ? "per FRAGMENT (blends in linear)" : "none"),
            touched, vols != null ? vols.Length : 0));
    }

    /// <summary>The viewport's background, as displayed: (25,25,30).</summary>
    static readonly Color ViewportClear = new Color(0.10f, 0.10f, 0.12f);

    /// <summary>True while WmvFrameDecodePass is decoding the frame, so a clear colour has to be
    /// left in the buffer in the authored domain. See ConfigureDisplayTransform.</summary>
    static bool ClearInAuthoredDomain;

    /// <summary>A camera clear colour in whichever domain the frame is currently composited in.
    /// Black and white are fixed points of the curve and come back unchanged.</summary>
    static Color ClearColour(Color displayed)
    {
        return ClearInAuthoredDomain ? displayed.gamma : displayed;
    }

    // SCRATCH: THE MODEL CAPTURE (WMV_VIEWPORT_SHOT). One capture waits at a time; a newer request moves it on.
    const int ViewportShotFrames = 40;         // frames between the last change on screen and the capture
    const double ViewportShotBusyLimitSeconds = 30.0;
    int viewportShotDue = -1;                  // the frame the waiting capture may be taken from, -1 for none
    bool viewportShotWaiting;
    int viewportShots;                         // captures taken, numbered in the log
    Bounds lastFramed;                         // the box the camera was last framed on, and the aspect it was framed at
    float lastFramedAspect;
    bool haveLastFramed;

    /// <summary>
    /// SCRATCH: capture the viewport (WMV_VIEWPORT_SHOT) once what is on screen has settled: 40 frames after the last
    /// request, and not while a load, a character's scene, a mount being prepared or a sequence switch waiting for its
    /// .anim is still on its way (for at most 30 seconds). A request is made when a model is put on screen, when the
    /// mount under a character changes, and while a character rides, when either model's sequence switch completes
    /// and when a newer scene is applied to the character -- so the capture shows the mount's commit, the clip asked
    /// for (and a pinned -wmvAnimTime pose), not a moment before them. A request while one waits moves it on.
    /// </summary>
    void RequestViewportShot()
    {
        string shot = System.Environment.GetEnvironmentVariable("WMV_VIEWPORT_SHOT");
        if (string.IsNullOrEmpty(shot))
            return;
        viewportShotDue = Time.frameCount + ViewportShotFrames;
        if (!viewportShotWaiting)
            StartCoroutine(CaptureViewportWhenSettled(shot));
    }

    /// <summary>What the viewport is still waiting on before a capture shows a settled state, or null.</summary>
    string ViewportBusy()
    {
        if (job != null || wmoJob != null)
            return "a load is in flight";
        if (dresser != null && dresser.Busy)
            return "the character's scene is being prepared";
        if (mounted != null && mounted.PreparingFileDataID != 0)
            return "a mount is being prepared";
        if (currentSlot.PendingAnimFetch.Count > CountPrefetches(currentSlot))
            return "the model's sequence switch waits for its .anim";
        if (mounted != null && mounted.Mount.PendingAnimFetch.Count > CountPrefetches(mounted.Mount))
            return "the mount's sequence switch waits for its .anim";
        return null;
    }

    /// <summary>The .anim fetches of a slot nothing waits on (prefetches, -1).</summary>
    static int CountPrefetches(WmvModelSlot slot)
    {
        int n = 0;
        foreach (int waiting in slot.PendingAnimFetch.Values)
            if (waiting < 0) n++;
        return n;
    }

    System.Collections.IEnumerator CaptureViewportWhenSettled(string name)
    {
        viewportShotWaiting = true;
        string busy = null;
        while (true)
        {
            double busySince = -1.0;
            while (true)
            {
                yield return new WaitForEndOfFrame();
                if (Time.frameCount < viewportShotDue)
                    continue;
                busy = ViewportBusy();
                if (busy == null)
                    break;
                if (busySince < 0.0)
                    busySince = Time.realtimeSinceStartupAsDouble;
                else if (Time.realtimeSinceStartupAsDouble - busySince > ViewportShotBusyLimitSeconds)
                    break;                                   // taken anyway, and said
            }
            // THE SIZE ASKED FOR. The host lays out its panes again when, say, a saved character is loaded, and resizes
            // the player to its pane: the capture asks for its size again, and when that changes the camera's aspect the
            // last box is framed again the way it was (the orbit override re-applied after it).
            int want = RequestedViewportSize();
            if (want > 0 && (Screen.width != want || Screen.height != want))
            {
                int fromW = Screen.width, fromH = Screen.height;
                Screen.SetResolution(want, want, false);
                for (int i = 0; i < 30 && (Screen.width != want || Screen.height != want); i++)
                    yield return null;
                Debug.Log(string.Format("WMV: viewport shot {0}: the screen was {1}x{2}, asked for {3}x{3} again -> {4}x{5}",
                                        name, fromW, fromH, want, Screen.width, Screen.height));
                Camera cam = Camera.main;
                if (haveLastFramed && cam != null && Mathf.Abs(cam.aspect - lastFramedAspect) > 1e-3f)
                {
                    orbit.Frame(lastFramed);
                    lastFramedAspect = cam.aspect;
                    ApplyViewportOrbitOverride("shot: ");
                }
                for (int i = 0; i < 5; i++)
                    yield return new WaitForEndOfFrame();
            }
            if (Time.frameCount >= viewportShotDue)
                break;                                       // no newer request came in meanwhile
        }
        viewportShotDue = -1;
        viewportShotWaiting = false;
        TakeViewportShot(name, busy);
    }

    /// <summary>SCRATCH: the box a model or a mounted character was just framed on, and the camera's aspect then, for a
    /// capture that has to ask for its size again (CaptureViewportWhenSettled). Nothing else reads it.</summary>
    void KeepFramed(Bounds framed)
    {
        Camera cam = Camera.main;
        lastFramed = framed;
        lastFramedAspect = cam != null ? cam.aspect : 1f;
        haveLastFramed = true;
    }

    /// <summary>The n x n size a capture asks for: WMV_VIEWPORT_SIZE, 1024 by default (as Awake), 0 when out of range.</summary>
    static int RequestedViewportSize()
    {
        int v = 1024;
        string sz = System.Environment.GetEnvironmentVariable("WMV_VIEWPORT_SIZE");
        if (!string.IsNullOrEmpty(sz)) int.TryParse(sz, out v);
        return v >= 128 && v <= 4096 ? v : 0;
    }

    /// <summary>SCRATCH: a world model's capture, 40 frames after it went on screen (unchanged).</summary>
    System.Collections.IEnumerator CaptureViewport(string name)
    {
        for (int i = 0; i < ViewportShotFrames; i++)
            yield return new WaitForEndOfFrame();
        TakeViewportShot(name, null);
    }

    /// <summary>SCRATCH: the frame the embedded viewport actually presented, with what it shows in the log: the
    /// emitters of the model on screen and of the mount the character rides, then the camera, and for a character on a
    /// mount its key, the seat, both sequences and their clocks. busy says what the capture stopped waiting for.</summary>
    void TakeViewportShot(string name, string busy)
    {
        Texture2D tex = null;
        try
        {
            tex = ScreenCapture.CaptureScreenshotAsTexture();
            string path = System.IO.Path.Combine(Application.dataPath, "../" + name + ".png");
            System.IO.File.WriteAllBytes(path, ImageConversion.EncodeToPNG(tex));
            viewportShots++;
            var rt = currentSlot.Runtime != null ? currentSlot.Runtime.Emitters : null;
            WmvRuntimeModel ridden = currentSlot.Runtime != null && mounted != null && mounted.RiddenFileDataID != 0
                                     ? mounted.Mount.Runtime : null;
            var mt = ridden != null ? ridden.Emitters : null;
            Debug.Log(string.Format(
                "WMV: viewport shot {0} -- {1}x{2}, HDR camera {3}; emitters p={4} r={5} live={6} "
                + "segs={7} draws={8} skipped={9}{10}",
                name, tex.width, tex.height, Camera.main != null && Camera.main.allowHDR,
                rt != null ? rt.ParticleEmitterCount : 0, rt != null ? rt.RibbonEmitterCount : 0,
                rt != null ? rt.LiveParticleCount : 0, rt != null ? rt.RibbonSegmentCount : 0,
                rt != null ? rt.DrawCallCount : 0, rt != null ? rt.SkippedEmitterCount : 0,
                ridden == null ? "" : string.Format(
                    "; the mount {0} ({1}) emitters p={2} r={3} live={4} segs={5} draws={6} skipped={7}",
                    mounted.Key, mounted.RiddenFileDataID,
                    mt != null ? mt.ParticleEmitterCount : 0, mt != null ? mt.RibbonEmitterCount : 0,
                    mt != null ? mt.LiveParticleCount : 0, mt != null ? mt.RibbonSegmentCount : 0,
                    mt != null ? mt.DrawCallCount : 0, mt != null ? mt.SkippedEmitterCount : 0)));

            Camera cam = Camera.main;
            var inv = System.Globalization.CultureInfo.InvariantCulture;
            string animTime = WmvModelBuilder.Debug_.AnimTime >= 0f
                              ? "every clock pinned at " + WmvModelBuilder.Debug_.AnimTime.ToString("0.##", inv) + " ms (-wmvAnimTime)"
                              : "clocks running";
            Debug.Log(string.Format(inv,
                "WMV: viewport shot {0} #{1}: camera at ({2:F3}, {3:F3}, {4:F3}) on pivot ({5:F3}, {6:F3}, {7:F3}), yaw {8:0.##} " +
                "pitch {9:0.##} distance {10:F3}, field of view {11:0.##}, aspect {12:F3}; {13}; {14}; {15}{16}",
                name, viewportShots,
                cam != null ? cam.transform.position.x : 0f, cam != null ? cam.transform.position.y : 0f,
                cam != null ? cam.transform.position.z : 0f, orbit.pivot.x, orbit.pivot.y, orbit.pivot.z, orbit.yaw, orbit.pitch,
                orbit.distance, cam != null ? cam.fieldOfView : 0f, cam != null ? cam.aspect : 0f,
                DescribeClock("the character " + currentSlot.FileDataID, currentSlot),
                ridden == null ? "no mount"
                    : string.Format(inv, "the mount {0} ({1}) seat {2} bone {3} at ({4:F4}, {5:F4}, {6:F4}) scale {7}, {8}",
                                    mounted.Key, mounted.RiddenFileDataID,
                                    mounted.SeatCase == WmvMountedScene.CaseBone ? "B (under its bone)"
                                    : mounted.SeatCase == WmvMountedScene.CaseNoBoneTransform ? "C (on its root, no bone transform)"
                                    : mounted.SeatCase == WmvMountedScene.CaseNoAttachment ? "A (at its origin, no attachment)"
                                    : "none", mounted.SeatBone, mounted.SeatLocalPosition.x, mounted.SeatLocalPosition.y,
                                    mounted.SeatLocalPosition.z, mounted.SeatScale, DescribeClock("its clock", mounted.Mount)),
                animTime, busy == null ? "" : "; taken while " + busy));
        }
        catch (System.Exception e) { Debug.LogWarning("WMV: viewport shot failed: " + e.Message); }
        if (tex != null) Destroy(tex);
    }

    /// <summary>"what: sequence n (animID a) at t of l ms, playing|paused xS" for a slot's animator, for the capture log.</summary>
    static string DescribeClock(string what, WmvModelSlot slot)
    {
        WmvM2Animator a = slot != null && slot.Runtime != null ? slot.Runtime.Animator : null;
        if (a == null)
            return what + ": not animated";
        return string.Format(System.Globalization.CultureInfo.InvariantCulture,
                             "{0}: sequence {1} (animID {2}) at {3:F0} of {4:F0} ms, {5} x{6:0.##}", what, a.SequenceIndex, a.AnimId,
                             a.TimeMs, a.LengthMs, a.IsPlaying ? "playing" : "paused", a.Speed);
    }

    /// <summary>
    /// SCRATCH: WMV_VIEWPORT_ORBIT="yaw:pitch[:distanceScale]" re-aims the camera just framed -- a world model, a model,
    /// or a character with the mount it rides -- so a capture can be taken from a named view: the audit's OpenGL
    /// references of world models are yaw 135 / pitch 30 and yaw 315 / pitch 30 in this camera's terms, and the archived
    /// viewport's yaw Y and pitch P are yaw 180 - Y and pitch 90 - P here (OrbitCamera::updatePosition through
    /// WowCoordinateConverter). Unset, nothing changes. Documented with WMV_VIEWPORT_SHOT and WMV_VIEWPORT_SIZE under
    /// "Capture hooks" in docs/unity-renderer/README.md. what prefixes the log line ("wmo: ", "mount: ", or "" for a model).
    /// </summary>
    void ApplyViewportOrbitOverride(string what)
    {
        string v = System.Environment.GetEnvironmentVariable("WMV_VIEWPORT_ORBIT");
        if (string.IsNullOrEmpty(v))
            return;
        string[] p = v.Split(':');
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        float yaw, pitch, scale = 1f;
        if (p.Length < 2 ||
            !float.TryParse(p[0], System.Globalization.NumberStyles.Float, inv, out yaw) ||
            !float.TryParse(p[1], System.Globalization.NumberStyles.Float, inv, out pitch))
            return;
        if (p.Length > 2 && !float.TryParse(p[2], System.Globalization.NumberStyles.Float, inv, out scale))
            scale = 1f;
        orbit.SetView(yaw, pitch, scale);
        Debug.Log(string.Format("WMV: {0}WMV_VIEWPORT_ORBIT {1} -> yaw {2} pitch {3} distance {4:F1}", what, v, orbit.yaw,
                                orbit.pitch, orbit.distance));
    }

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
        if (s.hasRider)
        {
            HandleRiddenState(s);
            return;
        }
        if (PushIsAboutMapObject(s.fileDataID))
            return;                                     // a world model has no animation
        if (!AboutThisModel(s.fileDataID))
            return;                                     // about a different model
        if (AboutTheLoad(s.fileDataID))
        {
            // For the model being loaded. It cannot be applied yet -- the animator does not exist
            // -- and it must NOT be applied to the model still on screen. Keep the latest, with
            // when it arrived, and BuildIfReady starts the new animator from it.
            loadState = s;
            loadStateAt = s.receivedSeconds;    // when it arrived, on the reader thread -- not when it was dequeued
            haveLoadState = true;
            return;
        }
        anim.ApplyAnimationState(currentSlot, s);
    }

    /// <summary>
    /// A ridden mount's playback (protocol 5): the mount's state in the top level, the character's nested beside it, both
    /// sampled by the host in one call. They are routed as selections with a role are (WmvSlotAnimation.RouteRiddenState)
    /// and applied in this one pass, each to its own animator (WmvSlotAnimation.ApplyRidden). The rider's half of a state
    /// about the character being loaded waits for its animator, as any load's state does. The top level is kept too, as
    /// the newest word on the host's mount, for the frame a mount of its file goes on (StartMountClock).
    /// </summary>
    void HandleRiddenState(WmvIpcClient.AnimationState s)
    {
        WmvSlotAnimation.Route mountRoute, riderRoute;
        WmvSlotAnimation.RouteRiddenState(s.load, s.fileDataID, Holding(), out mountRoute, out riderRoute);
        if (riderRoute != WmvSlotAnimation.Route.Rider && riderRoute != WmvSlotAnimation.Route.RiderOfLoad)
            return;                                     // about a character that is not here
        lastMountState = s;
        haveLastMountState = true;
        if (riderRoute == WmvSlotAnimation.Route.RiderOfLoad)
        {
            loadState = s.RiderState();
            loadStateAt = s.receivedSeconds;
            haveLoadState = true;
            return;
        }
        WmvModelSlot mount = mountRoute == WmvSlotAnimation.Route.Mount ? mounted.Mount : null;
        WmvM2Animator mountAnimator = mount != null && mount.Runtime != null ? mount.Runtime.Animator : null;
        WmvM2Animator riderAnimator = currentSlot.Runtime != null ? currentSlot.Runtime.Animator : null;
        bool mountWas = mountAnimator != null && mountAnimator.IsPlaying, riderWas = riderAnimator != null && riderAnimator.IsPlaying;
        anim.ApplyRidden(mount, currentSlot, s);
        // Said only when either model starts or stops, so a paused mount -- which stops the character too -- shows.
        if ((mountAnimator != null && mountAnimator.IsPlaying != mountWas) || (riderAnimator != null && riderAnimator.IsPlaying != riderWas))
            Debug.Log(string.Format("WMV: anim: ridden state: the mount {0} (sequence {1}), the character {2} (sequence {3})",
                                    mountAnimator == null ? "not on screen" : mountAnimator.IsPlaying ? "playing" : "paused",
                                    s.sequenceIndex, riderAnimator == null ? "not animated" : riderAnimator.IsPlaying ? "playing" : "paused",
                                    s.rider.sequenceIndex));
    }

    /// <summary>
    /// WMV's displayed skin changed. Fetch whatever textures that actually changes and re-bind
    /// them onto the materials already on screen -- the mesh is unaffected by which image its
    /// materials sample.
    /// </summary>
    void HandleModelSkin(WmvIpcClient.ModelTexturesResponse r)
    {
        if (PushIsAboutMapObject(r.fileDataID))
            return;                                     // a world model has no skin
        if (AboutTheLoad(r.fileDataID))
        {
            // For the model being loaded. If the load asks the host for its textures, the answer
            // carries the same geosets and particle colour; if it does not -- every texture
            // named by the file itself -- this push is the only word the app sends about the
            // display, so it is kept and adopted at build. See BuildIfReady.
            loadSkin = r;
            haveLoadSkin = true;
            if (r.hasSubmeshVisible)
                KeepLoadSubmeshVisible(r.submeshVisible, 0);
            return;
        }
        if (currentSlot.Runtime == null || currentSlot.Model == null)
            return;                                     // nothing built yet; the load will pick it up
        if (!AboutThisModel(r.fileDataID))
            return;                                     // about a different model

        // Geometry first, and whether or not the push carries textures: a variant can change which
        // submeshes are drawn as well as which texture they wear, and the two are independent. A
        // push for a model that resolves no texture at all is still the host's word on its
        // geometry (UnityIpcServer::sendModelSkin sends it for exactly that), and it used to be
        // dropped before this point.
        if (r.hasSubmeshVisible)
            ApplyHostSubmeshVisibility(r.submeshVisible, 0, "skin push");
        if (AdoptGeosets(currentSlot, r))
        {
            WmvModelBuilder.ApplyGeosets(currentSlot.Runtime, currentSlot.Geosets, s => Debug.Log("WMV: " + s));
            status.Set(string.Format("Geosets applied ({0} triangles, mesh unchanged)",
                                     currentSlot.Runtime.TriangleCount));
        }
        if (!r.ok || r.textures.Length == 0)
            return;

        if (AdoptParticleColor(currentSlot, r))
            ApplyParticleColor(currentSlot);

        var wanted = new Dictionary<int, int>();         // slot -> FileDataID
        foreach (var t in r.textures)
        {
            if (t.fileDataID <= 0) continue;
            foreach (int slot in SlotsForTexture(currentSlot.Model, t))
                wanted[slot] = PinTexture(slot, t.fileDataID);
        }

        var fetch = new List<KeyValuePair<int, int>>();
        foreach (var kv in wanted)
        {
            int have;
            if (currentSlot.TextureIds.TryGetValue(kv.Key, out have) && have == kv.Value &&
                currentSlot.Textures.ContainsKey(kv.Key))
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
            currentSlot.TextureIds[kv.Key] = kv.Value;
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
                currentSlot.Textures[slot] = BlpDecoder.Decode(r.data);
                skinJob.Applied++;
                Debug.Log("WMV: skin texture slot " + slot + ": " + currentSlot.Textures[slot].Width + "x" +
                          currentSlot.Textures[slot].Height + " " + currentSlot.Textures[slot].Encoding);
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
            WmvModelBuilder.RebindTextures(currentSlot.Runtime, currentSlot.Textures, currentSlot.Name,
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
    /// The user switched a geoset in WMV: the host's whole per-submesh state for the model it is
    /// displaying. Applied to the model on screen at once through the triangle arrays it already
    /// holds -- no reload, no rebuild, no camera, animation, material or emitter change -- and
    /// answered, so the host's checkboxes show what is actually drawn.
    /// </summary>
    void HandleModelGeosets(WmvIpcClient.GeosetVisibility g)
    {
        if (g.fileDataID <= 0)
        {
            ipc.ReportGeosetsApplied(g.fileDataID, g.revision, "rejected", "no fileDataID", null, 0, 0.0);
            return;
        }
        if (PushIsAboutMapObject(g.fileDataID))
        {
            ipc.ReportGeosetsApplied(g.fileDataID, g.revision, "rejected", "a world model has no geosets", null, 0, 0.0);
            return;
        }
        if (AboutTheLoad(g.fileDataID))
        {
            // Still loading: the build takes it, and acknowledges it then.
            KeepLoadSubmeshVisible(g.visible, g.revision);
            ipc.ReportGeosetsApplied(g.fileDataID, g.revision, "pending", "the model is still loading", null, 0, 0.0);
            return;
        }
        if (currentSlot.Runtime == null || currentSlot.Model == null)
        {
            ipc.ReportGeosetsApplied(g.fileDataID, g.revision, "rejected", "no model is built", null, 0, 0.0);
            return;
        }
        if (g.fileDataID != currentSlot.FileDataID)
        {
            ipc.ReportGeosetsApplied(g.fileDataID, g.revision, "rejected",
                                     "the viewport shows fileDataID " + currentSlot.FileDataID, null, 0, 0.0);
            return;
        }
        if (g.visible == null || g.visible.Length != g.submeshCount)
        {
            ipc.ReportGeosetsApplied(g.fileDataID, g.revision, "rejected", "malformed submesh list", null, 0, 0.0);
            return;
        }
        ApplyHostSubmeshVisibility(g.visible, g.revision, "geoset switch");
    }

    /// <summary>Apply the host's per-submesh state to the model on screen and report the outcome.</summary>
    void ApplyHostSubmeshVisibility(bool[] visible, int revision, string what)
    {
        WmvRuntimeModel onScreen = currentSlot.Runtime;
        double animMs = onScreen.Animator != null ? onScreen.Animator.TimeMs : 0.0;
        int drawn = WmvModelBuilder.ApplySubmeshVisibility(onScreen, visible, s => Debug.Log("WMV: " + s));
        if (drawn < 0)
        {
            ipc.ReportGeosetsApplied(currentSlot.FileDataID, revision, "rejected",
                                     string.Format("the host listed {0} submeshes, the skin has {1}",
                                                   visible != null ? visible.Length : 0, onScreen.SkinSubmeshCount),
                                     WmvModelBuilder.EffectiveSubmeshVisibility(onScreen),
                                     WmvModelBuilder.DrawnTriangleCount(onScreen), animMs);
            return;
        }
        Debug.Log(string.Format("WMV: {0} applied (revision {1}) at animation time {2:F0} ms, playing={3}",
                                what, revision, animMs, onScreen.Animator != null && onScreen.Animator.IsPlaying));
        status.Set(string.Format("Submesh visibility applied ({0} triangles, mesh unchanged)", drawn));
        ipc.ReportGeosetsApplied(currentSlot.FileDataID, revision, "applied", "",
                                 WmvModelBuilder.EffectiveSubmeshVisibility(onScreen), drawn, animMs);
    }

    /// <summary>
    /// Hold the host's per-submesh state for the model being loaded. Latest wins: every message
    /// that carries it carries the host's WHOLE state at the time, so a later one already includes
    /// an earlier switch. The newest switch's revision is kept for the acknowledgement at build, even
    /// when a skin push or the texture reply (revision 0) brought the newer copy.
    /// </summary>
    void KeepLoadSubmeshVisible(bool[] visible, int revision)
    {
        if (visible == null)
            return;
        loadSubmeshVisible = visible;
        haveLoadSubmeshVisible = true;
        if (revision > loadGeosetRevision)
            loadGeosetRevision = revision;
    }

    /// <summary>
    /// Take the geoset set out of a host message into a slot, if it reported one. Returns true when
    /// the set actually CHANGED, so the caller only touches the mesh when there is something to do.
    /// A message with hasGeosets false is silence, not an empty answer: the host had no creature
    /// selection to report, and whatever is on screen stays.
    /// </summary>
    bool AdoptGeosets(WmvModelSlot slot, WmvIpcClient.ModelTexturesResponse r)
    {
        if (!r.hasGeosets)
            return false;

        var next = new HashSet<int>();
        foreach (int g in r.geosets)
            next.Add(g);

        if (slot.Geosets != null && slot.Geosets.Count == next.Count)
        {
            bool same = true;
            foreach (int g in next)
                if (!slot.Geosets.Contains(g)) { same = false; break; }
            if (same)
                return false;
        }

        slot.Geosets = next;
        return true;
    }

    /// <summary>
    /// Take the ParticleColor override out of a host message into a slot.
    ///
    /// The host sends nine bytes -- start, middle and end as RGB 0..255 -- and an emitter's
    /// ParticleColorIndex of 11, 12 or 13 selects one of the three as ITS ramp. So each of the
    /// three slots gets the whole three-stop ramp rooted at that stop, which is the shape
    /// WmvEmitterRuntime.SetParticleColorOverride expects and what the legacy's
    /// particleColorReplacements holds (particle.h:105, three sets of three).
    ///
    /// An absent field means "no override", which is different from black: it must leave the
    /// emitters on their authored colours rather than tint them to nothing.
    /// </summary>
    bool AdoptParticleColor(WmvModelSlot slot, WmvIpcClient.ModelTexturesResponse r)
    {
        Color[][] next = null;
        if (r.particleColor != null && r.particleColor.Length >= 9)
        {
            var stops = new Color[3];
            for (int i = 0; i < 3; i++)
                stops[i] = new Color(r.particleColor[i * 3] / 255f,
                                     r.particleColor[i * 3 + 1] / 255f,
                                     r.particleColor[i * 3 + 2] / 255f, 1f);
            // Index 11 -> start, 12 -> middle, 13 -> end. Each is the ramp an emitter with that
            // index draws, so each is the same three stops rotated to begin at its own.
            next = new Color[3][];
            next[0] = new[] { stops[0], stops[1], stops[2] };
            next[1] = new[] { stops[1], stops[2], stops[0] };
            next[2] = new[] { stops[2], stops[0], stops[1] };
        }

        bool changed = (next == null) != (slot.ParticleColor == null);
        if (!changed && next != null)
            for (int i = 0; i < 3 && !changed; i++)
                for (int j = 0; j < 3 && !changed; j++)
                    if (next[i][j].r != slot.ParticleColor[i][j].r ||
                        next[i][j].g != slot.ParticleColor[i][j].g ||
                        next[i][j].b != slot.ParticleColor[i][j].b)
                        changed = true;
        slot.ParticleColor = next;
        if (changed && r.particleColorId > 0)
            Debug.Log("WMV: ParticleColor " + r.particleColorId + " applies to this model's emitters");
        return changed;
    }

    /// <summary>Push a slot's current override onto that slot's model.</summary>
    void ApplyParticleColor(WmvModelSlot slot)
    {
        if (slot.Runtime != null && slot.Runtime.Emitters != null)
            slot.Runtime.Emitters.SetParticleColorOverride(slot.ParticleColor);
    }

    void Fail(string reason)
    {
        status.Set("FAILED: " + reason);
        Debug.LogError("WMV: " + reason);
        // The host takes a character it hears could not be built back to its own canvas.
        if (job != null && job.Character)
        {
            if (job.Mount != null) job.Mount.Dispose();
            if (job.Dresser != null) job.Dresser.Dispose();
            if (job.Staged != null) job.Staged.Dispose();
            job.Mount = null;
            job.Dresser = null;
            job.Staged = null;
            ipc.ReportCharacterSceneApplied(job.FileDataID, job.Load, job.Scene != null ? job.Scene.revision : 0, "rejected",
                                            "load failed: " + reason, 0, 0, null, job.Clock.ElapsedMilliseconds,
                                            WmvIpcClient.MountKeyOf(job.Scene), "none", "");
        }
        if (haveLoadSubmeshVisible && loadGeosetRevision > 0 && job != null)
            ipc.ReportGeosetsApplied(job.FileDataID, loadGeosetRevision, "rejected", "the load failed: " + reason,
                                     null, 0, 0.0);
        haveLoadSubmeshVisible = false;
        loadSubmeshVisible = null;
        loadGeosetRevision = 0;
        job = null;
    }

    void OnDestroy()
    {
        if (job != null && job.Mount != null) job.Mount.Dispose();
        if (job != null && job.Dresser != null) job.Dresser.Dispose();
        if (job != null && job.Staged != null) job.Staged.Dispose();
        if (mounted != null) mounted.Dispose();         // the character comes off before the mount goes
        if (dresser != null) dresser.Dispose();
        if (currentSlot.Runtime != null) currentSlot.Runtime.Dispose();
        if (wmoJob != null) wmoJob.Dead = true;
        DisposeMapObject();
    }

    // ---------------------------------------------------------------- -wmvAllocCheck

    /// <summary>
    /// A window of frames over which managed allocation and gen-0 collections are counted with
    /// the animators running. Started 30 frames after the build so the load's own garbage is not
    /// charged to the frame loop (ten frames in); reported once, with the material animator's
    /// counters beside it. WmvMain has no other per-frame work of its own -- the IPC client and the animators run
    /// their own -- so this Update exists for the probe alone and does nothing without it. A character riding a
    /// mount is measured with the mount's animator and emitters running too, from ten frames after the mount under
    /// it changed (FrameRiddenScene), and not while a mount is still being prepared for it.
    /// </summary>
    class AllocProbe
    {
        public int StartFrame;
        public int Frames;
        public long HeapAtStart;
        public int Gen0AtStart;
        public int TogglesAtStart;
        public WmvRuntimeModel Mount;          // the mount on screen when the window started, or null
        public int MountTogglesAtStart;
        public bool Started, Done;
    }
    AllocProbe allocProbe;
    // Short, because the headless harness keeps the player up for only a second or two after
    // the load: sixty frames is what fits, and a per-frame figure needs no more.
    const int AllocProbeFrames = 60;

    void Update()
    {
        PumpMapObjectWork();
        if (dresser != null)
            dresser.Tick();
        // A world model on screen is measured too (AdoptMapObject starts the probe): it has no animator,
        // material animator or emitters, so its window shows what the frame loop itself allocates
        // while a static WMO is displayed.
        WmvRuntimeModel onScreen = currentSlot.Runtime;
        if (allocProbe == null || allocProbe.Done || (onScreen == null && currentMapObject == null))
            return;
        if (!allocProbe.Started)
        {
            // The window measures the FRAME LOOP. The load's own traffic -- the .anim files it
            // prefetches, a character's parts still arriving, a mount being fetched and built for it --
            // is decoding on this thread for a while after the build and would be charged to the frames otherwise.
            if (Time.frameCount < allocProbe.StartFrame || currentSlot.PendingAnimFetch.Count > 0 ||
                (dresser != null && dresser.Busy) ||
                (mounted != null && (mounted.PreparingFileDataID != 0 || mounted.Mount.PendingAnimFetch.Count > 0)))
                return;
            allocProbe.Started = true;
            allocProbe.HeapAtStart = System.GC.GetTotalMemory(false);
            allocProbe.Gen0AtStart = System.GC.CollectionCount(0);
            allocProbe.TogglesAtStart = onScreen != null && onScreen.MaterialAnimator != null
                ? onScreen.MaterialAnimator.GateToggles : 0;
            allocProbe.Mount = onScreen != null && mounted != null && mounted.RiddenFileDataID != 0 ? mounted.Mount.Runtime : null;
            allocProbe.MountTogglesAtStart = allocProbe.Mount != null && allocProbe.Mount.MaterialAnimator != null
                ? allocProbe.Mount.MaterialAnimator.GateToggles : 0;
            allocProbe.Frames = 0;
            return;
        }
        allocProbe.Frames++;
        if (allocProbe.Frames < AllocProbeFrames)
            return;
        allocProbe.Done = true;
        long heap = System.GC.GetTotalMemory(false) - allocProbe.HeapAtStart;
        int gen0 = System.GC.CollectionCount(0) - allocProbe.Gen0AtStart;
        WmvMaterialAnimator ma = onScreen != null ? onScreen.MaterialAnimator : null;
        WmvEmitterRuntime em = onScreen != null ? onScreen.Emitters : null;
        Debug.Log(string.Format(
            "WMV: alloccheck: {0} frames with {1}; managed heap delta {2} bytes ({3:F1} bytes/frame), "
            + "gen-0 collections {4}; material bindings evaluated per frame {5} ({6} colour, {7} colour-alpha, "
            + "{8} weight, {9} transform), gate toggles in the window {10}, bones moving {11}",
            allocProbe.Frames,
            onScreen == null ? "a static world model on screen (no animator)"
                : onScreen.Animator != null ? (allocProbe.Mount != null ? "the character's animator running, on its mount" : "the animator running")
                : "no animator",
            heap, heap / (double)allocProbe.Frames, gen0,
            ma != null ? ma.AnimatedCount : 0, ma != null ? ma.ColorCount : 0,
            ma != null ? ma.OpacityCount : 0, ma != null ? ma.WeightCount : 0,
            ma != null ? ma.TransformCount : 0,
            ma != null ? ma.GateToggles - allocProbe.TogglesAtStart : 0,
            onScreen != null && onScreen.Animator != null ? onScreen.Animator.AnimatedBoneCount : 0));

        // The emitters, in the same window and on the same clock. Reported separately because
        // "no allocation per frame" is a claim about THEM more than about anything else here:
        // they are the only per-frame work that grows and shrinks with what is on screen.
        Debug.Log(em == null
            ? "WMV: alloccheck: emitters none -- no component, no GameObject, no per-frame call"
            : string.Format("WMV: alloccheck: emitters {0} particle + {1} ribbon = {2} draw call(s); "
                            + "{3} live particle(s) of {4} slot(s) reserved; {5} ribbon segment(s)",
                            em.ParticleEmitterCount, em.RibbonEmitterCount, em.DrawCallCount,
                            em.LiveParticleCount, em.ParticleCapacity, em.RibbonSegmentCount));

        // The mount the character rode through the window: its animator and emitters ran in the same frames, so the heap
        // delta above covers them; their own counters follow.
        WmvRuntimeModel mountRt = allocProbe.Mount;
        if (mountRt == null)
            return;
        if (mounted == null || mounted.Mount.Runtime != mountRt || mountRt.Root == null)
        {
            Debug.Log("WMV: alloccheck: the mount the window began with was swapped or taken off during it -- its counters are not reported");
            return;
        }
        WmvMaterialAnimator mma = mountRt.MaterialAnimator;
        WmvEmitterRuntime mem = mountRt.Emitters;
        Debug.Log(string.Format(
            "WMV: alloccheck: the mount {0} (fileDataID {1}) in the same window: {2}; material bindings evaluated per frame {3} "
            + "({4} colour, {5} colour-alpha, {6} weight, {7} transform), gate toggles in the window {8}, bones moving {9}; "
            + "emitters {10}",
            mounted.Key, mounted.RiddenFileDataID, mountRt.Animator != null ? "its animator running" : "no animator",
            mma != null ? mma.AnimatedCount : 0, mma != null ? mma.ColorCount : 0, mma != null ? mma.OpacityCount : 0,
            mma != null ? mma.WeightCount : 0, mma != null ? mma.TransformCount : 0,
            mma != null ? mma.GateToggles - allocProbe.MountTogglesAtStart : 0,
            mountRt.Animator != null ? mountRt.Animator.AnimatedBoneCount : 0,
            mem == null ? "none"
                : string.Format("{0} particle + {1} ribbon = {2} draw call(s); {3} live particle(s) of {4} slot(s) reserved; "
                                + "{5} ribbon segment(s)", mem.ParticleEmitterCount, mem.RibbonEmitterCount, mem.DrawCallCount,
                                mem.LiveParticleCount, mem.ParticleCapacity, mem.RibbonSegmentCount)));
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
