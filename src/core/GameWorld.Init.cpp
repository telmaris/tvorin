#include "core/GameWorldInternal.h"
#include "core/Log.h"

#include <chrono>
#include <limits>

using namespace GameWorldInternal;

namespace
{
    constexpr int MultiplayerHumanSlots = 2;

    // Starting-village distance is measured on the actual road, not as a
    // straight-line distance.
    constexpr int kStartingVillageGap = 20;
    constexpr int kMinStartingVillageRoadTiles = 20;
    constexpr int kMaxStartingVillageRoadTiles = 30;
    constexpr int kMaxWorldLayoutAttempts = 8;

    void ApplyProvinceResourceProfile(MapParameters& mapParameters,
                                      const BuildableProvinceParameters& provinceParameters)
    {
        if (provinceParameters.resourceDeposits.empty())
            return;
        std::vector<ResourceProfileDeposit> deposits;
        deposits.reserve(provinceParameters.resourceDeposits.size());
        for (const auto& deposit : provinceParameters.resourceDeposits)
            deposits.push_back({deposit.resource, static_cast<float>(deposit.richness)});
        MapGenerator::ApplyResourceProfile(mapParameters, deposits);
    }

    const char* LayoutFailureName(WorldLayoutFailure failure)
    {
        switch (failure)
        {
            case WorldLayoutFailure::None: return "None";
            case WorldLayoutFailure::InvalidPlayerCount: return "InvalidPlayerCount";
            case WorldLayoutFailure::InvalidAnchors: return "InvalidAnchors";
            case WorldLayoutFailure::MissingStartingVillagePlan: return "MissingStartingVillagePlan";
            case WorldLayoutFailure::InternalError: return "InternalError";
        }
        return "InternalError";
    }

    // Retries remain reproducible from the original seed.
    unsigned int PerturbSeedForRetry(unsigned int seed, int attempt)
    {
        return seed ^ (0xB5297A4Du * (static_cast<unsigned int>(attempt) + 1));
    }

    Color PlayerSlotColor(int id)
    {
        static const std::array<Color, 7> colors{
            Color{66, 154, 255, 255},
            Color{220, 72, 72, 255},
            Color{230, 151, 62, 255},
            Color{176, 86, 216, 255},
            Color{73, 181, 126, 255},
            Color{217, 210, 82, 255},
            Color{88, 196, 210, 255}
        };
        return colors[static_cast<size_t>(std::clamp(id, 0, static_cast<int>(colors.size()) - 1))];
    }

    struct StartingVillagePlan
    {
        bool haveBuildable{false};
        Vec2i bestBuildableAnchor{};

        bool haveRoutable{false};
        Vec2i bestRoutableAnchor{};
        std::size_t bestPathLength{std::numeric_limits<std::size_t>::max()};
        bool bestWithinBudget{false};

        bool IsValid() const
        {
            return haveRoutable && bestWithinBudget &&
                   bestPathLength >= static_cast<std::size_t>(kMinStartingVillageRoadTiles) &&
                   bestPathLength <= static_cast<std::size_t>(kMaxStartingVillageRoadTiles);
        }
    };

    struct StartingVillagePathField
    {
        Vec2i minBounds{};
        Vec2i maxBounds{};
        int width{0};
        std::vector<int> parent;
        std::vector<int> distance;

        bool Contains(Vec2i pos) const
        {
            return pos.x >= minBounds.x && pos.y >= minBounds.y &&
                   pos.x <= maxBounds.x && pos.y <= maxBounds.y;
        }

        int Index(Vec2i pos) const
        {
            return (pos.y - minBounds.y) * width + (pos.x - minBounds.x);
        }

        int DistanceForTile(const TileMap& tilemap, int tileId) const
        {
            const Vec2i pos = tilemap.GetCoordsFromId(tileId);
            return Contains(pos) ? distance[Index(pos)] : -1;
        }

        int ParentForTile(const TileMap& tilemap, int tileId) const
        {
            const Vec2i pos = tilemap.GetCoordsFromId(tileId);
            return Contains(pos) ? parent[Index(pos)] : -1;
        }
    };

