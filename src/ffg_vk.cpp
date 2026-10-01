#include "ffg/ffg_vk.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <memory>
#include <vector>

#include "ffg_interpolate.h"

struct FfgVkContext {
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  uint32_t queueFamily = 0;

  PFN_vkCreateDescriptorSetLayout createDescriptorSetLayout = nullptr;
  PFN_vkDestroyDescriptorSetLayout destroyDescriptorSetLayout = nullptr;
  PFN_vkCreateDescriptorPool createDescriptorPool = nullptr;
  PFN_vkDestroyDescriptorPool destroyDescriptorPool = nullptr;
  PFN_vkAllocateDescriptorSets allocateDescriptorSets = nullptr;
  PFN_vkUpdateDescriptorSets updateDescriptorSets = nullptr;
  PFN_vkCreatePipelineLayout createPipelineLayout = nullptr;
  PFN_vkDestroyPipelineLayout destroyPipelineLayout = nullptr;
  PFN_vkCreateShaderModule createShaderModule = nullptr;
  PFN_vkDestroyShaderModule destroyShaderModule = nullptr;
  PFN_vkCreateComputePipelines createComputePipelines = nullptr;
  PFN_vkDestroyPipeline destroyPipeline = nullptr;
  PFN_vkCmdBindPipeline cmdBindPipeline = nullptr;
  PFN_vkCmdBindDescriptorSets cmdBindDescriptorSets = nullptr;
  PFN_vkCmdPushConstants cmdPushConstants = nullptr;
  PFN_vkCmdDispatch cmdDispatch = nullptr;
  PFN_vkCmdPipelineBarrier2 cmdPipelineBarrier2 = nullptr;
  PFN_vkCmdPipelineBarrier cmdPipelineBarrier = nullptr;

  VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

// V3 keeps the immutable pipeline in a normal v0.2 context, but gives every
// in-flight slot its own descriptor pool/set.  Updating one set therefore
// cannot race a command buffer that is still consuming another slot's set.
struct FfgVkContextV3 {
  FfgVkContext* core = nullptr;
  PFN_vkGetSemaphoreCounterValue getSemaphoreCounterValue = nullptr;

  struct Slot {
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkSemaphore retireSemaphore = VK_NULL_HANDLE;
    uint64_t retireValue = 0;
  };

