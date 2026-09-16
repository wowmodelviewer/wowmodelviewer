// WmvMountedScene.cs
//
// What a playable character RIDES (protocol 5). The host keeps the character loaded -- the model on screen
// stays the rider, dressed by WmvCharacterDresser -- and describes the mount inside the character's scene
// (WmvIpcClient.SceneMount): the mount's file, the bone and position of the attachment the character hangs
// from as the MOUNT's own attachment table gives them, the character's scale, the mount's display state and
// the sequence each of the two models plays. This builds that mount as a second, separately animated model
// and hangs the character's body root from its bone:
//
//   mount root                      a WmvRuntimeModel of its own: mesh, bones, animator, emitters
//     `- mount bone                 the bone the host's attachment lookup resolved
//          `- character body root   the rider on screen, with everything the dresser hung on it
//
// so the Transform hierarchy carries the character with the animated bone -- the host multiplies the rider's
// matrices by that bone's matrix and a translation to the attachment's position (ModelAttachment::setup) --
// and whatever the character wears follows its body as it always does. Nothing is copied per frame.
//
// The lifecycle is the dresser's. A scene's mount is a target; its files come through the ordinary asset
// channel under request ids this owns (Owns, OnAsset); the mount is built inactive and goes on screen only
// when the character's scene is applied (WmvCharacterDresser.CommitGate, OnCommitted), in that frame: the
// character, the mount and the riding pose appear together. A description of the same mount again (same key
// and file) keeps what was fetched, parsed or built for it; a new key builds the new mount beside the old one
// and swaps them in one frame; a scene without a mount is the dismount. The character always comes off a
// mount before the mount is destroyed: destroying the mount's root with the character under it would destroy
// the character's objects while its runtime still owns their meshes, materials and textures.
//
// Once on screen the mount animates on its own clock, from its own slot: the host's pushes about it (role
// "mount", and the top level of a ridden state) reach that slot through WmvMain and WmvSlotAnimation, as the
// character's reach the character's. PreparingFileDataID and PreparingDescribedAt say which mount is on its way
// and since when, which is what those pushes are routed and started by.
//
// What the camera frames while the character rides is both models at once (UnionBounds): the character's box no
// longer sits in the world's space once its root hangs from the mount's bone, so it is carried there through the
// body root before it is joined with the mount's.

using System;
using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow;

public class WmvMountedScene
{
    /// <summary>The fetched files kept for a mount chosen again, by size: a large mount's .m2 and skeleton are tens of
    /// megabytes, so a count would not bound it. The oldest go first; the file being built always stays.</summary>
    const long MaxCachedBytes = 64L * 1024 * 1024;

    /// <summary>Placement cases (see Placement).</summary>
    public const int CaseNoAttachment = 0, CaseBone = 1, CaseNoBoneTransform = 2;

    /// <summary>A mount being prepared: its files, parsed, and the build once all of them are here. Key and
    /// FileDataID are what the work was done for: a description with the same key and file keeps it.</summary>
    class Work
    {
        public string Key;
        public int FileDataID;
        public int Sequence;              // what the host's mount plays: parsed, read and built playing it
        public byte[] M2;
        public M2ParsedModel Model;
        public bool SkeletonDone;
        public bool AnimDone;
        public int AnimFileId;            // the .anim read for Sequence, 0 for none
        public byte[] AnimBytes;
        public M2ParsedSkin Skin;
        public WmvRuntimeModel Built;     // inactive until the commit
        public string BuiltTextureSignature = "";
        public string Failed;
        public readonly System.Diagnostics.Stopwatch Clock = System.Diagnostics.Stopwatch.StartNew();
        public long ParseMs, TextureMs, BuildMs, BuiltAtMs;
        public double DescribedAt;        // when the first scene describing this key arrived (WmvIpcClient.NowSeconds)
    }

    class PendingFile
    {
        public int FileDataID;
        public bool Texture;
        public WmvModelSlot RiderSlot;     // the .anim of the character's riding sequence, for the character's slot
    }

    readonly Func<int, string> requestByFileDataID;
    readonly Action<string> log;
    readonly Action onProgress;

    /// <summary>The mount on screen -- or, for a character still being loaded, the mount that goes on screen
    /// with it: its runtime, parsed model and bytes, file, textures, sequence and display state.</summary>
    public readonly WmvModelSlot Mount = new WmvModelSlot();

    string appliedKey = "";
    string appliedTextureSignature = "";
    Transform rider;                       // the character's body root while it hangs from the mount
    string placementSignature = "";
    int seatCase = -1, seatBone = -1;      // how it hangs (Placement), while it does
    Vector3 seatLocalPosition;
    float seatScale = 1f;

    /// <summary>Every mount runtime built and not disposed yet, across the player: the mounts on screen and those built
    /// for a scene not applied yet. runtimeState reports the count (liveMounts), which is how the host's lifecycle test
    /// tells the one mount on screen from a stale or doubled one; WmvRuntimeModel.Live counts them among all models.</summary>
    static readonly HashSet<WmvRuntimeModel> liveMounts = new HashSet<WmvRuntimeModel>();

    /// <summary>Mount runtimes alive (see liveMounts).</summary>
    public static int LiveMounts { get { return liveMounts.Count; } }

    /// <summary>Mount runtimes built since the player started (runtimeState's mountsBuilt): the same mount described again
    /// is never built again.</summary>
    public static int MountsBuilt { get; private set; }

    WmvIpcClient.SceneMount target;
    Work work;