    // Builds one reusable multi-source BFS field from the HQ perimeter. Every
    // village candidate can then inspect its own perimeter in O(perimeter)
    // time and reconstruct only the winning path. This keeps world-generation
    // cost proportional to players instead of running a map-sized BFS for
    // every one of the 20 candidate anchors.
    StartingVillagePathField BuildStartingVillagePathField(
        TileMap& tilemap, Player* player, Vec2i hqAnchor, Vec2i hqFootprint,
        Vec2i minBounds, Vec2i maxBounds,
        int maxDistance)
    {
        const int width = maxBounds.x - minBounds.x + 1;
        const int height = maxBounds.y - minBounds.y + 1;
        StartingVillagePathField field{
            minBounds, maxBounds, width,
            std::vector<int>(static_cast<size_t>(width) * height, -1),
            std::vector<int>(static_cast<size_t>(width) * height, -1)};
        const int tileCount = tilemap.params.sizeX * tilemap.params.sizeY;
        const auto& roadDefinition = GetBuildingDefinition(BuildingType::Road);
        auto passable = [&](int tileId)
        {
            Vec2i pos = tilemap.GetCoordsFromId(tileId);
            if (pos.x < minBounds.x || pos.y < minBounds.y ||
                pos.x > maxBounds.x || pos.y > maxBounds.y)
                return false;
            return tilemap.CanBuildFootprint(pos, roadDefinition.footprint, player,
                                             BuildingType::Road);
        };

        std::queue<int> frontier;
        for (int tileId : tilemap.GetAdjacentTileIds(hqAnchor, hqFootprint))
        {
            if (tileId < 0 || tileId >= tileCount || !passable(tileId))
                continue;
            const Vec2i pos = tilemap.GetCoordsFromId(tileId);
            if (!field.Contains(pos))
                continue;
            const int localId = field.Index(pos);
            if (field.distance[localId] >= 0)
                continue;
            field.distance[localId] = 0;
            frontier.push(tileId);
        }

        while (!frontier.empty())
        {
            const int current = frontier.front();
            frontier.pop();
            const Vec2i pos = tilemap.GetCoordsFromId(current);
            const int currentLocalId = field.Index(pos);
            if (field.distance[currentLocalId] >= maxDistance)
                continue;
            const std::array<Vec2i, 4> neighbours{
                Vec2i{pos.x + 1, pos.y}, Vec2i{pos.x - 1, pos.y},
                Vec2i{pos.x, pos.y + 1}, Vec2i{pos.x, pos.y - 1}};
            for (Vec2i next : neighbours)
            {
                if (!tilemap.IsInside(next) || !field.Contains(next))
                    continue;
                const int nextId = tilemap.GetIdFromCoords(next);
                const int nextLocalId = field.Index(next);
                if (field.distance[nextLocalId] >= 0 || !passable(nextId))
                    continue;
                field.parent[nextLocalId] = current;
                field.distance[nextLocalId] = field.distance[currentLocalId] + 1;
                frontier.push(nextId);
            }
        }
        return field;
    }

    std::vector<int> FindPathFromStartingVillageField(
        TileMap& tilemap, const StartingVillagePathField& field,
        Vec2i candidateAnchor, Vec2i candidateFootprint)
    {
        const std::vector<int> adjacent = tilemap.GetAdjacentTileIds(
            candidateAnchor, candidateFootprint);
        int reached = -1;
        int bestDistance = std::numeric_limits<int>::max();
        for (int tileId : adjacent)
        {
            if (tileId < 0)
                continue;
            const int distance = field.DistanceForTile(tilemap, tileId);
            if (distance < 0)
                continue;
            if (distance < bestDistance)
            {
                reached = tileId;
                bestDistance = distance;
            }
        }
        if (reached < 0)
            return {};

        std::vector<int> path;
        for (int cursor = reached; cursor >= 0;
             cursor = field.ParentForTile(tilemap, cursor))
            path.push_back(cursor);
        return path;
    }

    // Chooses the same plan during map-generation preflight and during actual
    // placement. The complete 4x5 candidate set is small; enumerating it
        // avoids proving one random offset safe and then choosing another one.
    StartingVillagePlan FindStartingVillagePlan(TileMap& tilemap,
                                                Player* player,
                                                Vec2i hqAnchor)
    {
        StartingVillagePlan plan;
        const Vec2i hqFootprint = MapGenerator::HeadquartersFootprint();
        Village villagePreview{0};
        const Vec2i villageFootprint = villagePreview.GetFootprint();

        struct VillageCandidate
        {
            Vec2i anchor{};
        };

        std::vector<VillageCandidate> candidates;
        candidates.reserve(20);
        for (int side = 0; side < 4; side++)
        {
            for (int offset = -2; offset <= 2; offset++)
            {
                switch (side)
                {
                    case 0:
                        candidates.push_back({
                            {hqAnchor.x - kStartingVillageGap - villageFootprint.x,
                             hqAnchor.y + offset}});
                        break;
                    case 1:
                        candidates.push_back({
                            {hqAnchor.x + hqFootprint.x + kStartingVillageGap,
                             hqAnchor.y + offset}});
                        break;
                    case 2:
                        candidates.push_back({
                            {hqAnchor.x + offset,
                             hqAnchor.y - kStartingVillageGap - villageFootprint.y}});
                        break;
                    default:
                        candidates.push_back({
                            {hqAnchor.x + offset,
                             hqAnchor.y + hqFootprint.y + kStartingVillageGap}});
                        break;
                }
            }
        }

        // All candidates are known before the search starts. Restrict the
        // multi-source field to the HQ/candidate corridor and to the largest
        // acceptable road budget; the old implementation flooded the entire
        // province once per player.
        Vec2i minBounds{hqAnchor.x - 2, hqAnchor.y - 2};
        Vec2i maxBounds{hqAnchor.x + hqFootprint.x + 2,
                        hqAnchor.y + hqFootprint.y + 2};
        for (const VillageCandidate& candidate : candidates)
        {
            minBounds.x = std::min(minBounds.x, candidate.anchor.x - 2);
            minBounds.y = std::min(minBounds.y, candidate.anchor.y - 2);
            maxBounds.x = std::max(maxBounds.x, candidate.anchor.x + villageFootprint.x + 2);
            maxBounds.y = std::max(maxBounds.y, candidate.anchor.y + villageFootprint.y + 2);
        }
        minBounds.x = std::clamp(minBounds.x, 0, tilemap.params.sizeX - 1);
        minBounds.y = std::clamp(minBounds.y, 0, tilemap.params.sizeY - 1);
        maxBounds.x = std::clamp(maxBounds.x, 0, tilemap.params.sizeX - 1);
        maxBounds.y = std::clamp(maxBounds.y, 0, tilemap.params.sizeY - 1);
        const StartingVillagePathField pathField = BuildStartingVillagePathField(
            tilemap, player, hqAnchor, hqFootprint,
            minBounds, maxBounds,
            kMaxStartingVillageRoadTiles + 4);

        for (VillageCandidate candidate : candidates)
        {
            candidate.anchor = ClampAnchor(candidate.anchor, villageFootprint, tilemap.params);
            if (!tilemap.CanBuildFootprint(candidate.anchor, villageFootprint, player,
                                           BuildingType::Village))
                continue;

            if (!plan.haveBuildable)
            {
                plan.haveBuildable = true;
                plan.bestBuildableAnchor = candidate.anchor;
            }

            // Only a route within the bounded starting-road budget is accepted.
            // If it does not fit, GenerateWorldLayout retries the map seed.
            std::vector<int> path = FindPathFromStartingVillageField(
                tilemap, pathField, candidate.anchor, villageFootprint);
            if (path.empty())
                continue;

            const bool withinBudget =
                path.size() >= static_cast<std::size_t>(kMinStartingVillageRoadTiles) &&
                path.size() <= static_cast<std::size_t>(kMaxStartingVillageRoadTiles);
            const bool betterRoutable =
                !plan.haveRoutable ||
                (withinBudget != plan.bestWithinBudget && withinBudget) ||
                (withinBudget == plan.bestWithinBudget &&
                 path.size() < plan.bestPathLength);
            if (betterRoutable)
            {
                plan.haveRoutable = true;
                plan.bestRoutableAnchor = candidate.anchor;
                plan.bestPathLength = path.size();
                plan.bestWithinBudget = withinBudget;
            }
        }

        return plan;
    }

