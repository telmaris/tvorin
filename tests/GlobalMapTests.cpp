#include "world/ScoutExpeditionService.h"
#include "world/GlobalMap.h"
#include "warfare/UnitDefinition.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

namespace
{
    std::unique_ptr<BuildableProvince> Buildable(ProvinceId id, PlayerId owner = InvalidPlayerId)
    {
        return std::make_unique<BuildableProvince>(id, Vec2i{static_cast<int>(id) * 10, 0}, owner);
    }
}

TEST(GlobalMapTests, RejectsDuplicateProvinceSelfEdgeAndDanglingEdge)
{
    GlobalMap map;
    ASSERT_TRUE(map.AddProvince(Buildable(1)));
    EXPECT_FALSE(map.AddProvince(Buildable(1)));
    EXPECT_FALSE(map.AddConnection(1, 1));
    EXPECT_FALSE(map.AddConnection(1, 2));
}

TEST(GlobalMapTests, ConnectionsAreSymmetricCanonicalAndStable)
{
    GlobalMap map;
    ASSERT_TRUE(map.AddProvince(Buildable(1)));
    ASSERT_TRUE(map.AddProvince(Buildable(2)));
    ASSERT_TRUE(map.AddProvince(Buildable(3)));
    ASSERT_TRUE(map.AddConnection(3, 1));
    ASSERT_TRUE(map.AddConnection(2, 1));
    EXPECT_FALSE(map.AddConnection(1, 3));
    EXPECT_EQ(map.GetNeighbors(1), (std::vector<ProvinceId>{2, 3}));
    EXPECT_EQ(map.GetNeighbors(2), (std::vector<ProvinceId>{1}));
    EXPECT_EQ(map.GetNeighbors(3), (std::vector<ProvinceId>{1}));
    EXPECT_EQ(map.GetEdgeCount(), 2u);
    EXPECT_TRUE(map.IsConnected());
}

TEST(GlobalMapTests, ShortestPathUsesStableConnectionIdTieBreaking)
{
    GlobalMap map;
    for (ProvinceId id = 1; id <= 4; ++id)
        ASSERT_TRUE(map.AddProvince(Buildable(id)));
    ASSERT_TRUE(map.AddConnection(1, 3, 20));
    ASSERT_TRUE(map.AddConnection(3, 4, 21));
    ASSERT_TRUE(map.AddConnection(1, 2, 10));
    ASSERT_TRUE(map.AddConnection(2, 4, 11));

    std::vector<ProvinceConnectionId> path;
    ASSERT_TRUE(map.FindShortestPath(1, 4, path));
    EXPECT_EQ(path, (std::vector<ProvinceConnectionId>{10, 11}));
    EXPECT_TRUE(map.HasPath(4, 1));
    EXPECT_FALSE(map.HasPath(1, 1));
}

TEST(GlobalMapTests, ViewKeepsEveryNodeWhileHidingUndiscoveredInformation)
{
    GlobalMap map;
    ASSERT_TRUE(map.AddProvince(std::make_unique<BuildableProvince>(1, Vec2i{0, 0}, 0)));
    ASSERT_TRUE(map.AddProvince(std::make_unique<StaticProvince>(2, ProvinceKind::BanditCamp,
                                                                  Vec2i{10, 0})));
    ASSERT_TRUE(map.AddProvince(Buildable(3)));
    ASSERT_TRUE(map.AddConnection(1, 2));
    ASSERT_TRUE(map.AddConnection(2, 3));
    ASSERT_TRUE(map.InitializeDiscovery(0, 1));

    const GlobalMapView initial = map.BuildViewFor(0);
    ASSERT_EQ(initial.nodes.size(), 3u);
    const auto initialUnknown = std::find_if(initial.nodes.begin(), initial.nodes.end(),
                                             [](const auto& node) { return node.id == 2; });
    ASSERT_NE(initialUnknown, initial.nodes.end());
    EXPECT_EQ(initialUnknown->knowledge, ProvinceKnowledgeLevel::ReachableUnknown);
    EXPECT_FALSE(initialUnknown->visibleKind.has_value());
    EXPECT_TRUE(initialUnknown->canScout);
    ASSERT_EQ(initial.edges.size(), 2u);
    EXPECT_FALSE(initial.edges[0].canInspect);

    ASSERT_TRUE(map.SetKnowledge(0, 2, ProvinceKnowledgeLevel::Scouted));
    const GlobalMapView scouted = map.BuildViewFor(0);
    ASSERT_EQ(scouted.nodes.size(), 3u);
    const auto scoutedNode = std::find_if(scouted.nodes.begin(), scouted.nodes.end(),
                                          [](const auto& node) { return node.id == 2; });
    ASSERT_NE(scoutedNode, scouted.nodes.end());
    EXPECT_EQ(scoutedNode->visibleKind, ProvinceKind::BanditCamp);
    EXPECT_EQ(scouted.edges.size(), 2u);
}