    readonly Dictionary<string, PendingFile> pending = new Dictionary<string, PendingFile>();
    readonly HashSet<int> requested = new HashSet<int>();          // files and textures in flight
    readonly HashSet<int> riderAnimsRequested = new HashSet<int>();
    readonly HashSet<int> riderAnimsFailed = new HashSet<int>();
    readonly Dictionary<int, byte[]> files = new Dictionary<int, byte[]>();
    readonly List<int> fileOrder = new List<int>();
    readonly HashSet<int> workFiles = new HashSet<int>();          // what the mount being prepared asked for
    long cachedBytes;
    readonly Dictionary<int, BlpImage> decoded = new Dictionary<int, BlpImage>();
    readonly HashSet<int> failedFiles = new HashSet<int>();
    bool disposed;

    /// <param name="requestByFileDataID">Sends an asset request and returns its id (WmvIpcClient.RequestAssetByFileDataID).</param>
    /// <param name="onProgress">Raised after an answer this owns was taken in, as the last thing OnAsset does: a
    /// character's scene may be waiting on it (WmvCharacterDresser.Repump).</param>
    public WmvMountedScene(Func<int, string> requestByFileDataID, Action<string> log, Action onProgress)
    {
        this.requestByFileDataID = requestByFileDataID;
        this.log = log;
        this.onProgress = onProgress;
    }

    /// <summary>The key of the mount on screen, "" for none.</summary>
    public string Key { get { return appliedKey; } }

    /// <summary>The FileDataID of the mount the character rides on screen, 0 when it rides none.</summary>
    public int RiddenFileDataID { get { return Mount.Runtime != null && appliedKey.Length > 0 ? Mount.FileDataID : 0; } }

    /// <summary>The character's body root, while it hangs from the mount; null otherwise.</summary>
    public Transform Rider { get { return rider; } }

    /// <summary>How the character hangs from the mount -- CaseNoAttachment, CaseBone or CaseNoBoneTransform -- or -1 while
    /// none does; the bone it hangs from (CaseBone, else -1); its local position and scale there.</summary>
    public int SeatCase { get { return rider != null ? seatCase : -1; } }
    public int SeatBone { get { return rider != null && seatCase == CaseBone ? seatBone : -1; } }
    public Vector3 SeatLocalPosition { get { return seatLocalPosition; } }
    public float SeatScale { get { return seatScale; } }

    /// <summary>A mount built for the target and not on screen yet (inactive), or null.</summary>
    public WmvRuntimeModel Staged { get { return work != null ? work.Built : null; } }

    /// <summary>The FileDataID of a mount being prepared for a scene not applied yet -- fetched, built, failed or waiting for
    /// the character's scene -- or 0 for none. The host has already replaced the mount on screen with it, so an animation
    /// push naming its file is about it.</summary>
    public int PreparingFileDataID { get { return work != null ? work.FileDataID : 0; } }

    /// <summary>When the first scene describing the mount being prepared arrived (WmvIpcClient.NowSeconds), or
    /// double.MaxValue when unknown. The host starts a mount choice's clocks before it sends that scene, so a state the
    /// character's slot kept from before it describes the clip the character played before.</summary>
    public double PreparingDescribedAt { get { return work != null ? work.DescribedAt : double.MaxValue; } }

    public bool Owns(string requestId) { return requestId != null && pending.ContainsKey(requestId); }

    // ---------------------------------------------------------------- the target

    /// <summary>
    /// The mount a newer scene describes. The same key and file as the mount on screen needs nothing built; the
    /// same key and file as the mount being prepared keeps its work (a newer revision, or the host sending its
    /// state again after waiting for an answer); anything else starts over.
    /// </summary>
    public void Retarget(WmvIpcClient.SceneMount m)
    {
        Retarget(m, double.MaxValue);
    }

    /// <param name="describedAt">When the scene carrying m arrived (WmvIpcClient.CharacterScene.receivedSeconds): kept
    /// with new work as PreparingDescribedAt; a later description of the same key keeps the first.</param>
    public void Retarget(WmvIpcClient.SceneMount m, double describedAt)
    {
        if (disposed || m == null)
            return;
        target = m;
        // A file that could not be read is asked for again for a newer scene, as the dresser does.
        failedFiles.Clear();
        riderAnimsFailed.Clear();
        if (IsApplied(m))
        {
            DropWork();
        }
        else if (work == null || work.Key != m.key || work.FileDataID != m.fileDataID || work.Failed != null)
        {
            DropWork();
            work = new Work { Key = m.key, FileDataID = m.fileDataID, Sequence = m.sequenceIndex, DescribedAt = describedAt };
            Log(string.Format("{0} ({1} {2}) requested: sequence {3}, attachment {4} on bone {5}, {6} texture(s){7}",
                              m.key, m.fileDataID, m.path, m.sequenceIndex, m.attachmentId, m.bone,
                              m.textures != null ? m.textures.Length : 0,
                              appliedKey.Length > 0 ? "; " + appliedKey + " stays on until it is built" : ""));
        }
        TexturesReady(m);
        Advance();
    }

    /// <summary>Drop the mount being prepared -- its scene will never be applied -- along with the work done for it.
    /// The mount on screen stays, and so does the account of the files still on the wire (as the dresser's does): their
    /// answers are still claimed here, so a late one is never read as an answer to whatever replaced this mount, its
    /// bytes still reach the cache, and a mount described again does not ask again for a file already coming.</summary>
    public void CancelTarget()
    {
        target = null;
        DropWork();
    }

