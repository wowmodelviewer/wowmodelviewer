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
#include <cmath>
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
  Bind(wxEVT_THREAD, &UnityIpcServer::onMapResultWake, this);
  m_clock.start();
}

UnityIpcServer::~UnityIpcServer()
{
  // No wake may be posted to this server once it goes: the notifier is cleared under the worker's lock, so none is
  // running when this returns (the wakes already queued go with the wxEvtHandler).
  if (m_mapAssetWorker && m_wakeOnResults)
    m_mapAssetWorker->setResultNotifier(nullptr);
  stop();
  // Results the worker made for this server's reads: nobody answers them any more (stop() dropped them).
  if (m_mapAssetWorker)
  {
    MapAssetWorker::Result r;
    while (m_mapAssetWorker->takeResult(r))
      ;
  }
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
  m_mapAssetsAnsweredStaleWaiting = 0;
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
  if (!m_listen || m_detached)
    return;
  m_activity.pollsServed++;
  const qint64 startNs = m_clock.nsecsElapsed();
  m_pollMapAnswered = 0;
  m_pollRecordMs = 0.0;
  PollTiming timing;
  pollAccept();
  dropPendingClient();
  if (m_client)
  {
    // THE TRANSPORT CAP: nothing is received while too much waits to be sent.
    updateReceivePause();
    if (m_receivePaused)
      m_transportStats.pollsPaused++;
    else
      pollReceive();
  }
  const qint64 receivedNs = m_clock.nsecsElapsed();
  const double recordMsBefore = m_pollRecordMs;
  // The map files asked for, in order, as far as this poll's budget goes (THE MAP ASSET QUEUE), and the worker's
  // results -- with or without a client: the results of reads whose connection ended are taken back and dropped.
  serveMapAssetQueue();
  dropPendingClient();
  const qint64 servedNs = m_clock.nsecsElapsed();
  if (m_client)
    pollSend();
  dropPendingClient();   // a stall found by pollSend
  logMapAnswerSummary(false);
  timing.receiveMs = (receivedNs - startNs) / 1e6;
  timing.serveMs = (servedNs - receivedNs) / 1e6;
  timing.recordMs = m_pollRecordMs - recordMsBefore;
  timing.sendMs = (m_clock.nsecsElapsed() - servedNs) / 1e6;
  timing.pollMs = (m_clock.nsecsElapsed() - startNs) / 1e6;
  timing.sincePreviousMs = m_lastPollNs < 0 ? -1.0 : (startNs - m_lastPollNs) / 1e6;
  timing.mapAnswered = m_pollMapAnswered;
  m_lastPollNs = startNs;
  if (onPollTiming)
    onPollTiming(timing);
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
  int sendBuffer = m_sendBufferBytes;
  setsockopt(c, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char *>(&sendBuffer), sizeof(sendBuffer));
  m_client = (unsigned long long)c;
  m_unityReady = false;
  m_playerProtocol = 0;
  m_sentImages.clear();
  m_sentImageIds.clear();
  m_inBuf.clear();
  m_outBuf.clear();
  m_outPos = 0;
  m_receivePaused = false;
  m_dropPending.clear();
  m_sendProgressNs = m_clock.nsecsElapsed();
  dropHeldMapAssets("a new connection");
  m_connectionSerial++;   // a read in flight of the connection before is never answered on this one
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
  m_receivePaused = false;
  m_dropPending.clear();
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
  // THE TRANSPORT CAP: at most m_linesPerPoll lines a poll, those left by the last poll first; complete lines beyond
  // the budget stay in m_inBuf and nothing more is received this poll.
  int budget = m_linesPerPoll;
  if (!handleBufferedLines(budget))
    return;   // a handler dropped the client
  if (m_receivePaused)
    return;   // a line's answers reached the mark: nothing more this poll
  if (budget <= 0)
  {
    m_transportStats.pollsLinesDeferred++;
    return;
  }
  char buf[RECV_CHUNK];
  for (;;)
  {
    const int n = recv((SOCKET)m_client, buf, (int)sizeof(buf), 0);
    if (n > 0)
    {
      m_inBuf.append(buf, (size_t)n);
      if (!handleBufferedLines(budget))
        return;
      if (m_receivePaused)
        return;
      if (budget <= 0)
      {
        m_transportStats.pollsLinesDeferred++;
        return;
      }
      if (m_inBuf.size() > MAX_LINE)   // (no complete line is left in it: a partial line this long is junk)
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
      m_sendProgressNs = m_clock.nsecsElapsed();
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
      checkSendStall();
      return;
    }
    dropClient("send error");
    return;
  }
  m_outBuf.clear();
  m_outPos = 0;
}

bool UnityIpcServer::handleBufferedLines(int & budget)
{
  size_t nl;
  // (The pause is checked again after every line: one poll's lines -- a getAsset is answered on the spot -- must not
  // take what waits to be sent past the mark.)
  while (budget > 0 && !m_receivePaused && (nl = m_inBuf.find('\n')) != std::string::npos)
  {
    std::string line = m_inBuf.substr(0, nl);
    m_inBuf.erase(0, nl + 1);
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    budget--;
    m_transportStats.linesHandled++;
    if (!line.empty())
      handleLine(line);
    if (!m_client)
      return false;
    updateReceivePause();
  }
  return true;
}

void UnityIpcServer::checkSendStall()
{
  if (m_outPos >= m_outBuf.size() || m_sendProgressNs < 0 || !m_dropPending.empty())
    return;
  const double waitedMs = (m_clock.nsecsElapsed() - m_sendProgressNs) / 1e6;
  if (waitedMs <= m_stallDropMs)
    return;
  m_transportStats.dropsStalled++;
  m_dropPending = QString("the client does not read: %1 bytes waited %2 ms with no send progress")
                    .arg(unsentBytes()).arg((qint64)waitedMs).toStdString();
  LOG_WARNING << "[unityipc] transport:" << QString::fromStdString(m_dropPending);
}

UnityIpcTestClient::~UnityIpcTestClient()
{
  close();
}

bool UnityIpcTestClient::connectTo(int port, int receiveBufferBytes)
{
  close();
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET)
    return false;
  if (receiveBufferBytes > 0)
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&receiveBufferBytes), sizeof(receiveBufferBytes));
  sockaddr_in addr;
  ZeroMemory(&addr, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons((u_short)port);
  if (connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
  {
    closesocket(s);
    return false;
  }
  m_socket = (unsigned long long)s;
  return true;
}

bool UnityIpcTestClient::sendText(const std::string & text)
{
  if (!m_socket)
    return false;
  size_t off = 0;
  while (off < text.size())
  {
    const int n = send((SOCKET)m_socket, text.data() + off, (int)std::min<size_t>(text.size() - off, 64 * 1024), 0);
    if (n <= 0)
      return false;
    off += (size_t)n;
  }
  return true;
}

long long UnityIpcTestClient::readAvailable(std::string * into)
{
  if (!m_socket)
    return -1;
  u_long nonBlocking = 1;
  ioctlsocket((SOCKET)m_socket, FIONBIO, &nonBlocking);
  long long total = 0;
  char buf[64 * 1024];
  bool closed = false;
  for (;;)
  {
    const int n = recv((SOCKET)m_socket, buf, (int)sizeof(buf), 0);
    if (n > 0)
    {
      total += n;
      if (into)
        into->append(buf, (size_t)n);
      continue;
    }
    if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK)
      closed = true;
    break;
  }
  u_long blocking = 0;
  ioctlsocket((SOCKET)m_socket, FIONBIO, &blocking);
  return closed && total == 0 ? -1 : total;
}

void UnityIpcTestClient::close()
{
  if (m_socket)
    closesocket((SOCKET)m_socket);
  m_socket = 0;
}

#else // !_WINDOWS

UnityIpcTestClient::~UnityIpcTestClient() {}
bool UnityIpcTestClient::connectTo(int, int) { return false; }
bool UnityIpcTestClient::sendText(const std::string &) { return false; }
long long UnityIpcTestClient::readAvailable(std::string *) { return -1; }
void UnityIpcTestClient::close() {}
bool UnityIpcServer::handleBufferedLines(int &) { return true; }
void UnityIpcServer::checkSendStall() {}

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
  if (!m_client || m_detached)
    return;   // (detached: the viewer is shutting down, and nothing is sent any more -- not only from a poll)
  size_t bytes = 1;
  for (const QByteArray & part : parts)
    bytes += (size_t)part.size();
  if (!admitQueued(bytes))
    return;
  for (const QByteArray & part : parts)
    m_outBuf.append(part.constData(), (size_t)part.size());
  m_outBuf.push_back('\n');
  noteQueued();
#ifdef _WINDOWS
  pollSend(); // try to push it out right away; leftovers go on the next poll
#endif
}

void UnityIpcServer::queueFrame(const QByteArray & header, const QByteArray & payload, double * appendMs, double * sendMs)
{
  if (!m_client || m_detached)
    return;
  if (!admitQueued((size_t)header.size() + (size_t)payload.size() + 2))
    return;
  QElapsedTimer step;
  step.start();
  m_outBuf.append(header.constData(), (size_t)header.size());
  m_outBuf.push_back('\n');
  m_outBuf.append(payload.constData(), (size_t)payload.size());
  m_outBuf.push_back('\n');   // the player checks it: a payload not followed by its newline means out of step
  noteQueued();
  if (appendMs)
    *appendMs = step.nsecsElapsed() / 1e6;
  step.restart();
#ifdef _WINDOWS
  pollSend();
#endif
  if (sendMs)
    *sendMs = step.nsecsElapsed() / 1e6;
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
           .arg(terrainTextures).arg(terrainTextureBytes).arg(terrainAlphaArrays) +
         QString(" worldInstances=%1 worldModels=%2 worldMeshes=%3 worldMaterials=%4 worldTextures=%5"
                 " worldGpuGeometryBytes=%6 worldGpuTextureBytes=%7")
           .arg(worldInstances).arg(worldModels).arg(worldMeshes).arg(worldMaterials).arg(worldTextures)
           .arg(worldGpuGeometryBytes).arg(worldGpuTextureBytes) +
         QString(" worldGroups=%1 worldGroupsVisible=%2 worldGroupsCulled=%3 worldInstancesCulled=%4 worldLoad=%5")
           .arg(worldGroups).arg(worldGroupsVisible).arg(worldGroupsCulled).arg(worldInstancesCulled).arg(worldLoad) +
         " worldKind=" + (worldKind.isEmpty() ? QString("-") : worldKind) +
         QString(" worldFrames=%1 worldFrameMsMedian=%2 worldFrameMsP95=%3 worldFrameMsMax=%4").arg(worldFrames)
           .arg(worldFrameMsMedian, 0, 'f', 2).arg(worldFrameMsP95, 0, 'f', 2).arg(worldFrameMsMax, 0, 'f', 2) +
         QString(" doodadInstances=%1 doodadPrototypes=%2 doodadMeshes=%3 doodadMaterials=%4 doodadTextures=%5"
                 " doodadGpuGeometryBytes=%6 doodadGpuTextureBytes=%7 doodadVisible=%8 doodadCulled=%9")
           .arg(doodadInstances).arg(doodadPrototypes).arg(doodadMeshes).arg(doodadMaterials).arg(doodadTextures)
           .arg(doodadGpuGeometryBytes).arg(doodadGpuTextureBytes).arg(doodadVisible).arg(doodadCulled) +
         QString(" liveGameObjects=%1 liveTexture2D=%2 liveMeshes=%3 liveMaterials=%4")
           .arg(liveGameObjects).arg(liveTexture2D).arg(liveMeshes).arg(liveMaterials);
}

