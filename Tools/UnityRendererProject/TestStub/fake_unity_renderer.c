/*
 * fake_unity_renderer.c
 *
 * Tiny stand-in for the Unity standalone player, honouring the same contracts the WMV
 * UnityRendererHost relies on:
 *
 *   UnityRenderer.exe -parentHWND <decimal hwnd> [delayed] [-logFile <path>] [-wmvPort <n>]
 *
 *   Embedding:
 *   - creates its window as a WS_CHILD of the given parent HWND
 *   - does NOT resize itself; the host resizes it (MoveWindow) on pane resize
 *   - exits its message loop when it receives WM_CLOSE (the host's polite shutdown)
 *
 *   IPC (protocol v1, newline-delimited JSON over localhost TCP; WMV is the server):
 *   - connects to 127.0.0.1:<wmvPort>, sends {"type":"unityReady","protocolVersion":1}
 *   - on {"type":"loadWoWModel","path":...,"fileDataID":...} requests the asset with
 *     {"type":"getAsset","requestId":...,"path":...} (or getAssetByFileDataID when no path)
 *   - on {"type":"assetResponse",...} logs ok / byteLength / sha1 / error and checks that the
 *     base64 payload decodes to byteLength bytes
 *   - with -wmvSelfTest ONLY: additionally probes the protocol's negative paths (the same
 *     asset by FileDataID, a missing path, an unknown message type). WMV passes that flag
 *     just for its -unityipctest diagnostic run, so a normal viewport launch shows nothing
 *     but the real model exchange.
 *   Everything is written to the -logFile and shown as status text in the window.
 *
 * Purpose: verify the WMV-side embedding AND the runtime asset access end to end without a
 * Unity install. The JSON handling is deliberately minimal (key lookup by string search) --
 * it targets exactly the messages WMV emits, not arbitrary JSON.
 *
 * Build (any MSVC x64/x86 developer prompt):
 *   cl fake_unity_renderer.c user32.lib gdi32.lib shell32.lib ws2_32.lib /Fe:UnityRenderer.exe
 *
 * Then place UnityRenderer.exe at tools\unity-renderer\ next to wowmodelviewer.exe.
 * Never commit the built exe.
 */

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>   /* CommandLineToArgvW (not pulled in under WIN32_LEAN_AND_MEAN) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WM_APP_STATUS (WM_APP + 1)

static FILE * g_log = NULL;
static HWND g_hwnd = NULL;
static CRITICAL_SECTION g_statusLock;
static char g_status[6][256];
static int g_statusCount = 0;

static void logf(const char * fmt, ...)
{
  va_list ap;
  if (!g_log) return;
  va_start(ap, fmt);
  vfprintf(g_log, fmt, ap);
  va_end(ap);
  fputc('\n', g_log);
  fflush(g_log);
}

/* Append a status line (thread-safe) and ask the window to repaint. */
static void status(const char * fmt, ...)
{
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
  buf[sizeof(buf) - 1] = 0;
  va_end(ap);

  logf("status: %s", buf);
  EnterCriticalSection(&g_statusLock);
  if (g_statusCount == 6)
  {
    memmove(g_status[0], g_status[1], sizeof(g_status[0]) * 5);
    g_statusCount = 5;
  }
  strcpy(g_status[g_statusCount++], buf);
  LeaveCriticalSection(&g_statusLock);
  if (g_hwnd)
    PostMessage(g_hwnd, WM_APP_STATUS, 0, 0);
}

/* ---------------------------------------------------------------- minimal JSON helpers */

/* Find "key": and return pointer to the first char of the value (after whitespace), or NULL. */
static const char * json_value(const char * json, const char * key)
{
  char pat[128];
  const char * p;
  _snprintf(pat, sizeof(pat), "\"%s\"", key);
  p = strstr(json, pat);
  if (!p) return NULL;
  p += strlen(pat);
  while (*p == ' ' || *p == '\t') p++;
  if (*p != ':') return NULL;
  p++;
  while (*p == ' ' || *p == '\t') p++;
  return p;
}

