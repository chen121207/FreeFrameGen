#include "ffg/ffg.h"
#include "d3d12_host.hpp"
#include "ffg_shader.hpp"
#include <cstddef>
#include <cmath>
#include <array>
#include <memory>
#include <vector>
struct FfgContext
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12DescriptorHeap> descriptors;
    ComPtr<ID3D12RootSignature> signature;
    ComPtr<ID3D12PipelineState> pso;
    UINT stride = 0;
};
struct FfgSlotV3
{
    ComPtr<ID3D12DescriptorHeap> descriptors;
    ComPtr<ID3D12Fence> retireFence;
    UINT64 retireValue = 0;
    // Shared-handle imports are retained with the descriptor slot until the
    // host's retire fence reaches retireValue.  Otherwise a caller could
    // close its exported handles while the GPU is still reading them.
    std::vector<ComPtr<ID3D12Resource>> sharedResources;
    std::vector<ComPtr<ID3D12Fence>> sharedFences;
};
struct FfgContextV3
{
    std::unique_ptr<FfgContext> core;
    std::vector<FfgSlotV3> slots;
};
extern "C" HRESULT __cdecl ffgCreate(ID3D12Device *device, FfgContext **output)
{
    if (!output)
        return E_POINTER;
    *output = nullptr;
    if (!device)
        return E_POINTER;
    try
    {
        auto c = std::make_unique<FfgContext>();
        c->device = device;
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{DXGI_FORMAT_R32G32B32A32_FLOAT};
        check(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof support));
        if (!(support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE))
            return DXGI_ERROR_UNSUPPORTED;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.NumDescriptors = 9;
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&c->descriptors)));
        c->stride = device->GetDescriptorHandleIncrementSize(hd.Type);
        D3D12_DESCRIPTOR_RANGE ranges[2] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0, 0, 0},
                                            {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 8}};
        D3D12_ROOT_PARAMETER p[2]{};
        p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        p[0].DescriptorTable = {2, ranges};
        p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        p[1].Constants = {0, 0, 4};
        D3D12_ROOT_SIGNATURE_DESC rs{2, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        c->signature = root(device, rs);
        auto cs = compile(FFG_SHADER, "main", "cs_5_1");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = c->signature.Get();
        pd.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
        check(device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&c->pso)));
        *output = c.release();
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}
extern "C" void __cdecl ffgDestroy(FfgContext *c)
{
    delete c;
}

extern "C" HRESULT __cdecl ffgGetCapabilities(FfgContext *c, FgdsCapabilities *capabilities)
{
    if (!c || !capabilities)
        return E_POINTER;
    // A larger struct is accepted so a newer caller can share the same binary
    // with this runtime.  Only the fields covered by this version are written.
    if (capabilities->structSize < sizeof(FgdsCapabilities))
        return E_INVALIDARG;
    capabilities->version = FGDS_VERSION_0_2;
    capabilities->nativeApiVersion = FGDS_VERSION_0_2;
    capabilities->backend = FGDS_BACKEND_D3D12;
    capabilities->maxWidth = 16384;
    capabilities->maxHeight = 16384;
    capabilities->requiredResources = FGDS_RESOURCE_CORE;
    capabilities->optionalResources = 0;
    capabilities->featureFlags = FGDS_FEATURE_NATIVE_V2 | FGDS_FEATURE_BIDIRECTIONAL_MOTION |
                                  FGDS_FEATURE_DEPTH_OCCLUSION |
                                  FGDS_FEATURE_OBJECT_ID_REJECTION | FGDS_FEATURE_CAMERA_CUT;
    capabilities->colorFormat = FGDS_FORMAT_RGBA32_FLOAT;
    capabilities->depthFormat = FGDS_FORMAT_R32_FLOAT;
    capabilities->motionFormat = FGDS_FORMAT_RG32_FLOAT;
    capabilities->objectIdFormat = FGDS_FORMAT_R32_UINT;
    for (auto &value : capabilities->reserved)
        value = 0;
    return S_OK;
}

