/*
 * ModelInspector.cpp
 */

#include "ModelInspector.h"

#include <wx/collpane.h>
#include <wx/notebook.h>
#include <wx/numformatter.h>
#include <wx/scrolwin.h>
#include <wx/srchctrl.h>
#include <wx/treelist.h>

#include <wx/time.h>

#include <algorithm>
#include <functional>
#include <map>
#include <set>

#include "animcontrol.h"
#include "Attachment.h"
#include "charcontrol.h"
#include "enums.h"
#include "globalvars.h"
#include "maptile.h"
#include "modelviewer.h"
#include "UiStyle.h"
#include "wmo.h"
#include "WoWModel.h"

#include "logger/Logger.h"

wxBEGIN_EVENT_TABLE(ModelInspector, wxPanel)
  EVT_NOTEBOOK_PAGE_CHANGED(ID_INSPECTOR_NOTEBOOK, ModelInspector::OnPageChanged)
  EVT_TREELIST_ITEM_CHECKED(ID_INSPECTOR_GEOSET_TREE, ModelInspector::OnGeosetChecked)
  EVT_TEXT(ID_INSPECTOR_GEOSET_FILTER, ModelInspector::OnGeosetFilter)
  EVT_SEARCHCTRL_CANCEL_BTN(ID_INSPECTOR_GEOSET_FILTER, ModelInspector::OnGeosetFilter)
  EVT_CHOICE(ID_INSPECTOR_ATTACHMENT, ModelInspector::OnAttachmentChoice)
  EVT_COLLAPSIBLEPANE_CHANGED(wxID_ANY, ModelInspector::OnOverridesToggled)
  EVT_TIMER(wxID_ANY, ModelInspector::OnWatchTimer)
wxEND_EVENT_TABLE()

namespace
{
  // One row of the geoset tree: a geoset group (the hundreds), one geoset id, or -- when several
  // submeshes share an id -- one submesh.
  class GeosetRow : public wxClientData
  {
  public:
    enum Kind { GROUP, GEOSET, SUBMESH };
    GeosetRow(Kind k, int g, int id, size_t index) : kind(k), group(g), geosetId(id), submesh(index) {}
    Kind kind;
    int group;
    int geosetId;
    size_t submesh;
  };

  // The submeshes (indices into WoWModel::geosets) a row stands for, read from the model rather
  // than from the tree, so the answer is always about the model as it is now.
  //
  // A group or geoset row stands for the model's OWN submeshes with those ids. Geoset numbering is
  // per model, so a merged part (an equipped item's copy, past ownGeosetCount) with the same
  // number is a different piece of geometry: it is listed as its own submesh row and switched
  // only there -- unless the id has no owned submesh at all, when the row is all there is.
  std::vector<size_t> submeshesFor(WoWModel * m, const GeosetRow * row)
  {
    std::vector<size_t> owned, merged;
    if (!m || !row)
      return owned;
    const size_t ownCount = m->ownGeosetCount();
    for (size_t i = 0; i < m->geosets.size(); i++)
    {
      const int id = (int)m->geosets[i]->id;
      if ((row->kind == GeosetRow::GROUP && id / 100 == row->group) ||
          (row->kind == GeosetRow::GEOSET && id == row->geosetId) ||
          (row->kind == GeosetRow::SUBMESH && i == row->submesh))
        (i < ownCount || row->kind == GeosetRow::SUBMESH ? owned : merged).push_back(i);
    }
    return owned.empty() ? merged : owned;
  }

  WoWModel * canvasModel()
  {
    return g_canvas ? const_cast<WoWModel *>(g_canvas->model()) : nullptr;
  }

  // Every model on the canvas: the root and its attachments, depth first.
  void collectModels(std::vector<WoWModel *> & out)
  {
    out.clear();
    if (!g_canvas)
      return;
    std::function<void(Attachment *)> walk = [&](Attachment * a) {
      if (!a)
        return;
      if (WoWModel * m = dynamic_cast<WoWModel *>(a->model()))
        if (std::find(out.begin(), out.end(), m) == out.end())
          out.push_back(m);
      for (Attachment * child : a->children)
        walk(child);
    };
    if (WoWModel * root = canvasModel())
      out.push_back(root);
    walk(g_canvas->root);
  }

  wxString baseName(const QString & path)
  {
    wxString s(path.toStdWString());
    s.Replace(wxT("/"), wxT("\\"));
    return s.AfterLast('\\');
  }

  wxString number(long value)
  {
    return wxNumberFormatter::ToString(value, wxNumberFormatter::Style_WithThousandsSep);
  }
}

ModelInspector::ModelInspector(wxWindow * parent, wxWindowID id)
  : wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL, wxT("modelInspector"))
{
  m_notebook = new wxNotebook(this, ID_INSPECTOR_NOTEBOOK, wxDefaultPosition, wxDefaultSize, 0,
                              wxT("modelInspectorNotebook"));
  BuildAppearancePage();
  BuildGeosetsPage();
  BuildInfoPage();

  wxBoxSizer * sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_notebook, 1, wxEXPAND | wxALL, FromDIP(UiStyle::XS));
  SetSizer(sizer);

  m_watch.SetOwner(this);
  m_watch.Start(500);
}

ModelInspector::~ModelInspector()
{
  m_watch.Stop();
}

wxWindow * ModelInspector::skinParent() const { return m_modelBox; }
wxWindow * ModelInspector::overridesParent() const { return m_overridesPane->GetPane(); }
wxWindow * ModelInspector::doodadParent() const { return m_wmoBox; }
wxWindow * ModelInspector::characterParent() const { return m_appearance; }

