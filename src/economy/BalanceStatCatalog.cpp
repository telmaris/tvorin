#include "economy/BalanceStatCatalog.h"

#include <algorithm>

const std::vector<BalanceStatCatalogEntry>& GetBalanceStatCatalog()
{
    static const std::vector<BalanceStatCatalogEntry> catalog{
        {BalanceStat::BuildTime, "BuildTime", "Build time", true},
        {BalanceStat::BuildCost, "BuildCost", "Build cost", true},
        {BalanceStat::ProductionCycleTime, "ProductionCycleTime", "Production cycle time", true},
        {BalanceStat::ProductionOutputAmount, "ProductionOutputAmount", "Production output", false},
        {BalanceStat::WorkerCapacity, "WorkerCapacity", "Worker capacity", true},
        {BalanceStat::TransportTime, "TransportTime", "Transport time", true},
        {BalanceStat::RoadCapacity, "RoadCapacity", "Road capacity", false},
        {BalanceStat::RoadSpeed, "RoadSpeed", "Road speed", false},
        {BalanceStat::ManpowerRate, "ManpowerRate", "Manpower growth", false},
        {BalanceStat::PopulationCap, "PopulationCap", "Population cap", false},
        {BalanceStat::BuilderAmount, "BuilderAmount", "Builders", false},
        {BalanceStat::UnitHp, "UnitHp", "Unit HP", false},
        {BalanceStat::UnitFieldAttack, "UnitFieldAttack", "Unit field attack", false},
        {BalanceStat::UnitSiegePower, "UnitSiegePower", "Unit siege power", false},
        {BalanceStat::UnitArmor, "UnitArmor", "Unit armor", false},
        {BalanceStat::UnitMoveSpeed, "UnitMoveSpeed", "Unit move speed", false},
        {BalanceStat::UnitAttackSpeed, "UnitAttackSpeed", "Unit attack speed", false},
        {BalanceStat::UnitRecruitTime, "UnitRecruitTime", "Unit recruit time", true},
        {BalanceStat::UnitRecruitManpowerCost, "UnitRecruitManpowerCost", "Unit manpower cost", true},
        {BalanceStat::ProvinceFortification, "ProvinceFortification", "Province fortification", false},
        {BalanceStat::ProvinceDefense, "ProvinceDefense", "Province defense", false},
        {BalanceStat::ProvinceCounterattack, "ProvinceCounterattack", "Province counterattack", false},
        {BalanceStat::ConquestSpoilsFraction, "ConquestSpoilsFraction", "Conquest spoils", false},
        {BalanceStat::ProvinceDefensePower, "ProvinceDefensePower", "Province defense power", false},
        {BalanceStat::ProvinceDefenseCoverage, "ProvinceDefenseCoverage", "Province defense coverage", false},
        {BalanceStat::ProvinceDefenseReadiness, "ProvinceDefenseReadiness", "Province defense readiness", false},
        {BalanceStat::ProvinceDefenseSupplyUse, "ProvinceDefenseSupplyUse", "Province defense supply use", true},
        {BalanceStat::TransportDispatchDelay, "TransportDispatchDelay", "Cargo dispatch delay", true},
        {BalanceStat::VillageSupplyConsumption, "VillageSupplyConsumption", "Village supply consumption", true},
        {BalanceStat::RouteTravelSpeed, "RouteTravelSpeed", "Route travel speed", false},
        {BalanceStat::RouteIncidentChance, "RouteIncidentChance", "Route incident chance", true},
        {BalanceStat::TradeExchangeRate, "TradeExchangeRate", "Trade exchange rate", true},
        {BalanceStat::TradeScoreGain, "TradeScoreGain", "Trade score gain", false},
        {BalanceStat::BattleAttack, "BattleAttack", "Battle attack", false},
        {BalanceStat::BattleCasualtyRate, "BattleCasualtyRate", "Battle casualty rate", true},
        {BalanceStat::BattleDuration, "BattleDuration", "Battle duration", true},
        {BalanceStat::GarrisonCapacity, "GarrisonCapacity", "Garrison capacity", false},
        {BalanceStat::GarrisonFoodUpkeep, "GarrisonFoodUpkeep", "Garrison food upkeep", true},
        {BalanceStat::RaidBuildingDestructionChance, "RaidBuildingDestructionChance", "Raid building destruction chance", true},
        {BalanceStat::RaidStockLossFraction, "RaidStockLossFraction", "Raid stock loss fraction", true},
        {BalanceStat::ProvinceEventChance, "ProvinceEventChance", "Province event chance", true},
        {BalanceStat::ProvinceEventWeight, "ProvinceEventWeight", "Province event weight", false},
        {BalanceStat::ProvinceEventDuration, "ProvinceEventDuration", "Province event duration", true},
        {BalanceStat::ColonizationDuration, "ColonizationDuration", "Colonization duration", true},
        {BalanceStat::ArmySupplyConsumptionReduction, "ArmySupplyConsumptionReduction", "Army supply consumption reduction", true}
    };
    return catalog;
}

std::optional<BalanceStat> TryParseBalanceStat(std::string_view serializedName)
{
    const auto& catalog = GetBalanceStatCatalog();
    const auto it = std::find_if(catalog.begin(), catalog.end(),
        [serializedName](const BalanceStatCatalogEntry& entry)
        {
            return serializedName == entry.serializedName;
        });
    if (it == catalog.end())
        return std::nullopt;
    return it->value;
}

const BalanceStatCatalogEntry* FindBalanceStatCatalogEntry(BalanceStat value)
{
    const auto& catalog = GetBalanceStatCatalog();
    const auto it = std::find_if(catalog.begin(), catalog.end(),
        [value](const BalanceStatCatalogEntry& entry) { return entry.value == value; });
    return it == catalog.end() ? nullptr : &*it;
}
