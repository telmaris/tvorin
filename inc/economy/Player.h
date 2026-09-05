#ifndef PLAYER_H
#define PLAYER_H

#include "core/Types.h"
#include "economy/BuildingFactory.h"
#include "economy/BuildingConfig.h"
#include "economy/BalanceModifiers.h"
#include "core/GameCommand.h"
#include "simulation/RoadNetwork.h"
#include "data/StrategicResource.h"
#include "research/Technology.h"
#include "economy/PlayerDataTracker.h"
#include "economy/PlayerEconomy.h"
#include "economy/ConstructionQueue.h"
#include "warfare/BattleUnit.h"
#include "warfare/TaskGroup.h"
#include "world/WorldIds.h"
#include "world/PlayerState.h"
#include "world/ProvinceSimulation.h"
#include "raylib.h"

#include <optional>
#include <map>
#include <set>

class TileMap;
class Player;

// Global player state. Local buildings, registries, roads and telemetry are
// owned by ProvinceSimulation::ProvinceEconomy; this object only keeps the
// campaign-wide identity, modifiers, research and roster.
class Player : public PlayerState
{
public:
    Player() = default;
    explicit Player(int i);
    Player(int i, TileMap& tmap);
    Player(int i, ProvinceSimulation& province);
    ~Player() = default;

    void BindProvince(ProvinceSimulation& province);
    void BindProvince(ProvinceId provinceId, ProvinceSimulation& province);
    ProvinceSimulation* GetProvinceSimulation(ProvinceId provinceId) const
    {
        const auto it = boundProvinces.find(provinceId);
        return it == boundProvinces.end() ? nullptr : it->second;
    }
    ProvinceSimulation* GetProvinceSimulation() const { return boundProvince; }
    ProvinceId GetActiveProvinceId() const { return activeProvinceId; }
    TaskGroupRegistry& GetTaskGroups() { return taskGroups; }
    const TaskGroupRegistry& GetTaskGroups() const { return taskGroups; }
    bool SetActiveProvince(ProvinceId provinceId)
    {
        if (GetProvinceSimulation(provinceId) == nullptr)
            return false;
        activeProvinceId = provinceId;
        boundProvince = boundProvinces.at(provinceId);
        return true;
    }
    bool UnbindProvince(ProvinceId provinceId)
    {
        const auto it = boundProvinces.find(provinceId);
        if (it == boundProvinces.end())
            return false;
        boundProvinces.erase(it);
        if (activeProvinceId == provinceId)
        {
            activeProvinceId = InvalidProvinceId;
            boundProvince = nullptr;
            if (!boundProvinces.empty())
            {
                const auto fallback = boundProvinces.begin();
                activeProvinceId = fallback->first;
                boundProvince = fallback->second;
            }
        }
        return true;
    }
    ProvinceEconomy* GetProvinceEconomy(ProvinceId provinceId) const
    {
        auto* simulation = GetProvinceSimulation(provinceId);
        return simulation != nullptr ? &simulation->GetEconomy() : nullptr;
    }
    ProvinceEconomy* GetProvinceEconomy() const
    {
        return boundProvince != nullptr ? &boundProvince->GetEconomy() : nullptr;
    }
    TileMap* GetTileMap() const;
    TileMap* GetTileMap(ProvinceId provinceId) const;
    void RebindTileMap(ProvinceId provinceId, TileMap& map);
    void RebindTileMap(TileMap& map);

    void UpdateFocus(double dt);
    void UpdateResearch(double dt);

    void UpdateEconomyTelemetry(double dt);
    bool IsTechnologyInProgress(const std::string& id) const;

    // Registers a newly placed building in player data indexes.
    void RegisterBuilding(Building* building);

    // Removes a building from player data indexes before it is destroyed.
    void UnregisterBuilding(Building* building);

    // Records a gameplay command accepted for this player.
    void TrackAcceptedCommand(GameCommandType type);
    void TrackAcceptedCommand(GameCommandType type, ProvinceId provinceId);