void ModelInspector::ShowPage(Page page)
{
  if ((int)page < (int)m_notebook->GetPageCount())
    m_notebook->SetSelection(page);
}

bool ModelInspector::IsLiveModel(const WoWModel * m)
{
  if (!m)
    return false;
  std::vector<WoWModel *> models;
  collectModels(models);
  return std::find(models.begin(), models.end(), m) != models.end();
}

ModelInspector::Context ModelInspector::currentContext() const
{
  if (!g_modelViewer || !g_canvas)
    return CONTEXT_NONE;
  if (g_modelViewer->isWMO && g_canvas->wmo)
    return CONTEXT_WMO;
  if (g_modelViewer->isADT && g_canvas->adt)
    return CONTEXT_OTHER;
  if (g_canvas->model())
    return g_modelViewer->isChar ? CONTEXT_CHARACTER : CONTEXT_MODEL;
  return CONTEXT_NONE;
}

// ---- Appearance ------------------------------------------------------------------------------

void ModelInspector::BuildAppearancePage()
{
  const int md = FromDIP(UiStyle::M);

  m_appearance = new wxPanel(m_notebook, wxID_ANY);
  m_appearanceSizer = new wxBoxSizer(wxVERTICAL);

  m_contextNote = UiStyle::secondaryLabel(m_appearance, _("No model loaded. Pick one in Browse."));
  m_appearanceSizer->Add(m_contextNote, 0, wxEXPAND | wxALL, md);

  m_modelBox = new wxPanel(m_appearance, wxID_ANY);
  m_noSkinsNote = UiStyle::secondaryLabel(m_modelBox, _("This model has no alternative skins."));
  m_overridesPane = new wxCollapsiblePane(m_modelBox, wxID_ANY, _("Texture overrides"), wxDefaultPosition,
                                          wxDefaultSize, wxCP_DEFAULT_STYLE | wxCP_NO_TLW_RESIZE);
  m_modelBox->Show(false);
  m_appearanceSizer->Add(m_modelBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, md);

  m_wmoBox = new wxPanel(m_appearance, wxID_ANY);
  m_wmoBox->Show(false);
  m_appearanceSizer->Add(m_wmoBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, md);

  m_appearance->SetSizer(m_appearanceSizer);
  m_notebook->AddPage(m_appearance, _("Appearance"), true);
}

void ModelInspector::AttachAppearance(AnimControl * anim, CharControl * chr)
{
  m_anim = anim;
  m_char = chr;
  const int xs = FromDIP(UiStyle::XS);
  const int sp = FromDIP(UiStyle::S);

  if (anim)
  {
    // Skin: the display variations, then the per-slot overrides, secondary and collapsed.
    wxBoxSizer * skin = new wxBoxSizer(wxVERTICAL);
    skin->Add(UiStyle::sectionHeader(m_modelBox, _("Skin")), 0, wxEXPAND | wxBOTTOM, sp);
    skin->Add(anim->skinList, 0, wxEXPAND | wxBOTTOM, sp);
    skin->Add(m_noSkinsNote, 0, wxEXPAND | wxBOTTOM, sp);
    skin->Add(m_overridesPane, 0, wxEXPAND);
    m_modelBox->SetSizer(skin);

    wxWindow * ov = m_overridesPane->GetPane();
    wxBoxSizer * overrides = new wxBoxSizer(wxVERTICAL);
    overrides->Add(anim->BLPSkinsLabel, 0, wxEXPAND | wxTOP | wxBOTTOM, xs);
    overrides->Add(anim->showBLPList, 0, wxBOTTOM, xs);
    wxFlexGridSizer * slotGrid = new wxFlexGridSizer(2, xs, sp);
    slotGrid->AddGrowableCol(1);
    slotGrid->Add(anim->BLPSkinLabel1, 0, wxALIGN_CENTER_VERTICAL);
    slotGrid->Add(anim->BLPSkinList1, 1, wxEXPAND);
    slotGrid->Add(anim->BLPSkinLabel2, 0, wxALIGN_CENTER_VERTICAL);
    slotGrid->Add(anim->BLPSkinList2, 1, wxEXPAND);
    slotGrid->Add(anim->BLPSkinLabel3, 0, wxALIGN_CENTER_VERTICAL);
    slotGrid->Add(anim->BLPSkinList3, 1, wxEXPAND);
    overrides->Add(slotGrid, 0, wxEXPAND);
    ov->SetSizer(overrides);

    // Doodads: which WMO doodad set is placed.
    wxBoxSizer * doodads = new wxBoxSizer(wxVERTICAL);
    doodads->Add(UiStyle::sectionHeader(m_wmoBox, _("Doodad set")), 0, wxEXPAND | wxBOTTOM, sp);
    doodads->Add(anim->wmoLabel, 0, wxEXPAND | wxBOTTOM, xs);
    doodads->Add(anim->wmoList, 0, wxEXPAND);
    m_wmoBox->SetSizer(doodads);
  }

  if (chr)
  {
    // A character's customization, equipment and tabard: the control fills the page and
    // scrolls on its own.
    chr->Show(false);
    m_appearanceSizer->Add(chr, 1, wxEXPAND);
  }

  RefreshAppearance();
}

