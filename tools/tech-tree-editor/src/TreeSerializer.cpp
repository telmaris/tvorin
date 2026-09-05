#include "TreeSerializer.h"

#include "data/Resource.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace
{
    // Kept deliberately in the same order as BalanceStats.h.
    const std::vector<std::string> balanceStatNames{
        "BuildTime", "BuildCost", "ProductionCycleTime", "ProductionOutputAmount",
        "WorkerCapacity", "TransportTime", "RoadCapacity", "RoadSpeed",
        "ManpowerRate", "PopulationCap", "BuilderAmount",
        "UnitHp", "UnitFieldAttack", "UnitSiegePower", "UnitArmor",
        "UnitMoveSpeed", "UnitAttackSpeed", "UnitRecruitTime", "UnitRecruitManpowerCost",
        "ProvinceFortification", "ProvinceDefense", "ProvinceCounterattack", "ConquestSpoilsFraction",
        "ProvinceDefensePower", "ProvinceDefenseCoverage", "ProvinceDefenseReadiness", "ProvinceDefenseSupplyUse",
        "TransportDispatchDelay"};

    const std::vector<BalanceStat> balanceStatValues{
        BalanceStat::BuildTime, BalanceStat::BuildCost, BalanceStat::ProductionCycleTime,
        BalanceStat::ProductionOutputAmount, BalanceStat::WorkerCapacity, BalanceStat::TransportTime,
        BalanceStat::RoadCapacity, BalanceStat::RoadSpeed, BalanceStat::ManpowerRate,
        BalanceStat::PopulationCap, BalanceStat::BuilderAmount,
        BalanceStat::UnitHp, BalanceStat::UnitFieldAttack, BalanceStat::UnitSiegePower,
        BalanceStat::UnitArmor, BalanceStat::UnitMoveSpeed, BalanceStat::UnitAttackSpeed,
        BalanceStat::UnitRecruitTime, BalanceStat::UnitRecruitManpowerCost,
        BalanceStat::ProvinceFortification, BalanceStat::ProvinceDefense, BalanceStat::ProvinceCounterattack,
        BalanceStat::ConquestSpoilsFraction,
        BalanceStat::ProvinceDefensePower, BalanceStat::ProvinceDefenseCoverage, BalanceStat::ProvinceDefenseReadiness,
        BalanceStat::ProvinceDefenseSupplyUse, BalanceStat::TransportDispatchDelay};

    const std::vector<std::string> buildingTypeNames{
        "Headquarters", "Village", "StorageBuilding", "Woodcutter", "HuntersHut",
        "LumberMill", "Mine", "Foundry", "Well", "WheatFarm", "Windmill", "Bakery",
        "Inn", "Paperworks", "Smith", "Mint", "Glassworks", "Powderworks",
        "University", "Barracks", "Road", "AnimalFarm",
        "Butcher", "Tannery", "Tailor", "Armorer", "HorseStable", "Kiln",
        "HouseholdWorkshop", "Soapworks", "Inkworks", "Scriptorium", "Copperworks",
        "UrbanWorkshop", "HempFarm", "Ropery", "Weaver", "Bowyer",
        "SpearWorkshop", "SiegeWorkshop"};

    const std::vector<BuildingType> buildingTypeValues{
        BuildingType::Headquarters, BuildingType::Village, BuildingType::StorageBuilding,
        BuildingType::Woodcutter, BuildingType::HuntersHut, BuildingType::LumberMill,
        BuildingType::Mine, BuildingType::Foundry, BuildingType::Well, BuildingType::WheatFarm,
        BuildingType::Windmill, BuildingType::Bakery, BuildingType::Inn, BuildingType::Paperworks,
        BuildingType::Smith, BuildingType::Mint, BuildingType::Glassworks, BuildingType::Powderworks,
        BuildingType::University, BuildingType::Barracks, BuildingType::Road,
        BuildingType::AnimalFarm, BuildingType::Butcher,
        BuildingType::Tannery, BuildingType::Tailor, BuildingType::Armorer, BuildingType::HorseStable,
        BuildingType::Kiln, BuildingType::HouseholdWorkshop, BuildingType::Soapworks,
        BuildingType::Inkworks, BuildingType::Scriptorium, BuildingType::Copperworks,
        BuildingType::UrbanWorkshop, BuildingType::HempFarm, BuildingType::Ropery,
        BuildingType::Weaver, BuildingType::Bowyer,
        BuildingType::SpearWorkshop, BuildingType::SiegeWorkshop};

    const std::vector<std::string> resourceTypeNames{
        "WOOD", "PLANKS", "COAL", "STONE", "IRON_ORE", "IRON", "COPPER_ORE", "COPPER",
        "TIN_ORE", "TIN", "BRONZE", "COKE", "STEEL", "SILVER_ORE", "SILVER", "GOLD_ORE", "GOLD",
        "SAND", "GLASS", "SULFUR", "SALTPETER", "GUNPOWDER",
        "LEATHER", "MEAT", "WHEAT", "BREAD", "FLOUR", "WATER", "BEER", "COINS",
        "FOOD_PROVISIONS", "PAPER", "TOOLS",
        "BRONZE_SWORD", "IRON_SWORD", "STEEL_SWORD", "SPEAR",
        "BOW", "ARROWS", "CROSSBOW", "BOLTS", "MUSKET", "CARTRIDGE",
        "WOODEN_SHIELD", "IRON_SHIELD", "LEATHER_ARMOR", "IRON_ARMOR", "HORSE",
        "CLAY", "CATTLE", "RAW_HIDE", "TALLOW", "CLOTHES", "POTTERY",
        "HOUSEHOLD_GOODS", "SOAP", "INK", "BOOKS", "COPPERWARE", "URBAN_GOODS",
        "HEMP", "FIBRE", "ROPE", "COPPER_VESSEL", "COPPER_PIPE", "MECHANICAL_PARTS",
        "HEAVY_BOW", "HEAVY_ARMOR", "BRICKS", "CLOTH", "BALLISTA",
        "BATTERING_RAM", "CATAPULT"};

    const std::vector<std::string> resourceCategoryNames{
        "Ore", "Mineral", "Metal", "Timber", "Textile", "Foodstuff", "Chemical",
        "Tool", "Paper", "Currency", "Mount", "MilitarySupply", "Sword", "Spear",
        "Bow", "Crossbow", "Firearm", "Ammunition", "Shield", "Armor", "Livestock",
        "CraftedGood", "SettlementSupply"};

    const std::vector<ResourceCategory> resourceCategoryValues{
        ResourceCategory::Ore, ResourceCategory::Mineral, ResourceCategory::Metal,
        ResourceCategory::Timber, ResourceCategory::Textile, ResourceCategory::Foodstuff,
        ResourceCategory::Chemical, ResourceCategory::Tool, ResourceCategory::Paper,
        ResourceCategory::Currency, ResourceCategory::Mount, ResourceCategory::MilitarySupply,
        ResourceCategory::Sword, ResourceCategory::Spear, ResourceCategory::Bow,
        ResourceCategory::Crossbow, ResourceCategory::Firearm, ResourceCategory::Ammunition,
        ResourceCategory::Shield, ResourceCategory::Armor, ResourceCategory::Livestock,
        ResourceCategory::CraftedGood, ResourceCategory::SettlementSupply};

    // Categories drive the lane fallback and LaneRank ordering in the view.
    const std::vector<std::string> categoryNames{
        "SCIENCE", "PRODUCTION", "MILITARY", "WARFARE", "SOCIAL", "LOGISTICS",
        "ECONOMY", "POLITICS", "GOVERNANCE"};

    // Only these survive AddTag's IsAllowedResearchTag filter; anything else is
    // dropped on load, so offering more would be a lie.
    const std::vector<std::string> tagNames{
        "production", "logistics", "manpower", "expansion", "military", "construction"};

    std::vector<std::string> Alphabetical(std::vector<std::string> values)
    {
        std::sort(values.begin(), values.end());
        return values;
    }

    // Parser mappings above deliberately retain enum/source order. Dropdowns
    // use separate sorted views so presentation order can never change the
    // name-to-enum correspondence used for serialization.
    const std::vector<std::string> alphabeticalBalanceStatNames = Alphabetical(balanceStatNames);
    const std::vector<std::string> alphabeticalBuildingTypeNames = Alphabetical(buildingTypeNames);
    const std::vector<std::string> alphabeticalResourceTypeNames = Alphabetical(resourceTypeNames);
    const std::vector<std::string> alphabeticalResourceCategoryNames = Alphabetical(resourceCategoryNames);
    const std::vector<std::string> alphabeticalCategoryNames = Alphabetical(categoryNames);
    const std::vector<std::string> alphabeticalTagNames = Alphabetical(tagNames);

    // The tokenizer has no escape syntax: a '"' inside a value would terminate
    // the token early and '#' outside quotes starts a comment. Quoting handles
    // '#'; the quote character itself has to go.
    std::string Quote(const std::string& value)
    {
        std::string safe = value;
        std::replace(safe.begin(), safe.end(), '"', '\'');
        return "\"" + safe + "\"";
    }

    // Twelve significant digits preserve editor-derived ratios such as
    // 1 / 0.95 without expanding ordinary source values (0.95 stays "0.95").
    // The former default precision of six wrote 1.052631... as 1.05263, which
    // then failed the editor's own round-trip comparison.
    std::string Number(double value)
    {
        std::ostringstream stream;
        stream << std::setprecision(12) << std::defaultfloat << value;
        return stream.str();
    }

    bool NearlyEqual(double a, double b)
    {
        return std::abs(a - b) < 1e-6;
    }

    bool UsesRateDisplay(BalanceStat stat)
    {
        return stat == BalanceStat::BuildTime ||
               stat == BalanceStat::ProductionCycleTime ||
               stat == BalanceStat::TransportTime ||
               stat == BalanceStat::TransportDispatchDelay;
    }

    std::string DescribeMismatch(const TechnologyDefinition& written, const TechnologyDefinition& reread)
    {
        auto fail = [&](const std::string& field) { return "'" + written.id + "' field " + field; };
        if (written.name != reread.name) return fail("name");
        if (written.description != reread.description) return fail("description");
        if (written.category != reread.category) return fail("category");
        if (written.layoutLane != reread.layoutLane) return fail("layout_lane");
        if (written.layoutOrder != reread.layoutOrder) return fail("layout_order");
        if (!NearlyEqual(written.researchTime, reread.researchTime)) return fail("research_time");
        if (written.prerequisites != reread.prerequisites) return fail("requires");
        if (written.costs.size() != reread.costs.size()) return fail("cost count");
        for (size_t i = 0; i < written.costs.size(); i++)
        {
            if (written.costs[i].type != reread.costs[i].type || written.costs[i].amount != reread.costs[i].amount)
                return fail("cost #" + std::to_string(i));
        }
        if (written.modifiers.size() != reread.modifiers.size()) return fail("modifier count");
        for (size_t i = 0; i < written.modifiers.size(); i++)
        {
            const auto& a = written.modifiers[i];
            const auto& b = reread.modifiers[i];
            if (a.stat != b.stat || !NearlyEqual(a.additive, b.additive) ||
                !NearlyEqual(a.multiplier, b.multiplier) || a.buildingType != b.buildingType ||
                a.resourceType != b.resourceType || a.resourceCategory != b.resourceCategory ||
                a.unitDefId != b.unitDefId)
                return fail("modifier #" + std::to_string(i) + " (" + SerializeModifier(a) + ")");
        }
        return "";
    }
}

