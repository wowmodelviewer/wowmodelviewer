/*----------------------------------------------------------------------*\
| This file is part of WoW Model Viewer                                  |
| If not, see <http://www.gnu.org/licenses/>.                            |
\*----------------------------------------------------------------------*/

/*
 * ImageSequenceDialog.h
 *
 *  Options dialog for the Image Sequence Exporter: output folder, filename prefix, format
 *  (PNG/JPG/EXR), resolution (presets + custom + keep-aspect), frame rate, frame range, padding,
 *  start number, transparent background, and "open folder when done". Fills an
 *  ImageSequenceExporter::Settings on OK.
 */

#ifndef _IMAGESEQUENCEDIALOG_H_
#define _IMAGESEQUENCEDIALOG_H_

#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include "ImageSequenceExporter.h"

class ModelViewer;
class wxSpinCtrl;
class wxSpinEvent;
class wxFilePickerCtrl;
class wxDirPickerCtrl;

class ImageSequenceDialog : public wxDialog
{
  public:
    ImageSequenceDialog(ModelViewer * parent);

    // Reads the controls into `out`. Returns false (with a message) if a field is invalid.
    bool getSettings(ImageSequenceExporter::Settings & out);

  private:
    void onFormat(wxCommandEvent & e);
    void onResolutionPreset(wxCommandEvent & e);
    void onFpsChoice(wxCommandEvent & e);
    void onRangeChoice(wxCommandEvent & e);
    void onKeepAspect(wxCommandEvent & e);
    void onWidth(wxSpinEvent & e);
    void onHeight(wxSpinEvent & e);
    void updateEnabled();

    ModelViewer * m_owner;

    // defaults derived from the loaded model/clip
    unsigned m_clipLengthMs;
    double   m_aspect;

    wxDirPickerCtrl * m_folder;
    wxTextCtrl  * m_prefix;
    wxChoice    * m_format;
    wxSpinCtrl  * m_padding;
    wxSpinCtrl  * m_startNum;
    wxChoice    * m_resPreset;
    wxSpinCtrl  * m_width;
    wxSpinCtrl  * m_height;
    wxCheckBox  * m_keepAspect;
    wxChoice    * m_fps;
    wxSpinCtrl  * m_fpsCustom;
    wxChoice    * m_range;
    wxSpinCtrl  * m_rangeStart;
    wxSpinCtrl  * m_rangeEnd;
    wxCheckBox  * m_transparent;
    wxCheckBox  * m_openFolder;

    DECLARE_EVENT_TABLE();
};

#endif /* _IMAGESEQUENCEDIALOG_H_ */
