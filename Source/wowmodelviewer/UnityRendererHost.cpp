/*
 * UnityRendererHost.cpp
 */

#include "UnityRendererHost.h"

#include <algorithm>

#include <wx/filename.h>
#include <wx/stdpaths.h>

#include "enums.h"
#include "modelviewer.h"
#include "UiControls.h"
#include "UiStyle.h"
#include "UnityIpcServer.h"
#include "util.h"

#include "logger/Logger.h"

IMPLEMENT_CLASS(UnityRendererHost, wxPanel)

BEGIN_EVENT_TABLE(UnityRendererHost, wxPanel)
  EVT_SIZE(UnityRendererHost::OnSize)
  EVT_SET_FOCUS(UnityRendererHost::OnSetFocus)
  EVT_PAINT(UnityRendererHost::OnPaint)
  EVT_TIMER(wxID_ANY, UnityRendererHost::OnNoticeTimer)
END_EVENT_TABLE()

namespace
{
  // One set of fonts for painting and for placing the button, so the two can never disagree.
  wxFont noticeTitleFont(const wxWindow * w)
  {
    wxFont f = w->GetFont();
    f.SetPointSize(f.GetPointSize() + 5);
    return f;
  }
  wxFont noticeDetailFont(const wxWindow * w)
  {
    wxFont f = w->GetFont();
    f.SetPointSize(f.GetPointSize() + 1);
    return f;
  }
}

// What the viewport area shows where the player's frame is not: a notice, or the notice's dark.
//
// While the player starts the frame paints nothing but the player's background colour (the backdrop): silent. The wait is about a second and the viewer is meant to look like a viewer, so a
// caption explaining that a renderer is starting would be on screen for exactly as long as it takes
// to read and would be the first thing the user ever sees. A dark rectangle that becomes the model
// is better than a dark rectangle that announces itself first, and matching the player's background
// means the handover is not a visible flash either.
//
// Failures are not silent: they are painted here as a notice (see setNotice), with the reason, and
// logged.
void UnityRendererHost::OnPaint(wxPaintEvent & WXUNUSED(event))
{
  wxPaintDC dc(this);
  const bool notice = m_notice && !m_noticeTitle.IsEmpty() && !m_contentShown;
  // The dark the notice's text and button are made for, also with no notice: the player's frame covers this panel
  // while the player is on screen (and paints the backdrop where the player does not), so what this paints is seen
  // only where the player has just gone -- a notice's colour there, never the player's.
  dc.SetBackground(wxBrush(UiStyle::palette().viewport));
  dc.Clear();

  if (!notice)
    return;

  // The notice, centred above its button (layoutNotice places the button).
  const wxRect area = GetClientRect();
  const wxFont titleFont = noticeTitleFont(this);
  const wxFont detailFont = noticeDetailFont(this);

  dc.SetFont(titleFont);
  const wxSize titleSize = dc.GetTextExtent(m_noticeTitle);
  dc.SetFont(detailFont);
  const wxArrayString lines = noticeDetailLines(dc);
  const int lineHeight = dc.GetCharHeight();

  const int gap = FromDIP(8);
  const bool button = m_noticeButton && m_noticeButton->IsShown();
  const int buttonHeight = button ? m_noticeButton->GetSize().y + FromDIP(20) : 0;
  const int total = titleSize.y + gap + (int)lines.size() * lineHeight + buttonHeight;
  int y = area.y + (area.height - total) / 2;

  dc.SetFont(titleFont);
  dc.SetTextForeground(UiStyle::palette().viewportText);
  dc.DrawText(m_noticeTitle, area.x + (area.width - titleSize.x) / 2, y);
  y += titleSize.y + gap;
  dc.SetFont(detailFont);
  dc.SetTextForeground(UiStyle::palette().viewportTextSecondary);
  for (size_t i = 0; i < lines.size(); i++)
  {
    const int width = dc.GetTextExtent(lines[i]).x;
    dc.DrawText(lines[i], area.x + (area.width - width) / 2, y);
    y += lineHeight;
  }
}

