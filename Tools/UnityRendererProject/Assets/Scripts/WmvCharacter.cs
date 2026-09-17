// WmvCharacter.cs
//
// Dresses a playable character from the host's RESOLVED state (characterScene, see WmvIpcClient and
// the host's UnityCharacterScene.h). The body is an ordinary model build -- WmvMain loads it like any
// other M2 -- and everything else a character wears is added here:
//
//   MERGED PARTS   collection armour and customization parts the host merged into the character
//                  (WoWModel::refreshMerging). Each is its own mesh, skinned to the BODY's bones
//                  through the bone table the host computed, with no animation of its own, exactly as
//                  the host draws them inside the character's passes.
//   ATTACHMENTS    item models the host attached at an attachment point (helm, shoulders, belt
//                  buckle, weapons, shields -- at the id the host resolved, sheathed or not). Each is a
//                  full model with its own skeleton, animation and emitters, parented to the body bone
//                  the host's attachment table names, at the attachment's offset.
//   THE BODY       the texture each slot binds (a file, or an image the host composited), its geoset
//                  display flags, and the closed hand while a weapon is held.
//
// Nothing here decides what a character wears or looks like. The dresser fetches what a scene names,
// builds what is new, and then applies the whole scene in one frame: no half-dressed frame, no part
// removed before its replacement is ready. A scene that also names what the character RIDES waits for
// that too (CommitGate), and the mount goes on in the same frame (OnCommitted; see WmvMountedScene.cs).

using System;
using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow;

public class WmvCharacterDresser
{
    /// <summary>Texture key a merged part's hand batches bind instead of their own slot (the host's
    /// hand-texture rule). Far outside any M2 texture table.</summary>
    const int HandTextureKey = 100000;

    const int MaxCachedFiles = 128;
    const int MaxCachedTextures = 160;

    class Part
    {
        public string Key;
        public bool Merged;
        public int FileDataID;
        public WmvRuntimeModel Runtime;
        public M2ParsedModel Model;
        public Dictionary<int, BlpImage> Textures = new Dictionary<int, BlpImage>();
        public string TextureSignature = "";
        public string PlacementSignature = "";
        public string FilledSlots = "";
    }

    /// <summary>A part being prepared for the scene in progress: its files, parsed. FileDataID is the
    /// file the work was done for: a part key is a host object's address, and the host can free one
    /// model and get a different one at the same address, so the key alone does not name the file.</summary>
    class Prep
    {
        public int FileDataID;
        public M2ParsedModel Model;
        public M2ParsedSkin Skin;
        public bool SkeletonDone;
        public string Failed;
    }

    readonly WmvIpcClient ipc;
    readonly Func<string, BlpImage> imageByHash;
    readonly Action<string> log;

    public WmvRuntimeModel Body { get; private set; }
    public M2ParsedModel BodyModel { get; private set; }
    public int BodyFileDataID { get; private set; }

    /// <summary>The host's serial of the loadWoWModel this character came from. Every answer about a
    /// scene carries it, so the host can tell an answer about an earlier load from one about the
    /// character it is showing now. It stays with the dresser when the character goes on screen.</summary>
    public int Load { get; private set; }

    readonly Dictionary<string, Part> parts = new Dictionary<string, Part>();

    // Files and decoded textures by FileDataID, bounded. A part taken off and put back on -- the usual
    // way to compare two items -- costs no fetch the second time.
    readonly Dictionary<int, byte[]> files = new Dictionary<int, byte[]>();
    readonly List<int> fileOrder = new List<int>();
    readonly Dictionary<int, BlpImage> decoded = new Dictionary<int, BlpImage>();
    readonly List<int> decodedOrder = new List<int>();
    readonly HashSet<int> failedFiles = new HashSet<int>();

    readonly Dictionary<string, int> pending = new Dictionary<string, int>();   // requestId -> fdid
    readonly HashSet<int> requested = new HashSet<int>();                       // fdids in flight
    readonly HashSet<int> wantAsTexture = new HashSet<int>();

    WmvIpcClient.CharacterScene target;
    readonly Dictionary<string, Prep> preps = new Dictionary<string, Prep>();
    readonly System.Diagnostics.Stopwatch clock = new System.Diagnostics.Stopwatch();
    string bodyTextureSignature = "";
    string fistSignature = "";
    int appliedRevision;

    /// <summary>Raised once, when the first scene has been applied to a body that is not on screen
    /// yet (a load): the caller puts the dressed character on screen, and the acknowledgement follows.</summary>
    Action onFirstApplied;
    bool staged;

    /// <summary>
    /// What the scene being prepared waits on OUTSIDE the dresser, asked on every pump: the scene is applied
    /// only once its own parts are ready AND this says so. A mounted character's scene waits here for its mount
    /// and the riding sequence, so the character, the mount and the pose go on screen in one frame
    /// (WmvMountedScene). Null, or true for a scene with nothing outside to wait on: applied as soon as its
    /// parts are ready, as always. When what it waits on arrives, the owner calls Repump.
    /// </summary>
    public Func<WmvIpcClient.CharacterScene, bool> CommitGate;

