#include "economy/BuildingConfig.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <map>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    struct Fraction
    {
        std::int64_t numerator{0};
        std::int64_t denominator{1};

        friend bool operator==(const Fraction& left, const Fraction& right)
        {
            return left.numerator * right.denominator == right.numerator * left.denominator;
        }
    };

    Fraction Reduce(Fraction value)
    {
        if (value.denominator < 0)
        {
            value.numerator = -value.numerator;
            value.denominator = -value.denominator;
        }
        const auto divisor = std::gcd(std::llabs(value.numerator), value.denominator);
        if (divisor > 1)
        {
            value.numerator /= divisor;
            value.denominator /= divisor;
        }
        return value;
    }

    Fraction Add(Fraction left, Fraction right)
    {
        return Reduce({left.numerator * right.denominator + right.numerator * left.denominator,
                       left.denominator * right.denominator});
    }

    Fraction Multiply(Fraction value, std::int64_t multiplier)
    {
        return Reduce({value.numerator * multiplier, value.denominator});
    }

    std::int64_t CycleMilliseconds(const ProductionDefinition& production)
    {
        return static_cast<std::int64_t>(std::llround(production.cycleTime * 1000.0));
    }

    std::optional<int> FindAmount(const std::vector<ResourceAmountDefinition>& amounts,
                                  ResourceType resource)
    {
        for (const auto& amount : amounts)
        {
            if (amount.type == resource)
                return amount.amount;
        }
        return std::nullopt;
    }

    std::optional<Fraction> RatePerMinute(const ProductionDefinition& production,
                                          ResourceType resource,
                                          bool input)
    {
        const auto amount = FindAmount(input ? production.inputs : production.outputs, resource);
        const auto cycleMilliseconds = CycleMilliseconds(production);
        if (!amount.has_value() || *amount <= 0 || cycleMilliseconds <= 0)
            return std::nullopt;
        return Reduce({static_cast<std::int64_t>(*amount) * 60000, cycleMilliseconds});
    }

    std::map<int, int> AmountSignature(const std::vector<ResourceAmountDefinition>& amounts)
    {
        std::map<int, int> result;
        for (const auto& amount : amounts)
            result[static_cast<int>(amount.type)] = amount.amount;
        return result;
    }

    std::map<int, std::pair<int, int>> BufferSignature(const std::vector<ResourceBufferDefinition>& buffers)
    {
        std::map<int, std::pair<int, int>> result;
        for (const auto& buffer : buffers)
        {
            result[static_cast<int>(buffer.type)] = {buffer.capacity, buffer.initialAmount};
        }
        return result;
    }

    bool SameProduction(const ProductionDefinition& left, const ProductionDefinition& right)
    {
        return CycleMilliseconds(left) == CycleMilliseconds(right) &&
               AmountSignature(left.inputs) == AmountSignature(right.inputs) &&
               AmountSignature(left.outputs) == AmountSignature(right.outputs) &&
               BufferSignature(left.inputBuffers) == BufferSignature(right.inputBuffers) &&
               BufferSignature(left.outputBuffers) == BufferSignature(right.outputBuffers);
    }

    void AuditBuffers(const std::string& building,
                      const std::string& section,
                      const ProductionDefinition& production,
                      std::vector<std::string>& violations,
                      std::ostringstream& report)
    {
        const auto cycleMilliseconds = CycleMilliseconds(production);
        report << building << ":" << section << " cycle=" << production.cycleTime << "s";
        for (const auto& output : production.outputs)
        {
            const auto rate = RatePerMinute(production, output.type, false);
            report << " out[" << static_cast<int>(output.type) << "]="
                   << (rate.has_value() ? std::to_string(rate->numerator) + "/" +
                                             std::to_string(rate->denominator) : "invalid");
        }
        report << "\n";

        if (!std::isfinite(production.cycleTime) || cycleMilliseconds <= 0)
        {
            violations.push_back(building + ":" + section + " has a non-positive cycle time");
            return;
        }

        const auto audit = [&](const ResourceAmountDefinition& amount,
                               const std::vector<ResourceBufferDefinition>& buffers,
                               const char* kind)
        {
            const auto buffer = std::find_if(buffers.begin(), buffers.end(),
                [&](const ResourceBufferDefinition& candidate)
                {
                    return candidate.type == amount.type;
                });
            if (buffer == buffers.end())
            {
                violations.push_back(building + ":" + section + " is missing " + kind + " buffer");
                return;
            }
            if (amount.amount <= 0 || buffer->capacity < amount.amount * 3 ||
                buffer->capacity % amount.amount != 0)
            {
                violations.push_back(building + ":" + section + " " + kind + " buffer for resource " +
                                     std::to_string(static_cast<int>(amount.type)) +
                                     " must hold at least three whole cycles");
            }
        };

        for (const auto& input : production.inputs)
            audit(input, production.inputBuffers, "input");
        for (const auto& output : production.outputs)
            audit(output, production.outputBuffers, "output");
    }

    const BuildingDefinition& GetDefinition(BuildingType type)
    {
        return GetBuildingDefinition(type);
    }

    Fraction RequireRate(const ProductionDefinition& production,
                         ResourceType resource,
                         bool input,
                         const char* description)
    {
        const auto rate = RatePerMinute(production, resource, input);
        EXPECT_TRUE(rate.has_value()) << description;
        return rate.value_or(Fraction{});
    }
}

