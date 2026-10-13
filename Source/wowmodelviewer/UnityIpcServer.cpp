/*
 * UnityIpcServer.cpp
 */

// winsock2 must precede any <windows.h> (wx drags it in). The exe is built with
// WIN32_LEAN_AND_MEAN, so windows.h does not pull the legacy winsock.h in either way.
#ifdef _WINDOWS
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#endif

#include "UnityIpcServer.h"

#include <algorithm>
#include <cstring>

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>

#include "UnityAssetAccess.h"

#include "logger/Logger.h"

namespace
{
  // The system's performance counter in milliseconds: one clock that the host and the player, two processes on one
  // machine, both read. 0 where there is none.
  double performanceCounterMs()
  {
#ifdef _WINDOWS
    LARGE_INTEGER count, frequency;
    if (QueryPerformanceCounter(&count) && QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0)
      return (double)count.QuadPart * 1000.0 / (double)frequency.QuadPart;
#endif
    return 0.0;
  }

  const int POLL_INTERVAL_MS = 20;
  const size_t RECV_CHUNK = 64 * 1024;
  const size_t MAX_LINE = 1024 * 1024;   // requests from the player are tiny; guard against junk
}

// A root ADT's first read is the read nothing in this process made before from that client's storage: the cold-read
// candidate. A WDT's is not cold, RequestTerrainTile having read it whole (ResolveTerrainTile, catalog->inspect()) on this
// thread just before.
MapAssetReadTracker::Read MapAssetReadTracker::note(int epoch, int fileDataID)
{
  if (epoch != m_epoch)
  {
    m_ids.clear();
    m_epoch = epoch;
  }
  if (m_ids.count(fileDataID))
    return Read::Again;
  if (m_ids.size() >= m_capacity)
  {
    m_untracked++;
    return Read::Untracked;
  }
  m_ids.insert(fileDataID);
  return Read::First;
}

void MapAssetReadTracker::reset(int epoch)
{
  m_ids.clear();
  m_epoch = epoch;
}

UnityIpcServer::UnityIpcServer()
{
  m_timer.SetOwner(this);
  Bind(wxEVT_TIMER, &UnityIpcServer::onPoll, this);
  m_clock.start();
}

UnityIpcServer::~UnityIpcServer()
{
  stop();
}

#ifdef _WINDOWS

namespace
{
  bool g_wsaStarted = false;

  bool ensureWinsock()
  {
    if (g_wsaStarted)
      return true;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
      LOG_ERROR << "[unityipc] WSAStartup failed:" << WSAGetLastError();
      return false;
    }
    g_wsaStarted = true; // left initialised for the process lifetime (cheap, and wx/Qt may share it)
    return true;
  }

  bool setNonBlocking(SOCKET s)
  {
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
  }
}

bool UnityIpcServer::start()
{
  if (m_listen)
    return true;
  if (!ensureWinsock())
    return false;

  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET)
  {
    LOG_ERROR << "[unityipc] socket() failed:" << WSAGetLastError();
    return false;
  }

  // Loopback only, ephemeral port: never reachable from the network, never collides with
  // another WMV instance. The chosen port is handed to the player on its command line.
  sockaddr_in addr;
  ZeroMemory(&addr, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 ||
      listen(s, 1) != 0 || !setNonBlocking(s))
  {
    LOG_ERROR << "[unityipc] bind/listen failed:" << WSAGetLastError();
    closesocket(s);
    return false;
  }

  int len = sizeof(addr);
  if (getsockname(s, reinterpret_cast<sockaddr *>(&addr), &len) != 0)
  {
    LOG_ERROR << "[unityipc] getsockname failed:" << WSAGetLastError();
    closesocket(s);
    return false;
  }

  m_listen = (unsigned long long)s;
  m_port = ntohs(addr.sin_port);
  m_unityReady = false;
  m_playerProtocol = 0;
  m_sentImages.clear();
  m_sentImageIds.clear();
  m_stats = Stats();
  m_mapAssetsServed.clear();
  m_mapAssetsServedTotal = 0;
  m_mapAssetRecordsDropped = 0;
  dropHeldMapAssets("the server started");
  m_mapAssetPolls.clear();
  m_mapAssetPollsTotal = 0;
  m_mapAssetsQueued = 0;
  m_mapAssetsRefusedQueueFull = 0;
  m_mapAssetQueueDepthMax = 0;
  m_timer.Start(POLL_INTERVAL_MS);
  LOG_INFO << "[unityipc] listening on 127.0.0.1:" << m_port << "(protocol v" << PROTOCOL_VERSION << ")";
  return true;
}

void UnityIpcServer::stop()
{
  m_timer.Stop();
  if (m_client)
  {
    closesocket((SOCKET)m_client);
    m_client = 0;
  }
  if (m_listen)
  {
    closesocket((SOCKET)m_listen);
    m_listen = 0;
    LOG_INFO << "[unityipc] stopped (port" << m_port << ")";
  }
  m_port = 0;
  m_unityReady = false;
  m_playerProtocol = 0;
  m_sentImages.clear();
  m_sentImageIds.clear();
  m_inBuf.clear();
  m_outBuf.clear();
  m_outPos = 0;
  dropHeldMapAssets("the server stopped");
}

void UnityIpcServer::onPoll(wxTimerEvent & WXUNUSED(event))
{
  poll();
}

void UnityIpcServer::poll()
{
  if (!m_listen)
    return;
  pollAccept();
  if (m_client)
  {
    pollReceive();
    // The map files asked for, in order, as far as this poll's budget goes (THE MAP ASSET QUEUE).
    if (m_client)
      serveMapAssetQueue();
    if (m_client)
      pollSend();
  }
}

void UnityIpcServer::pollAccept()
{
  SOCKET c = accept((SOCKET)m_listen, nullptr, nullptr);
  if (c == INVALID_SOCKET)
    return; // WSAEWOULDBLOCK = nobody waiting; anything else is also just "no client yet"

  if (m_client)
  {
    // Only the embedded player is expected; a second connection replaces a stale one.
    LOG_INFO << "[unityipc] new connection replaces the previous client";
    closesocket((SOCKET)m_client);
    m_client = 0;
  }
  setNonBlocking(c);
  BOOL noDelay = TRUE;
  setsockopt(c, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&noDelay), sizeof(noDelay));
  // The send buffer decides how much of a queued line leaves per poll: once it is full the rest waits
  // for the next timer tick (POLL_INTERVAL_MS). With the default, a 16 MB .skel response or an 11 MB
  // body image spent most of its transfer waiting on ticks -- over half a second per character load.
  // 64 MB is the largest asset served (MAX_ASSET_SIZE in UnityAssetAccess), so one binary payload (protocol 10) fits
  // an empty send buffer and usually leaves in the poll that queued it. Answers the player has not read yet share the
  // buffer; what does not fit goes out on the next ticks. The kernel takes only what is sent: a limit, not an
  // allocation.
  int sendBuffer = 64 * 1024 * 1024;
  setsockopt(c, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char *>(&sendBuffer), sizeof(sendBuffer));
  m_client = (unsigned long long)c;
  m_unityReady = false;
  m_playerProtocol = 0;
  m_sentImages.clear();
  m_sentImageIds.clear();
  m_inBuf.clear();
  m_outBuf.clear();
  m_outPos = 0;
  dropHeldMapAssets("a new connection");
  m_stats.connections++;
  LOG_INFO << "[unityipc] Unity connected (connection #" << m_stats.connections << ")";
}

void UnityIpcServer::dropClient(const char * why)
{
  if (!m_client)
    return;
  LOG_INFO << "[unityipc] Unity disconnected (" << why << ")";
  closesocket((SOCKET)m_client);
  m_client = 0;
  m_unityReady = false;
  m_playerProtocol = 0;
  m_sentImages.clear();
  m_sentImageIds.clear();
  m_inBuf.clear();
  m_outBuf.clear();
  m_outPos = 0;
  dropHeldMapAssets(why);
}

void UnityIpcServer::pollReceive()
{
  char buf[RECV_CHUNK];
  for (;;)
  {
    const int n = recv((SOCKET)m_client, buf, (int)sizeof(buf), 0);
    if (n > 0)
    {
      m_inBuf.append(buf, (size_t)n);
      // dispatch complete lines
      size_t nl;
      while ((nl = m_inBuf.find('\n')) != std::string::npos)
      {
        std::string line = m_inBuf.substr(0, nl);
        m_inBuf.erase(0, nl + 1);
        if (!line.empty() && line.back() == '\r')
          line.pop_back();
        if (!line.empty())
          handleLine(line);
        if (!m_client)
          return; // a handler dropped the client
      }
      if (m_inBuf.size() > MAX_LINE)
      {
        dropClient("oversized line");
        return;
      }
      continue;
    }
    if (n == 0)
    {
      dropClient("closed by peer");
      return;
    }
    const int err = WSAGetLastError();
    if (err == WSAEWOULDBLOCK)
      return;
    dropClient("recv error");
    return;
  }
}

void UnityIpcServer::pollSend()
{
  while (m_outPos < m_outBuf.size())
  {
    const int chunk = (int)std::min<size_t>(m_outBuf.size() - m_outPos, 256 * 1024);
    const int n = send((SOCKET)m_client, m_outBuf.data() + m_outPos, chunk, 0);
    if (n > 0)
    {
      m_outPos += (size_t)n;
      continue;
    }
    const int err = WSAGetLastError();
    if (n < 0 && err == WSAEWOULDBLOCK)
    {
      // Kernel buffer full; the rest goes out on the next poll. The sent front is dropped only once it
      // is the larger part of the buffer, so a compaction never moves more bytes than were sent since
      // the last one (see m_outPos).
      if (m_outPos > m_outBuf.size() / 2)
      {
        m_outBuf.erase(0, m_outPos);
        m_outPos = 0;
      }
      return;
    }
    dropClient("send error");
    return;
  }
  m_outBuf.clear();
  m_outPos = 0;
}

#else // !_WINDOWS

bool UnityIpcServer::start() { LOG_INFO << "[unityipc] not supported on this platform"; return false; }
void UnityIpcServer::stop() {}
void UnityIpcServer::onPoll(wxTimerEvent &) {}
void UnityIpcServer::poll() {}
void UnityIpcServer::pollAccept() {}
void UnityIpcServer::pollReceive() {}
void UnityIpcServer::pollSend() {}
void UnityIpcServer::dropClient(const char *) {}

#endif // _WINDOWS

// ---------------------------------------------------------------- protocol

