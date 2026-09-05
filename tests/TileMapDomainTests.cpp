#include "simulation/MapGenerator.h"
#include "economy/Player.h"
#include "core/RoadTopology.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>

namespace
{
    // Creates an owned grass map for TileMap tests.
    void FillMap(TileMap& map, Player* owner, int width = 12, int height = 12)
    {
        map.params.sizeX = width;
        map.params.sizeY = height;
        map.tilemap.clear();
        map.tilemap.reserve(width * height);
        for (int i = 0; i < width * height; i++)
        {
            Tile tile{i};
            tile.owner = owner;
            tile.tileType = TileType::GRASS;
            map.tilemap.push_back(std::move(tile));
        }
    }
}

TEST(TileMapDomainTests, TileBuildingLifecycle)
{
    Tile tile{3};

    tile.CreateBuilding(std::make_unique<Road>(9));
    ASSERT_NE(tile.GetBuilding(), nullptr);
    EXPECT_TRUE(tile.IsBuildingAnchor());
    EXPECT_TRUE(tile.HasBuilding());

    tile.DestroyBuilding();
    EXPECT_EQ(tile.GetBuilding(), nullptr);
    EXPECT_FALSE(tile.HasBuilding());
}

TEST(TileMapDomainTests, ContainsBuildingRejectsDestroyedObject)
{
    TileMap map;
    Player player{0, map};
    FillMap(map, &player, 8, 8);

    Building* pointer = map.PlaceLoadedBuilding(
        map.GetIdFromCoords({2, 2}), &player, std::make_unique<StorageBuilding>(1234));
    ASSERT_NE(pointer, nullptr);
    EXPECT_TRUE(map.ContainsBuilding(pointer));

    const int positionId = pointer->positionId;
    map.DestroyBuildingAt(positionId);
    EXPECT_FALSE(map.ContainsBuilding(pointer));
}

TEST(TileMapDomainTests, AdjacentTileIdsSkipFootprintAndDiagonals)
{
    TileMap map;
    Player player{0, map};
    FillMap(map, &player);

    auto* storage = map.PlaceLoadedBuilding(map.GetIdFromCoords({4, 4}), &player, std::make_unique<StorageBuilding>(1));
    ASSERT_NE(storage, nullptr);

    std::vector<int> adjacent = map.GetAdjacentTileIds(storage);
    EXPECT_FALSE(adjacent.empty());
    EXPECT_EQ(std::find(adjacent.begin(), adjacent.end(), map.GetIdFromCoords({4, 4})), adjacent.end());
    EXPECT_EQ(std::find(adjacent.begin(), adjacent.end(), map.GetIdFromCoords({3, 3})), adjacent.end());
    EXPECT_NE(std::find(adjacent.begin(), adjacent.end(), map.GetIdFromCoords({4, 3})), adjacent.end());
}

TEST(TileMapDomainTests, TerrainTexturePickerUsesWeightsAndFallbacks)
{
    TileMap map;
    std::mt19937 rng{123};

    EXPECT_EQ(map.GetTerrainTextureId(TileType::GRASS), 28);
    EXPECT_EQ(map.GetTerrainTextureId(static_cast<TileType>(999)), 0);

    map.terrainVariants[TileType::GRASS] = {{77, 0}, {88, 0}};
    EXPECT_EQ(map.PickTerrainTexture(TileType::GRASS, rng), 77);
    EXPECT_EQ(map.PickTerrainTexture(static_cast<TileType>(999), rng), 0);

    map.terrainVariants[TileType::GRASS] = {{77, 1}, {88, 0}};
    EXPECT_EQ(map.PickTerrainTexture(TileType::GRASS, rng), 77);
}

TEST(TileMapDomainTests, RoadAutotileMaskAndRefreshTrackNeighbors)
{
    TileMap map;
    Player player{0, map};
    FillMap(map, &player, 6, 6);

    auto* center = map.PlaceLoadedBuilding(map.GetIdFromCoords({2, 2}), &player, std::make_unique<Road>(1));
    auto* north = map.PlaceLoadedBuilding(map.GetIdFromCoords({2, 1}), &player, std::make_unique<Road>(2));
    auto* east = map.PlaceLoadedBuilding(map.GetIdFromCoords({3, 2}), &player, std::make_unique<Road>(3));
    ASSERT_NE(center, nullptr);
    ASSERT_NE(north, nullptr);
    ASSERT_NE(east, nullptr);

    int mask = map.GetRoadAutotileMask({2, 2});
    EXPECT_EQ(mask, RoadTopology::North | RoadTopology::East);

    map.RefreshRoadTilesAround({2, 2});
    EXPECT_EQ(center->textureId, map.GetRoadTextureId({2, 2}));
    EXPECT_TRUE(map.buildingsDirty);
}