TEST(ProductionBalanceTests, EveryProductionSectionHasAuditableBuffersAndRates)
{
    std::vector<std::string> violations;
    std::ostringstream report;

    for (const auto& definition : GetBuildingDefinitions())
    {
        if (!definition.production.inputs.empty() || !definition.production.outputs.empty())
            AuditBuffers(definition.name, "production", definition.production, violations, report);

        for (const auto& terrain : definition.terrainProductions)
        {
            AuditBuffers(definition.name, "terrain_production", terrain.production, violations, report);
        }

        for (const auto& recipe : definition.recipes)
            AuditBuffers(definition.name, "recipe " + recipe.name, recipe.production, violations, report);
    }

    EXPECT_TRUE(violations.empty()) << "Production rate report:\n" << report.str()
                                    << "Violations:\n";
    for (const auto& violation : violations)
        ADD_FAILURE() << violation;
}

TEST(ProductionBalanceTests, DefaultAndFirstEquivalentRecipeKeepTheSameThroughputContract)
{
    for (const auto& definition : GetBuildingDefinitions())
    {
        if (definition.recipes.empty() || definition.production.outputs.empty())
            continue;

        const auto defaultOutputs = AmountSignature(definition.production.outputs);
        const auto firstOutputs = AmountSignature(definition.recipes.front().production.outputs);
        if (defaultOutputs == firstOutputs)
        {
            EXPECT_TRUE(SameProduction(definition.production, definition.recipes.front().production))
                << definition.name << " default production differs from first equivalent recipe";
        }
    }
}

