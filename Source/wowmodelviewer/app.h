#ifndef APP_H
#define APP_H

#include <wx/wxprec.h>

#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#ifndef _WINDOWS
  #include "../../bin_support/Icons/wmv.xpm"
#endif

// headers
#include "modelviewer.h"

// vars
static wxString langNames[] =
{
  wxT("English"),
  wxT("Korean"),
  wxT("French"),
  wxT("German"),
  wxT("Simplified Chinese"),
  wxT("Traditional Chinese"),
  wxT("Spanish (EU)"),
  wxT("Spanish (Latin American)"),
  wxT("Russian"),
};

static const wxLanguage langIds[] =
{
  wxLANGUAGE_ENGLISH,
  wxLANGUAGE_KOREAN,
  wxLANGUAGE_FRENCH,
  wxLANGUAGE_GERMAN,
  wxLANGUAGE_CHINESE_SIMPLIFIED,
  wxLANGUAGE_CHINESE_TRADITIONAL,
  wxLANGUAGE_SPANISH,
  wxLANGUAGE_SPANISH,
  wxLANGUAGE_RUSSIAN,
};


 

class WowModelViewApp : public wxApp
{
public:
    virtual bool OnInit();
  virtual int OnExit();
  // Also on a headless run's exit (OnInit returned false): the game database is closed, so a database built for the
  // session only (a client still downloading) is deleted rather than left in dbcache/.
  void CleanUp() override;
  virtual void OnUnhandledException();
  virtual void OnFatalException();
  // wx 3.x keeps asserts on in release and pops a modal dialog by default; log + continue instead.
  virtual void OnAssertFailure(const wxChar *file, int line, const wxChar *func, const wxChar *cond, const wxChar *msg);
  void setInterfaceLocale();

  //virtual bool OnExceptionInMainLoop();
  //virtual void HandleEvent(wxEvtHandler *handler, wxEventFunction func, wxEvent& event) const ; 

  void LoadSettings();
  void SaveSettings();

  ModelViewer *frame;
  
  wxLocale locale; 
};

void searchMPQs(bool firstTime);

#endif