double ModifierEffectPercent(BalanceStat stat, double multiplier)
{
    if (UsesRateDisplay(stat) && multiplier > 0.0)
        return (1.0 / multiplier - 1.0) * 100.0;
    return (multiplier - 1.0) * 100.0;
}

double ModifierMultiplierFromEffectPercent(BalanceStat stat, double effectPercent)
{
    if (UsesRateDisplay(stat))
    {
        // A -100% rate would divide by zero. Preserve a finite, positive cycle
        // duration while still allowing an extreme slowdown to be authored.
        return 1.0 / std::max(0.01, 1.0 + effectPercent / 100.0);
    }
    return std::max(0.0, 1.0 + effectPercent / 100.0);
}

const std::vector<std::string>& RtsDataNames::BalanceStats() { return alphabeticalBalanceStatNames; }
const std::vector<std::string>& RtsDataNames::BuildingTypes() { return alphabeticalBuildingTypeNames; }
const std::vector<std::string>& RtsDataNames::ResourceTypes() { return alphabeticalResourceTypeNames; }
const std::vector<std::string>& RtsDataNames::ResourceCategories() { return alphabeticalResourceCategoryNames; }
const std::vector<std::string>& RtsDataNames::Categories() { return alphabeticalCategoryNames; }
const std::vector<std::string>& RtsDataNames::Tags() { return alphabeticalTagNames; }

