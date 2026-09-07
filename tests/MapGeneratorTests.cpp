#include "core/GameWorld.h"
#include "core/GameWorldInternal.h"
#include "simulation/MapGenerator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <future>
#include <limits>
#include <random>
#include <set>
#include <thread>
#include <tuple>
#include <vector>

namespace
{
    MapParameters MakeParams(unsigned int seed)
    {
        MapParameters params;
        params.sizePreset = MapSizePreset::S;
        params.seed = seed;
        return params;
    }

    // Cross product of (b-a) and (c-a) — zero means a/b/c are collinear.
    long long Cross(Vec2i a, Vec2i b, Vec2i c)
    {
        long long abx = b.x - a.x, aby = b.y - a.y;
        long long acx = c.x - a.x, acy = c.y - a.y;
        return abx * acy - aby * acx;
    }

}

TEST(MapGeneratorTests, WorldInitializationReportsMonotonicPresentationProgress)
{
    GameWorld world;
    MapParameters params = MakeParams(20260828u);
    params.aiOpponentCount = 1;
    std::vector<float> progressValues;
    std::vector<std::string> messages;

    ASSERT_TRUE(world.InitWorld(
        "progress-callback", nullptr, params,
        [&](float value, const std::string& message)
        {
            progressValues.push_back(value);
            messages.push_back(message);
        }));

    ASSERT_FALSE(progressValues.empty());
    EXPECT_TRUE(std::is_sorted(progressValues.begin(), progressValues.end()));
    EXPECT_FLOAT_EQ(progressValues.back(), 1.0f);
    EXPECT_EQ(messages.back(), "World ready");
}

TEST(MapGeneratorTests, WorldInitializesOnBackgroundThreadWithoutPresentationServices)
{
    const std::thread::id mainThread = std::this_thread::get_id();
    auto future = std::async(std::launch::async, []
    {
        GameWorld world;
        std::thread::id callbackThread;
        MapParameters params = MakeParams(20260829u);
        params.aiOpponentCount = 1;
        const bool initialized = world.InitWorld(
            "background-generation", nullptr, params,
            [&](float, const std::string&)
            {
                callbackThread = std::this_thread::get_id();
            });
        return std::tuple{initialized, world.IsInitialized(), callbackThread};
    });

    const auto [initialized, worldInitialized, callbackThread] = future.get();
    EXPECT_TRUE(initialized);
    EXPECT_TRUE(worldInitialized);
    EXPECT_NE(callbackThread, std::thread::id{});
    EXPECT_NE(callbackThread, mainThread);
}

TEST(MapGeneratorTests, EmergencySafeSeedInitializesSingleAndMultiplayerWorlds)
{
    MapParameters singleParams = MakeParams(12345u);
    singleParams.sizeX = MapGenerator::SizeFromPreset(MapSizePreset::S);
    singleParams.sizeY = singleParams.sizeX;
    singleParams.aiOpponentCount = 1;
    GameWorld singleWorld;
    ASSERT_TRUE(singleWorld.InitWorld(
        "safe-single", nullptr, singleParams));

    MapParameters multiplayerParams = singleParams;
    multiplayerParams.aiOpponentCount = 0;
    GameWorld multiplayerWorld;
    ASSERT_TRUE(multiplayerWorld.InitMultiplayerWorld(
        "safe-multiplayer", nullptr,
        multiplayerParams, 0, true));
}

TEST(MapGeneratorTests, WorldInitializationUsesPeacefulPlayerCountsAndProvinceSizes)
{
    MapParameters singleParams = MakeParams(111u);
    singleParams.sizePreset = MapSizePreset::L;
    singleParams.sizeX = MapGenerator::SizeFromPreset(MapSizePreset::L);
    singleParams.sizeY = singleParams.sizeX;
    singleParams.aiOpponentCount = 4;

    GameWorld singlePlayerWorld;
    ASSERT_TRUE(singlePlayerWorld.InitWorld(
        "peaceful-large-single", nullptr, singleParams));
    EXPECT_EQ(singlePlayerWorld.GetPlayerHandler().players.size(), 1u);
    EXPECT_EQ(singlePlayerWorld.GetTileMap().params.sizeX, 401);

    MapParameters multiplayerParams = singleParams;
    multiplayerParams.aiOpponentCount = 5;
    GameWorld multiplayerWorld;
    ASSERT_TRUE(multiplayerWorld.InitMultiplayerWorld(
        "peaceful-large-multiplayer", nullptr,
        multiplayerParams, 0, true));
    EXPECT_EQ(multiplayerWorld.GetPlayerHandler().players.size(), 2u);
    EXPECT_EQ(multiplayerWorld.GetTileMap().params.sizeX, 401);
}

