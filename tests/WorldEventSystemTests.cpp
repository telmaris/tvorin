#include "world/WorldEventSystem.h"
#include "economy/Building.h"
#include "economy/Player.h"
#include "world/ProvinceSimulation.h"

#include <gtest/gtest.h>

#include <algorithm>
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
        definition.repeatCooldownTicks = interval;
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
    auto province = std::make_unique<BuildableProvince>(1, Vec2i{0, 0}, 0);
    ProvinceSimulation& simulation = province->CreateSimulation();
    Player player{0, simulation};
    ASSERT_TRUE(map.AddProvince(std::move(province)));

    ProvinceEventSystem system(1234, &catalog,
                               PeriodicEventScheduleDefinition{1, 10, 10000, 1});
    system.Update(map, 0);
    EXPECT_EQ(system.GetInstances().size(), 0u);
    system.Update(map, 1);
    ASSERT_EQ(system.GetInstances().size(), 1u);
    const auto firstId = system.GetInstances().begin()->first;
    EXPECT_EQ(system.GetInstances().at(firstId).startTick, 1u);
    system.Update(map, 1);
    EXPECT_EQ(system.GetInstances().size(), 1u);
    system.Update(map, 6);
    EXPECT_TRUE(system.GetInstances().at(firstId).expired);
    system.Update(map, 9);
    EXPECT_EQ(system.GetInstances().size(), 1u);
    system.Update(map, 10);
    EXPECT_EQ(system.GetInstances().size(), 1u);
    system.Update(map, 11);
    ASSERT_EQ(system.GetInstances().size(), 2u);
    EXPECT_EQ(system.GetInstances().rbegin()->second.startTick, 11u);
}

TEST(WorldEventSystemTests, PeriodicSchedulerWaitsForInitialDelay)
{
    WorldEventCatalog catalog;
    catalog.emplace("periodic", MakeDefinition(
        "periodic", WorldEventTriggerDomain::ProvincePeriodic, 5));
    GlobalMap map;
    auto province = std::make_unique<BuildableProvince>(1, Vec2i{0, 0}, 0);
    ProvinceSimulation& simulation = province->CreateSimulation();
    Player player{0, simulation};
    ASSERT_TRUE(map.AddProvince(std::move(province)));

    ProvinceEventSystem system(22, &catalog,
                               PeriodicEventScheduleDefinition{10, 3, 10000, 1});
    for (std::uint64_t tick = 0; tick < 10; ++tick)
        system.Update(map, tick);
    EXPECT_TRUE(system.GetInstances().empty());
    system.Update(map, 10);
    ASSERT_EQ(system.GetInstances().size(), 1u);
    EXPECT_EQ(system.GetInstances().begin()->second.startTick, 10u);
}

TEST(WorldEventSystemTests, PeriodicSchedulerAllowsAtMostOneEventPerPlayerAndKeepsPlayersIndependent)
{
    WorldEventCatalog catalog;
    catalog.emplace("periodic", MakeDefinition(
        "periodic", WorldEventTriggerDomain::ProvincePeriodic, 1));
    GlobalMap map;
    auto first = std::make_unique<BuildableProvince>(1, Vec2i{0, 0}, 0);
    auto second = std::make_unique<BuildableProvince>(2, Vec2i{1, 0}, 0);
    auto third = std::make_unique<BuildableProvince>(3, Vec2i{2, 0}, 1);
    ProvinceSimulation& firstSimulation = first->CreateSimulation();
    ProvinceSimulation& secondSimulation = second->CreateSimulation();
    ProvinceSimulation& thirdSimulation = third->CreateSimulation();
    Player firstPlayer{0, firstSimulation};
    Player secondPlayer{1, thirdSimulation};
    (void)secondSimulation;
    ASSERT_TRUE(map.AddProvince(std::move(first)));
    ASSERT_TRUE(map.AddProvince(std::move(second)));
    ASSERT_TRUE(map.AddProvince(std::move(third)));

    ProvinceEventSystem system(88, &catalog,
                               PeriodicEventScheduleDefinition{1, 2, 10000, 10});
    system.Update(map, 1);
    ASSERT_EQ(system.GetInstances().size(), 2u);
    EXPECT_EQ(std::count_if(system.GetInstances().begin(), system.GetInstances().end(),
                            [](const auto& entry) { return entry.second.ownerId == 0; }), 1);
    EXPECT_EQ(std::count_if(system.GetInstances().begin(), system.GetInstances().end(),
                            [](const auto& entry) { return entry.second.ownerId == 1; }), 1);
    system.Update(map, 2);
    system.Update(map, 10);
    EXPECT_EQ(system.GetInstances().size(), 2u);
    system.Update(map, 11);
    EXPECT_EQ(system.GetInstances().size(), 4u);
}

