#include "economy/BuildingSalvage.h"
#include "economy/Player.h"
#include "economy/StockpileIndex.h"
#include "simulation/MapGenerator.h"

#include <gtest/gtest.h>

namespace
{
void MakeGrassMap(TileMap& map, int width = 12, int height = 12)
{
    map.params.sizeX = width;
    map.params.sizeY = height;
    map.tilemap.clear();
    map.tilemap.reserve(width * height);
    for (int id = 0; id < width * height; ++id)
    {
        Tile tile{id};
        tile.tileType = TileType::GRASS;
        map.tilemap.push_back(std::move(tile));
    }
}
}

TEST(BuildingSalvageTests, MovesBuffersAndRefundsRecordedPayment)
{
    TileMap map;
    Player player{0, map};
    MakeGrassMap(map);

    auto* warehouse = dynamic_cast<StorageBuilding*>(map.PlaceLoadedBuilding(
        map.GetIdFromCoords({8, 8}), &player, std::make_unique<StorageBuilding>(10)));
    ASSERT_NE(warehouse, nullptr);
    warehouse->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 10};
    warehouse->storage.buffers[ResourceType::PLANKS] = ResourceBuffer{ResourceType::PLANKS, 10};

    auto* producer = dynamic_cast<Woodcutter*>(map.PlaceLoadedBuilding(
        map.GetIdFromCoords({1, 1}), &player, std::make_unique<Woodcutter>(11)));
    ASSERT_NE(producer, nullptr);
    producer->production.inputBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 4};
    producer->production.outputBuffers[ResourceType::PLANKS] = ResourceBuffer{ResourceType::PLANKS, 4};
    producer->production.inputBuffers[ResourceType::WOOD].GenerateResource(ResourceType::WOOD);
    producer->production.outputBuffers[ResourceType::PLANKS].GenerateResource(ResourceType::PLANKS);
    producer->buildCostRecordState = BuildCostRecordState::PaidRecorded;
    producer->buildCostWasPaid = true;
    producer->paidBuildCosts = {{ResourceType::WOOD, 5}};

    const int positionId = producer->positionId;
    const auto preview = BuildDemolitionPreview(map, *producer, player);
    ASSERT_TRUE(preview.allowed) << preview.reason;
    ASSERT_EQ(preview.resources.size(), 2u);

    EXPECT_TRUE(ExecuteDemolition(map, player, *producer));
    EXPECT_EQ(map.GetBuilding(positionId), nullptr);
    EXPECT_EQ(StockpileIndex::GetTotal(player, ResourceType::WOOD), 3);
    EXPECT_EQ(StockpileIndex::GetTotal(player, ResourceType::PLANKS), 1);
}

TEST(BuildingSalvageTests, DropsOverflowButStillDemolishesWhenEvacuationCapacityIsInsufficient)
{
    TileMap map;
    Player player{0, map};
    MakeGrassMap(map);

    auto* warehouse = dynamic_cast<StorageBuilding*>(map.PlaceLoadedBuilding(
        map.GetIdFromCoords({8, 8}), &player, std::make_unique<StorageBuilding>(20)));
    ASSERT_NE(warehouse, nullptr);
    warehouse->storage.buffers.clear();
    warehouse->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 1};

    auto* producer = dynamic_cast<Woodcutter*>(map.PlaceLoadedBuilding(
        map.GetIdFromCoords({1, 1}), &player, std::make_unique<Woodcutter>(21)));
    ASSERT_NE(producer, nullptr);
    producer->production.outputBuffers[ResourceType::PLANKS] = ResourceBuffer{ResourceType::PLANKS, 1};
    producer->production.outputBuffers[ResourceType::PLANKS].GenerateResource(ResourceType::PLANKS);

    const int positionId = producer->positionId;
    const auto preview = BuildDemolitionPreview(map, *producer, player);
    ASSERT_TRUE(preview.allowed) << preview.reason;
    ASSERT_EQ(preview.resources.size(), 1u);
    EXPECT_EQ(preview.resources.front().returnedAmount, 0);
    EXPECT_EQ(preview.resources.front().lostAmount, 1);
    EXPECT_TRUE(ExecuteDemolition(map, player, *producer));
    EXPECT_EQ(map.GetBuilding(positionId), nullptr);
}

TEST(BuildingSalvageTests, HeadquartersReceivesRefundBeforeACloserAlternativeWarehouse)
{
    TileMap map;
    Player player{0, map};
    MakeGrassMap(map, 16, 16);

    auto* headquarters = dynamic_cast<Headquarters*>(map.PlaceLoadedBuilding(
        map.GetIdFromCoords({4, 4}), &player, std::make_unique<Headquarters>(30)));
    auto* warehouse = dynamic_cast<StorageBuilding*>(map.PlaceLoadedBuilding(
        map.GetIdFromCoords({10, 10}), &player, std::make_unique<StorageBuilding>(31)));
    auto* producer = dynamic_cast<Woodcutter*>(map.PlaceLoadedBuilding(
        map.GetIdFromCoords({1, 1}), &player, std::make_unique<Woodcutter>(32)));
    ASSERT_NE(headquarters, nullptr);
    ASSERT_NE(warehouse, nullptr);
    ASSERT_NE(producer, nullptr);

    headquarters->storage.buffers.clear();
    headquarters->storage.buffers[ResourceType::PLANKS] = ResourceBuffer{ResourceType::PLANKS, 1};
    warehouse->storage.buffers.clear();
    warehouse->storage.buffers[ResourceType::PLANKS] = ResourceBuffer{ResourceType::PLANKS, 5};
    producer->production.outputBuffers[ResourceType::PLANKS] = ResourceBuffer{ResourceType::PLANKS, 3};
    for (int i = 0; i < 3; ++i)
        producer->production.outputBuffers[ResourceType::PLANKS].GenerateResource(ResourceType::PLANKS);

    const auto preview = BuildDemolitionPreview(map, *producer, player);
    ASSERT_TRUE(preview.allowed) << preview.reason;
    ASSERT_EQ(preview.resources.size(), 1u);
    EXPECT_EQ(preview.resources.front().returnedAmount, 3);
    EXPECT_EQ(preview.resources.front().lostAmount, 0);

    ASSERT_TRUE(ExecuteDemolition(map, player, *producer));
    EXPECT_EQ(headquarters->storage.buffers[ResourceType::PLANKS].buffer.size(), 1u);
    EXPECT_EQ(warehouse->storage.buffers[ResourceType::PLANKS].buffer.size(), 2u);
}
