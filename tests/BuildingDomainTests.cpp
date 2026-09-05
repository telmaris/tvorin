#include "simulation/MapGenerator.h"
#include "economy/Player.h"
#include "economy/StockpileIndex.h"
#include "economy/ProductionBuildings.h"
#include "simulation/RoadNetwork.h"
#include "core/GameWorld.h"
#include "ui/GuiController.h"
#include "warfare/UnitDefinition.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<Building>);
static_assert(!std::is_move_constructible_v<Building>);
static_assert(!std::is_copy_constructible_v<Road>);
static_assert(!std::is_move_constructible_v<Road>);
static_assert(!std::is_copy_constructible_v<Headquarters>);
static_assert(!std::is_move_constructible_v<Headquarters>);
static_assert(!std::is_copy_constructible_v<Barracks>);
static_assert(!std::is_move_constructible_v<Barracks>);
static_assert(!std::is_copy_constructible_v<LumberMill>);
static_assert(!std::is_move_constructible_v<LumberMill>);

namespace
{
    // Creates a small owned grass map for building-domain tests.
    void FillOwnedGrass(TileMap& map, Player* owner, int width = 8, int height = 8)
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

    // Paints one terrain resource area with matching richness.
    void Paint(TileMap& map, Vec2i anchor, Vec2i footprint, TileType type, int richness)
    {
        for (int y = 0; y < footprint.y; y++)
        {
            for (int x = 0; x < footprint.x; x++)
            {
                Tile& tile = map.tilemap[map.GetIdFromCoords({anchor.x + x, anchor.y + y})];
                tile.tileType = type;
                tile.resourceRichness = richness;
            }
        }
    }

    // Places a loaded building and registers its footprint in the road network.
    template <typename T>
    T* PlaceAndRegister(TileMap& map, RoadNetwork& network, Player* owner, Vec2i anchor, int id)
    {
        auto* placed = dynamic_cast<T*>(
            map.PlaceLoadedBuilding(map.GetIdFromCoords(anchor), owner, std::make_unique<T>(id)));
        if (placed == nullptr)
            return nullptr;

        for (int occupiedTileId : map.GetBuildingTileIds(placed))
            network.UpdateNavMap(occupiedTileId, placed);
        return placed;
    }
}

TEST(BuildingDomainTests, BaseBuildingTracksEfficiencyConstructionAndProgress)
{
    Road building{1};
    building.lifetime = 20.0;
    building.activeTime = 5.0;
    EXPECT_FLOAT_EQ(building.GetEfficiency(), 0.25f);

    building.buildTime = 10.0;
    building.constructionRemaining = 6.0;
    EXPECT_FLOAT_EQ(building.GetConstructionProgress(), 0.4f);
    EXPECT_TRUE(building.UpdateConstruction(2.0));
    EXPECT_DOUBLE_EQ(building.constructionRemaining, 4.0);
    EXPECT_FALSE(building.UpdateConstruction(10.0));
    EXPECT_DOUBLE_EQ(building.constructionRemaining, 0.0);
}

TEST(BuildingDomainTests, BuildingCapabilitiesExposeAttachedComponents)
{
    Road road{1};
    EXPECT_TRUE(road.HasComponent<RoadComponent>());
    EXPECT_EQ(road.GetComponent<StorageComponent>(), nullptr);

    Woodcutter production{2};
    EXPECT_TRUE(production.HasComponent<ProductionComponent>());
    EXPECT_TRUE(production.HasComponent<LogisticsComponent>());
    EXPECT_TRUE(production.HasComponent<WorkerComponent>());
    EXPECT_TRUE(production.HasComponent<RecipeComponent>());
    EXPECT_FALSE(production.HasComponent<ResearchComponent>()); // only University researches
    EXPECT_EQ(production.GetComponent<ProductionComponent>(), &production.production);
    EXPECT_EQ(production.GetComponent<WorkerComponent>(), &production.workers);
    EXPECT_EQ(production.GetComponent<RecipeComponent>(), &production.recipes);

    University university{5};
    EXPECT_TRUE(university.HasComponent<ResearchComponent>());
    EXPECT_EQ(university.GetComponent<ResearchComponent>(), &university.research);

    Headquarters headquarters{3};
    EXPECT_TRUE(headquarters.HasComponent<StorageComponent>());
    EXPECT_EQ(headquarters.GetComponent<StorageComponent>(), &headquarters.storage);

    Barracks barracks{4};
    EXPECT_FALSE(barracks.HasComponent<StorageComponent>());
    EXPECT_TRUE(barracks.HasComponent<LocalResourceBufferComponent>());

}

TEST(BuildingDomainTests, RoadTrafficTelemetryAveragesLoadAndHoldsSaturationWarning)
{
    Road road{7};
    road.road.maxCapacity.SetBase(4);
    road.transportables.resize(4, nullptr);

    for (int i = 0; i < 100; ++i)
        road.road.Update(road, 0.1);

    EXPECT_NEAR(road.road.GetTrafficUtilizationTrend(), 1.0 - std::exp(-1.0), 0.01);
    EXPECT_TRUE(road.road.HasRecentSaturation());

    road.transportables.clear();
    for (int i = 0; i < 13; ++i)
        road.road.Update(road, 0.1);

    EXPECT_FALSE(road.road.HasRecentSaturation());
    EXPECT_GT(road.road.GetTrafficUtilizationTrend(), 0.0)
        << "trend should decay gradually instead of mirroring the current frame";
}

TEST(BuildingDomainTests, RoadDragLocksAxisUntilMousePauses)
{
    RoadDragStabilizer stabilizer;
    stabilizer.Begin({10, 10}, Vector2{100.0f, 100.0f});

    EXPECT_EQ(stabilizer.Constrain({10, 8}, Vector2{102.0f, 70.0f}, 0.016, {10, 10}),
              (Vec2i{10, 8}));
    EXPECT_EQ(stabilizer.GetAxis(), RoadDragStabilizer::Axis::Vertical);

    // Horizontal hand jitter is discarded while the vertical segment is live.
    EXPECT_EQ(stabilizer.Constrain({11, 7}, Vector2{104.0f, 50.0f}, 0.016, {10, 8}),
              (Vec2i{10, 7}));

    for (int i = 0; i < 10; ++i)
        stabilizer.Constrain({11, 7}, Vector2{104.0f, 50.0f}, 0.02, {10, 7});
    EXPECT_EQ(stabilizer.GetAxis(), RoadDragStabilizer::Axis::None);

    EXPECT_EQ(stabilizer.Constrain({13, 7}, Vector2{140.0f, 51.0f}, 0.016, {10, 7}),
              (Vec2i{13, 7}));
    EXPECT_EQ(stabilizer.GetAxis(), RoadDragStabilizer::Axis::Horizontal);
}

TEST(BuildingDomainTests, ProductionBuildingReportsBuffersConnectionsAndStalledState)
{
    Woodcutter building{7};
    building.production.ingredients.clear();
    building.production.products.clear();
    building.production.inputBuffers.clear();
    building.production.outputBuffers.clear();
    building.production.ingredients[ResourceType::WOOD] = 2;
    building.production.products[ResourceType::PLANKS] = 1;
    building.production.inputBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 4};
    building.production.outputBuffers[ResourceType::PLANKS] = ResourceBuffer{ResourceType::PLANKS, 2};
    building.workers.capacity = 4;
    building.workers.assigned = 2;
    building.production.cycleTime = 10.0;
    building.production.started = true;
    building.production.elapsed = 5.0;

    EXPECT_FLOAT_EQ(building.GetWorkerRatio(), 0.5f);
    EXPECT_FLOAT_EQ(building.GetProductionProgress(), 0.5f);
    EXPECT_TRUE(building.CanAcceptResource(ResourceType::WOOD));
    EXPECT_TRUE(building.CanReceiveResource(ResourceType::WOOD));
    EXPECT_FALSE(building.HasSupplier(ResourceType::WOOD));
    EXPECT_FALSE(building.HasReceiver(ResourceType::PLANKS));

    auto inputs = building.GetInputBufferViews();
    ASSERT_EQ(inputs.size(), 1u);
    EXPECT_EQ(inputs.front().recipeAmount, 2);

    auto outputs = building.GetOutputBufferViews();
    ASSERT_EQ(outputs.size(), 1u);
    EXPECT_EQ(outputs.front().recipeAmount, 1);

    EXPECT_TRUE(building.IsProductionStalled());
    building.SetProductionBlocked(true);
    EXPECT_FALSE(building.IsProductionStalled());
}

TEST(BuildingDomainTests, ProductionBuildingEffectiveCycleTimeUsesWorkerEfficiency)
{
    Woodcutter building{7};
    building.production.cycleTime = 10.0;
    building.workers.capacity = 4;
    building.workers.assigned = 2;

    EXPECT_DOUBLE_EQ(building.production.GetEffectiveCycleTime(building), 20.0);

    building.workers.assigned = 0;
    EXPECT_TRUE(std::isinf(building.production.GetEffectiveCycleTime(building)));
}

