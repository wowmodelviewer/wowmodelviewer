/*
 * ContactShadowControl.h
 *
 * Live controls for the Unity renderer's screen-space contact shadow, and nothing else.
 *
 * The contact shadow is the near-field occlusion the renderer marches in screen space -- the
 * hood's shadow on the face, the belt's on the tabard, the darkening where two plates meet. Its
 * seven numbers used to be compile-time constants in WmvOpaque.shader, which meant that finding a
 * look cost a shader rebuild and a restart per attempt. They are uniforms now, and this panel is
 * the other end of that: move a slider, watch the viewport, keep what you like.
 *
 * Everything here is pushed straight down the existing Unity IPC channel as a "contactShadow"
 * message (UnityIpcServer::sendContactShadow) on every tick of every slider. There is no apply
 * button and no round trip through a model reload: the next frame the player draws is the new
 * one. With no player connected the panel still works and still remembers -- the send is a no-op
 * and the values are pushed the moment a player announces itself.
 *
 * The values persist in userSettings/Config.ini under Session/Contact*, because tuning by eye is
 * something a person does across sessions and losing the answer on exit would be hostile. Reset
 * puts every slider back to the number the renderer ships with, which is also the number a fresh
 * install uses, so "what did it look like before I started?" is always one click away.
 *
 * Deliberately NOT here: the cast-shadow map, the key/fill directions, the ambient bands, bloom
 * and tonemapping. Those are the lighting rig, they were settled by eye already, and a panel that
 * offered them would invite undoing that by accident.
 */

#ifndef CONTACTSHADOWCONTROL_H
#define CONTACTSHADOWCONTROL_H

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <functional>

class ContactShadowControl : public wxWindow
{
  DECLARE_CLASS(ContactShadowControl)
  DECLARE_EVENT_TABLE()

public:
  /// The seven controls, in the units the renderer wants them. reach, thickness and bias are
  /// fractions of the MODEL RADIUS rather than distances, which is what lets one setting mean
  /// the same thing on a shoulder pad and on a boss; softness is the tangent of the occlusion
  /// cone's half-angle; steps and taps are sampling rates.
  struct Values
  {
    float strength;
    float reach;
    float softness;
    float thickness;
    float bias;
    int steps;
    int taps;
  };

  /// The numbers the renderer ships with. Kept in one place so Reset, the first run and the
  /// documentation cannot drift apart. MUST match WmvShadowRig.ResetContactSettings().
  static Values Defaults();

  ContactShadowControl(wxWindow * parent, wxWindowID id);

  Values values() const;

  /// Called on every slider change and once when the player connects. The host wires this to
  /// UnityIpcServer::sendContactShadow; the panel itself knows nothing about the IPC.
  std::function<void(const Values &)> onChanged;

  /// Re-send the current values. The host calls this from onUnityReady, because the player is
  /// launched after the app is built and may connect long after the user last touched a slider.
  void Push();

  void LoadSettings();
  void SaveSettings();

private:
  void OnSlider(wxCommandEvent & event);
  void OnReset(wxCommandEvent & event);
  void Apply(const Values & v);      // sliders + labels, without sending
  void UpdateLabels();

  wxSlider * m_strength;
  wxSlider * m_reach;
  wxSlider * m_softness;
  wxSlider * m_thickness;
  wxSlider * m_bias;
  wxSlider * m_steps;
  wxSlider * m_taps;

  wxStaticText * m_strengthVal;
  wxStaticText * m_reachVal;
  wxStaticText * m_softnessVal;
  wxStaticText * m_thicknessVal;
  wxStaticText * m_biasVal;
  wxStaticText * m_stepsVal;
  wxStaticText * m_tapsVal;
};

#endif // CONTACTSHADOWCONTROL_H
