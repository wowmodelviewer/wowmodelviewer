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
 * ColorPickerDialog.h
 *
 *  A modern, self-contained Photoshop-style colour picker that replaces the
 *  native Win32 wxColourDialog for choosing the viewport background colour.
 *  Left: a saturation x brightness square for the current hue. Middle: a
 *  rainbow hue strip. Right: new/current swatches plus editable H/S/B, R/G/B
 *  and #RRGGBB fields, all kept in sync. Fixed-size modal; OK/Cancel.
 *
 *  Usage:
 *      ColorPickerDialog dlg(this, initialColour);
 *      if (dlg.ShowModal() == wxID_OK) result = dlg.GetColour();
 */

#ifndef _COLORPICKERDIALOG_H_
#define _COLORPICKERDIALOG_H_

#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <wx/dialog.h>

class wxTextCtrl;

// Forward declarations of the two owner-drawn child controls (defined in the .cpp).
class SVSquarePanel;
class HueStripPanel;

class ColorPickerDialog : public wxDialog
{
  public:
    ColorPickerDialog(wxWindow* parent, const wxColour& initial);

    // The picked colour. Valid after ShowModal() == wxID_OK.
    wxColour GetColour() const;

    // --- called by the owner-drawn child controls (public so they can reach us) ---
    // Hue in [0,360), sat/val in [0,1].
    double getHue() const { return m_h; }
    double getSat() const { return m_s; }
    double getVal() const { return m_v; }

    // Set from the SV square (sat,val in [0,1]) or the hue strip (hue in [0,360)).
    void setSVFromControl(double s, double v);
    void setHueFromControl(double h);

    // Colour conversion helpers (H in [0,360), s,v in [0,1]; r,g,b in [0,255]).
    // Public + static so the owner-drawn controls can reuse them when painting.
    static void HSVtoRGB(double h, double s, double v, int& r, int& g, int& b);
    static void RGBtoHSV(int r, int g, int b, double& h, double& s, double& v);

  private:
    // --- event handlers ---
    void onHsbText(wxCommandEvent& e);
    void onRgbText(wxCommandEvent& e);
    void onHexText(wxCommandEvent& e);
    void onResetToCurrent(wxMouseEvent& e);
    void onNewSwatchPaint(wxPaintEvent& e);
    void onCurSwatchPaint(wxPaintEvent& e);

    // --- model helpers ---
    void syncFromHSV();          // recompute RGB cache from H/S/V, refresh every widget
    void refreshFields();        // push the model into all text fields (guarded)
    void refreshPanels();        // repaint SV square + hue strip + the "new" swatch

    // --- small clamps ---
    static int    clampInt(long v, int lo, int hi);
    static double clampDouble(double v, double lo, double hi);

    // --- working state ---
    wxColour m_initial;          // the "current" swatch, read-only reference
    double   m_h, m_s, m_v;      // authoritative model, HSV
    int      m_r, m_g, m_b;      // derived RGB cache
    bool     m_updating;         // re-entrancy guard while programmatically setting fields

    // --- widgets ---
    SVSquarePanel* m_svSquare;
    HueStripPanel* m_hueStrip;
    wxWindow*      m_newSwatch;
    wxWindow*      m_curSwatch;

    wxTextCtrl* m_txtH;
    wxTextCtrl* m_txtS;
    wxTextCtrl* m_txtV;   // HSB "B" = brightness / value
    wxTextCtrl* m_txtR;
    wxTextCtrl* m_txtG;
    wxTextCtrl* m_txtB;   // RGB blue
    wxTextCtrl* m_txtHex;

    wxDECLARE_NO_COPY_CLASS(ColorPickerDialog);
};

#endif /* _COLORPICKERDIALOG_H_ */