void UnityIpcServer::queueJson(const QJsonObject & obj)
{
  if (!m_client)
  {
    LOG_INFO << "[unityipc] no client connected -- dropping" << obj.value("type").toString();
    return;
  }
  queueLine(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void UnityIpcServer::queueLine(const QByteArray & line)
{
  queueLineParts({ line });
}

void UnityIpcServer::queueLineParts(std::initializer_list<QByteArray> parts)
{
  if (!m_client)
    return;
  for (const QByteArray & part : parts)
    m_outBuf.append(part.constData(), (size_t)part.size());
  m_outBuf.push_back('\n');
#ifdef _WINDOWS
  pollSend(); // try to push it out right away; leftovers go on the next poll
#endif
}

void UnityIpcServer::queueFrame(const QByteArray & header, const QByteArray & payload)
{
  if (!m_client)
    return;
  m_outBuf.append(header.constData(), (size_t)header.size());
  m_outBuf.push_back('\n');
  m_outBuf.append(payload.constData(), (size_t)payload.size());
  m_outBuf.push_back('\n');   // the player checks it: a payload not followed by its newline means out of step
#ifdef _WINDOWS
  pollSend();
#endif
}

QJsonArray UnityIpcServer::textureArray(const std::vector<UnityAssetAccess::ModelTexture> & textures)
{
  QJsonArray arr;
  for (const UnityAssetAccess::ModelTexture & t : textures)
  {
    QJsonObject o;
    o["index"] = t.index;
    o["type"] = t.type;
    o["fileDataID"] = t.fileDataID;
    o["source"] = UnityAssetAccess::sourceName(t.source);
    arr.append(o);
  }
  return arr;
}

void UnityIpcServer::addGeosets(QJsonObject & msg, int m2FileDataID)
{
  std::vector<int> geosets;
  const bool known = UnityAssetAccess::selectedModelGeosets(m2FileDataID, geosets);

  // "hasGeosets" separates "this display switches none on" (an answer) from "no selection to
  // report" (not an answer). Without it the renderer could not tell an empty list from silence,
  // and those mean opposite things: the first hides every non-zero geoset, the second must
  // change nothing.
  msg["hasGeosets"] = known;
  QJsonArray arr;
  for (size_t i = 0; i < geosets.size(); i++)
    arr.append(geosets[i]);
  msg["geosets"] = arr;

  // Protocol 2: the per-submesh flags themselves, which the id list above summarises lossily (per
  // id, never id 0). The renderer uses these when present. At a load they agree with the id list
  // (the host's rules set flags per id and never hide id 0 on a model this viewport shows); after
  // a Geosets checkbox they may not, and then they are the truth.
  std::vector<bool> visible;
  const bool haveVisible = UnityAssetAccess::displayedSubmeshVisibility(m2FileDataID, visible);
  msg["hasSubmeshVisible"] = haveVisible;
  if (haveVisible)
  {
    QJsonArray bits;
    for (size_t i = 0; i < visible.size(); i++)
      bits.append(visible[i] ? 1 : 0);
    msg["submeshCount"] = (int)visible.size();
    msg["submeshVisible"] = bits;
  }
}

bool UnityIpcServer::sendModelGeosets(int m2FileDataID, int revision)
{
  if (!playerSwitchesSubmeshes() || m2FileDataID <= 0)
    return false;
  std::vector<bool> visible;
  if (!UnityAssetAccess::displayedSubmeshVisibility(m2FileDataID, visible))
    return false;

  QJsonObject msg;
  msg["type"] = "modelGeosets";
  msg["fileDataID"] = m2FileDataID;
  msg["revision"] = revision;
  msg["submeshCount"] = (int)visible.size();
  QJsonArray bits;
  QString hidden;
  for (size_t i = 0; i < visible.size(); i++)
  {
    bits.append(visible[i] ? 1 : 0);
    if (!visible[i])
      hidden += (hidden.isEmpty() ? "" : ",") + QString::number((int)i);
  }
  msg["submeshVisible"] = bits;
  m_stats.geosetPushes++;
  m_stats.lastGeosets = QString("rev %1 hidden [%2]").arg(revision).arg(hidden.isEmpty() ? "none" : hidden);
  LOG_INFO << "[unityipc] -> modelGeosets fileDataID=" << m2FileDataID << "revision=" << revision
           << "submeshes=" << (int)visible.size() << "hidden=" << (hidden.isEmpty() ? QString("none") : hidden);
  queueJson(msg);
  return true;
}

void UnityIpcServer::handleCharacterSceneApplied(const QJsonObject & msg)
{
  SceneAck ack;
  ack.fileDataID = msg.value("fileDataID").toInt(0);
  ack.revision = msg.value("revision").toInt(0);
  ack.load = msg.value("load").toInt(0);
  ack.status = msg.value("status").toString();
  ack.reason = msg.value("reason").toString();
  ack.merged = msg.value("merged").toInt(0);
  ack.attachments = msg.value("attachments").toInt(0);
  ack.ms = msg.value("ms").toInt(0);
  const QJsonArray missing = msg.value("missing").toArray();
  for (const QJsonValue & v : missing)
    ack.missing << v.toString();
  ack.mountKey = msg.value("mountKey").toString();
  ack.mountStatus = msg.value("mountStatus").toString();
  ack.mountReason = msg.value("mountReason").toString();

  m_stats.sceneAcks++;
  if (ack.status == "applied")
    m_stats.sceneApplied++;
  m_stats.lastSceneAck = QString("rev %1 %2 %3/%4 %5").arg(ack.revision).arg(ack.status)
                           .arg(ack.merged).arg(ack.attachments).arg(ack.reason);
  const QString missingText = ack.missing.isEmpty() ? QString() : "missing: " + ack.missing.join(",");
  if (ack.mountStatus.isEmpty())
  {
    LOG_INFO << "[unityipc] <- characterSceneApplied fileDataID=" << ack.fileDataID << "load=" << ack.load
             << m_stats.lastSceneAck << "in" << ack.ms << "ms" << missingText;
  }
  else
  {
    // The mount's outcome, from a player that reports one (protocol 5). The player's text is appended, never
    // passed through arg().
    if (ack.mountStatus == "applied")
      m_stats.mountApplied++;
    else if (ack.mountStatus == "failed")
      m_stats.mountFailed++;
    m_stats.lastMountAck = (ack.mountKey.isEmpty() ? QString("-") : ack.mountKey) + " " + ack.mountStatus +
                           (ack.mountReason.isEmpty() ? QString() : " " + ack.mountReason);
    LOG_INFO << "[unityipc] <- characterSceneApplied fileDataID=" << ack.fileDataID << "load=" << ack.load
             << m_stats.lastSceneAck << "in" << ack.ms << "ms" << missingText << "mount" << m_stats.lastMountAck;
  }
  if (onCharacterSceneApplied)
    onCharacterSceneApplied(ack);
}

QString UnityIpcServer::MapObjectReport::describe() const
{
  const auto ms = [](double v) { return v < 0.0 ? QString("-") : QString::number(v, 'f', 1); };
  QString bounds = "-";
  if (hasBounds)
    bounds = QString("[%1,%2,%3]..[%4,%5,%6]")
               .arg(boundsMin[0], 0, 'f', 2).arg(boundsMin[1], 0, 'f', 2).arg(boundsMin[2], 0, 'f', 2)
               .arg(boundsMax[0], 0, 'f', 2).arg(boundsMax[1], 0, 'f', 2).arg(boundsMax[2], 0, 'f', 2);
  // The player's free text is appended, never passed through arg(): a "%1" in it would be substituted.
  return QString("fileDataID=%1 load=%2 status=%3 groups=%4 groupFilesRequested=%5 "
                 "groupFilesMissing=%6 batches=%7 submeshes=%8")
           .arg(fileDataID).arg(load).arg(status).arg(groups).arg(groupFilesRequested)
           .arg(groupFilesMissing).arg(batches).arg(submeshes) +
         QString(" renderers=%1 materials=%2 provisionalMaterials=%3 unresolvedMaterials=%4 blendedMaterials=%5 "
                 "texturesReferenced=%6 texturesDecoded=%7 texturesMissing=%8")
           .arg(renderers).arg(materials).arg(provisionalMaterials).arg(unresolvedMaterials).arg(blendedMaterials)
           .arg(texturesReferenced).arg(texturesDecoded).arg(texturesMissing) +
         QString(" vertices=%1 triangles=%2 bounds=%3 rootMs=%4 groupsMs=%5 texturesMs=%6 buildMs=%7 totalMs=%8 "
                 "liveMapObjects=%9")
           .arg(vertices).arg(triangles).arg(bounds).arg(ms(rootMs)).arg(ms(groupsMs)).arg(ms(texturesMs))
           .arg(ms(buildMs)).arg(ms(totalMs)).arg(liveMapObjects) +
         QString(" liveModels=%1").arg(liveModels) + " reason=\"" + reason + "\"";
}

void UnityIpcServer::handleMapObjectLoaded(const QJsonObject & msg)
{
  // Counts the player did not send stay -1 (see MapObjectReport); toDouble keeps 64-bit vertex and
  // triangle totals intact where toInt would clip them.
  const auto count = [&msg](const char * key) {
    const QJsonValue v = msg.value(QLatin1String(key));
    return v.isDouble() ? (int)v.toDouble() : -1;
  };
  const auto count64 = [&msg](const char * key) {
    const QJsonValue v = msg.value(QLatin1String(key));
    return v.isDouble() ? (long long)v.toDouble() : -1LL;
  };

  MapObjectReport r;
  r.fileDataID = msg.value("fileDataID").toInt(0);
  r.load = msg.value("load").toInt(0);
  r.status = msg.value("status").toString();
  r.reason = msg.value("reason").toString();
  r.groups = count("groups");
  r.groupFilesRequested = count("groupFilesRequested");
  r.groupFilesMissing = count("groupFilesMissing");
  r.batches = count("batches");
  r.submeshes = count("submeshes");
  r.renderers = count("renderers");
  r.materials = count("materials");
  r.provisionalMaterials = count("provisionalMaterials");
  r.unresolvedMaterials = count("unresolvedMaterials");
  r.blendedMaterials = count("blendedMaterials");
  r.texturesReferenced = count("texturesReferenced");
  r.texturesDecoded = count("texturesDecoded");
  r.texturesMissing = count("texturesMissing");
  r.vertices = count64("vertices");
  r.triangles = count64("triangles");
  const QJsonArray bmin = msg.value("boundsMin").toArray();
  const QJsonArray bmax = msg.value("boundsMax").toArray();
  r.hasBounds = bmin.size() == 3 && bmax.size() == 3;
  for (int i = 0; r.hasBounds && i < 3; i++)
  {
    r.boundsMin[i] = bmin.at(i).toDouble();
    r.boundsMax[i] = bmax.at(i).toDouble();
  }
  const QJsonObject timings = msg.value("timings").toObject();
  const auto timing = [&timings](const char * key) {
    const QJsonValue v = timings.value(QLatin1String(key));
    return v.isDouble() ? v.toDouble() : -1.0;
  };
  r.rootMs = timing("rootMs");
  r.groupsMs = timing("groupsMs");
  r.texturesMs = timing("texturesMs");
  r.buildMs = timing("buildMs");
  r.totalMs = timing("totalMs");
  r.liveMapObjects = count("liveMapObjects");
  r.liveModels = count("liveModels");

  m_stats.mapObjectReports++;
  if (r.status == "built")
    m_stats.mapObjectBuilt++;
  else if (r.status == "failed")
    m_stats.mapObjectFailed++;
  else if (r.status == "superseded")
    m_stats.mapObjectSuperseded++;
  m_stats.lastMapObject = QString("%1 load %2 %3 %4 groups ").arg(r.fileDataID).arg(r.load).arg(r.status)
                            .arg(r.groups) + r.reason;
  if (r.status == "failed")
    LOG_ERROR << "[unityipc] <- mapObjectLoaded" << r.describe();
  else
    LOG_INFO << "[unityipc] <- mapObjectLoaded" << r.describe();
  if (onMapObjectLoaded)
    onMapObjectLoaded(r);
}

QString UnityIpcServer::RuntimeState::describe() const
{
  // The player's key is appended, never passed through arg().
  return QString("query=%1 liveMapObjects=%2 liveModels=%3 modelFileDataID=%4 mapObjectFileDataID=%5 loading=%6 "
                 "mountFileDataID=%7")
           .arg(query).arg(liveMapObjects).arg(liveModels).arg(modelFileDataID).arg(mapObjectFileDataID)
           .arg(loading ? "true" : "false").arg(mountFileDataID) +
         " mountKey=\"" + mountKey + "\"" +
         QString(" liveMounts=%1 mountsBuilt=%2 mountSeat=%3 mountSeatBone=%4 modelSequence=%5 mountSequence=%6")
           .arg(liveMounts).arg(mountsBuilt).arg(mountSeat).arg(mountSeatBone).arg(modelSequence).arg(mountSequence) +
         QString(" mountEmitters=%1 mountRibbons=%2 mountParticles=%3 bodyRebinds=%4 viewFramings=%5 keptViews=%6")
           .arg(mountEmitters).arg(mountRibbons).arg(mountParticles).arg(bodyRebinds).arg(viewFramings).arg(keptViews) +
         QString(" background=%1,%2,%3").arg(backgroundR).arg(backgroundG).arg(backgroundB) +
         QString(" assetCacheEntries=%1 assetCacheBytes=%2 assetCacheHits=%3 assetCacheJoins=%4 assetCacheEvictions=%5"
                 " assetEpoch=%6")
           .arg(assetCacheEntries).arg(assetCacheBytes).arg(assetCacheHits).arg(assetCacheJoins).arg(assetCacheEvictions)
           .arg(assetEpoch) +
         QString(" liveTerrainTiles=%1 terrainLoad=%2 terrainLoading=%3 terrainView=%4 cameraMask=%5 meshObjects=%6"
                 " materialObjects=%7")
           .arg(liveTerrainTiles).arg(terrainLoad).arg(terrainLoading).arg(terrainView)
           .arg(hasCameraMask ? QString::number(cameraMask) : QString("-")).arg(meshObjects).arg(materialObjects) +
         QString(" terrainMeshes=%1 terrainMaterials=%2 terrainGameObjects=%3")
           .arg(terrainMeshes).arg(terrainMaterials).arg(terrainGameObjects) +
         QString(" terrainPendingRequests=%1 terrainRetiredRequests=%2 mapAnswersRouted=%3 mapAnswersDropped=%4"
                 " terrainTimeoutMs=%5 terrainRetiredTotal=%6 mapAnswersLate=%7 mapAnswersUnowned=%8")
           .arg(terrainPendingRequests).arg(terrainRetiredRequests).arg(mapAnswersRouted).arg(mapAnswersDropped)
           .arg(terrainTimeoutMs).arg(terrainRetiredTotal).arg(mapAnswersLate).arg(mapAnswersUnowned) +
         " terrainLastRetiredRequest=" + (hasTerrainLastRetiredRequest ? "\"" + terrainLastRetiredRequest + "\"" : QString("-")) +
         QString(" terrainTextures=%1 terrainTextureBytes=%2 terrainAlphaArrays=%3")
           .arg(terrainTextures).arg(terrainTextureBytes).arg(terrainAlphaArrays);
}

int UnityIpcServer::requestRuntimeState()
{
  if (!playerDrawsMapObjects())
    return 0;
  QJsonObject msg;
  msg["type"] = "runtimeState";
  msg["query"] = ++m_runtimeQuery;
  LOG_INFO << "[unityipc] -> runtimeState query=" << m_runtimeQuery;
  queueJson(msg);
  return m_runtimeQuery;
}

void UnityIpcServer::handleRuntimeState(const QJsonObject & msg)
{
  const auto count = [&msg](const char * key) {
    const QJsonValue v = msg.value(QLatin1String(key));
    return v.isDouble() ? (int)v.toDouble() : -1;
  };
  RuntimeState s;
  s.query = msg.value("query").toInt(0);
  s.liveMapObjects = count("liveMapObjects");
  s.liveModels = count("liveModels");
  s.modelFileDataID = count("modelFileDataID");
  s.mapObjectFileDataID = count("mapObjectFileDataID");
  s.loading = msg.value("loading").toBool(false);
  s.mountFileDataID = count("mountFileDataID");
  s.mountKey = msg.value("mountKey").toString();
  s.liveMounts = count("liveMounts");
  s.mountsBuilt = count("mountsBuilt");
  s.mountSeat = count("mountSeat");
  s.mountSeatBone = count("mountSeatBone");
  s.modelSequence = count("modelSequence");
  s.mountSequence = count("mountSequence");
  s.mountEmitters = count("mountEmitters");
  s.mountRibbons = count("mountRibbons");
  s.mountParticles = count("mountParticles");
  s.bodyRebinds = count("bodyRebinds");
  s.viewFramings = count("viewFramings");
  s.keptViews = count("keptViews");
  s.backgroundR = count("backgroundR");
  s.backgroundG = count("backgroundG");
  s.backgroundB = count("backgroundB");
  s.assetCacheEntries = count("assetCacheEntries");
  s.assetCacheBytes = count("assetCacheBytes");
  s.assetCacheHits = count("assetCacheHits");
  s.assetCacheJoins = count("assetCacheJoins");
  s.assetCacheEvictions = count("assetCacheEvictions");
  s.assetEpoch = count("assetEpoch");
  // Protocol 12. The two flags read -1 when absent, 0 or 1 when sent.
  const auto flag = [&msg](const char * key) {
    const QJsonValue v = msg.value(QLatin1String(key));
    return v.isBool() ? (v.toBool() ? 1 : 0) : -1;
  };
  s.liveTerrainTiles = count("liveTerrainTiles");
  s.terrainLoad = count("terrainLoad");
  s.terrainLoading = flag("terrainLoading");
  s.terrainView = flag("terrainView");
  const QJsonValue mask = msg.value("cameraMask");
  s.hasCameraMask = mask.isDouble();
  s.cameraMask = s.hasCameraMask ? (long long)mask.toDouble() : 0;
  s.meshObjects = count("meshObjects");
  s.materialObjects = count("materialObjects");
  s.terrainMeshes = count("terrainMeshes");
  s.terrainMaterials = count("terrainMaterials");
  s.terrainGameObjects = count("terrainGameObjects");
  s.terrainPendingRequests = count("terrainPendingRequests");
  s.terrainRetiredRequests = count("terrainRetiredRequests");
  s.mapAnswersRouted = count("mapAnswersRouted");
  s.mapAnswersDropped = count("mapAnswersDropped");
  s.terrainTimeoutMs = count("terrainTimeoutMs");
  s.terrainRetiredTotal = count("terrainRetiredTotal");
  s.mapAnswersLate = count("mapAnswersLate");
  s.mapAnswersUnowned = count("mapAnswersUnowned");
  // A text: "" (no request retired yet) is sent, not absent.
  s.hasTerrainLastRetiredRequest = msg.contains(QLatin1String("terrainLastRetiredRequest"));
  s.terrainLastRetiredRequest = msg.value(QLatin1String("terrainLastRetiredRequest")).toString();
  // Protocol 13 (TERRAIN TEXTURES). toDouble keeps a byte count past 2^31 intact.
  s.terrainTextures = count("terrainTextures");
  const QJsonValue textureBytes = msg.value(QLatin1String("terrainTextureBytes"));
  s.terrainTextureBytes = textureBytes.isDouble() ? (long long)textureBytes.toDouble() : -1LL;
  s.terrainAlphaArrays = count("terrainAlphaArrays");
  LOG_INFO << "[unityipc] <- runtimeState" << s.describe();
  if (onRuntimeState)
    onRuntimeState(s);
}

QString UnityIpcServer::ScreenshotResult::describe() const
{
  // The path and the player's error are appended, never passed through arg().
  return QString("request=%1 ok=%2 %3x%4 bytes=%5 renderMs=%6 encodeMs=%7 writeMs=%8 totalMs=%9")
           .arg(request).arg(ok ? "true" : "false").arg(width).arg(height).arg(bytes)
           .arg(renderMs, 0, 'f', 1).arg(encodeMs, 0, 'f', 1).arg(writeMs, 0, 'f', 1).arg(totalMs, 0, 'f', 1) +
         " path=\"" + path + "\"" + (error.isEmpty() ? QString() : " error=\"" + error + "\"");
}

int UnityIpcServer::requestScreenshot(const QString & path, int width, int height)
{
  if (!playerTakesScreenshots())
    return 0;
  QJsonObject msg;
  msg["type"] = "captureScreenshot";
  msg["request"] = ++m_screenshotRequest;
  msg["path"] = path;
  msg["width"] = width;
  msg["height"] = height;
  LOG_INFO << "[unityipc] -> captureScreenshot request=" << m_screenshotRequest << width << "x" << height << "path" << path;
  queueJson(msg);
  return m_screenshotRequest;
}

bool UnityIpcServer::sendPrefetchAssets(const std::vector<int> & fileDataIDs)
{
  if (!playerCachesAssets() || fileDataIDs.empty())
    return false;
  QJsonArray ids;
  QStringList names;
  for (int id : fileDataIDs)
    if (id > 0)
    {
      ids.append(id);
      names << QString::number(id);
    }
  if (ids.isEmpty())
    return false;
  QJsonObject msg;
  msg["type"] = "prefetchAssets";
  msg["assetEpoch"] = UnityAssetAccess::clientEpoch();
  msg["fileDataIDs"] = ids;
  LOG_INFO << "[unityipc] -> prefetchAssets" << names.join(' ') << "epoch" << UnityAssetAccess::clientEpoch();
  queueJson(msg);
  return true;
}

bool UnityIpcServer::sendViewportBackground(int r, int g, int b)
{
  if (!playerPaintsBackground())
    return false;
  if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255)
  {
    LOG_ERROR << "[unityipc] viewportBackground not sent, a channel is outside 0..255:" << r << g << b;
    return false;
  }
  QJsonObject msg;
  msg["type"] = "viewportBackground";
  msg["r"] = r;
  msg["g"] = g;
  msg["b"] = b;
  m_stats.backgroundPushes++;
  m_stats.lastBackground = QString::asprintf("#%02X%02X%02X", r, g, b);
  LOG_INFO << "[unityipc] -> viewportBackground" << m_stats.lastBackground;
  queueJson(msg);
  return true;
}

void UnityIpcServer::handleScreenshotSaved(const QJsonObject & msg)
{
  const auto number = [&msg](const char * key) {
    const QJsonValue v = msg.value(QLatin1String(key));
    return v.isDouble() ? v.toDouble() : -1.0;
  };
  ScreenshotResult r;
  r.request = msg.value("request").toInt(0);
  r.ok = msg.value("ok").toBool(false);
  r.error = msg.value("error").toString();
  r.path = msg.value("path").toString();
  r.width = (int)number("width");
  r.height = (int)number("height");
  r.bytes = (long long)number("bytes");
  r.renderMs = number("renderMs");
  r.encodeMs = number("encodeMs");
  r.writeMs = number("writeMs");
  r.totalMs = number("totalMs");
  if (r.ok)
    LOG_INFO << "[unityipc] <- screenshotSaved" << r.describe();
  else
    LOG_ERROR << "[unityipc] <- screenshotSaved" << r.describe();
  if (onScreenshotSaved)
    onScreenshotSaved(r);
}

void UnityIpcServer::handleGeosetsApplied(const QJsonObject & msg)
{
  GeosetAck ack;
  ack.fileDataID = msg.value("fileDataID").toInt();
  ack.revision = msg.value("revision").toInt();
  ack.status = msg.value("status").toString();
  ack.reason = msg.value("reason").toString();
  ack.triangles = msg.value("triangles").toInt();
  ack.animTimeMs = (long long)msg.value("animTimeMs").toDouble();
  if (msg.value("submeshVisible").isArray())
  {
    ack.hasVisible = true;
    const QJsonArray bits = msg.value("submeshVisible").toArray();
    for (int i = 0; i < bits.size(); i++)
      ack.visible.push_back(bits.at(i).toInt() != 0);
  }
  m_stats.geosetAcks++;
  if (ack.status == "rejected")
    m_stats.geosetRejects++;
  m_stats.lastGeosetAck = QString("rev %1 %2%3").arg(ack.revision).arg(ack.status)
                            .arg(ack.reason.isEmpty() ? QString() : " (" + ack.reason + ")");
  LOG_INFO << "[unityipc] <- modelGeosetsApplied fileDataID=" << ack.fileDataID << "revision=" << ack.revision
           << "status=" << ack.status << "triangles=" << ack.triangles << "animTimeMs=" << (qlonglong)ack.animTimeMs
           << (ack.reason.isEmpty() ? QString() : "reason=" + ack.reason);
  if (onGeosetsApplied)
    onGeosetsApplied(ack);
}

void UnityIpcServer::addParticleColor(QJsonObject & msg, int m2FileDataID)
{
  UnityAssetAccess::ParticleColorSet set;
  QString error;
  if (!UnityAssetAccess::resolveParticleColor(m2FileDataID, set, error))
    return;   // no override is the common case, and is not an error worth a field

  // [start.r,start.g,start.b, mid.r,..., end.r,...] as 0..255, the order the DBC stores them and
  // the order an emitter's ParticleColorIndex of 11 / 12 / 13 selects from.
  QJsonArray arr;
  for (int stop = 0; stop < 3; stop++)
    for (int ch = 0; ch < 3; ch++)
      arr.append(set.rgb[stop][ch]);
  msg["particleColor"] = arr;
  msg["particleColorId"] = set.id;
}

void UnityIpcServer::sendModelSkin(int m2FileDataID)
{
  if (!m_client || !m_unityReady || m2FileDataID <= 0)
    return;

  std::vector<UnityAssetAccess::ModelTexture> textures;
  QString error;
  if (!UnityAssetAccess::resolveModelTextures(m2FileDataID, textures, error))
  {
    // NO TEXTURE IS NOT NO ANSWER. A model with no creature display and no conventional skin
    // (felreavergolem, the cinematic models) resolves no texture at all -- but it still has
    // geosets the application is displaying, and the player needs them or it falls back to
    // "geoset 0 only" and drops submeshes the host displays. The message goes out with
    // an empty texture list; the player leaves its textures alone and takes the geometry.
    LOG_INFO << "[unityipc] modelSkin for" << m2FileDataID
             << "carries geosets only:" << error;
    textures.clear();
  }

  QJsonObject msg;
  msg["type"] = "modelSkin";
  msg["ok"] = true;              // same shape as a modelTextures reply, so one reader handles both
  msg["fileDataID"] = m2FileDataID;
  msg["textures"] = textureArray(textures);
  addGeosets(msg, m2FileDataID);
  addParticleColor(msg, m2FileDataID);
  m_stats.skinPushes++;
  // A model can resolve NO texture and still have something to say -- the geosets its display
  // switches on, and its particle colour -- so the message goes out either way, and nothing
  // here may index an empty list. (It used to read textures[0] unconditionally.)
  if (textures.empty())
  {
    m_stats.lastSkin = QString("no texture (geosets only)");
    LOG_INFO << "[unityipc] -> modelSkin fileDataID=" << m2FileDataID
             << "textures= 0 (geosets and particle colour only)";
  }
  else
  {
    m_stats.lastSkin = QString("%1 (%2)").arg(textures[0].fileDataID)
                                         .arg(UnityAssetAccess::sourceName(textures[0].source));
    LOG_INFO << "[unityipc] -> modelSkin fileDataID=" << m2FileDataID << "textures=" << (int)textures.size()
             << "source=" << UnityAssetAccess::sourceName(textures[0].source)
             << "first=" << textures[0].fileDataID;
  }
  queueJson(msg);
}

void UnityIpcServer::sendModelAnimation(int m2FileDataID, int sequenceIndex, int animID,
                                        int durationMs, bool loop, const QString & role, int load)
{
  if (!m_client || !m_unityReady || sequenceIndex < 0)
    return;

  QJsonObject msg;
  msg["type"] = "modelAnimation";
  msg["fileDataID"] = m2FileDataID;
  msg["sequenceIndex"] = sequenceIndex;
  msg["animID"] = animID;
  msg["durationMs"] = durationMs;
  msg["loop"] = loop;
  // A ridden mount's two models (protocol 5): which one the selection changed. Not sent about any other model.
  if (!role.isEmpty())
  {
    msg["role"] = role;
    msg["load"] = load;
    m_stats.rolePushes++;
  }
  m_stats.animPushes++;
  m_stats.lastAnimation = QString("seq %1 animID %2 %3ms").arg(sequenceIndex).arg(animID).arg(durationMs);
  if (role.isEmpty())
    LOG_INFO << "[unityipc] -> modelAnimation fileDataID=" << m2FileDataID
             << "sequenceIndex=" << sequenceIndex << "animID=" << animID << "durationMs=" << durationMs;
  else
    LOG_INFO << "[unityipc] -> modelAnimation fileDataID=" << m2FileDataID
             << "sequenceIndex=" << sequenceIndex << "animID=" << animID << "durationMs=" << durationMs
             << "role=" << role << "load=" << load;
  queueJson(msg);
}

void UnityIpcServer::sendModelAnimationState(int m2FileDataID, int sequenceIndex, bool playing,
                                             int timeMs, float speed, bool loop, bool explicitState,
                                             const RiderState * rider, int load)
{
  if (!m_client || !m_unityReady || sequenceIndex < 0)
    return;

  QJsonObject msg;
  msg["type"] = "modelAnimationState";
  msg["fileDataID"] = m2FileDataID;
  msg["sequenceIndex"] = sequenceIndex;
  msg["playing"] = playing;
  msg["timeMs"] = timeMs;
  msg["speed"] = speed;
  msg["loop"] = loop;
  msg["explicitState"] = explicitState;
  // When these positions were sampled, on the clock the player reads too. The line can wait behind other traffic
  // before the player reads it -- a composited body image is megabytes, read and decoded first -- and a position
  // applied as sent after such a wait put the player back by the wait.
  msg["sampledAtMs"] = performanceCounterMs();
  // A ridden mount (protocol 5): the rider's clock, sampled in the same call as the mount's above. "hasRider"
  // says it is there; a nested object's absence is not something every reader can tell apart from defaults.
  if (rider)
  {
    QJsonObject r;
    r["sequenceIndex"] = rider->sequenceIndex;
    r["playing"] = rider->playing;
    r["timeMs"] = rider->timeMs;
    r["speed"] = rider->speed;
    r["loop"] = rider->loop;
    msg["load"] = load;
    msg["hasRider"] = true;
    msg["rider"] = r;
    m_stats.riderStatePushes++;
    m_stats.lastRiderState = QString("seq %1 %2 %3ms x%4").arg(rider->sequenceIndex)
                               .arg(rider->playing ? "playing" : "paused").arg(rider->timeMs)
                               .arg(rider->speed, 0, 'f', 2);
  }
  m_stats.statePushes++;
  m_stats.lastState = QString("%1 %2ms x%3").arg(playing ? "playing" : "paused")
                                            .arg(timeMs).arg(speed, 0, 'f', 2);
  // Deliberately not logged per push: the heartbeat would bury everything else in the log. The
  // counter and lastState above are what a diagnostic run reports.
  queueJson(msg);
}

void UnityIpcServer::sendLoadWoWModel(const QString & path, int fileDataID, const QString & client,
                                      bool character, int load, const QString & kind, bool keepView)
{
  QJsonObject msg;
  msg["type"] = "loadWoWModel";
  msg["path"] = UnityAssetAccess::normalizePath(path);
  msg["fileDataID"] = fileDataID;
  msg["client"] = client;
  msg["character"] = character;
  msg["load"] = load;
  // Always stated. A player older than protocol 4 ignores the field (its message reader skips names it
  // does not know), which is harmless for "m2": that is what it assumes. "wmo" never goes to one.
  msg["kind"] = kind;
  if (keepView)
    msg["keepView"] = true;
  // Always stated (protocol 9): the player keeps asset files under it. An older player skips the name.
  msg["assetEpoch"] = UnityAssetAccess::clientEpoch();
  if (kind == "wmo")
    m_stats.mapObjectLoads++;
  LOG_INFO << "[unityipc] -> loadWoWModel path=" << msg["path"].toString() << "fileDataID=" << fileDataID
           << "load=" << load << "kind=" << kind << (character ? "(character)" : "") << (keepView ? "(keep view)" : "");
  queueJson(msg);
}

QString UnityIpcServer::shareCharacterImage(const QString & kind, const QImage & image)
{
  if (image.isNull())
    return QString();

  // The same pixels as last time under this kind: the player has them. A refresh composes a NEW
  // image every time, so pointer identity is only the fast path; equal contents are compared too,
  // and only a real change is re-sent -- the body is 8 MB of pixels.
  auto sent = m_sentImages.find(kind);
  if (sent != m_sentImages.end() && m_sentImageIds.count(kind))
  {
    const QImage & previous = sent->second;
    if (previous.cacheKey() == image.cacheKey())
      return m_sentImageIds[kind];
    if (previous.size() == image.size() && previous.format() == image.format() &&
        previous.sizeInBytes() == image.sizeInBytes() &&
        memcmp(previous.constBits(), image.constBits(), (size_t)image.sizeInBytes()) == 0)
    {
      // Keep THIS image in its place: a build names the body several times (its own slot, merged
      // parts that sample the composite, their hand passes), and every later build does too, and each
      // would otherwise compare all 8 MB again rather than match on the key.
      sent->second = image;
      return m_sentImageIds[kind];
    }
  }

  // 32 bits per pixel, no row padding, rows top first: exactly what the OpenGL upload handed the
  // driver as GL_BGRA_EXT. Anything else is converted to that layout rather than sent as is.
  QImage pixels = image;
  if (pixels.depth() != 32 || pixels.bytesPerLine() != pixels.width() * 4)
    pixels = pixels.convertToFormat(QImage::Format_ARGB32);

  const QString id = QString("%1-%2").arg(kind).arg(++m_imageSerial);
  const QByteArray raw = QByteArray::fromRawData((const char *)pixels.constBits(), pixels.width() * pixels.height() * 4);
  if (playerTakesBinaryFrames())
  {
    // Protocol 10: the pixels follow the line as they are (queueFrame copies them into the send buffer at once).
    QByteArray frameHead;
    frameHead.append("{\"type\":\"characterImage\",\"hash\":\"");
    frameHead.append(id.toLatin1());
    frameHead.append("\",\"kind\":\"");
    frameHead.append(kind.toLatin1());
    frameHead.append("\",\"width\":");
    frameHead.append(QByteArray::number(pixels.width()));
    frameHead.append(",\"height\":");
    frameHead.append(QByteArray::number(pixels.height()));
    frameHead.append(",\"format\":\"bgra8\",\"encoding\":\"binary\",\"payloadBytes\":");
    frameHead.append(QByteArray::number(raw.size()));
    frameHead.append("}");
    queueFrame(frameHead, raw);
    m_sentImages[kind] = image;
    m_sentImageIds[kind] = id;
    m_stats.imagePushes++;
    m_stats.imageBytes += raw.size();
    LOG_INFO << "[unityipc] -> characterImage" << id << pixels.width() << "x" << pixels.height()
             << "format" << (int)image.format() << "binary bytes" << raw.size();
    return id;
  }
  const QByteArray data = raw.toBase64();
  // Written out by hand rather than through QJsonDocument: the payload is a single ~11 MB string,
  // and a JSON document would copy it twice more for nothing. The base64 goes into the send buffer as
  // its own piece, not joined into a line first, for the same reason.
  QByteArray head;
  head.append("{\"type\":\"characterImage\",\"hash\":\"");
  head.append(id.toLatin1());
  head.append("\",\"kind\":\"");
  head.append(kind.toLatin1());
  head.append("\",\"width\":");
  head.append(QByteArray::number(pixels.width()));
  head.append(",\"height\":");
  head.append(QByteArray::number(pixels.height()));
  head.append(",\"format\":\"bgra8\",\"encoding\":\"base64\",\"data\":\"");
  queueLineParts({ head, data, QByteArray("\"}") });

  m_sentImages[kind] = image;
  m_sentImageIds[kind] = id;
  m_stats.imagePushes++;
  m_stats.imageBytes += raw.size();
  LOG_INFO << "[unityipc] -> characterImage" << id << pixels.width() << "x" << pixels.height()
           << "format" << (int)image.format() << "base64 bytes" << data.size();
  return id;
}

bool UnityIpcServer::sendCharacterScene(int m2FileDataID, int revision, const QJsonObject & scene)
{
  if (!playerDressesCharacters() || m2FileDataID <= 0)
    return false;
  QJsonObject msg = scene;
  msg["type"] = "characterScene";
  msg["fileDataID"] = m2FileDataID;
  msg["revision"] = revision;
  m_stats.scenePushes++;
  m_stats.lastScene = QString("rev %1: %2 merged, %3 attached")
                        .arg(revision).arg(scene.value("merged").toArray().size())
                        .arg(scene.value("attachments").toArray().size());
  if (scene.contains("mount"))
  {
    // A ridden mount (protocol 5; the caller sends one only to a player that rides mounts).
    const QJsonObject mount = scene.value("mount").toObject();
    const QJsonArray position = mount.value("position").toArray();
    m_stats.mountScenes++;
    m_stats.lastMountScene = QString("%1 %2 bone %3 seq %4 rider seq %5").arg(mount.value("key").toString())
                               .arg(mount.value("fileDataID").toInt()).arg(mount.value("bone").toInt())
                               .arg(mount.value("sequenceIndex").toInt()).arg(mount.value("riderSequenceIndex").toInt());
    LOG_INFO << "[unityipc] -> characterScene fileDataID=" << m2FileDataID << m_stats.lastScene << "| mount"
             << mount.value("key").toString() << "fileDataID=" << mount.value("fileDataID").toInt()
             << "attachmentId=" << mount.value("attachmentId").toInt() << "bone=" << mount.value("bone").toInt()
             << "position=" << position.at(0).toDouble() << position.at(1).toDouble() << position.at(2).toDouble()
             << "riderScale=" << mount.value("riderScale").toDouble()
             << "textures=" << mount.value("textures").toArray().size()
             << "submeshes=" << mount.value("submeshCount").toInt()
             << "particleColorSets=" << (mount.contains("particleColorSets") ? 1 : 0)
             << "sequenceIndex=" << mount.value("sequenceIndex").toInt()
             << "riderSequenceIndex=" << mount.value("riderSequenceIndex").toInt();
  }
  else
    LOG_INFO << "[unityipc] -> characterScene fileDataID=" << m2FileDataID << m_stats.lastScene;
  queueJson(msg);
  return true;
}

void UnityIpcServer::handleLine(const std::string & line)
{
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(line.data(), (int)line.size()), &err);
  if (doc.isNull() || !doc.isObject())
  {
    LOG_ERROR << "[unityipc] bad JSON from player:" << err.errorString() << "--" << QString::fromStdString(line.substr(0, 200));
    return;
  }
  const QJsonObject msg = doc.object();
  const QString type = msg.value("type").toString();

  if (type == "unityReady")
  {
    const int version = msg.value("protocolVersion").toInt(0);
    m_unityReady = true;
    m_playerProtocol = version;
    LOG_INFO << "[unityipc] <- unityReady (protocolVersion" << version << ")";
    // An older player is supported for what it can do -- the host checks the version before every
    // feature that needs a newer one and shows the out-of-date notice instead -- so it is worth a
    // warning, not an error. A newer or unknown one is.
    if (version > 0 && version < PROTOCOL_VERSION)
      LOG_WARNING << "[unityipc] player speaks protocol v" << version << ", older than WMV's v" << PROTOCOL_VERSION
                  << "-- what it cannot do (mounted characters below v5, world models below v4, characters below v3)"
                     " gets a notice, a screenshot (below v6) a status message, the viewport background (below v7)"
                     " stays the player's own default, a character's model generation (below v8) is not switched,"
                     " every load (below v9) fetches its files again, files travel as base64 text (below v10), an NPC on its"
                     " own model is shown without its weapons (below v11), no terrain tile is drawn (below v12), and a"
                     " terrain tile is drawn without its textures (below v13)";
    else if (version != PROTOCOL_VERSION)
      LOG_ERROR << "[unityipc] player speaks protocol v" << version << "but WMV expects v" << PROTOCOL_VERSION;
    if (onUnityReady)
      onUnityReady();
  }
  else if (type == "getAsset")
  {
    handleGetAsset(msg, false);
  }
  else if (type == "getAssetByFileDataID")
  {
    handleGetAsset(msg, true);
  }
  else if (type == "getModelTextures")
  {
    handleGetModelTextures(msg);
  }
  else if (type == "modelGeosetsApplied")
  {
    handleGeosetsApplied(msg);
  }
  else if (type == "characterSceneApplied")
  {
    handleCharacterSceneApplied(msg);
  }
  else if (type == "mapObjectLoaded")
  {
    handleMapObjectLoaded(msg);
  }
  else if (type == "runtimeState")
  {
    handleRuntimeState(msg);
  }
  else if (type == "screenshotSaved")
  {
    handleScreenshotSaved(msg);
  }
  else if (type == "getMapAsset")
  {
    handleGetMapAsset(msg);
  }
  else if (type == "terrainTileLoaded")
  {
    handleTerrainTileLoaded(msg);
  }
  else
  {
    LOG_ERROR << "[unityipc] unknown message type from player:" << type;
    if (msg.contains("requestId"))
    {
      QJsonObject resp;
      resp["type"] = "assetResponse";
      resp["requestId"] = msg.value("requestId");
      resp["ok"] = false;
      resp["error"] = QString("unknown message type '%1'").arg(type);
      queueJson(resp);
    }
  }
}

