#include "economy/BalanceStatCatalog.h"
#include "economy/BalanceStatDisplay.h"
#include "research/Technology.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <set>

TEST(BalanceContractTests, NewStatsHaveStableParserAndDisplayContracts)
{
    const auto path = std::filesystem::temp_directory_path() / "tvorin_balance_contracts.rtsdata";
    {
        std::ofstream out(path);
        ASSERT_TRUE(out.is_open());
        out << "technology contract_test\n"
               "name Contract\n"
               "modifier RouteTravelSpeed multiplier 1.1\n"
               "modifier TradeScoreGain additive 2\n"
               "modifier BattleAttack multiplier 1.2\n"
               "modifier GarrisonFoodUpkeep multiplier 0.9\n"
               "end\n";
    }
    const auto definitions = LoadTechnologyDefinitionsFromFile(path.string());
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    ASSERT_EQ(definitions.size(), 1u);
    ASSERT_EQ(definitions.front().modifiers.size(), 4u);
    EXPECT_EQ(definitions.front().modifiers[0].stat, BalanceStat::RouteTravelSpeed);
    EXPECT_EQ(definitions.front().modifiers[1].stat, BalanceStat::TradeScoreGain);
    EXPECT_EQ(definitions.front().modifiers[2].stat, BalanceStat::BattleAttack);
    EXPECT_EQ(definitions.front().modifiers[3].stat, BalanceStat::GarrisonFoodUpkeep);
    EXPECT_STRNE(BalanceStatLabel(BalanceStat::RouteTravelSpeed), "Effect");
    EXPECT_STRNE(BalanceStatLabel(BalanceStat::ProvinceEventDuration), "Effect");
    EXPECT_TRUE(LowerValueIsBetter(BalanceStat::RouteIncidentChance));
    EXPECT_FALSE(LowerValueIsBetter(BalanceStat::GarrisonCapacity));
}

TEST(BalanceContractTests, ShippedTechAndFocusAssetsUseNewStats)
{
    bool foundTech = false;
    for (const auto& definition : GetTechnologyDefinitions())
        for (const auto& modifier : definition.modifiers)
            foundTech |= modifier.stat == BalanceStat::RouteTravelSpeed;
    bool foundFocus = false;
    for (const auto& definition : GetFocusDefinitions())
        for (const auto& modifier : definition.modifiers)
            foundFocus |= modifier.stat == BalanceStat::GarrisonCapacity;
    EXPECT_TRUE(foundTech);
    EXPECT_TRUE(foundFocus);
}

TEST(BalanceContractTests, CatalogRoundTripsEveryGameplayStatAndExcludesSentinel)
{
    const auto& catalog = GetBalanceStatCatalog();
    ASSERT_EQ(catalog.size(), static_cast<std::size_t>(BalanceStat::Count));
    std::set<std::string> names;
    for (const auto& entry : catalog)
    {
        ASSERT_NE(entry.serializedName, nullptr);
        EXPECT_TRUE(names.insert(entry.serializedName).second);
        const auto parsed = TryParseBalanceStat(entry.serializedName);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(*parsed, entry.value);
        EXPECT_NE(FindBalanceStatCatalogEntry(entry.value), nullptr);
    }
    EXPECT_FALSE(TryParseBalanceStat("Count").has_value());
    EXPECT_EQ(FindBalanceStatCatalogEntry(BalanceStat::Count), nullptr);
}
