#pragma once

#include <string>
#include <glm/glm.hpp>

class OrbitCamera;
class Attachment;
class Renderer;
struct ViewportFBO;

/// @brief ImGui panel for capturing screenshots at custom resolutions.
namespace ScreenshotPanel
{

/// @brief Per-frame context for the screenshot panel.
struct DrawContext
{
    Renderer*    renderer           = nullptr;
    std::string* screenshotPath     = nullptr;
    std::string* screenshotStatus   = nullptr;
    bool*        useCanvasOverride  = nullptr;
    int*         canvasWidth        = nullptr;
    int*         canvasHeight       = nullptr;

    // Scene context for rendering at custom resolution
    ViewportFBO*   fbo      = nullptr;
    OrbitCamera*   camera   = nullptr;
    Attachment*    root     = nullptr;
    float          fov      = 0.0f;
    glm::vec3      bgColor{0.0f};
    bool           drawGrid = false;
};

void draw(DrawContext& ctx);

} // namespace ScreenshotPanel