namespace
{
bool validV2Frame(const FgdsFrameV2 &f, UINT width, UINT height)
{
    if (f.structSize < sizeof(FgdsFrameV2) || f.version != FGDS_VERSION_0_2 || f.reserved ||
        f.reserved2 || f.width != width || f.height != height || !f.color || !f.depth ||
        !f.motionToOther || !f.objectId || (f.resourceFlags & ~(
            FGDS_RESOURCE_CORE | FGDS_RESOURCE_HUD_MASK | FGDS_RESOURCE_TRANSPARENCY_MASK)))
        return false;
    if ((f.resourceFlags & FGDS_RESOURCE_CORE) != FGDS_RESOURCE_CORE)
        return false;
    const bool hudResource = (f.resourceFlags & FGDS_RESOURCE_HUD_MASK) != 0;
    const bool transparencyResource = (f.resourceFlags & FGDS_RESOURCE_TRANSPARENCY_MASK) != 0;
    if (hudResource != (f.hudMask != nullptr) ||
        transparencyResource != (f.transparencyMask != nullptr))
        return false;
    if (f.flags & ~FGDS_FRAME_V2_FLAGS)
        return false;
    if (((f.flags & FGDS_FRAME_HUD_MASK) != 0) != hudResource ||
        ((f.flags & FGDS_FRAME_TRANSPARENCY_MASK) != 0) != transparencyResource)
        return false;
    // The reference kernel expects producer motion to include camera motion;
    // jitter is reserved for a future explicit camera reconstruction path.
    if (f.jitterPixels[0] != 0 || f.jitterPixels[1] != 0)
        return false;
    for (float value : f.worldToClip)
        if (!std::isfinite(value))
            return false;
    return true;
}

void copyV2Frame(const FgdsFrameV2 &from, FgdsFrame &to)
{
    to = {};
    to.structSize = sizeof(FgdsFrame);
    to.version = FGDS_VERSION;
    to.width = from.width;
    to.height = from.height;
    to.frameId = from.frameId;
    to.timestampNs = from.timestampNs;
    for (size_t i = 0; i < 16; ++i)
        to.worldToClip[i] = from.worldToClip[i];
    to.jitterPixels[0] = from.jitterPixels[0];
    to.jitterPixels[1] = from.jitterPixels[1];
    to.flags = from.flags & FGDS_CAMERA_CUT;
    to.color = from.color;
    to.depth = from.depth;
    to.motionToOther = from.motionToOther;
    to.objectId = from.objectId;
}
} // namespace

extern "C" HRESULT __cdecl ffgRecordV2(FfgContext *c, ID3D12GraphicsCommandList *list,
                                        const FgdsPairV2 *pair, ID3D12Resource *out)
{
    if (!c || !list || !pair || !out)
        return E_POINTER;
    if (pair->structSize < sizeof(FgdsPairV2) || pair->version != FGDS_VERSION_0_2 ||
        pair->reserved || !std::isfinite(pair->alpha) || pair->alpha < 0 || pair->alpha > 1)
        return E_INVALIDARG;
    const auto &a = pair->frames[0];
    const auto &b = pair->frames[1];
    if (!a.width || !a.height || a.width > 16384 || a.height > 16384 || a.width != b.width ||
        a.height != b.height || a.frameId >= b.frameId || a.timestampNs >= b.timestampNs ||
        !validV2Frame(a, a.width, a.height) || !validV2Frame(b, a.width, a.height))
        return E_INVALIDARG;
    // Parsing an optional resource and then silently ignoring it would produce
    // incorrect HUD/particle edges.  Let the game fall back to v0.1-compatible
    // inputs until the mask-aware kernel is enabled.
    if ((a.resourceFlags | b.resourceFlags) &
        (FGDS_RESOURCE_HUD_MASK | FGDS_RESOURCE_TRANSPARENCY_MASK))
        return E_NOTIMPL;
    FgdsPair legacy{};
    legacy.structSize = sizeof(FgdsPair);
    legacy.version = FGDS_VERSION;
    legacy.alpha = pair->alpha;
    copyV2Frame(a, legacy.frames[0]);
    copyV2Frame(b, legacy.frames[1]);
    return ffgRecord(c, list, &legacy, out);
}

