#pragma once
// FGDS 0.4 shared-transport and HDR metadata contract.
//
// This file is deliberately free of Windows/Vulkan types.  Handles are
// represented by their native integer value so a producer and consumer can
// exchange a fixed C ABI without pulling one graphics SDK into the other.
// A handle value is meaningful only in the receiving process after the
// producer has exported/duplicated it with the declared handle type.
#include <stdint.h>

#define FGDS_VERSION_0_4 0x00040000u
#define FGDS_SHARED_VERSION_1 0x00010000u
#define FGDS_HDR_VERSION_1 0x00010000u

// Shared transport backends.  The handle payload is an opaque native value;
// it is never a pointer into the producer's address space.
#define FGDS_SHARED_BACKEND_D3D12 1u
#define FGDS_SHARED_BACKEND_VULKAN 2u

#define FGDS_SHARED_HANDLE_WIN32 1u
#define FGDS_SHARED_HANDLE_NT 2u
#define FGDS_SHARED_HANDLE_OPAQUE_FD 3u

#define FGDS_SHARED_SYNC_D3D12_FENCE 1u
#define FGDS_SHARED_SYNC_VULKAN_TIMELINE 2u

#define FGDS_SHARED_IMAGE_SHADER_RESOURCE (1u << 0)
#define FGDS_SHARED_IMAGE_UNORDERED_ACCESS (1u << 1)
#define FGDS_SHARED_IMAGE_IMPORTED (1u << 2)

#define FGDS_HDR_COLOR_SPACE_SDR 0u
#define FGDS_HDR_COLOR_SPACE_SCRGB 1u
#define FGDS_HDR_COLOR_SPACE_HDR10_PQ 2u
#define FGDS_HDR_COLOR_SPACE_HDR10_HLG 3u
#define FGDS_HDR_TRANSFER_SRGB 1u
#define FGDS_HDR_TRANSFER_LINEAR 2u
#define FGDS_HDR_TRANSFER_PQ 3u
#define FGDS_HDR_TRANSFER_HLG 4u
#define FGDS_HDR_FLAG_STATIC_METADATA (1u << 0)
#define FGDS_HDR_FLAG_DISPLAY_TARGET (1u << 1)
#define FGDS_HDR_FLAGS (FGDS_HDR_FLAG_STATIC_METADATA | FGDS_HDR_FLAG_DISPLAY_TARGET)

typedef struct FgdsHdrMetadata
{
    uint32_t structSize;
    uint32_t version;
    uint32_t colorSpace;
    uint32_t transferFunction;
    uint32_t flags;
    uint32_t reserved0;
    // xy pairs in the [0, 1] protocol range: R, G, B, then white point.
    float primaries[6];
    float whitePoint[2];
    float maxMasteringLuminanceNits;
    float minMasteringLuminanceNits;
    float maxContentLightLevelNits;
    float maxFrameAverageLightLevelNits;
    float nominalPeakLuminanceNits;
    uint32_t reserved[4];
} FgdsHdrMetadata;

typedef struct FgdsSharedImage
{
    uint32_t structSize;
    uint32_t version;
    uint32_t backend;
    uint32_t format; // FGDS_FORMAT_* or FGDS_VK_FORMAT_* protocol ID.
    uint32_t width;
    uint32_t height;
    uint32_t arrayLayers;
    uint32_t mipLevels;
    uint32_t sampleCount;
    uint32_t flags;
    uint32_t handleType; // FGDS_SHARED_HANDLE_*.
    uint32_t reserved0;
    uint64_t adapterLuid; // DXGI LUID packed as LowPart | HighPart << 32.
    uint64_t resourceHandle;
    uint64_t auxHandle; // Vulkan external allocation/metadata handle; zero for D3D12.
    uint64_t allocationSize; // Optional accounting value; zero means unknown.
    uint32_t reserved[4];
} FgdsSharedImage;

typedef struct FgdsSharedSync
{
    uint32_t structSize;
    uint32_t version;
    uint32_t backend;
    uint32_t type;       // FGDS_SHARED_SYNC_*.
    uint32_t handleType; // FGDS_SHARED_HANDLE_*.
    uint32_t flags;
    uint32_t reserved0;
    uint64_t adapterLuid;
    uint64_t fenceHandle;
    uint64_t value; // The value to wait for / retire at; zero is invalid.
    uint32_t reserved[4];
} FgdsSharedSync;

// Shared frame has no process-local pointers.  The producer exports each
// image and its ready fence before handing this value to the consumer.
typedef struct FgdsSharedFrame
{
    uint32_t structSize;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint64_t frameId;
    uint64_t timestampNs;
    float worldToClip[16];
    float jitterPixels[2];
    uint32_t flags;
    uint32_t resourceFlags;
    FgdsSharedImage color;
    FgdsSharedImage depth;
    FgdsSharedImage motionToOther;
    FgdsSharedImage objectId;
    FgdsSharedImage hudMask;
    FgdsSharedImage transparencyMask;
    FgdsSharedSync ready;
    FgdsHdrMetadata hdr;
    uint32_t reserved[4];
} FgdsSharedFrame;

// The output image and retire fence are supplied in the same message so a
// consumer can import all objects atomically before recording.  The consumer
// never signals retire itself in the record-only API; the host signals it
// after ExecuteCommandLists has been submitted.
typedef struct FgdsSharedPair
{
    uint32_t structSize;
    uint32_t version;
    FgdsSharedFrame frames[2];
    float alpha;
    uint32_t reserved0;
    FgdsSharedImage output;
    FgdsSharedSync retire;
    FgdsHdrMetadata outputHdr;
    uint32_t reserved[4];
} FgdsSharedPair;
