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
#include "UnityAssetAccess.h"
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
  m_geosetNotice->SetLabel(text);
  m_geosetNotice->Wrap(std::max(FromDIP(120), m_geosets->GetClientSize().x - 2 * FromDIP(UiStyle::M)));
  m_geosetNotice->Show(!text.IsEmpty());
  m_geosets->Layout();
}

// A checkbox was clicked: set those submeshes' display flags -- the same WoWModel::showGeoset the
// old tree's double-click called -- and make the change reach whichever viewport is on screen.
//
// THE UNITY VIEWPORT IS TOLD, AND ONLY WHAT IT CAN SHOW. It draws a submesh when its geoset id is
// 0 or is in the list of displayed ids the skin message carries (UnityAssetAccess::
// selectedModelGeosets, read from these same flags). So every change it can represent is pushed
// through SendCurrentSkinToUnity, like any other geoset change. A change it cannot represent --
// hiding id 0, splitting one id's submeshes, an attachment or a merged part -- would leave the
// checkbox saying one thing and the viewport showing another, so it is undone and the reason is
// shown instead. In the OpenGL viewport every change applies, as before.
//
// One more case it cannot show today: the player adopts geosets from a skin message only when that
// message also carries textures (WmvMain.HandleModelSkin returns before AdoptGeosets on an empty
// texture list). A model whose textures cannot be resolved therefore keeps the geosets it was
// built with until it is loaded again, so its checkboxes are refused in the Unity viewport too.
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
  if (g_modelViewer && g_modelViewer->isUnityViewportShowingModel())
  {
    if (m != canvasModel())
    {
      problem = _("The Unity viewport does not draw attachments, so this change would not be visible. "
                  "Nothing was changed.");
    }
    else
    {
      // The skin message reports the geosets of the model the animation controls are on; while
      // those follow an attachment (Render Options), the displayed model's are not sent at all.
      if (g_selModel != m)
        problem = _("The animation controls are on an attachment (Render Options), so the Unity viewport "
                    "would not receive this change. Nothing was changed.");
      const size_t owned = m->ownGeosetCount();
      std::set<int> ids;
      for (size_t index : parts)
      {
        if (!problem.IsEmpty())
          break;
        if (index >= owned)
        {
          problem = _("The Unity viewport does not draw merged parts, so this change would not be visible. "
                      "Nothing was changed.");
          break;
        }
        ids.insert((int)m->geosets[index]->id);
      }
      if (problem.IsEmpty())
      {
        std::vector<UnityAssetAccess::ModelTexture> textures;
        QString error;
        const int fdid = m->gamefile ? (int)m->gamefile->fileDataId() : 0;
        if (!UnityAssetAccess::resolveModelTextures(fdid, textures, error) || textures.empty())
          problem = _("The Unity viewport applies geoset changes together with the model's textures, and "
                      "none could be resolved for this model, so the change would not be visible. "
                      "Nothing was changed.");
      }
      for (size_t j = 0; problem.IsEmpty() && j < owned && j < m->geosets.size(); j++)
      {
        const int id = (int)m->geosets[j]->id;
        if (!ids.count(id))
          continue;
        bool unityDraws = (id == 0);
        for (size_t k = 0; !unityDraws && k < owned && k < m->geosets.size(); k++)
          unityDraws = ((int)m->geosets[k]->id == id && m->geosets[k]->display);
        if (unityDraws != m->geosets[j]->display)
          problem = (id == 0)
            ? _("The Unity viewport always draws the base mesh (geoset 0). Nothing was changed.")
            : wxString::Format(_("The Unity viewport shows geoset %d as a whole, so its submeshes cannot "
                                 "differ. Nothing was changed."), id);
      }
    }

    if (!problem.IsEmpty())
    {
      for (size_t i = 0; i < parts.size(); i++)
        m->showGeoset((uint)parts[i], before[i]);
      LOG_INFO << "[inspector] geoset change not representable in the Unity viewport; reverted";
    }
    else
    {
      g_modelViewer->SendCurrentSkinToUnity();
    }
  }

  SyncGeosetChecks();
  UpdateGeosetSummary();
  SetGeosetNotice(problem.IsEmpty() ? standingGeosetNote() : problem);
  m_geosetSig = geosetSignature();
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