void ModelInspector::RefreshAppearance()
{
  if (!m_appearance)
    return;

  const Context ctx = currentContext();
  m_lastContext = ctx;

  switch (ctx)
  {
    case CONTEXT_NONE:  m_contextNote->SetLabel(_("No model loaded. Pick one in Browse.")); break;
    case CONTEXT_OTHER: m_contextNote->SetLabel(_("Nothing to adjust for a map tile.")); break;
    default: break;
  }
  m_contextNote->Show(ctx == CONTEXT_NONE || ctx == CONTEXT_OTHER);

  m_modelBox->Show(ctx == CONTEXT_MODEL);
  m_wmoBox->Show(ctx == CONTEXT_WMO);
  if (m_char)
  {
    // A character gets the whole panel; any other model keeps the hand slots it always had.
    const bool handSlots = ctx == CONTEXT_MODEL && m_char->model &&
                           (m_char->model->getItem(CS_HAND_RIGHT) || m_char->model->getItem(CS_HAND_LEFT));
    m_char->SetHandsOnly(ctx != CONTEXT_CHARACTER);
    m_char->Show(ctx == CONTEXT_CHARACTER || handSlots);
  }

  if (m_anim)
  {
    const bool skins = m_anim->skinList->IsShown();
    const bool overrides = m_anim->showBLPList->IsShown() || m_anim->BLPSkinList1->IsShown() ||
                           m_anim->BLPSkinList2->IsShown() || m_anim->BLPSkinList3->IsShown();
    m_noSkinsNote->Show(!skins && !overrides);
    m_overridesPane->Show(overrides);
    m_overridesPane->GetPane()->Layout();
  }

  m_modelBox->Layout();
  m_wmoBox->Layout();
  m_appearance->Layout();
  if (m_char && m_char->IsShown())
    m_char->FitInside();
}

void ModelInspector::OnOverridesToggled(wxCollapsiblePaneEvent & WXUNUSED(event))
{
  m_modelBox->Layout();
  m_appearance->Layout();
}

// ---- Geosets ---------------------------------------------------------------------------------

void ModelInspector::BuildGeosetsPage()
{
  const int xs = FromDIP(UiStyle::XS);
  const int sp = FromDIP(UiStyle::S);
  const int md = FromDIP(UiStyle::M);

  m_geosets = new wxPanel(m_notebook, wxID_ANY);
  wxBoxSizer * sizer = new wxBoxSizer(wxVERTICAL);

  wxBoxSizer * attach = new wxBoxSizer(wxHORIZONTAL);
  m_attachmentLabel = new wxStaticText(m_geosets, wxID_ANY, _("Model"));
  m_attachmentChoice = new wxChoice(m_geosets, ID_INSPECTOR_ATTACHMENT);
  m_attachmentChoice->SetToolTip(_("The model or attachment whose geosets are listed"));
  attach->Add(m_attachmentLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, sp);
  attach->Add(m_attachmentChoice, 1, wxALIGN_CENTER_VERTICAL);
  sizer->Add(attach, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, md);

  m_geosetFilter = new wxSearchCtrl(m_geosets, ID_INSPECTOR_GEOSET_FILTER);
  m_geosetFilter->ShowCancelButton(true);
  m_geosetFilter->SetDescriptiveText(_("Filter by group or ID"));
  sizer->Add(m_geosetFilter, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, md);

  m_geosetSummary = UiStyle::secondaryLabel(m_geosets, wxEmptyString);
  sizer->Add(m_geosetSummary, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, md);

  m_geosetTree = new wxTreeListCtrl(m_geosets, ID_INSPECTOR_GEOSET_TREE, wxDefaultPosition, wxDefaultSize,
                                    wxTL_CHECKBOX | wxTL_3STATE | wxTL_SINGLE);
  m_geosetTree->AppendColumn(_("Geoset"), FromDIP(150), wxALIGN_LEFT, wxCOL_RESIZABLE);
  m_geosetTree->AppendColumn(_("ID"), FromDIP(52), wxALIGN_RIGHT, wxCOL_RESIZABLE);
  // wxTreeListCtrl gives the first column whatever the others leave, but the data view stretches
  // its LAST column to fill, and that stretched width would then be taken as the ID column's own.
  // Pin it first, so the name column is the one that grows.
  m_geosetTree->Bind(wxEVT_SIZE, [this](wxSizeEvent & e) {
    m_geosetTree->SetColumnWidth(1, FromDIP(52));
    e.Skip();
  });
  sizer->Add(m_geosetTree, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, xs);

  m_geosetNotice = new wxStaticText(m_geosets, wxID_ANY, wxEmptyString);
  m_geosetNotice->SetForegroundColour(wxColour(170, 90, 0));
  m_geosetNotice->Show(false);
  sizer->Add(m_geosetNotice, 0, wxEXPAND | wxALL, md);

  m_geosets->SetSizer(sizer);
  m_notebook->AddPage(m_geosets, _("Geosets"));

  m_attachmentLabel->Show(false);
  m_attachmentChoice->Show(false);
}

WoWModel * ModelInspector::geosetModel() const
{
  return IsLiveModel(m_geosetModel) ? m_geosetModel : nullptr;
}

void ModelInspector::RebuildAttachments()
{
  std::vector<WoWModel *> models;
  collectModels(models);

  WoWModel * keep = IsLiveModel(m_geosetModel) ? m_geosetModel : nullptr;
  m_attachmentModels = models;
  m_attachmentChoice->Clear();
  int select = 0;
  for (size_t i = 0; i < models.size(); i++)
  {
    wxString label = baseName(models[i]->name());
    if (i == 0)
      label += _(" (model)");
    m_attachmentChoice->Append(label);
    if (models[i] == keep)
      select = (int)i;
  }
  m_geosetModel = models.empty() ? nullptr : models[select];
  if (!models.empty())
    m_attachmentChoice->SetSelection(select);

  const bool several = models.size() > 1;
  if (m_attachmentChoice->IsShown() != several)
  {
    m_attachmentLabel->Show(several);
    m_attachmentChoice->Show(several);
    m_geosets->Layout();
  }
}

