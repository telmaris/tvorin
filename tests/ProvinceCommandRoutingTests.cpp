#include "core/GameWorld.h"
#include "core/GameSession.h"

#include <gtest/gtest.h>

TEST(ProvinceCommandRoutingTests, EqualTileIdsStayIsolatedAcrossPlayerProvinces)
{
    MapParameters parameters;
    parameters.sizeX = 101;
    parameters.sizeY = 101;
    parameters.seed = 4401;
    parameters.debugMode = true;

    GameWorld world;
    ASSERT_TRUE(world.InitMultiplayerWorld(
        "province-command-routing", nullptr, parameters, 0, true));

    const ProvinceId firstHome = world.GetPlayerProvinceId(0);
    const ProvinceId secondHome = world.GetPlayerProvinceId(1);
    ASSERT_NE(firstHome, InvalidProvinceId);
    ASSERT_NE(secondHome, InvalidProvinceId);
    ASSERT_NE(firstHome, secondHome);
    ASSERT_NE(world.GetTileMapForPlayer(0), world.GetTileMapForPlayer(1));

    Player* firstPlayer = world.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(firstPlayer, nullptr);
    const std::size_t buildingsBefore = firstPlayer->GetTrackedBuildings().size();
    const std::uint64_t commandId = world.SubmitCommand(
        GameCommand::BuildBuilding(0, secondHome, BuildingType::Road, {1, 1}));

    world.UpdateSimulation(FixedSimulationClock::FixedDt);
    const auto results = world.ConsumeCommandResults();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results.front().commandId, commandId);
    EXPECT_FALSE(results.front().accepted);
    EXPECT_EQ(firstPlayer->GetTrackedBuildings().size(), buildingsBefore);
}

TEST(ProvinceCommandRoutingTests, GarrisonCommandsResolveTheirExplicitProvince)
{
    MapParameters parameters;
    parameters.sizeX = 101;
    parameters.sizeY = 101;
    parameters.seed = 4403;
    parameters.debugMode = true;

    GameWorld world;
    ASSERT_TRUE(world.InitMultiplayerWorld(
        "garrison-command-routing", nullptr, parameters, 0, true));

    Player* player = world.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(player, nullptr);
    const ProvinceId provinceId = player->homeProvinceId;
    TileMap* map = player->GetTileMap(provinceId);
    ASSERT_NE(map, nullptr);

    auto* barracks = dynamic_cast<Barracks*>(map->PlaceLoadedBuilding(
        map->GetIdFromCoords({0, 0}), player, std::make_unique<Barracks>(900001)));
    auto* tower = dynamic_cast<GuardTower*>(map->PlaceLoadedBuilding(
        map->GetIdFromCoords({10, 0}), player, std::make_unique<GuardTower>(900002)));
    ASSERT_NE(barracks, nullptr);
    ASSERT_NE(tower, nullptr);

    BattleUnit unit{900003, player->id, "militia"};
    ASSERT_TRUE(UnitAssignmentService::AssignReserve(unit, provinceId, barracks->id));
    player->roster.AddUnit(std::move(unit));

    const std::uint64_t commandId = world.SubmitCommand(
        GameCommand::AssignUnitsToGarrison(
            player->id, provinceId, barracks->id, tower->id, {900003}));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    const auto results = world.ConsumeCommandResults();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results.front().commandId, commandId);
    EXPECT_TRUE(results.front().accepted);
    ASSERT_NE(player->roster.FindUnit(900003), nullptr);
    EXPECT_EQ(player->roster.FindUnit(900003)->assignment.kind,
              UnitAssignmentKind::DefensiveGarrison);
    EXPECT_EQ(player->roster.FindUnit(900003)->assignment.provinceId, provinceId);
    EXPECT_EQ(player->roster.FindUnit(900003)->assignment.buildingId, tower->id);
}

