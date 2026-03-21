#pragma once

// ---- Input Action System --------------------------------------------------
// Data-driven input-to-action mapping inspired by Game Engine Architecture
// (Gregory).  Raw inputs (mouse, keyboard) are resolved to logical actions
// via configurable bindings.  The resolved InputState is consumed by the
// ViewportController to manipulate the camera.

#include "imgui.h"

// ---------------------------------------------------------------------------
// Logical viewport actions
// ---------------------------------------------------------------------------

enum class ViewportAction
{
    Orbit,          // 2D mouse drag: yaw (from dx) + pitch (from dy)
    Pan,            // 2D mouse drag: lateral (from dx) + vertical (from dy)
    Zoom,           // 1D: scroll wheel or mouse-Y drag changes radius
    OrbitYaw,       // 1D key: yaw offset per frame
    OrbitPitch,     // 1D key: pitch offset per frame
    PanLateral,     // 1D key: pan along camera right vector
    PanVertical,    // 1D key: pan along world Z axis
    ResetCamera,    // Discrete one-shot trigger
};

// ---------------------------------------------------------------------------
// Trigger types
// ---------------------------------------------------------------------------

enum class TriggerType
{
    MouseDrag,      // Mouse button held + pointer movement
    MouseScroll,    // Mouse wheel rotation
    KeyHold,        // Key held down (continuous, per-frame)
    KeyPress,       // Key pressed (single trigger, edge-detected)
};

// ---------------------------------------------------------------------------
// Input binding — maps a raw input to a logical action
// ---------------------------------------------------------------------------

struct InputBinding
{
    ViewportAction   action      = ViewportAction::Orbit;
    TriggerType      trigger     = TriggerType::MouseDrag;

    /// Mouse button for MouseDrag trigger.
    ImGuiMouseButton mouseButton = ImGuiMouseButton_Left;

    /// Key for KeyHold / KeyPress triggers.
    ImGuiKey         key         = ImGuiKey_None;

    /// Scale applied to mouse delta X (MouseDrag) or wheel value (MouseScroll).
    float            scaleX      = 1.0f;

    /// Scale applied to mouse delta Y (MouseDrag).  Unused for 1D triggers.
    float            scaleY      = 1.0f;

    /// Fixed delta per frame for KeyHold bindings.
    float            keyDelta    = 0.0f;

    /// Multiplier applied when Shift is held (1.0 = Shift has no effect).
    float            shiftScale  = 0.1f;
};

// ---------------------------------------------------------------------------
// Resolved input state for one frame — consumed by ViewportController
// ---------------------------------------------------------------------------

struct InputState
{
    float orbitYaw   = 0.0f;    // Accumulated yaw delta
    float orbitPitch = 0.0f;    // Accumulated pitch delta
    float panX       = 0.0f;    // Lateral pan (camera-right units)
    float panZ       = 0.0f;    // Vertical pan (world-Z units)
    float zoom       = 0.0f;    // Radius delta (positive = further away)
    bool  resetCamera = false;  // One-shot trigger
};
