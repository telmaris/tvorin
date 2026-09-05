#include "research/Technology.h"
#include "data/RtsDataFile.h"
#include "core/Log.h"

#include <algorithm>
#include <cctype>

namespace
{
    constexpr const char* technologyDataPath = "assets/data/technologies.rtsdata";
    constexpr const char* focusDataPath = "assets/data/focuses.rtsdata";

    bool IsAllowedResearchTag(const std::string& tag)
    {
        return tag == "production" ||
               tag == "logistics" ||
               tag == "manpower" ||
               tag == "expansion" ||
               tag == "military" ||
               tag == "construction";
    }

    // Converts text to a balance stat identifier.
    BalanceStat ParseBalanceStat(const std::string& value)
    {
        if (value == "BuildTime") return BalanceStat::BuildTime;
        if (value == "BuildCost") return BalanceStat::BuildCost;
        if (value == "ProductionCycleTime") return BalanceStat::ProductionCycleTime;
        if (value == "ProductionOutputAmount") return BalanceStat::ProductionOutputAmount;
        if (value == "WorkerCapacity") return BalanceStat::WorkerCapacity;
        if (value == "TransportTime") return BalanceStat::TransportTime;
        if (value == "TransportDispatchDelay") return BalanceStat::TransportDispatchDelay;
        if (value == "RoadCapacity") return BalanceStat::RoadCapacity;
        if (value == "RoadSpeed") return BalanceStat::RoadSpeed;
        if (value == "ManpowerRate") return BalanceStat::ManpowerRate;
        if (value == "PopulationCap") return BalanceStat::PopulationCap;
        if (value == "VillageSupplyConsumption") return BalanceStat::VillageSupplyConsumption;
        if (value == "BuilderAmount") return BalanceStat::BuilderAmount;
        if (value == "UnitHp") return BalanceStat::UnitHp;
        if (value == "UnitFieldAttack") return BalanceStat::UnitFieldAttack;
        if (value == "UnitSiegePower") return BalanceStat::UnitSiegePower;
        if (value == "UnitArmor") return BalanceStat::UnitArmor;
        if (value == "UnitMoveSpeed") return BalanceStat::UnitMoveSpeed;
        if (value == "UnitAttackSpeed") return BalanceStat::UnitAttackSpeed;
        if (value == "UnitRecruitTime") return BalanceStat::UnitRecruitTime;
        if (value == "UnitRecruitManpowerCost") return BalanceStat::UnitRecruitManpowerCost;
        if (value == "ProvinceFortification") return BalanceStat::ProvinceFortification;
        if (value == "ProvinceDefense") return BalanceStat::ProvinceDefense;
        if (value == "ProvinceCounterattack") return BalanceStat::ProvinceCounterattack;
        if (value == "ConquestSpoilsFraction") return BalanceStat::ConquestSpoilsFraction;
        if (value == "ProvinceDefensePower") return BalanceStat::ProvinceDefensePower;
        if (value == "ProvinceDefenseCoverage") return BalanceStat::ProvinceDefenseCoverage;
        if (value == "ProvinceDefenseReadiness") return BalanceStat::ProvinceDefenseReadiness;
        if (value == "ProvinceDefenseSupplyUse") return BalanceStat::ProvinceDefenseSupplyUse;
        if (value == "RouteTravelSpeed") return BalanceStat::RouteTravelSpeed;
        if (value == "RouteIncidentChance") return BalanceStat::RouteIncidentChance;
        if (value == "TradeExchangeRate") return BalanceStat::TradeExchangeRate;
        if (value == "TradeScoreGain") return BalanceStat::TradeScoreGain;
        if (value == "BattleAttack") return BalanceStat::BattleAttack;
        if (value == "BattleCasualtyRate") return BalanceStat::BattleCasualtyRate;
        if (value == "BattleDuration") return BalanceStat::BattleDuration;
        if (value == "GarrisonCapacity") return BalanceStat::GarrisonCapacity;
        if (value == "GarrisonFoodUpkeep") return BalanceStat::GarrisonFoodUpkeep;
        if (value == "RaidBuildingDestructionChance") return BalanceStat::RaidBuildingDestructionChance;
        if (value == "RaidStockLossFraction") return BalanceStat::RaidStockLossFraction;
        if (value == "ProvinceEventChance") return BalanceStat::ProvinceEventChance;
        if (value == "ProvinceEventWeight") return BalanceStat::ProvinceEventWeight;
        if (value == "ProvinceEventDuration") return BalanceStat::ProvinceEventDuration;
        if (value == "ColonizationDuration") return BalanceStat::ColonizationDuration;
        return BalanceStat::BuildTime;
    }

