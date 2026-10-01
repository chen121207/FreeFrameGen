#pragma once
#include "d3d12_host.hpp"
#include <d3d11_4.h>
#include <limits>
#include <limits>

struct CaptureOutput
{
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIOutput1> output;
    DXGI_OUTPUT_DESC desc{};
};

struct CaptureWindow
{
    HWND hwnd = nullptr;
    std::wstring title;
    RECT client{};
};

inline bool captureWindowClientRect(HWND hwnd, RECT &client)
{
    RECT local{};
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd) || !GetClientRect(hwnd, &local))
        return false;
    POINT origin{local.left, local.top};
    if (!ClientToScreen(hwnd, &origin))
        return false;
    OffsetRect(&local, origin.x, origin.y);
    if (local.right <= local.left || local.bottom <= local.top)
        return false;
    client = local;
    return true;
}

inline std::vector<CaptureWindow> captureWindows()
{
    std::vector<CaptureWindow> result;
    EnumWindows(
        [](HWND hwnd, LPARAM parameter) -> BOOL {
            auto *windows = reinterpret_cast<std::vector<CaptureWindow> *>(parameter);
            wchar_t title[512]{};
            if (!GetWindowTextW(hwnd, title, ARRAYSIZE(title)) || !title[0])
                return TRUE;
            RECT client{};
            if (!captureWindowClientRect(hwnd, client))
                return TRUE;
            LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            if (style & WS_EX_TOOLWINDOW)
                return TRUE;
            windows->push_back({hwnd, title, client});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&result));
    return result;
}

inline const CaptureOutput *captureOutputForWindow(const std::vector<CaptureOutput> &outputs,
                                                   const CaptureWindow &window)
{
    POINT center{(window.client.left + window.client.right) / 2,
                 (window.client.top + window.client.bottom) / 2};
    HMONITOR monitor = MonitorFromPoint(center, MONITOR_DEFAULTTONEAREST);
    for (const auto &output : outputs)
        if (output.desc.Monitor == monitor)
            return &output;
    return nullptr;
}

// A Desktop Duplication frame may represent more than one source present when
// the consumer fell behind.  Interpolating from the previous consumed frame to
// that latest frame would span an unknown number of presents and produces
// incorrect motion.  Keep this guard independent of the capture object so the
// scheduler can be checked without a GPU or an active desktop duplication
// session.
inline bool capturePairIsContiguous(bool havePrevious, LONGLONG previousTimestamp,
                                    LONGLONG timestamp, UINT accumulatedFrames,
                                    LONGLONG qpcFrequency)
{
    if (!havePrevious || accumulatedFrames > 1 || qpcFrequency <= 0 || timestamp <= previousTimestamp)
        return false;
    return double(timestamp - previousTimestamp) / double(qpcFrequency) < .25;
}

// LastPresentTime is expressed in the same QPC domain as QueryPerformanceCounter.
// Keep this check pure so the scheduler can be tested without a desktop or GPU.
inline double captureFrameAgeMs(LONGLONG timestamp, LONGLONG now, LONGLONG qpcFrequency)
{
    if (timestamp <= 0 || now < timestamp || qpcFrequency <= 0)
        return std::numeric_limits<double>::infinity();
    return 1000.0 * double(now - timestamp) / double(qpcFrequency);
}

inline bool captureFrameWithinDeadline(LONGLONG timestamp, LONGLONG now,
                                        LONGLONG qpcFrequency, double budgetMs)
{
    if (!(budgetMs > 0.0))
        return false;
    return captureFrameAgeMs(timestamp, now, qpcFrequency) <= budgetMs;
}

