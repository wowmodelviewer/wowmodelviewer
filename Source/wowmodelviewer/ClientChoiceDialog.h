/*
 * ClientChoiceDialog.h
 *
 * Startup launcher ("Client Choice"): pick the World of Warcraft folder, the detected
 * product (from .build.info) and the data profile (schema directory), then Load. Shown
 * at startup instead of silently auto-loading the game.
 */
#ifndef CLIENTCHOICEDIALOG_H
#define CLIENTCHOICEDIALOG_H

#include <wx/dialog.h>

#include <vector>

#include <QString>

#include "GameFolder.h" // core::GameConfig

class wxTextCtrl;
class wxChoice;
class wxStaticText;
class wxButton;

class ClientChoiceDialog : public wxDialog
{
public:
  explicit ClientChoiceDialog(wxWindow * parent);

  // Valid after ShowModal() returns wxID_OK:
  wxString dataPath() const { return m_dataPath; }  // "<root>\Data\" for WoWFolder/gamePath
  core::GameConfig selectedConfig() const;          // chosen product / locale / version
  QString selectedProfile() const;                  // schema dir name, e.g. "12.0" ("" = auto)

  // Legacy MPQ path: when true, the user chose "Legacy MPQ client" instead of a modern CASC
  // product. The caller loads mpqFolder() via ModelViewer::LoadWoWFromMpq (the same code path as
  // File -> Load Legacy MPQ Client...); locale is auto-detected. The CASC fields above are unused.
  bool     isLegacyMpq() const { return m_legacyMpq; }
  wxString mpqFolder()   const { return m_mpqFolder; } // WoW root or Data folder (either works)

private:
  void buildUI(const wxString & initialRoot);
  void detect(const wxString & rootFolder);         // parse .build.info -> populate fields
  void populateProfiles();                          // scan games/wow/* schema dirs
  void selectProfileForVersion(const QString & version);

  void onBrowse(wxCommandEvent &);
  void onProductChanged(wxCommandEvent &);
  void onLoad(wxCommandEvent &);
  void onLegacyMpq(wxCommandEvent &);               // choose a pre-CASC (MoPaQ) client instead

  static wxString rootOf(const wxString & anyPath);  // strip a trailing "Data" segment
  static wxString dataPathOf(const wxString & root); // root + "\Data\"

  wxTextCtrl  * m_folder;
  wxStaticText* m_detected;
  wxChoice    * m_product;
  wxChoice    * m_profile;
  wxButton    * m_load;

  std::vector<core::GameConfig> m_configs;     // detected from .build.info
  std::vector<QString>          m_profileDirs; // available schema dirs under games/wow/
  wxString                      m_dataPath;

  bool                          m_legacyMpq;   // user picked the Legacy MPQ option
  wxString                      m_mpqFolder;   // chosen legacy folder (root or Data)

  DECLARE_EVENT_TABLE()
};

#endif /* CLIENTCHOICEDIALOG_H */
