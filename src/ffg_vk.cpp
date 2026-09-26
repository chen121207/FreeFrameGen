#include "ffg/ffg_vk.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

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

namespace {

template <typename T>
T load(PFN_vkGetDeviceProcAddr proc, VkDevice device, const char* name) {
  return reinterpret_cast<T>(proc(device, name));
}

bool validFrame(const FgdsVkFrame& frame, VkDevice device, VkPhysicalDevice physicalDevice,
                uint32_t queueFamily, uint32_t width, uint32_t height) {
  if (frame.structSize != sizeof(FgdsVkFrame) || frame.version != FGDS_VK_VERSION ||
      frame.reserved || frame.reserved2 || frame.width != width || frame.height != height ||
      frame.device != static_cast<uint64_t>(reinterpret_cast<uintptr_t>(device)) ||
      frame.physicalDevice != static_cast<uint64_t>(reinterpret_cast<uintptr_t>(physicalDevice)) ||
      frame.queueFamily != queueFamily || (frame.flags & ~FGDS_VK_CAMERA_CUT))
    return false;
  for (float value : frame.worldToClip)
    if (!std::isfinite(value))
      return false;
  if (!std::isfinite(frame.jitterPixels[0]) || !std::isfinite(frame.jitterPixels[1]))
    return false;
  return true;
}

VkImageView viewOf(const FgdsVkImage& image) {
  return reinterpret_cast<VkImageView>(static_cast<uintptr_t>(image.view));
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
  VkDescriptorImageInfo infos[9]{};
  for (uint32_t i = 0; i < 9; ++i) {
    if (!images[i]->image || !images[i]->view || images[i]->layout != VK_IMAGE_LAYOUT_GENERAL)
      return VK_ERROR_VALIDATION_FAILED_EXT;
    infos[i].imageView = viewOf(*images[i]);
    infos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
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
