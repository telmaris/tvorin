#include "world/GlobalMap.h"
#include "world/ProvinceConnection.h"
#include "core/GameCommand.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    std::unique_ptr<BuildableProvince> Buildable(ProvinceId id, PlayerId owner = InvalidPlayerId)
    {
        return std::make_unique<BuildableProvince>(
            id, Vec2i{static_cast<int>(id) * 10, 0}, owner);
    }
}

TEST(ProvinceConnectionTests, ShippedRouteCatalogHasLevelZeroAndDataDrivenUpgrades)
{
    const auto result = LoadProvinceConnectionDefinitionsFromFile("assets/data/routes.rtsdata");
    ASSERT_TRUE(result.IsValid());
    const auto it = result.definitions.find("land_route");
    ASSERT_NE(it, result.definitions.end());
    ASSERT_EQ(it->second.levels.size(), 3u);
    EXPECT_EQ(it->second.levels[0].level, 0);
    EXPECT_DOUBLE_EQ(it->second.levels[0].traversalTimeMultiplier, 1.0);
    EXPECT_EQ(it->second.levels[1].upgradeCost.size(), 2u);
    EXPECT_EQ(it->second.levels[2].upgradeCost.front().type, ResourceType::PLANKS);
}

TEST(ProvinceConnectionTests, LandRouteCanonicalizesEndpointsAndProgressesUpgrade)
{
    LandRouteConnection route(7, 9, 3);
    EXPECT_EQ(route.GetFirstProvinceId(), 3u);
    EXPECT_EQ(route.GetSecondProvinceId(), 9u);
    EXPECT_EQ(route.GetLevel(), 0);
    EXPECT_FALSE(route.IsUpgradeInProgress());

    const auto initial = route.ResolveTraversalStats({});
    EXPECT_DOUBLE_EQ(initial.traversalTimeMultiplier, 1.0);
    EXPECT_EQ(initial.incidentChanceReductionBasisPoints, 0);

    const auto* next = route.GetNextLevelDefinition();
    ASSERT_NE(next, nullptr);
    ASSERT_TRUE(route.BeginUpgrade(next->level, 3));
    EXPECT_TRUE(route.IsUpgradeInProgress());
    EXPECT_EQ(route.GetUpgradeTargetLevel(), 1);
    EXPECT_EQ(route.GetUpgradeRemainingTicks(), 3u);
    route.Update();
    route.Update();
    EXPECT_EQ(route.GetLevel(), 0);
    route.Update();
    EXPECT_EQ(route.GetLevel(), 1);
    EXPECT_FALSE(route.IsUpgradeInProgress());
    EXPECT_DOUBLE_EQ(route.ResolveTraversalStats({}).traversalTimeMultiplier, 0.85);
}

TEST(ProvinceConnectionTests, GlobalMapStoresOneConnectionAndAnIncidentIdIndex)
{
    GlobalMap map;
    ASSERT_TRUE(map.AddProvince(Buildable(1, 0)));
    ASSERT_TRUE(map.AddProvince(Buildable(2)));
    ASSERT_TRUE(map.AddProvince(Buildable(3)));
    ASSERT_TRUE(map.AddConnection(3, 1, 42));
    ASSERT_TRUE(map.AddConnection(1, 2));

    const auto* byId = map.FindConnection(42);
    ASSERT_NE(byId, nullptr);
    EXPECT_EQ(byId->GetFirstProvinceId(), 1u);
    EXPECT_EQ(byId->GetSecondProvinceId(), 3u);
    EXPECT_EQ(map.FindConnection(3, 1), byId);
    EXPECT_EQ(map.GetIncidentConnectionIds(1), (std::vector<ProvinceConnectionId>{42, 43}));
    EXPECT_EQ(map.GetNeighbors(1), (std::vector<ProvinceId>{2, 3}));
    EXPECT_EQ(map.GetConnectionCount(), 2u);
    EXPECT_EQ(map.GetEdgeCount(), 2u);
}

TEST(ProvinceConnectionTests, ViewExposesLevelAndPermissionOnlyAfterBothEndpointsAreScouted)
{
    GlobalMap map;
    ASSERT_TRUE(map.AddProvince(Buildable(1, 0)));
    ASSERT_TRUE(map.AddProvince(std::make_unique<NeutralCityProvince>(2, Vec2i{20, 0})));
    ASSERT_TRUE(map.AddConnection(1, 2, 17));
    ASSERT_TRUE(map.InitializeDiscovery(0, 1));

    const auto reachable = map.BuildViewFor(0);
    ASSERT_EQ(reachable.edges.size(), 1u);
    EXPECT_EQ(reachable.edges.front().connectionId, 17u);
    EXPECT_EQ(reachable.edges.front().level, 0);
    EXPECT_FALSE(reachable.edges.front().canInspect);
    EXPECT_FALSE(reachable.edges.front().canUpgrade);

    ASSERT_TRUE(map.SetKnowledge(0, 2, ProvinceKnowledgeLevel::Scouted));
    const auto scouted = map.BuildViewFor(0);
    ASSERT_EQ(scouted.edges.size(), 1u);
    EXPECT_TRUE(scouted.edges.front().canInspect);
    EXPECT_TRUE(scouted.edges.front().canUpgrade);
}

TEST(ProvinceConnectionTests, UpgradeCommandCarriesStableConnectionId)
{
    const GameCommand original =
        GameCommand::UpgradeProvinceConnection(4, 12, 99);
    GameCommand parsed;
    ASSERT_TRUE(GameCommand::TryDeserialize(original.Serialize(), parsed));
    EXPECT_EQ(parsed.type, GameCommandType::UpgradeProvinceConnection);
    EXPECT_EQ(parsed.playerId, 4);
    EXPECT_EQ(parsed.provinceId, 12u);
    EXPECT_EQ(parsed.connectionId, 99u);
}
