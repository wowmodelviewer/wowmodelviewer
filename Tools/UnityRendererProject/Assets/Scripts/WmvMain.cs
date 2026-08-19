// WmvMain.cs
//
// Bootstrap for the WMV Unity viewport player -- WMV's new renderer foundation and intended
// primary viewport (the OpenGL canvas is the legacy/fallback viewport during the migration;
// see docs/unity-renderer/README.md). Add this component to one empty GameObject in an
// otherwise-empty scene; at runtime it builds everything else: camera rig + orbit controls,
// a directional light, the V0 test cube, and the IPC server WMV talks to.
//
// V0 scope: embedding proof (test cube) + IPC skeleton. loadWoWModel is acknowledged with
// "not implemented yet"; the V1 loaders will build the scene from raw WoW data requested from
// WMV over IPC (WmvIpcServer.RequestAsset / RequestAssetByFileDataID).

using UnityEngine;

public class WmvMain : MonoBehaviour
{
    WmvIpcServer ipc;
    GameObject testCube;

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

        // V0 proof-of-life: a visible spinning cube until real WoW models arrive (V1).
        testCube = GameObject.CreatePrimitive(PrimitiveType.Cube);
        testCube.name = "WMV Test Cube";
        testCube.AddComponent<WmvSpin>();

        // IPC
        ipc = gameObject.AddComponent<WmvIpcServer>();
        ipc.OnClearScene = HandleClearScene;
        ipc.OnLoadWoWModel = HandleLoadWoWModel;
        ipc.OnSetCamera = HandleSetCamera;
    }

    // V1: request the model's M2 / skin / textures from WMV (ipc.RequestAsset /
    // RequestAssetByFileDataID) and build the scene from that data. V0 only acknowledges.
    void HandleLoadWoWModel(string sourcePath, int fileDataID)
    {
        var what = string.IsNullOrEmpty(sourcePath) ? ("fileDataID " + fileDataID) : sourcePath;
        Debug.Log("loadWoWModel " + what + ": not implemented yet");
        ipc.SendError("loadWoWModel not implemented yet (" + what + ")");
    }

    void HandleClearScene()
    {
        // Nothing to clear in V0 beyond making sure the test scene is visible.
        if (testCube != null) testCube.SetActive(true);
    }

    void HandleSetCamera(Vector3 position, Vector3 rotationEuler)
    {
        var cam = Camera.main;
        if (cam == null) return;
        cam.transform.position = position;
        cam.transform.rotation = Quaternion.Euler(rotationEuler);
    }
}

// Slow idle spin so the V0 test scene is visibly "live".
public class WmvSpin : MonoBehaviour
{
    void Update()
    {
        transform.Rotate(0f, 40f * Time.deltaTime, 0f);
    }
}
