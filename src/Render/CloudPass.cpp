#include "pch.h"
#include "Render/CloudPass.h"
#include "Render/CloudShaders.h"

#include <RE/Fallout.h>
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

// TODO-verify the hook slot and sky offsets at runtime before enabling by default.
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
        constexpr std::size_t kSetupGeometryVTableSlot = 7;
        constexpr const char* kConfigPath = "Data/F4SE/Plugins/VolClouds/clouds.json";

        struct Config
        {
            bool enabled{ false };
            float density{ 1.6f };
            float coverageScale{ 1.0f };
            float layerBottom{ 3500.0f };
            float layerTop{ 9000.0f };
            int steps{ 24 };
            float sunIntensity{ 2.5f };
            float ambient{ 0.22f };
            float windDrift{ 1.0f };
            float maxDistance{ 60000.0f };
            float blendAlpha{ 1.0f };
            float noiseScale{ 1.0f };
            int depthMode{ 0 };  // 0 = auto, 1 = standard Z (LESS_EQUAL), 2 = reversed Z (GREATER_EQUAL)
        };

        struct alignas(16) CloudCB
        {
            DirectX::XMFLOAT4X4 invViewProj;
            DirectX::XMFLOAT4 cameraPos;
            DirectX::XMFLOAT4 sunDir;      // w = intensity
            DirectX::XMFLOAT4 sunColor;
            DirectX::XMFLOAT4 ambientColor;
            DirectX::XMFLOAT4 windAndTime; // x angle, y speed, z time, w coverage
            DirectX::XMFLOAT4 params0;     // x bottom, y top, z density, w steps
            DirectX::XMFLOAT4 params1;
            DirectX::XMFLOAT4 params2;
        };

        struct FrameState
        {
            bool valid{ false };
            DirectX::XMFLOAT4X4 invViewProj{};
            DirectX::XMFLOAT3 cameraPos{};
            DirectX::XMFLOAT3 sunDir{ 0.0f, 1.0f, 0.0f };
            float sunIntensity{ 1.0f };
            DirectX::XMFLOAT3 sunColor{ 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT3 ambientColor{ 0.2f, 0.24f, 0.3f };
            float windAngle{ 0.0f };
            float windSpeed{ 0.0f };
            float coverage{ 0.5f };
            float flash{ 0.0f };
            float time{ 0.0f };
            bool reversedDepth{ false };
            float nearZ{ 0.0f };
            float farZ{ 1.0f };
        };

        using SetupGeometryFn = void (*)(void*, void*);

        std::mutex g_mutex;
        Config g_config;
        bool g_installed{ false };
        bool g_loggedNoDepth{ false };
        bool g_loggedTargetSkip{ false };
        std::uint32_t g_lastFrame{ 0xFFFFFFFFu };
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

        std::shared_ptr<spdlog::logger> Logger()
        {
            return spdlog::get("VolClouds");
        }

        void LoadConfigLocked()
        {
            Config cfg{};
            std::error_code ec;
            if (!std::filesystem::exists(kConfigPath, ec))
            {
                g_config = cfg;
                if (auto logger = Logger())
                    logger->info("[Clouds] No config at '{}'; volumetric clouds disabled until enabled in that file.", kConfigPath);
                return;
            }

            try
            {
                std::ifstream file(kConfigPath);
                if (!file.is_open())
                    throw std::runtime_error("open failed");
                const json data = json::parse(file);
                auto read = [&data](const char* key, auto& out) {
                    if (auto it = data.find(key); it != data.end())
                        out = it->get<std::decay_t<decltype(out)>>();
                };
                read("bEnabled", cfg.enabled);
                read("fDensity", cfg.density);
                read("fCoverageScale", cfg.coverageScale);
                read("fLayerBottom", cfg.layerBottom);
                read("fLayerTop", cfg.layerTop);
                read("iSteps", cfg.steps);
                read("fSunIntensity", cfg.sunIntensity);
                read("fAmbient", cfg.ambient);
                read("fWindDrift", cfg.windDrift);
                read("fMaxDistance", cfg.maxDistance);
                read("fBlendAlpha", cfg.blendAlpha);
                read("fNoiseScale", cfg.noiseScale);
                read("iDepthMode", cfg.depthMode);
                cfg.steps = std::clamp(cfg.steps, 4, 64);
                cfg.layerTop = std::max(cfg.layerTop, cfg.layerBottom + 100.0f);
                g_config = cfg;
                if (auto logger = Logger())
                    logger->info("[Clouds] Config loaded: enabled={} density={} steps={} layer=[{}, {}] depthMode={}",
                        cfg.enabled, cfg.density, cfg.steps, cfg.layerBottom, cfg.layerTop, cfg.depthMode);
            }
            catch (const std::exception& ex)
            {
                g_config = Config{};
                if (auto logger = Logger())
                    logger->error("[Clouds] Config '{}' failed to parse ({}); clouds disabled.", kConfigPath, ex.what());
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
                coverage = std::max(coverage, weather->cloudAlpha[i][1]);  // ColorTime::kDay
            }
            const auto flags = static_cast<std::uint8_t>(weather->weatherData[0xB]);  // WeatherData::kFlags
            if (flags & 0x2) coverage = std::max(coverage, 0.60f);  // cloudy
            if (flags & 0x4) coverage = std::max(coverage, 0.85f);  // rainy
            if (flags & 0x8) coverage = std::max(coverage, 0.90f);  // snow
            return std::clamp(coverage, 0.0f, 1.0f);
        }

        FrameState GatherFrameState(const RE::BSGraphics::State* graphicsState, const RE::Sky* sky, const Config& cfg)
        {
            FrameState out{};
            if (!graphicsState || !sky) return out;

            const auto& viewData = graphicsState->cameraState.camViewData;
            std::memcpy(&out.invViewProj, &viewData.viewProjMat, sizeof(out.invViewProj));
            DirectX::XMMATRIX vp = DirectX::XMLoadFloat4x4(&out.invViewProj);
            DirectX::XMVECTOR det = DirectX::XMVectorZero();
            const DirectX::XMMATRIX invVp = DirectX::XMMatrixInverse(&det, vp);
            DirectX::XMStoreFloat4x4(&out.invViewProj, invVp);
            if (!std::isfinite(DirectX::XMVectorGetX(det)) || std::abs(DirectX::XMVectorGetX(det)) < 1e-12f) return out;

            DirectX::XMFLOAT4X4 view{};
            std::memcpy(&view, &viewData.viewMat, sizeof(view));
            const DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, DirectX::XMLoadFloat4x4(&view));
            DirectX::XMFLOAT4X4 invViewF{};
            DirectX::XMStoreFloat4x4(&invViewF, invView);
            out.cameraPos = { invViewF._41, invViewF._42, invViewF._43 };

            DirectX::XMFLOAT4X4 proj{};
            std::memcpy(&proj, &viewData.projMat, sizeof(proj));
            out.reversedDepth = proj._33 <= 0.0f;
            out.nearZ = out.reversedDepth ? 1.0f : 0.0f;
            out.farZ = out.reversedDepth ? 0.0f : 1.0f;

            if (sky->sun && sky->sun->sunBaseNode)
            {
                const auto sunPos = sky->sun->sunBaseNode.get()->GetWorldTranslate();
                const auto& posAdjust = graphicsState->cameraState.posAdjust;
                DirectX::XMFLOAT3 dir{ (sunPos.x - posAdjust.x) - out.cameraPos.x,
                    (sunPos.y - posAdjust.y) - out.cameraPos.y,
                    (sunPos.z - posAdjust.z) - out.cameraPos.z };
                const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                if (len > 1.0f) {
                    out.sunDir = { dir.x / len, dir.y / len, dir.z / len };
                }
            }
            else
            {
                const float hour = std::clamp(sky->currentGameHour, 0.0f, 24.0f);
                const float angle = (hour - 6.0f) / 12.0f * DirectX::XM_PI;
                out.sunDir = { std::cos(angle), std::sin(angle), 0.35f };
                const float len = std::sqrt(out.sunDir.x * out.sunDir.x + out.sunDir.y * out.sunDir.y + 0.1225f);
                out.sunDir = { out.sunDir.x / len, out.sunDir.y / len, 0.35f / len };
            }

            const float dayBase = std::clamp(out.sunDir.z * 1.6f + 0.25f, 0.05f, 1.0f);
            const RE::NiColor sunTint = sky->skyColor[4];  // ColorType::kSunlight
            const RE::NiColor skyUpper = sky->skyColor[0];
            out.sunColor = { std::max(sunTint.r, 0.15f) * dayBase, std::max(sunTint.g, 0.15f) * dayBase,
                std::max(sunTint.b, 0.15f) * dayBase };
            out.sunIntensity = cfg.sunIntensity * dayBase;
            const float ambientScale = 0.35f + cfg.ambient;
            out.ambientColor = { std::max(skyUpper.r, 0.05f) * ambientScale, std::max(skyUpper.g, 0.06f) * ambientScale, std::max(skyUpper.b, 0.08f) * ambientScale };

            const float pct = std::clamp(sky->currentWeatherPct, 0.0f, 1.0f);
            const float currentCoverage = WeatherCoverage(sky->currentWeather);
            const float lastCoverage = sky->lastWeather ? WeatherCoverage(sky->lastWeather) : currentCoverage;
            out.coverage = std::clamp((lastCoverage + (currentCoverage - lastCoverage) * pct) * cfg.coverageScale, 0.0f, 1.0f);

            out.windAngle = sky->windAngle;
            out.windSpeed = sky->windSpeed * cfg.windDrift;
            out.flash = std::clamp(sky->flash, 0.0f, 1.0f);
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

        bool IsMainSceneTarget(ID3D11DeviceContext* ctx, std::uint32_t expectedWidth, std::uint32_t expectedHeight)
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
            if (desc.ArraySize > 1) return false;
            if (desc.SampleDesc.Count > 1) return false;
            if (desc.Format != DXGI_FORMAT_R11G11B10_FLOAT && desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) return false;
            const auto NearSize = [](std::uint32_t a, std::uint32_t b) {
                const auto diff = a > b ? a - b : b - a;
                return diff <= b / 4;
            };
            return NearSize(desc.Width, expectedWidth) && NearSize(desc.Height, expectedHeight);
        }

        void RenderClouds(ID3D11DeviceContext* ctx, const FrameState& frame, const Config& cfg)
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
            cb.params2 = { frame.farZ, frame.nearZ, frame.farZ, 0.0f };

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

            const bool useDepth = dsv != nullptr;
            if (dsv) dsv->Release();
            const bool reversed = cfg.depthMode == 2 || (cfg.depthMode == 0 && frame.reversedDepth);
            ID3D11DepthStencilState* depthState = !useDepth ? g_depthNone : (reversed ? g_depthReversed : g_depthStandard);
            if (!useDepth && !g_loggedNoDepth) {
                g_loggedNoDepth = true;
                if (auto logger = Logger()) logger->warn("[Clouds] No depth bound at sky pass; drawing without depth test.");
            }

            ctx->IASetInputLayout(nullptr);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ctx->VSSetShader(g_vs, nullptr, 0);
            ctx->PSSetShader(g_ps, nullptr, 0);
            ctx->GSSetShader(nullptr, nullptr, 0);
            ctx->HSSetShader(nullptr, nullptr, 0);
            ctx->DSSetShader(nullptr, nullptr, 0);
            ctx->VSSetConstantBuffers(0, 1, &g_cb);
            ctx->PSSetConstantBuffers(0, 1, &g_cb);
            ctx->OMSetBlendState(g_blend, nullptr, 0xFFFFFFFF);
            ctx->OMSetDepthStencilState(depthState, 0);
            ctx->RSSetState(g_raster);
            ctx->Draw(3, 0);
        }

        bool IsLastCloudLayerPass(void* a_pass, const RE::Sky* sky)
        {
            if (!a_pass) return false;
            auto* property = *reinterpret_cast<void**>(reinterpret_cast<char*>(a_pass) + 0x10);
            if (!property) return false;
            const auto skyObjectType = *reinterpret_cast<const int*>(reinterpret_cast<const char*>(property) + 0xA8);
            if (skyObjectType != 3) return false;
            const auto layer = *reinterpret_cast<const std::uint16_t*>(reinterpret_cast<const char*>(property) + 0xA4);
            auto* clouds = *reinterpret_cast<void**>(const_cast<char*>(reinterpret_cast<const char*>(sky)) + 0x88);
            if (!clouds) return true;
            const auto numLayers = *reinterpret_cast<const std::uint16_t*>(reinterpret_cast<const char*>(clouds) + 0x510);
            if (numLayers == 0) return true;
            return layer + 1 >= numLayers;
        }

        void Hook_SetupGeometry(void* a_shader, void* a_pass)
        {
            if (g_original)
                g_original(a_shader, a_pass);

            Config cfg;
            {
                std::scoped_lock lock(g_mutex);
                cfg = g_config;
            }
            if (!cfg.enabled) return;

            auto* graphicsState = RE::BSGraphics::State::GetSingleton();
            auto* sky = RE::Sky::GetSingleton();
            if (!graphicsState || !sky) return;

            const auto frame = graphicsState->frameID;
            if (frame == g_lastFrame) return;
            const auto mode = SkyModeRaw(sky);
            if (mode != 2 && mode != 3) return;
            if (!IsLastCloudLayerPass(a_pass, sky)) return;

            if (!g_vs || !g_ps || !g_cb) {
                if (!EnsureDeviceResources()) return;
            }
            auto* data = RE::BSGraphics::GetRendererData();
            if (!data || !data->context) return;
            auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(data->context);
            if (!IsMainSceneTarget(ctx, graphicsState->screenWidth, graphicsState->screenHeight)) {
                if (!g_loggedTargetSkip) {
                    g_loggedTargetSkip = true;
                    if (auto logger = Logger()) logger->info("[Clouds] Skipping cloud draw on a non-main render target.");
                }
                return;
            }

            const FrameState state = GatherFrameState(graphicsState, sky, cfg);
            if (!state.valid) return;

            RenderClouds(ctx, state, cfg);
            g_lastFrame = frame;
        }
    }

    bool Install()
    {
        std::scoped_lock lock(g_mutex);
        if (g_installed) return true;

        LoadConfigLocked();

        try
        {
            // TODO-verify the AE vtable slot index before enabling there.
            REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE::BSSkyShader[0] };
            auto** slots = reinterpret_cast<void**>(vtable.get());
            g_original = reinterpret_cast<std::uintptr_t>(slots[kSetupGeometryVTableSlot]);
            vtable.write_vfunc(kSetupGeometryVTableSlot, &Hook_SetupGeometry);
            g_startTime = std::chrono::steady_clock::now();
            g_installed = true;
            if (auto logger = Logger())
                logger->info("[Clouds] BSSkyShader::SetupGeometry hook installed (vtable slot {}).", kSetupGeometryVTableSlot);
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