TEST(MapGeneratorTests, ResourcePatchCountScalesWithProvinceArea)
{
    auto makeParameters = [](MapSizePreset preset)
    {
        MapParameters params;
        params.sizePreset = preset;
        params.sizeX = MapGenerator::SizeFromPreset(preset);
        params.sizeY = params.sizeX;
        params.seed = 91731u;
        params.resourceDensity = 0.5f;
        params.resourceFieldSize = 0.5f;
        params.resourcePatches = {
            {TileType::WOOD, 200, 1, 1, {}, 1.0f}};
        return params;
    };

    auto countWoodTiles = [](const TileMap& map)
    {
        return std::count_if(map.tilemap.begin(), map.tilemap.end(),
                             [](const Tile& tile) { return tile.tileType == TileType::WOOD; });
    };

    TileMap smallMap;
    TileMap largeMap;
    MapGenerator generator;
    MapParameters smallParams = makeParameters(MapSizePreset::S);
    MapParameters largeParams = makeParameters(MapSizePreset::XL);
    generator.GenerateTileMap(smallMap, smallParams);
    generator.GenerateTileMap(largeMap, largeParams);

    const int smallWoodTiles = countWoodTiles(smallMap);
    const int largeWoodTiles = countWoodTiles(largeMap);
    EXPECT_GT(smallWoodTiles, 0);
    EXPECT_GT(largeWoodTiles, smallWoodTiles)
        << "larger provinces must receive more deposit patches, not only larger empty space";
}

// The starting road is now validated only by its own buildability and length;
// no military gates or track-clearance rule participates in the choice.
TEST(MapGeneratorTests, StartingVillageRoadStaysWithinPeacefulBudget)
{
    const std::vector<std::pair<int, unsigned int>> scenarios{
        {1, 11u}, {2, 222u}, {1, 3333u}};
    for (const auto& [playerCount, seed] : scenarios)
    {
        MapParameters params = MakeParams(seed);
        params.aiOpponentCount = 5;
        GameWorld world;
        if (playerCount == 1)
        {
            ASSERT_TRUE(world.InitWorld("peaceful-road", nullptr, params));
        }
        else
        {
            ASSERT_TRUE(world.InitMultiplayerWorld(
                "peaceful-road-multiplayer", nullptr, params, 0, true));
        }

        for (const auto& [playerId, player] : world.GetPlayerHandler().players)
        {
            ASSERT_NE(player, nullptr);
            EXPECT_GE(player->GetTrackedBuildingCount(BuildingType::Road), 20)
                << "seed=" << seed << " player=" << playerId;
            EXPECT_LE(player->GetTrackedBuildingCount(BuildingType::Road), 30)
                << "seed=" << seed << " player=" << playerId;
        }
    }
}

TEST(MapGeneratorTests, SinglePlayerReturnsOneAnchorNearMapCenter)
{
    MapParameters params = MakeParams(1);
    auto anchors = MapGenerator::PickHeadquartersAnchors(params, 1);
    ASSERT_EQ(anchors.size(), 1u);
    Vec2i footprint = MapGenerator::HeadquartersFootprint();
    EXPECT_NEAR(anchors[0].x, params.sizeX / 2 - footprint.x / 2, 2);
    EXPECT_NEAR(anchors[0].y, params.sizeY / 2 - footprint.y / 2, 2);
}