TEST(BuildingDomainTests, ProductionInputRequestsStopAtManualBlockOrFullNextOutput)
{
    Woodcutter building{8};
    building.production.ingredients.clear();
    building.production.products.clear();
    building.production.inputBuffers.clear();
    building.production.outputBuffers.clear();
    building.production.products[ResourceType::PLANKS] = 2;
    building.production.outputBuffers[ResourceType::PLANKS] =
        ResourceBuffer{ResourceType::PLANKS, 4};
    building.workers.capacity = 1;
    building.workers.assigned = 1;
    building.production.cycleTime = 1.0;

    EXPECT_TRUE(building.production.ShouldRequestInputs(building));

    building.SetProductionBlocked(true);
    EXPECT_FALSE(building.production.ShouldRequestInputs(building));

    building.SetProductionBlocked(false);
    building.production.outputBuffers[ResourceType::PLANKS].buffer.push_back(
        Resource::CreateOwned(ResourceType::PLANKS));
    building.production.outputBuffers[ResourceType::PLANKS].buffer.push_back(
        Resource::CreateOwned(ResourceType::PLANKS));
    building.production.outputBuffers[ResourceType::PLANKS].buffer.push_back(
        Resource::CreateOwned(ResourceType::PLANKS));
    EXPECT_FALSE(building.production.ShouldRequestInputs(building));

    building.production.outputBuffers[ResourceType::PLANKS].FreeResource();
    building.production.outputBuffers[ResourceType::PLANKS].FreeResource();
    building.production.outputBuffers[ResourceType::PLANKS].FreeResource();
}

// Regression test for the "production stalls for a moment right at 100%"
// report: GetProductionProgress() used to divide by the unmodified
// cycleTime.GetBase(), while Produce() actually completes the cycle when
// elapsed reaches GetModifiedCycleTime() (tech/focus adjusted) — any active
// modifier on ProductionCycleTime desynced the two.
TEST(BuildingDomainTests, ProductionProgressMatchesModifiedCycleTimeNotBaseline)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);

    auto* building = dynamic_cast<Woodcutter*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({2, 2}), &player, std::make_unique<Woodcutter>(9)));
    ASSERT_NE(building, nullptr);
    building->production.cycleTime = 10.0;
    building->production.started = true;

    // An extra 50% slowdown (e.g. a focus penalty). Read back the real
    // effective threshold rather than hardcoding it: the point is that
    // GetProductionProgress must track WHATEVER GetModifiedCycleTime
    // returns, not the raw base.
    player.balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::ProductionCycleTime, 0.0, 1.5, BalanceModifierScope::Global(),
        std::nullopt, std::nullopt, "test:slow_production"});
    double effective = building->production.GetModifiedCycleTime(*building);
    ASSERT_GT(effective, 10.0) << "test modifier should make the cycle slower than the 10.0 base";

    // At elapsed == base (10.0), the cycle is nowhere near actually done
    // (needs `effective`) — progress must reflect that, not report 100%.
    building->production.elapsed = 10.0;
    EXPECT_FLOAT_EQ(building->GetProductionProgress(), static_cast<float>(10.0 / effective));

    // At elapsed == the real modified threshold, progress is exactly 100% —
    // matching the point where Produce() actually completes the cycle.
    building->production.elapsed = effective;
    EXPECT_FLOAT_EQ(building->GetProductionProgress(), 1.0f);
}

TEST(BuildingDomainTests, TerrainRichnessIsConsumedAndTurnsExhaustedTileToGrass)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);

    Vec2i anchor{2, 2};
    Vec2i footprint = GetBuildingDefinition(BuildingType::Woodcutter).footprint;
    Paint(map, anchor, footprint, TileType::WOOD, 1);

    auto* woodcutter = dynamic_cast<Woodcutter*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords(anchor), &player, std::make_unique<Woodcutter>(3)));
    ASSERT_NE(woodcutter, nullptr);

    EXPECT_TRUE(woodcutter->production.HasTerrainRichness(*woodcutter));
    EXPECT_TRUE(woodcutter->production.ConsumeTerrainRichness(*woodcutter));
    EXPECT_EQ(map.tilemap[map.GetIdFromCoords(anchor)].tileType, TileType::GRASS);
    EXPECT_TRUE(map.terrainDirty);
}

TEST(BuildingDomainTests, StorageBuffersExposeCapacityAndReceiveRules)
{
    StorageBuilding storage{5};
    storage.storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 1};

    EXPECT_TRUE(storage.IsStorageLike());
    EXPECT_TRUE(storage.CanAcceptResource(ResourceType::WOOD));
    EXPECT_TRUE(storage.CanReceiveResource(ResourceType::WOOD));

    storage.storage.buffers[ResourceType::WOOD].GenerateResource(ResourceType::WOOD);
    EXPECT_FALSE(storage.CanReceiveResource(ResourceType::WOOD));

    auto views = storage.GetOutputBufferViews();
    auto woodView = std::find_if(views.begin(), views.end(), [](const ResourceBufferView& view)
    {
        return view.type == ResourceType::WOOD;
    });
    ASSERT_NE(woodView, views.end());
    EXPECT_EQ(woodView->amount, 1);
    EXPECT_EQ(woodView->capacity, 1);
    storage.storage.buffers[ResourceType::WOOD].Clear();
}

TEST(BuildingDomainTests, StorageAddsGetsAndRejectsResourcesPrecisely)
{
    StorageBuilding storage{6};
    storage.storage.buffers.clear();
    storage.storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 1};

    Resource wood{ResourceType::WOOD};
    Resource stone{ResourceType::STONE};
    storage.AddResource(&wood);
    storage.AddResource(&stone);

    EXPECT_EQ(storage.storage.buffers[ResourceType::WOOD].buffer.size(), 1u);
    EXPECT_FALSE(storage.storage.buffers.contains(ResourceType::STONE));

    Resource fetched = storage.GetResource(ResourceType::WOOD);
    EXPECT_EQ(fetched.type, ResourceType::WOOD);
    EXPECT_TRUE(storage.storage.buffers[ResourceType::WOOD].buffer.empty());
}

TEST(BuildingDomainTests, ProductionBuildingAcceptsAndReturnsResources)
{
    Woodcutter building{8};
    building.production.inputBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 1};
    building.production.outputBuffers[ResourceType::PLANKS] = ResourceBuffer{ResourceType::PLANKS, 2};
    building.logistics.pendingRequests[ResourceType::WOOD] = 1;

    Resource wood{ResourceType::WOOD};
    building.AddResource(&wood);
    EXPECT_EQ(building.production.inputBuffers[ResourceType::WOOD].buffer.size(), 1u);
    EXPECT_EQ(building.logistics.pendingRequests[ResourceType::WOOD], 0);

    Resource fetched = building.GetResource(ResourceType::WOOD);
    EXPECT_EQ(fetched.type, ResourceType::WOOD);

    Resource plank{ResourceType::PLANKS};
    building.ReturnOutgoingResource(&plank);
    EXPECT_EQ(building.production.outputBuffers[ResourceType::PLANKS].buffer.size(), 1u);
}

TEST(BuildingDomainTests, ProductionBuildingRequestsFromMultipleSuppliers)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);
    RoadNetwork network{map};
    player.GetProvinceEconomy()->roadNetwork = std::make_unique<RoadNetwork>(map);

    Woodcutter building{8};
    building.owner = &player;
    building.production.products.clear();
    building.production.outputBuffers.clear();
    building.production.inputBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 4};

    StorageBuilding supplierA{1};
    supplierA.owner = &player;
    supplierA.storage.buffers.clear();
    supplierA.storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 2};
    supplierA.storage.buffers[ResourceType::WOOD].SetStoredAmount(1);

    StorageBuilding supplierB{2};
    supplierB.owner = &player;
    supplierB.storage.buffers.clear();
    supplierB.storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 2};
    supplierB.storage.buffers[ResourceType::WOOD].SetStoredAmount(1);

    building.SetSupplier(ResourceType::WOOD, &supplierA);
    building.SetSupplier(ResourceType::WOOD, &supplierB);

    EXPECT_TRUE(building.HasSupplier(ResourceType::WOOD));
    // Suppliers together hold only 2 WOOD, so a request for 4 cannot be filled
    // and must flag the producer as blocked.
    EXPECT_LT(building.logistics.RequestResource(ResourceType::WOOD, 4, building), 4);
    EXPECT_TRUE(building.logistics.requestBlocked);
    supplierA.storage.buffers[ResourceType::WOOD].Clear();
    supplierB.storage.buffers[ResourceType::WOOD].Clear();
}

TEST(BuildingDomainTests, MultipleProducersPushOutputToSameConsumerUntilInputIsReserved)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player, 12, 9);
    auto network = std::make_unique<RoadNetwork>(map);
    RoadNetwork* networkPtr = network.get();
    player.GetProvinceEconomy()->roadNetwork = std::move(network);

    auto* woodcutterA = PlaceAndRegister<Woodcutter>(map, *networkPtr, &player, {0, 0}, 1);
    auto* woodcutterB = PlaceAndRegister<Woodcutter>(map, *networkPtr, &player, {0, 3}, 2);
    auto* woodcutterC = PlaceAndRegister<Woodcutter>(map, *networkPtr, &player, {0, 6}, 3);
    auto* lumberMill = PlaceAndRegister<LumberMill>(map, *networkPtr, &player, {8, 3}, 4);
    ASSERT_NE(woodcutterA, nullptr);
    ASSERT_NE(woodcutterB, nullptr);
    ASSERT_NE(woodcutterC, nullptr);
    ASSERT_NE(lumberMill, nullptr);

    for (int y = 1; y <= 7; y++)
    {
        auto* road = PlaceAndRegister<Road>(map, *networkPtr, &player, {2, y}, 100 + y);
        ASSERT_NE(road, nullptr);
        road->road.maxCapacity.SetBase(16);
    }
    for (int x = 3; x <= 7; x++)
    {
        auto* road = PlaceAndRegister<Road>(map, *networkPtr, &player, {x, 4}, 200 + x);
        ASSERT_NE(road, nullptr);
        road->road.maxCapacity.SetBase(16);
    }

    auto fillWoodOutput = [](Woodcutter* woodcutter)
    {
        woodcutter->production.outputBuffers[ResourceType::WOOD].SetStoredAmount(3);
        woodcutter->SetReceiver(ResourceType::WOOD, nullptr);
    };
    fillWoodOutput(woodcutterA);
    fillWoodOutput(woodcutterB);
    fillWoodOutput(woodcutterC);

    lumberMill->production.inputBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 8};
    woodcutterA->SetReceiver(ResourceType::WOOD, lumberMill);
    woodcutterB->SetReceiver(ResourceType::WOOD, lumberMill);
    woodcutterC->SetReceiver(ResourceType::WOOD, lumberMill);

    woodcutterA->logistics.DispatchOutputs(*woodcutterA, woodcutterA->production);
    woodcutterB->logistics.DispatchOutputs(*woodcutterB, woodcutterB->production);
    woodcutterC->logistics.DispatchOutputs(*woodcutterC, woodcutterC->production);

    EXPECT_EQ(woodcutterA->transportables.size(), 3u);
    EXPECT_EQ(woodcutterB->transportables.size(), 3u);
    EXPECT_EQ(woodcutterC->transportables.size(), 2u);
    EXPECT_EQ(lumberMill->production.inputBuffers[ResourceType::WOOD].buffer.size(), 0u);
}

