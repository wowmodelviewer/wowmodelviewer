// WmvSlotAnimation.cs
//
// The app's animation choice and playback, applied to the model in a slot (WmvModelSlot): select a sequence, switch
// to it -- from the slot's own caches, from the file the model was built from, or from a .anim fetched once -- and
// follow the app's play/pause, speed and position. Nothing here builds, reloads or disposes a model, and nothing
// runs per frame: a model's animator keeps its own clock (WmvM2Animator), and these calls only re-bind or correct it.
// WmvMain drives every slot it animates through one of these; the code is the one the model on screen always ran,
// moved here so a second slot takes the same path and the lifecycle self-test can drive it without a player.
//
// A CHARACTER RIDING A MOUNT (protocol 5) is two models on two clocks: the character in WmvMain's slot, the mount in
// WmvMountedScene's. The host says which one a push is about -- modelAnimation carries a role ("mount" or "rider")
// and the character's load serial, and a ridden mount's modelAnimationState carries the mount in its top level with
// the rider's state nested beside it -- and RouteSelection turns that into the slot the push goes to. The role
// decides, never the FileDataID: a mount model can also be a playable race's body. ApplyRidden applies both halves of
// one state in one pass, each to its own animator; neither clock is read or moved for the other.

using System;
using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow;

public class WmvSlotAnimation
{
    readonly Func<int, string> requestByFileDataID;
    readonly Action<string> status;

    /// <summary>Raised when a slot's model has been bound to a sequence and its clock started (ApplyResolvedSequence):
    /// the switch is on screen. WmvMain's capture hook waits for it while a character rides. Null for nothing.</summary>
    public Action<WmvModelSlot> SequenceApplied;

    /// <param name="requestByFileDataID">Sends an asset request and returns its id (WmvIpcClient.RequestAssetByFileDataID).
    /// The answer is the caller's to hand back to OnAnimFileBytes, for the slot whose PendingAnimFetch names it.</param>
    /// <param name="status">A status line (WmvStatusOverlay.Set).</param>
    public WmvSlotAnimation(Func<int, string> requestByFileDataID, Action<string> status)
    {
        this.requestByFileDataID = requestByFileDataID;
        this.status = status;
    }

    // ---------------------------------------------------------------- which model a push is about

    /// <summary>The two roles of a ridden mount's animation pushes (protocol 5).</summary>
    public const string RoleMount = "mount", RoleRider = "rider";

    /// <summary>What the player holds when a push arrives, as far as routing it goes. WmvMain fills it.</summary>
    public struct Holding
    {
        public bool Loading;               // a load of either kind is in flight
        public bool LoadIsCharacter;       // ... of a playable character
        public int LoadSerial;             // ... under this load serial
        public int LoadMountPreparing;     // the mount being prepared for that character, 0 for none
        public int RiderLoad;              // the load serial of the character on screen, 0 for none
        public int RiderFileDataID;        // ... its model's file
        public bool RiderMounted;          // ... hangs from a mount on screen
        public int MountFileDataID;        // the mount on screen, 0 for none
        public int MountPreparing;         // a mount being prepared for the character on screen, 0 for none
    }

    /// <summary>Where a push goes (RouteSelection).</summary>
    public enum Route
    {
        AsBefore,           // no role: about the model its FileDataID names, exactly as before protocol 5
        HeldForDismount,    // no role, but about the character riding on screen: the host took it off its mount
        Rider,              // the character on screen
        RiderOfLoad,        // the character being loaded
        Mount,              // the mount on screen
        MountPreparing,     // a mount of that file still being prepared: kept for the frame it goes on
        Ignored,            // about none of them
    }