void UnityIpcServer::handleGetModelTextures(const QJsonObject & msg)
{
  const QString requestId = msg.value("requestId").toString();
  const int fdid = msg.value("fileDataID").toInt(0);
  m_stats.requests++;
  m_stats.lastRequest = QString("modelTextures %1").arg(fdid);
  LOG_INFO << "[unityipc] <- getModelTextures" << requestId << "fileDataID=" << fdid;

  std::vector<UnityAssetAccess::ModelTexture> textures;
  QString error;
  const bool ok = UnityAssetAccess::resolveModelTextures(fdid, textures, error);

  QJsonObject resp;
  resp["type"] = "modelTextures";
  resp["requestId"] = requestId;
  resp["ok"] = ok;
  resp["fileDataID"] = fdid;
  if (ok)
  {
    resp["textures"] = textureArray(textures);
    addGeosets(resp, fdid);
    addParticleColor(resp, fdid);
    m_stats.responsesOk++;
    LOG_INFO << "[unityipc] -> modelTextures" << requestId << "resolved" << (int)textures.size()
             << "texture(s) from" << UnityAssetAccess::sourceName(textures.empty()
                  ? UnityAssetAccess::ModelTexture::Database : textures[0].source);
  }
  else
  {
    resp["error"] = error;
    m_stats.responsesError++;
    m_stats.lastError = error;
    LOG_ERROR << "[unityipc] -> modelTextures" << requestId << "error:" << error;
  }
  queueJson(resp);
}

