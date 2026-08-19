// WmvIpcServer.cs
//
// IPC endpoint of the WMV Unity viewport player: a localhost TCP server speaking
// newline-delimited JSON (one object per line). Default port 9500, overridable with
// "-wmvPort <n>" on the player command line.
//
// The player is WMV's new renderer foundation and renders directly from WoW data: it asks
// WMV -- which owns the app UI, the active client/profile, CASC/MPQ access, DB/metadata and
// the runtime commands -- for raw assets and metadata over this channel. The player never
// reads game archives itself and no exported mesh/texture files are involved.
//
// V0 skeleton (future-facing vocabulary only):
//
//   WMV -> player (runtime commands)
//     clearScene
//     loadWoWModel { sourcePath, fileDataID }       V0: acknowledged "not implemented yet"
//     setCamera    { position[3], rotation[3] }
//
//   player -> WMV (state)
//     unityReady | loaded | error { message }
//
//   player -> WMV (asset / metadata requests; WMV serves them from CASC/MPQ + DB in V1)
//     getAsset             { path }
//     getAssetByFileDataID { fileDataID }
//
//   WMV -> player (asset replies, V1; payload framing to be finalised with V1)
//     asset { path | fileDataID, size }             V0: logged, not handled
//
// The listener/read loop runs on a background thread; handlers run on the main thread
// (Unity API is main-thread-only) via a queue drained in Update().

using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using UnityEngine;

public class WmvIpcServer : MonoBehaviour
{
    public Action OnClearScene;
    public Action<string, int> OnLoadWoWModel;     // (sourcePath, fileDataID)
    public Action<Vector3, Vector3> OnSetCamera;   // (position, rotationEuler)

    [Serializable] class Msg
    {
        public string type;
        public string sourcePath;
        public int fileDataID;
        public string path;
        public int size;
        public string message;
        public float[] position;
        public float[] rotation;
    }

    TcpListener listener;
    Thread thread;
    volatile bool stopping;
    NetworkStream clientStream;              // most recent client (the WMV host)
    readonly object streamLock = new object();
    readonly Queue<Msg> inbox = new Queue<Msg>();

    void Start()
    {
        int port = 9500;
        var args = Environment.GetCommandLineArgs();
        for (int i = 0; i < args.Length - 1; i++)
            if (args[i] == "-wmvPort" && int.TryParse(args[i + 1], out var p))
                port = p;

        listener = new TcpListener(IPAddress.Loopback, port);
        listener.Start();
        thread = new Thread(AcceptLoop) { IsBackground = true };
        thread.Start();
        Debug.Log("WMV IPC listening on 127.0.0.1:" + port);
    }

    void AcceptLoop()
    {
        try
        {
            while (!stopping)
            {
                var client = listener.AcceptTcpClient();
                var stream = client.GetStream();
                lock (streamLock) clientStream = stream;
                Send("{\"type\":\"unityReady\"}");

                try
                {
                    using (var reader = new StreamReader(stream, Encoding.UTF8))
                    {
                        string line;
                        while (!stopping && (line = reader.ReadLine()) != null)
                        {
                            if (line.Trim().Length == 0) continue;
                            var msg = JsonUtility.FromJson<Msg>(line);
                            if (msg != null)
                                lock (inbox) inbox.Enqueue(msg);
                        }
                    }
                }
                catch (IOException) { /* host disconnected; wait for the next one */ }
                lock (streamLock) clientStream = null;
            }
        }
        catch (SocketException) { /* listener stopped */ }
    }

    void Update()
    {
        while (true)
        {
            Msg msg;
            lock (inbox)
            {
                if (inbox.Count == 0) return;
                msg = inbox.Dequeue();
            }

            try
            {
                switch (msg.type)
                {
                    case "clearScene":
                        OnClearScene?.Invoke();
                        break;
                    case "loadWoWModel":
                        OnLoadWoWModel?.Invoke(msg.sourcePath ?? "", msg.fileDataID);
                        break;
                    case "setCamera":
                        if (msg.position != null && msg.position.Length == 3 &&
                            msg.rotation != null && msg.rotation.Length == 3)
                            OnSetCamera?.Invoke(
                                new Vector3(msg.position[0], msg.position[1], msg.position[2]),
                                new Vector3(msg.rotation[0], msg.rotation[1], msg.rotation[2]));
                        break;
                    case "asset":
                        // Reply to getAsset / getAssetByFileDataID. Payload handling lands with the
                        // V1 loaders; for now just show that the round trip works.
                        Debug.Log("asset reply for " + (msg.path ?? ("fileDataID " + msg.fileDataID)) +
                                  " (" + msg.size + " bytes) -- asset replies are not handled yet");
                        break;
                    default:
                        SendError("Unknown message type: " + msg.type);
                        break;
                }
            }
            catch (Exception e)
            {
                SendError(e.Message);
            }
        }
    }

    // ---- state -> WMV ----

    public void SendLoaded() { Send("{\"type\":\"loaded\"}"); }

    public void SendError(string message)
    {
        Debug.LogWarning("WMV IPC error: " + message);
        Send("{\"type\":\"error\",\"message\":\"" + Escape(message) + "\"}");
    }

    // ---- asset / metadata requests -> WMV (the V1 asset channel) ----
    // The player never reads CASC/MPQ itself: everything it renders is requested from WMV.

    public void RequestAsset(string path)
    {
        Send("{\"type\":\"getAsset\",\"path\":\"" + Escape(path) + "\"}");
    }

    public void RequestAssetByFileDataID(int fileDataID)
    {
        Send("{\"type\":\"getAssetByFileDataID\",\"fileDataID\":" + fileDataID + "}");
    }

    // Cheap JSON string escape for the two characters that matter here.
    static string Escape(string s)
    {
        return (s ?? "").Replace("\\", "\\\\").Replace("\"", "\\\"");
    }

    void Send(string json)
    {
        lock (streamLock)
        {
            if (clientStream == null) return;
            try
            {
                var bytes = Encoding.UTF8.GetBytes(json + "\n");
                clientStream.Write(bytes, 0, bytes.Length);
                clientStream.Flush();
            }
            catch (IOException) { clientStream = null; }
        }
    }

    void OnDestroy()
    {
        stopping = true;
        try { listener?.Stop(); } catch { }
    }
}
