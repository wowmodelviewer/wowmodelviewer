/*
 * ClientLoadProgress.h
 *
 * WHAT THE USER IS TOLD WHILE A CLIENT LOADS (ModelViewer::LoadWoW). A load reports a few stages the user can relate
 * to ("Reading game files..."), how far the current one is when that is known, and -- if it cannot finish -- why.
 * The only surface is the chooser's loading page (ClientLoadingPanel); a load without one (headless) reports nothing.
 *
 * Every call may repaint and pump the event loop, at most every few hundredths of a second, so the window stays
 * responsive during a long stage. Only the loading surface can be used meanwhile: the chooser is modal and the load
 * refuses to start a second one (ModelViewer::m_clientLoading).
 */

#ifndef CLIENTLOADPROGRESS_H
#define CLIENTLOADPROGRESS_H

#include <wx/string.h>

class ClientLoadProgress
{
public:
  virtual ~ClientLoadProgress() {}

  // A new stage: its text, and how far it is when that is already known (as progress()); otherwise the bar moves
  // without a number until the stage says. It is on screen at once.
  virtual void stage(const wxString & text, double fraction = -1.0) = 0;
  // How far the stage is, 0..1; below 0 while its amount of work is not known (the bar slides, no number).
  virtual void progress(double fraction) = 0;
  // A quiet second line under the bar (why this stage takes longer than usual), or none.
  virtual void detail(const wxString & text) = 0;
  // The load could not finish: the bar gives way to what went wrong and the facts for whoever looks into it.
  virtual void failed(const wxString & title, const wxString & message, const wxString & details) = 0;
};

#endif // CLIENTLOADPROGRESS_H