std::string RtsDataNames::NameOf(BalanceStat stat)
{
    auto it = std::find(balanceStatValues.begin(), balanceStatValues.end(), stat);
    return it == balanceStatValues.end()
        ? balanceStatNames.front()
        : balanceStatNames[std::distance(balanceStatValues.begin(), it)];
}

std::string RtsDataNames::NameOf(BuildingType type)
{
    auto it = std::find(buildingTypeValues.begin(), buildingTypeValues.end(), type);
    return it == buildingTypeValues.end()
        ? std::string()
        : buildingTypeNames[std::distance(buildingTypeValues.begin(), it)];
}

std::string RtsDataNames::NameOf(ResourceType type)
{
    std::string name = rt2s(type);
    return std::find(resourceTypeNames.begin(), resourceTypeNames.end(), name) == resourceTypeNames.end()
        ? std::string()
        : name;
}

std::string RtsDataNames::NameOf(ResourceCategory category)
{
    auto it = std::find(resourceCategoryValues.begin(), resourceCategoryValues.end(), category);
    return it == resourceCategoryValues.end()
        ? std::string()
        : resourceCategoryNames[std::distance(resourceCategoryValues.begin(), it)];
}

BalanceStat RtsDataNames::ToBalanceStat(const std::string& name)
{
    auto it = std::find(balanceStatNames.begin(), balanceStatNames.end(), name);
    return it == balanceStatNames.end()
        ? BalanceStat::BuildTime
        : balanceStatValues[std::distance(balanceStatNames.begin(), it)];
}

