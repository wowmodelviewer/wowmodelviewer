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

#include "ColorPickerDialog.h"

#include <wx/dcbuffer.h>
#include <wx/statline.h>
#include <wx/image.h>

// ----- geometry constants -----
static const int SV_SIZE   = 256;   // saturation x brightness square (square)
static const int HUE_W     = 22;    // hue strip width
static const int HUE_H     = 256;   // hue strip height (matches SV square height)
static const int SWATCH_W  = 88;
static const int SWATCH_H  = 40;
static const int MARK_R    = 5;     // SV marker radius

// ----- local control ids -----
enum
{
  ID_CPD_H = wxID_HIGHEST + 6100,
  ID_CPD_S,
  ID_CPD_V,
  ID_CPD_R,
  ID_CPD_G,
  ID_CPD_B,
  ID_CPD_HEX,
};

// ======================================================================
//  Owner-drawn: saturation x brightness square
// ======================================================================
class SVSquarePanel : public wxWindow
{
  public:
    SVSquarePanel(wxWindow* parent, ColorPickerDialog* owner)
      : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(SV_SIZE, SV_SIZE),
                 wxBORDER_SIMPLE),
        m_owner(owner), m_cachedHue(-1.0)
    {
      SetBackgroundStyle(wxBG_STYLE_PAINT);
      SetMinSize(wxSize(SV_SIZE, SV_SIZE));

      Bind(wxEVT_PAINT,      &SVSquarePanel::onPaint,     this);
      Bind(wxEVT_LEFT_DOWN,  &SVSquarePanel::onMouseDown, this);
      Bind(wxEVT_LEFT_UP,    &SVSquarePanel::onMouseUp,   this);
      Bind(wxEVT_MOTION,     &SVSquarePanel::onMotion,    this);
      Bind(wxEVT_MOUSE_CAPTURE_LOST, &SVSquarePanel::onCaptureLost, this);
    }

  private:
    void rebuildBitmap(double hue)
    {
      wxImage img(SV_SIZE, SV_SIZE, false);
      unsigned char* data = img.GetData();
      // x = saturation (0..1 left..right), y = value (1..0 top..bottom)
      for (int y = 0; y < SV_SIZE; ++y)
      {
        double v = 1.0 - (double)y / (double)(SV_SIZE - 1);
        for (int x = 0; x < SV_SIZE; ++x)
        {
          double s = (double)x / (double)(SV_SIZE - 1);
          int r, g, b;
          ColorPickerDialog::HSVtoRGB(hue, s, v, r, g, b);
          int idx = (y * SV_SIZE + x) * 3;
          data[idx + 0] = (unsigned char)r;
          data[idx + 1] = (unsigned char)g;
          data[idx + 2] = (unsigned char)b;
        }
      }
      m_bmp = wxBitmap(img);
      m_cachedHue = hue;
    }

    void onPaint(wxPaintEvent&)
    {
      wxAutoBufferedPaintDC dc(this);
      double hue = m_owner->getHue();
      if (m_cachedHue != hue || !m_bmp.IsOk())
        rebuildBitmap(hue);

      dc.DrawBitmap(m_bmp, 0, 0, false);

      // marker at (s,v)
      double s = m_owner->getSat();
      double v = m_owner->getVal();
      int mx = (int)(s * (SV_SIZE - 1) + 0.5);
      int my = (int)((1.0 - v) * (SV_SIZE - 1) + 0.5);

      // draw a two-tone ring so it stays visible on any background
      dc.SetBrush(*wxTRANSPARENT_BRUSH);
      dc.SetPen(wxPen(*wxBLACK, 1));
      dc.DrawCircle(mx, my, MARK_R + 1);
      dc.SetPen(wxPen(*wxWHITE, 1));
      dc.DrawCircle(mx, my, MARK_R);
    }

    void applyFromMouse(const wxPoint& p)
    {
      int x = p.x; if (x < 0) x = 0; if (x > SV_SIZE - 1) x = SV_SIZE - 1;
      int y = p.y; if (y < 0) y = 0; if (y > SV_SIZE - 1) y = SV_SIZE - 1;
      double s = (double)x / (double)(SV_SIZE - 1);
      double v = 1.0 - (double)y / (double)(SV_SIZE - 1);
      m_owner->setSVFromControl(s, v);
    }

    void onMouseDown(wxMouseEvent& e)
    {
      if (!HasCapture())
        CaptureMouse();
      applyFromMouse(e.GetPosition());
    }

    void onMotion(wxMouseEvent& e)
    {
      if (e.Dragging() && e.LeftIsDown() && HasCapture())
        applyFromMouse(e.GetPosition());
    }

    void onMouseUp(wxMouseEvent&)
    {
      if (HasCapture())
        ReleaseMouse();
    }

    void onCaptureLost(wxMouseCaptureLostEvent&)
    {
      // capture already gone; nothing to release
    }

    ColorPickerDialog* m_owner;
    wxBitmap           m_bmp;
    double             m_cachedHue;
};