    /// <summary>
    /// Raised in the frame a scene is applied, after its parts (and, for a staged body, after onFirstApplied has
    /// put the character on screen), before the answer goes out: the owner applies what else the scene describes
    /// -- its mount -- and says what became of it, which the answer carries. Null: the answer says "none".
    /// </summary>
    public Func<WmvIpcClient.CharacterScene, WmvIpcClient.MountAnswer> OnCommitted;

    public WmvCharacterDresser(WmvIpcClient ipc, int load, Func<string, BlpImage> imageByHash, Action<string> log)
    {
        this.ipc = ipc;
        Load = load;
        this.imageByHash = imageByHash;
        this.log = log;
    }

    public int PartCount { get { return parts.Count; } }
    /// <summary>A scene is being prepared or files are still on their way.</summary>
    public bool Busy { get { return target != null || pending.Count > 0; } }
    public int AppliedRevision { get { return appliedRevision; } }

    /// <summary>How often an applied scene bound the body's textures again (a changed composited image or texture slot).
    /// runtimeState reports it (bodyRebinds): mounting, dismounting or swapping a mount must not change it.</summary>
    public int BodyRebinds { get; private set; }

    /// <summary>
    /// Dress a body that has just been built and is not on screen yet. The body was built with this
    /// scene's body textures and flags; the parts, the closed hand and anything that changed since are
    /// applied by the dresser, then onApplied puts the character on screen. builtTextures is the
    /// decoded image of each body slot the build used, keyed by slot.
    /// </summary>
    public void BeginStaged(WmvRuntimeModel body, M2ParsedModel bodyModel, int fileDataID,
                            WmvIpcClient.CharacterScene scene, WmvIpcClient.CharacterScene builtWith,
                            Dictionary<int, BlpImage> builtTextures, Action onApplied)
    {
        Body = body;
        BodyModel = bodyModel;
        BodyFileDataID = fileDataID;
        staged = true;
        onFirstApplied = onApplied;
        // The textures the body was BUILT with; a newer scene that changed them is re-bound at commit.
        bodyTextureSignature = BodyTextureSignature(builtWith);
        // The body's files were fetched and decoded for that build. Without them here, the first Pump
        // asked for every one of them again and held the character off screen until the second copy was
        // decoded, only for Commit to find the signature unchanged and never use it.
        if (builtWith != null && builtWith.body != null && builtTextures != null)
        {
            foreach (var t in builtWith.body.textures)
            {
                BlpImage img;
                if (t != null && string.IsNullOrEmpty(t.image) && t.fileDataID > 0 &&
                    builtTextures.TryGetValue(t.slot, out img) && img != null)
                    Remember(decoded, decodedOrder, t.fileDataID, img, MaxCachedTextures);
            }
        }
        Retarget(scene);
    }

    /// <summary>A newer scene: plan it, replacing whatever scene was still being prepared.</summary>
    public void Retarget(WmvIpcClient.CharacterScene scene)
    {
        // Replaced, not refused: the host is waiting on the newer revision, and an answer about this one
        // must not read as a failure of the character on screen.
        if (target != null && target.revision != scene.revision)
            ipc.ReportCharacterSceneApplied(target.fileDataID, Load, target.revision, "superseded",
                                            "superseded by revision " + scene.revision, parts.Count, 0, null,
                                            clock.ElapsedMilliseconds, WmvIpcClient.MountKeyOf(target), "none", "");
        target = scene;
        clock.Reset();
        clock.Start();
        // A prepared part the new scene no longer names is dropped; one it still names keeps its work, but
        // only when the scene names it for the same file.
        var wanted = new Dictionary<string, int>();
        foreach (var m in scene.merged) if (m != null && !string.IsNullOrEmpty(m.key)) wanted[m.key] = m.fileDataID;
        foreach (var a in scene.attachments) if (a != null && !string.IsNullOrEmpty(a.key)) wanted[a.key] = a.fileDataID;
        var drop = new List<string>();
        foreach (var kv in preps)
        {
            int fdid;
            if (!wanted.TryGetValue(kv.Key, out fdid) || fdid != kv.Value.FileDataID)
                drop.Add(kv.Key);
        }
        foreach (var k in drop) preps.Remove(k);
        failedFiles.Clear();
        Pump();
    }

    /// <summary>
    /// A new load has started: the scene still being prepared belongs to a character about to be
    /// replaced, so it is dropped instead of being committed in the middle of that load. What is already
    /// applied stays on screen until the new model takes its place. Files still on their way are cached
    /// when they land, like any other.
    /// </summary>
    public void CancelTarget(string reason)
    {
        if (target == null)
            return;
        ipc.ReportCharacterSceneApplied(target.fileDataID, Load, target.revision, "superseded", reason,
                                        parts.Count, 0, null, clock.ElapsedMilliseconds,
                                        WmvIpcClient.MountKeyOf(target), "none", "");
        target = null;
        preps.Clear();
        clock.Stop();
    }

