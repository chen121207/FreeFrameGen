#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <vulkan/vulkan.h>

#include "ffg/ffg_vk.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
#ifdef min
#undef min
#endif

namespace {

// This test deliberately loads the Vulkan loader at runtime.  Vulkan SDKs do
// not have to ship an import library on Windows, and a machine without a
// loader should produce a CTest skip (77), rather than a false failure.
HMODULE gLoader = nullptr;
PFN_vkGetInstanceProcAddr gGetInstanceProcAddr = nullptr;
PFN_vkGetDeviceProcAddr gGetDeviceProcAddr = nullptr;
bool gUseSynchronization2 = false;

void logSkip(const char* reason) { std::printf("SKIP: %s\n", reason); }

template <typename T>
T instanceProc(VkInstance instance, const char* name) {
  return reinterpret_cast<T>(gGetInstanceProcAddr(instance, name));
}

// The implementation uses vkCmdPipelineBarrier2 when the device exposes the
// entry point.  This test enables synchronization2 when available; on a 1.2
// device we hide that optional entry point so the implementation uses the
// legacy barrier path, which is valid without the synchronization2 feature.
PFN_vkVoidFunction VKAPI_PTR testGetDeviceProcAddr(VkDevice device, const char* name) {
  if (!gGetDeviceProcAddr)
    return nullptr;
  if (!gUseSynchronization2 &&
      (std::strcmp(name, "vkCmdPipelineBarrier2") == 0 ||
       std::strcmp(name, "vkCmdPipelineBarrier2KHR") == 0))
    return nullptr;
  return gGetDeviceProcAddr(device, name);
}

template <typename T>
uint64_t handleValue(T handle) {
  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle));
}

struct DeviceFns {
  PFN_vkDestroyInstance destroyInstance = nullptr;
  PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices = nullptr;
  PFN_vkGetPhysicalDeviceProperties getPhysicalDeviceProperties = nullptr;
  PFN_vkGetPhysicalDeviceFeatures2 getPhysicalDeviceFeatures2 = nullptr;
  PFN_vkGetPhysicalDeviceQueueFamilyProperties getPhysicalDeviceQueueFamilyProperties = nullptr;
  PFN_vkGetPhysicalDeviceMemoryProperties getPhysicalDeviceMemoryProperties = nullptr;
  PFN_vkCreateDevice createDevice = nullptr;
  PFN_vkDestroyDevice destroyDevice = nullptr;
  PFN_vkGetDeviceQueue getDeviceQueue = nullptr;
  PFN_vkCreateCommandPool createCommandPool = nullptr;
  PFN_vkDestroyCommandPool destroyCommandPool = nullptr;
  PFN_vkAllocateCommandBuffers allocateCommandBuffers = nullptr;
  PFN_vkFreeCommandBuffers freeCommandBuffers = nullptr;
  PFN_vkBeginCommandBuffer beginCommandBuffer = nullptr;
  PFN_vkEndCommandBuffer endCommandBuffer = nullptr;
  PFN_vkResetCommandBuffer resetCommandBuffer = nullptr;
  PFN_vkQueueSubmit queueSubmit = nullptr;
  PFN_vkQueueWaitIdle queueWaitIdle = nullptr;
  PFN_vkDeviceWaitIdle deviceWaitIdle = nullptr;
  PFN_vkCreateImage createImage = nullptr;
  PFN_vkDestroyImage destroyImage = nullptr;
  PFN_vkGetImageMemoryRequirements getImageMemoryRequirements = nullptr;
  PFN_vkBindImageMemory bindImageMemory = nullptr;
  PFN_vkCreateImageView createImageView = nullptr;
  PFN_vkDestroyImageView destroyImageView = nullptr;
  PFN_vkAllocateMemory allocateMemory = nullptr;
  PFN_vkFreeMemory freeMemory = nullptr;
  PFN_vkCreateBuffer createBuffer = nullptr;
  PFN_vkDestroyBuffer destroyBuffer = nullptr;
  PFN_vkGetBufferMemoryRequirements getBufferMemoryRequirements = nullptr;
  PFN_vkBindBufferMemory bindBufferMemory = nullptr;
  PFN_vkMapMemory mapMemory = nullptr;
  PFN_vkUnmapMemory unmapMemory = nullptr;
  PFN_vkCreateSemaphore createSemaphore = nullptr;
  PFN_vkDestroySemaphore destroySemaphore = nullptr;
  PFN_vkGetSemaphoreCounterValue getSemaphoreCounterValue = nullptr;
  PFN_vkCmdPipelineBarrier cmdPipelineBarrier = nullptr;
  PFN_vkCmdClearColorImage cmdClearColorImage = nullptr;
  PFN_vkCmdCopyImageToBuffer cmdCopyImageToBuffer = nullptr;
  PFN_vkCmdCopyBufferToImage cmdCopyBufferToImage = nullptr;
  PFN_vkCmdBindPipeline cmdBindPipeline = nullptr;
  PFN_vkWaitSemaphores waitSemaphores = nullptr;
};

