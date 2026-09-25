#include "scene_renderer.hpp"
#include "ffg/ffg.h"
#include <filesystem>
#include <memory>
double mae(const std::vector<float> &a, const std::vector<float> &b)
{
    double sum = 0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (!std::isfinite(a[i]))
            throw std::runtime_error("non-finite output");
        sum += std::abs(a[i] - b[i]);
    }
    return sum / double(a.size());
}
int main(int argc, char **argv)
{
    try
    {
        bool warp = false, debug = false, headless = false;
        int frames = 120;
        for (int i = 1; i < argc; ++i)
        {
            std::string a = argv[i];
            if (a == "--warp")
                warp = true;
            else if (a == "--debug")
                debug = true;
            else if (a == "--headless")
                headless = true;
            else if (a == "--frames" && i + 1 < argc)
                frames = std::stoi(argv[++i]);
            else
                throw std::runtime_error("Unknown option");
        }
        Gpu gpu(warp, debug);
        D3D12SceneRenderer fixture(gpu);
        auto a = fixture.createFrame(), b = fixture.createFrame(), truth = fixture.createFrame();
        auto output =
            gpu.texture(DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        FfgContext *raw = nullptr;
        check(ffgCreate(gpu.device.Get(), &raw));
        std::unique_ptr<FfgContext, decltype(&ffgDestroy)> ctx(raw, ffgDestroy);
        auto pairFor = [&]() {
            FgdsPair p{};
            p.structSize = sizeof p;
            p.version = FGDS_VERSION;
            p.frames[0] = a.describe(1, 1000000000);
            p.frames[1] = b.describe(2, 1016666667);
            p.alpha = 0.5f;
            return p;
        };
        auto generate = [&](const FgdsPair &p) {
            gpu.begin();
            output.to(gpu.cmd.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            check(ffgRecord(ctx.get(), gpu.cmd.Get(), &p, output.r.Get()));
            gpu.submit();
        };
        auto run = [&](float t0, float t1, const char *label, bool save) {
            gpu.begin();
            fixture.record(demoPacket(t0, t1), a);
            fixture.record(demoPacket(t1, t0), b);
            fixture.record(demoPacket((t0 + t1) * 0.5f, (t0 + t1) * 0.5f), truth);
            gpu.submit();
            auto av = gpu.read(a.color, 4), bv = gpu.read(b.color, 4),
                 tv = gpu.read(truth.color, 4);
            auto pair = pairFor();
            generate(pair);
            auto result = gpu.read(output, 4);
            auto blend = av;
            for (size_t i = 0; i < blend.size(); ++i)
                blend[i] = (av[i] + bv[i]) * 0.5f;
            double error = mae(result, tv), baseline = mae(blend, tv);
            std::cout << label << ": MAE=" << error << " blend_MAE=" << baseline << "\n";
            if (t0 == t1 ? error > 1e-6 : (error > 0.008 || error >= baseline * 0.6))
                throw std::runtime_error("midpoint quality regression");
            pair.alpha = 0;
            generate(pair);
            if (mae(gpu.read(output, 4), av) > 1e-7)
                throw std::runtime_error("alpha zero");
            pair.alpha = 1;
            generate(pair);
            if (mae(gpu.read(output, 4), bv) > 1e-7)
                throw std::runtime_error("alpha one");
            pair.alpha = 0.5f;
            pair.frames[1].flags = FGDS_CAMERA_CUT;
            generate(pair);
            if (mae(gpu.read(output, 4), bv) > 1e-7)
                throw std::runtime_error("camera cut");
            if (save)
            {
                std::filesystem::create_directories("out");
                saveBmp("out/real_0.bmp", av);
                saveBmp("out/real_1.bmp", bv);
                saveBmp("out/generated.bmp", result);
                saveBmp("out/ground_truth.bmp", tv);
                saveBmp("out/blend_baseline.bmp", blend);
            }
        };
        run(0, 0, "static", false);
        run(0, 0.25f, "translation", true);
        run(1.2f, 1.45f, "occlusion", false);
        run(0.25f, 0, "reverse", false);
        auto bad = pairFor();
        bad.frames[1].width = 1;
        gpu.begin();
        if (ffgRecord(ctx.get(), gpu.cmd.Get(), &bad, output.r.Get()) != E_INVALIDARG)
            throw std::runtime_error("extent validation");
        bad = pairFor();
        bad.version = 0;
        if (ffgRecord(ctx.get(), gpu.cmd.Get(), &bad, output.r.Get()) != E_INVALIDARG)
            throw std::runtime_error("version validation");
        bad = pairFor();
        bad.alpha = NAN;
        if (ffgRecord(ctx.get(), gpu.cmd.Get(), &bad, output.r.Get()) != E_INVALIDARG)
            throw std::runtime_error("alpha validation");
        bad = pairFor();
        bad.frames[1].timestampNs = 0;
        if (ffgRecord(ctx.get(), gpu.cmd.Get(), &bad, output.r.Get()) != E_INVALIDARG)
            throw std::runtime_error("timestamp validation");
        bad = pairFor();
        bad.frames[0].jitterPixels[0] = 1;
        if (ffgRecord(ctx.get(), gpu.cmd.Get(), &bad, output.r.Get()) != E_INVALIDARG)
            throw std::runtime_error("jitter validation");
        bad = pairFor();
        if (ffgRecord(ctx.get(), gpu.cmd.Get(), &bad, a.color.r.Get()) != E_INVALIDARG)
            throw std::runtime_error("alias validation");
        gpu.submit();
        gpu.assertClean();
        std::cout << "PASS: FFG GPU interpolation, endpoints, cut, invalid contracts.\n";
        if (!headless)
        {
            gpu.openWindow(L"FreeFrameGen (FFG) - synthetic real / generated sequence");
            // Demonstrates ordering, not a production pacing policy or latency claim.
            for (int i = 0; i < frames && gpu.pump(); ++i)
            {
                float t0 = std::sin(float(i) * 0.05f), t1 = std::sin(float(i + 1) * 0.05f);
                gpu.begin();
                fixture.record(demoPacket(t0, t1), a);
                fixture.record(demoPacket(t1, t0), b);
                gpu.submit();
                gpu.present(a.color);
                auto pair = pairFor();
                generate(pair);
                gpu.present(output);
            }
            gpu.assertClean();
        }
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