static bool textureValid(FfgContext *c, ID3D12Resource *r, UINT w, UINT h, DXGI_FORMAT format)
{
    if (!r)
        return false;
    auto d = r->GetDesc();
    ComPtr<ID3D12Device> owner;
    if (FAILED(r->GetDevice(IID_PPV_ARGS(&owner))) || owner.Get() != c->device.Get())
        return false;
    return d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && d.Width == w && d.Height == h &&
           d.DepthOrArraySize == 1 && d.MipLevels == 1 && d.SampleDesc.Count == 1 &&
           d.Format == format && !(d.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
}
extern "C" HRESULT __cdecl ffgRecord(FfgContext *c, ID3D12GraphicsCommandList *list,
                                     const FgdsPair *pair, ID3D12Resource *out)
{
    if (!c || !list || !pair || !out)
        return E_POINTER;
    if (pair->structSize != sizeof(FgdsPair) || pair->version != FGDS_VERSION || pair->reserved ||
        !std::isfinite(pair->alpha) || pair->alpha < 0 || pair->alpha > 1)
        return E_INVALIDARG;
    const auto &a = pair->frames[0];
    const auto &b = pair->frames[1];
    if (!a.width || !a.height || a.width > 16384 || a.height > 16384 || a.width != b.width ||
        a.height != b.height || a.frameId >= b.frameId || a.timestampNs >= b.timestampNs)
        return E_INVALIDARG;
    if (list->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT &&
        list->GetType() != D3D12_COMMAND_LIST_TYPE_COMPUTE)
        return E_INVALIDARG;
    ComPtr<ID3D12Device> owner;
    if (FAILED(list->GetDevice(IID_PPV_ARGS(&owner))) || owner.Get() != c->device.Get())
        return E_INVALIDARG;
    DXGI_FORMAT formats[] = {DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32_FLOAT,
                             DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32_UINT};
    ID3D12Resource *resources[8]{};
    for (int side = 0; side < 2; ++side)
    {
        const auto &f = pair->frames[side];
        if (f.structSize != sizeof(FgdsFrame) || f.version != FGDS_VERSION || f.reserved ||
            (f.flags & ~FGDS_CAMERA_CUT) || f.hudMask || f.transparencyMask)
            return E_INVALIDARG;
        // v0 requires unjittered inputs; camera matrices are metadata, not a
        // second camera-motion warp. Producer motion must already include it.
        if (f.jitterPixels[0] != 0 || f.jitterPixels[1] != 0)
            return E_INVALIDARG;
        for (float x : f.worldToClip)
            if (!std::isfinite(x))
                return E_INVALIDARG;
        void *pointers[] = {f.color, f.depth, f.motionToOther, f.objectId};
        for (int k = 0; k < 4; ++k)
        {
            auto *r = static_cast<ID3D12Resource *>(pointers[k]);
            if (!textureValid(c, r, a.width, a.height, formats[k]) || r == out)
                return E_INVALIDARG;
            resources[side * 4 + k] = r;
        }
    }
    if (!textureValid(c, out, a.width, a.height, formats[0]) ||
        !(out->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))
        return E_INVALIDARG;
    auto handle = c->descriptors->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < 8; ++i)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC s{};
        s.Format = formats[i % 4];
        s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Texture2D.MipLevels = 1;
        c->device->CreateShaderResourceView(resources[i], &s, handle);
        handle.ptr += c->stride;
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
    u.Format = formats[0];
    u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    c->device->CreateUnorderedAccessView(out, nullptr, &u, handle);
    ID3D12DescriptorHeap *heaps[] = {c->descriptors.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(c->signature.Get());
    list->SetPipelineState(c->pso.Get());
    list->SetComputeRootDescriptorTable(0, c->descriptors->GetGPUDescriptorHandleForHeapStart());
    struct Constants
    {
        UINT w, h;
        float alpha;
        UINT reset;
    } values{a.width, a.height, pair->alpha, (a.flags | b.flags) & FGDS_CAMERA_CUT};
    list->SetComputeRoot32BitConstants(1, 4, &values, 0);
    list->Dispatch((a.width + 7) / 8, (a.height + 7) / 8, 1);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = out;
    list->ResourceBarrier(1, &barrier);
    return S_OK;
}

extern "C" HRESULT __cdecl ffgCreateV3(ID3D12Device *device, uint32_t slotCount,
                                         FfgContextV3 **output)
{
    if (!output)
        return E_POINTER;
    *output = nullptr;
    if (!device || slotCount == 0 || slotCount > FGDS_V3_MAX_SLOTS)
        return E_INVALIDARG;
    try
    {
        auto result = std::make_unique<FfgContextV3>();
        FfgContext *raw = nullptr;
        HRESULT hr = ffgCreate(device, &raw);
        if (FAILED(hr))
            return hr;
        result->core.reset(raw);
        result->slots.resize(slotCount);
        for (auto &slot : result->slots)
        {
            D3D12_DESCRIPTOR_HEAP_DESC hd{};
            hd.NumDescriptors = 9;
            hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&slot.descriptors)));
        }
        *output = result.release();
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

extern "C" void __cdecl ffgDestroyV3(FfgContextV3 *c)
{
    delete c;
}

