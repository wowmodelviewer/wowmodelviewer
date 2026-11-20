// The Linux surface: the canvas's X11 drawing window. Under Wayland the viewer runs through XWayland
// (the app sets GDK_BACKEND=x11 before GTK starts), so this is always an X11 window.

#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>
#include "PlatformSurface.h"

#include <gtk/gtk.h>
#include <gdk/gdkx.h>

std::vector<const char *> wmv::vk::platformInstanceExtensions()
{
  return { VK_KHR_XLIB_SURFACE_EXTENSION_NAME };
}

VkSurfaceKHR wmv::vk::createPlatformSurface(VkInstance instance, void * nativeWindow, std::string & error)
{
  // The canvas's drawing window (wxWindow::GTKGetDrawingWindow).
  GdkWindow * window = static_cast<GdkWindow *>(nativeWindow);
  if (!window || !GDK_IS_X11_WINDOW(window))
  {
    error = "The viewport needs an X11 window (run with GDK_BACKEND=x11).";
    return VK_NULL_HANDLE;
  }
  // The canvas draws into its own native child window, not its toplevel's.
  gdk_window_ensure_native(window);

  VkXlibSurfaceCreateInfoKHR ci = { VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR };
  ci.dpy = GDK_WINDOW_XDISPLAY(window);
  ci.window = GDK_WINDOW_XID(window);
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (vkCreateXlibSurfaceKHR(instance, &ci, nullptr, &surface) != VK_SUCCESS)
  {
    error = "The X11 surface for the viewport could not be created.";
    return VK_NULL_HANDLE;
  }
  return surface;
}
