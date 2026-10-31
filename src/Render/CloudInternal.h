#pragma once
#include "pch.h"
#include <RE/Fallout.h>
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
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include "Render/CloudConfig.h"

namespace RadClouds
{
    namespace detail
    {
        constexpr std::size_t kSetupGeometryVTableSlot = 7;
        using Config = CloudConfig;
        using SetupGeometryFn = void (*)(void*, void*);
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
    inline std::mutex g_mutex;
    inline Config g_config;
    inline bool g_installed{ false };
    inline bool g_loggedNoDepth{ false };
    inline bool g_loggedTargetSkip{ false };
    inline std::uint32_t g_lastFrame{ 0xFFFFFFFFu };
    inline std::uint32_t g_frameBestArea{ 0 };
    inline std::uint32_t g_frameDraws{ 0 };
    inline std::uint32_t g_frameMainDraws{ 0 };
    inline std::uint32_t g_frameSecondaryDraws{ 0 };
    inline std::uint32_t g_frameSizes[4]{};
    inline std::uint32_t g_loggedFrameDraws{ 0 };
    inline void* g_lastTarget{ nullptr };
    inline std::uint64_t g_hookCalls{ 0 };
    inline std::uint64_t g_draws{ 0 };
    inline std::uint64_t g_skipDisabled{ 0 };
    inline std::uint64_t g_skipMode{ 0 };
    inline std::uint64_t g_skipPass{ 0 };
    inline std::uint64_t g_skipTarget{ 0 };
    inline std::uint64_t g_skipDevice{ 0 };
    inline std::uint64_t g_skipGather{ 0 };
    inline bool g_firstHookLogged{ false };
    inline bool g_disabledLogged{ false };
    inline bool g_modeLogged{ false };
    inline bool g_passLogged{ false };
    inline bool g_targetLogged{ false };
    inline bool g_deviceLogged{ false };
    inline bool g_gatherLogged{ false };
    inline bool g_firstDrawLogged{ false };
    inline int g_debugOverride{ -1 };
    inline int g_suppressOverride{ -1 };
    inline bool g_hotkeyDebugDown{ false };
    inline bool g_hotkeySuppressDown{ false };
    inline bool g_firstCloudPassLogged{ false };
    inline bool g_fallbackLogged{ false };
    inline bool g_fallbackMode{ false };
    inline std::uint32_t g_skyTypeCounts[16]{};
    inline void* g_failedDevice{ nullptr };
    inline std::chrono::steady_clock::time_point g_startTime{};
    inline REL::Relocation<SetupGeometryFn> g_original;
    inline ID3D11Device* g_device{ nullptr };
    inline ID3D11VertexShader* g_vs{ nullptr };
    inline ID3D11PixelShader* g_ps{ nullptr };
    inline ID3D11Buffer* g_cb{ nullptr };
    inline ID3D11BlendState* g_blend{ nullptr };
    inline ID3D11DepthStencilState* g_depthStandard{ nullptr };
    inline ID3D11DepthStencilState* g_depthReversed{ nullptr };
    inline ID3D11DepthStencilState* g_depthNone{ nullptr };
    inline ID3D11RasterizerState* g_raster{ nullptr };
    inline ID3D11ShaderResourceView* g_noiseSRV{ nullptr };
    inline ID3D11SamplerState* g_noiseSampler{ nullptr };
    inline ID3D11SamplerState* g_clampSampler{ nullptr };
    inline ID3D11BlendState* g_blendOpaque{ nullptr };
    inline ID3D11PixelShader* g_compositePS{ nullptr };
    inline ID3D11Texture2D* g_cloudTex[2]{};
    inline ID3D11ShaderResourceView* g_cloudSRV[2]{};
    inline ID3D11RenderTargetView* g_cloudRTV[2]{};
    inline std::uint32_t g_cloudWidth{ 0 };
    inline std::uint32_t g_cloudHeight{ 0 };
    inline int g_cloudIndex{ 0 };
    inline bool g_hasPrevCloud{ false };
    inline DirectX::XMFLOAT4 g_prevRows[3]{};
    inline DirectX::XMFLOAT4 g_prevRow3{};
    inline ID3D11PixelShader* g_shadowMaskPS{ nullptr };
    inline ID3D11PixelShader* g_cloudShadowPS{ nullptr };
    inline ID3D11PixelShader* g_shadowDebugPS{ nullptr };
    inline ID3D11BlendState* g_blendMultiply{ nullptr };
    inline ID3D11Texture2D* g_shadowMaskTex{ nullptr };
    inline ID3D11ShaderResourceView* g_shadowMaskSRV{ nullptr };
    inline ID3D11RenderTargetView* g_shadowMaskRTV{ nullptr };
    inline ID3D11ShaderResourceView* g_depthSRV{ nullptr };
    inline ID3D11Resource* g_depthSRVRes{ nullptr };
    inline bool g_loggedShadowNoDepth{ false };
    inline bool g_shadowReady{ false };
    inline ID3D11PixelShader* g_depthProbePS{ nullptr };
    inline ID3D11Texture2D* g_probeTex{ nullptr };
    inline ID3D11RenderTargetView* g_probeRTV{ nullptr };
    inline ID3D11Texture2D* g_probeReadback{ nullptr };
    inline std::uint32_t g_probeCounter{ 0 };
    std::shared_ptr<spdlog::logger> Logger()
{
    return spdlog::get("VolClouds");
}