/* Copy a string value (handles \" and \\ escapes, good enough for WoW paths / hashes). */
static int json_get_string(const char * json, const char * key, char * out, size_t cap)
{
  const char * p = json_value(json, key);
  size_t n = 0;
  if (!p || *p != '"') return 0;
  p++;
  while (*p && *p != '"' && n + 1 < cap)
  {
    if (*p == '\\' && p[1]) { p++; }
    out[n++] = *p++;
  }
  out[n] = 0;
  return 1;
}

static int json_get_int(const char * json, const char * key, long long * out)
{
  const char * p = json_value(json, key);
  if (!p) return 0;
  *out = _strtoi64(p, NULL, 10);
  return 1;
}

static int json_get_bool(const char * json, const char * key, int * out)
{
  const char * p = json_value(json, key);
  if (!p) return 0;
  *out = (strncmp(p, "true", 4) == 0);
  return 1;
}

/* Length of the string value without copying it (the base64 payload can be MBs). */
static long long json_string_length(const char * json, const char * key)
{
  const char * p = json_value(json, key);
  const char * e;
  if (!p || *p != '"') return -1;
  p++;
  e = p;
  while (*e && *e != '"') { if (*e == '\\' && e[1]) e++; e++; }
  return (long long)(e - p);
}

/* ---------------------------------------------------------------- IPC client thread */

static SOCKET g_sock = INVALID_SOCKET;
static int g_wmvPort = 0;
static int g_nextRequest = 1;
static int g_selfTest = 0;   /* -wmvSelfTest: run the negative-path probes (diagnostic runs only) */

/* Escape backslash + quote for embedding a parsed string back into outgoing JSON. */
static void json_escape(const char * in, char * out, size_t cap)
{
  size_t n = 0;
  for (; *in && n + 2 < cap; in++)
  {
    if (*in == '\\' || *in == '"')
      out[n++] = '\\';
    out[n++] = *in;
  }
  out[n] = 0;
}

static void ipc_send(const char * line)
{
  if (g_sock == INVALID_SOCKET) return;
  logf("send: %s", line);
  send(g_sock, line, (int)strlen(line), 0);
  send(g_sock, "\n", 1, 0);
}

