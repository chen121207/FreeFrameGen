#pragma once
#include "d3d12_host.hpp"
#include "fgds/fgds.h"
// Original synthetic raster fixture, not L4D2 extraction.
struct SceneFrame
{
    Texture color, depth, motion, id, z;
    ComPtr<ID3D12DescriptorHeap> rtv, dsv;
    FgdsFrame describe(uint64_t frameId, uint64_t ns) const
    {
        FgdsFrame f{};
        f.structSize = sizeof f;
        f.version = FGDS_VERSION;
        f.width = Gpu::W;
        f.height = Gpu::H;
        f.frameId = frameId;
        f.timestampNs = ns;
        f.worldToClip[0] = f.worldToClip[5] = f.worldToClip[15] = 1;
        f.worldToClip[10] = 1.f / 11.f; // matches the raster shader's depth/11
        f.color = color.r.Get();
        f.depth = depth.r.Get();
        f.motionToOther = motion.r.Get();
        f.objectId = id.r.Get();
        return f;
    }
};
struct DrawRect
{
    float center[2], halfSize[2], color[4], motion[2], depth;
    uint32_t objectId;
};
static_assert(sizeof(DrawRect) == 48);
struct ScenePacket
{
    std::array<DrawRect, 3> draws;
};
inline ScenePacket demoPacket(float t, float other)
{
    ScenePacket s{};
    s.draws[0] = {{0, 0}, {1, 1}, {0.08f, 0.11f, 0.16f, 1}, {0, 0}, 10, 1};
    s.draws[1] = {{-0.35f + t * 0.3f, 0},
                  {0.18f, 0.35f},
                  {0.8f, 0.12f, 0.04f, 1},
                  {(other - t) * 0.3f * Gpu::W / 2, 0},
                  2,
                  2};
    s.draws[2] = {{0.35f - t * 0.2f, 0.18f},
                  {0.12f, 0.22f},
                  {0.05f, 0.65f, 0.18f, 1},
                  {(other - t) * -0.2f * Gpu::W / 2, 0},
                  1,
                  3};
    return s;
}
struct ISceneRenderer
{
    virtual ~ISceneRenderer() = default;
    virtual SceneFrame createFrame() = 0;
    virtual void record(const ScenePacket &, SceneFrame &) = 0;
};
class D3D12SceneRenderer final : public ISceneRenderer
{
    Gpu &gpu;
    ComPtr<ID3D12RootSignature> signature;
    ComPtr<ID3D12PipelineState> pso;

  public:
    explicit D3D12SceneRenderer(Gpu &g) : gpu(g)
    {
        const char *shader = R"(
cbuffer Draw:register(b0){float2 center;float2 halfSize;float4 color;float2 motion;float depth;uint objectId;}
struct Vertex{float4 pos:SV_Position;};
Vertex vs(uint id:SV_VertexID){
    const float2 corner[6]={float2(-1,-1),float2(-1,1),float2(1,-1),float2(1,-1),float2(-1,1),float2(1,1)};
    Vertex o;o.pos=float4(center+corner[id]*halfSize,depth/11.0,1);return o;
}
struct Pixel {float4 color:SV_Target0;float depth:SV_Target1;float2 motion:SV_Target2;uint objectId:SV_Target3;};
Pixel ps(Vertex i){Pixel o;o.color=color;
    if(objectId==1)o.color.rgb*=1+0.4*((uint(i.pos.x)/24+uint(i.pos.y)/24)%2);
    o.depth=depth;o.motion=motion;o.objectId=objectId;return o;
})";
        auto vs = compile(shader, "vs", "vs_5_1"), ps = compile(shader, "ps", "ps_5_1");
        D3D12_ROOT_PARAMETER param{};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        param.Constants = {0, 0, 12};
        D3D12_ROOT_SIGNATURE_DESC rs{1, &param, 0, nullptr,
                                     D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        signature = root(g.device.Get(), rs);
        D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
        p.pRootSignature = signature.Get();
        p.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        p.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        p.RasterizerState.DepthClipEnable = TRUE;
        p.SampleMask = UINT_MAX;
        p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        p.NumRenderTargets = 4;
        p.SampleDesc.Count = 1;
        p.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
        p.RTVFormats[1] = DXGI_FORMAT_R32_FLOAT;
        p.RTVFormats[2] = DXGI_FORMAT_R32G32_FLOAT;
        p.RTVFormats[3] = DXGI_FORMAT_R32_UINT;
        p.BlendState.IndependentBlendEnable = TRUE;
        for (auto &b : p.BlendState.RenderTarget)
            b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        p.DepthStencilState.DepthEnable = TRUE;
        p.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        p.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        p.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        check(g.device->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&pso)));
    }
    SceneFrame createFrame() override
    {
        SceneFrame f;
        auto &d = gpu.device;
        f.color =
            gpu.texture(DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        f.depth = gpu.texture(DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        f.motion = gpu.texture(DXGI_FORMAT_R32G32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        f.id = gpu.texture(DXGI_FORMAT_R32_UINT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        f.z = gpu.texture(DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        D3D12_DESCRIPTOR_HEAP_DESC h{};
        h.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        h.NumDescriptors = 4;
        check(d->CreateDescriptorHeap(&h, IID_PPV_ARGS(&f.rtv)));
        auto handle = f.rtv->GetCPUDescriptorHandleForHeapStart();
        for (auto *t : {&f.color, &f.depth, &f.motion, &f.id})
        {
            d->CreateRenderTargetView(t->r.Get(), nullptr, handle);
            handle.ptr += d->GetDescriptorHandleIncrementSize(h.Type);
        }
        h.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        h.NumDescriptors = 1;
        check(d->CreateDescriptorHeap(&h, IID_PPV_ARGS(&f.dsv)));
        d->CreateDepthStencilView(f.z.r.Get(), nullptr,
                                  f.dsv->GetCPUDescriptorHandleForHeapStart());
        return f;
    }
    void record(const ScenePacket &scene, SceneFrame &f) override
    {
        auto *c = gpu.cmd.Get();
        for (auto *t : {&f.color, &f.depth, &f.motion, &f.id})
            t->to(c, D3D12_RESOURCE_STATE_RENDER_TARGET);
        f.z.to(c, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        auto rtv = f.rtv->GetCPUDescriptorHandleForHeapStart(),
             dsv = f.dsv->GetCPUDescriptorHandleForHeapStart();
        const float zero[4] = {};
        auto h = rtv;
        for (UINT i = 0; i < 4; ++i)
        {
            c->ClearRenderTargetView(h, zero, 0, nullptr);
            h.ptr += gpu.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        }
        c->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
        c->OMSetRenderTargets(4, &rtv, TRUE, &dsv);
        D3D12_VIEWPORT v{0, 0, float(Gpu::W), float(Gpu::H), 0, 1};
        D3D12_RECT sc{0, 0, LONG(Gpu::W), LONG(Gpu::H)};
        c->RSSetViewports(1, &v);
        c->RSSetScissorRects(1, &sc);
        c->SetGraphicsRootSignature(signature.Get());
        c->SetPipelineState(pso.Get());
        c->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        for (const auto &draw : scene.draws)
        {
            c->SetGraphicsRoot32BitConstants(0, 12, &draw, 0);
            c->DrawInstanced(6, 1, 0, 0);
        }
        for (auto *t : {&f.color, &f.depth, &f.motion, &f.id})
            t->to(c, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
};