void ModelInspector::RebuildGeosetTree()
{
  if (!m_geosetTree)
    return;

  WoWModel * m = geosetModel();
  m_geosetCountBuilt = m ? m->geosets.size() : 0;
  m_geosetTree->Freeze();
  m_geosetTree->DeleteAllItems();

  if (m && !m->geosets.empty())
  {
    wxString needle = m_geosetFilter->GetValue();
    needle.Trim(true).Trim(false);
    needle.MakeLower();

    // group -> geoset id -> submeshes
    std::map<int, std::map<int, std::vector<size_t> > > groups;
    for (size_t i = 0; i < m->geosets.size(); i++)
    {
      const int id = (int)m->geosets[i]->id;
      groups[id / 100][id].push_back(i);
    }

    const size_t owned = m->ownGeosetCount();
    size_t rows = 0;
    for (auto & g : groups)
      rows += 1 + g.second.size();
    const bool expandAll = !needle.IsEmpty() || rows <= 40;

    for (auto & g : groups)
    {
      wxString groupName = WoWModel::getCGGroupName((CharGeosets)g.first).toStdWString();
      if (groupName.IsEmpty())
        groupName = wxString::Format(_("Group %d"), g.first);
      const bool groupMatches = needle.IsEmpty() || groupName.Lower().Contains(needle);

      std::vector<int> ids;
      for (auto & idEntry : g.second)
        if (groupMatches || wxString::Format(wxT("%d"), idEntry.first).Contains(needle))
          ids.push_back(idEntry.first);
      if (ids.empty())
        continue;

      wxTreeListItem groupItem = m_geosetTree->AppendItem(m_geosetTree->GetRootItem(), groupName, -1, -1,
                                                          new GeosetRow(GeosetRow::GROUP, g.first, -1, 0));
      m_geosetTree->SetItemText(groupItem, 1, wxString::Format(wxT("%d"), g.first * 100));

      for (int id : ids)
      {
        const std::vector<size_t> & parts = g.second[id];
        const wxString label = (id == 0) ? wxString(_("Base mesh"))
                                         : wxString::Format(_("Variant %d"), id % 100);
        wxTreeListItem idItem = m_geosetTree->AppendItem(groupItem, label, -1, -1,
                                                         new GeosetRow(GeosetRow::GEOSET, g.first, id, 0));
        m_geosetTree->SetItemText(idItem, 1, wxString::Format(wxT("%d"), id));

        // Several submeshes under one id: each stays individually switchable, as it was.
        if (parts.size() > 1)
          for (size_t index : parts)
          {
            wxString partLabel = wxString::Format(_("Submesh %u"), (unsigned)index);
            if (index >= owned)
              partLabel += _(" (merged)");
            m_geosetTree->AppendItem(idItem, partLabel, -1, -1,
                                     new GeosetRow(GeosetRow::SUBMESH, g.first, id, index));
          }
      }
      if (expandAll)
        m_geosetTree->Expand(groupItem);
    }
  }

  m_geosetTree->Thaw();
  SyncGeosetChecks();
  UpdateGeosetSummary();
  SetGeosetNotice(standingGeosetNote());
  m_geosetSig = geosetSignature();
}

void ModelInspector::SyncGeosetChecks()
{
  WoWModel * m = geosetModel();
  if (!m)
    return;

  m_updatingChecks = true;
  for (wxTreeListItem item = m_geosetTree->GetFirstItem(); item.IsOk(); item = m_geosetTree->GetNextItem(item))
  {
    const GeosetRow * row = static_cast<GeosetRow *>(m_geosetTree->GetItemData(item));
    const std::vector<size_t> parts = submeshesFor(m, row);
    size_t shown = 0;
    for (size_t index : parts)
      if (m->isGeosetDisplayed(index))
        shown++;
    const wxCheckBoxState state = (parts.empty() || shown == 0) ? wxCHK_UNCHECKED
                                : (shown == parts.size() ? wxCHK_CHECKED : wxCHK_UNDETERMINED);
    if (m_geosetTree->GetCheckedState(item) != state)
      m_geosetTree->CheckItem(item, state);
  }
  m_updatingChecks = false;
}

void ModelInspector::UpdateGeosetSummary()
{
  WoWModel * m = geosetModel();
  if (!m)
  {
    m_geosetSummary->SetLabel(currentContext() == CONTEXT_WMO ? _("WMOs have no geosets.")
                                                             : _("No model loaded."));
    return;
  }
  if (m->geosets.empty())
  {
    m_geosetSummary->SetLabel(_("This model has no geosets."));
    return;
  }
  size_t shown = 0;
  for (size_t i = 0; i < m->geosets.size(); i++)
    if (m->isGeosetDisplayed(i))
      shown++;
  m_geosetSummary->SetLabel(wxString::Format(_("%u of %u submeshes shown"), (unsigned)shown,
                                             (unsigned)m->geosets.size()));
}

// What stays true for as long as this model is listed, as opposed to the reply to one click.
wxString ModelInspector::standingGeosetNote() const
{
  WoWModel * m = geosetModel();
  if (m && g_modelViewer && g_modelViewer->isUnityViewportShowingModel() && m != canvasModel())
    return _("The Unity viewport does not draw attachments, so changes here only show in the OpenGL viewport.");
  return wxEmptyString;
}

void ModelInspector::SetGeosetNotice(const wxString & text)
{
  if (m_geosetNotice->GetLabel() == text && m_geosetNotice->IsShown() == !text.IsEmpty())
    return;
  m_noticeIsUnconfirmed = false;
  m_geosetNotice->SetLabel(text);
  m_geosetNotice->Wrap(std::max(FromDIP(120), m_geosets->GetClientSize().x - 2 * FromDIP(UiStyle::M)));
  m_geosetNotice->Show(!text.IsEmpty());
  m_geosets->Layout();
}

