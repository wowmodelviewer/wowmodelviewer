#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

class OrbitCamera;
class Attachment;
struct ViewportFBO;

namespace SceneRenderer
{

// ---- Light type -----------------------------------------------------------
enum LightType
{
    LIGHT_DIRECTIONAL = 0,
    LIGHT_POINT,
    LIGHT_SPOT,
    LIGHT_AMBIENT_ONLY
};

// ---- Lighting parameters --------------------------------------------------
struct LightSettings
{
    float direction[4] = { -1.0f, 1.0f, -1.0f, 0.0f };
    float diffuse[3]   = {  1.0f, 1.0f,  1.0f };
    float ambient[3]   = {  0.35f, 0.35f, 0.35f };
    float specular[3]  = {  0.0f, 0.0f,  0.0f };
    float intensity    = 1.0f;
    bool  enabled      = true;
    LightType type     = LIGHT_DIRECTIONAL;
    float position[3]  = { 0.0f, 5.0f, 0.0f };
    float spotCutoff   = 45.0f;
    float spotExponent = 10.0f;
};

// ---- Module state exposed for UI binding ----------------------------------
struct State
{
    LightSettings light;
    bool      drawCheckerBg  = true;
    bool      drawGradientBg = false;
    glm::vec3 gradientTop{0.15f, 0.20f, 0.35f};
    glm::vec3 gradientBottom{0.02f, 0.02f, 0.05f};
    GLuint    checkerTex     = 0;   // created by initResources()
};

/// Writable reference to the shared rendering state.
State& state() noexcept;

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
