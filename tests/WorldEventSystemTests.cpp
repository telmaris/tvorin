#include "world/WorldEventSystem.h"
#include "economy/Building.h"
#include "economy/Player.h"
#include "world/ProvinceSimulation.h"

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace
{
    WorldEventDefinition MakeDefinition(std::string id, WorldEventTriggerDomain trigger,
                                        std::uint64_t interval = 0)
    {
        WorldEventDefinition definition;
        definition.id = std::move(id);
        definition.name = definition.id;
        definition.title = definition.id;
        definition.trigger = trigger;
        definition.chanceBasisPoints = 10000;
        definition.weight = 1;
        definition.durationTicks = 5;
        definition.checkIntervalTicks = interval;
        return definition;
    }
}

TEST(WorldEventSystemTests, FiltersAndSelectsInStableWeightedOrder)
{
    WorldEventDefinition first = MakeDefinition("first", WorldEventTriggerDomain::Route);
    first.allowedKinds = {ProvinceKind::Buildable};
    first.requiredTrait = "fertile";
    first.weight = 2;
    WorldEventDefinition second = MakeDefinition("second", WorldEventTriggerDomain::Route);
    second.weight = 1;

    const std::vector<const WorldEventDefinition*> candidates{&first, &second};
    EXPECT_EQ(WeightedEventSelector::Select(candidates, 0), &first);
    EXPECT_EQ(WeightedEventSelector::Select(candidates, 1), &first);
    EXPECT_EQ(WeightedEventSelector::Select(candidates, 2), &second);

    EventEligibilityContext context;
    context.provinceKind = ProvinceKind::Buildable;
    context.provinceTraits = {"fertile"};
    EXPECT_TRUE(EventEligibilityService::IsEligible(
        first, WorldEventTriggerDomain::Route, context));
    context.provinceTraits.clear();
    EXPECT_FALSE(EventEligibilityService::IsEligible(
        first, WorldEventTriggerDomain::Route, context));
}

TEST(WorldEventSystemTests, PeriodicCadenceCreatesOnceAndExpiresWithoutRerolling)
{
    WorldEventCatalog catalog;
    catalog.emplace("periodic", MakeDefinition(
        "periodic", WorldEventTriggerDomain::ProvincePeriodic, 10));
    GlobalMap map;
    ASSERT_TRUE(map.AddProvince(std::make_unique<BuildableProvince>(
        1, Vec2i{0, 0}, 0)));

    ProvinceEventSystem system(1234, &catalog);
    system.Update(map, 0);
    ASSERT_EQ(system.GetInstances().size(), 1u);
    const auto firstId = system.GetInstances().begin()->first;
    EXPECT_EQ(system.GetInstances().at(firstId).startTick, 0u);
    system.Update(map, 0);
    EXPECT_EQ(system.GetInstances().size(), 1u);
    system.Update(map, 5);
    EXPECT_TRUE(system.GetInstances().at(firstId).expired);
    system.Update(map, 9);
    EXPECT_EQ(system.GetInstances().size(), 1u);
    system.Update(map, 10);
    ASSERT_EQ(system.GetInstances().size(), 2u);
    EXPECT_EQ(system.GetInstances().rbegin()->second.startTick, 10u);
}

TEST(WorldEventSystemTests, DiscoveryIsPerPlayerAndFeedDeduplicatesPopups)
{
    WorldEventCatalog catalog;
    auto definition = MakeDefinition("discovery", WorldEventTriggerDomain::Discovery);
    definition.followUpPoolId = "ancient_ruins";
    catalog.emplace(definition.id, definition);
    GlobalMap map;
    ASSERT_TRUE(map.AddProvince(std::make_unique<EventProvince>(
        7, Vec2i{0, 0}, "ancient_ruins")));

    ProvinceEventSystem system(77, &catalog);
    ASSERT_TRUE(system.TriggerDiscovery(map, 0, 7, 20));
    EXPECT_TRUE(dynamic_cast<EventProvince*>(map.FindProvince(7))->HasResolvedFor(0));
    EXPECT_FALSE(system.TriggerDiscovery(map, 0, 7, 21));

    const auto first = system.ConsumeFeedFor(0);
    ASSERT_EQ(first.size(), 1u);
    EXPECT_TRUE(system.GetFeed().HasDelivered(0, first.front().instanceId));
    EXPECT_TRUE(system.ConsumeFeedFor(0).empty());
    EXPECT_TRUE(system.ConsumeFeedFor(1).empty());
}

TEST(WorldEventSystemTests, RouteLossTargetsTheJourneyOnceAndKeepsItsStableId)
{
    WorldEventCatalog catalog;
    auto definition = MakeDefinition("ambush", WorldEventTriggerDomain::Route);
    definition.effects.emplace_back(KillJourneyUnitsEffect{1});
    catalog.emplace(definition.id, definition);

    GlobalMap map;
    ASSERT_TRUE(map.AddProvince(std::make_unique<BuildableProvince>(
        1, Vec2i{0, 0}, 0)));
    ASSERT_TRUE(map.AddProvince(std::make_unique<BuildableProvince>(
        2, Vec2i{1, 0}, InvalidPlayerId)));
    ASSERT_TRUE(map.AddConnection(1, 2));

    ProvinceEventSystem system(99, &catalog);
    ASSERT_TRUE(system.TriggerRoute(map, 0, 1, 2, 0, 10, 42));
    ASSERT_EQ(system.GetInstances().size(), 1u);
    const auto& instance = system.GetInstances().begin()->second;
    EXPECT_EQ(instance.journeyId, 42u);

    const auto losses = system.ConsumePendingJourneyUnitLosses();
    ASSERT_EQ(losses.size(), 1u);
    EXPECT_EQ(losses.front().journeyId, 42u);
    EXPECT_EQ(losses.front().amount, 1);
    // Reading a request is not execution. It remains retryable until the
    // journey authority confirms that the loss was actually applied.
    EXPECT_EQ(system.ConsumePendingJourneyUnitLosses().size(), 1u);
    EXPECT_TRUE(system.ConfirmJourneyEffectApplied(losses.front().eventId));
    EXPECT_TRUE(system.ConsumePendingJourneyUnitLosses().empty());
}