int UnityIpcServer::requestRuntimeState(bool light)
{
  if (!playerDrawsMapObjects())
    return 0;
  QJsonObject msg;
  msg["type"] = "runtimeState";
  msg["query"] = ++m_runtimeQuery;
  if (light)
    msg["light"] = true;
  LOG_INFO << "[unityipc] -> runtimeState query=" << m_runtimeQuery << (light ? "(light)" : "");
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
  // Protocol 16 (WORLD MODELS IN THE WORLD).
  const auto count64 = [&msg](const char * key) {
    const QJsonValue v = msg.value(QLatin1String(key));
    return v.isDouble() ? (long long)v.toDouble() : -1LL;
  };
  s.worldInstances = count("worldInstances");
  s.worldModels = count("worldModels");
  s.worldMeshes = count("worldMeshes");
  s.worldMaterials = count("worldMaterials");
  s.worldTextures = count("worldTextures");
  s.worldGpuGeometryBytes = count64("worldGpuGeometryBytes");
  s.worldGpuTextureBytes = count64("worldGpuTextureBytes");
  s.worldGroups = count("worldGroups");
  s.worldGroupsVisible = count("worldGroupsVisible");
  s.worldGroupsCulled = count("worldGroupsCulled");
  s.worldInstancesCulled = count("worldInstancesCulled");
  s.worldLoad = count("worldLoad");
  s.worldKind = msg.value(QLatin1String("worldKind")).toString();
  s.worldFrames = count("worldFrames");
  const auto ms = [&msg](const char * key) {
    const QJsonValue v = msg.value(QLatin1String(key));
    return v.isDouble() ? v.toDouble() : -1.0;
  };
  s.worldFrameMsMedian = ms("worldFrameMsMedian");
  s.worldFrameMsP95 = ms("worldFrameMsP95");
  s.worldFrameMsMax = ms("worldFrameMsMax");
  // Protocol 17 (DOODADS).
  s.doodadInstances = count("doodadInstances");
  s.doodadPrototypes = count("doodadPrototypes");
  s.doodadMeshes = count("doodadMeshes");
  s.doodadMaterials = count("doodadMaterials");
  s.doodadTextures = count("doodadTextures");
  s.doodadGpuGeometryBytes = count64("doodadGpuGeometryBytes");
  s.doodadGpuTextureBytes = count64("doodadGpuTextureBytes");
  s.doodadVisible = count("doodadVisible");
  s.doodadCulled = count("doodadCulled");
  s.liveGameObjects = count("liveGameObjects");
  s.liveTexture2D = count("liveTexture2D");
  s.liveMeshes = count("liveMeshes");
  s.liveMaterials = count("liveMaterials");
  // Protocol 18 (STREAMING).
  s.light = msg.value(QLatin1String("light")).toBool(false);
  s.streaming = msg.contains(QLatin1String("streaming")) ? (msg.value(QLatin1String("streaming")).toBool() ? 1 : 0) : -1;
  s.streamTiles = count("streamTiles");
  s.streamJobs = count("streamJobs");
  s.streamRequestsOut = count("streamRequestsOut");
  s.streamPlacements = count("streamPlacements");
  s.streamDoodads = count("streamDoodads");
  s.sceneTerrainTextures = count("sceneTerrainTextures");
  s.streamPrototypesEvicted = count64("streamPrototypesEvicted");
  s.streamTexturesEvicted = count64("streamTexturesEvicted");
  s.streamFilesAskedAgain = count64("streamFilesAskedAgain");
  s.streamImagesShared = count64("streamImagesShared");
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
  m_activity.messagesHandled++;
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
                     " own model is shown without its weapons (below v11), no terrain tile is drawn (below v12), a"
                     " terrain tile is drawn without its textures (below v13), and no world model is placed in the world"
                     " (below v16)";
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
  else if (type == "mapTilesInspected")
  {
    handleMapTilesInspected(msg);
  }
  else if (type == "terrainViewApplied")
  {
    handleTerrainViewApplied(msg);
  }
  else if (type == "worldCameraApplied")
  {
    handleWorldCameraApplied(msg);
  }
  else if (type == "worldShown")
  {
    handleWorldShown(msg);
  }
  else if (type == "worldProgress")
  {
    handleWorldProgress(msg);
  }
  else if (type == "worldModelInspected")
  {
    handleWorldModelInspected(msg);
  }
  else if (type == "streamTile")
  {
    // STREAMING (protocol 18): tile loads opened and closed by the player; the viewer's answer goes back as the reply.
    QJsonObject reply;
    if (onStreamTile)
      reply = onStreamTile(msg);
    if (!reply.isEmpty())
      queueJson(reply);
  }
  else if (type == "streamState")
  {
    handleStreamState(msg);
  }
  else if (type == "testFlightDone")
  {
    TestFlightDone d;
    d.request = msg.value("request").toInt(0);
    d.status = msg.value("status").toString();
    d.reason = msg.value("reason").toString();
    d.seconds = msg.value("seconds").toDouble(-1.0);
    d.frames = msg.value("frames").toInt(-1);
    d.frameMsMax = msg.value("frameMsMax").toDouble(-1.0);
    d.distance = msg.value("distance").toDouble(-1.0);
    LOG_INFO << "[unityipc] <- testFlightDone" << d.request << d.status << d.reason << "after" << d.seconds << "s," << d.frames
             << "frame(s), the longest" << d.frameMsMax << "ms," << d.distance << "yards";
    if (onTestFlightDone)
      onTestFlightDone(d);
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

bool UnityIpcServer::putTerrainCommand(QJsonObject & msg, const TerrainTileCommand & c, bool & textures) const
{
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
  textures = playerDrawsTerrainTextures() && c.maxTextureBytes > 0;
  if (textures)
  {
    msg["maxTextureBytes"] = c.maxTextureBytes;
    msg["maxTextureBytesPerLoad"] = c.maxTextureBytesPerLoad;
    msg["maxTexturesPerLoad"] = c.maxTexturesPerLoad;
  }
  // THE CAMERA (protocol 15): placed by the player when this load's tile goes on screen.
  const bool camera = playerFliesWorld() && !c.camera.mode.isEmpty();
  if (camera)
    putCamera(msg, c.camera);
  return camera;
}

bool UnityIpcServer::sendLoadTerrainTile(const TerrainTileCommand & c)
{
  if (!playerDrawsTerrain() || c.load <= 0)
    return false;
  QJsonObject msg;
  msg["type"] = "loadTerrainTile";
  bool textures = false;
  const bool camera = putTerrainCommand(msg, c, textures);
  m_stats.terrainLoads++;
  LOG_INFO << "[unityipc] -> loadTerrainTile load=" << c.load << "epoch=" << c.assetEpoch << "map=" << c.mapId
           << c.mapDirectory << "tile" << QString("%1_%2").arg(c.tileX).arg(c.tileY) << "wdtFileDataID=" << c.wdtFileDataID
           << "resolved by" << c.wdtResolvedBy << (c.wdtResolvedBy == "path" ? c.wdtPath : QString())
           << "client" << c.product << c.build << "maxAssetBytes=" << c.maxAssetBytes
           << (textures ? QString("maxTextureBytes=%1 maxTextureBytesPerLoad=%2 maxTexturesPerLoad=%3")
                            .arg(c.maxTextureBytes).arg(c.maxTextureBytesPerLoad).arg(c.maxTexturesPerLoad)
                        : playerDrawsTerrainTextures() ? QString("no textures offered")
                                                       : QString("no textures (a protocol %1 player)").arg(m_playerProtocol))
           << (camera ? "camera: " + describeCamera(c.camera) : QString());
  queueJson(msg);
  return true;
}

bool UnityIpcServer::sendLoadWorld(const TerrainTileCommand & c)
{
  if (!playerPlacesWorldModels() || c.load <= 0 || !c.world)
    return false;
  QJsonObject msg;
  msg["type"] = "loadWorld";
  bool textures = false;
  const bool camera = putTerrainCommand(msg, c, textures);
  msg["worldModels"] = true;
  msg["globalWmo"] = c.globalWmo;
  msg["maxWmoFileBytes"] = c.maxWmoFileBytes;
  msg["maxWmoBytesPerLoad"] = c.maxWmoBytesPerLoad;
  msg["maxWmoTextureBytes"] = c.maxWmoTextureBytes;
  msg["maxWmoTextureBytesPerLoad"] = c.maxWmoTextureBytesPerLoad;
  msg["maxWmoTexturesPerLoad"] = c.maxWmoTexturesPerLoad;
  msg["maxWmoModelsPerLoad"] = c.maxWmoModelsPerLoad;
  msg["maxWmoGroupsPerLoad"] = c.maxWmoGroupsPerLoad;
  msg["maxWmoPlacementsPerLoad"] = c.maxWmoPlacementsPerLoad;
  // DOODADS (protocol 17): only to a player that places them, and only when the load offers them. A protocol-17 player
  // that finds no doodad budget loads the world without doodads (a protocol-16 host), and an older one is sent exactly
  // the protocol-16 message.
  const bool doodads = c.doodads && playerPlacesDoodads();
  if (doodads)
  {
    msg["doodads"] = true;
    msg["maxM2FileBytes"] = c.maxM2FileBytes;
    msg["maxM2BytesPerLoad"] = c.maxM2BytesPerLoad;
    msg["maxM2TextureBytes"] = c.maxM2TextureBytes;
    msg["maxM2TextureBytesPerLoad"] = c.maxM2TextureBytesPerLoad;
    msg["maxM2ModelsPerLoad"] = c.maxM2ModelsPerLoad;
    msg["maxM2TexturesPerLoad"] = c.maxM2TexturesPerLoad;
    msg["maxDoodadInstancesPerLoad"] = c.maxDoodadInstancesPerLoad;
  }
  // STREAMING (protocol 18): the region the player keeps around its camera, to a player that streams; otherwise the load
  // is the single-tile one.
  const bool streams = c.streaming && doodads && playerStreams();
  if (streams)
  {
    QJsonObject s;
    s["radius"] = STREAM_RADIUS;
    s["keepRadius"] = STREAM_KEEP_RADIUS;
    s["maxResidentTiles"] = STREAM_MAX_RESIDENT_TILES;
    s["requestCredit"] = STREAM_REQUEST_CREDIT;
    s["maxTileLoads"] = STREAM_MAX_TILE_LOADS;
    msg["streaming"] = s;
  }
  m_stats.terrainLoads++;
  m_stats.worldLoads++;
  LOG_INFO << "[unityipc] -> loadWorld load=" << c.load << "epoch=" << c.assetEpoch << "map=" << c.mapId << c.mapDirectory
           << (c.globalWmo ? QString("its global world model (no tile)") : "tile " + QString("%1_%2").arg(c.tileX).arg(c.tileY))
           << "wdtFileDataID=" << c.wdtFileDataID << "resolved by" << c.wdtResolvedBy
           << (c.wdtResolvedBy == "path" ? c.wdtPath : QString()) << "client" << c.product << c.build
           << "maxAssetBytes=" << c.maxAssetBytes
           << (textures ? QString("maxTextureBytes=%1 maxTextureBytesPerLoad=%2 maxTexturesPerLoad=%3")
                            .arg(c.maxTextureBytes).arg(c.maxTextureBytesPerLoad).arg(c.maxTexturesPerLoad)
                        : QString("no textures offered"))
           << QString("world models: maxWmoFileBytes=%1 maxWmoBytesPerLoad=%2 maxWmoTextureBytes=%3 maxWmoTextureBytesPerLoad=%4"
                      " maxWmoTexturesPerLoad=%5 maxWmoModelsPerLoad=%6 maxWmoGroupsPerLoad=%7 maxWmoPlacementsPerLoad=%8")
                .arg(c.maxWmoFileBytes).arg(c.maxWmoBytesPerLoad).arg(c.maxWmoTextureBytes).arg(c.maxWmoTextureBytesPerLoad)
                .arg(c.maxWmoTexturesPerLoad).arg(c.maxWmoModelsPerLoad).arg(c.maxWmoGroupsPerLoad).arg(c.maxWmoPlacementsPerLoad)
           << (doodads ? QString("doodads: maxM2FileBytes=%1 maxM2BytesPerLoad=%2 maxM2TextureBytes=%3 maxM2TextureBytesPerLoad=%4"
                                 " maxM2ModelsPerLoad=%5 maxM2TexturesPerLoad=%6 maxDoodadInstancesPerLoad=%7")
                           .arg(c.maxM2FileBytes).arg(c.maxM2BytesPerLoad).arg(c.maxM2TextureBytes).arg(c.maxM2TextureBytesPerLoad)
                           .arg(c.maxM2ModelsPerLoad).arg(c.maxM2TexturesPerLoad).arg(c.maxDoodadInstancesPerLoad)
                       : playerPlacesDoodads() ? QString("no doodads offered")
                                               : QString("no doodads (a protocol %1 player)").arg(m_playerProtocol))
           << (camera ? "camera: " + describeCamera(c.camera) : QString())
           << (streams ? QString("streaming: radius %1, kept within %2, at most %3 resident, %4 requests out")
                           .arg(STREAM_RADIUS).arg(STREAM_KEEP_RADIUS).arg(STREAM_MAX_RESIDENT_TILES).arg(STREAM_REQUEST_CREDIT)
                       : QString());
  queueJson(msg);
  return true;
}

// ---------------------------------------------------------------- STREAMING (protocol 18)

int UnityIpcServer::sendTestFlight(const std::vector<double> & points, double speed, double pitch)
{
  if (!playerStreams())
    return 0;
  static int serial = 0;
  QJsonObject msg;
  msg["type"] = "testFlight";
  msg["request"] = ++serial;
  QJsonArray p;
  for (double v : points)
    p.append(v);
  msg["flightPoints"] = p;
  msg["flightSpeed"] = speed;
  msg["flightPitch"] = pitch;
  LOG_INFO << "[unityipc] -> testFlight" << serial << ":" << (int)(points.size() / 3) << "point(s) at" << speed << "yards a second";
  queueJson(msg);
  return serial;
}

int UnityIpcServer::answerStaleWaiting()
{
  if (!onMapAssetStale)
    return 0;
  // Two passes: the stale ones are taken out of the queue first, then answered. An answer is sent at once, and a send
  // that fails drops the client and clears the queue (dropClient -> dropHeldMapAssets): no queue iterator may live
  // across it.
  std::vector<std::pair<QueuedMapAsset, QString>> gone;
  for (auto it = m_mapAssetQueue.begin(); it != m_mapAssetQueue.end();)
  {
    m_activity.mapCallbacks++;
    QString why = onMapAssetStale(it->request);
    if (why.isEmpty())
    {
      ++it;
      continue;
    }
    gone.emplace_back(*it, std::move(why));
    it = m_mapAssetQueue.erase(it);
  }
  int answered = 0;
  const bool connected = m_client != 0;
  for (const auto & g : gone)
  {
    if (connected && !m_client)
    {
      // The connection ended while they were answered: the rest go unanswered, as the queue would have with it.
      m_mapAssetsQueueDropped++;
      continue;
    }
    QElapsedTimer staleTimer;
    staleTimer.start();
    MapAssetAnswer stale;
    stale.state = "stale";
    stale.detail = g.second;
    answerMapAsset(g.first.request, (m_clock.nsecsElapsed() - g.first.arrivedNs) / 1e6, stale, nullptr, nullptr, staleTimer);
    m_mapAssetsAnsweredStaleWaiting++;
    answered++;
  }
  return answered;
}

void UnityIpcServer::setStreaming(bool on)
{
  if (on == m_streaming)
    return;
  // A session ending: what its loads still have waiting is answered stale first, so the smaller queue of a single load is
  // not already full of them.
  if (!on)
    answerStaleWaiting();
  m_streaming = on;
  m_mapAssetQueueMax = on ? STREAM_QUEUE_MAX : MAP_ASSET_QUEUE_MAX;
  m_mapReadsInFlightMax = on ? STREAM_READS_IN_FLIGHT : MAP_ASSET_READS_IN_FLIGHT;
  const QByteArray each = qgetenv("WMV_LOG_MAP_ANSWERS");
  logMapAnswerSummary(true);
  m_logEachMapAnswer = !on || each == "1";
  LOG_INFO << "[unityipc] streaming" << (on ? "begins" : "ends") << ": map asset queue" << m_mapAssetQueueMax << ", reads in flight"
           << m_mapReadsInFlightMax << ", answers logged" << (m_logEachMapAnswer ? "one line each" : "as a summary every 2 s");
}

void UnityIpcServer::logMapAnswerSummary(bool force)
{
  if (m_answersSinceSummary == 0)
    return;
  const qint64 now = m_clock.nsecsElapsed();
  if (!force && m_answerSummaryNs >= 0 && now - m_answerSummaryNs < 2000LL * 1000 * 1000)
    return;
  LOG_INFO << "[unityipc] <- getMapAsset summary:" << m_answersSinceSummary << "answered (" << m_answeredAvailableSinceSummary
           << "available," << m_answeredStaleSinceSummary << "stale," << m_answeredOtherSinceSummary << "other)," << m_answerBytesSinceSummary
           << "bytes read; waiting" << (int)m_mapAssetQueue.size() << ", in flight" << (int)m_mapReadsInFlight.size();
  m_answersSinceSummary = m_answeredAvailableSinceSummary = m_answeredStaleSinceSummary = m_answeredOtherSinceSummary = 0;
  m_answerBytesSinceSummary = 0;
  m_answerSummaryNs = now;
}

void UnityIpcServer::handleStreamState(const QJsonObject & msg)
{
  StreamState s;
  const auto i = [&msg](const char * k) { return msg.contains(k) ? msg.value(k).toInt(-1) : -1; };
  const auto l = [&msg](const char * k) { return msg.contains(k) ? (long long)msg.value(k).toDouble(-1) : -1LL; };
  const auto d = [&msg](const char * k) { return msg.contains(k) ? msg.value(k).toDouble(-1.0) : -1.0; };
  s.load = msg.value("load").toInt(0);
  s.cameraX = msg.value("cameraX").toDouble(0.0);
  s.cameraY = msg.value("cameraY").toDouble(0.0);
  s.cameraZ = msg.value("cameraZ").toDouble(0.0);
  s.cameraTileX = msg.value("cameraTileX").toInt(-1);
  s.cameraTileY = msg.value("cameraTileY").toInt(-1);
  const QJsonArray tiles = msg.value("tiles").toArray();
  for (const QJsonValue & v : tiles)
  {
    if ((int)s.tiles.size() >= 64)
      break;
    const QJsonObject o = v.toObject();
    StreamTile t;
    t.x = o.value("x").toInt(-1);
    t.y = o.value("y").toInt(-1);
    t.load = o.value("load").toInt(0);
    t.state = o.value("state").toString();
    t.filesAsked = o.value("filesAsked").toInt(0);
    t.filesAnswered = o.value("filesAnswered").toInt(0);
    t.bytes = (long long)o.value("bytes").toDouble(0);
    t.worldPlaced = o.value("worldPlaced").toInt(0);
    t.worldPlacements = o.value("worldPlacements").toInt(0);
    t.doodadsPlaced = o.value("doodadsPlaced").toInt(0);
    t.doodadInstances = o.value("doodadInstances").toInt(0);
    s.tiles.push_back(t);
  }
  s.tilesResident = i("tilesResident");
  s.tilesLoading = i("tilesLoading");
  s.tilesWanted = i("tilesWanted");
  s.tilesOpened = i("tilesOpened");
  s.tilesClosed = i("tilesClosed");
  s.tilesFailed = i("tilesFailed");
  s.requestsOut = i("requestsOut");
  s.requestsWaiting = i("requestsWaiting");
  s.filesAsked = l("filesAsked");
  s.filesAnswered = l("filesAnswered");
  s.bytesReceived = l("bytesReceived");
  s.worldPrototypes = i("worldPrototypes");
  s.worldPrototypesPooled = i("worldPrototypesPooled");
  s.doodadPrototypes = i("doodadPrototypes");
  s.doodadPrototypesPooled = i("doodadPrototypesPooled");
  s.textures = i("textures");
  s.texturesPooled = i("texturesPooled");
  s.worldInstances = i("worldInstances");
  s.doodadInstances = i("doodadInstances");
  s.gpuTextureBytes = l("gpuTextureBytes");
  s.gpuGeometryBytes = l("gpuGeometryBytes");
  s.poolBytes = l("poolBytes");
  s.prototypesEvicted = l("prototypesEvicted");
  s.texturesEvicted = l("texturesEvicted");
  s.frameMsMax = d("frameMsMax");
  s.frameMsP95 = d("frameMsP95");
  s.uploadMsMax = d("uploadMsMax");
  s.workMsMax = d("workMsMax");
  s.framesOver33 = i("framesOver33");
  s.framesOver50 = i("framesOver50");
  s.managedHeapBytes = l("managedHeapBytes");
  s.raw = msg;
  m_lastStreamState = s;
  m_streamStatesReceived++;
  // One line a second at most (the messages come four times a second).
  const qint64 now = m_clock.nsecsElapsed();
  if (m_streamStateLogNs < 0 || now - m_streamStateLogNs >= 1000LL * 1000 * 1000)
  {
    m_streamStateLogNs = now;
    LOG_INFO << "[unityipc] <- streamState" << s.describe();
  }
  if (onStreamState)
    onStreamState(s);
}

QString UnityIpcServer::StreamState::describe() const
{
  QStringList t;
  for (const StreamTile & x : tiles)
    t << QString("%1_%2:%3:%4/%5").arg(x.x).arg(x.y).arg(x.state).arg(x.filesAnswered).arg(x.filesAsked);
  return QString("load=%1 camera=[%2,%3,%4] tile=%5_%6 resident=%7 loading=%8 wanted=%9")
           .arg(load).arg(cameraX, 0, 'f', 1).arg(cameraY, 0, 'f', 1).arg(cameraZ, 0, 'f', 1).arg(cameraTileX).arg(cameraTileY)
           .arg(tilesResident).arg(tilesLoading).arg(tilesWanted) +
         QString(" opened=%1 closed=%2 failed=%3 requestsOut=%4 waiting=%5 files=%6/%7 bytes=%8")
           .arg(tilesOpened).arg(tilesClosed).arg(tilesFailed).arg(requestsOut).arg(requestsWaiting).arg(filesAnswered)
           .arg(filesAsked).arg(bytesReceived) +
         QString(" prototypes=%1+%2/%3+%4 textures=%5+%6 instances=%7/%8 gpuTex=%9")
           .arg(worldPrototypes).arg(worldPrototypesPooled).arg(doodadPrototypes).arg(doodadPrototypesPooled).arg(textures)
           .arg(texturesPooled).arg(worldInstances).arg(doodadInstances).arg(gpuTextureBytes) +
         QString(" gpuGeom=%1 pool=%2 evicted=%3/%4 frameMax=%5 p95=%6 over33=%7 over50=%8 uploadMax=%9")
           .arg(gpuGeometryBytes).arg(poolBytes).arg(prototypesEvicted).arg(texturesEvicted).arg(frameMsMax, 0, 'f', 1)
           .arg(frameMsP95, 0, 'f', 1).arg(framesOver33).arg(framesOver50).arg(uploadMsMax, 0, 'f', 1) +
         " tiles " + t.join(" ");
}

void UnityIpcServer::putCamera(QJsonObject & msg, const WorldCamera & c)
{
  msg["cameraRequest"] = c.request;
  msg["cameraMode"] = c.mode;
  msg["cameraX"] = c.x;
  msg["cameraY"] = c.y;
  msg["cameraZ"] = c.z;
  msg["cameraHasZ"] = c.hasZ;
  msg["cameraYaw"] = c.yaw;
  msg["cameraPitch"] = c.pitch;
  msg["cameraEyeHeight"] = c.eyeHeight;
  // Protocol 16: only when asked (an older player reads none of it).
  if (c.start)
    msg["cameraStart"] = true;
}

QString UnityIpcServer::describeCamera(const WorldCamera & c)
{
  if (c.mode == "fly" && c.start)
    return QString("request %1 fly at the world model's start point").arg(c.request);
  if (c.mode != "fly")
    return QString("request %1 %2").arg(c.request).arg(c.mode);
  return QString("request %1 fly at (%2, %3, %4) yaw %5 pitch %6 eye %7").arg(c.request).arg(c.x, 0, 'f', 3).arg(c.y, 0, 'f', 3)
    .arg(c.hasZ ? QString::number(c.z, 'f', 3) : QString("ground")).arg(c.yaw, 0, 'f', 1).arg(c.pitch, 0, 'f', 1).arg(c.eyeHeight, 0, 'f', 2);
}

bool UnityIpcServer::sendSetWorldCamera(const WorldCamera & c)
{
  if (!playerFliesWorld() || c.mode.isEmpty())
    return false;
  QJsonObject msg;
  msg["type"] = "setWorldCamera";
  putCamera(msg, c);
  LOG_INFO << "[unityipc] -> setWorldCamera" << describeCamera(c);
  queueJson(msg);
  return true;
}

void UnityIpcServer::handleWorldCameraApplied(const QJsonObject & msg)
{
  WorldCameraApplied r;
  r.request = msg.value("request").toInt(0);
  r.mode = msg.value("mode").toString();
  r.status = msg.value("status").toString();
  r.reason = msg.value("reason").toString();
  r.x = msg.value("x").toDouble(0.0);
  r.y = msg.value("y").toDouble(0.0);
  r.z = msg.value("z").toDouble(0.0);
  r.ground = msg.value("ground").toBool(false);
  r.hasGroundZ = msg.value("hasGroundZ").toBool(false);
  r.groundZ = msg.value("groundZ").toDouble(0.0);
  r.yaw = msg.value("yaw").toDouble(0.0);
  r.pitch = msg.value("pitch").toDouble(0.0);
  r.load = msg.value("load").toInt(0);
  LOG_INFO << "[unityipc] <- worldCameraApplied" << r.request << r.mode << r.status << "load" << r.load
           << QString("at (%1, %2, %3) yaw %4 pitch %5").arg(r.x, 0, 'f', 3).arg(r.y, 0, 'f', 3).arg(r.z, 0, 'f', 3)
                .arg(r.yaw, 0, 'f', 1).arg(r.pitch, 0, 'f', 1)
           << (r.hasGroundZ ? QString("ground %1").arg(r.groundZ, 0, 'f', 3) : QString("no ground"))
           << (r.reason.isEmpty() ? QString() : "-- " + r.reason);
  if (onWorldCameraApplied)
    onWorldCameraApplied(r);
}

// ---------------------------------------------------------------- the Maps viewer (protocol 14)

bool UnityIpcServer::sendInspectMapTiles(int inspection, int mapId, int wdtFileDataID, int assetEpoch, const QString & md5,
                                         const QByteArray & bytes)
{
  if (!playerInspectsMapTiles() || inspection <= 0 || bytes.isEmpty() || bytes.size() > MAX_MAP_ASSET_BYTES)
    return false;
  if (unsentBytes() > MAP_ASSET_SEND_HIGH_WATER)
  {
    LOG_INFO << "[unityipc] inspectMapTiles" << inspection << "not sent:" << unsentBytes()
             << "bytes still wait to be sent (the player is not reading)";
    return false;
  }
  QJsonObject msg;
  msg["type"] = "inspectMapTiles";
  msg["inspection"] = inspection;
  msg["mapId"] = mapId;
  msg["wdtFileDataID"] = wdtFileDataID;
  msg["assetEpoch"] = assetEpoch;
  msg["md5"] = md5;
  msg["encoding"] = "binary";
  msg["payloadBytes"] = bytes.size();
  LOG_INFO << "[unityipc] -> inspectMapTiles" << inspection << "map" << mapId << "WDT" << wdtFileDataID << "(" << bytes.size()
           << "bytes) epoch" << assetEpoch;
  queueFrame(QJsonDocument(msg).toJson(QJsonDocument::Compact), bytes);
  return true;
}

bool UnityIpcServer::sendSetTerrainView(const QString & view)
{
  if (!playerInspectsMapTiles())
    return false;
  QJsonObject msg;
  msg["type"] = "setTerrainView";
  msg["view"] = view;
  LOG_INFO << "[unityipc] -> setTerrainView" << view;
  queueJson(msg);
  return true;
}

void UnityIpcServer::handleMapTilesInspected(const QJsonObject & msg)
{
  MapTilesInspection r;
  r.inspection = msg.value("inspection").toInt(0);
  r.status = msg.value("status").toString();
  r.reason = msg.value("reason").toString();
  r.mapId = msg.value("mapId").toInt(-1);
  r.wdtFileDataID = msg.value("wdtFileDataID").toInt(0);
  r.assetEpoch = msg.value("assetEpoch").toInt(0);
  r.wdtBytes = (long long)msg.value("wdtBytes").toDouble(-1);
  r.md5 = msg.value("md5").toString();
  r.kind = msg.value("kind").toString();
  r.mphdFlags = (quint32)msg.value("mphdFlags").toDouble(0);
  r.hasMain = msg.value("hasMain").toBool(false);
  r.hasMaid = msg.value("hasMaid").toBool(false);
  r.tilesInMain = msg.value("tilesInMain").toInt(-1);
  r.tilesWithRoot = msg.value("tilesWithRoot").toInt(-1);
  r.parseMs = msg.value("parseMs").toDouble(-1.0);
  r.globalWmoRecords = msg.value("globalWmoRecords").toInt(0);
  r.globalWmoFileDataID = (quint32)msg.value("globalWmoFileDataID").toDouble(0);
  r.globalWmoPath = msg.value("globalWmoPath").toString();
  // Protocol 16: the WDT's first global record, as stored (absent before it, or without one).
  if (msg.value("globalRecord").toBool(false))
  {
    const auto triple = [&msg](const char * key, double * out) {
      const QJsonArray a = msg.value(QLatin1String(key)).toArray();
      if (a.size() != 3)
        return false;
      for (int i = 0; i < 3; i++)
        out[i] = a.at(i).toDouble();
      return true;
    };
    MapTilesInspection::GlobalRecord & g = r.globalRecord;
    g.present = triple("globalPosition", g.position) && triple("globalRotation", g.rotation) &&
                triple("globalExtentsMin", g.extentsMin) && triple("globalExtentsMax", g.extentsMax);
    g.flags = msg.value("globalFlags").toInt(0);
    g.scale = msg.value("globalScale").toInt(0);
    g.uniqueId = (quint32)msg.value("globalUniqueId").toDouble(0);
  }
  const int tiles = 64 * 64;
  if (r.status == "inspected")
  {
    r.tileFlags = QByteArray::fromBase64(msg.value("tileFlags").toString().toLatin1());
    const auto ids = [&msg, tiles](const char * key, std::vector<quint32> & out) {
      if (!msg.contains(QLatin1String(key)))
        return true;   // absent: the WDT has no MAID
      const QByteArray b = QByteArray::fromBase64(msg.value(QLatin1String(key)).toString().toLatin1());
      if (b.size() != tiles * 4)
        return false;
      out.resize(tiles);
      const unsigned char * p = reinterpret_cast<const unsigned char *>(b.constData());
      for (int i = 0; i < tiles; i++)
        out[i] = (quint32)p[4 * i] | ((quint32)p[4 * i + 1] << 8) | ((quint32)p[4 * i + 2] << 16) | ((quint32)p[4 * i + 3] << 24);
      return true;
    };
    const bool rootsOk = ids("rootIds", r.rootIds);
    const bool tex0Ok = ids("tex0Ids", r.tex0Ids);
    const bool minimapsOk = ids("minimapIds", r.minimapIds);
    if (r.tileFlags.size() != tiles)
      r.tableProblem = QString("the tile flags decoded to %1 bytes, not %2").arg(r.tileFlags.size()).arg(tiles);
    else if (!rootsOk || !tex0Ok || !minimapsOk)
      r.tableProblem = "the MAID ids did not decode to 4,096 each";
    else if (!r.minimapIds.empty() && r.rootIds.empty())
      r.tableProblem = "the table has minimap ids but no MAID root ids";
    else if (r.hasMaid != !r.rootIds.empty())
      r.tableProblem = "the table's MAID ids and its hasMaid disagree";
    else
    {
      int main = 0, root = 0;
      for (int i = 0; i < tiles; i++)
      {
        main += (r.tileFlags[i] & 1) ? 1 : 0;
        root += (r.tileFlags[i] & 2) ? 1 : 0;
      }
      if (main != r.tilesInMain || root != r.tilesWithRoot)
        r.tableProblem = QString("the flags count %1 tiles in MAIN and %2 with a root, the message %3 and %4")
                           .arg(main).arg(root).arg(r.tilesInMain).arg(r.tilesWithRoot);
      else
        r.tableValid = true;
    }
  }
  LOG_INFO << "[unityipc] <- mapTilesInspected" << r.inspection << r.status << "map" << r.mapId << "WDT" << r.wdtFileDataID
           << "kind" << r.kind << "tiles in MAIN" << r.tilesInMain << "with a root" << r.tilesWithRoot << "parse" << r.parseMs
           << "ms" << (r.minimapIds.empty() ? QString() : QString("minimap ids %1").arg(r.minimapIds.size()))
           << (r.globalWmoRecords > 0 ? QString("global WMO %1 (%2 record(s))")
                                          .arg(r.globalWmoFileDataID ? QString::number(r.globalWmoFileDataID) : r.globalWmoPath)
                                          .arg(r.globalWmoRecords) +
                                          (r.globalRecord.present
                                             ? QString(" at (%1, %2, %3) rotation (%4, %5, %6) scale %7 flags 0x%8")
                                                 .arg(r.globalRecord.position[0], 0, 'f', 3).arg(r.globalRecord.position[1], 0, 'f', 3)
                                                 .arg(r.globalRecord.position[2], 0, 'f', 3).arg(r.globalRecord.rotation[0], 0, 'f', 3)
                                                 .arg(r.globalRecord.rotation[1], 0, 'f', 3).arg(r.globalRecord.rotation[2], 0, 'f', 3)
                                                 .arg(r.globalRecord.scale).arg(r.globalRecord.flags, 0, 16)
                                             : QString(" (no record sent)"))
                                      : QString())
           << (r.reason.isEmpty() ? QString() : "reason: " + r.reason)
           << (r.tableProblem.isEmpty() ? QString() : "TABLE REFUSED: " + r.tableProblem);
  if (onMapTilesInspected)
    onMapTilesInspected(r);
}

// ---------------------------------------------------------------- world models in the world (protocol 16)

bool UnityIpcServer::sendInspectWorldModel(int inspection, int mapId, int wmoFileDataID, int assetEpoch, const QString & md5,
                                           const QByteArray & bytes, const MapTilesInspection::GlobalRecord & g)
{
  if (!playerPlacesWorldModels() || inspection <= 0 || wmoFileDataID <= 0 || bytes.isEmpty() || bytes.size() > MAX_WMO_FILE_BYTES ||
      !g.present)
    return false;
  if (unsentBytes() > MAP_ASSET_SEND_HIGH_WATER)
  {
    LOG_INFO << "[unityipc] inspectWorldModel" << inspection << "not sent:" << unsentBytes()
             << "bytes still wait to be sent (the player is not reading)";
    return false;
  }
  const auto triple = [](const double * v) {
    QJsonArray a;
    for (int i = 0; i < 3; i++)
      a.append(v[i]);
    return a;
  };
  QJsonObject msg;
  msg["type"] = "inspectWorldModel";
  msg["inspection"] = inspection;
  msg["mapId"] = mapId;
  msg["wmoFileDataID"] = wmoFileDataID;
  msg["assetEpoch"] = assetEpoch;
  msg["md5"] = md5;
  msg["wmoPosition"] = triple(g.position);
  msg["wmoRotation"] = triple(g.rotation);
  msg["wmoExtentsMin"] = triple(g.extentsMin);
  msg["wmoExtentsMax"] = triple(g.extentsMax);
  msg["wmoFlags"] = g.flags;
  msg["wmoScale"] = g.scale;
  msg["wmoUniqueId"] = (double)g.uniqueId;
  msg["encoding"] = "binary";
  msg["payloadBytes"] = bytes.size();
  m_stats.worldInspections++;
  LOG_INFO << "[unityipc] -> inspectWorldModel" << inspection << "map" << mapId << "WMO" << wmoFileDataID << "(" << bytes.size()
           << "bytes) epoch" << assetEpoch;
  queueFrame(QJsonDocument(msg).toJson(QJsonDocument::Compact), bytes);
  return true;
}

void UnityIpcServer::handleWorldModelInspected(const QJsonObject & msg)
{
  WorldModelInspection r;
  r.inspection = msg.value("inspection").toInt(0);
  r.status = msg.value("status").toString();
  r.reason = msg.value("reason").toString();
  r.mapId = msg.value("mapId").toInt(-1);
  r.wmoFileDataID = msg.value("wmoFileDataID").toInt(0);
  r.assetEpoch = msg.value("assetEpoch").toInt(0);
  r.wmoBytes = (long long)msg.value("wmoBytes").toDouble(-1);
  r.md5 = msg.value("md5").toString();
  r.groups = msg.value("groups").toInt(-1);
  r.interiorGroups = msg.value("interiorGroups").toInt(-1);
  r.parseMs = msg.value("parseMs").toDouble(-1.0);
  m_stats.worldInspected++;
  const auto six = [&msg](const char * key, double * out) {
    const QJsonArray a = msg.value(QLatin1String(key)).toArray();
    if (a.size() != 6)
      return false;
    for (int i = 0; i < 6; i++)
      out[i] = a.at(i).toDouble();
    return true;
  };
  if (r.status == "inspected")
  {
    // At most 1,024 groups (the player refuses a root with more): 6 floats and one flag word each.
    const QByteArray boxes = QByteArray::fromBase64(msg.value("groupBoxes").toString().toLatin1());
    const QByteArray flags = QByteArray::fromBase64(msg.value("groupFlags").toString().toLatin1());
    if (!six("rootBox", r.rootBox) || !six("extents", r.extents))
      r.problem = "the root box or the extents did not decode to 6 numbers";
    else if (r.groups < 0 || r.groups > 1024)
      r.problem = QString("the message names %1 groups (0 to 1,024)").arg(r.groups);
    else if (boxes.size() != r.groups * 6 * 4 || flags.size() != r.groups * 4)
      r.problem = QString("the group boxes and flags decoded to %1 and %2 bytes, not %3 and %4 for %5 groups")
                    .arg(boxes.size()).arg(flags.size()).arg(r.groups * 24).arg(r.groups * 4).arg(r.groups);
    else
    {
      r.groupBoxes.resize(r.groups * 6);
      std::memcpy(r.groupBoxes.data(), boxes.constData(), boxes.size());
      r.groupFlags.resize(r.groups);
      const unsigned char * p = reinterpret_cast<const unsigned char *>(flags.constData());
      for (int i = 0; i < r.groups; i++)
        r.groupFlags[i] = (quint32)p[4 * i] | ((quint32)p[4 * i + 1] << 8) | ((quint32)p[4 * i + 2] << 16) | ((quint32)p[4 * i + 3] << 24);
      r.valid = true;
      for (float v : r.groupBoxes)
        if (!std::isfinite(v))
        {
          r.valid = false;
          r.problem = "a group box is not finite";
          break;
        }
    }
  }
  LOG_INFO << "[unityipc] <- worldModelInspected" << r.inspection << r.status << "map" << r.mapId << "WMO" << r.wmoFileDataID
           << r.wmoBytes << "bytes;" << r.groups << "group(s)," << r.interiorGroups << "interior; parse" << r.parseMs << "ms"
           << QString("root box (%1, %2, %3)..(%4, %5, %6)").arg(r.rootBox[0], 0, 'f', 2).arg(r.rootBox[1], 0, 'f', 2)
                .arg(r.rootBox[2], 0, 'f', 2).arg(r.rootBox[3], 0, 'f', 2).arg(r.rootBox[4], 0, 'f', 2).arg(r.rootBox[5], 0, 'f', 2)
           << (r.reason.isEmpty() ? QString() : "reason: " + r.reason)
           << (r.problem.isEmpty() ? QString() : "REFUSED: " + r.problem);
  if (onWorldModelInspected)
    onWorldModelInspected(r);
}

void UnityIpcServer::handleWorldShown(const QJsonObject & msg)
{
  WorldShown r;
  r.load = msg.value("load").toInt(0);
  r.mapId = msg.value("mapId").toInt(-1);
  r.kind = msg.value("kind").toString();
  r.tileX = msg.value("tileX").toInt(-1);
  r.tileY = msg.value("tileY").toInt(-1);
  r.placements = msg.value("placements").toInt(-1);
  r.models = msg.value("models").toInt(-1);
  r.shownMs = msg.value("shownMs").toDouble(-1.0);
  const QJsonArray o = msg.value("originWorld").toArray();
  r.hasOriginWorld = o.size() == 3;
  for (int i = 0; i < 3 && r.hasOriginWorld; i++)
    r.originWorld[i] = o.at(i).toDouble();
  m_stats.worldShown++;
  LOG_INFO << "[unityipc] <- worldShown load" << r.load << "map" << r.mapId << r.kind
           << (r.kind == "tile" ? QString("tile %1_%2").arg(r.tileX).arg(r.tileY) : QString()) << ":" << r.placements
           << "placement(s) of" << r.models << "world model(s) planned; shown after" << r.shownMs << "ms"
           << (r.hasOriginWorld ? QString("origin (%1, %2, %3)").arg(r.originWorld[0], 0, 'f', 3).arg(r.originWorld[1], 0, 'f', 3)
                                    .arg(r.originWorld[2], 0, 'f', 3)
                                : QString());
  if (onWorldShown)
    onWorldShown(r);
}

void UnityIpcServer::handleWorldProgress(const QJsonObject & msg)
{
  const auto count = [&msg](const char * key) {
    const QJsonValue v = msg.value(QLatin1String(key));
    return v.isDouble() ? (int)v.toDouble() : -1;
  };
  WorldProgress r;
  r.load = msg.value("load").toInt(0);
  r.phase = msg.value("phase").toString();
  r.placements = count("placements");
  r.models = count("models");
  r.modelsRequested = count("modelsRequested");
  r.modelsParsed = count("modelsParsed");
  r.modelsBuilt = count("modelsBuilt");
  r.modelsFailed = count("modelsFailed");
  r.modelsUnsupported = count("modelsUnsupported");
  r.modelsNotRequested = count("modelsNotRequested");
  r.placed = count("placed");
  r.placementsFailed = count("placementsFailed");
  r.filesAsked = count("filesAsked");
  r.filesAnswered = count("filesAnswered");
  const QJsonValue bytes = msg.value("bytesReceived");
  r.bytesReceived = bytes.isDouble() ? (long long)bytes.toDouble() : -1LL;
  r.elapsedMs = msg.value("elapsedMs").toDouble(-1.0);
  // Protocol 17 (DOODADS): the doodads apart from the world models; an older player sends none (empty, -1).
  r.doodadPhase = msg.value("doodadPhase").toString();
  r.doodadInstances = count("doodadInstances");
  r.doodadPlaced = count("doodadPlaced");
  r.doodadModels = count("doodadModels");
  r.doodadModelsBuilt = count("doodadModelsBuilt");
  r.doodadModelsFailed = count("doodadModelsFailed");
  r.doodadFilesAsked = count("doodadFilesAsked");
  r.doodadFilesAnswered = count("doodadFilesAnswered");
  const QJsonValue doodadBytes = msg.value("doodadBytes");
  r.doodadBytes = doodadBytes.isDouble() ? (long long)doodadBytes.toDouble() : -1LL;
  m_stats.worldProgress++;
  // The player's phase names are appended, never passed through arg().
  LOG_INFO << "[unityipc] <- worldProgress load" << r.load << r.phase << QString("models %1 requested %2 parsed %3 built %4 failed %5"
                                                                             " unsupported %6 not requested %7; placed %8 of %9 (%10"
                                                                             " failed); files %11 of %12, %13 bytes; %14 ms")
                                                    .arg(r.models).arg(r.modelsRequested).arg(r.modelsParsed).arg(r.modelsBuilt)
                                                    .arg(r.modelsFailed).arg(r.modelsUnsupported).arg(r.modelsNotRequested)
                                                    .arg(r.placed).arg(r.placements).arg(r.placementsFailed).arg(r.filesAnswered)
                                                    .arg(r.filesAsked).arg(r.bytesReceived).arg(r.elapsedMs, 0, 'f', 1)
           << (r.doodadPhase.isEmpty() ? QString()
                                       : "; doodads " + r.doodadPhase +
                                           QString(": placed %1 of %2, models built %3 of %4 (%5 failed); files %6 of %7, %8 bytes")
                                             .arg(r.doodadPlaced).arg(r.doodadInstances).arg(r.doodadModelsBuilt).arg(r.doodadModels)
                                             .arg(r.doodadModelsFailed).arg(r.doodadFilesAnswered).arg(r.doodadFilesAsked)
                                             .arg(r.doodadBytes));
  if (onWorldProgress)
    onWorldProgress(r);
}

void UnityIpcServer::handleTerrainViewApplied(const QJsonObject & msg)
{
  TerrainViewApplied r;
  r.view = msg.value("view").toString();
  r.status = msg.value("status").toString();
  r.reason = msg.value("reason").toString();
  r.load = msg.value("load").toInt(0);
  r.rebuildMs = msg.value("rebuildMs").toDouble(-1.0);
  r.texturedChunks = msg.value("texturedChunks").toInt(-1);
  r.neutralChunks = msg.value("neutralChunks").toInt(-1);
  r.textures = msg.value("textures").toInt(-1);
  r.retainedTextureBytes = (long long)msg.value("retainedTextureBytes").toDouble(-1);
  LOG_INFO << "[unityipc] <- terrainViewApplied" << r.view << r.status << "load" << r.load << "rebuild" << r.rebuildMs << "ms;"
           << r.texturedChunks << "chunk(s) textured," << r.neutralChunks << "neutral," << r.textures << "texture(s), kept"
           << r.retainedTextureBytes << "bytes" << (r.reason.isEmpty() ? QString() : "-- " + r.reason);
  if (onTerrainViewApplied)
    onTerrainViewApplied(r);
}

bool UnityIpcServer::submitViewerRead(MapAssetWorker::Job & job)
{
  if (!m_mapAssetWorker || m_mapAssetWorker->stopped())
    return false;
  if (!job.cancelled)
    job.cancelled = std::make_shared<std::atomic<bool>>(false);
  ViewerRead v;
  v.cancelled = job.cancelled;
  v.ticket = m_mapAssetWorker->submit(job);
  job.ticket = v.ticket;
  m_viewerReads.push_back(v);
  return true;
}

void UnityIpcServer::cancelViewerReads()
{
  for (ViewerRead & v : m_viewerReads)
    if (v.cancelled)
      v.cancelled->store(true);
  if (!m_viewerReads.empty() && m_mapAssetWorker)
    m_mapAssetWorker->wake();
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
         (readOutcome.isEmpty() ? QString(" read=none")
                                : " read=" + readOutcome + (readState.isEmpty() ? QString() : " (" + readState + ")")) +
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
  {
    QElapsedTimer hook;
    hook.start();
    onMapAssetServed(served);
    m_pollRecordMs += hook.nsecsElapsed() / 1e6;
  }
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
  // Back to the front of the queue, in the order they were held, to be served from the next poll on as any request is:
  // decided then, read by the worker, answered once read.
  std::vector<QJsonObject> held;
  held.swap(m_mapAssetsHeld);
  // (At most MAP_ASSETS_HELD_MAX: the queue may then hold that many more than MAP_ASSET_QUEUE_MAX for a while.)
  for (auto it = held.rbegin(); it != held.rend(); ++it)
  {
    LOG_INFO << "[unityipc] test: getMapAsset" << it->value("requestId").toString() << "released";
    m_mapAssetQueue.push_front({*it, m_clock.nsecsElapsed()});
    m_mapAssetsQueued++;
  }
  if ((int)m_mapAssetQueue.size() > m_mapAssetQueueDepthMax)
    m_mapAssetQueueDepthMax = (int)m_mapAssetQueue.size();
  return (int)held.size();
}

void UnityIpcServer::testHoldNextMapRead(const QString & role, bool beforeOpen)
{
  m_holdNextMapRead = true;
  m_holdMapReadRole = role;
  m_holdMapReadBeforeOpen = beforeOpen;
  LOG_INFO << "[unityipc] test: the next map read" << (role.isEmpty() ? QString("of any role") : "of role " + role)
           << "is held in the worker" << (beforeOpen ? "before its file is opened" : "with its file open");
}

void UnityIpcServer::testCancelMapReadHold()
{
  m_holdNextMapRead = false;
  m_holdMapReadRole.clear();
}

void UnityIpcServer::testCorruptNextMapRead(const QString & role)
{
  m_corruptNextMapRead = true;
  m_corruptMapReadRole = role;
  LOG_INFO << "[unityipc] test: the next map read" << (role.isEmpty() ? QString("of any role") : "of role " + role)
           << "has one byte changed before its content key is checked";
}

void UnityIpcServer::testDelayMapReads(int ms)
{
  m_mapReadDelayMs = ms < 0 ? 0 : ms;
  LOG_INFO << "[unityipc] test: every map read waits" << m_mapReadDelayMs << "ms before it begins";
}

bool UnityIpcServer::testMapReadHeld(QJsonObject * request) const
{
  if (!m_mapAssetWorker || !m_mapAssetWorker->testHolding())
    return false;
  const quint64 ticket = m_mapAssetWorker->testHeldTicket();
  for (const MapReadInFlight & f : m_mapReadsInFlight)
    if (f.ticket == ticket)
    {
      if (request)
        *request = f.request;
      return true;
    }
  return false;
}

void UnityIpcServer::testReleaseMapRead()
{
  if (m_mapAssetWorker)
    m_mapAssetWorker->testReleaseHold();
  LOG_INFO << "[unityipc] test: the held map read is released";
}

bool UnityIpcServer::testWaitMapReads(int ms)
{
  return !m_mapAssetWorker || m_mapAssetWorker->waitIdle(ms);
}

void UnityIpcServer::dropHeldMapAssets(const char * why)
{
  m_holdNextMapAsset = false;
  m_holdMapAssetRole.clear();
  m_holdNextMapRead = false;
  m_holdMapReadRole.clear();
  m_corruptNextMapRead = false;
  m_corruptMapReadRole.clear();
  m_holdReportLoad = 0;
  m_heldReport = QJsonObject();
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
  // ... and so do the reads in flight: the worker skips those it has not begun, and what it hands back is dropped
  // unanswered when it is taken (THE MAP ASSET WORKER).
  int orphaned = 0;
  for (MapReadInFlight & f : m_mapReadsInFlight)
    if (!f.orphaned)
    {
      f.orphaned = true;
      if (f.cancelled)
        f.cancelled->store(true);
      orphaned++;
    }
  if (orphaned > 0)
  {
    m_mapReadStats.orphaned += orphaned;
    if (m_mapAssetWorker)
      m_mapAssetWorker->wake();
    LOG_INFO << "[unityipc]" << orphaned << "map read(s) in flight will be dropped unanswered (" << why << ")";
  }
  if (m_mapAssetsHeld.empty())
    return;
  for (const QJsonObject & msg : m_mapAssetsHeld)
    LOG_INFO << "[unityipc] test: held getMapAsset" << msg.value("requestId").toString() << "load"
             << msg.value("load").toInt(0) << "dropped unanswered (" << why << ")";
  m_mapAssetsHeldDropped += (long long)m_mapAssetsHeld.size();
  m_mapAssetsHeld.clear();
}

void UnityIpcServer::cancelMapReads(const QString & reason)
{
  cancelViewerReads();
  for (MapReadInFlight & f : m_mapReadsInFlight)
    if (!f.orphaned && f.cancelReason.isEmpty())
    {
      f.cancelReason = reason;
      if (f.cancelled)
        f.cancelled->store(true);
      m_mapReadStats.cancelled++;
    }
  if (m_mapAssetWorker)
    m_mapAssetWorker->wake();
}

// One map file for the terrain load being built (TERRAIN above): held when the test hook says so, refused at once when
// MAP_ASSET_QUEUE_MAX wait already (nothing decided or read), otherwise queued to be served in its turn
// (serveMapAssetQueue, the same poll when the worker has room).
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
  if (m_mapAssetQueue.size() >= (size_t)m_mapAssetQueueMax)
  {
    QElapsedTimer handler;
    handler.start();
    m_mapAssetsRefusedQueueFull++;
    MapAssetAnswer refused;
    refused.state = "refused";
    refused.detail = QString("too many map requests waiting (%1 wait, at most %2)").arg(m_mapAssetQueue.size())
                       .arg(m_mapAssetQueueMax);
    answerMapAsset(msg, 0.0, refused, nullptr, nullptr, handler);
    return;
  }
  // STREAMING: in the order of the request's priority (smaller first; absent is 0), first come first served among equals --
  // requests without one (every request of a single-tile load) keep the order they came in.
  const int priority = msg.value("priority").toInt(0);
  auto at = m_mapAssetQueue.end();
  for (auto it = m_mapAssetQueue.begin(); it != m_mapAssetQueue.end(); ++it)
    if (it->request.value("priority").toInt(0) > priority)
    {
      at = it;
      break;
    }
  m_mapAssetQueue.insert(at, {msg, m_clock.nsecsElapsed()});
  m_mapAssetsQueued++;
  if ((int)m_mapAssetQueue.size() > m_mapAssetQueueDepthMax)
    m_mapAssetQueueDepthMax = (int)m_mapAssetQueue.size();
}

long long UnityIpcServer::unsentBytes() const
{
  return (long long)(m_outBuf.size() - m_outPos) + m_fakeUnsentBytes;
}

// ---------------------------------------------------------------- THE TRANSPORT CAP

void UnityIpcServer::updateReceivePause()
{
  const long long unsent = unsentBytes();
  if (!m_receivePaused && unsent >= m_receivePauseBytes)
  {
    m_receivePaused = true;
    m_transportStats.receivePauses++;
    LOG_INFO << "[unityipc] transport: receiving paused," << unsent << "bytes wait to be sent";
  }
  else if (m_receivePaused && unsent <= m_receiveResumeBytes)
  {
    m_receivePaused = false;
    LOG_INFO << "[unityipc] transport: receiving resumed," << unsent << "bytes wait to be sent";
  }
}

bool UnityIpcServer::admitQueued(size_t bytes)
{
  if (!m_dropPending.empty())
  {
    m_transportStats.messagesNotQueued++;
    return false;
  }
  const long long unsent = unsentBytes();
  if (unsent + (long long)bytes > m_hardCapBytes)
  {
    m_transportStats.dropsHardCap++;
    m_transportStats.messagesNotQueued++;
    m_dropPending = QString("the client does not read: %1 bytes wait to be sent and a message of %2 would pass the cap of %3")
                      .arg(unsent).arg((qint64)bytes).arg(m_hardCapBytes).toStdString();
    LOG_WARNING << "[unityipc] transport:" << QString::fromStdString(m_dropPending);
    return false;
  }
  if (unsent == 0)
    m_sendProgressNs = m_clock.nsecsElapsed();   // bytes begin to wait now (the stall is counted from here)
  return true;
}

void UnityIpcServer::noteQueued()
{
  const long long unsent = unsentBytes();
  if (unsent > m_transportStats.unsentBytesMax)
    m_transportStats.unsentBytesMax = unsent;
}

void UnityIpcServer::dropPendingClient()
{
  if (m_dropPending.empty())
    return;
  const std::string why = m_dropPending;
  m_dropPending.clear();
  if (m_client)
    dropClient(why.c_str());
}

void UnityIpcServer::testSetTransportLimits(long long pauseBytes, long long resumeBytes, long long hardCapBytes,
                                            int stallMs, int linesPerPoll, int sendBufferBytes)
{
  m_receivePauseBytes = pauseBytes;
  m_receiveResumeBytes = resumeBytes;
  m_hardCapBytes = hardCapBytes;
  m_stallDropMs = stallMs;
  m_linesPerPoll = linesPerPoll;
  m_sendBufferBytes = sendBufferBytes > 0 ? sendBufferBytes : 64 * 1024 * 1024;
}

void UnityIpcServer::testPushBytes(long long bytes)
{
  QJsonObject h;
  h["type"] = "transportTestPayload";
  h["payloadBytes"] = (double)bytes;
  queueFrame(QJsonDocument(h).toJson(QJsonDocument::Compact), QByteArray((int)bytes, 'x'));
}

void UnityIpcServer::setMapAssetWorker(const std::shared_ptr<MapAssetWorker> & worker)
{
  if (m_mapAssetWorker && m_wakeOnResults)
    m_mapAssetWorker->setResultNotifier(nullptr);
  m_wakeOnResults = false;
  m_mapAssetWorker = worker;
}

void UnityIpcServer::detachForShutdown()
{
  if (m_detached)
    return;
  // The notifier first, under the worker's lock: once this returns none is running and none will run. The worker is
  // kept: the destructor takes back what it made for this server.
  if (m_mapAssetWorker && m_wakeOnResults)
    m_mapAssetWorker->setResultNotifier(nullptr);
  m_wakeOnResults = false;
  m_detached = true;
  m_timer.Stop();
  onDispatchMapAsset = nullptr;
  onCompleteMapAsset = nullptr;
  onMapAssetStale = nullptr;
  onMapAssetWait = nullptr;
  onViewerMapRead = nullptr;
  onMapTilesInspected = nullptr;
  onTerrainViewApplied = nullptr;
  onWorldCameraApplied = nullptr;
  onWorldModelInspected = nullptr;
  onWorldShown = nullptr;
  onWorldProgress = nullptr;
  LOG_INFO << "[unityipc] detached for the viewer's shutdown:" << m_mapReadsInFlight.size()
           << "map read(s) in flight left unanswered; no poll serves from now on"
           << (m_wakePosted.load() ? "(a wake already queued will find nothing to do)" : "");
}

void UnityIpcServer::wakeOnMapResults()
{
  if (!m_mapAssetWorker)
    return;
  m_wakeOnResults = true;
  // On the worker thread, under its lock: QueueEvent is wx's way for another thread to post to a handler, and the
  // flag keeps at most one wake waiting.
  m_mapAssetWorker->setResultNotifier([this]() {
    if (!m_wakePosted.exchange(true))
      QueueEvent(new wxThreadEvent(wxEVT_THREAD));
  });
}

void UnityIpcServer::onMapResultWake(wxThreadEvent & WXUNUSED(event))
{
  // A result was made: a poll now. The flag is lowered first, so a result made during the poll posts another wake.
  m_activity.wakesHandled++;
  m_wakePosted.store(false);
  if (m_detached)
    return;   // the viewer is shutting down: a wake queued before detachForShutdown finds nothing to do
  m_mapReadStats.wakes++;
  // Wakes can follow one another in one pass of the event queue (a poll's answers bring the next results). A run of
  // them spends at most about MAP_ASSET_WAKE_BURST_MS of this thread; then the window gets its turn, and the results
  // wait for the timer's poll (at most POLL_INTERVAL_MS) or a wake after the run.
  const qint64 startNs = m_clock.nsecsElapsed();
  if (m_wakeBurstEndNs < 0 || startNs - m_wakeBurstEndNs > (qint64)MAP_ASSET_WAKE_BURST_GAP_US * 1000)
    m_wakeBurstMs = 0.0;   // a new run
  if (m_wakeBurstMs >= MAP_ASSET_WAKE_BURST_MS)
  {
    m_mapReadStats.wakesDeferred++;
    m_wakeBurstEndNs = startNs;
    return;
  }
  const long long takenBefore = m_mapReadStats.answered + m_mapReadStats.droppedUnanswered + m_viewerResultsTaken;
  poll();
  const long long taken = m_mapReadStats.answered + m_mapReadStats.droppedUnanswered + m_viewerResultsTaken - takenBefore;
  m_mapReadStats.takenOnWake += taken;
  m_wakeBurstEndNs = m_clock.nsecsElapsed();
  m_wakeBurstMs += (m_wakeBurstEndNs - startNs) / 1e6;
  if (m_wakeBurstMs > m_mapReadStats.wakeBurstMsMax)
    m_mapReadStats.wakeBurstMsMax = m_wakeBurstMs;
  // A poll takes results only as far as its budget goes: when it took some and more are ready (at most
  // MAP_ASSET_READS_IN_FLIGHT), one more wake. A poll that took none (results waiting for a client load) posts none.
  if (taken > 0 && m_mapAssetWorker && m_mapAssetWorker->resultReady() && !m_wakePosted.exchange(true))
    QueueEvent(new wxThreadEvent(wxEVT_THREAD));
}

void UnityIpcServer::waitForMapResult(int ms)
{
  // A server not listening serves nothing (its polls end at once): a plain wait.
  if (!m_mapAssetWorker || (m_mapReadsInFlight.empty() && m_viewerReads.empty()) || !isListening())
  {
    wxMilliSleep(ms);
    return;
  }
  // Results made since the last serving poll looked end the wait; once seen here they do not end the next one (a pump
  // that waits twice without a serving poll between must not spin).
  if (m_mapAssetWorker->waitResultsAfter(m_resultsSeen, ms))
    m_resultsSeen = m_mapAssetWorker->resultsMade();
}

// THE MAP ASSET QUEUE AND WORKER, once per poll, on this (the GUI) thread: (1) the reads in flight the viewer no
// longer wants are cancelled; (2) the worker's results are taken, in order, each checked again by the viewer and
// answered, until MAP_ASSET_POLL_BUDGET_BYTES were read for them or one file over MAP_ASSET_LARGE_FILE_BYTES was; (3)
// requests waiting are decided, first come first served, while fewer than MAP_ASSET_READS_IN_FLIGHT reads are in flight
// and no more than MAP_ASSET_SEND_HIGH_WATER bytes wait to be sent: a request that reads nothing is answered at once, a
// read goes to the worker. One record per poll that answered or dispatched any.
int UnityIpcServer::serveMapAssetQueue()
{
  if (m_mapAssetWorker)
    m_resultsSeen = m_mapAssetWorker->resultsMade();   // (before taking any: a result made from now on is new)
  if (m_mapAssetQueue.empty() && m_mapReadsInFlight.empty() && m_viewerReads.empty())
    return 0;
  QElapsedTimer timer;
  timer.start();
  MapAssetPoll poll;
  const double recordMsBefore = m_pollRecordMs;
  // While a client is being loaded the reads in flight wait: neither checked nor answered (onMapAssetWait).
  if (onMapAssetWait)
    m_activity.mapCallbacks++;
  const bool waiting = onMapAssetWait && onMapAssetWait();

  // (1) The viewer's check, again, for every read still in flight: a read not begun is skipped, one under way finishes
  // (reads are whole) and is answered stale.
  bool cancelledAny = false;
  if (onMapAssetStale && !waiting)
    for (MapReadInFlight & f : m_mapReadsInFlight)
      if (!f.orphaned && f.cancelReason.isEmpty())
      {
        m_activity.mapCallbacks++;
        const QString why = onMapAssetStale(f.request);
        if (!why.isEmpty())
        {
          f.cancelReason = why;
          if (f.cancelled)
            f.cancelled->store(true);
          m_mapReadStats.cancelled++;
          cancelledAny = true;
        }
      }
  if (cancelledAny && m_mapAssetWorker)
    m_mapAssetWorker->wake();

  // (2) Results, in order, as far as the poll's budget goes.
  MapAssetWorker::Result result;
  while (m_mapAssetWorker && (!m_mapReadsInFlight.empty() || !m_viewerReads.empty()) && !waiting)
  {
    if (poll.bytesRead >= MAP_ASSET_POLL_BUDGET_BYTES || poll.largeFiles > 0 || poll.viewerResults >= MAP_VIEWER_RESULTS_PER_POLL)
    {
      poll.stoppedForBudget = m_mapAssetWorker->resultReady();
      break;
    }
    if (!m_mapAssetWorker->takeResult(result))
      break;
    // THE VIEWER'S OWN READS: handed to the viewer, never answered to the player.
    auto own = std::find_if(m_viewerReads.begin(), m_viewerReads.end(),
                            [&result](const ViewerRead & v) { return v.ticket == result.ticket; });
    if (own != m_viewerReads.end())
    {
      m_viewerReads.erase(own);
      poll.viewerResults++;
      m_viewerResultsTaken++;
      poll.bytesRead += result.bytes.size();
      if (result.bytes.size() > MAP_ASSET_LARGE_FILE_BYTES)
        poll.largeFiles++;
      if (onViewerMapRead)
      {
        m_activity.mapCallbacks++;
        onViewerMapRead(result);
      }
      continue;
    }
    auto it = std::find_if(m_mapReadsInFlight.begin(), m_mapReadsInFlight.end(),
                           [&result](const MapReadInFlight & f) { return f.ticket == result.ticket; });
    if (it == m_mapReadsInFlight.end())
      continue;   // not one of this server's (another server's, before a restart of it): nobody to answer
    MapReadInFlight read = *it;
    m_mapReadsInFlight.erase(it);
    if (read.orphaned || read.connection != m_connectionSerial)
    {
      m_mapReadStats.droppedUnanswered++;
      continue;   // its connection ended (dropHeldMapAssets): dropped unanswered, its bytes freed here
    }
    QElapsedTimer complete;
    complete.start();
    MapAssetAnswer answer;
    if (onCompleteMapAsset)
    {
      m_activity.mapCallbacks++;
      answer = onCompleteMapAsset(read.request, result);
    }
    else
    {
      answer.state = "stale";
      answer.detail = "this viewer finishes no map read";
    }
    const long long bytesRead = answerMapAsset(read.request, read.queuedMs, answer, &read, &result, complete);
    m_mapReadStats.answered++;
    if (answer.state == "stale")
      m_mapReadStats.answeredStale++;
    poll.served++;
    poll.completed++;
    poll.bytesRead += bytesRead;
    if (bytesRead > MAP_ASSET_LARGE_FILE_BYTES)
      poll.largeFiles++;
  }

  // (3a) STREAMING: a request waiting for a load that has ended (a tile the player closed) is answered stale now, not when
  // it reaches the front -- the player took its credit back at the close, so it must not keep the queue's room.
  if (m_streaming && !waiting)
    poll.served += answerStaleWaiting();

  // (3) Requests waiting: decided and dispatched while the worker has room and the player keeps reading.
  while (!m_mapAssetQueue.empty())
  {
    if ((int)m_mapReadsInFlight.size() >= m_mapReadsInFlightMax)
      break;
    if (unsentBytes() > MAP_ASSET_SEND_HIGH_WATER)
    {
      m_mapReadStats.dispatchHeldBack++;
      break;
    }
    const QueuedMapAsset next = m_mapAssetQueue.front();
    m_mapAssetQueue.pop_front();
    const qint64 dispatchedNs = m_clock.nsecsElapsed();
    const double queuedMs = (dispatchedNs - next.arrivedNs) / 1e6;
    QElapsedTimer dispatchTimer;
    dispatchTimer.start();
    MapAssetDispatch d;
    if (onDispatchMapAsset)
    {
      m_activity.mapCallbacks++;
      d = onDispatchMapAsset(next.request);
    }
    else
    {
      d.answer.state = "refused";
      d.answer.detail = "this viewer serves no map files";
    }
    if (d.read && (!m_mapAssetWorker || m_mapAssetWorker->stopped()))
    {
      // Only while the viewer shuts down: nothing reads any more.
      d.read = false;
      d.answer = MapAssetAnswer();
      d.answer.state = "refused";
      d.answer.detail = "the map asset worker is not running";
      m_mapReadStats.refusedNoWorker++;
    }
    if (!d.read)
    {
      answerMapAsset(next.request, queuedMs, d.answer, nullptr, nullptr, dispatchTimer);
      poll.served++;
      continue;
    }
    const QString role = next.request.value("role").toString();
    MapAssetWorker::Job job;
    job.fileDataID = d.fileDataID;
    job.maxBytes = d.maxBytes;
    job.epoch = d.epoch;
    job.generation = d.generation;
    job.role = role;
    job.cancelled = std::make_shared<std::atomic<bool>>(false);
    if (m_holdNextMapRead && (m_holdMapReadRole.isEmpty() || m_holdMapReadRole == role))
    {
      m_holdNextMapRead = false;
      job.testHold = true;
      job.testHoldBeforeOpen = m_holdMapReadBeforeOpen;
    }
    if (m_corruptNextMapRead && (m_corruptMapReadRole.isEmpty() || m_corruptMapReadRole == role))
    {
      m_corruptNextMapRead = false;
      job.testCorrupt = true;
    }
    job.testDelayMs = m_mapReadDelayMs;
    MapReadInFlight read;
    read.request = next.request;
    read.arrivedNs = next.arrivedNs;
    read.dispatchedNs = dispatchedNs;
    read.queuedMs = queuedMs;
    read.timings = d.answer.timings;
    read.connection = m_connectionSerial;
    read.cancelled = job.cancelled;
    read.testHold = job.testHold;
    read.testCorrupt = job.testCorrupt;
    read.ticket = m_mapAssetWorker->submit(job);
    read.dispatchMs = dispatchTimer.nsecsElapsed() / 1e6;
    m_mapReadsInFlight.push_back(read);
    m_mapReadStats.dispatched++;
    if ((int)m_mapReadsInFlight.size() > m_mapReadStats.inFlightMax)
      m_mapReadStats.inFlightMax = (int)m_mapReadsInFlight.size();
    poll.dispatched++;
  }

  if (poll.served == 0 && poll.dispatched == 0)
    return 0;
  poll.serveMs = timer.nsecsElapsed() / 1e6;
  poll.recordMs = m_pollRecordMs - recordMsBefore;
  poll.waitingAfter = (int)m_mapAssetQueue.size();
  poll.inFlightAfter = (int)m_mapReadsInFlight.size();
  if (unsentBytes() > m_mapReadStats.unsentBytesMax)
    m_mapReadStats.unsentBytesMax = unsentBytes();
  m_mapAssetPollsTotal++;
  m_mapAssetPolls.push_back(poll);
  while (m_mapAssetPolls.size() > (size_t)MAP_ASSET_POLL_TIMINGS_KEPT)
    m_mapAssetPolls.pop_front();
  if (poll.stoppedForBudget)
    LOG_INFO << "[unityipc] map requests: this poll answered" << poll.served << "(" << poll.bytesRead << "bytes read for them, in"
             << QString::number(poll.serveMs, 'f', 3) << "ms);" << poll.waitingAfter << "wait," << poll.inFlightAfter
             << "in flight";
  if (onMapAssetPoll)
    onMapAssetPoll(poll);
  return poll.served;
}

// One answer: what the viewer decided at dispatch (nothing to read) or at completion (read is the read in flight and
// result its worker's result), as any asset goes back, a binary frame with what the decision said, and its record.
// guiTimer has run since this request's GUI work began in this poll (the dispatch, or the taking of its result): with a
// read's dispatch time, handlerMs is all the time the GUI thread spent on the request.
long long UnityIpcServer::answerMapAsset(const QJsonObject & msg, double queuedMs, const MapAssetAnswer & answer,
                                         const MapReadInFlight * read, const MapAssetWorker::Result * result,
                                         const QElapsedTimer & guiTimer)
{
  MapAssetServed served;
  served.requestId = msg.value("requestId").toString();
  served.load = msg.value("load").toInt(0);
  served.assetEpoch = msg.value("assetEpoch").toInt(0);
  served.epochNow = UnityAssetAccess::clientEpoch();
  served.role = msg.value("role").toString();
  served.fileDataID = msg.value("fileDataID").toInt(0);
  served.queuedMs = queuedMs;
  served.bytesRead = answer.bytesRead;
  served.timings = answer.timings;
  if (read)
  {
    // The dispatch's own timings first (the decision), then the read's and the completion's (answer.timings).
    for (auto it = read->timings.begin(); it != read->timings.end(); ++it)
      if (!served.timings.contains(it.key()))
        served.timings[it.key()] = it.value();
    served.timings["dispatchMs"] = read->dispatchMs;
    served.timings["inFlightMs"] = (m_clock.nsecsElapsed() - read->dispatchedNs) / 1e6 - guiTimer.nsecsElapsed() / 1e6;
    if (read->testHold)
      served.timings["testHeld"] = true;
    if (read->testCorrupt)
      served.timings["testCorrupted"] = true;
    if (!read->cancelReason.isEmpty())
      served.timings["cancelled"] = read->cancelReason;
  }
  if (result)
  {
    served.readOutcome = MapAssetWorker::outcomeName(result->outcome);
    if (result->outcome == MapAssetWorker::Result::Outcome::Read)
      served.readState = wow::WorldMapCatalog::availabilityName(result->inspection.state);
    served.timings["workerWaitMs"] = result->waitMs;
    served.timings["workerMs"] = result->workMs;
  }
  // Only a file served counts as the file's first read: a refused or stale request reads nothing, and an answer that
  // the file is not in the storage, not installed or unreadable serves nothing (its lookup is not a read to time).
  const bool ok = answer.state == "available";
  if (ok)
  {
    const MapAssetReadTracker::Read tracked = m_mapAssetReads.note(served.epochNow, served.fileDataID);
    served.first = tracked == MapAssetReadTracker::Read::First;
    served.firstTracked = tracked != MapAssetReadTracker::Read::Untracked;
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
      QElapsedTimer serialize;
      serialize.start();
      const QByteArray header = QJsonDocument(resp).toJson(QJsonDocument::Compact);
      served.timings["serializeMs"] = serialize.nsecsElapsed() / 1e6;
      double appendMs = -1.0, sendMs = -1.0;
      queueFrame(header, answer.bytes, &appendMs, &sendMs);
      served.timings["appendMs"] = appendMs;
      served.timings["sendMs"] = sendMs;
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
  // What the GUI thread spent on this request: its dispatch (a read's, kept with it) and this answer.
  const double guiMs = guiTimer.nsecsElapsed() / 1e6;
  served.handlerMs = guiMs + (read ? read->dispatchMs : 0.0);
  if (read)
    served.timings["completeMs"] = guiMs;
  QElapsedTimer logTimer;
  logTimer.start();
  if (m_logEachMapAnswer)
  {
    if (ok)
      LOG_INFO << "[unityipc] <- getMapAsset" << served.describe();
    else
      LOG_WARNING << "[unityipc] <- getMapAsset" << served.describe();
  }
  else
  {
    // STREAMING: counted for the summary every two seconds (logMapAnswerSummary).
    m_answersSinceSummary++;
    m_answerBytesSinceSummary += answer.bytesRead;
    if (ok)
      m_answeredAvailableSinceSummary++;
    else if (answer.state == "stale")
      m_answeredStaleSinceSummary++;
    else
      m_answeredOtherSinceSummary++;
  }
  served.timings["logMs"] = logTimer.nsecsElapsed() / 1e6;
  m_pollMapAnswered++;
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

QString UnityIpcServer::TerrainTileReport::describeWorld() const
{
  if (!hasWorld)
    return QString();
  const auto ms = [](double v) { return v < 0.0 ? QString("-") : QString::number(v, 'f', 1); };
  // The player's free text is appended, never passed through arg().
  return "world kind=" + worldKind + " state=" + worldState +
         QString(" records=%1 placements=%2 placed=%3 failed=%4 unsupported=%5 notRequested=%6 ended=%7")
           .arg(worldRecords).arg(worldPlacements).arg(worldPlaced).arg(worldFailed).arg(worldUnsupported)
           .arg(worldNotRequested).arg(worldEnded) +
         QString(" models=%1 modelsBuilt=%2 modelsFailed=%3 modelsNotRequested=%4 groupFiles=%5 groupFilesMissing=%6")
           .arg(worldModels).arg(worldModelsBuilt).arg(worldModelsFailed).arg(worldModelsNotRequested).arg(worldGroupFiles)
           .arg(worldGroupFilesMissing) +
         QString(" textures=%1 texturesDecoded=%2 texturesMissing=%3 fileBytes=%4 textureBytes=%5 gpuGeometryBytes=%6"
                 " gpuTextureBytes=%7")
           .arg(worldTextures).arg(worldTexturesDecoded).arg(worldTexturesMissing).arg(worldFileBytes).arg(worldTextureBytes)
           .arg(worldGpuGeometryBytes).arg(worldGpuTextureBytes) +
         (worldHasOrigin ? QString(" origin=[%1,%2,%3]").arg(worldOrigin[0], 0, 'f', 3).arg(worldOrigin[1], 0, 'f', 3)
                                .arg(worldOrigin[2], 0, 'f', 3)
                         : QString()) +
         (worldStart ? QString(" start=[%1,%2,%3]").arg(worldStartPoint[0], 0, 'f', 3).arg(worldStartPoint[1], 0, 'f', 3)
                             .arg(worldStartPoint[2], 0, 'f', 3) + " startReason=\"" + worldStartReason + "\""
                     : QString(" start=-")) +
         QString(" shownMs=%1 fetchMs=%2 parseMs=%3 decodeMs=%4 buildMs=%5 buildMaxMs=%6 placeMs=%7")
           .arg(ms(worldShownMs)).arg(ms(worldFetchMs)).arg(ms(worldParseMs)).arg(ms(worldDecodeMs)).arg(ms(worldBuildMs))
           .arg(ms(worldBuildMaxMs)).arg(ms(worldPlaceMs)) +
         " reason=\"" + worldReason + "\"" +
         // Protocol 17: the whole world's assembly and the doodads in brief (describeDoodads has the rest).
         (worldAssembly.isEmpty() ? QString() : " assembly=" + worldAssembly) +
         (!hasDoodads ? QString(" doodads=-")
                      : " doodads=" + doodadState +
                          QString(" doodadsPlaced=%1 doodadInstances=%2 doodadModelsBuilt=%3 doodadModels=%4")
                            .arg(doodadPlaced).arg(doodadInstances).arg(doodadModelsBuilt).arg(doodadModels));
}

QString UnityIpcServer::TerrainTileReport::describeDoodads() const
{
  if (!hasDoodads)
    return QString();
  const auto ms = [](double v) { return v < 0.0 ? QString("-") : QString::number(v, 'f', 1); };
  // The player's free text is appended, never passed through arg().
  return "doodads state=" + doodadState +
         QString(" mddfRecords=%1 mddfPlacements=%2 wmoDoodadRecords=%3 wmoPlacementsWithDoodads=%4 sets=%5 instances=%6")
           .arg(doodadMddfRecords).arg(doodadMddfPlacements).arg(doodadWmoRecords).arg(doodadWmoPlacements)
           .arg(doodadSetsSent).arg(doodadInstances) +
         QString(" placed=%1 failed=%2 unsupported=%3 notRequested=%4 ended=%5")
           .arg(doodadPlaced).arg(doodadFailed).arg(doodadUnsupported).arg(doodadNotRequested).arg(doodadEnded) +
         QString(" models=%1 modelsBuilt=%2 modelsFailed=%3 modelsUnsupported=%4 modelsNotRequested=%5 modelsPartial=%6")
           .arg(doodadModels).arg(doodadModelsBuilt).arg(doodadModelsFailed).arg(doodadModelsUnsupported)
           .arg(doodadModelsNotRequested).arg(doodadModelsPartial) +
         QString(" textures=%1 texturesDecoded=%2 texturesMissing=%3 textureUses=%4 textureUploads=%5")
           .arg(doodadTextures).arg(doodadTexturesDecoded).arg(doodadTexturesMissing).arg(doodadTextureUses)
           .arg(doodadTextureUploads) +
         QString(" fileBytes=%1 textureBytes=%2 gpuGeometryBytes=%3 gpuTextureBytes=%4")
           .arg(doodadFileBytes).arg(doodadTextureBytes).arg(doodadGpuGeometryBytes).arg(doodadGpuTextureBytes) +
         QString(" discoverMs=%1 firstVisibleMs=%2 fetchMs=%3 parseMs=%4 decodeMs=%5 buildMs=%6 buildMaxMs=%7 placeMs=%8"
                 " placeMaxMs=%9")
           .arg(ms(doodadDiscoverMs)).arg(ms(doodadFirstVisibleMs)).arg(ms(doodadFetchMs)).arg(ms(doodadParseMs))
           .arg(ms(doodadDecodeMs)).arg(ms(doodadBuildMs)).arg(ms(doodadBuildMaxMs)).arg(ms(doodadPlaceMs))
           .arg(ms(doodadPlaceMaxMs)) +
         " readyMs=" + ms(doodadReadyMs) + QString(" list=%1").arg(doodadListSent) +
         " reason=\"" + doodadReason + "\"";
}

int UnityIpcServer::TerrainTileReport::chunkState(const QString & name) const
{
  for (const auto & c : chunkStates)
    if (c.first == name)
      return c.second;
  return -1;
}

void UnityIpcServer::testReleaseTerrainReport()
{
  if (m_heldReport.isEmpty())
    return;
  const QJsonObject held = m_heldReport;
  m_heldReport = QJsonObject();
  LOG_INFO << "[unityipc] test: the held terrainTileLoaded of load" << held.value("load").toInt(0) << "is handled now";
  handleTerrainTileLoaded(held);
}

void UnityIpcServer::handleTerrainTileLoaded(const QJsonObject & msg)
{
  if (m_holdReportLoad > 0 && msg.value("load").toInt(0) == m_holdReportLoad && m_heldReport.isEmpty())
  {
    m_holdReportLoad = 0;
    m_heldReport = msg;
    LOG_INFO << "[unityipc] test: the terrainTileLoaded of load" << msg.value("load").toInt(0) << "("
             << msg.value("status").toString() << ") is held, not handled";
    return;
  }
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
  r.view = msg.value("view").toString();
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

  // Protocol 16 (WORLD MODELS IN THE WORLD): the world's outcome; its placements read up to TERRAIN_REPORT_PLACEMENTS_MAX.
  const QJsonValue worldValue = msg.value("world");
  if (worldValue.isObject())
  {
    const QJsonObject w = worldValue.toObject();
    const auto wcount = [&w](const char * key) {
      const QJsonValue v = w.value(QLatin1String(key));
      return v.isDouble() ? (int)v.toDouble() : -1;
    };
    const auto wcount64 = [&w](const char * key) {
      const QJsonValue v = w.value(QLatin1String(key));
      return v.isDouble() ? (long long)v.toDouble() : -1LL;
    };
    const auto numbers = [](const QJsonValue & value, double * out, int n) {
      const QJsonArray a = value.toArray();
      if (a.size() != n)
        return false;
      for (int i = 0; i < n; i++)
        out[i] = a.at(i).toDouble();
      return true;
    };
    r.hasWorld = true;
    r.worldKind = w.value("kind").toString();
    r.worldState = w.value("state").toString();
    r.worldReason = w.value("reason").toString();
    r.worldRecords = wcount("records");
    r.worldPlacements = wcount("placements");
    r.worldPlaced = wcount("placed");
    r.worldFailed = wcount("failed");
    r.worldUnsupported = wcount("unsupported");
    r.worldNotRequested = wcount("notRequested");
    r.worldEnded = wcount("ended");
    r.worldModels = wcount("models");
    r.worldModelsBuilt = wcount("modelsBuilt");
    r.worldModelsFailed = wcount("modelsFailed");
    r.worldModelsNotRequested = wcount("modelsNotRequested");
    r.worldGroupFiles = wcount("groupFiles");
    r.worldGroupFilesMissing = wcount("groupFilesMissing");
    r.worldTextures = wcount("textures");
    r.worldTexturesDecoded = wcount("texturesDecoded");
    r.worldTexturesMissing = wcount("texturesMissing");
    r.worldFileBytes = wcount64("fileBytes");
    r.worldTextureBytes = wcount64("textureBytes");
    r.worldGpuGeometryBytes = wcount64("gpuGeometryBytes");
    r.worldGpuTextureBytes = wcount64("gpuTextureBytes");
    r.worldHasOrigin = numbers(w.value("originWorld"), r.worldOrigin, 3);
    r.worldStart = w.value("start").toBool(false) && numbers(w.value("startPoint"), r.worldStartPoint, 3);
    r.worldStartReason = w.value("startReason").toString();
    const QJsonObject wt = w.value("timings").toObject();
    const auto wtiming = [&wt](const char * key) {
      const QJsonValue v = wt.value(QLatin1String(key));
      return v.isDouble() ? v.toDouble() : -1.0;
    };
    r.worldShownMs = wtiming("shownMs");
    r.worldFetchMs = wtiming("fetchMs");
    r.worldParseMs = wtiming("parseMs");
    r.worldDecodeMs = wtiming("decodeMs");
    r.worldBuildMs = wtiming("buildMs");
    r.worldBuildMaxMs = wtiming("buildMaxMs");
    r.worldPlaceMs = wtiming("placeMs");
    const QJsonValue listValue = w.value("list");
    if (listValue.isArray())
    {
      const QJsonArray list = listValue.toArray();
      r.worldListSent = list.size();
      for (int i = 0; i < list.size() && i < TERRAIN_REPORT_PLACEMENTS_MAX; i++)
      {
        const QJsonObject e = list.at(i).toObject();
        TerrainTileReport::Placement pl;
        pl.uniqueId = (quint32)e.value("uniqueId").toDouble(0);
        pl.fileDataID = e.value("fileDataID").isDouble() ? (int)e.value("fileDataID").toDouble() : -1;
        pl.records = e.value("records").isDouble() ? (int)e.value("records").toDouble() : -1;
        pl.state = e.value("state").toString();
        pl.reason = e.value("reason").toString();
        pl.scale = e.value("scale").toDouble(1.0);
        pl.groups = e.value("groups").isDouble() ? (int)e.value("groups").toDouble() : -1;
        pl.groupsDrawn = e.value("groupsDrawn").isDouble() ? (int)e.value("groupsDrawn").toDouble() : -1;
        numbers(e.value("position"), pl.position, 3);
        numbers(e.value("rotation"), pl.rotation, 3);
        numbers(e.value("extents"), pl.extents, 6);
        pl.hasRendered = numbers(e.value("rendered"), pl.rendered, 6);
        r.worldList.push_back(pl);
      }
    }
    // Protocol 17 (DOODADS): the whole world's assembly, and the doodads' outcome; their sets and models read up to
    // TERRAIN_REPORT_DOODAD_SETS_MAX and TERRAIN_REPORT_DOODAD_MODELS_MAX.
    r.worldAssembly = w.value("assembly").toString();
    const QJsonValue doodadsValue = w.value("doodads");
    if (doodadsValue.isObject())
    {
      const QJsonObject d = doodadsValue.toObject();
      const auto dcount = [&d](const char * key) {
        const QJsonValue v = d.value(QLatin1String(key));
        return v.isDouble() ? (int)v.toDouble() : -1;
      };
      const auto dcount64 = [&d](const char * key) {
        const QJsonValue v = d.value(QLatin1String(key));
        return v.isDouble() ? (long long)v.toDouble() : -1LL;
      };
      r.hasDoodads = true;
      r.doodadState = d.value("state").toString();
      r.doodadReason = d.value("reason").toString();
      r.doodadMddfRecords = dcount("mddfRecords");
      r.doodadMddfPlacements = dcount("mddfPlacements");
      r.doodadWmoRecords = dcount("wmoDoodadRecords");
      r.doodadWmoPlacements = dcount("wmoPlacementsWithDoodads");
      r.doodadInstances = dcount("instances");
      r.doodadPlaced = dcount("placed");
      r.doodadFailed = dcount("failed");
      r.doodadUnsupported = dcount("unsupported");
      r.doodadNotRequested = dcount("notRequested");
      r.doodadEnded = dcount("ended");
      r.doodadModels = dcount("models");
      r.doodadModelsBuilt = dcount("modelsBuilt");
      r.doodadModelsFailed = dcount("modelsFailed");
      r.doodadModelsUnsupported = dcount("modelsUnsupported");
      r.doodadModelsNotRequested = dcount("modelsNotRequested");
      r.doodadModelsPartial = dcount("modelsPartial");
      r.doodadTextures = dcount("textures");
      r.doodadTexturesDecoded = dcount("texturesDecoded");
      r.doodadTexturesMissing = dcount("texturesMissing");
      r.doodadTextureUses = dcount("textureUses");
      r.doodadTextureUploads = dcount("textureUploads");
      r.doodadFileBytes = dcount64("fileBytes");
      r.doodadTextureBytes = dcount64("textureBytes");
      r.doodadGpuGeometryBytes = dcount64("gpuGeometryBytes");
      r.doodadGpuTextureBytes = dcount64("gpuTextureBytes");
      const QJsonObject dt = d.value("timings").toObject();
      const auto dtiming = [&dt](const char * key) {
        const QJsonValue v = dt.value(QLatin1String(key));
        return v.isDouble() ? v.toDouble() : -1.0;
      };
      r.doodadDiscoverMs = dtiming("discoverMs");
      r.doodadFirstVisibleMs = dtiming("firstVisibleMs");
      r.doodadFetchMs = dtiming("fetchMs");
      r.doodadParseMs = dtiming("parseMs");
      r.doodadDecodeMs = dtiming("decodeMs");
      r.doodadBuildMs = dtiming("buildMs");
      r.doodadBuildMaxMs = dtiming("buildMaxMs");
      r.doodadPlaceMs = dtiming("placeMs");
      r.doodadPlaceMaxMs = dtiming("placeMaxMs");
      r.doodadReadyMs = dtiming("readyMs");
      const auto number = [](const QJsonObject & e, const char * key) {
        const QJsonValue v = e.value(QLatin1String(key));
        return v.isDouble() ? (int)v.toDouble() : -1;
      };
      const QJsonValue setsValue = d.value("sets");
      if (setsValue.isArray())
      {
        const QJsonArray sets = setsValue.toArray();
        r.doodadSetsSent = sets.size();
        for (int i = 0; i < sets.size() && i < TERRAIN_REPORT_DOODAD_SETS_MAX; i++)
        {
          const QJsonObject e = sets.at(i).toObject();
          TerrainTileReport::DoodadSet set;
          set.uniqueId = (quint32)e.value("uniqueId").toDouble(0);
          set.wmo = number(e, "wmo");
          set.set = number(e, "set");
          set.setName = e.value("setName").toString();
          set.setCount = number(e, "setCount");
          set.active = number(e, "active");
          set.reason = e.value("reason").toString();
          r.doodadSets.push_back(set);
        }
      }
      // The player lists at most 64 sets and 128 models and states how many there were (setsTotal, listTotal).
      r.doodadSetsSent = std::max<int>(r.doodadSetsSent, d.value("setsTotal").toInt(-1));
      const QJsonValue modelsValue = d.value("list");
      if (modelsValue.isArray())
      {
        const QJsonArray list = modelsValue.toArray();
        r.doodadListSent = list.size();
        for (int i = 0; i < list.size() && i < TERRAIN_REPORT_DOODAD_MODELS_MAX; i++)
        {
          const QJsonObject e = list.at(i).toObject();
          TerrainTileReport::DoodadModel model;
          model.fileDataID = number(e, "fileDataID");
          model.state = e.value("state").toString();
          model.reason = e.value("reason").toString();
          model.instances = number(e, "instances");
          model.placed = number(e, "placed");
          r.doodadList.push_back(model);
        }
      }
      r.doodadListSent = std::max<int>(r.doodadListSent, d.value("listTotal").toInt(-1));
      m_stats.doodadReports++;
    }
    m_stats.worldReports++;
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
  // The world on a line of its own, then each placement (the player's texts appended, never passed through arg()).
  if (r.hasWorld)
  {
    LOG_INFO << "[unityipc]    load" << r.load << r.describeWorld();
    for (const TerrainTileReport::Placement & pl : r.worldList)
      LOG_INFO << "[unityipc]    load" << r.load << "placement" << pl.uniqueId << "WMO" << pl.fileDataID << pl.state
               << QString("records=%1 groups=%2 drawn=%3 scale=%4 at (%5, %6, %7) rotation (%8, %9, %10)")
                    .arg(pl.records).arg(pl.groups).arg(pl.groupsDrawn).arg(pl.scale, 0, 'f', 4).arg(pl.position[0], 0, 'f', 3)
                    .arg(pl.position[1], 0, 'f', 3).arg(pl.position[2], 0, 'f', 3).arg(pl.rotation[0], 0, 'f', 3)
                    .arg(pl.rotation[1], 0, 'f', 3).arg(pl.rotation[2], 0, 'f', 3) +
                  (pl.hasRendered ? QString(" rendered (%1, %2, %3)..(%4, %5, %6)").arg(pl.rendered[0], 0, 'f', 2)
                                      .arg(pl.rendered[1], 0, 'f', 2).arg(pl.rendered[2], 0, 'f', 2).arg(pl.rendered[3], 0, 'f', 2)
                                      .arg(pl.rendered[4], 0, 'f', 2).arg(pl.rendered[5], 0, 'f', 2)
                                  : QString()) +
                  (pl.reason.isEmpty() ? QString() : " reason=\"" + pl.reason + "\"");
    if (r.worldListSent > (int)r.worldList.size())
      LOG_WARNING << "[unityipc]    load" << r.load << ":" << r.worldListSent << "placements sent," << (int)r.worldList.size()
                  << "read (at most" << TERRAIN_REPORT_PLACEMENTS_MAX << ")";
  }
  // Protocol 17: the doodads on one line, then the first TERRAIN_REPORT_DOODAD_LOG_MAX models one per line (the player
  // lists the models not built first) and the rest counted by state: a line per doodad would be thousands per load.
  if (r.hasDoodads)
  {
    LOG_INFO << "[unityipc]    load" << r.load << r.describeDoodads();
    std::map<QString, int> modelsNotLogged;
    int modelsLogged = 0;
    for (const TerrainTileReport::DoodadModel & m : r.doodadList)
    {
      if (modelsLogged >= TERRAIN_REPORT_DOODAD_LOG_MAX)
      {
        modelsNotLogged[m.state]++;
        continue;
      }
      modelsLogged++;
      LOG_INFO << "[unityipc]    load" << r.load << "doodad model" << m.fileDataID << m.state
               << QString("placed %1 of %2").arg(m.placed).arg(m.instances) +
                    (m.reason.isEmpty() ? QString() : " reason=\"" + m.reason + "\"");
    }
    if (!modelsNotLogged.empty())
    {
      QStringList counts;
      for (const auto & n : modelsNotLogged)
        counts << n.first + QString(" %1").arg(n.second);
      LOG_INFO << "[unityipc]    load" << r.load << ":" << (int)r.doodadList.size() - modelsLogged
               << "more doodad models, by state: " + counts.join(", ");
    }
    if (r.doodadSetsSent > (int)r.doodadSets.size() || r.doodadListSent > (int)r.doodadList.size())
      LOG_INFO << "[unityipc]    load" << r.load << ":" << r.doodadSetsSent << "doodad sets and" << r.doodadListSent
                  << "doodad models in the load," << (int)r.doodadSets.size() << "and" << (int)r.doodadList.size() << "listed (at most"
                  << TERRAIN_REPORT_DOODAD_SETS_MAX << "and" << TERRAIN_REPORT_DOODAD_MODELS_MAX << ")";
  }
  if (onTerrainTileLoaded)
    onTerrainTileLoaded(r);
}
