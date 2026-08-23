// WmvM2Animator.cs
//
// Plays one M2 animation -- the model's default idle -- by writing bone transforms every frame.
//
// WHY THERE IS NO ANIMATOR CONTROLLER HERE. Unity's animation system wants clips authored at
// build time; this renderer receives a model over IPC at runtime and has to be moving a second
// later. An AnimationClip would have to be built per model, per sequence, from curves this code
// would have to fill in anyway -- and clips are exactly the kind of asset a player build strips.
// Writing localPosition/localRotation/localScale straight onto the bone transforms is both less
// machinery and a closer match to what the legacy viewport does.
//
// WHAT IT REPRODUCES. The legacy viewport composes a bone as
//
//     local = T(pivot) * T(translation(t)) * R(rotation(t)) * S(scale(t)) * T(-pivot)
//     world = parent.world * local
//
// (Bone::calcMatrix), evaluating each track at a time in milliseconds inside the playing
// sequence. The skeleton this renderer already builds puts every bone at its pivot with the rest
// transform localPosition = pivot - parentPivot, and Unity composes a bone as
// parent.world * T(localPosition) * R(localRotation) * S(localScale). Adding the translation
// track to that rest offset and setting the rotation and scale from their tracks reproduces the
// expression above term for term: the pivot translations telescope through the parent chain, and
// the bind poses are unchanged. A bone with no track for this sequence simply keeps its rest
// transform, which is what the viewport's "no tracks, matrix stays identity" case amounts to.
//
// GLOBAL SEQUENCES. A track can be bound to a global sequence instead of the animation: it then
// loops over that sequence's own duration on a clock that does not care what is playing. The
// legacy evaluator reads entry 0 of such a track's keys and takes the time as
// globalTime % duration; both are reproduced here.

using System;
using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow;

/// <summary>
/// Drives one model's bones from its M2 tracks. Added by WmvModelBuilder to the model root, so it
/// dies with the model.
/// </summary>
public class WmvM2Animator : MonoBehaviour
{
    /// <summary>
    /// The clock global-sequence tracks run on, in milliseconds. Shared by every model, which is
    /// the point of a global sequence: it is global.
    /// </summary>
    public static double GlobalTimeMs;

    /// <summary>One track, with its keys already in Unity space.</summary>
    struct Track<T>
    {
        public M2Interpolation Interpolation;
        public int GlobalSequence;
        public uint[] Times;
        public T[] Values;
        public bool HasData { get { return Values != null && Values.Length > 0; } }
    }

    struct AnimatedBone
    {
        public Transform Transform;
        public Vector3 RestLocalPosition;
        public Track<Vector3> Translation;
        public Track<Quaternion> Rotation;
        public Track<Vector3> Scale;
    }

    AnimatedBone[] bones = new AnimatedBone[0];

    /// <summary>
    /// Converted tracks, keyed by sequence index.
    ///
    /// Setup turns the parsed WoW tracks into the renderer's own space -- every key of every
    /// moving bone, three tracks each -- and that conversion allocates. Doing it again each time
    /// the user returns to an animation they have already watched is the last per-switch
    /// allocation left after the parse and the fetch were dealt with, so it is kept too. The
    /// entries hold this model's bone transforms, so the cache is dropped whenever the animator is
    /// pointed at a different skeleton.
    /// </summary>
    readonly Dictionary<int, AnimatedBone[]> convertedBySequence = new Dictionary<int, AnimatedBone[]>();
    Transform[] cachedFor;              // the skeleton the cache above belongs to
    readonly Dictionary<int, int> globalCountBySequence = new Dictionary<int, int>();
    uint[] globalSequences = new uint[0];
    float lengthMs = 1f;
    double timeMs;

    /// <summary>
    /// Playback state, mirrored from the app. It is the app's clock that is authoritative: this
    /// renderer runs its own only so the motion is smooth between the messages that carry it.
    /// </summary>
    bool playing = true;
    float speed = 1f;

    /// <summary>
    /// How far this renderer's clock may sit from the app's before it is snapped.
    ///
    /// Two renderers timing themselves independently drift, and the app sends its position on a
    /// slow heartbeat to correct that. Snapping on every message would trade the drift for a
    /// visible stutter once a second, so a small difference is left alone -- at 30 frames a second
    /// one frame is 33 ms, and a difference under that cannot be seen.
    /// </summary>
    const float TimeSnapToleranceMs = 40f;

