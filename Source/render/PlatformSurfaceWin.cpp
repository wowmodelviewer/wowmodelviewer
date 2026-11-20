// The Windows surface: the canvas's HWND.

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "PlatformSurface.h"

#include <windows.h>

std::vector<const char *> wmv::vk::platformInstanceExtensions()
{
  return { VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
}

VkSurfaceKHR wmv::vk::createPlatformSurface(VkInstance instance, void * nativeWindow, std::string & error)
{
  VkWin32SurfaceCreateInfoKHR ci = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
  ci.hinstance = GetModuleHandle(nullptr);
  ci.hwnd = (HWND)nativeWindow;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (vkCreateWin32SurfaceKHR(instance, &ci, nullptr, &surface) != VK_SUCCESS)
  {
    error = "The Win32 surface for the viewport could not be created.";
    return VK_NULL_HANDLE;
  }
  return surface;
}
