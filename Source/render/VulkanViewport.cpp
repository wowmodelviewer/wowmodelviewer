/*
 * VulkanViewport.cpp -- see VulkanViewport.h.
 *
 * Layout of a frame: one render pass with a multisampled colour and depth target resolved into the swapchain
 * image (or into the capture image). Each draw of the list is one vkCmdDrawIndexed with a pipeline chosen by its
 * state, a descriptor set for its four textures, and a push constant naming its entry in the frame's storage
 * buffer of per-draw data (matrices, colours, combiner parameters). Vertices, indices and that per-draw data are
 * written each frame into host-visible buffers of the frame in flight.
 */

#include "VulkanViewport.h"
#include <memory>

#include "PlatformSurface.h"
#include "RenderList.h"
#include "Texture.h"
#include "TextureManager.h"

#include "logger/Logger.h"

#include "glm/gtc/matrix_inverse.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>

#include "model_vert_spv.h"
#include "model_frag_spv.h"

namespace
{
  const uint32_t FRAMES_IN_FLIGHT = 2;
  const int SAMPLE_TARGET = 4;
  const VkFormat COLOR_FORMAT_PREFERRED = VK_FORMAT_B8G8R8A8_UNORM;

  // Mirrors common.glsl's DrawData (std430).
  struct GpuDrawData
  {
    glm::mat4 mvp;
    glm::mat4 modelView;
    glm::mat4 normalMatrix;
    glm::mat4 texMatrix0;
    glm::mat4 texMatrix1;
    glm::vec4 color;
    glm::vec4 emissive;
    glm::ivec4 uvSource;
    int32_t pixelShader;
    int32_t egxBlendMode;
    int32_t flags;
    int32_t textureCount;
    float alphaRef;
    float combinerAlphaTest;
    float specularWeight;
    float envBoost;
    float metalSpec;
    float metalTight;
    float pad0;
    float pad1;
  };
  static_assert(sizeof(GpuDrawData) == 416, "GpuDrawData must match common.glsl");

  struct GpuFrameData
  {
    glm::vec4 keyDirView;
    glm::vec4 lightLevels;
    glm::vec4 lightColor;
    glm::vec4 options;
  };

  enum DrawFlags
  {
    FLAG_UNLIT = 1,
    FLAG_TEXTURED = 2,
    FLAG_ALPHA_TEST = 4,
    FLAG_PARTICLE_MULTITEX = 8,
    FLAG_SCREEN_SPACE = 16,
    FLAG_TWO_SIDED = 32
  };

  const char * vkResultName(VkResult r)
  {
    switch (r)
    {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    default: return "VkResult error";
    }
  }

  // ------------------------------------------------------------------------------------------------ context
  // One instance and device for the process; viewports share it.
  struct Context
  {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props = {};
    VkPhysicalDeviceMemoryProperties memProps = {};
    VkPhysicalDeviceFeatures enabledFeatures = {};
    bool portabilitySubset = false;
    std::string error;

    static Context & get()
    {
      static Context c;
      return c;
    }

    bool init(VkSurfaceKHR (*makeSurface)(VkInstance, void *, std::string &) = nullptr)
    {
      if (device)
        return true;
      if (!error.empty())
        return false;

      // ---- instance
      std::vector<const char *> extensions = wmv::vk::platformInstanceExtensions();
      extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);

      uint32_t available = 0;
      vkEnumerateInstanceExtensionProperties(nullptr, &available, nullptr);
      std::vector<VkExtensionProperties> instExts(available);
      vkEnumerateInstanceExtensionProperties(nullptr, &available, instExts.data());
      auto hasInstExt = [&](const char * name) {
        for (const auto & e : instExts)
          if (std::strcmp(e.extensionName, name) == 0)
            return true;
        return false;
      };

      VkInstanceCreateFlags instFlags = 0;
      if (hasInstExt(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
      {
        // MoltenVK is a "portability" driver: the loader lists it only when asked to.
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        instFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
      }
      if (hasInstExt(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
        extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

      for (const char * e : extensions)
        if (!hasInstExt(e))
        {
          error = std::string("The Vulkan driver lacks the instance extension ") + e + ".";
          return false;
        }

      VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
      app.pApplicationName = "WoW Model Viewer";
      app.applicationVersion = VK_MAKE_VERSION(1, 2, 0);
      app.pEngineName = "WMV";
      app.apiVersion = VK_API_VERSION_1_1;

      std::vector<const char *> layers;
      if (getenv("WMV_VULKAN_VALIDATION"))
        layers.push_back("VK_LAYER_KHRONOS_validation");

      VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
      ici.flags = instFlags;
      ici.pApplicationInfo = &app;
      ici.enabledExtensionCount = (uint32_t)extensions.size();
      ici.ppEnabledExtensionNames = extensions.data();
      ici.enabledLayerCount = (uint32_t)layers.size();
      ici.ppEnabledLayerNames = layers.data();
      VkResult r = vkCreateInstance(&ici, nullptr, &instance);
      if (r != VK_SUCCESS)
      {
        error = std::string("Vulkan is not available (vkCreateInstance: ") + vkResultName(r) +
                "). Install your graphics driver's current version.";
        return false;
      }

      // ---- physical device: a graphics queue, swapchain support; discrete GPUs first
      uint32_t count = 0;
      vkEnumeratePhysicalDevices(instance, &count, nullptr);
      std::vector<VkPhysicalDevice> devices(count);
      vkEnumeratePhysicalDevices(instance, &count, devices.data());
      int bestScore = -1;
      for (VkPhysicalDevice pd : devices)
      {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(pd, &p);
        uint32_t qc = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qc, nullptr);
        std::vector<VkQueueFamilyProperties> qf(qc);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qc, qf.data());
        int family = -1;
        for (uint32_t i = 0; i < qc; i++)
          if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
          {
            family = (int)i;
            break;
          }
        if (family < 0)
          continue;
        uint32_t ec = 0;
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &ec, nullptr);
        std::vector<VkExtensionProperties> de(ec);
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &ec, de.data());
        bool swap = false;
        for (const auto & e : de)
          if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
            swap = true;
        if (!swap)
          continue;
        int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3
                  : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
        if (score > bestScore)
        {
          bestScore = score;
          physical = pd;
          queueFamily = (uint32_t)family;
        }
      }
      if (!physical)
      {
        error = "No Vulkan graphics device with presentation support was found.";
        return false;
      }
      vkGetPhysicalDeviceProperties(physical, &props);
      vkGetPhysicalDeviceMemoryProperties(physical, &memProps);

      // ---- device
      uint32_t ec = 0;
      vkEnumerateDeviceExtensionProperties(physical, nullptr, &ec, nullptr);
      std::vector<VkExtensionProperties> de(ec);
      vkEnumerateDeviceExtensionProperties(physical, nullptr, &ec, de.data());
      std::vector<const char *> devExts = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
      for (const auto & e : de)
        if (std::strcmp(e.extensionName, "VK_KHR_portability_subset") == 0)
        {
          devExts.push_back("VK_KHR_portability_subset"); // required when the device offers it (MoltenVK)
          portabilitySubset = true;
        }

      VkPhysicalDeviceFeatures supported;
      vkGetPhysicalDeviceFeatures(physical, &supported);
      enabledFeatures.fillModeNonSolid = supported.fillModeNonSolid;
      enabledFeatures.samplerAnisotropy = supported.samplerAnisotropy;

