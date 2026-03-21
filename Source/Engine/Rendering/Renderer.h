#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

struct ViewportFBO;

/// Low-level rendering operations for the engine's OpenGL pipeline.
///
/// Encapsulates GPU resource management, fixed-function lighting,
/// common backgrounds, grid drawing, and FBO render-pass bookkeeping.
/// Higher-level, game-specific rendering (e.g. model drawing) lives
/// in the application layer and uses Renderer as a building block.
class Renderer
{
public:
    // ---- Light types -------------------------------------------------------

    enum class LightType : int
    {
        Directional = 0,
        Point,
        Spot,
        AmbientOnly
    };

    /// Plain-data description of a single light source.
    struct LightSettings
    {
        float     direction[4] = { -1.0f, 1.0f, -1.0f, 0.0f };
        float     diffuse[3]   = {  1.0f, 1.0f,  1.0f };
        float     ambient[3]   = {  0.35f, 0.35f, 0.35f };
        float     specular[3]  = {  0.0f, 0.0f,  0.0f };
        float     intensity    = 1.0f;
        bool      enabled      = true;
        LightType type         = LightType::Directional;
        float     position[3]  = { 0.0f, 5.0f, 0.0f };
        float     spotCutoff   = 45.0f;
        float     spotExponent = 10.0f;
    };

    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    // ---- Lifecycle ---------------------------------------------------------

    /// Create GPU resources.  Call once after the OpenGL context is current.
    void init();

    /// Release GPU resources.
    void shutdown();

    // ---- Rendering operations ----------------------------------------------

    /// Configure OpenGL fixed-function lighting from the given settings.
    void applyLighting(const LightSettings& light);

    /// Draw a tiled checkerboard pattern in screen space.
    void drawCheckerboard(int w, int h);

    /// Draw a vertical gradient quad in screen space.
    void drawGradient(int w, int h,
                      const glm::vec3& top, const glm::vec3& bottom);

    /// Draw a ground grid with highlighted centre axes.
    void drawGrid(float size = 40.0f, float step = 1.0f);

    /// Load a perspective projection matrix.
    void applyProjection(int w, int h, float fov,
                         float nearPlane = 0.1f, float farPlane = 6400.0f);

    /// Load a view (camera) matrix.
    void applyView(const glm::mat4& viewMatrix);

    /// Begin an off-screen FBO render pass (resize, bind, clear).
    void beginPass(ViewportFBO& fbo, int w, int h,
                   const glm::vec3& clearColor);

    /// End the current FBO render pass (unbind).
    void endPass(ViewportFBO& fbo);

    // ---- Resource access ---------------------------------------------------

    GLuint checkerTexture() const noexcept { return m_checkerTex; }

private:
    GLuint m_checkerTex = 0;
};
