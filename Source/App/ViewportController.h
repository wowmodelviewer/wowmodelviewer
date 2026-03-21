#pragma once

// ---- Viewport Controller --------------------------------------------------
// Applies resolved InputState to an OrbitCamera.  This is the bridge between
// the input system and the camera, keeping both subsystems independent.

class OrbitCamera;
struct InputState;

namespace ViewportController
{

/// Apply the resolved input state to the orbit camera.
void apply(const InputState& input, OrbitCamera& camera);

} // namespace ViewportController
