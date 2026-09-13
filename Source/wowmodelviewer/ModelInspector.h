/*
 * ModelInspector.h
 *
 * The "Model" panel on the right of the window: everything about the thing on screen, in three
 * tabs, adapting to what is loaded.
 *
 *   Appearance  a creature or item's skins and per-slot texture overrides; a character's
 *               customization, equipment and tabard (the character control lives here now); a
 *               WMO's doodad sets.
 *   Geosets     the model's submeshes by geoset group, with a checkbox each.
 *   Info        name, path, FileDataID and the model's sizes.
 *
 * It owns no model state. The controls on the Appearance page belong to AnimControl and
 * CharControl, which still drive them exactly as before; this class only arranges them and
 * decides which sections apply. The geoset checkboxes write the same display flags the old geoset
 * tree toggled, and push them to the Unity viewport through the same skin message every other
 * geoset change uses.
 */

#ifndef MODELINSPECTOR_H
#define MODELINSPECTOR_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include <wx/collpane.h>
#include <wx/timer.h>

#include <vector>

class AnimControl;
class CharControl;
class WoWModel;
class Attachment;
class wxBookCtrlEvent;
class wxChoice;
class wxFlexGridSizer;
class wxNotebook;
class wxScrolledWindow;
class wxSearchCtrl;
class wxTreeListCtrl;
class wxTreeListEvent;

class ModelInspector : public wxPanel
{
public:
  enum Context
  {
    CONTEXT_NONE,        // nothing loaded
    CONTEXT_MODEL,       // a creature, item or other plain M2
    CONTEXT_CHARACTER,
    CONTEXT_WMO,
    CONTEXT_OTHER        // a map tile, an image
  };

  enum Page { PAGE_APPEARANCE = 0, PAGE_GEOSETS, PAGE_INFO };

  ModelInspector(wxWindow * parent, wxWindowID id);
  ~ModelInspector();

  // Where AnimControl creates its skin selector, its texture-override rows and its doodad-set
  // selector, and where CharControl is created. Each section is its own panel, so a section that
  // does not apply is hidden as a whole without touching the shown state of the controls in it
  // (AnimControl reads skinList->IsShown() as "this model has skins").
  wxWindow * skinParent() const;
  wxWindow * overridesParent() const;
  wxWindow * doodadParent() const;
  wxWindow * characterParent() const;

  // Arrange the controls AnimControl and CharControl created on the Appearance page. Once, right
  // after both exist.
  void AttachAppearance(AnimControl * anim, CharControl * chr);

  // What is on screen changed (a model, character, WMO or map tile was loaded). Re-picks the
  // Appearance sections and rebuilds the Geosets and Info tabs.
  void ContentChanged();

  // Show/hide/relayout the Appearance sections after AnimControl changed which of its controls
  // are visible.
  void RefreshAppearance();

  // The model's geoset display flags were changed by something else (a skin, a customization):
  // update the checkboxes without rebuilding the tree.
  void UpdateGeosetSelection();

  void ShowPage(Page page);

  // True while m is still one of the models on the canvas (the root model or any attachment).
  // Pointers such as g_selModel are not cleared when a model is unloaded; check before reading.
  static bool IsLiveModel(const WoWModel * m);

private:
  Context currentContext() const;
  WoWModel * geosetModel() const;   // the model the Geosets tab is showing, if it is still live

  void BuildAppearancePage();
  void BuildGeosetsPage();
  void BuildInfoPage();

  void RebuildAttachments();
  void RebuildGeosetTree();
  void SyncGeosetChecks();
  void UpdateGeosetSummary();
  void RebuildInfo();
  void AddInfoRow(const wxString & label, const wxString & value);

  void OnPageChanged(wxBookCtrlEvent & event);
  void OnGeosetChecked(wxTreeListEvent & event);
  void OnGeosetFilter(wxCommandEvent & event);
  void OnAttachmentChoice(wxCommandEvent & event);
  void OnOverridesToggled(wxCollapsiblePaneEvent & event);
  void OnWatchTimer(wxTimerEvent & event);

  // A cheap fingerprint of what the visible tab shows, so a change made elsewhere (equipment,
  // customization, a menu command) is picked up while the tab is open.
  size_t geosetSignature() const;
  wxString standingGeosetNote() const;
  void SetGeosetNotice(const wxString & text);

  wxNotebook * m_notebook = nullptr;

  // Appearance
  wxPanel * m_appearance = nullptr;
  wxBoxSizer * m_appearanceSizer = nullptr;
  wxStaticText * m_contextNote = nullptr;
  wxPanel * m_modelBox = nullptr;       // skin + texture overrides
  wxStaticText * m_noSkinsNote = nullptr;
  wxCollapsiblePane * m_overridesPane = nullptr;
  wxPanel * m_wmoBox = nullptr;         // doodad set
  Context m_lastContext = CONTEXT_NONE;
  AnimControl * m_anim = nullptr;
  CharControl * m_char = nullptr;

  // Geosets
  wxPanel * m_geosets = nullptr;
  wxStaticText * m_attachmentLabel = nullptr;
  wxChoice * m_attachmentChoice = nullptr;
  wxSearchCtrl * m_geosetFilter = nullptr;
  wxStaticText * m_geosetSummary = nullptr;
  wxStaticText * m_geosetNotice = nullptr;
  wxTreeListCtrl * m_geosetTree = nullptr;
  std::vector<WoWModel *> m_attachmentModels;   // index = m_attachmentChoice position
  WoWModel * m_geosetModel = nullptr;
  size_t m_geosetSig = 0;
  size_t m_geosetCountBuilt = 0;

  // Info
  wxScrolledWindow * m_info = nullptr;
  wxFlexGridSizer * m_infoGrid = nullptr;
  wxStaticText * m_infoEmpty = nullptr;
  const void * m_infoFor = nullptr;
  Context m_infoContext = CONTEXT_NONE;

  wxTimer m_watch;
  bool m_updatingChecks = false;

  wxDECLARE_EVENT_TABLE();
};

#endif // MODELINSPECTOR_H