void UnityIpcServer::handleGetAsset(const QJsonObject & msg, bool byFileDataID)
{
  const QString requestId = msg.value("requestId").toString();
  m_stats.requests++;

  UnityAssetAccess::Result result;
  if (byFileDataID)
  {
    const int fdid = msg.value("fileDataID").toInt(0);
    m_stats.lastRequest = QString("fileDataID %1").arg(fdid);
    LOG_INFO << "[unityipc] <- getAssetByFileDataID" << requestId << "fileDataID=" << fdid;
    result = UnityAssetAccess::readByFileDataID(fdid);
  }
  else
  {
    const QString path = msg.value("path").toString();
    m_stats.lastRequest = UnityAssetAccess::normalizePath(path);
    LOG_INFO << "[unityipc] <- getAsset" << requestId << "path=" << m_stats.lastRequest;
    result = UnityAssetAccess::readByPath(path);
  }
  m_stats.lastProvider = result.provider;

  QJsonObject resp;
  resp["type"] = "assetResponse";
  resp["requestId"] = requestId;
  resp["ok"] = result.ok;
  if (!result.path.isEmpty())
    resp["path"] = result.path;
  if (result.fileDataID > 0)
    resp["fileDataID"] = result.fileDataID;

  if (result.ok && playerTakesBinaryFrames())
  {
    // Protocol 10: the file follows the line as it is -- no base64, no copy into a JSON document, and no SHA-1 (only
    // a log ever read it, and it cost more than the rest of the answer).
    resp["byteLength"] = result.data.size();
    resp["encoding"] = "binary";
    resp["payloadBytes"] = result.data.size();
    if (result.fromClientStorage)
      resp["cacheable"] = true;
    m_stats.responsesOk++;
    m_stats.bytesServed += result.data.size();
    m_stats.lastError.clear();
    LOG_INFO << "[unityipc] -> assetResponse" << requestId << "ok provider=" << result.provider
             << "bytes=" << result.data.size() << "(binary)";
    queueFrame(QJsonDocument(resp).toJson(QJsonDocument::Compact), result.data);
    return;
  }
  if (result.ok)
  {
    const QByteArray sha1 = QCryptographicHash::hash(result.data, QCryptographicHash::Sha1).toHex();
    resp["byteLength"] = result.data.size();
    resp["sha1"] = QString::fromLatin1(sha1);
    resp["encoding"] = "base64";   // a player before protocol 10: base64 in the JSON line, with a SHA-1 for the log
    resp["data"] = QString::fromLatin1(result.data.toBase64());
    // A file of the client's own storage (protocol 9): the player may keep it for as long as the epoch it asked under.
    if (result.fromClientStorage)
      resp["cacheable"] = true;
    m_stats.responsesOk++;
    m_stats.bytesServed += result.data.size();
    m_stats.lastError.clear();
    LOG_INFO << "[unityipc] -> assetResponse" << requestId << "ok provider=" << result.provider
             << "bytes=" << result.data.size() << "sha1=" << QString::fromLatin1(sha1);
  }
  else
  {
    resp["error"] = result.error;
    m_stats.responsesError++;
    m_stats.lastError = result.error;
    LOG_ERROR << "[unityipc] -> assetResponse" << requestId << "error provider=" << result.provider
              << ":" << result.error;
  }
  queueJson(resp);
}

