#include "core/GameWorld.h"
#include "core/GameSession.h"
#include "economy/StockpileIndex.h"

#include <gtest/gtest.h>

#include <algorithm>

TEST(ScoutExpeditionTests, ScoutCommandConsumesSupplyAndCompletesOnFixedTick)
{
    MapParameters parameters;
    parameters.sizeX = 101;
    parameters.sizeY = 101;
    parameters.seed = 5501;

    GameWorld world;
    ASSERT_TRUE(world.InitWorld("scout-expedition", nullptr, parameters));
    Player* player = world.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(player, nullptr);

    const ProvinceId homeId = player->homeProvinceId;
    const auto neighbours = world.GetGlobalMap().GetNeighbors(homeId);
    ASSERT_FALSE(neighbours.empty());
    const ProvinceId targetId = neighbours.front();

    BattleUnit scout(1, player->id, "scout");
    ASSERT_TRUE(UnitAssignmentService::AssignReserve(scout, homeId, 1));
    player->roster.AddUnit(std::move(scout));
    BattleUnit secondScout(2, player->id, "scout");
    ASSERT_TRUE(UnitAssignmentService::AssignReserve(secondScout, homeId, 1));
    player->roster.AddUnit(std::move(secondScout));
    player->nextUnitInstanceId = 3;
    player->strategicResources.Set(StrategicResourceType::SupplyPackages, 1.0);

    const std::uint64_t commandId = world.SubmitCommand(
        GameCommand::StartScoutExpedition(player->id, homeId, targetId, {1, 2}));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);
    const auto results = world.ConsumeCommandResults();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results.front().commandId, commandId);
    ASSERT_TRUE(results.front().accepted);
    EXPECT_EQ(player->strategicResources.Get(StrategicResourceType::SupplyPackages), 0.0);

    const auto& journeys = world.GetArmyJourneySystem().GetJourneys();
    ASSERT_EQ(journeys.size(), 1u);
    const WorldJourneyId journeyId = journeys.begin()->first;
    EXPECT_TRUE(UnitAssignmentService::IsOnJourney(*player->roster.FindUnit(1), journeyId));
    EXPECT_TRUE(UnitAssignmentService::IsOnJourney(*player->roster.FindUnit(2), journeyId));
    const std::uint64_t completionTick = journeys.begin()->second.legCompletionTick;

    while (world.GetSimulationTick() < completionTick)
        world.UpdateSimulation(FixedSimulationClock::FixedDt);

    EXPECT_EQ(world.GetGlobalMap().FindProvince(targetId)->GetKnowledge(0),
              ProvinceKnowledgeLevel::Scouted);
    EXPECT_FALSE(ScoutExpeditionService::HasActiveFor(
        world.GetArmyJourneySystem(), 0, targetId));
    EXPECT_TRUE(UnitAssignmentService::IsInReserve(*player->roster.FindUnit(1), homeId));
    EXPECT_TRUE(UnitAssignmentService::IsInReserve(*player->roster.FindUnit(2), homeId));
    const auto& reports = world.GetEventSystem().GetFeed().GetHistory();
    const auto report = std::find_if(reports.begin(), reports.end(),
        [targetId](const WorldEventNotificationView& notification)
        {
            return notification.definitionId == "scout_mission_completed" &&
                   notification.provinceId == targetId;
        });
    ASSERT_NE(report, reports.end());
    EXPECT_NE(report->description.find("2 scouts returned safely"), std::string::npos);
}

TEST(ScoutExpeditionTests, ExpeditionDefinitionIsLoadedFromData)
{
    const ExpeditionDefinition* definition = FindExpeditionDefinition("scout");
    ASSERT_NE(definition, nullptr);
    EXPECT_EQ(definition->role, ExpeditionRole::Scout);
    EXPECT_EQ(definition->requiredUnitRole, UnitRole::Scout);
    EXPECT_EQ(definition->requiredUnitCount, 1);
    EXPECT_EQ(definition->supplyResource, StrategicResourceType::SupplyPackages);
    EXPECT_EQ(definition->supplyCost, 1);
    EXPECT_EQ(definition->durationTicks, 200u);
}

