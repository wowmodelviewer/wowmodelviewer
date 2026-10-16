/*
 * MapOverview.cpp -- the Maps viewer's overhead map in the viewport's place (MapOverview.h).
 */

#include "MapOverview.h"

#include <wx/checkbox.h>

#include "MapTilePanel.h"
#include "MapsViewer.h"
#include "UiControls.h"
#include "UiStyle.h"

namespace
{
  wxString qs(const QString & s) { return wxString(s.toStdWString()); }
}

MapOverview::MapOverview(wxWindow * parent, MapsViewer * maps)
  : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL, wxT("mapOverview")), m_maps(maps)
{
  UiStyle::applyPanel(this);
  const int sp = FromDIP(UiStyle::S), m = FromDIP(UiStyle::M);
  m_title = new wxStaticText(this, wxID_ANY, wxEmptyString);
  m_title->SetFont(UiStyle::font(UiStyle::Type::Section));
  m_hint = UiStyle::secondaryLabel(this, _("Click the map to set where to enter the world; double-click to enter it there. "
                                           "The wheel zooms, a drag moves."));
  m_wholeBox = new wxCheckBox(this, wxID_ANY, _("Show the whole 64 x 64 grid"));
  m_wholeBox->SetToolTip(_("The map shows the box of its tiles; tick to show every tile of the 64 x 64 ADT grid"));
  m_enter = new UiButton(this, wxID_ANY, _("Enter world"), UiButton::Kind::Primary);
  m_enter->SetName(wxT("mapOverviewEnterWorld"));
  m_grid = new MapTileGrid(this, maps);
  m_hover = UiStyle::secondaryLabel(this, wxEmptyString);
  m_target = new wxStaticText(this, wxID_ANY, wxEmptyString);

  wxBoxSizer * head = new wxBoxSizer(wxHORIZONTAL);
  wxBoxSizer * titles = new wxBoxSizer(wxVERTICAL);
  titles->Add(m_title, 0);
  titles->Add(m_hint, 0, wxTOP, FromDIP(UiStyle::XS));
  head->Add(titles, 1, wxALIGN_CENTER_VERTICAL);
  head->Add(m_wholeBox, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, m);
  head->Add(m_enter, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, m);
  wxBoxSizer * foot = new wxBoxSizer(wxHORIZONTAL);
  foot->Add(m_hover, 1, wxALIGN_CENTER_VERTICAL);
  foot->Add(m_target, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, m);
  wxBoxSizer * top = new wxBoxSizer(wxVERTICAL);
  top->Add(head, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, m);
  top->Add(m_grid, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, m);
  top->Add(foot, 0, wxEXPAND | wxALL, sp);
  SetSizer(top);

  m_grid->onHover = [this]() { HoverChanged(); };
  m_wholeBox->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent & e) { m_grid->SetWholeGrid(e.IsChecked()); });
  m_enter->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { m_maps->EnterWorld(); });
  Refresh2();
}

void MapOverview::ImagesChanged()
{
  m_grid->ImagesChanged();
}

void MapOverview::Refresh2()
{
  const int id = m_maps->mapId();
  const QString name = id < 0 ? QString() : (m_maps->mapName().isEmpty() ? m_maps->mapDirectory() : m_maps->mapName());
  m_title->SetLabel(id < 0 ? wxString(_("Maps")) : wxString::Format(wxT("%s  [%d]"), qs(name), id));
  m_hint->Show(m_maps->hasGrid() && m_maps->tilesAvailable() > 0);
  m_wholeBox->Enable(m_maps->hasGrid());
  QString why;
  const bool canEnter = m_maps->canEnter(why);
  m_enter->Enable(canEnter);
  m_enter->SetToolTip(canEnter ? _("Load the world at the target and put the free camera there") : qs(why));
  m_enter->Show(m_maps->hasGrid());
  QString target = m_maps->hasTarget() ? "Target: " + m_maps->targetLine() : QString();
  if (m_maps->entering())
    target = m_maps->tileDetail();
  else if (m_maps->hasTarget() && !canEnter)
    target += "  --  " + why;
  m_target->SetLabel(qs(target));
  HoverChanged();
  m_grid->Refresh(false);
  Layout();
}

void MapOverview::HoverChanged()
{
  const int x = m_grid->hoverX(), y = m_grid->hoverY();
  QString text;
  if (x >= 0 && m_maps->hasGrid())
  {
    text = QString("Tile %1, %2 (%3_%4): %5").arg(x).arg(y).arg(x).arg(y).arg(MapsViewer::stateName(m_maps->tileState(x, y)));
    const QString picture = m_maps->minimapState(x, y);
    if (!picture.isEmpty())
      text += "; " + picture;
  }
  else if (m_maps->hasGrid())
    text = m_maps->statusLine();
  m_hover->SetLabel(qs(text));
}