    bool ValidateStartingVillagePlans(TileMap& tilemap,
                                      const std::vector<Vec2i>& hqAnchors,
                                      int playerCount)
    {
        if (playerCount <= 0 || hqAnchors.size() != static_cast<size_t>(playerCount))
            return false;

        for (int playerId = 0; playerId < playerCount; playerId++)
        {
            StartingVillagePlan plan = FindStartingVillagePlan(
                tilemap, nullptr, hqAnchors[playerId]);
            if (!plan.IsValid())
            {
                Log::Msg("[MapGenerator]", "starting village validation failed for player ",
                         playerId, " (road tiles ",
                         plan.haveRoutable ? std::to_string(plan.bestPathLength) : "unroutable",
                         "), retrying");
                return false;
            }
        }
        return true;
    }

    // Adds a debug resource package to the player's headquarters.
    void GrantDebugResourcesToHeadquarters(Player* player, int amount)
    {
        if (player == nullptr || amount <= 0)
            return;

        for (auto* building : player->GetTrackedBuildingsWithComponent<StorageComponent>())
        {
            auto* storage = building != nullptr ? building->GetComponent<StorageComponent>() : nullptr;
            if (storage == nullptr || building->buildingType != BuildingType::Headquarters)
                continue;

            for (ResourceType type : resourceTypes)
            {
                auto& buffer = storage->buffers[type];
                if (buffer.type == ResourceType::Null)
                    buffer = ResourceBuffer{type, amount};
                buffer.bufferSize = std::max(buffer.bufferSize, static_cast<int>(buffer.buffer.size()) + amount);
                for (int i = 0; i < amount; i++)
                    buffer.GenerateResource(type);
            }
            Log::Msg("[Debug]", "starting HQ received ", amount, " of every resource");
            return;
        }
    }

    void GrantDebugManpower(Player* player)
    {
        if (player == nullptr)
            return;
        int cap = player->GetPopulationCap();
        int gift = static_cast<int>(cap * 0.7);
        if (gift > 0)
        {
            player->AddManpower(static_cast<double>(gift));
            Log::Msg("[Debug]", player->name, " received ", gift, " manpower (70% of cap ", cap, ")");
        }
    }

}

