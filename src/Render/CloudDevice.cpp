#include "pch.h"
#include "Render/CloudInternal.h"
#include "Render/CloudShaders.h"

namespace RadClouds
{
    namespace detail
    {
    void ReleaseResources()
{
    auto release = [](auto*& ptr) {
        if (ptr) {
            ptr->Release();
            ptr = nullptr;
        }
    };
    release(g_raster);
    release(g_depthNone);
    release(g_depthReversed);
    release(g_depthStandard);
    release(g_blend);
    release(g_cb);
    release(g_noiseSRV);
    release(g_noiseSampler);
    release(g_clampSampler);
    release(g_compositePS);
    release(g_blendOpaque);
    for (int i = 0; i < 2; ++i) {
        release(g_cloudTex[i]);
        release(g_cloudSRV[i]);
        release(g_cloudRTV[i]);
    }
    g_cloudWidth = 0;
    g_cloudHeight = 0;
    g_hasPrevCloud = false;
    release(g_shadowMaskTex);
    release(g_shadowMaskSRV);
    release(g_shadowMaskRTV);
    release(g_shadowMaskPS);
    release(g_cloudShadowPS);
    release(g_shadowDebugPS);
    release(g_blendMultiply);
    release(g_depthSRV);
    release(g_depthSRVRes);
    release(g_ps);
    release(g_vs);
    g_device = nullptr;
}
bool CompileShaders(ID3D11Device* device)
{
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* err = nullptr;
    const auto compile = [&err](const char* src, const char* target, ID3DBlob** out) {
        return SUCCEEDED(D3DCompile(src, std::strlen(src), nullptr, nullptr, nullptr,
            "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err));
    };
    bool ok = compile(Shaders::kFullscreenVS, "vs_5_0", &vsBlob);
    if (ok && err) { err->Release(); err = nullptr; }
    ok = ok && compile(Shaders::kCloudPS, "ps_5_0", &psBlob);
    if (ok)
    {
        ok = SUCCEEDED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_vs)) &&
             SUCCEEDED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_ps));
    }
    if (!ok)
    {
        if (auto logger = Logger())
        {
            logger->error("[Clouds] Shader compile/create failed: {}",
                err ? static_cast<const char*>(err->GetBufferPointer()) : "unknown");
        }
        if (err) err->Release();
        if (vsBlob) vsBlob->Release();
        if (psBlob) psBlob->Release();
        return false;
    }
    if (err) { err->Release(); err = nullptr; }
    vsBlob->Release();
    psBlob->Release();
    ID3DBlob* compBlob = nullptr;
    ID3DBlob* compErr = nullptr;
    if (FAILED(D3DCompile(Shaders::kCompositePS, std::strlen(Shaders::kCompositePS), nullptr, nullptr, nullptr,
            "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &compBlob, &compErr)) || !compBlob) {
        if (auto logger = Logger())
            logger->error("[Clouds] Composite shader compile failed: {}",
                compErr ? static_cast<const char*>(compErr->GetBufferPointer()) : "unknown");
        if (compErr) compErr->Release();
        if (compBlob) compBlob->Release();
        return false;
    }
    if (compErr) compErr->Release();
    const bool compOk = SUCCEEDED(device->CreatePixelShader(compBlob->GetBufferPointer(), compBlob->GetBufferSize(), nullptr, &g_compositePS));
    compBlob->Release();
    if (!compOk) return false;
    D3D11_BLEND_DESC opaqueDesc{};
    opaqueDesc.RenderTarget[0].BlendEnable = FALSE;
    opaqueDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateBlendState(&opaqueDesc, &g_blendOpaque))) return false;
    D3D11_SAMPLER_DESC clampDesc{};
    clampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    clampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    clampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    clampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    clampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&clampDesc, &g_clampSampler))) return false;

    const auto compilePS = [&](const char* src, ID3D11PixelShader** out) {
        ID3DBlob* blob = nullptr;
        ID3DBlob* error = nullptr;
        const bool ok = SUCCEEDED(D3DCompile(src, std::strlen(src), nullptr, nullptr, nullptr, "main", "ps_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &error)) && blob &&
            SUCCEEDED(device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, out));
        if (!ok && Logger())
            Logger()->error("[Clouds] Shadow shader compile failed: {}", error ? static_cast<const char*>(error->GetBufferPointer()) : "unknown");
        if (error) error->Release();
        if (blob) blob->Release();
        return ok;
    };
    compilePS(Shaders::kDepthProbePS, &g_depthProbePS);
    const bool shadowMaskOk = compilePS(Shaders::kShadowMaskPS, &g_shadowMaskPS);
    const bool shadowMainOk = compilePS(Shaders::kCloudShadowPS, &g_cloudShadowPS);
    const bool shadowDebugOk = compilePS(Shaders::kShadowDebugPS, &g_shadowDebugPS);
    g_shadowReady = shadowMaskOk && shadowMainOk && shadowDebugOk;
    if (!g_shadowReady && Logger())
        Logger()->warn("[Clouds] Cloud shadow shaders unavailable; shadows disabled, clouds continue.");

    D3D11_BLEND_DESC multiplyDesc{};
    multiplyDesc.RenderTarget[0].BlendEnable = TRUE;
    multiplyDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_DEST_COLOR;
    multiplyDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
    multiplyDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    multiplyDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    multiplyDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    multiplyDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    multiplyDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateBlendState(&multiplyDesc, &g_blendMultiply))) {
        g_shadowReady = false;
        if (Logger()) Logger()->warn("[Clouds] Multiply blend state creation failed; shadows disabled.");
    } else if (Logger()) {
        Logger()->info("[Clouds] Multiply blend state created (DEST_COLOR/ZERO); shadow shaders ok={}.", g_shadowReady);
    }
    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth = sizeof(CloudCB);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateBuffer(&cbDesc, nullptr, &g_cb))) {
        if (auto logger = Logger()) logger->error("[Clouds] Constant buffer creation failed.");
        return false;
    }
    D3D11_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateBlendState(&blendDesc, &g_blend))) return false;
    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    ds.StencilEnable = FALSE;
    if (FAILED(device->CreateDepthStencilState(&ds, &g_depthStandard))) return false;
    ds.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
    if (FAILED(device->CreateDepthStencilState(&ds, &g_depthReversed))) return false;
    ds.DepthEnable = FALSE;
    if (FAILED(device->CreateDepthStencilState(&ds, &g_depthNone))) return false;
    D3D11_RASTERIZER_DESC rs{};
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_NONE;
    rs.DepthClipEnable = FALSE;
    rs.ScissorEnable = FALSE;
    if (FAILED(device->CreateRasterizerState(&rs, &g_raster))) return false;
    return true;
}
bool LoadNoiseVolume(ID3D11Device* device)
{
    constexpr const char* path = "Data/F4SE/Plugins/VolClouds/cloudnoise.dds";
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    const auto size = static_cast<std::size_t>(file.tellg());
    if (size < 148 + 65536) return false;
    std::vector<char> bytes(size);
    file.seekg(0);
    file.read(bytes.data(), static_cast<std::streamsize>(size));
    if (std::memcmp(bytes.data(), "DDS ", 4) != 0) return false;
    const auto* h = reinterpret_cast<const std::uint32_t*>(bytes.data() + 4);
    const std::uint32_t height = h[2], width = h[3], depth = h[5];
    if (h[31] != 28 || h[32] != 4 || width == 0 || height == 0 || depth == 0 || width > 512 || height > 512 || depth > 512) return false;
    D3D11_TEXTURE3D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.Depth = depth; desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = bytes.data() + 148; init.SysMemPitch = width * 4; init.SysMemSlicePitch = width * height * 4;
    ID3D11Texture3D* texture = nullptr;
    if (device->CreateTexture3D(&desc, &init, &texture) < 0 || !texture) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = desc.Format; srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D; srv.Texture3D.MipLevels = 1;
    const long hr = static_cast<long>(device->CreateShaderResourceView(texture, &srv, &g_noiseSRV));
    texture->Release();
    if (hr < 0) return false;
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP; sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (device->CreateSamplerState(&sd, &g_noiseSampler) < 0) return false;
    if (auto logger = Logger()) logger->info("[Clouds] Noise volume loaded {}x{}x{}.", width, height, depth);
    return true;
}
bool EnsureCloudTextures(ID3D11Device* device, std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0) return false;
    if (g_cloudWidth == width && g_cloudHeight == height && g_cloudTex[0]) return true;
    for (int i = 0; i < 2; ++i) {
        if (g_cloudTex[i]) { g_cloudTex[i]->Release(); g_cloudTex[i] = nullptr; }
        if (g_cloudSRV[i]) { g_cloudSRV[i]->Release(); g_cloudSRV[i] = nullptr; }
        if (g_cloudRTV[i]) { g_cloudRTV[i]->Release(); g_cloudRTV[i] = nullptr; }
    }
    g_cloudWidth = width;
    g_cloudHeight = height;
    g_hasPrevCloud = false;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    for (int i = 0; i < 2; ++i) {
        if (device->CreateTexture2D(&desc, nullptr, &g_cloudTex[i]) < 0) return false;
        if (device->CreateShaderResourceView(g_cloudTex[i], nullptr, &g_cloudSRV[i]) < 0) return false;
        if (device->CreateRenderTargetView(g_cloudTex[i], nullptr, &g_cloudRTV[i]) < 0) return false;
    }
    return true;
}
bool EnsureShadowMask(ID3D11Device* device)
{
    if (g_shadowMaskTex) return true;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 128;
    desc.Height = 128;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    if (device->CreateTexture2D(&desc, nullptr, &g_shadowMaskTex) < 0) return false;
    if (device->CreateShaderResourceView(g_shadowMaskTex, nullptr, &g_shadowMaskSRV) < 0) return false;
    if (device->CreateRenderTargetView(g_shadowMaskTex, nullptr, &g_shadowMaskRTV) < 0) return false;
    return true;
}

