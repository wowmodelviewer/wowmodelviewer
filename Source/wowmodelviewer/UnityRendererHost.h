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
 * delayed"): the player reparents itself into a frame window of this panel's own and renders
 * there with its own graphics device, in its own process. Because it is out-of-process and draws
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

  // The player has connected and announced itself, so its window is up (and is shown now) and the
  // frame's painting is no longer what the user sees. Until then the frame paints only the player's
  // background colour (or the panel a notice, when one is up). Announcing itself also clears a
  // "did not respond" problem left by a player that was slower to start than checkPlayerHealth
  // allows.
  void setPlayerReady(bool ready);
  bool isPlayerReady() const { return m_playerReady; }

  // THE BACKDROP: the colour the player clears to (ModelViewer::viewportBackgroundShown). The player's frame paints it
  // wherever the player's window does not cover it -- while the player starts, so its first frame is not a change of
  // colour -- and a player launched after this is given it on its command line ("-wmvBackground RRGGBB").
  // A notice keeps the palette's viewport colour, which its text and button are made for. Invalid: that colour too,
  // and nothing on the command line.
  void setBackdrop(const wxColour & colour);

  // THE VIEWPORT NOTICE. Whenever the player's own window is not what should be on screen, the
  // panel paints a title, a detail line and (optionally) one button instead:
  //   - nothing is loaded yet: the empty viewer's prompt, whose button opens a model;
  //   - what is loaded is something the Unity viewport cannot draw yet (a mounted character, a
  //     WMO on an older player...): what it is and that it cannot be shown, no button;
  //   - the player is missing, could not start, exited or disconnected: why, with a button that
  //     restarts it.
  // This is the host panel's own painting. The player is only taken OFF SCREEN meanwhile -- the process
  // keeps running with whatever it last built -- and put back when the notice is cleared, both at once:
  // its window lives in a frame window of this panel's own, which is what is moved out of the panel's
  // client area and back (see applyEmbeddedVisibility). Nothing is sent to the player and nothing in it
  // changes.
  //
  // actionId is the menu command the button posts to the frame (for example ID_UI_MODELS);
  // 0 or an empty label means no button. Setting the same notice again is cheap and repaints
  // nothing.
  void setNotice(const wxString & title, const wxString & detail, const wxString & actionLabel = wxString(),
                 int actionId = 0);
  void clearNotice();
  bool hasNotice() const { return m_notice; }
  const wxString & noticeTitle() const { return m_noticeTitle; }

  // THE CONTENT WINDOW. A window of the app's own that takes the viewport's place -- the texture view,
  // for a texture picked in Browse -- covering the panel, the player and any notice. As with a notice,
  // the player is only taken off screen meanwhile (its frame) and nothing is sent to it. The window is a child of
  // this panel, sized with it; showContent(false) gives the viewport back.
  void setContent(wxWindow * content);
  void showContent(bool show);
  bool isShowingContent() const { return m_contentShown; }

  // THE HOLD (a viewer-mode switch). While held, the player is neither taken off screen, put back nor
  // resized; releasing the last hold applies what is due -- its size, then put back; or taken off screen
  // with the panel painted where it was -- with the new workspace's repaint, so the switch is one change
  // on screen (a switch ending on a notice takes it off at the very end of the freeze: coverPlayerNow;
  // taken off earlier, the frozen panel could not paint there until the switch is over, and its old pixels
  // would show). The player's window is not resized while the texture view covers it (it
  // keeps the size of the viewer it was covered from); it is sized to the viewport again just before it is put back.
  void holdPlayer();
  void releasePlayer();
  // The player's frame (see m_playerFrame), or null where there is no player (not Windows).
  wxWindow * playerFrame() const { return m_playerFrame; }
  // A load about to be sent to the player: while it is parked (behind a notice) since a switch -- which does not size
  // it -- its window given the viewport's size, so what it builds is framed for the viewport's shape (it frames by its
  // own aspect, once, when built). Sent and waited for -- one frame of the player's at most -- like every resize of it.
  // On screen it has the viewport's size already.
  void sizePlayerForLoad();
  // The keyboard is in the player's window. Asked without asking that window anything: wxWindow::FindFocus sends it
  // WM_GETDLGCODE to find a wx window for it, which waits on the player's thread (once per frame it draws).
  bool playerHasKeyboard();
  // A viewer switch ending on a notice (held): the player's frame off screen now, at the end of the freeze -- the
  // old workspace stays whole until then, and the switch's repaint paints the notice where it was. Its release finds
  // it done. True when it was on screen until now: paintPlainNow once the freeze is over.
  bool coverPlayerNow();
  // Where the player was, this panel plain (the notice's dark, no text) at once -- before any other work, a load's
  // own (a building picked in Models loads after the switch, before the repaint) -- not the pixels that were under
  // the player's window, which show from the moment it goes until this panel paints. The notice follows with the
  // next paint. Not while the content window covers it (it is what shows there).
  void paintPlainNow();