    /// <summary>Does the scene still being prepared name this composited image? Such an image has to stay
    /// available until that scene is applied or replaced, even after a newer image of its kind arrived.</summary>
    public bool WantsImage(string hash)
    {
        return target != null && SceneNamesImage(target, hash);
    }

    /// <summary>Does any texture binding in the scene -- body, merged parts (hand included), attachments --
    /// name this composited image?</summary>
    public static bool SceneNamesImage(WmvIpcClient.CharacterScene scene, string hash)
    {
        if (scene == null || string.IsNullOrEmpty(hash))
            return false;
        if (scene.body != null && NamesImage(scene.body.textures, hash))
            return true;
        if (scene.merged != null)
            foreach (var m in scene.merged)
                if (m != null && (NamesImage(m.textures, hash) || (m.handTexture != null && m.handTexture.image == hash)))
                    return true;
        if (scene.attachments != null)
            foreach (var a in scene.attachments)
                if (a != null && NamesImage(a.textures, hash))
                    return true;
        return false;
    }

    static bool NamesImage(WmvIpcClient.SceneTexture[] list, string hash)
    {
        if (list == null)
            return false;
        foreach (var t in list)
            if (t != null && t.image == hash)
                return true;
        return false;
    }

    public bool Owns(string requestId) { return requestId != null && pending.ContainsKey(requestId); }

    public void OnAsset(WmvIpcClient.AssetResponse r)
    {
        int fdid;
        if (!pending.TryGetValue(r.requestId, out fdid))
            return;
        pending.Remove(r.requestId);
        requested.Remove(fdid);
        if (!r.ok || r.data == null || r.data.Length == 0)
        {
            failedFiles.Add(fdid);
            Log("file " + fdid + " could not be read: " + (r.error ?? "empty"));
        }
        else if (wantAsTexture.Contains(fdid))
        {
            try
            {
                Remember(decoded, decodedOrder, fdid, BlpDecoder.Decode(r.data), MaxCachedTextures);
            }
            catch (WowParseException e)
            {
                failedFiles.Add(fdid);
                Log("texture " + fdid + " could not be decoded: " + e.Message);
            }
        }
        else
        {
            Remember(files, fileOrder, fdid, r.data, MaxCachedFiles);
        }
        Pump();
    }

    /// <summary>A composited image arrived: a scene may be waiting on it.</summary>
    public void OnImage() { if (target != null) Pump(); }

    /// <summary>Something the scene waits on outside the dresser (CommitGate) arrived: look again.</summary>
    public void Repump() { if (target != null) Pump(); }

    /// <summary>Per frame: attached items follow the body's play/pause, as the host ticks them with the
    /// character's frame delta (zero while paused) at their own speed. Merged parts follow the body's
    /// culling.</summary>
    public void Tick()
    {
        if (Body == null || Body.Root == null)
            return;
        bool playing = Body.Animator == null || Body.Animator.IsPlaying;
        bool bodyBoundsFollowPose = BodyBoundsFollowPose();
        foreach (var p in parts.Values)
        {
            if (p.Runtime == null)
                continue;
            if (p.Merged)
            {
                // A merged part is culled against a box fixed at its build, taken from the idle. The body's
                // renderer measures its real bounds every frame whenever its sequence moves bones, and an
                // animation switch can turn that on or off (WmvModelBuilder.ApplySequence), so the part
                // follows it: a pose that carries the body out of the idle's box -- a death, a swim -- must
                // not cull the hair, face and armour on it while the body stays drawn.
                if (p.Runtime.Skin != null && p.Runtime.Skin.updateWhenOffscreen != bodyBoundsFollowPose)
                    p.Runtime.Skin.updateWhenOffscreen = bodyBoundsFollowPose;
                continue;
            }
            if (p.Runtime.Animator == null)
                continue;
            if (p.Runtime.Animator.IsPlaying != playing)
                p.Runtime.Animator.SetTransportOnly(playing, 1f);
        }
    }

    bool BodyBoundsFollowPose()
    {
        return Body != null && Body.Skin != null && Body.Skin.updateWhenOffscreen;
    }

    public void Dispose()
    {
        foreach (var p in parts.Values)
            if (p.Runtime != null)
                p.Runtime.Dispose();
        parts.Clear();
        preps.Clear();
        pending.Clear();
        requested.Clear();
        target = null;
        onFirstApplied = null;
        CommitGate = null;
        OnCommitted = null;
    }

    /// <summary>Counts for the performance log: renderers, materials and textures of every part.</summary>
    public void Measure(out int renderers, out int materials, out int textures)
    {
        renderers = 0; materials = 0; textures = 0;
        foreach (var p in parts.Values)
        {
            if (p.Runtime == null) continue;
            renderers++;
            materials += p.Runtime.Materials.Length;
            textures += p.Runtime.Textures.Length;
            if (p.Runtime.Emitters != null)
                renderers += p.Runtime.Emitters.DrawCallCount;
        }
    }

