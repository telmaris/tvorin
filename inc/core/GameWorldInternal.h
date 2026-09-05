#ifndef GAMEWORLD_INTERNAL_H
#define GAMEWORLD_INTERNAL_H

#include "core/GameWorld.h"
#include "core/Log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <random>
#include <set>

namespace GameWorldInternal
{
    // Creates a concrete building instance while loading save data.
    inline std::unique_ptr<Building> CreateBuildingFromType(BuildingType type, int id)
    {
        switch (type)
        {
            case BuildingType::Headquarters: return std::make_unique<Headquarters>(id);
            case BuildingType::Village: return std::make_unique<Village>(id);
            case BuildingType::StorageBuilding: return std::make_unique<StorageBuilding>(id);
            case BuildingType::Woodcutter: return std::make_unique<Woodcutter>(id);
            case BuildingType::HuntersHut: return std::make_unique<HuntersHut>(id);
            case BuildingType::LumberMill: return std::make_unique<LumberMill>(id);
            case BuildingType::Mine: return std::make_unique<Mine>(id);
            case BuildingType::Foundry: return std::make_unique<Foundry>(id);
            case BuildingType::Well: return std::make_unique<Well>(id);
            case BuildingType::WheatFarm: return std::make_unique<WheatFarm>(id);
            case BuildingType::Windmill: return std::make_unique<Windmill>(id);
            case BuildingType::Bakery: return std::make_unique<Bakery>(id);
            case BuildingType::Inn: return std::make_unique<Inn>(id);
            case BuildingType::Paperworks: return std::make_unique<Paperworks>(id);
            case BuildingType::Smith: return std::make_unique<Smith>(id);
            case BuildingType::Mint: return std::make_unique<Mint>(id);
            case BuildingType::Glassworks: return std::make_unique<Glassworks>(id);
            case BuildingType::Powderworks: return std::make_unique<Powderworks>(id);
            case BuildingType::University: return std::make_unique<University>(id);
            case BuildingType::Barracks: return std::make_unique<Barracks>(id);
            case BuildingType::GuardTower: return std::make_unique<GuardTower>(id);
            case BuildingType::Fortress: return std::make_unique<Fortress>(id);
            case BuildingType::Road: return std::make_unique<Road>(id);
            case BuildingType::AnimalFarm:
            case BuildingType::Butcher:
            case BuildingType::Tannery:
            case BuildingType::Tailor:
            case BuildingType::Armorer:
            case BuildingType::HorseStable:
            case BuildingType::Kiln:
            case BuildingType::HouseholdWorkshop:
            case BuildingType::Soapworks:
            case BuildingType::Inkworks:
            case BuildingType::Scriptorium:
            case BuildingType::Copperworks:
            case BuildingType::UrbanWorkshop:
            case BuildingType::HempFarm:
            case BuildingType::Ropery:
            case BuildingType::Weaver:
            case BuildingType::Bowyer:
            case BuildingType::SpearWorkshop:
            case BuildingType::SiegeWorkshop:
                return std::make_unique<ConfiguredProductionBuilding>(id, type);
            default: return nullptr;
        }
    }

    // Writes one resource buffer snapshot to a save stream.
    inline void SaveResourceBuffer(std::ostream& out, const char* tag, const ResourceBuffer& buffer)
    {
        out << tag << ' ' << static_cast<int>(buffer.type) << ' '
            << buffer.bufferSize << ' ' << buffer.buffer.size() << '\n';
    }

    // Restores one resource buffer from saved capacity and amount values.
    inline void LoadResourceBuffer(ResourceBuffer& buffer, ResourceType type, int capacity, int amount)
    {
        buffer.type = type;
        buffer.bufferSize = capacity;
        buffer.SetStoredAmount(amount);
    }

    // Deferred connection restored after all saved buildings are placed.
    struct PendingConnection
    {
        TileMap* map{nullptr};
        int sourcePosition{-1};
        ResourceType resource{ResourceType::Null};
        int targetPosition{-1};
        bool receiver{false};
        bool alternative{false};
    };

