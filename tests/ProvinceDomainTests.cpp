#include "core/GameWorld.h"
#include "core/GameSession.h"
#include "world/GlobalMap.h"
#include "world/Province.h"

#include <gtest/gtest.h>

#include <concepts>
#include <memory>

namespace
{
    template <typename T>
    concept HasSimulationAccessor = requires(T& province)
    {
        province.GetSimulation();
    };

    template <typename T>
    concept HasOwnerAccessor = requires(T& province)
    {
        province.GetOwnerId();
    };

    template <typename T>
    concept HasProvinceUpdate = requires(T& province, Player& player)
    {
        province.Update(player, 0.1, 1u);
    };
}

TEST(ProvinceDomainTests, NeutralProvincesHaveIndependentConcreteState)
{
    NeutralCityProvince city(1, {10, 20}, "market_city");
    BanditProvince bandits(2, {20, 20}, "road_bandits");
    EventProvince eventSite(3, {30, 20}, "ancient_ruins");

    EXPECT_EQ(city.GetKind(), ProvinceKind::NeutralSettlement);
    EXPECT_EQ(city.GetDefinitionId(), "market_city");
    EXPECT_EQ(bandits.GetKind(), ProvinceKind::BanditCamp);
    EXPECT_EQ(bandits.GetDefinitionId(), "road_bandits");
    EXPECT_EQ(eventSite.GetKind(), ProvinceKind::TreasureSite);
    EXPECT_EQ(eventSite.GetEventPoolId(), "ancient_ruins");
    EXPECT_EQ(eventSite.GetDiscoveryAttempts(0), 0u);

    eventSite.RecordDiscoveryAttempt(0);
    eventSite.MarkResolvedFor(0);
    EXPECT_EQ(eventSite.GetDiscoveryAttempts(0), 1u);
    EXPECT_TRUE(eventSite.HasResolvedFor(0));
}

TEST(ProvinceDomainTests, ProvinceInterfaceDoesNotExposePlayerOrSimulation)
{
    static_assert(!HasSimulationAccessor<IProvince>);
    static_assert(!HasOwnerAccessor<IProvince>);
    static_assert(!HasProvinceUpdate<IProvince>);
    static_assert(HasSimulationAccessor<BuildableProvince>);
    static_assert(HasOwnerAccessor<BuildableProvince>);
    static_assert(!HasSimulationAccessor<NeutralCityProvince>);
    static_assert(!HasOwnerAccessor<NeutralCityProvince>);

    std::unique_ptr<IProvince> neutral =
        std::make_unique<NeutralCityProvince>(1, Vec2i{0, 0});
    EXPECT_FALSE(neutral->IsBuildable());
    EXPECT_EQ(neutral->GetKnowledge(0), ProvinceKnowledgeLevel::Hidden);
}

TEST(ProvinceDomainTests, KnowledgeOnlyMovesForward)
{
    BuildableProvince province(1, {0, 0});

    EXPECT_TRUE(province.SetKnowledge(7, ProvinceKnowledgeLevel::ReachableUnknown));
    EXPECT_TRUE(province.SetKnowledge(7, ProvinceKnowledgeLevel::Scouted));
    EXPECT_FALSE(province.SetKnowledge(7, ProvinceKnowledgeLevel::ReachableUnknown));
    EXPECT_EQ(province.GetKnowledge(7), ProvinceKnowledgeLevel::Scouted);
    EXPECT_FALSE(province.SetKnowledge(InvalidPlayerId, ProvinceKnowledgeLevel::Owned));
}

TEST(ProvinceDomainTests, TransformKeepsStableIdentityKnowledgeAndConnections)
{
    GlobalMap map;
    ASSERT_TRUE(map.AddProvince(std::make_unique<BuildableProvince>(1, Vec2i{0, 0})));
    ASSERT_TRUE(map.AddProvince(std::make_unique<BanditProvince>(2, Vec2i{10, 0})));
    ASSERT_TRUE(map.AddProvince(std::make_unique<EventProvince>(3, Vec2i{20, 0})));
    ASSERT_TRUE(map.AddConnection(1, 2));
    ASSERT_TRUE(map.AddConnection(2, 3));
    ASSERT_TRUE(map.SetKnowledge(5, 2, ProvinceKnowledgeLevel::Scouted));

    ASSERT_TRUE(map.TransformProvince(
        std::make_unique<BuildableProvince>(2, Vec2i{10, 0})));

    const auto* transformed = map.FindBuildableProvince(2);
    ASSERT_NE(transformed, nullptr);
    EXPECT_EQ(transformed->GetId(), 2u);
    EXPECT_EQ(transformed->GetLayoutPosition(), (Vec2i{10, 0}));
    EXPECT_EQ(transformed->GetKnowledge(5), ProvinceKnowledgeLevel::Scouted);
    EXPECT_EQ(map.GetNeighbors(2), (std::vector<ProvinceId>{1, 3}));
    EXPECT_TRUE(map.IsAdjacent(1, 2));
    EXPECT_TRUE(map.IsAdjacent(2, 3));
}

TEST(ProvinceDomainTests, EachOwnedProvinceSimulationTicksOnce)
{
    MapParameters parameters;
    parameters.sizeX = 101;
    parameters.sizeY = 101;
    parameters.seed = 9011;

    GameWorld world;
    ASSERT_TRUE(world.InitMultiplayerWorld(
        "two-owned-provinces", nullptr, parameters, 0, true));
    const ProvinceId first = world.GetPlayerProvinceId(0);
    const ProvinceId second = world.GetPlayerProvinceId(1);
    ASSERT_NE(first, InvalidProvinceId);
    ASSERT_NE(second, InvalidProvinceId);

    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    const auto* firstProvince = world.GetGlobalMap().FindBuildableProvince(first);
    const auto* secondProvince = world.GetGlobalMap().FindBuildableProvince(second);
    ASSERT_NE(firstProvince, nullptr);
    ASSERT_NE(secondProvince, nullptr);
    ASSERT_NE(firstProvince->GetSimulation(), nullptr);
    ASSERT_NE(secondProvince->GetSimulation(), nullptr);
    EXPECT_EQ(firstProvince->GetSimulation()->GetEconomy().simulationTick, 1u);
    EXPECT_EQ(secondProvince->GetSimulation()->GetEconomy().simulationTick, 1u);
}
