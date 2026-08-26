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
#include "economy/ConqueredEconomy.h"
#include "warfare/BattleUnit.h"
#include "raylib.h"

#include <optional>
#include <set>

class TileMap;
class Player;

enum class PlayerControllerType
{
    LocalHuman,
    AI,
    Remote
};

// Player-owned state: buildings, logistics network and strategic resources.
class Player
{
public:
    Player() = default;
    Player(int i, TileMap& tmap) : tilemap(&tmap), id(i), build(this, tmap, id)
    {
        roadNetwork = std::make_unique<RoadNetwork>(tmap);
        RefreshTechnologyModifiers();
    }

    void RebindTileMap(TileMap& map)
    {
        tilemap = &map;
        build.RebindTileMap(map);
    }

    void UpdateFocus(double dt);
    void UpdateResearch(double dt);

    void UpdateEconomyTelemetry(double dt) { economyTelemetry.Update(*this, dt); }
    // TD(etap-6.3): advances productivity ramps on buildings captured from an
    // eliminated player (no-op once nothing is ramping).
    void UpdateConqueredEconomy(double dt) { conqueredEconomy.Tick(*this, dt); }

    bool IsTechnologyInProgress(const std::string& id) const;

    // Registers a newly placed building in player data indexes.
    void RegisterBuilding(Building* building);

    // Removes a building from player data indexes before it is destroyed.
    void UnregisterBuilding(Building* building);

    // Records a gameplay command accepted for this player.
    void TrackAcceptedCommand(GameCommandType type) { dataTracker.TrackCommand(type); }

    // Returns tracked player buildings without scanning the map.
    const std::set<Building*>& GetTrackedBuildings() const { return dataTracker.buildings; }

    template<typename T>
    const std::set<Building*>& GetTrackedBuildingsWithComponent() const
    {
        return dataTracker.BuildingsWithComponent<T>();
    }

    // Returns whether the player has a tracked building of this type.
    bool HasTrackedBuilding(BuildingType type, bool completedOnly = false) const
    {
        return dataTracker.HasBuilding(type, completedOnly);
    }

    // Returns the number of tracked buildings of this type.
    int GetTrackedBuildingCount(BuildingType type, bool completedOnly = false) const
    {
        return dataTracker.CountBuildings(type, completedOnly);
    }

    std::size_t GetLiveShipmentCount() const
    {
        return roadNetwork != nullptr ? roadNetwork->GetLiveShipmentCount() : 0;
    }

    // Read-only access for routing diagnostics and tutorial milestones. The
    // graph is still mutated only by simulation-side building commands.
    RoadNetwork* GetRoadNetwork() const { return roadNetwork.get(); }

    // Returns how many accepted commands of a given type were processed.
    int GetAcceptedCommandCount(GameCommandType type) const
    {
        auto it = dataTracker.processedCommands.find(type);
        return it != dataTracker.processedCommands.end() ? it->second : 0;
    }