    // Returns tracked player buildings without scanning the map.
    const std::set<Building*>& GetTrackedBuildings() const;

    template<typename T>
    const std::set<Building*>& GetTrackedBuildingsWithComponent() const
    {
        const auto* economy = GetProvinceEconomy();
        if (economy == nullptr)
        {
            static const std::set<Building*> empty;
            return empty;
        }
        return economy->dataTracker.BuildingsWithComponent<T>();
    }

    // Returns whether the player has a tracked building of this type.
    bool HasTrackedBuilding(BuildingType type, bool completedOnly = false) const
    {
        const auto* economy = GetProvinceEconomy();
        return economy != nullptr && economy->dataTracker.HasBuilding(type, completedOnly);
    }

    // Returns the number of tracked buildings of this type.
    int GetTrackedBuildingCount(BuildingType type, bool completedOnly = false) const
    {
        const auto* economy = GetProvinceEconomy();
        return economy != nullptr ? economy->dataTracker.CountBuildings(type, completedOnly) : 0;
    }

    std::size_t GetLiveShipmentCount() const
    {
        const auto* economy = GetProvinceEconomy();
        return economy != nullptr && economy->roadNetwork != nullptr
            ? economy->roadNetwork->GetLiveShipmentCount() : 0;
    }

    // Read-only access for routing diagnostics and tutorial milestones. The
    // graph is still mutated only by simulation-side building commands.
    RoadNetwork* GetRoadNetwork() const
    {
        const auto* economy = GetProvinceEconomy();
        return economy != nullptr ? economy->roadNetwork.get() : nullptr;
    }

    // Returns how many accepted commands of a given type were processed.
    int GetAcceptedCommandCount(GameCommandType type) const
    {
        const auto* economy = GetProvinceEconomy();
        if (economy == nullptr)
            return 0;
        auto it = economy->dataTracker.processedCommands.find(type);
        return it != economy->dataTracker.processedCommands.end() ? it->second : 0;
    }

    // Builds a building type on a tile id and registers it in logistics.
    template <typename T>
    Building* Build(int tilePos, bool chargeCost = true)
    {
        static_assert(std::is_base_of<Building, T>::value);
        ProvinceEconomy* economy = GetProvinceEconomy();
        if (economy == nullptr || economy->tilemap == nullptr)
            return nullptr;
        TileMap& tilemap = *economy->tilemap;
        T preview{0};
        Vec2i anchor = tilemap.GetCoordsFromId(tilePos);
        if (!tilemap.CanPlaceBuilding(preview.buildingType, anchor, preview.GetFootprint(), this))
            return nullptr;

        const auto& definition = GetBuildingDefinition(preview.buildingType);
        const auto effectiveCosts = GetEffectiveBuildCosts(definition);
        if (chargeCost && !TryPayBuildCost(effectiveCosts))
        {
            ReportBuildCostFailure(definition.name);
            return nullptr;
        }

        economy->build.Build<T>(tilePos);
        auto bld = tilemap.GetBuilding(tilePos);
        if (bld != nullptr)
        {
            double buildTime = ModifyBalanceAt(BalanceStat::BuildTime, definition.buildTime, preview.buildingType, anchor);
            bld->buildTime = buildTime;
            bld->constructionRemaining = chargeCost ? buildTime : 0.0;
            bld->buildCostRecordState = chargeCost ? BuildCostRecordState::PaidRecorded
                                                   : BuildCostRecordState::Free;
            bld->buildCostWasPaid = chargeCost;
            bld->paidBuildCosts = chargeCost ? effectiveCosts : std::vector<ResourceAmountDefinition>{};
            if (!bld->IsUnderConstruction())
            {
                if (economy->roadNetwork != nullptr)
                    for (int occupiedTileId : tilemap.GetBuildingTileIds(bld))
                        economy->roadNetwork->UpdateNavMap(occupiedTileId, bld);
                tilemap.AutoConnectBuilding(bld);
            }
        }
        return bld;
    }

