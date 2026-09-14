/*
 * UnityIpcServer.h
 *
 * Localhost IPC server for the embedded Unity renderer (protocol v4). WMV is the SERVER:
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
 *     { "type":"unityReady", "protocolVersion":4 }
 *     { "type":"getAsset",             "requestId":"abc123", "path":"creature/chicken/chicken.m2" }
 *     { "type":"getAssetByFileDataID", "requestId":"abc124", "fileDataID":123456 }
 *     { "type":"getModelTextures",     "requestId":"abc125", "fileDataID":123200 }
 *     { "type":"modelGeosetsApplied", "fileDataID":1521037, "revision":7, "status":"applied",
 *       "reason":"", "submeshVisible":[1,0,1], "triangles":2364, "animTimeMs":840 }
 *     { "type":"characterSceneApplied", "fileDataID":1011653, "revision":4, "load":12, "status":"applied",
 *       "reason":"", "merged":3, "attachments":4, "missing":[], "ms":212 }
 *     { "type":"mapObjectLoaded", "fileDataID":115058, "load":13, "status":"built", "reason":"",
 *       "groups":1, "groupFilesRequested":1, "groupFilesMissing":0, "batches":3, "submeshes":3, "renderers":1,
 *       "materials":3, "provisionalMaterials":3, "unresolvedMaterials":0, "blendedMaterials":0,
 *       "texturesReferenced":3, "texturesDecoded":3, "texturesMissing":0, "vertices":1234, "triangles":987,
 *       "boundsMin":[x,y,z], "boundsMax":[x,y,z],
 *       "timings":{"rootMs":5,"groupsMs":40,"texturesMs":60,"buildMs":12,"totalMs":130},
 *       "liveMapObjects":1, "liveModels":0 }
 *     { "type":"runtimeState", "query":3, "liveMapObjects":0, "liveModels":1, "modelFileDataID":1234567,
 *       "mapObjectFileDataID":0, "loading":false }
 *   WMV -> player
 *     { "type":"loadWoWModel", "path":"creature/chicken/chicken.m2", "fileDataID":0, "client":"active",
 *       "character":false, "load":12, "kind":"m2" }
 *     { "type":"loadWoWModel", "path":"world/wmo/.../it_trollhouse03.wmo", "fileDataID":115058,
 *       "client":"active", "character":false, "load":13, "kind":"wmo" }
 *     { "type":"runtimeState", "query":3 }
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
 *     { "type":"characterImage", "hash":"body-3", "width":2048, "height":1024, "format":"bgra8",
 *       "encoding":"base64", "data":"..." }
 *     { "type":"characterScene", "fileDataID":1011653, "revision":4,
 *       "body":{ "textures":[ {"slot":0,"type":1,"image":"body-3"}, {"slot":1,"type":6,"fileDataID":1234} ],
 *                "submeshCount":120, "submeshVisible":[1,0,...], "closeRightHand":true, "closeLeftHand":false },
 *       "merged":[ {"key":"m4353217","fileDataID":4353217,"mergeIndex":1,"textures":[...],
 *                   "submeshCount":6,"submeshVisible":[...],"boneMap":[0,1,2,...]} ],
 *       "attachments":[ {"key":"a11:1234567","fileDataID":1234567,"attachmentId":11,"mirrored":false,
 *                        "visible":true,"textures":[...],"submeshCount":2,"submeshVisible":[1,1]} ] }
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
 * CHARACTERS (protocol 3). A playable character is not one model: it is a body with a host-composited
 * texture, the collection armour and customization parts refreshMerging laid into it, and the item
 * models attached at its attachment points. loadWoWModel carries
 * "character":true for one, and characterScene carries the RESOLVED state of all of it (see
 * UnityCharacterScene.h): per model, the texture each slot binds -- a FileDataID, or an image the host
 * composited -- the geoset display flags, the host's bone table for a merged model, and the attachment
 * id for an attached one. It is sent whole whenever any of that changes (a customization, equipment,
 * a render toggle, a geoset checkbox); the player dresses the body it built from the same load and
 * applies each scene as a whole, and answers characterSceneApplied: "applied" (with the parts it could
 * not build in "missing"), "rejected" (a real refusal: the load failed -- the reason then starts with
 * "load failed" -- the scene is not about the character on screen or being loaded, or it does not fit)
 * or "superseded" (dropped because a newer scene or a new load replaced it, which is answered in its
 * own right; not a failure). "load" in loadWoWModel is the host's load serial, increasing with every
 * load and never 0; each characterSceneApplied names the serial of the load its scene belonged to (0
 * when it belonged to none), so an answer about an earlier load -- often of the same body model, and
 * so the same fileDataID -- is never taken for one about the load on display. characterImage carries a
 * composited image once, before the first scene that names it; a scene naming an image the player
 * already holds does not resend the pixels. Rows are top row first, bytes B,G,R,A -- the memory
 * layout of the QImage the host's GL texture was uploaded from.
 *
 * WORLD MODELS (protocol 4). A WMO travels as a loadWoWModel with "kind":"wmo" naming the ROOT file
 * ("kind" absent, or "m2", is a model as before); the host sends it only to a player that announced
 * protocol 4 or later, and an older player gets the out-of-date notice instead. The player fetches the
 * root, its LOD0 group files (the first MOHD group-count entries of GFID) and the material textures
 * with getAssetByFileDataID -- the host sends no geometry -- and answers each load once with
 * mapObjectLoaded: "built", "failed" (with the reason) or "superseded" (a newer load of either kind
 * replaced it before it was built; not a failure). "load" is the serial of the loadWoWModel it answers.
 * The counts describe what was built (bounds in Unity space; timings in milliseconds), and
 * liveMapObjects / liveModels are the runtime roots alive in the player after the outcome was adopted,
 * which is how a lifecycle test proves nothing leaked or doubled. No skin, animation or geoset message
 * is sent about a WMO: it has none. runtimeState (also protocol 4) is a test's question: the player
 * answers it with the runtimes alive right now and the fileDataID of the model and of the world model on
 * screen (0 for none), "query" echoing the question's number. It is what shows a world model left alive
 * under a model -- a step that ends on a model has no mapObjectLoaded, and the next world-model adoption
 * disposes whatever is left before it counts.
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
#include <initializer_list>
#include <map>
#include <string>

#include <vector>

#include <QByteArray>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "UnityAssetAccess.h"

class UnityIpcServer : public wxEvtHandler
{
public:
  static const int PROTOCOL_VERSION = 4;

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
  // The player can dress a character from a characterScene (protocol 3).
  bool playerDressesCharacters() const { return m_client && m_unityReady && m_playerProtocol >= 3; }
  // The player draws world models: it takes loadWoWModel "kind":"wmo" and answers mapObjectLoaded (protocol 4).
  bool playerDrawsMapObjects() const { return m_client && m_unityReady && m_playerProtocol >= 4; }

  // Runtime command: tell the player which model is active. Either path or fileDataID may be
  // empty/0. Queued if the player is connected; dropped (logged) otherwise.
  // character: the model is a playable character, dressed by the characterScene that follows.
  // load: the host's load serial for this load (> 0), which the player's characterSceneApplied and
  // mapObjectLoaded echo.
  // kind: "m2" (a model) or "wmo" (a world model ROOT, fileDataID required; see WORLD MODELS above).
  // Callers send "wmo" only when playerDrawsMapObjects().
  void sendLoadWoWModel(const QString & path, int fileDataID, const QString & client = QStringLiteral("active"),
                        bool character = false, int load = 0, const QString & kind = QStringLiteral("m2"));

  // Runtime command: the resolved state of the character on display (UnityCharacterScene::build).
  // False when the player cannot dress characters or nothing was sent.
  bool sendCharacterScene(int m2FileDataID, int revision, const QJsonObject & scene);

  // The id a composited image travels under. kind names the image's role ("body", "eyes"); while
  // its pixels are the ones last sent under that kind the same id comes back and nothing is sent,
  // otherwise the image goes out (characterImage) under a new id first.
  QString shareCharacterImage(const QString & kind, const QImage & image);

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

  // The player's answer to a characterScene.
  struct SceneAck
  {
    int fileDataID = 0;
    int revision = 0;
    int load = 0;                // the load serial the scene belonged to (0: none the player knew)
    QString status;              // "applied" / "pending" / "rejected" / "superseded"
    QString reason;
    int merged = 0;
    int attachments = 0;
    QStringList missing;         // parts the player could not build (key)
    int ms = 0;
  };
  std::function<void(const SceneAck &)> onCharacterSceneApplied;

  // The player's report on a world-model load (mapObjectLoaded). Every field of the message; a count
  // the player did not send reads -1, so a check can tell "absent" from zero.
  struct MapObjectReport
  {
    int fileDataID = 0;
    int load = 0;                  // the loadWoWModel serial this answers
    QString status;                // "built" / "failed" / "superseded"
    QString reason;
    int groups = -1;
    int groupFilesRequested = -1;
    int groupFilesMissing = -1;
    int batches = -1;
    int submeshes = -1;
    int renderers = -1;
    int materials = -1;
    int provisionalMaterials = -1;
    int unresolvedMaterials = -1;
    int blendedMaterials = -1;
    int texturesReferenced = -1;
    int texturesDecoded = -1;
    int texturesMissing = -1;
    long long vertices = -1;
    long long triangles = -1;
    bool hasBounds = false;        // both arrays present with three numbers each
    double boundsMin[3] = { 0.0, 0.0, 0.0 };
    double boundsMax[3] = { 0.0, 0.0, 0.0 };
    double rootMs = -1.0;
    double groupsMs = -1.0;
    double texturesMs = -1.0;
    double buildMs = -1.0;
    double totalMs = -1.0;
    int liveMapObjects = -1;       // world-model runtime roots alive in the player after adoption
    int liveModels = -1;           // model/character runtime roots alive in the player after adoption
    // The whole report on one line, every field, for logs.
    QString describe() const;
  };
  // Raised on the GUI thread for every mapObjectLoaded.
  std::function<void(const MapObjectReport &)> onMapObjectLoaded;

  // The player's answer to runtimeState: what it holds at the moment it answered. A field the player did
  // not send reads -1.
  struct RuntimeState
  {
    int query = 0;                 // the number of the question this answers
    int liveMapObjects = -1;       // world-model runtimes alive
    int liveModels = -1;           // model runtimes alive (a character's parts included)
    int modelFileDataID = -1;      // the model on screen, 0 for none
    int mapObjectFileDataID = -1;  // the world model on screen, 0 for none
    bool loading = false;          // a load of either kind in flight
    QString describe() const;
  };
  // Ask the player what it holds (runtimeState). Returns the question's number, which the answer echoes,
  // or 0 when nothing was sent (no player, or one older than protocol 4).
  int requestRuntimeState();
  // Raised on the GUI thread for every runtimeState answer.
  std::function<void(const RuntimeState &)> onRuntimeState;

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
    int scenePushes = 0;    // characterScene messages sent
    int imagePushes = 0;    // characterImage messages sent
    long long imageBytes = 0;
    int sceneAcks = 0;      // characterSceneApplied received, any status
    int sceneApplied = 0;   // ... of which "applied"
    QString lastScene;      // "rev <n>: <merged> merged, <attachments> attached"
    QString lastSceneAck;   // "rev <n> <status> <merged>/<attachments> <reason>"
    int mapObjectLoads = 0;       // loadWoWModel "kind":"wmo" sent
    int mapObjectReports = 0;     // mapObjectLoaded received, any status
    int mapObjectBuilt = 0;       // ... of which "built"
    int mapObjectFailed = 0;      // ... of which "failed"
    int mapObjectSuperseded = 0;  // ... of which "superseded"
    QString lastMapObject;        // "<fileDataID> load <n> <status> <groups> groups <reason>" of the last report
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
  void handleCharacterSceneApplied(const QJsonObject & msg);
  void handleMapObjectLoaded(const QJsonObject & msg);
  void handleRuntimeState(const QJsonObject & msg);
  void queueLine(const QByteArray & line);
  // One line assembled from pieces straight in the send buffer: a characterImage line is ~11 MB, and
  // joining it into one QByteArray first would copy all of it once more.
  void queueLineParts(std::initializer_list<QByteArray> parts);
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
  // The composited images this connection has been sent, by kind, and the id each went under.
  std::map<QString, QImage> m_sentImages;
  std::map<QString, QString> m_sentImageIds;
  int m_imageSerial = 0;
  int m_runtimeQuery = 0;            // the last runtimeState question's number
  std::string m_inBuf;              // partial incoming line
  std::string m_outBuf;              // pending bytes to send (partial sends are normal for big assets)
  // How much of m_outBuf has gone out. Sent bytes are dropped from the front only when they are more
  // than half the buffer (or all of it): erasing them after every send moved the whole unsent rest
  // each time, which for an 11 MB image line sent 256 KB at a time was hundreds of MB of memmove.
  size_t m_outPos = 0;
  Stats m_stats;
};

#endif // UNITYIPCSERVER_H