    // Returns the tile-space center of a footprint.
    inline Vec2i FootprintCenter(Vec2i anchor, Vec2i footprint)
    {
        return Vec2i{
            anchor.x + footprint.x / 2,
            anchor.y + footprint.y / 2};
    }

    inline Vec2i ClampAnchor(Vec2i anchor, Vec2i footprint, const MapParameters& params)
    {
        return Vec2i{
            std::clamp(anchor.x, 1, std::max(1, params.sizeX - footprint.x - 2)),
            std::clamp(anchor.y, 1, std::max(1, params.sizeY - footprint.y - 2))};
    }

    inline void SetFootprintTerrain(TileMap& tilemap, Vec2i anchor, Vec2i footprint, TileType type, std::mt19937& rng, int padding = 0)
    {
        for (int y = -padding; y < footprint.y + padding; y++)
        {
            for (int x = -padding; x < footprint.x + padding; x++)
            {
                Vec2i pos{anchor.x + x, anchor.y + y};
                if (!tilemap.IsInside(pos))
                    continue;

                auto& tile = tilemap[pos];
                tile.tileType = type;
                tile.terrainTextureId = tilemap.PickTerrainTexture(type, rng);
                tile.resourceOverlayTextureId = -1;
                tile.resourceRichness = type == TileType::GRASS ? 0 : std::max(1, tilemap.params.resourceRichness);
            }
        }
        tilemap.terrainDirty = true;
    }

    inline bool FootprintsOverlap(Vec2i a, Vec2i aSize, Vec2i b, Vec2i bSize, int padding = 0)
    {
        return a.x - padding < b.x + bSize.x + padding &&
               a.x + aSize.x + padding > b.x - padding &&
               a.y - padding < b.y + bSize.y + padding &&
               a.y + aSize.y + padding > b.y - padding;
    }

    // Builds a compact, rounded rectangle with a gently uneven outline.
    // The old radius-4 circle occupied 49 tiles. These masks contain 52-53
    // tiles (+6-8%), remain 4-connected, and may be rotated so the four
    // starting deposits do not all share the same silhouette.
    inline std::vector<Vec2i> BuildStartingResourcePatchOffsets(std::mt19937& rng)
    {
        constexpr std::array<int, 7> halfWidths{2, 3, 4, 4, 4, 3, 2};
        std::uniform_int_distribution<int> shiftDist(-1, 1);
        std::uniform_int_distribution<int> extraTileDist(1, 2);
        std::uniform_int_distribution<int> rotationDist(0, 1);

        const int upperShift = shiftDist(rng);
        const int lowerShift = shiftDist(rng);
        const bool rotate = rotationDist(rng) != 0;

        std::vector<Vec2i> offsets;
        offsets.reserve(53);
        for (int row = 0; row < static_cast<int>(halfWidths.size()); row++)
        {
            const int y = row - 3;
            const int rowShift = y < -1 ? upperShift : (y > 1 ? lowerShift : 0);
            for (int x = -halfWidths[row]; x <= halfWidths[row]; x++)
                offsets.push_back({x + rowShift, y});
        }

        // Add one or two small shoulder bulges. Restricting them away from
        // the top/bottom tips keeps the edge rounded instead of spiky.
        std::vector<Vec2i> shoulderCandidates;
        for (int row = 1; row <= 5; row++)
        {
            const int y = row - 3;
            const int rowShift = y < -1 ? upperShift : (y > 1 ? lowerShift : 0);
            shoulderCandidates.push_back({rowShift - halfWidths[row] - 1, y});
            shoulderCandidates.push_back({rowShift + halfWidths[row] + 1, y});
        }
        std::shuffle(shoulderCandidates.begin(), shoulderCandidates.end(), rng);
        const int extraTiles = extraTileDist(rng);
        offsets.insert(offsets.end(), shoulderCandidates.begin(),
                       shoulderCandidates.begin() + extraTiles);

        if (rotate)
        {
            for (Vec2i& offset : offsets)
                std::swap(offset.x, offset.y);
        }
        return offsets;
    }

