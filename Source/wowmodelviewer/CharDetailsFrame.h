/*
 * CharDetailsFrame.h
 *
 *  Created on: 21 dec. 2014
 *      Author: Jeromnimo
 */

#ifndef _CHARDETAILSFRAME_H_
#define _CHARDETAILSFRAME_H_

#ifndef WX_PRECOMP
#  include <wx/wx.h>
#endif

#include <wx/window.h>
class wxAuiToolBar;
class wxSpinButton;
class wxSpinEvent;
class wxStaticText;

#include "CharDetails.h"

#include "metaclasses/Observer.h"

class CharDetailsFrame : public wxWindow, public Observer
{
public:
  CharDetailsFrame(wxWindow* parent);

  void setModel(WoWModel * model);

  // The Model selector (Classic | HD) follows the character on screen: shown, checked and enabled as
  // ModelViewer::characterVariantState says, with the reason or the last switch's note under it.
  void syncModelVariant();

  void onEvent(Event *) override;

protected:


private:
  DECLARE_CLASS(CharDetailsFrame)
  DECLARE_EVENT_TABLE()

  wxFlexGridSizer * charCustomizationGS_;
  wxCheckBox * dhMode_;
  wxAuiToolBar * variantBar_ = nullptr;
  wxStaticText * variantNote_ = nullptr;
  wxString variantSignature_;
  wxString variantNoteText_;    // the note as written, before it is wrapped
  int variantNoteWidth_ = -1;   // the page width it was last wrapped for
  bool variantRewrapPending_ = false;

  void onModelVariant(wxCommandEvent & event);
  void onSize(wxSizeEvent & event);
  void wrapVariantNote();
  // As buildRows lays the panel out: this panel's height changed, and the scrolled page around it follows.
  void relayoutPage();

  void onRandomise(wxCommandEvent &event);
  void onDHMode(wxCommandEvent &event);

  // One row per option the character can use now (CharDetails::getCustomizationOptions).
  void buildRows();

  WoWModel * model_;
  bool rebuildPending_ = false;
};


#endif /* _CHARDETAILSFRAME_H_ */