    /// <summary>
    /// After a sequence change, how long until the animation actually MOVES again?
    ///
    /// This is the measurement the switching work needed and did not have. Timing the switch
    /// itself only ever showed the parse and the fetch; what a viewer notices is whether the model
    /// starts moving on the next frame or stands there. Those are different questions, and for a
    /// while the answer to the second was "up to a second" while the first said "one millisecond".
    ///
    /// Reports the frames AND the milliseconds, so a hold is distinguishable from a stall: if
    /// frames keep counting while the time stands still, the renderer is running fine and the
    /// animation is being held -- which is what a missing playback-state push looks like.
    /// Diagnostics only, under -wmvAnimCheck.
    /// </summary>
    int advanceWatchFrame = -1;
    float advanceWatchStart;
    int advanceWatchFrames;

    public void BeginAdvanceWatch()
    {
        if (!WmvModelBuilder.Debug_.AnimCheck)
            return;
        advanceWatchFrame = Time.frameCount;
        advanceWatchStart = Time.realtimeSinceStartup;
        advanceWatchFrames = 0;
    }

    void AdvanceWatchTick(double before, double dt)
    {
        if (advanceWatchFrame < 0)
            return;
        advanceWatchFrames++;
        if (timeMs != before)
        {
            Debug.Log(string.Format(
                "WMV: anim: moving again {0} frame(s) / {1:F0} ms after the switch "
                + "(playing={2} speed={3:F2}, frame time {4:F1} ms)",
                Time.frameCount - advanceWatchFrame,
                (Time.realtimeSinceStartup - advanceWatchStart) * 1000.0, playing, speed, dt));
            advanceWatchFrame = -1;
            return;
        }
        // A model the app has PAUSED is supposed to stand still; that is the feature, not a fault.
        // Stop watching rather than reporting it.
        if (!playing || speed <= 0f)
        {
            advanceWatchFrame = -1;
            return;
        }
        // Rendering, told to play, and still not moving. Said once, loudly: it means the app never
        // told this renderer to resume, and it will sit here until something does.
        if (advanceWatchFrames == 30)
            Debug.LogWarning(string.Format(
                "WMV: anim: still not moving {0} frames / {1:F0} ms after the switch -- frames ARE "
                + "running, so the animation is being HELD (playing={2} speed={3:F2})",
                advanceWatchFrames, (Time.realtimeSinceStartup - advanceWatchStart) * 1000.0,
                playing, speed));
    }

    /// <summary>How many corrections were needed, out of how many state messages. Reported with
    /// each correction, because that ratio is what says whether the two clocks keep step.</summary>
    public int SnapCount { get; private set; }
    public int StateUpdates { get; private set; }

    public bool IsPlaying { get { return playing; } }
    public float Speed { get { return speed; } }

    /// <summary>Sequence index and AnimId, for the log and for nothing else.</summary>
    public int SequenceIndex { get; private set; }
    public int AnimId { get; private set; }
    public int AnimatedBoneCount { get { return bones.Length; } }
    public float LengthMs { get { return lengthMs; } }
    public int GlobalSequenceTrackCount { get; private set; }