    void AddTag(std::vector<std::string>& tags, std::string tag)
    {
        tag.erase(std::remove(tag.begin(), tag.end(), ','), tag.end());
        if (tag.empty())
            return;

        std::transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
        if (!IsAllowedResearchTag(tag))
            return;
        if (std::find(tags.begin(), tags.end(), tag) == tags.end())
            tags.push_back(std::move(tag));
    }

    void AddCategoryTag(std::vector<std::string>& tags, const std::string& category)
    {
        if (category == "PRODUCTION")
            AddTag(tags, "production");
        else if (category == "WARFARE" || category == "MILITARY")
            AddTag(tags, "military");
        else if (category == "SOCIAL")
            AddTag(tags, "manpower");
    }

    void AddBuildingTags(std::vector<std::string>& tags, BuildingType type)
    {
        switch (type)
        {
            case BuildingType::Woodcutter:
            case BuildingType::HuntersHut:
            case BuildingType::LumberMill:
            case BuildingType::Mine:
            case BuildingType::Foundry:
            case BuildingType::Well:
            case BuildingType::WheatFarm:
            case BuildingType::Windmill:
            case BuildingType::Bakery:
            case BuildingType::Inn:
            case BuildingType::Paperworks:
            case BuildingType::Smith:
                AddTag(tags, "production");
                break;
            case BuildingType::StorageBuilding: AddTag(tags, "logistics"); break;
            case BuildingType::Road: AddTag(tags, "roads"); AddTag(tags, "logistics"); break;
            case BuildingType::Village: AddTag(tags, "manpower"); break;
            case BuildingType::University: break;
            case BuildingType::Barracks:
                AddTag(tags, "military");
                break;
            case BuildingType::Headquarters:
                AddTag(tags, "expansion");
                break;
            default:
                break;
        }
    }

    void AddResourceTags(std::vector<std::string>& tags, ResourceType type)
    {
        switch (type)
        {
            case ResourceType::FOOD_PROVISIONS:
                AddTag(tags, "logistics");
                AddTag(tags, "manpower");
                break;
            case ResourceType::PAPER: break;
            case ResourceType::BRONZE_SWORD:
            case ResourceType::IRON_SWORD:
            case ResourceType::STEEL_SWORD:
            case ResourceType::SPEAR:
            case ResourceType::BOW:
            case ResourceType::CROSSBOW:
            case ResourceType::ARROWS:
            case ResourceType::BOLTS:
            case ResourceType::WOODEN_SHIELD:
            case ResourceType::IRON_SHIELD:
            case ResourceType::LEATHER_ARMOR:
            case ResourceType::IRON_ARMOR:
            case ResourceType::HORSE:
                AddTag(tags, "military");
                break;
            default:
                break;
        }
    }

    void InferTags(TechnologyDefinition& definition)
    {
        auto explicitTags = definition.tags;
        definition.tags.clear();
        for (const auto& tag : explicitTags)
            AddTag(definition.tags, tag);
        AddCategoryTag(definition.tags, definition.category);

        for (const auto& cost : definition.costs)
            AddResourceTags(definition.tags, cost.type);

        for (const auto& modifier : definition.modifiers)
        {
            switch (modifier.stat)
            {
                case BalanceStat::BuildTime:
                case BalanceStat::BuildCost:
                case BalanceStat::BuilderAmount:
                    AddTag(definition.tags, "construction");
                    break;
                case BalanceStat::ProductionCycleTime:
                case BalanceStat::ProductionOutputAmount:
                case BalanceStat::WorkerCapacity:
                    AddTag(definition.tags, "production");
                    break;
                case BalanceStat::TransportTime:
                case BalanceStat::TransportDispatchDelay:
                case BalanceStat::RoadCapacity:
                case BalanceStat::RoadSpeed:
                    AddTag(definition.tags, "logistics");
                    break;
                case BalanceStat::ManpowerRate:
                case BalanceStat::PopulationCap:
                    AddTag(definition.tags, "manpower");
                    break;
                case BalanceStat::VillageSupplyConsumption:
                    AddTag(definition.tags, "manpower");
                    AddTag(definition.tags, "logistics");
                    break;
                case BalanceStat::RouteTravelSpeed:
                case BalanceStat::RouteIncidentChance:
                    AddTag(definition.tags, "logistics");
                    break;
                case BalanceStat::TradeExchangeRate:
                case BalanceStat::TradeScoreGain:
                    AddTag(definition.tags, "expansion");
                    break;
                case BalanceStat::BattleAttack:
                case BalanceStat::BattleCasualtyRate:
                case BalanceStat::BattleDuration:
                case BalanceStat::GarrisonCapacity:
                case BalanceStat::GarrisonFoodUpkeep:
                case BalanceStat::RaidBuildingDestructionChance:
                case BalanceStat::RaidStockLossFraction:
                    AddTag(definition.tags, "military");
                    break;
                case BalanceStat::ProvinceEventChance:
                case BalanceStat::ProvinceEventWeight:
                case BalanceStat::ProvinceEventDuration:
                case BalanceStat::ColonizationDuration:
                    AddTag(definition.tags, "expansion");
                    break;
            }
            if (modifier.buildingType.has_value())
                AddBuildingTags(definition.tags, modifier.buildingType.value());
            if (modifier.resourceType.has_value())
                AddResourceTags(definition.tags, modifier.resourceType.value());
        }
    }