BuildingType RtsDataNames::ToBuildingType(const std::string& name)
{
    auto it = std::find(buildingTypeNames.begin(), buildingTypeNames.end(), name);
    return it == buildingTypeNames.end()
        ? BuildingType::Building
        : buildingTypeValues[std::distance(buildingTypeNames.begin(), it)];
}

ResourceType RtsDataNames::ToResourceType(const std::string& name)
{
    for (int i = 0; i < static_cast<int>(ResourceType::Null); i++)
    {
        auto candidate = static_cast<ResourceType>(i);
        if (rt2s(candidate) == name)
            return candidate;
    }
    return ResourceType::Null;
}

ResourceCategory RtsDataNames::ToResourceCategory(const std::string& name)
{
    auto it = std::find(resourceCategoryNames.begin(), resourceCategoryNames.end(), name);
    return it == resourceCategoryNames.end()
        ? ResourceCategory::None
        : resourceCategoryValues[std::distance(resourceCategoryNames.begin(), it)];
}

std::string SerializeModifier(const BalanceModifier& modifier)
{
    std::ostringstream line;
    line << "modifier " << RtsDataNames::NameOf(modifier.stat);

    // ParseModifier reads key/value pairs, so order does not matter to the
    // parser; this order just matches how the existing data files read.
    if (std::abs(modifier.additive) > 1e-9)
        line << " additive " << Number(modifier.additive);
    if (!NearlyEqual(modifier.multiplier, 1.0))
        line << " multiplier " << Number(modifier.multiplier);
    if (modifier.buildingType.has_value())
    {
        std::string name = RtsDataNames::NameOf(modifier.buildingType.value());
        if (!name.empty())
            line << " building " << name;
    }
    if (modifier.resourceType.has_value())
    {
        std::string name = RtsDataNames::NameOf(modifier.resourceType.value());
        if (!name.empty())
            line << " resource " << name;
    }
    if (modifier.resourceCategory.has_value())
    {
        std::string name = RtsDataNames::NameOf(modifier.resourceCategory.value());
        if (!name.empty())
            line << " category " << name;
    }
    if (modifier.unitDefId.has_value() && !modifier.unitDefId.value().empty())
        line << " unit " << modifier.unitDefId.value();

    return line.str();
}