    // Builds a building type on map coordinates.
    template <typename T>
    Building* Build(Vec2i pos, bool chargeCost = true)
    {
        ProvinceEconomy* economy = GetProvinceEconomy();
        return economy != nullptr && economy->tilemap != nullptr
            ? Build<T>(economy->tilemap->GetIdFromCoords(pos), chargeCost) : nullptr;
    }

    bool HasBuildResources(const std::vector<ResourceAmountDefinition>& costs) const;
    bool HasBuildResources(const ProvinceEconomy& economy,
                           const std::vector<ResourceAmountDefinition>& costs) const;
    void ReportBuildCostFailure(const std::string& buildingName) const;
    // Returns only technology/focus gates, without checking the building's
    // material cost. Upgrade commands reuse this for their own next-level
    // cost so UI and authority agree on unlock requirements.
    std::vector<std::string> GetBuildUnlockRequirementFailures(const BuildingDefinition& definition) const;
    std::vector<std::string> GetBuildRequirementFailures(const BuildingDefinition& definition, bool ignoreDebugFreeBuild = true) const;
    std::vector<std::string> GetBuildRequirementFailures(
        const BuildingDefinition& definition, const ProvinceEconomy& economy,
        bool ignoreDebugFreeBuild = true) const;

    // Applies BalanceStat::BuildCost modifiers (tech/focus/state) to a building's base resource costs.
    std::vector<ResourceAmountDefinition> GetEffectiveBuildCosts(const BuildingDefinition& definition) const
    {
        std::vector<ResourceAmountDefinition> effective;
        effective.reserve(definition.buildCosts.size());
        for (const auto& cost : definition.buildCosts)
        {
            int amount = ModifyBalanceInt(BalanceStat::BuildCost, cost.amount, definition.type, cost.type, 0);
            effective.push_back({cost.type, amount});
        }
        return effective;
    }

    // Returns true when all build unlock requirements and costs are currently satisfied.
    bool CanBuildDefinition(const BuildingDefinition& definition) const
    {
        // This query is also used by AI players. Debug mode makes construction
        // free only for the local human (GameWorld.Commands.cpp), so the
        // generic affordability check must never suppress resource failures
        // merely because the map itself is a debug map. The UI handles the
        // local player's free-build exception explicitly.
        return GetBuildRequirementFailures(definition, false).empty();
    }

    int GetPopulationCap() const;

    double GetTotalPopulation() const
    {
        return strategicResources.Get(StrategicResourceType::Manpower) +
               strategicResources.Get(StrategicResourceType::Workers) +
               strategicResources.Get(StrategicResourceType::Soldiers);
    }

    double GetFoodProductivity() const;
    double GetFoodProductivity(const ProvinceEconomy& economy) const;
    // Raw food supply ratio (0-1) averaged across villages — see Player.cpp
    // for why this must not be confused with GetFoodProductivity().
    double GetFoodSupplyRatio() const;
    double GetFoodSupplyRatio(const ProvinceEconomy& economy) const;

    double ModifyBalance(BalanceStat stat, double base, BuildingType buildingType = BuildingType::Building,
                         ResourceType resourceType = ResourceType::Null) const
    {
        return balanceModifiers.ModifyDouble(base, MakeBalanceContext(stat, buildingType, resourceType));
    }

    int ModifyBalanceInt(BalanceStat stat, int base, BuildingType buildingType = BuildingType::Building,
                         ResourceType resourceType = ResourceType::Null,
                         int minimum = 0) const
    {
        return balanceModifiers.ModifyInt(base, MakeBalanceContext(stat, buildingType, resourceType), minimum);
    }

    double ModifyBalanceAt(BalanceStat stat, double base, BuildingType buildingType, Vec2i position,
                           ResourceType resourceType = ResourceType::Null) const
    {
        return balanceModifiers.ModifyDouble(base, MakeBalanceContext(stat, buildingType, resourceType, position));
    }