    // Converts text to a building type identifier.
    BuildingType ParseBuildingType(const std::string& value)
    {
        if (value == "Headquarters") return BuildingType::Headquarters;
        if (value == "Village") return BuildingType::Village;
        if (value == "StorageBuilding") return BuildingType::StorageBuilding;
        if (value == "Woodcutter") return BuildingType::Woodcutter;
        if (value == "HuntersHut") return BuildingType::HuntersHut;
        if (value == "LumberMill") return BuildingType::LumberMill;
        if (value == "Mine") return BuildingType::Mine;
        if (value == "Foundry") return BuildingType::Foundry;
        if (value == "Well") return BuildingType::Well;
        if (value == "WheatFarm") return BuildingType::WheatFarm;
        if (value == "Windmill") return BuildingType::Windmill;
        if (value == "Bakery") return BuildingType::Bakery;
        if (value == "Inn") return BuildingType::Inn;
        if (value == "Paperworks") return BuildingType::Paperworks;
        if (value == "Smith") return BuildingType::Smith;
        if (value == "University") return BuildingType::University;
        if (value == "Barracks") return BuildingType::Barracks;
        if (value == "Road") return BuildingType::Road;
        // These extra configured building types are parsed here as well.
        if (value == "Mint") return BuildingType::Mint;
        if (value == "Glassworks") return BuildingType::Glassworks;
        if (value == "Powderworks") return BuildingType::Powderworks;
        if (value == "AnimalFarm") return BuildingType::AnimalFarm;
        if (value == "Butcher") return BuildingType::Butcher;
        if (value == "Tannery") return BuildingType::Tannery;
        if (value == "Tailor") return BuildingType::Tailor;
        if (value == "Armorer") return BuildingType::Armorer;
        if (value == "HorseStable") return BuildingType::HorseStable;
        if (value == "Kiln") return BuildingType::Kiln;
        if (value == "HouseholdWorkshop") return BuildingType::HouseholdWorkshop;
        if (value == "Soapworks") return BuildingType::Soapworks;
        if (value == "Inkworks") return BuildingType::Inkworks;
        if (value == "Scriptorium") return BuildingType::Scriptorium;
        if (value == "Copperworks") return BuildingType::Copperworks;
        if (value == "UrbanWorkshop") return BuildingType::UrbanWorkshop;
        if (value == "HempFarm") return BuildingType::HempFarm;
        if (value == "Ropery") return BuildingType::Ropery;
        if (value == "Weaver") return BuildingType::Weaver;
        if (value == "Bowyer") return BuildingType::Bowyer;
        if (value == "SpearWorkshop") return BuildingType::SpearWorkshop;
        if (value == "SiegeWorkshop") return BuildingType::SiegeWorkshop;
        return BuildingType::Building;
    }