    /// <summary>
    /// Bind the animator to a model's bones.
    ///
    /// Only bones that actually move in this sequence are kept: on a 236-bone boss the idle moves
    /// a third of them, and the rest would be identical writes every frame. Track values are
    /// converted to Unity space HERE rather than per frame -- the axis map is a linear isometry,
    /// so interpolating converted keys gives the same result as converting an interpolated value,
    /// and doing it once per key instead of once per frame is free.
    /// </summary>
    public void Setup(M2ParsedModel model, Transform[] boneTransforms, Vector3[] restLocalPositions,
                      Action<string> log)
    {
        SequenceIndex = model.AnimatedSequence;
        globalSequences = model.GlobalSequences;
        M2Sequence seq = model.Sequences[SequenceIndex];
        AnimId = seq.AnimId;
        lengthMs = seq.Length > 0 ? seq.Length : 1f;

        // A different skeleton invalidates everything converted against the previous one.
        if (!ReferenceEquals(cachedFor, boneTransforms))
        {
            convertedBySequence.Clear();
            cachedFor = boneTransforms;
        }

        AnimatedBone[] already;
        if (convertedBySequence.TryGetValue(SequenceIndex, out already))
        {
            bones = already;
            GlobalSequenceTrackCount = globalCountBySequence.ContainsKey(SequenceIndex)
                ? globalCountBySequence[SequenceIndex] : 0;
            timeMs = 0.0;
            if (log != null)
                log(string.Format("anim: sequence [{0}] animId {1}{2}, {3} ms, {4} bone(s) move "
                                  + "(tracks already converted)",
                                  SequenceIndex, AnimId, AnimId == 0 ? " (Stand)" : "",
                                  lengthMs, bones.Length));
            return;
        }

        var kept = new List<AnimatedBone>();
        int globalTracks = 0;
        var unsupported = new Dictionary<M2Interpolation, int>();

        int n = Math.Min(model.Bones.Length, boneTransforms.Length);
        for (int i = 0; i < n; i++)
        {
            M2BoneDef def = model.Bones[i];
            if (!def.IsAnimated)
                continue;

            AnimatedBone b;
            b.Transform = boneTransforms[i];
            b.RestLocalPosition = restLocalPositions[i];
            b.Translation = ConvertVectorTrack(def.Translation, false);
            b.Rotation = ConvertRotationTrack(def.Rotation);
            b.Scale = ConvertVectorTrack(def.Scale, true);
            kept.Add(b);

            if (def.Translation.IsGlobal) globalTracks++;
            if (def.Rotation.IsGlobal) globalTracks++;
            if (def.Scale.IsGlobal) globalTracks++;
            CountUnsupported(unsupported, def.Translation.Interpolation, def.Translation.HasData);
            CountUnsupported(unsupported, def.Rotation.Interpolation, def.Rotation.HasData);
            CountUnsupported(unsupported, def.Scale.Interpolation, def.Scale.HasData);
        }

        bones = kept.ToArray();
        GlobalSequenceTrackCount = globalTracks;
        convertedBySequence[SequenceIndex] = bones;
        globalCountBySequence[SequenceIndex] = globalTracks;

        if (log != null)
        {
            log(string.Format("anim: sequence [{0}] animId {1}{2}, {3} ms, {4} of {5} bone(s) " +
                              "move, {6} track(s) on a global sequence",
                              SequenceIndex, AnimId, AnimId == 0 ? " (Stand)" : "",
                              lengthMs, bones.Length, model.Bones.Length, globalTracks));
            foreach (var kv in unsupported)
                log(string.Format("anim: {0} track(s) use {1} interpolation, which is read as " +
                                  "linear in this milestone (its tangents are not)", kv.Value, kv.Key));
        }
        // Start at the beginning of the loop rather than wherever a previous model left off.
        timeMs = 0.0;
    }

    static void CountUnsupported(Dictionary<M2Interpolation, int> counts, M2Interpolation kind, bool hasData)
    {
        if (!hasData || kind == M2Interpolation.None || kind == M2Interpolation.Linear)
            return;
        int v;
        counts.TryGetValue(kind, out v);
        counts[kind] = v + 1;
    }

    void LateUpdate()
    {
        double dt = Time.deltaTime * 1000.0;

        // GLOBAL SEQUENCES KEEP RUNNING WHILE THE ANIMATION IS PAUSED, and ignore the speed. That
        // is not an oversight copied by accident: the legacy viewport advances its global clock
        // before it decides whether the animation is paused, and its speed multiplier lives inside
        // the animation tick alone (ModelCanvas::tick / AnimManager::Tick). A torch keeps
        // flickering while the creature is held still.
        GlobalTimeMs += dt;

        double beforeTimeMs = timeMs;

        if (playing)
        {
            timeMs += dt * speed;
            if (timeMs >= lengthMs)
                timeMs -= Math.Floor(timeMs / lengthMs) * lengthMs;
            else if (timeMs < 0.0)
                timeMs += Math.Ceiling(-timeMs / lengthMs) * lengthMs;
        }
        ApplyPose((float)timeMs);
        AdvanceWatchTick(beforeTimeMs, dt);
    }