// World-generation retries happen before any player-visible state exists. A
// failed terrain, anchor or starting-settlement attempt is discarded in full
// and rebuilt from a deterministic seed derived from the requested seed.
GameWorld::WorldLayoutResult GameWorld::GenerateWorldLayout(TileMap& targetMap,
                                                            MapParameters& params,
                                                            int playerCount)
{
    TileMap& tilemap = targetMap;
    WorldLayoutResult result;
    result.hqFootprint = MapGenerator::HeadquartersFootprint();
    result.requestedSeed = params.seed;
    result.finalSeed = params.seed;

    const auto generationStarted = std::chrono::steady_clock::now();
    auto elapsedMs = [](std::chrono::steady_clock::time_point begin)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - begin).count();
    };
    long long terrainMs = 0;
    long long anchorsMs = 0;
    long long validationMs = 0;
    auto logAttempt = [&](WorldLayoutFailure failure, const std::string& detail)
    {
        Log::Msg("[MapGenerator]", "layout attempt preset=",
                 static_cast<int>(params.sizePreset), " size=", params.sizeX,
                 "x", params.sizeY, " players=", playerCount,
                 " seed=", result.finalSeed, " attempt=", result.attempts,
                 " failure=", LayoutFailureName(failure),
                 " timings_ms terrain=", terrainMs, " anchors=", anchorsMs,
                 " validation=", validationMs,
                 " total=", elapsedMs(generationStarted), " detail=", detail);
    };

    if (playerCount <= 0 || playerCount > 7)
    {
        result.failure = WorldLayoutFailure::InvalidPlayerCount;
        result.failureReason = "invalid player count";
        logAttempt(result.failure, result.failureReason);
        return result;
    }

    for (int attempt = 0; attempt < kMaxWorldLayoutAttempts; ++attempt)
    {
        result.attempts = attempt + 1;
        result.finalSeed = attempt == 0
            ? result.requestedSeed
            : PerturbSeedForRetry(result.requestedSeed, attempt);
        params.seed = result.finalSeed;
        terrainMs = 0;
        anchorsMs = 0;
        validationMs = 0;

        tilemap.tilemap.clear();
        const auto terrainStarted = std::chrono::steady_clock::now();
        tilemap.generator.GenerateTileMap(tilemap, params);
        terrainMs = elapsedMs(terrainStarted);

        const auto anchorsStarted = std::chrono::steady_clock::now();
        const std::vector<Vec2i> anchors = MapGenerator::PickHeadquartersAnchors(
            params, playerCount);
        anchorsMs = elapsedMs(anchorsStarted);
        bool validAnchors = anchors.size() == static_cast<size_t>(playerCount);
        for (size_t i = 0; validAnchors && i < anchors.size(); i++)
        {
            if (!tilemap.IsInsideFootprint(anchors[i], result.hqFootprint))
            {
                validAnchors = false;
                break;
            }
            for (size_t j = 0; j < i; j++)
            {
                if (anchors[i] == anchors[j])
                {
                    validAnchors = false;
                    break;
                }
            }
        }

        if (!validAnchors)
        {
            result.failure = WorldLayoutFailure::InvalidAnchors;
            result.failureReason = "headquarters anchors are incomplete or overlap";
            logAttempt(result.failure, result.failureReason);
            tilemap.tilemap.clear();
            continue;
        }

        const auto validationStarted = std::chrono::steady_clock::now();
        if (!ValidateStartingVillagePlans(tilemap, anchors, playerCount))
        {
            result.failure = WorldLayoutFailure::MissingStartingVillagePlan;
            result.failureReason = "starting village plan is unavailable";
            validationMs = elapsedMs(validationStarted);
            logAttempt(result.failure, result.failureReason);
            tilemap.tilemap.clear();
            continue;
        }

        result.success = true;
        result.failure = WorldLayoutFailure::None;
        validationMs = elapsedMs(validationStarted);
        result.anchors = anchors;
        logAttempt(result.failure, "layout accepted");
        return result;
    }

    if (result.failure == WorldLayoutFailure::None)
    {
        result.failure = WorldLayoutFailure::InternalError;
        result.failureReason = "world layout retry budget exhausted";
    }
    tilemap.tilemap.clear();
    return result;
}

Player* GameWorld::CreatePlayer(int id, PlayerControllerType controllerType, const std::string& name,
                                Color color, TileMap& map)
{
    auto player = std::make_unique<Player>(id);
    // Runtime players are bound to their campaign province. The map argument
    // is only the local map selected by the caller (and is kept here so the
    // starting-layout helpers do not need to know about campaign ownership).
    auto stateIt = pendingHomeProvinceByPlayer.find(static_cast<PlayerId>(id));
    if (stateIt != pendingHomeProvinceByPlayer.end())
    {
        auto* province = dynamic_cast<BuildableProvince*>(
            globalMap.FindProvince(stateIt->second));
        if (province != nullptr)
        {
            auto& simulation = province->CreateSimulation();
            simulation.BindExternalTileMap(map);
            player->BindProvince(simulation);
        }
    }
    player->name = name;
    player->controllerType = controllerType;
    player->color = color;

    Player* ptr = player.get();
    playerHandler.players[id] = std::move(player);
    AttachControllerForPlayer(ptr);
    return ptr;
}

Vec2i GameWorld::CreateStartingHq(Player* player, Vec2i hqAnchor, unsigned int seed, TileMap& map)
{
    if (player == nullptr)
        return hqAnchor;

    TileMap& tilemap = map;
    Vec2i hqFootprint = MapGenerator::HeadquartersFootprint();
    hqAnchor = ClampAnchor(hqAnchor, hqFootprint, tilemap.params);
    std::mt19937 resourceRng(seed ^ 0xC2B2AE35u);
    MapGenerator::PrepareStartingArea(tilemap, hqAnchor, resourceRng);
    SetFootprintTerrain(tilemap, hqAnchor, hqFootprint, TileType::GRASS, resourceRng, 3);

    player->Build<Headquarters>(hqAnchor, false);
    return hqAnchor;
}