extern "C" HRESULT __cdecl ffgGetCapabilitiesV3(FfgContextV3 *c,
                                                   FgdsCapabilitiesV3 *capabilities)
{
    if (!c || !capabilities)
        return E_POINTER;
    if (capabilities->structSize < sizeof(FgdsCapabilitiesV3))
        return E_INVALIDARG;
    capabilities->version = FGDS_VERSION_0_3;
    capabilities->backend = FGDS_BACKEND_D3D12;
    capabilities->slotCount = static_cast<uint32_t>(c->slots.size());
    capabilities->maxSlots = FGDS_V3_MAX_SLOTS;
    capabilities->maxWidth = 16384;
    capabilities->maxHeight = 16384;
    capabilities->requiredResources = FGDS_RESOURCE_CORE;
    capabilities->optionalResources = 0;
    capabilities->featureFlags = FGDS_FEATURE_NATIVE_V2 | FGDS_FEATURE_BIDIRECTIONAL_MOTION |
                                  FGDS_FEATURE_DEPTH_OCCLUSION |
                                  FGDS_FEATURE_OBJECT_ID_REJECTION | FGDS_FEATURE_CAMERA_CUT |
                                  FGDS_FEATURE_MULTI_FLIGHT | FGDS_FEATURE_HDR_METADATA |
                                  FGDS_FEATURE_SHARED_RESOURCES;
    capabilities->colorFormat = FGDS_FORMAT_RGBA32_FLOAT;
    capabilities->depthFormat = FGDS_FORMAT_R32_FLOAT;
    capabilities->motionFormat = FGDS_FORMAT_RG32_FLOAT;
    capabilities->objectIdFormat = FGDS_FORMAT_R32_UINT;
    for (auto &value : capabilities->reserved)
        value = 0;
    return S_OK;
}

namespace
{
HRESULT validateV3Pair(FfgContext *c, ID3D12GraphicsCommandList *list,
                       const FgdsPairV2 *pair, ID3D12Resource *out)
{
    if (!c || !list || !pair || !out)
        return E_POINTER;
    if (pair->structSize < sizeof(FgdsPairV2) || pair->version != FGDS_VERSION_0_2 ||
        pair->reserved || !std::isfinite(pair->alpha) || pair->alpha < 0 || pair->alpha > 1)
        return E_INVALIDARG;
    const auto &a = pair->frames[0];
    const auto &b = pair->frames[1];
    if (!a.width || !a.height || a.width > 16384 || a.height > 16384 || a.width != b.width ||
        a.height != b.height || a.frameId >= b.frameId || a.timestampNs >= b.timestampNs ||
        !validV2Frame(a, a.width, a.height) || !validV2Frame(b, a.width, a.height))
        return E_INVALIDARG;
    if ((a.resourceFlags | b.resourceFlags) &
        (FGDS_RESOURCE_HUD_MASK | FGDS_RESOURCE_TRANSPARENCY_MASK))
        return E_NOTIMPL;
    if (list->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT &&
        list->GetType() != D3D12_COMMAND_LIST_TYPE_COMPUTE)
        return E_INVALIDARG;
    ComPtr<ID3D12Device> owner;
    if (FAILED(list->GetDevice(IID_PPV_ARGS(&owner))) || owner.Get() != c->device.Get())
        return E_INVALIDARG;
    const DXGI_FORMAT formats[] = {DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32_FLOAT,
                                   DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32_UINT};
    for (int side = 0; side < 2; ++side)
    {
        const auto &f = pair->frames[side];
        void *pointers[] = {f.color, f.depth, f.motionToOther, f.objectId};
        for (int k = 0; k < 4; ++k)
        {
            auto *r = static_cast<ID3D12Resource *>(pointers[k]);
            if (!textureValid(c, r, a.width, a.height, formats[k]) || r == out)
                return E_INVALIDARG;
        }
    }
    if (!textureValid(c, out, a.width, a.height, formats[0]) ||
        !(out->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))
        return E_INVALIDARG;
    return S_OK;
}
} // namespace