TEST(BuildingDomainTests, ProducerWithNoReceiverPushesFullOutputToNearestHeadquarters)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player, 10, 6);
    auto network = std::make_unique<RoadNetwork>(map);
    RoadNetwork* networkPtr = network.get();
    player.GetProvinceEconomy()->roadNetwork = std::move(network);

    Vec2i woodAnchor{0, 1};
    Paint(map, woodAnchor, GetBuildingDefinition(BuildingType::Woodcutter).footprint, TileType::WOOD, 10);
    auto* woodcutter = PlaceAndRegister<Woodcutter>(map, *networkPtr, &player, woodAnchor, 1);
    auto* headquarters = PlaceAndRegister<Headquarters>(map, *networkPtr, &player, {5, 1}, 2);
    ASSERT_NE(woodcutter, nullptr);
    ASSERT_NE(headquarters, nullptr);

    ASSERT_NE(PlaceAndRegister<Road>(map, *networkPtr, &player, {2, 2}, 10), nullptr);
    ASSERT_NE(PlaceAndRegister<Road>(map, *networkPtr, &player, {3, 2}, 11), nullptr);
    ASSERT_NE(PlaceAndRegister<Road>(map, *networkPtr, &player, {4, 2}, 12), nullptr);

    woodcutter->SetReceiver(ResourceType::WOOD, nullptr);
    woodcutter->production.outputBuffers[ResourceType::WOOD].SetStoredAmount(3);
    const size_t initialHeadquartersWood = headquarters->storage.buffers[ResourceType::WOOD].buffer.size();

    woodcutter->logistics.DispatchOutputs(*woodcutter, woodcutter->production);

    EXPECT_EQ(woodcutter->transportables.size(), 3u);
    EXPECT_TRUE(woodcutter->HasReceiver(ResourceType::WOOD));

    for (int i = 0; i < 8; i++)
        map.UpdateBuildings(1.1);

    EXPECT_TRUE(woodcutter->transportables.empty());
    EXPECT_EQ(headquarters->storage.buffers[ResourceType::WOOD].buffer.size(), initialHeadquartersWood + 3u);
}

TEST(BuildingDomainTests, ConcurrentHqSupplyDoesNotPreventProducerDispatchToHq)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player, 16, 9);
    auto network = std::make_unique<RoadNetwork>(map);
    RoadNetwork* networkPtr = network.get();
    player.GetProvinceEconomy()->roadNetwork = std::move(network);

    Vec2i woodAnchor{0, 1};
    Paint(map, woodAnchor, GetBuildingDefinition(BuildingType::Woodcutter).footprint,
          TileType::WOOD, 100);
    auto* woodcutter = PlaceAndRegister<Woodcutter>(map, *networkPtr, &player, woodAnchor, 1);
    auto* headquarters = PlaceAndRegister<Headquarters>(map, *networkPtr, &player, {8, 1}, 2);
    auto* lumberMill = PlaceAndRegister<LumberMill>(map, *networkPtr, &player, {4, 5}, 3);
    ASSERT_NE(woodcutter, nullptr);
    ASSERT_NE(headquarters, nullptr);
    ASSERT_NE(lumberMill, nullptr);

    for (int x = 2; x <= 7; ++x)
        ASSERT_NE(PlaceAndRegister<Road>(map, *networkPtr, &player, {x, 2}, 100 + x), nullptr);
    ASSERT_NE(PlaceAndRegister<Road>(map, *networkPtr, &player, {5, 3}, 200), nullptr);
    ASSERT_NE(PlaceAndRegister<Road>(map, *networkPtr, &player, {5, 4}, 201), nullptr);

    map.AutoConnectBuilding(headquarters);
    map.AutoConnectBuilding(woodcutter);
    map.AutoConnectBuilding(lumberMill);
    ASSERT_TRUE(woodcutter->HasReceiver(ResourceType::WOOD));
    ASSERT_TRUE(lumberMill->HasSupplier(ResourceType::WOOD));

    const std::size_t hqWoodBefore =
        headquarters->storage.buffers[ResourceType::WOOD].buffer.size();
    ASSERT_EQ(lumberMill->logistics.RequestResource(ResourceType::WOOD, 3, *lumberMill), 3);

    woodcutter->production.outputBuffers[ResourceType::WOOD].SetStoredAmount(8);
    woodcutter->logistics.DispatchOutputs(*woodcutter, woodcutter->production);
    EXPECT_TRUE(woodcutter->production.outputBuffers[ResourceType::WOOD].buffer.empty())
        << "HQ -> Lumber Mill traffic must not leave a connected Woodcutter stuck at 8/8";
    EXPECT_EQ(woodcutter->transportables.size(), 8u);

    for (int tick = 0; tick < 30; ++tick)
        map.UpdateBuildings(1.1);

    EXPECT_TRUE(woodcutter->transportables.empty());
    EXPECT_EQ(lumberMill->production.inputBuffers[ResourceType::WOOD].buffer.size(), 3u);
    EXPECT_EQ(headquarters->storage.buffers[ResourceType::WOOD].buffer.size(),
              hqWoodBefore - 3u + 8u);
}

TEST(BuildingDomainTests, ProducerPushesResourceImmediatelyWhenProductionCompletes)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player, 10, 6);
    auto network = std::make_unique<RoadNetwork>(map);
    RoadNetwork* networkPtr = network.get();
    player.GetProvinceEconomy()->roadNetwork = std::move(network);

    Vec2i woodAnchor{0, 1};
    Paint(map, woodAnchor, GetBuildingDefinition(BuildingType::Woodcutter).footprint, TileType::WOOD, 10);
    auto* woodcutter = PlaceAndRegister<Woodcutter>(map, *networkPtr, &player, woodAnchor, 1);
    auto* headquarters = PlaceAndRegister<Headquarters>(map, *networkPtr, &player, {5, 1}, 2);
    ASSERT_NE(woodcutter, nullptr);
    ASSERT_NE(headquarters, nullptr);

    ASSERT_NE(PlaceAndRegister<Road>(map, *networkPtr, &player, {2, 2}, 10), nullptr);
    ASSERT_NE(PlaceAndRegister<Road>(map, *networkPtr, &player, {3, 2}, 11), nullptr);
    ASSERT_NE(PlaceAndRegister<Road>(map, *networkPtr, &player, {4, 2}, 12), nullptr);

    woodcutter->SetReceiver(ResourceType::WOOD, nullptr);
    woodcutter->workers.assigned = woodcutter->workers.capacity.GetBase();
    woodcutter->production.started = true;
    woodcutter->production.elapsed = woodcutter->production.GetModifiedCycleTime(*woodcutter);

    woodcutter->production.Produce(*woodcutter, 0.01);

    const int baseWoodOutput = woodcutter->production.products[ResourceType::WOOD];
    const int expectedTransported = woodcutter->production.GetModifiedOutputAmount(*woodcutter, ResourceType::WOOD, baseWoodOutput);
    EXPECT_EQ(woodcutter->transportables.size(), static_cast<size_t>(expectedTransported));
    EXPECT_TRUE(woodcutter->production.outputBuffers[ResourceType::WOOD].buffer.empty());
    EXPECT_TRUE(woodcutter->HasReceiver(ResourceType::WOOD));
}

TEST(BuildingDomainTests, RoadStatsUseConfiguredBaseValues)
{
    Road road{20};
    road.road.maxCapacity = 9;
    road.road.speedModifier = 1.75;
    road.transportTime = 7.0;

    EXPECT_EQ(road.GetModifiedMaxCapacity(), 9);
    EXPECT_DOUBLE_EQ(road.GetModifiedSpeedModifier(), 1.75);
    EXPECT_DOUBLE_EQ(road.GetModifiedTransportTime(), 4.0);
}

