#ifndef WORLD_PROVINCE_DEFINITION_H
#define WORLD_PROVINCE_DEFINITION_H

#include "economy/BalanceStats.h"
#include "world/Province.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

enum class ProvinceDefinitionArchetype : std::uint8_t
{
    Buildable,
    NeutralCity,
    BanditCamp,
    EventSite
};

enum class ProvinceEffectTarget : std::uint8_t
{
    SelfProvince,
    AdjacentProvince,
    IncidentConnection
};

struct ProvinceIntRange
{
    int min{0};
    int max{0};
};

struct ProvinceValueRange
{
    double min{0.0};
    double max{0.0};
};

struct ProvinceEffectDefinition
{
    ProvinceEffectTarget target{ProvinceEffectTarget::SelfProvince};
    BalanceStat stat{BalanceStat::BuildTime};
    double additive{0.0};
    double multiplier{1.0};
    std::string condition;
};

struct ProvinceTraitDefinition
{
    std::string id;
    std::string displayName;
    std::vector<ProvinceEffectDefinition> effects;
};

// Immutable definition data. Runtime province instances store selected values
// and mutable state, never a pointer to this catalog.
struct ProvinceDefinition
{
    std::string id;
    ProvinceDefinitionArchetype archetype{ProvinceDefinitionArchetype::Buildable};
    std::string displayName;
    int generationWeight{0};

    ProvinceIntRange sizeX{201, 501};
    ProvinceIntRange sizeY{201, 501};
    ProvinceValueRange resourceWealth{0.0, 1.0};
    ProvinceValueRange resourceDensity{0.0, 1.0};
    ProvinceValueRange resourceFieldSize{0.0, 1.0};
    ProvinceValueRange resourceRichness{0.0, 1.0};
    ProvinceValueRange waterAmount{0.0, 1.0};
    ProvinceValueRange mountainAmount{0.0, 1.0};
    ProvinceValueRange ruggedness{0.0, 1.0};

    ProvinceIntRange cityWealth{0, 0};
    std::map<std::string, ProvinceIntRange> cityInitialStock;
    std::map<std::string, ProvinceValueRange> cityBuyPrices;
    std::map<std::string, ProvinceValueRange> cityTradePrices;
    double barterPenaltyMultiplier{1.25};

    ProvinceIntRange banditStrength{0, 0};
    std::string banditLootTableId;
    int banditRaidWeight{0};

    std::string eventPoolId;
    std::vector<ProvinceTraitDefinition> traits;

    ProvinceKind Kind() const;
};

using ProvinceDefinitionCatalog = std::map<std::string, ProvinceDefinition>;

struct ProvinceDefinitionDiagnostic
{
    std::string path;
    std::size_t line{0};
    std::string message;
};

struct ProvinceDefinitionCatalogLoadResult
{
    ProvinceDefinitionCatalog definitions;
    std::vector<ProvinceDefinitionDiagnostic> diagnostics;

    bool IsValid() const { return diagnostics.empty() && !definitions.empty(); }
};

ProvinceDefinitionCatalogLoadResult LoadProvinceDefinitionCatalog(const std::string& path);
inline ProvinceDefinitionCatalogLoadResult LoadProvinceDefinitionsFromFile(const std::string& path)
{
    return LoadProvinceDefinitionCatalog(path);
}

// The runtime catalog is loaded once from the shipped data file. Callers only
// receive const access; tests and tools should use LoadProvinceDefinitionCatalog.
const ProvinceDefinitionCatalog& GetProvinceDefinitions();
const ProvinceDefinition* FindProvinceDefinition(std::string_view id);

#endif
