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

#include <cstring>

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>

#include "UnityAssetAccess.h"

#include "logger/Logger.h"

namespace
{
  const int POLL_INTERVAL_MS = 20;
  const size_t RECV_CHUNK = 64 * 1024;
  const size_t MAX_LINE = 1024 * 1024;   // requests from the player are tiny; guard against junk
}

UnityIpcServer::UnityIpcServer()
{
  m_timer.SetOwner(this);
  Bind(wxEVT_TIMER, &UnityIpcServer::onPoll, this);
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
  int sendBuffer = 4 * 1024 * 1024;
  setsockopt(c, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char *>(&sendBuffer), sizeof(sendBuffer));
  m_client = (unsigned long long)c;
  m_unityReady = false;
  m_playerProtocol = 0;
  m_sentImages.clear();
  m_sentImageIds.clear();
  m_inBuf.clear();
  m_outBuf.clear();
  m_outPos = 0;
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
         QString(" mountEmitters=%1 mountRibbons=%2 mountParticles=%3 bodyRebinds=%4 viewFramings=%5")
           .arg(mountEmitters).arg(mountRibbons).arg(mountParticles).arg(bodyRebinds).arg(viewFramings);
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
  LOG_INFO << "[unityipc] <- runtimeState" << s.describe();
  if (onRuntimeState)
    onRuntimeState(s);
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
                                      bool character, int load, const QString & kind)
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
  if (kind == "wmo")
    m_stats.mapObjectLoads++;
  LOG_INFO << "[unityipc] -> loadWoWModel path=" << msg["path"].toString() << "fileDataID=" << fileDataID
           << "load=" << load << "kind=" << kind << (character ? "(character)" : "");
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
  const QByteArray data = QByteArray::fromRawData((const char *)pixels.constBits(),
                                                  pixels.width() * pixels.height() * 4).toBase64();
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
  m_stats.imageBytes += data.size();
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
                     " gets a notice";
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

  if (result.ok)
  {
    const QByteArray sha1 = QCryptographicHash::hash(result.data, QCryptographicHash::Sha1).toHex();
    resp["byteLength"] = result.data.size();
    resp["sha1"] = QString::fromLatin1(sha1);
    resp["encoding"] = "base64";   // V1: base64 in the JSON line; a binary frame can replace this later
    resp["data"] = QString::fromLatin1(result.data.toBase64());
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
