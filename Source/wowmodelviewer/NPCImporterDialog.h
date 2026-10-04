/*
 * NPCImporterDialog.h
 *
 * CHARACTER > LOAD NPC / MODEL...: one dialog, two ways in (a tab strip, UiTabBar):
 *   URL  a Wowhead NPC link. The page gives the NPC's ID, name, type and display ID (WowheadImporter, a network
 *        request); the caller then loads that NPC as it always has (ModelViewer::LoadNPCByDisplay).
 *   ID   an ID of a type the user picks (ModelIdLookup): an M2 FileDataID, a Creature Display ID or a Creature Model
 *        ID, resolved in the loaded client and loaded by the dialog, which stays open to say why when it cannot.
 * The page and the ID type last chosen are remembered until the viewer closes (not the ID).
 *
 * View > View NPC's "Import from URL" button uses the link half alone (Use::ImportLink): it reads the NPC from the page
 * and the chooser loads it (getImportedId, getNPCLine).
 */

#ifndef _NPCIMPORTERDIALOG_H_
#define _NPCIMPORTERDIALOG_H_

#include <wx/dialog.h>

class wxButton;
class wxChoice;
class wxPanel;
class wxSimplebook;
class wxStaticText;
class wxTextCtrl;
class UiTabBar;

#include <QString>

class NPCimporterDialog : public wxDialog
{
  public:
  enum class Use
  {
    Load,         // Character > Load NPC / Model...: a link or an ID
    ImportLink    // View NPC's "Import from URL": a link only, loaded by the caller
  };

  NPCimporterDialog(wxWindow * parent, Use use);

  // The NPC a link gave: its ID (-1 when none), and "id,displayId,type,name".
  int getImportedId();
  QString getNPCLine();

  private:
  void OnLoad();
  bool importLink();   // the URL page: reads the NPC from the page; false, with the reason shown, when it cannot
  bool loadById();     // the ID page: resolves and loads; false, with the reason shown, when it cannot
  void showKindNote();
  void setHint(const wxString & text, bool warning);
  void setBusy(bool busy);
  bool idPageShown() const;

  Use m_use;
  wxPanel * m_panel = nullptr;
  UiTabBar * m_tabs = nullptr;
  wxSimplebook * m_book = nullptr;
  wxTextCtrl * m_url = nullptr;
  wxChoice * m_kind = nullptr;
  wxStaticText * m_kindNote = nullptr;
  wxTextCtrl * m_id = nullptr;
  wxStaticText * m_hint = nullptr;
  wxButton * m_load = nullptr;
  wxButton * m_cancel = nullptr;
  bool m_busy = false;
  bool m_abandoned = false;   // cancelled while busy: what the work brings back is dropped

  // What a link gave.
  int m_npcId = -1;
  int m_npcDisplayId = 0;
  int m_npcType = 0;
  QString m_npcName;
};

#endif /* _NPCIMPORTERDIALOG_H_ */