    /// <summary>
    /// Mirror the app's playback state.
    ///
    /// Play/pause and speed are taken as given -- they are state, not measurements. The TIME is
    /// treated as a correction rather than an assignment: it is only applied when this renderer's
    /// own clock has drifted further than a frame away from it, because the app sends its position
    /// on a heartbeat and snapping to every one would replace smooth motion with a periodic jump.
    /// An explicit change (a scrub, a stop, a new selection) arrives with the app's time already
    /// far from this one, so it snaps naturally without needing to be flagged as special.
    /// </summary>
    /// <summary>
    /// Apply play/pause and speed WITHOUT touching the clock.
    ///
    /// Those two are the app's state, not the sequence's: they stay true while a switch is still
    /// resolving, or has landed somewhere other than what was asked for. The position is the one
    /// thing that would be wrong to carry across in those cases, so it is left alone.
    /// </summary>
    public void SetTransportOnly(bool isPlaying, float playbackSpeed)
    {
        playing = isPlaying;
        speed = playbackSpeed > 0f ? playbackSpeed : 0f;
        if (!playing)
            ApplyPose((float)timeMs);
    }

    public void SetPlaybackState(bool isPlaying, float timeFromApp, float playbackSpeed)
    {
        StateUpdates++;
        if (WmvModelBuilder.Debug_.AnimCheck)
            Debug.Log(string.Format("WMV: anim state #{0}: playing={1} timeMs={2:F0} speed={3:F2} " +
                                    "(mine {4:F0})", StateUpdates, isPlaying, timeFromApp,
                                    playbackSpeed, timeMs));
        playing = isPlaying;
        speed = playbackSpeed > 0f ? playbackSpeed : 0f;

        if (lengthMs > 0f)
        {
            float mine = (float)timeMs;
            float theirs = timeFromApp % lengthMs;
            if (theirs < 0f) theirs += lengthMs;
            // Measure the SHORT way round the loop: 10 ms and (length - 10) ms are 20 ms apart,
            // not a whole sequence apart, and a plain subtraction would snap on every wrap.
            float diff = Math.Abs(mine - theirs);
            if (diff > lengthMs * 0.5f)
                diff = lengthMs - diff;
            if (diff > TimeSnapToleranceMs)
            {
                timeMs = theirs;
                SnapCount++;
                // Logged every time, because it is rare by construction: an explicit change (a
                // scrub, a stop) or genuine clock drift. A run that fills the log with these is
                // telling you the two clocks are not keeping step, which is worth knowing.
                Debug.Log(string.Format(
                    "WMV: anim: clock corrected by {0:F0} ms -- was {1:F0}, app says {2:F0} " +
                    "(correction {3} of {4} state update(s))",
                    diff, mine, theirs, SnapCount, StateUpdates));
            }
        }

        // A paused model still has to show the frame it is paused on -- LateUpdate will not pose it
        // while playing is false, and the pose it is holding may be the one before the scrub.
        if (!playing)
            ApplyPose((float)timeMs);
    }

    /// <summary>
    /// Write the pose at one instant onto the bones. Public because the -wmvAnimCheck diagnostic
    /// drives it directly: sampling the sequence is the only way to answer "does this move, and by
    /// how much" without a person watching the viewport.
    /// </summary>
    public void ApplyPose(float t)
    {
        for (int i = 0; i < bones.Length; i++)
        {
            AnimatedBone b = bones[i];
            if (b.Transform == null)
                continue;

            if (b.Translation.HasData)
                b.Transform.localPosition = b.RestLocalPosition +
                                            EvalVector(b.Translation, TrackTime(b.Translation.GlobalSequence, t));
            if (b.Rotation.HasData)
                b.Transform.localRotation = EvalRotation(b.Rotation, TrackTime(b.Rotation.GlobalSequence, t));
            if (b.Scale.HasData)
                b.Transform.localScale = EvalVector(b.Scale, TrackTime(b.Scale.GlobalSequence, t));
        }
    }