static void handle_line(const char * line)
{
  char type[64] = {0};
  if (!json_get_string(line, "type", type, sizeof(type)))
  {
    logf("recv: (no type) %.200s", line);
    return;
  }

  if (strcmp(type, "loadWoWModel") == 0)
  {
    char path[512] = {0}, client[64] = {0}, req[600];
    long long fdid = 0;
    json_get_string(line, "path", path, sizeof(path));
    json_get_int(line, "fileDataID", &fdid);
    json_get_string(line, "client", client, sizeof(client));
    logf("recv: loadWoWModel path=%s fileDataID=%lld client=%s", path, fdid, client);
    status("Active client received (%s)", client[0] ? client : "active");

    if (path[0])
    {
      char escaped[1024];
      json_escape(path, escaped, sizeof(escaped));
      _snprintf(req, sizeof(req), "{\"type\":\"getAsset\",\"requestId\":\"s%d\",\"path\":\"%s\"}", g_nextRequest++, escaped);
      status("Requested %s", path);
    }
    else if (fdid > 0)
    {
      _snprintf(req, sizeof(req), "{\"type\":\"getAssetByFileDataID\",\"requestId\":\"s%d\",\"fileDataID\":%lld}", g_nextRequest++, fdid);
      status("Requested fileDataID %lld", fdid);
    }
    else
    {
      status("loadWoWModel without path/fileDataID -- ignored");
      return;
    }
    ipc_send(req);

    /* Diagnostic runs only (-wmvSelfTest): cover the rest of the v1 surface -- the same asset
     * by FileDataID (CASC clients; MPQ answers a clean "not supported"), a deliberately missing
     * path, and an unknown message type (must be answered, not dropped). A normal viewport
     * launch stops after the single real request above. */
    if (g_selfTest)
    {
      if (fdid > 0)
      {
        _snprintf(req, sizeof(req), "{\"type\":\"getAssetByFileDataID\",\"requestId\":\"s%d\",\"fileDataID\":%lld}", g_nextRequest++, fdid);
        ipc_send(req);
      }
      _snprintf(req, sizeof(req), "{\"type\":\"getAsset\",\"requestId\":\"s%d\",\"path\":\"creature/chicken/does_not_exist.m2\"}", g_nextRequest++);
      ipc_send(req);
      _snprintf(req, sizeof(req), "{\"type\":\"bogusMessage\",\"requestId\":\"s%d\"}", g_nextRequest++);
      ipc_send(req);
      /* metadata request: WMV resolves the model textures from the client database */
      _snprintf(req, sizeof(req), "{\"type\":\"getModelTextures\",\"requestId\":\"s%d\",\"fileDataID\":%lld}",
                g_nextRequest++, fdid);
      ipc_send(req);
    }
  }
  else if (strcmp(type, "modelTextures") == 0)
  {
    int ok = 0;
    long long fdid = 0;
    char err[256] = {0};
    json_get_bool(line, "ok", &ok);
    json_get_int(line, "fileDataID", &fdid);
    if (ok)
    {
      logf("recv: modelTextures for %lld -> %.400s", fdid, line);
      status("Model textures resolved");
    }
    else
    {
      json_get_string(line, "error", err, sizeof(err));
      logf("recv: modelTextures for %lld failed: %s", fdid, err);
      status("Model textures: %s", err);
    }
  }
  else if (strcmp(type, "modelSkin") == 0)
  {
    /* Pushed, not asked for: the skin on display in WMV changed. The real player re-uploads the
     * textures and keeps its mesh; the stub only has to prove the message arrives and carries the
     * selection, which is what the -unityipctest run checks. */
    long long fdid = 0;
    json_get_int(line, "fileDataID", &fdid);
    logf("recv: modelSkin for %lld -> %.400s", fdid, line);
    status("Skin changed");
  }
  else if (strcmp(type, "assetResponse") == 0)
  {
    int ok = 0;
    char path[512] = {0}, sha1[64] = {0}, err[256] = {0}, enc[32] = {0}, reqId[32] = {0};
    long long byteLength = -1, fdid = 0, b64len;
    char meta[2048];
    const char * scalars = line;

    /* Elide the (potentially multi-MB) base64 "data" value before the scalar-key lookups:
     * every json_* helper strstr-scans from the start of its input, so parsing the scalar
     * keys straight off the full line would rescan the payload once per key. The remaining
     * scalar fields fit comfortably in a small bounded copy. */
    b64len = json_string_length(line, "data");
    if (b64len > 0)
    {
      const char * v = json_value(line, "data"); /* points at the opening quote */
      const char * tail = v + 1 + b64len + 1;    /* first char after the closing quote */
      size_t pre = (size_t)(v - line);
      size_t post = strlen(tail);
      if (pre + post + 3 < sizeof(meta))
      {
        memcpy(meta, line, pre);
        meta[pre] = '"'; meta[pre + 1] = '"';
        memcpy(meta + pre + 2, tail, post + 1);
        scalars = meta;
      }
    }

    json_get_bool(scalars, "ok", &ok);
    json_get_string(scalars, "requestId", reqId, sizeof(reqId));
    json_get_string(scalars, "path", path, sizeof(path));
    json_get_int(scalars, "fileDataID", &fdid);
    if (ok)
    {
      json_get_int(scalars, "byteLength", &byteLength);
      json_get_string(scalars, "sha1", sha1, sizeof(sha1));
      json_get_string(scalars, "encoding", enc, sizeof(enc));
      /* decoded length of a base64 string = 3/4 of its length minus padding */
      {
        long long decoded = b64len >= 0 ? (b64len / 4) * 3 : -1;
        const char * d = json_value(line, "data");
        if (decoded >= 0 && d)
        {
          const char * e = d + 1 + b64len; /* char after the last base64 char */
          if (b64len >= 1 && e[-1] == '=') decoded--;
          if (b64len >= 2 && e[-2] == '=') decoded--;
        }
        logf("recv: assetResponse %s ok=1 path=%s fileDataID=%lld byteLength=%lld sha1=%s encoding=%s base64chars=%lld decodedLength=%lld %s first16=%.16s",
             reqId, path, fdid, byteLength, sha1, enc, b64len, decoded,
             decoded == byteLength ? "(length OK)" : "(LENGTH MISMATCH)", d ? d + 1 : "");
        status("Received %lld bytes for %s (sha1 %.8s..., decode %s)", byteLength,
               path[0] ? path : "asset", sha1, decoded == byteLength ? "OK" : "MISMATCH");
      }
    }
    else
    {
      json_get_string(scalars, "error", err, sizeof(err));
      logf("recv: assetResponse %s ok=0 path=%s fileDataID=%lld error=%s", reqId, path, fdid, err);
      status("Error for %s: %s", path[0] ? path : "asset", err);
    }
  }
  else
  {
    logf("recv: %s (unhandled) %.200s", type, line);
  }
}

