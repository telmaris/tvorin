#include "world/WorldJourney.h"

#include <gtest/gtest.h>

namespace
{
    GlobalMap MakeJourneyMap()
    {
        GlobalMap map;
        map.AddProvince(std::make_unique<StaticProvince>(1, ProvinceKind::Buildable, Vec2i{0, 0}));
        map.AddProvince(std::make_unique<StaticProvince>(2, ProvinceKind::NeutralSettlement, Vec2i{1, 0}));
        map.AddProvince(std::make_unique<StaticProvince>(3, ProvinceKind::BanditCamp, Vec2i{2, 0}));
        map.AddConnection(1, 2, 10);
        map.AddConnection(2, 3, 20);
        return map;
    }
}

TEST(WorldJourneyTests, MovesOneLegAtATimeAndEmitsPointerFreeEvents)
{
    GlobalMap map = MakeJourneyMap();
    WorldJourneySystem system;
    WorldJourney journey;
    journey.ownerId = 0;
    journey.sourceProvinceId = 1;
    journey.targetProvinceId = 3;
    journey.legPlan = {{10}, {20}};
    journey.payload = ScoutParty{{7}};
    const auto start = system.Start(journey, map, 100, WorldJourneyRules{10});
    ASSERT_TRUE(start) << start.failureReason;
    ASSERT_EQ(system.GetJourneys().size(), 1u);
    const auto id = system.GetJourneys().begin()->first;
    EXPECT_EQ(system.GetJourneys().at(id).status, WorldJourneyStatus::InTransit);

    system.Update(map, 109);
    EXPECT_TRUE(system.ConsumeLegEvents().empty());
    system.Update(map, 110);
    auto events = system.ConsumeLegEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events.front().fromProvinceId, 1u);
    EXPECT_EQ(events.front().toProvinceId, 2u);
    EXPECT_FALSE(events.front().journeySucceeded);
    EXPECT_EQ(system.GetJourneys().at(id).currentLeg, 1u);

    system.Update(map, 119);
    EXPECT_TRUE(system.ConsumeLegEvents().empty());
    system.Update(map, 120);
    events = system.ConsumeLegEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events.front().fromProvinceId, 2u);
    EXPECT_EQ(events.front().toProvinceId, 3u);
    EXPECT_TRUE(events.front().journeySucceeded);
    EXPECT_EQ(system.GetJourneys().at(id).status, WorldJourneyStatus::Succeeded);
}

TEST(WorldJourneyTests, InvalidPathAndFailedJourneyNeverPretendToSucceed)
{
    GlobalMap map = MakeJourneyMap();
    WorldJourneySystem system;
    WorldJourney invalid;
    invalid.ownerId = 0;
    invalid.sourceProvinceId = 1;
    invalid.targetProvinceId = 3;
    invalid.legPlan = {{20}};
    const auto invalidStart = system.Start(invalid, map, 0, WorldJourneyRules{10});
    EXPECT_FALSE(invalidStart);
    EXPECT_FALSE(invalidStart.failureReason.empty());

    WorldJourney valid = invalid;
    valid.legPlan = {{10}, {20}};
    ASSERT_TRUE(system.Start(valid, map, 0, WorldJourneyRules{10}));
    const auto id = system.GetJourneys().begin()->first;
    ASSERT_TRUE(system.FailJourney(id));
    EXPECT_EQ(system.GetJourneys().at(id).status, WorldJourneyStatus::Failed);
    system.Update(map, 1000);
    EXPECT_TRUE(system.ConsumeLegEvents().empty());
}

TEST(WorldJourneyTests, CapturesRouteSpeedRulesAndAppliesThemToEveryLeg)
{
    GlobalMap map = MakeJourneyMap();
    WorldJourneySystem system;
    WorldJourney journey;
    journey.ownerId = 0;
    journey.sourceProvinceId = 1;
    journey.targetProvinceId = 3;
    journey.legPlan = {{10}, {20}};
    journey.payload = TradeCargo{ResourceType::COINS, ResourceType::STONE, 1};

    WorldJourneyRules rules;
    rules.baseLegDurationTicks = 10;
    rules.routeTravelSpeedMultiplier = 2.0;
    const auto start = system.Start(journey, map, 100, rules);
    ASSERT_TRUE(start) << start.failureReason;
    const auto id = system.GetJourneys().begin()->first;
    ASSERT_EQ(system.GetJourneys().at(id).rules.routeTravelSpeedMultiplier, 2.0);
    EXPECT_EQ(system.GetJourneys().at(id).legCompletionTick, 105u);

    system.Update(map, 104);
    EXPECT_TRUE(system.ConsumeLegEvents().empty());
    system.Update(map, 105);
    auto events = system.ConsumeLegEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_FALSE(events.front().journeySucceeded);
    EXPECT_EQ(system.GetJourneys().at(id).legCompletionTick, 110u);

    system.Update(map, 109);
    EXPECT_TRUE(system.ConsumeLegEvents().empty());
    system.Update(map, 110);
    events = system.ConsumeLegEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_TRUE(events.front().journeySucceeded);
}

TEST(WorldJourneyTests, FinalLegIncidentConsumesOneScoutAndOnlyFailsAnEmptyParty)
{
    GlobalMap map = MakeJourneyMap();
    WorldJourneySystem system;
    WorldJourney journey;
    journey.ownerId = 0;
    journey.sourceProvinceId = 1;
    journey.targetProvinceId = 2;
    journey.legPlan = {{10}};
    journey.payload = ScoutParty{{9, 3}};
    const auto start = system.Start(journey, map, 0, WorldJourneyRules{10});
    ASSERT_TRUE(start) << start.failureReason;
    const auto id = system.GetJourneys().begin()->first;

    system.Update(map, 10);
    ASSERT_EQ(system.GetJourneys().at(id).status, WorldJourneyStatus::Succeeded);
    EXPECT_EQ(system.ApplyScoutUnitLoss(id, 1), std::vector<int>({3}));
    EXPECT_EQ(system.GetJourneys().at(id).status, WorldJourneyStatus::Succeeded);
    const auto* survivors = std::get_if<ScoutParty>(&system.GetJourneys().at(id).payload);
    ASSERT_NE(survivors, nullptr);
    EXPECT_EQ(survivors->unitInstanceIds, std::vector<int>({9}));

    EXPECT_EQ(system.ApplyScoutUnitLoss(id, 1), std::vector<int>({9}));
    EXPECT_EQ(system.GetJourneys().at(id).status, WorldJourneyStatus::Failed);
}