    /// <summary>Put every animated bone back exactly where the bind pose expects it.</summary>
    public void RestorePose()
    {
        for (int i = 0; i < bones.Length; i++)
        {
            if (bones[i].Transform == null)
                continue;
            bones[i].Transform.localPosition = bones[i].RestLocalPosition;
            bones[i].Transform.localRotation = Quaternion.identity;
            bones[i].Transform.localScale = Vector3.one;
        }
    }

    /// <summary>
    /// The time to evaluate a track at: the animation's own, or the global sequence's clock.
    ///
    /// A global sequence of zero length is a real thing in shipped data. The legacy evaluator
    /// returns a default-constructed value for it, which is right for a translation (no movement)
    /// and wrong for a scale (it collapses the bone to a point). Holding the first keyframe is the
    /// reading that cannot make a model vanish.
    /// </summary>
    float TrackTime(int globalSequence, float animationTime)
    {
        if (globalSequence < 0 || globalSequence >= globalSequences.Length)
            return animationTime;
        uint duration = globalSequences[globalSequence];
        if (duration == 0)
            return 0f;
        return (float)(GlobalTimeMs % duration);
    }

    /// <summary>
    /// Find the keyframe span containing t and how far into it we are. Mirrors the legacy
    /// evaluator: before the first key or past the last one, the nearest key is held rather than
    /// wrapped -- a track need not span the whole sequence.
    /// </summary>
    static bool Span(uint[] times, float t, out int index, out float r)
    {
        index = 0;
        r = 0f;
        int n = times.Length;
        if (n < 2)
            return false;
        if (t >= times[n - 1])
        {
            index = n - 1;
            return false;
        }
        for (int i = 0; i < n - 1; i++)
        {
            if (t >= times[i] && t < times[i + 1])
            {
                index = i;
                float span = times[i + 1] - times[i];
                r = span > 0f ? (t - times[i]) / span : 0f;
                return true;
            }
        }
        return false;
    }

    static Vector3 EvalVector(Track<Vector3> track, float t)
    {
        int i;
        float r;
        if (!Span(track.Times, t, out i, out r))
            return track.Values[Mathf.Min(i, track.Values.Length - 1)];
        if (track.Interpolation == M2Interpolation.None)
            return track.Values[i];
        return Vector3.Lerp(track.Values[i], track.Values[i + 1], r);
    }

    static Quaternion EvalRotation(Track<Quaternion> track, float t)
    {
        int i;
        float r;
        if (!Span(track.Times, t, out i, out r))
            return track.Values[Mathf.Min(i, track.Values.Length - 1)];
        if (track.Interpolation == M2Interpolation.None)
            return track.Values[i];
        // Slerp, as the legacy evaluator does for quaternions -- a component-wise lerp would
        // shorten the quaternion and pinch the joint at the middle of every span.
        return Quaternion.Slerp(track.Values[i], track.Values[i + 1], r);
    }

    static Track<Vector3> ConvertVectorTrack(M2Track<WowVec3> src, bool isScale)
    {
        Track<Vector3> dst = new Track<Vector3>();
        dst.Interpolation = src.Interpolation;
        dst.GlobalSequence = src.GlobalSequence;
        dst.Times = src.Times;
        if (!src.HasData)
            return dst;
        var values = new Vector3[src.Values.Length];
        for (int i = 0; i < values.Length; i++)
        {
            float x, y, z;
            if (isScale)
                WowCoordinateConverter.ConvertScale(src.Values[i], out x, out y, out z);
            else
                WowCoordinateConverter.ConvertPosition(src.Values[i], out x, out y, out z);
            values[i] = new Vector3(x, y, z);
        }
        dst.Values = values;
        return dst;
    }

    static Track<Quaternion> ConvertRotationTrack(M2Track<WowQuat> src)
    {
        Track<Quaternion> dst = new Track<Quaternion>();
        dst.Interpolation = src.Interpolation;
        dst.GlobalSequence = src.GlobalSequence;
        dst.Times = src.Times;
        if (!src.HasData)
            return dst;
        var values = new Quaternion[src.Values.Length];
        for (int i = 0; i < values.Length; i++)
        {
            float x, y, z, w;
            WowCoordinateConverter.ConvertRotation(src.Values[i], out x, out y, out z, out w);
            values[i] = new Quaternion(x, y, z, w);
        }
        dst.Values = values;
        return dst;
    }
}
