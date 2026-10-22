// WmvTerrainSessionWorldLight.cs
//
// THE OUTDOOR LIGHT OF THE SCENE (protocol 20, docs/world-viewer/10-light-sky-fog.md 6.3-6.5): the part of WmvTerrainSession
// that keeps a load's "worldLight" (Wow/World/WorldLight.cs, parsed on the reader thread) as the scene's, beside Stage 09's
// zone lights of the water (WmvTerrainSessionLiquids.cs, unchanged):
//   the commit    a single load's or a world's commit makes its object the scene's -- none when the load carried none (an
//                 older host), when its state is not "ok" (said, with why), or when it is another map's (ignored, and said).
//                 A streaming session's tile loads change nothing: the scene keeps its entry's, as the host gives every tile
//                 load the entry's object.
//   the drop      the scene's object goes with the scene (DisposeTiles), and the outdoor light's frame state is reset at once
//                 (WmvWorldLight.Off; the sky's mesh and material go with it).
//   switched on   the terrain's normal stream is uploaded only while the light is on (WmvTerrainBuilder), so a setWorldLight
//                 that switches it on draws the resident tiles built without it again (WorldLightOn): the tile on screen at
//                 once (RebuildShown's way), a streaming session's tiles one a frame, nearest first (RebuildNextStreamTile). A
//                 tile that has the stream keeps it until it is built again or released, also after the light goes off.
// With WMV_LIGHT=off the object is still kept (the host sends it to every protocol-20 player) but nothing is said of it: the
// log stays Stage 09's.
// The player reads it at the camera once a rendered frame (WmvMain.WorldLightFrame).

using Wmv.Wow.World;

public sealed partial class WmvTerrainSession
{
    WorldLightSet sceneWorldLight;

    /// <summary>The scene's outdoor light (its load's "worldLight", when "ok" and of the load's map), null when it has none.</summary>
    public WorldLightSet SceneWorldLight { get { return sceneWorldLight; } }

    /// <summary>The commit: the load's outdoor light becomes the scene's (see the file header).</summary>
    void CommitSceneWorldLight(Job j)
    {
        WorldLightSet l = j.Cmd.WorldLight;
        sceneWorldLight = null;
        if (l == null)
            return;
        bool quiet = WmvWorldLight.HardOff;
        if (l.State != "ok")
        {
            if (!quiet)
                Log("load {0}: outdoor light {1}{2}: none", j.Cmd.Load, l.State, l.Detail.Length > 0 ? " (" + l.Detail + ")" : "");
            return;
        }
        if (l.Map != j.Cmd.MapId)
        {
            if (!quiet)
                Log("load {0}: the outdoor light is map {1}'s, not the load's map {2}: ignored", j.Cmd.Load, l.Map, j.Cmd.MapId);
            return;
        }
        sceneWorldLight = l;
        if (!quiet)
            Log("load {0}: outdoor light {1}", j.Cmd.Load, l.Summary());
    }

    /// <summary>DisposeTiles: the scene's outdoor light goes with it.</summary>
    void DropSceneWorldLight()
    {
        sceneWorldLight = null;
        WmvWorldLight.Off();
    }

    /// <summary>setWorldLight switched the outdoor light on (WmvMain.HandleSetWorldLight): every resident tile built while it
    /// was off (WmvRuntimeTerrainTile.LightNormalsOmitted) is drawn again with the normal stream -- the tile on screen now
    /// (RebuildShown), a streaming session's tiles one a frame, nearest first (RebuildNextStreamTile, from their kept meshes and
    /// content). Nothing with WMV_LIGHT=off or -wmvTerrainLight=off. Returns the tiles drawn again or queued.</summary>
    public int WorldLightOn()
    {
        if (disposed || !WmvWorldLight.TerrainNormalsWanted)
            return 0;
        if (stream == null)
        {
            if (shown == null || shown.Disposed || !shown.LightNormalsOmitted)
                return 0;
            string reason;
            double ms;
            int load;
            return RebuildShown("setWorldLight on: the outdoor light's normal stream", out reason, out ms, out load) == "applied" ? 1 : 0;
        }
        int n = 0;
        foreach (StreamTile st in stream.Tiles.Values)
            if (st.Tile != null && !st.Tile.Disposed && st.Tile.LightNormalsOmitted && !st.RebuildDue)
            {
                st.RebuildDue = true;
                n++;
            }
        if (n > 0)
            Log("load {0}: the outdoor light switched on: {1} resident tile(s) built while it was off are drawn again with its normal stream, " +
                "one a frame", stream.EntryLoad, n);
        return n;
    }
}
