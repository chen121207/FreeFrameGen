#pragma once
// FGDS 0.1/0.2/0.3 experimental, same-process D3D12 contract. Not a stable standard.
#include <stdint.h>
#include "ipc.h"
#ifdef __cplusplus
#include <d3d12.h>
extern "C"
{
#endif
#define FGDS_VERSION 0x00010000u
#define FGDS_VERSION_0_1 FGDS_VERSION
#define FGDS_VERSION_0_2 0x00020000u
#define FGDS_VERSION_0_3 0x00030000u
#define FGDS_CAMERA_CUT 1u

// The v0.1 structures below are frozen.  Do not append fields to them: a
// caller compiled against the original header passes sizeof(FgdsFrame) and
// sizeof(FgdsPair) to the DLL.  v0.2 adds new structures after the frozen ABI
// and a separate ffgRecordV2 entry point.
    typedef struct FgdsFrame
    {
        uint32_t structSize, version, width, height;
        uint64_t frameId, timestampNs;
        // Row-major, row-vector, unjittered world -> clip. Demo is orthographic.
        float worldToClip[16];
        float jitterPixels[2];
        uint32_t flags, reserved;
        // Borrowed ID3D12Resource*: all same device, 2D, single-sample/mip/layer.
        // Color: RGBA32_FLOAT, linear Rec709 (not sRGB), opaque pre-HUD.
        // Depth: R32_FLOAT, positive linear view distance, smaller = nearer.
        // Motion: RG32_FLOAT, pixels, +x right/+y down, THIS -> OTHER pair frame.
        // Frame 0 motion is forward; frame 1 motion is backward. Neither is
        // silently inferred from a previous frame's backward vector.
        // Object ID: R32_UINT, stable nonzero ID (0 = invalid).
        void *color;
        void *depth;
        void *motionToOther;
        void *objectId;
        // Reserved optional R8_UNORM masks. v0 runtime rejects non-null masks.
        void *hudMask;
        void *transparencyMask;
    } FgdsFrame;
    typedef struct FgdsPair
    {
        uint32_t structSize, version;
        FgdsFrame frames[2]; // chronological, matching extent
        float alpha;         // 0 = older real frame, 1 = newer real frame
        uint32_t reserved;
    } FgdsPair;

    // v0.2 resource bits.  The four core resources remain required by the
    // reference kernel.  Mask bits describe optional resources in the wire
    // contract; the current D3D12 runtime reports them as unsupported and
    // returns E_NOTIMPL when a caller supplies one.
#define FGDS_RESOURCE_COLOR (1u << 0)
#define FGDS_RESOURCE_DEPTH (1u << 1)
#define FGDS_RESOURCE_MOTION (1u << 2)
#define FGDS_RESOURCE_OBJECT_ID (1u << 3)
#define FGDS_RESOURCE_HUD_MASK (1u << 4)
#define FGDS_RESOURCE_TRANSPARENCY_MASK (1u << 5)
#define FGDS_RESOURCE_CORE \
    (FGDS_RESOURCE_COLOR | FGDS_RESOURCE_DEPTH | FGDS_RESOURCE_MOTION | FGDS_RESOURCE_OBJECT_ID)

#define FGDS_FRAME_HUD_MASK (1u << 1)
#define FGDS_FRAME_TRANSPARENCY_MASK (1u << 2)
#define FGDS_FRAME_V2_FLAGS (FGDS_CAMERA_CUT | FGDS_FRAME_HUD_MASK | FGDS_FRAME_TRANSPARENCY_MASK)

    // Stable format identifiers avoid exposing DXGI/Vulkan enum values in the
    // common protocol header.  v0.2 D3D12 currently reports these exact four
    // formats and does not perform format conversion.
#define FGDS_FORMAT_UNKNOWN 0u
#define FGDS_FORMAT_RGBA32_FLOAT 1u
#define FGDS_FORMAT_R32_FLOAT 2u
#define FGDS_FORMAT_RG32_FLOAT 3u
#define FGDS_FORMAT_R32_UINT 4u

#define FGDS_BACKEND_D3D12 1u

    // Feature bits returned by ffgGetCapabilities.  A bit is only advertised
    // when the runtime consumes that feature, rather than merely parsing it.
#define FGDS_FEATURE_NATIVE_V2 (1ull << 0)
#define FGDS_FEATURE_BIDIRECTIONAL_MOTION (1ull << 1)
#define FGDS_FEATURE_DEPTH_OCCLUSION (1ull << 2)
#define FGDS_FEATURE_OBJECT_ID_REJECTION (1ull << 3)
#define FGDS_FEATURE_CAMERA_CUT (1ull << 4)
// v0.4 advertises transport capabilities separately from the frozen v0.1,
// v0.2 and v0.3 record entry points.  A bit is only set once the runtime can
// validate/import the corresponding descriptor; it does not imply that a
// caller's adapter or driver supports every external-handle extension.
#define FGDS_FEATURE_HDR_METADATA (1ull << 6)
#define FGDS_FEATURE_SHARED_RESOURCES (1ull << 7)

    typedef struct FgdsCapabilities
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
    } FgdsCapabilities;

    // Same resource prefix as FgdsFrame, followed by explicit resource bits.
    // structSize is append-only: future producers may pass a larger value;
    // consumers must only read fields covered by the current size.
    typedef struct FgdsFrameV2
    {
        uint32_t structSize, version, width, height;
        uint64_t frameId, timestampNs;
        float worldToClip[16];
        float jitterPixels[2];
        uint32_t flags, reserved;
        void *color;
        void *depth;
        void *motionToOther;
        void *objectId;
        void *hudMask;
        void *transparencyMask;
        uint32_t resourceFlags;
        uint32_t reserved2;
    } FgdsFrameV2;

    typedef struct FgdsPairV2
    {
        uint32_t structSize, version;
        FgdsFrameV2 frames[2];
        float alpha;
        uint32_t reserved;
    } FgdsPairV2;

    // v0.3 adds a separate, fence-retired multi-flight recording API. Its
    // input is still FgdsPairV2; do not change the frozen v0.1/v0.2 layouts.
    // Each slot owns nine shader-visible descriptors. A slot cannot be
    // overwritten until the GPU fence value supplied with its prior record
    // has completed. The application owns the fence and signals it only after
    // the recorded command list has executed on its queue.
#define FGDS_FEATURE_MULTI_FLIGHT (1ull << 5)
#define FGDS_V3_MAX_SLOTS 8u
    typedef struct FgdsCapabilitiesV3
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
    } FgdsCapabilitiesV3;
#ifdef __cplusplus
}
static_assert(sizeof(float) == 4);
#endif
