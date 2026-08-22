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

        // Proof-of-life until a real model arrives. Its default material comes from the
        // built-in resources, which a player build may have stripped (that is what makes an
        // untouched primitive render magenta), so give it the same shader the model builder
        // resolved rather than leaving a confusing magenta cube on screen.
        placeholder = GameObject.CreatePrimitive(PrimitiveType.Cube);
        placeholder.name = "WMV Placeholder";
        placeholder.AddComponent<WmvSpin>();
        var placeholderShader = WmvModelBuilder.ResolveShader(s => Debug.Log("WMV: " + s));
        if (placeholderShader != null)
            placeholder.GetComponent<MeshRenderer>().material = new Material(placeholderShader);

        status = gameObject.AddComponent<WmvStatusOverlay>();
        status.Set("Starting ...");

        ipc = gameObject.AddComponent<WmvIpcClient>();
        ipc.OnStatus = s => status.Set(s);
        ipc.OnLoadWoWModel = HandleLoadWoWModel;
        ipc.OnAssetResponse = HandleAssetResponse;
        ipc.OnModelTextures = HandleModelTextures;
        ipc.OnModelSkin = HandleModelSkin;
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
            job.Model = M2Parser.Parse(r.data);
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
            currentName = string.IsNullOrEmpty(job.Model.Name) ? "WoWModel" : job.Model.Name;
            currentFileDataID = job.FileDataID;
            currentTextures.Clear();
            foreach (var kv in job.Textures) currentTextures[kv.Key] = kv.Value;
            skinJob = null;

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