    // Converts text to a resource type identifier.
    ResourceType ParseResourceType(const std::string& value)
    {
        if (value == "WOOD") return ResourceType::WOOD;
        if (value == "PLANKS") return ResourceType::PLANKS;
        if (value == "COAL") return ResourceType::COAL;
        if (value == "STONE") return ResourceType::STONE;
        if (value == "IRON_ORE") return ResourceType::IRON_ORE;
        if (value == "IRON") return ResourceType::IRON;
        if (value == "COPPER_ORE") return ResourceType::COPPER_ORE;
        if (value == "COPPER") return ResourceType::COPPER;
        if (value == "SILVER_ORE") return ResourceType::SILVER_ORE;
        if (value == "SILVER") return ResourceType::SILVER;
        if (value == "GOLD_ORE") return ResourceType::GOLD_ORE;
        if (value == "GOLD") return ResourceType::GOLD;
        if (value == "LEATHER") return ResourceType::LEATHER;
        if (value == "MEAT") return ResourceType::MEAT;
        if (value == "WHEAT") return ResourceType::WHEAT;
        if (value == "BREAD") return ResourceType::BREAD;
        if (value == "FLOUR") return ResourceType::FLOUR;
        if (value == "WATER") return ResourceType::WATER;
        if (value == "BEER") return ResourceType::BEER;
        if (value == "COINS") return ResourceType::COINS;
        if (value == "FOOD_PROVISIONS") return ResourceType::FOOD_PROVISIONS;
        if (value == "PAPER") return ResourceType::PAPER;
        if (value == "TOOLS") return ResourceType::TOOLS;
        if (value == "IRON_SWORD") return ResourceType::IRON_SWORD;
        if (value == "STEEL_SWORD") return ResourceType::STEEL_SWORD;
        if (value == "BOW") return ResourceType::BOW;
        if (value == "ARROWS") return ResourceType::ARROWS;
        if (value == "HORSE") return ResourceType::HORSE;
        if (value == "BRONZE_SWORD") return ResourceType::BRONZE_SWORD;
        if (value == "SPEAR") return ResourceType::SPEAR;
        if (value == "CROSSBOW") return ResourceType::CROSSBOW;
        if (value == "BOLTS") return ResourceType::BOLTS;
        if (value == "WOODEN_SHIELD") return ResourceType::WOODEN_SHIELD;
        if (value == "IRON_SHIELD") return ResourceType::IRON_SHIELD;
        if (value == "LEATHER_ARMOR") return ResourceType::LEATHER_ARMOR;
        if (value == "IRON_ARMOR") return ResourceType::IRON_ARMOR;
        // Same gap as ParseBuildingType above: these exist in ResourceType (and
        // in rt2s) but were unparseable, so `cost STEEL 10` read as Null.
        if (value == "TIN_ORE") return ResourceType::TIN_ORE;
        if (value == "TIN") return ResourceType::TIN;
        if (value == "BRONZE") return ResourceType::BRONZE;
        if (value == "COKE") return ResourceType::COKE;
        if (value == "STEEL") return ResourceType::STEEL;
        if (value == "SAND") return ResourceType::SAND;
        if (value == "GLASS") return ResourceType::GLASS;
        if (value == "SULFUR") return ResourceType::SULFUR;
        if (value == "SALTPETER") return ResourceType::SALTPETER;
        if (value == "GUNPOWDER") return ResourceType::GUNPOWDER;
        if (value == "MUSKET") return ResourceType::MUSKET;
        if (value == "CARTRIDGE") return ResourceType::CARTRIDGE;
        if (value == "CLAY") return ResourceType::CLAY;
        if (value == "CATTLE") return ResourceType::CATTLE;
        if (value == "RAW_HIDE") return ResourceType::RAW_HIDE;
        if (value == "TALLOW") return ResourceType::TALLOW;
        if (value == "CLOTHES") return ResourceType::CLOTHES;
        if (value == "POTTERY") return ResourceType::POTTERY;
        if (value == "HOUSEHOLD_GOODS") return ResourceType::HOUSEHOLD_GOODS;
        if (value == "SOAP") return ResourceType::SOAP;
        if (value == "INK") return ResourceType::INK;
        if (value == "BOOKS") return ResourceType::BOOKS;
        if (value == "COPPERWARE") return ResourceType::COPPERWARE;
        if (value == "URBAN_GOODS") return ResourceType::URBAN_GOODS;
        if (value == "HEMP") return ResourceType::HEMP;
        if (value == "FIBRE") return ResourceType::FIBRE;
        if (value == "ROPE") return ResourceType::ROPE;
        if (value == "COPPER_VESSEL") return ResourceType::COPPER_VESSEL;
        if (value == "COPPER_PIPE") return ResourceType::COPPER_PIPE;
        if (value == "MECHANICAL_PARTS") return ResourceType::MECHANICAL_PARTS;
        if (value == "HEAVY_BOW") return ResourceType::HEAVY_BOW;
        if (value == "IRON_SWORD") return ResourceType::IRON_SWORD;
        if (value == "HEAVY_ARMOR") return ResourceType::HEAVY_ARMOR;
        if (value == "BRICKS") return ResourceType::BRICKS;
        if (value == "CLOTH") return ResourceType::CLOTH;
        if (value == "BALLISTA") return ResourceType::BALLISTA;
        if (value == "BATTERING_RAM") return ResourceType::BATTERING_RAM;
        if (value == "CATAPULT") return ResourceType::CATAPULT;
        return ResourceType::Null;
    }