  std::vector<Slot> slots;
};

namespace {

template <typename T>
T load(PFN_vkGetDeviceProcAddr proc, VkDevice device, const char* name) {
  return reinterpret_cast<T>(proc(device, name));
}

bool imageDefined(const FgdsVkImage& image);

bool validFrame(const FgdsVkFrame& frame, VkDevice device, VkPhysicalDevice physicalDevice,
                uint32_t queueFamily, uint32_t width, uint32_t height) {
  if (frame.structSize != sizeof(FgdsVkFrame) || frame.version != FGDS_VK_VERSION ||
      frame.reserved || frame.reserved2 || frame.width != width || frame.height != height ||
      frame.device != static_cast<uint64_t>(reinterpret_cast<uintptr_t>(device)) ||
      frame.physicalDevice != static_cast<uint64_t>(reinterpret_cast<uintptr_t>(physicalDevice)) ||
      frame.queueFamily != queueFamily || (frame.flags & ~FGDS_VK_CAMERA_CUT) ||
      imageDefined(frame.hudMask) || imageDefined(frame.transparencyMask))
    return false;
  for (float value : frame.worldToClip)
    if (!std::isfinite(value))
      return false;
  // The current kernel has no explicit camera-jitter reconstruction path.
  if (frame.jitterPixels[0] != 0.0f || frame.jitterPixels[1] != 0.0f)
    return false;
  return true;
}

VkImageView viewOf(const FgdsVkImage& image) {
#if VK_USE_64_BIT_PTR_DEFINES
  return reinterpret_cast<VkImageView>(static_cast<uintptr_t>(image.view));
#else
  // Vulkan non-dispatchable handles are uint64_t on Win32.  A pointer-style
  // reinterpret_cast is ill-formed there even though the FGDS wire value is
  // still the same 64-bit handle.  Keep the conversion explicit for both
  // Vulkan handle representations.
  return static_cast<VkImageView>(image.view);
#endif
}

bool imageDefined(const FgdsVkImage& image) {
  return image.image != 0 || image.view != 0 || image.format != 0 || image.layout != 0;
}

bool validV2Frame(const FgdsVkFrameV2& frame, VkDevice device,
                 VkPhysicalDevice physicalDevice, uint32_t queueFamily,
                 uint32_t width, uint32_t height) {
  if (frame.structSize < sizeof(FgdsVkFrameV2) ||
      frame.version != FGDS_VK_VERSION_0_2 || frame.reserved || frame.reserved2 ||
      frame.reserved3 || frame.width != width || frame.height != height ||
      frame.device != static_cast<uint64_t>(reinterpret_cast<uintptr_t>(device)) ||
      frame.physicalDevice != static_cast<uint64_t>(reinterpret_cast<uintptr_t>(physicalDevice)) ||
      frame.queueFamily != queueFamily || (frame.flags & ~FGDS_VK_FRAME_V2_FLAGS))
    return false;
  for (float value : frame.worldToClip)
    if (!std::isfinite(value))
      return false;
  if (frame.jitterPixels[0] != 0.0f || frame.jitterPixels[1] != 0.0f)
    return false;
  if (frame.resourceFlags & ~(FGDS_VK_RESOURCE_CORE | FGDS_VK_RESOURCE_HUD_MASK |
                              FGDS_VK_RESOURCE_TRANSPARENCY_MASK))
    return false;
  if ((frame.resourceFlags & FGDS_VK_RESOURCE_CORE) != FGDS_VK_RESOURCE_CORE)
    return false;
  const bool hudResource = (frame.resourceFlags & FGDS_VK_RESOURCE_HUD_MASK) != 0;
  const bool transparencyResource =
      (frame.resourceFlags & FGDS_VK_RESOURCE_TRANSPARENCY_MASK) != 0;
  if (hudResource != imageDefined(frame.hudMask) ||
      transparencyResource != imageDefined(frame.transparencyMask))
    return false;
  if (((frame.flags & FGDS_VK_FRAME_HUD_MASK) != 0) != hudResource ||
      ((frame.flags & FGDS_VK_FRAME_TRANSPARENCY_MASK) != 0) != transparencyResource)
    return false;
  if (!frame.color.image || !frame.color.view || !frame.depth.image || !frame.depth.view ||
      !frame.motionToOther.image || !frame.motionToOther.view || !frame.objectId.image ||
      !frame.objectId.view)
    return false;
  return true;
}

void copyV2Frame(const FgdsVkFrameV2& from, FgdsVkFrame& to) {
  to = {};
  to.structSize = sizeof(FgdsVkFrame);
  to.version = FGDS_VK_VERSION;
  to.width = from.width;
  to.height = from.height;
  to.frameId = from.frameId;
  to.timestampNs = from.timestampNs;
  std::copy(std::begin(from.worldToClip), std::end(from.worldToClip), std::begin(to.worldToClip));
  to.jitterPixels[0] = from.jitterPixels[0];
  to.jitterPixels[1] = from.jitterPixels[1];
  to.flags = from.flags & FGDS_VK_CAMERA_CUT;
  to.device = from.device;
  to.physicalDevice = from.physicalDevice;
  to.queueFamily = from.queueFamily;
  to.color = from.color;
  to.depth = from.depth;
  to.motionToOther = from.motionToOther;
  to.objectId = from.objectId;
  to.hudMask = from.hudMask;
  to.transparencyMask = from.transparencyMask;
  to.ready = from.ready;
}

} // namespace

extern "C" FFG_VK_API VkResult __cdecl ffgVkCreate(
    VkPhysicalDevice physicalDevice, VkDevice device, uint32_t queueFamily,
    PFN_vkGetDeviceProcAddr getProc, FfgVkContext** output) {
  if (!output || !physicalDevice || !device || !getProc)
    return VK_ERROR_INITIALIZATION_FAILED;
  *output = nullptr;

  auto context = std::make_unique<FfgVkContext>();
  context->physicalDevice = physicalDevice;
  context->device = device;
  context->queueFamily = queueFamily;

#define LOAD(member, name) context->member = load<PFN_##name>(getProc, device, #name)
  LOAD(createDescriptorSetLayout, vkCreateDescriptorSetLayout);
  LOAD(destroyDescriptorSetLayout, vkDestroyDescriptorSetLayout);
  LOAD(createDescriptorPool, vkCreateDescriptorPool);
  LOAD(destroyDescriptorPool, vkDestroyDescriptorPool);
  LOAD(allocateDescriptorSets, vkAllocateDescriptorSets);
  LOAD(updateDescriptorSets, vkUpdateDescriptorSets);
  LOAD(createPipelineLayout, vkCreatePipelineLayout);
  LOAD(destroyPipelineLayout, vkDestroyPipelineLayout);
  LOAD(createShaderModule, vkCreateShaderModule);
  LOAD(destroyShaderModule, vkDestroyShaderModule);
  LOAD(createComputePipelines, vkCreateComputePipelines);
  LOAD(destroyPipeline, vkDestroyPipeline);
  LOAD(cmdBindPipeline, vkCmdBindPipeline);
  LOAD(cmdBindDescriptorSets, vkCmdBindDescriptorSets);
  LOAD(cmdPushConstants, vkCmdPushConstants);
  LOAD(cmdDispatch, vkCmdDispatch);
  LOAD(cmdPipelineBarrier2, vkCmdPipelineBarrier2);
  LOAD(cmdPipelineBarrier, vkCmdPipelineBarrier);
#undef LOAD

  if (!context->createDescriptorSetLayout || !context->destroyDescriptorSetLayout ||
      !context->createDescriptorPool || !context->destroyDescriptorPool ||
      !context->allocateDescriptorSets || !context->updateDescriptorSets ||
      !context->createPipelineLayout || !context->destroyPipelineLayout ||
      !context->createShaderModule || !context->destroyShaderModule ||
      !context->createComputePipelines || !context->destroyPipeline ||
      !context->cmdBindPipeline || !context->cmdBindDescriptorSets ||
      !context->cmdPushConstants || !context->cmdDispatch)
    return VK_ERROR_EXTENSION_NOT_PRESENT;

  VkDescriptorSetLayoutBinding bindings[9]{};
  for (uint32_t i = 0; i < 9; ++i) {
    bindings[i].binding = i;
    bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[i].descriptorCount = 1;
    bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  }
  VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layoutInfo.bindingCount = 9;
  layoutInfo.pBindings = bindings;
  VkResult result = context->createDescriptorSetLayout(device, &layoutInfo, nullptr,
                                                        &context->setLayout);
  if (result != VK_SUCCESS)
    return result;

  VkPushConstantRange pushRange{};
  pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  pushRange.size = 16;
  VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &context->setLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushRange;
  result = context->createPipelineLayout(device, &pipelineLayoutInfo, nullptr,
                                         &context->pipelineLayout);
  if (result != VK_SUCCESS) {
    context->destroyDescriptorSetLayout(device, context->setLayout, nullptr);
    return result;
  }

  VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  shaderInfo.codeSize = sizeof(ffg_interpolate);
  shaderInfo.pCode = ffg_interpolate;
  VkShaderModule shader = VK_NULL_HANDLE;
  result = context->createShaderModule(device, &shaderInfo, nullptr, &shader);
  if (result != VK_SUCCESS) {
    context->destroyPipelineLayout(device, context->pipelineLayout, nullptr);
    context->destroyDescriptorSetLayout(device, context->setLayout, nullptr);
    return result;
  }
  VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
  stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  stage.module = shader;
  stage.pName = "main";
  VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pipelineInfo.stage = stage;
  pipelineInfo.layout = context->pipelineLayout;
  result = context->createComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                           &context->pipeline);
  context->destroyShaderModule(device, shader, nullptr);
  if (result != VK_SUCCESS) {
    context->destroyPipelineLayout(device, context->pipelineLayout, nullptr);
    context->destroyDescriptorSetLayout(device, context->setLayout, nullptr);
    return result;
  }

  VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 9};
  VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  poolInfo.maxSets = 1;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &poolSize;
  result = context->createDescriptorPool(device, &poolInfo, nullptr, &context->descriptorPool);
  if (result != VK_SUCCESS) {
    context->destroyPipeline(device, context->pipeline, nullptr);
    context->destroyPipelineLayout(device, context->pipelineLayout, nullptr);
    context->destroyDescriptorSetLayout(device, context->setLayout, nullptr);
    return result;
  }
  VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocInfo.descriptorPool = context->descriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &context->setLayout;
  result = context->allocateDescriptorSets(device, &allocInfo, &context->descriptorSet);
  if (result != VK_SUCCESS) {
    context->destroyDescriptorPool(device, context->descriptorPool, nullptr);
    context->destroyPipeline(device, context->pipeline, nullptr);
    context->destroyPipelineLayout(device, context->pipelineLayout, nullptr);
    context->destroyDescriptorSetLayout(device, context->setLayout, nullptr);
    return result;
  }

  *output = context.release();
  return VK_SUCCESS;
}

extern "C" FFG_VK_API void __cdecl ffgVkDestroy(FfgVkContext* context) {
  if (!context)
    return;
  if (context->device) {
    if (context->descriptorPool && context->destroyDescriptorPool)
      context->destroyDescriptorPool(context->device, context->descriptorPool, nullptr);
    if (context->pipeline && context->destroyPipeline)
      context->destroyPipeline(context->device, context->pipeline, nullptr);
    if (context->pipelineLayout && context->destroyPipelineLayout)
      context->destroyPipelineLayout(context->device, context->pipelineLayout, nullptr);
    if (context->setLayout && context->destroyDescriptorSetLayout)
      context->destroyDescriptorSetLayout(context->device, context->setLayout, nullptr);
  }
  delete context;
}

