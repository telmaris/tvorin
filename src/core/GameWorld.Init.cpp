#include "core/GameWorldInternal.h"
#include "core/Log.h"
#include "ai/AIDifficulty.h"

#include <limits>
#include <set>

using namespace GameWorldInternal;

namespace
{
    constexpr int MultiplayerHumanSlots = 2;

    // Starting-village distance is measured on the actual road, not as a
    // straight-line distance. Keeping a one-tile buffer from the immutable
    // military track prevents the road from winding around it.
    constexpr int kStartingVillageGap = 20;
    constexpr int kMinStartingVillageRoadTiles = 20;
    constexpr int kMaxStartingVillageRoadTiles = 30;
    constexpr int kStartingVillageMilitaryRoadClearance = 1;

    // B5 (docs/work_plan_2026-07-13.md): deterministic reseed for a
    // regeneration retry — same inputs always produce the same sequence of
    // attempts, so a retried world is still fully reproducible from the
    // original seed.
    unsigned int PerturbSeedForRetry(unsigned int seed, int attempt)
    {
        return seed ^ (0xB5297A4Du * (static_cast<unsigned int>(attempt) + 1));
    }

    // Validates that the military road ring connects every player exactly
    // once in a single cycle — mirrors the check
    // tests/MilitaryRoadNetworkTests.cpp already exercises for
    // MilitaryRoadNetwork::Generate's output. Used to decide whether a
    // generation attempt needs to be retried (B5).
    bool ValidateMilitaryRing(const MilitaryRoadNetwork& roads, int playerCount)
    {
        if (playerCount < 2)
            return true;
        // A 2-player "ring" is a single mutual edge, not a closed cycle — one
        // route, not `playerCount` (bug found 2026-07-14: this off-by-one
        // made validation fail on EVERY attempt for every 2-player world,
        // silently wasting every retry and falling through to "proceeding
        // with the last attempt" every single time. The actual generated
        // rings were fine throughout — MilitaryRoadNetworkTests.
        // TwoPlayersGetExactlyOneMutualRoute already asserts exactly 1 route
        // for 2 players — so no test caught it; only the retry log spam did.
        size_t expectedRoutes = playerCount == 2 ? 1u : static_cast<size_t>(playerCount);
        if (roads.GetRoutes().size() != expectedRoutes)
            return false;

        std::set<int> visited;
        int current = 0;
        int previous = -1;
        for (int step = 0; step < playerCount; step++)
        {
            visited.insert(current);
            std::vector<int> neighbors = roads.GetNeighbors(current);
            if (neighbors.size() != (playerCount == 2 ? 1u : 2u))
                return false;

            int next = -1;
            for (int n : neighbors)
                if (n != previous) { next = n; break; }
            if (next == -1)
                next = neighbors.front();
            previous = current;
            current = next;
        }
        return visited.size() == static_cast<size_t>(playerCount);
    }

