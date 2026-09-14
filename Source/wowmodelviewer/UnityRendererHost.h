/*
 * UnityRendererHost.h
 *
 * Embedded Unity renderer viewport -- THE viewport of WMV. It is always the centre pane of the
 * main window; the OpenGL ModelCanvas is archived as a hidden internal service (GL context for
 * texture decoding, model ownership, animation clock) and is never shown. Unity renders
 * DIRECTLY FROM WOW DATA: it requests raw assets and metadata from WMV over IPC (asset-access /
 * runtime APIs) -- there is no OBJ/FBX/GLB export step. WMV provides the app UI, the active
 * client/profile, CASC/MPQ access, DB/metadata and the runtime commands; Unity provides the
 * rendering pipeline. See docs/unity-renderer/README.md.
 *
 * Mechanics: hosts a SEPARATELY BUILT Unity standalone player inside a plain wxPanel by
 * launching it with the player's documented embedding arguments ("-parentHWND <hwnd>
 * delayed"): the player reparents itself into this panel's native window and renders there
 * with its own graphics device, in its own process. Because it is out-of-process and draws
 * into ITS OWN child window, the app's single WGL context stays bound to the hidden ModelCanvas
 * HWND and nothing here calls into GL at all.
 *
 * The player build is never part of the WMV build. When it is missing, cannot be started, exits
 * or drops its connection, the panel paints a NOTICE saying so, with a button that restarts it;
 * content the player cannot draw yet gets a notice of its own (see setNotice). The exe location
 * is the "Tools/UnityRendererPath" setting when set, else tools\unity-renderer\UnityRenderer.exe
 * next to the WMV executable. See Tools/UnityRendererProject/ for the player project.
 */

#ifndef UNITYRENDERERHOST_H
#define UNITYRENDERERHOST_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include <wx/timer.h>

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
  // to the player (-wmvPort). Returns false when the exe is missing, the IPC server cannot start
  // or the process cannot be started; the reason is logged and kept in playerProblem() for the
  // viewport's notice. No dialog is ever shown: a modal box per load was how a missing player used
  // to be reported.
  //
  // selfTest additionally passes -wmvSelfTest, which asks a diagnostic-capable player (the
  // TestStub) to exercise the protocol's negative paths -- a missing asset and an unknown
  // message type. Normal launches never request those, so the viewport only ever shows the
  // real model exchange.
  bool launch(bool selfTest = false);

  // The runtime IPC channel to the player (always valid; listening only while launched).
  UnityIpcServer * ipc() const { return m_ipc; }

  // True while the embedded player process is alive (reaps the handle when it has exited,
  // so a crashed/closed player can simply be relaunched).
  bool isRunning();

  // Close the embedded player: polite WM_CLOSE to its window first, hard-terminate as a
  // fallback. Called from the host frame's OnClose, from a restart and from the destructor. A
  // player closed here was meant to stop, so it is not reported as a problem.
  void shutdown();

  // WHY THE PLAYER IS NOT THERE, in words for the viewport notice: the build is missing, it could
  // not be started, it exited, it never responded, or it stopped talking to WMV. Empty while the
  // player is running (or was never asked to run). Set by launch() and checkPlayerHealth(); cleared
  // by a launch that succeeds and by the player announcing itself.
  const wxString & playerProblem() const { return m_playerProblem; }

  // Notice a player that has exited or dropped its connection since it was launched, or that has
  // still not announced itself PLAYER_READY_TIMEOUT_MS after launch. Cheap, meant to be polled from
  // an existing timer. Returns true when this call found a NEW problem, so the caller knows to
  // repaint the viewport's state.
  bool checkPlayerHealth();
  static const unsigned long PLAYER_READY_TIMEOUT_MS = 30000;

  // The player has connected and announced itself, so its window is up and this panel's own
  // painting is no longer what the user sees. Until then the panel paints only the player's
  // background colour underneath (or a notice, when one is up). Announcing itself also clears a
  // "did not respond" problem left by a player that was slower to start than checkPlayerHealth
  // allows.
  void setPlayerReady(bool ready);
  bool isPlayerReady() const { return m_playerReady; }

  // THE VIEWPORT NOTICE. Whenever the player's own window is not what should be on screen, the
  // panel paints a title, a detail line and (optionally) one button instead:
  //   - nothing is loaded yet: the empty viewer's prompt, whose button opens a model;
  //   - what is loaded is something the Unity viewport cannot draw yet (a map tile, a mounted
  //     character, a WMO on an older player...): what it is and that it cannot be shown, no button;
  //   - the player is missing, could not start, exited or disconnected: why, with a button that
  //     restarts it.
  // This is the host panel's own painting. The player's window is only HIDDEN meanwhile -- the
  // process keeps running with whatever it last built -- and is shown again the moment the notice
  // is cleared. Nothing is sent to the player and nothing in it changes.
  //
  // actionId is the menu command the button posts to the frame (for example ID_UI_OPEN_MODEL);
  // 0 or an empty label means no button. Setting the same notice again is cheap and repaints
  // nothing.
  void setNotice(const wxString & title, const wxString & detail, const wxString & actionLabel = wxString(),
                 int actionId = 0);
  void clearNotice();
  bool hasNotice() const { return m_notice; }
  const wxString & noticeTitle() const { return m_noticeTitle; }

private:
  void OnSize(wxSizeEvent & event);
  void OnSetFocus(wxFocusEvent & event);
  void OnPaint(wxPaintEvent & event);
  void OnNoticeTimer(wxTimerEvent & event);
  void layoutNotice();
  // The detail text broken into lines that fit the panel, so a long path or reason stays readable
  // instead of running off both edges.
  wxArrayString noticeDetailLines(wxDC & dc) const;
  // Hide the player's window while a notice is up, show it otherwise. Asynchronous, so a player
  // that is busy starting up can never stall this thread.
  void applyEmbeddedVisibility();

  bool m_notice = false;
  wxString m_noticeTitle, m_noticeDetail, m_noticeActionLabel;
  int m_noticeActionId = 0;
  wxButton * m_noticeButton = nullptr;
  wxTimer m_noticeTimer;   // the player creates its window some time after launch: hide it when it appears
  int m_noticeTicksLeft = -1;

  // See playerProblem(). m_playerExpected is true from a successful launch until shutdown(), so an
  // exit in between is told apart from a player that was closed on purpose.
  wxString m_playerProblem;
  bool m_playerExpected = false;

  // False until the player reports in. Only affects what this panel paints underneath it.
  bool m_playerReady = false;
  // When the process was started, so the log can say how long the user waited for it. That
  // number is the whole reason the viewport is started at app launch rather than on demand.
  unsigned long m_launchedAtMs = 0;

  // The player does not follow the parent's size on its own; the host must resize the
  // embedded child window whenever the panel resizes (it fills the whole client area).
  void resizeEmbeddedWindow();

  UnityIpcServer * m_ipc = nullptr; // owned

#ifdef _WINDOWS
  // The player's top-level-turned-child window inside this panel (found lazily: the
  // player creates it asynchronously after process start).
  HWND findEmbeddedWindow();

  HANDLE m_process = nullptr; // owned; nullptr when no player has been launched
  DWORD  m_processId = 0;
  HWND   m_embeddedWnd = nullptr;
#endif
};

#endif // UNITYRENDERERHOST_H