extern "C" FFG_VK_API VkResult __cdecl ffgVkGetCapabilities(
    FfgVkContext* context, FgdsVkCapabilities* capabilities) {
  if (!context || !capabilities)
    return VK_ERROR_INITIALIZATION_FAILED;
  if (capabilities->structSize < sizeof(FgdsVkCapabilities))
    return VK_ERROR_VALIDATION_FAILED_EXT;
  capabilities->version = FGDS_VK_VERSION_0_2;
  capabilities->nativeApiVersion = FGDS_VK_VERSION_0_2;
  capabilities->backend = FGDS_VK_BACKEND_VULKAN;
  capabilities->maxWidth = 16384;
  capabilities->maxHeight = 16384;
  capabilities->requiredResources = FGDS_VK_RESOURCE_CORE;
  capabilities->optionalResources = 0;
  capabilities->featureFlags = FGDS_VK_FEATURE_NATIVE_V2 |
                                FGDS_VK_FEATURE_BIDIRECTIONAL_MOTION |
                                FGDS_VK_FEATURE_DEPTH_OCCLUSION |
                                FGDS_VK_FEATURE_OBJECT_ID_REJECTION |
                                FGDS_VK_FEATURE_CAMERA_CUT;
  capabilities->colorFormat = FGDS_VK_FORMAT_RGBA32_FLOAT;
  capabilities->depthFormat = FGDS_VK_FORMAT_R32_FLOAT;
  capabilities->motionFormat = FGDS_VK_FORMAT_RG32_FLOAT;
  capabilities->objectIdFormat = FGDS_VK_FORMAT_R32_UINT;
  for (auto& value : capabilities->reserved)
    value = 0;
  return VK_SUCCESS;
}