    /// <summary>
    /// Is everything a scene's mount needs here, so the character's scene can be applied with it in one frame:
    /// the mount built (or failed for good), a newer description's textures, and -- for a mount going on now --
    /// the keys of the character's riding sequence when they live in a .anim (asked for here, into the
    /// character's slot). riderModel is the character's parsed model, riderSlot the slot that plays it.
    /// </summary>
    public bool ReadyFor(WmvIpcClient.SceneMount m, WmvModelSlot riderSlot, M2ParsedModel riderModel)
    {
        if (disposed || m == null)
            return true;
        if (IsApplied(m))
            return TexturesReady(m);
        if (work == null || work.Key != m.key || work.FileDataID != m.fileDataID)
            Retarget(m);                                    // never judged by work done for another mount
        if (work == null || (work.Built == null && work.Failed == null))
        {
            RiderSequenceReady(m.riderSequenceIndex, riderSlot, riderModel);    // fetched beside the mount
            return false;
        }
        bool ready = RiderSequenceReady(m.riderSequenceIndex, riderSlot, riderModel);
        if (work.Failed == null)
            ready &= TexturesReady(m);
        return ready;
    }

    bool IsApplied(WmvIpcClient.SceneMount m)
    {
        return m != null && appliedKey.Length > 0 && m.key == appliedKey && Mount.Runtime != null &&
               m.fileDataID == Mount.FileDataID;
    }

    bool RiderSequenceReady(int sequence, WmvModelSlot slot, M2ParsedModel model)
    {
        if (sequence < 0 || slot == null || model == null || sequence >= model.Sequences.Length ||
            WmvModelBuilder.Debug_.NoAnim)
            return true;
        if (slot.BoneTrackCache.ContainsKey(sequence))
            return true;
        int animFile = M2Parser.ExternalAnimFileId(model, sequence);
        if (animFile == 0 || slot.AnimFileCache.ContainsKey(animFile) || riderAnimsFailed.Contains(animFile))
            return true;
        if (riderAnimsRequested.Add(animFile))
        {
            pending[requestByFileDataID(animFile)] = new PendingFile { FileDataID = animFile, RiderSlot = slot };
            Log(string.Format("the character's riding sequence {0} keeps its keys in .anim {1}: fetched before the mount goes on",
                              sequence, animFile));
        }
        return false;
    }

    // ---------------------------------------------------------------- files

    public void OnAsset(WmvIpcClient.AssetResponse r)
    {
        PendingFile q;
        if (disposed || r == null || !pending.TryGetValue(r.requestId, out q))
            return;
        pending.Remove(r.requestId);
        bool ok = r.ok && r.data != null && r.data.Length > 0;
        if (q.RiderSlot != null)
        {
            riderAnimsRequested.Remove(q.FileDataID);
            if (ok)
                q.RiderSlot.AnimFileCache[q.FileDataID] = r.data;
            else
            {
                riderAnimsFailed.Add(q.FileDataID);
                Log(".anim " + q.FileDataID + " of the character's riding sequence could not be read (" +
                    (r.error ?? "empty") + ") -- the sequence switch falls back as any other would");
            }
        }
        else
        {
            requested.Remove(q.FileDataID);
            if (!ok)
            {
                failedFiles.Add(q.FileDataID);
                Log("file " + q.FileDataID + " could not be read: " + (r.error ?? "empty"));
            }
            else if (q.Texture)
            {
                var sw = System.Diagnostics.Stopwatch.StartNew();
                try { decoded[q.FileDataID] = BlpDecoder.Decode(r.data); }
                catch (WowParseException e)
                {
                    failedFiles.Add(q.FileDataID);
                    Log("texture " + q.FileDataID + " could not be decoded: " + e.Message);
                }
                if (work != null) work.TextureMs += sw.ElapsedMilliseconds;
            }
            else
            {
                Remember(q.FileDataID, r.data);
            }
            Advance();
        }
        // Last: the owner may apply the character's scene from here, which disposes or replaces mounts.
        if (onProgress != null)
            onProgress();
    }

    bool FileReady(int fdid, out byte[] bytes)
    {
        workFiles.Add(fdid);
        if (files.TryGetValue(fdid, out bytes))
            return true;
        if (!failedFiles.Contains(fdid))
            Request(fdid, false);
        return false;
    }

    /// <summary>Are the textures a description names decoded (or failed for good)? Asks for the ones that are not.</summary>
    bool TexturesReady(WmvIpcClient.SceneMount m)
    {
        bool ready = true;
        if (m == null || m.textures == null)
            return true;
        foreach (var t in m.textures)
        {
            if (t == null || t.fileDataID <= 0 || decoded.ContainsKey(t.fileDataID) || failedFiles.Contains(t.fileDataID))
                continue;
            Request(t.fileDataID, true);
            ready = false;
        }
        return ready;
    }

    void Request(int fdid, bool texture)
    {
        if (!requested.Add(fdid))
            return;
        pending[requestByFileDataID(fdid)] = new PendingFile { FileDataID = fdid, Texture = texture };
    }

    void Remember(int fdid, byte[] bytes)
    {
        byte[] had;
        if (files.TryGetValue(fdid, out had))
            cachedBytes -= had.Length;
        else
            fileOrder.Add(fdid);
        files[fdid] = bytes;
        cachedBytes += bytes.Length;
        // Oldest first, never a file the mount being prepared has asked for: evicting its skeleton for its parent
        // would only have both asked for again, in turn.
        for (int i = 0; i < fileOrder.Count && cachedBytes > MaxCachedBytes; )
        {
            int id = fileOrder[i];
            if (workFiles.Contains(id)) { i++; continue; }
            fileOrder.RemoveAt(i);
            cachedBytes -= files[id].Length;
            files.Remove(id);
        }
    }