extern "C" HRESULT __cdecl ffgRecordV3(FfgContextV3 *c, ID3D12GraphicsCommandList *list,
                                         const FgdsPairV2 *pair, ID3D12Resource *out,
                                         uint32_t slotIndex, ID3D12Fence *completionFence,
                                         uint64_t completionValue)
{
    if (!c || !list || !pair || !out)
        return E_POINTER;
    if (slotIndex >= c->slots.size())
        return E_INVALIDARG;
    if (!completionFence || completionValue == 0)
        return E_INVALIDARG;
    auto &slot = c->slots[slotIndex];
    if (slot.retireFence && slot.retireFence->GetCompletedValue() < slot.retireValue)
        return DXGI_ERROR_WAS_STILL_DRAWING;
    // A completed slot is no longer referenced by any in-flight command
    // list.  Release imports before writing the next descriptor set.
    if (slot.retireFence)
    {
        slot.sharedResources.clear();
        slot.sharedFences.clear();
    }
    if (completionFence)
    {
        ComPtr<ID3D12Device> owner;
        if (FAILED(completionFence->GetDevice(IID_PPV_ARGS(&owner))) ||
            owner.Get() != c->core->device.Get())
            return E_INVALIDARG;
        // A value that is already complete cannot represent this future
        // dispatch; signalling it after submission would be a no-op.
        if (completionFence->GetCompletedValue() >= completionValue)
            return E_INVALIDARG;
    }
    HRESULT hr = validateV3Pair(c->core.get(), list, pair, out);
    if (FAILED(hr))
        return hr;

    const auto &a = pair->frames[0];
    const auto &b = pair->frames[1];
    const DXGI_FORMAT formats[] = {DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32_FLOAT,
                                   DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32_UINT};
    void *pointers[8] = {a.color, a.depth, a.motionToOther, a.objectId,
                         b.color, b.depth, b.motionToOther, b.objectId};
    auto handle = slot.descriptors->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < 8; ++i)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC s{};
        s.Format = formats[i % 4];
        s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Texture2D.MipLevels = 1;
        c->core->device->CreateShaderResourceView(static_cast<ID3D12Resource *>(pointers[i]), &s,
                                                   handle);
        handle.ptr += c->core->stride;
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
    u.Format = formats[0];
    u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    c->core->device->CreateUnorderedAccessView(out, nullptr, &u, handle);
    ID3D12DescriptorHeap *heaps[] = {slot.descriptors.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(c->core->signature.Get());
    list->SetPipelineState(c->core->pso.Get());
    list->SetComputeRootDescriptorTable(0, slot.descriptors->GetGPUDescriptorHandleForHeapStart());
    struct Constants
    {
        UINT w, h;
        float alpha;
        UINT reset;
    } values{a.width, a.height, pair->alpha, (a.flags | b.flags) & FGDS_CAMERA_CUT};
    list->SetComputeRoot32BitConstants(1, 4, &values, 0);
    list->Dispatch((a.width + 7) / 8, (a.height + 7) / 8, 1);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = out;
    list->ResourceBarrier(1, &barrier);
    // Publish the retirement token only after all validation and command
    // recording succeeded. The host must signal this value after submission.
    slot.retireFence = completionFence;
    slot.retireValue = completionValue;
    return S_OK;
}

