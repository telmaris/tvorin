#include "world/WorldEventDefinition.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

TEST(WorldEventDefinitionTests, ShippedCatalogIsTypedAndValid)
{
    const auto& catalog = GetWorldEventCatalog();
    ASSERT_GE(catalog.size(), 11u);
    const auto& schedule = GetPeriodicEventScheduleDefinition();
    EXPECT_EQ(schedule.initialDelayTicks, 6000u);
    EXPECT_EQ(schedule.checkIntervalTicks, 3000u);
    EXPECT_EQ(schedule.occurrenceChanceBasisPoints, 2500);
    EXPECT_EQ(schedule.minimumGapTicks, 6000u);
    const auto* ambush = FindWorldEventDefinition("bandit_ambush");
    ASSERT_NE(ambush, nullptr);
    EXPECT_EQ(ambush->trigger, WorldEventTriggerDomain::Route);
    ASSERT_EQ(ambush->effects.size(), 1u);
    EXPECT_EQ(std::get<KillJourneyUnitsEffect>(ambush->effects.front()).amount, 1);
    const auto* raid = FindWorldEventDefinition("bandit_raid");
    ASSERT_NE(raid, nullptr);
    EXPECT_EQ(raid->trigger, WorldEventTriggerDomain::ProvincePeriodic);
    EXPECT_EQ(raid->repeatCooldownTicks, 30000u);
    EXPECT_EQ(raid->requiredAdjacentTrait, "ambush_country");
    ASSERT_EQ(raid->effects.size(), 1u);
    EXPECT_EQ(std::get<StartRaidEffect>(raid->effects.front()).strength, 20);
    const auto* harvest = FindWorldEventDefinition("frontier_harvest");
    ASSERT_NE(harvest, nullptr);
    EXPECT_EQ(harvest->repeatCooldownTicks, 12000u);
    EXPECT_EQ(std::get<ModifyResourceEffect>(harvest->effects.front()).type, ResourceType::WHEAT);
    const auto* completed = FindWorldEventDefinition("scout_mission_completed");
    ASSERT_NE(completed, nullptr);
    EXPECT_EQ(completed->trigger, WorldEventTriggerDomain::Discovery);
    EXPECT_EQ(completed->chanceBasisPoints, 0);
    EXPECT_TRUE(completed->effects.empty());
    ASSERT_NE(FindWorldEventDefinition("scout_mission_failed"), nullptr);
}

TEST(WorldEventDefinitionTests, ParserRejectsUnknownEffectAndIllegalDestroy)
{
    const auto path = std::filesystem::temp_directory_path() / "tvorin_events_invalid.rtsdata";
    {
        std::ofstream out(path);
        ASSERT_TRUE(out.is_open());
        out << "event invalid\n"
               "name Invalid\n"
               "title Invalid\n"
               "trigger Discovery\n"
               "chance_bp 100\n"
               "weight 1\n"
               "duration_ticks 0\n"
               "effect DestroyBuilding 1\n"
               "end\n";
    }
    const auto result = LoadWorldEventDefinitionsFromFile(path.string());
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    EXPECT_FALSE(result.IsValid());
    EXPECT_TRUE(result.definitions.empty());
    EXPECT_FALSE(result.diagnostics.empty());
}

TEST(WorldEventDefinitionTests, ParserAcceptsTimedModifierAndConditions)
{
    const auto path = std::filesystem::temp_directory_path() / "tvorin_events_valid.rtsdata";
    {
        std::ofstream out(path);
        ASSERT_TRUE(out.is_open());
        out << "event valid\n"
               "name Valid\n"
               "title Valid\n"
               "description Desc\n"
               "trigger Raid\n"
               "chance_bp 100\n"
               "weight 2\n"
               "duration_ticks 50\n"
               "allow_kind Buildable\n"
               "require_trait fortified\n"
               "effect AddTimedModifier RaidStockLossFraction 0 0.8 50\n"
               "effect StartRaid 4\n"
               "end\n";
    }
    const auto result = LoadWorldEventDefinitionsFromFile(path.string());
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    ASSERT_TRUE(result.IsValid());
    ASSERT_EQ(result.definitions.at("valid").effects.size(), 2u);
    EXPECT_EQ(std::get<AddTimedModifierEffect>(result.definitions.at("valid").effects[0]).stat,
              BalanceStat::RaidStockLossFraction);
}

TEST(WorldEventDefinitionTests, ParserAcceptsPeriodicScheduleAndRepeatCooldown)
{
    const auto path = std::filesystem::temp_directory_path() / "tvorin_events_schedule.rtsdata";
    {
        std::ofstream out(path);
        ASSERT_TRUE(out.is_open());
        out << "periodic_schedule initial_delay_ticks 12 check_interval_ticks 7 chance_bp 4000 minimum_gap_ticks 19\n"
               "event periodic\n"
               "trigger ProvincePeriodic\n"
               "polarity Positive\n"
               "weight 3\n"
               "repeat_cooldown_ticks 42\n"
               "duration_ticks 0\n"
               "allow_kind Buildable\n"
               "effect ModifyResource WHEAT 1\n"
               "end\n";
    }
    const auto result = LoadWorldEventDefinitionsFromFile(path.string());
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    ASSERT_TRUE(result.IsValid());
    EXPECT_EQ(result.periodicSchedule.initialDelayTicks, 12u);
    EXPECT_EQ(result.periodicSchedule.checkIntervalTicks, 7u);
    EXPECT_EQ(result.periodicSchedule.occurrenceChanceBasisPoints, 4000);
    EXPECT_EQ(result.periodicSchedule.minimumGapTicks, 19u);
    EXPECT_EQ(result.definitions.at("periodic").repeatCooldownTicks, 42u);
    EXPECT_EQ(result.definitions.at("periodic").chanceBasisPoints, 0);
}