    // ---------------------------------------------------------------- the build

    /// <summary>
    /// Take the work as far as the files here allow: the .m2, its skeleton (and the skeleton's parent), the keys of
    /// the sequence the host plays when they live in a .anim, the first skin profile and the textures the target
    /// names; then the build, standalone -- skeleton, bind poses, animator, emitters, posed bounds -- and inactive.
    /// </summary>
    void Advance()
    {
        Work w = work;
        if (w == null || w.Built != null || w.Failed != null || target == null)
            return;

        if (w.Model == null)
        {
            byte[] m2;
            if (!FileReady(w.FileDataID, out m2))
            {
                if (failedFiles.Contains(w.FileDataID)) Fail(w, "its .m2 could not be read");
                return;
            }
            var sw = System.Diagnostics.Stopwatch.StartNew();
            try { w.Model = M2Parser.Parse(m2, w.Sequence); }
            catch (WowParseException e) { Fail(w, "parse failed: " + e.Message); return; }
            w.ParseMs += sw.ElapsedMilliseconds;
            w.M2 = m2;
            if (w.Model.SkinFileDataIDs.Length == 0) { Fail(w, "no skin profile (SFID)"); return; }
            w.SkeletonDone = w.Model.SkeletonFileDataID == 0;
        }

        if (!w.SkeletonDone)
        {
            int skid = w.Model.SkeletonFileDataID;
            byte[] skel;
            if (!FileReady(skid, out skel))
            {
                if (!failedFiles.Contains(skid))
                    return;
                Log(w.Key + ": skeleton " + skid + " could not be read -- the mount is drawn static");
                w.SkeletonDone = true;
            }
            else
            {
                int parentId = M2Parser.ReadSkeletonParentId(skel);
                byte[] parent = null;
                if (parentId > 0 && !FileReady(parentId, out parent))
                {
                    if (!failedFiles.Contains(parentId))
                        return;
                    parent = null;
                }
                var sw = System.Diagnostics.Stopwatch.StartNew();
                try { M2Parser.ApplySkeleton(w.Model, skel, parent, w.Sequence); }
                catch (WowParseException e) { Log(w.Key + ": skeleton " + skid + " not applied (" + e.Message + ") -- drawn static"); }
                w.ParseMs += sw.ElapsedMilliseconds;
                w.SkeletonDone = true;
            }
        }

        if (!w.AnimDone)
        {
            // The parse cannot read a sequence whose keys are in a .anim and falls back to the idle; the mount is
            // to go on screen playing what the host's does, so those keys are fetched and read before the build.
            int animFile = M2Parser.ExternalAnimFileId(w.Model, w.Sequence);
            if (animFile != 0 && w.Model.AnimatedSequence != w.Sequence)
            {
                byte[] anim;
                if (!FileReady(animFile, out anim))
                {
                    if (!failedFiles.Contains(animFile))
                        return;
                    Log(w.Key + ": .anim " + animFile + " of sequence " + w.Sequence + " could not be read -- the mount plays " +
                        w.Model.AnimatedSequence + " instead");
                }
                else
                {
                    var sw = System.Diagnostics.Stopwatch.StartNew();
                    try
                    {
                        M2Parser.ReadAnimationInto(w.M2, w.Sequence, w.Model, anim);
                        w.AnimFileId = animFile;
                        w.AnimBytes = anim;
                    }
                    catch (WowParseException e) { Log(w.Key + ": sequence " + w.Sequence + " could not be read (" + e.Message + ")"); }
                    w.ParseMs += sw.ElapsedMilliseconds;
                }
            }
            w.AnimDone = true;
        }

        if (w.Skin == null)
        {
            int sfid = w.Model.SkinFileDataIDs[0];
            byte[] skinBytes;
            if (!FileReady(sfid, out skinBytes))
            {
                if (failedFiles.Contains(sfid)) Fail(w, "its skin could not be read");
                return;
            }
            var sw = System.Diagnostics.Stopwatch.StartNew();
            try { w.Skin = M2SkinParser.Parse(skinBytes); }
            catch (WowParseException e) { Fail(w, "skin parse failed: " + e.Message); return; }
            w.ParseMs += sw.ElapsedMilliseconds;
        }

        if (!TexturesReady(target))
            return;

        bool[] flags = WmvIpcClient.Flags(target.submeshVisible);
        string name = "mount_" + w.FileDataID;
        long t0 = w.Clock.ElapsedMilliseconds;
        try
        {
            w.Built = WmvModelBuilder.Build(w.Model, w.Skin, SlotTextures(target), name, s => Log(name + ": " + s), null,
                                            flags.Length == w.Skin.Submeshes.Length ? flags : null);
        }
        catch (Exception e)
        {
            Fail(w, "build failed: " + e.GetType().Name + ": " + e.Message);
            return;
        }
        w.BuildMs = w.Clock.ElapsedMilliseconds - t0;
        w.BuiltAtMs = w.Clock.ElapsedMilliseconds;
        liveMounts.Add(w.Built);
        MountsBuilt++;
        w.BuiltTextureSignature = BoundTextureSignature(target);
        // Built and posed; nothing of it shows, animates or emits until the character's scene puts it on.
        w.Built.Root.SetActive(false);
        int playing = w.Model.AnimatedSequence;
        Log(string.Format("{0} ({1}) built in {2} ms since it was requested (parse {3}, texture decode {4}, build {5}): " +
                          "{6} vertices, {7} triangles, {8} submeshes, {9} bone(s){10}, {11} emitter draw call(s), " +
                          "sequence {12}{13} -- waiting for the character's scene",
                          w.Key, w.FileDataID, w.BuiltAtMs, w.ParseMs, w.TextureMs, w.BuildMs, w.Built.VertexCount,
                          w.Built.TriangleCount, w.Built.SubmeshCount, w.Built.Bones.Length,
                          w.Built.Animator != null ? ", animated" : "",
                          w.Built.Emitters != null ? w.Built.Emitters.DrawCallCount : 0, playing,
                          playing >= 0 && playing < w.Model.Sequences.Length
                              ? " (animID " + w.Model.Sequences[playing].AnimId + ")" : ""));
    }