namespace
{
bool allZero(const uint32_t *values, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (values[i] != 0)
            return false;
    return true;
}

bool validSharedHandleType(uint32_t type)
{
    // ID3D12Device::OpenSharedHandle accepts Win32 and NT handles.  Opaque FD
    // is reserved for the Vulkan transport and must not reach this entry.
    return type == FGDS_SHARED_HANDLE_WIN32 || type == FGDS_SHARED_HANDLE_NT;
}

uint64_t deviceAdapterLuid(ID3D12Device *device)
{
    const LUID luid = device->GetAdapterLuid();
    return static_cast<uint64_t>(static_cast<uint32_t>(luid.LowPart)) |
           (static_cast<uint64_t>(static_cast<uint32_t>(luid.HighPart)) << 32);
}

bool validHdr(const FgdsHdrMetadata &metadata)
{
    if (metadata.structSize < sizeof(FgdsHdrMetadata) ||
        metadata.version != FGDS_HDR_VERSION_1 || metadata.reserved0 != 0 ||
        (metadata.flags & ~FGDS_HDR_FLAGS) != 0 ||
        !allZero(metadata.reserved, 4))
        return false;
    if (metadata.colorSpace > FGDS_HDR_COLOR_SPACE_HDR10_HLG ||
        metadata.transferFunction < FGDS_HDR_TRANSFER_SRGB ||
        metadata.transferFunction > FGDS_HDR_TRANSFER_HLG)
        return false;
    for (float value : metadata.primaries)
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
            return false;
    for (float value : metadata.whitePoint)
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
            return false;
    const float luminances[] = {metadata.maxMasteringLuminanceNits,
                                metadata.minMasteringLuminanceNits,
                                metadata.maxContentLightLevelNits,
                                metadata.maxFrameAverageLightLevelNits,
                                metadata.nominalPeakLuminanceNits};
    for (float value : luminances)
        if (!std::isfinite(value) || value < 0.0f)
            return false;
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

bool equalHdr(const FgdsHdrMetadata &a, const FgdsHdrMetadata &b)
{
    if (a.colorSpace != b.colorSpace || a.transferFunction != b.transferFunction ||
        a.flags != b.flags || a.maxMasteringLuminanceNits != b.maxMasteringLuminanceNits ||
        a.minMasteringLuminanceNits != b.minMasteringLuminanceNits ||
        a.maxContentLightLevelNits != b.maxContentLightLevelNits ||
        a.maxFrameAverageLightLevelNits != b.maxFrameAverageLightLevelNits ||
        a.nominalPeakLuminanceNits != b.nominalPeakLuminanceNits)
        return false;
    for (size_t i = 0; i < 6; ++i)
        if (a.primaries[i] != b.primaries[i])
            return false;
    for (size_t i = 0; i < 2; ++i)
        if (a.whitePoint[i] != b.whitePoint[i])
            return false;
    return true;
}

HRESULT validateSharedImage(const FgdsSharedImage &image, uint32_t format,
                            uint32_t width, uint32_t height, uint32_t requiredFlags,
                            uint64_t expectedLuid)
{
    if (image.structSize < sizeof(FgdsSharedImage) || image.version != FGDS_SHARED_VERSION_1 ||
        image.backend != FGDS_SHARED_BACKEND_D3D12 || image.format != format ||
        image.width != width || image.height != height || image.arrayLayers != 1 ||
        image.mipLevels != 1 || image.sampleCount != 1 ||
        (image.flags & ~(FGDS_SHARED_IMAGE_SHADER_RESOURCE |
                         FGDS_SHARED_IMAGE_UNORDERED_ACCESS |
                         FGDS_SHARED_IMAGE_IMPORTED)) != 0 ||
        (image.flags & requiredFlags) != requiredFlags ||
        !validSharedHandleType(image.handleType) || image.reserved0 != 0 ||
        image.adapterLuid == 0 || image.adapterLuid != expectedLuid ||
        image.resourceHandle == 0 || image.auxHandle != 0 ||
        !allZero(image.reserved, 4))
        return E_INVALIDARG;
    return S_OK;
}

HRESULT validateSharedSync(const FgdsSharedSync &sync, uint64_t expectedLuid)
{
    if (sync.structSize < sizeof(FgdsSharedSync) || sync.version != FGDS_SHARED_VERSION_1 ||
        sync.backend != FGDS_SHARED_BACKEND_D3D12 ||
        sync.type != FGDS_SHARED_SYNC_D3D12_FENCE || !validSharedHandleType(sync.handleType) ||
        sync.flags != 0 || sync.reserved0 != 0 || sync.adapterLuid == 0 ||
        sync.adapterLuid != expectedLuid || sync.fenceHandle == 0 || sync.value == 0 ||
        !allZero(sync.reserved, 4))
        return E_INVALIDARG;
    return S_OK;
}

HRESULT validateSharedFrame(const FgdsSharedFrame &frame, uint64_t expectedLuid,
                            uint32_t expectedWidth, uint32_t expectedHeight)
{
    if (frame.structSize < sizeof(FgdsSharedFrame) || frame.version != FGDS_SHARED_VERSION_1 ||
        frame.width != expectedWidth || frame.height != expectedHeight || frame.flags & ~FGDS_FRAME_V2_FLAGS ||
        frame.reserved[0] || frame.reserved[1] || frame.reserved[2] || frame.reserved[3] ||
        frame.resourceFlags & ~(FGDS_RESOURCE_CORE | FGDS_RESOURCE_HUD_MASK |
                                FGDS_RESOURCE_TRANSPARENCY_MASK))
        return E_INVALIDARG;
    if ((frame.resourceFlags & FGDS_RESOURCE_CORE) != FGDS_RESOURCE_CORE)
        return E_INVALIDARG;
    if (frame.resourceFlags & (FGDS_RESOURCE_HUD_MASK | FGDS_RESOURCE_TRANSPARENCY_MASK))
        return E_NOTIMPL;
    if (frame.jitterPixels[0] != 0.0f || frame.jitterPixels[1] != 0.0f)
        return E_INVALIDARG;
    for (float value : frame.worldToClip)
        if (!std::isfinite(value))
            return E_INVALIDARG;
    if (!validHdr(frame.hdr))
        return E_INVALIDARG;
    HRESULT hr = validateSharedImage(frame.color, FGDS_FORMAT_RGBA32_FLOAT, expectedWidth,
                                     expectedHeight, FGDS_SHARED_IMAGE_SHADER_RESOURCE,
                                     expectedLuid);
    if (FAILED(hr))
        return hr;
    hr = validateSharedImage(frame.depth, FGDS_FORMAT_R32_FLOAT, expectedWidth, expectedHeight,
                             FGDS_SHARED_IMAGE_SHADER_RESOURCE, expectedLuid);
    if (FAILED(hr))
        return hr;
    hr = validateSharedImage(frame.motionToOther, FGDS_FORMAT_RG32_FLOAT, expectedWidth,
                             expectedHeight, FGDS_SHARED_IMAGE_SHADER_RESOURCE, expectedLuid);
    if (FAILED(hr))
        return hr;
    hr = validateSharedImage(frame.objectId, FGDS_FORMAT_R32_UINT, expectedWidth, expectedHeight,
                             FGDS_SHARED_IMAGE_SHADER_RESOURCE, expectedLuid);
    if (FAILED(hr))
        return hr;
    return validateSharedSync(frame.ready, expectedLuid);
}

void copySharedFrame(const FgdsSharedFrame &from, FgdsFrameV2 &to,
                     ID3D12Resource *color, ID3D12Resource *depth,
                     ID3D12Resource *motion, ID3D12Resource *objectId)
{
    to = {};
    to.structSize = sizeof(FgdsFrameV2);
    to.version = FGDS_VERSION_0_2;
    to.width = from.width;
    to.height = from.height;
    to.frameId = from.frameId;
    to.timestampNs = from.timestampNs;
    for (size_t i = 0; i < 16; ++i)
        to.worldToClip[i] = from.worldToClip[i];
    to.jitterPixels[0] = from.jitterPixels[0];
    to.jitterPixels[1] = from.jitterPixels[1];
    to.flags = from.flags;
    to.color = color;
    to.depth = depth;
    to.motionToOther = motion;
    to.objectId = objectId;
    to.resourceFlags = FGDS_RESOURCE_CORE;
}

HRESULT openSharedResource(ID3D12Device *device, const FgdsSharedImage &image,
                           ComPtr<ID3D12Resource> &resource)
{
    if (sizeof(uintptr_t) < sizeof(uint64_t) && (image.resourceHandle >> 32) != 0)
        return E_INVALIDARG;
    HANDLE handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(image.resourceHandle));
    return device->OpenSharedHandle(handle, IID_PPV_ARGS(&resource));
}

HRESULT openSharedFence(ID3D12Device *device, const FgdsSharedSync &sync,
                        ComPtr<ID3D12Fence> &fence)
{
    if (sizeof(uintptr_t) < sizeof(uint64_t) && (sync.fenceHandle >> 32) != 0)
        return E_INVALIDARG;
    HANDLE handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(sync.fenceHandle));
    return device->OpenSharedHandle(handle, IID_PPV_ARGS(&fence));
}
} // namespace