TEST(TileMapDomainTests, AutoConnectAndConnectReceiverToggleProductionLinks)
{
    TileMap map;
    Player player{0, map};
    FillMap(map, &player);

    auto* storage = map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<StorageBuilding>(1));
    auto* mill = dynamic_cast<LumberMill*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({6, 1}), &player, std::make_unique<LumberMill>(2)));
    ASSERT_NE(storage, nullptr);
    ASSERT_NE(mill, nullptr);

    map.AutoConnectBuilding(mill);
    EXPECT_TRUE(mill->HasReceiver(ResourceType::PLANKS));
    EXPECT_TRUE(mill->HasSupplier(ResourceType::WOOD));

    map.ConnectReceiver(mill, storage);
    EXPECT_FALSE(mill->HasReceiver(ResourceType::PLANKS));
    map.ConnectReceiver(mill, storage);
    EXPECT_TRUE(mill->HasReceiver(ResourceType::PLANKS));
}

TEST(TileMapDomainTests, CardinalRoadMaskCoversAllSixteenConfigurations)
{
    for (int expected = 0; expected < 16; expected++)
    {
        const int actual = RoadTopology::GetCardinalMask(2, 2, 5, 5,
            [&](int x, int y)
            {
                if (x == 1 && y == 2) return (expected & RoadTopology::West) != 0;
                if (x == 3 && y == 2) return (expected & RoadTopology::East) != 0;
                if (x == 2 && y == 1) return (expected & RoadTopology::North) != 0;
                if (x == 2 && y == 3) return (expected & RoadTopology::South) != 0;
                return false;
            });
        EXPECT_EQ(actual, expected);
    }
}

TEST(TileMapDomainTests, RoadMaskIgnoresAdjacentNonRoadBuildings)
{
    TileMap map;
    Player player{0, map};
    FillMap(map, &player, 8, 8);

    auto* center = map.PlaceLoadedBuilding(map.GetIdFromCoords({3, 3}), &player, std::make_unique<Road>(1));
    auto* north = map.PlaceLoadedBuilding(map.GetIdFromCoords({3, 2}), &player, std::make_unique<Road>(2));
    auto* eastBuilding = map.PlaceLoadedBuilding(map.GetIdFromCoords({4, 3}), &player, std::make_unique<StorageBuilding>(3));
    ASSERT_NE(center, nullptr);
    ASSERT_NE(north, nullptr);
    ASSERT_NE(eastBuilding, nullptr);

    EXPECT_EQ(map.GetRoadAutotileMask({3, 3}), RoadTopology::North);

}

TEST(TileMapDomainTests, AutoConnectConsumerDoesNotChangeExistingProducerDestination)
{
    TileMap map;
    Player player{0, map};
    FillMap(map, &player);
    map[Vec2i{1, 1}].tileType = TileType::WOOD;

    auto* storage = map.PlaceLoadedBuilding(map.GetIdFromCoords({8, 1}), &player,
                                            std::make_unique<StorageBuilding>(1));
    auto* woodcutter = dynamic_cast<Woodcutter*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player,
                                std::make_unique<Woodcutter>(2)));
    ASSERT_NE(storage, nullptr);
    ASSERT_NE(woodcutter, nullptr);

    map.AutoConnectBuilding(woodcutter);
    auto receiversBefore = woodcutter->GetReceiverViews();
    ASSERT_EQ(receiversBefore.size(), 1u);
    EXPECT_EQ(receiversBefore.front().type, ResourceType::WOOD);
    EXPECT_EQ(receiversBefore.front().building, storage);
    EXPECT_FALSE(receiversBefore.front().alternative);

    auto* lumberMill = dynamic_cast<LumberMill*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({4, 5}), &player,
                                std::make_unique<LumberMill>(3)));
    ASSERT_NE(lumberMill, nullptr);
    map.AutoConnectBuilding(lumberMill);

    auto receiversAfter = woodcutter->GetReceiverViews();
    ASSERT_EQ(receiversAfter.size(), 1u);
    EXPECT_EQ(receiversAfter.front().building, storage)
        << "constructing a consumer must not replace the producer's destination";
    EXPECT_FALSE(receiversAfter.front().alternative)
        << "constructing a consumer must not add a hidden alternative destination";

    map.ConnectReceiver(woodcutter, lumberMill);
    auto receiversAfterPlayerCommand = woodcutter->GetReceiverViews();
    ASSERT_EQ(receiversAfterPlayerCommand.size(), 1u);
    EXPECT_EQ(receiversAfterPlayerCommand.front().building, lumberMill)
        << "the explicit player command remains the way to route producer output";
}