    // ---------------------------------------------------------------- planning

    /// <summary>Fetch what the target scene still needs; apply it once nothing is missing.</summary>
    void Pump()
    {
        if (target == null || Body == null)
            return;
        bool ready = true;

        // Body textures.
        foreach (var t in target.body.textures)
            ready &= TextureReady(t);

        // The closed hand's pose may live in a .anim file.
        int fistAnim = FistAnimFile();
        if (fistAnim > 0)
        {
            byte[] ignored;
            ready &= FileReady(fistAnim, out ignored) || failedFiles.Contains(fistAnim);
        }

        foreach (var m in target.merged)
        {
            if (m == null) continue;
            foreach (var t in m.textures) ready &= TextureReady(t);
            if (m.handTexture != null) ready &= TextureReady(m.handTexture);
            Part have;
            if (!(parts.TryGetValue(m.key, out have) && have.FileDataID == m.fileDataID &&
                  have.FilledSlots == FilledSlotsOf(m.textures)))
                ready &= Prepare(m.key, m.fileDataID, -1);
        }
        foreach (var a in target.attachments)
        {
            if (a == null) continue;
            foreach (var t in a.textures) ready &= TextureReady(t);
            Part have;
            if (!(parts.TryGetValue(a.key, out have) && have.FileDataID == a.fileDataID))
                // The host starts an attached item's own animation manager on sequence index 0.
                ready &= Prepare(a.key, a.fileDataID, 0);
        }

        // Asked whether or not the parts are ready, so what the gate has to fetch starts now, beside them.
        bool outsideReady = CommitGate == null || CommitGate(target);
        if (ready && outsideReady)
            Commit();
    }

    bool TextureReady(WmvIpcClient.SceneTexture t)
    {
        if (t == null) return true;
        if (!string.IsNullOrEmpty(t.image))
            return true;                          // images arrive before the scene that names them
        if (t.fileDataID <= 0 || failedFiles.Contains(t.fileDataID))
            return true;
        if (decoded.ContainsKey(t.fileDataID))
            return true;
        wantAsTexture.Add(t.fileDataID);
        Request(t.fileDataID);
        return false;
    }

    bool FileReady(int fdid, out byte[] bytes)
    {
        if (files.TryGetValue(fdid, out bytes))
            return true;
        if (!failedFiles.Contains(fdid))
            Request(fdid);
        return false;
    }

    void Request(int fdid)
    {
        if (requested.Contains(fdid))
            return;
        requested.Add(fdid);
        pending[ipc.RequestAssetByFileDataID(fdid)] = fdid;
    }

    /// <summary>
    /// Bring a new part's files in: the .m2, its skeleton (and the skeleton's parent) when it has one,
    /// and its first skin profile. True when the part is ready to build or has failed for good.
    /// </summary>
    bool Prepare(string key, int fdid, int wantedSequence)
    {
        Prep p;
        // Work prepared under this key for a different file is not this part's (see Prep).
        if (!preps.TryGetValue(key, out p) || p.FileDataID != fdid)
        {
            p = new Prep { FileDataID = fdid };
            preps[key] = p;
        }
        if (p.Failed != null)
            return true;

        byte[] m2;
        if (p.Model == null)
        {
            if (!FileReady(fdid, out m2))
            {
                if (failedFiles.Contains(fdid)) { p.Failed = "its .m2 could not be read"; return true; }
                return false;
            }
            try { p.Model = M2Parser.Parse(m2, wantedSequence); }
            catch (WowParseException e) { p.Failed = "parse failed: " + e.Message; return true; }
            p.SkeletonDone = p.Model.SkeletonFileDataID == 0;
        }

        if (!p.SkeletonDone)
        {
            int skid = p.Model.SkeletonFileDataID;
            byte[] skel;
            if (!FileReady(skid, out skel))
            {
                if (failedFiles.Contains(skid)) { p.SkeletonDone = true; return Prepare(key, fdid, wantedSequence); }
                return false;
            }
            int parentId = M2Parser.ReadSkeletonParentId(skel);
            byte[] parent = null;
            if (parentId > 0 && !FileReady(parentId, out parent))
            {
                if (!failedFiles.Contains(parentId))
                    return false;
                parent = null;
            }
            try { M2Parser.ApplySkeleton(p.Model, skel, parent, wantedSequence); }
            catch (WowParseException e) { Log("part " + fdid + ": skeleton not applied (" + e.Message + ") -- drawn static"); }
            p.SkeletonDone = true;
        }

        if (p.Skin == null)
        {
            if (p.Model.SkinFileDataIDs.Length == 0) { p.Failed = "no skin profile (SFID)"; return true; }
            int sfid = p.Model.SkinFileDataIDs[0];
            byte[] skinBytes;
            if (!FileReady(sfid, out skinBytes))
            {
                if (failedFiles.Contains(sfid)) { p.Failed = "its skin could not be read"; return true; }
                return false;
            }
            try { p.Skin = M2SkinParser.Parse(skinBytes); }
            catch (WowParseException e) { p.Failed = "skin parse failed: " + e.Message; return true; }
        }
        return true;
    }

