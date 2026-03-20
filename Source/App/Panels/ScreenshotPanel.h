#pragma once

#include <string>
#include <glm/glm.hpp>

class OrbitCamera;
class Attachment;
struct ViewportFBO;

namespace ScreenshotPanel
{

struct DrawContext
{
    char*        screenshotPath     = nullptr;
    int          screenshotPathSize = 0;
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
