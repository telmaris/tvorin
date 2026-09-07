#include "core/GameWorld.h"
#include "core/GameSession.h"

#include <gtest/gtest.h>

TEST(CampaignPersistenceTests, ActiveExpeditionAndBothLocalMapsRoundTrip)
{
    MapParameters parameters;
    parameters.sizeX = 101;
    parameters.sizeY = 101;
    parameters.seed = 6601;

    GameWorld host;
    ASSERT_TRUE(host.InitMultiplayerWorld(
        "campaign-persistence", nullptr, parameters, 0, true));
    Player* player = host.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(player, nullptr);
    const ProvinceId homeId = player->homeProvinceId;
    const ProvinceId targetId = host.GetGlobalMap().GetNeighbors(homeId).front();
    BattleUnit scout(1, 0, "scout");
    ASSERT_TRUE(UnitAssignmentService::AssignReserve(scout, homeId, 1));
    player->roster.AddUnit(std::move(scout));
    player->nextUnitInstanceId = 2;
    player->strategicResources.Set(StrategicResourceType::SupplyPackages, 1.0);
    host.SubmitCommand(GameCommand::StartScoutExpedition(0, homeId, targetId, {1}));
    host.UpdateSimulation(FixedSimulationClock::FixedDt);
    ASSERT_TRUE(host.ConsumeCommandResults().front().accepted);

    const std::string payload = host.SerializeSimulationState();
    ASSERT_FALSE(payload.empty());
    GameWorld restored;
    ASSERT_TRUE(restored.InitMultiplayerWorld(
        "campaign-persistence-client", nullptr, parameters, 1, false));
    ASSERT_TRUE(restored.RestoreSimulationState(payload, 1));

    EXPECT_EQ(restored.BuildChecksum(), host.BuildChecksum());
    EXPECT_EQ(restored.GetLocalProvinceId(), restored.GetPlayerProvinceId(1));
    ASSERT_EQ(restored.GetArmyJourneySystem().GetJourneys().size(), 1u);
    const auto& restoredJourney = restored.GetArmyJourneySystem().GetJourneys().begin()->second;
    EXPECT_EQ(restoredJourney.targetProvinceId, targetId);
    EXPECT_EQ(restoredJourney.status, WorldJourneyStatus::InTransit);
    EXPECT_NE(restored.GetPlayerHandler().players.at(0)->roster.FindUnit(1), nullptr);
    EXPECT_NE(restored.GetTileMapForPlayer(0), restored.GetTileMapForPlayer(1));
}

TEST(CampaignPersistenceTests, ProvinceEconomiesKeepOnePlayersLocalRegistriesSeparate)
{
    TileMap firstMap;
    TileMap secondMap;
    const auto fill = [](TileMap& map)
    {
        map.params.sizeX = 8;
        map.params.sizeY = 8;
        map.tilemap.clear();
        map.tilemap.reserve(64);
        for (int id = 0; id < 64; ++id)
        {
            Tile tile{id};
            tile.tileType = TileType::GRASS;
            map.tilemap.push_back(std::move(tile));
        }
    };
    fill(firstMap);
    fill(secondMap);

    Player player{0, firstMap};
    ProvinceSimulation secondProvince{2, 0};
    secondProvince.BindExternalTileMap(secondMap);
    secondProvince.BindPlayer(player);

    Building* firstStorage = firstMap.PlaceLoadedBuilding(
        firstMap.GetIdFromCoords({0, 0}), &player, std::make_unique<StorageBuilding>(1));
    Building* secondStorage = secondMap.PlaceLoadedBuilding(
        secondMap.GetIdFromCoords({0, 0}), &player, std::make_unique<StorageBuilding>(2));
    ASSERT_NE(firstStorage, nullptr);
    ASSERT_NE(secondStorage, nullptr);

    ProvinceEconomy* firstEconomy = firstStorage->provinceEconomy;
    ProvinceEconomy* secondEconomy = secondStorage->provinceEconomy;
    ASSERT_NE(firstEconomy, nullptr);
    ASSERT_NE(secondEconomy, nullptr);
    ASSERT_NE(firstEconomy, secondEconomy);
    EXPECT_EQ(firstEconomy->provinceId, 1u);
    EXPECT_EQ(secondEconomy->provinceId, 2u);
    EXPECT_EQ(firstEconomy->dataTracker.buildings.size(), 1u);
    EXPECT_EQ(secondEconomy->dataTracker.buildings.size(), 1u);
    EXPECT_NE(firstEconomy->roadNetwork.get(), secondEconomy->roadNetwork.get());

    firstMap.DestroyBuildingAt(firstStorage->positionId);
    EXPECT_TRUE(firstEconomy->dataTracker.buildings.empty());
    EXPECT_EQ(secondEconomy->dataTracker.buildings.size(), 1u);
    EXPECT_EQ(secondMap.GetBuilding(secondStorage->positionId), secondStorage);
}