// User request (2026-07-20): generic building upgrade system, starting with
// roads. Exercises the whole live path — UpgradeComponent ticking via
// Building::Update, level-up, BalanceModifier application scoped to this one
// road (BalanceModifierScope::BuildingAtPosition) — and the one invariant the
// user explicitly called out: the road must stay fully operational
// (never IsUnderConstruction()) for the entire upgrade.
TEST(BuildingDomainTests, RoadUpgradeProgressesConsumesResourcesAndAppliesModifiers)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);

    auto* storage = dynamic_cast<StorageBuilding*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({2, 2}), &player, std::make_unique<StorageBuilding>(1)));
    ASSERT_NE(storage, nullptr);
    storage->storage.buffers[ResourceType::STONE] = ResourceBuffer{ResourceType::STONE, 20};
    storage->storage.buffers[ResourceType::STONE].SetStoredAmount(20);

    auto* road = dynamic_cast<Road*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({5, 5}), &player, std::make_unique<Road>(2)));
    ASSERT_NE(road, nullptr);
    // Same owner, different position — must NOT be affected by the other
    // road's upgrade (proves the modifier is scoped to one building
    // instance, not the whole player or every Road).
    auto* otherRoad = dynamic_cast<Road*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({6, 5}), &player, std::make_unique<Road>(3)));
    ASSERT_NE(otherRoad, nullptr);
    int otherBaseCapacity = otherRoad->GetModifiedMaxCapacity();
    ASSERT_EQ(road->upgrade.level, 1);
    ASSERT_GE(road->upgrade.maxLevel, 2) << "buildings.rtsdata's Road entry needs at least one 'upgrade level 2 ...' line";
    int baseCapacity = road->GetModifiedMaxCapacity();

    const auto& definition = GetBuildingDefinition(BuildingType::Road);
    auto levelIt = std::find_if(definition.upgradeLevels.begin(), definition.upgradeLevels.end(),
        [](const BuildingUpgradeLevelDefinition& d) { return d.level == 2; });
    ASSERT_NE(levelIt, definition.upgradeLevels.end());
    ASSERT_FALSE(levelIt->cost.empty());
    int stoneBefore = storage->storage.buffers[ResourceType::STONE].buffer.size();

    // Mirrors GameWorld.Commands.cpp's UpgradeBuilding handler exactly (pay,
    // then start), without going through the command-serialization layer.
    ASSERT_TRUE(player.TryPayBuildCost(levelIt->cost));
    EXPECT_LT(storage->storage.buffers[ResourceType::STONE].buffer.size(), stoneBefore) << "cost was not deducted";
    road->upgrade.isUpgrading = true;
    road->upgrade.upgradeRemaining = levelIt->buildTime;

    EXPECT_FALSE(road->IsUnderConstruction());

    // Not enough elapsed time yet — still upgrading, no level change, but the
    // road never stops being a normal, operational road.
    road->Update(levelIt->buildTime * 0.5);
    EXPECT_TRUE(road->upgrade.isUpgrading);
    EXPECT_EQ(road->upgrade.level, 1);
    EXPECT_FALSE(road->IsUnderConstruction());

    // Finishes the remaining time: level up, modifier applied and scoped to
    // THIS road only (BuildingAtPosition), not every Road on the map.
    road->Update(levelIt->buildTime * 0.6);
    EXPECT_FALSE(road->upgrade.isUpgrading);
    EXPECT_EQ(road->upgrade.level, 2);
    EXPECT_FALSE(road->IsUnderConstruction());
    EXPECT_GT(road->GetModifiedMaxCapacity(), baseCapacity);
    EXPECT_EQ(otherRoad->GetModifiedMaxCapacity(), otherBaseCapacity)
        << "the upgrade modifier leaked to a road it was never applied to";
}

// UpgradeComponent persists only `level`/`isUpgrading`/`upgradeRemaining`
// (save v28's UPG block) — the BalanceModifiers it implies are NOT
// serialized, same as tech/focus: GameWorld::LoadFromFile re-derives them via
// Player::ApplyUpgradeLevelModifiers right after reading the UPG tag. This
// confirms that round-trip actually reproduces the live bonus, not just the
// raw level number.
TEST(BuildingDomainTests, SaveAndLoadPreservesRoadUpgradeLevelAndReappliesModifiers)
{
    GameWorld world;
    MapParameters params;
    params.sizePreset = MapSizePreset::S;
    params.aiOpponentCount = 1;
    params.seed = 4242;
    world.InitWorld("test", nullptr, params);

    Player* player = world.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(player, nullptr);
    Road* road = nullptr;
    for (auto* building : player->GetTrackedBuildings())
        if (auto* candidate = dynamic_cast<Road*>(building))
            road = candidate;
    ASSERT_NE(road, nullptr) << "InitWorld should have laid a starting Road (Village<->HQ)";

    // Simulate a completed upgrade to level 2 (same end state UpgradeComponent::Update
    // reaches, without waiting out the real timer).
    road->upgrade.level = 2;
    road->road.SetPriorityResource(ResourceType::IRON_SWORD);
    player->ApplyUpgradeLevelModifiers(*road);
    int expectedCapacity = road->GetModifiedMaxCapacity();
    ASSERT_GT(expectedCapacity, road->road.maxCapacity.GetBase());

    const auto path = (std::filesystem::temp_directory_path() / "rts_road_upgrade_test.save").string();
    ASSERT_TRUE(world.SaveToFile(path));

    GameWorld loaded;
    ASSERT_TRUE(loaded.LoadFromFile(path, nullptr));
    std::filesystem::remove(path);

    Player* loadedPlayer = loaded.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(loadedPlayer, nullptr);
    ASSERT_EQ(loadedPlayer->GetTileMap(), &loaded.GetTileMap());
    Road* loadedRoad = nullptr;
    for (auto* building : loadedPlayer->GetTrackedBuildings())
        if (auto* candidate = dynamic_cast<Road*>(building))
            if (candidate->positionId == road->positionId)
                loadedRoad = candidate;
    ASSERT_NE(loadedRoad, nullptr);

    EXPECT_EQ(loadedRoad->upgrade.level, 2);
    EXPECT_FALSE(loadedRoad->upgrade.isUpgrading);
    EXPECT_EQ(loadedRoad->road.priorityResource, ResourceType::IRON_SWORD);
    EXPECT_EQ(loadedRoad->GetModifiedMaxCapacity(), expectedCapacity);
}

TEST(BuildingDomainTests, ConfiguredBuildingConstructorsApplyRuntimeDefinitions)
{
    Woodcutter woodcutter{1};
    EXPECT_EQ(woodcutter.buildingType, BuildingType::Woodcutter);
    EXPECT_FALSE(woodcutter.production.products.empty());

    HuntersHut hunter{2};
    hunter.InitBuilding(TileType::WOOD);
    EXPECT_EQ(hunter.buildingType, BuildingType::HuntersHut);
    EXPECT_FALSE(hunter.production.consumesTerrain);
    EXPECT_FALSE(hunter.production.products.empty());

    Mine mine{3};
    mine.InitBuilding(TileType::STONE);
    EXPECT_EQ(mine.buildingType, BuildingType::Mine);
    EXPECT_FALSE(mine.production.products.empty());

    LumberMill lumberMill{4};
    Foundry foundry{5};
    Well well{6};
    WheatFarm wheatFarm{7};
    Windmill windmill{8};
    Bakery bakery{9};
    Inn inn{10};
    Paperworks paperworks{11};
    Smith smith{12};
    University university{13};
    EXPECT_EQ(lumberMill.buildingType, BuildingType::LumberMill);
    EXPECT_EQ(foundry.buildingType, BuildingType::Foundry);
    EXPECT_EQ(well.buildingType, BuildingType::Well);
    EXPECT_EQ(wheatFarm.buildingType, BuildingType::WheatFarm);
    EXPECT_EQ(windmill.buildingType, BuildingType::Windmill);
    EXPECT_EQ(bakery.buildingType, BuildingType::Bakery);
    EXPECT_EQ(inn.buildingType, BuildingType::Inn);
    EXPECT_EQ(paperworks.buildingType, BuildingType::Paperworks);
    EXPECT_EQ(smith.buildingType, BuildingType::Smith);
    EXPECT_EQ(university.buildingType, BuildingType::University);

    Headquarters hq{14};
    Village village{15};
    Barracks barracks{19};
    EXPECT_EQ(hq.buildingType, BuildingType::Headquarters);
    EXPECT_FALSE(hq.CanBeManuallyDestroyed());
    EXPECT_EQ(village.buildingType, BuildingType::Village);
    EXPECT_EQ(barracks.buildingType, BuildingType::Barracks);
}

TEST(BuildingDomainTests, VillageGeneratesManpowerAndFoodShortageReducesProductivity)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);

    auto* village = dynamic_cast<Village*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<Village>(40)));
    ASSERT_NE(village, nullptr);
    village->owner = &player;
    village->constructionRemaining = 0.0;
    village->population.manpowerRate = 1.0;
    village->population.populationCap = 10;
    village->population.upkeepInterval = 1.0;
    village->population.foodPackageUpkeep = 1.0;
    village->population.foodBuffer.Clear();
    village->population.foodBuffer = ResourceBuffer{ResourceType::FOOD_PROVISIONS, 3};
    village->population.foodBuffer.SetStoredAmount(1);
    village->population.foodSupplyLevel = 1.0;

    village->Update(1.0);
    EXPECT_TRUE(village->population.hasFood);
    EXPECT_DOUBLE_EQ(player.strategicResources.Get(StrategicResourceType::Manpower), 1.0);
    EXPECT_DOUBLE_EQ(village->activeTime, 1.0);

    village->Update(1.0);
    EXPECT_TRUE(village->population.hasFood);
    EXPECT_NEAR(village->GetFoodSupplyRatio(), 0.67, 0.0001);
    EXPECT_NEAR(village->GetWorkerProductivity(), 0.769, 0.0001);
    EXPECT_NEAR(player.strategicResources.Get(StrategicResourceType::Manpower), 1.67, 0.0001);
    EXPECT_NEAR(village->activeTime, 1.67, 0.0001);
    village->population.foodBuffer.Clear();
}