    int FistAnimFile()
    {
        if (BodyModel == null || target == null)
            return 0;
        if (!target.body.closeRightHand && !target.body.closeLeftHand)
            return 0;
        return M2Parser.ExternalAnimFileId(BodyModel, target.body.fistSequence);
    }

    // ---------------------------------------------------------------- applying

    void Commit()
    {
        WmvIpcClient.CharacterScene scene = target;
        target = null;
        var missing = new List<string>();
        var sw = System.Diagnostics.Stopwatch.StartNew();

        // ---- the body -----------------------------------------------------------------------
        string bodySig = BodyTextureSignature(scene);
        if (bodySig != bodyTextureSignature)
        {
            var dict = TexturesFor(scene.body.textures, missing, "body");
            WmvModelBuilder.RebindTextures(Body, dict, "body", null);
            bodyTextureSignature = bodySig;
            BodyRebinds++;
            Log("body textures re-bound (" + dict.Count + " slot(s))");
        }
        bool[] bodyFlags = WmvIpcClient.Flags(scene.body.submeshVisible);
        if (bodyFlags.Length > 0 && WmvModelBuilder.ApplySubmeshVisibility(Body, bodyFlags, null) < 0)
            missing.Add("body geosets (" + bodyFlags.Length + " listed, skin has " + Body.SkinSubmeshCount + ")");
        ApplyFist(scene, missing);

        // ---- parts: remove what the scene no longer names -------------------------------------
        var named = new HashSet<string>();
        foreach (var m in scene.merged) if (m != null) named.Add(m.key);
        foreach (var a in scene.attachments) if (a != null) named.Add(a.key);
        var remove = new List<string>();
        foreach (var kv in parts)
            if (!named.Contains(kv.Key)) remove.Add(kv.Key);
        foreach (var k in remove)
        {
            if (parts[k].Runtime != null) parts[k].Runtime.Dispose();
            parts.Remove(k);
        }

        // ---- merged parts ----------------------------------------------------------------------
        int merged = 0, attached = 0;
        foreach (var m in scene.merged)
        {
            if (m == null) continue;
            if (ApplyMerged(m, missing)) merged++;
        }
        foreach (var a in scene.attachments)
        {
            if (a == null) continue;
            if (ApplyAttachment(a, missing)) attached++;
        }
        preps.Clear();
        appliedRevision = scene.revision;

        int renderers, materials, textures;
        Measure(out renderers, out materials, out textures);
        Log(string.Format("scene revision {0} applied in {1} ms (prepared over {2} ms): {3} merged, {4} attached; " +
                          "parts {5} renderer(s) incl. emitters, {6} material(s), {7} texture(s); body {8} material(s){9}",
                          scene.revision, sw.ElapsedMilliseconds, clock.ElapsedMilliseconds, merged, attached,
                          renderers, materials, textures, Body.Materials.Length,
                          missing.Count > 0 ? "; MISSING " + string.Join(", ", missing.ToArray()) : ""));

        if (staged)
        {
            staged = false;
            Action done = onFirstApplied;
            onFirstApplied = null;
            if (done != null) done();
        }
        // Still this frame: what the scene describes beyond the character (its mount) goes on with it.
        Func<WmvIpcClient.CharacterScene, WmvIpcClient.MountAnswer> committed = OnCommitted;
        WmvIpcClient.MountAnswer mount = committed != null ? committed(scene) : WmvIpcClient.MountAnswer.None(scene);
        ipc.ReportCharacterSceneApplied(BodyFileDataID, Load, scene.revision, "applied", "", merged, attached, missing,
                                        clock.ElapsedMilliseconds, mount.Key, mount.Status, mount.Reason);
    }

    void ApplyFist(WmvIpcClient.CharacterScene scene, List<string> missing)
    {
        WmvIpcClient.SceneBody b = scene.body;
        var bones = new List<int>();
        if (b.closeRightHand) bones.AddRange(b.rightFingerBones);
        if (b.closeLeftHand) bones.AddRange(b.leftFingerBones);
        string sig = b.fistSequence + ":" + b.fistTimeMs + ":" + string.Join(",", bones.ConvertAll(x => x.ToString()).ToArray());
        if (sig == fistSignature)
            return;
        if (Body.Animator == null || Body.Bones.Length == 0)
        {
            if (bones.Count > 0)
                missing.Add("closed hand (the body has no animator)");
            fistSignature = sig;
            return;
        }
        M2BoneDef[] tracks = null;
        if (bones.Count > 0)
        {
            byte[] external = null;
            int animFile = M2Parser.ExternalAnimFileId(BodyModel, b.fistSequence);
            if (animFile > 0 && !files.TryGetValue(animFile, out external))
                missing.Add("closed hand (.anim " + animFile + ")");
            tracks = M2Parser.ReadBoneTracksForSequence(BodyModel, b.fistSequence, external);
        }
        Body.Animator.SetPoseOverride(Body.Bones, Body.BoneRestPositions, bones.ToArray(), tracks, b.fistTimeMs);
        fistSignature = sig;
        Log(bones.Count > 0
            ? string.Format("closed hand: {0} finger bone(s) posed from sequence {1} at {2} ms", bones.Count, b.fistSequence, b.fistTimeMs)
            : "closed hand: released");
    }