extern "C" HRESULT __cdecl ffgValidateHdrMetadata(const FgdsHdrMetadata *metadata)
{
    if (!metadata)
        return E_POINTER;
    return validHdr(*metadata) ? S_OK : E_INVALIDARG;
}

extern "C" HRESULT __cdecl ffgValidateSharedPairV1(const FgdsSharedPair *pair)
{
    if (!pair)
        return E_POINTER;
    if (pair->structSize < sizeof(FgdsSharedPair) || pair->version != FGDS_SHARED_VERSION_1 ||
        pair->reserved0 != 0 || !std::isfinite(pair->alpha) || pair->alpha < 0.0f ||
        pair->alpha > 1.0f || !allZero(pair->reserved, 4))
        return E_INVALIDARG;
    const auto &a = pair->frames[0];
    const auto &b = pair->frames[1];
    if (!a.width || !a.height || a.width > 16384 || a.height > 16384 ||
        a.width != b.width || a.height != b.height || a.frameId >= b.frameId ||
        a.timestampNs >= b.timestampNs)
        return E_INVALIDARG;
    if (a.color.adapterLuid == 0)
        return E_INVALIDARG;
    const uint64_t luid = a.color.adapterLuid;
    HRESULT hr = validateSharedFrame(a, luid, a.width, a.height);
    if (FAILED(hr))
        return hr;
    hr = validateSharedFrame(b, luid, a.width, a.height);
    if (FAILED(hr))
        return hr;
    if (!equalHdr(a.hdr, b.hdr) || !validHdr(pair->outputHdr) ||
        !equalHdr(a.hdr, pair->outputHdr))
        return E_INVALIDARG;
    hr = validateSharedImage(pair->output, FGDS_FORMAT_RGBA32_FLOAT, a.width, a.height,
                             FGDS_SHARED_IMAGE_UNORDERED_ACCESS, luid);
    if (FAILED(hr))
        return hr;
    return validateSharedSync(pair->retire, luid);
}

