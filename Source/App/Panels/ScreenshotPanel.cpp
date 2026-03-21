#include "ScreenshotPanel.h"

#include <cassert>

#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "imgui.h"
#include "imgui_stdlib.h"

#include "Logger.h"
#include "ViewportFBO.h"
#include "OrbitCamera.h"
#include "Renderer.h"
#include "Attachment.h"

#include "stb_image_write.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <vector>

namespace
{

// Capture the current FBO contents to a PNG file.
void captureScreenshot(const std::string& path, ViewportFBO& fbo, std::string& status)
{
    if (fbo.width <= 0 || fbo.height <= 0 || !fbo.fbo)
    {
        status = "No viewport to capture.";
        return;
    }

    const int w = fbo.width;
    const int h = fbo.height;
    std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 4);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo.fbo);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Flip vertically (OpenGL is bottom-up, PNG is top-down)
    const size_t rowBytes = static_cast<size_t>(w) * 4;
    std::vector<unsigned char> row(rowBytes);
    for (int y = 0; y < h / 2; ++y)
    {
        unsigned char* top = pixels.data() + y * rowBytes;
        unsigned char* bot = pixels.data() + (h - 1 - y) * rowBytes;
        std::memcpy(row.data(), top, rowBytes);
        std::memcpy(top, bot, rowBytes);
        std::memcpy(bot, row.data(), rowBytes);
    }

    if (stbi_write_png(path.c_str(), w, h, 4, pixels.data(), static_cast<int>(rowBytes)))
    {
        status = "Saved: " + path;
        LOG_INFO << "Screenshot saved to " << path;
    }
    else
    {
        status = "Failed to write: " + path;
        LOG_ERROR << "Screenshot failed: " << path;
    }
}

// Render the scene at a custom resolution into a temporary FBO and save as PNG.
void captureAtResolution(const std::string& path, int cw, int ch,
                         Renderer& renderer, OrbitCamera& camera,
                         Attachment* root, float fov,
                         const glm::vec3& bgColor, bool drawGrid,
                         std::string& status)
{
    ViewportFBO tmpFbo;
    renderer.renderScene(tmpFbo, cw, ch, camera, fov, bgColor, drawGrid,
                         [root]() {
                             if (!root) return;
                             glEnable(GL_LIGHTING);
                             glEnable(GL_TEXTURE_2D);
                             glEnable(GL_DEPTH_TEST);
                             glDepthFunc(GL_LEQUAL);
                             root->draw();
                             glEnable(GL_TEXTURE_2D);
                             glDisable(GL_LIGHTING);
                             glDepthMask(GL_FALSE);
                             glEnable(GL_BLEND);
                             root->drawParticles();
                             glDisable(GL_BLEND);
                             glDepthMask(GL_TRUE);
                         });

    // Read pixels from the temp FBO
    std::vector<unsigned char> pixels(static_cast<size_t>(cw) * ch * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, tmpFbo.fbo);
    glReadPixels(0, 0, cw, ch, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    const size_t rowBytes = static_cast<size_t>(cw) * 4;
    std::vector<unsigned char> row(rowBytes);
    for (int y = 0; y < ch / 2; ++y)
    {
        unsigned char* top = pixels.data() + y * rowBytes;
        unsigned char* bot = pixels.data() + (ch - 1 - y) * rowBytes;
        std::memcpy(row.data(), top, rowBytes);
        std::memcpy(top, bot, rowBytes);
        std::memcpy(bot, row.data(), rowBytes);
    }

    if (stbi_write_png(path.c_str(), cw, ch, 4, pixels.data(), static_cast<int>(rowBytes)))
    {
        status = std::format("Saved ({}x{}): {}", cw, ch, path);
        LOG_INFO << "Screenshot saved to " << path << " (" << cw << "x" << ch << ")";
    }
    else
    {
        status = "Failed to write: " + path;
    }

    tmpFbo.destroy();
}

} // anonymous namespace

void ScreenshotPanel::draw(DrawContext& ctx)
{
    assert(ctx.screenshotPath && "DrawContext::screenshotPath must not be null");
    assert(ctx.screenshotStatus && "DrawContext::screenshotStatus must not be null");
    assert(ctx.useCanvasOverride && "DrawContext::useCanvasOverride must not be null");
    assert(ctx.fbo && "DrawContext::fbo must not be null");
    assert(ctx.camera && "DrawContext::camera must not be null");

    ImGui::SeparatorText("Capture Viewport");
    ImGui::Text("Output File:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##screenshotPath", ctx.screenshotPath);

    if (ImGui::Button("Save Screenshot", ImVec2(-1, 0)))
    {
        if (*ctx.useCanvasOverride && *ctx.canvasWidth > 0 && *ctx.canvasHeight > 0)
        {
            captureAtResolution(*ctx.screenshotPath,
                                *ctx.canvasWidth, *ctx.canvasHeight,
                                *ctx.renderer, *ctx.camera, ctx.root,
                                ctx.fov, ctx.bgColor, ctx.drawGrid,
                                *ctx.screenshotStatus);
        }
        else
        {
            captureScreenshot(*ctx.screenshotPath, *ctx.fbo, *ctx.screenshotStatus);
        }
    }

    if (!ctx.screenshotStatus->empty())
    {
        bool isError = ctx.screenshotStatus->find("Failed") != std::string::npos ||
                       ctx.screenshotStatus->find("No ") != std::string::npos;
        if (isError)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", ctx.screenshotStatus->c_str());
        else
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", ctx.screenshotStatus->c_str());
    }

    ImGui::SeparatorText("Canvas Size Override");
    ImGui::Checkbox("Use custom resolution", ctx.useCanvasOverride);
    if (*ctx.useCanvasOverride)
    {
        ImGui::SetNextItemWidth(100);
        ImGui::InputInt("Width##canvas", ctx.canvasWidth, 0, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::InputInt("Height##canvas", ctx.canvasHeight, 0, 0);
        *ctx.canvasWidth  = std::max(1, std::min(*ctx.canvasWidth, 8192));
        *ctx.canvasHeight = std::max(1, std::min(*ctx.canvasHeight, 8192));

        ImGui::Text("Quick:");
        ImGui::SameLine();
        if (ImGui::SmallButton("1080p")) { *ctx.canvasWidth = 1920; *ctx.canvasHeight = 1080; }
        ImGui::SameLine();
        if (ImGui::SmallButton("1440p")) { *ctx.canvasWidth = 2560; *ctx.canvasHeight = 1440; }
        ImGui::SameLine();
        if (ImGui::SmallButton("4K"))    { *ctx.canvasWidth = 3840; *ctx.canvasHeight = 2160; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Square")) { *ctx.canvasWidth = 2048; *ctx.canvasHeight = 2048; }
    }
    else
    {
        ImGui::TextDisabled("Captures at current viewport resolution (%dx%d).",
                            ctx.fbo->width, ctx.fbo->height);
    }
}
