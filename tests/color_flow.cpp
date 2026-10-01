#include "color_flow.hpp"

void uploadBytes(Gpu &g, Texture &t, const void *data, UINT rowBytes)
{
    auto desc = t.r->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT64 bytes = 0;
    g.device->GetCopyableFootprints(&desc, 0, 1, 0, &fp, nullptr, nullptr, &bytes);
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = bytes;
    bd.Height = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> buffer;
    check(g.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd,
                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                            IID_PPV_ARGS(&buffer)));
    void *mapped = nullptr;
    D3D12_RANGE empty{};
    check(buffer->Map(0, &empty, &mapped));
    for (UINT y = 0; y < g.height; ++y)
        memcpy(static_cast<char *>(mapped) + fp.Offset + size_t(y) * fp.Footprint.RowPitch,
               static_cast<const char *>(data) + size_t(y) * rowBytes, rowBytes);
    buffer->Unmap(0, nullptr);
    g.begin();
    t.to(g.cmd.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = buffer.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = fp;
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = t.r.Get();
    g.cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    t.to(g.cmd.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    g.submit();
}
void upload(Gpu &g, Texture &t, const std::vector<float> &v)
{
    uploadBytes(g, t, v.data(), g.width * 16);
}
std::vector<float> image(UINT w, UINT h, int dx, int dy, bool smooth = false)
{
    std::vector<float> v(size_t(w) * h * 4);
    for (UINT y = 0; y < h; ++y)
        for (UINT x = 0; x < w; ++x)
        {
            unsigned hash = unsigned(int(x) - dx) * 1664525u + unsigned(int(y) - dy) * 1013904223u;
            for (UINT c = 0; c < 3; ++c)
            {
                hash ^= hash >> 16;
                hash *= 2246822519u;
                v[(size_t(y) * w + x) * 4 + c] = float(hash & 255) / 255.f;
                if (smooth)
                    v[(size_t(y) * w + x) * 4 + c] =
                        .5f +
                        .25f * std::sin(float(int(x) - dx) * (.11f + .031f * c) +
                                        float(int(y) - dy) * .073f) +
                        .25f * std::cos(float(int(y) - dy) * (.19f + .027f * c) -
                                        float(int(x) - dx) * .041f);
            }
            v[(size_t(y) * w + x) * 4 + 3] = 1;
        }
    return v;
}
std::vector<float> occlusionScene(UINT w, UINT h, int dx, int dy)
{
    std::vector<float> v(size_t(w) * h * 4);
    for (UINT y = 0; y < h; ++y)
        for (UINT x = 0; x < w; ++x)
        {
            const int sx = int(x) - dx, sy = int(y) - dy;
            bool object = sx >= 38 && sx < 132 && sy >= 24 && sy < 88;
            size_t i = (size_t(y) * w + x) * 4;
            v[i + 0] = object ? .08f : .72f;
            v[i + 1] = object ? .74f : .18f;
            v[i + 2] = object ? .22f : .08f;
            v[i + 3] = 1;
        }
    return v;
}
double error(const std::vector<float> &a, const std::vector<float> &b, UINT w, UINT h, UINT border)
{
    double sum = 0;
    size_t count = 0;
    for (UINT y = border; y < h - border; ++y)
        for (UINT x = border; x < w - border; ++x)
            for (UINT c = 0; c < 3; ++c)
            {
                size_t i = (size_t(y) * w + x) * 4 + c;
                if (!std::isfinite(a[i]))
                    throw std::runtime_error("Nonfinite color flow output");
                sum += std::abs(a[i] - b[i]);
                ++count;
            }
    return sum / double(count);
}
int main(int argc, char **argv)
{
    try
    {
        bool warp = false, debug = false, stress = false;
        for (int i = 1; i < argc; ++i)
        {
            std::string a = argv[i];
            if (a == "--warp")
                warp = true;
            else if (a == "--debug")
                debug = true;
            else if (a == "--stress")
                stress = true;
            else
                throw std::runtime_error("Unknown test option");
        }
        // Odd extents exercise partial thread groups and non-tight readback row pitch.
        Gpu g(warp, debug, nullptr, 181, 105);
        ColorFlow flow(g);
        auto a =
            g.texture(DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        auto b =
            g.texture(DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        for (auto delta :
             std::array<std::array<int, 2>, 5>{{{0, 0}, {8, 4}, {-8, -4}, {4, -8}, {6, -2}}})
        {
            // Standard non-grid motion uses band-limited texture; --stress also runs the
            // same quality gate against raw pixel noise to protect the adaptive search path.
            bool smooth = delta[0] == 6 && !stress;
            auto av = image(g.width, g.height, 0, 0, smooth),
                 bv = image(g.width, g.height, delta[0], delta[1], smooth);
            auto truth = image(g.width, g.height, delta[0] / 2, delta[1] / 2, smooth);
            upload(g, a, av);
            upload(g, b, bv);
            flow.generate(a, b);
            auto result = g.read(flow.output, 4);
            auto blend = av;
            for (size_t i = 0; i < blend.size(); ++i)
                blend[i] = (av[i] + bv[i]) * .5f;
            double actual = error(result, truth, g.width, g.height, 32);
            double baseline = error(blend, truth, g.width, g.height, 32);
            std::cout << "motion=" << delta[0] << "," << delta[1] << " interior_MAE=" << actual
                      << " blend=" << baseline << "\n";
            if (actual > .015 || (baseline > 0 && actual >= baseline * .25))
                throw std::runtime_error("Flow interpolation regression");
            for (float alpha : {0.f, 1.f})
            {
                flow.generate(a, b, alpha);
                if (error(g.read(flow.output, 4), alpha == 0 ? av : bv, g.width, g.height, 0) >
                    1e-7)
                    throw std::runtime_error("Endpoint regression");
            }
        }
        // A translated foreground rectangle creates both exposed background and
        // newly revealed object pixels.  Measure against a hard midpoint and
        // retain the blend error as a baseline; this exercises confidence and
        // hole fallback rather than only global color disagreement.
        auto occA = occlusionScene(g.width, g.height, 0, 0);
        auto occB = occlusionScene(g.width, g.height, 10, 0);
        auto occTruth = occlusionScene(g.width, g.height, 5, 0);
        upload(g, a, occA);
        upload(g, b, occB);
        flow.generate(a, b);
        auto occResult = g.read(flow.output, 4);
        auto occBlend = occA;
        for (size_t i = 0; i < occBlend.size(); ++i)
            occBlend[i] = (occA[i] + occB[i]) * .5f;
        double occActual = error(occResult, occTruth, g.width, g.height, 8);
        double occBaseline = error(occBlend, occTruth, g.width, g.height, 8);
        std::cout << "occlusion_shift=10,0 interior_MAE=" << occActual
                  << " blend=" << occBaseline << "\n";
        if (!(occActual < occBaseline * .75))
            throw std::runtime_error("Occlusion interpolation regression");
        // A full-scene appearance change has no reliable color correspondence.
        // The generated image must select the newer endpoint instead of
        // inventing a midpoint from unrelated texture.
        auto cutA = image(g.width, g.height, 0, 0);
        auto cutB = cutA;
        for (size_t i = 0; i < cutB.size(); i += 4)
            for (size_t c = 0; c < 3; ++c)
                cutB[i + c] = 1.f - cutB[i + c];
        upload(g, a, cutA);
        upload(g, b, cutB);
        flow.generate(a, b);
        auto cutResult = g.read(flow.output, 4);
        double cutActual = error(cutResult, cutB, g.width, g.height, 8);
        auto cutBlend = cutA;
        for (size_t i = 0; i < cutBlend.size(); ++i)
            cutBlend[i] = (cutA[i] + cutB[i]) * .5f;
        double cutBaseline = error(cutBlend, cutB, g.width, g.height, 8);
        std::cout << "camera_cut inversion interior_MAE=" << cutActual
                  << " blend=" << cutBaseline << "\n";
        if (cutActual > 1e-7 || cutBaseline < .1)
            throw std::runtime_error("Camera-cut fallback regression");
        auto red = image(g.width, g.height, 0, 0), blue = red;
        for (size_t i = 0; i < red.size(); i += 4)
        {
            red[i] = 1;
            red[i + 1] = red[i + 2] = 0;
            blue[i] = blue[i + 1] = 0;
            blue[i + 2] = 1;
        }
        upload(g, a, red);
        upload(g, b, blue);
        flow.generate(a, b);
        if (error(g.read(flow.output, 4), blue, g.width, g.height, 0) > 1e-7)
            throw std::runtime_error("Unreliable motion fallback regression");
        auto bgra = g.texture(DXGI_FORMAT_B8G8R8A8_UNORM, D3D12_RESOURCE_FLAG_NONE);
        std::vector<unsigned> pixels(size_t(g.width) * g.height, 0xff804020u);
        uploadBytes(g, bgra, pixels.data(), g.width * 4);
        flow.convert(bgra, a);
        auto converted = g.read(a, 4);
        auto decode = [](float s) {
            return s <= .04045f ? s / 12.92f : std::pow((s + .055f) / 1.055f, 2.4f);
        };
        float expected[] = {decode(128 / 255.f), decode(64 / 255.f), decode(32 / 255.f), 1.f};
        for (size_t i = 0; i < converted.size(); ++i)
            if (std::abs(converted[i] - expected[i % 4]) > .001f)
                throw std::runtime_error("BGRA/sRGB conversion regression");
        std::cout << "PASS: BGRA channel order and sRGB decoding (no green-tint channel swap).\n";
        g.assertClean();
        std::cout << "PASS: color-only GPU estimated motion, midpoint, endpoints, "
                     "unreliable-motion fallback.\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