    // Converts text to a resource category, enabling category-wide bonuses in the
    // data files (e.g. `modifier ProductionOutputAmount category Metal multiplier 1.10`).
    ResourceCategory ParseResourceCategory(const std::string& value)
    {
        if (value == "Ore")            return ResourceCategory::Ore;
        if (value == "Mineral")        return ResourceCategory::Mineral;
        if (value == "Metal")          return ResourceCategory::Metal;
        if (value == "Timber")         return ResourceCategory::Timber;
        if (value == "Textile")        return ResourceCategory::Textile;
        if (value == "Foodstuff")      return ResourceCategory::Foodstuff;
        if (value == "Chemical")       return ResourceCategory::Chemical;
        if (value == "Tool")           return ResourceCategory::Tool;
        if (value == "Paper")          return ResourceCategory::Paper;
        if (value == "Currency")       return ResourceCategory::Currency;
        if (value == "Mount")          return ResourceCategory::Mount;
        if (value == "MilitarySupply") return ResourceCategory::MilitarySupply;
        if (value == "Sword")          return ResourceCategory::Sword;
        if (value == "Spear")          return ResourceCategory::Spear;
        if (value == "Bow")            return ResourceCategory::Bow;
        if (value == "Crossbow")       return ResourceCategory::Crossbow;
        if (value == "Firearm")        return ResourceCategory::Firearm;
        if (value == "Ammunition")     return ResourceCategory::Ammunition;
        if (value == "Shield")         return ResourceCategory::Shield;
        if (value == "Armor")          return ResourceCategory::Armor;
        if (value == "Livestock")      return ResourceCategory::Livestock;
        if (value == "CraftedGood")    return ResourceCategory::CraftedGood;
        if (value == "SettlementSupply") return ResourceCategory::SettlementSupply;
        return ResourceCategory::None;
    }

    // Returns built-in technologies used when the data file is missing.
    std::vector<TechnologyDefinition> MakeDefaultTechnologies()
    {
        return {
            TechnologyDefinition{
                "forestry",
                "Mathematics",
                "Counting, ratios and measured work create the foundation for every later science.",
                "SCIENCE",
                12.0,
                {},
                {{ResourceType::PAPER, 10}, {ResourceType::WOOD, 30}, {ResourceType::TOOLS, 2}},
                {
                    BalanceModifier{BalanceStat::ProductionCycleTime, 0.0, 0.85, BalanceModifierScope::Global(), BuildingType::Woodcutter, std::nullopt, "tech:forestry"},
                    BalanceModifier{BalanceStat::ProductionOutputAmount, 1.0, 1.0, BalanceModifierScope::Global(), BuildingType::Woodcutter, ResourceType::WOOD, "tech:forestry"}
                },
                {},
                "Core Sciences",
                10},
            TechnologyDefinition{
                "masonry",
                "Physics",
                "Natural philosophy explains force, weight and materials well enough to improve construction and defense.",
                "SCIENCE",
                16.0,
                {"forestry"},
                {{ResourceType::PAPER, 12}, {ResourceType::STONE, 30}},
                // TD(etap-1): the HitPoints modifiers this tech used to grant HQ/defensive
                // buildings were dropped with TerritoryComponent; HQ defense returns as a
                // dedicated HqComponent stat in ETAP 6.
                {},
                {},
                "Core Sciences",
                20},
            TechnologyDefinition{
                "logistics",
                "Logistics",
                "Roads move goods faster and carry more traffic.",
                "SCIENCE",
                20.0,
                {"forestry"},
                {{ResourceType::PAPER, 18}, {ResourceType::PLANKS, 25}},
                {
                    BalanceModifier{BalanceStat::RoadSpeed, 0.0, 1.20, BalanceModifierScope::Global(), BuildingType::Road, std::nullopt, "tech:logistics"},
                    BalanceModifier{BalanceStat::RoadCapacity, 2.0, 1.0, BalanceModifierScope::Global(), BuildingType::Road, std::nullopt, "tech:logistics"}
                },
                {},
                "Engineering",
                30},
            TechnologyDefinition{
                "village_records",
                "Social Sciences",
                "Population records and social observation turn settlement management into a formal field of study.",
                "SCIENCE",
                18.0,
                {"forestry"},
                {{ResourceType::PAPER, 15}, {ResourceType::FOOD_PROVISIONS, 10}},
                {
                    BalanceModifier{BalanceStat::PopulationCap, 20.0, 1.0, BalanceModifierScope::Global(), BuildingType::Village, std::nullopt, "tech:village_records"},
                    BalanceModifier{BalanceStat::ManpowerRate, 0.0, 1.10, BalanceModifierScope::Global(), std::nullopt, std::nullopt, "tech:village_records"}
                },
                {},
                "Social Sciences",
                40},
            TechnologyDefinition{
                "sawmill_blades",
                "Sawmill Blades",
                "Better saws improve plank production.",
                "SCIENCE",
                24.0,
                {"forestry"},
                {{ResourceType::PAPER, 16}, {ResourceType::TOOLS, 6}, {ResourceType::IRON, 12}},
                {
                    BalanceModifier{BalanceStat::ProductionCycleTime, 0.0, 0.80, BalanceModifierScope::Global(), BuildingType::LumberMill, std::nullopt, "tech:sawmill_blades"},
                    BalanceModifier{BalanceStat::ProductionOutputAmount, 1.0, 1.0, BalanceModifierScope::Global(), BuildingType::LumberMill, ResourceType::PLANKS, "tech:sawmill_blades"}
                },
                {},
                "Engineering",
                50},
            TechnologyDefinition{
                "deep_mining",
                "Deep Mining",
                "Mines extract ore and stone more efficiently.",
                "SCIENCE",
                30.0,
                {"masonry"},
                {{ResourceType::PAPER, 22}, {ResourceType::TOOLS, 8}, {ResourceType::PLANKS, 20}},
                {
                    BalanceModifier{BalanceStat::ProductionOutputAmount, 1.0, 1.0, BalanceModifierScope::Global(), BuildingType::Mine, ResourceType::IRON_ORE, "tech:deep_mining"},
                    BalanceModifier{BalanceStat::ProductionOutputAmount, 1.0, 1.0, BalanceModifierScope::Global(), BuildingType::Mine, ResourceType::COAL, "tech:deep_mining"},
                    BalanceModifier{BalanceStat::ProductionOutputAmount, 1.0, 1.0, BalanceModifierScope::Global(), BuildingType::Mine, ResourceType::STONE, "tech:deep_mining"}
                },
                {},
                "Natural Sciences",
                60}
        };
    }

