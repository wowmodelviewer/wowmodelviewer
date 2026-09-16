/*
* CharDetailsCustomizationChoice.cpp
*
*  Created on: 27 sep. 2020
*      Author: Jeromnimo
*/

#include "CharDetailsCustomizationChoice.h"

#include <wx/dcmemory.h>

#include "CharDetails.h"
#include "CharDetailsEvent.h"
#include "Game.h"

#include "logger/Logger.h"

IMPLEMENT_CLASS(CharDetailsCustomizationChoice, wxWindow)

BEGIN_EVENT_TABLE(CharDetailsCustomizationChoice, wxWindow)
  EVT_COMBOBOX(wxID_ANY, CharDetailsCustomizationChoice::onChoice)
END_EVENT_TABLE()

namespace
{
  // ChrCustomizationChoice.SwatchColor is packed 0xAARRGGBB (ARGB)
  // Build a small swatch bitmap: single fill, a left/right
  // split for dual-colour choices, or a crossed-out box for "no colour".
  wxBitmap makeSwatchBitmap(unsigned int c0, unsigned int c1)
  {
    const int w = 38, h = 14;
    wxBitmap bmp(w, h);
    wxMemoryDC dc(bmp);
    auto toCol = [](unsigned int c) { return wxColour((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); };

    if (c0 == 0 && c1 == 0) // "none"
    {
      dc.SetBackground(*wxWHITE_BRUSH);
      dc.Clear();
      dc.SetPen(*wxGREY_PEN);
      dc.DrawLine(0, h - 1, w, 0);
    }
    else if (c1 != 0) // dual colour
    {
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.SetBrush(wxBrush(toCol(c0)));
      dc.DrawRectangle(0, 0, w / 2, h);
      dc.SetBrush(wxBrush(toCol(c1)));
      dc.DrawRectangle(w / 2, 0, w - w / 2, h);
    }
    else // single colour
    {
      dc.SetBackground(wxBrush(toCol(c0)));
      dc.Clear();
    }

    dc.SelectObject(wxNullBitmap);
    return bmp;
  }
}


CharDetailsCustomizationChoice::CharDetailsCustomizationChoice(wxWindow* parent, CharDetails & details, uint chrCustomizationChoiceID)
: wxWindow(parent, wxID_ANY), ID_(chrCustomizationChoiceID), details_(details)
{
  auto top = new wxFlexGridSizer(2, 0, 5);
  top->AddGrowableCol(2);

  details_.attach(this);

  auto option = GAMEDATABASE.sqlQuery(QString("SELECT Name_Lang FROM ChrCustomizationOption WHERE ID = %1").arg(ID_));

  if(option.valid && !option.values.empty())
  {
    top->Add(new wxStaticText(this, wxID_ANY, option.values[0][0].toStdWString()),
      wxSizerFlags().Align(wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 5));

    choice_ = new wxBitmapComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, wxCB_READONLY);

    top->Add(choice_, wxSizerFlags().Align(wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL).Border(wxRIGHT, 5));

    SetAutoLayout(true);
    top->SetSizeHints(this);
    SetSizer(top);

    buildList();
  }

  refresh();
}

void CharDetailsCustomizationChoice::onChoice(wxCommandEvent& event)
{
  LOG_INFO << __FUNCTION__ << event.GetSelection();
  const int selection = event.GetSelection();
  if (selection < 0 || selection >= static_cast<int>(values_.size()))
    return;
  details_.set(ID_, values_[selection]); // identity is the choice ID behind the item, never its label
}

void CharDetailsCustomizationChoice::onEvent(Event * e)
{
  auto * event = dynamic_cast<CharDetailsEvent *>(e);
  if (event && (event->type() == CharDetailsEvent::CHOICE_LIST_CHANGED) && (event->getCustomizationOptionId() == ID_))
  {
    // The valid choices can change with another option's choice (a Skin Color list follows Skin
    // Type, Face Features follows Jaw Features): rebuild the list when they did, then select the
    // current choice. Neither step sets a choice, so this cannot start another change.
    if (details_.getCustomizationChoices(ID_) != listedChoices_)
      buildList();
    refresh();
  }
}

void CharDetailsCustomizationChoice::buildList()
{
  if (choice_)
  {
    // clear list and re add possible choices
    choice_->Clear();
    values_.clear();

    const auto ids = details_.getCustomizationChoices(ID_);
    listedChoices_ = ids;

    LOG_INFO << __FUNCTION__ << ID_;

    if (ids.empty())
      return;

    auto query = QString("SELECT OrderIndex,Name_Lang,ID,SwatchColor1,SwatchColor2 FROM ChrCustomizationChoice WHERE ID IN (");
    for(auto id:ids)
    {
      query += QString::number(id);
      query += ",";
    }

    query.chop(1);
    query += ")";

    LOG_INFO << query;

    auto rows = GAMEDATABASE.sqlQuery(query);

    if(rows.valid && !rows.values.empty())
    {
      // Items follow the order getCustomizationChoices gives (client order: OrderIndex, then ID).
      std::map<uint, std::vector<QString> > rowByID;
      for (const auto & row : rows.values)
        rowByID[row[2].toUInt()] = row;
      std::vector<std::vector<QString> > choices;
      for (const uint id : ids)
      {
        const auto it = rowByID.find(id);
        if (it != rowByID.end())
          choices.push_back(it->second);
      }

      // colour options (Skin/Hair/Eye Colour ...) carry a SwatchColor on their
      // choices; show those as colour swatches instead of
      // the meaningless "Choice N" text.
      bool isColourOption = false;
      for (auto v : choices)
        if (v[3].toLongLong() != 0 || v[4].toLongLong() != 0) { isColourOption = true; break; }

      for(auto v:choices)
      {
        const wxString num = wxString::Format(wxT("%i"), v[0].toInt()); // OrderIndex, shown as a plain number

        if (isColourOption)
        {
          // colour swatch + its number, so colours stay referenceable
          const unsigned int c0 = static_cast<unsigned int>(v[3].toLongLong());
          const unsigned int c1 = static_cast<unsigned int>(v[4].toLongLong());
          // On a colour option, an UNNAMED choice with no swatch isn't a real colour --
          // it's a class/transmog eye-glow or other conditional entry (e.g. the Death
          // Knight eye glow that showed up as Eye Colour "18" on a Blood Elf). Skip those
          // so the colour picker only lists actual colours. A swatch-less choice that
          // does have a name (the "None" entry) is legitimate and is kept.
          if (c0 == 0 && c1 == 0 && v[1].isEmpty())
            continue;
          choice_->Append(num, makeSwatchBitmap(c0, c1));
        }
        else if(!v[1].isEmpty())
          choice_->Append(wxString(v[1].toStdString().c_str(), wxConvUTF8)); // named choice
        else
          choice_->Append(num); // unnamed, non-colour choice -> just the number

        values_.push_back(v[2].toUInt());
      }
    }
  }
}


void CharDetailsCustomizationChoice::refresh()
{
  if (choice_)
  {
    // Select the item of the current choice; a current choice the list does not show (none, or one
    // set from outside the valid list, e.g. an imported class-specific choice) selects nothing.
    const auto it = std::find(values_.begin(), values_.end(), details_.get(ID_));
    choice_->SetSelection(it != values_.end() ? static_cast<int>(it - values_.begin()) : wxNOT_FOUND);

    Layout();
  }
}


