#include "scenes/MultiplayerLobbyProtocol.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace
{
    MapParameters MakeNeutralMultiplayerParams(MapSizePreset sizePreset,
                                               float resourceDensity,
                                               float resourceFieldSize,
                                               float resourceRichnessSlider,
                                               bool debugMode)
    {
        MapParameters params;
        params.sizePreset = sizePreset;
        params.sizeX = MapGenerator::SizeFromPreset(params.sizePreset);
        params.sizeY = params.sizeX;
        params.seed = 27015;
        params.resourceDensity = resourceDensity;
        params.resourceFieldSize = resourceFieldSize;
        params.resourceRichness = 40 + static_cast<int>(std::round(
            std::clamp(resourceRichnessSlider, 0.0f, 1.0f) * 120.0f));
        params.aiOpponentCount = 0;
        params.aiDifficulty = 0;
        params.debugMode = debugMode;
        return params;
    }

    bool ReadCommonSettings(std::istringstream& in,
                            int& legacyAiCount,
                            int& size,
                            int& legacyAiDifficulty,
                            float& density,
                            float& fieldSize,
                            int& richness,
                            int& debug)
    {
        return static_cast<bool>(in >> legacyAiCount >> size >> legacyAiDifficulty
                                    >> density >> fieldSize >> richness >> debug);
    }

    MapParameters ParseNeutralParams(int size,
                                     float density,
                                     float fieldSize,
                                     int richness,
                                     int debug)
    {
        return MakeNeutralMultiplayerParams(
            static_cast<MapSizePreset>(std::clamp(size, 0, 3)),
            std::clamp(density, 0.0f, 1.0f),
            std::clamp(fieldSize, 0.0f, 1.0f),
            std::clamp((richness - 40) / 120.0f, 0.0f, 1.0f),
            debug != 0);
    }

    bool ReadCampaignTail(std::istringstream& in, int versionOrLegacyAiCount,
                          CampaignGenerationParameters& campaign)
    {
        int size = 0;
        int legacyAiDifficulty = 0;
        float density = 0.65f;
        float fieldSize = 0.45f;
        int richness = 120;
        int debug = 0;
        unsigned int localSeed = 27015;

        if (versionOrLegacyAiCount == 2 || versionOrLegacyAiCount == 3)
        {
            int legacyAiCount = 0;
            if (!(in >> legacyAiCount >> size >> legacyAiDifficulty >> density >> fieldSize
                    >> richness >> debug >> localSeed))
                return false;
            (void)legacyAiCount;
        }
        else
        {
            const int legacyAiCount = versionOrLegacyAiCount;
            if (!(in >> size >> legacyAiDifficulty >> density >> fieldSize
                    >> richness >> debug))
                return false;
            (void)legacyAiCount;

            std::string trailing;
            if (in >> trailing)
                return false;
            campaign = MakeCampaignGenerationParameters(
                ParseNeutralParams(size, density, fieldSize, richness, debug));
            return true;
        }

        std::uint32_t globalSeed = 0;
        int globalFogOfWar = 1;
        if (!(in >> globalSeed >> campaign.globalMap.provinceCount
                >> campaign.globalMap.extraEdgeCount
                >> campaign.globalMap.layoutRadius
                >> campaign.globalMap.minimumLayoutSpacing
                >> campaign.globalMap.maximumPlacementAttemptsPerProvince
                >> campaign.globalMap.minimumNeutralBuildables
                >> campaign.globalMap.buildableWeight
                >> campaign.globalMap.neutralCityWeight
                >> campaign.globalMap.banditCampWeight
                >> campaign.globalMap.eventSiteWeight
                >> campaign.globalMap.startBoundaryClearance
                >> campaign.globalMap.buildableWealthScale
                >> campaign.globalMap.cityWealthScale
                >> campaign.globalMap.banditStrengthScale))
            return false;

        if (versionOrLegacyAiCount >= 3 && !(in >> globalFogOfWar))
            return false;
        if (versionOrLegacyAiCount >= 3 && globalFogOfWar != 0 && globalFogOfWar != 1)
            return false;

        std::string trailing;
        if (in >> trailing)
            return false;

        campaign.localMap = ParseNeutralParams(size, density, fieldSize, richness, debug);
        campaign.localMap.seed = localSeed;
        campaign.globalMap.seed = globalSeed;
        campaign.globalMap.fogOfWarEnabled = globalFogOfWar != 0;
        return true;
    }
}

namespace MultiplayerLobbyProtocol
{
    std::string SerializeStart(const std::string& sessionName, const MapParameters& params)
    {
        std::ostringstream out;
        out << "START " << std::quoted(sessionName) << ' '
            << 0 << ' '
            << static_cast<int>(params.sizePreset) << ' '
            << 0 << ' '
            << params.resourceDensity << ' '
            << params.resourceFieldSize << ' '
            << params.resourceRichness << ' '
            << (params.debugMode ? 1 : 0);
        return out.str();
    }

    bool TryDeserializeStart(const std::string& payload, std::string& sessionName, MapParameters& params)
    {
        std::istringstream in(payload);
        int legacyAiCount = 0;
        int size = 0;
        int legacyAiDifficulty = 0;
        int richness = 120;
        int debug = 0;
        float density = 0.65f;
        float fieldSize = 0.45f;
        if (!(in >> std::quoted(sessionName)) ||
            !ReadCommonSettings(in, legacyAiCount, size, legacyAiDifficulty,
                                density, fieldSize, richness, debug))
            return false;

        (void)legacyAiCount;
        (void)legacyAiDifficulty;
        params = ParseNeutralParams(size, density, fieldSize, richness, debug);
        return true;
    }

