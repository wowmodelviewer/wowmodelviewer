/*
 * UnityRendererHost.cpp
 */

#include "UnityRendererHost.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>

#include "enums.h"
#include "util.h"

#include "logger/Logger.h"

IMPLEMENT_CLASS(UnityRendererHost, wxPanel)

BEGIN_EVENT_TABLE(UnityRendererHost, wxPanel)
  EVT_SIZE(UnityRendererHost::OnSize)
  EVT_SET_FOCUS(UnityRendererHost::OnSetFocus)
END_EVENT_TABLE()

UnityRendererHost::UnityRendererHost(wxWindow * parent, wxWindowID id)
{
  // A plain panel: the player reparents its own window into this one and paints it
  // entirely, so no wx-side drawing is needed. Black background = unobtrusive while
  // the player is still starting up (or after it exited).
  Create(parent, id, wxDefaultPosition, wxSize(640, 480), wxNO_BORDER | wxCLIP_CHILDREN, wxT("UnityRendererHost"));
  SetBackgroundColour(*wxBLACK);
}

UnityRendererHost::~UnityRendererHost()
{
  shutdown();
}

wxString UnityRendererHost::resolveUnityExePath()
{
  if (!unityRendererPath.IsEmpty())
    return unityRendererPath;

  // Default: tools\unity-renderer\UnityRenderer.exe next to the WMV executable.
  wxFileName exeFn(wxStandardPaths::Get().GetExecutablePath());
  return exeFn.GetPath(wxPATH_GET_VOLUME) + SLASH + wxT("tools") + SLASH +
         wxT("unity-renderer") + SLASH + wxT("UnityRenderer.exe");
}

#ifdef _WINDOWS

namespace
{
  struct FindChildData
  {
    DWORD processId;
    HWND found;
  };

  BOOL CALLBACK findChildByProcessId(HWND child, LPARAM lparam)
  {
    FindChildData * data = reinterpret_cast<FindChildData *>(lparam);
    DWORD pid = 0;
    GetWindowThreadProcessId(child, &pid);
    if (pid == data->processId)
    {
      data->found = child;
      return FALSE; // stop enumerating
    }
    return TRUE;
  }
}

bool UnityRendererHost::launch()
{
  if (isRunning())
    return true;

  const wxString exePath = resolveUnityExePath();
  if (!wxFileName::FileExists(exePath))
  {
    LOG_ERROR << "Unity renderer player not found:" << QString::fromWCharArray(exePath.c_str());
    wxMessageBox(wxString::Format(
                   _("No Unity renderer build was found at:\n\n%s\n\n"
                     "The Unity viewport is optional -- the rest of the application is unaffected.\n\n"
                     "To use it, build the player from Tools\\UnityRendererProject to that location, "
                     "or point \"Tools/UnityRendererPath\" in userSettings\\Config.ini at your build."),
                   exePath.c_str()),
                 _("Unity Renderer not found"), wxOK | wxICON_INFORMATION, this);
    return false;
  }

  // The player's documented embedding contract: "-parentHWND <hwnd> delayed" makes it
  // create its window as a child of ours. The HWND is passed in decimal. A log file in
  // userSettings keeps player output next to WMV's own log.
  wxFileName exeFn(wxStandardPaths::Get().GetExecutablePath());
  const wxString logPath = exeFn.GetPath(wxPATH_GET_VOLUME) + SLASH + wxT("userSettings") + SLASH + wxT("unityRenderer.log");
  wxString cmdLine = wxString::Format(wxT("\"%s\" -parentHWND %llu delayed -logFile \"%s\""),
                                      exePath.c_str(),
                                      (unsigned long long)(uintptr_t)GetHandle(),
                                      logPath.c_str());

  STARTUPINFOW si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));

  // CreateProcess may modify the command-line buffer -> writable copy.
  std::wstring cmdBuf(cmdLine.c_str());
  const std::wstring workDir(wxFileName(exePath).GetPath(wxPATH_GET_VOLUME).c_str());

  if (!CreateProcessW(nullptr, &cmdBuf[0], nullptr, nullptr, FALSE, 0, nullptr,
                      workDir.empty() ? nullptr : workDir.c_str(), &si, &pi))
  {
    const DWORD err = GetLastError();
    LOG_ERROR << "Failed to start Unity renderer player (error" << (int)err << "):"
              << QString::fromWCharArray(exePath.c_str());
    wxMessageBox(wxString::Format(_("Failed to start the Unity renderer player (error %lu):\n\n%s"),
                                  (unsigned long)err, exePath.c_str()),
                 _("Unity Renderer"), wxOK | wxICON_ERROR, this);
    return false;
  }

  CloseHandle(pi.hThread);
  m_process = pi.hProcess;
  m_processId = pi.dwProcessId;
  m_embeddedWnd = nullptr;

  LOG_INFO << "Unity renderer player started (pid" << (int)m_processId << "):"
           << QString::fromWCharArray(exePath.c_str());
  return true;
}