TEST(MapGeneratorTests, MultiplePlayersGetDistinctNonCollinearAnchors)
{
    for (int playerCount : {2, 3, 4, 5, 6})
    {
        MapParameters params = MakeParams(1000 + static_cast<unsigned int>(playerCount));
        auto anchors = MapGenerator::PickHeadquartersAnchors(params, playerCount);
        ASSERT_EQ(anchors.size(), static_cast<size_t>(playerCount)) << "playerCount=" << playerCount;

        // No two anchors coincide.
        std::set<std::pair<int, int>> seen;
        for (Vec2i anchor : anchors)
            EXPECT_TRUE(seen.insert({anchor.x, anchor.y}).second)
                << "duplicate anchor at (" << anchor.x << "," << anchor.y << ") playerCount=" << playerCount;

        if (playerCount < 3)
            continue;

        // Not every anchor collinear (the reported bug: players placed on a
        // line, so ring edges cross). At least one triple must have a
        // non-zero cross product.
        bool foundNonCollinearTriple = false;
        for (size_t i = 0; i < anchors.size() && !foundNonCollinearTriple; i++)
            for (size_t j = i + 1; j < anchors.size() && !foundNonCollinearTriple; j++)
                for (size_t k = j + 1; k < anchors.size() && !foundNonCollinearTriple; k++)
                    if (Cross(anchors[i], anchors[j], anchors[k]) != 0)
                        foundNonCollinearTriple = true;
        EXPECT_TRUE(foundNonCollinearTriple) << "all anchors collinear, playerCount=" << playerCount;
    }
}

TEST(MapGeneratorTests, AnchorsStayWithinMapBounds)
{
    Vec2i footprint = MapGenerator::HeadquartersFootprint();
    for (int playerCount : {2, 6})
    {
        MapParameters params = MakeParams(42);
        auto anchors = MapGenerator::PickHeadquartersAnchors(params, playerCount);
        for (Vec2i anchor : anchors)
        {
            EXPECT_GE(anchor.x, 0);
            EXPECT_GE(anchor.y, 0);
            EXPECT_LE(anchor.x + footprint.x, params.sizeX);
            EXPECT_LE(anchor.y + footprint.y, params.sizeY);
        }
    }
}

TEST(MapGeneratorTests, PlacementIsDeterministicForSameSeed)
{
    MapParameters params = MakeParams(777);
    auto anchorsA = MapGenerator::PickHeadquartersAnchors(params, 5);
    auto anchorsB = MapGenerator::PickHeadquartersAnchors(params, 5);
    EXPECT_EQ(anchorsA, anchorsB);
}

TEST(MapGeneratorTests, FailedWorldGenerationReturnsControlledErrorWithoutPartialWorld)
{
    MapParameters params = MakeParams(0xC0FFEEu);
    params.sizeX = 1;
    params.sizeY = 1;
    params.aiOpponentCount = 5;

    GameWorld world;
    EXPECT_FALSE(world.InitWorld("invalid-layout", nullptr, params));
    EXPECT_FALSE(world.IsInitialized());
    EXPECT_THAT(world.GetInitializationError(), testing::HasSubstr("seed"));
    EXPECT_THAT(world.GetInitializationError(), testing::HasSubstr("last attempt seed"));
    EXPECT_TRUE(world.GetPlayerHandler().players.empty());
    EXPECT_TRUE(world.GetTileMap().tilemap.empty());
}

