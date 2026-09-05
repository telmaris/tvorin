#ifndef WORLD_PROVINCE_CONNECTION_H
#define WORLD_PROVINCE_CONNECTION_H

#include "data/Resource.h"
#include "world/WorldIds.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct RouteModifierContext
{
    ProvinceId sourceProvinceId{InvalidProvinceId};
    ProvinceId targetProvinceId{InvalidProvinceId};
    PlayerId playerId{InvalidPlayerId};
};

struct RouteTraversalStats
{
    double traversalTimeMultiplier{1.0};
    int incidentChanceReductionBasisPoints{0};
};

struct ProvinceConnectionResourceCost
{
    ResourceType type{ResourceType::Null};
    int amount{0};
};

struct ProvinceConnectionLevelDefinition
{
    int level{0};
    double traversalTimeMultiplier{1.0};
    int incidentChanceReductionBasisPoints{0};
    std::uint64_t upgradeDurationTicks{0};
    std::vector<ProvinceConnectionResourceCost> upgradeCost;
};

struct ProvinceConnectionDefinition
{
    std::string id;
    std::vector<ProvinceConnectionLevelDefinition> levels;
};

using ProvinceConnectionDefinitionCatalog =
    std::map<std::string, ProvinceConnectionDefinition>;

struct ProvinceConnectionDefinitionDiagnostic
{
    std::string path;
    std::size_t line{0};
    std::string message;
};

struct ProvinceConnectionDefinitionLoadResult
{
    ProvinceConnectionDefinitionCatalog definitions;
    std::vector<ProvinceConnectionDefinitionDiagnostic> diagnostics;

    bool IsValid() const { return diagnostics.empty() && !definitions.empty(); }
};

ProvinceConnectionDefinitionLoadResult LoadProvinceConnectionDefinitionsFromFile(
    const std::string& path);
const ProvinceConnectionDefinitionCatalog& GetProvinceConnectionDefinitions();
const ProvinceConnectionDefinition* FindProvinceConnectionDefinition(std::string_view id);

class IProvinceConnection
{
public:
    virtual ~IProvinceConnection() = default;
    virtual ProvinceConnectionId GetId() const = 0;
    virtual ProvinceId GetFirstProvinceId() const = 0;
    virtual ProvinceId GetSecondProvinceId() const = 0;
    virtual int GetLevel() const = 0;
    // Immutable physical distance used by every campaign journey.  The base
    // fallback keeps lightweight test doubles source-compatible; production
    // routes override it with their canonical map-derived value.
    virtual int GetLengthUnits() const { return 1; }
    virtual RouteTraversalStats ResolveTraversalStats(
        const RouteModifierContext& context) const = 0;
};

class LandRouteConnection final : public IProvinceConnection
{
public:
    LandRouteConnection(ProvinceConnectionId id, ProvinceId firstProvinceId,
                        ProvinceId secondProvinceId,
                        std::string definitionId = "land_route",
                        int lengthUnits = 1);

    ProvinceConnectionId GetId() const override { return id; }
    ProvinceId GetFirstProvinceId() const override { return firstProvinceId; }
    ProvinceId GetSecondProvinceId() const override { return secondProvinceId; }
    int GetLevel() const override { return level; }
    int GetLengthUnits() const override { return lengthUnits; }
    RouteTraversalStats ResolveTraversalStats(
        const RouteModifierContext& context) const override;

    const std::string& GetDefinitionId() const { return definitionId; }
    int GetMaxLevel() const;
    bool IsUpgradeInProgress() const { return upgradeTargetLevel >= 0; }
    int GetUpgradeTargetLevel() const { return upgradeTargetLevel; }
    std::uint64_t GetUpgradeRemainingTicks() const { return upgradeRemainingTicks; }
    const ProvinceConnectionLevelDefinition* GetLevelDefinition(int requestedLevel) const;
    const ProvinceConnectionLevelDefinition* GetNextLevelDefinition() const;

    bool BeginUpgrade(int requestedLevel, std::uint64_t durationTicks);
    void Update();
    bool SetRuntimeState(int currentLevel, int targetLevel,
                         std::uint64_t remainingTicks);

private:
    ProvinceConnectionId id{InvalidProvinceConnectionId};
    ProvinceId firstProvinceId{InvalidProvinceId};
    ProvinceId secondProvinceId{InvalidProvinceId};
    std::string definitionId;
    int lengthUnits{1};
    int level{0};
    int upgradeTargetLevel{-1};
    std::uint64_t upgradeRemainingTicks{0};
};

#endif
