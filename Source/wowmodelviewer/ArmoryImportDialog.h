/*
 * ArmoryImportDialog.h
 *
 * Character > Import Armory Character. A character is named by region, realm and name -- the
 * realm picked from the region's realm list when the Armory proxy serves one, typed otherwise --
 * and imported through the same importer plugin a pasted Armory link goes through: the fields
 * become exactly such a link. Pasting a link stays available underneath.
 *
 * Every outcome (looking up, imported, why it failed) is shown in the dialog itself, and the
 * dialog stays open on the imported character, so the next one can be typed straight away.
 * The last region and realm that imported and the last few characters are kept in the [Armory]
 * group of Config.ini; the realm lists are cached next to it in ArmoryRealms.json.
 */

#ifndef ARMORYIMPORTDIALOG_H
#define ARMORYIMPORTDIALOG_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <QString>
#include <QVector>

class ModelViewer;
class wxComboBox;

class ArmoryImportDialog : public wxDialog
{
public:
  explicit ArmoryImportDialog(ModelViewer * viewer);

private:
  struct Region
  {
    QString code;   // as the importer and the proxy want it: eu, us, kr, tw
    wxString label; // Europe, United States, ...
  };

  struct Realm
  {
    QString slug; // as the profile API wants it, e.g. "argent-dawn"
    QString name; // as people know it, e.g. "Argent Dawn"
  };

  struct Recent
  {
    QString name;      // the character, as the server spells it
    QString realmSlug;
    QString realmName;
    QString region;
  };

  enum class Tone { Working, Success, Error };

  void buildLayout();
  void loadSettings();
  void rememberSuccess(const Recent & entry);
  void fillRecent();
  void loadRealms(bool allowFetch);
  void fillRealmChoices();
  QVector<Realm> knownRealms() const;
  const Region * currentRegion() const;
  wxString regionLabel(const QString & code) const;

  // What the fields would send: the realm slug and its display name, and the character name
  // ready for the request. False when a field is empty or cannot be sent; hint then says why
  // (warn) or, for a realm turned into a slug, what will be sent instead.
  bool resolveRealm(QString & slug, QString & displayName, wxString & hint, bool & warn) const;
  bool resolveName(QString & name, wxString & hint) const;
  void refreshState();

  void onImportCharacter(const char * trigger);
  void onImportLink();
  void onRecentChosen();
  void onRegionChosen();
  void runImport(const wxString & link, const wxString & lookingUp, wxWindow * origin, bool fromLink);
  void setBusy(bool busy);
  void showStatus(Tone tone, const wxString & title, const wxString & detail);
  void growToFit();

  ModelViewer * m_viewer;
  QVector<Region> m_regions;
  QVector<Recent> m_recent;
  QVector<Realm> m_realms;        // the selected region's realm list, when one is cached
  bool m_realmListLoaded = false; // m_realms is the region's whole list, not just remembered realms
  bool m_busy = false;

  wxStaticText * m_recentLabel = nullptr;
  wxChoice * m_recentChoice = nullptr;
  wxChoice * m_region = nullptr;
  wxComboBox * m_realm = nullptr;
  wxTextCtrl * m_name = nullptr;
  wxStaticText * m_hint = nullptr;
  wxButton * m_import = nullptr;
  wxStaticText * m_statusTitle = nullptr;
  wxStaticText * m_statusDetail = nullptr;
  wxTextCtrl * m_link = nullptr;
  wxButton * m_importLink = nullptr;
  wxButton * m_close = nullptr;
  int m_contentWidth = 0;
};

#endif // ARMORYIMPORTDIALOG_H
