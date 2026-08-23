/*
 * UnityRendererHost.h
 *
 * Embedded Unity renderer viewport -- the NEW RENDERER FOUNDATION for WMV. Direction: the
 * embedded Unity player is to become the baseline / primary rendering viewport; the existing
 * OpenGL ModelCanvas is kept as the LEGACY / FALLBACK renderer during the migration and gets
 * maintenance only. Future rendering work (characters, equipment, maps, fog, OBS scenes,
 * stream features) targets Unity first. Unity renders DIRECTLY FROM WOW DATA: it requests raw
 * assets and metadata from WMV over IPC (asset-access / runtime APIs) -- there is no
 * OBJ/FBX/GLB export step. WMV provides the app UI, the active client/profile, CASC/MPQ
 * access, DB/metadata and the runtime commands; Unity provides the modern rendering
 * pipeline. See docs/unity-renderer/README.md.
 *
 * Mechanics: hosts a SEPARATELY BUILT Unity standalone player inside a plain wxPanel by
 * launching it with the player's documented embedding arguments ("-parentHWND <hwnd>
 * delayed"): the player reparents itself into this panel's native window and renders there
 * with its own graphics device, in its own process. Because it is out-of-process and draws
 * into ITS OWN child window, the legacy OpenGL viewport is untouched -- the app's single WGL
 * context stays bound to the ModelCanvas HWND and nothing here calls into GL at all.
 *
 * Current migration stage (V1, runtime asset access over the IPC server below): the player
 * build is OPTIONAL at build and run time and never
 * part of the WMV build -- if the exe is absent, launch() shows a clear message and the app
 * (and the legacy viewport) continue normally. The exe location is the
 * "Tools/UnityRendererPath" setting when set, else tools\unity-renderer\UnityRenderer.exe
 * next to the WMV executable. See Tools/UnityRendererProject/ for the player project.
 */

#ifndef UNITYRENDERERHOST_H
#define UNITYRENDERERHOST_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#ifdef _WINDOWS
#include <windows.h>
#include <wx/msw/winundef.h>
#endif

class UnityIpcServer;

class UnityRendererHost : public wxPanel
{
    DECLARE_CLASS(UnityRendererHost)
    DECLARE_EVENT_TABLE()

public:
  UnityRendererHost(wxWindow * parent, wxWindowID id);
  ~UnityRendererHost();

  // Full path to the player exe: the user's Tools/UnityRendererPath override when set,
  // else <WMV exe dir>\tools\unity-renderer\UnityRenderer.exe. Existence is not checked.
  static wxString resolveUnityExePath();

  // Launch the player embedded in this panel. Safe to call repeatedly (no-op while the
  // player is already running). Starts the localhost IPC server first and passes its port
  // to the player (-wmvPort). Returns false after showing a non-fatal error message (or only
  // logging it when showErrors is false, e.g. headless runs) when the exe is missing or the
  // process cannot be started.
  //
  // selfTest additionally passes -wmvSelfTest, which asks a diagnostic-capable player (the
  // TestStub) to exercise the protocol's negative paths -- a missing asset and an unknown
  // message type. Normal launches never request those, so the viewport only ever shows the
  // real model exchange.
  bool launch(bool showErrors = true, bool selfTest = false);

  // The runtime IPC channel to the player (always valid; listening only while launched).
  UnityIpcServer * ipc() const { return m_ipc; }

  // True while the embedded player process is alive (reaps the handle when it has exited,
  // so a crashed/closed player can simply be relaunched).
  bool isRunning();

  // Close the embedded player: polite WM_CLOSE to its window first, hard-terminate as a
  // fallback. Called from the host frame's OnClose and from the destructor.
  void shutdown();

  // The player has connected and announced itself, so its window is up and this panel's own
  // painting is no longer what the user sees. Until then the panel says what it is waiting for
  // rather than showing an empty rectangle.
  void setPlayerReady(bool ready);
  bool isPlayerReady() const { return m_playerReady; }

private:
  void OnSize(wxSizeEvent & event);
  void OnSetFocus(wxFocusEvent & event);
  void OnPaint(wxPaintEvent & event);

  // False until the player reports in. Only affects what this panel paints underneath it.
  bool m_playerReady = false;
  // When the process was started, so the log can say how long the user waited for it. That
  // number is the whole reason the viewport is started at app launch rather than on demand.
  unsigned long m_launchedAtMs = 0;

  // The player does not follow the parent's size on its own; the host must resize the
  // embedded child window whenever the panel resizes (it fills the whole client area).
  void resizeEmbeddedWindow();

#ifdef _WINDOWS
  // The player's top-level-turned-child window inside this panel (found lazily: the
  // player creates it asynchronously after process start).
  HWND findEmbeddedWindow();

  UnityIpcServer * m_ipc = nullptr; // owned

  HANDLE m_process = nullptr; // owned; nullptr when no player has been launched
  DWORD  m_processId = 0;
  HWND   m_embeddedWnd = nullptr;
#endif
};

#endif // UNITYRENDERERHOST_H