TEST(GlobalMapTests, DisablingGlobalFogRevealsAllProvinceAndRouteDetails)
{
    GlobalMap map;
    ASSERT_TRUE(map.AddProvince(std::make_unique<BuildableProvince>(1, Vec2i{0, 0}, 0)));
    ASSERT_TRUE(map.AddProvince(std::make_unique<StaticProvince>(2, ProvinceKind::BanditCamp,
                                                                  Vec2i{10, 0})));
    ASSERT_TRUE(map.AddConnection(1, 2));
    ASSERT_TRUE(map.InitializeDiscovery(0, 1));
    map.SetFogOfWarEnabled(false);

    const auto view = map.BuildViewFor(0);
    ASSERT_EQ(view.nodes.size(), 2u);
    const auto bandit = std::find_if(view.nodes.begin(), view.nodes.end(),
                                     [](const auto& node) { return node.id == 2; });
    ASSERT_NE(bandit, view.nodes.end());
    EXPECT_EQ(bandit->knowledge, ProvinceKnowledgeLevel::Scouted);
    EXPECT_EQ(bandit->visibleKind, ProvinceKind::BanditCamp);
    ASSERT_EQ(view.edges.size(), 1u);
    EXPECT_TRUE(view.edges.front().canInspect);
    EXPECT_FALSE(view.fogOfWarEnabled);
}

TEST(GlobalMapTests, KnownProvinceViewExposesOnlyLegalActionCapabilities)
{
    GlobalMap map;
    ASSERT_TRUE(map.AddProvince(std::make_unique<BuildableProvince>(
        1, Vec2i{0, 0}, 0)));
    ASSERT_TRUE(map.AddProvince(std::make_unique<NeutralCityProvince>(
        2, Vec2i{1, 0}, "neutral_city")));
    ASSERT_TRUE(map.AddProvince(std::make_unique<BuildableProvince>(
        3, Vec2i{2, 0}, InvalidPlayerId)));
    ASSERT_TRUE(map.AddConnection(1, 2));
    ASSERT_TRUE(map.AddConnection(2, 3));
    ASSERT_TRUE(map.InitializeDiscovery(0, 1));
    ASSERT_TRUE(map.SetKnowledge(0, 2, ProvinceKnowledgeLevel::Scouted));
    ASSERT_TRUE(map.SetKnowledge(0, 3, ProvinceKnowledgeLevel::Scouted));

    const auto view = map.BuildViewFor(0);
    const auto city = std::find_if(view.nodes.begin(), view.nodes.end(),
                                   [](const auto& node) { return node.id == 2; });
    const auto frontier = std::find_if(view.nodes.begin(), view.nodes.end(),
                                       [](const auto& node) { return node.id == 3; });
    ASSERT_NE(city, view.nodes.end());
    ASSERT_NE(frontier, view.nodes.end());
    EXPECT_TRUE(city->canTrade);
    EXPECT_TRUE(city->canAttack);
    EXPECT_FALSE(city->canColonize);
    EXPECT_TRUE(frontier->canColonize);
    EXPECT_FALSE(frontier->canTrade);
}

TEST(GlobalMapTests, MovePreservesStableProvinceIdsAndTopology)
{
    GlobalMap original;
    ASSERT_TRUE(original.AddProvince(Buildable(1)));
    ASSERT_TRUE(original.AddProvince(Buildable(2)));
    ASSERT_TRUE(original.AddConnection(1, 2));
    GlobalMap moved = std::move(original);
    ASSERT_NE(moved.FindProvince(1), nullptr);
    EXPECT_EQ(moved.GetProvinceIds(), (std::vector<ProvinceId>{1, 2}));
    EXPECT_TRUE(moved.IsAdjacent(1, 2));
}

TEST(GlobalMapGeneratorTests, DifferentSeedChangesTheGlobalLayout)
{
    GlobalMapParameters firstParameters;
    firstParameters.seed = 101;
    GlobalMapParameters secondParameters = firstParameters;
    secondParameters.seed = 102;
    const auto first = GlobalMapGenerator::Generate(firstParameters, 2);
    const auto second = GlobalMapGenerator::Generate(secondParameters, 2);
    ASSERT_TRUE(first.success);
    ASSERT_TRUE(second.success);
    EXPECT_NE(first.map.FindProvince(1)->GetLayoutPosition().x,
              second.map.FindProvince(1)->GetLayoutPosition().x);
}

