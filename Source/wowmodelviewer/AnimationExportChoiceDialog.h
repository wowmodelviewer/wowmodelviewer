/*----------------------------------------------------------------------*\
| This file is part of WoW Model Viewer                                  |
|                                                                        |
| WoW Model Viewer is free software: you can redistribute it and/or      |
| modify it under the terms of the GNU General Public License as         |
| published by the Free Software Foundation, either version 3 of the     |
| License, or (at your option) any later version.                        |
|                                                                        |
| WoW Model Viewer is distributed in the hope that it will be useful,    |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with WoW Model Viewer.                                           |
| If not, see <http://www.gnu.org/licenses/>.                            |
\*----------------------------------------------------------------------*/

/*
 * AnimationExportChoiceDialog.h
 *
 *  Created on: 3 jul. 2015
 *   Copyright: 2015, WoW Model Viewer (http://wowmodelviewer.net)
 */

#ifndef _ANIMATIONEXPORTCHOICEDIALOG_H_
#define _ANIMATIONEXPORTCHOICEDIALOG_H_

// Includes / class Declarations
//--------------------------------------------------------------------
// STL

// Qt

// Externals

// Other libraries
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include <wx/choicdlg.h>

class wxButton;

// Current library

// Namespaces used
//--------------------------------------------------------------------

// Class Declaration
//--------------------------------------------------------------------
class AnimationExportChoiceDialog : public wxMultiChoiceDialog
{
  public :
    // Constants / Enums

    // Constructors
    AnimationExportChoiceDialog(wxWindow *parent, const wxString &message, const wxString &caption, const wxArrayString &choices);

    // Destructors
    ~AnimationExportChoiceDialog() {}

    // Methods
    // Export-content toggles (the clip list below them is the animation selection). Read by
    // OnExport to drive ExporterPlugin::setExportOptions / setAnimationsToExport.
    bool exportMesh() const;
    bool exportSkeleton() const;
    bool exportSkinning() const;
    bool exportAnimations() const;

    // Members

  protected :
    // Constants / Enums

    // Constructors

    // Destructors

    // Methods

    // Members


  private :
    // Constants / Enums

    // Constructors

    // Destructors

    // Methods
    void updateButtons(wxCommandEvent& event);
    void OnSelectAll(wxCommandEvent &event);
    void OnUnselectAll(wxCommandEvent &event);
    void onToggleAnimations(wxCommandEvent &event);


    // Members
    wxButton * m_selectall;
    wxButton * m_unselectall;

    // Export-content checkboxes (default all-on). "Export Animations" enables/disables the
    // clip list + Select/Unselect buttons.
    wxCheckBox * m_cbMesh;
    wxCheckBox * m_cbSkeleton;
    wxCheckBox * m_cbSkinning;
    wxCheckBox * m_cbAnimations;

    DECLARE_EVENT_TABLE();

    // friend class declarations
};

// static members definition
#ifdef _ANIMATIONEXPORTCHOICEDIALOG_CPP_

#endif



#endif /* _ANIMATIONEXPORTCHOICEDIALOG_H_ */