// ======================================================================
//  Owner-drawn: rainbow hue strip
// ======================================================================
class HueStripPanel : public wxWindow
{
  public:
    HueStripPanel(wxWindow* parent, ColorPickerDialog* owner)
      : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(HUE_W, HUE_H),
                 wxBORDER_SIMPLE),
        m_owner(owner)
    {
      SetBackgroundStyle(wxBG_STYLE_PAINT);
      SetMinSize(wxSize(HUE_W, HUE_H));

      Bind(wxEVT_PAINT,      &HueStripPanel::onPaint,     this);
      Bind(wxEVT_LEFT_DOWN,  &HueStripPanel::onMouseDown, this);
      Bind(wxEVT_LEFT_UP,    &HueStripPanel::onMouseUp,   this);
      Bind(wxEVT_MOTION,     &HueStripPanel::onMotion,    this);
      Bind(wxEVT_MOUSE_CAPTURE_LOST, &HueStripPanel::onCaptureLost, this);
      buildBitmap();
    }

  private:
    void buildBitmap()
    {
      wxImage img(HUE_W, HUE_H, false);
      unsigned char* data = img.GetData();
      for (int y = 0; y < HUE_H; ++y)
      {
        double h = (double)y / (double)(HUE_H - 1) * 360.0;
        if (h >= 360.0) h = 359.999;
        int r, g, b;
        ColorPickerDialog::HSVtoRGB(h, 1.0, 1.0, r, g, b);
        for (int x = 0; x < HUE_W; ++x)
        {
          int idx = (y * HUE_W + x) * 3;
          data[idx + 0] = (unsigned char)r;
          data[idx + 1] = (unsigned char)g;
          data[idx + 2] = (unsigned char)b;
        }
      }
      m_bmp = wxBitmap(img);
    }

    void onPaint(wxPaintEvent&)
    {
      wxAutoBufferedPaintDC dc(this);
      if (!m_bmp.IsOk())
        buildBitmap();
      dc.DrawBitmap(m_bmp, 0, 0, false);

      // marker: a two-tone horizontal line across the strip at the current hue.
      // Draw a black line at the marker row and a white companion line one
      // pixel toward the interior, clamping both into range so the marker stays
      // fully visible at the extreme top (my==0) and bottom (my==HUE_H-1) edges.
      double h = m_owner->getHue();
      int my = (int)(h / 360.0 * (HUE_H - 1) + 0.5);
      if (my < 0) my = 0;
      if (my > HUE_H - 1) my = HUE_H - 1;

      int myWhite = (my <= 0) ? (my + 1) : (my - 1);
      if (myWhite < 0) myWhite = 0;
      if (myWhite > HUE_H - 1) myWhite = HUE_H - 1;

      dc.SetPen(wxPen(*wxWHITE, 1));
      dc.DrawLine(0, myWhite, HUE_W, myWhite);
      dc.SetPen(wxPen(*wxBLACK, 1));
      dc.DrawLine(0, my, HUE_W, my);
    }

    void applyFromMouse(const wxPoint& p)
    {
      int y = p.y; if (y < 0) y = 0; if (y > HUE_H - 1) y = HUE_H - 1;
      double h = (double)y / (double)(HUE_H - 1) * 360.0;
      if (h >= 360.0) h = 359.999;
      m_owner->setHueFromControl(h);
    }

    void onMouseDown(wxMouseEvent& e)
    {
      if (!HasCapture())
        CaptureMouse();
      applyFromMouse(e.GetPosition());
    }

    void onMotion(wxMouseEvent& e)
    {
      if (e.Dragging() && e.LeftIsDown() && HasCapture())
        applyFromMouse(e.GetPosition());
    }

    void onMouseUp(wxMouseEvent&)
    {
      if (HasCapture())
        ReleaseMouse();
    }

    void onCaptureLost(wxMouseCaptureLostEvent&)
    {
    }

    ColorPickerDialog* m_owner;
    wxBitmap           m_bmp;
};