    int ModifyBalanceIntAt(BalanceStat stat, int base, BuildingType buildingType, Vec2i position,
                           ResourceType resourceType = ResourceType::Null,
                           int minimum = 0) const
    {
        return balanceModifiers.ModifyInt(base, MakeBalanceContext(stat, buildingType, resourceType, position), minimum);
    }

    double ModifyBalanceForBuilding(BalanceStat stat, double base, const Building* building,
                                    ResourceType resourceType = ResourceType::Null) const
    {
        return balanceModifiers.ModifyDouble(base, MakeBalanceContext(stat, building, resourceType));
    }

    int ModifyBalanceIntForBuilding(BalanceStat stat, int base, const Building* building,
                                    ResourceType resourceType = ResourceType::Null,
                                    int minimum = 0) const
    {
        return balanceModifiers.ModifyInt(base, MakeBalanceContext(stat, building, resourceType), minimum);
    }

    double ModifyBalanceAt(BalanceStat stat, double base, ProvinceId provinceId,
                           BuildingType buildingType, Vec2i position,
                           ResourceType resourceType = ResourceType::Null) const
    {
        return balanceModifiers.ModifyDouble(
            base, MakeBalanceContext(stat, buildingType, resourceType, position, provinceId));
    }

    // Resolves a unit-scoped stat while retaining the province/building
    // context. Used by garrison upkeep and future unit-specific province
    // effects; the unit definition ID is never stored in a building component.
    double ModifyBalanceForUnit(BalanceStat stat, double base, const Building* building,
                                const std::string& unitDefId,
                                ResourceType resourceType = ResourceType::Null) const
    {
        BalanceModifierContext context = MakeBalanceContext(stat, building, resourceType);
        context.unitDefId = unitDefId;
        return balanceModifiers.ModifyDouble(base, context);
    }

    // Resolves a floating-point stat for a concrete building context.
    double ResolveStat(const Stat<double>& stat, const Building* building,
                       ResourceType resourceType = ResourceType::Null) const
    {
        return ModifyBalanceForBuilding(stat.GetStatId(), stat.GetBase(), building, resourceType);
    }

    // Resolves an integer stat for a concrete building context.
    int ResolveStat(const Stat<int>& stat, const Building* building,
                    ResourceType resourceType = ResourceType::Null,
                    int minimum = 0) const
    {
        return ModifyBalanceIntForBuilding(stat.GetStatId(), stat.GetBase(), building, resourceType, minimum);
    }

    // Resolves a floating-point stat for a map-position context before a building exists.
    double ResolveStatAt(const Stat<double>& stat, BuildingType buildingType, Vec2i position,
                         ResourceType resourceType = ResourceType::Null) const
    {
        return ModifyBalanceAt(stat.GetStatId(), stat.GetBase(), buildingType, position, resourceType);
    }

    // Resolves an integer stat for a map-position context before a building exists.
    int ResolveStatAt(const Stat<int>& stat, BuildingType buildingType, Vec2i position,
                      ResourceType resourceType = ResourceType::Null,
                      int minimum = 0) const
    {
        return ModifyBalanceIntAt(stat.GetStatId(), stat.GetBase(), buildingType, position, resourceType, minimum);
    }

    bool CanResearchTechnology(const std::string& id) const;
    bool CanResearchTechnology(const std::string& id,
                               const ProvinceEconomy& economy) const;

    bool CanUnlockFocus(const std::string& id) const
    {
        const auto* definition = FindFocusDefinition(id);
        return definition != nullptr && focuses.CanStartFocus(id);
    }

    bool UnlockFocus(const std::string& id);

    bool StartFocus(const std::string& id);

    bool UnlockTechnology(const std::string& id);
    bool StartTechnologyResearch(const std::string& id, Building* university);
    bool StartTechnologyResearch(const std::string& id, Building* university,
                                 ProvinceEconomy& economy);

    // Rebuilds the modifier set entries emitted by unlocked technologies.
    void RefreshTechnologyModifiers();

    // Rebuilds all per-building upgrade modifiers from the currently tracked
    // buildings. Persistence calls this after the complete building section
    // has been read, so modifier state never depends on serialization order.
    void RefreshUpgradeModifiers();