    void Fail(Work w, string reason)
    {
        w.Failed = reason;
        Log(w.Key + " (" + w.FileDataID + ") could not be built: " + reason);
    }

    /// <summary>The decoded image of each slot a description binds; a slot whose file did not arrive is left
    /// out, as a slot the host bound to nothing is.</summary>
    Dictionary<int, BlpImage> SlotTextures(WmvIpcClient.SceneMount m)
    {
        var dict = new Dictionary<int, BlpImage>();
        if (m == null || m.textures == null)
            return dict;
        foreach (var t in m.textures)
        {
            BlpImage img;
            if (t != null && t.fileDataID > 0 && decoded.TryGetValue(t.fileDataID, out img) && img != null)
                dict[t.slot] = img;
        }
        return dict;
    }

    /// <summary>What a description's textures bind right now: each slot with a decoded file. A file that arrives after
    /// an earlier attempt failed changes it, and so gets bound.</summary>
    string BoundTextureSignature(WmvIpcClient.SceneMount m)
    {
        if (m == null || m.textures == null) return "";
        var sb = new System.Text.StringBuilder();
        foreach (var t in m.textures)
            if (t != null && t.fileDataID > 0 && decoded.ContainsKey(t.fileDataID))
                sb.Append(t.slot).Append('=').Append(t.fileDataID).Append(';');
        return sb.ToString();
    }

    void DropWork()
    {
        if (work != null && work.Built != null)
        {
            liveMounts.Remove(work.Built);
            work.Built.Dispose();            // never on screen: nothing hangs from it
        }
        work = null;
        workFiles.Clear();
    }

    // ---------------------------------------------------------------- the commit

    /// <summary>
    /// Put a scene's mount under the character, in the frame the character's scene is applied. The mount on screen
    /// described again gets its textures, geosets and particle colours and, if they changed, a new seat. A new mount
    /// that was built goes on: the character moves onto it, and the mount it rode is disposed only after it left.
    /// A new mount that could not be built leaves the character on screen off any mount. newMount says a different
    /// mount was committed -- built or not -- so the caller starts the character's riding sequence.
    /// </summary>
    public WmvIpcClient.MountAnswer Commit(WmvIpcClient.SceneMount m, WmvRuntimeModel riderBody, out bool newMount)
    {
        newMount = false;
        var answer = new WmvIpcClient.MountAnswer { Key = m != null && m.key != null ? m.key : "", Status = "none", Reason = "" };
        if (disposed || m == null)
            return answer;

        if (IsApplied(m))
        {
            ApplyDisplayState(m, appliedTextureSignature);
            Seat(m, riderBody);
            PruneTextures(m);
            target = null;
            answer.Status = "applied";
            return answer;
        }

        newMount = true;
        Work w = work;
        string why = w == null || w.Key != m.key || w.FileDataID != m.fileDataID ? "it was never prepared for this scene"
                   : w.Failed ?? (w.Built == null ? "it was not built when the character's scene was applied" : null);
        if (why != null)
        {
            // The mount it rode is gone on the host as well: the character stays on screen, on no mount.
            string was = appliedKey;
            DetachRider();
            DisposeApplied();
            DropWork();
            target = null;
            Log(string.Format("{0} ({1}) is not put under the character: {2}{3}", m.key, m.fileDataID, why,
                              was.Length > 0 ? "; " + was + " disposed after the character came off it" : ""));
            answer.Status = "failed";
            answer.Reason = why;
            return answer;
        }

        // THE SWAP, IN ONE FRAME: the new mount comes on, the character moves onto it, and only then does the
        // mount it rode go.
        WmvRuntimeModel old = Mount.Runtime;
        string oldKey = appliedKey;
        work = null;
        workFiles.Clear();
        target = null;
        Mount.Runtime = w.Built;
        Mount.Model = w.Model;
        Mount.M2Bytes = w.M2;
        Mount.FileDataID = w.FileDataID;
        Mount.Name = "mount_" + w.FileDataID;
        Mount.SelectedSequence = w.Sequence;
        Mount.BoneTrackCache.Clear();
        Mount.MaterialTrackCache.Clear();
        Mount.AnimFileCache.Clear();
        Mount.AbandonAnimFetches();        // the slot now holds another model: a fetch out for the last one is dropped
        Mount.HaveAppState = false;
        if (w.AnimFileId != 0 && w.AnimBytes != null)
            Mount.AnimFileCache[w.AnimFileId] = w.AnimBytes;
        Mount.Geosets = null;
        Mount.ParticleColor = null;
        appliedKey = m.key;
        ApplyDisplayState(m, w.BuiltTextureSignature);
        w.Built.Root.SetActive(true);
        placementSignature = "";
        int placement = Seat(m, riderBody);
        if (old != null)
            DisposeRuntime(old);
        // The host starts a mount's clock at its first frame, playing at 1x (a fresh AnimManager, AnimControl::
        // UpdateModel). From now, not from the build, which may have been waiting on the character.
        if (Mount.Runtime.Animator != null)
            Mount.Runtime.Animator.StartFromApp(true, 0f, 1f);
        PruneTextures(m);
        int playing = Mount.Model.AnimatedSequence;
        Log(string.Format("{0} ({1}) on screen, {2} ms after it was requested ({3} ms waiting for the character's scene): " +
                          "playing sequence {4}; {5}{6}",
                          m.key, m.fileDataID, w.Clock.ElapsedMilliseconds, w.Clock.ElapsedMilliseconds - w.BuiltAtMs, playing,
                          riderBody != null && placement >= 0 ? "the character rides it" : "no character to seat",
                          oldKey.Length > 0 ? "; " + oldKey + " disposed after the character left it" : ""));
        answer.Status = "applied";
        return answer;
    }

