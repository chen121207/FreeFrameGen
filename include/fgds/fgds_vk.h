#pragma once
/*
 * FGDS Vulkan 0.1/0.2/0.3 experimental contract.
 *
 * This header intentionally avoids a hard dependency on the Vulkan SDK so
 * that a producer and a consumer can share the ABI from x86 or x64 builds.
 * Vulkan handles are encoded as their native 64-bit handle value. The
 * producer remains responsible for device ownership and synchronization.
 */
#include <stdint.h>
#include "ipc.h"

#define FGDS_VK_VERSION 0x00010000u
#define FGDS_VK_VERSION_0_1 FGDS_VK_VERSION
#define FGDS_VK_VERSION_0_2 0x00020000u
#define FGDS_VK_VERSION_0_3 0x00030000u
#define FGDS_VK_VERSION_0_4 0x00040000u
#define FGDS_VK_CAMERA_CUT 1u
#define FGDS_VK_BACKEND_VULKAN 2u

// Keep the v0.1 Vulkan structs frozen for the same reason as the D3D12
// contract.  v0.2 appends explicit resource bits and uses separate entry
// points; v0.3 adds capabilities and multi-flight entries, so an old
// producer/consumer never observes a changed sizeof.
#define FGDS_VK_RESOURCE_COLOR (1u << 0)
#define FGDS_VK_RESOURCE_DEPTH (1u << 1)
#define FGDS_VK_RESOURCE_MOTION (1u << 2)
#define FGDS_VK_RESOURCE_OBJECT_ID (1u << 3)
#define FGDS_VK_RESOURCE_HUD_MASK (1u << 4)
#define FGDS_VK_RESOURCE_TRANSPARENCY_MASK (1u << 5)
#define FGDS_VK_RESOURCE_CORE \
    (FGDS_VK_RESOURCE_COLOR | FGDS_VK_RESOURCE_DEPTH | FGDS_VK_RESOURCE_MOTION | \
     FGDS_VK_RESOURCE_OBJECT_ID)
#define FGDS_VK_FRAME_HUD_MASK (1u << 1)
#define FGDS_VK_FRAME_TRANSPARENCY_MASK (1u << 2)
#define FGDS_VK_FRAME_V2_FLAGS \
    (FGDS_VK_CAMERA_CUT | FGDS_VK_FRAME_HUD_MASK | FGDS_VK_FRAME_TRANSPARENCY_MASK)
#define FGDS_VK_FORMAT_RGBA32_FLOAT 1u
#define FGDS_VK_FORMAT_R32_FLOAT 2u
#define FGDS_VK_FORMAT_RG32_FLOAT 3u
#define FGDS_VK_FORMAT_R32_UINT 4u
#define FGDS_VK_FEATURE_NATIVE_V2 (1ull << 0)
#define FGDS_VK_FEATURE_BIDIRECTIONAL_MOTION (1ull << 1)
#define FGDS_VK_FEATURE_DEPTH_OCCLUSION (1ull << 2)
#define FGDS_VK_FEATURE_OBJECT_ID_REJECTION (1ull << 3)
#define FGDS_VK_FEATURE_CAMERA_CUT (1ull << 4)
#define FGDS_VK_FEATURE_MULTI_FLIGHT (1ull << 5)
#define FGDS_VK_FEATURE_HDR_METADATA (1ull << 6)
#define FGDS_VK_FEATURE_SHARED_RESOURCES (1ull << 7)
#define FGDS_VK_V3_MAX_SLOTS 8u

// External-memory/semaphore descriptors are deliberately separate from the
// same-process FgdsVkImage/FgdsVkSync values above.  A VkImage/VkSemaphore
// handle is never sent across a process boundary; only the exported memory or
// semaphore handle plus image creation metadata is transported.
#define FGDS_VK_EXTERNAL_VERSION_1 0x00010000u
#define FGDS_VK_EXTERNAL_LAYOUT_GENERAL 1u // VkImageLayout::VK_IMAGE_LAYOUT_GENERAL
#define FGDS_VK_EXTERNAL_SEMAPHORE_TIMELINE 1u
#define FGDS_VK_EXTERNAL_QUEUE_FAMILY_IGNORED 0xffffffffu
typedef struct FgdsVkExternalImage
{
    uint32_t structSize;
    uint32_t version;
    FgdsSharedImage image; // backend must be FGDS_SHARED_BACKEND_VULKAN.
    uint32_t imageLayout;  // Must be GENERAL at the FGDS compute boundary.
    uint32_t queueFamily;
    uint32_t reserved[2];
} FgdsVkExternalImage;