wxArrayString UnityRendererHost::noticeDetailLines(wxDC & dc) const
{
  // Explicit line breaks are kept; each paragraph is then wrapped at word boundaries to the panel
  // width less a margin. A single word wider than that (a long path) is left whole on its own line.
  wxArrayString out;
  const int maxWidth = std::max(FromDIP(160), GetClientSize().x - 2 * FromDIP(24));
  const wxArrayString paragraphs = wxSplit(m_noticeDetail, '\n', 0);
  for (size_t p = 0; p < paragraphs.size(); p++)
  {
    const wxArrayString words = wxSplit(paragraphs[p], ' ', 0);
    wxString line;
    for (size_t w = 0; w < words.size(); w++)
    {
      const wxString candidate = line.IsEmpty() ? words[w] : line + wxT(" ") + words[w];
      if (!line.IsEmpty() && dc.GetTextExtent(candidate).x > maxWidth)
      {
        out.Add(line);
        line = words[w];
      }
      else
        line = candidate;
    }
    out.Add(line);
  }
  return out;
}

void UnityRendererHost::setNotice(const wxString & title, const wxString & detail, const wxString & actionLabel,
                                  int actionId)
{
  const bool wantButton = actionId != 0 && !actionLabel.IsEmpty();
  if (m_notice && m_noticeTitle == title && m_noticeDetail == detail && m_noticeActionLabel == actionLabel &&
      m_noticeActionId == actionId)
    return;   // the same notice: nothing to repaint, and no timer to restart

  const bool wasCovered = playerCovered();
  m_notice = true;
  m_noticeTitle = title;
  m_noticeDetail = detail;
  m_noticeActionLabel = actionLabel;
  m_noticeActionId = actionId;

  if (wantButton && !m_noticeButton)
  {
    m_noticeButton = new UiButton(this, wxID_ANY, actionLabel, UiButton::Kind::Primary);
    m_noticeButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
      if (m_noticeActionId == 0 || !GetParent())
        return;
      wxCommandEvent command(wxEVT_MENU, m_noticeActionId);
      wxPostEvent(GetParent(), command);
    });
  }
  if (m_noticeButton)
  {
    m_noticeButton->SetLabel(actionLabel);
    m_noticeButton->SetSize(m_noticeButton->GetBestSize());
    m_noticeButton->Show(wantButton);
  }

  watchEmbeddedVisibility(wasCovered);
  layoutNotice();
  Refresh();
}

void UnityRendererHost::clearNotice()
{
  if (!m_notice)
    return;
  const bool wasCovered = playerCovered();
  m_notice = false;
  if (m_noticeButton)
    m_noticeButton->Show(false);
  watchEmbeddedVisibility(wasCovered);
  Refresh();
}

void UnityRendererHost::watchEmbeddedVisibility(bool wasCovered)
{
  if (playerCovered() && !wasCovered)
  {
    // Covered: the frame is parked (now, or at the hold's release), and stays so whatever the player does.
    if (!m_keyboardReleaseDue)
      m_noticeTimer.Stop();
  }
  else if (!playerCovered() && wasCovered)
  {
    // Uncovered: applied again for a moment -- the player's own window may still be due its show (it is made
    // hidden, some time after launch), and the player has been seen to put back a size it was given while hidden.
    m_noticeTicksLeft = 20;
    m_noticeTimer.Start(100);
  }
  applyEmbeddedVisibility();
}

void UnityRendererHost::setContent(wxWindow * content)
{
  m_content = content;
  if (m_content)
  {
    m_content->Show(m_contentShown);
    m_content->SetSize(GetClientRect());
  }
}

void UnityRendererHost::showContent(bool show)
{
  if (!m_content || m_contentShown == show)
    return;
  const bool wasCovered = playerCovered();
  m_contentShown = show;
  if (show)
  {
    m_content->SetSize(GetClientRect());
    m_content->Show();
    m_content->Raise();
  }
  else
  {
    // The keyboard leaves with it, or it would stay in a hidden window. (A panel's SetFocus keeps the
    // focus where it is when a child of it has it.)
    wxWindow * focus = wxWindow::FindFocus();
    for (wxWindow * w = focus; w; w = w->GetParent())
      if (w == m_content)
      {
        SetFocusIgnoringChildren();
        break;
      }
    m_content->Hide();
  }
  watchEmbeddedVisibility(wasCovered);
  Refresh();
}