TEST(WorldEventSystemTests, ZeroDurationRaidRemainsPendingUntilAuthorityConfirmsIt)
{
    WorldEventCatalog catalog;
    auto definition = MakeDefinition("raid", WorldEventTriggerDomain::ProvincePeriodic, 10);
    definition.durationTicks = 0;
    definition.effects.emplace_back(StartRaidEffect{20});
    catalog.emplace(definition.id, definition);

    GlobalMap map;
    ASSERT_TRUE(map.AddProvince(std::make_unique<BuildableProvince>(
        1, Vec2i{0, 0}, 0)));
    ProvinceEventSystem system(123, &catalog);
    system.Update(map, 0);

    const auto firstAttempt = system.ConsumePendingRaidRequests();
    ASSERT_EQ(firstAttempt.size(), 1u);
    EXPECT_EQ(system.ConsumePendingRaidRequests().size(), 1u);
    EXPECT_TRUE(system.RecordAppliedEffect(firstAttempt.front().eventId,
        {AppliedWorldEventEffectKind::RaidStarted, ResourceType::Null,
         firstAttempt.front().strength}));
    EXPECT_TRUE(system.ConfirmRaidStarted(firstAttempt.front().eventId));
    EXPECT_TRUE(system.ConsumePendingRaidRequests().empty());
    const auto feed = system.ConsumeFeedFor(0);
    ASSERT_EQ(feed.size(), 1u);
    ASSERT_EQ(feed.front().appliedEffects.size(), 1u);
    EXPECT_EQ(feed.front().appliedEffects.front().kind,
              AppliedWorldEventEffectKind::RaidStarted);
    EXPECT_EQ(feed.front().appliedEffects.front().amount, 20);
}

TEST(WorldEventSystemTests, DestroyBuildingEffectMutatesOwnedProvinceDeterministically)
{
    WorldEventCatalog catalog;
    auto definition = MakeDefinition("collapse", WorldEventTriggerDomain::ProvincePeriodic, 10);
    definition.durationTicks = 0;
    definition.effects.emplace_back(DestroyBuildingEffect{1});
    catalog.emplace(definition.id, definition);

    auto province = std::make_unique<BuildableProvince>(1, Vec2i{0, 0}, 0);
    ProvinceSimulation& simulation = province->CreateSimulation();
    Player player{0, simulation};
    TileMap& localMap = simulation.GetTileMap();
    localMap.params.sizeX = 20;
    localMap.params.sizeY = 20;
    for (int tileId = 0; tileId < 400; ++tileId)
    {
        localMap.tilemap.emplace_back(tileId);
        localMap.tilemap.back().tileType = TileType::GRASS;
        localMap.tilemap.back().owner = &player;
        localMap.tilemap.back().ownerId = player.id;
    }
    ASSERT_NE(localMap.PlaceLoadedBuilding(
        localMap.GetIdFromCoords({2, 2}), &player,
        std::make_unique<StorageBuilding>(10)), nullptr);
    ASSERT_NE(localMap.PlaceLoadedBuilding(
        localMap.GetIdFromCoords({8, 8}), &player,
        std::make_unique<StorageBuilding>(20)), nullptr);

    GlobalMap map;
    ASSERT_TRUE(map.AddProvince(std::move(province)));
    ProvinceEventSystem system(456, &catalog);
    system.Update(map, 0);

    EXPECT_EQ(simulation.GetEconomy().dataTracker.buildings.size(), 1u);
    EXPECT_EQ(system.GetInstances().size(), 1u);
}

TEST(WorldEventSystemTests, ExplicitMissionReportUsesThePersistentChronologicalFeed)
{
    WorldEventCatalog catalog;
    auto definition = MakeDefinition("scout_report", WorldEventTriggerDomain::Discovery);
    definition.title = "Scouting mission completed";
    definition.description = "Default report";
    catalog.emplace(definition.id, definition);

    ProvinceEventSystem system(12, &catalog);
    ASSERT_TRUE(system.PublishNotification("scout_report", 0, 7, 3, 55,
                                           "Two scouts returned."));
    ASSERT_TRUE(system.PublishNotification("scout_report", 0, 8, 3, 56,
                                           "One scout returned."));
    const auto& history = system.GetFeed().GetHistory();
    ASSERT_EQ(history.size(), 2u);
    EXPECT_LT(history[0].instanceId, history[1].instanceId);
    EXPECT_EQ(history[0].startTick, 55u);
    EXPECT_EQ(history[1].startTick, 56u);
    EXPECT_EQ(history[0].description, "Two scouts returned.");
    EXPECT_TRUE(history[0].expired);
}