TEST(GlobalMapGeneratorTests, PropagatesGlobalFogOfWarSetting)
{
    GlobalMapParameters parameters;
    parameters.seed = 20260904u;
    parameters.provinceCount = 8;
    parameters.extraEdgeCount = 4;
    parameters.fogOfWarEnabled = false;

    const auto generated = GlobalMapGenerator::Generate(parameters, 1);
    ASSERT_TRUE(generated.success) << generated.failureReason;
    EXPECT_FALSE(generated.map.IsFogOfWarEnabled());
    EXPECT_FALSE(generated.map.BuildViewFor(0).fogOfWarEnabled);
}

TEST(GlobalMapGeneratorTests, ProvinceSeedsAreStableAndIndependent)
{
    const auto first = GlobalMapGenerator::DeriveProvinceSeed(1234u, 7u);
    const auto second = GlobalMapGenerator::DeriveProvinceSeed(1234u, 8u);
    EXPECT_EQ(first, GlobalMapGenerator::DeriveProvinceSeed(1234u, 7u));
    EXPECT_NE(first, second);
    EXPECT_NE(first, GlobalMapGenerator::DeriveProvinceSeed(1235u, 7u));
}

TEST(GlobalMapViewTests, UnknownProvinceKeepsLayoutAndOmitsTypeOwner)
{
    GlobalMap map;
    ASSERT_TRUE(map.AddProvince(std::make_unique<BuildableProvince>(1, Vec2i{0, 0}, 0)));
    ASSERT_TRUE(map.AddProvince(std::make_unique<StaticProvince>(2, ProvinceKind::TreasureSite,
                                                                  Vec2i{10, 0})));
    ASSERT_TRUE(map.AddProvince(std::make_unique<StaticProvince>(3, ProvinceKind::BanditCamp,
                                                                  Vec2i{20, 0})));
    ASSERT_TRUE(map.AddConnection(1, 2));
    ASSERT_TRUE(map.AddConnection(2, 3));
    ASSERT_TRUE(map.InitializeDiscovery(0, 1));

    const auto view = map.BuildViewFor(0);
    ASSERT_EQ(view.nodes.size(), 3u);
    const auto hidden = std::find_if(view.nodes.begin(), view.nodes.end(),
                                     [](const auto& node) { return node.id == 3; });
    ASSERT_NE(hidden, view.nodes.end());
    EXPECT_FALSE(hidden->visibleKind.has_value());
    EXPECT_FALSE(hidden->visibleOwner.has_value());
    ASSERT_EQ(view.edges.size(), 2u);
    EXPECT_FALSE(view.edges.back().canInspect);
}

TEST(ProvinceKnowledgeTests, ScoutServiceCreatesOneCanonicalJourney)
{
    GlobalMap map;
    ASSERT_TRUE(map.AddProvince(std::make_unique<BuildableProvince>(1, Vec2i{0, 0}, 0)));
    ASSERT_TRUE(map.AddProvince(std::make_unique<StaticProvince>(2, ProvinceKind::NeutralSettlement,
                                                                  Vec2i{1, 0})));
    ASSERT_TRUE(map.AddProvince(std::make_unique<StaticProvince>(3, ProvinceKind::TreasureSite,
                                                                  Vec2i{2, 0})));
    ASSERT_TRUE(map.AddConnection(1, 2));
    ASSERT_TRUE(map.AddConnection(2, 3));
    ASSERT_TRUE(map.InitializeDiscovery(0, 1));

    UnitRoster roster;
    BattleUnit scout(7, 0, "scout");
    ASSERT_TRUE(UnitAssignmentService::AssignReserve(scout, 1, 42));
    roster.AddUnit(std::move(scout));
    WorldJourneySystem journeys;
    WorldJourneyId id = InvalidWorldJourneyId;
    std::string failure;
    ASSERT_TRUE(ScoutExpeditionService::Start(
        0, 1, 2, {7}, roster, map, journeys, 40, id, failure)) << failure;
    EXPECT_TRUE(ScoutExpeditionService::HasActiveFor(journeys, 0, 2));
    journeys.Update(map, 239);
    EXPECT_EQ(map.FindProvince(2)->GetKnowledge(0), ProvinceKnowledgeLevel::ReachableUnknown);
    journeys.Update(map, 240);
    EXPECT_EQ(journeys.GetJourneys().at(id).status, WorldJourneyStatus::Succeeded);
    EXPECT_FALSE(ScoutExpeditionService::HasActiveFor(journeys, 0, 2));
    EXPECT_TRUE(UnitAssignmentService::IsOnJourney(*roster.FindUnit(7), id));
}