TEST(ProductionBalanceTests, ReferenceFoodClusterHasExactRatesAndFeedsVillageLevelOne)
{
    const auto& well = GetDefinition(BuildingType::Well).production;
    const auto& wheatFarm = GetDefinition(BuildingType::WheatFarm).production;
    const auto& windmill = GetDefinition(BuildingType::Windmill).production;
    const auto& bakery = GetDefinition(BuildingType::Bakery).production;
    const auto& huntersHut = GetDefinition(BuildingType::HuntersHut).terrainProductions.front().production;
    const auto& inn = GetDefinition(BuildingType::Inn).production;
    const auto& village = GetDefinition(BuildingType::Village).village;

    EXPECT_EQ(CycleMilliseconds(well), 6000);
    EXPECT_EQ(CycleMilliseconds(wheatFarm), 12000);
    EXPECT_EQ(CycleMilliseconds(windmill), 12000);
    EXPECT_EQ(CycleMilliseconds(bakery), 12000);
    EXPECT_EQ(CycleMilliseconds(huntersHut), 12000);
    EXPECT_EQ(CycleMilliseconds(inn), 24000);

    const auto wellWater = RequireRate(well, ResourceType::WATER, false, "Well WATER");
    const auto wheatWater = RequireRate(wheatFarm, ResourceType::WATER, true, "WheatFarm WATER");
    const auto wheatOutput = RequireRate(wheatFarm, ResourceType::WHEAT, false, "WheatFarm WHEAT");
    const auto windmillInput = RequireRate(windmill, ResourceType::WHEAT, true, "Windmill WHEAT");
    const auto windmillOutput = RequireRate(windmill, ResourceType::FLOUR, false, "Windmill FLOUR");
    const auto bakeryFlour = RequireRate(bakery, ResourceType::FLOUR, true, "Bakery FLOUR");
    const auto bakeryWater = RequireRate(bakery, ResourceType::WATER, true, "Bakery WATER");
    const auto bakeryBread = RequireRate(bakery, ResourceType::BREAD, false, "Bakery BREAD");
    const auto hunterMeat = RequireRate(huntersHut, ResourceType::MEAT, false, "HuntersHut MEAT");
    const auto innBread = RequireRate(inn, ResourceType::BREAD, true, "Inn BREAD");
    const auto innMeat = RequireRate(inn, ResourceType::MEAT, true, "Inn MEAT");
    const auto innWater = RequireRate(inn, ResourceType::WATER, true, "Inn WATER");
    const auto innFood = RequireRate(inn, ResourceType::FOOD_PROVISIONS, false, "Inn FOOD_PROVISIONS");

    EXPECT_TRUE(wellWater == (Fraction{20, 1}));
    EXPECT_TRUE(wheatWater == (Fraction{20, 1}));
    EXPECT_TRUE(wheatOutput == (Fraction{20, 1}));
    EXPECT_TRUE(windmillInput == (Fraction{20, 1}));
    EXPECT_TRUE(windmillOutput == (Fraction{10, 1}));
    EXPECT_TRUE(bakeryFlour == (Fraction{10, 1}));
    EXPECT_TRUE(bakeryWater == (Fraction{10, 1}));
    EXPECT_TRUE(bakeryBread == (Fraction{10, 1}));
    EXPECT_TRUE(hunterMeat == (Fraction{5, 1}));
    EXPECT_TRUE(innBread == (Fraction{20, 1}));
    EXPECT_TRUE(innMeat == (Fraction{15, 1}));
    EXPECT_TRUE(innWater == (Fraction{20, 1}));
    EXPECT_TRUE(innFood == (Fraction{5, 1}));

    EXPECT_TRUE(Multiply(wellWater, 4) ==
                Add(Add(Multiply(wheatWater, 2), Multiply(bakeryWater, 2)), innWater));
    EXPECT_TRUE(Multiply(wheatOutput, 2) == Multiply(windmillInput, 2));
    EXPECT_TRUE(Multiply(windmillOutput, 2) == Multiply(bakeryFlour, 2));
    EXPECT_TRUE(Multiply(bakeryBread, 2) == innBread);
    EXPECT_TRUE(Multiply(hunterMeat, 3) == innMeat);

    ASSERT_FALSE(village.supplyRules.empty());
    const auto& foodRule = village.supplyRules.front();
    ASSERT_EQ(foodRule.resource, ResourceType::FOOD_PROVISIONS);
    const auto villageLevelOneConsumption = Reduce({
        static_cast<std::int64_t>(village.populationCap) * foodRule.packageAmount * 60000,
        static_cast<std::int64_t>(std::llround(foodRule.residentsPerPackage * foodRule.intervalSeconds * 1000.0))});
    EXPECT_EQ(innFood, villageLevelOneConsumption);
}

TEST(ProductionBalanceTests, ControlledBasicChainRatiosStayExplicit)
{
    const auto& wood = GetDefinition(BuildingType::Woodcutter).production;
    const auto& planks = GetDefinition(BuildingType::LumberMill).production;
    EXPECT_TRUE(Multiply(RequireRate(wood, ResourceType::WOOD, false, "Woodcutter WOOD"), 3) ==
                Multiply(RequireRate(planks, ResourceType::WOOD, true, "LumberMill WOOD"), 4));

    const auto& iron = GetDefinition(BuildingType::Foundry).production;
    const auto* ironMine = FindTerrainProductionDefinition(BuildingType::Mine, TileType::IRON_ORE);
    const auto* coalMine = FindTerrainProductionDefinition(BuildingType::Mine, TileType::COAL);
    ASSERT_NE(ironMine, nullptr);
    ASSERT_NE(coalMine, nullptr);
    EXPECT_TRUE(Multiply(RequireRate(ironMine->production, ResourceType::IRON_ORE, false, "Iron mine"), 3) ==
                Multiply(RequireRate(iron, ResourceType::IRON_ORE, true, "Foundry IRON_ORE"), 3));
    EXPECT_TRUE(Multiply(RequireRate(coalMine->production, ResourceType::COAL, false, "Coal mine"), 2) ==
                Multiply(RequireRate(iron, ResourceType::COAL, true, "Foundry COAL"), 3));

    const auto* tannery = &GetDefinition(BuildingType::Tannery).production;
    const auto& tailor = GetDefinition(BuildingType::Tailor);
    ASSERT_FALSE(tailor.recipes.empty());
    EXPECT_TRUE(Multiply(RequireRate(*tannery, ResourceType::LEATHER, false, "Tannery LEATHER"), 2) ==
                RequireRate(tailor.recipes.front().production, ResourceType::LEATHER, true,
                            "Tailor LEATHER"));
}
