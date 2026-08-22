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
    uint[] globalSequences = new uint[0];
    float lengthMs = 1f;
    double timeMs;

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
        GlobalTimeMs += dt;
        timeMs += dt;
        if (timeMs >= lengthMs)
            timeMs -= Math.Floor(timeMs / lengthMs) * lengthMs;
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