ID3D11ShaderResourceView* DepthSRVFrom(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* dsv)
{
    if (!dsv) return nullptr;
    ID3D11Resource* res = nullptr;
    dsv->GetResource(&res);
    if (!res) return nullptr;
    if (res == g_depthSRVRes && g_depthSRV) { res->Release(); return g_depthSRV; }
    if (g_depthSRV) { g_depthSRV->Release(); g_depthSRV = nullptr; }
    if (g_depthSRVRes) { g_depthSRVRes->Release(); g_depthSRVRes = nullptr; }
    ID3D11Texture2D* tex = nullptr;
    res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex));
    D3D11_TEXTURE2D_DESC desc{};
    if (tex) { tex->GetDesc(&desc); tex->Release(); }
    DXGI_FORMAT srvFormat = DXGI_FORMAT_UNKNOWN;
    switch (desc.Format) {
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_R32_FLOAT:
        srvFormat = DXGI_FORMAT_R32_FLOAT;
        break;
    case DXGI_FORMAT_R24G8_TYPELESS:
        srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        break;
    case DXGI_FORMAT_R16_TYPELESS:
        srvFormat = DXGI_FORMAT_R16_UNORM;
        break;
    case DXGI_FORMAT_R32G8X24_TYPELESS:
        srvFormat = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        break;
    default:
        break;
    }
    if (srvFormat == DXGI_FORMAT_UNKNOWN) {
        res->Release();
        if (!g_loggedShadowNoDepth && Logger()) {
            g_loggedShadowNoDepth = true;
            Logger()->warn("[Clouds] Scene depth format {} has no sampleable SRV; depth-based effects disabled.", static_cast<int>(desc.Format));
        }
        return nullptr;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = srvFormat;
    srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    if (g_device->CreateShaderResourceView(res, &srv, &g_depthSRV) < 0) {
        g_depthSRV = nullptr;
        res->Release();
        if (!g_loggedShadowNoDepth && Logger()) {
            g_loggedShadowNoDepth = true;
            Logger()->warn("[Clouds] Scene depth SRV creation failed (texture format {}, SRV format {}).", static_cast<int>(desc.Format), static_cast<int>(srvFormat));
        }
        return nullptr;
    }
    if (Logger())
        Logger()->info("[Clouds] Scene depth SRV created: texture format {} -> SRV format {}, {}x{}.", static_cast<int>(desc.Format), static_cast<int>(srvFormat), desc.Width, desc.Height);
    g_depthSRVRes = res;
    return g_depthSRV;
}