    // Builds a building type on a tile id and registers it in logistics.
    template <typename T>
    Building* Build(int tilePos, bool chargeCost = true)
    {
        static_assert(std::is_base_of<Building, T>::value);
        T preview{0};
        Vec2i anchor = tilemap->GetCoordsFromId(tilePos);
        if (!tilemap->CanPlaceBuilding(preview.buildingType, anchor, preview.GetFootprint(), this))
            return nullptr;

        const auto& definition = GetBuildingDefinition(preview.buildingType);
        const auto effectiveCosts = GetEffectiveBuildCosts(definition);
        if (chargeCost && !TryPayBuildCost(effectiveCosts))
        {
            ReportBuildCostFailure(definition.name);
            return nullptr;
        }

        build.Build<T>(tilePos);
        auto bld = tilemap->GetBuilding(tilePos);
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
                for (int occupiedTileId : tilemap->GetBuildingTileIds(bld))
                    roadNetwork->UpdateNavMap(occupiedTileId, bld);
                tilemap->AutoConnectBuilding(bld);
            }
        }
        return bld;
    }

    // Builds a building type on map coordinates.
    template <typename T>
    Building* Build(Vec2i pos, bool chargeCost = true)
    {
        return Build<T>(tilemap->GetIdFromCoords(pos), chargeCost);
    }

    bool HasBuildResources(const std::vector<ResourceAmountDefinition>& costs) const;
    void ReportBuildCostFailure(const std::string& buildingName) const;
    std::vector<std::string> GetBuildRequirementFailures(const BuildingDefinition& definition, bool ignoreDebugFreeBuild = true) const;

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
    // Raw food supply ratio (0-1) averaged across villages — see Player.cpp
    // for why this must not be confused with GetFoodProductivity().
    double GetFoodSupplyRatio() const;
    // How well the defence towers are stocked: ammo held / ammo capacity
    // averaged over every completed tower (1.0 when the player has none, so
    // "no towers" never reads as an emergency). Tower ammo lives in each
    // tower's own local buffer, deliberately outside the warehouse network
    // StockpileIndex reports — this is the one number that summarises it.
    double GetAmmunitionSupplyRatio() const;

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

    bool CanUnlockFocus(const std::string& id) const
    {
        const auto* definition = FindFocusDefinition(id);
        return definition != nullptr && focuses.CanStartFocus(id);
    }

    bool UnlockFocus(const std::string& id);

    bool StartFocus(const std::string& id);

    bool UnlockTechnology(const std::string& id);
    bool StartTechnologyResearch(const std::string& id, Building* university);

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
                                              std::optional<Vec2i> position = std::nullopt) const
    {
        BalanceModifierContext context{stat, buildingType, resourceType};
        context.position = position;
        if (position.has_value() && tilemap->IsInside(position.value()))
            context.positionId = tilemap->GetIdFromCoords(position.value());
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
        if (building->positionId >= 0)
            context.position = tilemap->GetCoordsFromId(building->positionId);
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
    // Returns resources to owned storage (cancelling an in-progress build).
    // Overflow beyond available storage capacity is dropped.
    void RefundBuildCost(const std::vector<ResourceAmountDefinition>& costs);

    // Starts resource transport through this player's road network.
    bool BeginTransport(Building* src, Building* dest, Resource* res)
    {
        return roadNetwork->BeginTransport(src, dest, res);
    }

    int id;
    std::string name{"Player"};
    Color color{66, 154, 255, 255};
    PlayerControllerType controllerType{PlayerControllerType::LocalHuman};
    bool debugMode{false};
    // Set true when this player's Headquarters is captured — the player is
    // eliminated (all remaining assets pass to the conqueror). Deterministic:
    // written only inside the simulation tick. The scene reads it for win/lose UI.
    bool defeated{false};

    std::unique_ptr<RoadNetwork> roadNetwork;
    TileMap* tilemap{nullptr};
    BFactory build;

    // ETAP 10: Strategic building registries — indexed direct access without map scans.
    // Updated event-driven (onBuildingCreated/Destroyed). Deterministic vector order.
    std::vector<Building*> storages;          // StorageComponent — resource warehouses (includes HQ)
    std::vector<Building*> villages;          // PopulationComponent — civilian settlements
    uint32_t registryGeneration{0};           // bumped on any registry change — used for cache invalidation

    StrategicResourcePool strategicResources;
    TechnologyState technologies;
    FocusState focuses;
    BalanceModifierSet balanceModifiers;
    PlayerDataTracker dataTracker;
    ConstructionQueue construction;
    PlayerEconomyTelemetry economyTelemetry;
    // TD(etap-6.3): productivity ramps on buildings captured from eliminated
    // players. Empty for a player who has never conquered anything.
    ConqueredEconomy conqueredEconomy;

    // TD(etap-3): recruited-but-not-yet-deployed BattleUnit pool (replaces the
    // old war system's ArmyRegistry).
    UnitRoster roster;
    // Per-player instance-id counter, prefixed with this player's id (same
    // pattern as build.buildingId) so ids stay unique across the whole world
    // without needing a GameWorld back-reference from deep inside a component.
    int nextUnitInstanceId{1};
};

// Local human-controlled player type.
class HumanPlayer : public Player
{
public:
    HumanPlayer() = default;
};

#endif
