/*
 * PlatformSurface.h
 *
 * The one per-platform piece of the viewport: a Vulkan surface for the native window the viewer's wxWidgets
 * canvas has -- an NSView (macOS, through a CAMetalLayer and VK_EXT_metal_surface), an HWND (Windows), or the
 * canvas's GdkWindow on X11 (Linux).
 */

#ifndef _PLATFORMSURFACE_H_
#define _PLATFORMSURFACE_H_

#include <string>
#include <vector>

// The core API only: each PlatformSurface*.cpp turns its platform's surface extension on itself (VK_USE_PLATFORM_*),
// so windows.h and Xlib.h -- whose macros break Qt's headers -- never reach the renderer.
#include <vulkan/vulkan.h>

namespace wmv
{
  namespace vk
  {
    // The instance extensions the platform's surface needs (besides VK_KHR_surface).
    std::vector<const char *> platformInstanceExtensions();

    // A surface for the native window, or VK_NULL_HANDLE with the reason.
    VkSurfaceKHR createPlatformSurface(VkInstance instance, void * nativeWindow, std::string & error);
  }
}

#endif