TEST(BuildingDomainTests, VillageSupplyConsumptionUsesIndependentModifiedIntervalsAtEveryTier)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);

    auto* village = dynamic_cast<Village*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({1, 1}), &player, std::make_unique<Village>(42)));
    ASSERT_NE(village, nullptr);
    village->constructionRemaining = 0.0;
    village->population.SetSettlementLevel(3);
    village->population.upkeepInterval = 60.0;
    village->population.foodBuffer = ResourceBuffer{ResourceType::FOOD_PROVISIONS, 200};
    village->population.householdGoodsBuffer = ResourceBuffer{ResourceType::HOUSEHOLD_GOODS, 200};
    village->population.urbanGoodsBuffer = ResourceBuffer{ResourceType::URBAN_GOODS, 200};
    village->population.foodBuffer.SetStoredAmount(200);
    village->population.householdGoodsBuffer.SetStoredAmount(200);
    village->population.urbanGoodsBuffer.SetStoredAmount(200);

    player.balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::VillageSupplyConsumption, 0.0, 0.9, BalanceModifierScope::Global(),
        BuildingType::Village, ResourceType::FOOD_PROVISIONS, "test:food_conservation"});
    player.balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::VillageSupplyConsumption, 0.0, 0.5, BalanceModifierScope::Global(),
        BuildingType::Village, ResourceType::HOUSEHOLD_GOODS, "test:household_conservation"});
    player.balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::VillageSupplyConsumption, 0.0, 0.0, BalanceModifierScope::Global(),
        BuildingType::Village, ResourceType::URBAN_GOODS, "test:urban_conservation"});

    EXPECT_NEAR(village->population.GetEffectiveSupplyUpkeepInterval(
        *village, ResourceType::FOOD_PROVISIONS), 60.0 / 0.9, 0.0001);
    EXPECT_DOUBLE_EQ(village->population.GetEffectiveSupplyUpkeepInterval(
        *village, ResourceType::HOUSEHOLD_GOODS), 120.0);
    EXPECT_TRUE(std::isinf(village->population.GetEffectiveSupplyUpkeepInterval(
        *village, ResourceType::URBAN_GOODS)));

    // Ten minutes: City pays 10 food per tick at 0.9 cadence (9 payments),
    // 3 household goods at 0.5 cadence (5 payments), and no urban goods at 0.
    for (int second = 0; second < 600; second++)
        village->Update(1.0);

    EXPECT_EQ(village->population.foodBuffer.buffer.size(), 110u);
    EXPECT_EQ(village->population.householdGoodsBuffer.buffer.size(), 185u);
    EXPECT_EQ(village->population.urbanGoodsBuffer.buffer.size(), 200u);
}

TEST(BuildingDomainTests, VillageKeepsOnlyOneFoodProvisionInReserve)
{
    Village village{41};

    EXPECT_EQ(village.population.foodBuffer.bufferSize, 2)
        << "one provision is for the next upkeep and one is the local reserve";
    EXPECT_EQ(village.population.GetFoodDemand(), 2);

    village.population.foodBuffer.SetStoredAmount(1);
    EXPECT_EQ(village.population.GetFoodDemand(), 1);
}

TEST(BuildingDomainTests, VillageRequestsFoodProvisionsFromOwnedStorage)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player, 12, 8);
    player.GetProvinceEconomy()->roadNetwork = std::make_unique<RoadNetwork>(map);

    auto* storage = dynamic_cast<StorageBuilding*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({0, 1}), &player, std::make_unique<StorageBuilding>(1)));
    auto* village = dynamic_cast<Village*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({6, 1}), &player, std::make_unique<Village>(2)));
    ASSERT_NE(storage, nullptr);
    ASSERT_NE(village, nullptr);
    for (int tileId : map.GetBuildingTileIds(storage))
        player.GetRoadNetwork()->UpdateNavMap(tileId, storage);
    for (int tileId : map.GetBuildingTileIds(village))
        player.GetRoadNetwork()->UpdateNavMap(tileId, village);

    auto* roadA = dynamic_cast<Road*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({3, 2}), &player, std::make_unique<Road>(3)));
    auto* roadB = dynamic_cast<Road*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({4, 2}), &player, std::make_unique<Road>(4)));
    auto* roadC = dynamic_cast<Road*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({5, 2}), &player, std::make_unique<Road>(5)));
    ASSERT_NE(roadA, nullptr);
    ASSERT_NE(roadB, nullptr);
    ASSERT_NE(roadC, nullptr);
    player.GetRoadNetwork()->UpdateNavMap(roadA->positionId, roadA);
    player.GetRoadNetwork()->UpdateNavMap(roadB->positionId, roadB);
    player.GetRoadNetwork()->UpdateNavMap(roadC->positionId, roadC);

    storage->storage.buffers.clear();
    storage->storage.buffers[ResourceType::FOOD_PROVISIONS] = ResourceBuffer{ResourceType::FOOD_PROVISIONS, 3};
    storage->storage.buffers[ResourceType::FOOD_PROVISIONS].SetStoredAmount(1);
    village->population.foodBuffer.Clear();
    village->population.foodBuffer = ResourceBuffer{ResourceType::FOOD_PROVISIONS, 3};

    village->RequestFoodSupply();

    ASSERT_EQ(storage->transportables.size(), 1u);
    auto* resource = dynamic_cast<Resource*>(storage->transportables.front());
    ASSERT_NE(resource, nullptr);
    EXPECT_EQ(resource->type, ResourceType::FOOD_PROVISIONS);
    EXPECT_EQ(resource->targetBuilding, village);
    storage->storage.buffers[ResourceType::FOOD_PROVISIONS].Clear();
}

TEST(BuildingDomainTests, ConstructionQueueLimitsActiveBuildersAndTracksPositions)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player, 12, 8);

    auto place = [&](int id, Vec2i at) -> Road*
    {
        auto* road = dynamic_cast<Road*>(
            map.PlaceLoadedBuilding(map.GetIdFromCoords(at), &player, std::make_unique<Road>(id)));
        if (road != nullptr)
        {
            road->buildTime = 10.0;
            road->constructionRemaining = 10.0;
        }
        return road;
    };

    // Placed out of id order to prove the queue orders by id (== placement order).
    Road* second = place(20, {2, 1});
    Road* first = place(10, {4, 1});
    Road* third = place(30, {6, 1});
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(third, nullptr);

    // One builder: only the earliest (lowest-id) building actually progresses.
    player.GetProvinceEconomy()->construction.builders = 1;
    player.GetProvinceEconomy()->construction.Refresh(player);
    EXPECT_EQ(player.GetProvinceEconomy()->construction.QueueLength(), 3);
    EXPECT_EQ(player.GetProvinceEconomy()->construction.QueuePosition(first->id), 1);
    EXPECT_EQ(player.GetProvinceEconomy()->construction.QueuePosition(second->id), 2);
    EXPECT_EQ(player.GetProvinceEconomy()->construction.QueuePosition(third->id), 3);
    EXPECT_TRUE(player.GetProvinceEconomy()->construction.IsActive(first->id));
    EXPECT_FALSE(player.GetProvinceEconomy()->construction.IsActive(second->id));
    EXPECT_TRUE(first->constructionActive);
    EXPECT_FALSE(second->constructionActive);

    // Only the assigned builder advances construction; queued buildings wait.
    first->Update(1.0);
    second->Update(1.0);
    EXPECT_DOUBLE_EQ(first->constructionRemaining, 9.0);
    EXPECT_DOUBLE_EQ(second->constructionRemaining, 10.0);

    // A BuilderAmount buff (tech / focus / national) widens the active window.
    player.balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::BuilderAmount, 1.0, 1.0, BalanceModifierScope::Global(),
        std::nullopt, std::nullopt, "tech:masons"});
    EXPECT_EQ(player.GetProvinceEconomy()->construction.EffectiveBuilders(player), 2);
    player.GetProvinceEconomy()->construction.Refresh(player);
    EXPECT_TRUE(player.GetProvinceEconomy()->construction.IsActive(second->id));
    EXPECT_FALSE(player.GetProvinceEconomy()->construction.IsActive(third->id));

    // Removing the front building (cancel) renumbers the rest of the queue.
    player.UnregisterBuilding(first);
    player.GetProvinceEconomy()->construction.Refresh(player);
    EXPECT_EQ(player.GetProvinceEconomy()->construction.QueuePosition(first->id), 0);
    EXPECT_EQ(player.GetProvinceEconomy()->construction.QueuePosition(second->id), 1);
    EXPECT_EQ(player.GetProvinceEconomy()->construction.QueuePosition(third->id), 2);
}

TEST(BuildingDomainTests, RefundBuildCostReturnsResourcesToStorageAndDropsOverflow)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player);

    auto* storage = dynamic_cast<StorageBuilding*>(
        map.PlaceLoadedBuilding(map.GetIdFromCoords({2, 2}), &player, std::make_unique<StorageBuilding>(1)));
    ASSERT_NE(storage, nullptr);
    storage->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 10};
    storage->storage.buffers[ResourceType::WOOD].SetStoredAmount(2);

    // Cancelling a build hands the paid resources back into owned storage.
    player.RefundBuildCost({{ResourceType::WOOD, 3}});
    EXPECT_EQ(storage->storage.buffers[ResourceType::WOOD].buffer.size(), 5u);

    // Anything past the buffer capacity spills rather than piling up.
    player.RefundBuildCost({{ResourceType::WOOD, 100}});
    EXPECT_EQ(storage->storage.buffers[ResourceType::WOOD].buffer.size(), 10u);

    storage->storage.buffers[ResourceType::WOOD].Clear();
}