void GameWorld::CreateStartingVillageAndResources(Player* player, Vec2i hqAnchor,
                                                  unsigned int seed, TileMap& map)
{
    if (player == nullptr)
        return;

    TileMap& tilemap = map;
    Vec2i hqFootprint = MapGenerator::HeadquartersFootprint();
    hqAnchor = ClampAnchor(hqAnchor, hqFootprint, tilemap.params);
    std::mt19937 resourceRng(seed ^ 0xC2B2AE35u);

    Village villagePreview{0};
    Vec2i villageFootprint = villagePreview.GetFootprint();
    // The village sits 20 tiles beyond the HQ footprint. The actual route is
    // measured below so the starting road stays within its bounded budget.
    const int gap = kStartingVillageGap;

    // Measure each candidate's real road length up front via
    // FindRoadPathBetweenFootprints. No route is committed until the actual
    // road length passes the preflight check.
    // The budget counts only actual Road tiles (perimeter-to-perimeter, not
    // the HQ/Village footprints themselves) — matches CountOwnedBuildings(Road).
    constexpr int kMaxVillageRoadTiles = kMaxStartingVillageRoadTiles;

    struct VillageCandidate
    {
        Vec2i anchor{};
    };

    std::vector<VillageCandidate> villageCandidates;
    auto addCandidate = [&](int side, int offset)
    {
        switch (side)
        {
            case 0:
                villageCandidates.push_back({
                    {hqAnchor.x - gap - villageFootprint.x, hqAnchor.y + offset}});
                break;
            case 1:
                villageCandidates.push_back({
                    {hqAnchor.x + hqFootprint.x + gap, hqAnchor.y + offset}});
                break;
            case 2:
                villageCandidates.push_back({
                    {hqAnchor.x + offset, hqAnchor.y - gap - villageFootprint.y}});
                break;
            default:
                villageCandidates.push_back({
                    {hqAnchor.x + offset, hqAnchor.y + hqFootprint.y + gap}});
                break;
        }
    };

    // Always include one centered candidate on every side, then enumerate
    // every offset so the opposite side is guaranteed to be considered.
    for (int side = 0; side < 4; side++)
        addCandidate(side, 0);

    // Enumerate every offset so the placement pass uses exactly the same
    // candidate set as the map-generation preflight.
    for (int side = 0; side < 4; side++)
        for (int offset = -2; offset <= 2; offset++)
            addCandidate(side, offset);

    // Keep a buildable fallback only for diagnostics; GenerateWorldLayout
    // should already have rejected any map without a valid routable plan.
    bool haveBuildable = false;
    Vec2i bestBuildableAnchor{};
    bool haveRoutable = false;
    Vec2i bestRoutableAnchor{};
    std::size_t bestPathLength = std::numeric_limits<std::size_t>::max();
    bool bestWithinBudget = false;

    for (VillageCandidate candidate : villageCandidates)
    {
        candidate.anchor = ClampAnchor(candidate.anchor, villageFootprint, tilemap.params);
        if (!tilemap.CanBuildFootprint(candidate.anchor, villageFootprint, player))
            continue;

        if (!haveBuildable)
        {
            haveBuildable = true;
            bestBuildableAnchor = candidate.anchor;
        }

        std::vector<int> path = FindRoadPathBetweenFootprints(
            tilemap, player, candidate.anchor, villageFootprint,
            hqAnchor, hqFootprint);
        if (path.empty())
            continue;

        const bool withinBudget =
            path.size() >= static_cast<std::size_t>(kMinStartingVillageRoadTiles) &&
            path.size() <= static_cast<std::size_t>(kMaxVillageRoadTiles);
        const bool betterRoutable =
            !haveRoutable ||
            (withinBudget != bestWithinBudget && withinBudget) ||
            (withinBudget == bestWithinBudget && path.size() < bestPathLength);
        if (betterRoutable)
        {
            haveRoutable = true;
            bestPathLength = path.size();
            bestRoutableAnchor = candidate.anchor;
            bestWithinBudget = withinBudget;
        }
    }

    if (!haveBuildable || !haveRoutable || !bestWithinBudget ||
        bestPathLength < static_cast<std::size_t>(kMinStartingVillageRoadTiles))
    {
        Log::Msg("[MapGenerator]", "Starting village placement rejected: no road in range ",
                 kMinStartingVillageRoadTiles, "..", kMaxVillageRoadTiles);
        return;
    }
    Vec2i villageAnchor = bestRoutableAnchor;
    SetFootprintTerrain(tilemap, villageAnchor, villageFootprint, TileType::GRASS, resourceRng, 3);
    Building* village = player->Build<Village>(villageAnchor, false);

    if (village != nullptr)
        BuildStartRoad(player, villageAnchor, villageFootprint, hqAnchor, hqFootprint);

    const auto resourceLayout = GameWorldInternal::PlanStartingResourceLayout(
        tilemap, hqAnchor, hqFootprint, villageAnchor, villageFootprint, resourceRng);
    if (!GameWorldInternal::PlaceStartingResourceLayout(tilemap, resourceLayout, resourceRng))
        Log::Msg("[MapGenerator]", "Starting resource layout failed for player ", player->id);
}

bool GameWorld::InitializeGlobalCampaign(
    int playerCount, const GlobalMapGenerationParameters& parameters)
{
    std::vector<PlayerId> humanPlayers;
    humanPlayers.reserve(static_cast<std::size_t>(playerCount));
    for (int playerId = 0; playerId < playerCount; ++playerId)
        humanPlayers.push_back(static_cast<PlayerId>(playerId));

    auto generated = GlobalMapGenerator::Generate(parameters, humanPlayers);
    if (!generated.success)
    {
        initializationError = "Global map generation failed: " + generated.failureReason;
        return false;
    }

    globalMap = std::move(generated.map);
    pendingHomeProvinceByPlayer.clear();
    for (const auto& [playerId, homeProvinceId] : generated.homeProvinceByPlayer)
    {
        auto* province = dynamic_cast<BuildableProvince*>(globalMap.FindProvince(homeProvinceId));
        if (province == nullptr)
        {
            initializationError = "Global map start province is invalid";
            return false;
        }

        pendingHomeProvinceByPlayer[playerId] = homeProvinceId;
    }
    return true;
}

bool GameWorld::InitializeGlobalCampaign(int playerCount, std::uint32_t seed)
{
    GlobalMapGenerationParameters parameters;
    parameters.seed = seed;
    return InitializeGlobalCampaign(playerCount, parameters);
}

