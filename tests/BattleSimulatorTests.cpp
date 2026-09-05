#include "warfare/BattleSimulator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <initializer_list>

namespace
{
    BattleSideSnapshot Side(PlayerId owner, std::initializer_list<double> attacks,
                            double defense = 0.0)
    {
        BattleSideSnapshot side;
        side.ownerId = owner;
        side.defensiveBonus = defense;
        int id = owner * 100 + 1;
        for (const double attack : attacks)
            side.units.push_back({id++, "test_unit", attack});
        return side;
    }
}

TEST(BattleSimulatorTests, ShippedRulesAreDataDriven)
{
    const BattleRules& rules = GetBattleRules();
    EXPECT_DOUBLE_EQ(rules.baseLossFraction, 0.20);
    EXPECT_LT(rules.casualtyCapFraction, 1.0);
    EXPECT_GT(rules.durationTicks, 0u);
}

TEST(BattleSimulatorTests, IdenticalSnapshotsAndSeedProduceIdenticalOutcome)
{
    const auto attacker = Side(0, {10.0, 7.0, 4.0});
    const auto defender = Side(1, {5.0, 5.0});
    const BattleRules rules = GetBattleRules();
    const auto first = BattleSimulator::Resolve(attacker, defender, rules, 12345);
    const auto second = BattleSimulator::Resolve(attacker, defender, rules, 12345);
    EXPECT_EQ(first.valid, second.valid);
    EXPECT_EQ(first.winner, second.winner);
    EXPECT_EQ(first.attackerLostUnitIds, second.attackerLostUnitIds);
    EXPECT_EQ(first.defenderLostUnitIds, second.defenderLostUnitIds);
    EXPECT_DOUBLE_EQ(first.lootValue, second.lootValue);
}

TEST(BattleSimulatorTests, OrdinaryBattleLeavesSurvivorsAndLossesBelongToInput)
{
    const auto attacker = Side(0, {10.0, 9.0, 8.0, 7.0});
    const auto defender = Side(1, {10.0, 9.0, 8.0, 7.0});
    const auto outcome = BattleSimulator::Resolve(attacker, defender, GetBattleRules(), 9);
    ASSERT_TRUE(outcome.valid);
    EXPECT_FALSE(outcome.crushingVictory);
    EXPECT_LT(outcome.attackerLostUnitIds.size(), attacker.units.size());
    EXPECT_LT(outcome.defenderLostUnitIds.size(), defender.units.size());
    for (const int id : outcome.attackerLostUnitIds)
        EXPECT_TRUE(std::any_of(attacker.units.begin(), attacker.units.end(),
                               [id](const auto& unit) { return unit.instanceId == id; }));
    for (const int id : outcome.defenderLostUnitIds)
        EXPECT_TRUE(std::any_of(defender.units.begin(), defender.units.end(),
                               [id](const auto& unit) { return unit.instanceId == id; }));
}

TEST(BattleSimulatorTests, RejectsEmptyBattleAndDuplicateOrNegativeLossInputs)
{
    const BattleSideSnapshot emptyAttacker{0, {}, 0.0};
    const BattleSideSnapshot emptyDefender{1, {}, 0.0};
    EXPECT_FALSE(BattleSimulator::Resolve(emptyAttacker, emptyDefender,
                                          GetBattleRules(), 1).valid);

    auto attacker = Side(0, {5.0, 5.0});
    attacker.units[1].instanceId = attacker.units[0].instanceId;
    EXPECT_FALSE(BattleSimulator::Resolve(attacker, Side(1, {5.0}),
                                          GetBattleRules(), 1).valid);

    attacker = Side(0, {-1.0});
    EXPECT_FALSE(BattleSimulator::Resolve(attacker, Side(1, {5.0}),
                                          GetBattleRules(), 1).valid);
}

TEST(BattleSimulatorTests, StrongerSideDoesNotLoseMoreOftenAcrossSeeds)
{
    const auto stronger = Side(0, {12.0, 10.0, 8.0});
    const auto weaker = Side(1, {5.0, 5.0, 5.0});
    int strongerWins = 0;
    int weakerWins = 0;
    for (BattleSeed seed = 0; seed < 128; ++seed)
    {
        const auto outcome = BattleSimulator::Resolve(stronger, weaker,
                                                      GetBattleRules(), seed);
        if (outcome.winner == BattleWinner::Attacker) ++strongerWins;
        if (outcome.winner == BattleWinner::Defender) ++weakerWins;
    }
    EXPECT_GE(strongerWins, weakerWins);
}
