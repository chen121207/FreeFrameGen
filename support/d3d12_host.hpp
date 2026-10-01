#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <string>
#include <stdexcept>
#include <fstream>
#include <iostream>
#include <cmath>
#include <cstring>
#include <algorithm>
using Microsoft::WRL::ComPtr;
inline void check(HRESULT h)
{
    if (FAILED(h))
    {
        char b[64];
        sprintf_s(b, "HRESULT 0x%08X", unsigned(h));
        throw std::runtime_error(b);
    }
}
inline ComPtr<ID3DBlob> compile(const char *s, const char *entry, const char *target)
{
    ComPtr<ID3DBlob> b, e;
    HRESULT h = D3DCompile(s, strlen(s), nullptr, nullptr, nullptr, entry, target,
                           D3DCOMPILE_ENABLE_STRICTNESS, 0, &b, &e);
    if (FAILED(h))
        throw std::runtime_error(e ? (char *)e->GetBufferPointer() : "shader compilation failed");
    return b;
}
inline ComPtr<ID3D12RootSignature> root(ID3D12Device *d, const D3D12_ROOT_SIGNATURE_DESC &desc)
{
    ComPtr<ID3DBlob> b, e;
    check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &b, &e));
    ComPtr<ID3D12RootSignature> r;
    check(d->CreateRootSignature(0, b->GetBufferPointer(), b->GetBufferSize(), IID_PPV_ARGS(&r)));
    return r;
}
inline void barrier(ID3D12GraphicsCommandList *c, ID3D12Resource *r, D3D12_RESOURCE_STATES a,
                    D3D12_RESOURCE_STATES b)
{
    if (a == b)
        return;
    D3D12_RESOURCE_BARRIER v{};
    v.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    v.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, a, b};
    c->ResourceBarrier(1, &v);
}
struct Texture
{
    ComPtr<ID3D12Resource> r;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    void to(ID3D12GraphicsCommandList *c, D3D12_RESOURCE_STATES s)
    {
        barrier(c, r.Get(), state, s);
        state = s;
    }
};
// FFG owns a separate presentation swap chain. Borderless is the safe
// full-screen replacement path: it covers the selected monitor without
// touching the game's process or swap chain. Exclusive SetFullscreenState is
// intentionally not used because it is session/driver dependent and can make
// Desktop Duplication lose access.
enum class PresentMode
{
    Windowed,
    Borderless,
};
inline LRESULT CALLBACK windowProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_CLOSE)
    {
        DestroyWindow(h);
        return 0;
    }
    if (m == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
struct Gpu
{
    static constexpr UINT W = 512, H = 288;
    UINT width = W, height = H;
    ComPtr<IDXGIFactory6> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12InfoQueue> info;
    ComPtr<IDXGISwapChain3> swap;
    ComPtr<ID3D12DescriptorHeap> presentRtv, presentSrv;
    ComPtr<ID3D12RootSignature> presentRoot;
    ComPtr<ID3D12PipelineState> presentPso;
    std::array<ComPtr<ID3D12Resource>, 2> back;
    HANDLE eventHandle = nullptr, latency = nullptr;
    UINT64 serial = 0;
    HWND window = nullptr;
    bool debug = false;
    PresentMode presentMode = PresentMode::Windowed;
    bool hdrOutput = false;
    UINT presentWidth = 0, presentHeight = 0;
    HMONITOR presentMonitor = nullptr;
    Gpu(bool warp, bool wantDebug, IDXGIAdapter1 *selected = nullptr, UINT w = W, UINT h = H)
        : width(w), height(h)
    {
        if (wantDebug)
        {
            ComPtr<ID3D12Debug> d;
            check(D3D12GetDebugInterface(IID_PPV_ARGS(&d)));
            d->EnableDebugLayer();
            debug = true;
        }
        check(CreateDXGIFactory2(debug ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter1> adapter;
        if (selected)
            adapter = selected;
        else if (warp)
            check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        else
        {
            for (UINT i = 0;; ++i)
            {
                ComPtr<IDXGIAdapter1> a;
                auto hr = factory->EnumAdapterByGpuPreference(
                    i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a));
                if (hr == DXGI_ERROR_NOT_FOUND)
                    break;
                check(hr);
                DXGI_ADAPTER_DESC1 desc{};
                a->GetDesc1(&desc);
                if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                    SUCCEEDED(D3D12CreateDevice(a.Get(), D3D_FEATURE_LEVEL_11_0,
                                                __uuidof(ID3D12Device), nullptr)))
                {
                    adapter = a;
                    break;
                }
            }
        }
        if (!adapter)
            throw std::runtime_error(
                "No hardware D3D12 adapter; use --warp explicitly for CPU test");
        DXGI_ADAPTER_DESC1 ad{};
        adapter->GetDesc1(&ad);
        std::wcout << L"Adapter: " << ad.Description << L"\n";
        check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 caps{};
        check(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &caps, sizeof caps));
        std::cout << "DXR tier (capability only): " << caps.RaytracingTier << "\n";
        if (debug)
            check(device.As(&info));
        D3D12_COMMAND_QUEUE_DESC q{};
        check(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)));
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(&allocator)));
        check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                        IID_PPV_ARGS(&cmd)));
        check(cmd->Close());
        check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!eventHandle)
            throw std::runtime_error("CreateEvent");
    }
    ~Gpu()
    {
        if (queue && fence && eventHandle)
        {
            try
            {
                wait();
            }
            catch (...)
            {
            }
        }
        if (latency)
            CloseHandle(latency);
        if (eventHandle)
            CloseHandle(eventHandle);
        if (window && IsWindow(window))
            DestroyWindow(window);
    }
    void wait()
    {
        check(queue->Signal(fence.Get(), ++serial));
        if (fence->GetCompletedValue() < serial)
        {
            check(fence->SetEventOnCompletion(serial, eventHandle));
            if (WaitForSingleObject(eventHandle, 10000) != WAIT_OBJECT_0)
                throw std::runtime_error("GPU fence timeout");
        }
    }
    void begin()
    {
        check(allocator->Reset());
        check(cmd->Reset(allocator.Get(), nullptr));
    }
    void submit()
    {
        check(cmd->Close());
        ID3D12CommandList *lists[] = {cmd.Get()};
        queue->ExecuteCommandLists(1, lists);
        wait();
    }
    // Deliberately serial for a correctness baseline; NOT a low-latency scheduler.
    Texture texture(DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags)
    {
        Texture t;
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = width;
        rd.Height = height;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = format;
        rd.SampleDesc.Count = 1;
        rd.Flags = flags;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = format;
        if (flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
            clear.DepthStencil.Depth = 1;
        const auto *optimized = (flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                                          D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
                                    ? &clear
                                    : nullptr;
        check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, t.state, optimized,
                                              IID_PPV_ARGS(&t.r)));
        return t;
    }
    std::vector<float> read(Texture &t, UINT channels)
    {
        auto rd = t.r->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT64 size;
        device->GetCopyableFootprints(&rd, 0, 1, 0, &fp, nullptr, nullptr, &size);
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = size;
        bd.Height = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> b;
        check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd,
                                              D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                              IID_PPV_ARGS(&b)));
        auto saved = t.state;
        begin();
        t.to(cmd.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = t.r.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = b.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = fp;
        cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        t.to(cmd.Get(), saved);
        submit();
        void *p;
        D3D12_RANGE range{0, SIZE_T(size)};
        check(b->Map(0, &range, &p));
        const UINT tw = UINT(rd.Width), th = rd.Height;
        std::vector<float> v(size_t(tw) * th * channels);
        for (UINT y = 0; y < th; ++y)
            memcpy(v.data() + size_t(y) * tw * channels,
                   (char *)p + fp.Offset + y * fp.Footprint.RowPitch, size_t(tw) * channels * 4);
        D3D12_RANGE empty{};
        b->Unmap(0, &empty);
        return v;
    }
    void assertClean()
    {
        if (!info)
            return;
        for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i)
        {
            SIZE_T n = 0;
            info->GetMessage(i, nullptr, &n);
            std::vector<char> bytes(n);
            auto *m = (D3D12_MESSAGE *)bytes.data();
            check(info->GetMessage(i, m, &n));
            if (m->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
            {
                std::cerr << m->pDescription << "\n";
                throw std::runtime_error("D3D12 debug-layer warning/error");
            }
        }
    }
    void openWindow(const wchar_t *title, bool excludeCapture = false,
                    PresentMode mode = PresentMode::Windowed, HMONITOR monitor = nullptr,
                    bool requestHdr = false)
    {
        presentMode = mode;
        presentMonitor = monitor;
        hdrOutput = requestHdr;
        presentWidth = width;
        presentHeight = height;
        WNDCLASSW wc{};
        wc.lpfnWndProc = windowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"D3D12ResearchDemo";
        RegisterClassW(&wc);
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        RECT rc{0, 0, LONG(width), LONG(height)};
        int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
        if (mode == PresentMode::Borderless)
        {
            style = WS_POPUP;
            if (!monitor)
                monitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO mi{sizeof(mi)};
            if (!GetMonitorInfoW(monitor, &mi))
                throw std::runtime_error("GetMonitorInfo");
            x = mi.rcMonitor.left;
            y = mi.rcMonitor.top;
            presentWidth = UINT(std::max<LONG>(1, mi.rcMonitor.right - mi.rcMonitor.left));
            presentHeight = UINT(std::max<LONG>(1, mi.rcMonitor.bottom - mi.rcMonitor.top));
            rc = {0, 0, LONG(presentWidth), LONG(presentHeight)};
        }
        else
            AdjustWindowRect(&rc, style, FALSE);
        window = CreateWindowW(wc.lpszClassName, title, style, x, y, rc.right - rc.left,
                               rc.bottom - rc.top, nullptr, nullptr, wc.hInstance, nullptr);
        if (!window)
            throw std::runtime_error("CreateWindow");
        if (mode == PresentMode::Borderless)
            SetWindowPos(window, HWND_TOPMOST, x, y, LONG(presentWidth), LONG(presentHeight),
                         SWP_NOACTIVATE | SWP_SHOWWINDOW);
        if (excludeCapture)
        {
            DWORD affinity = 0;
            if (!SetWindowDisplayAffinity(window, WDA_EXCLUDEFROMCAPTURE) ||
                !GetWindowDisplayAffinity(window, &affinity) || affinity != WDA_EXCLUDEFROMCAPTURE)
                throw std::runtime_error(
                    "Capture exclusion unavailable; refusing recursive capture");
        }
        DXGI_SWAP_CHAIN_DESC1 d{};
        d.Width = presentWidth;
        d.Height = presentHeight;
        // scRGB FP16 is the safe HDR output path: it carries linear values
        // and an explicit Windows color-space tag without claiming HDR10
        // mastering metadata for an SDR Desktop Duplication source.
        d.Format = requestHdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d.BufferCount = 2;
        d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        d.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        ComPtr<IDXGISwapChain1> s;
        check(factory->CreateSwapChainForHwnd(queue.Get(), window, &d, nullptr, nullptr, &s));
        check(s.As(&swap));
        if (requestHdr)
        {
            UINT support = 0;
            const auto colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
            if (FAILED(swap->CheckColorSpaceSupport(colorSpace, &support)) ||
                !(support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
                throw std::runtime_error("Selected display does not support scRGB HDR output");
            check(swap->SetColorSpace1(colorSpace));
        }
        check(swap->SetMaximumFrameLatency(1));
        latency = swap->GetFrameLatencyWaitableObject();
        check(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER));
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.NumDescriptors = 2;
        check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&presentRtv)));
        auto cpu = presentRtv->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < 2; ++i)
        {
            check(swap->GetBuffer(i, IID_PPV_ARGS(&back[i])));
            device->CreateRenderTargetView(back[i].Get(), nullptr, cpu);
            cpu.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        }
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = 1;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&presentSrv)));
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable = {1, &range};
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants = {0, 0, 4};
        D3D12_STATIC_SAMPLER_DESC presentSampler{};
        presentSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        presentSampler.AddressU = presentSampler.AddressV = presentSampler.AddressW =
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        presentSampler.MaxLOD = D3D12_FLOAT32_MAX;
        D3D12_ROOT_SIGNATURE_DESC rs{2, params, 1, &presentSampler,
                                     D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        presentRoot = root(device.Get(), rs);
        const char *shader = R"(Texture2D<float4> img:register(t0);SamplerState samp:register(s0);
cbuffer Present:register(b0){uint dstW;uint dstH;uint hdr;uint pad;}
float4 vs(uint id:SV_VertexID):SV_Position {float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);}
float3 srgb(float3 c){c=saturate(c);return lerp(1.055*pow(c,1.0/2.4)-.055,12.92*c,step(c,.0031308));}
float4 ps(float4 p:SV_Position):SV_Target {
 uint sw,sh;img.GetDimensions(sw,sh);
 float2 uv=p.xy/float2(max(dstW,1),max(dstH,1));
 float srcAspect=float(sw)/max(float(sh),1);float dstAspect=float(dstW)/max(float(dstH),1);
 if(srcAspect>dstAspect){float scale=dstAspect/srcAspect;uv.x=(uv.x-.5)/scale+.5;}
 else if(srcAspect<dstAspect){float scale=srcAspect/dstAspect;uv.y=(uv.y-.5)/scale+.5;}
 float3 c=img.SampleLevel(samp,uv,0).rgb;
 return float4(hdr?c:srgb(c),1);
})";
        auto vs = compile(shader, "vs", "vs_5_1"), ps = compile(shader, "ps", "ps_5_1");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
        p.pRootSignature = presentRoot.Get();
        p.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        p.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        p.RasterizerState.DepthClipEnable = TRUE;
        p.SampleMask = UINT_MAX;
        p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        p.NumRenderTargets = 1;
        p.RTVFormats[0] = d.Format;
        p.SampleDesc.Count = 1;
        p.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        check(device->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&presentPso)));
        ShowWindow(window, SW_SHOW);
    }
    bool pump(bool *generationEnabled = nullptr)
    {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE))
        {
            if (m.message == WM_QUIT)
                return false;
            if (m.message == WM_KEYDOWN && m.hwnd == window)
            {
                if (m.wParam == VK_ESCAPE)
                    return false;
                if (generationEnabled && m.wParam == VK_SPACE && !(m.lParam & (1LL << 30)))
                    *generationEnabled = !*generationEnabled;
            }
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        return true;
    }
    void present(Texture &t)
    {
        if (WaitForSingleObject(latency, 1000) != WAIT_OBJECT_0)
            throw std::runtime_error("presentation wait timeout");
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = t.r->GetDesc().Format;
        sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(t.r.Get(), &sd,
                                         presentSrv->GetCPUDescriptorHandleForHeapStart());
        const UINT idx = swap->GetCurrentBackBufferIndex();
        begin();
        auto saved = t.state;
        t.to(cmd.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        barrier(cmd.Get(), back[idx].Get(), D3D12_RESOURCE_STATE_PRESENT,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto rtv = presentRtv->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += idx * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        cmd->SetGraphicsRootSignature(presentRoot.Get());
        cmd->SetPipelineState(presentPso.Get());
        ID3D12DescriptorHeap *heaps[] = {presentSrv.Get()};
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetGraphicsRootDescriptorTable(0, presentSrv->GetGPUDescriptorHandleForHeapStart());
        const UINT constants[4] = {presentWidth, presentHeight, hdrOutput ? 1u : 0u, 0u};
        cmd->SetGraphicsRoot32BitConstants(1, 4, constants, 0);
        D3D12_VIEWPORT vp{0, 0, float(presentWidth), float(presentHeight), 0, 1};
        D3D12_RECT sc{0, 0, LONG(presentWidth), LONG(presentHeight)};
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sc);
        cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(3, 1, 0, 0);
        barrier(cmd.Get(), back[idx].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_PRESENT);
        t.to(cmd.Get(), saved);
        submit();
        check(swap->Present(1, 0));
    }
};
inline void saveBmp(const std::string &path, const std::vector<float> &rgba)
{
    BITMAPFILEHEADER f{};
    BITMAPINFOHEADER i{};
    f.bfType = 0x4d42;
    f.bfOffBits = sizeof(f) + sizeof(i);
    f.bfSize = f.bfOffBits + Gpu::W * Gpu::H * 4;
    i.biSize = sizeof(i);
    i.biWidth = Gpu::W;
    i.biHeight = -LONG(Gpu::H);
    i.biPlanes = 1;
    i.biBitCount = 32;
    i.biCompression = BI_RGB;
    std::ofstream out(path, std::ios::binary);
    out.write((char *)&f, sizeof(f));
    out.write((char *)&i, sizeof(i));
    for (size_t n = 0; n < rgba.size(); n += 4)
    {
        unsigned char b[4];
        for (int k = 0; k < 3; ++k)
            b[2 - k] =
                (unsigned char)(std::pow(std::clamp(rgba[n + k], 0.f, 1.f), 1.f / 2.2f) * 255.f +
                                0.5f);
        b[3] = 255;
        out.write((char *)b, 4);
    }
    if (!out)
        throw std::runtime_error("image write failed");
}