TEST(ScoutExpeditionTests, ConvenienceScoutCommandChoosesAvailableReserveScout)
{
    MapParameters parameters;
    parameters.sizeX = 101;
    parameters.sizeY = 101;
    parameters.seed = 5502;

    GameWorld world;
    ASSERT_TRUE(world.InitWorld("scout-command-selection", nullptr, parameters));
    Player* player = world.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(player, nullptr);

    const ProvinceId homeId = player->homeProvinceId;
    const auto neighbours = world.GetGlobalMap().GetNeighbors(homeId);
    ASSERT_FALSE(neighbours.empty());

    BattleUnit travellingScout(1, player->id, "scout");
    ASSERT_TRUE(UnitAssignmentService::AssignJourney(travellingScout, homeId, 99, 1));
    player->roster.AddUnit(std::move(travellingScout));

    BattleUnit reserveScout(2, player->id, "scout");
    ASSERT_TRUE(UnitAssignmentService::AssignReserve(reserveScout, homeId, 1));
    player->roster.AddUnit(std::move(reserveScout));
    player->nextUnitInstanceId = 3;
    player->strategicResources.Set(StrategicResourceType::SupplyPackages, 1.0);

    const std::uint64_t commandId = world.SubmitCommand(
        GameCommand::ScoutProvince(player->id, neighbours.front()));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    const auto results = world.ConsumeCommandResults();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results.front().commandId, commandId);
    EXPECT_TRUE(results.front().accepted);
    EXPECT_TRUE(UnitAssignmentService::IsOnJourney(*player->roster.FindUnit(1), 99));
    EXPECT_EQ(player->roster.FindUnit(2)->assignment.kind, UnitAssignmentKind::Journey);
}

TEST(ScoutExpeditionTests, ActiveProvinceScoutUsesLocalFoodProvisions)
{
    MapParameters parameters;
    parameters.sizeX = 101;
    parameters.sizeY = 101;
    parameters.seed = 5504;

    GameWorld world;
    ASSERT_TRUE(world.InitWorld("scout-active-province", nullptr, parameters));
    Player* player = world.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(player, nullptr);

    const ProvinceId homeId = player->homeProvinceId;
    ASSERT_EQ(player->GetActiveProvinceId(), homeId);
    const auto neighbours = world.GetGlobalMap().GetNeighbors(homeId);
    ASSERT_FALSE(neighbours.empty());

    ProvinceEconomy* economy = player->GetProvinceEconomy(homeId);
    ASSERT_NE(economy, nullptr);
    const int provisionsBefore = StockpileIndex::GetTotal(
        *economy, ResourceType::FOOD_PROVISIONS);
    ASSERT_GE(provisionsBefore, 1);
    ASSERT_EQ(player->strategicResources.Get(StrategicResourceType::SupplyPackages), 0.0);

    BattleUnit scout(1, player->id, "scout");
    ASSERT_TRUE(UnitAssignmentService::AssignReserve(scout, homeId, 1));
    player->roster.AddUnit(std::move(scout));
    player->nextUnitInstanceId = 2;

    const std::uint64_t commandId = world.SubmitCommand(
        GameCommand::ScoutProvince(player->id, homeId, neighbours.front()));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    const auto results = world.ConsumeCommandResults();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results.front().commandId, commandId);
    EXPECT_TRUE(results.front().accepted);
    EXPECT_EQ(player->roster.FindUnit(1)->assignment.kind, UnitAssignmentKind::Journey);
    // The village may request its own food in the same simulation tick. The
    // expedition guarantee is therefore a decrease by at least its package
    // cost, not exclusive ownership of the stockpile delta.
    EXPECT_LE(StockpileIndex::GetTotal(*economy, ResourceType::FOOD_PROVISIONS),
              provisionsBefore - 1);
}

TEST(ScoutExpeditionTests, SnapshotKeepsScoutActionForReachableUnknownProvince)
{
    MapParameters parameters;
    parameters.sizeX = 101;
    parameters.sizeY = 101;
    parameters.seed = 5503;

    GameWorld world;
    ASSERT_TRUE(world.InitWorld("scout-snapshot-action", nullptr, parameters));
    Player* player = world.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(player, nullptr);
    const ProvinceId targetId = world.GetGlobalMap().GetNeighbors(player->homeProvinceId).front();

    const auto snapshot = world.BuildSnapshot();
    const auto target = std::find_if(snapshot.globalMapView.nodes.begin(),
        snapshot.globalMapView.nodes.end(), [targetId](const GlobalMapNodeView& node)
        {
            return node.id == targetId;
        });
    ASSERT_NE(target, snapshot.globalMapView.nodes.end());
    EXPECT_EQ(target->knowledge, ProvinceKnowledgeLevel::ReachableUnknown);
    EXPECT_TRUE(target->canScout);
}