extern "C" FFG_VK_API VkResult __cdecl ffgVkRecord(
    FfgVkContext* context, VkCommandBuffer commandBuffer, const FgdsVkPair* pair,
    const FgdsVkImage* output) {
  if (!context || !commandBuffer || !pair || !output)
    return VK_ERROR_INITIALIZATION_FAILED;
  if (pair->structSize != sizeof(FgdsVkPair) || pair->version != FGDS_VK_VERSION ||
      pair->reserved || !std::isfinite(pair->alpha) || pair->alpha < 0.0f || pair->alpha > 1.0f)
    return VK_ERROR_VALIDATION_FAILED_EXT;
  const auto& a = pair->frames[0];
  const auto& b = pair->frames[1];
  if (!a.width || !a.height || a.width > 16384 || a.height > 16384 ||
      a.width != b.width || a.height != b.height || a.frameId >= b.frameId ||
      a.timestampNs >= b.timestampNs || !validFrame(a, context->device, context->physicalDevice,
                                                      context->queueFamily, a.width, a.height) ||
      !validFrame(b, context->device, context->physicalDevice, context->queueFamily, a.width,
                  a.height))
    return VK_ERROR_VALIDATION_FAILED_EXT;

  const FgdsVkImage* images[9] = {&a.color, &a.depth, &a.motionToOther, &a.objectId,
                                   &b.color, &b.depth, &b.motionToOther, &b.objectId, output};
  const uint32_t formats[9] = {FGDS_VK_FORMAT_RGBA32_FLOAT, FGDS_VK_FORMAT_R32_FLOAT,
                               FGDS_VK_FORMAT_RG32_FLOAT, FGDS_VK_FORMAT_R32_UINT,
                               FGDS_VK_FORMAT_RGBA32_FLOAT, FGDS_VK_FORMAT_R32_FLOAT,
                               FGDS_VK_FORMAT_RG32_FLOAT, FGDS_VK_FORMAT_R32_UINT,
                               FGDS_VK_FORMAT_RGBA32_FLOAT};
  VkDescriptorImageInfo infos[9]{};
  for (uint32_t i = 0; i < 9; ++i) {
    if (!images[i]->image || !images[i]->view || images[i]->layout != VK_IMAGE_LAYOUT_GENERAL ||
        images[i]->format != formats[i])
      return VK_ERROR_VALIDATION_FAILED_EXT;
    infos[i].imageView = viewOf(*images[i]);
    infos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  }
  for (uint32_t i = 0; i < 8; ++i) {
    if (images[i]->image == output->image || images[i]->view == output->view)
      return VK_ERROR_VALIDATION_FAILED_EXT;
  }
  VkWriteDescriptorSet writes[9]{};
  for (uint32_t i = 0; i < 9; ++i) {
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = context->descriptorSet;
    writes[i].dstBinding = i;
    writes[i].descriptorCount = 1;
    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[i].pImageInfo = &infos[i];
  }
  context->updateDescriptorSets(context->device, 9, writes, 0, nullptr);
  context->cmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, context->pipeline);
  context->cmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                 context->pipelineLayout, 0, 1, &context->descriptorSet, 0, nullptr);
  struct Constants { uint32_t width, height; float alpha; uint32_t reset; } constants{
      a.width, a.height, pair->alpha, (a.flags | b.flags) & FGDS_VK_CAMERA_CUT};
  context->cmdPushConstants(commandBuffer, context->pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                            0, sizeof(constants), &constants);
  context->cmdDispatch(commandBuffer, (a.width + 7) / 8, (a.height + 7) / 8, 1);
  if (context->cmdPipelineBarrier2) {
    VkMemoryBarrier2 memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    memory.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    memory.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    memory.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    memory.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &memory;
    context->cmdPipelineBarrier2(commandBuffer, &dependency);
  } else if (context->cmdPipelineBarrier) {
    VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    memory.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memory.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    context->cmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &memory, 0, nullptr,
                                0, nullptr);
  }
  return VK_SUCCESS;
}

extern "C" FFG_VK_API VkResult __cdecl ffgVkRecordV2(
    FfgVkContext* context, VkCommandBuffer commandBuffer, const FgdsVkPairV2* pair,
    const FgdsVkImage* output) {
  if (!context || !commandBuffer || !pair || !output)
    return VK_ERROR_INITIALIZATION_FAILED;
  if (pair->structSize < sizeof(FgdsVkPairV2) || pair->version != FGDS_VK_VERSION_0_2 ||
      pair->reserved || !std::isfinite(pair->alpha) || pair->alpha < 0.0f || pair->alpha > 1.0f)
    return VK_ERROR_VALIDATION_FAILED_EXT;
  const auto& a = pair->frames[0];
  const auto& b = pair->frames[1];
  if (!a.width || !a.height || a.width > 16384 || a.height > 16384 ||
      a.width != b.width || a.height != b.height || a.frameId >= b.frameId ||
      a.timestampNs >= b.timestampNs ||
      !validV2Frame(a, context->device, context->physicalDevice, context->queueFamily, a.width,
                    a.height) ||
      !validV2Frame(b, context->device, context->physicalDevice, context->queueFamily, a.width,
                    a.height))
    return VK_ERROR_VALIDATION_FAILED_EXT;
  if ((a.resourceFlags | b.resourceFlags) &
      (FGDS_VK_RESOURCE_HUD_MASK | FGDS_VK_RESOURCE_TRANSPARENCY_MASK))
    return VK_ERROR_FEATURE_NOT_PRESENT;
  FgdsVkPair legacy{};
  legacy.structSize = sizeof(FgdsVkPair);
  legacy.version = FGDS_VK_VERSION;
  legacy.alpha = pair->alpha;
  copyV2Frame(a, legacy.frames[0]);
  copyV2Frame(b, legacy.frames[1]);
  return ffgVkRecord(context, commandBuffer, &legacy, output);
}