TEST(MapGeneratorTests, StartingResourcePatchShapeIsRoundedIrregularAndSlightlyLarger)
{
    std::set<std::set<std::pair<int, int>>> distinctShapes;
    for (unsigned int seed : {11u, 222u, 3333u, 44444u})
    {
        std::mt19937 rng(seed);
        const std::vector<Vec2i> offsets =
            GameWorldInternal::BuildStartingResourcePatchOffsets(rng);

        // The previous radius-4 circle had 49 tiles. The new shape is
        // deliberately 6-8% larger while remaining compact and connected.
        EXPECT_GE(offsets.size(), 52u);
        EXPECT_LE(offsets.size(), 53u);

        std::set<std::pair<int, int>> uniqueOffsets;
        int minX = std::numeric_limits<int>::max();
        int minY = std::numeric_limits<int>::max();
        int maxX = std::numeric_limits<int>::min();
        int maxY = std::numeric_limits<int>::min();
        for (Vec2i offset : offsets)
        {
            uniqueOffsets.insert({offset.x, offset.y});
            minX = std::min(minX, offset.x);
            minY = std::min(minY, offset.y);
            maxX = std::max(maxX, offset.x);
            maxY = std::max(maxY, offset.y);
        }
        EXPECT_EQ(uniqueOffsets.size(), offsets.size());

        const int width = maxX - minX + 1;
        const int height = maxY - minY + 1;
        EXPECT_EQ(std::min(width, height), 7);
        EXPECT_GE(std::max(width, height), 9);
        EXPECT_LE(std::max(width, height), 11);

        std::set<std::pair<int, int>> visited;
        std::vector<std::pair<int, int>> frontier{*uniqueOffsets.begin()};
        while (!frontier.empty())
        {
            const auto current = frontier.back();
            frontier.pop_back();
            if (!visited.insert(current).second)
                continue;

            constexpr std::array<std::pair<int, int>, 4> neighbours{{
                {1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
            for (const auto [dx, dy] : neighbours)
            {
                const std::pair<int, int> next{current.first + dx, current.second + dy};
                if (uniqueOffsets.contains(next) && !visited.contains(next))
                    frontier.push_back(next);
            }
        }
        EXPECT_EQ(visited.size(), uniqueOffsets.size());
        distinctShapes.insert(std::move(uniqueOffsets));
    }

    EXPECT_GT(distinctShapes.size(), 1u);
}

// Every HQ needs reachable starter deposits for the first production chains.
TEST(MapGeneratorTests, EveryHqGetsStartingCoalAndIronOrePatches)
{
    for (unsigned int seed : {11u, 222u, 3333u, 44444u})
    {
        MapParameters params;
        params.sizePreset = MapSizePreset::S;
        params.aiOpponentCount = 1;
        params.seed = seed;

        GameWorld world;
        world.InitWorld("test", nullptr, params);
        TileMap& map = world.GetTileMap();

        for (auto& [playerId, player] : world.GetPlayerHandler().players)
        {
            Building* hq = nullptr;
            for (auto* building : player->GetTrackedBuildings())
                if (building != nullptr && building->buildingType == BuildingType::Headquarters)
                    hq = building;
            ASSERT_NE(hq, nullptr) << "seed=" << seed << " player=" << playerId;
            Vec2i hqCenter = map.GetCoordsFromId(hq->positionId);

            int coalTiles = 0;
            int ironTiles = 0;
            constexpr int kSearchRadius = 35;
            for (int y = -kSearchRadius; y <= kSearchRadius; y++)
            {
                for (int x = -kSearchRadius; x <= kSearchRadius; x++)
                {
                    Vec2i pos{hqCenter.x + x, hqCenter.y + y};
                    if (!map.IsInside(pos))
                        continue;
                    const Tile& tile = map[pos];
                    if (tile.tileType == TileType::COAL)
                    {
                        coalTiles++;
                    }
                    else if (tile.tileType == TileType::IRON_ORE)
                    {
                        ironTiles++;
                    }
                }
            }

            EXPECT_GE(coalTiles, 10) << "seed=" << seed << " player=" << playerId << " too few COAL tiles near HQ";
            EXPECT_GE(ironTiles, 10) << "seed=" << seed << " player=" << playerId << " too few IRON_ORE tiles near HQ";
        }
    }
}

TEST(MapGeneratorTests, StartingResourceLayoutIsDeterministicButNotCardinal)
{
    MapParameters params = MakeParams(1234);
    params.sizeX = 401;
    params.sizeY = 401;
    TileMap map;
    MapGenerator generator;
    generator.GenerateTileMap(map, params);
    for (auto& tile : map.tilemap)
    {
        tile.tileType = TileType::GRASS;
        tile.resourceRichness = 0;
        tile.resourceOverlayTextureId = -1;
    }

    const Vec2i hqAnchor{198, 198};
    const Vec2i hqFootprint{4, 4};
    const Vec2i villageAnchor{20, 20};
    const Vec2i villageFootprint{4, 4};
    std::mt19937 rngA(77);
    std::mt19937 rngB(77);
    std::mt19937 rngC(78);
    const auto layoutA = GameWorldInternal::PlanStartingResourceLayout(
        map, hqAnchor, hqFootprint, villageAnchor, villageFootprint, rngA);
    const auto layoutB = GameWorldInternal::PlanStartingResourceLayout(
        map, hqAnchor, hqFootprint, villageAnchor, villageFootprint, rngB);
    const auto layoutC = GameWorldInternal::PlanStartingResourceLayout(
        map, hqAnchor, hqFootprint, villageAnchor, villageFootprint, rngC);

    ASSERT_TRUE(layoutA.valid);
    ASSERT_TRUE(layoutB.valid);
    ASSERT_TRUE(layoutC.valid);
    for (size_t index = 0; index < layoutA.patches.size(); index++)
    {
        EXPECT_EQ(layoutA.patches[index].type, layoutB.patches[index].type);
        EXPECT_EQ(layoutA.patches[index].center, layoutB.patches[index].center);
        const Vec2i delta{
            layoutA.patches[index].center.x - (hqAnchor.x + hqFootprint.x / 2),
            layoutA.patches[index].center.y - (hqAnchor.y + hqFootprint.y / 2)};
        const int distanceSquared = delta.x * delta.x + delta.y * delta.y;
        EXPECT_GE(distanceSquared,
                  layoutA.patches[index].minCenterDist * layoutA.patches[index].minCenterDist);
        EXPECT_LE(distanceSquared,
                  layoutA.patches[index].maxCenterDist * layoutA.patches[index].maxCenterDist);
    }

    bool changed = false;
    for (size_t index = 0; index < layoutA.patches.size(); index++)
        changed = changed || layoutA.patches[index].type != layoutC.patches[index].type ||
                  layoutA.patches[index].center != layoutC.patches[index].center;
    EXPECT_TRUE(changed);
}

// Playtest report (2026-07-20): the starting Village's actual ROAD path to
// HQ (not straight-line distance) could end up much longer than intended
// once BuildStartRoad detours around the military track. Village placement
// now measures the real road path up front and re-rolls away from
// candidates that would exceed the budget (GameWorld.Init.cpp). If no
// detached candidate fits, the complete world layout is regenerated.
TEST(MapGeneratorTests, StartingVillageRoadStaysWithinBudget)
{
    constexpr int kMinVillageRoadTiles = 20;
    constexpr int kMaxVillageRoadTiles = 30;
    for (unsigned int seed : {11u, 222u, 3333u, 44444u, 55555u, 66666u})
    {
        MapParameters params;
        params.sizePreset = MapSizePreset::S;
        params.aiOpponentCount = 1;
        params.seed = seed;

        GameWorld world;
        world.InitWorld("test", nullptr, params);

        for (auto& [playerId, player] : world.GetPlayerHandler().players)
        {
            Building* hq = nullptr;
            Building* village = nullptr;
            for (auto* building : player->GetTrackedBuildings())
            {
                if (building == nullptr)
                    continue;
                if (building->buildingType == BuildingType::Headquarters)
                    hq = building;
                if (building->buildingType == BuildingType::Village)
                    village = building;
            }
            ASSERT_NE(hq, nullptr) << "seed=" << seed << " player=" << playerId;
            // A Village should always be placeable on a freshly generated
            // map; if this ever fires it means placement failed outright,
            // not just "farther than budget".
            ASSERT_NE(village, nullptr) << "seed=" << seed << " player=" << playerId << " no starting village placed";

            // The budget is on actual ROAD tiles (what BuildStartRoad places
            // between the two footprints), not RoadNetwork::CalculatePath's
            // footprint-inclusive convention (which always counts 2 more —
            // one tile from each endpoint's own footprint). At world-init
            // time the only roads a fresh player owns are this start road.
            int roadTiles = player->GetTrackedBuildingCount(BuildingType::Road);
            EXPECT_GE(roadTiles, kMinVillageRoadTiles)
                << "seed=" << seed << " player=" << playerId << " village road is " << roadTiles << " tiles";
            EXPECT_LE(roadTiles, kMaxVillageRoadTiles)
                << "seed=" << seed << " player=" << playerId << " village road is " << roadTiles << " tiles";
        }
    }
}