void UnityRendererHost::layoutNotice()
{
  if (!m_noticeButton || !m_noticeButton->IsShown())
    return;
  const wxRect area = GetClientRect();
  wxClientDC dc(this);
  dc.SetFont(noticeTitleFont(this));
  const int titleHeight = dc.GetTextExtent(m_noticeTitle.IsEmpty() ? wxString(wxT("X")) : m_noticeTitle).y;
  dc.SetFont(noticeDetailFont(this));
  const int detailHeight = (int)noticeDetailLines(dc).size() * dc.GetCharHeight();
  const wxSize button = m_noticeButton->GetSize();
  const int total = titleHeight + FromDIP(8) + detailHeight + FromDIP(20) + button.y;
  const int y = area.y + (area.height - total) / 2 + titleHeight + FromDIP(8) + detailHeight + FromDIP(20);
  m_noticeButton->Move(area.x + (area.width - button.x) / 2, y);
}

void UnityRendererHost::holdPlayer()
{
  m_playerHold++;
}

void UnityRendererHost::releasePlayer()
{
  if (m_playerHold > 0 && --m_playerHold == 0)
    applyEmbeddedVisibility();
}

void UnityRendererHost::OnNoticeTimer(wxTimerEvent & WXUNUSED(event))
{
  // (A resize or a keyboard release skipped for a hung player keeps at least one tick left: resizeEmbeddedWindow,
  // takeKeyboardFromParkedPlayer.)
  if (m_noticeTicksLeft > 0)
    --m_noticeTicksLeft;
  if (m_keyboardReleaseDue)
    takeKeyboardFromParkedPlayer();
  applyEmbeddedVisibility();
  if (m_noticeTicksLeft == 0)
    m_noticeTimer.Stop();
}

void UnityRendererHost::setPlayerReady(bool ready)
{
  if (m_playerReady == ready)
    return;
  m_playerReady = ready;
  if (ready && m_launchedAtMs != 0)
    LOG_INFO << "Unity renderer ready" << (int)(GetTickCount() - m_launchedAtMs)
             << "ms after launch.";
  // A player that has connected and announced itself is working, whatever was said about it before:
  // the only problem that can precede this is checkPlayerHealth's "did not respond" for a player that
  // was slower to start than it allows. The caller (onUnityReady) then updates the viewport's notice.
  if (ready && !m_playerProblem.IsEmpty())
  {
    LOG_INFO << "Unity renderer announced itself after all; clearing:" << QString::fromWCharArray(m_playerProblem.c_str());
    m_playerProblem.Clear();
  }
  Refresh(false);
  // Its window exists by now, hidden by the "delayed" start: shown now, parked or not (applyEmbeddedVisibility), and
  // again for a moment from the timer.
  if (ready)
  {
    m_noticeTicksLeft = 20;
    m_noticeTimer.Start(100);
    applyEmbeddedVisibility();
  }
}

void UnityRendererHost::setBackdrop(const wxColour & colour)
{
  if (colour == m_backdrop)
    return;
  m_backdrop = colour;
  Refresh(false);
  if (m_playerFrame)
    m_playerFrame->Refresh(false);
}