extern "C" HRESULT __cdecl ffgRecordSharedV1(FfgContextV3 *context,
                                               ID3D12CommandQueue *queue,
                                               ID3D12GraphicsCommandList *list,
                                               const FgdsSharedPair *pair,
                                               uint32_t slotIndex)
{
    if (!context || !queue || !list || !pair)
        return E_POINTER;
    HRESULT hr = ffgValidateSharedPairV1(pair);
    if (FAILED(hr))
        return hr;
    if (slotIndex >= context->slots.size())
        return E_INVALIDARG;
    // Check slot retirement before inserting any producer waits.  A busy slot
    // must be side-effect free so a caller can retry after the shared retire
    // fence advances without accumulating duplicate queue waits.
    auto &slot = context->slots[slotIndex];
    if (slot.retireFence && slot.retireFence->GetCompletedValue() < slot.retireValue)
        return DXGI_ERROR_WAS_STILL_DRAWING;
    ComPtr<ID3D12Device> queueDevice;
    if (FAILED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) ||
        queueDevice.Get() != context->core->device.Get())
        return E_INVALIDARG;
    if (pair->output.adapterLuid != deviceAdapterLuid(context->core->device.Get()))
        return E_INVALIDARG;

    std::array<ComPtr<ID3D12Resource>, 8> resources;
    std::array<ComPtr<ID3D12Fence>, 3> fences;
    const FgdsSharedFrame *frames[2] = {&pair->frames[0], &pair->frames[1]};
    const FgdsSharedImage *images[8] = {
        &frames[0]->color, &frames[0]->depth, &frames[0]->motionToOther, &frames[0]->objectId,
        &frames[1]->color, &frames[1]->depth, &frames[1]->motionToOther, &frames[1]->objectId};
    for (size_t i = 0; i < resources.size(); ++i)
    {
        hr = openSharedResource(context->core->device.Get(), *images[i], resources[i]);
        if (FAILED(hr))
            return hr;
        const DXGI_FORMAT formats[] = {DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32_FLOAT,
                                       DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32_UINT};
        const auto &frame = *frames[i / 4];
        if (!textureValid(context->core.get(), resources[i].Get(), frame.width, frame.height,
                          formats[i % 4]))
            return E_INVALIDARG;
    }
    ComPtr<ID3D12Resource> output;
    hr = openSharedResource(context->core->device.Get(), pair->output, output);
    if (FAILED(hr))
        return hr;
    if (!textureValid(context->core.get(), output.Get(), pair->output.width,
                      pair->output.height, DXGI_FORMAT_R32G32B32A32_FLOAT) ||
        !(output->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))
        return E_INVALIDARG;

    hr = openSharedFence(context->core->device.Get(), pair->frames[0].ready, fences[0]);
    if (FAILED(hr))
        return hr;
    hr = openSharedFence(context->core->device.Get(), pair->frames[1].ready, fences[1]);
    if (FAILED(hr))
        return hr;
    hr = openSharedFence(context->core->device.Get(), pair->retire, fences[2]);
    if (FAILED(hr))
        return hr;
    for (size_t i = 0; i < 2; ++i)
    {
        ComPtr<ID3D12Device> owner;
        if (FAILED(fences[i]->GetDevice(IID_PPV_ARGS(&owner))) ||
            owner.Get() != context->core->device.Get())
            return E_INVALIDARG;
    }
    ComPtr<ID3D12Device> retireOwner;
    if (FAILED(fences[2]->GetDevice(IID_PPV_ARGS(&retireOwner))) ||
        retireOwner.Get() != context->core->device.Get())
        return E_INVALIDARG;

    FgdsPairV2 native{};
    native.structSize = sizeof(FgdsPairV2);
    native.version = FGDS_VERSION_0_2;
    native.alpha = pair->alpha;
    copySharedFrame(pair->frames[0], native.frames[0], resources[0].Get(), resources[1].Get(),
                    resources[2].Get(), resources[3].Get());
    copySharedFrame(pair->frames[1], native.frames[1], resources[4].Get(), resources[5].Get(),
                    resources[6].Get(), resources[7].Get());
    // Queue waits are inserted before the host submits the recorded command
    // list.  They establish producer -> compute ordering without a CPU wait.
    hr = queue->Wait(fences[0].Get(), pair->frames[0].ready.value);
    if (FAILED(hr))
        return hr;
    hr = queue->Wait(fences[1].Get(), pair->frames[1].ready.value);
    if (FAILED(hr))
        return hr;
    hr = ffgRecordV3(context, list, &native, output.Get(), slotIndex, fences[2].Get(),
                     pair->retire.value);
    if (FAILED(hr))
        return hr;
    slot.sharedResources.assign(resources.begin(), resources.end());
    slot.sharedResources.push_back(output);
    slot.sharedFences.assign(fences.begin(), fences.end());
    return S_OK;
}
