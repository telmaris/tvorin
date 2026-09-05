#include "warfare/BattleUnit.h"

#include <gtest/gtest.h>

TEST(UnitAssignmentTests, ServiceMaintainsExactlyOneTypedLocation)
{
    BattleUnit unit(1, 0, "militia");
    ASSERT_TRUE(UnitAssignmentService::AssignReserve(unit, 3, 42));
    EXPECT_EQ(unit.assignment.kind, UnitAssignmentKind::BarracksReserve);
    EXPECT_TRUE(UnitAssignmentService::IsInReserve(unit, 3));
    EXPECT_EQ(unit.assignment.provinceId, 3u);
    EXPECT_EQ(unit.assignment.worldJourneyId, InvalidWorldJourneyId);
    EXPECT_EQ(unit.assignment.battleId, InvalidBattleId);

    ASSERT_TRUE(UnitAssignmentService::AssignJourney(unit, 3, 9));
    EXPECT_EQ(unit.assignment.kind, UnitAssignmentKind::Journey);
    EXPECT_FALSE(UnitAssignmentService::IsInReserve(unit, 3));
    EXPECT_TRUE(UnitAssignmentService::IsOnJourney(unit, 9));

    ASSERT_TRUE(UnitAssignmentService::AssignBattle(unit, 4, 12, 42));
    EXPECT_EQ(unit.assignment.kind, UnitAssignmentKind::Battle);
    EXPECT_EQ(unit.assignment.buildingId, 42);
    EXPECT_TRUE(UnitAssignmentService::IsInBattle(unit, 12));
    EXPECT_FALSE(UnitAssignmentService::IsOnJourney(unit, 9));
}

TEST(UnitAssignmentTests, JourneyAndBattleKeepOriginBarracksForReturnToReserve)
{
    BattleUnit unit(1, 0, "militia");
    ASSERT_TRUE(UnitAssignmentService::AssignReserve(unit, 3, 42));
    ASSERT_TRUE(UnitAssignmentService::AssignJourney(unit, 3, 9, 42));

    EXPECT_EQ(unit.assignment.buildingId, 42);

    ASSERT_TRUE(UnitAssignmentService::AssignBattle(unit, 4, 12,
                                                    unit.assignment.buildingId));
    EXPECT_EQ(unit.assignment.buildingId, 42);
    ASSERT_TRUE(UnitAssignmentService::AssignReserve(unit, 3, unit.assignment.buildingId));
    EXPECT_EQ(unit.assignment.kind, UnitAssignmentKind::BarracksReserve);
    EXPECT_EQ(unit.assignment.buildingId, 42);
}

TEST(UnitAssignmentTests, NewUnitHasExplicitUnassignedState)
{
    BattleUnit unit(1, 0, "scout");
    EXPECT_TRUE(unit.assignment.IsStructurallyValid());
    EXPECT_EQ(unit.assignment.kind, UnitAssignmentKind::Unassigned);
    EXPECT_FALSE(UnitAssignmentService::IsInReserve(unit, 8));
}

TEST(UnitAssignmentTests, InvalidTransitionsDoNotReplaceCurrentAssignment)
{
    BattleUnit unit(1, 0, "militia");
    ASSERT_TRUE(UnitAssignmentService::AssignReserve(unit, 3, 42));
    const UnitAssignment before = unit.assignment;
    EXPECT_FALSE(UnitAssignmentService::AssignBattle(unit, InvalidProvinceId, 4));
    EXPECT_EQ(unit.assignment.kind, before.kind);
    EXPECT_EQ(unit.assignment.provinceId, before.provinceId);
    EXPECT_EQ(unit.assignment.buildingId, before.buildingId);
}