    /// <summary>A scene without a mount: the character comes off the one it rides, back at the origin with the
    /// identity, and the mount is disposed. The character and what it wears are not touched otherwise.</summary>
    public WmvIpcClient.MountAnswer Dismount()
    {
        string was = appliedKey;
        int fdid = Mount.FileDataID;
        CancelTarget();
        DetachRider();
        DisposeApplied();
        if (was.Length > 0)
            Log(was + " (" + fdid + ") dismounted: the character is off it, at the origin, and the mount is disposed");
        return new WmvIpcClient.MountAnswer { Key = "", Status = "none", Reason = "" };
    }

    /// <summary>Destroy everything this holds: the character comes off first, then the mount on screen, the mount
    /// being prepared and every request out goes. Late answers are not claimed.</summary>
    public void Dispose()
    {
        if (disposed)
            return;
        CancelTarget();
        DetachRider();
        DisposeApplied();
        pending.Clear();                   // nothing of this claims an answer any more
        requested.Clear();
        riderAnimsRequested.Clear();
        files.Clear();
        fileOrder.Clear();
        cachedBytes = 0;
        decoded.Clear();
        failedFiles.Clear();
        riderAnimsFailed.Clear();
        disposed = true;
    }

    void ApplyDisplayState(WmvIpcClient.SceneMount m, string builtWith)
    {
        WmvRuntimeModel rt = Mount.Runtime;
        if (rt == null)
            return;
        string texSig = BoundTextureSignature(m);
        Dictionary<int, BlpImage> slots = SlotTextures(m);
        if (texSig != builtWith)
        {
            WmvModelBuilder.RebindTextures(rt, slots, Mount.Name, null);
            Log(m.key + ": textures re-bound (" + slots.Count + " slot(s))");
        }
        appliedTextureSignature = texSig;
        Mount.Textures.Clear();
        foreach (var kv in slots) Mount.Textures[kv.Key] = kv.Value;
        Mount.TextureIds.Clear();
        if (m.textures != null)
            foreach (var t in m.textures)
                if (t != null && t.fileDataID > 0)
                    Mount.TextureIds[t.slot] = t.fileDataID;

        // The build took the flags when they fit the skin; a newer description's are applied through the triangle
        // arrays the build keeps, as a Geosets checkbox is.
        bool[] flags = WmvIpcClient.Flags(m.submeshVisible);
        if (flags.Length > 0 && !SameFlags(flags, rt.SubmeshVisible) &&
            WmvModelBuilder.ApplySubmeshVisibility(rt, flags, null) < 0)
            Log(string.Format("{0}: geoset flags not applied -- the host listed {1} submeshes, the skin has {2}, so the " +
                              "geoset-id rule decides", m.key, flags.Length, rt.SkinSubmeshCount));

        // A fresh build carries no replacement (Mount.ParticleColor was cleared with it), so one is set whenever the
        // description has one, and again whenever it changes.
        Color[][] sets = ParticleSets(m.particleColorSets);
        if (!SameSets(sets, Mount.ParticleColor))
        {
            Mount.ParticleColor = sets;
            if (rt.Emitters != null)
                rt.Emitters.SetParticleColorOverride(sets);
            Log(m.key + ": particle colours " + (sets != null ? "replaced by the display's three sets" : "back to the emitters' own") +
                (rt.Emitters != null ? "" : " (the mount has no emitters)"));
        }
    }

    // ---------------------------------------------------------------- the seat

