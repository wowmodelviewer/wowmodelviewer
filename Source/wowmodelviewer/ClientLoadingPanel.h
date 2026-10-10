/*
 * ClientLoadingPanel.h
 *
 * THE LOADING PAGE of the chooser ("Choose World of Warcraft"): what a click on a client's card turns the chooser
 * into while that client opens -- one window from the click to the loaded client, in the palette of the theme in use.
 *
 *   Opening Retail
 *   Midnight · 12.1.0
 *
 *   Building the game database...
 *   [=============-----------]
 *   First time with this version of the game: this is done once, later loads reuse it.
 *
 * The bar is determinate when the stage knows its amount of work (files indexed, database tables built), otherwise
 * it slides without a number. A new stage is painted at once (its work may run without a pause); within a stage the
 * page repaints and pumps the event loop at most ~30 times a second, so a stream of values does not flicker. If the
 * load fails, the bar gives way to what went wrong, with Back (to the cards) and Close.
 *
 * The page is as tall as what it says, no more: the chooser fits itself around it while a client loads (onResize
 * says when that height changed) and returns to the cards' size on Back.
 */

#ifndef CLIENTLOADINGPANEL_H
#define CLIENTLOADINGPANEL_H

#include <functional>

#include <wx/panel.h>

#include "ClientLoadProgress.h"

class wxBoxSizer;
class wxStaticText;
class UiButton;
class UiProgressBar;
struct InstalledClient;

class ClientLoadingPanel : public wxPanel, public ClientLoadProgress
{
public:
  explicit ClientLoadingPanel(wxWindow * parent);

  // A load of this client starts: its name and version, the first stage, nothing failed.
  void begin(const InstalledClient & client);
  bool hasFailed() const { return m_failed; }

  void stage(const wxString & text, double fraction = -1.0) override;
  void progress(double fraction) override;
  void detail(const wxString & text) override;
  void failed(const wxString & title, const wxString & message, const wxString & details) override;

  // The failed state's buttons.
  std::function<void()> onBack;
  std::function<void()> onClose;
  // The page's height changed (the failed state, a longer quiet line): the window around it fits it again.
  std::function<void()> onResize;

  // For the tests: what the page says now, and its bar.
  wxString stageText() const;
  UiProgressBar * bar() const { return m_bar; }

private:
  // Repaints the page and lets the event loop run, unless that was done less than a frame ago (force: now).
  void pump(bool force = false);
  // Lays the page out again and has the window fit it (onResize).
  void fit();
  // No quiet line, and only the one line kept for it.
  void clearDetail();

  wxStaticText * m_title = nullptr;
  wxStaticText * m_subtitle = nullptr;
  wxBoxSizer * m_progress = nullptr; // the stage, the bar and the quiet line
  wxStaticText * m_stage = nullptr;
  UiProgressBar * m_bar = nullptr;
  wxStaticText * m_detail = nullptr;
  wxString m_detailText;             // the quiet line as given (the label holds it wrapped)
  int m_detailLine = 0;              // the height kept for the quiet line: one line of its font
  wxBoxSizer * m_failure = nullptr;  // the failed state's message and facts
  wxStaticText * m_message = nullptr;
  wxStaticText * m_details = nullptr;
  wxWindow * m_buttons = nullptr;
  UiButton * m_back = nullptr;
  bool m_failed = false;
  long long m_lastPump = 0;
};

#endif // CLIENTLOADINGPANEL_H