TEST(CampaignPersistenceTests, ActiveJourneyAndRaidRuntimeRoundTrip)
{
    MapParameters parameters;
    parameters.sizeX = 101;
    parameters.sizeY = 101;
    parameters.seed = 6602;

    GameWorld world;
    ASSERT_TRUE(world.InitMultiplayerWorld(
        "runtime-persistence", nullptr, parameters, 0, true));
    Player* player = world.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(player, nullptr);
    const ProvinceId homeId = player->homeProvinceId;
    const ProvinceId targetId = world.GetGlobalMap().GetNeighbors(homeId).front();
    const ProvinceConnectionId connectionId = world.GetGlobalMap()
        .FindConnection(homeId, targetId)->GetId();

    WorldJourney journey;
    journey.ownerId = player->id;
    journey.sourceProvinceId = homeId;
    journey.targetProvinceId = targetId;
    journey.legPlan = {{connectionId}};
    journey.payload = Colonists{3};
    const auto journeyStart = world.GetArmyJourneySystem().Start(
        journey, world.GetGlobalMap(), world.GetSimulationTick(),
        WorldJourneyRules{25});
    ASSERT_TRUE(journeyStart) << journeyStart.failureReason;

    std::string failure;

    BattleId raidId = InvalidBattleId;
    ASSERT_TRUE(world.GetBattleSystem().StartRaid(
        *player, homeId, 5, world.GetGlobalMap(), world.GetSimulationTick(),
        world.GetGlobalMap().GetGenerationSeed(), raidId, failure)) << failure;

    WorldEventNotificationView raidNotification;
    raidNotification.instanceId = 9001;
    raidNotification.ownerId = player->id;
    raidNotification.provinceId = homeId;
    raidNotification.definitionId = "bandit_raid";
    raidNotification.appliedEffects.push_back(
        {AppliedWorldEventEffectKind::RaidStarted, ResourceType::Null, 5});
    world.GetEventSystem().GetFeed().Push(std::move(raidNotification));

    const auto payload = world.SerializeSimulationState();
    ASSERT_FALSE(payload.empty());
    GameWorld restored;
    ASSERT_TRUE(restored.RestoreSimulationState(payload));
    EXPECT_EQ(restored.BuildChecksum(), world.BuildChecksum());
    ASSERT_EQ(restored.GetArmyJourneySystem().GetJourneys().size(), 1u);
    EXPECT_EQ(restored.GetArmyJourneySystem().GetJourneys().begin()->second.targetProvinceId, targetId);
    ASSERT_EQ(restored.GetBattleSystem().GetBattles().size(), 1u);
    EXPECT_TRUE(restored.GetBattleSystem().GetBattles().begin()->second.isRaid);
    const auto& restoredFeed = restored.GetEventSystem().GetFeed().GetHistory();
    ASSERT_EQ(restoredFeed.size(), 1u);
    ASSERT_EQ(restoredFeed.front().appliedEffects.size(), 1u);
    EXPECT_EQ(restoredFeed.front().appliedEffects.front().kind,
              AppliedWorldEventEffectKind::RaidStarted);
}