    struct StartingResourcePatchPlan
    {
        TileType type{TileType::GRASS};
        Vec2i center{-1, -1};
        std::vector<Vec2i> offsets;
        Vec2i minOffset{};
        Vec2i maxOffset{};
        int minCenterDist{0};
        int maxCenterDist{0};
    };

    struct StartingResourceLayout
    {
        bool valid{false};
        std::array<StartingResourcePatchPlan, 4> patches{};
    };

    // Plans all four starting fields together. This keeps the randomized
    // centres collision-free and prevents one already-painted patch from
    // changing the answer for the next independent greedy search.
    inline StartingResourceLayout PlanStartingResourceLayout(
        const TileMap& tilemap, Vec2i hqAnchor, Vec2i hqFootprint,
        Vec2i villageAnchor, Vec2i villageFootprint, std::mt19937& rng)
    {
        StartingResourceLayout layout;
        const Vec2i hqCenter{hqAnchor.x + hqFootprint.x / 2,
                             hqAnchor.y + hqFootprint.y / 2};


        std::array<TileType, 4> resourceTypes{
            TileType::WOOD, TileType::STONE, TileType::COAL, TileType::IRON_ORE};
        std::shuffle(resourceTypes.begin(), resourceTypes.end(), rng);
        constexpr int kNearStartingResourceMinDistance = 20;
        constexpr int kNearStartingResourceMaxDistance = 26;
        constexpr int kFarStartingResourceMinDistance = 29;
        constexpr int kFarStartingResourceMaxDistance = 35;
        const std::array<int, 4> minDistances{
            kNearStartingResourceMinDistance, kNearStartingResourceMinDistance,
            kFarStartingResourceMinDistance, kFarStartingResourceMinDistance};
        const std::array<int, 4> maxDistances{
            kNearStartingResourceMaxDistance, kNearStartingResourceMaxDistance,
            kFarStartingResourceMaxDistance, kFarStartingResourceMaxDistance};
        for (int index = 0; index < 4; index++)
        {
            auto& patch = layout.patches[index];
            patch.type = resourceTypes[index];
            patch.minCenterDist = minDistances[index];
            patch.maxCenterDist = maxDistances[index];
            patch.offsets = BuildStartingResourcePatchOffsets(rng);
            patch.minOffset = patch.offsets.front();
            patch.maxOffset = patch.offsets.front();
            for (Vec2i offset : patch.offsets)
            {
                patch.minOffset.x = std::min(patch.minOffset.x, offset.x);
                patch.minOffset.y = std::min(patch.minOffset.y, offset.y);
                patch.maxOffset.x = std::max(patch.maxOffset.x, offset.x);
                patch.maxOffset.y = std::max(patch.maxOffset.y, offset.y);
            }
        }

        struct Candidate
        {
            Vec2i center{};
            Vec2i anchor{};
            Vec2i size{};
            int paintable{0};
            double angle{0.0};
        };
        std::array<std::vector<Candidate>, 4> candidates;
        for (int index = 0; index < 4; index++)
        {
            const auto& patch = layout.patches[index];
            const Vec2i patchSize{patch.maxOffset.x - patch.minOffset.x + 1,
                                  patch.maxOffset.y - patch.minOffset.y + 1};
            for (int y = -patch.maxCenterDist; y <= patch.maxCenterDist; y++)
            {
                for (int x = -patch.maxCenterDist; x <= patch.maxCenterDist; x++)
                {
                    const int distanceSquared = x * x + y * y;
                    if (distanceSquared < patch.minCenterDist * patch.minCenterDist ||
                        distanceSquared > patch.maxCenterDist * patch.maxCenterDist)
                        continue;

                    const Vec2i centre{hqCenter.x + x, hqCenter.y + y};
                    const Vec2i anchor{centre.x + patch.minOffset.x,
                                       centre.y + patch.minOffset.y};
                    if (!tilemap.IsInside(centre) || !tilemap.IsInsideFootprint(anchor, patchSize) ||
                        FootprintsOverlap(anchor, patchSize, hqAnchor, hqFootprint, 1) ||
                        FootprintsOverlap(anchor, patchSize, villageAnchor, villageFootprint, 1))
                        continue;

                    int paintable = 0;
                    bool validSide = true;
                    for (Vec2i offset : patch.offsets)
                    {
                        const Vec2i pos{centre.x + offset.x, centre.y + offset.y};
                        if (!tilemap.IsInside(pos))
                        {
                            validSide = false;
                            break;
                        }
                        const int tileId = tilemap.GetIdFromCoords(pos);
                        const Tile& tile = tilemap.tilemap[tileId];
                        if (tile.HasBuilding() ||
                            tile.tileType != TileType::GRASS)
                        {
                            validSide = false;
                            break;
                        }
                        paintable++;
                    }
                    if (!validSide || paintable == 0)
                        continue;

                    candidates[index].push_back({centre, anchor, patchSize, paintable,
                                                std::atan2(static_cast<double>(y), static_cast<double>(x))});
                }
            }
        }

        std::uniform_real_distribution<double> angleDistribution(
            -3.14159265358979323846, 3.14159265358979323846);
        constexpr double minAngularSeparation = 0.55;
        constexpr int maxTrials = 64;
        int bestScore = std::numeric_limits<int>::min();
        for (int trial = 0; trial < maxTrials; trial++)
        {
            std::array<double, 4> targetAngles{};
            for (int index = 0; index < 4; index++)
            {
                bool accepted = false;
                for (int attempt = 0; attempt < 16 && !accepted; attempt++)
                {
                    targetAngles[index] = angleDistribution(rng);
                    accepted = true;
                    for (int prior = 0; prior < index; prior++)
                    {
                        double difference = std::abs(targetAngles[index] - targetAngles[prior]);
                        difference = std::min(difference, 6.28318530717958647692 - difference);
                        if (difference < minAngularSeparation)
                        {
                            accepted = false;
                            break;
                        }
                    }
                }
            }

            std::array<int, 4> selected{};
            selected.fill(-1);
            bool complete = true;
            int totalScore = 0;
            for (int index = 0; index < 4 && complete; index++)
            {
                int selectedScore = std::numeric_limits<int>::min();
                for (int candidateIndex = 0;
                     candidateIndex < static_cast<int>(candidates[index].size()); candidateIndex++)
                {
                    const Candidate& candidate = candidates[index][candidateIndex];
                    bool overlaps = false;
                    for (int prior = 0; prior < index; prior++)
                    {
                        if (selected[prior] < 0)
                            continue;
                        const Candidate& previous = candidates[prior][selected[prior]];
                        overlaps = overlaps || FootprintsOverlap(
                            candidate.anchor, candidate.size, previous.anchor, previous.size, 1);
                    }
                    if (overlaps)
                        continue;

                    double angleDifference = std::abs(candidate.angle - targetAngles[index]);
                    angleDifference = std::min(angleDifference, 6.28318530717958647692 - angleDifference);
                    const int score = candidate.paintable * 1000 -
                                      static_cast<int>(angleDifference * 100.0);
                    if (score > selectedScore)
                    {
                        selectedScore = score;
                        selected[index] = candidateIndex;
                    }
                }
                if (selected[index] < 0)
                    complete = false;
                else
                    totalScore += selectedScore;
            }

            if (!complete || totalScore <= bestScore)
                continue;
            bestScore = totalScore;
            layout.valid = true;
            for (int index = 0; index < 4; index++)
                layout.patches[index].center = candidates[index][selected[index]].center;
        }

        return layout;
    }