// ======================================================================
//  Small clamp helpers
// ======================================================================
int ColorPickerDialog::clampInt(long v, int lo, int hi)
{
  if (v < (long)lo) return lo;
  if (v > (long)hi) return hi;
  return (int)v;
}

double ColorPickerDialog::clampDouble(double v, double lo, double hi)
{
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// ======================================================================
//  Colour conversion helpers
// ======================================================================
void ColorPickerDialog::HSVtoRGB(double h, double s, double v, int& r, int& g, int& b)
{
  if (s <= 0.0)
  {
    int gray = clampInt((long)(v * 255.0 + 0.5), 0, 255);
    r = g = b = gray;
    return;
  }

  double hh = h;
  while (hh >= 360.0) hh -= 360.0;
  while (hh < 0.0)    hh += 360.0;
  hh /= 60.0;
  int    i = (int)hh;
  double f = hh - i;
  double p = v * (1.0 - s);
  double q = v * (1.0 - s * f);
  double t = v * (1.0 - s * (1.0 - f));

  double rf, gf, bf;
  switch (i)
  {
    case 0:  rf = v; gf = t; bf = p; break;
    case 1:  rf = q; gf = v; bf = p; break;
    case 2:  rf = p; gf = v; bf = t; break;
    case 3:  rf = p; gf = q; bf = v; break;
    case 4:  rf = t; gf = p; bf = v; break;
    default: rf = v; gf = p; bf = q; break;  // case 5
  }

  r = clampInt((long)(rf * 255.0 + 0.5), 0, 255);
  g = clampInt((long)(gf * 255.0 + 0.5), 0, 255);
  b = clampInt((long)(bf * 255.0 + 0.5), 0, 255);
}

void ColorPickerDialog::RGBtoHSV(int r, int g, int b, double& h, double& s, double& v)
{
  double rf = r / 255.0, gf = g / 255.0, bf = b / 255.0;
  double mx = rf; if (gf > mx) mx = gf; if (bf > mx) mx = bf;
  double mn = rf; if (gf < mn) mn = gf; if (bf < mn) mn = bf;
  double delta = mx - mn;

  v = mx;
  s = (mx <= 0.0) ? 0.0 : (delta / mx);

  if (delta <= 0.0)
  {
    h = 0.0;  // undefined hue for grey; keep 0
    return;
  }

  if (mx == rf)
    h = (gf - bf) / delta;
  else if (mx == gf)
    h = 2.0 + (bf - rf) / delta;
  else
    h = 4.0 + (rf - gf) / delta;

  h *= 60.0;
  if (h < 0.0) h += 360.0;
  if (h >= 360.0) h -= 360.0;
}

// ======================================================================
//  Dialog
// ======================================================================
ColorPickerDialog::ColorPickerDialog(wxWindow* parent, const wxColour& initial)
  : wxDialog(parent, wxID_ANY, wxT("Background Colour"),
             wxDefaultPosition, wxDefaultSize,
             wxDEFAULT_DIALOG_STYLE & ~(wxRESIZE_BORDER | wxMAXIMIZE_BOX)),
    m_initial(initial),
    m_h(0.0), m_s(0.0), m_v(0.0),
    m_r(0), m_g(0), m_b(0),
    m_updating(false),
    m_svSquare(NULL), m_hueStrip(NULL),
    m_newSwatch(NULL), m_curSwatch(NULL),
    m_txtH(NULL), m_txtS(NULL), m_txtV(NULL),
    m_txtR(NULL), m_txtG(NULL), m_txtB(NULL), m_txtHex(NULL)
{
  // seed model from the initial colour
  if (!m_initial.IsOk())
    m_initial = wxColour(0, 0, 0);
  RGBtoHSV(m_initial.Red(), m_initial.Green(), m_initial.Blue(), m_h, m_s, m_v);

  // ---------- layout ----------
  wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);
  wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);

  // SV square
  m_svSquare = new SVSquarePanel(this, this);
  row->Add(m_svSquare, 0, wxALL, 8);

  // Hue strip
  m_hueStrip = new HueStripPanel(this, this);
  row->Add(m_hueStrip, 0, wxTOP | wxBOTTOM | wxRIGHT, 8);

  // Right-hand column: swatches + fields
  wxBoxSizer* col = new wxBoxSizer(wxVERTICAL);

  // swatches (new above current), owner-drawn so they honour wxBG_STYLE_PAINT
  wxFlexGridSizer* swz = new wxFlexGridSizer(2, 2, 4, 8);

  m_newSwatch = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxSize(SWATCH_W, SWATCH_H),
                             wxBORDER_SIMPLE);
  m_newSwatch->SetBackgroundStyle(wxBG_STYLE_PAINT);
  m_newSwatch->Bind(wxEVT_PAINT, &ColorPickerDialog::onNewSwatchPaint, this);

  m_curSwatch = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxSize(SWATCH_W, SWATCH_H),
                             wxBORDER_SIMPLE);
  m_curSwatch->SetBackgroundStyle(wxBG_STYLE_PAINT);
  m_curSwatch->Bind(wxEVT_PAINT, &ColorPickerDialog::onCurSwatchPaint, this);
  // optional nicety: click "current" to reset the model to the initial colour
  m_curSwatch->Bind(wxEVT_LEFT_DOWN, &ColorPickerDialog::onResetToCurrent, this);

  swz->Add(new wxStaticText(this, wxID_ANY, wxT("new")),
           0, wxALIGN_CENTRE_VERTICAL);
  swz->Add(m_newSwatch, 0);
  swz->Add(new wxStaticText(this, wxID_ANY, wxT("current")),
           0, wxALIGN_CENTRE_VERTICAL);
  swz->Add(m_curSwatch, 0);
  col->Add(swz, 0, wxBOTTOM, 10);

  col->Add(new wxStaticLine(this, wxID_ANY), 0, wxEXPAND | wxBOTTOM, 8);

  // numeric fields
  wxFlexGridSizer* grid = new wxFlexGridSizer(7, 2, 5, 6);
  grid->AddGrowableCol(1, 1);

  const int FW = 60;  // field width
  m_txtH   = new wxTextCtrl(this, ID_CPD_H,   wxEmptyString, wxDefaultPosition, wxSize(FW, -1));
  m_txtS   = new wxTextCtrl(this, ID_CPD_S,   wxEmptyString, wxDefaultPosition, wxSize(FW, -1));
  m_txtV   = new wxTextCtrl(this, ID_CPD_V,   wxEmptyString, wxDefaultPosition, wxSize(FW, -1));
  m_txtR   = new wxTextCtrl(this, ID_CPD_R,   wxEmptyString, wxDefaultPosition, wxSize(FW, -1));
  m_txtG   = new wxTextCtrl(this, ID_CPD_G,   wxEmptyString, wxDefaultPosition, wxSize(FW, -1));
  m_txtB   = new wxTextCtrl(this, ID_CPD_B,   wxEmptyString, wxDefaultPosition, wxSize(FW, -1));
  m_txtHex = new wxTextCtrl(this, ID_CPD_HEX, wxEmptyString, wxDefaultPosition, wxSize(FW + 24, -1));

  grid->Add(new wxStaticText(this, wxID_ANY, wxT("H (deg):")), 0, wxALIGN_CENTRE_VERTICAL);
  grid->Add(m_txtH, 0);
  grid->Add(new wxStaticText(this, wxID_ANY, wxT("S (%):")),   0, wxALIGN_CENTRE_VERTICAL);
  grid->Add(m_txtS, 0);
  grid->Add(new wxStaticText(this, wxID_ANY, wxT("B (%):")),   0, wxALIGN_CENTRE_VERTICAL);
  grid->Add(m_txtV, 0);
  grid->Add(new wxStaticText(this, wxID_ANY, wxT("R:")),       0, wxALIGN_CENTRE_VERTICAL);
  grid->Add(m_txtR, 0);
  grid->Add(new wxStaticText(this, wxID_ANY, wxT("G:")),       0, wxALIGN_CENTRE_VERTICAL);
  grid->Add(m_txtG, 0);
  grid->Add(new wxStaticText(this, wxID_ANY, wxT("B:")),       0, wxALIGN_CENTRE_VERTICAL);
  grid->Add(m_txtB, 0);
  grid->Add(new wxStaticText(this, wxID_ANY, wxT("Hex:")),     0, wxALIGN_CENTRE_VERTICAL);
  grid->Add(m_txtHex, 0);

  col->Add(grid, 0, wxEXPAND);
  col->AddStretchSpacer(1);

  row->Add(col, 0, wxALL, 8);
  top->Add(row, 1, wxEXPAND);

  // buttons
  top->Add(new wxStaticLine(this, wxID_ANY), 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
  wxSizer* btns = CreateButtonSizer(wxOK | wxCANCEL);
  if (btns)
    top->Add(btns, 0, wxEXPAND | wxALL, 8);

  // event bindings for the fields
  m_txtH->Bind(wxEVT_TEXT, &ColorPickerDialog::onHsbText, this);
  m_txtS->Bind(wxEVT_TEXT, &ColorPickerDialog::onHsbText, this);
  m_txtV->Bind(wxEVT_TEXT, &ColorPickerDialog::onHsbText, this);
  m_txtR->Bind(wxEVT_TEXT, &ColorPickerDialog::onRgbText, this);
  m_txtG->Bind(wxEVT_TEXT, &ColorPickerDialog::onRgbText, this);
  m_txtB->Bind(wxEVT_TEXT, &ColorPickerDialog::onRgbText, this);
  m_txtHex->Bind(wxEVT_TEXT, &ColorPickerDialog::onHexText, this);

  SetSizerAndFit(top);
  CentreOnParent();

  wxButton* okBtn = wxDynamicCast(FindWindow(wxID_OK), wxButton);
  if (okBtn)
    okBtn->SetDefault();

  // initial population
  syncFromHSV();
}