TEST(CampaignPersistenceTests, InFlightRoadShipmentRoundTripsAtExactProgress)
{
    MapParameters parameters;
    parameters.sizeX = 101;
    parameters.sizeY = 101;
    parameters.seed = 6603;

    GameWorld world;
    ASSERT_TRUE(world.InitMultiplayerWorld(
        "shipment-persistence", nullptr, parameters, 0, true));
    Player* player = world.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(player, nullptr);
    const ProvinceId provinceId = player->homeProvinceId;
    ProvinceEconomy* economy = player->GetProvinceEconomy(provinceId);
    ASSERT_NE(economy, nullptr);
    ASSERT_NE(economy->tilemap, nullptr);
    ASSERT_NE(economy->roadNetwork, nullptr);
    TileMap& map = *economy->tilemap;

    const auto placeAndRegister = [&](std::unique_ptr<Building> building, Vec2i position)
    {
        Building* placed = map.PlaceLoadedBuilding(
            map.GetIdFromCoords(position), player, std::move(building));
        if (placed != nullptr)
            for (int tileId : map.GetBuildingTileIds(placed))
                economy->roadNetwork->UpdateNavMap(tileId, placed);
        return placed;
    };

    auto* source = dynamic_cast<StorageBuilding*>(placeAndRegister(
        std::make_unique<StorageBuilding>(200001), {0, 1}));
    auto* target = dynamic_cast<StorageBuilding*>(placeAndRegister(
        std::make_unique<StorageBuilding>(200002), {7, 1}));
    ASSERT_NE(source, nullptr);
    ASSERT_NE(target, nullptr);
    ASSERT_NE(placeAndRegister(std::make_unique<Road>(200003), {3, 2}), nullptr);
    ASSERT_NE(placeAndRegister(std::make_unique<Road>(200004), {4, 2}), nullptr);
    ASSERT_NE(placeAndRegister(std::make_unique<Road>(200005), {5, 2}), nullptr);
    ASSERT_NE(placeAndRegister(std::make_unique<Road>(200006), {6, 2}), nullptr);

    source->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 2};
    target->storage.buffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 2};
    source->storage.buffers[ResourceType::WOOD].GenerateResource(ResourceType::WOOD);
    auto [available, resource] = source->storage.buffers[ResourceType::WOOD].GetResource();
    ASSERT_TRUE(available);
    ASSERT_NE(resource, nullptr);
    ASSERT_TRUE(player->BeginTransport(source, target, resource));

    for (int tick = 0; tick < 3; ++tick)
        world.UpdateSimulation(FixedSimulationClock::FixedDt);

    std::vector<ResourceShipment> before;
    economy->roadNetwork->AppendShipmentRecords(before);
    const auto beforeIt = std::find_if(before.begin(), before.end(), [&](const auto& shipment)
    {
        return shipment.sourceBuildingId == source->id &&
               shipment.targetBuildingId == target->id;
    });
    ASSERT_NE(beforeIt, before.end());
    ASSERT_GT(beforeIt->elapsedTime, 0.0);
    const ResourceShipment expected = *beforeIt;
    const std::uint64_t checksumBefore = world.BuildChecksum();

    const std::string payload = world.SerializeSimulationState();
    ASSERT_FALSE(payload.empty());
    GameWorld restored;
    ASSERT_TRUE(restored.RestoreSimulationState(payload));
    EXPECT_EQ(restored.BuildChecksum(), checksumBefore);

    Player* restoredPlayer = restored.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(restoredPlayer, nullptr);
    ProvinceEconomy* restoredEconomy = restoredPlayer->GetProvinceEconomy(provinceId);
    ASSERT_NE(restoredEconomy, nullptr);
    ASSERT_NE(restoredEconomy->roadNetwork, nullptr);
    std::vector<ResourceShipment> after;
    restoredEconomy->roadNetwork->AppendShipmentRecords(after);
    EXPECT_EQ(after.size(), before.size());
    const auto afterIt = std::find_if(after.begin(), after.end(), [&](const auto& shipment)
    {
        return shipment.id == expected.id;
    });
    ASSERT_NE(afterIt, after.end());
    EXPECT_EQ(afterIt->type, expected.type);
    EXPECT_EQ(afterIt->sourceBuildingId, expected.sourceBuildingId);
    EXPECT_EQ(afterIt->targetBuildingId, expected.targetBuildingId);
    EXPECT_EQ(afterIt->pathTileIds, expected.pathTileIds);
    EXPECT_EQ(afterIt->currentPathStep, expected.currentPathStep);
    EXPECT_DOUBLE_EQ(afterIt->elapsedTime, expected.elapsedTime);
    EXPECT_DOUBLE_EQ(afterIt->transportTime, expected.transportTime);
}

TEST(CampaignPersistenceTests, PeriodicSchedulerRoundTripsItsAuthorityState)
{
    MapParameters parameters;
    parameters.sizeX = 101;
    parameters.sizeY = 101;
    parameters.seed = 6604;

    GameWorld world;
    ASSERT_TRUE(world.InitMultiplayerWorld(
        "scheduler-persistence", nullptr, parameters, 0, true));

    PeriodicEventSchedulerState schedulerState;
    schedulerState.nextCheckTick = 12345;
    schedulerState.nextAllowedEventTick = 14567;
    schedulerState.attemptCounter = 17;
    schedulerState.definitionCooldownUntil.emplace("frontier_harvest", 16789);
    ASSERT_TRUE(world.GetEventSystem().RestorePeriodicSchedulerState(0,
                                                                      schedulerState));

    const std::string payload = world.SerializeSimulationState();
    ASSERT_FALSE(payload.empty());
    GameWorld restored;
    ASSERT_TRUE(restored.RestoreSimulationState(payload));

    EXPECT_EQ(restored.BuildChecksum(), world.BuildChecksum());
    const auto& states = restored.GetEventSystem().GetPeriodicScheduler().GetStates();
    ASSERT_EQ(states.size(), 1u);
    const auto& restoredState = states.at(0);
    EXPECT_EQ(restoredState.nextCheckTick, schedulerState.nextCheckTick);
    EXPECT_EQ(restoredState.nextAllowedEventTick, schedulerState.nextAllowedEventTick);
    EXPECT_EQ(restoredState.attemptCounter, schedulerState.attemptCounter);
    EXPECT_EQ(restoredState.definitionCooldownUntil,
              schedulerState.definitionCooldownUntil);
}