    std::vector<TechnologyDefinition> MakeDefaultFocuses()
    {
        return {
            TechnologyDefinition{
                "frontier_settlement",
                "Frontier Settlement",
                "Organize early settlement logistics and unlock civic expansion paths.",
                "SOCIAL",
                10.0,
                {},
                {{ResourceType::PAPER, 5}, {ResourceType::WOOD, 20}},
                {
                    BalanceModifier{BalanceStat::PopulationCap, 10.0, 1.0, BalanceModifierScope::Global(), BuildingType::Village, std::nullopt, "focus:frontier_settlement"}
                }},
            TechnologyDefinition{
                "militia_charter",
                "Militia Charter",
                "Formalize local defense and improve early garrison capacity.",
                "WARFARE",
                12.0,
                {"frontier_settlement"},
                {{ResourceType::PAPER, 8}, {ResourceType::FOOD_PROVISIONS, 8}},
                // TD(etap-1): garrison/recruitment-time modifiers dropped with
                // GarrisonComponent/RecruitmentComponent; recruitment bonuses return in ETAP 3.
                {}},
            TechnologyDefinition{
                "academic_patronage",
                "Academic Patronage",
                "Prepare state support for formal research institutions.",
                "SOCIAL",
                14.0,
                {"frontier_settlement"},
                {{ResourceType::PAPER, 12}, {ResourceType::COINS, 5}},
                {
                    BalanceModifier{BalanceStat::BuildTime, 0.0, 0.90, BalanceModifierScope::Global(), BuildingType::University, std::nullopt, "focus:academic_patronage"}
                }}
        };
    }

    // Parses one modifier line in a technology block.
    BalanceModifier ParseModifier(const std::vector<std::string>& tokens, const std::string& techId)
    {
        BalanceModifier modifier;
        modifier.stat = tokens.size() > 1 ? ParseBalanceStat(tokens[1]) : BalanceStat::BuildTime;
        modifier.scope = BalanceModifierScope::Global();
        modifier.source = "tech:" + techId;

        for (size_t i = 2; i + 1 < tokens.size(); i += 2)
        {
            const std::string& key = tokens[i];
            const std::string& value = tokens[i + 1];
            if (key == "additive")
                modifier.additive = RtsDataDoubleOr(value);
            else if (key == "multiplier")
                modifier.multiplier = RtsDataDoubleOr(value);
            else if (key == "building")
                modifier.buildingType = ParseBuildingType(value);
            else if (key == "resource")
                modifier.resourceType = ParseResourceType(value);
            else if (key == "category")
                modifier.resourceCategory = ParseResourceCategory(value);
            else if (key == "unit")
                modifier.unitDefId = value;
        }

        if (modifier.stat == BalanceStat::ManpowerRate)
        {
            modifier.buildingType.reset();
        }
        return modifier;
    }

