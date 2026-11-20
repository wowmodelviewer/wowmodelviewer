// The macOS surface: the canvas's NSView, made layer-hosting with a CAMetalLayer that MoltenVK draws into.

#define VK_USE_PLATFORM_METAL_EXT
#include <vulkan/vulkan.h>
#include "PlatformSurface.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

std::vector<const char *> wmv::vk::platformInstanceExtensions()
{
  return { VK_EXT_METAL_SURFACE_EXTENSION_NAME };
}

VkSurfaceKHR wmv::vk::createPlatformSurface(VkInstance instance, void * nativeWindow, std::string & error)
{
  NSView * view = (__bridge NSView *)nativeWindow;
  if (!view)
  {
    error = "The viewport has no native view.";
    return VK_NULL_HANDLE;
  }

  CAMetalLayer * layer = [CAMetalLayer layer];
  layer.opaque = YES;
  // Draw at the display's resolution: the swapchain is made in backing pixels.
  layer.contentsScale = view.window ? view.window.backingScaleFactor : [NSScreen mainScreen].backingScaleFactor;
  [view setLayer:layer];
  [view setWantsLayer:YES];

  VkMetalSurfaceCreateInfoEXT ci = { VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT };
  ci.pLayer = layer;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (vkCreateMetalSurfaceEXT(instance, &ci, nullptr, &surface) != VK_SUCCESS)
  {
    error = "The Metal surface for the viewport could not be created.";
    return VK_NULL_HANDLE;
  }
  return surface;
}
