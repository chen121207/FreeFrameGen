#pragma once
#include <vulkan/vulkan.h>
#include "fgds/fgds_vk.h"
#ifdef FFG_VK_BUILD
#define FFG_VK_API __declspec(dllexport)
#else
#define FFG_VK_API __declspec(dllimport)
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct FfgVkContext FfgVkContext;
// Independent Vulkan runtime; no D3D12 dependency or device/queue ownership.
FFG_VK_API VkResult __cdecl ffgVkCreate(VkPhysicalDevice physicalDevice,
    VkDevice device, uint32_t queueFamily, PFN_vkGetDeviceProcAddr getProc,
    FfgVkContext** output);
FFG_VK_API void __cdecl ffgVkDestroy(FfgVkContext* context);

// Records only; never submits, waits or presents. One context per in-flight
// dispatch. Caller must fence-complete before reuse/destroy, and externally
// serialize access. Command buffer must belong to the supplied device/family,
// be recording outside a render pass, and support compute.
//
// All views are identity-swizzled color-aspect 2D, one mip/layer/sample, STORAGE
// images in GENERAL. Formats: RGBA32F, R32F, RG32F, R32UI (same as D3D12 FGDS).
// Output: RGBA32F, same extent, no alias with inputs (including aliased memory).
// Caller handles image transitions, queue ownership and producer->compute
// barriers. ready semaphores are metadata: caller MUST put waits in its submit.
// Output remains GENERAL. A compute-write -> all-commands memory barrier is
// recorded. Compute pipeline/descriptors/push constants are clobbered.
//
// Vulkan cannot introspect handles: format/size/device metadata are a caller
// guarantee, NOT independently verified by this ABI. Invalid handles are UB.
FFG_VK_API VkResult __cdecl ffgVkRecord(FfgVkContext*, VkCommandBuffer,
    const FgdsVkPair*, const FgdsVkImage* output);
#ifdef __cplusplus
}
#endif