        void LoadConfigLocked();
        void ReleaseResources();


        bool CompileShaders(ID3D11Device* device);
        bool LoadNoiseVolume(ID3D11Device* device);
        bool EnsureCloudTextures(ID3D11Device* device, std::uint32_t width, std::uint32_t height);
        bool EnsureShadowMask(ID3D11Device* device);
        ID3D11ShaderResourceView* DepthSRVFrom(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* dsv);
        bool EnsureProbeResources(ID3D11Device* device);
        bool EnsureDeviceResources();
        std::uint32_t SkyModeRaw(const RE::Sky* sky);
        float WeatherCoverage(const RE::TESWeather* weather);
        FrameState GatherFrameState(const RE::BSGraphics::State* graphicsState, const RE::Sky* sky, const Config& cfg);
        bool IsMainSceneTarget(ID3D11DeviceContext* ctx, std::uint32_t expectedWidth, std::uint32_t expectedHeight, TargetInfo* outInfo);
        void RenderClouds(ID3D11DeviceContext* ctx, const FrameState& frame, const Config& cfg, const TargetInfo& target);
        struct CloudPassInfo;
        bool IsCloudPass(void* a_pass, const RE::Sky* sky, CloudPassInfo* outInfo);
        void* SkyPropertyOfPass(void* a_pass);
        void PollHotkeys();
        void Hook_SetupGeometry(void* a_shader, void* a_pass);
ma once
#include "pch.h"
#include <RE/Fallout.h>
#include <DirectXMath.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include "Render/CloudConfig.h"

namespace RadClouds
{
    namespace detail
    {
        constexpr std::size_t kSetupGeometryVTableSlot = 7;
        using Config = CloudConfig;
        using SetupGeometryFn = void (*)(void*, void*);
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
    inline std::mutex g_mutex;
    inline Config g_config;
    inline bool g_installed{ false };
    inline bool g_loggedNoDepth{ false };
    inline bool g_loggedTargetSkip{ false };
    inline std::uint32_t g_lastFrame{ 0xFFFFFFFFu };
    inline std::uint32_t g_frameBestArea{ 0 };
    inline std::uint32_t g_frameDraws{ 0 };
    inline std::uint32_t g_frameMainDraws{ 0 };
    inline std::uint32_t g_frameSecondaryDraws{ 0 };
    inline std::uint32_t g_frameSizes[4]{};
    inline std::uint32_t g_loggedFrameDraws{ 0 };
    inline void* g_lastTarget{ nullptr };
    inline std::uint64_t g_hookCalls{ 0 };
    inline std::uint64_t g_draws{ 0 };
    inline std::uint64_t g_skipDisabled{ 0 };
    inline std::uint64_t g_skipMode{ 0 };
    inline std::uint64_t g_skipPass{ 0 };
    inline std::uint64_t g_skipTarget{ 0 };
    inline std::uint64_t g_skipDevice{ 0 };
    inline std::uint64_t g_skipGather{ 0 };
    inline bool g_firstHookLogged{ false };
    inline bool g_disabledLogged{ false };
    inline bool g_modeLogged{ false };
    inline bool g_passLogged{ false };
    inline bool g_targetLogged{ false };
    inline bool g_deviceLogged{ false };
    inline bool g_gatherLogged{ false };
    inline bool g_firstDrawLogged{ false };
    inline int g_debugOverride{ -1 };
    inline int g_suppressOverride{ -1 };
    inline bool g_hotkeyDebugDown{ false };
    inline bool g_hotkeySuppressDown{ false };
    inline bool g_firstCloudPassLogged{ false };
    inline bool g_fallbackLogged{ false };
    inline bool g_fallbackMode{ false };
    inline std::uint32_t g_skyTypeCounts[16]{};
    inline void* g_failedDevice{ nullptr };
    inline std::chrono::steady_clock::time_point g_startTime{};
    inline REL::Relocation<SetupGeometryFn> g_original;
    inline ID3D11Device* g_device{ nullptr };
    inline ID3D11VertexShader* g_vs{ nullptr };
    inline ID3D11PixelShader* g_ps{ nullptr };
    inline ID3D11Buffer* g_cb{ nullptr };
    inline ID3D11BlendState* g_blend{ nullptr };
    inline ID3D11DepthStencilState* g_depthStandard{ nullptr };
    inline ID3D11DepthStencilState* g_depthReversed{ nullptr };
    inline ID3D11DepthStencilState* g_depthNone{ nullptr };
    inline ID3D11RasterizerState* g_raster{ nullptr };
    inline ID3D11ShaderResourceView* g_noiseSRV{ nullptr };
    inline ID3D11SamplerState* g_noiseSampler{ nullptr };
    inline ID3D11SamplerState* g_clampSampler{ nullptr };
    inline ID3D11BlendState* g_blendOpaque{ nullptr };
    inline ID3D11PixelShader* g_compositePS{ nullptr };
    inline ID3D11Texture2D* g_cloudTex[2]{};
    inline ID3D11ShaderResourceView* g_cloudSRV[2]{};
    inline ID3D11RenderTargetView* g_cloudRTV[2]{};
    inline std::uint32_t g_cloudWidth{ 0 };
    inline std::uint32_t g_cloudHeight{ 0 };
    inline int g_cloudIndex{ 0 };
    inline bool g_hasPrevCloud{ false };
    inline DirectX::XMFLOAT4 g_prevRows[3]{};
    inline DirectX::XMFLOAT4 g_prevRow3{};
    inline ID3D11PixelShader* g_shadowMaskPS{ nullptr };
    inline ID3D11PixelShader* g_cloudShadowPS{ nullptr };
    inline ID3D11PixelShader* g_shadowDebugPS{ nullptr };
    inline ID3D11BlendState* g_blendMultiply{ nullptr };
    inline ID3D11Texture2D* g_shadowMaskTex{ nullptr };
    inline ID3D11ShaderResourceView* g_shadowMaskSRV{ nullptr };
    inline ID3D11RenderTargetView* g_shadowMaskRTV{ nullptr };
    inline ID3D11ShaderResourceView* g_depthSRV{ nullptr };
    inline ID3D11Resource* g_depthSRVRes{ nullptr };
    inline bool g_loggedShadowNoDepth{ false };
    inline bool g_shadowReady{ false };
    inline ID3D11PixelShader* g_depthProbePS{ nullptr };
    inline ID3D11Texture2D* g_probeTex{ nullptr };
    inline ID3D11RenderTargetView* g_probeRTV{ nullptr };
    inline ID3D11Texture2D* g_probeReadback{ nullptr };
    inline std::uint32_t g_probeCounter{ 0 };
    std::shared_ptr<spdlog::logger> Logger()
{
    return spdlog::get("VolClouds");
}