    inline bool PlaceStartingResourceLayout(TileMap& tilemap, const StartingResourceLayout& layout,
                                            std::mt19937& rng)
    {
        if (!layout.valid)
            return false;

        for (const auto& patch : layout.patches)
        {
            int painted = 0;
            for (Vec2i offset : patch.offsets)
            {
                const Vec2i pos{patch.center.x + offset.x, patch.center.y + offset.y};
                if (!tilemap.IsInside(pos))
                    continue;
                Tile& tile = tilemap[pos];
                if (tile.HasBuilding() || tile.tileType != TileType::GRASS)
                    continue;

                tile.tileType = patch.type;
                const bool usesOverlay = tilemap.HasResourceOverlay(patch.type);
                tile.terrainTextureId = tilemap.PickTerrainTexture(
                    usesOverlay ? TileType::GRASS : patch.type, rng);
                tile.resourceOverlayTextureId = usesOverlay
                    ? tilemap.PickResourceOverlayTexture(patch.type,
                        ResourceOverlayEdgeDirection::None, rng)
                    : -1;
                tile.resourceRichness = std::max(1, tilemap.params.resourceRichness);
                painted++;
            }
            if (painted == 0)
                return false;
        }

        tilemap.terrainDirty = true;
        return true;
    }

    inline std::vector<int> FindRoadPathBetweenFootprints(TileMap& tilemap, Player* player,
        Vec2i fromAnchor, Vec2i fromFootprint, Vec2i toAnchor, Vec2i toFootprint)
    {
        const auto& roadDefinition = GetBuildingDefinition(BuildingType::Road);
        auto passable = [&](int tileId)
        {
            Vec2i pos = tilemap.GetCoordsFromId(tileId);
            return tilemap.CanBuildFootprint(pos, roadDefinition.footprint, player, BuildingType::Road);
        };

        std::vector<int> fromAdjacent = tilemap.GetAdjacentTileIds(fromAnchor, fromFootprint);
        std::vector<int> toAdjacent = tilemap.GetAdjacentTileIds(toAnchor, toFootprint);
        if (fromAdjacent.empty() || toAdjacent.empty())
            return {};

        std::set<int> goalSet(toAdjacent.begin(), toAdjacent.end());

        // Unweighted 4-directional BFS — deterministic given fixed map
        // state (no RNG involved in world-gen pathing).
        int maxIndex = tilemap.params.sizeX * tilemap.params.sizeY;
        std::vector<int> parent(maxIndex, -1);
        std::vector<bool> visited(maxIndex, false);
        std::queue<int> frontier;
        for (int tileId : fromAdjacent)
        {
            if (visited[tileId] || !passable(tileId))
                continue;
            visited[tileId] = true;
            frontier.push(tileId);
        }

        int reached = -1;
        while (!frontier.empty())
        {
            int current = frontier.front();
            frontier.pop();
            if (goalSet.contains(current))
            {
                reached = current;
                break;
            }

            Vec2i pos = tilemap.GetCoordsFromId(current);
            const std::array<Vec2i, 4> neighbours{
                Vec2i{pos.x + 1, pos.y}, Vec2i{pos.x - 1, pos.y},
                Vec2i{pos.x, pos.y + 1}, Vec2i{pos.x, pos.y - 1}
            };
            for (Vec2i next : neighbours)
            {
                if (!tilemap.IsInside(next))
                    continue;
                int nextId = tilemap.GetIdFromCoords(next);
                if (visited[nextId] || !passable(nextId))
                    continue;
                visited[nextId] = true;
                parent[nextId] = current;
                frontier.push(nextId);
            }
        }

        // No route exists between any pair of perimeter tiles, which means
        // the building is fully boxed in by terrain or existing structures.
        if (reached < 0)
            return {};

        std::vector<int> path;
        for (int cursor = reached; cursor >= 0; cursor = parent[cursor])
            path.push_back(cursor);
        std::reverse(path.begin(), path.end());
        return path;
    }

    // Builds an orthogonal road between two starting buildings using the same
    // multi-source, multi-target BFS used by generation preflight.
    inline void BuildStartRoad(Player* player, Vec2i fromAnchor, Vec2i fromFootprint,
        Vec2i toAnchor, Vec2i toFootprint)
    {
        if (player == nullptr)
            return;

        TileMap* map = player->GetTileMap();
        if (map == nullptr)
            return;
        TileMap& tilemap = *map;
        std::vector<int> path = FindRoadPathBetweenFootprints(
            tilemap, player, fromAnchor, fromFootprint, toAnchor, toFootprint);

        // No route exists — leave the village unconnected rather than place
        // roads that the placement rules would refuse.
        for (int tileId : path)
            player->Build<Road>(tilemap.GetCoordsFromId(tileId), false);
    }
}


#endif