    std::string SerializeState(const std::string& sessionName,
                               const std::string& hostName,
                               const std::string& remoteName,
                               const MapParameters& params)
    {
        std::ostringstream out;
        out << "STATE " << std::quoted(sessionName) << ' '
            << std::quoted(hostName) << ' '
            << std::quoted(remoteName) << ' '
            << 0 << ' '
            << static_cast<int>(params.sizePreset) << ' '
            << 0 << ' '
            << params.resourceDensity << ' '
            << params.resourceFieldSize << ' '
            << params.resourceRichness << ' '
            << (params.debugMode ? 1 : 0);
        return out.str();
    }

    bool TryDeserializeState(const std::string& payload,
                             std::string& sessionName,
                             std::string& hostName,
                             std::string& remoteName,
                             MapParameters& params)
    {
        std::istringstream in(payload);
        int legacyAiCount = 0;
        int size = 0;
        int legacyAiDifficulty = 0;
        int richness = 120;
        int debug = 0;
        float density = 0.65f;
        float fieldSize = 0.45f;
        if (!(in >> std::quoted(sessionName) >> std::quoted(hostName)
                >> std::quoted(remoteName)) ||
            !ReadCommonSettings(in, legacyAiCount, size, legacyAiDifficulty,
                                density, fieldSize, richness, debug))
            return false;

        (void)legacyAiCount;
        (void)legacyAiDifficulty;
        params = ParseNeutralParams(size, density, fieldSize, richness, debug);
        return true;
    }

    std::string SerializeStart(const std::string& sessionName,
                               const CampaignGenerationParameters& campaign)
    {
        const MapParameters& local = campaign.localMap;
        const GlobalMapGenerationParameters& global = campaign.globalMap;
        std::ostringstream out;
        out << std::setprecision(17)
            << "START " << std::quoted(sessionName) << ' ' << 3 << ' '
            << 0 << ' ' << static_cast<int>(local.sizePreset) << ' ' << 0 << ' '
            << local.resourceDensity << ' ' << local.resourceFieldSize << ' '
            << local.resourceRichness << ' ' << (local.debugMode ? 1 : 0) << ' '
            << local.seed << ' ' << global.seed << ' ' << global.provinceCount << ' '
            << global.extraEdgeCount << ' ' << global.layoutRadius << ' '
            << global.minimumLayoutSpacing << ' '
            << global.maximumPlacementAttemptsPerProvince << ' '
            << global.minimumNeutralBuildables << ' ' << global.buildableWeight << ' '
            << global.neutralCityWeight << ' ' << global.banditCampWeight << ' '
            << global.eventSiteWeight << ' ' << global.startBoundaryClearance << ' '
            << global.buildableWealthScale << ' ' << global.cityWealthScale << ' '
            << global.banditStrengthScale << ' ' << (global.fogOfWarEnabled ? 1 : 0);
        return out.str();
    }

    bool TryDeserializeStart(const std::string& payload, std::string& sessionName,
                             CampaignGenerationParameters& campaign)
    {
        std::istringstream in(payload);
        int versionOrLegacyAiCount = 0;
        if (!(in >> std::quoted(sessionName) >> versionOrLegacyAiCount))
            return false;
        if (versionOrLegacyAiCount == 2)
        {
            // V2 is explicit and requires the complete campaign section.
            return ReadCampaignTail(in, versionOrLegacyAiCount, campaign);
        }

        // A pre-campaign payload remains readable and receives safe global
        // defaults through the same conversion used by old callers.
        return ReadCampaignTail(in, versionOrLegacyAiCount, campaign);
    }

    std::string SerializeState(const std::string& sessionName,
                               const std::string& hostName,
                               const std::string& remoteName,
                               const CampaignGenerationParameters& campaign)
    {
        const MapParameters& local = campaign.localMap;
        const GlobalMapGenerationParameters& global = campaign.globalMap;
        std::ostringstream out;
        out << std::setprecision(17)
            << "STATE " << std::quoted(sessionName) << ' '
            << std::quoted(hostName) << ' ' << std::quoted(remoteName) << ' '
            << 3 << ' ' << 0 << ' ' << static_cast<int>(local.sizePreset) << ' ' << 0 << ' '
            << local.resourceDensity << ' ' << local.resourceFieldSize << ' '
            << local.resourceRichness << ' ' << (local.debugMode ? 1 : 0) << ' '
            << local.seed << ' ' << global.seed << ' ' << global.provinceCount << ' '
            << global.extraEdgeCount << ' ' << global.layoutRadius << ' '
            << global.minimumLayoutSpacing << ' '
            << global.maximumPlacementAttemptsPerProvince << ' '
            << global.minimumNeutralBuildables << ' ' << global.buildableWeight << ' '
            << global.neutralCityWeight << ' ' << global.banditCampWeight << ' '
            << global.eventSiteWeight << ' ' << global.startBoundaryClearance << ' '
            << global.buildableWealthScale << ' ' << global.cityWealthScale << ' '
            << global.banditStrengthScale << ' ' << (global.fogOfWarEnabled ? 1 : 0);
        return out.str();
    }

    bool TryDeserializeState(const std::string& payload,
                             std::string& sessionName,
                             std::string& hostName,
                             std::string& remoteName,
                             CampaignGenerationParameters& campaign)
    {
        std::istringstream in(payload);
        int versionOrLegacyAiCount = 0;
        if (!(in >> std::quoted(sessionName) >> std::quoted(hostName)
                >> std::quoted(remoteName) >> versionOrLegacyAiCount))
            return false;
        return ReadCampaignTail(in, versionOrLegacyAiCount, campaign);
    }
}