        void LoadConfigLocked();
        void ReleaseResources();
        bool CompileShaders(ID3D11Device* device);
        bool LoadNoiseVolume(ID3D11Device* device);
        bool EnsureCloudTextures(ID3D11Device* device, std::uint32_t width, std::uint32_t height);
        bool EnsureShadowMask(ID3D11Device* device);
        ID3D11ShaderResourceView* DepthSRVFrom(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* dsv);
        bool EnsureProbeResources(ID3D11Device* device);
        bool EnsureDeviceResources();
        std::uint32_t SkyModeRaw(const RE::Sky* sky);
        float WeatherCoverage(const RE::TESWeather* weather);
        FrameState GatherFrameState(const RE::BSGraphics::State* graphicsState, const RE::Sky* sky, const Config& cfg);
        bool IsMainSceneTarget(ID3D11DeviceContext* ctx, std::uint32_t expectedWidth, std::uint32_t expectedHeight, TargetInfo* outInfo);
        void RenderClouds(ID3D11DeviceContext* ctx, const FrameState& frame, const Config& cfg, const TargetInfo& target);
        struct CloudPassInfo;
        bool IsCloudPass(void* a_pass, const RE::Sky* sky, CloudPassInfo* outInfo);
        void* SkyPropertyOfPass(void* a_pass);
        void PollHotkeys();
        void Hook_SetupGeometry(void* a_shader, void* a_pass);
    struct TargetInfo
{
    std::uint32_t format{ 0 };
    std::uint32_t width{ 0 };
    std::uint32_t height{ 0 };
    std::uint32_t arraySize{ 0 };
    std::uint32_t samples{ 0 };
    void* textureId{ nullptr };
};
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
    }
}
