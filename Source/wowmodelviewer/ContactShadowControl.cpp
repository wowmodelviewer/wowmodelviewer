#include "ContactShadowControl.h"

#include <wx/settings.h>
#include <wx/statline.h>

#include <QSettings>
#include <QString>

#include "logger/Logger.h"
#include "util.h"

// Local ids. They only have to be unique inside this window's own event table, but they are
// pushed well clear of wxWidgets' own range so a stray id from elsewhere cannot land on one.
enum
{
  ID_CS_STRENGTH = wxID_HIGHEST + 2400,
  ID_CS_REACH,
  ID_CS_SOFTNESS,
  ID_CS_THICKNESS,
  ID_CS_BIAS,
  ID_CS_STEPS,
  ID_CS_TAPS,
  ID_CS_RESET
};

// Slider positions are integers, so each control carries a divisor that turns its position into
// the number the renderer wants. Every divisor is chosen so that the DEFAULT POSITION maps to
// exactly the float the renderer ships with -- 110/300 is the same float as 0.36666667f, 40/100
// as 0.4f, 80/1000 as 0.08f -- which is what lets a fresh install render bit-identically to a
// build with no panel at all.
static const int   CS_STRENGTH_MAX = 100;    const float CS_STRENGTH_DIV = 100.0f;
static const int   CS_REACH_MAX = 200;       const float CS_REACH_DIV = 300.0f;
static const int   CS_SOFTNESS_MAX = 100;    const float CS_SOFTNESS_DIV = 100.0f;
static const int   CS_THICKNESS_MAX = 300;   const float CS_THICKNESS_DIV = 1000.0f;
static const int   CS_BIAS_MAX = 500;        const float CS_BIAS_DIV = 10000.0f;
static const int   CS_STEPS_MIN = 4,  CS_STEPS_MAX = 64;
static const int   CS_TAPS_MIN = 1,   CS_TAPS_MAX = 24;

IMPLEMENT_CLASS(ContactShadowControl, wxWindow)

BEGIN_EVENT_TABLE(ContactShadowControl, wxWindow)
  EVT_SLIDER(ID_CS_STRENGTH, ContactShadowControl::OnSlider)
  EVT_SLIDER(ID_CS_REACH, ContactShadowControl::OnSlider)
  EVT_SLIDER(ID_CS_SOFTNESS, ContactShadowControl::OnSlider)
  EVT_SLIDER(ID_CS_THICKNESS, ContactShadowControl::OnSlider)
  EVT_SLIDER(ID_CS_BIAS, ContactShadowControl::OnSlider)
  EVT_SLIDER(ID_CS_STEPS, ContactShadowControl::OnSlider)
  EVT_SLIDER(ID_CS_TAPS, ContactShadowControl::OnSlider)
  EVT_BUTTON(ID_CS_RESET, ContactShadowControl::OnReset)
END_EVENT_TABLE()

ContactShadowControl::Values ContactShadowControl::Defaults()
{
  Values v;
  v.strength = 0.4f;
  v.reach = 0.36666667f;
  v.softness = 0.25f;
  v.thickness = 0.08f;
  v.bias = 0.010f;
  v.steps = 32;
  v.taps = 8;
  return v;
}

