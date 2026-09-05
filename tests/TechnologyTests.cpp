#include "research/Technology.h"
#include "data/RtsDataFile.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>

namespace
{
    // Writes a temporary technology data file for parser-level tests.
    std::filesystem::path WriteTechnologyFixture(const std::string& text)
    {
        const auto path = std::filesystem::temp_directory_path() / "rts_technology_fixture.rtsdata";
        std::ofstream file(path);
        file << text;
        return path;
    }

    bool ContainsTag(const TechnologyDefinition& definition, const std::string& tag)
    {
        return std::find(definition.tags.begin(), definition.tags.end(), tag) != definition.tags.end();
    }

    bool IsAllowedResearchTag(const std::string& tag)
    {
        return tag == "production" ||
               tag == "logistics" ||
               tag == "manpower" ||
               tag == "expansion" ||
               tag == "military" ||
               tag == "construction";
    }

    void ExpectEveryDefinitionBlockHasExplicitTags(const std::string& path)
    {
        const auto lines = ReadRtsDataLines(path);
        bool inDefinition = false;
        bool hasTags = false;
        int definitionCount = 0;

        for (const auto& tokens : lines)
        {
            if (tokens[0] == "technology")
            {
                if (inDefinition)
                    ADD_FAILURE() << "Nested technology block before end in " << path;
                inDefinition = true;
                hasTags = false;
                definitionCount++;
                continue;
            }

            if (!inDefinition)
                continue;

            if (tokens[0] == "tag" || tokens[0] == "tags")
                hasTags = true;
            else if (tokens[0] == "end")
            {
                EXPECT_TRUE(hasTags) << "Missing explicit tags before definition ending in " << path;
                inDefinition = false;
            }
        }

        EXPECT_FALSE(inDefinition) << "Unclosed technology block in " << path;
        EXPECT_GT(definitionCount, 0);
    }
}

TEST(TechnologyTests, LoadsTechnologyDataFileWithQuotedTextPrerequisitesCostsAndModifiers)
{
    const auto path = WriteTechnologyFixture(R"DATA(
# parser fixture
technology archery
    name "Archery Training"
    description "Unlocks better ranged troops."
    category WARFARE
    research_time 7.5
    requires forestry
    cost PAPER 3
    cost IRON_SWORD 2
    cost BRONZE_SWORD 1
    modifier BuildTime multiplier 0.75 building Barracks
    modifier ProductionOutputAmount additive 2 multiplier 1.25 building Woodcutter resource WOOD
    tags WARFARE military, archers expansion
end
technology invalid_only_name
end
)DATA");

    const auto definitions = LoadTechnologyDefinitionsFromFile(path.string());
    ASSERT_EQ(definitions.size(), 2u);

    const auto& archery = definitions.front();
    EXPECT_EQ(archery.id, "archery");
    EXPECT_EQ(archery.name, "Archery Training");
    EXPECT_EQ(archery.description, "Unlocks better ranged troops.");
    EXPECT_EQ(archery.category, "WARFARE");
    EXPECT_DOUBLE_EQ(archery.researchTime, 7.5);
    ASSERT_EQ(archery.prerequisites.size(), 1u);
    EXPECT_EQ(archery.prerequisites.front(), "forestry");
    ASSERT_EQ(archery.costs.size(), 3u);
    EXPECT_EQ(archery.costs[0].type, ResourceType::PAPER);
    EXPECT_EQ(archery.costs[1].type, ResourceType::IRON_SWORD);
    EXPECT_EQ(archery.costs[2].type, ResourceType::BRONZE_SWORD);

    ASSERT_EQ(archery.modifiers.size(), 2u);
    EXPECT_EQ(archery.modifiers[0].stat, BalanceStat::BuildTime);
    ASSERT_TRUE(archery.modifiers[0].buildingType.has_value());
    EXPECT_EQ(archery.modifiers[0].buildingType.value(), BuildingType::Barracks);
    EXPECT_EQ(archery.modifiers[1].resourceType, ResourceType::WOOD);
    EXPECT_DOUBLE_EQ(archery.modifiers[1].additive, 2.0);
    EXPECT_DOUBLE_EQ(archery.modifiers[1].multiplier, 1.25);
    EXPECT_TRUE(ContainsTag(archery, "military"));
    EXPECT_TRUE(ContainsTag(archery, "expansion"));
    EXPECT_FALSE(ContainsTag(archery, "warfare"));
    EXPECT_FALSE(ContainsTag(archery, "archers"));
    EXPECT_EQ(std::count(archery.tags.begin(), archery.tags.end(), "military"), 1);

    EXPECT_EQ(definitions[1].id, "invalid_only_name");
    EXPECT_EQ(definitions[1].name, "invalid_only_name");
}

TEST(TechnologyTests, ParsesTransportDispatchDelayAsLogisticsModifier)
{
    const auto path = WriteTechnologyFixture(R"DATA(
technology rapid_dispatch
    name "Rapid Dispatch"
    modifier TransportDispatchDelay multiplier 0.75 building StorageBuilding resource WOOD
end
)DATA");

    const auto definitions = LoadTechnologyDefinitionsFromFile(path.string());
    ASSERT_EQ(definitions.size(), 1u);
    ASSERT_EQ(definitions.front().modifiers.size(), 1u);

    const auto& modifier = definitions.front().modifiers.front();
    EXPECT_EQ(modifier.stat, BalanceStat::TransportDispatchDelay);
    EXPECT_DOUBLE_EQ(modifier.multiplier, 0.75);
    EXPECT_EQ(modifier.buildingType, BuildingType::StorageBuilding);
    EXPECT_EQ(modifier.resourceType, ResourceType::WOOD);
    EXPECT_TRUE(ContainsTag(definitions.front(), "logistics"));
}