// ---------------------------------------------------------------- terrain (protocol 12)

bool UnityIpcServer::sendLoadTerrainTile(const TerrainTileCommand & c)
{
  if (!playerDrawsTerrain() || c.load <= 0)
    return false;
  QJsonObject msg;
  msg["type"] = "loadTerrainTile";
  msg["load"] = c.load;
  msg["assetEpoch"] = c.assetEpoch;
  msg["mapId"] = c.mapId;
  msg["mapDirectory"] = c.mapDirectory;
  msg["product"] = c.product;
  msg["build"] = c.build;
  msg["wdtFileDataID"] = c.wdtFileDataID;
  msg["wdtResolvedBy"] = c.wdtResolvedBy;
  if (c.wdtResolvedBy == "path")
    msg["wdtPath"] = c.wdtPath;
  msg["tileX"] = c.tileX;
  msg["tileY"] = c.tileY;
  msg["maxAssetBytes"] = c.maxAssetBytes;
  // TERRAIN TEXTURES (protocol 13): only to a player that takes them, and only when the load offers them. A protocol-13
  // player that finds no maxTextureBytes asks for no tex0 or texture (a protocol-12 host).
  const bool textures = playerDrawsTerrainTextures() && c.maxTextureBytes > 0;
  if (textures)
  {
    msg["maxTextureBytes"] = c.maxTextureBytes;
    msg["maxTextureBytesPerLoad"] = c.maxTextureBytesPerLoad;
    msg["maxTexturesPerLoad"] = c.maxTexturesPerLoad;
  }
  m_stats.terrainLoads++;
  LOG_INFO << "[unityipc] -> loadTerrainTile load=" << c.load << "epoch=" << c.assetEpoch << "map=" << c.mapId
           << c.mapDirectory << "tile" << QString("%1_%2").arg(c.tileX).arg(c.tileY) << "wdtFileDataID=" << c.wdtFileDataID
           << "resolved by" << c.wdtResolvedBy << (c.wdtResolvedBy == "path" ? c.wdtPath : QString())
           << "client" << c.product << c.build << "maxAssetBytes=" << c.maxAssetBytes
           << (textures ? QString("maxTextureBytes=%1 maxTextureBytesPerLoad=%2 maxTexturesPerLoad=%3")
                            .arg(c.maxTextureBytes).arg(c.maxTextureBytesPerLoad).arg(c.maxTexturesPerLoad)
                        : playerDrawsTerrainTextures() ? QString("no textures offered")
                                                       : QString("no textures (a protocol %1 player)").arg(m_playerProtocol));
  queueJson(msg);
  return true;
}

