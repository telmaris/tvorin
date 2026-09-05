#include "warfare/WarfareViews.h"

#include <gtest/gtest.h>

TEST(WarfareViewsTests, JourneyViewExposesOnlyStableProgressAndPayloadKind)
{
    WorldJourney journey;
    journey.id = 7;
    journey.ownerId = 2;
    journey.sourceProvinceId = 10;
    journey.targetProvinceId = 20;
    journey.kind = WorldJourneyKind::Attack;
    journey.legPlan = {{3, 1, 10000, 0, 0, 25}, {4, 1, 10000, 0, 0, 25}};
    journey.currentLeg = 1;
    journey.startTick = 100;
    journey.legCompletionTick = 150;
    journey.payload = ArmyParty{{11, 12}};

    const auto view = BuildJourneyStatusView(journey, 125);
    EXPECT_EQ(view.journeyId, 7u);
    EXPECT_EQ(view.totalLegs, 2u);
    EXPECT_EQ(view.remainingTicks, 25u);
    EXPECT_EQ(view.kind, WorldJourneyKind::Attack);
}

TEST(WarfareViewsTests, BattleViewHidesExactForceClassFromUninvolvedViewer)
{
    BattleInstance battle;
    battle.id = 5;
    battle.attackerId = 1;
    battle.defenderId = 2;
    battle.endTick = 200;
    battle.attackerSnapshot.units.push_back({10, "militia", 20.0});
    battle.defenderSnapshot.units.push_back({20, "swordsman", 40.0});

    const auto hidden = BuildBattleStatusView(battle, 150, 3);
    EXPECT_EQ(hidden.attackerForceClass, WorldForceClass::Unknown);
    EXPECT_EQ(hidden.defenderForceClass, WorldForceClass::Unknown);

    const auto own = BuildBattleStatusView(battle, 150, 1);
    EXPECT_NE(own.attackerForceClass, WorldForceClass::Unknown);
    EXPECT_EQ(own.defenderForceClass, WorldForceClass::Unknown);
}

TEST(WarfareViewsTests, ReportViewCopiesOutcomeWithoutSimulationPointers)
{
    BattleReport report;
    report.battleId = 9;
    report.outcome.winner = BattleWinner::Attacker;
    report.outcome.attackerLostUnitIds = {1, 2};
    report.outcome.defenderLostUnitIds = {3};
    report.destroyedBuildingIds = {44};
    report.lostResources[ResourceType::WOOD] = 6;

    const auto view = BuildBattleReportView(report);
    EXPECT_EQ(view.winner, BattleWinner::Attacker);
    EXPECT_EQ(view.attackerLosses, 2);
    EXPECT_EQ(view.defenderLosses, 1);
    EXPECT_EQ(view.destroyedBuildingIds, std::vector<int>({44}));
    EXPECT_EQ(view.lostResources.at(ResourceType::WOOD), 6);
}

TEST(WarfareViewsTests, StatusStringsAreStableForUi)
{
    EXPECT_STREQ(ToString(WorldJourneyStatus::AwaitingUnload), "Awaiting unload");
    EXPECT_STREQ(ToString(BattleLifecycleStatus::Active), "Active");
    EXPECT_STREQ(ToString(BattleWinner::Draw), "Draw");
    EXPECT_STREQ(ToString(GarrisonSupplyStatus::RequestPending), "Request pending");
}
