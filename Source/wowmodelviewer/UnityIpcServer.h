/*
 * UnityIpcServer.h
 *
 * Localhost IPC server for the embedded Unity renderer (protocol v2). WMV is the SERVER:
 * UnityRendererHost starts this listener BEFORE launching the player and passes the port on
 * the player's command line (-wmvPort <n>); the player connects back, announces itself with
 * unityReady and then asks WMV for the raw WoW assets/metadata it renders from. This is the
 * channel that lets the Unity viewport render directly from WoW data -- WMV keeps the app UI,
 * the active client/profile, CASC/MPQ access, DB/metadata and the runtime commands; the player
 * only ever sees bytes served here. No files are written to disk; there is no export workflow.
 *
 * Transport: TCP bound to 127.0.0.1 only (ephemeral port), newline-delimited JSON (one
 * object per line, UTF-8). Asset bytes travel base64-encoded inside the assetResponse line in
 * V1 -- simple and debuggable; a binary frame (JSON header + length-prefixed payload) can
 * replace it later without changing the request side.
 *
 *   player -> WMV
 *     { "type":"unityReady", "protocolVersion":2 }
 *     { "type":"getAsset",             "requestId":"abc123", "path":"creature/chicken/chicken.m2" }
 *     { "type":"getAssetByFileDataID", "requestId":"abc124", "fileDataID":123456 }
 *     { "type":"getModelTextures",     "requestId":"abc125", "fileDataID":123200 }
 *     { "type":"modelGeosetsApplied", "fileDataID":1521037, "revision":7, "status":"applied",
 *       "reason":"", "submeshVisible":[1,0,1], "triangles":2364, "animTimeMs":840 }
 *   WMV -> player
 *     { "type":"loadWoWModel", "path":"creature/chicken/chicken.m2", "fileDataID":0, "client":"active" }
 *     { "type":"assetResponse", "requestId":"abc123", "ok":true, "path":"...", "fileDataID":n,
 *       "byteLength":123456, "sha1":"...", "encoding":"base64", "data":"..." }
 *     { "type":"assetResponse", "requestId":"abc123", "ok":false, "error":"not found" }
 *     { "type":"modelTextures", "requestId":"abc125", "ok":true, "fileDataID":123200,
 *       "textures":[ { "index":0, "type":11, "fileDataID":123199, "source":"selection" } ] }
 *     { "type":"modelSkin", "fileDataID":1521037,
 *       "textures":[ { "index":0, "type":11, "fileDataID":1521061, "source":"selection" } ],
 *       "geosets":[ 101 ], "hasGeosets":true,
 *       "hasSubmeshVisible":true, "submeshCount":3, "submeshVisible":[1,1,0] }
 *     { "type":"modelGeosets", "fileDataID":1521037, "revision":7, "submeshCount":3,
 *       "submeshVisible":[1,0,1] }
 *     { "type":"modelAnimation", "fileDataID":1521037, "sequenceIndex":2, "animID":0,
 *       "durationMs":2000, "loop":true }
 *     { "type":"modelAnimationState", "fileDataID":1521037, "sequenceIndex":2, "playing":true,
 *       "timeMs":840, "speed":1.0, "loop":true, "explicitState":false }
 *
 * getModelTextures exists because modern M2s do NOT name their replaceable textures (a
 * creature skin's TXID entry is 0 and the texture array carries no filename) -- the skin comes
 * from the client database, which only WMV can read. It returns metadata only; the renderer
 * still fetches the bytes with getAssetByFileDataID.
 *
 * "type" is the WoW texture TYPE the texture feeds (11/12/13 for the three creature skin slots),
 * NOT a position: variation order and M2 texture-slot order need not agree, so the renderer maps
 * the type onto the slots that declare it. "source" says where the answer came from --
 * "selection" is the skin the viewport is showing right now, "database" only the model's default.
 *
 * modelSkin is pushed whenever the displayed skin changes (the dropdown, or the default chosen on
 * model load). Same payload as modelTextures, no request: the player swaps the texture and keeps
 * the mesh it already built.
 *
 * GEOSETS. "geosets" is the id summary the renderer has always taken (non-zero ids on); protocol 2
 * adds "submeshVisible", the host's own display flag for each of the displayed model's submeshes in
 * skin order, which the renderer uses instead when it is present. modelSkin and the modelTextures
 * reply carry both, so a load, a skin change and a reconnect all deliver the current state.
 * modelGeosets carries only the per-submesh state and is pushed when the user switches a geoset
 * (Model > Geosets): no texture or particle-colour lookup, applied to the built mesh at once. The
 * player answers each with modelGeosetsApplied -- "applied" (with what it now draws), "pending"
 * (kept for the model still loading) or "rejected" (with why; nothing changed) -- and sends the same
 * report with revision 0 after a load or a skin push applied a per-submesh state, so the host always
 * knows what the renderer is drawing rather than assuming it.
 *
 * modelAnimation is pushed the same way whenever the animation on display changes, and once after
 * loadWoWModel so the player starts on the animation the app is showing rather than on its own
 * idle. "sequenceIndex" is what the player must act on: it indexes the model's animation table,
 * which is both how the keyframes are stored and how the app's own selector identifies a choice.
 * Two sequences routinely share an "animID" (sub-animations of one action), so the id alone
 * cannot pick one; it is carried for the log and for recognising the idle (animID 0, "Stand").
 *
 * modelAnimationState carries the PLAYBACK state of that animation: whether it is running, how
 * fast, and where in the sequence it currently is. Unlike the skin and the animation choice there
 * is no single funnel for it -- play/pause, the speed slider, the frame slider, stop, step and
 * clear each change it, and the time advances every frame with no control involved at all -- so it
 * is pushed on every control change AND on a slow heartbeat while playing. The heartbeat is the
 * only correction channel for clock drift between two independently-timed renderers; the player
 * decides whether a given "timeMs" is worth snapping to, because only it knows where its own
 * clock is.
 *
 * Implementation: plain Winsock2, non-blocking, polled from the GUI thread by a wxTimer (the
 * app has no Qt event loop, so QTcpServer signals would never fire; and GAMEDIRECTORY must be
 * used from the GUI thread anyway). One client at a time (the embedded player). Windows only,
 * like the host itself.
 */