bool UnityIpcServer::sendClearTerrain(const QString & reason)
{
  if (!playerDrawsTerrain())
    return false;
  QJsonObject msg;
  msg["type"] = "clearTerrain";
  msg["reason"] = reason;
  m_stats.terrainClears++;
  LOG_INFO << "[unityipc] -> clearTerrain reason=" << reason;
  queueJson(msg);
  return true;
}

QString UnityIpcServer::MapAssetServed::describe() const
{
  // The request's id and the detail are appended, never passed through arg().
  return "requestId=" + requestId +
         QString(" load=%1 epoch=%2 (now %3) role=%4 fileDataID=%5 -> state=%6 bytes=%7 md5=%8 readMs=%9")
           .arg(load).arg(assetEpoch).arg(epochNow).arg(role).arg(fileDataID).arg(state).arg(bytes)
           .arg(md5.isEmpty() ? QString("-") : md5).arg(readMs < 0.0 ? QString("-") : QString::number(readMs, 'f', 3)) +
         QString(" handlerMs=%1 queuedMs=%2 bytesRead=%3 first=%4").arg(handlerMs, 0, 'f', 3)
           .arg(queuedMs < 0.0 ? QString("-") : QString::number(queuedMs, 'f', 3)).arg(bytesRead)
           .arg(!firstTracked ? "untracked" : first ? "yes" : "no") +
         (detail.isEmpty() ? QString() : " detail=\"" + detail + "\"");
}

void UnityIpcServer::recordMapAssetServed(const MapAssetServed & served)
{
  m_mapAssetsServedTotal++;
  m_mapAssetsServed.push_back(served);
  while (m_mapAssetsServed.size() > (size_t)MAP_ASSET_RECORDS_KEPT)
  {
    m_mapAssetsServed.pop_front();
    m_mapAssetRecordsDropped++;
  }
  if (onMapAssetServed)
    onMapAssetServed(served);
}

void UnityIpcServer::testHoldNextMapAsset(const QString & role)
{
  m_holdNextMapAsset = true;
  m_holdMapAssetRole = role;
  LOG_INFO << "[unityipc] test: the next getMapAsset" << (role.isEmpty() ? QString("of any role") : "of role " + role)
           << "is held unanswered";
}

void UnityIpcServer::testCancelMapAssetHold()
{
  m_holdNextMapAsset = false;
  m_holdMapAssetRole.clear();
}

int UnityIpcServer::testReleaseHeldMapAssets()
{
  std::vector<QJsonObject> held;
  held.swap(m_mapAssetsHeld);
  for (const QJsonObject & msg : held)
  {
    LOG_INFO << "[unityipc] test: getMapAsset" << msg.value("requestId").toString() << "released";
    answerMapAsset(msg);
  }
  return (int)held.size();
}

void UnityIpcServer::dropHeldMapAssets(const char * why)
{
  m_holdNextMapAsset = false;
  m_holdMapAssetRole.clear();
  // The queue goes with the connection too: its requests came over it, and no answer meant for one player may reach
  // another (map request ids start again in every player).
  if (!m_mapAssetQueue.empty())
  {
    LOG_INFO << "[unityipc]" << (int)m_mapAssetQueue.size() << "getMapAsset request(s) waiting dropped unanswered, the first"
             << m_mapAssetQueue.front().request.value("requestId").toString() << "load"
             << m_mapAssetQueue.front().request.value("load").toInt(0) << "(" << why << ")";
    m_mapAssetsQueueDropped += (long long)m_mapAssetQueue.size();
    m_mapAssetQueue.clear();
  }
  if (m_mapAssetsHeld.empty())
    return;
  for (const QJsonObject & msg : m_mapAssetsHeld)
    LOG_INFO << "[unityipc] test: held getMapAsset" << msg.value("requestId").toString() << "load"
             << msg.value("load").toInt(0) << "dropped unanswered (" << why << ")";
  m_mapAssetsHeldDropped += (long long)m_mapAssetsHeld.size();
  m_mapAssetsHeld.clear();
}

// One map file for the terrain load being built (TERRAIN above): held when the test hook says so, refused at once when
// MAP_ASSET_QUEUE_MAX wait already (nothing decided or read), otherwise queued to be served in its turn
// (serveMapAssetQueue, the same poll when the budget allows).
void UnityIpcServer::handleGetMapAsset(const QJsonObject & msg)
{
  m_stats.mapAssetRequests++;
  const QString role = msg.value("role").toString();
  if (m_holdNextMapAsset && (m_holdMapAssetRole.isEmpty() || m_holdMapAssetRole == role) &&
      m_mapAssetsHeld.size() < (size_t)MAP_ASSETS_HELD_MAX)
  {
    m_holdNextMapAsset = false;
    m_mapAssetsHeld.push_back(msg);
    LOG_INFO << "[unityipc] test: getMapAsset" << msg.value("requestId").toString() << "role" << role << "load"
             << msg.value("load").toInt(0) << "held unanswered (" << (int)m_mapAssetsHeld.size() << "held)";
    return;
  }
  if (m_mapAssetQueue.size() >= (size_t)MAP_ASSET_QUEUE_MAX)
  {
    m_mapAssetsRefusedQueueFull++;
    MapAssetAnswer refused;
    refused.state = "refused";
    refused.detail = QString("too many map requests waiting (%1 wait, at most %2)").arg(m_mapAssetQueue.size())
                       .arg(MAP_ASSET_QUEUE_MAX);
    answerMapAsset(msg, 0.0, &refused);
    return;
  }
  m_mapAssetQueue.push_back({msg, m_clock.nsecsElapsed()});
  m_mapAssetsQueued++;
  if ((int)m_mapAssetQueue.size() > m_mapAssetQueueDepthMax)
    m_mapAssetQueueDepthMax = (int)m_mapAssetQueue.size();
}

