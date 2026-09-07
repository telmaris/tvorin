#include "economy/BalanceStatDisplay.h"
#include "economy/BalanceStatCatalog.h"

#include <cmath>

const char* BalanceStatLabel(BalanceStat stat)
{
    const auto* entry = FindBalanceStatCatalogEntry(stat);
    return entry != nullptr ? entry->displayName : "Effect";
}

bool LowerValueIsBetter(BalanceStat stat)
{
    const auto* entry = FindBalanceStatCatalogEntry(stat);
    return entry != nullptr && entry->lowerValueIsBetter;
}

const char* ImprovedRateLabel(BalanceStat stat)
{
    switch (stat)
    {
        case BalanceStat::BuildTime: return "Build speed";
        case BalanceStat::ProductionCycleTime: return "Production speed";
        case BalanceStat::TransportTime: return "Transport speed";
        case BalanceStat::TransportDispatchDelay: return "Cargo dispatch speed";
        default: return BalanceStatLabel(stat);
    }
}

bool IsPositiveModifier(const BalanceModifier& modifier)
{
    bool lowerIsBetter = LowerValueIsBetter(modifier.stat);
    if (std::abs(modifier.additive) > 0.001)
        return lowerIsBetter ? modifier.additive < 0.0 : modifier.additive > 0.0;
    if (std::abs(modifier.multiplier - 1.0) > 0.001)
        return lowerIsBetter ? modifier.multiplier < 1.0 : modifier.multiplier > 1.0;
    return true;
}

const char* BalanceBuildingLabel(BuildingType type)
{
    switch (type)
    {
        case BuildingType::Headquarters: return "Headquarters";
        case BuildingType::Village: return "Village";
        case BuildingType::StorageBuilding: return "Storage";
        case BuildingType::Woodcutter: return "Woodcutter";
        case BuildingType::HuntersHut: return "Hunters Hut";
        case BuildingType::LumberMill: return "Lumber Mill";
        case BuildingType::Mine: return "Mine";
        case BuildingType::Foundry: return "Foundry";
        case BuildingType::Well: return "Well";
        case BuildingType::WheatFarm: return "Wheat Farm";
        case BuildingType::Windmill: return "Windmill";
        case BuildingType::Bakery: return "Bakery";
        case BuildingType::Inn: return "Inn";
        case BuildingType::Paperworks: return "Paperworks";
        case BuildingType::Smith: return "Smith";
        case BuildingType::Mint: return "Mint";
        case BuildingType::Glassworks: return "Glassworks";
        case BuildingType::Powderworks: return "Powderworks";
        case BuildingType::University: return "University";
        case BuildingType::Barracks: return "Barracks";
        case BuildingType::Road: return "Road";
        case BuildingType::AnimalFarm: return "Animal Farm";
        case BuildingType::Butcher: return "Butcher";
        case BuildingType::Tannery: return "Tannery";
        case BuildingType::Tailor: return "Tailor";
        case BuildingType::Armorer: return "Armorer";
        case BuildingType::HorseStable: return "Horse Stable";
        case BuildingType::Kiln: return "Kiln";
        case BuildingType::HouseholdWorkshop: return "Household Workshop";
        case BuildingType::Soapworks: return "Soapworks";
        case BuildingType::Inkworks: return "Inkworks";
        case BuildingType::Scriptorium: return "Scriptorium";
        case BuildingType::Copperworks: return "Copperworks";
        case BuildingType::UrbanWorkshop: return "Urban Workshop";
        case BuildingType::HempFarm: return "Hemp Farm";
        case BuildingType::Ropery: return "Ropery";
        case BuildingType::Weaver: return "Weaver";
        case BuildingType::Bowyer: return "Bowyer";
        case BuildingType::SpearWorkshop: return "Spear Workshop";
        case BuildingType::SiegeWorkshop: return "Siege Workshop";
        default: return "Building";
    }
}