TEST(ProvinceCommandRoutingTests, TaskGroupCreationResolvesStableBarracksIdNotTileIndex)
{
    MapParameters parameters;
    parameters.sizeX = 101;
    parameters.sizeY = 101;
    parameters.seed = 4404;
    parameters.debugMode = true;

    GameWorld world;
    ASSERT_TRUE(world.InitMultiplayerWorld(
        "task-group-command-routing", nullptr, parameters, 0, true));
    Player* player = world.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(player, nullptr);
    const ProvinceId provinceId = player->homeProvinceId;
    TileMap* map = player->GetTileMap(provinceId);
    ASSERT_NE(map, nullptr);

    constexpr int BarracksBuildingId = 9'000'001;
    auto* barracks = dynamic_cast<Barracks*>(map->PlaceLoadedBuilding(
        map->GetIdFromCoords({0, 0}), player,
        std::make_unique<Barracks>(BarracksBuildingId)));
    ASSERT_NE(barracks, nullptr);
    ASSERT_GE(BarracksBuildingId, static_cast<int>(map->tilemap.size()));

    const std::uint64_t commandId = world.SubmitCommand(
        GameCommand::CreateTaskGroup(player->id, provinceId, BarracksBuildingId));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    const auto results = world.ConsumeCommandResults();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results.front().commandId, commandId);
    EXPECT_TRUE(results.front().accepted);
    ASSERT_EQ(player->taskGroups.GetGroups().size(), 1u);
    EXPECT_EQ(player->taskGroups.GetGroups().begin()->second.homeBarracksBuildingId,
              BarracksBuildingId);
}

TEST(ProvinceCommandRoutingTests, LocalModifierScopeIncludesProvinceIdentity)
{
    ProvinceSimulation first{101, 0};
    ProvinceSimulation second{202, 0};
    Player player{0, first};
    player.BindProvince(second);

    auto initialize = [&](ProvinceSimulation& province)
    {
        TileMap& map = province.GetTileMap();
        map.params.sizeX = 20;
        map.params.sizeY = 20;
        for (int tileId = 0; tileId < 400; ++tileId)
        {
            map.tilemap.emplace_back(tileId);
            map.tilemap.back().tileType = TileType::GRASS;
            map.tilemap.back().owner = &player;
            map.tilemap.back().ownerId = player.id;
        }
        return dynamic_cast<Road*>(map.PlaceLoadedBuilding(
            map.GetIdFromCoords({3, 3}), &player, std::make_unique<Road>(77)));
    };

    Road* firstRoad = initialize(first);
    Road* secondRoad = initialize(second);
    ASSERT_NE(firstRoad, nullptr);
    ASSERT_NE(secondRoad, nullptr);

    player.balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::RoadCapacity, 5.0, 1.0,
        BalanceModifierScope::Building(first.GetProvinceId(), firstRoad->id),
        BuildingType::Road, std::nullopt, "test:province-local"});

    EXPECT_EQ(player.ModifyBalanceIntForBuilding(
                  BalanceStat::RoadCapacity, 10, firstRoad),
              15);
    EXPECT_EQ(player.ModifyBalanceIntForBuilding(
                  BalanceStat::RoadCapacity, 10, secondRoad),
              10);
}

TEST(CampaignMultiplayerTests, BothOwnedProvincesTickAndSnapshotsUseLocalHome)
{
    MapParameters parameters;
    parameters.sizeX = 101;
    parameters.sizeY = 101;
    parameters.seed = 4402;

    GameWorld host;
    GameWorld client;
    ASSERT_TRUE(host.InitMultiplayerWorld(
        "campaign-host", nullptr, parameters, 0, true));
    ASSERT_TRUE(client.InitMultiplayerWorld(
        "campaign-client", nullptr, parameters, 1, false));

    ASSERT_NE(host.GetTileMapForPlayer(0), host.GetTileMapForPlayer(1));
    EXPECT_NE(host.GetPlayerProvinceId(0), host.GetPlayerProvinceId(1));
    EXPECT_TRUE(host.GetGlobalMap().FindBuildableProvince(host.GetPlayerProvinceId(0))->GetSimulation() != nullptr);
    EXPECT_TRUE(host.GetGlobalMap().FindBuildableProvince(host.GetPlayerProvinceId(1))->GetSimulation() != nullptr);

    for (int tick = 0; tick < 5; ++tick)
        host.UpdateSimulation(FixedSimulationClock::FixedDt);

    for (ProvinceId provinceId : host.GetGlobalMap().GetProvinceIds())
    {
        const auto* province = host.GetGlobalMap().FindBuildableProvince(provinceId);
        if (province != nullptr && province->GetSimulation() != nullptr)
            EXPECT_EQ(province->GetSimulation()->GetEconomy().simulationTick, 5u);
    }

    const GameSnapshot hostSnapshot = host.BuildSnapshot();
    const GameSnapshot clientSnapshot = client.BuildSnapshot();
    EXPECT_EQ(hostSnapshot.activeProvinceId, host.GetPlayerProvinceId(0));
    EXPECT_EQ(clientSnapshot.activeProvinceId, client.GetPlayerProvinceId(1));
    EXPECT_NE(hostSnapshot.activeProvinceId, clientSnapshot.activeProvinceId);
}