    // (Re-)applies the BalanceModifiers for a building's current
    // UpgradeComponent::level, replacing whatever the previous level had.
    // Called when an upgrade completes, and once per upgraded building after
    // loading a save (upgrade level is persisted; the modifiers it implies
    // are re-derived, the same way tech/focus modifiers are).
    void ApplyUpgradeLevelModifiers(Building& building);

    // Clears all unlocked tech/focuses and cancels in-progress research (debug helper).
    void ResetResearchState();

    BalanceModifierContext MakeBalanceContext(BalanceStat stat, BuildingType buildingType,
                                              ResourceType resourceType = ResourceType::Null,
                                              std::optional<Vec2i> position = std::nullopt,
                                              ProvinceId provinceId = InvalidProvinceId) const
    {
        BalanceModifierContext context{stat, buildingType, resourceType};
        context.position = position;
        context.provinceId = provinceId;
        const TileMap* map = provinceId != InvalidProvinceId
            ? GetTileMap(provinceId) : GetTileMap();
        if (position.has_value() && map != nullptr && map->IsInside(position.value()))
            context.positionId = map->GetIdFromCoords(position.value());
        return context;
    }

    BalanceModifierContext MakeBalanceContext(BalanceStat stat, const Building* building,
                                              ResourceType resourceType = ResourceType::Null) const
    {
        BalanceModifierContext context = MakeBalanceContext(
            stat,
            building != nullptr ? building->buildingType : BuildingType::Building,
            resourceType);

        if (building == nullptr)
            return context;

        context.buildingId = building->id;
        context.positionId = building->positionId;
        context.provinceId = building->provinceId;
        const TileMap* map = building->GetProvinceEconomy() != nullptr
            ? building->GetProvinceEconomy()->tilemap : GetTileMap();
        if (building->positionId >= 0 && map != nullptr)
            context.position = map->GetCoordsFromId(building->positionId);
        return context;
    }

    // TD(etap-3): unit-stat modifier lookup, mirroring the building-scoped
    // overloads above — filters BalanceModifier::unitDefId (see
    // BalanceModifiers.h) so a tech/focus can target one unit type.
    double ModifyBalanceForUnit(BalanceStat stat, double base, const std::string& unitDefId) const
    {
        BalanceModifierContext context{stat};
        context.unitDefId = unitDefId;
        return balanceModifiers.ModifyDouble(base, context);
    }

    double AddManpower(double amount);
    int AutoAssignWorkers(Building* building);
    bool TryPayBuildCost(const std::vector<ResourceAmountDefinition>& costs);
    bool TryPayBuildCost(ProvinceEconomy& economy,
                         const std::vector<ResourceAmountDefinition>& costs);
    // Returns resources to owned storage (cancelling an in-progress build).
    // Overflow beyond available storage capacity is dropped.
    void RefundBuildCost(const std::vector<ResourceAmountDefinition>& costs);
    void RefundBuildCost(ProvinceEconomy& economy,
                         const std::vector<ResourceAmountDefinition>& costs);

    // Starts resource transport through this player's road network.
    bool BeginTransport(Building* src, Building* dest, Resource* res);

private:
    ProvinceEconomy* GetCampaignEconomy() const;
    ProvinceSimulation* boundProvince{nullptr};
    // Runtime-only index. Province ownership and active-view selection are
    // serialized as IDs elsewhere; these pointers are rebuilt after load.
    std::map<ProvinceId, ProvinceSimulation*> boundProvinces;
    ProvinceId activeProvinceId{InvalidProvinceId};
    // Standalone economy used only by direct domain tests and tools that
    // construct a Player with a raw TileMap. GameWorld players bind to a
    // GlobalMap-owned ProvinceSimulation instead.
    std::unique_ptr<ProvinceSimulation> compatibilityProvince;
};

// Local human-controlled player type.
class HumanPlayer : public Player
{
public:
    HumanPlayer() = default;
};

#endif