bool GameWorld::BindPlayersToGlobalCampaign()
{
    for (const auto& [playerId, homeProvinceId] : pendingHomeProvinceByPlayer)
    {
        auto playerIt = playerHandler.players.find(static_cast<int>(playerId));
        auto* province = dynamic_cast<BuildableProvince*>(globalMap.FindProvince(homeProvinceId));
        if (playerIt == playerHandler.players.end() || playerIt->second == nullptr || province == nullptr)
        {
            initializationError = "Global map start province is not bound to a human player";
            return false;
        }
        Player* player = playerIt->second.get();
        player->homeProvinceId = homeProvinceId;
        if (!globalMap.SetBuildableOwner(playerId, homeProvinceId))
        {
            initializationError = "Global map start province ownership could not be assigned";
            return false;
        }
        auto& simulation = province->CreateSimulation();
        if (player->GetTileMap() != nullptr)
            province->BindSimulationToTileMap(*player->GetTileMap());
        player->BindProvince(simulation);
        simulation.SetOwnerId(playerId);
        if (province->GetKnowledge(playerId) != ProvinceKnowledgeLevel::Owned &&
            !globalMap.InitializeDiscovery(playerId, homeProvinceId))
            return false;
    }
    pendingHomeProvinceByPlayer.clear();
    return true;
}

// Initializes runtime state for this object. Terrain, HQ anchors and the
// starting-settlement plan are accepted before any player or building exists;
// only then are the local human and its starting buildings created.
bool GameWorld::InitWorld(std::string name, Renderer* r, MapParameters params,
                          const GenerationProgressCallback& progress)
{
    return InitWorld(std::move(name), r,
                     CampaignGenerationParameters{std::move(params)}, progress);
}

bool GameWorld::InitWorld(std::string name, Renderer* r,
                          CampaignGenerationParameters campaign,
                          const GenerationProgressCallback& progress)
{
    initialized = false;
    initializationError.clear();
    worldName = name;
    render = r;
    globalMap = GlobalMap{};
    eventSystem.Clear();
    eventSystem.SetCampaignSeed(campaign.globalMap.seed);
    armyJourneySystem.Clear();
    battleSystem.Clear();
    activeTradeOrders.clear();
    nextTradeOrderId = 1;
    campaignGenerationParameters = campaign;
    pendingColonizations.clear();
    pendingHomeProvinceByPlayer.clear();
    playerHandler.players.clear();
    controllers.clear();
    const auto reportProgress = [&](float value, const char* message)
    {
        if (progress)
            progress(std::clamp(value, 0.0f, 1.0f), message);
    };
    reportProgress(0.04f, "Preparing world parameters");

    MapParameters params = campaign.localMap;
    params.aiOpponentCount = 0;
    params.aiDifficulty = 0;
    constexpr int playerCount = 1;
    if (!InitializeGlobalCampaign(playerCount, campaign.globalMap))
        return false;
    const ProvinceId homeProvinceId = pendingHomeProvinceByPlayer.at(0);
    MapParameters localMapParams = campaign.localMap;
    localMapParams.seed = GlobalMapGenerator::DeriveProvinceSeed(
        campaign.globalMap.seed, homeProvinceId);
    auto* homeProvince = dynamic_cast<BuildableProvince*>(globalMap.FindProvince(homeProvinceId));
    if (homeProvince == nullptr)
    {
        initializationError = "Global map home province is missing";
        return false;
    }
    if (!homeProvince->GetParameters().naturalResourceTypes.empty())
        MapGenerator::FilterResourcePatchesForProfile(
            localMapParams, homeProvince->GetParameters().naturalResourceTypes);
    ApplyProvinceResourceProfile(localMapParams, homeProvince->GetParameters());
    TileMap& primaryMap = homeProvince->CreateSimulation().GetTileMap();
    reportProgress(0.14f, "Generating terrain and starting area");
    WorldLayoutResult layout = GenerateWorldLayout(primaryMap, localMapParams, playerCount);
    if (!layout.success)
    {
        initializationError = std::string("World generation failed [") +
            LayoutFailureName(layout.failure) + "] (seed " +
            std::to_string(layout.requestedSeed) + ", last attempt seed " +
            std::to_string(layout.finalSeed) + "): " +
            (layout.failureReason.empty() ? "no valid layout" : layout.failureReason);
        Log::Msg("[MapGenerator]", initializationError);
        return false;
    }
    const std::vector<Vec2i>& anchors = layout.anchors;
    const Vec2i hqFootprint = layout.hqFootprint;
    reportProgress(0.60f, "Creating players");

    localPlayerId = 0;
    auto* human = CreatePlayer(0, PlayerControllerType::LocalHuman, "Player", PlayerSlotColor(0), primaryMap);

    std::map<int, Vec2i> hqAnchorsByPlayer{{0, anchors[0]}};
    std::map<int, Player*> playersById{{0, human}};
    std::map<int, unsigned int> baseSeedByPlayer{{0, localMapParams.seed}};

    reportProgress(0.72f, "Placing headquarters");
    for (const auto& [playerId, anchor] : hqAnchorsByPlayer)
        CreateStartingHq(playersById.at(playerId), anchor, baseSeedByPlayer.at(playerId), primaryMap);

    reportProgress(0.82f, "Building starting settlements");
    for (const auto& [playerId, anchor] : hqAnchorsByPlayer)
    {
        Player* p = playersById.at(playerId);
        CreateStartingVillageAndResources(p, anchor, baseSeedByPlayer.at(playerId), primaryMap);
        // Debug resources are an explicit local-human aid. Normal starting
        // stock comes exclusively from the HQ/Village data definitions.
        if (params.debugMode)
        {
            p->debugMode = true;
            if (playerId == 0)
            {
                GrantDebugResourcesToHeadquarters(p, 50);
                GrantDebugManpower(p);
            }
        }
    }

    if (!BindPlayersToGlobalCampaign())
        return false;
    reportProgress(0.94f, "Finalizing visibility and camera");
    if (render != nullptr)
    {
        render->camera.zoom = 1.75f;
        render->camera.rotation = 0.0f;
        Vec2f hqWorldCenter{
            static_cast<float>(anchors[0].x * TILE_SIZE) + hqFootprint.x * TILE_SIZE * 0.5f,
            static_cast<float>(anchors[0].y * TILE_SIZE) + hqFootprint.y * TILE_SIZE * 0.5f};
        render->CenterCameraOnWorld(hqWorldCenter, {primaryMap.params.sizeX, primaryMap.params.sizeY});
        cachedCameraTarget = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        cachedCameraZoom = -1.0f;
    }
    UpdateFogOfWar();
    initialized = true;
    reportProgress(1.0f, "World ready");
    return true;
}

