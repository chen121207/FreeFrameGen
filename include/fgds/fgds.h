#pragma once
// FGDS 0.1 experimental, same-process D3D12 contract. Not a stable standard.
#include <stdint.h>
#ifdef __cplusplus
#include <d3d12.h>
extern "C"
{
#endif
#define FGDS_VERSION 0x00010000u
#define FGDS_CAMERA_CUT 1u
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
#ifdef __cplusplus
}
static_assert(sizeof(float) == 4);
#endif