    // Keep the two gates of a multi-route HQ on meaningfully separated faces.
    // This is part of the military-track contract too; without it, a village
    // validation retry could accidentally accept a seed whose ring has two
    // gates collapsing near the same corner.
    bool ValidateMilitaryGateSpread(const MilitaryRoadNetwork& roads,
                                    const TileMap& tilemap, int playerCount,
                                    Vec2i hqFootprint)
    {
        const int minSeparation = hqFootprint.x - 1;
        for (int playerId = 0; playerId < playerCount; playerId++)
        {
            std::vector<Vec2i> gates;
            for (const MilitaryRoute& route : roads.GetRoutes())
            {
                int gateTile = -1;
                if (route.playerA == playerId && !route.tiles.empty())
                    gateTile = route.tiles.front();
                else if (route.playerB == playerId && !route.tiles.empty())
                    gateTile = route.tiles.back();
                if (gateTile >= 0)
                    gates.push_back(tilemap.GetCoordsFromId(gateTile));
            }

            if (gates.size() < 2)
                continue;
            if (gates.size() != 2)
                return false;

            const int chebyshev = std::max(
                std::abs(gates[0].x - gates[1].x),
                std::abs(gates[0].y - gates[1].y));
            if (chebyshev < minSeparation)
                return false;
        }
        return true;
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

    Vec2i DirectionFromHqToTile(Vec2i hqAnchor, Vec2i hqFootprint, Vec2i tile)
    {
        if (tile.x < hqAnchor.x)
            return {-1, 0};
        if (tile.x >= hqAnchor.x + hqFootprint.x)
            return {1, 0};
        if (tile.y < hqAnchor.y)
            return {0, -1};
        if (tile.y >= hqAnchor.y + hqFootprint.y)
            return {0, 1};
        return {};
    }

    std::vector<Vec2i> GetMilitaryExitDirections(const MilitaryRoadNetwork& roads,
                                                 const TileMap& tilemap,
                                                 int playerId,
                                                 Vec2i hqAnchor,
                                                 Vec2i hqFootprint)
    {
        std::vector<Vec2i> directions;
        for (const MilitaryRoute& route : roads.GetRoutes())
        {
            int gateTile = -1;
            if (route.playerA == playerId && !route.tiles.empty())
                gateTile = route.tiles.front();
            else if (route.playerB == playerId && !route.tiles.empty())
                gateTile = route.tiles.back();

            if (gateTile < 0)
                continue;

            Vec2i direction = DirectionFromHqToTile(
                hqAnchor, hqFootprint, tilemap.GetCoordsFromId(gateTile));
            if (direction.x != 0 || direction.y != 0)
                directions.push_back(direction);
        }
        return directions;
    }

    int CountMatchingDirections(Vec2i candidateDirection,
                                const std::vector<Vec2i>& exitDirections,
                                bool opposite)
    {
        int matches = 0;
        for (Vec2i exitDirection : exitDirections)
        {
            if (opposite)
                exitDirection = {-exitDirection.x, -exitDirection.y};
            if (candidateDirection == exitDirection)
                matches++;
        }
        return matches;
    }

    struct StartingVillagePlan
    {
        bool haveBuildable{false};
        Vec2i bestBuildableAnchor{};
        bool bestBuildableDetached{false};
        int bestBuildableExitMatches{std::numeric_limits<int>::max()};
        int bestBuildableOppositeMatches{-1};

        bool haveRoutable{false};
        Vec2i bestRoutableAnchor{};
        int bestRoadClearance{0};
        std::size_t bestPathLength{std::numeric_limits<std::size_t>::max()};
        bool bestWithinBudget{false};
        int bestExitMatches{std::numeric_limits<int>::max()};
        int bestOppositeMatches{-1};

        bool IsValid() const
        {
            return haveRoutable && bestWithinBudget &&
                   bestPathLength >= static_cast<std::size_t>(kMinStartingVillageRoadTiles) &&
                   bestPathLength <= static_cast<std::size_t>(kMaxStartingVillageRoadTiles) &&
                   bestRoadClearance >= kStartingVillageMilitaryRoadClearance;
        }
    };

    struct StartingVillagePathField
    {
        std::vector<int> parent;
        std::vector<int> distance;
    };

    // Builds one reusable multi-source BFS field from the HQ perimeter. Every
    // village candidate can then inspect its own perimeter in O(perimeter)
    // time and reconstruct only the winning path. This keeps world-generation
    // cost proportional to players instead of running a map-sized BFS for
    // every one of the 20 candidate anchors.
    StartingVillagePathField BuildStartingVillagePathField(
        TileMap& tilemap, Player* player, Vec2i hqAnchor, Vec2i hqFootprint,
        int militaryRoadClearance)
    {
        const int tileCount = tilemap.params.sizeX * tilemap.params.sizeY;
        StartingVillagePathField field{
            std::vector<int>(tileCount, -1),
            std::vector<int>(tileCount, -1)};
        const auto& roadDefinition = GetBuildingDefinition(BuildingType::Road);
        auto passable = [&](int tileId)
        {
            Vec2i pos = tilemap.GetCoordsFromId(tileId);
            return tilemap.CanBuildFootprint(pos, roadDefinition.footprint, player,
                                             BuildingType::Road) &&
                   HasMilitaryRoadClearance(tilemap, pos, roadDefinition.footprint,
                                             militaryRoadClearance);
        };

        std::queue<int> frontier;
        for (int tileId : tilemap.GetAdjacentTileIds(hqAnchor, hqFootprint))
        {
            if (tileId < 0 || tileId >= tileCount || field.distance[tileId] >= 0 ||
                !passable(tileId))
                continue;
            field.distance[tileId] = 0;
            frontier.push(tileId);
        }

        while (!frontier.empty())
        {
            const int current = frontier.front();
            frontier.pop();
            const Vec2i pos = tilemap.GetCoordsFromId(current);
            const std::array<Vec2i, 4> neighbours{
                Vec2i{pos.x + 1, pos.y}, Vec2i{pos.x - 1, pos.y},
                Vec2i{pos.x, pos.y + 1}, Vec2i{pos.x, pos.y - 1}};
            for (Vec2i next : neighbours)
            {
                if (!tilemap.IsInside(next))
                    continue;
                const int nextId = tilemap.GetIdFromCoords(next);
                if (field.distance[nextId] >= 0 || !passable(nextId))
                    continue;
                field.parent[nextId] = current;
                field.distance[nextId] = field.distance[current] + 1;
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
            if (tileId < 0 || tileId >= static_cast<int>(field.distance.size()) ||
                field.distance[tileId] < 0)
                continue;
            if (field.distance[tileId] < bestDistance)
            {
                reached = tileId;
                bestDistance = field.distance[tileId];
            }
        }
        if (reached < 0)
            return {};

        std::vector<int> path;
        for (int cursor = reached; cursor >= 0; cursor = field.parent[cursor])
            path.push_back(cursor);
        return path;
    }

    // Chooses the same plan during map-generation preflight and during actual
    // placement. The complete 4x5 candidate set is small; enumerating it
    // avoids proving one random offset safe and then choosing another one.
    StartingVillagePlan FindStartingVillagePlan(TileMap& tilemap,
                                                const MilitaryRoadNetwork& militaryRoads,
                                                Player* player, int playerId,
                                                Vec2i hqAnchor)
    {
        StartingVillagePlan plan;
        const Vec2i hqFootprint = MapGenerator::HeadquartersFootprint();
        Village villagePreview{0};
        const Vec2i villageFootprint = villagePreview.GetFootprint();
        const std::vector<Vec2i> exitDirections = GetMilitaryExitDirections(
            militaryRoads, tilemap, playerId, hqAnchor, hqFootprint);
        const StartingVillagePathField pathField = BuildStartingVillagePathField(
            tilemap, player, hqAnchor, hqFootprint,
            kStartingVillageMilitaryRoadClearance);

        struct VillageCandidate
        {
            Vec2i anchor{};
            Vec2i direction{};
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
                             hqAnchor.y + offset}, {-1, 0}});
                        break;
                    case 1:
                        candidates.push_back({
                            {hqAnchor.x + hqFootprint.x + kStartingVillageGap,
                             hqAnchor.y + offset}, {1, 0}});
                        break;
                    case 2:
                        candidates.push_back({
                            {hqAnchor.x + offset,
                             hqAnchor.y - kStartingVillageGap - villageFootprint.y}, {0, -1}});
                        break;
                    default:
                        candidates.push_back({
                            {hqAnchor.x + offset,
                             hqAnchor.y + hqFootprint.y + kStartingVillageGap}, {0, 1}});
                        break;
                }
            }
        }

        for (VillageCandidate candidate : candidates)
        {
            candidate.anchor = ClampAnchor(candidate.anchor, villageFootprint, tilemap.params);
            if (!tilemap.CanBuildFootprint(candidate.anchor, villageFootprint, player,
                                           BuildingType::Village))
                continue;

            const int exitMatches = CountMatchingDirections(candidate.direction, exitDirections, false);
            const int oppositeMatches = CountMatchingDirections(candidate.direction, exitDirections, true);
            const bool footprintDetached = HasMilitaryRoadClearance(
                tilemap, candidate.anchor, villageFootprint,
                kStartingVillageMilitaryRoadClearance);
            const bool betterBuildable =
                !plan.haveBuildable ||
                (footprintDetached != plan.bestBuildableDetached && footprintDetached) ||
                (footprintDetached == plan.bestBuildableDetached &&
                 exitMatches != plan.bestBuildableExitMatches &&
                 exitMatches < plan.bestBuildableExitMatches) ||
                (footprintDetached == plan.bestBuildableDetached &&
                 exitMatches == plan.bestBuildableExitMatches &&
                 oppositeMatches > plan.bestBuildableOppositeMatches);
            if (betterBuildable)
            {
                plan.haveBuildable = true;
                plan.bestBuildableAnchor = candidate.anchor;
                plan.bestBuildableDetached = footprintDetached;
                plan.bestBuildableExitMatches = exitMatches;
                plan.bestBuildableOppositeMatches = oppositeMatches;
            }

            // Only a route with a one-tile military-track buffer is accepted.
            // If it does not fit the 20..30 tile budget, the whole map attempt
            // is rejected and GenerateWorldLayout regenerates the world.
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
                 exitMatches != plan.bestExitMatches && exitMatches < plan.bestExitMatches) ||
                (withinBudget == plan.bestWithinBudget &&
                 exitMatches == plan.bestExitMatches &&
                 oppositeMatches != plan.bestOppositeMatches &&
                 oppositeMatches > plan.bestOppositeMatches) ||
                (withinBudget == plan.bestWithinBudget &&
                 exitMatches == plan.bestExitMatches &&
                 oppositeMatches == plan.bestOppositeMatches &&
                 path.size() < plan.bestPathLength);
            if (betterRoutable)
            {
                plan.haveRoutable = true;
                plan.bestRoutableAnchor = candidate.anchor;
                plan.bestRoadClearance = kStartingVillageMilitaryRoadClearance;
                plan.bestPathLength = path.size();
                plan.bestWithinBudget = withinBudget;
                plan.bestExitMatches = exitMatches;
                plan.bestOppositeMatches = oppositeMatches;
            }
        }

        return plan;
    }

    bool ValidateStartingVillagePlans(TileMap& tilemap,
                                      const MilitaryRoadNetwork& militaryRoads,
                                      const std::vector<Vec2i>& hqAnchors,
                                      int playerCount)
    {
        if (playerCount <= 0 || hqAnchors.size() != static_cast<size_t>(playerCount))
            return false;

        for (int playerId = 0; playerId < playerCount; playerId++)
        {
            StartingVillagePlan plan = FindStartingVillagePlan(
                tilemap, militaryRoads, nullptr, playerId, hqAnchors[playerId]);
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

    // Difficulty is an init-only starting advantage. All levels run the same
    // decision model; the profile controls only resources and manpower. The
    // grant is params-driven and therefore identical on host/client mirrors
    // (lockstep-safe) and requires no save-format change.
    void GrantDifficultyStartingBonus(Player* aiPlayer, int aiDifficulty)
    {
        if (aiPlayer == nullptr)
            return;
        const AIDifficultyProfile& profile = GetAIDifficultyProfile(aiDifficulty);

        auto grantResource = [](StorageComponent* storage, ResourceType type, int amount)
        {
            if (amount <= 0)
                return;
            auto& buffer = storage->buffers[type];
            if (buffer.type == ResourceType::Null)
                buffer = ResourceBuffer{type, amount};
            buffer.bufferSize = std::max(buffer.bufferSize, static_cast<int>(buffer.buffer.size()) + amount);
            for (int i = 0; i < amount; i++)
                buffer.GenerateResource(type);
        };

        if (!profile.startingResources.empty())
        {
            for (auto* building : aiPlayer->GetTrackedBuildingsWithComponent<StorageComponent>())
            {
                auto* storage = building != nullptr ? building->GetComponent<StorageComponent>() : nullptr;
                if (storage == nullptr || building->buildingType != BuildingType::Headquarters)
                    continue;

                for (const AIStartingResourceGrant& grant : profile.startingResources)
                    grantResource(storage, grant.resource, grant.amount);
                break;  // a player owns at most one HQ
            }
        }

        double manpowerGift = aiPlayer->GetPopulationCap() * profile.manpowerCapFraction;
        if (manpowerGift > 0.0)
            aiPlayer->AddManpower(manpowerGift);
    }

}

// B5 (docs/work_plan_2026-07-13.md): see the declaration comment in
// GameWorld.h for the retry rationale (safe because nothing player-visible
// exists yet at this point in InitWorld/InitMultiplayerWorld).
GameWorld::WorldLayoutResult GameWorld::GenerateWorldLayout(MapParameters& params, int playerCount)
{
    constexpr int kMaxAttempts = 32;
    WorldLayoutResult result;
    result.hqFootprint = MapGenerator::HeadquartersFootprint();
    result.requestedSeed = params.seed;
    result.finalSeed = params.seed;

    if (playerCount <= 0)
    {
        result.failureReason = "invalid player count";
        return result;
    }

    const unsigned int baseSeed = params.seed;

    auto validAnchors = [&](const std::vector<Vec2i>& anchors)
    {
        if (anchors.size() != static_cast<size_t>(playerCount))
            return false;
        for (size_t i = 0; i < anchors.size(); i++)
        {
            if (!tilemap.IsInsideFootprint(anchors[i], result.hqFootprint))
                return false;
            for (size_t j = 0; j < i; j++)
            {
                if (anchors[i] == anchors[j])
                    return false;
            }
        }
        return true;
    };

    for (int attempt = 0; attempt < kMaxAttempts; attempt++)
    {
        params.seed = attempt == 0 ? baseSeed : PerturbSeedForRetry(baseSeed, attempt);
        result.finalSeed = params.seed;
        result.attempts = attempt + 1;

        tilemap.generator.GenerateTileMap(tilemap, params);
        const std::vector<Vec2i> anchors = MapGenerator::PickHeadquartersAnchors(params, playerCount);
        if (!validAnchors(anchors))
        {
            result.failureReason = "headquarters anchors are incomplete or overlap";
            Log::Msg("[MapGenerator]", "invalid HQ anchors on attempt ", attempt,
                     " (seed ", params.seed, "), retrying");
            continue;
        }

        std::map<int, Vec2i> hqAnchorsByPlayer;
        for (int playerId = 0; playerId < playerCount; playerId++)
            hqAnchorsByPlayer[playerId] = anchors[playerId];

        militaryRoads = MilitaryRoadNetwork{};
        // Keep the route tie-break seed stable across village-layout retries.
        // The terrain and HQ layout still change with params.seed, while a
        // retry cannot introduce an unrelated gate-collapse variant merely
        // because the village check asked for another map attempt.
        militaryRoads.Generate(tilemap, hqAnchorsByPlayer, result.hqFootprint,
                               MapGenerator::HeadquartersTerritorySize(), baseSeed);

        if (ValidateMilitaryRing(militaryRoads, playerCount) &&
            ValidateMilitaryGateSpread(militaryRoads, tilemap, playerCount, result.hqFootprint) &&
            ValidateStartingVillagePlans(tilemap, militaryRoads, anchors, playerCount))
        {
            if (attempt > 0)
                Log::Msg("[MapGenerator]", "world generation succeeded on retry attempt ", attempt,
                          " (seed ", params.seed, ")");
            result.success = true;
            result.anchors = anchors;
            return result;
        }

        Log::Msg("[MapGenerator]", "world generation validation failed on attempt ", attempt,
                   " (seed ", params.seed, "), retrying");
        result.failureReason = "military ring or starting village validation failed";
    }

    Log::Msg("[MapGenerator]", "world generation failed after ", kMaxAttempts,
              " attempts (requested seed ", result.requestedSeed,
              ", last seed ", result.finalSeed, ")");
    tilemap.tilemap.clear();
    tilemap.terrainDirty = true;
    tilemap.buildingsDirty = true;
    militaryRoads = MilitaryRoadNetwork{};
    return result;
}

// Creates and registers the requested runtime object.
Player* GameWorld::CreatePlayer(int id, PlayerControllerType controllerType, const std::string& name, Color color)
{
    auto player = std::make_unique<Player>(id, tilemap);
    player->name = name;
    player->controllerType = controllerType;
    player->color = color;

    Player* ptr = player.get();
    playerHandler.players[id] = std::move(player);
    AttachControllerForPlayer(ptr);
    return ptr;
}

// Creates and registers the requested runtime object.
Vec2i GameWorld::CreateStartingHq(Player* player, Vec2i hqAnchor, unsigned int seed)
{
    if (player == nullptr)
        return hqAnchor;

    Vec2i hqFootprint = MapGenerator::HeadquartersFootprint();
    hqAnchor = ClampAnchor(hqAnchor, hqFootprint, tilemap.params);
    std::mt19937 resourceRng(seed ^ 0xC2B2AE35u);
    MapGenerator::PrepareStartingArea(tilemap, hqAnchor, resourceRng);
    SetFootprintTerrain(tilemap, hqAnchor, hqFootprint, TileType::GRASS, resourceRng, 3);

    player->Build<Headquarters>(hqAnchor, false);
    return hqAnchor;
}

// Creates and registers the requested runtime object.
void GameWorld::CreateStartingVillageAndResources(Player* player, Vec2i hqAnchor, unsigned int seed)
{
    if (player == nullptr)
        return;

    Vec2i hqFootprint = MapGenerator::HeadquartersFootprint();
    hqAnchor = ClampAnchor(hqAnchor, hqFootprint, tilemap.params);
    std::mt19937 resourceRng(seed ^ 0xC2B2AE35u);

    Village villagePreview{0};
    Vec2i villageFootprint = villagePreview.GetFootprint();
    // The village sits 20 tiles beyond the HQ footprint. The actual route is
    // measured below, because a military-track detour must not be hidden by a
    // harmless-looking straight-line distance.
    const int gap = kStartingVillageGap;

    // Measure each candidate's real road length up front via
    // FindRoadPathBetweenFootprints. No route is committed until the actual
    // road length and the military-track
    // clearance have both passed the preflight check.
    // The budget counts only actual Road tiles (perimeter-to-perimeter, not
    // the HQ/Village footprints themselves) — matches CountOwnedBuildings(Road).
    constexpr int kMaxVillageRoadTiles = kMaxStartingVillageRoadTiles;

    struct VillageCandidate
    {
        Vec2i anchor{};
        Vec2i direction{};
    };

    std::vector<VillageCandidate> villageCandidates;
    auto addCandidate = [&](int side, int offset)
    {
        switch (side)
        {
            case 0:
                villageCandidates.push_back({
                    {hqAnchor.x - gap - villageFootprint.x, hqAnchor.y + offset}, {-1, 0}});
                break;
            case 1:
                villageCandidates.push_back({
                    {hqAnchor.x + hqFootprint.x + gap, hqAnchor.y + offset}, {1, 0}});
                break;
            case 2:
                villageCandidates.push_back({
                    {hqAnchor.x + offset, hqAnchor.y - gap - villageFootprint.y}, {0, -1}});
                break;
            default:
                villageCandidates.push_back({
                    {hqAnchor.x + offset, hqAnchor.y + hqFootprint.y + gap}, {0, 1}});
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
    int bestBuildableExitMatches = std::numeric_limits<int>::max();
    int bestBuildableOppositeMatches = -1;
    bool bestBuildableDetached = false;
    bool haveRoutable = false;
    Vec2i bestRoutableAnchor{};
    int bestRoadClearance = 0;
    std::size_t bestPathLength = std::numeric_limits<std::size_t>::max();
    bool bestWithinBudget = false;
    bool bestDetached = false;
    int bestExitMatches = std::numeric_limits<int>::max();
    int bestOppositeMatches = -1;
    const std::vector<Vec2i> exitDirections = GetMilitaryExitDirections(
        militaryRoads, tilemap, player->id, hqAnchor, hqFootprint);

    for (VillageCandidate candidate : villageCandidates)
    {
        candidate.anchor = ClampAnchor(candidate.anchor, villageFootprint, tilemap.params);
        if (!tilemap.CanBuildFootprint(candidate.anchor, villageFootprint, player))
            continue;

        const int exitMatches = CountMatchingDirections(candidate.direction, exitDirections, false);
        const int oppositeMatches = CountMatchingDirections(candidate.direction, exitDirections, true);
        const bool footprintDetached = HasMilitaryRoadClearance(
            tilemap, candidate.anchor, villageFootprint, 1);
        const bool betterBuildable =
            !haveBuildable ||
            (footprintDetached != bestBuildableDetached && footprintDetached) ||
            (footprintDetached == bestBuildableDetached &&
             exitMatches != bestBuildableExitMatches && exitMatches < bestBuildableExitMatches) ||
            (footprintDetached == bestBuildableDetached &&
             exitMatches == bestBuildableExitMatches &&
             oppositeMatches > bestBuildableOppositeMatches);
        if (betterBuildable)
        {
            haveBuildable = true;
            bestBuildableAnchor = candidate.anchor;
            bestBuildableDetached = footprintDetached;
            bestBuildableExitMatches = exitMatches;
            bestBuildableOppositeMatches = oppositeMatches;
        }

        std::vector<int> path;
        bool detached = false;
        if (footprintDetached)
        {
            path = FindRoadPathBetweenFootprints(
                tilemap, player, candidate.anchor, villageFootprint,
                hqAnchor, hqFootprint, 1);
            detached = !path.empty();
        }
        if (path.empty())
            continue;

        const bool withinBudget =
            path.size() >= static_cast<std::size_t>(kMinStartingVillageRoadTiles) &&
            path.size() <= static_cast<std::size_t>(kMaxVillageRoadTiles);
        const bool betterRoutable =
            !haveRoutable ||
            (detached != bestDetached && detached) ||
            (detached == bestDetached && withinBudget != bestWithinBudget && withinBudget) ||
            (detached == bestDetached && withinBudget == bestWithinBudget &&
             exitMatches != bestExitMatches && exitMatches < bestExitMatches) ||
            (detached == bestDetached && withinBudget == bestWithinBudget &&
             exitMatches == bestExitMatches && oppositeMatches != bestOppositeMatches &&
             oppositeMatches > bestOppositeMatches) ||
            (detached == bestDetached && withinBudget == bestWithinBudget &&
             exitMatches == bestExitMatches && oppositeMatches == bestOppositeMatches &&
             path.size() < bestPathLength);
        if (betterRoutable)
        {
            haveRoutable = true;
            bestPathLength = path.size();
            bestRoutableAnchor = candidate.anchor;
            bestRoadClearance = detached ? 1 : 0;
            bestWithinBudget = withinBudget;
            bestDetached = detached;
            bestExitMatches = exitMatches;
            bestOppositeMatches = oppositeMatches;
        }
    }

    if (!haveBuildable || !haveRoutable || !bestWithinBudget ||
        bestPathLength < static_cast<std::size_t>(kMinStartingVillageRoadTiles))
    {
        Log::Msg("[MapGenerator]", "Starting village placement rejected: no detached road in range ",
                 kMinStartingVillageRoadTiles, "..", kMaxVillageRoadTiles);
        return;
    }
    Vec2i villageAnchor = bestRoutableAnchor;
    SetFootprintTerrain(tilemap, villageAnchor, villageFootprint, TileType::GRASS, resourceRng, 3);
    Building* village = player->Build<Village>(villageAnchor, false);

    if (village != nullptr)
        BuildStartRoad(player, villageAnchor, villageFootprint, hqAnchor, hqFootprint,
                       bestRoadClearance);

    // WOOD/STONE stay on the original ring (17..23); COAL/IRON_ORE (user
    // request 2026-07-19: iron is often missing near spawn) sit on a wider
    // ring (26..32) so all four patches fit around the HQ without collisions
    // — four spread directions, one per patch.
    PlaceStartingResourcePatch(tilemap, hqAnchor, hqFootprint, villageAnchor, villageFootprint,
                               TileType::WOOD, resourceRng, 17, 23, Vec2i{-1, 0});
    PlaceStartingResourcePatch(tilemap, hqAnchor, hqFootprint, villageAnchor, villageFootprint,
                               TileType::STONE, resourceRng, 17, 23, Vec2i{1, 0});
    PlaceStartingResourcePatch(tilemap, hqAnchor, hqFootprint, villageAnchor, villageFootprint,
                               TileType::COAL, resourceRng, 26, 32, Vec2i{0, -1});
    PlaceStartingResourcePatch(tilemap, hqAnchor, hqFootprint, villageAnchor, villageFootprint,
                               TileType::IRON_ORE, resourceRng, 26, 32, Vec2i{0, 1});
}

// Initializes runtime state for this object.
//
// Generation order (fixed 2026-07-13 — user report: military road sometimes
// cut through the starting village or its resource-road network):
//   1. Terrain, HQ anchors (B1) and the military road ring (B2) are
//      generated together by GenerateWorldLayout, retrying on a perturbed
//      seed if the ring or starting-village layout fails validation (B5) —
//      entirely before any player
//      or building exists, so a retry never needs to undo anything.
//   2. Every player is created and its Headquarters is placed
//      (CreateStartingHq) at its now-final validated anchor.
//   3. Only THEN is each player's village, start road and starting resource
//      patches placed (CreateStartingVillageAndResources) —
//      TileMap::CanBuildFootprint already refuses any tile with
//      isMilitaryRoad set, so this step automatically steers clear of the
//      road with no extra bookkeeping.
// Previously the military road was generated LAST, after everything
// (including village + resources) was already built, so its pathfinder
// could only route around bases by treating each one as a big blocked
// rectangle — a real box, but the fallback above could still land on a
// village or a resource-patch access road placed near that rectangle's edge.
bool GameWorld::InitWorld(std::string name, Renderer* r, AudioSystem* a, MapParameters params)
{
    initialized = false;
    initializationError.clear();
    combatTelemetry.Clear();
    worldName = name;
    render = r;
    audio  = a;

    int opponentCount = std::clamp(params.aiOpponentCount, 0, 5);
    int playerCount = opponentCount + 1;
    // B1 (docs/work_plan_2026-07-13.md): every HQ — including the human
    // player's — sits on the same deterministic n-gon; no player is
    // special-cased to the exact map center anymore.
    WorldLayoutResult layout = GenerateWorldLayout(params, playerCount);
    if (!layout.success)
    {
        initializationError = "World generation failed (seed " +
            std::to_string(layout.requestedSeed) + ", last attempt seed " +
            std::to_string(layout.finalSeed) + "): " +
            (layout.failureReason.empty() ? "no valid layout" : layout.failureReason);
        Log::Msg("[MapGenerator]", initializationError);
        return false;
    }
    const std::vector<Vec2i>& anchors = layout.anchors;
    const Vec2i hqFootprint = layout.hqFootprint;

    localPlayerId = 0;
    auto* human = CreatePlayer(0, PlayerControllerType::LocalHuman, "Player", PlayerSlotColor(0));

    std::map<int, Vec2i> hqAnchorsByPlayer{{0, anchors[0]}};
    std::map<int, Player*> playersById{{0, human}};
    std::map<int, unsigned int> baseSeedByPlayer{{0, params.seed ^ 0x9E3779B9u}};

    for (int i = 0; i < opponentCount; i++)
    {
        int playerId = i + 1;
        auto* enemy = CreatePlayer(playerId, PlayerControllerType::AI, "AI Opponent " + std::to_string(playerId), PlayerSlotColor(playerId));
        hqAnchorsByPlayer[playerId] = anchors[playerId];
        playersById[playerId] = enemy;
        baseSeedByPlayer[playerId] = params.seed ^ (0x85EBCA6Bu + static_cast<unsigned int>(i * 104729));
    }

    // Military road ring already generated (and validated/retried, B5) by
    // GenerateWorldLayout above, before any of these players/HQs existed.
    for (const auto& [playerId, anchor] : hqAnchorsByPlayer)
        CreateStartingHq(playersById.at(playerId), anchor, baseSeedByPlayer.at(playerId));

    for (const auto& [playerId, anchor] : hqAnchorsByPlayer)
    {
        Player* p = playersById.at(playerId);
        CreateStartingVillageAndResources(p, anchor, baseSeedByPlayer.at(playerId));
        // Preserves the original (asymmetric) debug behavior: only the human
        // player gets a resource/manpower grant here, AI opponents only get
        // the debugMode flag — matches pre-reorder InitWorld exactly.
        if (params.debugMode)
        {
            p->debugMode = true;
            if (playerId == 0)
            {
                GrantDebugResourcesToHeadquarters(p, 50);
                GrantDebugManpower(p);
            }
        }
        // Keyed on the slot id, NOT controllerType — playerId 0 is always
        // the human here, and slot identity is what stays identical between
        // a host world and a client mirror.
        if (playerId != 0)
            GrantDifficultyStartingBonus(p, params.aiDifficulty);
    }

    if (render != nullptr)
    {
        render->camera.zoom = 1.75f;
        render->camera.rotation = 0.0f;
        Vec2f hqWorldCenter{
            static_cast<float>(anchors[0].x * TILE_SIZE) + hqFootprint.x * TILE_SIZE * 0.5f,
            static_cast<float>(anchors[0].y * TILE_SIZE) + hqFootprint.y * TILE_SIZE * 0.5f};
        render->CenterCameraOnWorld(hqWorldCenter, {tilemap.params.sizeX, tilemap.params.sizeY});
        cachedCameraTarget = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        cachedCameraZoom = -1.0f;
    }
    UpdateFogOfWar();
    initialized = true;
    return true;
}

// Initializes deterministic multiplayer runtime state with server-assigned slots.
bool GameWorld::InitMultiplayerWorld(std::string name, Renderer* r, AudioSystem* a, MapParameters params, int localId, bool authoritativeHost)
{
    initialized = false;
    initializationError.clear();
    combatTelemetry.Clear();
    worldName = name;
    render = r;
    audio  = a;

    localPlayerId = std::clamp(localId, 0, MultiplayerHumanSlots - 1);

    int opponentCount = std::clamp(params.aiOpponentCount, 0, 5);
    int playerCount = MultiplayerHumanSlots + opponentCount;
    // B1 (docs/work_plan_2026-07-13.md): every HQ sits on the same
    // deterministic n-gon, same as InitWorld. B5: terrain + anchors + the
    // military road ring are generated (and retried on validation failure)
    // together, before any player/building exists.
    WorldLayoutResult layout = GenerateWorldLayout(params, playerCount);
    if (!layout.success)
    {
        initializationError = "World generation failed (seed " +
            std::to_string(layout.requestedSeed) + ", last attempt seed " +
            std::to_string(layout.finalSeed) + "): " +
            (layout.failureReason.empty() ? "no valid layout" : layout.failureReason);
        Log::Msg("[MapGenerator]", initializationError);
        return false;
    }
    const std::vector<Vec2i>& anchors = layout.anchors;
    const Vec2i hqFootprint = layout.hqFootprint;

    std::map<int, Vec2i> hqAnchorsByPlayer;
    std::map<int, Player*> playersById;
    std::map<int, unsigned int> baseSeedByPlayer;
    Vec2i cameraAnchor = anchors[0];

    // Pass 1: create every player and pick every HQ anchor — no building yet
    // (see InitWorld's comment for why the military road must be generated
    // before any base is placed).
    for (int playerId = 0; playerId < MultiplayerHumanSlots; playerId++)
    {
        PlayerControllerType controllerType = PlayerControllerType::Remote;
        if (playerId == localPlayerId)
            controllerType = PlayerControllerType::LocalHuman;

        std::string playerName = playerId == 0 ? "Host" : "Client";
        auto* player = CreatePlayer(playerId, controllerType, playerName, PlayerSlotColor(playerId));
        Vec2i anchor = anchors[playerId];
        hqAnchorsByPlayer[playerId] = anchor;
        playersById[playerId] = player;
        baseSeedByPlayer[playerId] = params.seed ^ (0x9E3779B9u + static_cast<unsigned int>(playerId * 104729));
        if (playerId == localPlayerId)
            cameraAnchor = anchor;
    }

    for (int i = 0; i < opponentCount; i++)
    {
        int playerId = MultiplayerHumanSlots + i;
        PlayerControllerType controllerType = authoritativeHost ? PlayerControllerType::AI : PlayerControllerType::Remote;
        auto* enemy = CreatePlayer(playerId, controllerType, "AI Opponent " + std::to_string(i + 1), PlayerSlotColor(playerId));
        hqAnchorsByPlayer[playerId] = anchors[playerId];
        playersById[playerId] = enemy;
        baseSeedByPlayer[playerId] = params.seed ^ (0x85EBCA6Bu + static_cast<unsigned int>(i * 104729));
    }

    // Pass 2: place every Headquarters. Military road ring already generated
    // (and validated/retried, B5) by GenerateWorldLayout above.
    for (const auto& [playerId, anchor] : hqAnchorsByPlayer)
        CreateStartingHq(playersById.at(playerId), anchor, baseSeedByPlayer.at(playerId));

    // Pass 3: village + start road + resource patches — CanBuildFootprint
    // already refuses isMilitaryRoad tiles, so this automatically avoids the
    // road baked in above.
    for (const auto& [playerId, anchor] : hqAnchorsByPlayer)
        CreateStartingVillageAndResources(playersById.at(playerId), anchor, baseSeedByPlayer.at(playerId));

    // Keyed on the slot id, NOT controllerType — AI slots are Remote on a
    // client mirror, and both sides must build the identical starting state.
    for (const auto& [playerId, anchor] : hqAnchorsByPlayer)
        if (playerId >= MultiplayerHumanSlots)
            GrantDifficultyStartingBonus(playersById.at(playerId), params.aiDifficulty);

    if (params.debugMode)
    {
        for (auto& [id, player] : playerHandler.players)
        {
            if (player == nullptr) continue;
            player->debugMode = true;
            GrantDebugResourcesToHeadquarters(player.get(), 50);
            GrantDebugManpower(player.get());
        }
    }

    if (render != nullptr)
    {
        render->camera.zoom = 1.75f;
        render->camera.rotation = 0.0f;
        Vec2f hqWorldCenter{
            static_cast<float>(cameraAnchor.x * TILE_SIZE) + hqFootprint.x * TILE_SIZE * 0.5f,
            static_cast<float>(cameraAnchor.y * TILE_SIZE) + hqFootprint.y * TILE_SIZE * 0.5f};
        render->CenterCameraOnWorld(hqWorldCenter, {tilemap.params.sizeX, tilemap.params.sizeY});
        cachedCameraTarget = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        cachedCameraZoom = -1.0f;
    }
    UpdateFogOfWar();
    initialized = true;
    return true;
}