// A checkbox was clicked: set those submeshes' display flags -- the same WoWModel::showGeoset the
// old tree's double-click called -- and make the change reach whichever viewport is on screen.
//
// THE UNITY VIEWPORT GETS THE FLAGS THEMSELVES. It used to receive only the list of switched-on
// geoset IDS and draw id 0 unconditionally, so hiding a base-mesh submesh, or one of several
// submeshes that share an id, could not be expressed and those clicks were undone. Protocol 2
// sends the displayed model's whole per-submesh state (UnityIpcServer::sendModelGeosets), which the
// player applies to the mesh it already built -- no reload -- and ANSWERS: the checkboxes are only
// left as clicked when the renderer reports it applied the state (or is about to, for a model still
// loading). A rejection puts the flags back to what the renderer last confirmed and says why.
//
// Ownership: the state sent is the canvas model's OWN submeshes by index, so an attachment or a
// merged part that happens to use the same geoset number is never part of it. An attachment's own
// checkboxes change only that attachment -- which the OpenGL viewport draws and the Unity viewport
// does not, so there they are refused while Unity is the viewport on screen. So is every click while
// the player on screen is an older build that cannot switch submeshes (protocol 1).
void ModelInspector::OnGeosetChecked(wxTreeListEvent & event)
{
  if (m_updatingChecks)
    return;

  WoWModel * m = geosetModel();
  const wxTreeListItem item = event.GetItem();
  const GeosetRow * row = item.IsOk() ? static_cast<GeosetRow *>(m_geosetTree->GetItemData(item)) : nullptr;
  if (!m || !row)
  {
    // Not inside the tree's own event: the data view still uses the clicked item after it.
    CallAfter([this]() { RebuildAttachments(); RebuildGeosetTree(); });
    return;
  }

  const bool show = (m_geosetTree->GetCheckedState(item) == wxCHK_CHECKED);
  const std::vector<size_t> parts = submeshesFor(m, row);
  std::vector<bool> before;
  for (size_t index : parts)
  {
    before.push_back(m->isGeosetDisplayed(index));
    m->showGeoset((uint)index, show);
  }

  wxString problem;
  const bool unityOnScreen = g_modelViewer && g_modelViewer->isUnityViewportOnScreen();
  if (m != canvasModel())
  {
    if (unityOnScreen)
      problem = _("The Unity viewport does not draw attachments, so this change would not be visible. "
                  "Nothing was changed.");
  }
  else
  {
    bool merged = false;
    for (size_t index : parts)
      merged = merged || index >= m->ownGeosetCount();
    if (merged && unityOnScreen)
      problem = _("The Unity viewport does not draw merged parts, so this change would not be visible. "
                  "Nothing was changed.");
    else if (unityOnScreen && g_modelViewer->unityPlayerReady() && !g_modelViewer->unityPlayerSwitchesSubmeshes())
      problem = _("The Unity renderer in use is an older build that cannot switch geosets. Rebuild it to "
                  "switch them here. Nothing was changed.");
    else if (g_modelViewer)
    {
      const int revision = g_modelViewer->SendCurrentGeosetsToUnity();
      if (revision > 0)
      {
        PendingGeosetChange change;
        change.revision = revision;
        change.fileDataID = (int)m->gamefile->fileDataId();
        change.sentAtMs = wxGetUTCTimeMillis().GetValue();
        for (size_t i = 0; i < parts.size(); i++)
          if (parts[i] < m->ownGeosetCount())
            change.undo.push_back(std::make_pair(parts[i], (bool)before[i]));
        m_pendingGeosets.push_back(change);
        if (m_pendingGeosets.size() > 64)
          m_pendingGeosets.erase(m_pendingGeosets.begin());
      }
    }
  }

  if (!problem.IsEmpty())
  {
    for (size_t i = 0; i < parts.size(); i++)
      m->showGeoset((uint)parts[i], before[i]);
    LOG_INFO << "[inspector] geoset change not shown by the Unity viewport; reverted";
  }

  SyncGeosetChecks();
  UpdateGeosetSummary();
  SetGeosetNotice(problem.IsEmpty() ? standingGeosetNote() : problem);
  m_geosetSig = geosetSignature();
}