// Regression test, updated 2026-07-15 (docs/work_plan_2026-07-13.md, A5 +
// correction): the T3 background-pull mechanism this test originally
// verified (LogisticsComponent::MaintainStorageRequests continuously topping
// up Barracks' own buffer regardless of demand) was removed by design.
// QueueRecruitment now queues the order immediately (tagged "waiting" — see
// RecruitmentQueueEntry::resourcesReady) and requests the shortfall from its
// wired supplier; RecruitmentComponent::Update keeps retrying each tick until
// it physically arrives through the real road network, at which point the
// entry's build timer starts.
TEST(BuildingDomainTests, BarracksRequestsAndReceivesUnitCostsOnDemandThroughRoadNetwork)
{
    TileMap map;
    map.params.sizeX = 10;
    map.params.sizeY = 6;
    map.tilemap.clear();
    map.tilemap.reserve(map.params.sizeX * map.params.sizeY);
    for (int i = 0; i < map.params.sizeX * map.params.sizeY; i++)
        map.tilemap.emplace_back(i);

    Player player{0, map};
    player.strategicResources.Set(StrategicResourceType::Manpower, 100);

    auto* warehouse = dynamic_cast<StorageBuilding*>(player.Build<StorageBuilding>(Vec2i{0, 0}, false));
    auto* barracks = dynamic_cast<Barracks*>(player.Build<Barracks>(Vec2i{5, 0}, false));
    auto* roadA = player.Build<Road>(Vec2i{3, 1}, false);
    auto* roadB = player.Build<Road>(Vec2i{4, 1}, false);
    ASSERT_NE(warehouse, nullptr);
    ASSERT_NE(barracks, nullptr);
    ASSERT_NE(roadA, nullptr);
    ASSERT_NE(roadB, nullptr);

    const UnitDefinition* swordsman = FindUnitDefinition("swordsman");
    ASSERT_NE(swordsman, nullptr);
    for (const auto& cost : swordsman->cost)
        for (int i = 0; i < cost.amount; i++)
            warehouse->storage.buffers[cost.type].GenerateResource(cost.type);

    // No ticks have run yet: nothing has moved toward the Barracks on its
    // own, unlike the removed background poll.
    EXPECT_EQ(barracks->storage.buffers[ResourceType::IRON_SWORD].buffer.size(), 0u);

    ASSERT_TRUE(barracks->recruitment.QueueRecruitment(*barracks, "swordsman"))
        << "manpower alone should be enough to queue the order";
    ASSERT_FALSE(barracks->recruitment.queue.empty());
    EXPECT_FALSE(barracks->recruitment.queue.front().resourcesReady)
        << "nothing is local yet, so the order should start out waiting";

    bool resourcesArrived = false;
    for (int tick = 0; tick < 20 && !resourcesArrived; tick++)
    {
        warehouse->Update(1.0);
        roadA->Update(1.0);
        roadB->Update(1.0);
        barracks->Update(1.0);
        resourcesArrived = !barracks->recruitment.queue.empty() && barracks->recruitment.queue.front().resourcesReady;
    }

    EXPECT_TRUE(resourcesArrived) << "current swordsman costs never arrived at Barracks";
}

// Regression: each advanced unit must have its current definition's storage
// and technology prerequisites satisfied before it can join the queue.
TEST(BuildingDomainTests, BarracksCanRecruitKnightAndRamNotJustSwordsman)
{
    TileMap map;
    Player player{0, map};
    player.strategicResources.Set(StrategicResourceType::Manpower, 100);

    Barracks barracks{1};
    barracks.owner = &player;
    // Recruitment now pulls from the player's GLOBAL storage network
    // (docs/work_plan_2026-07-13.md, 2026-07-14), which is indexed via
    // RegisterBuilding — Player::Build<T> does this implicitly, but a
    // manually-constructed Barracks needs it done explicitly here.
    player.RegisterBuilding(&barracks);
    player.technologies.RestoreTechnology("furnace_and_casting_geometry");
    player.technologies.RestoreTechnology("torsion_engines");

    auto stockCosts = [&](const char* unitId)
    {
        const UnitDefinition* definition = FindUnitDefinition(unitId);
        ASSERT_NE(definition, nullptr);
        for (const auto& cost : definition->cost)
        {
            ASSERT_TRUE(barracks.storage.buffers.contains(cost.type))
                << unitId << " costs " << rt2s(cost.type) << " but Barracks has no buffer for it";
            for (int i = 0; i < cost.amount; i++)
                barracks.storage.buffers[cost.type].GenerateResource(cost.type);
        }
    };

    stockCosts("knight");
    EXPECT_TRUE(barracks.recruitment.QueueRecruitment(barracks, "knight"));

    stockCosts("ram");
    EXPECT_TRUE(barracks.recruitment.QueueRecruitment(barracks, "ram"));
}

// Updated 2026-07-15 (docs/work_plan_2026-07-13.md, A5 correction): Diagnose
// and QueueRecruitment are now DELIBERATELY decoupled — Diagnose stays a
// global feasibility scan (see BuildingComponents.h for why), while
// QueueRecruitment only hard-fails on manpower and otherwise queues the
// order as "waiting" until resources arrive locally. They no longer agree
// on "empty iff recruitable" by design.
TEST(BuildingDomainTests, DiagnoseRecruitmentBlockReportsMissingResourceAndManpower)
{
    TileMap map;
    Player player{0, map};

    Barracks barracks{1};
    barracks.owner = &player;
    // See BarracksCanRecruitKnightAndRamNotJustSwordsman above: global
    // resource feasibility needs the building registered to be visible.
    player.RegisterBuilding(&barracks);

    // No manpower, no resources yet: Diagnose reports both, and
    // QueueRecruitment fails outright — manpower is the one hard gate.
    std::string reason = barracks.recruitment.DiagnoseRecruitmentBlock(barracks, "swordsman");
    EXPECT_FALSE(reason.empty());
    EXPECT_FALSE(barracks.recruitment.QueueRecruitment(barracks, "swordsman"));
    EXPECT_TRUE(barracks.recruitment.queue.empty());

    // Give manpower but still no unit-cost resources anywhere: Diagnose still
    // reports the missing resource, but QueueRecruitment now succeeds anyway
    // — the order joins the queue tagged "waiting for resources" instead of
    // being rejected.
    player.strategicResources.Set(StrategicResourceType::Manpower, 100);
    reason = barracks.recruitment.DiagnoseRecruitmentBlock(barracks, "swordsman");
    EXPECT_FALSE(reason.empty());
    ASSERT_TRUE(barracks.recruitment.QueueRecruitment(barracks, "swordsman"));
    ASSERT_FALSE(barracks.recruitment.queue.empty());
    EXPECT_FALSE(barracks.recruitment.queue.back().resourcesReady);

    // Stock it directly: Diagnose returns empty. The order queued above is
    // still waiting, and strict FIFO (TODO #1, 2026-07-16) means a fresh
    // order must NOT grab the buffer past it — Update()'s readiness pass
    // serves the oldest waiting entry first. Stock the current configured
    // swordsman cost rather than duplicating balance constants here.
    const UnitDefinition* swordsman = FindUnitDefinition("swordsman");
    ASSERT_NE(swordsman, nullptr);
    for (const auto& cost : swordsman->cost)
        for (int i = 0; i < cost.amount; i++)
            barracks.storage.buffers[cost.type].GenerateResource(cost.type);
    EXPECT_TRUE(barracks.recruitment.DiagnoseRecruitmentBlock(barracks, "swordsman").empty());
    ASSERT_TRUE(barracks.recruitment.QueueRecruitment(barracks, "swordsman"));
    EXPECT_FALSE(barracks.recruitment.queue.back().resourcesReady)
        << "strict FIFO: a fresh order waits behind the earlier waiting entry";
    barracks.recruitment.Update(barracks, 0.01);
    EXPECT_TRUE(barracks.recruitment.queue.front().resourcesReady)
        << "the stocked cost must go to the FIRST waiting order";

    // With no earlier waiting entries, a fresh order whose cost is already
    // buffered consumes immediately and starts counting down right away.
    barracks.recruitment.queue.clear();
    for (const auto& cost : swordsman->cost)
        for (int i = 0; i < cost.amount; i++)
            barracks.storage.buffers[cost.type].GenerateResource(cost.type);
    ASSERT_TRUE(barracks.recruitment.QueueRecruitment(barracks, "swordsman"));
    EXPECT_TRUE(barracks.recruitment.queue.back().resourcesReady);
}