// Initializes deterministic multiplayer runtime state with server-assigned slots.
bool GameWorld::InitMultiplayerWorld(std::string name, Renderer* r,
                                     MapParameters params, int localId,
                                     bool authoritativeHost,
                                     const GenerationProgressCallback& progress)
{
    return InitMultiplayerWorld(std::move(name), r,
                                CampaignGenerationParameters{std::move(params)},
                                localId, authoritativeHost, progress);
}

bool GameWorld::InitMultiplayerWorld(std::string name, Renderer* r,
                                     CampaignGenerationParameters campaign, int localId,
                                     bool authoritativeHost,
                                     const GenerationProgressCallback& progress)
{
    initialized = false;
    initializationError.clear();
    worldName = name;
    render = r;
    globalMap = GlobalMap{};
    eventSystem.Clear();
    eventSystem.SetCampaignSeed(campaign.globalMap.seed);
    armyJourneySystem.Clear();
    battleSystem.Clear();
    activeTradeOrders.clear();
    nextTradeOrderId = 1;
    campaignGenerationParameters = campaign;
    pendingColonizations.clear();
    pendingHomeProvinceByPlayer.clear();
    playerHandler.players.clear();
    controllers.clear();
    const auto reportProgress = [&](float value, const char* message)
    {
        if (progress)
            progress(std::clamp(value, 0.0f, 1.0f), message);
    };
    reportProgress(0.04f, "Preparing multiplayer world");

    localPlayerId = std::clamp(localId, 0, MultiplayerHumanSlots - 1);
    MapParameters params = campaign.localMap;
    params.aiOpponentCount = 0;
    params.aiDifficulty = 0;
    constexpr int playerCount = MultiplayerHumanSlots;
    (void)authoritativeHost;
    if (!InitializeGlobalCampaign(playerCount, campaign.globalMap))
        return false;
    const ProvinceId hostHomeProvinceId = pendingHomeProvinceByPlayer.at(0);
    MapParameters primaryMapParams = campaign.localMap;
    primaryMapParams.seed = GlobalMapGenerator::DeriveProvinceSeed(
        campaign.globalMap.seed, hostHomeProvinceId);
    auto* hostProvince = dynamic_cast<BuildableProvince*>(globalMap.FindProvince(hostHomeProvinceId));
    if (hostProvince == nullptr)
    {
        initializationError = "Global map host home province is missing";
        return false;
    }
    if (!hostProvince->GetParameters().naturalResourceTypes.empty())
        MapGenerator::FilterResourcePatchesForProfile(
            primaryMapParams, hostProvince->GetParameters().naturalResourceTypes);
    ApplyProvinceResourceProfile(primaryMapParams, hostProvince->GetParameters());
    TileMap& primaryMap = hostProvince->CreateSimulation().GetTileMap();
    reportProgress(0.14f, "Generating peaceful terrain and starting areas");
    WorldLayoutResult layout = GenerateWorldLayout(primaryMap, primaryMapParams, 1);
    if (!layout.success)
    {
        initializationError = std::string("World generation failed [") +
            LayoutFailureName(layout.failure) + "] (seed " +
            std::to_string(layout.requestedSeed) + ", last attempt seed " +
            std::to_string(layout.finalSeed) + "): " +
            (layout.failureReason.empty() ? "no valid layout" : layout.failureReason);
        Log::Msg("[MapGenerator]", initializationError);
        return false;
    }
    const std::vector<Vec2i>& anchors = layout.anchors;
    const Vec2i hqFootprint = layout.hqFootprint;
    reportProgress(0.60f, "Creating player slots");

    std::map<int, Vec2i> hqAnchorsByPlayer;
    std::map<int, Player*> playersById;
    std::map<int, TileMap*> mapsByPlayer;
    std::map<int, unsigned int> baseSeedByPlayer;
    Vec2i cameraAnchor = anchors[0];

    // Pass 1: create both human slots and pick their HQ anchors before placing
    // any building.
    for (int playerId = 0; playerId < MultiplayerHumanSlots; playerId++)
    {
        PlayerControllerType controllerType = PlayerControllerType::Remote;
        if (playerId == localPlayerId)
            controllerType = PlayerControllerType::LocalHuman;

        std::string playerName = playerId == 0 ? "Host" : "Client";
        TileMap* playerMap = &primaryMap;
        Vec2i anchor = anchors[0];
        if (playerId != 0)
        {
            const auto stateIt = pendingHomeProvinceByPlayer.find(static_cast<PlayerId>(playerId));
            auto* province = stateIt == pendingHomeProvinceByPlayer.end()
                ? nullptr
                : dynamic_cast<BuildableProvince*>(globalMap.FindProvince(stateIt->second));
            if (province == nullptr)
            {
                initializationError = "Global map client start province is missing";
                return false;
            }
            auto& simulation = province->CreateSimulation();
            MapParameters localParams = campaign.localMap;
            localParams.seed = GlobalMapGenerator::DeriveProvinceSeed(
                campaign.globalMap.seed, stateIt->second);
            localParams.aiOpponentCount = 0;
            localParams.aiDifficulty = 0;
            if (!province->GetParameters().naturalResourceTypes.empty())
                MapGenerator::FilterResourcePatchesForProfile(
                    localParams, province->GetParameters().naturalResourceTypes);
            ApplyProvinceResourceProfile(localParams, province->GetParameters());
            const WorldLayoutResult localLayout = GenerateWorldLayout(
                simulation.GetTileMap(), localParams, 1);
            if (!localLayout.success)
            {
                initializationError = "Client province local map generation failed";
                return false;
            }
            anchor = localLayout.anchors.front();
            playerMap = &simulation.GetTileMap();
        }
        auto* player = CreatePlayer(playerId, controllerType, playerName, PlayerSlotColor(playerId), *playerMap);
        hqAnchorsByPlayer[playerId] = anchor;
        playersById[playerId] = player;
        mapsByPlayer[playerId] = playerMap;
        baseSeedByPlayer[playerId] = GlobalMapGenerator::DeriveProvinceSeed(
            campaign.globalMap.seed,
            pendingHomeProvinceByPlayer.at(static_cast<PlayerId>(playerId)));
        if (playerId == localPlayerId)
            cameraAnchor = anchor;
    }

    // Pass 2: place both Headquarters.
    reportProgress(0.72f, "Placing headquarters");
    for (const auto& [playerId, anchor] : hqAnchorsByPlayer)
        CreateStartingHq(playersById.at(playerId), anchor, baseSeedByPlayer.at(playerId),
                         *mapsByPlayer.at(playerId));

    // Pass 3: village + start road + resource patches.
    reportProgress(0.82f, "Building starting settlements");
    for (const auto& [playerId, anchor] : hqAnchorsByPlayer)
        CreateStartingVillageAndResources(playersById.at(playerId), anchor,
                                          baseSeedByPlayer.at(playerId), *mapsByPlayer.at(playerId));

    if (params.debugMode)
    {
        Player* localPlayer = playerHandler.players.at(localPlayerId).get();
        if (localPlayer != nullptr)
        {
            localPlayer->debugMode = true;
            GrantDebugResourcesToHeadquarters(localPlayer, 50);
            GrantDebugManpower(localPlayer);
        }
        for (auto& [id, player] : playerHandler.players)
            if (player != nullptr)
                player->debugMode = true;
    }

    if (!BindPlayersToGlobalCampaign())
        return false;

    reportProgress(0.94f, "Finalizing visibility and camera");
    if (render != nullptr)
    {
        render->camera.zoom = 1.75f;
        render->camera.rotation = 0.0f;
        Vec2f hqWorldCenter{
            static_cast<float>(cameraAnchor.x * TILE_SIZE) + hqFootprint.x * TILE_SIZE * 0.5f,
            static_cast<float>(cameraAnchor.y * TILE_SIZE) + hqFootprint.y * TILE_SIZE * 0.5f};
        render->CenterCameraOnWorld(hqWorldCenter,
                                    {GetTileMap().params.sizeX, GetTileMap().params.sizeY});
        cachedCameraTarget = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        cachedCameraZoom = -1.0f;
    }
    UpdateFogOfWar();
    initialized = true;
    reportProgress(1.0f, "World ready");
    return true;
}