private:
  void OnSize(wxSizeEvent & event);
  void OnSetFocus(wxFocusEvent & event);
  void OnPaint(wxPaintEvent & event);
  void OnNoticeTimer(wxTimerEvent & event);
  void layoutNotice();
  // The detail text broken into lines that fit the panel, so a long path or reason stays readable
  // instead of running off both edges.
  wxArrayString noticeDetailLines(wxDC & dc) const;
  // The player off screen while a notice or the content window is up, on screen otherwise, once no hold is left:
  // its frame window moved, from this thread, which never waits for the player to do so. (Putting it back waits for a
  // resize of the player's window when its size has to change.)
  void applyEmbeddedVisibility();
  // The frame at the panel's client area, or parked far off any screen, as m_playerOnScreen says.
  void placePlayerFrame();
  // A player's window that has the keyboard as it is taken off screen gives it up on the next turn of the event loop,
  // after this panel has painted (it takes the keyboard, and hands it on as OnSetFocus does): keys must not go to a
  // window no one sees. The move waits for the player's thread, as a click elsewhere does -- not for one Windows
  // considers hung (tried again from the timer until it responds: takeKeyboardFromParkedPlayer).
  void releaseKeyboardFromPlayer();
  void takeKeyboardFromParkedPlayer();
  bool m_keyboardReleaseDue = false;
  // The player is off screen (its frame) while a notice or the content window is in front of it.
  bool playerCovered() const { return m_notice || m_contentShown; }
  // After the player is uncovered, and once it is ready, the visibility is applied again for a moment from the
  // timer: the player makes its window some time after launch, hidden ("delayed"), and has been seen to put back a
  // size it was given while hidden.
  void watchEmbeddedVisibility(bool wasCovered);

  wxWindow * m_content = nullptr;
  bool m_contentShown = false;

  // THE PLAYER'S FRAME: a child window of this panel, its size, that the player's window is made a child of
  // ("-parentHWND"). Parked far off any screen (x = -32000, where no pointer can be: the player tells whether the
  // pointer is over it from position alone) and put back, it takes the player off screen and back synchronously: the player's own window handles messages on the player's thread, once per frame it draws, and
  // a hide posted to it landed 50 ms and more later on a desktop where it is busy presenting -- the other viewer's
  // model or building stayed on screen, in its colour, until then. Moved, not hidden: hiding an ancestor of the
  // window with the keyboard (the player's, often) moves the keyboard, which waits on the player's thread. (The
  // player's window does not clip itself against its siblings -- WS_CHILD | WS_VISIBLE only -- so a window raised
  // above it is no cover while it is on screen.) Windows only.
  wxWindow * m_playerFrame = nullptr;
  bool m_playerOnScreen = true;

  bool m_notice = false;
  wxString m_noticeTitle, m_noticeDetail, m_noticeActionLabel;
  int m_noticeActionId = 0;
  wxButton * m_noticeButton = nullptr;
  wxTimer m_noticeTimer;   // watchEmbeddedVisibility
  int m_noticeTicksLeft = -1;
  int m_playerHold = 0;     // holdPlayer() count

  // See playerProblem(). m_playerExpected is true from a successful launch until shutdown(), so an
  // exit in between is told apart from a player that was closed on purpose.
  wxString m_playerProblem;
  bool m_playerExpected = false;

  // False until the player reports in: its window is shown from then on (applyEmbeddedVisibility).
  bool m_playerReady = false;
  wxColour m_backdrop;   // setBackdrop
  // When the process was started, so the log can say how long the user waited for it. That
  // number is the whole reason the viewport is started at app launch rather than on demand.
  unsigned long m_launchedAtMs = 0;

  // The player does not follow the parent's size on its own; the host must resize the
  // embedded child window whenever the panel resizes (it fills the whole client area).
  // Sent to the player's thread and waited for, never posted: a posted resize is handled after any sent later, and
  // could land after the one sent as it is put back, leaving it at the old size. (The threads' input queues are
  // attached -- a parent and child across processes -- so SWP_ASYNCWINDOWPOS would not post anyway.) Parked, only when
  // asked (puttingBack: OnSize, sizePlayerForLoad, the put-back) -- not by the re-asserts. Never for a player Windows
  // considers hung (no message taken for 5 s, e.g. a long build): this thread would hang with it. On screen it is then
  // tried again from the timer until it responds; parked, as it is put back.
  void resizeEmbeddedWindow(bool puttingBack = false);

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