// One row of the grid: the name, the slider, and the value it currently has. The value is shown
// because a slider without a number cannot be written down, and the point of this panel is to
// arrive at numbers worth keeping.
static void addRow(wxWindow * parent, wxFlexGridSizer * grid, const wxString & label,
                   wxSlider * slider, wxStaticText ** valueOut, const wxString & tip)
{
  wxStaticText * name = new wxStaticText(parent, wxID_ANY, label);
  name->SetToolTip(tip);
  slider->SetToolTip(tip);
  *valueOut = new wxStaticText(parent, wxID_ANY, wxT("       "),
                               wxDefaultPosition, wxSize(58, -1), wxALIGN_RIGHT);
  grid->Add(name, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  grid->Add(slider, 1, wxEXPAND);
  grid->Add(*valueOut, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
}

ContactShadowControl::ContactShadowControl(wxWindow * parent, wxWindowID id)
  : wxWindow(parent, id, wxDefaultPosition, wxSize(340, 300))
{
  const Values d = Defaults();

  m_strength  = new wxSlider(this, ID_CS_STRENGTH,  (int)(d.strength * CS_STRENGTH_DIV + 0.5f),
                             0, CS_STRENGTH_MAX);
  m_reach     = new wxSlider(this, ID_CS_REACH,     (int)(d.reach * CS_REACH_DIV + 0.5f),
                             0, CS_REACH_MAX);
  m_softness  = new wxSlider(this, ID_CS_SOFTNESS,  (int)(d.softness * CS_SOFTNESS_DIV + 0.5f),
                             0, CS_SOFTNESS_MAX);
  m_thickness = new wxSlider(this, ID_CS_THICKNESS, (int)(d.thickness * CS_THICKNESS_DIV + 0.5f),
                             1, CS_THICKNESS_MAX);
  m_bias      = new wxSlider(this, ID_CS_BIAS,      (int)(d.bias * CS_BIAS_DIV + 0.5f),
                             0, CS_BIAS_MAX);
  m_steps     = new wxSlider(this, ID_CS_STEPS,     d.steps, CS_STEPS_MIN, CS_STEPS_MAX);
  m_taps      = new wxSlider(this, ID_CS_TAPS,      d.taps, CS_TAPS_MIN, CS_TAPS_MAX);

  wxFlexGridSizer * grid = new wxFlexGridSizer(3, 4, 8);
  grid->AddGrowableCol(1, 1);

  addRow(this, grid, _("Strength"), m_strength, &m_strengthVal,
         _("How much light one contact removes. 0 switches the effect off."));
  addRow(this, grid, _("Reach"), m_reach, &m_reachVal,
         _("How far the probe looks for an occluder, as a fraction of the model's radius. "
           "Longer reaches shade further from the contact and cost more."));
  addRow(this, grid, _("Softness"), m_softness, &m_softnessVal,
         _("The width of the occlusion cone. 0 is a single ray, which gives a hard, "
           "straight-edged shadow; larger values give a penumbra that grows with the distance "
           "to the occluder."));
  addRow(this, grid, _("Thickness"), m_thickness, &m_thicknessVal,
         _("How thick an occluder is assumed to be. Too small and shadows break up on thin "
           "geometry; too large and unrelated surfaces count as touching."));
  addRow(this, grid, _("Bias"), m_bias, &m_biasVal,
         _("How close to itself a surface may find an occluder before the hit is rejected. "
           "Raise it if surfaces shadow themselves; lower it to keep shading tighter to a "
           "contact."));

  wxStaticText * quality = new wxStaticText(this, wxID_ANY, _("Sampling"));
  quality->SetFont(quality->GetFont().Bold());

  wxFlexGridSizer * grid2 = new wxFlexGridSizer(3, 4, 8);
  grid2->AddGrowableCol(1, 1);
  addRow(this, grid2, _("Steps"), m_steps, &m_stepsVal,
         _("Samples along the reach. Too few and the shadow breaks into a checkerboard on thin, "
           "steeply angled geometry. Does not change what counts as a contact, only how finely "
           "it is resolved."));
  addRow(this, grid2, _("Taps"), m_taps, &m_tapsVal,
         _("Samples across the cone. Only matters when Softness is above zero: too few and the "
           "penumbra shows as stripes."));

  wxButton * reset = new wxButton(this, ID_CS_RESET, _("Reset to defaults"));

  wxStaticText * note = new wxStaticText(this, wxID_ANY,
    _("Contact shadows only. Changes reach the Unity viewport immediately;\n"
      "with no viewport open the values are still kept and sent when it starts."));
  note->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

  wxBoxSizer * top = new wxBoxSizer(wxVERTICAL);
  top->Add(grid, 0, wxEXPAND | wxALL, 8);
  top->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
  top->Add(quality, 0, wxLEFT | wxTOP, 8);
  top->Add(grid2, 0, wxEXPAND | wxALL, 8);
  top->Add(reset, 0, wxLEFT | wxBOTTOM, 8);
  top->Add(note, 0, wxALL, 8);
  SetSizerAndFit(top);

  UpdateLabels();
}

ContactShadowControl::Values ContactShadowControl::values() const
{
  Values v;
  v.strength  = m_strength->GetValue() / CS_STRENGTH_DIV;
  v.reach     = m_reach->GetValue() / CS_REACH_DIV;
  v.softness  = m_softness->GetValue() / CS_SOFTNESS_DIV;
  v.thickness = m_thickness->GetValue() / CS_THICKNESS_DIV;
  v.bias      = m_bias->GetValue() / CS_BIAS_DIV;
  v.steps     = m_steps->GetValue();
  v.taps      = m_taps->GetValue();
  return v;
}

void ContactShadowControl::Apply(const Values & v)
{
  m_strength->SetValue((int)(v.strength * CS_STRENGTH_DIV + 0.5f));
  m_reach->SetValue((int)(v.reach * CS_REACH_DIV + 0.5f));
  m_softness->SetValue((int)(v.softness * CS_SOFTNESS_DIV + 0.5f));
  m_thickness->SetValue((int)(v.thickness * CS_THICKNESS_DIV + 0.5f));
  m_bias->SetValue((int)(v.bias * CS_BIAS_DIV + 0.5f));
  m_steps->SetValue(v.steps);
  m_taps->SetValue(v.taps);
  UpdateLabels();
}

void ContactShadowControl::UpdateLabels()
{
  const Values v = values();
  m_strengthVal->SetLabel(wxString::Format(wxT("%.2f"), v.strength));
  m_reachVal->SetLabel(wxString::Format(wxT("%.3f R"), v.reach));
  m_softnessVal->SetLabel(wxString::Format(wxT("%.2f"), v.softness));
  m_thicknessVal->SetLabel(wxString::Format(wxT("%.3f R"), v.thickness));
  m_biasVal->SetLabel(wxString::Format(wxT("%.4f R"), v.bias));
  m_stepsVal->SetLabel(wxString::Format(wxT("%d"), v.steps));
  m_tapsVal->SetLabel(wxString::Format(wxT("%d"), v.taps));
}

void ContactShadowControl::Push()
{
  if (onChanged)
    onChanged(values());
}

void ContactShadowControl::OnSlider(wxCommandEvent & WXUNUSED(event))
{
  UpdateLabels();
  Push();
}

void ContactShadowControl::OnReset(wxCommandEvent & WXUNUSED(event))
{
  Apply(Defaults());
  Push();
}

// Persisted next to every other Session/ key, in the same Config.ini. The defaults passed to
// QSettings::value are the renderer's own, so a file that has never seen this panel -- or one
// whose keys have been deleted to get back to a clean look -- comes up exactly as a fresh
// install does.
void ContactShadowControl::LoadSettings()
{
  const Values d = Defaults();
  QSettings config(QString::fromWCharArray(cfgPath.c_str()), QSettings::IniFormat);
  Values v;
  v.strength  = (float)config.value("Session/ContactStrength", d.strength).toDouble();
  v.reach     = (float)config.value("Session/ContactReach", d.reach).toDouble();
  v.softness  = (float)config.value("Session/ContactSoftness", d.softness).toDouble();
  v.thickness = (float)config.value("Session/ContactThickness", d.thickness).toDouble();
  v.bias      = (float)config.value("Session/ContactBias", d.bias).toDouble();
  v.steps     = config.value("Session/ContactSteps", d.steps).toInt();
  v.taps      = config.value("Session/ContactTaps", d.taps).toInt();
  Apply(v);
}

void ContactShadowControl::SaveSettings()
{
  const Values v = values();
  QSettings config(QString::fromWCharArray(cfgPath.c_str()), QSettings::IniFormat);
  config.setValue("Session/ContactStrength", (double)v.strength);
  config.setValue("Session/ContactReach", (double)v.reach);
  config.setValue("Session/ContactSoftness", (double)v.softness);
  config.setValue("Session/ContactThickness", (double)v.thickness);
  config.setValue("Session/ContactBias", (double)v.bias);
  config.setValue("Session/ContactSteps", v.steps);
  config.setValue("Session/ContactTaps", v.taps);
}
