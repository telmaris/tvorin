#ifndef PERSISTENCE_LIMITS_H
#define PERSISTENCE_LIMITS_H

#include <cstddef>
#include <limits>

// Product limits shared by save loading and network state restoration. These
// are element limits, not a substitute for the serialized byte cap.
struct PersistenceLimits
{
    // A local province is bounded by the XL pilot map. Campaign-wide limits
    // are introduced separately with GlobalMap in Stage 2.
    static constexpr int MaxMapDimension = 501;
    static constexpr std::size_t MaxMapTiles = 251'001;
    static constexpr int MaxSupportedPlayers = 8;
    static constexpr std::size_t MaxGlobalProvinces = 500;
    static constexpr std::size_t MaxGlobalEdges = MaxGlobalProvinces * 4;
    static constexpr std::size_t MaxProvinceMaps = MaxGlobalProvinces;
    static constexpr std::size_t MaxCampaignTiles = MaxMapTiles * MaxProvinceMaps;
    // Serialized worlds can legitimately exceed the old 64 MiB ceiling once
    // many province simulations have been materialized. This is a safety cap,
    // not a target allocation: parsers still validate every nested count.
    static constexpr std::size_t MaxSerializedStateBytes = 512u * 1024u * 1024u;
    static constexpr std::size_t SaveBackupCount = 10;

    static constexpr std::size_t MaxConcurrentWorldJourneys = 16'384;
    static constexpr std::size_t MaxRetainedWorldJourneys = 4'096;
    static constexpr std::size_t MaxWorldJourneys =
        MaxConcurrentWorldJourneys + MaxRetainedWorldJourneys;
    static constexpr std::size_t MaxJourneyPathConnections = 4'096;
    static constexpr std::size_t MaxConcurrentBattles = 2'048;
    static constexpr std::size_t MaxRetainedBattles = 1'024;
    static constexpr std::size_t MaxBattleRecords =
        MaxConcurrentBattles + MaxRetainedBattles;
    static constexpr std::size_t MaxBattleReports = 2'048;
    static constexpr std::size_t MaxWorldEventInstances = 16'384;
    static constexpr std::size_t MaxRetainedWorldEventInstances = 2'048;
    static constexpr std::size_t MaxWorldEventFeedEntries = 2'048;
    static constexpr std::size_t MaxColonizationOperations = MaxGlobalProvinces;
    static constexpr std::size_t MaxExpeditionUnits = 32;
    static constexpr std::size_t MaxProvinceKnowledgeEntries =
        MaxGlobalProvinces * MaxSupportedPlayers;
    static constexpr std::size_t MaxBuildings = MaxMapTiles;
    static constexpr std::size_t MaxRouteCount =
        static_cast<std::size_t>(MaxSupportedPlayers) * (MaxSupportedPlayers - 1);
    static constexpr std::size_t MaxRouteTiles = MaxMapTiles;
    static constexpr std::size_t MaxUnits = MaxMapTiles;
    static constexpr std::size_t MaxProjectiles = MaxMapTiles;
    static constexpr std::size_t MaxQueueEntries = MaxMapTiles;
    static constexpr std::size_t MaxBufferEntries = 256;
    // Must not exceed the fixed ResourcePool capacity.
    static constexpr std::size_t MaxActiveShipments = 1'000'000;
    static constexpr std::size_t MaxStringBytes = 4096;

    static bool CheckedArea(int width, int height, std::size_t& result) noexcept
    {
        if (width <= 0 || height <= 0 || width > MaxMapDimension || height > MaxMapDimension)
            return false;
        const auto w = static_cast<std::size_t>(width);
        const auto h = static_cast<std::size_t>(height);
        if (w > MaxMapTiles / h)
            return false;
        result = w * h;
        return result <= MaxMapTiles;
    }

    static bool IsCountInRange(int count, std::size_t maximum) noexcept
    {
        return count >= 0 && static_cast<std::size_t>(count) <= maximum;
    }

    static bool IsCountInRange(std::size_t count, std::size_t maximum) noexcept
    {
        return count <= maximum;
    }
};

#endif