UnityRendererHost::UnityRendererHost(wxWindow * parent, wxWindowID id)
{
  // A plain panel: the player's window lives in a frame window of its own (m_playerFrame), which paints the
  // backdrop while the player starts. The panel paints only the viewport's dark -- what the notice and its button
  // stand on, and what shows where the player has just gone.
  Create(parent, id, wxDefaultPosition, wxSize(640, 480), wxNO_BORDER | wxCLIP_CHILDREN, wxT("UnityRendererHost"));
  // OnPaint fills every pixel with the notice's dark (and a notice): no erase first.
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  // Still the panel's colour, not the backdrop's: the notice button's corners are drawn in it.
  SetBackgroundColour(UiStyle::palette().viewport);
#ifdef _WINDOWS
  // The player's frame (m_playerFrame): the backdrop wherever the player's window does not cover it (while it
  // starts); the keyboard to the player, as this panel hands it on.
  // Clipped by its siblings too: the texture view, raised above it, is not painted over.
  m_playerFrame = new wxWindow(this, wxID_ANY, wxPoint(0, 0), GetClientSize(),
                               wxBORDER_NONE | wxCLIP_CHILDREN | wxCLIP_SIBLINGS, wxT("unityPlayerFrame"));
  m_playerFrame->SetBackgroundStyle(wxBG_STYLE_PAINT);
  m_playerFrame->Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
    wxPaintDC dc(m_playerFrame);
    if (m_contentShown)
      return;   // the texture view is over it
    dc.SetBackground(wxBrush(m_backdrop.IsOk() ? m_backdrop : UiStyle::palette().viewport));
    dc.Clear();
  });
  // The keyboard as this panel hands it on (OnSetFocus): the panel gives it to its first child, this frame, and wx
  // restores it here after another application had it. On to the texture view while it covers the player, to the
  // player while it is on screen (and no switch holds it); parked behind a notice, it stays here (keys reach the
  // viewer's shortcuts). Never reached by Tab.
  m_playerFrame->DisableFocusFromKeyboard();
  m_playerFrame->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent & event) {
    if (m_contentShown && m_content)
      m_content->SetFocus();
    else if (!m_notice && m_playerOnScreen && m_playerHold == 0)
      if (HWND wnd = findEmbeddedWindow())
        ::SetFocus(wnd);
    event.Skip();
  });
#endif
  m_ipc = new UnityIpcServer();
  m_noticeTimer.SetOwner(this);
}

UnityRendererHost::~UnityRendererHost()
{
  shutdown();
  delete m_ipc;
  m_ipc = nullptr;
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

bool UnityRendererHost::launch(bool selfTest)
{
  if (isRunning())
    return true;

  const wxString exePath = resolveUnityExePath();
  if (!wxFileName::FileExists(exePath))
  {
    LOG_ERROR << "Unity renderer player not found:" << QString::fromWCharArray(exePath.c_str());
    m_playerProblem = wxString::Format(
      _("No Unity renderer build was found at %s. Build the player from Tools\\UnityRendererProject to that "
        "location, or point \"Tools/UnityRendererPath\" in userSettings\\Config.ini at your build."),
      exePath.c_str());
    return false;
  }

  // The player's documented embedding contract: "-parentHWND <hwnd> delayed" makes it
  // create its window as a child of ours. The HWND is passed in decimal. A log file in
  // userSettings keeps player output next to WMV's own log.
  wxFileName exeFn(wxStandardPaths::Get().GetExecutablePath());
  const wxString logPath = exeFn.GetPath(wxPATH_GET_VOLUME) + SLASH + wxT("userSettings") + SLASH + wxT("unityRenderer.log");
  // The parent is the player's frame (m_playerFrame), so parking the frame takes the player off screen at once.
  // (Parked or not, the frame is the panel's size: OnSize.)
  wxString cmdLine = wxString::Format(wxT("\"%s\" -parentHWND %llu delayed -logFile \"%s\""),
                                      exePath.c_str(),
                                      (unsigned long long)(uintptr_t)m_playerFrame->GetHandle(),
                                      logPath.c_str());

  // Runtime IPC: WMV is the server. Start listening BEFORE the player exists and hand it the
  // port; the player connects back and announces unityReady. A server that cannot start is a
  // launch failure: a player given no port runs standalone, is never told what to draw and would
  // sit in the viewport as an empty rectangle with nothing saying why. So it is not started, and
  // the notice says what went wrong (the socket error itself is logged by the server).
  if (!m_ipc || !m_ipc->start())
  {
    LOG_ERROR << "Unity renderer player not started: the IPC server could not start.";
    m_playerProblem = _("WMV could not open the local connection the Unity renderer talks to it over, so the "
                        "renderer was not started. The reason is in userSettings\\log.txt.");
    return false;
  }
  cmdLine += wxString::Format(wxT(" -wmvPort %d"), m_ipc->port());
  // The background, so the player's first frame already shows it (it is sent again once the player connects).
  if (m_backdrop.IsOk())
    cmdLine += wxString::Format(wxT(" -wmvBackground %02X%02X%02X"), m_backdrop.Red(), m_backdrop.Green(),
                                m_backdrop.Blue());

  // Diagnostic runs only: ask the player to also probe the protocol's error paths.
  if (selfTest)
    cmdLine += wxT(" -wmvSelfTest");

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
    if (m_ipc)
      m_ipc->stop(); // nobody will ever connect; don't leave the listener + poll timer running
    LOG_ERROR << "Failed to start Unity renderer player (error" << (int)err << "):"
              << QString::fromWCharArray(exePath.c_str());
    m_playerProblem = wxString::Format(_("The Unity renderer at %s could not be started (Windows error %lu)."),
                                       exePath.c_str(), (unsigned long)err);
    return false;
  }

  CloseHandle(pi.hThread);
  m_process = pi.hProcess;
  m_processId = pi.dwProcessId;
  m_embeddedWnd = nullptr;
  m_launchedAtMs = GetTickCount();
  m_playerReady = false;
  m_playerExpected = true;
  m_playerProblem.Clear();

  LOG_INFO << "Unity renderer player started (pid" << (int)m_processId << "):"
           << QString::fromWCharArray(cmdLine.c_str());
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
  // Closed on purpose (app exit, a restart, the end of a self-test): not a problem to report.
  m_playerExpected = false;
  m_playerReady = false;
  if (!m_process)
  {
    // The player may have exited on its own and been reaped by isRunning(); the IPC listener
    // and its poll timer must not outlive it. stop() is idempotent.
    if (m_ipc)
      m_ipc->stop();
    return;
  }

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
    // Service only cross-thread SENT messages (what the player's DestroyWindow delivers to
    // this thread) plus paint -- never queued input/timers/commands, which could re-enter
    // handlers mid-teardown (shutdown also runs during frame destruction).
    const DWORD wait = MsgWaitForMultipleObjects(1, &m_process, FALSE, 3000 - elapsed, QS_SENDMESSAGE | QS_PAINT);
    if (wait == WAIT_OBJECT_0) { exited = true; break; }
    if (wait != WAIT_OBJECT_0 + 1)
      break; // timeout/failure
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE | PM_QS_SENDMESSAGE | PM_QS_PAINT))
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

  if (m_ipc)
    m_ipc->stop();
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