TEST(BuildingDomainTests, ReproSwitchingSupplierAwayFromHqFallback)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player, 16, 6);
    auto network = std::make_unique<RoadNetwork>(map);
    RoadNetwork* networkPtr = network.get();
    player.GetProvinceEconomy()->roadNetwork = std::move(network);

    Vec2i woodAnchor{0, 1};
    Paint(map, woodAnchor, GetBuildingDefinition(BuildingType::Woodcutter).footprint, TileType::WOOD, 10);
    auto* headquarters = PlaceAndRegister<Headquarters>(map, *networkPtr, &player, {5, 1}, 1);
    auto* lumberMill = PlaceAndRegister<LumberMill>(map, *networkPtr, &player, {10, 1}, 2);
    auto* woodcutter = PlaceAndRegister<Woodcutter>(map, *networkPtr, &player, woodAnchor, 3);
    ASSERT_NE(headquarters, nullptr);
    ASSERT_NE(lumberMill, nullptr);
    ASSERT_NE(woodcutter, nullptr);
    player.GetProvinceEconomy()->storages.push_back(headquarters);

    // Mirrors GameWorld.Commands.cpp's placement path: each new building
    // auto-connects, so LumberMill's WOOD input should fall back to HQ.
    map.AutoConnectBuilding(headquarters);
    map.AutoConnectBuilding(lumberMill);
    map.AutoConnectBuilding(woodcutter);

    ASSERT_TRUE(lumberMill->HasSupplier(ResourceType::WOOD));
    bool suppliedByHqOnly = true;
    for (const auto& view : lumberMill->GetSupplierViews())
        if (view.type == ResourceType::WOOD && view.building != headquarters)
            suppliedByHqOnly = false;
    EXPECT_TRUE(suppliedByHqOnly) << "sanity check: baseline fallback should be HQ";

    // Mirrors BasicMapViewSystem::RmbReleased: player selects Woodcutter and
    // right-clicks LumberMill to wire it as the real supplier.
    map.ConnectReceiver(woodcutter, lumberMill, false);

    bool stillPullingFromHq = false;
    bool nowSuppliedByWoodcutter = false;
    for (const auto& view : lumberMill->GetSupplierViews())
    {
        if (view.type != ResourceType::WOOD)
            continue;
        if (view.building == headquarters)
            stillPullingFromHq = true;
        if (view.building == woodcutter)
            nowSuppliedByWoodcutter = true;
    }

    EXPECT_TRUE(nowSuppliedByWoodcutter) << "LumberMill should now list Woodcutter as WOOD supplier";
    EXPECT_FALSE(stillPullingFromHq) << "LumberMill should have dropped the HQ fallback supplier";
}

TEST(BuildingDomainTests, AutoConnectUsesHeadquartersInsteadOfPrivateBuildingBuffer)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player, 24, 8);
    auto network = std::make_unique<RoadNetwork>(map);
    RoadNetwork* networkPtr = network.get();
    player.GetProvinceEconomy()->roadNetwork = std::move(network);

    auto* headquarters = PlaceAndRegister<Headquarters>(map, *networkPtr, &player, {0, 1}, 1);
    auto* barracks = PlaceAndRegister<Barracks>(map, *networkPtr, &player, {14, 1}, 2);
    auto* bowyer = dynamic_cast<ConfiguredProductionBuilding*>(map.PlaceLoadedBuilding(
        map.GetIdFromCoords({17, 1}), &player,
        std::make_unique<ConfiguredProductionBuilding>(3, BuildingType::Bowyer)));
    ASSERT_NE(headquarters, nullptr);
    ASSERT_NE(barracks, nullptr);
    ASSERT_NE(bowyer, nullptr);

    map.AutoConnectBuilding(bowyer);

    ASSERT_FALSE(bowyer->GetInputBufferViews().empty());
    for (const auto& input : bowyer->GetInputBufferViews())
    {
        bool suppliedByHeadquarters = false;
        bool suppliedByBarracks = false;
        for (const auto& supplier : bowyer->GetSupplierViews())
        {
            if (supplier.type != input.type)
                continue;
            suppliedByHeadquarters |= supplier.building == headquarters;
            suppliedByBarracks |= supplier.building == barracks;
        }
        EXPECT_TRUE(suppliedByHeadquarters) << "input " << static_cast<int>(input.type);
        EXPECT_FALSE(suppliedByBarracks) << "input " << static_cast<int>(input.type);
    }
}

TEST(BuildingDomainTests, ConsumerStopsPullingFromHqAfterSupplierReassignment)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player, 16, 6);
    auto network = std::make_unique<RoadNetwork>(map);
    RoadNetwork* networkPtr = network.get();
    player.GetProvinceEconomy()->roadNetwork = std::move(network);
    // Production buildings only request inputs once they are staffed
    // (ProductionComponent::Update), and workers come out of manpower.
    player.strategicResources.Set(StrategicResourceType::Manpower, 200);

    auto* headquarters = PlaceAndRegister<Headquarters>(map, *networkPtr, &player, {0, 1}, 1);
    auto* lumberMill = PlaceAndRegister<LumberMill>(map, *networkPtr, &player, {6, 1}, 2);
    auto* woodcutter = PlaceAndRegister<Woodcutter>(map, *networkPtr, &player, {12, 1}, 3);
    ASSERT_NE(headquarters, nullptr);
    ASSERT_NE(lumberMill, nullptr);
    ASSERT_NE(woodcutter, nullptr);
    player.GetProvinceEconomy()->dataTracker.RegisterBuilding(headquarters);
    player.GetProvinceEconomy()->dataTracker.RegisterBuilding(lumberMill);

    for (int x = 3; x <= 5; x++)
    {
        auto* road = PlaceAndRegister<Road>(map, *networkPtr, &player, {x, 2}, 100 + x);
        ASSERT_NE(road, nullptr);
        road->road.maxCapacity.SetBase(16);
    }

    map.AutoConnectBuilding(headquarters);
    map.AutoConnectBuilding(lumberMill);
    ASSERT_TRUE(lumberMill->HasSupplier(ResourceType::WOOD));

    // Baseline: with HQ as the wired supplier, LumberMill's own request pulls
    // WOOD out of it. Delivery takes several ticks to travel the road, same as
    // ProducerWithNoReceiverPushesFullOutputToNearestHeadquarters.
    headquarters->storage.buffers[ResourceType::WOOD].GenerateResource(ResourceType::WOOD);
    for (int i = 0; i < 8; i++)
        map.UpdateBuildings(1.1);
    EXPECT_GT(lumberMill->production.inputBuffers[ResourceType::WOOD].buffer.size(), 0u)
        << "sanity check: a consumer wired to HQ should pull WOOD from it";

    // Player rewires LumberMill's WOOD supplier away from HQ (mirrors
    // BasicMapViewSystem::RmbReleased connecting Woodcutter directly). That
    // strips the warehouse supplier, which is the signature
    // LogisticsComponent::IsRestrictedToDirectSuppliers reads as "the player
    // chose where this comes from" — the warehouse-network fallback in
    // RequestResource must not quietly undo it.
    lumberMill->production.inputBuffers[ResourceType::WOOD].Clear();
    map.ConnectReceiver(woodcutter, lumberMill, false);
    ASSERT_TRUE(lumberMill->logistics.IsRestrictedToDirectSuppliers(ResourceType::WOOD));

    // Keep the direct supplier deliberately dry. A direct request below can
    // then only succeed if the forbidden warehouse fallback is still active.
    woodcutter->production.outputBuffers[ResourceType::WOOD].Clear();

    headquarters->storage.buffers[ResourceType::WOOD].GenerateResource(ResourceType::WOOD);
    const std::size_t shipmentsBefore = networkPtr->GetLiveShipmentCount();
    EXPECT_EQ(lumberMill->logistics.RequestResource(ResourceType::WOOD, 1, *lumberMill), 0)
        << "LumberMill must not pull WOOD from HQ once its supplier was explicitly reassigned";
    EXPECT_EQ(networkPtr->GetLiveShipmentCount(), shipmentsBefore)
        << "a restricted request must not create a shipment from the warehouse network";
}

// User report (2026-07-25): building a StorageBuilding made the whole resource
// system go haywire — the HQ tried to move its entire contents into the new
// depot. Root cause was StorageComponent::Update's ambient push; this pins the
// fixed contract: a new warehouse changes nothing about stock that exists.
TEST(BuildingDomainTests, NewStorageBuildingDoesNotDrainExistingWarehouses)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player, 20, 8);
    auto network = std::make_unique<RoadNetwork>(map);
    RoadNetwork* networkPtr = network.get();
    player.GetProvinceEconomy()->roadNetwork = std::move(network);
    // Production buildings only request inputs once they are staffed
    // (ProductionComponent::Update), and workers come out of manpower.
    player.strategicResources.Set(StrategicResourceType::Manpower, 200);

    auto* headquarters = PlaceAndRegister<Headquarters>(map, *networkPtr, &player, {0, 1}, 1);
    ASSERT_NE(headquarters, nullptr);
    player.GetProvinceEconomy()->dataTracker.RegisterBuilding(headquarters);

    // A Headquarters starts with a stock from buildings.rtsdata; clear it so
    // the assertions below are about exactly the 12 units placed here.
    headquarters->storage.buffers[ResourceType::WOOD].Clear();
    for (int i = 0; i < 12; i++)
        headquarters->storage.buffers[ResourceType::WOOD].GenerateResource(ResourceType::WOOD);
    ASSERT_EQ(StockpileIndex::GetTotal(player, ResourceType::WOOD), 12);

    // A depot near the HQ, fully road-connected — the worst case for the old
    // ambient push, which needed only "accepts WOOD and has room".
    auto* depot = PlaceAndRegister<StorageBuilding>(map, *networkPtr, &player, {8, 1}, 2);
    ASSERT_NE(depot, nullptr);
    player.GetProvinceEconomy()->dataTracker.RegisterBuilding(depot);
    for (int x = 3; x <= 7; x++)
    {
        auto* road = PlaceAndRegister<Road>(map, *networkPtr, &player, {x, 2}, 100 + x);
        ASSERT_NE(road, nullptr);
        road->road.maxCapacity.SetBase(16);
    }
    map.AutoConnectBuilding(depot);
    ASSERT_FALSE(networkPtr->CalculatePath(headquarters, depot).empty())
        << "sanity check: the warehouses must be road-connected for this to be a real test";

    for (int i = 0; i < 40; i++)
        map.UpdateBuildings(1.1);

    EXPECT_EQ(headquarters->storage.buffers[ResourceType::WOOD].buffer.size(), 12u)
        << "HQ must keep its own stock when a new warehouse appears";
    EXPECT_TRUE(depot->storage.buffers[ResourceType::WOOD].buffer.empty())
        << "a new warehouse starts empty and fills only from what producers deliver to it";
    EXPECT_EQ(StockpileIndex::GetTotal(player, ResourceType::WOOD), 12)
        << "nothing may be created or lost by adding a warehouse";
}