    // Parses one technology block from tokenized data.
    TechnologyDefinition ParseTechnology(const std::vector<std::vector<std::string>>& lines, size_t& index)
    {
        TechnologyDefinition definition;
        definition.id = lines[index].size() > 1 ? lines[index][1] : "";
        definition.name = definition.id;

        while (++index < lines.size())
        {
            const auto& tokens = lines[index];
            const auto& command = tokens[0];
            if (command == "end")
            {
                InferTags(definition);
                return definition;
            }

            if (command == "name" && tokens.size() >= 2)
                definition.name = tokens[1];
            else if (command == "description" && tokens.size() >= 2)
                definition.description = tokens[1];
            else if (command == "category" && tokens.size() >= 2)
                definition.category = tokens[1];
            else if (command == "research_time" && tokens.size() >= 2)
                definition.researchTime = RtsDataDoubleOr(tokens[1]);
            else if (command == "layout_lane" && tokens.size() >= 2)
                definition.layoutLane = tokens[1];
            else if (command == "layout_order" && tokens.size() >= 2)
                definition.layoutOrder = RtsDataIntOr(tokens[1]);
            else if ((command == "tag" || command == "tags") && tokens.size() >= 2)
            {
                for (size_t tokenIndex = 1; tokenIndex < tokens.size(); tokenIndex++)
                    AddTag(definition.tags, tokens[tokenIndex]);
            }
            else if (command == "requires" && tokens.size() >= 2)
                definition.prerequisites.push_back(tokens[1]);
            else if (command == "cost" && tokens.size() >= 3)
                definition.costs.push_back({ParseResourceType(tokens[1]), RtsDataIntOr(tokens[2])});
            else if (command == "modifier")
                definition.modifiers.push_back(ParseModifier(tokens, definition.id));
        }
        InferTags(definition);
        return definition;
    }

    // Loads technology definitions from tokenized data lines or provided defaults.
    std::vector<TechnologyDefinition> ParseTechnologyDefinitions(
        const std::vector<std::vector<std::string>>& lines,
        std::vector<TechnologyDefinition> defaults)
    {
        if (lines.empty())
        {
            for (auto& definition : defaults)
                InferTags(definition);
            return defaults;
        }

        std::vector<TechnologyDefinition> definitions;
        for (size_t i = 0; i < lines.size(); i++)
        {
            if (lines[i][0] == "technology")
                definitions.push_back(ParseTechnology(lines, i));
        }
        if (!definitions.empty())
            return definitions;

        for (auto& definition : defaults)
            InferTags(definition);
        return defaults;
    }
}

// Loads technology definitions from a specific data file.
std::vector<TechnologyDefinition> LoadTechnologyDefinitionsFromFile(const std::string& path)
{
    return ParseTechnologyDefinitions(ReadRtsDataLines(path), MakeDefaultTechnologies());
}

std::vector<TechnologyDefinition> LoadFocusDefinitionsFromFile(const std::string& path)
{
    return ParseTechnologyDefinitions(ReadRtsDataLines(path), MakeDefaultFocuses());
}

// Returns all loaded technology definitions.
const std::vector<TechnologyDefinition>& GetTechnologyDefinitions()
{
    static std::vector<TechnologyDefinition> definitions = LoadTechnologyDefinitionsFromFile(technologyDataPath);
    return definitions;
}

const std::vector<TechnologyDefinition>& GetFocusDefinitions()
{
    static std::vector<TechnologyDefinition> definitions = LoadFocusDefinitionsFromFile(focusDataPath);
    return definitions;
}

void ReloadTechnologyDefinitions()
{
    const_cast<std::vector<TechnologyDefinition>&>(GetTechnologyDefinitions()) =
        LoadTechnologyDefinitionsFromFile(technologyDataPath);
    Log::Msg("[Debug]", "Technology definitions reloaded from disk");
}

void ReloadFocusDefinitions()
{
    const_cast<std::vector<TechnologyDefinition>&>(GetFocusDefinitions()) =
        LoadFocusDefinitionsFromFile(focusDataPath);
    Log::Msg("[Debug]", "Focus definitions reloaded from disk");
}

// Finds one technology definition by id.
const TechnologyDefinition* FindTechnologyDefinition(const std::string& id)
{
    for (const auto& definition : GetTechnologyDefinitions())
    {
        if (definition.id == id)
            return &definition;
    }
    return nullptr;
}

const TechnologyDefinition* FindFocusDefinition(const std::string& id)
{
    for (const auto& definition : GetFocusDefinitions())
    {
        if (definition.id == id)
            return &definition;
    }
    return nullptr;
}