#ifndef UNITYIPCSERVER_H
#define UNITYIPCSERVER_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <functional>
#include <string>

#include <vector>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "UnityAssetAccess.h"

class UnityIpcServer : public wxEvtHandler
{
public:
  static const int PROTOCOL_VERSION = 2;

  UnityIpcServer();
  ~UnityIpcServer();

  // Bind 127.0.0.1 on an ephemeral port and start polling. False (with a logged reason) if
  // the socket cannot be created; the host then launches the player without IPC.
  bool start();
  void stop();

  bool isListening() const { return m_listen != 0; }
  bool isConnected() const { return m_client != 0; }
  bool isUnityReady() const { return m_unityReady; }
  int  port() const { return m_port; }
  // The protocol the connected player announced in unityReady (0 until then). A player older than
  // protocol 2 cannot switch submeshes: sendModelGeosets sends it nothing, since it would never answer.
  int  playerProtocolVersion() const { return m_playerProtocol; }
  bool playerSwitchesSubmeshes() const { return m_client && m_unityReady && m_playerProtocol >= 2; }

  // Runtime command: tell the player which model is active. Either path or fileDataID may be
  // empty/0. Queued if the player is connected; dropped (logged) otherwise.
  void sendLoadWoWModel(const QString & path, int fileDataID, const QString & client = QStringLiteral("active"));

  // Runtime command: the skin on display changed. Resolves the model's textures the same way
  // getModelTextures does -- so the push and the reply can never disagree -- and sends them
  // unasked. No-op when the player is not connected, or when nothing can be resolved.
  void sendModelSkin(int m2FileDataID);

  // Runtime command: the animation on display changed. sequenceIndex indexes the model's
  // animation table (what the app's own selector picks); animID and durationMs come from that
  // entry. No-op when the player is not connected.
  void sendModelAnimation(int m2FileDataID, int sequenceIndex, int animID, int durationMs, bool loop);

  // Runtime command: the playback state of that animation changed (or a heartbeat while it runs).
  // timeMs is the app's current position in the sequence. explicitState is true for a push made
  // by a control -- play, pause, a frame step, the frame or speed slider, the start of a load --
  // and false for the heartbeat: the player applies an explicit position as given, however small
  // the step, and holds heartbeats to a dead band so transport jitter cannot make it twitch.
  // No-op when the player is not connected.
  void sendModelAnimationState(int m2FileDataID, int sequenceIndex, bool playing, int timeMs,
                               float speed, bool loop, bool explicitState);

