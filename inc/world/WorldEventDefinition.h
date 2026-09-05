#ifndef WORLD_EVENT_DEFINITION_H
#define WORLD_EVENT_DEFINITION_H

#include "economy/BalanceStats.h"
#include "data/Resource.h"
#include "world/Province.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

enum class WorldEventTriggerDomain : std::uint8_t
{
    ProvincePeriodic,
    Discovery,
    Route,
    Raid
};

struct ModifyResourceEffect { ResourceType type{ResourceType::Null}; int amount{0}; };
struct AddTimedModifierEffect
{
    BalanceStat stat{BalanceStat::BuildTime};
    double additive{0.0};
    double multiplier{1.0};
    std::uint64_t durationTicks{0};
};
struct ModifyTradeScoreEffect { int amount{0}; };
struct DamageCityStockEffect { ResourceType type{ResourceType::Null}; int amount{0}; };
struct KillJourneyUnitsEffect { int amount{0}; };
struct StartRaidEffect { int strength{0}; };
struct DamageProvinceStockEffect { ResourceType type{ResourceType::Null}; int amount{0}; };
struct DestroyBuildingEffect { int amount{0}; };

using WorldEventEffect = std::variant<ModifyResourceEffect, AddTimedModifierEffect,
                                      ModifyTradeScoreEffect, DamageCityStockEffect,
                                      KillJourneyUnitsEffect, StartRaidEffect,
                                      DamageProvinceStockEffect, DestroyBuildingEffect>;

struct WorldEventDefinition
{
    std::string id;
    std::string name;
    std::string title;
    std::string description;
    WorldEventTriggerDomain trigger{WorldEventTriggerDomain::ProvincePeriodic};
    int chanceBasisPoints{0};
    int weight{0};
    std::uint64_t durationTicks{0};
    std::uint64_t checkIntervalTicks{0};
    std::vector<ProvinceKind> allowedKinds;
    std::vector<ProvinceKind> excludedKinds;
    std::string requiredTrait;
    std::string requiredAdjacentTrait;
    int minimumRouteLevel{0};
    std::vector<WorldEventEffect> effects;
    std::string followUpPoolId;
};

using WorldEventCatalog = std::map<std::string, WorldEventDefinition>;

struct WorldEventDefinitionDiagnostic
{
    std::string path;
    std::size_t line{0};
    std::string message;
};

struct WorldEventDefinitionLoadResult
{
    WorldEventCatalog definitions;
    std::vector<WorldEventDefinitionDiagnostic> diagnostics;
    bool IsValid() const { return diagnostics.empty() && !definitions.empty(); }
};

WorldEventDefinitionLoadResult LoadWorldEventDefinitionsFromFile(const std::string& path);
const WorldEventCatalog& GetWorldEventCatalog();
const WorldEventDefinition* FindWorldEventDefinition(std::string_view id);

#endif