void UnityRendererHost::resizeEmbeddedWindow(bool puttingBack)
{
  // Not while held, nor while the texture view covers it: back in Models it must have the Models
  // viewport's size, and the player has been seen to put back a size it was given while hidden. Nor while
  // parked behind a notice (its frame is parked, so it draws nowhere meanwhile), except for a load
  // (sizePlayerForLoad): it is sized as it is put back.
  if (!isRunning() || m_playerHold > 0 || m_contentShown || (!m_playerOnScreen && !puttingBack))
    return;
  if (HWND wnd = findEmbeddedWindow())
  {
    const wxSize size = GetClientSize();
    RECT rect;
    if (::GetWindowRect(wnd, &rect) && rect.right - rect.left == size.GetWidth() && rect.bottom - rect.top == size.GetHeight())
      return;
    if (::IsHungAppWindow(wnd))
    {
      // Not waited for. Going on screen (or on it): again from the timer until it responds; parked, as it is put back.
      if (m_playerOnScreen || puttingBack)
      {
        m_noticeTicksLeft = std::max(m_noticeTicksLeft, 1);
        if (!m_noticeTimer.IsRunning())
          m_noticeTimer.Start(100);
      }
      return;
    }
    MoveWindow(wnd, 0, 0, size.GetWidth(), size.GetHeight(), TRUE);
  }
}

void UnityRendererHost::paintPlainNow()
{
  if (m_contentShown)
    return;
  // The children not clipped: a notice button made with the notice has not painted yet, and what is under it is
  // what was under the player too (it paints itself with the notice). The frame is parked, outside the client area.
  const HWND hwnd = (HWND)GetHandle();
  if (HDC dc = ::GetDCEx(hwnd, NULL, DCX_CACHE | DCX_CLIPSIBLINGS))
  {
    RECT rect;
    ::GetClientRect(hwnd, &rect);
    const wxColour & dark = UiStyle::palette().viewport;
    HBRUSH brush = ::CreateSolidBrush(RGB(dark.Red(), dark.Green(), dark.Blue()));
    ::FillRect(dc, &rect, brush);
    ::DeleteObject(brush);
    ::ReleaseDC(hwnd, dc);
  }
}