wxColour ColorPickerDialog::GetColour() const
{
  return wxColour((unsigned char)m_r, (unsigned char)m_g, (unsigned char)m_b);
}

// Recompute the RGB cache from the authoritative HSV, then refresh everything.
void ColorPickerDialog::syncFromHSV()
{
  HSVtoRGB(m_h, m_s, m_v, m_r, m_g, m_b);
  refreshFields();
  refreshPanels();
}

void ColorPickerDialog::refreshFields()
{
  if (m_updating)
    return;
  m_updating = true;

  m_txtH->ChangeValue(wxString::Format(wxT("%d"), (int)(m_h + 0.5)));
  m_txtS->ChangeValue(wxString::Format(wxT("%d"), (int)(m_s * 100.0 + 0.5)));
  m_txtV->ChangeValue(wxString::Format(wxT("%d"), (int)(m_v * 100.0 + 0.5)));
  m_txtR->ChangeValue(wxString::Format(wxT("%d"), m_r));
  m_txtG->ChangeValue(wxString::Format(wxT("%d"), m_g));
  m_txtB->ChangeValue(wxString::Format(wxT("%d"), m_b));
  m_txtHex->ChangeValue(wxString::Format(wxT("#%02X%02X%02X"), m_r, m_g, m_b));

  m_updating = false;
}