  // Runtime command: the displayed model's per-submesh display state changed (a Geosets
  // checkbox). Sends the WHOLE current state (UnityAssetAccess::displayedSubmeshVisibility), numbered
  // by revision so the player's modelGeosetsApplied answer can be matched to it. False (nothing
  // sent) when the player is not connected and ready, speaks a protocol older than 2, or
  // m2FileDataID is not the canvas model.
  bool sendModelGeosets(int m2FileDataID, int revision);

  // The player's answer to modelGeosets, or its report after a load / skin push (revision 0).
  struct GeosetAck
  {
    int fileDataID = 0;
    int revision = 0;
    QString status;              // "applied" / "pending" / "rejected"
    QString reason;
    bool hasVisible = false;
    std::vector<bool> visible;   // what the renderer now switches on, per skin submesh
    int triangles = 0;
    long long animTimeMs = 0;
  };
  // Raised on the GUI thread for every modelGeosetsApplied.
  std::function<void(const GeosetAck &)> onGeosetsApplied;

  // Raised (on the GUI thread) when the player's unityReady arrives -- the host uses it to
  // push the currently displayed model.
  std::function<void()> onUnityReady;

  // One accept/receive/send pass. Driven by the wxTimer normally; the headless self-test calls
  // it directly because no event loop runs there.
  void poll();

  // Diagnostics (also logged with the [unityipc] prefix): counters for self-tests/reports.
  struct Stats
  {
    int connections = 0;
    int requests = 0;       // getAsset + getAssetByFileDataID received
    int responsesOk = 0;
    int responsesError = 0;
    long long bytesServed = 0;
    int skinPushes = 0;     // modelSkin messages sent (the displayed skin changed)
    int animPushes = 0;     // modelAnimation messages sent (the displayed animation changed)
    int statePushes = 0;    // modelAnimationState messages sent (play/pause/speed/time)
    int geosetPushes = 0;   // modelGeosets messages sent (a geoset was switched)
    int geosetAcks = 0;     // modelGeosetsApplied received, any status
    int geosetRejects = 0;  // ... of which "rejected"
    QString lastGeosets;    // "rev <n> hidden [i,j]" of the last modelGeosets sent
    QString lastGeosetAck;  // "rev <n> <status> <reason>" of the last answer
    QString lastRequest;    // "path" or "fileDataID n"
    QString lastProvider;   // "CASC" / "MPQ" / ""
    QString lastError;
    QString lastSkin;       // "<fileDataID> (<source>)" of the last skin pushed
    QString lastAnimation;  // "seq <n> animID <id> <ms>ms" of the last animation pushed
    QString lastState;      // "playing|paused <ms>ms x<speed>" of the last state pushed
  };
  const Stats & stats() const { return m_stats; }

private:
  void onPoll(wxTimerEvent & event);
  void pollAccept();
  void pollReceive();
  void pollSend();
  void handleLine(const std::string & line);
  void handleGetAsset(const QJsonObject & msg, bool byFileDataID);
  void handleGetModelTextures(const QJsonObject & msg);
  void handleGeosetsApplied(const QJsonObject & msg);
  static QJsonArray textureArray(const std::vector<UnityAssetAccess::ModelTexture> & textures);
  // Adds "geosets"/"hasGeosets" to a message about one model, when a selection is known.
  static void addGeosets(QJsonObject & msg, int m2FileDataID);

  // The item ParticleColor override, when the displayed model has one. Attached to the same two
  // messages as the geosets, so a renderer that has the textures always has the colours that go
  // with them and the two can never describe different states.
  static void addParticleColor(QJsonObject & msg, int m2FileDataID);
  void queueJson(const QJsonObject & obj);
  void dropClient(const char * why);

  wxTimer m_timer;
  unsigned long long m_listen = 0;   // SOCKET (kept opaque here to keep winsock out of the header)
  unsigned long long m_client = 0;
  int m_port = 0;
  bool m_unityReady = false;
  int m_playerProtocol = 0;          // protocolVersion from the player's unityReady
  std::string m_inBuf;               // partial incoming line
  std::string m_outBuf;              // pending bytes to send (partial sends are normal for big assets)
  Stats m_stats;
};

#endif // UNITYIPCSERVER_H
