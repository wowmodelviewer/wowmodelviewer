// WmvMain.cs
//
// Bootstrap for the WMV Unity viewport player -- WMV's new renderer foundation and intended
// primary viewport (the OpenGL canvas is the legacy/fallback viewport during the migration;
// see docs/unity-renderer/README.md). Add this component to one empty GameObject in an
// otherwise-empty scene; at runtime it builds everything else: camera rig + orbit controls,
// a directional light, the test cube, the on-screen status text, and the IPC client that
// connects back to WMV.
//
// V1 scope: runtime asset access. On loadWoWModel the player requests the model's raw
// bytes from WMV (getAsset / getAssetByFileDataID), verifies the assetResponse (byte
// length + SHA-1) and reports it on screen. No M2 parsing and no mesh rendering yet --
// that is the next step, built on top of these bytes.

using UnityEngine;

public class WmvMain : MonoBehaviour
{
    WmvIpcClient ipc;
    GameObject testCube;
    WmvStatusOverlay status;

    string pendingRequestId;
    string pendingWhat;

    void Awake()
    {
        // The player is embedded in a host app whose window normally has focus;
        // without this the player would pause immediately. (Also set Run In
        // Background in Player Settings -- this is a belt-and-braces override.)
        Application.runInBackground = true;

        // Camera rig
        var cam = Camera.main;
        if (cam == null)
        {
            var camGo = new GameObject("Main Camera");
            cam = camGo.AddComponent<Camera>();
            camGo.tag = "MainCamera";
        }
        cam.transform.position = new Vector3(0f, 1.2f, -4f);
        cam.transform.LookAt(Vector3.zero);
        cam.clearFlags = CameraClearFlags.SolidColor;
        cam.backgroundColor = new Color(0.10f, 0.10f, 0.12f);
        cam.gameObject.AddComponent<WmvOrbitCamera>();

        // Light
        var lightGo = new GameObject("Directional Light");
        var light = lightGo.AddComponent<Light>();
        light.type = LightType.Directional;
        light.intensity = 1.1f;
        lightGo.transform.rotation = Quaternion.Euler(50f, -30f, 0f);

        // Proof-of-life: a visible spinning cube until real WoW models arrive.
        testCube = GameObject.CreatePrimitive(PrimitiveType.Cube);
        testCube.name = "WMV Test Cube";
        testCube.AddComponent<WmvSpin>();

        // Status text in the viewport
        status = gameObject.AddComponent<WmvStatusOverlay>();
        status.Set("Starting ...");

        // IPC client (connects back to the WMV server given by -wmvPort)
        ipc = gameObject.AddComponent<WmvIpcClient>();
        ipc.OnStatus = s => status.Set(s);
        ipc.OnLoadWoWModel = HandleLoadWoWModel;
        ipc.OnAssetResponse = HandleAssetResponse;
    }

    // WMV tells us which model is active. V1: fetch its raw bytes and report; V2+: parse
    // the M2 and its skin/textures (also fetched through getAsset) and build the scene.
    void HandleLoadWoWModel(string path, int fileDataID, string client)
    {
        status.Set("Active client received (" + client + ")");
        if (!string.IsNullOrEmpty(path))
        {
            pendingWhat = path;
            pendingRequestId = ipc.RequestAsset(path);
        }
        else if (fileDataID > 0)
        {
            pendingWhat = "fileDataID " + fileDataID;
            pendingRequestId = ipc.RequestAssetByFileDataID(fileDataID);
        }
        else
        {
            status.Set("loadWoWModel without path or fileDataID -- ignored");
            return;
        }
        status.Set("Requested " + pendingWhat + " ...");
        Debug.Log("WMV IPC: requested " + pendingWhat + " (requestId " + pendingRequestId + ")");
    }

    void HandleAssetResponse(WmvIpcClient.AssetResponse r)
    {
        var what = !string.IsNullOrEmpty(r.path) ? r.path : ("fileDataID " + r.fileDataID);
        if (!r.ok)
        {
            status.Set("Error for " + what + ": " + r.error);
            Debug.LogWarning("WMV IPC: assetResponse error for " + what + ": " + r.error);
            return;
        }
        var verdict = r.hashMatches ? "sha1 OK" : "sha1 MISMATCH (reported " + r.sha1 + ", local " + r.localSha1 + ")";
        status.Set("Received " + r.byteLength + " bytes for " + what + " (" + verdict + ")");
        Debug.Log("WMV IPC: received " + what + ": byteLength=" + r.byteLength +
                  " decoded=" + (r.data != null ? r.data.Length : 0) + " sha1=" + r.sha1 + " " + verdict);
        // Next step (not V1): parse r.data as M2 and request its skins/textures the same way.
    }
}

// Slow idle spin so the test scene is visibly "live".
public class WmvSpin : MonoBehaviour
{
    void Update()
    {
        transform.Rotate(0f, 40f * Time.deltaTime, 0f);
    }
}

// Top-left status lines (last few messages) drawn with the immediate-mode GUI --
// no scene/canvas setup needed.
public class WmvStatusOverlay : MonoBehaviour
{
    const int MaxLines = 6;
    readonly System.Collections.Generic.List<string> lines = new System.Collections.Generic.List<string>();
    GUIStyle style;

    public void Set(string line)
    {
        lines.Add(line);
        while (lines.Count > MaxLines) lines.RemoveAt(0);
    }

    void OnGUI()
    {
        if (style == null)
        {
            style = new GUIStyle(GUI.skin.label) { fontSize = 14, richText = false };
            style.normal.textColor = new Color(0.92f, 0.92f, 0.95f);
        }
        GUI.Label(new Rect(10, 8, Screen.width - 20, 22), "WMV Unity Renderer  (V1: runtime asset access)", style);
        for (int i = 0; i < lines.Count; i++)
            GUI.Label(new Rect(10, 30 + i * 20, Screen.width - 20, 22), lines[i], style);
    }
}
