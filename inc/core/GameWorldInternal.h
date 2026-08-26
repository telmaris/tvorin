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
            case BuildingType::Road: return std::make_unique<Road>(id);
            case BuildingType::DefenseTower: return std::make_unique<DefenseTower>(id);
            case BuildingType::Bridge: return std::make_unique<Bridge>(id);
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

    // `minCenterDist`/`maxCenterDist`: ring the patch center must fall in
    // (tiles from HQ center). `preferredDir`: unit-ish direction vector the
    // search biases toward within that ring, so multiple patches (WOOD/
    // STONE/COAL/IRON_ORE) spread around the HQ instead of overlapping —
    // see CreateStartingVillageAndResources for the four directions used.
    inline void PlaceStartingResourcePatch(TileMap& tilemap, Vec2i hqAnchor, Vec2i hqFootprint,
                                    Vec2i villageAnchor, Vec2i villageFootprint,
                                    TileType type, std::mt19937& rng,
                                    int minCenterDist, int maxCenterDist, Vec2i preferredDir)
    {
        Vec2i center{hqAnchor.x + hqFootprint.x / 2, hqAnchor.y + hqFootprint.y / 2};
        const std::vector<Vec2i> patchOffsets = BuildStartingResourcePatchOffsets(rng);
        Vec2i minOffset = patchOffsets.front();
        Vec2i maxOffset = patchOffsets.front();
        int maxExtent = 0;
        for (Vec2i offset : patchOffsets)
        {
            minOffset.x = std::min(minOffset.x, offset.x);
            minOffset.y = std::min(minOffset.y, offset.y);
            maxOffset.x = std::max(maxOffset.x, offset.x);
            maxOffset.y = std::max(maxOffset.y, offset.y);
            maxExtent = std::max({maxExtent, std::abs(offset.x), std::abs(offset.y)});
        }
        // User request (2026-07-17): patch centers live in a ring around the
        // HQ center (the patch stays clear of the 10-tile HQ build apron once
        // minCenterDist is 17+) while staying inside the starting zone.
        // COAL/IRON_ORE (user request 2026-07-19: iron is often missing near
        // spawn) use a wider ring than WOOD/STONE so all four patches fit
        // around the HQ without collisions.
        int kMinPatchCenterDist = minCenterDist;
        int kMaxPatchCenterDist = maxCenterDist;
        int preferredMagnitude = kMaxPatchCenterDist - maxExtent;
        Vec2i preferredOffset{preferredDir.x * preferredMagnitude, preferredDir.y * preferredMagnitude};

        // Track-side reachability (user report 2026-07-19, AIBehaviorHarnessTests
        // regression): a patch landing on the far side of the military track
        // from HQ forces the AI's first extractor there to need a Bridge
        // crossing it wouldn't otherwise need — and if that Bridge's own
        // PLANKS+STONE cost is blocked by the very extractor stranded across
        // it (no output reaching storage to build the stock), the deadlock is
        // permanent. One flood fill from the HQ center, avoiding
        // isMilitaryRoad tiles (already carved by GenerateWorldLayout before
        // any player exists), marks every tile reachable from HQ WITHOUT
        // crossing the track; candidates outside that region are rejected
        // outright below, same as this function already rejects tiles
        // sitting on the track itself.
        int mapTileCount = tilemap.params.sizeX * tilemap.params.sizeY;
        std::vector<bool> sameSideAsHq(mapTileCount, false);
        {
            std::queue<int> frontier;
            if (tilemap.IsInside(center))
            {
                int startId = tilemap.GetIdFromCoords(center);
                if (!tilemap[startId].isMilitaryRoad)
                {
                    sameSideAsHq[startId] = true;
                    frontier.push(startId);
                }
            }
            while (!frontier.empty())
            {
                int current = frontier.front();
                frontier.pop();
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
                    if (sameSideAsHq[nextId] || tilemap[nextId].isMilitaryRoad)
                        continue;
                    sameSideAsHq[nextId] = true;
                    frontier.push(nextId);
                }
            }
        }

        Vec2i bestCenter{-1, -1};
        int bestScore = std::numeric_limits<int>::min();
        for (int y = -kMaxPatchCenterDist; y <= kMaxPatchCenterDist; y++)
        {
            for (int x = -kMaxPatchCenterDist; x <= kMaxPatchCenterDist; x++)
            {
                Vec2i patchCenter{center.x + x, center.y + y};
                int distSq = x * x + y * y;
                if (distSq < kMinPatchCenterDist * kMinPatchCenterDist ||
                    distSq > kMaxPatchCenterDist * kMaxPatchCenterDist)
                    continue;
                if (!tilemap.IsInside(patchCenter) || !sameSideAsHq[tilemap.GetIdFromCoords(patchCenter)])
                    continue;

                Vec2i patchAnchor{patchCenter.x + minOffset.x, patchCenter.y + minOffset.y};
                Vec2i patchSize{
                    maxOffset.x - minOffset.x + 1,
                    maxOffset.y - minOffset.y + 1};
                if (!tilemap.IsInsideFootprint(patchAnchor, patchSize))
                    continue;
                if (FootprintsOverlap(patchAnchor, patchSize, hqAnchor, hqFootprint, 1) ||
                    FootprintsOverlap(patchAnchor, patchSize, villageAnchor, villageFootprint, 1))
                    continue;

                int paintableTiles = 0;
                for (Vec2i offset : patchOffsets)
                {
                    Vec2i pos{patchCenter.x + offset.x, patchCenter.y + offset.y};
                    if (!tilemap.IsInside(pos))
                        continue;

                    const Tile& tile = tilemap[pos];
                    if (tile.tileType == TileType::GRASS && !tile.HasBuilding() && !tile.isMilitaryRoad)
                        paintableTiles++;
                }

                if (paintableTiles <= 0)
                    continue;

                int preferredDistance = std::abs(x - preferredOffset.x) + std::abs(y - preferredOffset.y);
                int score = paintableTiles * 100 - preferredDistance;
                if (score > bestScore)
                {
                    bestScore = score;
                    bestCenter = patchCenter;
                }
            }
        }

        if (bestCenter.x < 0)
        {
            for (int y = -kMaxPatchCenterDist; y <= kMaxPatchCenterDist && bestCenter.x < 0; y++)
            {
                for (int x = -kMaxPatchCenterDist; x <= kMaxPatchCenterDist && bestCenter.x < 0; x++)
                {
                    Vec2i pos{center.x + x, center.y + y};
                    if (!tilemap.IsInside(pos))
                        continue;

                    Tile& tile = tilemap[pos];
                    if (tile.tileType == TileType::GRASS && !tile.HasBuilding())
                        bestCenter = pos;
                }
            }
        }

        if (bestCenter.x < 0)
            return;

        int painted = 0;
        for (Vec2i offset : patchOffsets)
        {
            Vec2i pos{bestCenter.x + offset.x, bestCenter.y + offset.y};
            if (!tilemap.IsInside(pos))
                continue;

            auto* building = tilemap.GetBuilding(pos);
            if (building != nullptr)
                continue;

            Tile& tile = tilemap[pos];
            if (tile.tileType != TileType::GRASS)
                continue;
            // Since the 2026-07-12 generation reorder these starting
            // patches run AFTER the military road is baked — a road tile
            // still has tileType GRASS (only isMilitaryRoad + richness=0
            // are set), so without this check the patch painted WOOD/
            // STONE straight over the unit track (user report 2026-07-14:
            // "tor jednostek na poletku kamienia czy drewna").
            if (tile.isMilitaryRoad)
                continue;

            tile.tileType = type;
            const bool usesOverlay = tilemap.HasResourceOverlay(type);
            tile.terrainTextureId = tilemap.PickTerrainTexture(usesOverlay ? TileType::GRASS : type, rng);
            tile.resourceOverlayTextureId = usesOverlay
                ? tilemap.PickResourceOverlayTexture(type, ResourceOverlayEdgeDirection::None, rng)
                : -1;
            tile.resourceRichness = std::max(1, tilemap.params.resourceRichness);
            painted++;
        }
        if (painted == 0)
            Log::Msg("[MapGenerator]", "Starting resource patch failed for tile type ", static_cast<int>(type));
        tilemap.terrainDirty = true;
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

        std::vector<bool> sameSideAsHq(tilemap.tilemap.size(), false);
        std::queue<int> frontier;
        if (tilemap.IsInside(hqCenter))
        {
            const int startId = tilemap.GetIdFromCoords(hqCenter);
            if (!tilemap.tilemap[startId].isMilitaryRoad)
            {
                sameSideAsHq[startId] = true;
                frontier.push(startId);
            }
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
                if (sameSideAsHq[nextId] || tilemap.tilemap[nextId].isMilitaryRoad)
                    continue;
                sameSideAsHq[nextId] = true;
                frontier.push(nextId);
            }
        }

        std::array<TileType, 4> resourceTypes{
            TileType::WOOD, TileType::STONE, TileType::COAL, TileType::IRON_ORE};
        std::shuffle(resourceTypes.begin(), resourceTypes.end(), rng);
        const std::array<int, 4> minDistances{17, 17, 26, 26};
        const std::array<int, 4> maxDistances{23, 23, 32, 32};
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
                        if (!sameSideAsHq[tileId] || tile.isMilitaryRoad || tile.HasBuilding() ||
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
                if (tile.HasBuilding() || tile.isMilitaryRoad || tile.tileType != TileType::GRASS)
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

    // Builds an orthogonal road between two starting buildings, routing
    // around the military track (user report 2026-07-19: "road do village
    // nachodzi na tor jednostek"). The original version walked a straight
    // Manhattan path (horizontal leg then vertical leg) between ONE fixed
    // point on each building's nearest side, with no awareness of the track
    // at all — CanBuildFootprint (via Player::Build) already refuses to
    // place a Road on an isMilitaryRoad tile, so a track tile on that
    // straight line was silently skipped, but the walk kept going past it
    // regardless, leaving a road that runs up against (or gaps across) the
    // track instead of avoiding it.
    //
    // A first BFS-based fix here still picked ONE fixed point per side as
    // the mandatory start/goal — which failed just as badly whenever that
    // one point happened to land ON the track itself (very possible: the
    // track is only GUARANTEED straight for kStubSeedLength tiles out of each HQ,
    // MilitaryRoadNetwork.cpp, and is free to wiggle after that). A 60-seed
    // sweep caught 9/120 villages this way: the BFS correctly found no path
    // to an unbuildable single tile and visited ~99% of the map proving it.
    //
    // Fixed properly by treating EVERY tile adjacent to each building's
    // footprint as a valid start/goal (same pattern AIActions::SubmitRoadPath
    // already uses for the same reason) — a multi-source, multi-target BFS
    // that finds the nearest REACHABLE pair of perimeter tiles instead of
    // gambling that one specific point is buildable.
    // Multi-source, multi-target BFS between every tile adjacent to each
    // building's footprint (same pattern AIActions::SubmitRoadPath uses),
    // treating every isMilitaryRoad-avoiding, buildable tile as passable.
    // Read-only — places nothing. Returns an empty path if no route exists.
    inline bool HasMilitaryRoadClearance(TileMap& tilemap, Vec2i anchor, Vec2i footprint, int clearance)
    {
        for (int y = anchor.y; y < anchor.y + footprint.y; y++)
        {
            for (int x = anchor.x; x < anchor.x + footprint.x; x++)
            {
                for (int offsetY = -clearance; offsetY <= clearance; offsetY++)
                {
                    for (int offsetX = -clearance; offsetX <= clearance; offsetX++)
                    {
                        Vec2i nearby{x + offsetX, y + offsetY};
                        if (tilemap.IsInside(nearby) && tilemap[nearby].isMilitaryRoad)
                            return false;
                    }
                }
            }
        }
        return true;
    }

    inline std::vector<int> FindRoadPathBetweenFootprints(TileMap& tilemap, Player* player,
        Vec2i fromAnchor, Vec2i fromFootprint, Vec2i toAnchor, Vec2i toFootprint,
        int militaryRoadClearance = 0)
    {
        const auto& roadDefinition = GetBuildingDefinition(BuildingType::Road);
        auto passable = [&](int tileId)
        {
            Vec2i pos = tilemap.GetCoordsFromId(tileId);
            return tilemap.CanBuildFootprint(pos, roadDefinition.footprint, player, BuildingType::Road) &&
                   HasMilitaryRoadClearance(tilemap, pos, roadDefinition.footprint, militaryRoadClearance);
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

        // No track-avoiding route exists between any pair of perimeter
        // tiles — shouldn't happen (the ring doesn't enclose a single HQ's
        // local area) short of a building being fully boxed in.
        if (reached < 0)
            return {};

        std::vector<int> path;
        for (int cursor = reached; cursor >= 0; cursor = parent[cursor])
            path.push_back(cursor);
        std::reverse(path.begin(), path.end());
        return path;
    }

    // Builds an orthogonal road between two starting buildings, routing
    // around the military track (user report 2026-07-19: "road do village
    // nachodzi na tor jednostek"). The original version walked a straight
    // Manhattan path (horizontal leg then vertical leg) between ONE fixed
    // point on each building's nearest side, with no awareness of the track
    // at all — CanBuildFootprint (via Player::Build) already refuses to
    // place a Road on an isMilitaryRoad tile, so a track tile on that
    // straight line was silently skipped, but the walk kept going past it
    // regardless, leaving a road that runs up against (or gaps across) the
    // track instead of avoiding it.
    //
    // A first BFS-based fix here still picked ONE fixed point per side as
    // the mandatory start/goal — which failed just as badly whenever that
    // one point happened to land ON the track itself (very possible: the
    // track is only GUARANTEED straight for kStubSeedLength tiles out of each HQ,
    // MilitaryRoadNetwork.cpp, and is free to wiggle after that). A 60-seed
    // sweep caught 9/120 villages this way: the BFS correctly found no path
    // to an unbuildable single tile and visited ~99% of the map proving it.
    //
    // Fixed properly by treating EVERY tile adjacent to each building's
    // footprint as a valid start/goal (same pattern AIActions::SubmitRoadPath
    // already uses for the same reason) — a multi-source, multi-target BFS
    // that finds the nearest REACHABLE pair of perimeter tiles instead of
    // gambling that one specific point is buildable. Pathfinding itself now
    // lives in FindRoadPathBetweenFootprints above so callers can measure a
    // route's length before committing to build it (village-too-far re-roll,
    // GameWorld.Init.cpp).
    inline void BuildStartRoad(Player* player, Vec2i fromAnchor, Vec2i fromFootprint,
        Vec2i toAnchor, Vec2i toFootprint, int militaryRoadClearance = 0)
    {
        if (player == nullptr)
            return;

        TileMap& tilemap = *player->tilemap;
        std::vector<int> path = FindRoadPathBetweenFootprints(
            tilemap, player, fromAnchor, fromFootprint, toAnchor, toFootprint, militaryRoadClearance);

        // No track-avoiding route exists — leave the village unconnected
        // rather than place a Road the placement rules would refuse anyway
        // (the old straight-walk fallback did exactly that, silently, for
        // the same net result).
        for (int tileId : path)
            player->Build<Road>(tilemap.GetCoordsFromId(tileId), false);
    }
}


#endif