    bool ApplyMerged(WmvIpcClient.SceneMerged m, List<string> missing)
    {
        Part part;
        // Built again, not rebound, when the slots the scene fills change: see FilledSlotsOf.
        string filledSlots = FilledSlotsOf(m.textures);
        bool exists = parts.TryGetValue(m.key, out part) && part.FileDataID == m.fileDataID && part.Runtime != null &&
                      part.FilledSlots == filledSlots;
        string texSig = TextureSignature(m.textures) + "|hand:" + (m.handTexture != null ? TextureSignature(new[] { m.handTexture }) : "") +
                        "|" + string.Join(",", Array.ConvertAll(m.handSubmeshes ?? new int[0], x => x.ToString()));
        bool[] flags = WmvIpcClient.Flags(m.submeshVisible);

        if (!exists)
        {
            if (part != null && part.Runtime != null) part.Runtime.Dispose();
            Prep prep;
            if (!preps.TryGetValue(m.key, out prep) || prep.Failed != null || prep.Model == null || prep.Skin == null)
            {
                missing.Add(m.key + " (" + m.fileDataID + ": " + (prep != null && prep.Failed != null ? prep.Failed : "not prepared") + ")");
                parts.Remove(m.key);
                return false;
            }
            M2ParsedModel model = prep.Model;
            // The host draws a merged part's passes with NO colour, transparency, weight or UV animation
            // of their own -- static colour and alpha included (refreshMerging sets their channels to -1).
            model.Colors = new M2ColorDef[0];
            model.TextureWeights = new float[0];
            model.TextureWeightLookup = new ushort[0];
            model.TextureTransforms = new M2TextureTransform[0];
            model.TextureTransformLookup = new ushort[0];

            var textures = TexturesFor(m.textures, missing, m.key);
            var options = new WmvBuildOptions { NoAnimation = true };
            if (m.handSubmeshes != null && m.handSubmeshes.Length > 0 && m.handTexture != null)
            {
                var hand = TexturesFor(new[] { m.handTexture }, missing, m.key + " hand");
                BlpImage img;
                if (hand.TryGetValue(-1, out img))
                {
                    textures[HandTextureKey] = img;
                    options.BaseTextureOverride = new Dictionary<int, int>();
                    foreach (int s in m.handSubmeshes)
                        options.BaseTextureOverride[s] = HandTextureKey;
                }
            }
            if (Body.Bones.Length > 0 && Body.BindPoses.Length == Body.Bones.Length)
            {
                Bounds b = new Bounds(Body.Bounds.center, Body.Bounds.size + Vector3.one * (Body.Bounds.size.magnitude * 0.5f));
                options.Skeleton = new WmvExternalSkeleton
                {
                    Bones = Body.Bones, BindPoses = Body.BindPoses, BoneMap = m.boneMap ?? new int[0], LocalBounds = b,
                };
            }
            WmvRuntimeModel runtime;
            try
            {
                runtime = WmvModelBuilder.Build(model, prep.Skin, textures, "merged_" + m.fileDataID, null, null,
                                                flags.Length == prep.Skin.Submeshes.Length ? flags : null, options);
            }
            catch (Exception e)
            {
                missing.Add(m.key + " (" + m.fileDataID + ": build failed: " + e.Message + ")");
                parts.Remove(m.key);
                return false;
            }
            runtime.Root.transform.SetParent(Body.Root.transform, false);
            // Culled the way the body is from its first frame; Tick keeps it so (see there).
            if (runtime.Skin != null)
                runtime.Skin.updateWhenOffscreen = BodyBoundsFollowPose();
            part = new Part { Key = m.key, Merged = true, FileDataID = m.fileDataID, Runtime = runtime, Model = model,
                              Textures = textures, TextureSignature = texSig, FilledSlots = filledSlots };
            parts[m.key] = part;
            Log(string.Format("merged part {0} built: {1} submesh(es), {2} material(s), bone map {3} entr(ies){4}",
                              m.fileDataID, runtime.SubmeshCount, runtime.Materials.Length, m.boneMap != null ? m.boneMap.Length : 0,
                              options.Skeleton == null ? " -- the body has no skeleton, drawn at rest" : ""));
            return true;
        }

        if (part.TextureSignature != texSig)
        {
            var textures = TexturesFor(m.textures, missing, m.key);
            if (m.handTexture != null)
            {
                var hand = TexturesFor(new[] { m.handTexture }, missing, m.key + " hand");
                BlpImage img;
                if (hand.TryGetValue(-1, out img)) textures[HandTextureKey] = img;
            }
            WmvModelBuilder.RebindTextures(part.Runtime, textures, "merged_" + m.fileDataID, null);
            part.Textures = textures;
            part.TextureSignature = texSig;
        }
        if (flags.Length > 0 && WmvModelBuilder.ApplySubmeshVisibility(part.Runtime, flags, null) < 0)
            missing.Add(m.key + " geosets");
        return true;
    }