void ColorPickerDialog::refreshPanels()
{
  // Just request repaints. The SV square caches its gradient bitmap keyed on
  // the hue and rebuilds it inside onPaint() only when the hue actually
  // changed, so a plain Refresh() here is correct and cheap: pure S/V, RGB or
  // hex edits (hue unchanged) reuse the cached bitmap and only redraw the
  // marker. The hue strip's bitmap is hue-independent and never rebuilt.
  if (m_svSquare)
    m_svSquare->Refresh(false);
  if (m_hueStrip)
    m_hueStrip->Refresh(false);
  if (m_newSwatch)
    m_newSwatch->Refresh(false);  // owner-drawn; repaint with the new colour
}

// ---- swatch painting (owner-drawn) ----
void ColorPickerDialog::onNewSwatchPaint(wxPaintEvent&)
{
  wxAutoBufferedPaintDC dc(m_newSwatch);
  wxSize sz = m_newSwatch->GetClientSize();
  dc.SetBrush(wxBrush(wxColour((unsigned char)m_r, (unsigned char)m_g, (unsigned char)m_b)));
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.DrawRectangle(0, 0, sz.GetWidth(), sz.GetHeight());
}

void ColorPickerDialog::onCurSwatchPaint(wxPaintEvent&)
{
  wxAutoBufferedPaintDC dc(m_curSwatch);
  wxSize sz = m_curSwatch->GetClientSize();
  dc.SetBrush(wxBrush(m_initial));
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.DrawRectangle(0, 0, sz.GetWidth(), sz.GetHeight());
}

// ---- called by the SV square ----
void ColorPickerDialog::setSVFromControl(double s, double v)
{
  m_s = clampDouble(s, 0.0, 1.0);
  m_v = clampDouble(v, 0.0, 1.0);
  syncFromHSV();
}