// THE MAP ASSET QUEUE: the requests waiting, first come first served, each decided, accounted and read only now
// (answerMapAsset). The poll stops serving once MAP_ASSET_POLL_BUDGET_BYTES were read in it or a file over
// MAP_ASSET_LARGE_FILE_BYTES was (a size is only known once the file is read: that file ends the poll's serving). Requests
// that read nothing cost the budget nothing. One record per poll that served any.
int UnityIpcServer::serveMapAssetQueue()
{
  if (m_mapAssetQueue.empty())
    return 0;
  QElapsedTimer timer;
  timer.start();
  MapAssetPoll poll;
  while (!m_mapAssetQueue.empty())
  {
    if (poll.bytesRead >= MAP_ASSET_POLL_BUDGET_BYTES || poll.largeFiles > 0)
    {
      poll.stoppedForBudget = true;
      break;
    }
    const QueuedMapAsset next = m_mapAssetQueue.front();
    m_mapAssetQueue.pop_front();
    const double queuedMs = (m_clock.nsecsElapsed() - next.arrivedNs) / 1e6;
    const long long read = answerMapAsset(next.request, queuedMs);
    poll.served++;
    poll.bytesRead += read;
    if (read > MAP_ASSET_LARGE_FILE_BYTES)
      poll.largeFiles++;
  }
  poll.serveMs = timer.nsecsElapsed() / 1e6;
  poll.waitingAfter = (int)m_mapAssetQueue.size();
  m_mapAssetPollsTotal++;
  m_mapAssetPolls.push_back(poll);
  while (m_mapAssetPolls.size() > (size_t)MAP_ASSET_POLL_TIMINGS_KEPT)
    m_mapAssetPolls.pop_front();
  if (poll.stoppedForBudget)
    LOG_INFO << "[unityipc] map requests: this poll served" << poll.served << "(" << poll.bytesRead << "bytes read in"
             << QString::number(poll.serveMs, 'f', 3) << "ms);" << poll.waitingAfter << "wait for the next poll";
  if (onMapAssetPoll)
    onMapAssetPoll(poll);
  return poll.served;
}

// The viewer decides and reads (onGetMapAsset), or the answer is given (refused on arrival); it goes back as any asset
// does, as a binary frame, with what the decision said. Timed from here to the answer queued (and, with room in the
// socket's buffer, sent): all of it is GUI-thread time.
long long UnityIpcServer::answerMapAsset(const QJsonObject & msg, double queuedMs, const MapAssetAnswer * given)
{
  QElapsedTimer handler;
  handler.start();
  MapAssetServed served;
  served.requestId = msg.value("requestId").toString();
  served.load = msg.value("load").toInt(0);
  served.assetEpoch = msg.value("assetEpoch").toInt(0);
  served.epochNow = UnityAssetAccess::clientEpoch();
  served.role = msg.value("role").toString();
  served.fileDataID = msg.value("fileDataID").toInt(0);
  served.queuedMs = queuedMs;

  MapAssetAnswer answer;
  if (given)
    answer = *given;
  else if (onGetMapAsset)
    answer = onGetMapAsset(msg);
  else
  {
    answer.state = "refused";
    answer.detail = "this viewer serves no map files";
  }
  served.bytesRead = answer.bytesRead;
  // Only a file served counts as the file's first read: a refused or stale request reads nothing, and an answer that
  // the file is not in the storage, not installed or unreadable serves nothing (its lookup is not a read to time).
  const bool ok = answer.state == "available";
  if (ok)
  {
    const MapAssetReadTracker::Read read = m_mapAssetReads.note(served.epochNow, served.fileDataID);
    served.first = read == MapAssetReadTracker::Read::First;
    served.firstTracked = read != MapAssetReadTracker::Read::Untracked;
  }

  // As every assetResponse, plus what the request named and what the host decided. Never cacheable: the player's
  // asset cache keeps the files of its model loads, and a map file is fetched for one terrain load.
  QJsonObject resp;
  resp["type"] = "assetResponse";
  resp["requestId"] = served.requestId;
  resp["ok"] = ok;
  if (served.fileDataID > 0)
    resp["fileDataID"] = served.fileDataID;
  resp["role"] = served.role;
  resp["load"] = served.load;
  resp["assetEpoch"] = served.assetEpoch;
  resp["state"] = answer.state;
  resp["detail"] = answer.detail;
  resp["readMs"] = answer.readMs;
  if (ok)
  {
    served.bytes = answer.bytes.size();
    served.md5 = answer.md5;
    m_stats.mapAssetsAvailable++;
    resp["md5"] = answer.md5;
    resp["byteLength"] = answer.bytes.size();
    if (playerTakesBinaryFrames())
    {
      resp["encoding"] = "binary";
      resp["payloadBytes"] = answer.bytes.size();
      queueFrame(QJsonDocument(resp).toJson(QJsonDocument::Compact), answer.bytes);
    }
    else
    {
      resp["encoding"] = "base64";
      resp["data"] = QString::fromLatin1(answer.bytes.toBase64());
      queueJson(resp);
    }
  }
  else
  {
    // The player's generic answer path reads "error".
    resp["error"] = answer.detail.isEmpty() ? answer.state : answer.detail;
    queueJson(resp);
  }
  served.state = answer.state;
  served.detail = answer.detail;
  served.readMs = answer.readMs;
  served.handlerMs = handler.nsecsElapsed() / 1e6;
  if (ok)
    LOG_INFO << "[unityipc] <- getMapAsset" << served.describe();
  else
    LOG_WARNING << "[unityipc] <- getMapAsset" << served.describe();
  recordMapAssetServed(served);
  return answer.bytesRead;
}

QString UnityIpcServer::TerrainTileReport::describe() const
{
  const auto ms = [](double v) { return v < 0.0 ? QString("-") : QString::number(v, 'f', 1); };
  const auto vec = [](bool has, const double * v) {
    return has ? QString("[%1,%2,%3]").arg(v[0], 0, 'f', 3).arg(v[1], 0, 'f', 3).arg(v[2], 0, 'f', 3) : QString("-");
  };
  // The player's free text and the hashes are appended, never passed through arg().
  return QString("load=%1 status=%2 map=%3 tile=%4_%5 wdtFileDataID=%6 rootFileDataID=%7 wdtBytes=%8 rootBytes=%9")
           .arg(load).arg(status).arg(mapId).arg(tileX).arg(tileY).arg(wdtFileDataID).arg(rootFileDataID).arg(wdtBytes)
           .arg(rootBytes) +
         " wdtMd5=" + (wdtMd5.isEmpty() ? QString("-") : wdtMd5) + " rootMd5=" + (rootMd5.isEmpty() ? QString("-") : rootMd5) +
         QString(" chunks=%1 built=%2 skipped=%3 unsupported=%4 vertices=%5 triangles=%6 holeCells=%7 bounds=%8..%9")
           .arg(chunks).arg(chunksBuilt).arg(chunksSkipped).arg(chunksUnsupported).arg(vertices).arg(triangles)
           .arg(holeCells).arg(vec(hasBounds, boundsMin)).arg(vec(hasBounds, boundsMax)) +
         QString(" rootOffset=%1 originWorld=%2").arg(vec(hasRootOffset, rootOffset)).arg(vec(hasOriginWorld, originWorld)) +
         QString(" wdtRoundTripMs=%1 wdtParseMs=%2 rootRoundTripMs=%3 parseMs=%4 buildMs=%5 uploadMs=%6 firstFrameMs=%7"
                 " totalMs=%8")
           .arg(ms(wdtRoundTripMs)).arg(ms(wdtParseMs)).arg(ms(rootRoundTripMs)).arg(ms(parseMs)).arg(ms(buildMs))
           .arg(ms(uploadMs)).arg(ms(firstFrameMs)).arg(ms(totalMs)) +
         QString(" meshBytes=%1 liveTerrainTiles=%2 liveModels=%3 liveMapObjects=%4 meshObjects=%5 materialObjects=%6"
                 " terrainGameObjects=%7 managedHeapBytes=%8 nativeAllocatedBytes=%9")
           .arg(meshBytes).arg(liveTerrainTiles).arg(liveModels).arg(liveMapObjects).arg(meshObjects).arg(materialObjects)
           .arg(terrainGameObjects).arg(managedHeapBytes).arg(nativeAllocatedBytes) +
         (hostState.isEmpty() ? QString() : " hostState=\"" + hostState + "\" hostDetail=\"" + hostDetail + "\"") +
         " reason=\"" + reason + "\"" +
         // Protocol 13: the requests the load retired, and its material in brief (each texture is logged on its own).
         (requestsRetired < 0 ? QString() : QString(" requestsRetired=%1 [").arg(requestsRetired) +
                                              retiredRequestIds.join(",") + "]") +
         (!hasMaterial ? QString()
                       : " material=" + materialState + " materialReason=\"" + materialReason + "\"" +
                           QString(" tex0=%1 tex0Bytes=%2").arg(tex0FileDataID).arg(tex0Bytes) +
                           " tex0Md5=" + (tex0Md5.isEmpty() ? QString("-") : tex0Md5) + " alphaFormat=" + alphaFormat +
                           QString(" textures=%1 uniqueTextures=%2 layerReferences=%3 duplicateReferences=%4 materials=%5"
                                   " textureSourceBytes=%6 textureGpuBytes=%7 alphaGpuBytes=%8 materialVertexBytes=%9")
                             .arg(texturesSent).arg(uniqueTextures).arg(layerReferences).arg(duplicateReferences)
                             .arg(materials).arg(textureSourceBytes).arg(textureGpuBytes).arg(alphaGpuBytes)
                             .arg(materialVertexBytes) +
                           QString(" tex0RoundTripMs=%1 tex0ParseMs=%2 textureFetchMs=%3 textureUploadMs=%4 alphaBuildMs=%5"
                                   " materialBuildMs=%6")
                             .arg(ms(tex0RoundTripMs)).arg(ms(tex0ParseMs)).arg(ms(textureFetchMs))
                             .arg(ms(textureUploadMs)).arg(ms(alphaBuildMs)).arg(ms(materialBuildMs)) +
                           " chunkStates={" + [this]() {
                             QStringList s;
                             for (const auto & c : chunkStates)
                               s << c.first + "=" + QString::number(c.second);
                             return s.join(",");
                           }() + "}");
}