void UnityRendererHost::sizePlayerForLoad()
{
  if (!m_playerOnScreen)
    resizeEmbeddedWindow(true);
}

bool UnityRendererHost::playerHasKeyboard()
{
  HWND wnd = findEmbeddedWindow();
  const HWND focus = ::GetFocus();
  return wnd && focus && (focus == wnd || ::IsChild(wnd, focus));
}

void UnityRendererHost::releaseKeyboardFromPlayer()
{
  // On the next turn of the event loop -- which comes before the paint that is pending (wx runs these first) -- so
  // that paint is made here first: the move waits on the player's thread, and the notice is not left unpainted meanwhile.
  CallAfter([this]() { takeKeyboardFromParkedPlayer(); });
}

void UnityRendererHost::takeKeyboardFromParkedPlayer()
{
  m_keyboardReleaseDue = false;
  if (m_playerOnScreen || !playerHasKeyboard())
    return;   // put back meanwhile, or it gave the keyboard up itself
  if (::IsHungAppWindow(findEmbeddedWindow()))
  {
    // Not waited for: tried again from the timer until it responds.
    m_keyboardReleaseDue = true;
    m_noticeTicksLeft = std::max(m_noticeTicksLeft, 1);
    if (!m_noticeTimer.IsRunning())
      m_noticeTimer.Start(100);
    return;
  }
  Update();
  if (m_noticeButton && m_noticeButton->IsShown())
    m_noticeButton->Update();
  SetFocusIgnoringChildren();
}

void UnityRendererHost::applyEmbeddedVisibility()
{
  // The frame, once no hold is left (a hold's release applies what is due): covered, parked; uncovered, put back,
  // the player given the viewport's size first (sent, so handled before it is on screen). Checked again on each
  // re-assert: the player has been seen to put back a size it was given while hidden.
  if (m_playerFrame && m_playerHold == 0)
  {
    const bool onScreen = !playerCovered();
    if (onScreen && !m_playerOnScreen)
    {
      // Back: its size first; this panel painted plain under it first (a notice's pixels are still there, and are
      // what shows until the player's window is composed again).
      resizeEmbeddedWindow(true);
      Refresh(false);
      Update();
      m_playerOnScreen = true;
      placePlayerFrame();
    }
    else if (!onScreen && m_playerOnScreen)
    {
      // Off (a notice or the content window put up outside a viewer switch -- a load's, a failure's, a lost player's;
      // a switch's own is coverPlayerNow): this panel plain where it was at once, the notice with the next paint (the
      // content window covers its own area). Not the notice itself here and now: what set it may change it again
      // before that paint.
      m_playerOnScreen = false;
      placePlayerFrame();
      paintPlainNow();
      releaseKeyboardFromPlayer();
      Refresh(false);
    }
    else if (onScreen)
      resizeEmbeddedWindow();
  }
  if (!m_process || m_playerHold > 0 || !m_playerReady)
    return;
  // The player's own window shown, asynchronously, once the player has announced itself (its "delayed" start makes it
  // hidden; by then it has been sent the colour for what is loaded) -- parked too: shown when the frame comes back, the
  // frame's paint (clipped by it) never fills in under it, so what is under it stays this panel's dark.
  if (HWND wnd = findEmbeddedWindow())
    if ((GetWindowLongPtr(wnd, GWL_STYLE) & WS_VISIBLE) == 0)
      ShowWindowAsync(wnd, SW_SHOWNA);
}

void UnityRendererHost::OnSetFocus(wxFocusEvent & event)
{
  // Hand keyboard focus straight to the embedded player so its input works when the
  // pane is clicked/activated (not while it is off screen behind a notice or the content window -- the
  // content window takes it then -- nor while a switch holds it: the move waits on the player's thread).
  if (m_contentShown && m_content)
    m_content->SetFocus();
  else if (!m_notice && m_playerOnScreen && m_playerHold == 0)
    if (HWND wnd = findEmbeddedWindow())
      ::SetFocus(wnd);
  event.Skip();
}

#else // !_WINDOWS

