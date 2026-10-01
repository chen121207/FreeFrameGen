#include "desktop_capture.hpp"
#include "color_flow.hpp"
#include <chrono>

int main(int argc, char **argv)
{
    try
    {
        int selected = -1, selectedWindow = -1, seconds = 0, width = 960;
        double deadlineMs = 24.0;
        bool list = false, listWindows = false, headless = false, debug = false, selfTest = false;
        bool borderless = false, hdrOutput = false;
        for (int i = 1; i < argc; ++i)
        {
            std::string a = argv[i];
            if (a == "--list")
                list = true;
            else if (a == "--list-windows")
                listWindows = true;
            else if (a == "--headless")
                headless = true;
            else if (a == "--debug")
                debug = true;
            else if (a == "--fullscreen" || a == "--borderless" || a == "--replace")
                borderless = true;
            else if (a == "--hdr-output" || a == "--hdr")
                hdrOutput = true;
            else if (a == "--self-test")
                selfTest = true;
            else if (a == "--output" && i + 1 < argc)
                selected = std::stoi(argv[++i]);
            else if (a == "--window" && i + 1 < argc)
                selectedWindow = std::stoi(argv[++i]);
            else if (a == "--seconds" && i + 1 < argc)
                seconds = std::stoi(argv[++i]);
            else if (a == "--width" && i + 1 < argc)
                width = std::stoi(argv[++i]);
            else if (a == "--deadline-ms" && i + 1 < argc)
                deadlineMs = std::stod(argv[++i]);
            else if (a == "--help")
            {
                std::cout << "FFG capture: --list | --list-windows | --output N [--window N] "
                             "[--seconds N] [--width 320..1920] [--deadline-ms 4..100] "
                             "[--debug] [--headless] [--fullscreen|--replace] [--hdr-output]\n"
                          << "No arguments: interactive display selection. Preview: SPACE toggles "
                             "FG, ESC stops.\n"
                          << "--window captures a visible window client area; use --list-windows "
                             "to choose one. Without --output its monitor is selected.\n"
                          << "--fullscreen/--replace uses an FFG-owned borderless topmost window "
                             "over the selected monitor; it never injects into a game.\n"
                          << "--hdr-output requests an FP16 scRGB swapchain and Windows color-space "
                             "tag; the Desktop Duplication source remains SDR BGRA8.\n"
                          << "--self-test runs the capture history guard without touching the desktop.\n";
                return 0;
            }
            else
                throw std::runtime_error("Unknown option; use --help");
        }
        if (selfTest)
        {
            struct Case
            {
                bool havePrevious;
                LONGLONG previous;
                LONGLONG current;
                UINT accumulated;
                LONGLONG qpcFrequency;
                bool expected;
            };
            const Case cases[] = {
                {false, 0, 0, 1, 1000, false},   // first sample establishes history
                {true, 100, 200, 1, 1000, true}, // one contiguous present
                {true, 100, 200, 2, 1000, false}, // consumer skipped a present
                {true, 200, 100, 1, 1000, false}, // QPC rollback
                {true, 100, 100, 1, 1000, false}, // duplicate timestamp
                {true, 100, 349, 1, 1000, true}, // just inside the 250 ms guard
                {true, 100, 350, 1, 1000, false},
                {true, 100, 200, 1, 0, false}, // invalid frequency must reset
            };
            for (const auto &test : cases)
            {
                auto actual = capturePairIsContiguous(test.havePrevious, test.previous,
                                                       test.current, test.accumulated,
                                                       test.qpcFrequency);
                if (actual != test.expected)
                    throw std::runtime_error("Capture scheduler self-test failed");
            }
            struct DeadlineCase
            {
                LONGLONG stamp;
                LONGLONG now;
                LONGLONG frequency;
                double budgetMs;
                bool expected;
            };
            const DeadlineCase deadlineCases[] = {
                {100, 105, 1000, 6.0, true},  // 5 ms old
                {100, 107, 1000, 6.0, false}, // 7 ms old
                {100, 99, 1000, 6.0, false},  // QPC rollback
                {100, 105, 0, 6.0, false},    // invalid QPC frequency
                {100, 105, 1000, 0.0, false}, // disabled budget
            };
            for (const auto &test : deadlineCases)
            {
                auto actual = captureFrameWithinDeadline(test.stamp, test.now, test.frequency,
                                                          test.budgetMs);
                if (actual != test.expected)
                    throw std::runtime_error("Capture deadline self-test failed");
            }
            std::cout << "PASS: capture scheduler contiguous-history guard.\n";
            return 0;
        }
        if (seconds < 0 || seconds > 86400 || width < 320 || width > 1920 ||
            !(deadlineMs >= 4.0 && deadlineMs <= 100.0))
            throw std::runtime_error("Invalid seconds/width/deadline-ms");
        auto windows = captureWindows();
        if (listWindows)
        {
            for (size_t i = 0; i < windows.size(); ++i)
            {
                const auto &w = windows[i].client;
                std::wcout << i << L": " << windows[i].title << L"  [" << w.left << L"," << w.top
                           << L" - " << w.right << L"," << w.bottom << L"]\n";
            }
        }
        auto outputs = captureOutputs();
        for (size_t i = 0; i < outputs.size(); ++i)
        {
            auto &d = outputs[i].desc;
            std::wcout << i << L": " << d.DeviceName << L"  "
                       << d.DesktopCoordinates.right - d.DesktopCoordinates.left << L"x"
                       << d.DesktopCoordinates.bottom - d.DesktopCoordinates.top << L"\n";
        }
        if ((list || listWindows) && selectedWindow < 0)
            return 0;
        if (outputs.empty())
            throw std::runtime_error("No attached display");
        const CaptureWindow *window = nullptr;
        if (selectedWindow >= 0)
        {
            if (size_t(selectedWindow) >= windows.size())
                throw std::runtime_error("Invalid window index");
            window = &windows[size_t(selectedWindow)];
            if (selected < 0)
            {
                auto *inferred = captureOutputForWindow(outputs, *window);
                if (!inferred)
                    throw std::runtime_error("Window monitor is not an attached capture output");
                selected = int(inferred - outputs.data());
            }
        }
        if (selected < 0)
        {
            if (headless)
                throw std::runtime_error(
                    "Headless requires explicit --output (or --window) and --seconds");
            std::cout << (window ? "FFG captures the selected window client area locally. No "
                                 : "FFG captures the ENTIRE selected display locally. No ")
                      << "recording/upload/injection.\n"
                      << (borderless ? "Borderless replacement output enabled.\n"
                                     : "Windowed preview output enabled.\n")
                      << (hdrOutput ? "HDR output requested (scRGB FP16; source is SDR BGRA8).\n"
                                    : "SDR output.\n")
                      << "Enter display index to start (Ctrl+C cancels): " << std::flush;
            if (!(std::cin >> selected))
                return 0;
        }
        if (selected < 0 || size_t(selected) >= outputs.size())
            throw std::runtime_error("Invalid output index");
        if (headless && seconds == 0)
            throw std::runtime_error("Headless requires bounded --seconds");
        const auto &o = outputs[size_t(selected)];
        RECT r = o.desc.DesktopCoordinates;
        if (window)
        {
            RECT clipped{};
            if (!IntersectRect(&clipped, &r, &window->client))
                throw std::runtime_error("Window does not intersect the selected display");
            r = clipped;
        }
        UINT w = UINT(std::min(width, int(r.right - r.left)));
        UINT h =
            UINT(std::max(1, int(double(w) * double(r.bottom - r.top) / double(r.right - r.left))));
        Gpu gpu(false, debug, o.adapter.Get(), w, h);
        DesktopCapture capture(gpu, o, window ? &r : nullptr);
        ColorFlow flow(gpu);
        auto previous =
            gpu.texture(DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        auto current =
            gpu.texture(DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if (!headless)
            gpu.openWindow(L"FreeFrameGen - capture replacement output", true,
                           borderless ? PresentMode::Borderless : PresentMode::Windowed,
                           o.desc.Monitor, hdrOutput);
        LARGE_INTEGER freq{};
        QueryPerformanceFrequency(&freq);
        LONGLONG last = 0, stamp = 0;
        bool havePrevious = false;
        unsigned captured = 0, generated = 0, dropped = 0, resets = 0;
        unsigned deadlineSkips = 0, deadlineOverruns = 0;
        bool enabled = true;
        double workMs = 0;
        double generationEwmaMs = 0;
        auto start = std::chrono::steady_clock::now();
        while (gpu.pump(&enabled))
        {
            auto now = std::chrono::steady_clock::now();
            if (seconds && std::chrono::duration<double>(now - start).count() >= seconds)
                break;
            UINT accumulated = 0;
            if (!capture.acquire(gpu, stamp, accumulated))
                continue;
            ++captured;
            dropped += accumulated > 1 ? accumulated - 1 : 0;
            const bool contiguous = capturePairIsContiguous(havePrevious, last, stamp, accumulated,
                                                            freq.QuadPart);
            LARGE_INTEGER nowQpc{};
            QueryPerformanceCounter(&nowQpc);
            const bool freshEnough = captureFrameWithinDeadline(
                stamp, nowQpc.QuadPart, freq.QuadPart, deadlineMs);
            // A prior expensive interpolation is a backpressure signal. Skip
            // one interpolation when it consumed the configured source-age
            // budget; conversion still runs so shared D3D11/D3D12 ownership is
            // released and the newest real frame remains available.
            const bool predictedOverrun = generationEwmaMs > deadlineMs;
            const bool generatePair = enabled && contiguous && freshEnough && !predictedOverrun;
            if (enabled && (!freshEnough || predictedOverrun))
                ++deadlineSkips;
            if (predictedOverrun && !generatePair)
                generationEwmaMs *= .5; // one-frame cooldown; avoid starving generation
            // Conversion and interpolation share one command list.  The
            // single-flight submit still waits for the queue fence before the
            // next Desktop Duplication acquire, preserving D3D11/D3D12
            // ownership while removing the old convert->wait->generate->wait
            // bubble from every contiguous pair.
            auto flowStart = std::chrono::steady_clock::now();
            if (generatePair)
                flow.convertAndGenerate(capture.shared12, current, &previous, &current);
            else
                flow.convertAndGenerate(capture.shared12, current);
            const double pairWorkMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                          flowStart)
                    .count();
            if (generatePair)
            {
                workMs += pairWorkMs;
                generationEwmaMs = generationEwmaMs == 0
                                       ? pairWorkMs
                                       : generationEwmaMs * .75 + pairWorkMs * .25;
                // Do not put an old midpoint in front of the newest real
                // frame if this completed pair already consumed its budget.
                const bool overrun = pairWorkMs > deadlineMs;
                if (overrun)
                    ++deadlineOverruns;
                ++generated;
                // One source interval of lookahead. Chronological A -> midpoint -> B (next
                // iteration). Present(1) paces each output, but source is NOT locked to half the
                // display rate.
                if (!headless && !overrun)
                {
                    gpu.present(previous);
                    if (!gpu.pump(&enabled))
                        break;
                    gpu.present(flow.output);
                }
                else if (!headless)
                {
                    gpu.present(current);
                }
            }
            else
            {
                ++resets;
                if (!headless)
                    gpu.present(current);
            }
            std::swap(previous, current);
            last = stamp;
            // Do not bridge a period in which the user disabled generation;
            // the first enabled frame establishes a fresh pair.
            havePrevious = enabled;
            if (!headless)
            {
                auto title = std::wstring(L"FreeFrameGen | ") + (enabled ? L"FG ON" : L"FG OFF") +
                             L" | SPACE: compare | ESC: stop | captured=" +
                             std::to_wstring(captured) + L" generated=" +
                             std::to_wstring(generated);
                SetWindowTextW(gpu.window, title.c_str());
            }
        }
        gpu.assertClean();
        std::cout << "Captured=" << captured << " generated=" << generated
                  << " accumulated_skips=" << dropped << " history_resets=" << resets
                  << " deadline_skips=" << deadlineSkips
                  << " deadline_overruns=" << deadlineOverruns
                  << " processing=" << w << "x" << h
                  << " mean_flow_submit_wait_ms=" << (generated ? workMs / generated : 0) << "\n";
        std::cout << "No captured pixels read back, saved, or uploaded. Timing is NOT end-to-end "
                     "latency.\n";
        if (!captured)
            throw std::runtime_error("No desktop frames received");
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FFG capture failed: " << e.what() << "\n";
        return 1;
    }
}
