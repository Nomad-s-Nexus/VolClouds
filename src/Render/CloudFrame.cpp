#include "pch.h"
#include "Render/CloudInternal.h"
#include "Render/CloudShaders.h"

namespace RadClouds
{
    namespace detail
    {
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
    }
}
