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
// Caller sets capabilities->structSize.  The runtime writes only the fields
// covered by this version, so a larger append-only struct is forward-safe.
FFG_VK_API VkResult __cdecl ffgVkGetCapabilities(FfgVkContext* context,
    FgdsVkCapabilities* capabilities);

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
// The runtime validates the declared FGDS_VK_FORMAT_* IDs, GENERAL layout,
// zero jitter, and output image/view handle inequality with all inputs.  It
// cannot verify actual VkImage formats, backing-memory aliasing, or ownership.
FFG_VK_API VkResult __cdecl ffgVkRecord(FfgVkContext*, VkCommandBuffer,
    const FgdsVkPair*, const FgdsVkImage* output);

// Vulkan v0.2 append-only input contract.  It preserves ffgVkRecord and the
// frozen v0.1 structs.  Core resources are required; supplied masks return
// VK_ERROR_FEATURE_NOT_PRESENT until a mask-aware kernel is enabled.
FFG_VK_API VkResult __cdecl ffgVkRecordV2(FfgVkContext*, VkCommandBuffer,
    const FgdsVkPairV2*, const FgdsVkImage* output);

// Vulkan v0.3 multi-flight recording API.  A V3 context owns one descriptor
// set/pool per slot and can record several command buffers without rewriting
// descriptors used by an earlier submission.  The API still only records
// commands; it never submits, waits, or signals the completion semaphore.
//
// completionSemaphore/value is a required caller-owned timeline semaphore
// token that will be signalled after the recorded command buffer executes.
// Every call must pass a non-null semaphore and a non-zero value.  The runtime
// queries that semaphore before recording and returns
// VK_ERROR_VALIDATION_FAILED_EXT when its current counter is already at or
// above completionValue.  When a slot is reused, the previous token is also
// queried and VK_NOT_READY is returned until it has completed.  The semaphore
// must be created for this device; the caller remains responsible for queue
// submit ordering and signal values.
typedef struct FfgVkContextV3 FfgVkContextV3;
FFG_VK_API VkResult __cdecl ffgVkCreateV3(VkPhysicalDevice physicalDevice,
    VkDevice device, uint32_t queueFamily, PFN_vkGetDeviceProcAddr getProc,
    uint32_t slotCount, FfgVkContextV3** output);
FFG_VK_API void __cdecl ffgVkDestroyV3(FfgVkContextV3* context);
FFG_VK_API VkResult __cdecl ffgVkGetCapabilitiesV3(FfgVkContextV3* context,
    FgdsVkCapabilitiesV3* capabilities);
FFG_VK_API VkResult __cdecl ffgVkRecordV3(FfgVkContextV3* context,
    VkCommandBuffer commandBuffer, const FgdsVkPairV2* pair,
    const FgdsVkImage* output, uint32_t slotIndex,
    VkSemaphore completionSemaphore, uint64_t completionValue);
#ifdef __cplusplus
}
#endif
