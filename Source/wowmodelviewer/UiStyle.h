/*
 * UiStyle.h
 *
 * The few shared measurements and helpers the docked panels are laid out with, so Browse, Model
 * and Animation use one spacing scale and one way of titling a section instead of each picking
 * its own pixel offsets. Everything is in DIPs and goes through FromDIP, so the panels scale with
 * the monitor's DPI setting.
 *
 * Deliberately plain: native controls, the system's own colours and fonts, a bold label and a
 * hairline for a section. The viewport is the thing to look at; the panels around it should read
 * as quiet tools.
 */

#ifndef UISTYLE_H
#define UISTYLE_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include <wx/aui/aui.h>
#include <wx/statline.h>

namespace UiStyle
{
  // Spacing scale. Use these, not ad-hoc numbers: XS between a label and its control, S between
  // rows, M around a panel's content, L between sections.
  const int XS = 4;
  const int S = 8;
  const int M = 12;
  const int L = 16;

  // Minimum height of single-line controls (buttons, combos, search fields).
  const int ControlHeight = 24;

  inline int dip(const wxWindow * w, int v) { return w ? w->FromDIP(v) : v; }

  // Secondary text: hints, units, counts. The system's grey text colour, so it follows the theme.
  inline wxColour secondaryText() { return wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT); }

  inline wxStaticText * secondaryLabel(wxWindow * parent, const wxString & text, wxWindowID id = wxID_ANY)
  {
    wxStaticText * label = new wxStaticText(parent, id, text);
    label->SetForegroundColour(secondaryText());
    return label;
  }

  // A card: a few controls that belong together and have to stand out from the rows around them (the
  // Model panel's Mount card). The system window colour inside a hairline in the docking manager's pane
  // border colour, so it follows the theme like the rest and stays apart from the face-coloured sections.
  inline wxColour cardBackground() { return wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW); }
  inline wxColour cardBorder() { return wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE).ChangeLightness(82); }

  // A section title: a bold label followed by a hairline across the remaining width.
  inline wxSizer * sectionHeader(wxWindow * parent, const wxString & title, wxStaticText ** labelOut = nullptr)
  {
    wxBoxSizer * row = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText * label = new wxStaticText(parent, wxID_ANY, title);
    label->SetFont(label->GetFont().Bold());
    row->Add(label, 0, wxALIGN_CENTER_VERTICAL);
    row->Add(new wxStaticLine(parent, wxID_ANY), 1, wxALIGN_CENTER_VERTICAL | wxLEFT, dip(parent, S));
    if (labelOut)
      *labelOut = label;
    return row;
  }

  // Flat, low-contrast pane captions and thin sashes for the docking manager: no gradients, no
  // highlighted "active" caption, so the panel titles do not compete with the viewport.
  inline void applyDockArt(wxAuiManager & manager, const wxWindow * frame)
  {
    wxAuiDockArt * art = manager.GetArtProvider();
    if (!art)
      return;

    const wxColour face = wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE);
    const wxColour caption = face.ChangeLightness(94);
    const wxColour captionText = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT).ChangeLightness(140);
    const wxColour border = face.ChangeLightness(82);

    art->SetMetric(wxAUI_DOCKART_GRADIENT_TYPE, wxAUI_GRADIENT_NONE);
    art->SetMetric(wxAUI_DOCKART_CAPTION_SIZE, dip(frame, 22));
    art->SetMetric(wxAUI_DOCKART_SASH_SIZE, dip(frame, 5));
    art->SetMetric(wxAUI_DOCKART_PANE_BORDER_SIZE, 1);
    art->SetColour(wxAUI_DOCKART_BACKGROUND_COLOUR, face);
    art->SetColour(wxAUI_DOCKART_SASH_COLOUR, face);
    art->SetColour(wxAUI_DOCKART_BORDER_COLOUR, border);
    art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_COLOUR, caption);
    art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_GRADIENT_COLOUR, caption);
    art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_TEXT_COLOUR, captionText);
    art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_COLOUR, caption);
    art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_GRADIENT_COLOUR, caption);
    art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_TEXT_COLOUR, captionText);

    wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    font.MakeBold();
    art->SetFont(wxAUI_DOCKART_CAPTION_FONT, font);
  }
}

#endif // UISTYLE_H
