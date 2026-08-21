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

#include <QCryptographicHash>
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
  m_inBuf.clear();
  m_outBuf.clear();
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
  m_client = (unsigned long long)c;
  m_unityReady = false;
  m_inBuf.clear();
  m_outBuf.clear();
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
  m_inBuf.clear();
  m_outBuf.clear();
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
  while (!m_outBuf.empty())
  {
    const int chunk = (int)std::min<size_t>(m_outBuf.size(), 256 * 1024);
    const int n = send((SOCKET)m_client, m_outBuf.data(), chunk, 0);
    if (n > 0)
    {
      m_outBuf.erase(0, (size_t)n);
      continue;
    }
    const int err = WSAGetLastError();
    if (n < 0 && err == WSAEWOULDBLOCK)
      return; // kernel buffer full; the rest goes out on the next poll
    dropClient("send error");
    return;
  }
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
  const QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact);
  m_outBuf.append(line.constData(), (size_t)line.size());
  m_outBuf.push_back('\n');
#ifdef _WINDOWS
  pollSend(); // try to push it out right away; leftovers go on the next poll
#endif
}

void UnityIpcServer::sendLoadWoWModel(const QString & path, int fileDataID, const QString & client)
{
  QJsonObject msg;
  msg["type"] = "loadWoWModel";
  msg["path"] = UnityAssetAccess::normalizePath(path);
  msg["fileDataID"] = fileDataID;
  msg["client"] = client;
  LOG_INFO << "[unityipc] -> loadWoWModel path=" << msg["path"].toString() << "fileDataID=" << fileDataID;
  queueJson(msg);
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
    LOG_INFO << "[unityipc] <- unityReady (protocolVersion" << version << ")";
    if (version != PROTOCOL_VERSION)
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