// The renderer's answer to a geoset state: "applied" with what it now draws, "pending" (the model is
// still loading; the build will apply it and answer again), or "rejected" with why.
void ModelInspector::OnUnityGeosetsApplied(const UnityIpcServer::GeosetAck & ack)
{
  WoWModel * root = canvasModel();
  if (!root || !root->gamefile || (int)root->gamefile->fileDataId() != ack.fileDataID)
    return;   // about a model that is no longer displayed

  const size_t owned = std::min(root->ownGeosetCount(), root->geosets.size());
  auto findChange = [this](int revision) {
    for (size_t i = 0; i < m_pendingGeosets.size(); i++)
      if (m_pendingGeosets[i].revision == revision)
        return (int)i;
    return -1;
  };

  wxString notice;
  if (ack.status == "pending")
  {
    const int at = findChange(ack.revision);
    if (at >= 0)
      m_pendingGeosets[at].acknowledged = true;
    return;   // nothing to show yet: the build applies it and answers "applied" or "rejected"
  }

  if (ack.status == "applied")
  {
    // States go out whole and in order, so an answer settles its revision and every earlier one.
    bool answersResync = false;
    while (!m_pendingGeosets.empty() && ack.revision != 0 && m_pendingGeosets.front().revision <= ack.revision)
    {
      answersResync = answersResync || (m_pendingGeosets.front().revision == ack.revision && m_pendingGeosets.front().resync);
      m_pendingGeosets.erase(m_pendingGeosets.begin());
    }
    m_lastSettledRevision = std::max(m_lastSettledRevision, ack.revision);
    if (ack.hasVisible)
    {
      m_unityConfirmed = ack.visible;
      m_unityConfirmedFileDataID = ack.fileDataID;
    }
    // Nothing newer on its way: what the renderer draws must now be what the checkboxes say. If it
    // is not -- the flags changed after that state was sent, by something that did not send -- send
    // the current state again rather than leave the two disagreeing. Once: when the answer to that
    // resend still disagrees, sending again would only repeat it, so it is logged and left.
    // Only for a model the Unity viewport shows: a character is loaded into the player too, but drawn
    // by the OpenGL canvas, and its flags keep changing as the character is composed.
    if (m_pendingGeosets.empty() && ack.hasVisible && g_modelViewer && g_modelViewer->unityCanShowCurrentModel())
    {
      bool same = ack.visible.size() == owned;
      for (size_t i = 0; same && i < owned; i++)
        same = (ack.visible[i] == root->geosets[i]->display);
      if (!same && answersResync)
      {
        LOG_ERROR << "[inspector] the Unity viewport still reports a different submesh state after the current"
                     " state was sent again (revision" << ack.revision << ") -- not sending it a third time";
      }
      else if (!same)
      {
        const int revision = g_modelViewer->SendCurrentGeosetsToUnity();
        LOG_INFO << "[inspector] the Unity viewport reported a submesh state the Geosets tab no longer shows"
                    " -- current state sent again (revision" << revision << ")";
        if (revision > 0)
        {
          PendingGeosetChange change;
          change.revision = revision;
          change.fileDataID = ack.fileDataID;
          change.sentAtMs = wxGetUTCTimeMillis().GetValue();
          change.resync = true;
          m_pendingGeosets.push_back(change);
        }
      }
    }
  }
  else if (ack.status == "rejected")
  {
    const int at = findChange(ack.revision);
    if (at < 0 && ack.revision != 0 && ack.revision <= m_lastSettledRevision)
      return;   // already settled by a later answer
    if (at < 0 && ack.revision != 0 && !m_pendingGeosets.empty() && m_pendingGeosets.back().revision > ack.revision)
      return;   // a state no click owns, with a newer whole state on its way that settles it
    const bool newest = at < 0 || at == (int)m_pendingGeosets.size() - 1;
    // Only a click the Unity viewport ON SCREEN did not take is undone. When the OpenGL canvas is the
    // viewport, it already draws the flags as clicked; the side pane falling behind is said, not undone.
    const bool unityOnScreen = g_modelViewer && g_modelViewer->isUnityViewportOnScreen();
    if (ack.revision != 0)
      m_lastSettledRevision = std::max(m_lastSettledRevision, ack.revision);
    if (at >= 0 && !newest)
    {
      // An older state, with a newer (whole) one still on its way: nothing to change yet, but should
      // the newer one be rejected too, this click has to come off with it -- its undo goes first in
      // the next change's list, so undoing newest to oldest still restores the original flags.
      PendingGeosetChange & next = m_pendingGeosets[at + 1];
      next.undo.insert(next.undo.begin(), m_pendingGeosets[at].undo.begin(), m_pendingGeosets[at].undo.end());
      m_pendingGeosets.erase(m_pendingGeosets.begin() + at);
    }
    else if (ack.revision != 0)
    {
      // The newest state -- a click's (tracked) or one a load path sent (not tracked) -- was not taken.
      // Undo on the host what the renderer did not take: back to the state it last confirmed when
      // that is known for this model, otherwise the pending clicks' own before-values.
      if (unityOnScreen)
      {
        if (m_unityConfirmedFileDataID == ack.fileDataID && m_unityConfirmed.size() == owned)
        {
          for (size_t i = 0; i < owned; i++)
            root->showGeoset((uint)i, m_unityConfirmed[i]);
        }
        else
        {
          for (auto it = m_pendingGeosets.rbegin(); it != m_pendingGeosets.rend(); ++it)
            for (auto u = it->undo.rbegin(); u != it->undo.rend(); ++u)
              root->showGeoset((uint)u->first, u->second);
        }
        notice = wxString::Format(_("The Unity viewport did not apply this change (%s). The checkboxes show "
                                    "what it draws."), wxString(ack.reason.toStdWString()));
        LOG_INFO << "[inspector] geoset state rejected by the Unity viewport:" << ack.reason << "-- reverted";
      }
      else
      {
        notice = wxString::Format(_("The Unity pane did not apply this geoset change (%s); the viewport shows "
                                    "it."), wxString(ack.reason.toStdWString()));
        LOG_INFO << "[inspector] geoset state rejected by the Unity side pane:" << ack.reason
                 << "-- kept, the OpenGL viewport draws it";
      }
      m_pendingGeosets.clear();
    }
    else
    {
      // Revision 0: the state that came with a load or a skin push did not fit the renderer's skin,
      // so it is drawing the geoset-id default for this model instead.
      notice = wxString::Format(_("The Unity viewport could not take this model's submesh state (%s) and "
                                  "shows its default geosets."), wxString(ack.reason.toStdWString()));
    }
  }

  if (geosetModel() == root)
  {
    SyncGeosetChecks();
    UpdateGeosetSummary();
    m_geosetSig = geosetSignature();
  }
  if (!notice.IsEmpty())
    SetGeosetNotice(notice);
  else if (m_noticeIsUnconfirmed && m_pendingGeosets.empty())
    SetGeosetNotice(standingGeosetNote());
}