std::string SerializeTree(const std::vector<TechnologyDefinition>& definitions)
{
    std::ostringstream out;
    for (size_t i = 0; i < definitions.size(); i++)
    {
        const auto& definition = definitions[i];
        out << "technology " << definition.id << "\n";
        out << "    name " << Quote(definition.name) << "\n";
        if (!definition.description.empty())
            out << "    description " << Quote(definition.description) << "\n";
        out << "    category " << definition.category << "\n";
        if (!definition.layoutLane.empty())
            out << "    layout_lane " << Quote(definition.layoutLane) << "\n";
        if (definition.layoutOrder != std::numeric_limits<int>::max())
            out << "    layout_order " << definition.layoutOrder << "\n";
        if (!definition.tags.empty())
        {
            out << "    tags";
            for (const auto& tag : definition.tags)
                out << " " << tag;
            out << "\n";
        }
        out << "    research_time " << Number(definition.researchTime) << "\n";
        for (const auto& prerequisite : definition.prerequisites)
            out << "    requires " << prerequisite << "\n";
        for (const auto& cost : definition.costs)
        {
            std::string name = RtsDataNames::NameOf(cost.type);
            if (!name.empty())
                out << "    cost " << name << " " << cost.amount << "\n";
        }
        for (const auto& modifier : definition.modifiers)
            out << "    " << SerializeModifier(modifier) << "\n";
        out << "end\n";
        if (i + 1 < definitions.size())
            out << "\n";
    }
    return out.str();
}

SaveResult SaveTree(const std::string& path,
                    const std::vector<TechnologyDefinition>& definitions,
                    bool isFocus)
{
    if (definitions.empty())
    {
        // An empty file would make the loader fall back to built-in defaults,
        // which reads as "your tree vanished". Refuse instead.
        return {false, "Refusing to save an empty tree (the game would fall back to built-in defaults)"};
    }

    std::error_code ec;
    std::string backup = path + ".bak";
    if (std::filesystem::exists(path) && !std::filesystem::exists(backup))
        std::filesystem::copy_file(path, backup, ec);

    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
            return {false, "Cannot open for writing: " + path};
        file << SerializeTree(definitions);
        if (!file.good())
            return {false, "Write failed: " + path};
    }

    // Read back with the game's parser and diff. This is what makes the
    // hand-written enum name tables above safe: a wrong name shows up here
    // instead of in the game weeks later.
    auto reread = isFocus ? LoadFocusDefinitionsFromFile(path) : LoadTechnologyDefinitionsFromFile(path);
    if (reread.size() != definitions.size())
    {
        return {false, "Round-trip failed: wrote " + std::to_string(definitions.size()) +
                       " nodes, parser read " + std::to_string(reread.size())};
    }
    for (size_t i = 0; i < definitions.size(); i++)
    {
        if (definitions[i].id != reread[i].id)
            return {false, "Round-trip failed: node #" + std::to_string(i) + " id changed"};
        std::string mismatch = DescribeMismatch(definitions[i], reread[i]);
        if (!mismatch.empty())
            return {false, "Round-trip mismatch: " + mismatch};
    }

    return {true, "Saved " + std::to_string(definitions.size()) + " nodes, round-trip verified"};
}