typedef struct FgdsVkExternalSync
{
    uint32_t structSize;
    uint32_t version;
    FgdsSharedSync sync; // type must be FGDS_SHARED_SYNC_VULKAN_TIMELINE.
    uint32_t semaphoreType; // Must be the timeline semaphore type (1).
    uint32_t reserved[2];
} FgdsVkExternalSync;

typedef struct FgdsVkImage
{
    uint64_t image;
    uint64_t view;
    uint32_t format; // FGDS_VK_FORMAT_* protocol ID, not a VkFormat number.
    uint32_t layout; // VkImageLayout numeric value; current kernel requires GENERAL.
} FgdsVkImage;

typedef struct FgdsVkSync
{
    uint64_t semaphore;
    uint64_t value;
} FgdsVkSync;

typedef struct FgdsVkFrame
{
    uint32_t structSize, version, width, height;
    uint64_t frameId, timestampNs;
    float worldToClip[16];
    float jitterPixels[2];
    uint32_t flags, reserved;
    uint64_t device;
    uint64_t physicalDevice;
    uint32_t queueFamily;
    uint32_t reserved2;
    FgdsVkImage color;
    FgdsVkImage depth;
    FgdsVkImage motionToOther;
    FgdsVkImage objectId;
    FgdsVkImage hudMask;
    FgdsVkImage transparencyMask;
    FgdsVkSync ready;
} FgdsVkFrame;

typedef struct FgdsVkPair
{
    uint32_t structSize, version;
    FgdsVkFrame frames[2];
    float alpha;
    uint32_t reserved;
} FgdsVkPair;

typedef struct FgdsVkCapabilities
{
    uint32_t structSize;
    uint32_t version;
    uint32_t nativeApiVersion;
    uint32_t backend;
    uint32_t maxWidth;
    uint32_t maxHeight;
    uint32_t requiredResources;
    uint32_t optionalResources;
    uint64_t featureFlags;
    uint32_t colorFormat;
    uint32_t depthFormat;
    uint32_t motionFormat;
    uint32_t objectIdFormat;
    uint32_t reserved[4];
} FgdsVkCapabilities;

typedef struct FgdsVkFrameV2
{
    uint32_t structSize, version, width, height;
    uint64_t frameId, timestampNs;
    float worldToClip[16];
    float jitterPixels[2];
    uint32_t flags, reserved;
    uint64_t device;
    uint64_t physicalDevice;
    uint32_t queueFamily;
    uint32_t reserved2;
    FgdsVkImage color;
    FgdsVkImage depth;
    FgdsVkImage motionToOther;
    FgdsVkImage objectId;
    FgdsVkImage hudMask;
    FgdsVkImage transparencyMask;
    FgdsVkSync ready;
    uint32_t resourceFlags;
    uint32_t reserved3;
} FgdsVkFrameV2;

typedef struct FgdsVkPairV2
{
    uint32_t structSize, version;
    FgdsVkFrameV2 frames[2];
    float alpha;
    uint32_t reserved;
} FgdsVkPairV2;

// v0.3 appends a capability structure for the multi-flight recording API.
// The v0.1/v0.2 structs above remain frozen.  slotCount is the number of
// descriptor-set slots allocated by ffgVkCreateV3; maxSlots is the protocol
// ceiling and lets a caller size its own frame queue without probing.
typedef struct FgdsVkCapabilitiesV3
{
    uint32_t structSize;
    uint32_t version;
    uint32_t backend;
    uint32_t slotCount;
    uint32_t maxSlots;
    uint32_t maxWidth;
    uint32_t maxHeight;
    uint32_t requiredResources;
    uint32_t optionalResources;
    uint64_t featureFlags;
    uint32_t colorFormat;
    uint32_t depthFormat;
    uint32_t motionFormat;
    uint32_t objectIdFormat;
    uint32_t reserved[4];
} FgdsVkCapabilitiesV3;