void ModelInspector::UnityPlayerRestarted()
{
  ForgetUnityGeosetState();
}

// Answers the Unity player can no longer give -- it restarted, disconnected, or the model changed --
// are not waited for. The state it draws next comes with the model push and is answered by its build.
void ModelInspector::ForgetUnityGeosetState()
{
  m_pendingGeosets.clear();
  m_unityConfirmed.clear();
  m_unityConfirmedFileDataID = 0;
  m_lastSettledRevision = 0;
  if (m_noticeIsUnconfirmed)
    SetGeosetNotice(standingGeosetNote());
}

wxString ModelInspector::UnconfirmedNotice()
{
  return _("Waiting for the Unity viewport to confirm the last geoset change.");
}

void ModelInspector::OnGeosetFilter(wxCommandEvent & event)
{
  if (event.GetEventType() == wxEVT_SEARCHCTRL_CANCEL_BTN)
    m_geosetFilter->ChangeValue(wxEmptyString);
  RebuildGeosetTree();
}

void ModelInspector::OnAttachmentChoice(wxCommandEvent & WXUNUSED(event))
{
  const int sel = m_attachmentChoice->GetSelection();
  if (sel >= 0 && sel < (int)m_attachmentModels.size() && IsLiveModel(m_attachmentModels[sel]))
    m_geosetModel = m_attachmentModels[sel];
  RebuildGeosetTree();
}

void ModelInspector::UpdateGeosetSelection()
{
  if (!m_geosetTree)
    return;
  WoWModel * m = geosetModel();
  if (!m)
    return;
  if (m->geosets.size() != m_geosetCountBuilt)
  {
    RebuildGeosetTree();
    return;
  }
  SyncGeosetChecks();
  UpdateGeosetSummary();
  m_geosetSig = geosetSignature();
}

size_t ModelInspector::geosetSignature() const
{
  WoWModel * m = geosetModel();
  size_t h = (size_t)m;
  if (!m)
    return h;
  h = h * 31 + m->geosets.size();
  for (size_t i = 0; i < m->geosets.size(); i++)
    h = h * 31 + (size_t)m->geosets[i]->id * 2 + (m->geosets[i]->display ? 1 : 0);
  return h;
}

// ---- Info ------------------------------------------------------------------------------------