    /// <summary>
    /// Hang the character's body root where the host draws the rider: under the placement Placement gives, with the
    /// identity rotation and the character's scale on all three axes. Re-placed only when the bone, position,
    /// scale, mount or character changed. Returns the placement case, or -1 when there is nothing to seat.
    /// </summary>
    int Seat(WmvIpcClient.SceneMount m, WmvRuntimeModel riderBody)
    {
        if (riderBody == null || riderBody.Root == null || Mount.Runtime == null || Mount.Runtime.Root == null)
            return -1;
        Transform body = riderBody.Root.transform;
        if (rider != null && rider != body)
            DetachRider();                  // a different character: the one before comes off first
        float scale = m.riderScale > 0f ? m.riderScale : 1f;
        // A new mount clears the signature before it seats the character (Commit), so the mount is not part of it.
        string sig = m.bone + ":" + string.Join(",", Array.ConvertAll(m.position ?? new float[0], x => x.ToString("R"))) +
                     ":" + scale.ToString("R");
        if (rider == body && sig == placementSignature)
            return -1;

        Transform parent;
        Vector3 local;
        int placement = Placement(m.bone, m.position, Mount.Runtime, Mount.Model, out parent, out local);
        body.SetParent(parent, false);
        body.localPosition = local;
        body.localRotation = Quaternion.identity;
        body.localScale = new Vector3(scale, scale, scale);
        rider = body;
        placementSignature = sig;
        seatCase = placement;
        seatBone = m.bone;
        seatLocalPosition = local;
        seatScale = scale;
        Log(string.Format("{0}: the character hangs from attachment {1} -- {2}, local position ({3:F4}, {4:F4}, {5:F4}), " +
                          "scale {6}", m.key, m.attachmentId,
                          placement == CaseBone ? "bone " + m.bone + " of the mount, less its pivot (case B)"
                          : placement == CaseNoBoneTransform ? "the mount's root at the converted position: bone " + m.bone +
                                                               " has no Transform in this build (case C)"
                          : "the mount's root at its origin: the mount has no such attachment (case A)",
                          local.x, local.y, local.z, scale));
        return placement;
    }

    /// <summary>
    /// Where the character hangs from a mount, as the host draws the rider (Attachment::setup -> WoWModel::setupAtt ->
    /// ModelAttachment::setup: the mount bone's matrix, then a translation to the attachment's position):
    ///   CaseNoAttachment     bone &lt; 0 -- the mount's table has no entry for the id, and the host applies no
    ///                        transform at all: the mount's root, at zero;
    ///   CaseBone             a bone this build has a Transform for -- that bone, at the attachment position less the
    ///                        bone's pivot, both converted (WmvCharacterDresser.AttachmentLocalPosition): a Unity bone
    ///                        stands at its pivot with the identity at rest, so the animated bone carries the point
    ///                        exactly as the host's bone matrix does;
    ///   CaseNoBoneTransform  a bone this build has no Transform for (a mount drawn as a static mesh) -- the mount's
    ///                        root at the converted position: the host still translates to it, under a bone at rest.
    /// position is the host's, in WoW model space. Kept static so the self-test checks the three cases directly.
    /// </summary>
    public static int Placement(int bone, float[] position, WmvRuntimeModel mount, M2ParsedModel mountModel,
                                out Transform parent, out Vector3 localPosition)
    {
        parent = mount != null && mount.Root != null ? mount.Root.transform : null;
        localPosition = Vector3.zero;
        if (bone < 0)
            return CaseNoAttachment;
        var pos = position != null && position.Length >= 3 ? new WowVec3(position[0], position[1], position[2])
                                                           : new WowVec3(0f, 0f, 0f);
        if (mount != null && mountModel != null && bone < mount.Bones.Length && bone < mountModel.Bones.Length &&
            mount.Bones[bone] != null)
        {
            parent = mount.Bones[bone];
            localPosition = WmvCharacterDresser.AttachmentLocalPosition(pos, mountModel.Bones[bone].Pivot);
            return CaseBone;
        }
        float x, y, z;
        WowCoordinateConverter.ConvertPosition(pos, out x, out y, out z);
        localPosition = new Vector3(x, y, z);
        return CaseNoBoneTransform;
    }

    void DetachRider()
    {
        Transform t = rider;
        rider = null;
        placementSignature = "";
        seatCase = seatBone = -1;
        if (t == null)
            return;                         // already destroyed with its own model
        t.SetParent(null, false);
        t.localPosition = Vector3.zero;
        t.localRotation = Quaternion.identity;
        t.localScale = Vector3.one;
    }

    /// <summary>Dispose a mount runtime -- with the character taken off it first if it still hangs anywhere under it.</summary>
    void DisposeRuntime(WmvRuntimeModel rt)
    {
        if (rt == null)
            return;
        if (rider != null && rt.Root != null && rider.IsChildOf(rt.Root.transform))
            DetachRider();
        liveMounts.Remove(rt);
        rt.Dispose();
    }

    void DisposeApplied()
    {
        DisposeRuntime(Mount.Runtime);
        Mount.Runtime = null;
        Mount.Model = null;
        Mount.M2Bytes = null;
        Mount.FileDataID = 0;
        Mount.Name = "WoWModel";
        Mount.SelectedSequence = -1;
        Mount.Textures.Clear();
        Mount.TextureIds.Clear();
        Mount.BoneTrackCache.Clear();
        Mount.MaterialTrackCache.Clear();
        Mount.AnimFileCache.Clear();
        Mount.AbandonAnimFetches();        // as in Commit: the slot holds nothing now
        Mount.HaveAppState = false;
        Mount.Geosets = null;
        Mount.ParticleColor = null;
        appliedKey = "";
        appliedTextureSignature = "";
    }

    /// <summary>Keep only the decoded textures the mount on screen binds: its build and a rebind have uploaded them,
    /// and the images stay for a later rebind of the same mount.</summary>
    void PruneTextures(WmvIpcClient.SceneMount m)
    {
        var keep = new HashSet<int>();
        if (m != null && m.textures != null)
            foreach (var t in m.textures)
                if (t != null) keep.Add(t.fileDataID);
        var drop = new List<int>();
        foreach (var kv in decoded)
            if (!keep.Contains(kv.Key)) drop.Add(kv.Key);
        foreach (int k in drop) decoded.Remove(k);
    }

    // ---------------------------------------------------------------- framing