bool UnityRendererHost::isRunning()
{
  if (!m_process)
    return false;
  if (WaitForSingleObject(m_process, 0) == WAIT_TIMEOUT)
    return true;

  // Player exited on its own (or crashed): reap the handle so launch() can start a new one.
  LOG_INFO << "Unity renderer player (pid" << (int)m_processId << ") has exited.";
  CloseHandle(m_process);
  m_process = nullptr;
  m_processId = 0;
  m_embeddedWnd = nullptr;
  return false;
}

void UnityRendererHost::shutdown()
{
  if (!m_process)
    return;

  // Polite first: the embedding contract is to send the player's window WM_CLOSE.
  if (HWND wnd = findEmbeddedWindow())
    PostMessage(wnd, WM_CLOSE, 0, 0);

  // Wait for the exit while STILL PUMPING messages: destroying a window that is parented
  // into our panel makes the player's process send this thread messages synchronously
  // (WM_PARENTNOTIFY etc.); a plain blocking wait would stall the player's DestroyWindow
  // until the timeout fired and needlessly hard-terminate it every time.
  const DWORD start = GetTickCount();
  bool exited = false;
  for (;;)
  {
    const DWORD elapsed = GetTickCount() - start;
    if (elapsed >= 3000)
      break;
    const DWORD wait = MsgWaitForMultipleObjects(1, &m_process, FALSE, 3000 - elapsed, QS_ALLINPUT);
    if (wait == WAIT_OBJECT_0) { exited = true; break; }
    if (wait != WAIT_OBJECT_0 + 1)
      break; // timeout/failure
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
    {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  if (!exited)
  {
    LOG_INFO << "Unity renderer player did not close in time -- terminating (pid" << (int)m_processId << ")";
    TerminateProcess(m_process, 0);
    WaitForSingleObject(m_process, 1000);
  }

  CloseHandle(m_process);
  m_process = nullptr;
  m_processId = 0;
  m_embeddedWnd = nullptr;
}

HWND UnityRendererHost::findEmbeddedWindow()
{
  if (m_embeddedWnd && IsWindow(m_embeddedWnd))
    return m_embeddedWnd;
  m_embeddedWnd = nullptr;

  if (!m_process)
    return nullptr;

  FindChildData data = { m_processId, nullptr };
  EnumChildWindows((HWND)GetHandle(), findChildByProcessId, reinterpret_cast<LPARAM>(&data));
  m_embeddedWnd = data.found;
  return m_embeddedWnd;
}

void UnityRendererHost::resizeEmbeddedWindow()
{
  if (!isRunning())
    return;
  if (HWND wnd = findEmbeddedWindow())
  {
    const wxSize size = GetClientSize();
    MoveWindow(wnd, 0, 0, size.GetWidth(), size.GetHeight(), TRUE);
  }
}

void UnityRendererHost::OnSetFocus(wxFocusEvent & event)
{
  // Hand keyboard focus straight to the embedded player so its input works when the
  // pane is clicked/activated.
  if (HWND wnd = findEmbeddedWindow())
    ::SetFocus(wnd);
  event.Skip();
}

#else // !_WINDOWS

bool UnityRendererHost::launch()
{
  wxMessageBox(_("The embedded Unity renderer is only supported on Windows."),
               _("Unity Renderer"), wxOK | wxICON_INFORMATION, this);
  return false;
}

bool UnityRendererHost::isRunning() { return false; }
void UnityRendererHost::shutdown() {}
void UnityRendererHost::resizeEmbeddedWindow() {}
void UnityRendererHost::OnSetFocus(wxFocusEvent & event) { event.Skip(); }

#endif // _WINDOWS

void UnityRendererHost::OnSize(wxSizeEvent & event)
{
  resizeEmbeddedWindow();
  event.Skip();
}
