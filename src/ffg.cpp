#include "ffg/ffg.h"
#include "d3d12_host.hpp"
#include "ffg_shader.hpp"
#include <memory>
struct FfgContext
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12DescriptorHeap> descriptors;
    ComPtr<ID3D12RootSignature> signature;
    ComPtr<ID3D12PipelineState> pso;
    UINT stride = 0;
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