extern "C" FFG_VK_API VkResult __cdecl ffgVkCreateV3(
    VkPhysicalDevice physicalDevice, VkDevice device, uint32_t queueFamily,
    PFN_vkGetDeviceProcAddr getProc, uint32_t slotCount, FfgVkContextV3** output) {
  if (!output || !physicalDevice || !device || !getProc || slotCount == 0 ||
      slotCount > FGDS_VK_V3_MAX_SLOTS)
    return VK_ERROR_INITIALIZATION_FAILED;
  *output = nullptr;

  // A timeline counter is the only synchronization primitive that remains
  // queryable after an application resets/reuses its submit objects.  Require
  // it at creation so a V3 context never silently loses its retirement guard.
  auto getCounter = load<PFN_vkGetSemaphoreCounterValue>(
      getProc, device, "vkGetSemaphoreCounterValue");
  if (!getCounter)
    getCounter = reinterpret_cast<PFN_vkGetSemaphoreCounterValue>(
        getProc(device, "vkGetSemaphoreCounterValueKHR"));
  if (!getCounter)
    return VK_ERROR_EXTENSION_NOT_PRESENT;

  FfgVkContext* core = nullptr;
  VkResult result = ffgVkCreate(physicalDevice, device, queueFamily, getProc, &core);
  if (result != VK_SUCCESS)
    return result;

  auto context = std::make_unique<FfgVkContextV3>();
  context->core = core;
  context->getSemaphoreCounterValue = getCounter;
  context->slots.resize(slotCount);

  VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 9};
  VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  poolInfo.maxSets = 1;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &poolSize;
  VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &core->setLayout;

  for (auto& slot : context->slots) {
    result = core->createDescriptorPool(device, &poolInfo, nullptr, &slot.descriptorPool);
    if (result != VK_SUCCESS)
      break;
    allocInfo.descriptorPool = slot.descriptorPool;
    result = core->allocateDescriptorSets(device, &allocInfo, &slot.descriptorSet);
    if (result != VK_SUCCESS)
      break;
  }
  if (result != VK_SUCCESS) {
    for (auto& slot : context->slots) {
      if (slot.descriptorPool)
        core->destroyDescriptorPool(device, slot.descriptorPool, nullptr);
    }
    ffgVkDestroy(core);
    return result;
  }

  *output = context.release();
  return VK_SUCCESS;
}

extern "C" FFG_VK_API void __cdecl ffgVkDestroyV3(FfgVkContextV3* context) {
  if (!context)
    return;
  if (context->core) {
    for (auto& slot : context->slots) {
      if (slot.descriptorPool)
        context->core->destroyDescriptorPool(context->core->device, slot.descriptorPool,
                                              nullptr);
    }
    ffgVkDestroy(context->core);
  }
  delete context;
}

extern "C" FFG_VK_API VkResult __cdecl ffgVkGetCapabilitiesV3(
    FfgVkContextV3* context, FgdsVkCapabilitiesV3* capabilities) {
  if (!context || !context->core || !capabilities)
    return VK_ERROR_INITIALIZATION_FAILED;
  if (capabilities->structSize < sizeof(FgdsVkCapabilitiesV3))
    return VK_ERROR_VALIDATION_FAILED_EXT;
  capabilities->version = FGDS_VK_VERSION_0_3;
  capabilities->backend = FGDS_VK_BACKEND_VULKAN;
  capabilities->slotCount = static_cast<uint32_t>(context->slots.size());
  capabilities->maxSlots = FGDS_VK_V3_MAX_SLOTS;
  capabilities->maxWidth = 16384;
  capabilities->maxHeight = 16384;
  capabilities->requiredResources = FGDS_VK_RESOURCE_CORE;
  capabilities->optionalResources = 0;
  capabilities->featureFlags = FGDS_VK_FEATURE_NATIVE_V2 |
                                FGDS_VK_FEATURE_BIDIRECTIONAL_MOTION |
                                FGDS_VK_FEATURE_DEPTH_OCCLUSION |
                                FGDS_VK_FEATURE_OBJECT_ID_REJECTION |
                                FGDS_VK_FEATURE_CAMERA_CUT |
                                FGDS_VK_FEATURE_MULTI_FLIGHT |
                                FGDS_VK_FEATURE_HDR_METADATA;
  capabilities->colorFormat = FGDS_VK_FORMAT_RGBA32_FLOAT;
  capabilities->depthFormat = FGDS_VK_FORMAT_R32_FLOAT;
  capabilities->motionFormat = FGDS_VK_FORMAT_RG32_FLOAT;
  capabilities->objectIdFormat = FGDS_VK_FORMAT_R32_UINT;
  for (auto& value : capabilities->reserved)
    value = 0;
  return VK_SUCCESS;
}