    /// <summary>
    /// Which model an animation push is about. No role is the model the push names, as it always was -- except a push
    /// about the character while it rides on screen: a host that seats characters on mounts sends every push about a
    /// ridden mount's models with a role, so one without is the character after the host took it off its mount, ahead
    /// of the scene that says so. A role picks the model: "rider" the character, "mount" the mount it rides; the load
    /// serial picks the character -- the one being loaded or the one on screen -- and a push about neither is ignored.
    /// The FileDataID only has to name that model's file, so a mount and a rider built from one file are told apart by
    /// the role alone. A mount of that file still being prepared is checked before the one on screen: the host has
    /// replaced the mount on screen with it already. fileDataID 0 is "not said" (a nested rider state carries none).
    /// why says what an ignored push was about.
    /// </summary>
    public static Route RouteSelection(string role, int load, int fileDataID, Holding h, out string why)
    {
        why = "";
        if (string.IsNullOrEmpty(role))
        {
            if (!h.Loading && h.RiderMounted && fileDataID > 0 && fileDataID == h.RiderFileDataID)
                return Route.HeldForDismount;
            return Route.AsBefore;
        }
        bool mount = role == RoleMount;
        if (!mount && role != RoleRider)
        {
            why = "the role is neither \"" + RoleMount + "\" nor \"" + RoleRider + "\"";
            return Route.Ignored;
        }
        if (h.Loading)
        {
            if (!h.LoadIsCharacter || load != h.LoadSerial)
            {
                why = "a load is in flight that it is not about";
                return Route.Ignored;
            }
            if (!mount)
                return Route.RiderOfLoad;
            if (h.LoadMountPreparing > 0 && fileDataID == h.LoadMountPreparing)
                return Route.MountPreparing;
            why = "the character being loaded rides no mount of that file yet";
            return Route.Ignored;
        }
        if (h.RiderLoad <= 0 || load != h.RiderLoad)
        {
            why = h.RiderLoad <= 0 ? "no character is on screen" : "not the load of the character on screen";
            return Route.Ignored;
        }
        if (!mount)
        {
            if (fileDataID > 0 && h.RiderFileDataID > 0 && fileDataID != h.RiderFileDataID)
            {
                why = "it names a file other than the character's";
                return Route.Ignored;
            }
            return Route.Rider;
        }
        if (h.MountPreparing > 0 && fileDataID == h.MountPreparing)
            return Route.MountPreparing;
        if (h.MountFileDataID > 0 && fileDataID == h.MountFileDataID)
            return Route.Mount;
        why = h.MountFileDataID > 0 || h.MountPreparing > 0 ? "the character rides no mount of that file"
                                                            : "the character rides no mount";
        return Route.Ignored;
    }

    /// <summary>Where the two halves of a ridden mount's modelAnimationState go: its top level by the rule of a selection
    /// with role "mount" and that FileDataID, its nested rider by the rule of one with role "rider".</summary>
    public static void RouteRiddenState(int load, int mountFileDataID, Holding h, out Route mount, out Route rider)
    {
        string why;
        mount = RouteSelection(RoleMount, load, mountFileDataID, h, out why);
        rider = RouteSelection(RoleRider, load, 0, h, out why);
    }

    // ---------------------------------------------------------------- selection and switching

    /// <summary>
    /// The app selected a sequence for the model in a slot: remember it, and show it unless that model is
    /// still loading (aboutTheLoad: the selection names the load in flight) or already plays it.
    /// </summary>
    public void SelectSequence(WmvModelSlot slot, int sequenceIndex, bool aboutTheLoad)
    {
        slot.SelectedSequence = sequenceIndex;

        if (slot.Runtime == null || slot.M2Bytes == null || aboutTheLoad)
        {
            // Still loading. The parse below will use this selection when it gets there.
            status("Animation " + sequenceIndex + " selected (model still loading)");
            return;
        }
        // A re-push of what is PLAYING is a no-op: the animator is not re-bound and its clock is
        // not touched. Judged by what is playing, not by what was last asked for, so a request
        // that fell back to the idle is retried when it is asked for again.
        if (slot.Runtime.Animator != null && slot.Runtime.Animator.SequenceIndex == sequenceIndex &&
            slot.Model != null && slot.Model.AnimatedSequence == sequenceIndex)
        {
            status("Animation unchanged");
            return;
        }

        SwitchToSequence(slot, sequenceIndex);
    }