void GameWorld::AttachPresentation(Renderer* renderer)
{
    render = renderer;
    cachedCameraTarget = {std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max()};
    cachedCameraZoom = -1.0f;

    if (!initialized || render == nullptr)
        return;

    auto playerIt = playerHandler.players.find(localPlayerId);
    if (playerIt == playerHandler.players.end() || playerIt->second == nullptr)
        return;

    Building* headquarters = nullptr;
    for (Building* building : playerIt->second->GetTrackedBuildings())
    {
        if (building != nullptr &&
            building->buildingType == BuildingType::Headquarters)
        {
            headquarters = building;
            break;
        }
    }
    if (headquarters == nullptr)
        return;

    const TileMap& localMap = GetTileMap();
    const Vec2i anchor = localMap.GetCoordsFromId(headquarters->positionId);
    const Vec2i footprint = headquarters->GetFootprint();
    const Vec2f center{
        static_cast<float>(anchor.x * TILE_SIZE) + footprint.x * TILE_SIZE * 0.5f,
        static_cast<float>(anchor.y * TILE_SIZE) + footprint.y * TILE_SIZE * 0.5f};
    render->camera.zoom = 1.75f;
    render->camera.rotation = 0.0f;
    render->CenterCameraOnWorld(center, {localMap.params.sizeX, localMap.params.sizeY});
}