extern "C" FFG_VK_API VkResult __cdecl ffgVkRecordV3(
    FfgVkContextV3* context, VkCommandBuffer commandBuffer, const FgdsVkPairV2* pair,
    const FgdsVkImage* output, uint32_t slotIndex, VkSemaphore completionSemaphore,
    uint64_t completionValue) {
  if (!context || !context->core || !commandBuffer || !pair || !output)
    return VK_ERROR_INITIALIZATION_FAILED;
  if (slotIndex >= context->slots.size())
    return VK_ERROR_VALIDATION_FAILED_EXT;
  if (completionSemaphore == VK_NULL_HANDLE || completionValue == 0)
    return VK_ERROR_VALIDATION_FAILED_EXT;

  // The caller's new token must describe a future signal.  Rejecting an
  // already-reached value prevents a slot from appearing retired before the
  // command buffer is submitted, including when a slot changes semaphores.
  uint64_t completionCounter = 0;
  VkResult result = context->getSemaphoreCounterValue(
      context->core->device, completionSemaphore, &completionCounter);
  if (result != VK_SUCCESS)
    return result;
  if (completionCounter >= completionValue)
    return VK_ERROR_VALIDATION_FAILED_EXT;

  auto& slot = context->slots[slotIndex];
  if (slot.retireSemaphore != VK_NULL_HANDLE) {
    uint64_t completed = 0;
    result = context->getSemaphoreCounterValue(
        context->core->device, slot.retireSemaphore, &completed);
    if (result != VK_SUCCESS)
      return result;
    if (completed < slot.retireValue)
      return VK_NOT_READY;
  }

  // Reuse the thoroughly validated V2 path while overriding only the
  // descriptor set selected for this slot.  The local copy prevents any
  // mutation of the shared core descriptor set and keeps slots independent.
  FfgVkContext view = *context->core;
  view.descriptorSet = slot.descriptorSet;
  result = ffgVkRecordV2(&view, commandBuffer, pair, output);
  if (result == VK_SUCCESS) {
    slot.retireSemaphore = completionSemaphore;
    slot.retireValue = completionValue;
  }
  return result;
}

namespace {
bool sharedAllZero(const uint32_t* values, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (values[i] != 0)
      return false;
  }
  return true;
}

bool vkHdrValid(const FgdsHdrMetadata& metadata) {
  if (metadata.structSize < sizeof(FgdsHdrMetadata) ||
      metadata.version != FGDS_HDR_VERSION_1 || metadata.reserved0 != 0 ||
      (metadata.flags & ~FGDS_HDR_FLAGS) != 0 || !sharedAllZero(metadata.reserved, 4))
    return false;
  if (metadata.colorSpace > FGDS_HDR_COLOR_SPACE_HDR10_HLG ||
      metadata.transferFunction < FGDS_HDR_TRANSFER_SRGB ||
      metadata.transferFunction > FGDS_HDR_TRANSFER_HLG)
    return false;
  for (float value : metadata.primaries) {
    if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
      return false;
  }
  for (float value : metadata.whitePoint) {
    if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
      return false;
  }
  const float luminances[] = {metadata.maxMasteringLuminanceNits,
                              metadata.minMasteringLuminanceNits,
                              metadata.maxContentLightLevelNits,
                              metadata.maxFrameAverageLightLevelNits,
                              metadata.nominalPeakLuminanceNits};
  for (float value : luminances) {
    if (!std::isfinite(value) || value < 0.0f)
      return false;
  }
  if (metadata.colorSpace == FGDS_HDR_COLOR_SPACE_SDR)
    return metadata.transferFunction == FGDS_HDR_TRANSFER_SRGB ||
           metadata.transferFunction == FGDS_HDR_TRANSFER_LINEAR;
  if (metadata.maxMasteringLuminanceNits <= 0.0f ||
      metadata.minMasteringLuminanceNits > metadata.maxMasteringLuminanceNits ||
      metadata.nominalPeakLuminanceNits <= 0.0f)
    return false;
  if (metadata.colorSpace == FGDS_HDR_COLOR_SPACE_SCRGB)
    return metadata.transferFunction == FGDS_HDR_TRANSFER_LINEAR;
  if (metadata.colorSpace == FGDS_HDR_COLOR_SPACE_HDR10_PQ)
    return metadata.transferFunction == FGDS_HDR_TRANSFER_PQ;
  return metadata.transferFunction == FGDS_HDR_TRANSFER_HLG;
}

