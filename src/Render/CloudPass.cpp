#include "pch.h"
#include "Render/CloudInternal.h"
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
    namespace detail
    {
        using SetupGeometryFn = void (*)(void*, void*);
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
            const float skyEpsilon = std::clamp(cfg.compositeDepthEpsilon, 0.0f, 0.5f);
            const float skyZ = frame.reversedDepth ? skyEpsilon : (1.0f - skyEpsilon);
            cb.params2 = { skyZ, frame.nearZ, frame.farZ, static_cast<float>(cfg.debugMode) };
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
            ctx->PSSetShader(g_compositePS, nullptr, 0);
            ctx->PSSetShaderResources(0, 2, compositeSRVs);
            ctx->PSSetSamplers(0, 1, &g_clampSampler);
            ctx->OMSetBlendState(g_blend, nullptr, 0xFFFFFFFF);
            ctx->OMSetDepthStencilState(depthState, 0);
            ctx->RSSetState(g_raster);
            ctx->Draw(3, 0);
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
                if (logger) logger->info("[Clouds] First draw: target {}x{} skyObjectType={} layer={}/{} source={} cam=({:.0f},{:.0f},{:.0f}) sunDir=({:.2f},{:.2f},{:.2f}) coverage={:.2f} reversedDepth={} depthMode={} debugMode={} skyZ={:.6f} epsilon={:.6f}",
                    target.width, target.height, passInfo.skyObjectType, passInfo.layer, passInfo.numLayers,
                    state.sourceName ? state.sourceName : "?", state.cameraPos.x, state.cameraPos.y, state.cameraPos.z,
                    state.sunDir.x, state.sunDir.y, state.sunDir.z, state.coverage, state.reversedDepth, cfg.depthMode, cfg.debugMode,
                    state.reversedDepth ? cfg.compositeDepthEpsilon : 1.0f - cfg.compositeDepthEpsilon, cfg.compositeDepthEpsilon);
            }
            if (callIndex % 600 == 0 && logger) {
                logger->info("[Clouds] Stats: calls={} draws={} skipped(disabled={} mode={} pass={} target={} device={} gather={}) types[sun={} glare={} atmo={} clouds={} skyquad={} stars={} moon={} shadow={} other={}] fallback={}.",
                    g_hookCalls, g_draws, g_skipDisabled, g_skipMode, g_skipPass, g_skipTarget, g_skipDevice, g_skipGather,
                    g_skyTypeCounts[0], g_skyTypeCounts[1], g_skyTypeCounts[2], g_skyTypeCounts[3], g_skyTypeCounts[4],
                    g_skyTypeCounts[5], g_skyTypeCounts[6], g_skyTypeCounts[7], g_skyTypeCounts[8], g_fallbackMode);
            }
        }
    }

    using namespace detail;

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