// The other half of the same contract: passive warehouses must not strand
// stock. A consumer wired to an empty HQ still gets served from the depot that
// actually holds the goods, as long as a road connects them.
TEST(BuildingDomainTests, ConsumerPullsFromUnwiredWarehouseThatHoldsTheStock)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player, 24, 8);
    auto network = std::make_unique<RoadNetwork>(map);
    RoadNetwork* networkPtr = network.get();
    player.GetProvinceEconomy()->roadNetwork = std::move(network);
    // Production buildings only request inputs once they are staffed
    // (ProductionComponent::Update), and workers come out of manpower.
    player.strategicResources.Set(StrategicResourceType::Manpower, 200);

    auto* headquarters = PlaceAndRegister<Headquarters>(map, *networkPtr, &player, {0, 1}, 1);
    auto* lumberMill = PlaceAndRegister<LumberMill>(map, *networkPtr, &player, {6, 1}, 2);
    auto* depot = PlaceAndRegister<StorageBuilding>(map, *networkPtr, &player, {12, 1}, 3);
    ASSERT_NE(headquarters, nullptr);
    ASSERT_NE(lumberMill, nullptr);
    ASSERT_NE(depot, nullptr);
    player.GetProvinceEconomy()->dataTracker.RegisterBuilding(headquarters);
    player.GetProvinceEconomy()->dataTracker.RegisterBuilding(lumberMill);
    player.GetProvinceEconomy()->dataTracker.RegisterBuilding(depot);

    // All three buildings are 3x3 (assets/data/buildings.rtsdata), so the
    // road line skips x:[6,8] — that span is the LumberMill's own footprint.
    for (int x : {3, 4, 5, 9, 10, 11})
    {
        auto* road = PlaceAndRegister<Road>(map, *networkPtr, &player, {x, 2}, 100 + x);
        ASSERT_NE(road, nullptr);
        road->road.maxCapacity.SetBase(16);
    }

    map.AutoConnectBuilding(headquarters);
    map.AutoConnectBuilding(lumberMill);

    // All the wood is in the depot; HQ (the wired supplier) is emptied of its
    // buildings.rtsdata starting stock so it has nothing to serve.
    headquarters->storage.buffers[ResourceType::WOOD].Clear();
    for (int i = 0; i < 4; i++)
        depot->storage.buffers[ResourceType::WOOD].GenerateResource(ResourceType::WOOD);
    ASSERT_TRUE(headquarters->storage.buffers[ResourceType::WOOD].buffer.empty());

    auto sources = StockpileIndex::RankSourcesFor(ResourceType::WOOD, *lumberMill);
    ASSERT_EQ(sources.size(), 1u) << "only the depot holds WOOD, so only it is a source";
    EXPECT_EQ(sources.front(), depot);

    // Sampled per tick rather than asserted at the end: LumberMill consumes
    // WOOD as fast as it arrives, so a single check afterwards can miss the
    // delivery entirely (same reason as
    // BarracksRequestsAndReceivesUnitCostsOnDemandThroughRoadNetwork).
    bool woodArrived = false;
    for (int i = 0; i < 20 && !woodArrived; i++)
    {
        map.UpdateBuildings(1.1);
        woodArrived = !lumberMill->production.inputBuffers[ResourceType::WOOD].buffer.empty();
    }

    EXPECT_TRUE(woodArrived)
        << "a consumer must reach stock held by a warehouse it is not wired to";
    EXPECT_LT(depot->storage.buffers[ResourceType::WOOD].buffer.size(), 4u)
        << "the depot is where the wood actually came from";
}

// Road connectivity is a hard requirement, not a ranking preference: a
// warehouse with no path to the requester can never deliver, so it must not
// appear as a source at all.
TEST(BuildingDomainTests, RankSourcesForSkipsWarehousesWithNoRoadPath)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player, 24, 8);
    auto network = std::make_unique<RoadNetwork>(map);
    RoadNetwork* networkPtr = network.get();
    player.GetProvinceEconomy()->roadNetwork = std::move(network);
    // Production buildings only request inputs once they are staffed
    // (ProductionComponent::Update), and workers come out of manpower.
    player.strategicResources.Set(StrategicResourceType::Manpower, 200);

    auto* headquarters = PlaceAndRegister<Headquarters>(map, *networkPtr, &player, {0, 1}, 1);
    auto* lumberMill = PlaceAndRegister<LumberMill>(map, *networkPtr, &player, {6, 1}, 2);
    auto* strandedDepot = PlaceAndRegister<StorageBuilding>(map, *networkPtr, &player, {18, 5}, 3);
    ASSERT_NE(headquarters, nullptr);
    ASSERT_NE(lumberMill, nullptr);
    ASSERT_NE(strandedDepot, nullptr);
    player.GetProvinceEconomy()->dataTracker.RegisterBuilding(headquarters);
    player.GetProvinceEconomy()->dataTracker.RegisterBuilding(lumberMill);
    player.GetProvinceEconomy()->dataTracker.RegisterBuilding(strandedDepot);

    // Roads reach the HQ only; the far depot has none.
    for (int x = 3; x <= 5; x++)
    {
        auto* road = PlaceAndRegister<Road>(map, *networkPtr, &player, {x, 2}, 100 + x);
        ASSERT_NE(road, nullptr);
        road->road.maxCapacity.SetBase(16);
    }

    headquarters->storage.buffers[ResourceType::WOOD].Clear();
    for (int i = 0; i < 4; i++)
    {
        headquarters->storage.buffers[ResourceType::WOOD].GenerateResource(ResourceType::WOOD);
        strandedDepot->storage.buffers[ResourceType::WOOD].GenerateResource(ResourceType::WOOD);
    }

    // Both hold WOOD and both count as stock the player owns...
    EXPECT_EQ(StockpileIndex::GetTotal(player, ResourceType::WOOD), 8);
    EXPECT_EQ(StockpileIndex::GetHoldings(player, ResourceType::WOOD).size(), 2u);

    // ...but only the connected one can actually serve a request.
    auto sources = StockpileIndex::RankSourcesFor(ResourceType::WOOD, *lumberMill);
    ASSERT_EQ(sources.size(), 1u);
    EXPECT_EQ(sources.front(), headquarters);
}

// StockpileIndex is the single answer to "how much do I have", and it counts
// warehouses only — a Barracks' queued unit costs are that building's own
// consumption buffer, not stock anything else can spend.
TEST(BuildingDomainTests, StockpileIndexCountsWarehousesOnly)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player, 20, 8);
    auto network = std::make_unique<RoadNetwork>(map);
    RoadNetwork* networkPtr = network.get();
    player.GetProvinceEconomy()->roadNetwork = std::move(network);
    // Production buildings only request inputs once they are staffed
    // (ProductionComponent::Update), and workers come out of manpower.
    player.strategicResources.Set(StrategicResourceType::Manpower, 200);

    auto* headquarters = PlaceAndRegister<Headquarters>(map, *networkPtr, &player, {0, 1}, 1);
    auto* depot = PlaceAndRegister<StorageBuilding>(map, *networkPtr, &player, {6, 1}, 2);
    auto* barracks = PlaceAndRegister<Barracks>(map, *networkPtr, &player, {12, 1}, 3);
    ASSERT_NE(headquarters, nullptr);
    ASSERT_NE(depot, nullptr);
    ASSERT_NE(barracks, nullptr);

    // The configured HQ may start with arrows (currently 40). This test owns
    // its fixture amounts, so clear both warehouse buffers before adding the
    // exact warehouse holdings asserted below.
    headquarters->storage.buffers[ResourceType::ARROWS].Clear();
    depot->storage.buffers[ResourceType::ARROWS].Clear();
    for (int i = 0; i < 3; i++)
        headquarters->storage.buffers[ResourceType::ARROWS].GenerateResource(ResourceType::ARROWS);
    for (int i = 0; i < 2; i++)
        depot->storage.buffers[ResourceType::ARROWS].GenerateResource(ResourceType::ARROWS);
    EXPECT_EQ(StockpileIndex::GetTotal(player, ResourceType::ARROWS), 5)
        << "only warehouse arrows count as shared stock";

    auto holdings = StockpileIndex::GetHoldings(player, ResourceType::ARROWS);
    ASSERT_EQ(holdings.size(), 2u) << "holdings name the warehouses, by building id";
    EXPECT_EQ(holdings[0].buildingId, headquarters->id);
    EXPECT_EQ(holdings[0].amount, 3);
    EXPECT_EQ(holdings[1].buildingId, depot->id);
    EXPECT_EQ(holdings[1].amount, 2);

    EXPECT_FALSE(StockpileIndex::IsWarehouse(barracks));
    EXPECT_TRUE(StockpileIndex::IsWarehouse(headquarters));
    EXPECT_TRUE(StockpileIndex::IsWarehouse(depot));
}
