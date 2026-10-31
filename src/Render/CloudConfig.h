#pragma once
#include <cstdint>

namespace RadClouds
{
    inline constexpr const char* kCloudConfigPath = "Data/F4SE/Plugins/VolClouds/clouds.json";

    struct CloudConfig
    {
        bool enabled{ false };
        bool suppressVanillaClouds{ true };
        bool suppressSunGlare{ false };
        float density{ 1.6f };
        float coverageScale{ 1.0f };
        float layerBottom{ 3500.0f };
        float layerTop{ 9000.0f };
        int steps{ 48 };
        float sunIntensity{ 2.5f };
        float ambient{ 0.22f };
        float windDrift{ 1.0f };
        float maxDistance{ 60000.0f };
        float blendAlpha{ 1.0f };
        float noiseScale{ 1.0f };
        int depthMode{ 0 };
        int debugMode{ 0 };
        float renderScale{ 0.5f };
        float temporalBlend{ 0.0f };
        float fogCoupling{ 1.0f };
        float sunShafts{ 0.0f };
        float stormDensityBoost{ 1.5f };
        float stormCoverageBoost{ 0.1f };
        float clearCoverageScale{ 0.55f };
        float sunsetWarmth{ 0.35f };
        bool cloudShadows{ false };
        float compositeDepthEpsilon{ 0.01f };
        float shadowStrength{ 0.45f };
        float shadowExtent{ 9000.0f };
        int shadowDebug{ 0 };
        float layer2Bottom{ 7000.0f };
        float layer2Top{ 11000.0f };
        float layer2Density{ 0.9f };
        float layer2Coverage{ 0.45f };
        float phaseG{ 0.65f };
        float powder{ 0.65f };
        float ambientBoost{ 1.4f };
        float horizonFade{ 12000.0f };
        float stormDarken{ 0.35f };
        float cirrus{ 0.8f };
    };

    bool LoadCloudConfig(const char* a_path, CloudConfig& a_out);
}