bool UnityRendererHost::launch(bool WXUNUSED(selfTest))
{
  // No player exists for this platform. The viewport's notice says so (see
  // ModelViewer::UpdateUnityViewportState); a dialog on top of that would only repeat it.
  m_playerProblem = _("The Unity viewport is not available on this platform.");
  return false;
}

bool UnityRendererHost::isRunning() { return false; }
void UnityRendererHost::shutdown() { m_playerExpected = false; m_playerReady = false; }
void UnityRendererHost::resizeEmbeddedWindow(bool) {}
void UnityRendererHost::releaseKeyboardFromPlayer() {}
void UnityRendererHost::takeKeyboardFromParkedPlayer() {}
void UnityRendererHost::sizePlayerForLoad() {}
bool UnityRendererHost::playerHasKeyboard() { return false; }
void UnityRendererHost::paintPlainNow() {}
void UnityRendererHost::applyEmbeddedVisibility() {}
void UnityRendererHost::OnSetFocus(wxFocusEvent & event) { event.Skip(); }

#endif // _WINDOWS

// A player that was launched and not closed on purpose, but is gone or never arrived: its process
// exited (a crash, or closed from outside); it is alive and no longer connected, whether it dropped
// before or after announcing itself -- the player does not reconnect, so it would sit there frozen; or
// it is alive and has still not announced itself long after launch (hung on start-up, behind an error
// dialog of its own, or unable to connect). Every one of these would otherwise leave the viewport an
// empty rectangle for as long as the app runs, so each gets the notice with its restart button.
bool UnityRendererHost::checkPlayerHealth()
{
  if (!m_playerExpected || !m_playerProblem.IsEmpty())
    return false;
  if (!isRunning())
  {
    LOG_ERROR << "Unity renderer player exited unexpectedly.";
    m_playerProblem = _("The Unity renderer stopped unexpectedly.");
  }
  else if (m_ipc && !m_ipc->isConnected() && (m_playerReady || m_ipc->stats().connections > 0))
  {
    LOG_ERROR << "Unity renderer player is running but has lost its connection to WMV.";
    m_playerProblem = _("The Unity renderer lost its connection to WMV.");
  }
#ifdef _WINDOWS
  // Generous: a first start after a player rebuild compiles shaders, and a busy start-up (a client
  // loading) can hold this thread. A player that is merely slow is not left behind a false notice for
  // long either: its unityReady clears the problem (setPlayerReady).
  else if (!m_playerReady && m_launchedAtMs != 0 && GetTickCount() - m_launchedAtMs > PLAYER_READY_TIMEOUT_MS)
  {
    LOG_ERROR << "Unity renderer player is running but has not announced itself" << (int)(GetTickCount() - m_launchedAtMs)
              << "ms after launch.";
    m_playerProblem = _("The Unity renderer started but did not respond.");
  }
#endif
  else
    return false;
  m_playerReady = false;
  return true;
}

bool UnityRendererHost::coverPlayerNow()
{
  if (!m_playerFrame || !playerCovered() || !m_playerOnScreen)
    return false;
  m_playerOnScreen = false;
  placePlayerFrame();
  releaseKeyboardFromPlayer();
  return true;
}

void UnityRendererHost::placePlayerFrame()
{
  if (!m_playerFrame)
    return;
  // Parked: the same size, far off any screen (as a minimised window is), where the panel clips it away and no pointer
  // can be over the player's window.
  const wxRect area = GetClientRect();
  m_playerFrame->SetSize(m_playerOnScreen ? area.x : -32000, m_playerOnScreen ? area.y : -32000, area.width, area.height);
}

void UnityRendererHost::OnSize(wxSizeEvent & event)
{
  placePlayerFrame();
  // Parked too (not during a switch, which holds it): it follows the viewport, so what it builds behind a notice is
  // framed for it, and it has presented at that size before it is put back (sized only then, its last frame would be
  // shown scaled to the new size until it presents again).
  resizeEmbeddedWindow(true);
  if (m_content)
    m_content->SetSize(GetClientRect());
  layoutNotice();
  if (m_notice)
    Refresh();
  // The status bar reports the viewport's size, and this is the viewport.
  if (ModelViewer * frame = wxDynamicCast(GetParent(), ModelViewer))
    frame->UpdateCanvasStatus();
  event.Skip();
}
