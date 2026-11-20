/*
 * VulkanViewport.h
 *
 * The viewport's renderer: draws a wmv::RenderList with Vulkan (MoltenVK on macOS) into a native window, or into
 * an offscreen image for screenshots and image sequences. It owns everything on the GPU -- the swapchain, the
 * copies of the textures the list names, the pipelines for each blend/depth/cull state -- and nothing about the
 * scene: the caller records the frame (the model code does, see RenderList.h) and hands it over.
 */

#ifndef _VULKANVIEWPORT_H_
#define _VULKANVIEWPORT_H_

#include <memory>
#include <string>
#include <vector>

#include "glm/glm.hpp"

namespace wmv
{
  class RenderList;

  namespace vk
  {
    // How a frame is lit and cleared. The defaults are the preview rig: an ambient floor, a key straight down
    // the world's vertical (+z, the models' up) and a weaker front light from the camera.
    struct FrameSettings
    {
      glm::vec4 clearColor = glm::vec4(0.25f, 0.25f, 0.25f, 1.0f); // alpha 0 for a transparent capture
      bool lightingOff = false;  // masking: every draw takes its colour unlit
      glm::vec3 keyDirection = glm::vec3(0.0f, 0.0f, 1.0f); // world space, towards the light
      float ambient = 0.62f;
      float key = 0.63f;
      float front = 0.30f;
      float ceiling = 1.25f;
      glm::vec3 lightColor = glm::vec3(1.0f);
    };

    class Viewport
    {
    public:
      // nativeWindow: the window to draw into -- an NSView* on macOS, an HWND on Windows, the canvas's
      // GdkWindow* on Linux (X11). Returns nullptr and says why when Vulkan, the device or the surface is not available.
      static std::unique_ptr<Viewport> create(void * nativeWindow, std::string & error);
      virtual ~Viewport() {}

      // Draws and presents a frame of the given size in pixels (the window's backing size). False when nothing
      // could be drawn (a zero size, a lost surface); lastError() says why.
      virtual bool render(const RenderList & list, const FrameSettings & settings, int width, int height) = 0;

      // Draws the frame into an offscreen image of the given size and reads it back: RGBA, top row first.
      virtual bool capture(const RenderList & list, const FrameSettings & settings, int width, int height,
                           std::vector<unsigned char> & rgba) = 0;

      // The GPU and API version, for the log and the About box.
      virtual std::string description() const = 0;
      virtual std::string lastError() const = 0;

      // The largest capture the device can make, per side.
      virtual int maxCaptureSize() const = 0;
    };
  }
}

#endif