TEST(WorldEventSystemTests, PeriodicRepeatCooldownAndCatalogSizeDoNotChangeCadence)
{
    WorldEventCatalog smallCatalog;
    smallCatalog.emplace("periodic", MakeDefinition(
        "periodic", WorldEventTriggerDomain::ProvincePeriodic, 5));
    GlobalMap smallMap;
    auto smallProvince = std::make_unique<BuildableProvince>(1, Vec2i{0, 0}, 0);
    ProvinceSimulation& smallSimulation = smallProvince->CreateSimulation();
    Player smallPlayer{0, smallSimulation};
    ASSERT_TRUE(smallMap.AddProvince(std::move(smallProvince)));

    WorldEventCatalog largeCatalog = smallCatalog;
    for (int index = 0; index < 20; ++index)
    {
        auto definition = MakeDefinition("extra_" + std::to_string(index),
                                         WorldEventTriggerDomain::ProvincePeriodic, 1);
        largeCatalog.emplace(definition.id, std::move(definition));
    }
    GlobalMap largeMap;
    auto largeProvince = std::make_unique<BuildableProvince>(1, Vec2i{0, 0}, 0);
    ProvinceSimulation& largeSimulation = largeProvince->CreateSimulation();
    Player largePlayer{0, largeSimulation};
    ASSERT_TRUE(largeMap.AddProvince(std::move(largeProvince)));

    const PeriodicEventScheduleDefinition schedule{1, 3, 10000, 6};
    ProvinceEventSystem smallSystem(100, &smallCatalog, schedule);
    ProvinceEventSystem largeSystem(100, &largeCatalog, schedule);
    for (std::uint64_t tick = 0; tick <= 20; ++tick)
    {
        smallSystem.Update(smallMap, tick);
        largeSystem.Update(largeMap, tick);
    }

    ASSERT_EQ(smallSystem.GetInstances().size(), largeSystem.GetInstances().size());
    auto startTicks = [](const ProvinceEventSystem& system)
    {
        std::vector<std::uint64_t> result;
        for (const auto& [id, instance] : system.GetInstances())
            result.push_back(instance.startTick);
        return result;
    };
    EXPECT_EQ(startTicks(smallSystem), startTicks(largeSystem));
    ASSERT_EQ(smallSystem.GetInstances().size(), 4u);
    EXPECT_EQ(smallSystem.GetInstances().begin()->second.startTick, 1u);
    EXPECT_EQ(smallSystem.GetInstances().rbegin()->second.startTick, 19u);

}

TEST(WorldEventSystemTests, PeriodicSameSeedProducesTheSameInstanceTrace)
{
    WorldEventCatalog catalog;
    catalog.emplace("periodic", MakeDefinition(
        "periodic", WorldEventTriggerDomain::ProvincePeriodic, 5));

    GlobalMap firstMap;
    auto firstProvince = std::make_unique<BuildableProvince>(1, Vec2i{0, 0}, 0);
    ProvinceSimulation& firstSimulation = firstProvince->CreateSimulation();
    Player firstPlayer{0, firstSimulation};
    ASSERT_TRUE(firstMap.AddProvince(std::move(firstProvince)));

    GlobalMap secondMap;
    auto secondProvince = std::make_unique<BuildableProvince>(1, Vec2i{0, 0}, 0);
    ProvinceSimulation& secondSimulation = secondProvince->CreateSimulation();
    Player secondPlayer{0, secondSimulation};
    ASSERT_TRUE(secondMap.AddProvince(std::move(secondProvince)));

    const PeriodicEventScheduleDefinition schedule{1, 3, 10000, 1};
    ProvinceEventSystem firstSystem(9001, &catalog, schedule);
    ProvinceEventSystem secondSystem(9001, &catalog, schedule);
    for (std::uint64_t tick = 0; tick <= 20; ++tick)
    {
        firstSystem.Update(firstMap, tick);
        secondSystem.Update(secondMap, tick);
    }

    ASSERT_EQ(firstSystem.GetInstances().size(), secondSystem.GetInstances().size());
    for (const auto& [id, firstInstance] : firstSystem.GetInstances())
    {
        const auto secondIt = secondSystem.GetInstances().find(id);
        ASSERT_NE(secondIt, secondSystem.GetInstances().end());
        const auto& secondInstance = secondIt->second;
        EXPECT_EQ(firstInstance.ownerId, secondInstance.ownerId);
        EXPECT_EQ(firstInstance.provinceId, secondInstance.provinceId);
        EXPECT_EQ(firstInstance.definitionId, secondInstance.definitionId);
        EXPECT_EQ(firstInstance.startTick, secondInstance.startTick);
        EXPECT_EQ(firstInstance.endTick, secondInstance.endTick);
        EXPECT_EQ(firstInstance.outcomeRoll, secondInstance.outcomeRoll);
        EXPECT_EQ(firstInstance.deterministicAttemptCounter,
                  secondInstance.deterministicAttemptCounter);
    }
}