// Returns whether the technology has already been unlocked.
bool TechnologyState::HasTechnology(const std::string& id) const
{
    return unlocked.find(id) != unlocked.end();
}

// Returns whether all prerequisites for the technology are currently met.
bool TechnologyState::CanUnlock(const std::string& id) const
{
    const auto* definition = FindTechnologyDefinition(id);
    if (definition == nullptr || HasTechnology(id))
        return false;

    for (const auto& prerequisite : definition->prerequisites)
    {
        if (!HasTechnology(prerequisite))
            return false;
    }
    return true;
}

// Unlocks a technology and makes its modifiers available.
bool TechnologyState::UnlockTechnology(const std::string& id)
{
    if (!CanUnlock(id))
        return false;

    unlocked.insert(id);
    return true;
}

// Restores a technology from save data without prerequisite checks.
void TechnologyState::RestoreTechnology(const std::string& id)
{
    if (FindTechnologyDefinition(id) != nullptr)
        unlocked.insert(id);
}

// Clears all unlocked technologies.
void TechnologyState::Clear()
{
    unlocked.clear();
}

// Adds unlocked technology modifiers to a balance modifier set.
void TechnologyState::CollectModifiers(BalanceModifierSet& target) const
{
    for (const auto& id : unlocked)
    {
        const auto* definition = FindTechnologyDefinition(id);
        if (definition == nullptr)
            continue;

        for (auto modifier : definition->modifiers)
        {
            modifier.source = "tech:" + id;
            target.AddModifier(std::move(modifier));
        }
    }
}

bool FocusState::HasFocus(const std::string& id) const
{
    return unlocked.find(id) != unlocked.end();
}

bool FocusState::CanUnlock(const std::string& id) const
{
    const auto* definition = FindFocusDefinition(id);
    if (definition == nullptr || HasFocus(id))
        return false;
    for (const auto& prerequisite : definition->prerequisites)
        if (!HasFocus(prerequisite))
            return false;
    return true;
}

bool FocusState::CanStartFocus(const std::string& id) const
{
    return activeFocusId.empty() && CanUnlock(id);
}

bool FocusState::StartFocus(const std::string& id)
{
    if (!CanStartFocus(id))
        return false;

    const auto* definition = FindFocusDefinition(id);
    if (definition == nullptr)
        return false;

    activeFocusId = id;
    activeFocusRemaining = std::max(0.0, definition->researchTime);
    if (activeFocusRemaining <= 0.0)
        return UpdateActiveFocus(0.0);

    return true;
}

bool FocusState::UpdateActiveFocus(double dt)
{
    if (activeFocusId.empty())
        return false;

    activeFocusRemaining = std::max(0.0, activeFocusRemaining - std::max(0.0, dt));
    if (activeFocusRemaining > 0.0)
        return false;

    std::string completed = activeFocusId;
    activeFocusId.clear();
    activeFocusRemaining = 0.0;
    return UnlockFocus(completed);
}

double FocusState::GetActiveFocusProgress() const
{
    const auto* definition = FindFocusDefinition(activeFocusId);
    if (definition == nullptr || definition->researchTime <= 0.0)
        return 0.0;

    return std::clamp(1.0 - activeFocusRemaining / definition->researchTime, 0.0, 1.0);
}

bool FocusState::UnlockFocus(const std::string& id)
{
    if (!CanUnlock(id))
        return false;
    unlocked.insert(id);
    return true;
}

void FocusState::RestoreFocus(const std::string& id)
{
    if (FindFocusDefinition(id) != nullptr)
        unlocked.insert(id);
}

bool FocusState::RestoreActiveFocus(const std::string& id, double remaining)
{
    if (id.empty())
    {
        activeFocusId.clear();
        activeFocusRemaining = 0.0;
        return remaining == 0.0;
    }
    if (FindFocusDefinition(id) == nullptr || !std::isfinite(remaining) || remaining < 0.0 || HasFocus(id))
        return false;
    activeFocusId = id;
    activeFocusRemaining = remaining;
    return true;
}

void FocusState::Clear()
{
    unlocked.clear();
    activeFocusId.clear();
    activeFocusRemaining = 0.0;
}

void FocusState::CollectModifiers(BalanceModifierSet& target) const
{
    for (const auto& id : unlocked)
    {
        const auto* definition = FindFocusDefinition(id);
        if (definition == nullptr)
            continue;
        for (auto modifier : definition->modifiers)
        {
            modifier.source = "focus:" + id;
            target.AddModifier(std::move(modifier));
        }
    }
}