    bool ApplyAttachment(WmvIpcClient.SceneAttachment a, List<string> missing)
    {
        Part part;
        bool exists = parts.TryGetValue(a.key, out part) && part.FileDataID == a.fileDataID && part.Runtime != null;
        string texSig = TextureSignature(a.textures);
        bool[] flags = WmvIpcClient.Flags(a.submeshVisible);

        if (!exists)
        {
            if (part != null && part.Runtime != null) part.Runtime.Dispose();
            Prep prep;
            if (!preps.TryGetValue(a.key, out prep) || prep.Failed != null || prep.Model == null || prep.Skin == null)
            {
                missing.Add(a.key + " (" + a.fileDataID + ": " + (prep != null && prep.Failed != null ? prep.Failed : "not prepared") + ")");
                parts.Remove(a.key);
                return false;
            }
            var textures = TexturesFor(a.textures, missing, a.key);
            WmvRuntimeModel runtime;
            try
            {
                runtime = WmvModelBuilder.Build(prep.Model, prep.Skin, textures, "attached_" + a.fileDataID, null, null,
                                                flags.Length == prep.Skin.Submeshes.Length ? flags : null);
            }
            catch (Exception e)
            {
                missing.Add(a.key + " (" + a.fileDataID + ": build failed: " + e.Message + ")");
                parts.Remove(a.key);
                return false;
            }
            part = new Part { Key = a.key, Merged = false, FileDataID = a.fileDataID, Runtime = runtime, Model = prep.Model,
                              Textures = textures, TextureSignature = texSig };
            parts[a.key] = part;
            // A fresh item animation starts where the host's does -- at the start of its sequence -- and
            // holds while the character is paused.
            if (runtime.Animator != null && Body.Animator != null && !Body.Animator.IsPlaying)
                runtime.Animator.SetTransportOnly(false, 1f);
            Log(string.Format("attached part {0} at attachment {1} (bone {2}) built: {3} submesh(es), {4} material(s){5}{6}",
                              a.fileDataID, a.attachmentId, a.bone, runtime.SubmeshCount, runtime.Materials.Length,
                              runtime.Animator != null ? ", animated" : "", runtime.Emitters != null ? ", emitters" : ""));
        }
        else
        {
            if (part.TextureSignature != texSig)
            {
                var textures = TexturesFor(a.textures, missing, a.key);
                WmvModelBuilder.RebindTextures(part.Runtime, textures, "attached_" + a.fileDataID, null);
                part.Textures = textures;
                part.TextureSignature = texSig;
            }
            if (flags.Length > 0 && WmvModelBuilder.ApplySubmeshVisibility(part.Runtime, flags, null) < 0)
                missing.Add(a.key + " geosets");
        }
        Place(part, a);
        return true;
    }

    /// <summary>
    /// Hang an attached model where the host draws it: WoWModel::setupAtt multiplies the CHARACTER's bone
    /// matrix by a translation to the attachment's position (ModelAttachment::setup). A body bone's
    /// Transform sits at its pivot with the identity at rest (WmvModelBuilder.BuildSkeleton), so the same
    /// point is the attachment position less that pivot, in the bone's space, and the animated bone
    /// carries it -- rotation, translation and scale -- exactly as the host's matrix does. An id the
    /// character has no entry for gets no transform: the model's origin.
    /// </summary>
    void Place(Part part, WmvIpcClient.SceneAttachment a)
    {
        float scale = a.scale > 0f ? a.scale : 1f;
        string sig = a.bone + ":" + string.Join(",", Array.ConvertAll(a.position ?? new float[0], x => x.ToString("R"))) +
                     ":" + a.mirrored + ":" + scale + ":" + a.visible;
        if (sig == part.PlacementSignature)
            return;
        Transform t = part.Runtime.Root.transform;
        Vector3 local = Vector3.zero;
        Transform parent = Body.Root.transform;
        if (a.bone >= 0 && a.bone < Body.Bones.Length && BodyModel != null && a.bone < BodyModel.Bones.Length &&
            a.position != null && a.position.Length >= 3)
        {
            parent = Body.Bones[a.bone];
            local = AttachmentLocalPosition(new WowVec3(a.position[0], a.position[1], a.position[2]),
                                            BodyModel.Bones[a.bone].Pivot);
        }
        t.SetParent(parent, false);
        t.localPosition = local;
        t.localRotation = Quaternion.identity;
        // The host's left-hand mirror negates the model's Y axis (WoWModel::drawModel), which is Unity's X.
        t.localScale = new Vector3(a.mirrored ? -scale : scale, scale, scale);
        part.Runtime.Root.SetActive(a.visible);
        // A part placed before and moved now (sheathed or drawn, say) is said; a new part's build line says where it went.
        if (part.PlacementSignature.Length > 0)
            Log(string.Format("attached part {0} moved to attachment {1} (bone {2}){3}", a.fileDataID, a.attachmentId, a.bone,
                              a.visible ? "" : ", hidden"));
        part.PlacementSignature = sig;
    }

