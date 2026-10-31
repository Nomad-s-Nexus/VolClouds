#include "pch.h"
#include "Render/CloudConfig.h"

namespace RadClouds
{
    bool LoadCloudConfig(const char* a_path, CloudConfig& a_out)
    {
        CloudConfig cfg{};
        std::error_code ec;
        if (!std::filesystem::exists(a_path, ec))
        {
            a_out = cfg;
            return false;
        }
        try
        {
            std::ifstream file(a_path);
            if (!file.is_open())
                return false;
            const json data = json::parse(file);
            auto read = [&data](const char* key, auto& out) {
                if (auto it = data.find(key); it != data.end())
                    out = it->get<std::decay_t<decltype(out)>>();
            };
            read("bEnabled", cfg.enabled);
            read("bSuppressVanillaClouds", cfg.suppressVanillaClouds);
            read("bSuppressSunGlare", cfg.suppressSunGlare);
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
            read("iDebugMode", cfg.debugMode);
            read("fRenderScale", cfg.renderScale);
            read("fTemporalBlend", cfg.temporalBlend);
            read("fFogCoupling", cfg.fogCoupling);
            read("fSunShafts", cfg.sunShafts);
            read("fStormDensityBoost", cfg.stormDensityBoost);
            read("fStormCoverageBoost", cfg.stormCoverageBoost);
            read("fClearCoverageScale", cfg.clearCoverageScale);
            read("fSunsetWarmth", cfg.sunsetWarmth);
            read("bCloudShadows", cfg.cloudShadows);
            read("fCompositeDepthEpsilon", cfg.compositeDepthEpsilon);
            read("fCloudShadowStrength", cfg.shadowStrength);
            read("fCloudShadowExtent", cfg.shadowExtent);
            read("iCloudShadowDebug", cfg.shadowDebug);
            read("fLayer2Bottom", cfg.layer2Bottom);
            read("fLayer2Top", cfg.layer2Top);
            read("fLayer2Density", cfg.layer2Density);
            read("fLayer2Coverage", cfg.layer2Coverage);
            read("fPhase", cfg.phaseG);
            read("fPowder", cfg.powder);
            read("fAmbientBoost", cfg.ambientBoost);
            read("fHorizonFade", cfg.horizonFade);
            read("fStormDarken", cfg.stormDarken);
            read("fCirrus", cfg.cirrus);
            cfg.steps = cfg.steps < 4 ? 4 : (cfg.steps > 64 ? 64 : cfg.steps);
            if (cfg.layerTop < cfg.layerBottom + 100.0f) cfg.layerTop = cfg.layerBottom + 100.0f;
            a_out = cfg;
            return true;
        }
        catch (...)
        {
            a_out = cfg;
            return false;
        }
    }
}