TEST(TechnologyTests, ParsesVillageSupplyConsumptionWithIndependentResourceFilter)
{
    const auto path = WriteTechnologyFixture(R"DATA(
technology food_conservation
    name "Food Conservation"
    modifier VillageSupplyConsumption multiplier 0.9 building Village resource FOOD_PROVISIONS
end
)DATA");

    const auto definitions = LoadTechnologyDefinitionsFromFile(path.string());
    ASSERT_EQ(definitions.size(), 1u);
    ASSERT_EQ(definitions.front().modifiers.size(), 1u);

    const auto& modifier = definitions.front().modifiers.front();
    EXPECT_EQ(modifier.stat, BalanceStat::VillageSupplyConsumption);
    EXPECT_DOUBLE_EQ(modifier.multiplier, 0.9);
    EXPECT_EQ(modifier.buildingType, BuildingType::Village);
    EXPECT_EQ(modifier.resourceType, ResourceType::FOOD_PROVISIONS);
    EXPECT_TRUE(ContainsTag(definitions.front(), "manpower"));
    EXPECT_TRUE(ContainsTag(definitions.front(), "logistics"));
}

TEST(TechnologyTests, ParsesProvinceDefenseStatsAndUnitFilter)
{
    const auto path = WriteTechnologyFixture(R"DATA(
technology veteran_swordsmen
    name "Veteran Swordsmen"
    category WARFARE
    research_time 10
    modifier UnitFieldAttack multiplier 1.1 unit swordsman
    modifier UnitSiegePower additive 1
    modifier UnitRecruitTime multiplier 0.9
    modifier UnitRecruitManpowerCost multiplier 0.95
    modifier ProvinceFortification additive 50
    modifier ProvinceDefense additive 2
    modifier ProvinceCounterattack additive 1
    modifier ProvinceDefensePower multiplier 1.2
    modifier ProvinceDefenseCoverage additive 1
    modifier ProvinceDefenseReadiness multiplier 1.15
    modifier ProvinceDefenseSupplyUse additive -1
end
)DATA");

    const auto definitions = LoadTechnologyDefinitionsFromFile(path.string());
    ASSERT_EQ(definitions.size(), 1u);
    const auto& modifiers = definitions.front().modifiers;
    ASSERT_EQ(modifiers.size(), 11u);

    EXPECT_EQ(modifiers[0].stat, BalanceStat::UnitFieldAttack);
    ASSERT_TRUE(modifiers[0].unitDefId.has_value());
    EXPECT_EQ(modifiers[0].unitDefId.value(), "swordsman");

    EXPECT_EQ(modifiers[1].stat, BalanceStat::UnitSiegePower);
    EXPECT_EQ(modifiers[2].stat, BalanceStat::UnitRecruitTime);
    EXPECT_EQ(modifiers[3].stat, BalanceStat::UnitRecruitManpowerCost);
    EXPECT_EQ(modifiers[4].stat, BalanceStat::ProvinceFortification);
    EXPECT_EQ(modifiers[5].stat, BalanceStat::ProvinceDefense);
    EXPECT_EQ(modifiers[6].stat, BalanceStat::ProvinceCounterattack);
    EXPECT_EQ(modifiers[7].stat, BalanceStat::ProvinceDefensePower);
    EXPECT_EQ(modifiers[8].stat, BalanceStat::ProvinceDefenseCoverage);
    EXPECT_EQ(modifiers[9].stat, BalanceStat::ProvinceDefenseReadiness);
    EXPECT_EQ(modifiers[10].stat, BalanceStat::ProvinceDefenseSupplyUse);
}

// TEST(TechnologyTests, AssetTechnologyAndFocusDefinitionsDeclareExplicitTags)
// {
//     ExpectEveryDefinitionBlockHasExplicitTags("assets/data/technologies.rtsdata");
//     ExpectEveryDefinitionBlockHasExplicitTags("assets/data/focuses.rtsdata");
// }

TEST(TechnologyTests, LoadedAssetTagsAreNormalizedAndUnique)
{
    auto expectNormalizedTags = [](const std::vector<TechnologyDefinition>& definitions)
    {
        for (const auto& definition : definitions)
        {
            ASSERT_FALSE(definition.tags.empty()) << definition.id;
            for (const auto& tag : definition.tags)
            {
                EXPECT_FALSE(tag.empty()) << definition.id;
                EXPECT_TRUE(std::all_of(tag.begin(), tag.end(), [](unsigned char c)
                {
                    return !std::isupper(c);
                })) << definition.id << " tag " << tag;
                EXPECT_TRUE(IsAllowedResearchTag(tag)) << definition.id << " tag " << tag;
                EXPECT_EQ(std::count(definition.tags.begin(), definition.tags.end(), tag), 1)
                    << definition.id << " duplicate tag " << tag;
            }
        }
    };

    expectNormalizedTags(LoadTechnologyDefinitionsFromFile("assets/data/technologies.rtsdata"));
    expectNormalizedTags(LoadFocusDefinitionsFromFile("assets/data/focuses.rtsdata"));
}

TEST(TechnologyTests, MissingTechnologyDataUsesBuiltInDefaults)
{
    const auto definitions = LoadTechnologyDefinitionsFromFile("missing_technology_fixture.rtsdata");

    EXPECT_NE(std::find_if(definitions.begin(), definitions.end(), [](const TechnologyDefinition& definition)
    {
        return definition.id == "forestry";
    }), definitions.end());
}
