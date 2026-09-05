#include "warfare/BattleLifecycle.h"
#include "economy/Player.h"

#include <gtest/gtest.h>

#include <memory>

namespace
{
    GlobalMap MakeBattleMap(bool revealTarget = true)
    {
        GlobalMap map;
        map.AddProvince(std::make_unique<BuildableProvince>(1, Vec2i{0, 0}, 0));
        auto bandit = std::make_unique<BanditProvince>(2, Vec2i{1, 0}, "bandit_camp");
        bandit->SetStrength(2);
        BuildableProvinceParameters future;
        future.definitionId = "frontier_buildable";
        future.sizeX = 201;
        future.sizeY = 201;
        future.localSeed = 123;
        bandit->SetFutureBuildableParameters(future);
        map.AddProvince(std::move(bandit));
        map.AddConnection(1, 2, 10);
        map.SetKnowledge(0, 1, ProvinceKnowledgeLevel::Owned);
        if (revealTarget)
            map.SetKnowledge(0, 2, ProvinceKnowledgeLevel::Scouted);
        return map;
    }
}

TEST(BattleLifecycleTests, ArmyTravelsBeforeSnapshotAndCrushingBanditVictoryTransformsProvince)
{
    GlobalMap map = MakeBattleMap();
    Player attacker(0);
    attacker.roster.AddUnit(BattleUnit(1, 0, "swordsman"));
    ASSERT_TRUE(UnitAssignmentService::AssignReserve(*attacker.roster.FindUnit(1), 1, 42));
    Player neutral(1);
    std::map<PlayerId, Player*> players{{0, &attacker}, {1, &neutral}};
    WorldJourneySystem journeys;
    BattleLifecycleSystem battles;
    BattleId battleId = InvalidBattleId;
    std::string failure;
    ASSERT_TRUE(battles.StartProvinceAttack(attacker, 1, 2, {1}, map, journeys,
                                             players, 0, 99, battleId, failure)) << failure;
    ASSERT_NE(battleId, InvalidBattleId);
    EXPECT_EQ(attacker.roster.FindUnit(1)->assignment.kind, UnitAssignmentKind::Journey);
    EXPECT_EQ(attacker.roster.FindUnit(1)->assignment.worldJourneyId,
              battles.GetBattles().at(battleId).journeyId);
    EXPECT_EQ(battles.GetBattles().at(battleId).status, BattleLifecycleStatus::InTransit);

    battles.Update(map, journeys, players, 99, 99);
    EXPECT_EQ(battles.GetBattles().at(battleId).status, BattleLifecycleStatus::InTransit);
    battles.Update(map, journeys, players, 100, 99);
    EXPECT_EQ(battles.GetBattles().at(battleId).status, BattleLifecycleStatus::Active);
    EXPECT_TRUE(battles.ConsumeReports().empty());

    battles.Update(map, journeys, players, 199, 99);
    EXPECT_TRUE(battles.ConsumeReports().empty());
    battles.Update(map, journeys, players, 200, 99);
    const auto reports = battles.ConsumeReports();
    ASSERT_EQ(reports.size(), 1u);
    EXPECT_EQ(reports.front().outcome.winner, BattleWinner::Attacker);
    EXPECT_TRUE(reports.front().outcome.crushingVictory);
    EXPECT_TRUE(reports.front().banditTransformed);
    EXPECT_EQ(map.FindProvince(2)->GetId(), 2u);
    EXPECT_EQ(map.FindProvince(2)->GetKind(), ProvinceKind::Buildable);
    EXPECT_EQ(attacker.roster.FindUnit(1)->assignment.kind,
              UnitAssignmentKind::BarracksReserve);
    EXPECT_TRUE(UnitAssignmentService::IsInReserve(*attacker.roster.FindUnit(1), 1));
}

TEST(BattleLifecycleTests, AuthorityRejectsUnknownTargetAndUnitsNotInSourceReserve)
{
    GlobalMap map = MakeBattleMap();
    Player attacker(0);
    attacker.roster.AddUnit(BattleUnit(1, 0, "swordsman"));
    std::map<PlayerId, Player*> players{{0, &attacker}};
    WorldJourneySystem journeys;
    BattleLifecycleSystem battles;
    BattleId battleId = InvalidBattleId;
    std::string failure;
    EXPECT_FALSE(battles.StartProvinceAttack(attacker, 1, 2, {1}, map, journeys,
                                              players, 0, 1, battleId, failure));
    ASSERT_TRUE(UnitAssignmentService::AssignReserve(*attacker.roster.FindUnit(1), 1, 42));
    GlobalMap hiddenMap = MakeBattleMap(false);
    EXPECT_FALSE(battles.StartProvinceAttack(attacker, 1, 2, {1}, hiddenMap, journeys,
                                              players, 0, 1, battleId, failure));
}