    /// <summary>
    /// Show a different animation of the model already loaded in a slot.
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
    public void SwitchToSequence(WmvModelSlot slot, int sequenceIndex)
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
        if (slot.BoneTrackCache.TryGetValue(sequenceIndex, out cached))
        {
            // The material tracks that FOLLOW the sequence (colour alpha, texture transforms)
            // were re-read with the bones the first time; restore that read too, or the
            // materials would keep the keys of whichever sequence was read last.
            WmvModelSlot.MaterialTrackSet mats;
            if (slot.MaterialTrackCache.TryGetValue(sequenceIndex, out mats))
            {
                slot.Model.Colors = mats.Colors;
                slot.Model.TextureTransforms = mats.Transforms;
                slot.Model.MaterialSurvey = mats.Survey;   // its per-sequence counts, for the log
            }
            long heapBefore = System.GC.GetTotalMemory(false);
            int gcBefore = System.GC.CollectionCount(0);
            var swc = System.Diagnostics.Stopwatch.StartNew();
            slot.Model.Bones = cached;
            slot.Model.AnimatedSequence = sequenceIndex;
            slot.Model.AnimationSkipReason = null;
            ApplyResolvedSequence(slot, sequenceIndex, "cached");
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
        int animFileId = M2Parser.ExternalAnimFileId(slot.Model, sequenceIndex);
        byte[] external = null;
        if (animFileId != 0 && !slot.AnimFileCache.TryGetValue(animFileId, out external))
        {
            foreach (int waiting in slot.PendingAnimFetch.Values)
                if (waiting == sequenceIndex)
                    return;                          // already on its way
            string req = requestByFileDataID(animFileId);
            slot.PendingAnimFetch[req] = sequenceIndex;
            status(string.Format("Animation {0}: fetching its .anim file ({1})",
                                 sequenceIndex, animFileId));
            return;
        }

        ReadAndApplySequence(slot, sequenceIndex, external, animFileId == 0 ? "in-file" : "from .anim");
    }

    /// <summary>
    /// Fetch every external .anim file the model in a slot names, as soon as it is built.
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
    public void PrefetchAnimFiles(WmvModelSlot slot)
    {
        if (slot.Model == null || WmvModelBuilder.Debug_.NoAnim)
            return;
        var wanted = new HashSet<int>();
        foreach (var e in slot.Model.AnimFileIds)
            if (e.FileDataID > 0)
                wanted.Add(e.FileDataID);
        // A switch waiting on its .anim has already asked for that file.
        foreach (int waiting in slot.PendingAnimFetch.Values)
            if (waiting >= 0)
                wanted.Remove(M2Parser.ExternalAnimFileId(slot.Model, waiting));
        if (wanted.Count == 0)
            return;
        foreach (int fileId in wanted)
        {
            if (slot.AnimFileCache.ContainsKey(fileId))
                continue;
            string req = requestByFileDataID(fileId);
            slot.PendingAnimFetch[req] = -1;     // -1: nothing is waiting on it, just fill the cache
        }
        Debug.Log(string.Format("WMV: anim: fetching {0} .anim file(s) up front so switching to "
                                + "one never waits", wanted.Count));
    }

