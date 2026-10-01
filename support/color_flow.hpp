#pragma once
#include "d3d12_host.hpp"
#include "capture_shader.hpp"

// Single-flight GPU block matching + bidirectional color warp. Independent of FGDS.
struct ColorFlow
{
    Gpu &g;
    ComPtr<ID3D12DescriptorHeap> heap;
    ComPtr<ID3D12RootSignature> signature;
    ComPtr<ID3D12PipelineState> estimatePso, interpolatePso, convertPso, downsamplePso;
    Texture forward, backward, output, coarseA, coarseB;
    UINT stride;
    explicit ColorFlow(Gpu &gpu) : g(gpu)
    {
        output =
            g.texture(DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        auto w = g.width, h = g.height;
        g.width = (w + 7) / 8;
        g.height = (h + 7) / 8;
        forward =
            g.texture(DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        backward =
            g.texture(DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        g.width = (w + 3) / 4;
        g.height = (h + 3) / 4;
        coarseA =
            g.texture(DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        coarseB =
            g.texture(DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        g.width = w;
        g.height = h;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = 42;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(g.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)));
        stride = g.device->GetDescriptorHandleIncrementSize(hd.Type);
        D3D12_DESCRIPTOR_RANGE ranges[] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6, 0, 0, 0},
                                           {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 6}};
        D3D12_ROOT_PARAMETER p[2]{};
        p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        p[0].DescriptorTable = {2, ranges};
        p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        p[1].Constants = {0, 0, 4};
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rs{2, p, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        signature = root(g.device.Get(), rs);
        auto make = [&](const char *name, ComPtr<ID3D12PipelineState> &pso) {
            auto b = compile(CAPTURE_SHADER, name, "cs_5_1");
            D3D12_COMPUTE_PIPELINE_STATE_DESC d{};
            d.pRootSignature = signature.Get();
            d.CS = {b->GetBufferPointer(), b->GetBufferSize()};
            check(g.device->CreateComputePipelineState(&d, IID_PPV_ARGS(&pso)));
        };
        make("estimate", estimatePso);
        make("interpolate", interpolatePso);
        make("convert", convertPso);
        make("downsample", downsamplePso);
    }
    void dispatch(UINT slot, Texture &a, Texture &b, Texture &dest, ID3D12PipelineState *pso,
                  UINT mode, float alpha, UINT dispatchW, UINT dispatchH, bool srgb = false)
    {
        auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += size_t(slot) * 7 * stride;
        Texture *src[] = {&a, &b, &forward, &backward, &coarseA, &coarseB};
        for (auto *t : src)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC d{};
            d.Format = t->r->GetDesc().Format;
            if (srgb && t == &a)
                d.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            d.Texture2D.MipLevels = 1;
            // Unused flow SRVs must not alias the active UAV in the descriptor table.
            g.device->CreateShaderResourceView(t == &dest ? nullptr : t->r.Get(), &d, cpu);
            cpu.ptr += stride;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
        u.Format = dest.r->GetDesc().Format;
        u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        g.device->CreateUnorderedAccessView(dest.r.Get(), nullptr, &u, cpu);
        dest.to(g.cmd.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12DescriptorHeap *heaps[] = {heap.Get()};
        g.cmd->SetDescriptorHeaps(1, heaps);
        g.cmd->SetComputeRootSignature(signature.Get());
        g.cmd->SetPipelineState(pso);
        auto gpu = heap->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += UINT64(slot) * 7 * stride;
        g.cmd->SetComputeRootDescriptorTable(0, gpu);
        struct
        {
            UINT w, h, mode;
            float alpha;
        } params{g.width, g.height, mode, alpha};
        g.cmd->SetComputeRoot32BitConstants(1, 4, &params, 0);
        g.cmd->Dispatch((dispatchW + 7) / 8, (dispatchH + 7) / 8, 1);
        dest.to(g.cmd.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    // Record one colour conversion into the command list owned by the caller.
    // The source is returned to COMMON before the command list is submitted so
    // that the D3D11 capture producer can acquire the shared texture again as
    // soon as the queue fence for this submission completes.  Keeping this as
    // a recording helper lets capture combine conversion and interpolation in
    // one queue submission (and therefore one CPU fence wait).
    void convertRecorded(Texture &src, Texture &dst)
    {
        src.to(g.cmd.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        dispatch(3, src, src, dst, convertPso.Get(), 0, 0, g.width, g.height, true);
        src.to(g.cmd.Get(), D3D12_RESOURCE_STATE_COMMON);
    }

    // Record motion estimation and the bidirectional warp without beginning or
    // submitting a command list.  a/b must remain valid until the enclosing
    // queue submission completes; callers that need to reuse them must retain
    // the existing Gpu::submit CPU wait or use a per-slot fence.
    void generateRecorded(Texture &a, Texture &b, float alpha = .5f)
    {
        a.to(g.cmd.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        b.to(g.cmd.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        dispatch(4, a, b, coarseA, downsamplePso.Get(), 0, alpha, (g.width + 3) / 4,
                 (g.height + 3) / 4);
        dispatch(5, b, a, coarseB, downsamplePso.Get(), 0, alpha, (g.width + 3) / 4,
                 (g.height + 3) / 4);
        dispatch(0, a, b, forward, estimatePso.Get(), 0, alpha, (g.width + 7) / 8,
                 (g.height + 7) / 8);
        dispatch(1, a, b, backward, estimatePso.Get(), 1, alpha, (g.width + 7) / 8,
                 (g.height + 7) / 8);
        dispatch(2, a, b, output, interpolatePso.Get(), 0, alpha, g.width, g.height);
    }

    void convert(Texture &src, Texture &dst)
    {
        g.begin();
        convertRecorded(src, dst);
        g.submit();
    }

    void generate(Texture &a, Texture &b, float alpha = .5f)
    {
        g.begin();
        generateRecorded(a, b, alpha);
        g.submit();
    }

    // Capture path fast path: desktop-copy conversion and (when a contiguous
    // pair exists) interpolation are recorded back-to-back before a single
    // ExecuteCommandLists/CPU fence wait.  This removes one full GPU drain per
    // real frame while preserving the single-flight ownership contract: the
    // caller still waits in Gpu::submit before acquiring the next capture
    // frame, so D3D11 cannot overwrite shared12 while D3D12 reads it.
    void convertAndGenerate(Texture &src, Texture &dst, Texture *a = nullptr,
                            Texture *b = nullptr, float alpha = .5f)
    {
        g.begin();
        convertRecorded(src, dst);
        if (a && b)
            generateRecorded(*a, *b, alpha);
        g.submit();
    }
};