      const float priority = 1.0f;
      VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
      qci.queueFamilyIndex = queueFamily;
      qci.queueCount = 1;
      qci.pQueuePriorities = &priority;

      VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
      dci.queueCreateInfoCount = 1;
      dci.pQueueCreateInfos = &qci;
      dci.enabledExtensionCount = (uint32_t)devExts.size();
      dci.ppEnabledExtensionNames = devExts.data();
      dci.pEnabledFeatures = &enabledFeatures;
      r = vkCreateDevice(physical, &dci, nullptr, &device);
      if (r != VK_SUCCESS)
      {
        error = std::string("The Vulkan device could not be created (") + vkResultName(r) + ").";
        return false;
      }
      vkGetDeviceQueue(device, queueFamily, 0, &queue);

      LOG_INFO << "[vulkan] device" << props.deviceName << "API" << VK_VERSION_MAJOR(props.apiVersion) << "."
               << VK_VERSION_MINOR(props.apiVersion) << "." << VK_VERSION_PATCH(props.apiVersion)
               << (portabilitySubset ? "(portability subset)" : "");
      return true;
    }

    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags flags) const
    {
      for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & flags) == flags)
          return i;
      return UINT32_MAX;
    }
  };

  // ------------------------------------------------------------------------------------------------ resources
  struct Buffer
  {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void * mapped = nullptr;

    void destroy(VkDevice d)
    {
      if (mapped) vkUnmapMemory(d, memory);
      if (buffer) vkDestroyBuffer(d, buffer, nullptr);
      if (memory) vkFreeMemory(d, memory, nullptr);
      *this = Buffer();
    }
  };

  bool createBuffer(Context & c, VkDeviceSize size, VkBufferUsageFlags usage, Buffer & out)
  {
    VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(c.device, &bci, nullptr, &out.buffer) != VK_SUCCESS)
      return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(c.device, out.buffer, &req);
    VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = c.memoryType(req.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(c.device, &mai, nullptr, &out.memory) != VK_SUCCESS)
      return false;
    vkBindBufferMemory(c.device, out.buffer, out.memory, 0);
    vkMapMemory(c.device, out.memory, 0, VK_WHOLE_SIZE, 0, &out.mapped);
    out.size = size;
    return true;
  }

  // Makes sure a host buffer holds at least 'need' bytes, growing it (by half again) when it does not.
  bool ensureBuffer(Context & c, Buffer & b, VkDeviceSize need, VkBufferUsageFlags usage)
  {
    if (b.buffer && b.size >= need)
      return true;
    b.destroy(c.device);
    VkDeviceSize size = std::max<VkDeviceSize>(need + need / 2, 64 * 1024);
    return createBuffer(c, size, usage, b);
  }

  struct Image
  {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;

    void destroy(VkDevice d)
    {
      if (view) vkDestroyImageView(d, view, nullptr);
      if (image) vkDestroyImage(d, image, nullptr);
      if (memory) vkFreeMemory(d, memory, nullptr);
      *this = Image();
    }
  };

  bool createImage(Context & c, uint32_t w, uint32_t h, uint32_t mips, VkFormat format, VkSampleCountFlagBits samples,
                   VkImageUsageFlags usage, VkImageAspectFlags aspect, Image & out)
  {
    VkImageCreateInfo ici = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = { w, h, 1 };
    ici.mipLevels = mips;
    ici.arrayLayers = 1;
    ici.samples = samples;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = usage;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(c.device, &ici, nullptr, &out.image) != VK_SUCCESS)
      return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(c.device, out.image, &req);
    VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = c.memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(c.device, &mai, nullptr, &out.memory) != VK_SUCCESS)
      return false;
    vkBindImageMemory(c.device, out.image, out.memory, 0);
    VkImageViewCreateInfo vci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vci.image = out.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = format;
    vci.subresourceRange = { aspect, 0, mips, 0, 1 };
    return vkCreateImageView(c.device, &vci, nullptr, &out.view) == VK_SUCCESS;
  }

  void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to, uint32_t baseMip,
                    uint32_t mips, VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage,
                    VkPipelineStageFlags dstStage)
  {
    VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, baseMip, mips, 0, 1 };
    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
  }

  // A texture on the GPU, for one generation of a TextureManager texture.
  struct GpuTexture
  {
    Image image;
    uint32_t generation = 0;
    uint64_t lastUsedFrame = 0;
  };

  struct PipelineKey
  {
    uint8_t blend;
    bool blendEnabled, cull, frontCW, depthTest, depthWrite, lines, wireframe;
    uint32_t pack() const
    {
      return (uint32_t)blend | (blendEnabled << 8) | (cull << 9) | (frontCW << 10) | (depthTest << 11) |
             (depthWrite << 12) | (lines << 13) | (wireframe << 14);
    }
  };

  struct FrameResources
  {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    uint32_t poolCapacity = 0;
    Buffer vertices, indices, draws, frame;
  };

  // ------------------------------------------------------------------------------------------------ viewport
  class VulkanViewportImpl : public wmv::vk::Viewport
  {
  public:
    VulkanViewportImpl() : m_c(Context::get()) {}
    ~VulkanViewportImpl() override { shutdown(); }

    bool init(void * nativeWindow, std::string & error)
    {
      if (!m_c.init())
      {
        error = m_c.error;
        return false;
      }
      VkDevice d = m_c.device;

      if (nativeWindow)
      {
        m_surface = wmv::vk::createPlatformSurface(m_c.instance, nativeWindow, error);
        if (!m_surface)
          return false;
        VkBool32 supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_c.physical, m_c.queueFamily, m_surface, &supported);
        if (!supported)
        {
          error = "The Vulkan device cannot present to this window.";
          return false;
        }
        uint32_t fc = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_c.physical, m_surface, &fc, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(fc);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_c.physical, m_surface, &fc, formats.data());
        m_surfaceFormat = formats.empty() ? VkSurfaceFormatKHR{ COLOR_FORMAT_PREFERRED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }
                                          : formats[0];
        // UNORM, as GL's default framebuffer was: the shaders' values are the colours shown.
        for (const auto & f : formats)
          if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM)
          {
            m_surfaceFormat = f;
            break;
          }
      }
      else
        m_surfaceFormat = { COLOR_FORMAT_PREFERRED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
      m_colorFormat = m_surfaceFormat.format;

      // samples: the most of 4 the device can do for both colour and depth
      const VkSampleCountFlags counts = m_c.props.limits.framebufferColorSampleCounts &
                                        m_c.props.limits.framebufferDepthSampleCounts;
      m_samples = VK_SAMPLE_COUNT_1_BIT;
      for (int s = SAMPLE_TARGET; s >= 2; s /= 2)
        if (counts & (VkSampleCountFlags)s)
        {
          m_samples = (VkSampleCountFlagBits)s;
          break;
        }

      m_depthFormat = VK_FORMAT_D32_SFLOAT;
      for (VkFormat f : { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM })
      {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(m_c.physical, f, &fp);
        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
          m_depthFormat = f;
          break;
        }
      }

      VkCommandPoolCreateInfo cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
      cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
      cpci.queueFamilyIndex = m_c.queueFamily;
      vkCreateCommandPool(d, &cpci, nullptr, &m_cmdPool);

      for (auto & f : m_frames)
      {
        VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        cai.commandPool = m_cmdPool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        vkAllocateCommandBuffers(d, &cai, &f.cmd);
        VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(d, &fci, nullptr, &f.fence);
        VkSemaphoreCreateInfo sci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        vkCreateSemaphore(d, &sci, nullptr, &f.imageAvailable);
      }

      if (!createLayouts() || !createRenderPasses() || !createShaders() || !createSamplers() || !createWhiteTexture())
      {
        error = "The viewport's Vulkan objects could not be created.";
        return false;
      }
      return true;
    }

    // ---------------------------------------------------------------- frame
    bool render(const wmv::RenderList & list, const wmv::vk::FrameSettings & settings, int width, int height) override
    {
      if (!m_surface)
        return fail("This viewport has no window.");
      if (width <= 0 || height <= 0)
        return fail("The window has no size.");

      if (m_swapchain == VK_NULL_HANDLE || m_swapExtent.width != (uint32_t)width ||
          m_swapExtent.height != (uint32_t)height)
        if (!recreateSwapchain(width, height))
          return false;

      FrameResources & f = m_frames[m_frameIndex];
      vkWaitForFences(m_c.device, 1, &f.fence, VK_TRUE, UINT64_MAX);

      uint32_t imageIndex = 0;
      VkResult r = vkAcquireNextImageKHR(m_c.device, m_swapchain, UINT64_MAX, f.imageAvailable, VK_NULL_HANDLE,
                                         &imageIndex);
      if (r == VK_ERROR_OUT_OF_DATE_KHR)
      {
        m_swapExtent = { 0, 0 }; // recreated on the next frame
        return fail("The swapchain went out of date.");
      }
      if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
        return fail(std::string("vkAcquireNextImageKHR: ") + vkResultName(r));

      vkResetFences(m_c.device, 1, &f.fence);
      vkResetCommandBuffer(f.cmd, 0);
      VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
      bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(f.cmd, &bi);

      uploadTextures(f.cmd, list);
      if (!recordScene(f, list, settings, m_presentPass, m_swapFramebuffers[imageIndex], m_swapExtent))
      {
        vkEndCommandBuffer(f.cmd);
        submit(f, VK_NULL_HANDLE, VK_NULL_HANDLE); // keep the fence cycle intact
        return false;
      }
      vkEndCommandBuffer(f.cmd);

      VkSemaphore done = m_renderFinished[imageIndex];
      submit(f, f.imageAvailable, done);

      VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
      pi.waitSemaphoreCount = 1;
      pi.pWaitSemaphores = &done;
      pi.swapchainCount = 1;
      pi.pSwapchains = &m_swapchain;
      pi.pImageIndices = &imageIndex;
      r = vkQueuePresentKHR(m_c.queue, &pi);
      if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
        m_swapExtent = { 0, 0 };

      m_frameIndex = (m_frameIndex + 1) % FRAMES_IN_FLIGHT;
      m_frameCounter++;
      collectTextures();
      return true;
    }

    bool capture(const wmv::RenderList & list, const wmv::vk::FrameSettings & settings, int width, int height,
                 std::vector<unsigned char> & rgba) override
    {
      if (width <= 0 || height <= 0 || width > maxCaptureSize() || height > maxCaptureSize())
        return fail("The capture size is out of range.");
      VkDevice d = m_c.device;
      vkDeviceWaitIdle(d);

      const VkExtent2D extent = { (uint32_t)width, (uint32_t)height };
      Image color, depth, target;
      Buffer readback;
      VkFramebuffer fb = VK_NULL_HANDLE;
      bool ok = createTargets(extent, color, depth) &&
                createImage(m_c, extent.width, extent.height, 1, m_colorFormat, VK_SAMPLE_COUNT_1_BIT,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT, target) &&
                createBuffer(m_c, (VkDeviceSize)width * height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, readback);
      if (ok)
        fb = createFramebuffer(m_capturePass, extent, color, depth, target.view);
      if (!ok || !fb)
      {
        color.destroy(d); depth.destroy(d); target.destroy(d); readback.destroy(d);
        return fail("The capture image could not be created.");
      }

      FrameResources & f = m_frames[m_frameIndex];
      vkWaitForFences(d, 1, &f.fence, VK_TRUE, UINT64_MAX);
      vkResetFences(d, 1, &f.fence);
      vkResetCommandBuffer(f.cmd, 0);
      VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
      bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(f.cmd, &bi);
      uploadTextures(f.cmd, list);
      ok = recordScene(f, list, settings, m_capturePass, fb, extent);
      if (ok)
      {
        VkBufferImageCopy copy = {};
        copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copy.imageExtent = { extent.width, extent.height, 1 };
        vkCmdCopyImageToBuffer(f.cmd, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);
      }
      vkEndCommandBuffer(f.cmd);
      submit(f, VK_NULL_HANDLE, VK_NULL_HANDLE);
      vkWaitForFences(d, 1, &f.fence, VK_TRUE, UINT64_MAX);

      if (ok)
      {
        rgba.resize((size_t)width * height * 4);
        const unsigned char * src = static_cast<const unsigned char *>(readback.mapped);
        const bool bgra = (m_colorFormat == VK_FORMAT_B8G8R8A8_UNORM || m_colorFormat == VK_FORMAT_B8G8R8A8_SRGB);
        for (size_t i = 0; i < rgba.size(); i += 4)
        {
          rgba[i + 0] = src[i + (bgra ? 2 : 0)];
          rgba[i + 1] = src[i + 1];
          rgba[i + 2] = src[i + (bgra ? 0 : 2)];
          rgba[i + 3] = src[i + 3];
        }
      }

      vkDestroyFramebuffer(d, fb, nullptr);
      color.destroy(d); depth.destroy(d); target.destroy(d); readback.destroy(d);
      m_frameCounter++;
      return ok;
    }

    std::string description() const override
    {
      char buf[256];
      snprintf(buf, sizeof(buf), "%s, Vulkan %u.%u.%u%s", m_c.props.deviceName, VK_VERSION_MAJOR(m_c.props.apiVersion),
               VK_VERSION_MINOR(m_c.props.apiVersion), VK_VERSION_PATCH(m_c.props.apiVersion),
               m_c.portabilitySubset ? " (MoltenVK)" : "");
      return buf;
    }

    std::string lastError() const override { return m_error; }

    int maxCaptureSize() const override { return (int)std::min<uint32_t>(m_c.props.limits.maxFramebufferWidth, 16384); }

  private:
    bool fail(const std::string & why)
    {
      if (why != m_error)
        LOG_ERROR << "[vulkan]" << QString::fromStdString(why);
      m_error = why;
      return false;
    }

    void submit(FrameResources & f, VkSemaphore wait, VkSemaphore signal)
    {
      const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
      si.waitSemaphoreCount = wait ? 1 : 0;
      si.pWaitSemaphores = &wait;
      si.pWaitDstStageMask = &stage;
      si.commandBufferCount = 1;
      si.pCommandBuffers = &f.cmd;
      si.signalSemaphoreCount = signal ? 1 : 0;
      si.pSignalSemaphores = &signal;
      vkQueueSubmit(m_c.queue, 1, &si, f.fence);
    }

    // ---------------------------------------------------------------- setup
    bool createLayouts()
    {
      VkDevice d = m_c.device;
      VkDescriptorSetLayoutBinding frameBindings[2] = {};
      frameBindings[0].binding = 0;
      frameBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      frameBindings[0].descriptorCount = 1;
      frameBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
      frameBindings[1].binding = 1;
      frameBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      frameBindings[1].descriptorCount = 1;
      frameBindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
      VkDescriptorSetLayoutCreateInfo lci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
      lci.bindingCount = 2;
      lci.pBindings = frameBindings;
      if (vkCreateDescriptorSetLayout(d, &lci, nullptr, &m_frameSetLayout) != VK_SUCCESS)
        return false;

      VkDescriptorSetLayoutBinding texBindings[4] = {};
      for (uint32_t i = 0; i < 4; i++)
      {
        texBindings[i].binding = i;
        texBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texBindings[i].descriptorCount = 1;
        texBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
      }
      lci.bindingCount = 4;
      lci.pBindings = texBindings;
      if (vkCreateDescriptorSetLayout(d, &lci, nullptr, &m_texSetLayout) != VK_SUCCESS)
        return false;

      VkPushConstantRange pcr = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int32_t) };
      VkDescriptorSetLayout sets[2] = { m_frameSetLayout, m_texSetLayout };
      VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
      plci.setLayoutCount = 2;
      plci.pSetLayouts = sets;
      plci.pushConstantRangeCount = 1;
      plci.pPushConstantRanges = &pcr;
      return vkCreatePipelineLayout(d, &plci, nullptr, &m_pipelineLayout) == VK_SUCCESS;
    }

    VkRenderPass makeRenderPass(VkImageLayout finalLayout)
    {
      const bool msaa = m_samples != VK_SAMPLE_COUNT_1_BIT;
      std::vector<VkAttachmentDescription> att;
      VkAttachmentDescription color = {};
      color.format = m_colorFormat;
      color.samples = m_samples;
      color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      color.storeOp = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
      color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      color.finalLayout = msaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : finalLayout;
      att.push_back(color);

      VkAttachmentDescription depth = {};
      depth.format = m_depthFormat;
      depth.samples = m_samples;
      depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      att.push_back(depth);

      if (msaa)
      {
        VkAttachmentDescription resolve = color;
        resolve.samples = VK_SAMPLE_COUNT_1_BIT;
        resolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        resolve.finalLayout = finalLayout;
        att.push_back(resolve);
      }

      VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
      VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
      VkAttachmentReference resolveRef = { 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
      VkSubpassDescription sub = {};
      sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
      sub.colorAttachmentCount = 1;
      sub.pColorAttachments = &colorRef;
      sub.pDepthStencilAttachment = &depthRef;
      sub.pResolveAttachments = msaa ? &resolveRef : nullptr;

      VkSubpassDependency deps[2] = {};
      deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
      deps[0].dstSubpass = 0;
      deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
      deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
      deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      deps[1].srcSubpass = 0;
      deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
      deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      deps[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
      deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

      VkRenderPassCreateInfo rpci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
      rpci.attachmentCount = (uint32_t)att.size();
      rpci.pAttachments = att.data();
      rpci.subpassCount = 1;
      rpci.pSubpasses = &sub;
      rpci.dependencyCount = 2;
      rpci.pDependencies = deps;
      VkRenderPass pass = VK_NULL_HANDLE;
      vkCreateRenderPass(m_c.device, &rpci, nullptr, &pass);
      return pass;
    }

    bool createRenderPasses()
    {
      m_presentPass = makeRenderPass(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
      m_capturePass = makeRenderPass(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
      return m_presentPass && m_capturePass;
    }

    VkShaderModule shaderModule(const uint32_t * code, size_t bytes)
    {
      VkShaderModuleCreateInfo smci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
      smci.codeSize = bytes;
      smci.pCode = code;
      VkShaderModule m = VK_NULL_HANDLE;
      vkCreateShaderModule(m_c.device, &smci, nullptr, &m);
      return m;
    }

    bool createShaders()
    {
      m_vert = shaderModule(model_vert_spv, sizeof(model_vert_spv));
      m_frag = shaderModule(model_frag_spv, sizeof(model_frag_spv));
      return m_vert && m_frag;
    }

    bool createSamplers()
    {
      // [clampS][clampT]
      for (int s = 0; s < 2; s++)
        for (int t = 0; t < 2; t++)
        {
          VkSamplerCreateInfo sci = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
          sci.magFilter = VK_FILTER_LINEAR;
          sci.minFilter = VK_FILTER_LINEAR;
          sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
          sci.addressModeU = s ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
          sci.addressModeV = t ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
          sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
          sci.maxLod = VK_LOD_CLAMP_NONE;
          if (m_c.enabledFeatures.samplerAnisotropy)
          {
            sci.anisotropyEnable = VK_TRUE;
            sci.maxAnisotropy = std::min(8.0f, m_c.props.limits.maxSamplerAnisotropy);
          }
          if (vkCreateSampler(m_c.device, &sci, nullptr, &m_samplers[s][t]) != VK_SUCCESS)
            return false;
        }
      return true;
    }

    // ---------------------------------------------------------------- one-time commands
    VkCommandBuffer beginOneTime()
    {
      VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
      cai.commandPool = m_cmdPool;
      cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      cai.commandBufferCount = 1;
      VkCommandBuffer cmd;
      vkAllocateCommandBuffers(m_c.device, &cai, &cmd);
      VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
      bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(cmd, &bi);
      return cmd;
    }

    void endOneTime(VkCommandBuffer cmd)
    {
      vkEndCommandBuffer(cmd);
      VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
      si.commandBufferCount = 1;
      si.pCommandBuffers = &cmd;
      vkQueueSubmit(m_c.queue, 1, &si, VK_NULL_HANDLE);
      vkQueueWaitIdle(m_c.queue);
      vkFreeCommandBuffers(m_c.device, m_cmdPool, 1, &cmd);
    }

    // ---------------------------------------------------------------- textures
    bool createWhiteTexture()
    {
      if (!createImage(m_c, 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT,
                       VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, m_white))
        return false;
      Buffer staging;
      if (!createBuffer(m_c, 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging))
        return false;
      std::memset(staging.mapped, 0xff, 4);
      VkCommandBuffer cmd = beginOneTime();
      imageBarrier(cmd, m_white.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 1, 0,
                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
      VkBufferImageCopy copy = {};
      copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
      copy.imageExtent = { 1, 1, 1 };
      vkCmdCopyBufferToImage(cmd, staging.buffer, m_white.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
      imageBarrier(cmd, m_white.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   0, 1, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
      endOneTime(cmd);
      staging.destroy(m_c.device);
      return true;
    }

    // Uploads, before the frame's render pass, every texture the list names that the GPU does not have in its
    // current generation. Levels the texture lacks are made by blits.
    void uploadTextures(VkCommandBuffer cmd, const wmv::RenderList & list)
    {
      for (const wmv::Draw & d : list.draws)
        for (int u = 0; u < 4; u++)
        {
          const TextureID id = d.material.tex[u];
          if (id == 0)
            continue;
          const Texture * tex = TEXTUREMANAGER.texture(id);
          if (!tex || tex->mips.empty() || tex->w <= 0 || tex->h <= 0)
            continue;
          auto it = m_textures.find(id);
          if (it != m_textures.end())
          {
            it->second.lastUsedFrame = m_frameCounter;
            if (it->second.generation == tex->generation)
              continue;
            retire(it->second.image);
            m_textures.erase(it);
          }
          GpuTexture g;
          if (uploadTexture(cmd, *tex, g.image))
          {
            g.generation = tex->generation;
            g.lastUsedFrame = m_frameCounter;
            m_textures[id] = g;
          }
        }
    }

    bool uploadTexture(VkCommandBuffer cmd, const Texture & tex, Image & out)
    {
      const uint32_t w = (uint32_t)tex.w, h = (uint32_t)tex.h;
      const uint32_t fullMips = 1 + (uint32_t)std::floor(std::log2((float)std::max(w, h)));
      // levels as stored, as long as each is the size it should be
      uint32_t stored = 0;
      for (uint32_t l = 0; l < tex.mips.size() && l < fullMips; l++)
      {
        const uint32_t lw = std::max(1u, w >> l), lh = std::max(1u, h >> l);
        if (tex.mips[l].size() != (size_t)lw * lh * 4)
          break;
        stored++;
      }
      if (stored == 0)
        return false;

      if (!createImage(m_c, w, h, fullMips, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT,
                       VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                       VK_IMAGE_ASPECT_COLOR_BIT, out))
      {
        out.destroy(m_c.device);
        return false;
      }

      VkDeviceSize total = 0;
      for (uint32_t l = 0; l < stored; l++)
        total += tex.mips[l].size();
      Buffer staging;
      if (!createBuffer(m_c, total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging))
      {
        out.destroy(m_c.device);
        return false;
      }
      std::vector<VkBufferImageCopy> copies;
      VkDeviceSize offset = 0;
      for (uint32_t l = 0; l < stored; l++)
      {
        std::memcpy((char *)staging.mapped + offset, tex.mips[l].data(), tex.mips[l].size());
        VkBufferImageCopy c = {};
        c.bufferOffset = offset;
        c.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1 };
        c.imageExtent = { std::max(1u, w >> l), std::max(1u, h >> l), 1 };
        copies.push_back(c);
        offset += tex.mips[l].size();
      }

      imageBarrier(cmd, out.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, fullMips, 0,
                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
      vkCmdCopyBufferToImage(cmd, staging.buffer, out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             (uint32_t)copies.size(), copies.data());

      // the levels the file did not have, each blitted from the one above it
      for (uint32_t l = stored; l < fullMips; l++)
      {
        imageBarrier(cmd, out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, l - 1,
                     1, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkImageBlit blit = {};
        blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, l - 1, 0, 1 };
        blit.srcOffsets[1] = { (int32_t)std::max(1u, w >> (l - 1)), (int32_t)std::max(1u, h >> (l - 1)), 1 };
        blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1 };
        blit.dstOffsets[1] = { (int32_t)std::max(1u, w >> l), (int32_t)std::max(1u, h >> l), 1 };
        vkCmdBlitImage(cmd, out.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, out.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        imageBarrier(cmd, out.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, l - 1,
                     1, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT);
      }
      imageBarrier(cmd, out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0,
                   fullMips, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

      // the staging buffer lives until the frame that used it has finished
      m_retiredBuffers.push_back({ staging, m_frameCounter });
      return true;
    }

    void retire(Image & image)
    {
      m_retiredImages.push_back({ image, m_frameCounter });
      image = Image();
    }

    // Frees what the GPU can no longer be using, and the copies of textures the manager has dropped.
    void collectTextures()
    {
      const uint64_t safe = m_frameCounter > FRAMES_IN_FLIGHT + 1 ? m_frameCounter - FRAMES_IN_FLIGHT - 1 : 0;
      for (auto it = m_retiredImages.begin(); it != m_retiredImages.end();)
        if (it->second <= safe) { it->first.destroy(m_c.device); it = m_retiredImages.erase(it); }
        else ++it;
      for (auto it = m_retiredBuffers.begin(); it != m_retiredBuffers.end();)
        if (it->second <= safe) { it->first.destroy(m_c.device); it = m_retiredBuffers.erase(it); }
        else ++it;

      if (m_frameCounter % 60 != 0)
        return;
      for (auto it = m_textures.begin(); it != m_textures.end();)
      {
        if (!TEXTUREMANAGER.texture(it->first) || m_frameCounter - it->second.lastUsedFrame > 600)
        {
          retire(it->second.image);
          it = m_textures.erase(it);
        }
        else
          ++it;
      }
    }

    VkImageView textureView(TextureID id, bool textured)
    {
      if (!textured || id == 0)
        return m_white.view;
      auto it = m_textures.find(id);
      return it == m_textures.end() ? m_white.view : it->second.image.view;
    }

    // ---------------------------------------------------------------- pipelines
    VkPipeline pipeline(const PipelineKey & key, VkRenderPass pass)
    {
      const uint64_t k = ((uint64_t)key.pack() << 1) | (pass == m_capturePass ? 1 : 0);
      auto it = m_pipelines.find(k);
      if (it != m_pipelines.end())
        return it->second;

      VkPipelineShaderStageCreateInfo stages[2] = {};
      stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
      stages[0].module = m_vert;
      stages[0].pName = "main";
      stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
      stages[1].module = m_frag;
      stages[1].pName = "main";

      VkVertexInputBindingDescription binding = { 0, sizeof(wmv::Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
      VkVertexInputAttributeDescription attrs[5] = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, (uint32_t)offsetof(wmv::Vertex, pos) },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, (uint32_t)offsetof(wmv::Vertex, normal) },
        { 2, 0, VK_FORMAT_R32G32_SFLOAT, (uint32_t)offsetof(wmv::Vertex, uv0) },
        { 3, 0, VK_FORMAT_R32G32_SFLOAT, (uint32_t)offsetof(wmv::Vertex, uv1) },
        { 4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, (uint32_t)offsetof(wmv::Vertex, color) },
      };
      VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
      vi.vertexBindingDescriptionCount = 1;
      vi.pVertexBindingDescriptions = &binding;
      vi.vertexAttributeDescriptionCount = 5;
      vi.pVertexAttributeDescriptions = attrs;

      VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
      ia.topology = key.lines ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

      VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
      vp.viewportCount = 1;
      vp.scissorCount = 1;

      VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
      rs.polygonMode = (key.wireframe && m_c.enabledFeatures.fillModeNonSolid) ? VK_POLYGON_MODE_LINE
                                                                               : VK_POLYGON_MODE_FILL;
      rs.cullMode = key.cull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
      // GL's default front face is counter-clockwise. The viewport flips y (a negative viewport height), which
      // keeps GL's winding, so the same faces are the front ones.
      rs.frontFace = key.frontCW ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
      rs.lineWidth = 1.0f;

      VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
      ms.rasterizationSamples = m_samples;

      VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
      ds.depthTestEnable = key.depthTest;
      ds.depthWriteEnable = key.depthWrite;
      ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

      VkPipelineColorBlendAttachmentState cb = {};
      cb.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                          VK_COLOR_COMPONENT_A_BIT;
      if (key.blendEnabled)
      {
        VkBlendFactor src = VK_BLEND_FACTOR_SRC_ALPHA, dst = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        switch (key.blend)
        {
        case wmv::BLEND_OPAQUE:      src = VK_BLEND_FACTOR_SRC_ALPHA; dst = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; break;
        case wmv::BLEND_ALPHA_KEY:   src = VK_BLEND_FACTOR_ONE; dst = VK_BLEND_FACTOR_ZERO; break;
        case wmv::BLEND_ALPHA:       src = VK_BLEND_FACTOR_SRC_ALPHA; dst = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; break;
        case wmv::BLEND_ADD:         src = VK_BLEND_FACTOR_ONE; dst = VK_BLEND_FACTOR_ONE; break;
        case wmv::BLEND_ADD_ALPHA:   src = VK_BLEND_FACTOR_SRC_ALPHA; dst = VK_BLEND_FACTOR_ONE; break;
        case wmv::BLEND_MODULATE:    src = VK_BLEND_FACTOR_DST_COLOR; dst = VK_BLEND_FACTOR_ZERO; break;
        case wmv::BLEND_MODULATE_2X: src = VK_BLEND_FACTOR_DST_COLOR; dst = VK_BLEND_FACTOR_SRC_COLOR; break;
        case wmv::BLEND_7:           src = VK_BLEND_FACTOR_ONE; dst = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; break;
        case wmv::BLEND_ADD_COLOR:   src = VK_BLEND_FACTOR_SRC_COLOR; dst = VK_BLEND_FACTOR_ONE; break;
        default: break;
        }
        cb.blendEnable = VK_TRUE;
        cb.srcColorBlendFactor = src;
        cb.dstColorBlendFactor = dst;
        cb.colorBlendOp = VK_BLEND_OP_ADD;
        // glBlendFunc applies one pair to alpha as well; colour factors are not valid for alpha in every case
        // they mean the same, so map them onto their alpha equivalents.
        auto alphaOf = [](VkBlendFactor f) {
          switch (f)
          {
          case VK_BLEND_FACTOR_SRC_COLOR: return VK_BLEND_FACTOR_SRC_ALPHA;
          case VK_BLEND_FACTOR_DST_COLOR: return VK_BLEND_FACTOR_DST_ALPHA;
          case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
          default: return f;
          }
        };
        cb.srcAlphaBlendFactor = alphaOf(src);
        cb.dstAlphaBlendFactor = alphaOf(dst);
        cb.alphaBlendOp = VK_BLEND_OP_ADD;
      }
      VkPipelineColorBlendStateCreateInfo cbs = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
      cbs.attachmentCount = 1;
      cbs.pAttachments = &cb;

      VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
      VkPipelineDynamicStateCreateInfo dy = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
      dy.dynamicStateCount = 2;
      dy.pDynamicStates = dyn;

      VkGraphicsPipelineCreateInfo gpci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
      gpci.stageCount = 2;
      gpci.pStages = stages;
      gpci.pVertexInputState = &vi;
      gpci.pInputAssemblyState = &ia;
      gpci.pViewportState = &vp;
      gpci.pRasterizationState = &rs;
      gpci.pMultisampleState = &ms;
      gpci.pDepthStencilState = &ds;
      gpci.pColorBlendState = &cbs;
      gpci.pDynamicState = &dy;
      gpci.layout = m_pipelineLayout;
      gpci.renderPass = pass;
      VkPipeline p = VK_NULL_HANDLE;
      if (vkCreateGraphicsPipelines(m_c.device, VK_NULL_HANDLE, 1, &gpci, nullptr, &p) != VK_SUCCESS)
        p = VK_NULL_HANDLE;
      m_pipelines[k] = p;
      return p;
    }

    // ---------------------------------------------------------------- targets
    bool createTargets(VkExtent2D extent, Image & color, Image & depth)
    {
      const bool msaa = m_samples != VK_SAMPLE_COUNT_1_BIT;
      if (msaa && !createImage(m_c, extent.width, extent.height, 1, m_colorFormat, m_samples,
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                               VK_IMAGE_ASPECT_COLOR_BIT, color))
        return false;
      return createImage(m_c, extent.width, extent.height, 1, m_depthFormat, m_samples,
                         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                         VK_IMAGE_ASPECT_DEPTH_BIT, depth);
    }

    VkFramebuffer createFramebuffer(VkRenderPass pass, VkExtent2D extent, const Image & color, const Image & depth,
                                    VkImageView target)
    {
      const bool msaa = m_samples != VK_SAMPLE_COUNT_1_BIT;
      std::vector<VkImageView> views;
      views.push_back(msaa ? color.view : target);
      views.push_back(depth.view);
      if (msaa)
        views.push_back(target);
      VkFramebufferCreateInfo fci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
      fci.renderPass = pass;
      fci.attachmentCount = (uint32_t)views.size();
      fci.pAttachments = views.data();
      fci.width = extent.width;
      fci.height = extent.height;
      fci.layers = 1;
      VkFramebuffer fb = VK_NULL_HANDLE;
      vkCreateFramebuffer(m_c.device, &fci, nullptr, &fb);
      return fb;
    }

    void destroySwapchain()
    {
      VkDevice d = m_c.device;
      for (VkFramebuffer fb : m_swapFramebuffers)
        vkDestroyFramebuffer(d, fb, nullptr);
      m_swapFramebuffers.clear();
      for (VkImageView v : m_swapViews)
        vkDestroyImageView(d, v, nullptr);
      m_swapViews.clear();
      for (VkSemaphore s : m_renderFinished)
        vkDestroySemaphore(d, s, nullptr);
      m_renderFinished.clear();
      m_swapColor.destroy(d);
      m_swapDepth.destroy(d);
    }

    bool recreateSwapchain(int width, int height)
    {
      VkDevice d = m_c.device;
      vkDeviceWaitIdle(d);
      destroySwapchain();

      VkSurfaceCapabilitiesKHR caps;
      vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_c.physical, m_surface, &caps);
      VkExtent2D extent = { (uint32_t)width, (uint32_t)height };
      if (caps.currentExtent.width != UINT32_MAX)
        extent = caps.currentExtent;
      extent.width = std::max(caps.minImageExtent.width, std::min(caps.maxImageExtent.width, extent.width));
      extent.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, extent.height));
      if (extent.width == 0 || extent.height == 0)
        return fail("The window has no size.");

      uint32_t images = std::max(caps.minImageCount, 2u);
      if (caps.maxImageCount && images > caps.maxImageCount)
        images = caps.maxImageCount;

      VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
      if (!(caps.supportedCompositeAlpha & alpha))
        for (VkCompositeAlphaFlagBitsKHR a : { VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                              VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR })
          if (caps.supportedCompositeAlpha & a) { alpha = a; break; }

      VkSwapchainCreateInfoKHR sci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
      sci.surface = m_surface;
      sci.minImageCount = images;
      sci.imageFormat = m_surfaceFormat.format;
      sci.imageColorSpace = m_surfaceFormat.colorSpace;
      sci.imageExtent = extent;
      sci.imageArrayLayers = 1;
      sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
      sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
      sci.preTransform = caps.currentTransform;
      sci.compositeAlpha = alpha;
      sci.presentMode = VK_PRESENT_MODE_FIFO_KHR; // vsync: the viewport never needs more than the display shows
      sci.clipped = VK_TRUE;
      sci.oldSwapchain = m_swapchain;
      VkSwapchainKHR swapchain = VK_NULL_HANDLE;
      VkResult r = vkCreateSwapchainKHR(d, &sci, nullptr, &swapchain);
      if (m_swapchain)
        vkDestroySwapchainKHR(d, m_swapchain, nullptr);
      m_swapchain = swapchain;
      if (r != VK_SUCCESS)
      {
        m_swapchain = VK_NULL_HANDLE;
        return fail(std::string("vkCreateSwapchainKHR: ") + vkResultName(r));
      }
      m_swapExtent = extent;

      uint32_t count = 0;
      vkGetSwapchainImagesKHR(d, m_swapchain, &count, nullptr);
      std::vector<VkImage> swapImages(count);
      vkGetSwapchainImagesKHR(d, m_swapchain, &count, swapImages.data());

      if (!createTargets(extent, m_swapColor, m_swapDepth))
        return fail("The viewport's render targets could not be created.");

      for (VkImage img : swapImages)
      {
        VkImageViewCreateInfo vci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vci.image = img;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = m_surfaceFormat.format;
        vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkImageView view;
        vkCreateImageView(d, &vci, nullptr, &view);
        m_swapViews.push_back(view);
        m_swapFramebuffers.push_back(createFramebuffer(m_presentPass, extent, m_swapColor, m_swapDepth, view));
        VkSemaphoreCreateInfo semci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkSemaphore s;
        vkCreateSemaphore(d, &semci, nullptr, &s);
        m_renderFinished.push_back(s);
      }
      LOG_INFO << "[vulkan] swapchain" << extent.width << "x" << extent.height << "," << count << "images,"
               << (int)m_samples << "samples";
      return true;
    }

    // ---------------------------------------------------------------- recording
    bool recordScene(FrameResources & f, const wmv::RenderList & list, const wmv::vk::FrameSettings & settings,
                     VkRenderPass pass, VkFramebuffer fb, VkExtent2D extent)
    {
      VkDevice d = m_c.device;

      // draw order: by layer, then as recorded
      std::vector<uint32_t> order(list.draws.size());
      for (uint32_t i = 0; i < order.size(); i++)
        order[i] = i;
      std::stable_sort(order.begin(), order.end(),
                       [&](uint32_t a, uint32_t b) { return list.draws[a].layer < list.draws[b].layer; });

      // per-frame data
      const VkDeviceSize vbytes = std::max<size_t>(list.vertices.size(), 1) * sizeof(wmv::Vertex);
      const VkDeviceSize ibytes = std::max<size_t>(list.indices.size(), 1) * sizeof(uint32_t);
      const VkDeviceSize dbytes = std::max<size_t>(list.draws.size(), 1) * sizeof(GpuDrawData);
      if (!ensureBuffer(m_c, f.vertices, vbytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
          !ensureBuffer(m_c, f.indices, ibytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT) ||
          !ensureBuffer(m_c, f.draws, dbytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) ||
          !ensureBuffer(m_c, f.frame, sizeof(GpuFrameData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT))
        return fail("The frame's buffers could not be allocated.");
      if (!list.vertices.empty())
        std::memcpy(f.vertices.mapped, list.vertices.data(), list.vertices.size() * sizeof(wmv::Vertex));
      if (!list.indices.empty())
        std::memcpy(f.indices.mapped, list.indices.data(), list.indices.size() * sizeof(uint32_t));

      // GL's clip space has z in [-1, 1]; Vulkan's in [0, 1].
      const glm::mat4 clipFix(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0.5f, 1);
      const glm::mat4 proj = clipFix * list.projection;

      GpuDrawData * gd = static_cast<GpuDrawData *>(f.draws.mapped);
      for (size_t i = 0; i < list.draws.size(); i++)
      {
        const wmv::Draw & dr = list.draws[i];
        const wmv::Material & m = dr.material;
        GpuDrawData & g = gd[i];
        if (m.screenSpace)
        {
          g.mvp = clipFix * dr.model;
          g.modelView = glm::mat4(1.0f);
        }
        else
        {
          g.modelView = list.view * dr.model;
          g.mvp = proj * g.modelView;
        }
        g.normalMatrix = glm::inverseTranspose(g.modelView);
        g.texMatrix0 = m.texMatrix[0];
        g.texMatrix1 = m.texMatrix[1];
        g.color = m.color;
        g.emissive = m.emissive;
        g.uvSource = glm::ivec4(m.uvSource[0], m.uvSource[1], m.uvSource[2], m.uvSource[3]);
        g.pixelShader = m.pixelShader;
        g.egxBlendMode = m.egxBlendMode;
        static const bool noAlphaTest = getenv("WMV_DEBUG_NOALPHATEST") != nullptr;
        g.flags = (m.unlit ? FLAG_UNLIT : 0) | (m.textured ? FLAG_TEXTURED : 0) |
                  (m.alphaTest && !noAlphaTest ? FLAG_ALPHA_TEST : 0) |
                  (m.particleMultiTexture ? FLAG_PARTICLE_MULTITEX : 0) | (m.screenSpace ? FLAG_SCREEN_SPACE : 0) |
                  (m.twoSided && !m.cull ? FLAG_TWO_SIDED : 0);
        g.textureCount = m.textureCount;
        g.alphaRef = m.alphaRef;
        g.combinerAlphaTest = m.combinerAlphaTest;
        g.specularWeight = m.specularWeight;
        g.envBoost = m.envBoost;
        g.metalSpec = m.metalSpec;
        g.metalTight = m.metalTight;
        g.pad0 = g.pad1 = 0.0f;
      }

      GpuFrameData fd;
      const glm::vec3 keyView = glm::normalize(glm::mat3(list.view) * settings.keyDirection);
      fd.keyDirView = glm::vec4(keyView, 0.0f);
      fd.lightLevels = glm::vec4(settings.ambient, settings.key, settings.front, settings.ceiling);
      fd.lightColor = glm::vec4(settings.lightColor, 1.0f);
      fd.options = glm::vec4(settings.lightingOff ? 1.0f : 0.0f, 0, 0, 0);
      std::memcpy(f.frame.mapped, &fd, sizeof(fd));

      // descriptors: one frame set, one texture set per draw
      const uint32_t setsNeeded = (uint32_t)list.draws.size() + 1;
      if (f.poolCapacity < setsNeeded)
      {
        if (f.pool)
          vkDestroyDescriptorPool(d, f.pool, nullptr);
        f.poolCapacity = std::max(setsNeeded * 2, 256u);
        VkDescriptorPoolSize sizes[3] = {
          { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 },
          { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
          { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, f.poolCapacity * 4 },
        };
        VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        dpci.maxSets = f.poolCapacity;
        dpci.poolSizeCount = 3;
        dpci.pPoolSizes = sizes;
        vkCreateDescriptorPool(d, &dpci, nullptr, &f.pool);
      }
      else
        vkResetDescriptorPool(d, f.pool, 0);

      VkDescriptorSet frameSet;
      VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
      dsai.descriptorPool = f.pool;
      dsai.descriptorSetCount = 1;
      dsai.pSetLayouts = &m_frameSetLayout;
      if (vkAllocateDescriptorSets(d, &dsai, &frameSet) != VK_SUCCESS)
        return fail("The frame's descriptors could not be allocated.");
      VkDescriptorBufferInfo drawsInfo = { f.draws.buffer, 0, VK_WHOLE_SIZE };
      VkDescriptorBufferInfo frameInfo = { f.frame.buffer, 0, sizeof(GpuFrameData) };
      VkWriteDescriptorSet writes[2] = {};
      writes[0].sType = writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[0].dstSet = writes[1].dstSet = frameSet;
      writes[0].dstBinding = 0;
      writes[0].descriptorCount = 1;
      writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writes[0].pBufferInfo = &drawsInfo;
      writes[1].dstBinding = 1;
      writes[1].descriptorCount = 1;
      writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      writes[1].pBufferInfo = &frameInfo;
      vkUpdateDescriptorSets(d, 2, writes, 0, nullptr);

      // ---- the pass
      VkClearValue clears[3] = {};
      clears[0].color = { { settings.clearColor.r, settings.clearColor.g, settings.clearColor.b, settings.clearColor.a } };
      clears[1].depthStencil = { 1.0f, 0 };
      clears[2].color = clears[0].color;
      VkRenderPassBeginInfo rpbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
      rpbi.renderPass = pass;
      rpbi.framebuffer = fb;
      rpbi.renderArea = { { 0, 0 }, extent };
      rpbi.clearValueCount = m_samples != VK_SAMPLE_COUNT_1_BIT ? 3 : 2;
      rpbi.pClearValues = clears;
      vkCmdBeginRenderPass(f.cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

      // y flipped, as GL's window coordinates run up
      VkViewport viewport = { 0.0f, (float)extent.height, (float)extent.width, -(float)extent.height, 0.0f, 1.0f };
      VkRect2D scissor = { { 0, 0 }, extent };
      vkCmdSetViewport(f.cmd, 0, 1, &viewport);
      vkCmdSetScissor(f.cmd, 0, 1, &scissor);

      VkDeviceSize zero = 0;
      vkCmdBindVertexBuffers(f.cmd, 0, 1, &f.vertices.buffer, &zero);
      vkCmdBindIndexBuffer(f.cmd, f.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
      vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &frameSet, 0, nullptr);

      VkPipeline bound = VK_NULL_HANDLE;
      for (uint32_t i : order)
      {
        const wmv::Draw & dr = list.draws[i];
        if (dr.indexCount == 0)
          continue;
        const wmv::Material & m = dr.material;
        static const bool noCull = getenv("WMV_DEBUG_NOCULL") != nullptr;
        PipelineKey key;
        key.blend = (uint8_t)m.blend;
        key.blendEnabled = m.blendEnabled;
        key.cull = m.cull && !noCull;
        key.frontCW = m.frontFaceCW;
        key.depthTest = m.depthTest;
        key.depthWrite = m.depthWrite;
        key.lines = dr.primitive == wmv::PRIM_LINES;
        key.wireframe = m.wireframe;
        VkPipeline p = pipeline(key, pass);
        if (!p)
          continue;
        if (p != bound)
        {
          vkCmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
          bound = p;
        }

        VkDescriptorSet texSet;
        dsai.pSetLayouts = &m_texSetLayout;
        if (vkAllocateDescriptorSets(d, &dsai, &texSet) != VK_SUCCESS)
          break;
        VkDescriptorImageInfo infos[4];
        VkWriteDescriptorSet tw[4] = {};
        for (int u = 0; u < 4; u++)
        {
          // GL's wrap state was set on the pass's first texture only
          const bool clampS = (u == 0) && !m.wrapS, clampT = (u == 0) && !m.wrapT;
          infos[u] = { m_samplers[clampS][clampT], textureView(m.tex[u], m.textured),
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
          tw[u].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
          tw[u].dstSet = texSet;
          tw[u].dstBinding = (uint32_t)u;
          tw[u].descriptorCount = 1;
          tw[u].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
          tw[u].pImageInfo = &infos[u];
        }
        vkUpdateDescriptorSets(d, 4, tw, 0, nullptr);
        vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 1, 1, &texSet, 0, nullptr);

        const int32_t index = (int32_t)i;
        vkCmdPushConstants(f.cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(index), &index);
        vkCmdDrawIndexed(f.cmd, dr.indexCount, 1, dr.firstIndex, 0, 0);
      }

      vkCmdEndRenderPass(f.cmd);
      return true;
    }

    void shutdown()
    {
      if (!m_c.device)
        return;
      VkDevice d = m_c.device;
      vkDeviceWaitIdle(d);
      destroySwapchain();
      if (m_swapchain) vkDestroySwapchainKHR(d, m_swapchain, nullptr);
      m_swapchain = VK_NULL_HANDLE;
      for (auto & t : m_textures) t.second.image.destroy(d);
      m_textures.clear();
      for (auto & r : m_retiredImages) r.first.destroy(d);
      m_retiredImages.clear();
      for (auto & r : m_retiredBuffers) r.first.destroy(d);
      m_retiredBuffers.clear();
      m_white.destroy(d);
      for (auto & f : m_frames)
      {
        f.vertices.destroy(d); f.indices.destroy(d); f.draws.destroy(d); f.frame.destroy(d);
        if (f.pool) vkDestroyDescriptorPool(d, f.pool, nullptr);
        if (f.fence) vkDestroyFence(d, f.fence, nullptr);
        if (f.imageAvailable) vkDestroySemaphore(d, f.imageAvailable, nullptr);
        f = FrameResources();
      }
      for (auto & p : m_pipelines) if (p.second) vkDestroyPipeline(d, p.second, nullptr);
      m_pipelines.clear();
      for (auto & row : m_samplers) for (VkSampler s : row) if (s) vkDestroySampler(d, s, nullptr);
      if (m_vert) vkDestroyShaderModule(d, m_vert, nullptr);
      if (m_frag) vkDestroyShaderModule(d, m_frag, nullptr);
      if (m_presentPass) vkDestroyRenderPass(d, m_presentPass, nullptr);
      if (m_capturePass) vkDestroyRenderPass(d, m_capturePass, nullptr);
      if (m_pipelineLayout) vkDestroyPipelineLayout(d, m_pipelineLayout, nullptr);
      if (m_frameSetLayout) vkDestroyDescriptorSetLayout(d, m_frameSetLayout, nullptr);
      if (m_texSetLayout) vkDestroyDescriptorSetLayout(d, m_texSetLayout, nullptr);
      if (m_cmdPool) vkDestroyCommandPool(d, m_cmdPool, nullptr);
      if (m_surface) vkDestroySurfaceKHR(m_c.instance, m_surface, nullptr);
      m_surface = VK_NULL_HANDLE;
    }

    Context & m_c;
    std::string m_error;

    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSurfaceFormatKHR m_surfaceFormat = {};
    VkFormat m_colorFormat = COLOR_FORMAT_PREFERRED;
    VkFormat m_depthFormat = VK_FORMAT_D32_SFLOAT;
    VkSampleCountFlagBits m_samples = VK_SAMPLE_COUNT_1_BIT;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkExtent2D m_swapExtent = { 0, 0 };
    std::vector<VkImageView> m_swapViews;
    std::vector<VkFramebuffer> m_swapFramebuffers;
    std::vector<VkSemaphore> m_renderFinished;
    Image m_swapColor, m_swapDepth;

    VkCommandPool m_cmdPool = VK_NULL_HANDLE;
    std::array<FrameResources, FRAMES_IN_FLIGHT> m_frames;
    uint32_t m_frameIndex = 0;
    uint64_t m_frameCounter = 1;

    VkRenderPass m_presentPass = VK_NULL_HANDLE, m_capturePass = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_frameSetLayout = VK_NULL_HANDLE, m_texSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkShaderModule m_vert = VK_NULL_HANDLE, m_frag = VK_NULL_HANDLE;
    VkSampler m_samplers[2][2] = {};
    std::unordered_map<uint64_t, VkPipeline> m_pipelines;

    Image m_white;
    std::unordered_map<TextureID, GpuTexture> m_textures;
    std::vector<std::pair<Image, uint64_t>> m_retiredImages;
    std::vector<std::pair<Buffer, uint64_t>> m_retiredBuffers;
  };
}

std::unique_ptr<wmv::vk::Viewport> wmv::vk::Viewport::create(void * nativeWindow, std::string & error)
{
  std::unique_ptr<VulkanViewportImpl> v(new VulkanViewportImpl());
  if (!v->init(nativeWindow, error))
    return nullptr;
  return std::unique_ptr<Viewport>(v.release());
}