    /// <summary>The .anim bytes a slot asked for arrived. Cache them and finish the switch that was waiting.</summary>
    public void OnAnimFileBytes(WmvModelSlot slot, WmvIpcClient.AssetResponse r, int sequenceIndex)
    {
        slot.PendingAnimFetch.Remove(r.requestId);
        if (sequenceIndex < 0 && (!r.ok || r.data == null))
        {
            slot.PendingAnimFetch.Remove(r.requestId);
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
                sequenceIndex, M2Parser.ExternalAnimFileId(slot.Model, sequenceIndex),
                r.error ?? "empty response"));
            status(string.Format("Animation {0}: its .anim file could not be read",
                                 sequenceIndex));
            return;
        }
        if (sequenceIndex < 0)
        {
            // A prefetch: nothing is waiting on it, it just belongs in the cache. The reply names
            // the file, so it can be filed without a sequence to look it up from.
            slot.AnimFileCache[r.fileDataID] = r.data;
            return;
        }
        int animFileId = M2Parser.ExternalAnimFileId(slot.Model, sequenceIndex);
        if (animFileId != 0)
            slot.AnimFileCache[animFileId] = r.data;
        // Only apply if this is still what the app wants: the user may have moved on while the
        // bytes were in flight.
        if (sequenceIndex != slot.SelectedSequence)
            return;
        ReadAndApplySequence(slot, sequenceIndex, r.data, "from .anim");
    }

    /// <summary>Read one sequence's bone tracks into a slot's model, cache them, and put them on screen.</summary>
    void ReadAndApplySequence(WmvModelSlot slot, int sequenceIndex, byte[] external, string source)
    {
        try
        {
            // Time AND bytes. The time is what the switch costs now; the allocation is what it
            // costs a frame or two later, when the collector runs -- and it is the collector, not
            // the reading, that a viewer feels as a stutter.
            long heapBefore = System.GC.GetTotalMemory(false);
            int gcBefore = System.GC.CollectionCount(0);
            var sw = System.Diagnostics.Stopwatch.StartNew();
            M2Parser.ReadAnimationInto(slot.M2Bytes, sequenceIndex, slot.Model, external);
            long readMs = sw.ElapsedMilliseconds;

            // Cache under the sequence that RESOLVED, not the one asked for: a request that fell
            // back to the idle must not be remembered as though it had played.
            if (slot.Model.AnimatedSequence >= 0)
            {
                slot.BoneTrackCache[slot.Model.AnimatedSequence] = slot.Model.Bones;
                // Copies: the parser refills these arrays in place on the next read.
                slot.MaterialTrackCache[slot.Model.AnimatedSequence] = new WmvModelSlot.MaterialTrackSet
                {
                    Colors = (M2ColorDef[])slot.Model.Colors.Clone(),
                    Transforms = (M2TextureTransform[])slot.Model.TextureTransforms.Clone(),
                    Survey = slot.Model.MaterialSurvey
                };
            }

            ApplyResolvedSequence(slot, sequenceIndex, source);
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
            status("Animation change failed: " + e.Message);
        }
    }

    /// <summary>Bind whatever a slot's model now holds, and say what is actually playing.</summary>
    void ApplyResolvedSequence(WmvModelSlot slot, int requested, string source)
    {
        if (!WmvModelBuilder.ApplySequence(slot.Runtime, slot.Model, s => Debug.Log("WMV: " + s)))
        {
            status("Animation " + requested + " could not be played");
            return;
        }
        // Report what is PLAYING, not what was asked for: a sequence whose keyframes cannot be
        // read falls back to the idle, and saying "animation 20" while the idle plays is the kind
        // of log that costs an hour later.
        int playing = slot.Model.AnimatedSequence;
        status(string.Format("Animation {0} (animID {1}, {2} ms, {3}){4}",
                             playing, slot.Model.Sequences[playing].AnimId,
                             slot.Model.Sequences[playing].Length, source,
                             playing == requested ? "" : " -- fell back from " + requested));
        if (playing != requested && slot.Model.AnimationSkipReason != null)
            Debug.Log("WMV: anim: " + slot.Model.AnimationSkipReason);

        // The app's playback state applies to whatever is now on screen. Without this a switch
        // starts from the animator's own defaults -- playing, at 1x -- which is wrong whenever the
        // app is paused or the speed slider is not at 1, and is not corrected until the next
        // heartbeat. The heartbeat is meant to correct DRIFT, not to start the animation.
        if (slot.HaveAppState && slot.Runtime.Animator != null)
        {
            // The app's position for this sequence, projected by the time the state has been
            // waiting -- a .anim fetch can take a while, and a heartbeat may be a second old --
            // and applied as given: a freshly bound sequence is a fresh clock, and a difference
            // held to the dead band here would stay for the life of the sequence.
            float at = slot.LastAppState.sequenceIndex == playing ? ProjectedTime(slot.LastAppState) : 0f;
            slot.Runtime.Animator.StartFromApp(slot.LastAppState.playing, at, slot.LastAppState.speed);
        }
        // Watch whether it actually starts moving; see WmvM2Animator.BeginAdvanceWatch.
        if (slot.Runtime.Animator != null)
            slot.Runtime.Animator.BeginAdvanceWatch();
        if (SequenceApplied != null)
            SequenceApplied(slot);
    }

    /// <summary>The app's position in a state, projected from when the state arrived to now, at the app's speed while it
    /// plays.</summary>
    static float ProjectedTime(WmvIpcClient.AnimationState s)
    {
        float at = s.timeMs;
        if (s.playing && s.receivedSeconds > 0.0)
            at += (float)((WmvIpcClient.NowSeconds - s.receivedSeconds) * 1000.0) * Mathf.Max(s.speed, 0f);
        return at;
    }

    // ---------------------------------------------------------------- playback

    /// <summary>Apply one playback state from the app to the animator of the model in a slot (see
    /// WmvMain.HandleModelAnimationState).</summary>
    public void ApplyAnimationState(WmvModelSlot slot, WmvIpcClient.AnimationState s)
    {
        if (slot.Runtime == null || slot.Runtime.Animator == null)
            return;                                     // nothing playing to apply it to
        // Remember it even when it cannot be applied yet -- a deferred or fallen-back switch
        // will ask for it as soon as it knows what is playing.
        slot.LastAppState = s;
        slot.HaveAppState = true;

        if (s.sequenceIndex >= 0 && slot.Runtime.Animator.SequenceIndex != s.sequenceIndex)
        {
            // Not about what is on screen. The play/pause and speed still are, though: they are
            // the app's, not the sequence's, and dropping them here is what left a switch running
            // at the wrong speed or moving while the app was paused.
            slot.Runtime.Animator.SetTransportOnly(s.playing, s.speed);
            return;
        }

        bool wasPlaying = slot.Runtime.Animator.IsPlaying;
        float wasSpeed = slot.Runtime.Animator.Speed;
        slot.Runtime.Animator.SetPlaybackState(s.playing, s.timeMs, s.speed, s.explicitState);

        // Only the changes worth reading are surfaced: the heartbeat would otherwise write a line
        // a second for the whole session.
        if (s.playing != wasPlaying)
            status(s.playing ? "Playing" : "Paused");
        else if (System.Math.Abs(s.speed - wasSpeed) > 0.001f)
            status(string.Format("Speed {0:0.##}x", s.speed));
    }

    /// <summary>
    /// A ridden mount's modelAnimationState, both halves in this one call: the top level to the mount's slot, the nested
    /// rider to the character's -- each to its own animator, as ApplyAnimationState applies any state. Either slot may be
    /// null: that half is about a model not on screen. The rider's playing is applied as the host sent it, which is the
    /// MOUNT's pause: the host's tick stops the whole tree's time when the mount is paused, whatever the rider's own
    /// animation manager says.
    /// </summary>
    public void ApplyRidden(WmvModelSlot mount, WmvModelSlot rider, WmvIpcClient.AnimationState s)
    {
        if (mount != null)
            ApplyAnimationState(mount, s);
        if (rider != null && s.hasRider)
            ApplyAnimationState(rider, s.RiderState());
    }

    /// <summary>
    /// Start the clock of a model that has just gone on screen, from the app's last state for its slot: at the state's
    /// position projected to now when the state names the sequence playing and arrived no earlier than validFrom
    /// (WmvIpcClient.NowSeconds); otherwise at the sequence's first frame, with that state's play/pause and speed -- or
    /// the fallback's when the slot has no state at all. Returns the position started at, in ms, or -1 when the slot has
    /// no animator.
    /// </summary>
    public static float StartClock(WmvModelSlot slot, double validFrom, bool fallbackPlaying, float fallbackSpeed)
    {
        WmvM2Animator animator = slot != null && slot.Runtime != null ? slot.Runtime.Animator : null;
        if (animator == null || slot.Model == null)
            return -1f;
        if (!slot.HaveAppState)
        {
            animator.StartFromApp(fallbackPlaying, 0f, fallbackSpeed);
            return 0f;
        }
        WmvIpcClient.AnimationState s = slot.LastAppState;
        float at = s.sequenceIndex == slot.Model.AnimatedSequence && s.receivedSeconds >= validFrom ? ProjectedTime(s) : 0f;
        animator.StartFromApp(s.playing, at, s.speed);
        return at;
    }

    /// <summary>
    /// Start the clock of a model that has just gone on screen in step with another one the host started in the same
    /// instant and has run on the same ticks since: elapsedMs of playback into its sequence at this slot's own speed (the
    /// slot's last state's, or the fallback when it has none), playing or paused as given. Returns the position started at,
    /// in ms, or -1 when the slot has no animator.
    /// </summary>
    public static float StartInStep(WmvModelSlot slot, double elapsedMs, bool playing, float fallbackSpeed)
    {
        WmvM2Animator animator = slot != null && slot.Runtime != null ? slot.Runtime.Animator : null;
        if (animator == null || slot.Model == null)
            return -1f;
        float speed = slot.HaveAppState ? slot.LastAppState.speed : fallbackSpeed;
        float at = (float)(Math.Max(elapsedMs, 0.0) * Mathf.Max(speed, 0f));
        animator.StartFromApp(playing, at, speed);
        return at;
    }

    /// <summary>-wmvAnimTime for a mounted character: the mount posed at the instant first, then the character, whose
    /// billboard bones take their world rotation under the bone the mount's pose has just placed. Either may be null.</summary>
    public static void PoseMountedAt(WmvRuntimeModel mount, WmvRuntimeModel rider, float timeMs)
    {
        WmvModelBuilder.PoseAt(mount, timeMs);
        WmvModelBuilder.PoseAt(rider, timeMs);
    }
}
