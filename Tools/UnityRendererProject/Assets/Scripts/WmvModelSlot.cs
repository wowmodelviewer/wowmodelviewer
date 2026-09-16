// WmvModelSlot.cs
//
// What the viewport player keeps about ONE model: its runtime objects, the parsed model and the .m2
// bytes it came from, its FileDataID, name and textures, the animation the app selected for it and what
// was read to play it (bone and material tracks per sequence, external .anim files and the fetches
// still out), the app's last playback state, and the display state the host reported for it (geosets,
// particle colour).
//
// WmvMain keeps one slot for the model on screen (currentSlot). The sequence code (WmvSlotAnimation) --
// selecting, switching, reading and binding a sequence, applying the app's playback state, prefetching
// and receiving .anim files -- and WmvMain's display-state code -- taking geosets and a particle colour
// out of a host message, pushing the particle colour onto the emitters -- take the slot they act on
// rather than reading fields of its own. A load fills the slot of the model it replaces: WmvMain.currentSlot says
// which parts describe which model while a load is in flight. WmvMountedScene keeps a second slot for
// the mount a character rides (WmvMountedScene.Mount), filled when that mount goes on screen.
//
// A plain holder: no Unity lifecycle and no IPC of its own. Whoever puts a runtime in a slot disposes it.

using System.Collections.Generic;
using UnityEngine;
using Wmv.Wow;

public class WmvModelSlot
{
    /// <summary>The model's runtime objects (mesh, materials, bones, animator, emitters), or null when
    /// nothing is built.</summary>
    public WmvRuntimeModel Runtime;

    /// <summary>
    /// The .m2 bytes of the model. Kept because only ONE animation's keyframes are
    /// parsed at a time -- a track on disk is an array of per-sequence arrays -- so following the
    /// app to a different animation means parsing these again with a different sequence in mind.
    /// A creature .m2 is a few hundred KB; re-fetching it over IPC for every dropdown change
    /// would cost a round-trip to save that.
    /// </summary>
    public byte[] M2Bytes;

    /// <summary>
    /// The sequence the app says it is showing, or -1 when it has not said. Remembered because the
    /// push can arrive before the model finishes loading -- it is sent right after loadWoWModel --
    /// and then it decides which animation the model is built playing, rather than the model
    /// starting on its own idle and being corrected a frame later.
    /// </summary>
    public int SelectedSequence = -1;

    // Kept after a successful load so the SKIN can change without reloading anything. A creature
    // normally has several skins -- chicken2 has seven -- and WMV lets the user pick among them;
    // when they do, only the textures behind the existing materials need re-uploading.
    public M2ParsedModel Model;
    public string Name = "WoWModel";
    public int FileDataID;
    public readonly Dictionary<int, BlpImage> Textures = new Dictionary<int, BlpImage>();
    public readonly Dictionary<int, int> TextureIds = new Dictionary<int, int>();   // slot -> FileDataID

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
    public readonly Dictionary<int, M2BoneDef[]> BoneTrackCache = new Dictionary<int, M2BoneDef[]>();
    public struct MaterialTrackSet { public M2ColorDef[] Colors; public M2TextureTransform[] Transforms; public M2MaterialTrackSurvey Survey; }
    public readonly Dictionary<int, MaterialTrackSet> MaterialTrackCache = new Dictionary<int, MaterialTrackSet>();

    /// <summary>
    /// External .anim files already fetched, keyed by FileDataID. A sequence whose keyframes are
    /// not in the .m2 needs its .anim bytes; fetching them again on every visit would put a
    /// network round trip in front of an animation the renderer already has.
    /// </summary>
    public readonly Dictionary<int, byte[]> AnimFileCache = new Dictionary<int, byte[]>();

    /// <summary>The .anim fetch in flight, if any: requestId -> the sequence waiting on it.</summary>
    public readonly Dictionary<string, int> PendingAnimFetch = new Dictionary<string, int>();

    /// <summary>Fetches this slot stopped waiting for because it was given another model while they were out
    /// (AbandonAnimFetches). Their answers still arrive: kept here so they are claimed and dropped rather than read
    /// as an answer to whatever else is in flight.</summary>
    public readonly HashSet<string> AbandonedAnimFetch = new HashSet<string>();

    /// <summary>Stop waiting for the .anim fetches out for this slot -- the model it holds is being replaced, so their
    /// sequence numbers mean nothing now -- without forgetting the requests themselves.</summary>
    public void AbandonAnimFetches()
    {
        foreach (var id in PendingAnimFetch.Keys)
            AbandonedAnimFetch.Add(id);
        PendingAnimFetch.Clear();
    }

    /// <summary>
    /// The last playback state the app sent, whether or not it could be applied when it arrived.
    ///
    /// A state message names the sequence it is about, and it used to be dropped whenever the
    /// renderer was not already on that sequence. That is exactly the moment it matters most: a
    /// selection that fell back to the idle, or one still waiting on its .anim file, would lose
    /// the app's play/pause, speed and position entirely and keep whatever the previous animation
    /// happened to be doing. Kept here, it can be applied to whatever ends up playing.
    /// </summary>
    public WmvIpcClient.AnimationState LastAppState;
    public bool HaveAppState;

    /// <summary>
    /// The geoset numbers the displayed creature variant switches on, or null when the host has
    /// not reported any. Two variants of the same creature can differ by GEOMETRY rather than
    /// texture -- one horse's mane instead of another -- and this is what decides which submeshes
    /// are drawn. See WmvModelBuilder.GeosetVisible.
    /// </summary>
    public HashSet<int> Geosets;

    /// <summary>
    /// The item ParticleColor override the host last reported, as three RGB stops, or null when
    /// the displayed item names none. Kept alongside Geosets because it arrives on the
    /// same two messages and describes the same displayed state.
    /// </summary>
    public Color[][] ParticleColor;
}