// ---- called by the hue strip ----
void ColorPickerDialog::setHueFromControl(double h)
{
  while (h >= 360.0) h -= 360.0;
  while (h < 0.0)    h += 360.0;
  m_h = h;
  syncFromHSV();
}

// ---- an H/S/B field changed (HSV is authoritative here) ----
void ColorPickerDialog::onHsbText(wxCommandEvent& e)
{
  if (m_updating)
    return;

  long val = 0;
  switch (e.GetId())
  {
    case ID_CPD_H:
      if (m_txtH->GetValue().ToLong(&val))
      {
        val = clampInt(val, 0, 360);
        m_h = (val >= 360) ? 359.0 : (double)val;
      }
      break;
    case ID_CPD_S:
      if (m_txtS->GetValue().ToLong(&val))
        m_s = (double)clampInt(val, 0, 100) / 100.0;
      break;
    case ID_CPD_V:
      if (m_txtV->GetValue().ToLong(&val))
        m_v = (double)clampInt(val, 0, 100) / 100.0;
      break;
    default:
      return;
  }

  syncFromHSV();
}

// ---- an R/G/B field changed (RGB is authoritative here) ----
void ColorPickerDialog::onRgbText(wxCommandEvent& e)
{
  if (m_updating)
    return;

  long val = 0;
  switch (e.GetId())
  {
    case ID_CPD_R:
      if (m_txtR->GetValue().ToLong(&val))
        m_r = clampInt(val, 0, 255);
      break;
    case ID_CPD_G:
      if (m_txtG->GetValue().ToLong(&val))
        m_g = clampInt(val, 0, 255);
      break;
    case ID_CPD_B:
      if (m_txtB->GetValue().ToLong(&val))
        m_b = clampInt(val, 0, 255);
      break;
    default:
      return;
  }

  // Recompute HSV from the edited RGB, then refresh fields + panels directly
  // (do NOT go through syncFromHSV, which would recompute RGB from the freshly
  // derived HSV and could drift by a rounding step on grey/edge colours).
  RGBtoHSV(m_r, m_g, m_b, m_h, m_s, m_v);
  refreshFields();
  refreshPanels();
}

// ---- the hex field changed ----
void ColorPickerDialog::onHexText(wxCommandEvent&)
{
  if (m_updating)
    return;

  wxString s = m_txtHex->GetValue().Trim(true).Trim(false);
  if (!s.IsEmpty() && s[0] == wxT('#'))
    s = s.Mid(1);

  // need exactly 6 hex digits; ignore anything else gracefully
  if (s.length() != 6)
    return;

  for (size_t i = 0; i < s.length(); ++i)
  {
    wxUniChar c = s[i];
    bool ok = (c >= wxT('0') && c <= wxT('9')) ||
              (c >= wxT('a') && c <= wxT('f')) ||
              (c >= wxT('A') && c <= wxT('F'));
    if (!ok)
      return;
  }

  long rgb = 0;
  if (!s.ToLong(&rgb, 16))
    return;

  m_r = (int)((rgb >> 16) & 0xFF);
  m_g = (int)((rgb >> 8)  & 0xFF);
  m_b = (int)( rgb        & 0xFF);
  RGBtoHSV(m_r, m_g, m_b, m_h, m_s, m_v);

  // RGB is authoritative here; refresh fields + panels directly (as in the RGB
  // handler) rather than via syncFromHSV, which would recompute RGB from the
  // derived HSV. refreshFields is guarded, and ChangeValue does not re-fire
  // wxEVT_TEXT, so the hex field is safely repopulated without recursion.
  refreshFields();
  refreshPanels();
}

// ---- optional: click "current" swatch to revert to the initial colour ----
void ColorPickerDialog::onResetToCurrent(wxMouseEvent&)
{
  // Restore the EXACT initial RGB bytes (RGB-authoritative), then derive HSV
  // from them and refresh fields + panels directly. Going through syncFromHSV
  // would recompute RGB from the derived HSV and could drift by a rounding
  // step on grey/edge colours, so the "reset" would not be bit-exact.
  m_r = m_initial.Red();
  m_g = m_initial.Green();
  m_b = m_initial.Blue();
  RGBtoHSV(m_r, m_g, m_b, m_h, m_s, m_v);
  refreshFields();
  refreshPanels();
}