template <typename T>
bool loadDeviceProc(DeviceFns& f, VkDevice device, const char* name, T& output) {
  (void)f;
  output = reinterpret_cast<T>(gGetDeviceProcAddr(device, name));
  return output != nullptr;
}

struct Allocation {
  VkDeviceMemory memory = VK_NULL_HANDLE;
};

struct ImageResource {
  VkImage image = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
};

struct BufferResource {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
};

uint32_t findMemoryType(const VkPhysicalDeviceMemoryProperties& properties,
                        uint32_t bits, VkMemoryPropertyFlags required) {
  for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
    if ((bits & (1u << i)) &&
        (properties.memoryTypes[i].propertyFlags & required) == required)
      return i;
  }
  return std::numeric_limits<uint32_t>::max();
}

bool createImage(DeviceFns& f, VkDevice device,
                 const VkPhysicalDeviceMemoryProperties& memoryProperties,
                 uint32_t width, uint32_t height, VkFormat format,
                 ImageResource& resource) {
  VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = format;
  imageInfo.extent = {width, height, 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (f.createImage(device, &imageInfo, nullptr, &resource.image) != VK_SUCCESS)
    return false;

  VkMemoryRequirements requirements{};
  f.getImageMemoryRequirements(device, resource.image, &requirements);
  const uint32_t type = findMemoryType(memoryProperties, requirements.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (type == std::numeric_limits<uint32_t>::max())
    return false;
  VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocateInfo.allocationSize = requirements.size;
  allocateInfo.memoryTypeIndex = type;
  if (f.allocateMemory(device, &allocateInfo, nullptr, &resource.memory) != VK_SUCCESS)
    return false;
  if (f.bindImageMemory(device, resource.image, resource.memory, 0) != VK_SUCCESS)
    return false;

  VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  viewInfo.image = resource.image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.layerCount = 1;
  if (f.createImageView(device, &viewInfo, nullptr, &resource.view) != VK_SUCCESS)
    return false;
  resource.format = format;
  return true;
}

bool createBuffer(DeviceFns& f, VkDevice device,
                  const VkPhysicalDeviceMemoryProperties& memoryProperties,
                  VkDeviceSize size, BufferResource& resource) {
  VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bufferInfo.size = size;
  bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (f.createBuffer(device, &bufferInfo, nullptr, &resource.buffer) != VK_SUCCESS)
    return false;
  VkMemoryRequirements requirements{};
  f.getBufferMemoryRequirements(device, resource.buffer, &requirements);
  const uint32_t type = findMemoryType(
      memoryProperties, requirements.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (type == std::numeric_limits<uint32_t>::max())
    return false;
  VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocateInfo.allocationSize = requirements.size;
  allocateInfo.memoryTypeIndex = type;
  if (f.allocateMemory(device, &allocateInfo, nullptr, &resource.memory) != VK_SUCCESS)
    return false;
  if (f.bindBufferMemory(device, resource.buffer, resource.memory, 0) != VK_SUCCESS)
    return false;
  resource.size = size;
  return true;
}

void transition(DeviceFns& f, VkCommandBuffer commandBuffer, VkImage image,
                VkImageLayout oldLayout, VkImageLayout newLayout,
                VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.srcAccessMask = srcAccess;
  barrier.dstAccessMask = dstAccess;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.layerCount = 1;
  f.cmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1,
                       &barrier);
}

VkClearColorValue clearColor(float r, float g, float b, float a) {
  VkClearColorValue value{};
  value.float32[0] = r;
  value.float32[1] = g;
  value.float32[2] = b;
  value.float32[3] = a;
  return value;
}

void clearImage(DeviceFns& f, VkCommandBuffer commandBuffer, VkImage image,
                const VkClearColorValue& value) {
  VkImageSubresourceRange range{};
  range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  range.levelCount = 1;
  range.layerCount = 1;
  f.cmdClearColorImage(commandBuffer, image, VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
}

uint32_t formatId(VkFormat format) {
  switch (format) {
    case VK_FORMAT_R32G32B32A32_SFLOAT:
      return FGDS_VK_FORMAT_RGBA32_FLOAT;
    case VK_FORMAT_R32_SFLOAT:
      return FGDS_VK_FORMAT_R32_FLOAT;
    case VK_FORMAT_R32G32_SFLOAT:
      return FGDS_VK_FORMAT_RG32_FLOAT;
    case VK_FORMAT_R32_UINT:
      return FGDS_VK_FORMAT_R32_UINT;
    default:
      return 0;
  }
}

FgdsVkImage fgImage(const ImageResource& image) {
  FgdsVkImage result{};
  result.image = handleValue(image.image);
  result.view = handleValue(image.view);
  result.format = formatId(image.format);
  result.layout = VK_IMAGE_LAYOUT_GENERAL;
  return result;
}

FgdsVkFrameV2 makeFrame(VkDevice device, VkPhysicalDevice physicalDevice,
                        uint32_t queueFamily, uint32_t width, uint32_t height,
                        uint64_t frameId, const ImageResource& color,
                        const ImageResource& depth, const ImageResource& motion,
                        const ImageResource& objectId) {
  FgdsVkFrameV2 frame{};
  frame.structSize = sizeof(frame);
  frame.version = FGDS_VK_VERSION_0_2;
  frame.width = width;
  frame.height = height;
  frame.frameId = frameId;
  frame.timestampNs = frameId + 1;
  frame.worldToClip[0] = frame.worldToClip[5] = frame.worldToClip[10] = frame.worldToClip[15] = 1.0f;
  frame.device = handleValue(device);
  frame.physicalDevice = handleValue(physicalDevice);
  frame.queueFamily = queueFamily;
  frame.color = fgImage(color);
  frame.depth = fgImage(depth);
  frame.motionToOther = fgImage(motion);
  frame.objectId = fgImage(objectId);
  frame.resourceFlags = FGDS_VK_RESOURCE_CORE;
  return frame;
}

int run() {
  gLoader = LoadLibraryW(L"vulkan-1.dll");
  if (!gLoader) {
    logSkip("vulkan-1.dll is not available");
    return 77;
  }
  gGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
      GetProcAddress(gLoader, "vkGetInstanceProcAddr"));
  if (!gGetInstanceProcAddr) {
    logSkip("vkGetInstanceProcAddr is not available");
    return 77;
  }
  auto enumerateInstanceVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
      gGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
  uint32_t instanceVersion = VK_API_VERSION_1_0;
  if (enumerateInstanceVersion)
    enumerateInstanceVersion(&instanceVersion);
  if (VK_VERSION_MINOR(instanceVersion) < 2) {
    logSkip("Vulkan 1.2 is required for timeline semaphores");
    return 77;
  }

  auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(
      gGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
  if (!createInstance) {
    logSkip("vkCreateInstance is unavailable");
    return 77;
  }
  VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  appInfo.pApplicationName = "FreeFrameGen Vulkan V3 smoke";
  appInfo.applicationVersion = 1;
  appInfo.pEngineName = "FreeFrameGen";
  appInfo.engineVersion = 1;
  appInfo.apiVersion = std::min(instanceVersion, VK_API_VERSION_1_3);
  VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  instanceInfo.pApplicationInfo = &appInfo;
  VkInstance instance = VK_NULL_HANDLE;
  VkResult result = createInstance(&instanceInfo, nullptr, &instance);
  if (result != VK_SUCCESS) {
    logSkip("vkCreateInstance failed");
    return 77;
  }

  auto enumeratePhysicalDevices = instanceProc<PFN_vkEnumeratePhysicalDevices>(
      instance, "vkEnumeratePhysicalDevices");
  auto getPhysicalDeviceQueueFamilyProperties =
      instanceProc<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
          instance, "vkGetPhysicalDeviceQueueFamilyProperties");
  auto getPhysicalDeviceFeatures2 = instanceProc<PFN_vkGetPhysicalDeviceFeatures2>(
      instance, "vkGetPhysicalDeviceFeatures2");
  auto getPhysicalDeviceMemoryProperties =
      instanceProc<PFN_vkGetPhysicalDeviceMemoryProperties>(
          instance, "vkGetPhysicalDeviceMemoryProperties");
  if (!enumeratePhysicalDevices || !getPhysicalDeviceQueueFamilyProperties ||
      !getPhysicalDeviceFeatures2 || !getPhysicalDeviceMemoryProperties) {
    logSkip("required physical-device functions are unavailable");
    auto destroyInstance = instanceProc<PFN_vkDestroyInstance>(instance, "vkDestroyInstance");
    destroyInstance(instance, nullptr);
    return 77;
  }
  uint32_t physicalCount = 0;
  if (enumeratePhysicalDevices(instance, &physicalCount, nullptr) != VK_SUCCESS || !physicalCount) {
    logSkip("no Vulkan physical device");
    auto destroyInstance = instanceProc<PFN_vkDestroyInstance>(instance, "vkDestroyInstance");
    destroyInstance(instance, nullptr);
    return 77;
  }
  std::vector<VkPhysicalDevice> physicalDevices(physicalCount);
  enumeratePhysicalDevices(instance, &physicalCount, physicalDevices.data());

  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  uint32_t queueFamily = 0;
  VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeatures{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
  VkPhysicalDeviceSynchronization2Features synchronization2Features{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
  for (VkPhysicalDevice candidate : physicalDevices) {
    synchronization2Features.pNext = nullptr;
    timelineFeatures.pNext = &synchronization2Features;
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &timelineFeatures;
    getPhysicalDeviceFeatures2(candidate, &features);
    if (!timelineFeatures.timelineSemaphore)
      continue;
    uint32_t count = 0;
    getPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    getPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
    for (uint32_t i = 0; i < count; ++i) {
      if (families[i].queueCount && (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
        physicalDevice = candidate;
        queueFamily = i;
        break;
      }
    }
    if (physicalDevice)
      break;
  }
  if (!physicalDevice) {
    logSkip("no compute queue with timeline semaphore support");
    auto destroyInstance = instanceProc<PFN_vkDestroyInstance>(instance, "vkDestroyInstance");
    destroyInstance(instance, nullptr);
    return 77;
  }

  timelineFeatures.pNext = &synchronization2Features;
  VkPhysicalDeviceFeatures2 enabledFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  enabledFeatures.pNext = &timelineFeatures;
  getPhysicalDeviceFeatures2(physicalDevice, &enabledFeatures);
  // The query above filled supported bits.  We only enable the extension/core
  // feature bits used by this test and clear all unrelated feature bits.
  enabledFeatures.features = {};
  timelineFeatures.timelineSemaphore = VK_TRUE;
  if (synchronization2Features.synchronization2)
    synchronization2Features.synchronization2 = VK_TRUE;
  else
    synchronization2Features.synchronization2 = VK_FALSE;
  gUseSynchronization2 = synchronization2Features.synchronization2 == VK_TRUE;
  const float priority = 1.0f;
  VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  queueInfo.queueFamilyIndex = queueFamily;
  queueInfo.queueCount = 1;
  queueInfo.pQueuePriorities = &priority;
  VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  deviceInfo.pNext = &enabledFeatures;
  deviceInfo.queueCreateInfoCount = 1;
  deviceInfo.pQueueCreateInfos = &queueInfo;
  auto createDevice = instanceProc<PFN_vkCreateDevice>(instance, "vkCreateDevice");
  VkDevice device = VK_NULL_HANDLE;
  if (!createDevice || createDevice(physicalDevice, &deviceInfo, nullptr, &device) != VK_SUCCESS) {
    logSkip("vkCreateDevice failed");
    auto destroyInstance = instanceProc<PFN_vkDestroyInstance>(instance, "vkDestroyInstance");
    destroyInstance(instance, nullptr);
    return 77;
  }

  gGetDeviceProcAddr = instanceProc<PFN_vkGetDeviceProcAddr>(instance, "vkGetDeviceProcAddr");
  if (!gGetDeviceProcAddr) {
    logSkip("vkGetDeviceProcAddr is unavailable");
    auto destroyDevice = instanceProc<PFN_vkDestroyDevice>(instance, "vkDestroyDevice");
    destroyDevice(device, nullptr);
    auto destroyInstance = instanceProc<PFN_vkDestroyInstance>(instance, "vkDestroyInstance");
    destroyInstance(instance, nullptr);
    return 77;
  }
  DeviceFns f{};
  const bool loaded =
      loadDeviceProc(f, device, "vkDestroyDevice", f.destroyDevice) &&
      loadDeviceProc(f, device, "vkGetDeviceQueue", f.getDeviceQueue) &&
      loadDeviceProc(f, device, "vkCreateCommandPool", f.createCommandPool) &&
      loadDeviceProc(f, device, "vkDestroyCommandPool", f.destroyCommandPool) &&
      loadDeviceProc(f, device, "vkAllocateCommandBuffers", f.allocateCommandBuffers) &&
      loadDeviceProc(f, device, "vkFreeCommandBuffers", f.freeCommandBuffers) &&
      loadDeviceProc(f, device, "vkBeginCommandBuffer", f.beginCommandBuffer) &&
      loadDeviceProc(f, device, "vkEndCommandBuffer", f.endCommandBuffer) &&
      loadDeviceProc(f, device, "vkQueueSubmit", f.queueSubmit) &&
      loadDeviceProc(f, device, "vkQueueWaitIdle", f.queueWaitIdle) &&
      loadDeviceProc(f, device, "vkDeviceWaitIdle", f.deviceWaitIdle) &&
      loadDeviceProc(f, device, "vkCreateImage", f.createImage) &&
      loadDeviceProc(f, device, "vkDestroyImage", f.destroyImage) &&
      loadDeviceProc(f, device, "vkGetImageMemoryRequirements", f.getImageMemoryRequirements) &&
      loadDeviceProc(f, device, "vkBindImageMemory", f.bindImageMemory) &&
      loadDeviceProc(f, device, "vkCreateImageView", f.createImageView) &&
      loadDeviceProc(f, device, "vkDestroyImageView", f.destroyImageView) &&
      loadDeviceProc(f, device, "vkAllocateMemory", f.allocateMemory) &&
      loadDeviceProc(f, device, "vkFreeMemory", f.freeMemory) &&
      loadDeviceProc(f, device, "vkCreateBuffer", f.createBuffer) &&
      loadDeviceProc(f, device, "vkDestroyBuffer", f.destroyBuffer) &&
      loadDeviceProc(f, device, "vkGetBufferMemoryRequirements", f.getBufferMemoryRequirements) &&
      loadDeviceProc(f, device, "vkBindBufferMemory", f.bindBufferMemory) &&
      loadDeviceProc(f, device, "vkMapMemory", f.mapMemory) &&
      loadDeviceProc(f, device, "vkUnmapMemory", f.unmapMemory) &&
      loadDeviceProc(f, device, "vkCreateSemaphore", f.createSemaphore) &&
      loadDeviceProc(f, device, "vkDestroySemaphore", f.destroySemaphore) &&
      loadDeviceProc(f, device, "vkGetSemaphoreCounterValue", f.getSemaphoreCounterValue) &&
      loadDeviceProc(f, device, "vkCmdPipelineBarrier", f.cmdPipelineBarrier) &&
      loadDeviceProc(f, device, "vkCmdClearColorImage", f.cmdClearColorImage) &&
      loadDeviceProc(f, device, "vkCmdCopyImageToBuffer", f.cmdCopyImageToBuffer);
  if (!loaded) {
    logSkip("required device functions are unavailable");
    f.destroyDevice(device, nullptr);
    auto destroyInstance = instanceProc<PFN_vkDestroyInstance>(instance, "vkDestroyInstance");
    destroyInstance(instance, nullptr);
    return 77;
  }

  VkQueue queue = VK_NULL_HANDLE;
  f.getDeviceQueue(device, queueFamily, 0, &queue);
  VkCommandPool commandPool = VK_NULL_HANDLE;
  VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = queueFamily;
  if (f.createCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
    std::printf("FAIL: vkCreateCommandPool\n");
    f.destroyDevice(device, nullptr);
    auto destroyInstance = instanceProc<PFN_vkDestroyInstance>(instance, "vkDestroyInstance");
    destroyInstance(instance, nullptr);
    return 2;
  }
  VkCommandBuffer commandBuffers[4]{};
  VkCommandBufferAllocateInfo commandAllocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  commandAllocate.commandPool = commandPool;
  commandAllocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  commandAllocate.commandBufferCount = 4;
  if (f.allocateCommandBuffers(device, &commandAllocate, commandBuffers) != VK_SUCCESS) {
    std::printf("FAIL: vkAllocateCommandBuffers\n");
    f.destroyCommandPool(device, commandPool, nullptr);
    f.destroyDevice(device, nullptr);
    auto destroyInstance = instanceProc<PFN_vkDestroyInstance>(instance, "vkDestroyInstance");
    destroyInstance(instance, nullptr);
    return 2;
  }

  VkPhysicalDeviceMemoryProperties memoryProperties{};
  getPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
  constexpr uint32_t width = 8;
  constexpr uint32_t height = 8;
  ImageResource color0{}, depth0{}, motion0{}, object0{};
  ImageResource color1{}, depth1{}, motion1{}, object1{};
  ImageResource output0{}, output1{};
  std::vector<ImageResource*> images = {&color0, &depth0, &motion0, &object0,
                                        &color1, &depth1, &motion1, &object1,
                                        &output0, &output1};
  const VkFormat imageFormats[] = {
      VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT,
      VK_FORMAT_R32_UINT, VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R32_SFLOAT,
      VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32_UINT, VK_FORMAT_R32G32B32A32_SFLOAT,
      VK_FORMAT_R32G32B32A32_SFLOAT};
  bool resourcesOkay = true;
  for (size_t i = 0; i < images.size(); ++i)
    resourcesOkay = resourcesOkay &&
                    createImage(f, device, memoryProperties, width, height, imageFormats[i],
                                *images[i]);

  const VkDeviceSize readbackSize = static_cast<VkDeviceSize>(width) * height * 4 * sizeof(float);
  BufferResource readback{};
  resourcesOkay = resourcesOkay && createBuffer(f, device, memoryProperties, readbackSize * 2, readback);
  if (!resourcesOkay) {
    std::printf("FAIL: image or readback allocation\n");
    f.deviceWaitIdle(device);
    if (readback.buffer) f.destroyBuffer(device, readback.buffer, nullptr);
    if (readback.memory) f.freeMemory(device, readback.memory, nullptr);
    for (ImageResource* image : images) {
      if (image->view) f.destroyImageView(device, image->view, nullptr);
      if (image->image) f.destroyImage(device, image->image, nullptr);
      if (image->memory) f.freeMemory(device, image->memory, nullptr);
    }
    f.freeCommandBuffers(device, commandPool, 4, commandBuffers);
    f.destroyCommandPool(device, commandPool, nullptr);
    f.destroyDevice(device, nullptr);
    auto destroyInstance = instanceProc<PFN_vkDestroyInstance>(instance, "vkDestroyInstance");
    destroyInstance(instance, nullptr);
    return 2;
  }

  auto begin = [&](VkCommandBuffer commandBuffer) {
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    return f.beginCommandBuffer(commandBuffer, &beginInfo) == VK_SUCCESS;
  };
  auto end = [&](VkCommandBuffer commandBuffer) {
    return f.endCommandBuffer(commandBuffer) == VK_SUCCESS;
  };
  // Initialize every image to GENERAL once.  The inputs then remain read-only
  // while both V3 slots execute, and each output has a known layout.
  bool commandOkay = begin(commandBuffers[0]);
  for (ImageResource* image : images) {
    transition(f, commandBuffers[0], image->image, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
  }
  clearImage(f, commandBuffers[0], color0.image, clearColor(1, 0, 0, 1));
  clearImage(f, commandBuffers[0], color1.image, clearColor(0, 1, 0, 1));
  clearImage(f, commandBuffers[0], depth0.image, clearColor(1, 0, 0, 0));
  clearImage(f, commandBuffers[0], depth1.image, clearColor(1, 0, 0, 0));
  clearImage(f, commandBuffers[0], motion0.image, clearColor(0, 0, 0, 0));
  clearImage(f, commandBuffers[0], motion1.image, clearColor(0, 0, 0, 0));
  clearImage(f, commandBuffers[0], object0.image, clearColor(1, 0, 0, 0));
  clearImage(f, commandBuffers[0], object1.image, clearColor(1, 0, 0, 0));
  for (ImageResource* image : images) {
    transition(f, commandBuffers[0], image->image, VK_IMAGE_LAYOUT_GENERAL,
               VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
               VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  }
  commandOkay = commandOkay && end(commandBuffers[0]);
  VkSubmitInfo initSubmit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  initSubmit.commandBufferCount = 1;
  initSubmit.pCommandBuffers = &commandBuffers[0];
  commandOkay = commandOkay &&
                f.queueSubmit(queue, 1, &initSubmit, VK_NULL_HANDLE) == VK_SUCCESS &&
                f.queueWaitIdle(queue) == VK_SUCCESS;

  VkSemaphore timeline = VK_NULL_HANDLE;
  VkSemaphoreTypeCreateInfo timelineType{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
  timelineType.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  timelineType.initialValue = 0;
  VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  semaphoreInfo.pNext = &timelineType;
  commandOkay = commandOkay &&
                f.createSemaphore(device, &semaphoreInfo, nullptr, &timeline) == VK_SUCCESS;
  if (!commandOkay) {
    std::printf("FAIL: initialization command failed\n");
    return 2;
  }

  FfgVkContextV3* context = nullptr;
  result = ffgVkCreateV3(physicalDevice, device, queueFamily, testGetDeviceProcAddr, 2, &context);
  if (result != VK_SUCCESS || !context) {
    std::printf("FAIL: ffgVkCreateV3 result=%d\n", static_cast<int>(result));
    return 2;
  }
  FgdsVkCapabilitiesV3 capabilities{};
  capabilities.structSize = sizeof(capabilities);
  result = ffgVkGetCapabilitiesV3(context, &capabilities);
  if (result != VK_SUCCESS || capabilities.slotCount != 2 ||
      !(capabilities.featureFlags & FGDS_VK_FEATURE_MULTI_FLIGHT)) {
    std::printf("FAIL: V3 capabilities result=%d slots=%u flags=%llu\n", static_cast<int>(result),
                capabilities.slotCount, static_cast<unsigned long long>(capabilities.featureFlags));
    return 2;
  }
  FgdsVkPairV2 pair{};
  pair.structSize = sizeof(pair);
  pair.version = FGDS_VK_VERSION_0_2;
  pair.alpha = 0.5f;
  pair.frames[0] = makeFrame(device, physicalDevice, queueFamily, width, height, 1,
                             color0, depth0, motion0, object0);
  pair.frames[1] = makeFrame(device, physicalDevice, queueFamily, width, height, 2,
                             color1, depth1, motion1, object1);
  FgdsVkImage output0View = fgImage(output0);
  FgdsVkImage output1View = fgImage(output1);

  if (!begin(commandBuffers[1])) {
    std::printf("FAIL: begin slot 0 command\n");
    return 2;
  }
  result = ffgVkRecordV3(context, commandBuffers[1], &pair, &output0View, 0, timeline, 1);
  if (result != VK_SUCCESS) {
    std::printf("FAIL: first slot 0 record result=%d\n", static_cast<int>(result));
    return 2;
  }
  VkResult busy = ffgVkRecordV3(context, commandBuffers[1], &pair, &output0View, 0, timeline, 1);
  if (busy != VK_NOT_READY) {
    std::printf("FAIL: expected VK_NOT_READY on busy slot, got %d\n", static_cast<int>(busy));
    return 2;
  }
  if (!end(commandBuffers[1]) || !begin(commandBuffers[2])) {
    std::printf("FAIL: command setup for slot 1\n");
    return 2;
  }
  result = ffgVkRecordV3(context, commandBuffers[2], &pair, &output1View, 1, timeline, 2);
  if (result != VK_SUCCESS || !end(commandBuffers[2])) {
    std::printf("FAIL: slot 1 record result=%d\n", static_cast<int>(result));
    return 2;
  }

  uint64_t signalValues[] = {2};
  VkCommandBuffer recordedBuffers[] = {commandBuffers[1], commandBuffers[2]};
  VkTimelineSemaphoreSubmitInfo timelineSubmit{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
  timelineSubmit.signalSemaphoreValueCount = 1;
  timelineSubmit.pSignalSemaphoreValues = signalValues;
  VkSemaphore signalSemaphores[] = {timeline};
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.pNext = &timelineSubmit;
  submit.commandBufferCount = 2;
  submit.pCommandBuffers = recordedBuffers;
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = signalSemaphores;
  result = f.queueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
  if (result != VK_SUCCESS || f.queueWaitIdle(queue) != VK_SUCCESS) {
    std::printf("FAIL: V3 queue submit result=%d\n", static_cast<int>(result));
    return 2;
  }
  uint64_t completed = 0;
  f.getSemaphoreCounterValue(device, timeline, &completed);
  if (completed < 2) {
    std::printf("FAIL: timeline semaphore did not reach 2 (got %llu)\n",
                static_cast<unsigned long long>(completed));
    return 2;
  }

  // The slot is reusable once its token is complete.  Record and submit a
  // third command with slot 0 to prove that the retirement guard is not sticky.
  if (!begin(commandBuffers[3])) {
    std::printf("FAIL: begin slot reuse command\n");
    return 2;
  }
  result = ffgVkRecordV3(context, commandBuffers[3], &pair, &output0View, 0, timeline, 3);
  if (result != VK_SUCCESS || !end(commandBuffers[3])) {
    std::printf("FAIL: slot reuse record result=%d\n", static_cast<int>(result));
    return 2;
  }
  uint64_t reuseValue = 3;
  VkTimelineSemaphoreSubmitInfo reuseTimeline{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
  reuseTimeline.signalSemaphoreValueCount = 1;
  reuseTimeline.pSignalSemaphoreValues = &reuseValue;
  VkSubmitInfo reuseSubmit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  reuseSubmit.pNext = &reuseTimeline;
  reuseSubmit.commandBufferCount = 1;
  reuseSubmit.pCommandBuffers = &commandBuffers[3];
  reuseSubmit.signalSemaphoreCount = 1;
  reuseSubmit.pSignalSemaphores = &timeline;
  if (f.queueSubmit(queue, 1, &reuseSubmit, VK_NULL_HANDLE) != VK_SUCCESS ||
      f.queueWaitIdle(queue) != VK_SUCCESS) {
    std::printf("FAIL: slot reuse submit\n");
    return 2;
  }

  if (!begin(commandBuffers[0])) {
    std::printf("FAIL: begin readback command\n");
    return 2;
  }
  for (ImageResource* image : {&output0, &output1}) {
    transition(f, commandBuffers[0], image->image, VK_IMAGE_LAYOUT_GENERAL,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
               VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT);
  }
  VkBufferImageCopy copies[2]{};
  copies[0].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copies[0].imageSubresource.layerCount = 1;
  copies[0].imageExtent = {width, height, 1};
  copies[1] = copies[0];
  copies[1].bufferOffset = readbackSize;
  f.cmdCopyImageToBuffer(commandBuffers[0], output0.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         readback.buffer, 1, &copies[0]);
  f.cmdCopyImageToBuffer(commandBuffers[0], output1.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         readback.buffer, 1, &copies[1]);
  if (!end(commandBuffers[0])) {
    std::printf("FAIL: end readback command\n");
    return 2;
  }
  VkSubmitInfo readbackSubmit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  readbackSubmit.commandBufferCount = 1;
  readbackSubmit.pCommandBuffers = &commandBuffers[0];
  if (f.queueSubmit(queue, 1, &readbackSubmit, VK_NULL_HANDLE) != VK_SUCCESS ||
      f.queueWaitIdle(queue) != VK_SUCCESS) {
    std::printf("FAIL: readback submit\n");
    return 2;
  }
  void* mapped = nullptr;
  if (f.mapMemory(device, readback.memory, 0, readback.size, 0, &mapped) != VK_SUCCESS) {
    std::printf("FAIL: map readback\n");
    return 2;
  }
  const float* pixels = static_cast<const float*>(mapped);
  bool outputOkay = true;
  for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
    outputOkay = outputOkay && std::fabs(pixels[i * 4 + 0] - 0.5f) < 0.06f &&
                 std::fabs(pixels[i * 4 + 1] - 0.5f) < 0.06f &&
                 std::fabs(pixels[i * 4 + 2]) < 0.06f &&
                 std::fabs(pixels[i * 4 + 3] - 1.0f) < 0.06f;
  }
  f.unmapMemory(device, readback.memory);
  ffgVkDestroyV3(context);
  f.deviceWaitIdle(device);
  f.destroySemaphore(device, timeline, nullptr);
  f.destroyBuffer(device, readback.buffer, nullptr);
  f.freeMemory(device, readback.memory, nullptr);
  for (ImageResource* image : images) {
    f.destroyImageView(device, image->view, nullptr);
    f.destroyImage(device, image->image, nullptr);
    f.freeMemory(device, image->memory, nullptr);
  }
  f.freeCommandBuffers(device, commandPool, 4, commandBuffers);
  f.destroyCommandPool(device, commandPool, nullptr);
  f.destroyDevice(device, nullptr);
  auto destroyInstance = instanceProc<PFN_vkDestroyInstance>(instance, "vkDestroyInstance");
  destroyInstance(instance, nullptr);
  if (!outputOkay) {
    std::printf("FAIL: shader output mismatch\n");
    return 2;
  }
  std::printf("PASS: Vulkan V3 slots, timeline retirement, reuse and output readback\n");
  return 0;
}

} // namespace

int main() {
  return run();
}