bool EnsureProbeResources(ID3D11Device* device)
{
    if (g_probeTex) return true;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 64; desc.Height = 36; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32_FLOAT; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (device->CreateTexture2D(&desc, nullptr, &g_probeTex) < 0) return false;
    if (device->CreateRenderTargetView(g_probeTex, nullptr, &g_probeRTV) < 0) return false;
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (device->CreateTexture2D(&desc, nullptr, &g_probeReadback) < 0) return false;
    return true;
}

bool EnsureDeviceResources()
{
    auto* data = RE::BSGraphics::GetRendererData();
    if (!data) return false;
    void* rawDevice = data->device;
    void* rawContext = data->context;
    if (!rawDevice || !rawContext) return false;
    if (g_failedDevice == rawDevice) return false;
    if (g_device == reinterpret_cast<ID3D11Device*>(rawDevice) && g_vs && g_ps) return true;
    ReleaseResources();
    g_device = reinterpret_cast<ID3D11Device*>(rawDevice);
    if (!CompileShaders(g_device)) {
        ReleaseResources();
        g_failedDevice = rawDevice;
        return false;
    }
    LoadNoiseVolume(g_device);
    if (auto logger = Logger())
        logger->info("[Clouds] D3D11 resources created for device {:p}.", static_cast<void*>(g_device));
    return true;
}
    }
}