bool vkExternalHandleType(uint32_t type) {
  return type == FGDS_SHARED_HANDLE_WIN32 || type == FGDS_SHARED_HANDLE_OPAQUE_FD;
}
}  // namespace

extern "C" FFG_VK_API VkResult __cdecl ffgVkValidateHdrMetadata(
    const FgdsHdrMetadata* metadata) {
  if (!metadata)
    return VK_ERROR_INITIALIZATION_FAILED;
  return vkHdrValid(*metadata) ? VK_SUCCESS : VK_ERROR_VALIDATION_FAILED_EXT;
}

extern "C" FFG_VK_API VkResult __cdecl ffgVkValidateExternalImageV1(
    const FgdsVkExternalImage* external) {
  if (!external)
    return VK_ERROR_INITIALIZATION_FAILED;
  const auto& image = external->image;
  if (external->structSize < sizeof(FgdsVkExternalImage) ||
      external->version != FGDS_VK_EXTERNAL_VERSION_1 ||
      external->imageLayout != FGDS_VK_EXTERNAL_LAYOUT_GENERAL ||
      !sharedAllZero(external->reserved, 2) ||
      image.structSize < sizeof(FgdsSharedImage) ||
      image.version != FGDS_SHARED_VERSION_1 ||
      image.backend != FGDS_SHARED_BACKEND_VULKAN || image.width == 0 || image.height == 0 ||
      image.arrayLayers != 1 || image.mipLevels != 1 || image.sampleCount != 1 ||
      (image.flags & ~(FGDS_SHARED_IMAGE_SHADER_RESOURCE |
                       FGDS_SHARED_IMAGE_UNORDERED_ACCESS |
                       FGDS_SHARED_IMAGE_IMPORTED)) != 0 ||
      !(image.flags & FGDS_SHARED_IMAGE_SHADER_RESOURCE) || image.handleType == 0 ||
      !vkExternalHandleType(image.handleType) || image.resourceHandle == 0 ||
      image.adapterLuid == 0 || image.reserved0 != 0 || !sharedAllZero(image.reserved, 4))
    return VK_ERROR_VALIDATION_FAILED_EXT;
  if (image.format != FGDS_VK_FORMAT_RGBA32_FLOAT && image.format != FGDS_VK_FORMAT_R32_FLOAT &&
      image.format != FGDS_VK_FORMAT_RG32_FLOAT && image.format != FGDS_VK_FORMAT_R32_UINT)
    return VK_ERROR_VALIDATION_FAILED_EXT;
  // A Vulkan image is recreated in the consumer process from the exported
  // allocation.  Passing a producer VkImage handle or an auxiliary D3D12
  // handle would make the descriptor process-local and is rejected.
  if (image.auxHandle != 0)
    return VK_ERROR_VALIDATION_FAILED_EXT;
  return VK_SUCCESS;
}

extern "C" FFG_VK_API VkResult __cdecl ffgVkValidateExternalSyncV1(
    const FgdsVkExternalSync* external) {
  if (!external)
    return VK_ERROR_INITIALIZATION_FAILED;
  const auto& sync = external->sync;
  if (external->structSize < sizeof(FgdsVkExternalSync) ||
      external->version != FGDS_VK_EXTERNAL_VERSION_1 ||
      external->semaphoreType != FGDS_VK_EXTERNAL_SEMAPHORE_TIMELINE ||
      !sharedAllZero(external->reserved, 2) || sync.structSize < sizeof(FgdsSharedSync) ||
      sync.version != FGDS_SHARED_VERSION_1 || sync.backend != FGDS_SHARED_BACKEND_VULKAN ||
      sync.type != FGDS_SHARED_SYNC_VULKAN_TIMELINE || !vkExternalHandleType(sync.handleType) ||
      sync.flags != 0 || sync.reserved0 != 0 || sync.adapterLuid == 0 ||
      sync.fenceHandle == 0 || sync.value == 0 || !sharedAllZero(sync.reserved, 4))
    return VK_ERROR_VALIDATION_FAILED_EXT;
  return VK_SUCCESS;
}