inline std::vector<CaptureOutput> captureOutputs()
{
    ComPtr<IDXGIFactory1> f;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&f)));
    std::vector<CaptureOutput> result;
    for (UINT a = 0;; ++a)
    {
        ComPtr<IDXGIAdapter1> adapter;
        auto hr = f->EnumAdapters1(a, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;
        check(hr);
        for (UINT o = 0;; ++o)
        {
            ComPtr<IDXGIOutput> output;
            hr = adapter->EnumOutputs(o, &output);
            if (hr == DXGI_ERROR_NOT_FOUND)
                break;
            check(hr);
            CaptureOutput item;
            item.adapter = adapter;
            check(output->GetDesc(&item.desc));
            if (!item.desc.AttachedToDesktop)
                continue;
            check(output.As(&item.output));
            result.push_back(item);
        }
    }
    return result;
}
struct DesktopCapture
{
    ComPtr<ID3D11Device5> device;
    ComPtr<ID3D11DeviceContext4> context;
    ComPtr<IDXGIOutputDuplication> duplication;
    ComPtr<ID3D11Texture2D> shared11;
    ComPtr<ID3D11Fence> fence11;
    ComPtr<ID3D12Fence> fence12;
    Texture shared12;
    UINT64 serial = 0;
    UINT width = 0, height = 0;
    UINT sourceWidth = 0, sourceHeight = 0;
    RECT screenRegion{};
    LONG desktopLeft = 0, desktopTop = 0;
    explicit DesktopCapture(Gpu &g, const CaptureOutput &out, const RECT *requestedRegion = nullptr)
    {
        if (out.desc.Rotation != DXGI_MODE_ROTATION_IDENTITY)
            throw std::runtime_error("Rotated display unsupported in capture v0.2");
        ComPtr<IDXGIOutput6> output6;
        if (SUCCEEDED(out.output.As(&output6)))
        {
            DXGI_OUTPUT_DESC1 desc{};
            check(output6->GetDesc1(&desc));
            if (desc.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)
                throw std::runtime_error(
                    "HDR/wide-gamut capture unsupported; select an SDR display");
        }
        ComPtr<ID3D11Device> d;
        ComPtr<ID3D11DeviceContext> c;
        check(D3D11CreateDevice(out.adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &d,
                                nullptr, &c));
        check(d.As(&device));
        check(c.As(&context));
        check(out.output->DuplicateOutput(device.Get(), &duplication));
        DXGI_OUTDUPL_DESC desc{};
        duplication->GetDesc(&desc);
        sourceWidth = desc.ModeDesc.Width;
        sourceHeight = desc.ModeDesc.Height;
        RECT desktop = out.desc.DesktopCoordinates;
        desktopLeft = desktop.left;
        desktopTop = desktop.top;
        screenRegion = requestedRegion ? *requestedRegion : desktop;
        RECT clipped{};
        if (!IntersectRect(&clipped, &screenRegion, &desktop) ||
            clipped.right - clipped.left < 64 || clipped.bottom - clipped.top < 64)
            throw std::runtime_error("Capture region does not intersect the selected display");
        screenRegion = clipped;
        width = UINT(screenRegion.right - screenRegion.left);
        height = UINT(screenRegion.bottom - screenRegion.top);
        LONG relativeLeft = screenRegion.left - desktop.left;
        LONG relativeTop = screenRegion.top - desktop.top;
        if (relativeLeft < 0 || relativeTop < 0 ||
            relativeLeft + LONG(width) > LONG(sourceWidth) ||
            relativeTop + LONG(height) > LONG(sourceHeight))
            throw std::runtime_error("Capture region is outside the selected display");
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = width;
        rd.Height = height;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        rd.SampleDesc.Count = 1;
        rd.Flags =
            D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        check(g.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd,
                                                D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                IID_PPV_ARGS(&shared12.r)));
        HANDLE handle = nullptr;
        check(
            g.device->CreateSharedHandle(shared12.r.Get(), nullptr, GENERIC_ALL, nullptr, &handle));
        HRESULT hr = device->OpenSharedResource1(handle, IID_PPV_ARGS(&shared11));
        CloseHandle(handle);
        check(hr);
        check(g.device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence12)));
        check(g.device->CreateSharedHandle(fence12.Get(), nullptr, GENERIC_ALL, nullptr, &handle));
        hr = device->OpenSharedFence(handle, IID_PPV_ARGS(&fence11));
        CloseHandle(handle);
        check(hr);
    }
    // Caller finishes all D3D12 reads before the next acquire (single-flight baseline).
    bool acquire(Gpu &g, LONGLONG &timestamp, UINT &accumulated)
    {
        DXGI_OUTDUPL_FRAME_INFO info{};
        ComPtr<IDXGIResource> resource;
        auto hr = duplication->AcquireNextFrame(20, &info, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
            return false;
        if (hr == DXGI_ERROR_ACCESS_LOST)
            throw std::runtime_error("Capture access lost (mode/session change). Restart capture.");
        check(hr);
        struct Release
        {
            IDXGIOutputDuplication *d;
            ~Release()
            {
                d->ReleaseFrame();
            }
        } release{duplication.Get()};
        // Pointer-only updates are not new game/desktop frames.
        if (!info.LastPresentTime.QuadPart)
            return false;
        ComPtr<ID3D11Texture2D> source;
        check(resource.As(&source));
        D3D11_TEXTURE2D_DESC desc{};
        source->GetDesc(&desc);
        if (desc.Width != sourceWidth || desc.Height != sourceHeight ||
            desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
            throw std::runtime_error("Capture surface changed; restart capture");
        LONG relativeLeft = screenRegion.left - desktopLeft;
        LONG relativeTop = screenRegion.top - desktopTop;
        D3D11_BOX box{UINT(relativeLeft), UINT(relativeTop), 0,
                      UINT(relativeLeft + LONG(width)), UINT(relativeTop + LONG(height)), 1};
        if (width == sourceWidth && height == sourceHeight)
            context->CopyResource(shared11.Get(), source.Get());
        else
            context->CopySubresourceRegion(shared11.Get(), 0, 0, 0, 0, source.Get(), 0, &box);
        check(context->Signal(fence11.Get(), ++serial));
        context->Flush();
        check(g.queue->Wait(fence12.Get(), serial));
        timestamp = info.LastPresentTime.QuadPart;
        accumulated = info.AccumulatedFrames;
        return true;
    }
};