static DWORD WINAPI ipc_thread(LPVOID param)
{
  WSADATA wsa;
  struct sockaddr_in addr;
  char * buf;
  size_t cap = 1 << 16, len = 0;

  (void)param;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { status("WSAStartup failed"); return 1; }

  status("Connecting to WMV on 127.0.0.1:%d ...", g_wmvPort);
  g_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((u_short)g_wmvPort);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(g_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0)
  {
    status("Connect failed (WSA error %d)", WSAGetLastError());
    closesocket(g_sock); g_sock = INVALID_SOCKET;
    return 1;
  }
  status("Connected to WMV (127.0.0.1:%d)", g_wmvPort);
  ipc_send("{\"type\":\"unityReady\",\"protocolVersion\":1}");

  buf = (char *)malloc(cap);
  if (!buf) { status("out of memory"); closesocket(g_sock); g_sock = INVALID_SOCKET; return 1; }
  for (;;)
  {
    int n;
    char * nl;
    if (len + 4096 > cap)
    {
      char * nb = (char *)realloc(buf, cap * 2);
      if (!nb) { status("out of memory (line of %llu bytes)", (unsigned long long)len); break; }
      cap *= 2;
      buf = nb;
    }
    n = recv(g_sock, buf + len, (int)(cap - len - 1), 0);
    if (n <= 0) { status("Disconnected from WMV"); break; }
    len += n;
    buf[len] = 0;
    while ((nl = (char *)memchr(buf, '\n', len)) != NULL)
    {
      *nl = 0;
      if (nl > buf && nl[-1] == '\r') nl[-1] = 0;
      if (buf[0]) handle_line(buf);
      len -= (size_t)(nl + 1 - buf);
      memmove(buf, nl + 1, len);
      buf[len] = 0;
    }
  }
  free(buf);
  {
    SOCKET s = (SOCKET)InterlockedExchangePointer((PVOID volatile *)&g_sock, (PVOID)INVALID_SOCKET);
    if (s != INVALID_SOCKET)
      closesocket(s);
  }
  return 0;
}