int UnityIpcServer::TerrainTileReport::chunkState(const QString & name) const
{
  for (const auto & c : chunkStates)
    if (c.first == name)
      return c.second;
  return -1;
}

void UnityIpcServer::handleTerrainTileLoaded(const QJsonObject & msg)
{
  // Numbers the player did not send stay -1 (see TerrainTileReport); toDouble keeps 64-bit sizes intact.
  const auto count = [&msg](const char * key) {
    const QJsonValue v = msg.value(QLatin1String(key));
    return v.isDouble() ? (int)v.toDouble() : -1;
  };
  const auto count64 = [&msg](const char * key) {
    const QJsonValue v = msg.value(QLatin1String(key));
    return v.isDouble() ? (long long)v.toDouble() : -1LL;
  };
  const auto triple = [&msg](const char * key, double * out) {
    const QJsonArray a = msg.value(QLatin1String(key)).toArray();
    if (a.size() != 3)
      return false;
    for (int i = 0; i < 3; i++)
      out[i] = a.at(i).toDouble();
    return true;
  };

  TerrainTileReport r;
  r.message = msg;
  r.load = msg.value("load").toInt(0);
  r.status = msg.value("status").toString();
  r.reason = msg.value("reason").toString();
  r.hostState = msg.value("hostState").toString();
  r.hostDetail = msg.value("hostDetail").toString();
  r.mapId = count("mapId");
  r.tileX = count("tileX");
  r.tileY = count("tileY");
  r.wdtFileDataID = count("wdtFileDataID");
  r.rootFileDataID = count("rootFileDataID");
  r.wdtBytes = count64("wdtBytes");
  r.rootBytes = count64("rootBytes");
  r.wdtMd5 = msg.value("wdtMd5").toString();
  r.rootMd5 = msg.value("rootMd5").toString();
  r.chunks = count("chunks");
  r.chunksBuilt = count("chunksBuilt");
  r.chunksSkipped = count("chunksSkipped");
  r.chunksUnsupported = count("chunksUnsupported");
  r.vertices = count64("vertices");
  r.triangles = count64("triangles");
  r.holeCells = count("holeCells");
  r.hasBounds = triple("boundsMin", r.boundsMin) && triple("boundsMax", r.boundsMax);
  r.hasRootOffset = triple("rootOffset", r.rootOffset);
  r.hasOriginWorld = triple("originWorld", r.originWorld);
  const QJsonObject timings = msg.value("timings").toObject();
  const auto timing = [&timings](const char * key) {
    const QJsonValue v = timings.value(QLatin1String(key));
    return v.isDouble() ? v.toDouble() : -1.0;
  };
  r.wdtRoundTripMs = timing("wdtRoundTripMs");
  r.wdtParseMs = timing("wdtParseMs");
  r.rootRoundTripMs = timing("rootRoundTripMs");
  r.parseMs = timing("parseMs");
  r.buildMs = timing("buildMs");
  r.uploadMs = timing("uploadMs");
  r.firstFrameMs = timing("firstFrameMs");
  r.totalMs = timing("totalMs");
  r.meshBytes = count64("meshBytes");
  r.liveTerrainTiles = count("liveTerrainTiles");
  r.liveModels = count("liveModels");
  r.liveMapObjects = count("liveMapObjects");
  r.meshObjects = count("meshObjects");
  r.materialObjects = count("materialObjects");
  r.terrainGameObjects = count("terrainGameObjects");
  r.managedHeapBytes = count64("managedHeapBytes");
  r.nativeAllocatedBytes = count64("nativeAllocatedBytes");

  // Protocol 13 (TERRAIN TEXTURES): the requests the load retired (a count, the ids beside it; an array alone is taken as
  // the ids) and the material. A player before it sends neither (-1, no material). Lists are read up to
  // TERRAIN_REPORT_LIST_MAX entries (the retired ids, the chunk-state counters) or TERRAIN_REPORT_TEXTURES_MAX (the
  // texture entries): the message is the player's, and what the host keeps of it is bounded.
  {
    const QJsonValue retired = msg.value("requestsRetired");
    QJsonArray ids = msg.value("retiredRequestIds").toArray();
    if (retired.isArray())
    {
      ids = retired.toArray();
      r.requestsRetired = ids.size();
    }
    else if (retired.isDouble())
      r.requestsRetired = (int)retired.toDouble();
    for (int i = 0; i < ids.size() && i < TERRAIN_REPORT_LIST_MAX; i++)
      r.retiredRequestIds << ids.at(i).toString();
  }
  const QJsonValue materialValue = msg.value("material");
  if (materialValue.isObject())
  {
    const QJsonObject m = materialValue.toObject();
    const auto mcount = [&m](const char * key) {
      const QJsonValue v = m.value(QLatin1String(key));
      return v.isDouble() ? (int)v.toDouble() : -1;
    };
    const auto mcount64 = [&m](const char * key) {
      const QJsonValue v = m.value(QLatin1String(key));
      return v.isDouble() ? (long long)v.toDouble() : -1LL;
    };
    r.hasMaterial = true;
    r.materialState = m.value("state").toString();
    r.materialReason = m.value("reason").toString();
    r.tex0FileDataID = mcount("tex0FileDataID");
    r.tex0Bytes = mcount64("tex0Bytes");
    r.tex0Md5 = m.value("tex0Md5").toString();
    r.alphaFormat = m.value("alphaFormat").toString();
    const QJsonObject states = m.value("chunkStates").toObject();
    for (auto it = states.begin(); it != states.end(); ++it)
      if (r.chunkStates.size() < (size_t)TERRAIN_REPORT_LIST_MAX)
        r.chunkStates.emplace_back(it.key(), it.value().isDouble() ? (int)it.value().toDouble() : -1);
    const QJsonValue texturesValue = m.value("textures");
    if (texturesValue.isArray())
    {
      const QJsonArray list = texturesValue.toArray();
      r.texturesSent = list.size();
      for (int i = 0; i < list.size() && i < TERRAIN_REPORT_TEXTURES_MAX; i++)
      {
        const QJsonObject t = list.at(i).toObject();
        const auto tcount = [&t](const char * key) {
          const QJsonValue v = t.value(QLatin1String(key));
          return v.isDouble() ? (long long)v.toDouble() : -1LL;
        };
        TerrainTileReport::Texture x;
        x.fileDataID = (int)tcount("fileDataID");
        x.role = t.value("role").toString();
        x.state = t.value("state").toString();
        x.detail = t.value("detail").toString();
        x.bytes = tcount("bytes");
        x.md5 = t.value("md5").toString();
        x.width = (int)tcount("width");
        x.height = (int)tcount("height");
        x.format = t.value("format").toString();
        x.mips = (int)tcount("mips");
        const QJsonValue upload = t.value("uploadMs");
        x.uploadMs = upload.isDouble() ? upload.toDouble() : -1.0;
        r.textures.push_back(x);
      }
    }
    r.uniqueTextures = mcount("uniqueTextures");
    r.layerReferences = mcount("layerReferences");
    r.duplicateReferences = mcount("duplicateReferences");
    r.textureSourceBytes = mcount64("textureSourceBytes");
    r.textureGpuBytes = mcount64("textureGpuBytes");
    r.alphaGpuBytes = mcount64("alphaGpuBytes");
    r.materialVertexBytes = mcount64("materialVertexBytes");
    r.materials = mcount("materials");
    const QJsonObject mt = m.value("timings").toObject();
    const auto mtiming = [&mt](const char * key) {
      const QJsonValue v = mt.value(QLatin1String(key));
      return v.isDouble() ? v.toDouble() : -1.0;
    };
    r.tex0RoundTripMs = mtiming("tex0RoundTripMs");
    r.tex0ParseMs = mtiming("tex0ParseMs");
    r.textureFetchMs = mtiming("textureFetchMs");
    r.textureUploadMs = mtiming("textureUploadMs");
    r.alphaBuildMs = mtiming("alphaBuildMs");
    r.materialBuildMs = mtiming("materialBuildMs");
  }

  m_stats.terrainReports++;
  if (r.status == "built")
    m_stats.terrainBuilt++;
  else if (r.status == "failed")
    m_stats.terrainFailed++;
  m_stats.lastTerrain = QString("load %1 %2 map %3 tile %4_%5 ").arg(r.load).arg(r.status).arg(r.mapId).arg(r.tileX)
                          .arg(r.tileY) + r.reason;
  if (r.status == "failed")
    LOG_ERROR << "[unityipc] <- terrainTileLoaded" << r.describe();
  else
    LOG_INFO << "[unityipc] <- terrainTileLoaded" << r.describe();
  // Each texture of the material on a line of its own (the player's texts appended, never passed through arg()), the
  // first TERRAIN_REPORT_LIST_MAX of them; the rest (a list that long is mostly "not requested") counted by state.
  std::map<QString, int> notLogged;
  int logged = 0;
  for (const TerrainTileReport::Texture & t : r.textures)
  {
    if (logged >= TERRAIN_REPORT_LIST_MAX)
    {
      notLogged[t.role + " " + t.state]++;
      continue;
    }
    logged++;
    LOG_INFO << "[unityipc]    load" << r.load << "texture" << t.fileDataID << (t.role.isEmpty() ? QString("-") : t.role)
             << t.state
             << QString("bytes=%1 %2x%3 ").arg(t.bytes).arg(t.width).arg(t.height) +
                  (t.format.isEmpty() ? QString("-") : t.format) +
                  QString(" mips=%1 uploadMs=%2").arg(t.mips)
                    .arg(t.uploadMs < 0.0 ? QString("-") : QString::number(t.uploadMs, 'f', 3)) +
                  " md5=" + (t.md5.isEmpty() ? QString("-") : t.md5) +
                  (t.detail.isEmpty() ? QString() : " detail=\"" + t.detail + "\"");
  }
  if (!notLogged.empty())
  {
    QStringList counts;
    for (const auto & n : notLogged)
      counts << n.first + QString(" %1").arg(n.second);
    LOG_INFO << "[unityipc]    load" << r.load << ":" << (int)r.textures.size() - logged
             << "more texture entries, by role and state: " + counts.join(", ");
  }
  if (r.texturesSent > (int)r.textures.size())
    LOG_WARNING << "[unityipc]    load" << r.load << ":" << r.texturesSent << "texture entries sent," << (int)r.textures.size()
                << "read (at most" << TERRAIN_REPORT_TEXTURES_MAX << ")";
  if (onTerrainTileLoaded)
    onTerrainTileLoaded(r);
}