    /// <summary>The eight corners of a box as the signs of its extents, allocated once: carrying a box into the world
    /// allocates nothing.</summary>
    static readonly Vector3[] CornerSigns =
    {
        new Vector3(-1f, -1f, -1f), new Vector3(1f, -1f, -1f), new Vector3(-1f, 1f, -1f), new Vector3(1f, 1f, -1f),
        new Vector3(-1f, -1f, 1f), new Vector3(1f, -1f, 1f), new Vector3(-1f, 1f, 1f), new Vector3(1f, 1f, 1f),
    };

    /// <summary>
    /// What the camera, the shadow window and the light check frame while the character rides the mount on screen: one
    /// box in the world around both models. The mount's box (WmvRuntimeModel.Bounds, in its own space) is carried through
    /// its root and the character's body box through Body.Root.localToWorldMatrix, which holds the mount bone the body
    /// hangs from as it is posed at this instant, the seat's offset and the rider scale. False when the character rides
    /// nothing on screen: union is then the character's own box carried through its root. Measured where it is called
    /// -- the frame a mount goes on, is swapped or comes off -- and never per frame.
    /// </summary>
    public bool UnionBounds(WmvRuntimeModel riderBody, out Bounds union)
    {
        union = new Bounds();
        if (riderBody == null || riderBody.Root == null)
            return false;
        if (rider == null || rider != riderBody.Root.transform || Mount.Runtime == null || Mount.Runtime.Root == null)
        {
            union = WorldBounds(riderBody.Bounds, riderBody.Root.transform.localToWorldMatrix);
            return false;
        }
        union = UnionOf(Mount.Runtime, riderBody);
        return true;
    }

    /// <summary>The world box around a mount and a character hanging from it, each model's own box carried through its
    /// root (see UnionBounds). Kept static so the self-test checks it on a hierarchy of its own.</summary>
    public static Bounds UnionOf(WmvRuntimeModel mount, WmvRuntimeModel riderBody)
    {
        bool any = false;
        Vector3 min = Vector3.zero, max = Vector3.zero;
        if (mount != null && mount.Root != null)
            Encapsulate(mount.Bounds, mount.Root.transform.localToWorldMatrix, ref any, ref min, ref max);
        if (riderBody != null && riderBody.Root != null)
            Encapsulate(riderBody.Bounds, riderBody.Root.transform.localToWorldMatrix, ref any, ref min, ref max);
        var b = new Bounds();
        if (any)
            b.SetMinMax(min, max);
        return b;
    }

    /// <summary>A box in a transform's space as a box in the world: the axis-aligned box around its eight corners carried
    /// through localToWorld, which holds the whole box whatever that turns, moves or scales.</summary>
    public static Bounds WorldBounds(Bounds local, Matrix4x4 localToWorld)
    {
        bool any = false;
        Vector3 min = Vector3.zero, max = Vector3.zero;
        Encapsulate(local, localToWorld, ref any, ref min, ref max);
        var b = new Bounds();
        b.SetMinMax(min, max);
        return b;
    }

    /// <summary>Grow a world box (min and max; any false while it holds nothing yet) by the eight corners of a box in a
    /// transform's space, carried through localToWorld. No allocation.</summary>
    public static void Encapsulate(Bounds local, Matrix4x4 localToWorld, ref bool any, ref Vector3 min, ref Vector3 max)
    {
        Vector3 c = local.center, e = local.extents;
        for (int i = 0; i < CornerSigns.Length; i++)
        {
            Vector3 s = CornerSigns[i];
            Vector3 p = localToWorld.MultiplyPoint3x4(new Vector3(c.x + s.x * e.x, c.y + s.y * e.y, c.z + s.z * e.z));
            if (!any)
            {
                min = p;
                max = p;
                any = true;
                continue;
            }
            min = Vector3.Min(min, p);
            max = Vector3.Max(max, p);
        }
    }

    // ---------------------------------------------------------------- display state

    /// <summary>
    /// The host's particleColorSets as WmvEmitterRuntime.SetParticleColorOverride takes them: set i -- the ramp an
    /// emitter with ParticleColorIndex 11 + i draws -- as its start, mid and end colours. 27 numbers, three sets of
    /// three RGB colours, 0..255; anything shorter is no replacement (null), which leaves the authored colours.
    /// </summary>
    public static Color[][] ParticleSets(int[] values)
    {
        if (values == null || values.Length < 27)
            return null;
        var sets = new Color[3][];
        for (int s = 0; s < 3; s++)
        {
            sets[s] = new Color[3];
            for (int k = 0; k < 3; k++)
            {
                int i = (s * 3 + k) * 3;
                sets[s][k] = new Color(values[i] / 255f, values[i + 1] / 255f, values[i + 2] / 255f, 1f);
            }
        }
        return sets;
    }

    static bool SameSets(Color[][] a, Color[][] b)
    {
        if (a == null || b == null)
            return a == b;
        for (int s = 0; s < 3; s++)
            for (int k = 0; k < 3; k++)
                if (a[s][k].r != b[s][k].r || a[s][k].g != b[s][k].g || a[s][k].b != b[s][k].b)
                    return false;
        return true;
    }

    static bool SameFlags(bool[] a, bool[] b)
    {
        if (a == null || b == null || a.Length != b.Length)
            return false;
        for (int i = 0; i < a.Length; i++)
            if (a[i] != b[i]) return false;
        return true;
    }

    void Log(string s)
    {
        if (log != null) log("mount: " + s);
    }
}