TEST(WorldEventSystemTests, PeriodicSelectionNeverUsesAnIneligibleProvince)
{
    WorldEventCatalog catalog;
    auto definition = MakeDefinition(
        "fertile_periodic", WorldEventTriggerDomain::ProvincePeriodic, 1);
    definition.requiredTrait = "fertile";
    catalog.emplace(definition.id, definition);

    GlobalMap map;
    auto ineligible = std::make_unique<BuildableProvince>(1, Vec2i{0, 0}, 0);
    ProvinceSimulation& ineligibleSimulation = ineligible->CreateSimulation();
    Player player{0, ineligibleSimulation};
    auto eligible = std::make_unique<BuildableProvince>(2, Vec2i{1, 0}, 0);
    BuildableProvinceParameters parameters;
    parameters.traitIds = {"fertile"};
    eligible->SetParameters(parameters);
    ProvinceSimulation& eligibleSimulation = eligible->CreateSimulation();
    player.BindProvince(2, eligibleSimulation);
    ASSERT_TRUE(map.AddProvince(std::move(ineligible)));
    ASSERT_TRUE(map.AddProvince(std::move(eligible)));

    ProvinceEventSystem system(9002, &catalog,
                               PeriodicEventScheduleDefinition{1, 1, 10000, 1});
    system.Update(map, 1);
    ASSERT_EQ(system.GetInstances().size(), 1u);
    EXPECT_EQ(system.GetInstances().begin()->second.provinceId, 2u);
}

TEST(WorldEventSystemTests, PeriodicRepeatCooldownBlocksTheSameDefinition)
{
    WorldEventCatalog catalog;
    catalog.emplace("periodic", MakeDefinition(
        "periodic", WorldEventTriggerDomain::ProvincePeriodic, 100));
    GlobalMap map;
    auto province = std::make_unique<BuildableProvince>(1, Vec2i{0, 0}, 0);
    ProvinceSimulation& simulation = province->CreateSimulation();
    Player player{0, simulation};
    ASSERT_TRUE(map.AddProvince(std::move(province)));

    ProvinceEventSystem system(31, &catalog,
                               PeriodicEventScheduleDefinition{1, 1, 10000, 1});
    system.Update(map, 1);
    ASSERT_EQ(system.GetInstances().size(), 1u);
    for (std::uint64_t tick = 2; tick <= 100; ++tick)
        system.Update(map, tick);
    EXPECT_EQ(system.GetInstances().size(), 1u);
    system.Update(map, 101);
    EXPECT_EQ(system.GetInstances().size(), 2u);
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
    auto province = std::make_unique<BuildableProvince>(1, Vec2i{0, 0}, 0);
    ProvinceSimulation& simulation = province->CreateSimulation();
    Player player{0, simulation};
    ASSERT_TRUE(map.AddProvince(std::move(province)));
    ProvinceEventSystem system(123, &catalog,
                               PeriodicEventScheduleDefinition{1, 10, 10000, 1});
    system.Update(map, 1);

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
    }
    ASSERT_NE(localMap.PlaceLoadedBuilding(
        localMap.GetIdFromCoords({2, 2}), &player,
        std::make_unique<StorageBuilding>(10)), nullptr);
    ASSERT_NE(localMap.PlaceLoadedBuilding(
        localMap.GetIdFromCoords({8, 8}), &player,
        std::make_unique<StorageBuilding>(20)), nullptr);

    GlobalMap map;
    ASSERT_TRUE(map.AddProvince(std::move(province)));
    ProvinceEventSystem system(456, &catalog,
                               PeriodicEventScheduleDefinition{1, 10, 10000, 1});
    system.Update(map, 1);

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