void ModelInspector::BuildInfoPage()
{
  const int sp = FromDIP(UiStyle::S);
  const int md = FromDIP(UiStyle::M);

  m_info = new wxScrolledWindow(m_notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
  m_info->SetScrollRate(0, FromDIP(8));
  wxBoxSizer * sizer = new wxBoxSizer(wxVERTICAL);
  m_infoEmpty = UiStyle::secondaryLabel(m_info, _("No model loaded."));
  sizer->Add(m_infoEmpty, 0, wxEXPAND | wxALL, md);
  m_infoGrid = new wxFlexGridSizer(2, sp, md);
  m_infoGrid->AddGrowableCol(1);
  sizer->Add(m_infoGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, md);
  m_info->SetSizer(sizer);
  m_notebook->AddPage(m_info, _("Info"));
}

void ModelInspector::AddInfoRow(const wxString & label, const wxString & value)
{
  wxStaticText * l = UiStyle::secondaryLabel(m_info, label);
  wxStaticText * v = new wxStaticText(m_info, wxID_ANY, value, wxDefaultPosition, wxDefaultSize,
                                      wxST_ELLIPSIZE_MIDDLE | wxST_NO_AUTORESIZE);
  v->SetMinSize(wxSize(FromDIP(60), -1));
  v->SetToolTip(value);
  m_infoGrid->Add(l, 0, wxALIGN_TOP);
  m_infoGrid->Add(v, 1, wxEXPAND);
}

// Only what the loaded asset actually has: a count that does not apply is left out, not shown
// as zero.
void ModelInspector::RebuildInfo()
{
  if (!m_infoGrid)
    return;

  m_info->Freeze();
  m_infoGrid->Clear(true);

  const Context ctx = currentContext();
  m_infoContext = ctx;
  m_infoFor = nullptr;

  if (ctx == CONTEXT_MODEL || ctx == CONTEXT_CHARACTER)
  {
    WoWModel * m = canvasModel();
    m_infoFor = m;
    const QString path = m->gamefile ? m->gamefile->fullname() : m->name();
    const wxString lowerPath = wxString(path.toStdWString()).Lower();

    AddInfoRow(_("Name"), baseName(path));
    AddInfoRow(_("Type"), ctx == CONTEXT_CHARACTER ? _("Character")
                        : lowerPath.StartsWith(wxT("creature")) ? _("Creature")
                        : lowerPath.StartsWith(wxT("item")) ? _("Item") : _("Model"));
    AddInfoRow(_("Path"), wxString(path.toStdWString()));
    if (m->gamefile && m->gamefile->fileDataId() > 0)
      AddInfoRow(_("FileDataID"), wxString::Format(wxT("%u"), (unsigned)m->gamefile->fileDataId()));
    if (!m->origVertices.empty())
      AddInfoRow(_("Vertices"), number((long)m->origVertices.size()));
    if (m->indices.size() >= 3)
      AddInfoRow(_("Triangles"), number((long)(m->indices.size() / 3)));
    if (!m->geosets.empty())
    {
      const size_t owned = m->ownGeosetCount();
      wxString text = number((long)std::min(owned, m->geosets.size()));
      if (m->geosets.size() > owned)
        text += wxString::Format(_(" (+%u merged)"), (unsigned)(m->geosets.size() - owned));
      AddInfoRow(_("Submeshes"), text);
    }
    const glm::vec3 size = m->header.boundSphere.max - m->header.boundSphere.min;
    if (size.x > 0.0f || size.y > 0.0f || size.z > 0.0f)
      AddInfoRow(_("Bounds"), wxString::Format(wxT("%.2f \u00D7 %.2f \u00D7 %.2f"), size.x, size.y, size.z));
    if (!m->anims.empty())
      AddInfoRow(_("Animations"), number((long)m->anims.size()));
    if (!m->bones.empty())
      AddInfoRow(_("Bones"), number((long)m->bones.size()));
  }
  else if (ctx == CONTEXT_WMO)
  {
    WMO * w = g_canvas->wmo;
    m_infoFor = w;
    const QString path = w->itemName();
    AddInfoRow(_("Name"), baseName(path));
    AddInfoRow(_("Type"), _("World model (WMO)"));
    AddInfoRow(_("Path"), wxString(path.toStdWString()));
    if (w->nGroups > 0)
      AddInfoRow(_("Groups"), number((long)w->nGroups));
    if (!w->doodadsets.empty())
      AddInfoRow(_("Doodad sets"), number((long)w->doodadsets.size()));
    if (w->nDoodads > 0)
      AddInfoRow(_("Doodads"), number((long)w->nDoodads));
    if (!w->lights.empty())
      AddInfoRow(_("Lights"), number((long)w->lights.size()));
  }
  else if (ctx == CONTEXT_OTHER)
  {
    MapTile * t = g_canvas->adt;
    m_infoFor = t;
    AddInfoRow(_("Name"), t->name.AfterLast('\\').AfterLast('/'));
    AddInfoRow(_("Type"), _("Map tile (ADT)"));
    AddInfoRow(_("Path"), t->name);
  }

  m_infoEmpty->Show(ctx == CONTEXT_NONE);
  m_info->Layout();
  m_info->FitInside();
  m_info->Thaw();
}

// ---- Keeping up ------------------------------------------------------------------------------

void ModelInspector::ContentChanged()
{
  // Answers about the previous model mean nothing now; its own load and skin pushes report again.
  // (RebuildGeosetTree below sets the notice afresh.)
  m_pendingGeosets.clear();
  m_unityConfirmed.clear();
  m_unityConfirmedFileDataID = 0;
  m_lastSettledRevision = 0;
  RefreshAppearance();
  RebuildAttachments();
  RebuildGeosetTree();
  RebuildInfo();
}

void ModelInspector::OnPageChanged(wxBookCtrlEvent & event)
{
  event.Skip();
  if (event.GetSelection() == PAGE_GEOSETS)
  {
    RebuildAttachments();
    RebuildGeosetTree();
  }
  else if (event.GetSelection() == PAGE_INFO)
  {
    RebuildInfo();
  }
}

// Changes made elsewhere -- equipment, customization, a skin, a menu command -- while a tab is
// open. Cheap: a fingerprint of the geoset flags and a pointer comparison, twice a second, and
// only for the tab that is actually on screen.
void ModelInspector::OnWatchTimer(wxTimerEvent & WXUNUSED(event))
{
  // A geoset state the Unity viewport has not answered for a while is said so, rather than the
  // checkboxes quietly implying it was applied. Not undone: the player may simply be busy, and its
  // answer, when it comes, settles the checkboxes either way.
  // A player that has gone away will not answer at all: stop waiting (its restart resends the state).
  if (!m_pendingGeosets.empty() && !(g_modelViewer && g_modelViewer->unityPlayerReady()))
  {
    LOG_INFO << "[inspector] the Unity player went away with" << (int)m_pendingGeosets.size()
             << "geoset state(s) unanswered -- no longer waiting";
    ForgetUnityGeosetState();
  }
  if (!m_pendingGeosets.empty())
  {
    const long long now = wxGetUTCTimeMillis().GetValue();
    bool late = false;
    for (const PendingGeosetChange & change : m_pendingGeosets)
      late = late || (!change.acknowledged && now - change.sentAtMs > 2500);
    if (late && !m_noticeIsUnconfirmed)
    {
      SetGeosetNotice(UnconfirmedNotice());
      m_noticeIsUnconfirmed = true;
    }
  }

  if (!IsShownOnScreen())
    return;

  switch (m_notebook->GetSelection())
  {
    case PAGE_APPEARANCE:
      if (currentContext() != m_lastContext)
        RefreshAppearance();
      break;

    case PAGE_GEOSETS:
    {
      std::vector<WoWModel *> models;
      collectModels(models);
      if (models != m_attachmentModels || (!geosetModel() && !models.empty()))
      {
        RebuildAttachments();
        RebuildGeosetTree();
      }
      else if (geosetSignature() != m_geosetSig)
      {
        // New submeshes (equipment merged in) need new rows; flags alone only need the checks.
        WoWModel * m = geosetModel();
        if (m && m->geosets.size() != m_geosetCountBuilt)
          RebuildGeosetTree();
        else
        {
          SyncGeosetChecks();
          UpdateGeosetSummary();
          m_geosetSig = geosetSignature();
        }
      }
      break;
    }

    case PAGE_INFO:
    {
      const Context ctx = currentContext();
      const void * shown = (ctx == CONTEXT_WMO) ? (const void *)g_canvas->wmo
                         : (ctx == CONTEXT_OTHER) ? (const void *)g_canvas->adt
                         : (const void *)(g_canvas ? g_canvas->model() : nullptr);
      if (ctx != m_infoContext || shown != m_infoFor)
        RebuildInfo();
      break;
    }
  }
}
