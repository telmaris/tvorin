#include "world/ProvinceDefinition.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace
{
    class ProvinceDefinitionFixture
    {
    public:
        explicit ProvinceDefinitionFixture(std::string contents)
            : path(std::filesystem::temp_directory_path() /
                   "tvorin_province_definition_fixture.rtsdata")
        {
            std::ofstream file(path);
            file << contents;
        }

        ~ProvinceDefinitionFixture()
        {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }

        std::filesystem::path path;
    };

    std::string MinimalProvince(std::string id, std::string extra = {})
    {
        return "province " + std::move(id) + "\n"
               "archetype Buildable\n"
               "display_name \"Test Province\"\n"
               "weight 1\n"
               "size_x 201 301\n"
               "size_y 201 301\n"
               "resource_wealth 0.5 1.0\n"
               "resource_density 0.2 0.8\n"
               "resource_field_size 0.2 0.8\n"
               "resource_richness 0.2 0.8\n"
               "water_amount 0.0 0.2\n"
               "mountain_amount 0.0 0.2\n"
               "ruggedness 0.0 0.2\n" +
               extra + "end\n";
    }
}

TEST(ProvinceDefinitionTests, ShippedCatalogIsValidAndDataDriven)
{
    const auto result = LoadProvinceDefinitionCatalog("assets/data/provinces.rtsdata");
    ASSERT_TRUE(result.IsValid());
    ASSERT_EQ(result.definitions.size(), 4u);

    const auto buildable = result.definitions.find("frontier_buildable");
    ASSERT_NE(buildable, result.definitions.end());
    EXPECT_EQ(buildable->second.Kind(), ProvinceKind::Buildable);
    EXPECT_EQ(buildable->second.sizeX.min, 201);
    EXPECT_EQ(buildable->second.sizeX.max, 501);
    ASSERT_EQ(buildable->second.traits.size(), 1u);
    ASSERT_EQ(buildable->second.traits.front().effects.size(), 1u);
    EXPECT_EQ(buildable->second.traits.front().effects.front().stat,
              BalanceStat::ProductionOutputAmount);

    EXPECT_NE(FindProvinceDefinition("neutral_city"), nullptr);
    EXPECT_EQ(FindProvinceDefinition("does_not_exist"), nullptr);
}

TEST(ProvinceDefinitionTests, RejectsUnknownTokensAndDoesNotPartiallyAcceptBlock)
{
    ProvinceDefinitionFixture fixture(
        MinimalProvince("broken", "unknown_field 123\n") +
        MinimalProvince("valid_but_not_returned"));
    const auto result = LoadProvinceDefinitionCatalog(fixture.path.string());

    EXPECT_FALSE(result.IsValid());
    EXPECT_TRUE(result.definitions.empty());
    ASSERT_FALSE(result.diagnostics.empty());
    EXPECT_EQ(result.diagnostics.front().path, fixture.path.string());
    EXPECT_EQ(result.diagnostics.front().line, 14u);
}

TEST(ProvinceDefinitionTests, RejectsDuplicateIdNegativeWeightAndReversedRange)
{
    std::string negative = MinimalProvince("negative");
    negative.replace(negative.find("weight 1"), std::string("weight 1").size(), "weight -1");
    std::string reversed = MinimalProvince("reversed");
    reversed.replace(reversed.find("size_x 201 301"),
                    std::string("size_x 201 301").size(), "size_x 401 201");
    ProvinceDefinitionFixture fixture(
        MinimalProvince("same") +
        MinimalProvince("same") +
        negative + reversed);
    const auto result = LoadProvinceDefinitionCatalog(fixture.path.string());

    EXPECT_FALSE(result.IsValid());
    EXPECT_TRUE(result.definitions.empty());
    ASSERT_GE(result.diagnostics.size(), 3u);
    EXPECT_NE(result.diagnostics[0].message.find("duplicate"), std::string::npos);
    EXPECT_NE(result.diagnostics[1].message.find("weight"), std::string::npos);
    EXPECT_NE(result.diagnostics.back().message.find("size range"), std::string::npos);
}

TEST(ProvinceDefinitionTests, RejectsNonFiniteNumbersAndUnknownBalanceStats)
{
    ProvinceDefinitionFixture fixture(
        MinimalProvince("bad_values",
                       "resource_wealth nan 1.0\n"
                       "trait invalid_effect\n"
                       "effect Self NoSuchStat 0.0 1.0\n"));
    const auto result = LoadProvinceDefinitionCatalog(fixture.path.string());

    EXPECT_FALSE(result.IsValid());
    EXPECT_TRUE(result.definitions.empty());
    ASSERT_GE(result.diagnostics.size(), 2u);
    bool foundRangeError = false;
    bool foundStatError = false;
    for (const auto& diagnostic : result.diagnostics)
    {
        foundRangeError |= diagnostic.message.find("resource_wealth") != std::string::npos;
        foundStatError |= diagnostic.message.find("BalanceStat") != std::string::npos;
    }
    EXPECT_TRUE(foundRangeError);
    EXPECT_TRUE(foundStatError);
}
