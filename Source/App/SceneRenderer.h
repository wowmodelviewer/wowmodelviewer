#pragma once

#include "Renderer.h"
#include <glm/glm.hpp>

class OrbitCamera;
class Attachment;
struct ViewportFBO;

namespace SceneRenderer
{

// ---- Backward-compatible type aliases (canonical types live in Renderer) ---
using LightType     = Renderer::LightType;
using LightSettings = Renderer::LightSettings;

inline constexpr LightType LIGHT_DIRECTIONAL  = LightType::Directional;
inline constexpr LightType LIGHT_POINT        = LightType::Point;
inline constexpr LightType LIGHT_SPOT         = LightType::Spot;
inline constexpr LightType LIGHT_AMBIENT_ONLY = LightType::AmbientOnly;

// ---- Module state exposed for UI binding ----------------------------------
struct State
{
    LightSettings light;
    bool      drawCheckerBg  = true;
    bool      drawGradientBg = false;
    glm::vec3 gradientTop{0.15f, 0.20f, 0.35f};
    glm::vec3 gradientBottom{0.02f, 0.02f, 0.05f};
};

/// Writable reference to the shared rendering state.
State& state() noexcept;

/// Access the engine-level Renderer instance.
Renderer& renderer() noexcept;

/// Create GPU resources (checkerboard texture, etc.).  Call once after
/// the OpenGL context is current.
void initResources();

/// Destroy GPU resources.
void shutdown();

/// Apply OpenGL fixed-function lighting from the current light settings.
void setupLighting();

/// Draw the ground grid (wireframe with blue centre axes).
void renderGrid();

/// Draw the scene-graph root (opaque geometry then particles).
void renderObjects(Attachment* root);

/// Draw the checkerboard background pattern.
void renderCheckerboard(int w, int h);

/// Full render pass: clear, background, projection, lighting, grid, model.
void renderToFBO(ViewportFBO& fbo, int w, int h,
                 const OrbitCamera& camera, Attachment* root,
                 float fov, const glm::vec3& clearColor, bool drawGrid);

} // namespace SceneRenderer
