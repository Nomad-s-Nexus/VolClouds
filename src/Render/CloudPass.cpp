#include "pch.h"
#include "Render/CloudConfig.h"
#include "Render/CloudPass.h"
#include "Render/CloudShaders.h"
#include <RE/Fallout.h>
#if __has_include(<RE/C/Clouds.h>) && __has_include(<RE/B/BSSkyShaderProperty.h>)
#define VOLCLOUDS_TYPED_SKY 1
#else
#define VOLCLOUDS_TYPED_SKY 0
#endif
#include <RE/B/BSGraphics.h>
#include <RE/B/BSSkyShader.h>
#include <RE/N/NiBillboardNode.h>
#include <RE/S/Sky.h>
#include <RE/S/Sun.h>
#include <RE/T/TESWeather.h>
#ifdef MEM_RELEASE
#undef MEM_RELEASE
#endif
#ifdef constant
#undef constant
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
namespace RadClouds
{
    namespace
    {
        constexpr std::size_t kSetupGeometryVTableSlot = 7; using Config = CloudConfig;
        struct alignas(16) CloudCB
        {
            DirectX::XMFLOAT4X4 invViewProj; DirectX::XMFLOAT4 cameraPos;
            DirectX::XMFLOAT4 sunDir;
            DirectX::XMFLOAT4 sunColor; DirectX::XMFLOAT4 ambientColor;
            DirectX::XMFLOAT4 windAndTime;
            DirectX::XMFLOAT4 params0;
            DirectX::XMFLOAT4 params1; DirectX::XMFLOAT4 params2;
            DirectX::XMFLOAT4 params3;
            DirectX::XMFLOAT4 params4;
            DirectX::XMFLOAT4 params5;
            DirectX::XMFLOAT4 params6;
            DirectX::XMFLOAT4 params7;
            DirectX::XMFLOAT4 params8;
            DirectX::XMFLOAT4 params9;
            DirectX::XMFLOAT4 params10;
            DirectX::XMFLOAT4 params11;
            DirectX::XMFLOAT4 params12;
            DirectX::XMFLOAT4 params13;
        };
        struct FrameState
        {
            bool valid{ false };
            DirectX::XMFLOAT4X4 invViewProj{};
            DirectX::XMFLOAT4X4 viewProj{};
            DirectX::XMFLOAT3 cameraPos{};
            DirectX::XMFLOAT3 sunDir{ 0.0f, 1.0f, 0.0f };
            float sunIntensity{ 1.0f };
            DirectX::XMFLOAT3 sunColor{ 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT3 ambientColor{ 0.2f, 0.24f, 0.3f };
            float windAngle{ 0.0f };
            float windSpeed{ 0.0f };
            float coverage{ 0.5f };
            float flash{ 0.0f };
            float stormFactor{ 0.0f };
            float time{ 0.0f };
            bool reversedDepth{ false };
            float nearZ{ 0.0f };
            float farZ{ 1.0f };
            DirectX::XMFLOAT3 posAdjust{};
            const char* sourceName{ nullptr };
            bool hadMatrix{ false };
            float matrixSample[4]{};
        };
        using SetupGeometryFn = void (*)(void*, void*);
        std::mutex g_mutex;
        Config g_config;
        bool g_installed{ false };
        bool g_loggedNoDepth{ false };
        bool g_loggedTargetSkip{ false };
        std::uint32_t g_lastFrame{ 0xFFFFFFFFu };
        std::uint32_t g_frameBestArea{ 0 };
        std::uint32_t g_frameDraws{ 0 };
        std::uint32_t g_frameMainDraws{ 0 };
        std::uint32_t g_frameSecondaryDraws{ 0 };
        std::uint32_t g_frameSizes[4]{};
        std::uint32_t g_loggedFrameDraws{ 0 };
        void* g_lastTarget{ nullptr };
        std::uint64_t g_hookCalls{ 0 };
        std::uint64_t g_draws{ 0 };
        std::uint64_t g_skipDisabled{ 0 };
        std::uint64_t g_skipMode{ 0 };
        std::uint64_t g_skipPass{ 0 };
        std::uint64_t g_skipTarget{ 0 };
        std::uint64_t g_skipDevice{ 0 };
        std::uint64_t g_skipGather{ 0 };
        bool g_firstHookLogged{ false };
        bool g_disabledLogged{ false };
        bool g_modeLogged{ false };
        bool g_passLogged{ false };
        bool g_targetLogged{ false };
        bool g_deviceLogged{ false };
        bool g_gatherLogged{ false };
        bool g_firstDrawLogged{ false };
        int g_debugOverride{ -1 };
        int g_suppressOverride{ -1 };
        bool g_hotkeyDebugDown{ false };
        bool g_hotkeySuppressDown{ false };
        bool g_firstCloudPassLogged{ false };
        bool g_fallbackLogged{ false };
        bool g_fallbackMode{ false };
        std::uint32_t g_skyTypeCounts[16]{};
        void* g_failedDevice{ nullptr };
        std::chrono::steady_clock::time_point g_startTime{};
        REL::Relocation<SetupGeometryFn> g_original;
        ID3D11Device* g_device{ nullptr };
        ID3D11VertexShader* g_vs{ nullptr };
        ID3D11PixelShader* g_ps{ nullptr };
        ID3D11Buffer* g_cb{ nullptr };
        ID3D11BlendState* g_blend{ nullptr };
        ID3D11DepthStencilState* g_depthStandard{ nullptr };
        ID3D11DepthStencilState* g_depthReversed{ nullptr };
        ID3D11DepthStencilState* g_depthNone{ nullptr };
        ID3D11RasterizerState* g_raster{ nullptr };
        ID3D11ShaderResourceView* g_noiseSRV{ nullptr };
        ID3D11SamplerState* g_noiseSampler{ nullptr };
        ID3D11SamplerState* g_clampSampler{ nullptr };
        ID3D11BlendState* g_blendOpaque{ nullptr };
        ID3D11PixelShader* g_compositePS{ nullptr };
        ID3D11Texture2D* g_cloudTex[2]{};
        ID3D11ShaderResourceView* g_cloudSRV[2]{};
        ID3D11RenderTargetView* g_cloudRTV[2]{};
        std::uint32_t g_cloudWidth{ 0 };
        std::uint32_t g_cloudHeight{ 0 };
        int g_cloudIndex{ 0 };
        bool g_hasPrevCloud{ false };
        DirectX::XMFLOAT4 g_prevRows[3]{};
        DirectX::XMFLOAT4 g_prevRow3{};
        ID3D11PixelShader* g_shadowMaskPS{ nullptr };
        ID3D11PixelShader* g_cloudShadowPS{ nullptr };
        ID3D11PixelShader* g_shadowDebugPS{ nullptr };
        ID3D11BlendState* g_blendMultiply{ nullptr };
        ID3D11Texture2D* g_shadowMaskTex{ nullptr };
        ID3D11ShaderResourceView* g_shadowMaskSRV{ nullptr };
        ID3D11RenderTargetView* g_shadowMaskRTV{ nullptr };
        ID3D11ShaderResourceView* g_depthSRV{ nullptr };
        ID3D11Resource* g_depthSRVRes{ nullptr };
        bool g_loggedShadowNoDepth{ false };
        bool g_shadowReady{ false };
        ID3D11PixelShader* g_depthProbePS{ nullptr };
        ID3D11Texture2D* g_probeTex{ nullptr };
        ID3D11RenderTargetView* g_probeRTV{ nullptr };
        ID3D11Texture2D* g_probeReadback{ nullptr };
        std::uint32_t g_probeCounter{ 0 };
        std::shared_ptr<spdlog::logger> Logger()
        {
            return spdlog::get("VolClouds");
        }
        void LoadConfigLocked()
        {
            CloudConfig cfg{};
            const bool loaded = LoadCloudConfig(kCloudConfigPath, cfg);
            g_config = cfg;
            if (auto logger = Logger()) {
                if (!loaded) {
                    logger->info("[Clouds] No config at '{}'; volumetric clouds disabled until enabled in that file.", kCloudConfigPath);
                } else {
                    logger->info("[Clouds] Config loaded: enabled={} density={:.2f} steps={} layer=[{:.0f}, {:.0f}] depthMode={}",
                        cfg.enabled, cfg.density, cfg.steps, cfg.layerBottom, cfg.layerTop, cfg.depthMode);
                }
            }
        }
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
        std::uint32_t SkyModeRaw(const RE::Sky* sky)
        {
            return *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const char*>(sky) + 0x36C);
        }
        float WeatherCoverage(const RE::TESWeather* weather)
        {
            if (!weather) return 0.0f;
            float coverage = 0.35f;
            const auto layers = std::min<std::uint32_t>(weather->numCloudLayers, 4u);
            for (std::uint32_t i = 0; i < layers; ++i)
            {
                if ((weather->cloudLayerDisabledBits & (1u << i)) != 0) continue;
                coverage = std::max(coverage, weather->cloudAlpha[i][1]);
            }
            const auto flags = static_cast<std::uint8_t>(weather->weatherData[0xB]);
            if (flags & 0x2) coverage = std::max(coverage, 0.45f);
            if (flags & 0x4) coverage = std::max(coverage, 0.65f);
            if (flags & 0x8) coverage = std::max(coverage, 0.70f);
            return std::clamp(coverage, 0.0f, 1.0f);
        }
        FrameState GatherFrameState(const RE::BSGraphics::State* graphicsState, const RE::Sky* sky, const Config& cfg)
        {
            FrameState out{};
            if (!graphicsState || !sky) return out;
            const RE::BSGraphics::ViewData* candidates[3]{};
            const RE::NiPoint3* adjusts[3]{};
            const char* names[3]{};
            candidates[0] = &graphicsState->cameraState.camViewData;
            adjusts[0] = &graphicsState->cameraState.posAdjust;
            names[0] = "State::cameraState";
            const auto* rendererData = RE::BSGraphics::GetRendererData();
            if (rendererData && rendererData->shadowState) {
                candidates[1] = &rendererData->shadowState->cameraData;
                adjusts[1] = &rendererData->shadowState->posAdjust;
                names[1] = "RendererData::shadowState";
            }
            if (!graphicsState->cameraDataCache.empty()) {
                const auto& last = graphicsState->cameraDataCache.back();
                candidates[2] = &last.camViewData;
                adjusts[2] = &last.posAdjust;
                names[2] = "cameraDataCache::back";
            }
            const auto finite4 = [](const DirectX::XMFLOAT4& v) {
                return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w);
            };
            const auto finiteMatrix = [&](const DirectX::XMFLOAT4X4& m) {
                for (int r = 0; r < 4; ++r)
                    if (!finite4({ m.m[r][0], m.m[r][1], m.m[r][2], m.m[r][3] })) return false;
                return true;
            };
            for (int i = 0; i < 3 && !out.hadMatrix; ++i) {
                if (!candidates[i]) continue;
                DirectX::XMFLOAT4X4 vpRaw{};
                std::memcpy(&vpRaw, &candidates[i]->viewProjMat, sizeof(vpRaw));
                if (!finiteMatrix(vpRaw)) continue;
                const float row0 = std::abs(vpRaw._11) + std::abs(vpRaw._12) + std::abs(vpRaw._13) + std::abs(vpRaw._14);
                if (row0 < 1e-6f) continue;
                DirectX::XMVECTOR det = DirectX::XMVectorZero();
                const DirectX::XMMATRIX invVp = DirectX::XMMatrixInverse(&det, DirectX::XMLoadFloat4x4(&vpRaw));
                DirectX::XMFLOAT4X4 invVpF{};
                DirectX::XMStoreFloat4x4(&invVpF, invVp);
                if (!finiteMatrix(invVpF)) continue;
                const float detScalar = DirectX::XMVectorGetX(det);
                if (!std::isfinite(detScalar)) continue;
                DirectX::XMFLOAT4X4 viewRaw{};
                std::memcpy(&viewRaw, &candidates[i]->viewMat, sizeof(viewRaw));
                if (!finiteMatrix(viewRaw)) continue;
                const DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, DirectX::XMLoadFloat4x4(&viewRaw));
                DirectX::XMFLOAT4X4 invViewF{};
                DirectX::XMStoreFloat4x4(&invViewF, invView);
                if (!finiteMatrix(invViewF)) continue;
                DirectX::XMFLOAT4X4 projRaw{};
                std::memcpy(&projRaw, &candidates[i]->projMat, sizeof(projRaw));
                out.reversedDepth = finiteMatrix(projRaw) && projRaw._33 <= 0.0f;
                out.nearZ = out.reversedDepth ? 1.0f : 0.0f;
                out.farZ = out.reversedDepth ? 0.0f : 1.0f;
                out.invViewProj = invVpF;
                out.viewProj = vpRaw;
                const auto& pa = *adjusts[i];
                out.posAdjust = { pa.x, pa.y, pa.z };
                out.cameraPos = { invViewF._41 + pa.x, invViewF._42 + pa.y, invViewF._43 + pa.z };
                out.matrixSample[0] = vpRaw._11; out.matrixSample[1] = vpRaw._12;
                out.matrixSample[2] = vpRaw._13; out.matrixSample[3] = vpRaw._14;
                out.sourceName = names[i];
                out.hadMatrix = true;
            }
            if (!out.hadMatrix) {
                DirectX::XMFLOAT4X4 vpRaw{};
                std::memcpy(&vpRaw, &graphicsState->cameraState.camViewData.viewProjMat, sizeof(vpRaw));
                out.matrixSample[0] = vpRaw._11; out.matrixSample[1] = vpRaw._12;
                out.matrixSample[2] = vpRaw._13; out.matrixSample[3] = vpRaw._14;
                return out;
            }
            if (sky->sun && sky->sun->light)
            {
                const auto* lightNode = reinterpret_cast<const RE::NiAVObject*>(sky->sun->light.get());
                const auto& rot = lightNode->GetWorldRotate();
                DirectX::XMFLOAT3 dir{ -rot[0][2], -rot[1][2], -rot[2][2] };
                if (dir.z < 0.0f) {
                    dir.x = -dir.x; dir.y = -dir.y; dir.z = -dir.z;
                }
                const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                if (len > 1e-4f) {
                    out.sunDir = { dir.x / len, dir.y / len, dir.z / len };
                }
            }
            if (out.sunDir.z <= 0.01f)
            {
                const float hour = std::clamp(sky->currentGameHour, 0.0f, 24.0f);
                const float angle = (hour - 6.0f) / 12.0f * DirectX::XM_PI;
                out.sunDir = { std::cos(angle), std::sin(angle), 0.35f };
                const float len = std::sqrt(out.sunDir.x * out.sunDir.x + out.sunDir.y * out.sunDir.y + 0.1225f);
                out.sunDir = { out.sunDir.x / len, out.sunDir.y / len, 0.35f / len };
            }
            const float dayBase = std::clamp(out.sunDir.z * 1.6f + 0.25f, 0.05f, 1.0f);
            out.sunDir.z = std::max(out.sunDir.z, 0.15f);
            const RE::NiColor sunTint = sky->skyColor[4];
            const RE::NiColor skyUpper = sky->skyColor[0];
            out.sunColor = { std::max(sunTint.r, 0.15f) * dayBase, std::max(sunTint.g, 0.15f) * dayBase,
                std::max(sunTint.b, 0.15f) * dayBase };
            out.sunIntensity = cfg.sunIntensity * dayBase;
            const float ambientScale = 0.9f + cfg.ambient * 2.0f;
            out.ambientColor = { std::max(skyUpper.r, 0.05f) * ambientScale, std::max(skyUpper.g, 0.06f) * ambientScale, std::max(skyUpper.b, 0.08f) * ambientScale };
            const float pct = std::clamp(sky->currentWeatherPct, 0.0f, 1.0f);
            const float currentCoverage = WeatherCoverage(sky->currentWeather);
            const float lastCoverage = sky->lastWeather ? WeatherCoverage(sky->lastWeather) : currentCoverage;
            out.coverage = std::clamp((lastCoverage + (currentCoverage - lastCoverage) * pct) * cfg.coverageScale, 0.0f, 0.92f);
            const bool isStorm = out.stormFactor > 0.5f;
            const bool isCloudy = !isStorm && out.coverage > 0.45f;
            if (isStorm) {
                out.coverage = std::clamp(out.coverage + cfg.stormCoverageBoost, 0.0f, 0.95f);
                out.windSpeed *= 1.4f;
            } else if (!isCloudy) {
                out.coverage *= cfg.clearCoverageScale;
            }
            const float sunElevation01 = std::clamp(out.sunDir.z, 0.0f, 1.0f);
            const float sunset = 1.0f - std::clamp(sunElevation01 / 0.35f, 0.0f, 1.0f);
            if (sunset > 0.0f) {
                const float warmth = sunset * cfg.sunsetWarmth;
                out.sunColor.x *= 1.0f + warmth;
                out.sunColor.z *= 1.0f - warmth * 0.35f;
                out.ambientColor.x *= 1.0f + warmth * 0.4f;
                out.ambientColor.z *= 1.0f - warmth * 0.15f;
            }
            out.windAngle = sky->windAngle;
            out.windSpeed = sky->windSpeed * cfg.windDrift;
            out.flash = std::clamp(sky->flash, 0.0f, 1.0f);
            if (const auto* weather = sky->currentWeather) {
                const auto flags = static_cast<std::uint8_t>(weather->weatherData[0xB]);
                out.stormFactor = (flags & 0x4) ? 0.9f : ((flags & 0x8) ? 0.9f : ((flags & 0x2) ? 0.35f : 0.0f));
            }
            out.time = static_cast<float>(std::chrono::duration<double>(std::chrono::steady_clock::now() - g_startTime).count());
            out.valid = true;
            return out;
        }
        struct CachedShaderState
        {
            ID3D11VertexShader* vs{};
            ID3D11PixelShader* ps{};
            ID3D11GeometryShader* gs{};
            ID3D11HullShader* hs{};
            ID3D11DomainShader* ds{};
            ID3D11InputLayout* layout{};
            D3D11_PRIMITIVE_TOPOLOGY topology{ D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST };
            ID3D11Buffer* vbs[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT]{};
            UINT vbStrides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT]{};
            UINT vbOffsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT]{};
            ID3D11Buffer* ib{};
            DXGI_FORMAT ibFormat{ DXGI_FORMAT_UNKNOWN };
            UINT ibOffset{};
            ID3D11BlendState* blend{};
            FLOAT blendFactor[4]{};
            UINT blendMask{};
            ID3D11DepthStencilState* depth{};
            UINT stencilRef{};
            ID3D11RasterizerState* raster{};
            D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
            UINT viewportCount{};
            D3D11_RECT scissorRects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
            UINT scissorCount{};
            ID3D11Buffer* vsCbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT]{};
            ID3D11Buffer* psCbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT]{};
        };
        class DrawScope
        {
        public:
            explicit DrawScope(ID3D11DeviceContext* ctx) : ctx_(ctx)
            {
                if (!ctx_) return;
                ctx_->VSGetShader(&s_.vs, nullptr, nullptr);
                ctx_->PSGetShader(&s_.ps, nullptr, nullptr);
                ctx_->GSGetShader(&s_.gs, nullptr, nullptr);
                ctx_->HSGetShader(&s_.hs, nullptr, nullptr);
                ctx_->DSGetShader(&s_.ds, nullptr, nullptr);
                ctx_->IAGetInputLayout(&s_.layout);
                ctx_->IAGetPrimitiveTopology(&s_.topology);
                ctx_->IAGetVertexBuffers(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, s_.vbs, s_.vbStrides, s_.vbOffsets);
                ctx_->IAGetIndexBuffer(&s_.ib, &s_.ibFormat, &s_.ibOffset);
                ctx_->OMGetBlendState(&s_.blend, s_.blendFactor, &s_.blendMask);
                ctx_->OMGetDepthStencilState(&s_.depth, &s_.stencilRef);
                ctx_->RSGetState(&s_.raster);
                s_.viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
                ctx_->RSGetViewports(&s_.viewportCount, s_.viewports);
                s_.scissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
                ctx_->RSGetScissorRects(&s_.scissorCount, s_.scissorRects);
                ctx_->VSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, s_.vsCbs);
                ctx_->PSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, s_.psCbs);
            }
            ~DrawScope()
            {
                if (!ctx_) return;
                ctx_->VSSetShader(s_.vs, nullptr, 0);
                ctx_->PSSetShader(s_.ps, nullptr, 0);
                ctx_->GSSetShader(s_.gs, nullptr, 0);
                ctx_->HSSetShader(s_.hs, nullptr, 0);
                ctx_->DSSetShader(s_.ds, nullptr, 0);
                ctx_->IASetInputLayout(s_.layout);
                ctx_->IASetPrimitiveTopology(s_.topology);
                ctx_->IASetVertexBuffers(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, s_.vbs, s_.vbStrides, s_.vbOffsets);
                ctx_->IASetIndexBuffer(s_.ib, s_.ibFormat, s_.ibOffset);
                ctx_->OMSetBlendState(s_.blend, s_.blendFactor, s_.blendMask);
                ctx_->OMSetDepthStencilState(s_.depth, s_.stencilRef);
                ctx_->RSSetState(s_.raster);
                ctx_->RSSetViewports(s_.viewportCount, s_.viewports);
                ctx_->RSSetScissorRects(s_.scissorCount, s_.scissorRects);
                ctx_->VSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, s_.vsCbs);
                ctx_->PSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, s_.psCbs);
                ReleaseAll();
            }
            DrawScope(const DrawScope&) = delete;
            DrawScope& operator=(const DrawScope&) = delete;
        private:
            void ReleaseAll()
            {
                auto release = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
                release(s_.vs); release(s_.ps); release(s_.gs); release(s_.hs); release(s_.ds);
                release(s_.layout); release(s_.ib); release(s_.blend); release(s_.depth); release(s_.raster);
                for (auto*& p : s_.vbs) release(p);
                for (auto*& p : s_.vsCbs) release(p);
                for (auto*& p : s_.psCbs) release(p);
            }
            ID3D11DeviceContext* ctx_{};
            CachedShaderState s_{};
        };
        struct TargetInfo
        {
            std::uint32_t format{ 0 };
            std::uint32_t width{ 0 };
            std::uint32_t height{ 0 };
            std::uint32_t arraySize{ 0 };
            std::uint32_t samples{ 0 };
            void* textureId{ nullptr };
        };
        bool IsMainSceneTarget(ID3D11DeviceContext* ctx, std::uint32_t expectedWidth, std::uint32_t expectedHeight, TargetInfo* outInfo)
        {
            ID3D11RenderTargetView* rtv = nullptr;
            ctx->OMGetRenderTargets(1, &rtv, nullptr);
            if (!rtv) return false;
            ID3D11Resource* res = nullptr;
            rtv->GetResource(&res);
            rtv->Release();
            if (!res) return false;
            ID3D11Texture2D* tex = nullptr;
            res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex));
            res->Release();
            if (!tex) return false;
            D3D11_TEXTURE2D_DESC desc{};
            tex->GetDesc(&desc);
            tex->Release();
            if (outInfo) {
                *outInfo = { static_cast<std::uint32_t>(desc.Format), desc.Width, desc.Height, desc.ArraySize, desc.SampleDesc.Count };
                outInfo->textureId = tex;
            }
            if (desc.ArraySize > 1) return false;
            if (desc.SampleDesc.Count > 1) return false;
            if (desc.Format != DXGI_FORMAT_R11G11B10_FLOAT && desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) return false;
            const auto NearSize = [](std::uint32_t a, std::uint32_t b) {
                const auto diff = a > b ? a - b : b - a;
                return diff <= b / 4;
            };
            return NearSize(desc.Width, expectedWidth) && NearSize(desc.Height, expectedHeight);
        }
        void RenderClouds(ID3D11DeviceContext* ctx, const FrameState& frame, const Config& cfg, const TargetInfo& target)
        {
            if (!g_vs || !g_ps || !g_cb) return;
            if (!EnsureDeviceResources()) return;
            if (!ctx || !g_vs || !g_ps || !g_cb) return;
            ID3D11RenderTargetView* rtv = nullptr;
            ID3D11DepthStencilView* dsv = nullptr;
            ctx->OMGetRenderTargets(1, &rtv, &dsv);
            if (!rtv) {
                if (dsv) dsv->Release();
                return;
            }
            rtv->Release();
            CloudCB cb{};
            cb.invViewProj = frame.invViewProj;
            cb.cameraPos = { frame.cameraPos.x, frame.cameraPos.y, frame.cameraPos.z, 0.0f };
            cb.sunDir = { frame.sunDir.x, frame.sunDir.y, frame.sunDir.z, frame.sunIntensity };
            cb.sunColor = { frame.sunColor.x, frame.sunColor.y, frame.sunColor.z, 0.0f };
            cb.ambientColor = { frame.ambientColor.x, frame.ambientColor.y, frame.ambientColor.z, 0.0f };
            cb.windAndTime = { frame.windAngle, frame.windSpeed, frame.time, frame.coverage };
            cb.params0 = { cfg.layerBottom, cfg.layerTop, cfg.density, static_cast<float>(cfg.steps) };
            cb.params1 = { cfg.maxDistance, cfg.noiseScale, frame.flash, cfg.blendAlpha };
            cb.params2 = { frame.farZ, frame.nearZ, frame.farZ, static_cast<float>(cfg.debugMode) };
            cb.params3 = { cfg.layer2Bottom, cfg.layer2Top, cfg.layer2Density, cfg.layer2Coverage };
            cb.params4 = { cfg.phaseG, cfg.powder, cfg.ambientBoost, cfg.horizonFade };
            cb.params5 = { frame.stormFactor * cfg.stormDarken, frame.stormFactor, cfg.cirrus, frame.coverage };
            cb.params6 = { g_noiseSRV ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
            cb.params7 = g_hasPrevCloud ? g_prevRows[0] : DirectX::XMFLOAT4{ 0, 0, 0, 0 };
            cb.params8 = g_hasPrevCloud ? g_prevRows[1] : DirectX::XMFLOAT4{ 0, 0, 0, 0 };
            cb.params9 = g_hasPrevCloud ? g_prevRows[2] : DirectX::XMFLOAT4{ 0, 0, 0, 0 };
            cb.params10 = { g_hasPrevCloud ? cfg.temporalBlend : 0.0f, 1.0f, cfg.fogCoupling, cfg.sunShafts };
            cb.params11 = { frame.cameraPos.x, frame.cameraPos.y, cfg.shadowExtent, 128.0f };
            cb.params12 = { cfg.shadowStrength, 0.0f, frame.reversedDepth ? 1.0f : 0.0f, static_cast<float>(cfg.shadowDebug) };
            cb.params13 = g_hasPrevCloud ? g_prevRow3 : DirectX::XMFLOAT4{ 0, 0, 0, 0 };
            const D3D11_MAPPED_SUBRESOURCE mapped = [&]() {
                D3D11_MAPPED_SUBRESOURCE m{};
                ctx->Map(g_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
                return m;
            }();
            if (mapped.pData) {
                std::memcpy(mapped.pData, &cb, sizeof(cb));
                ctx->Unmap(g_cb, 0);
            }
            DrawScope scope(ctx);
            const std::uint32_t fullWidth = target.width ? target.width : 1920u;
            const std::uint32_t fullHeight = target.height ? target.height : 1080u;
            const float scale = std::clamp(cfg.renderScale, 0.25f, 1.0f);
            const std::uint32_t halfWidth = std::max(1u, static_cast<std::uint32_t>(fullWidth * scale));
            const std::uint32_t halfHeight = std::max(1u, static_cast<std::uint32_t>(fullHeight * scale));
            if (!EnsureCloudTextures(g_device, halfWidth, halfHeight)) return;
            const bool useDepth = dsv != nullptr;
            if (dsv) dsv->Release();
            const bool reversed = cfg.depthMode == 2 || (cfg.depthMode == 0 && frame.reversedDepth);
            const bool noDepth = !useDepth || cfg.depthMode == 3 || cfg.debugMode != 0;
            ID3D11DepthStencilState* depthState = noDepth ? g_depthNone : (reversed ? g_depthReversed : g_depthStandard);
            if (!useDepth && !g_loggedNoDepth) {
                g_loggedNoDepth = true;
                if (auto logger = Logger()) logger->warn("[Clouds] No depth bound at sky pass; drawing without depth test.");
            }
            const int current = g_cloudIndex;
            const int previous = 1 - current;
            ID3D11RenderTargetView* mainRTV = nullptr;
            ID3D11DepthStencilView* mainDSV = nullptr;
            ctx->OMGetRenderTargets(1, &mainRTV, &mainDSV);
            if (!mainRTV) {
                if (mainDSV) mainDSV->Release();
                return;
            }
            D3D11_VIEWPORT savedViewport{};
            UINT viewportCount = 1;
            ctx->RSGetViewports(&viewportCount, &savedViewport);
            ctx->IASetInputLayout(nullptr);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ctx->VSSetShader(g_vs, nullptr, 0);
            ctx->GSSetShader(nullptr, nullptr, 0);
            ctx->HSSetShader(nullptr, nullptr, 0);
            ctx->DSSetShader(nullptr, nullptr, 0);
            ctx->VSSetConstantBuffers(0, 1, &g_cb);
            ctx->PSSetConstantBuffers(0, 1, &g_cb);
            {
                D3D11_VIEWPORT halfViewport{ 0.0f, 0.0f, static_cast<float>(halfWidth), static_cast<float>(halfHeight), 0.0f, 1.0f };
                ctx->RSSetViewports(1, &halfViewport);
                ctx->OMSetRenderTargets(1, &g_cloudRTV[current], nullptr);
                const float clear[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
                ctx->ClearRenderTargetView(g_cloudRTV[current], clear);
                ctx->PSSetShader(g_ps, nullptr, 0);
                ctx->PSSetShaderResources(0, 1, &g_noiseSRV);
                ctx->PSSetSamplers(0, 1, &g_noiseSampler);
                ctx->OMSetBlendState(g_blendOpaque, nullptr, 0xFFFFFFFF);
                ctx->OMSetDepthStencilState(g_depthNone, 0);
                ctx->RSSetState(g_raster);
                ctx->Draw(3, 0);
            }
            if (cfg.cloudShadows && g_shadowReady && EnsureShadowMask(g_device)) {
                D3D11_VIEWPORT maskViewport{ 0.0f, 0.0f, 128.0f, 128.0f, 0.0f, 1.0f };
                ctx->RSSetViewports(1, &maskViewport);
                ctx->OMSetRenderTargets(1, &g_shadowMaskRTV, nullptr);
                ctx->PSSetShader(g_shadowMaskPS, nullptr, 0);
                ctx->PSSetShaderResources(0, 1, &g_noiseSRV);
                ctx->PSSetSamplers(0, 1, &g_noiseSampler);
                ctx->OMSetBlendState(g_blendOpaque, nullptr, 0xFFFFFFFF);
                ctx->OMSetDepthStencilState(g_depthNone, 0);
                ctx->RSSetState(g_raster);
                ctx->Draw(3, 0);
            }

            ctx->RSSetViewports(1, &savedViewport);
            ctx->OMSetRenderTargets(1, &mainRTV, mainDSV);
            ID3D11ShaderResourceView* compositeSRVs[2]{ g_cloudSRV[current], g_cloudSRV[previous] };
            ID3D11ShaderResourceView* compositeDepth = DepthSRVFrom(ctx, mainDSV);
            if (!compositeDepth) {
                D3D11_MAPPED_SUBRESOURCE patch{};
                if (ctx->Map(g_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &patch) >= 0) {
                    cb.params12.y = 0.0f;
                    std::memcpy(patch.pData, &cb, sizeof(cb));
                    ctx->Unmap(g_cb, 0);
                }
            }
            ctx->PSSetShader(g_compositePS, nullptr, 0);
            ctx->PSSetShaderResources(0, 2, compositeSRVs);
            ctx->PSSetShaderResources(4, 1, &compositeDepth);
            ctx->PSSetSamplers(0, 1, &g_clampSampler);
            ctx->OMSetBlendState(g_blend, nullptr, 0xFFFFFFFF);
            ctx->OMSetDepthStencilState(compositeDepth ? g_depthNone : depthState, 0);
            ctx->RSSetState(g_raster);
            ctx->Draw(3, 0);
            ID3D11ShaderResourceView* noDepthSRV = nullptr;
            ctx->PSSetShaderResources(4, 1, &noDepthSRV);
            if (cfg.cloudShadows && g_shadowReady) {
                ID3D11ShaderResourceView* depthSRV = DepthSRVFrom(ctx, mainDSV);
                if (depthSRV && g_shadowMaskSRV) {
                    ctx->PSSetShader(g_cloudShadowPS, nullptr, 0);
                    ctx->PSSetShaderResources(2, 1, &depthSRV);
                    ctx->PSSetShaderResources(3, 1, &g_shadowMaskSRV);
                    ctx->PSSetSamplers(0, 1, &g_clampSampler);
                    ctx->OMSetBlendState(g_blendMultiply, nullptr, 0xFFFFFFFF);
                    ctx->OMSetDepthStencilState(g_depthNone, 0);
                    ctx->RSSetState(g_raster);
                    ctx->Draw(3, 0);
                }
            }

            if (cfg.shadowDebug > 0 && g_shadowReady && g_shadowMaskSRV) {
                ctx->PSSetShader(g_shadowDebugPS, nullptr, 0);
                ctx->PSSetShaderResources(3, 1, &g_shadowMaskSRV);
                ctx->PSSetSamplers(0, 1, &g_clampSampler);
                ctx->OMSetBlendState(g_blendOpaque, nullptr, 0xFFFFFFFF);
                ctx->OMSetDepthStencilState(g_depthNone, 0);
                ctx->RSSetState(g_raster);
                ctx->Draw(3, 0);
            }

            if (++g_probeCounter >= 600 && g_depthProbePS && mainDSV) {
                g_probeCounter = 0;
                ID3D11ShaderResourceView* probeDepth = DepthSRVFrom(ctx, mainDSV);
                if (probeDepth && EnsureProbeResources(g_device)) {
                    D3D11_VIEWPORT probeViewport{ 0.0f, 0.0f, 64.0f, 36.0f, 0.0f, 1.0f };
                    ctx->RSSetViewports(1, &probeViewport);
                    ctx->OMSetRenderTargets(1, &g_probeRTV, nullptr);
                    ctx->PSSetShader(g_depthProbePS, nullptr, 0);
                    ctx->PSSetShaderResources(4, 1, &probeDepth);
                    ctx->PSSetSamplers(0, 1, &g_clampSampler);
                    ctx->OMSetBlendState(g_blendOpaque, nullptr, 0xFFFFFFFF);
                    ctx->OMSetDepthStencilState(g_depthNone, 0);
                    ctx->Draw(3, 0);
                    ctx->CopyResource(g_probeReadback, g_probeTex);
                    D3D11_MAPPED_SUBRESOURCE mapped{};
                    if (ctx->Map(g_probeReadback, 0, D3D11_MAP_READ, 0, &mapped) >= 0) {
                        const float skyDepth = frame.reversedDepth ? 0.0f : 1.0f;
                        std::uint32_t notFar = 0, samples = 0;
                        float minD = 1e9f, maxD = -1e9f;
                        for (int y = 0; y < 36; ++y) {
                            const float* row = reinterpret_cast<const float*>(static_cast<const char*>(mapped.pData) + y * mapped.RowPitch);
                            for (int x = 0; x < 64; ++x) {
                                const float d = row[x];
                                if (!std::isfinite(d)) continue;
                                ++samples;
                                minD = std::min(minD, d);
                                maxD = std::max(maxD, d);
                                if (std::abs(d - skyDepth) > 0.0015f) ++notFar;
                            }
                        }
                        ctx->Unmap(g_probeReadback, 0);
                        if (auto logger = Logger())
                            logger->info("[Clouds] Depth probe: target {}x{} samples={} notFar={} min={:.6f} max={:.6f} reversed={}",
                                target.width, target.height, samples, notFar, minD, maxD, frame.reversedDepth);
                    }
                    ctx->RSSetViewports(1, &savedViewport);
                    ctx->OMSetRenderTargets(1, &mainRTV, mainDSV);
                }
            }

            ID3D11ShaderResourceView* noSRVs[3]{ nullptr, nullptr, nullptr };
            ctx->PSSetShaderResources(0, 3, noSRVs);
            if (mainDSV) mainDSV->Release();
            mainRTV->Release();
            g_prevRows[0] = { frame.viewProj._11, frame.viewProj._12, frame.viewProj._13, frame.viewProj._14 };
            g_prevRows[1] = { frame.viewProj._21, frame.viewProj._22, frame.viewProj._23, frame.viewProj._24 };
            g_prevRows[2] = { frame.viewProj._31, frame.viewProj._32, frame.viewProj._33, frame.viewProj._34 };
            g_prevRow3 = { frame.viewProj._41, frame.viewProj._42, frame.viewProj._43, frame.viewProj._44 };
            g_hasPrevCloud = true;
            g_cloudIndex = previous;
            ctx->Draw(3, 0);
        }
        struct CloudPassInfo
        {
            bool hasProperty{ false };
            int skyObjectType{ -1 };
            std::uint32_t layer{ 0 };
            std::uint32_t numLayers{ 0 };
        };
        bool IsCloudPass(void* a_pass, const RE::Sky* sky, CloudPassInfo* outInfo)
        {
            if (!a_pass) return false;
            void* property = *reinterpret_cast<void**>(reinterpret_cast<char*>(a_pass) + 0x10);
            if (!property) return false;
#if VOLCLOUDS_TYPED_SKY
            auto* skyProperty = reinterpret_cast<RE::BSSkyShaderProperty*>(property);
            const auto skyObjectType = static_cast<int>(skyProperty->skyObjectType);
            const auto layer = static_cast<std::uint32_t>(skyProperty->usCloudLayer);
            const auto numLayers = sky->clouds ? static_cast<std::uint32_t>(sky->clouds->usNumLayers) : 0u;
            if (outInfo)
                *outInfo = { true, skyObjectType, layer, numLayers };
            return skyProperty->skyObjectType == RE::BSSkyShaderProperty::SkyObject::kClouds;
#else
            const auto skyObjectType = *reinterpret_cast<const int*>(reinterpret_cast<const char*>(property) + 0xA8);
            const auto layer = *reinterpret_cast<const std::uint16_t*>(reinterpret_cast<const char*>(property) + 0xA4);
            auto* clouds = *reinterpret_cast<void**>(const_cast<char*>(reinterpret_cast<const char*>(sky)) + 0x88);
            const auto numLayers = clouds ? *reinterpret_cast<const std::uint16_t*>(reinterpret_cast<const char*>(clouds) + 0x510) : 0;
            if (outInfo)
                *outInfo = { true, skyObjectType, static_cast<std::uint32_t>(layer), static_cast<std::uint32_t>(numLayers) };
            return skyObjectType == 3;
#endif
        }
        void* SkyPropertyOfPass(void* a_pass)
        {
            if (!a_pass) return nullptr;
            return *reinterpret_cast<void**>(reinterpret_cast<char*>(a_pass) + 0x10);
        }
        void PollHotkeys()
        {
            const bool debugDown = (::GetAsyncKeyState(VK_F10) & 0x8000) != 0;
            const bool suppressDown = (::GetAsyncKeyState(VK_F11) & 0x8000) != 0;
            auto logger = Logger();
            if (debugDown && !g_hotkeyDebugDown) {
                g_debugOverride = (g_debugOverride == 1) ? 0 : 1;
                if (logger) logger->info("[Clouds] F10: debug full-screen override = {}.", g_debugOverride);
            }
            if (suppressDown && !g_hotkeySuppressDown) {
                g_suppressOverride = (g_suppressOverride == 1) ? 0 : 1;
                if (logger) logger->info("[Clouds] F11: vanilla cloud suppression = {}.", g_suppressOverride);
            }
            g_hotkeyDebugDown = debugDown;
            g_hotkeySuppressDown = suppressDown;
        }
        void Hook_SetupGeometry(void* a_shader, void* a_pass)
        {
            Config cfg;
            {
                std::scoped_lock lock(g_mutex);
                cfg = g_config;
            }
            PollHotkeys();
            const int debugMode = g_debugOverride >= 0 ? g_debugOverride : cfg.debugMode;
            const bool suppressVanilla = g_suppressOverride >= 0 ? (g_suppressOverride == 1) : cfg.suppressVanillaClouds;
            if (cfg.enabled && suppressVanilla) {
                if (void* property = SkyPropertyOfPass(a_pass)) {
#if VOLCLOUDS_TYPED_SKY
                    auto* skyProperty = reinterpret_cast<RE::BSSkyShaderProperty*>(property);
                    const int type = static_cast<int>(skyProperty->skyObjectType);
#else
                    const int type = *reinterpret_cast<const int*>(reinterpret_cast<const char*>(property) + 0xA8);
#endif
                    const bool isVanillaCloud = type == 3;
                    const bool isSunGlare = cfg.suppressSunGlare && (type == 0 || type == 1);
                    if (isVanillaCloud || isSunGlare)
                        *reinterpret_cast<float*>(reinterpret_cast<char*>(property) + 0x7C) = 0.0f;
                }
            }
            if (g_original)
                g_original(a_shader, a_pass);
            const auto callIndex = ++g_hookCalls;
            auto logger = Logger();
            if (!g_firstHookLogged) {
                g_firstHookLogged = true;
                if (logger) logger->info("[Clouds] SetupGeometry hook fired (first call).");
            }
            if (!cfg.enabled) {
                ++g_skipDisabled;
                if (!g_disabledLogged) {
                    g_disabledLogged = true;
                    if (logger) logger->info("[Clouds] Hook fired but bEnabled=false; pass skipped.");
                }
                return;
            }
            auto* graphicsState = RE::BSGraphics::State::GetSingleton();
            auto* sky = RE::Sky::GetSingleton();
            if (!graphicsState || !sky) return;
            const auto frame = graphicsState->frameID;
            const auto mode = SkyModeRaw(sky);
            if (mode != 2 && mode != 3) {
                ++g_skipMode;
                if (!g_modeLogged) {
                    g_modeLogged = true;
                    if (logger) logger->info("[Clouds] Sky mode {} (0 none, 1 interior, 2 dome, 3 full); clouds skipped.", mode);
                }
                return;
            }
            CloudPassInfo passInfo{};
            const bool isCloudPass = IsCloudPass(a_pass, sky, &passInfo);
            if (passInfo.hasProperty && passInfo.skyObjectType >= 0 && passInfo.skyObjectType < 16)
                ++g_skyTypeCounts[passInfo.skyObjectType];
            if (isCloudPass && !g_firstCloudPassLogged) {
                g_firstCloudPassLogged = true;
                if (logger) logger->info("[Clouds] First cloud pass: layer={} numLayers={} techniqueAccepted=true.",
                    passInfo.layer, passInfo.numLayers);
            }
            if (!isCloudPass) {
                ++g_skipPass;
                if (!g_passLogged) {
                    g_passLogged = true;
                    if (logger) logger->info("[Clouds] Non-cloud sky pass: hasProperty={} skyObjectType={} layer={}/{}; waiting for a cloud pass.",
                        passInfo.hasProperty, passInfo.skyObjectType, passInfo.layer, passInfo.numLayers);
                }
                const bool allowFallback = g_fallbackMode && (passInfo.skyObjectType == 2 || passInfo.skyObjectType == 4);
                if (!allowFallback) {
                    if (!g_fallbackMode && !g_firstCloudPassLogged && callIndex > 240) {
                        g_fallbackMode = true;
                        if (logger) logger->warn("[Clouds] No cloud pass observed after {} hook calls; falling back to sky-quad/atmosphere pass.", callIndex);
                    }
                    return;
                }
                if (!g_fallbackLogged) {
                    g_fallbackLogged = true;
                    if (logger) logger->info("[Clouds] Fallback pass in use (skyObjectType={}).", passInfo.skyObjectType);
                }
            }
            if (!g_vs || !g_ps || !g_cb) {
                if (!EnsureDeviceResources()) {
                    ++g_skipDevice;
                    if (!g_deviceLogged) {
                        g_deviceLogged = true;
                        if (logger) logger->warn("[Clouds] Device/shader resources unavailable; see earlier errors.");
                    }
                    return;
                }
            }
            auto* data = RE::BSGraphics::GetRendererData();
            if (!data || !data->context) return;
            auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(data->context);
            TargetInfo target{};
            if (!IsMainSceneTarget(ctx, graphicsState->screenWidth, graphicsState->screenHeight, &target)) {
                ++g_skipTarget;
                if (!g_targetLogged) {
                    g_targetLogged = true;
                    if (logger) logger->info("[Clouds] Bound target rejected: format={} {}x{} samples={} array={} (screen {}x{}).",
                        target.format, target.width, target.height, target.samples, target.arraySize,
                        graphicsState->screenWidth, graphicsState->screenHeight);
                }
                return;
            }
            if (frame != g_lastFrame) {
                if (g_frameDraws > 0 && (g_loggedFrameDraws < 6 || frame % 300 == 0)) {
                    ++g_loggedFrameDraws;
                    if (auto logger = Logger())
                        logger->info("[Clouds] Frame {} draws={} main={} secondary={} sizes=[{}x{}, {}x{}, {}x{}, {}x{}]",
                            g_lastFrame, g_frameDraws, g_frameMainDraws, g_frameSecondaryDraws,
                            g_frameSizes[0] >> 16, g_frameSizes[0] & 0xFFFF,
                            g_frameSizes[1] >> 16, g_frameSizes[1] & 0xFFFF,
                            g_frameSizes[2] >> 16, g_frameSizes[2] & 0xFFFF,
                            g_frameSizes[3] >> 16, g_frameSizes[3] & 0xFFFF);
                }
                g_frameBestArea = 0;
                g_frameDraws = 0;
                g_frameMainDraws = 0;
                g_frameSecondaryDraws = 0;
                g_frameSizes[0] = g_frameSizes[1] = g_frameSizes[2] = g_frameSizes[3] = 0;
            }
            const std::uint32_t targetArea = target.width * target.height;
            if (targetArea < g_frameBestArea) return;
            if (frame == g_lastFrame && target.textureId == g_lastTarget) return;
            const FrameState state = GatherFrameState(graphicsState, sky, cfg);
            if (!state.valid) {
                ++g_skipGather;
                if (!g_gatherLogged) {
                    g_gatherLogged = true;
                    if (logger) logger->warn("[Clouds] Frame state invalid: viewProj row0 = [{:.4f}, {:.4f}, {:.4f}, {:.4f}]; all camera sources failed.",
                        state.matrixSample[0], state.matrixSample[1], state.matrixSample[2], state.matrixSample[3]);
                }
                return;
            }
            cfg.debugMode = debugMode;
            RenderClouds(ctx, state, cfg, target);
            ++g_draws;
            g_lastFrame = frame;
            g_lastTarget = target.textureId;
            g_frameBestArea = targetArea;
            {
                const bool mainView = targetArea * 10 >= graphicsState->screenWidth * graphicsState->screenHeight * 9;
                ++g_frameDraws;
                if (mainView) ++g_frameMainDraws; else ++g_frameSecondaryDraws;
                for (int i = 0; i < 4; ++i) {
                    if (g_frameSizes[i] == 0) {
                        g_frameSizes[i] = (std::min(target.width, 0xFFFFu) << 16) | std::min(target.height, 0xFFFFu);
                        break;
                    }
                }
                if (g_frameDraws == 1 && (g_loggedFrameDraws < 6 || frame % 300 == 0) && Logger())
                    Logger()->info("[Clouds] Draw frame={} kind={} size={}x{}", frame, mainView ? "main" : "secondary",
                        target.width, target.height);
            }
            if (!g_firstDrawLogged) {
                g_firstDrawLogged = true;
                if (logger) logger->info("[Clouds] First draw: target {}x{} skyObjectType={} layer={}/{} source={} cam=({:.0f},{:.0f},{:.0f}) sunDir=({:.2f},{:.2f},{:.2f}) coverage={:.2f} reversedDepth={} depthMode={} debugMode={}.",
                    target.width, target.height, passInfo.skyObjectType, passInfo.layer, passInfo.numLayers,
                    state.sourceName ? state.sourceName : "?", state.cameraPos.x, state.cameraPos.y, state.cameraPos.z,
                    state.sunDir.x, state.sunDir.y, state.sunDir.z, state.coverage, state.reversedDepth, cfg.depthMode, cfg.debugMode);
            }
            if (callIndex % 600 == 0 && logger) {
                logger->info("[Clouds] Stats: calls={} draws={} skipped(disabled={} mode={} pass={} target={} device={} gather={}) types[sun={} glare={} atmo={} clouds={} skyquad={} stars={} moon={} shadow={} other={}] fallback={}.",
                    g_hookCalls, g_draws, g_skipDisabled, g_skipMode, g_skipPass, g_skipTarget, g_skipDevice, g_skipGather,
                    g_skyTypeCounts[0], g_skyTypeCounts[1], g_skyTypeCounts[2], g_skyTypeCounts[3], g_skyTypeCounts[4],
                    g_skyTypeCounts[5], g_skyTypeCounts[6], g_skyTypeCounts[7], g_skyTypeCounts[8], g_fallbackMode);
            }
        }
    }
    bool Install()
    {
        std::scoped_lock lock(g_mutex);
        if (g_installed) return true;
        LoadConfigLocked();
        try
        {
            REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE::BSSkyShader[0] };
            std::uintptr_t altVtable = 0;
            try {
                REL::Relocation<std::uintptr_t> alt{ RE::VTABLE::BSSkyShader[1] };
                altVtable = alt.get();
            }
            catch (...) {
                altVtable = 0;
            }
            auto** slots = reinterpret_cast<void**>(vtable.get());
            g_original = reinterpret_cast<std::uintptr_t>(slots[kSetupGeometryVTableSlot]);
            vtable.write_vfunc(kSetupGeometryVTableSlot, &Hook_SetupGeometry);
            g_startTime = std::chrono::steady_clock::now();
            g_installed = true;
            if (auto logger = Logger()) {
                logger->info("[Clouds] BSSkyShader::SetupGeometry hook installed (vtable slot {}); vtable base 0x{:X} (alt vtable 0x{:X}).",
                    kSetupGeometryVTableSlot, vtable.get(), altVtable);
                logger->info("[Clouds] Config enabled={} (file '{}').", g_config.enabled, kCloudConfigPath);
            }
        }
        catch (const std::exception& ex)
        {
            if (auto logger = Logger())
                logger->error("[Clouds] Hook install failed: {}", ex.what());
            g_installed = false;
        }
        return g_installed;
    }
    void ReloadConfig()
    {
        std::scoped_lock lock(g_mutex);
        LoadConfigLocked();
    }
    bool IsEnabled()
    {
        return g_installed && g_config.enabled;
    }
}
