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

#if defined(__APPLE__)
#  define VK_USE_PLATFORM_METAL_EXT
#elif defined(_WIN32)
#  define VK_USE_PLATFORM_WIN32_KHR
#else
#  define VK_USE_PLATFORM_XLIB_KHR
#endif
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