    /// <summary>
    /// Where an attached model sits in its body bone's space: the attachment position less the bone's
    /// pivot, both converted to Unity space. The bone's Transform stands at that pivot with the identity
    /// at rest, so a child at this local position is exactly on the attachment point, and the animated
    /// bone carries it from there. Kept apart from Place so the self-test can check the arithmetic
    /// without a host scene.
    /// </summary>
    public static Vector3 AttachmentLocalPosition(WowVec3 attachmentPosition, WowVec3 bonePivot)
    {
        float px, py, pz, bx, by, bz;
        WowCoordinateConverter.ConvertPosition(attachmentPosition, out px, out py, out pz);
        WowCoordinateConverter.ConvertPosition(bonePivot, out bx, out by, out bz);
        return new Vector3(px - bx, py - by, pz - bz);
    }

    Dictionary<int, BlpImage> TexturesFor(WmvIpcClient.SceneTexture[] list, List<string> missing, string owner)
    {
        var dict = new Dictionary<int, BlpImage>();
        if (list == null) return dict;
        foreach (var t in list)
        {
            if (t == null) continue;
            BlpImage img = null;
            if (!string.IsNullOrEmpty(t.image))
            {
                img = imageByHash(t.image);
                if (img == null) missing.Add(owner + " slot " + t.slot + " image " + t.image);
            }
            else if (t.fileDataID > 0)
            {
                if (!decoded.TryGetValue(t.fileDataID, out img))
                    missing.Add(owner + " slot " + t.slot + " texture " + t.fileDataID);
            }
            if (img != null)
                dict[t.slot] = img;
        }
        return dict;
    }

    static string TextureSignature(WmvIpcClient.SceneTexture[] list)
    {
        if (list == null) return "";
        var sb = new System.Text.StringBuilder();
        foreach (var t in list)
            if (t != null)
                sb.Append(t.slot).Append('=').Append(string.IsNullOrEmpty(t.image) ? t.fileDataID.ToString() : "i" + t.image).Append(';');
        return sb.ToString();
    }

    /// <summary>
    /// The texture slots a scene fills for a part, without the files in them. The builder arms each material's
    /// combiner and alpha from the textures present when it builds (WmvModelBuilder.CreateMaterial), and
    /// RebindTextures only re-points images. A slot that was empty at the build and filled later -- an item level
    /// whose display adds a texture type the first one did not name, on a model the host re-creates at the same
    /// address -- would otherwise keep a combiner with no second unit and an opaque alpha: an effect card drawn as a
    /// solid rectangle.
    /// </summary>
    static string FilledSlotsOf(WmvIpcClient.SceneTexture[] list)
    {
        if (list == null) return "";
        var sb = new System.Text.StringBuilder();
        foreach (var t in list)
            if (t != null)
                sb.Append(t.slot).Append(';');
        return sb.ToString();
    }

    static string BodyTextureSignature(WmvIpcClient.CharacterScene scene)
    {
        return scene != null && scene.body != null ? TextureSignature(scene.body.textures) : "";
    }

    static void Remember<T>(Dictionary<int, T> cache, List<int> order, int key, T value, int max)
    {
        if (!cache.ContainsKey(key))
        {
            order.Add(key);
            while (order.Count > max)
            {
                cache.Remove(order[0]);
                order.RemoveAt(0);
            }
        }
        cache[key] = value;
    }

    /// <summary>The body's texture bindings from a scene, for its first build.</summary>
    public static bool BodyTexturesFrom(WmvIpcClient.CharacterScene scene, Func<string, BlpImage> imageByHash,
                                        out List<KeyValuePair<int, int>> files, Dictionary<int, BlpImage> images,
                                        List<string> missing)
    {
        files = new List<KeyValuePair<int, int>>();
        if (scene == null || scene.body == null)
            return false;
        foreach (var t in scene.body.textures)
        {
            if (t == null) continue;
            if (!string.IsNullOrEmpty(t.image))
            {
                BlpImage img = imageByHash(t.image);
                if (img != null) images[t.slot] = img;
                else missing.Add("body slot " + t.slot + " image " + t.image);
            }
            else if (t.fileDataID > 0)
                files.Add(new KeyValuePair<int, int>(t.slot, t.fileDataID));
        }
        return true;
    }

    void Log(string s)
    {
        if (log != null) log("character: " + s);
    }
}