/* ---------------------------------------------------------------- window */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
    case WM_PAINT:
    {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      RECT rc;
      int i;
      GetClientRect(hwnd, &rc);

      /* vertical blue->dark gradient so resizes are visually obvious */
      int h = rc.bottom > 0 ? rc.bottom : 1;
      for (int y = 0; y < rc.bottom; y++)
      {
        int shade = 40 + (150 * (h - y)) / h;
        HBRUSH brush = CreateSolidBrush(RGB(20, 30, shade));
        RECT line = { 0, y, rc.right, y + 1 };
        FillRect(dc, &line, brush);
        DeleteObject(brush);
      }

      char text[128];
      _snprintf(text, sizeof(text), "UnityRenderer stub  %ldx%ld", rc.right, rc.bottom);
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, RGB(230, 230, 240));
      DrawTextA(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

      /* status lines (IPC state) top-left */
      EnterCriticalSection(&g_statusLock);
      for (i = 0; i < g_statusCount; i++)
      {
        RECT r = { 8, 8 + i * 18, rc.right - 8, 8 + (i + 1) * 18 };
        DrawTextA(dc, g_status[i], -1, &r, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
      }
      LeaveCriticalSection(&g_statusLock);

      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_APP_STATUS:
    case WM_SIZE:
      InvalidateRect(hwnd, NULL, TRUE);
      return 0;
    case WM_CLOSE:      /* the host's polite shutdown path */
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdLine, int show)
{
  HWND parent = NULL;
  HANDLE ipcThread = NULL;

  InitializeCriticalSection(&g_statusLock);

  int argc = 0;
  LPWSTR * argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; i < argc; i++)
  {
    /* -wmvSelfTest takes no value and may be last, so the loop runs to argc and each
     * value-taking flag checks for its argument itself. */
    const wchar_t * val = (i + 1 < argc) ? argv[i + 1] : NULL;
    if (wcscmp(argv[i], L"-wmvSelfTest") == 0)
      g_selfTest = 1;
    else if (val && wcscmp(argv[i], L"-parentHWND") == 0)
      parent = (HWND)(UINT_PTR)_wcstoui64(val, NULL, 10);
    else if (val && wcscmp(argv[i], L"-logFile") == 0)
      g_log = _wfopen(val, L"w");
    else if (val && wcscmp(argv[i], L"-wmvPort") == 0)
      g_wmvPort = _wtoi(val);
  }
  LocalFree(argv);

  logf("fake_unity_renderer: parentHWND=%p wmvPort=%d selfTest=%d", (void *)parent, g_wmvPort, g_selfTest);

  WNDCLASSA wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.lpszClassName = "FakeUnityRenderer";
  RegisterClassA(&wc);

  DWORD style;
  int w = 640, h = 480;
  if (parent && IsWindow(parent))
  {
    style = WS_CHILD | WS_VISIBLE;
    RECT rc;
    if (GetClientRect(parent, &rc))
    {
      w = rc.right > 0 ? rc.right : w;
      h = rc.bottom > 0 ? rc.bottom : h;
    }
  }
  else
  {
    style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;  /* standalone fallback for manual runs */
    parent = NULL;
  }

  g_hwnd = CreateWindowA("FakeUnityRenderer", "UnityRenderer stub", style,
                         0, 0, w, h, parent, NULL, inst, NULL);
  if (!g_hwnd)
  {
    logf("CreateWindow failed (%lu)", GetLastError());
    if (g_log) fclose(g_log);
    return 1;
  }

  if (g_wmvPort > 0)
    ipcThread = CreateThread(NULL, 0, ipc_thread, NULL, 0, NULL);
  else
    status("No -wmvPort given -- embedding only, no IPC");

  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0) > 0)
  {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  {
    /* Take ownership of the socket before closing it so the IPC thread cannot close the
     * same (possibly recycled) handle again -- whichever side swaps first does the close. */
    SOCKET s = (SOCKET)InterlockedExchangePointer((PVOID volatile *)&g_sock, (PVOID)INVALID_SOCKET);
    if (s != INVALID_SOCKET)
      closesocket(s);
  }
  if (ipcThread) { WaitForSingleObject(ipcThread, 500); CloseHandle(ipcThread); }
  logf("fake_unity_renderer: clean exit");
  if (g_log) fclose(g_log);
  return 0;
}
