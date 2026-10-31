#include "pch.h"
#include "Render/CloudPass.h"

#include <ShlObj.h>
#include <KnownFolders.h>

namespace
{
    std::shared_ptr<spdlog::logger> logger;

    enum class GameVersion
    {
        Unknown,
        OG,
        NewGen,
        AE
    };

    GameVersion g_gameVersion{ GameVersion::Unknown };

    GameVersion DetectGameVersion(const F4SE::QueryInterface* a_f4se)
    {
        if (!a_f4se)
            return GameVersion::Unknown;
        const auto ver = a_f4se->RuntimeVersion();
        if (ver == F4SE::RUNTIME_1_10_163)
            return GameVersion::OG;
        if (ver >= F4SE::RUNTIME_1_10_980 && ver <= F4SE::RUNTIME_1_10_984)
            return GameVersion::NewGen;
        if (ver == F4SE::RUNTIME_1_11_240)
            return GameVersion::AE;
        return GameVersion::Unknown;
    }

    const char* GameVersionString(GameVersion version)
    {
        switch (version) {
        case GameVersion::OG:
            return "OG 1.10.163";
        case GameVersion::AE:
            return "AE 1.11.240";
        case GameVersion::NewGen:
            return "NG 1.10.980-1.10.984 (unsupported)";
        default:
            return "unknown";
        }
    }

    std::filesystem::path GetLogDirectory()
    {
        PWSTR documents = nullptr;
        if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &documents)) && documents) {
            std::filesystem::path path{ documents };
            ::CoTaskMemFree(documents);
            return path / L"My Games" / L"Fallout4" / L"F4SE";
        }
        return std::filesystem::path{ L"VolCloudsLogs" };
    }

    void BootstrapLog(const std::string& a_line) noexcept
    {
        try
        {
            const auto directory = GetLogDirectory();
            std::error_code ec;
            std::filesystem::create_directories(directory, ec);
            std::ofstream file(directory / L"VolClouds.bootstrap.log", std::ios::app);
            if (file.is_open())
                file << a_line << '\n';
        }
        catch (...)
        {
        }
    }

    void InitializeLogger()
    {
        try
        {
            const auto directory = GetLogDirectory();
            std::error_code ec;
            std::filesystem::create_directories(directory, ec);
            logger = spdlog::basic_logger_mt("VolClouds", (directory / L"VolClouds.log").string(), true);
            spdlog::set_default_logger(logger);
            logger->set_level(spdlog::level::debug);
            logger->flush_on(spdlog::level::info);
            logger->info("=================================================");
            logger->info("VolClouds loaded");
            logger->info("=================================================");
        }
        catch (const std::exception&)
        {
            logger = std::make_shared<spdlog::logger>("VolClouds", std::make_shared<spdlog::sinks::null_sink_mt>());
            spdlog::set_default_logger(logger);
        }
    }

    void MessageHandler(F4SE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg)
            return;

        switch (a_msg->type) {
        case F4SE::MessagingInterface::kGameDataReady:
            logger->info("Game data ready; installing volumetric cloud pass.");
            if (!RadClouds::Install())
                logger->error("Volumetric cloud pass failed to install.");
            break;
        case F4SE::MessagingInterface::kPostLoadGame:
            RadClouds::ReloadConfig();
            logger->info("Save loaded; cloud config reloaded.");
            break;
        default:
            break;
        }
    }
}

F4SE_PLUGIN_VERSION = []() noexcept {
    F4SE::PluginVersionData data{};
    data.PluginVersion(REL::Version(0, 7, 3));
    data.PluginName("Volumetric Clouds");
    data.AuthorName("NomadsReach");
    data.UsesSigScanning(false);
    data.UsesAddressLibrary(true);
    data.UsesAddressLibraryNG(true);
    data.HasNoStructUse(false);
    data.IsLayoutDependent(true);
    data.IsLayoutDependentNG(true);
    data.CompatibleVersions({});
    return data;
}();

extern "C" __declspec(dllexport) bool F4SEAPI
F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
    if (!a_f4se || !a_info)
        return false;

    a_info->infoVersion = F4SE::PluginInfo::kVersion;
    a_info->name = "Volumetric Clouds";
    a_info->version = 1;

    if (a_f4se->IsEditor())
        return false;

    g_gameVersion = DetectGameVersion(a_f4se);
    const bool supported = g_gameVersion == GameVersion::OG || g_gameVersion == GameVersion::AE;
    BootstrapLog(std::string("query: editor=0 supported=") + (supported ? "1" : "0"));
    return supported;
}

extern "C" __declspec(dllexport) bool F4SEAPI
F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
    BootstrapLog("load: enter");
    F4SE::Init(a_f4se, { .log = false });

    if (g_gameVersion == GameVersion::Unknown)
        g_gameVersion = DetectGameVersion(a_f4se);

    InitializeLogger();

    if (g_gameVersion != GameVersion::OG && g_gameVersion != GameVersion::AE) {
        logger->critical("Unsupported Fallout 4 runtime: {}. Supported: OG 1.10.163 and AE 1.11.240.",
            GameVersionString(g_gameVersion));
        return false;
    }

    logger->info("Runtime: {}", GameVersionString(g_gameVersion));
    logger->info("Plugin version 0.7.3 loaded.");
    {
        std::error_code ec;
        const auto config = std::filesystem::current_path(ec) / L"Data/F4SE/Plugins/VolClouds/clouds.json";
        logger->info("Config expected at '{}' (exists={}).", config.string(), std::filesystem::exists(config, ec));
    }

    auto* messaging = F4SE::GetMessagingInterface();
    if (!messaging) {
        logger->critical("F4SE messaging interface unavailable.");
        return false;
    }
    if (!messaging->RegisterListener(MessageHandler)) {
        logger->critical("Failed to register F4SE message listener.");
        return false;
    }

    logger->info("Plugin loaded; awaiting game data ready.");
    return true;
}
