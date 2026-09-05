#ifndef BUILDING_COMPONENTS_H
#define BUILDING_COMPONENTS_H

#include "data/Resource.h"
#include "core/Stat.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class Building;

enum class TileType : int;
struct ResourceBufferView;
struct BuildingConnectionView;

struct ProductionRecipeRuntime
{
    std::string name;
    double cycleTime{0.0};
    std::map<ResourceType, int> inputs;
    std::map<ResourceType, int> outputs;
    std::map<ResourceType, int> inputBufferCapacities;
    std::map<ResourceType, int> outputBufferCapacities;
    int workerCapacity{5};
    std::vector<std::string> requiredTechnologies;
    std::vector<std::string> requiredFocuses;
};

enum class BuildingCapability : std::size_t
{
    Production = 0,
    Logistics,
    Workers,
    Recipes,
    Research,
    Storage,
    LocalResourceBuffer,
    Population,
    Road,
    Recruitment,
    Upgrade,
    DefenseCoverage,
    Garrison,
    GarrisonUpkeep,
    Safety,
    Count
};

constexpr std::size_t BuildingCapabilityCount = static_cast<std::size_t>(BuildingCapability::Count);
using BuildingCapabilitySet = std::bitset<BuildingCapabilityCount>;

// Base interface for all building capability components.
// Components own a distinct slice of building state and behaviour.
class IBuildingComponent
{
public:
    virtual ~IBuildingComponent() = default;
    virtual BuildingCapability GetCapability() const { return BuildingCapability::Count; }
    virtual void Update(Building& self, double dt) {}
    virtual void OnAttached(Building& self) {}
};

struct RoadComponent : IBuildingComponent
{
    int upgradeLevel{1};
    Stat<int> maxCapacity{BalanceStat::RoadCapacity, 5};
    Stat<double> speedModifier{BalanceStat::RoadSpeed, 1.0};
    // v1 product priority. Null means that the road keeps the historical
    // first-come admission policy. It is deliberately a resource value, not
    // a speed/capacity modifier: priority may reorder admission only.
    ResourceType priorityResource{ResourceType::Null};
    // Visual/diagnostic telemetry. The EMA describes sustained utilization,
    // while the short hold makes a momentary capacity jam visible long enough
    // to notice. Neither value affects transport simulation.
    double trafficUtilizationEma{0.0};
    double saturationIndicatorRemaining{0.0};

    BuildingCapability GetCapability() const override { return BuildingCapability::Road; }
    void Update(Building& self, double dt) override;
    int GetModifiedMaxCapacity(const Building& self) const;
    double GetModifiedSpeedModifier(const Building& self) const;
    double GetTrafficUtilizationTrend() const;
    bool HasRecentSaturation() const;

    ResourceType GetPriorityResource() const { return priorityResource; }
    void SetPriorityResource(ResourceType resource) { priorityResource = IsValidPriorityResource(resource) ? resource : ResourceType::Null; }
    static bool IsValidPriorityResource(ResourceType resource)
    {
        if (resource == ResourceType::Null)
            return true;
        return ResourcePresentationRank(resource) < static_cast<int>(std::size(resourceTypes)) &&
               resourceTypes[ResourcePresentationRank(resource)] == resource;
    }
};

// --- UpgradeComponent ---
// Generic, player-triggered per-instance upgrade progression — introduced
// for Road but deliberately not road-specific (any future BuildingType can
// register one). `level` starts at 1 (baseline, not itself upgradeable-to)
// and climbs toward `maxLevel`; per-level cost/duration/effect data lives in
// BuildingUpgradeLevelDefinition (BuildingConfig.h), looked up by level from
// the owning building's BuildingDefinition::upgradeLevels — this component
// only holds the live per-instance state, not the data.
struct UpgradeComponent : IBuildingComponent
{
    int level{1};
    int maxLevel{1};
    bool isUpgrading{false};
    // Mirrors Building::constructionActive but kept separate: an upgrading
    // building must NOT report IsUnderConstruction() (that would block
    // transport, SetReceiver, etc. — see GameWorld.Commands.cpp), so it
    // can't share constructionRemaining/constructionActive. ConstructionQueue
    // sets this the same way it sets constructionActive, sharing the same
    // builder-count pool.
    bool upgradeActive{true};
    double upgradeRemaining{0.0};

    BuildingCapability GetCapability() const override { return BuildingCapability::Upgrade; }
    void Update(Building& self, double dt) override;
};

// --- ProductionComponent ---
// Owns the time-based production cycle: input buffers, cycle timer, output generation,
// and terrain-richness consumption.
struct ProductionComponent : IBuildingComponent
{
    TileType terrainType;                          // terrain tile type this producer targets
    std::map<ResourceType, int> ingredients;       // recipe inputs (type -> amount per cycle)
    std::map<ResourceType, int> products;          // recipe outputs (type -> amount per cycle)
    Stat<double> cycleTime{BalanceStat::ProductionCycleTime, 0.0};
    double elapsed{0.0};
    bool started{false};
    std::map<ResourceType, ResourceBuffer> inputBuffers;
    std::map<ResourceType, ResourceBuffer> outputBuffers;
    bool consumesTerrain{true};
    int totalProduced{0};

    ProductionComponent();

    BuildingCapability GetCapability() const override { return BuildingCapability::Production; }
    void Update(Building& self, double dt) override;
    void Produce(Building& self, double dt);
    bool ShouldRequestInputs(const Building& self) const;
    // Fraction of the CURRENT cycle elapsed, against the same (tech/focus-
    // modified) cycle time Produce() actually completes the cycle against —
    // using the unmodified base here would desync the reported percentage
    // from the real completion point (see GetProgress's definition).
    float GetProgress(const Building& self) const;
    double GetModifiedCycleTime(const Building& self) const;
    // Cycle time adjusted for current worker efficiency; infinity when idle.
    double GetEffectiveCycleTime(const Building& self) const;
    int GetModifiedOutputAmount(const Building& self, ResourceType type, int base) const;
    bool HasTerrainRichness(const Building& self) const;
    bool ConsumeTerrainRichness(Building& self);

    std::vector<ResourceBufferView> GetInputBufferViews(const std::map<ResourceType,int>& recipe) const;
    std::vector<ResourceBufferView> GetOutputBufferViews(const Building& self) const;
};

// --- LogisticsComponent ---
// Manages supplier/receiver connections and inbound transport-request bookkeeping.
struct LogisticsComponent : IBuildingComponent
{
    std::map<ResourceType, std::vector<Building*>> suppliers;
    std::map<ResourceType, Building*>              receivers;
    std::map<ResourceType, Building*>              altReceivers;
    std::map<ResourceType, int>                    pendingRequests;
    bool requestBlocked{false};

    BuildingCapability GetCapability() const override { return BuildingCapability::Logistics; }
    bool HasSupplier(ResourceType type) const;
    bool HasReceiver(ResourceType type) const;
    bool AcceptsSupplierFor(ResourceType type, const Building* supplier) const;
    // True when the player narrowed this building's supply for `type` down to
    // direct producers — RequestResource then skips the warehouse-network
    // fallback rather than overriding that decision.
    bool IsRestrictedToDirectSuppliers(ResourceType type) const;

    void SetSupplier(ResourceType type, Building* supplier, Building& self);
    void SetReceiver(ResourceType type, Building* receiver, Building& self,
                     ProductionComponent& prod);
    void SetAltReceiver(ResourceType type, Building* receiver, Building& self);
    void RemoveSupplier(ResourceType type, Building* supplier);
    void RemoveReceiver(ResourceType type, Building* receiver, Building& self,
                        ProductionComponent& prod);

    int RequestResource(ResourceType type, int amount, Building& self);
    void MaintainRequests(Building& self, ProductionComponent& prod);
    void DispatchOutputs(Building& self, ProductionComponent& prod);
    int HandleTransportFrom(ResourceType type, int amount, Building* receiver,
                            Building& self, ProductionComponent& prod);
    // Returns whether this building has a physical road path to any of its
    // owner's storage-like buildings (HQ included). The result is
    // order-independent and ignores construction state.
    // Not free (one road-network BFS per storage until a hit) — callers on a
    // per-tick path should throttle/cache.
    bool IsConnectedToRoadNetwork(Building& self) const;

    std::vector<BuildingConnectionView> GetSupplierViews(const ProductionComponent& prod) const;
    std::vector<BuildingConnectionView> GetReceiverViews(const ProductionComponent& prod) const;
};

// --- WorkerComponent ---
// Tracks assigned-worker count and worker-slot capacity for production buildings.
struct WorkerComponent : IBuildingComponent
{
    Stat<int> capacity{BalanceStat::WorkerCapacity, 5};
    int assigned{0};

    BuildingCapability GetCapability() const override { return BuildingCapability::Workers; }
    float GetRatio() const;
    int GetModifiedCapacity(const Building& self) const;
};

struct RecipeComponent : IBuildingComponent
{
    std::vector<ProductionRecipeRuntime> recipes;
    int activeRecipeIndex{0};

    BuildingCapability GetCapability() const override { return BuildingCapability::Recipes; }
    bool HasSelectableRecipes() const;
    std::string GetActiveRecipeName() const;
    bool IsRecipeAvailable(const Building& self, int index) const;
    void SetRecipes(std::vector<ProductionRecipeRuntime> newRecipes,
                    Building& self,
                    ProductionComponent& production,
                    LogisticsComponent& logistics,
                    WorkerComponent& workers);
    bool SetActiveRecipe(int index,
                         Building& self,
                         ProductionComponent& production,
                         LogisticsComponent& logistics,
                         WorkerComponent& workers);
    bool CycleRecipe(Building& self,
                     ProductionComponent& production,
                     LogisticsComponent& logistics,
                     WorkerComponent& workers);
};

// --- ResearchComponent ---
// Tracks active technology-research progress for the University building.
struct ResearchComponent : IBuildingComponent
{
    std::string technologyId;
    double remaining{0.0};
    double total{0.0};

    BuildingCapability GetCapability() const override { return BuildingCapability::Research; }
    bool Start(const std::string& id, double time);
    // Returns true when research just completed (remaining reached 0).
    bool Tick(double dt);
    double GetProgress() const;
};

// --- StorageComponent ---
// Generic multi-resource storage hub. This capability is reserved for
// Headquarters, StorageBuilding and future buildings whose actual role is
// sharing stock with the whole economy.
//
// Passive by design: it accepts deliveries (AddResource) and serves requests
// (HandleTransport), but has no Update() and never initiates a transfer. The
// ambient "push my buffer to anyone who accepts it" scan this used to run made
// every new StorageBuilding drain the HQ into itself (see
// src/economy/StorageComponent.cpp for the full story). Consumers pull; the
// warehouse network they pull from is resolved by StockpileIndex.
struct StorageComponent : IBuildingComponent
{
    std::map<ResourceType, ResourceBuffer> buffers;

    BuildingCapability GetCapability() const override { return BuildingCapability::Storage; }
    bool CanAccept(ResourceType type) const;
    bool CanReceive(ResourceType type) const;
    void AddResource(Resource* res, Building& self);
    void ReturnOutgoingResource(Resource* res);
    Resource GetResource(ResourceType type);
    int HandleTransport(ResourceType type, int amount, Building* receiver, Building& self);

    std::vector<ResourceBufferView> GetBufferViews() const;
};

// Private delivery buffer used by consumers such as Barracks. It accepts
// resources addressed to its owning building, but it
// is deliberately not a warehouse: it is absent from Player::storages, cannot
// serve outgoing requests and never contributes to StockpileIndex.
struct LocalResourceBufferComponent : IBuildingComponent
{
    std::map<ResourceType, ResourceBuffer> buffers;

    BuildingCapability GetCapability() const override { return BuildingCapability::LocalResourceBuffer; }
    bool CanAccept(ResourceType type) const;
    bool CanReceive(ResourceType type) const;
    void AddResource(Resource* res, Building& self);
    void ReturnOutgoingResource(Resource* res);
    Resource GetResource(ResourceType type);
    std::vector<ResourceBufferView> GetBufferViews() const;
};

// --- PopulationComponent ---
// Manpower generation and food-supply tracking for Village buildings.
struct PopulationComponent : IBuildingComponent
{
    Stat<double> manpowerRate{BalanceStat::ManpowerRate, 5.0};
    Stat<int> populationCap{BalanceStat::PopulationCap, 1000};
    int settlementLevel{1};
    // Index 0 is unused. Village's constructor populates every playable
    // level from buildings.rtsdata, inheriting omitted values from the
    // previous level.
    std::array<int, 4> levelPopulationCaps{};
    std::array<double, 4> levelManpowerRates{};
    // Per-resource upkeep progress. `upkeepTimer` remains the food timer to
    // preserve the pre-v33 save field and tests that tune Village cadence.
    double upkeepTimer{0.0};
    double householdUpkeepTimer{0.0};
    double urbanUpkeepTimer{0.0};
    double upkeepInterval{10.0};
    double foodPackageUpkeep{1.0};
    bool hasFood{true};
    double foodSupplyLevel{1.0};
    double foodSupplyDropPerMissedUpkeep{0.25};
    // One package covers the next upkeep payment; one more is the village's
    // only local reserve, so a fresh village never monopolizes food logistics.
    ResourceBuffer foodBuffer{ResourceType::FOOD_PROVISIONS, 2};
    ResourceBuffer householdGoodsBuffer{ResourceType::HOUSEHOLD_GOODS, 6};
    ResourceBuffer urbanGoodsBuffer{ResourceType::URBAN_GOODS, 3};
    double householdSupplyLevel{1.0};
    double urbanSupplyLevel{1.0};

    BuildingCapability GetCapability() const override { return BuildingCapability::Population; }
    void Update(Building& self, double dt) override;
    double GetFoodSupplyRatio() const;
    double GetManpowerProductivity() const;
    double GetWorkerProductivity() const;
    int RequestFoodSupply(Building& self);
    int GetFoodDemand() const;
    void SetSettlementLevel(int level);
    int GetActiveSettlementLevel() const;
    int GetActivePopulationCap() const;
    bool RequiresSupply(ResourceType type) const;
    int GetSupplyUpkeep(ResourceType type) const;
    double GetEffectiveSupplyUpkeepInterval(const Building& self, ResourceType type) const;
    ResourceBuffer* GetSupplyBuffer(ResourceType type);
    const ResourceBuffer* GetSupplyBuffer(ResourceType type) const;
    int RequestSupply(Building& self, ResourceType type);
};

// --- RecruitmentComponent ---
// One in-progress recruit order: which unit definition, and how much of its
// recruitTime remains. Manpower is deducted up front when the order is
// queued (QueueRecruitment). Resources may not be — resourcesReady is false
// while this entry is still waiting on a RequestResource delivery; `total`/
// `remaining` only start counting down once resourcesReady flips true (see
// RecruitmentComponent::Update).
struct RecruitmentQueueEntry
{
    std::string unitDefId;
    double total{0.0};
    double remaining{0.0};
    bool resourcesReady{true};
};

// Recruitment queue for a unit-producing building (Barracks; future
// Stables/Workshop are only new UnitDefinition::recruitBuilding
// values, no new component). User request (docs/work_plan_2026-07-13.md,
// 2026-07-15 + TODO #1 2026-07-16): an order joins the queue immediately on
// click (as long as manpower allows), tagged "waiting for resources" if its
// cost isn't already sitting in this building's own local resource buffer.
// Update() then works the queue in strict FIFO order: entries flip
// resourcesReady (consuming their cost) as deliveries land — even behind a
// training front entry — while only the FIRST waiting entry requests its
// shortfall from the road network, net of what's already in flight, so the
// building orders exactly one unit's cost at a time and never stockpiles.
// The timed build runs only for the front entry once it's resourcesReady.
// On completion the finished unit is added to the owning player's
// UnitRoster in state InRoster.
struct RecruitmentComponent : IBuildingComponent
{
    std::deque<RecruitmentQueueEntry> queue;

    BuildingCapability GetCapability() const override { return BuildingCapability::Recruitment; }
    void Update(Building& self, double dt) override;
    // Queues one unit order now: validates the unit exists and the owner has
    // enough manpower (deducted immediately — the only hard gate), then
    // pushes a queue entry. If no earlier entry is still waiting and this
    // building's own buffer already covers the resource cost, it's consumed
    // now and the entry starts counting down right away; otherwise the entry
    // waits (see RecruitmentQueueEntry::resourcesReady) and Update() requests
    // the shortfall in-flight-aware, one unit at a time, strict FIFO.
    // Returns false only when manpower alone is insufficient — resource
    // unavailability never blocks queueing.
    bool QueueRecruitment(Building& self, const std::string& unitDefId);
    // Non-mutating read, for GUI/AI feasibility — returns an empty string
    // when the unit's resource cost exists SOMEWHERE in the player's global
    // storage network and manpower is sufficient, otherwise a short
    // human-readable reason. Deliberately global even though QueueRecruitment
    // itself only checks this building's local buffer: this is what lets the
    // GUI button / AI candidate generation offer recruiting before anything
    // has physically arrived — if this also checked only the local buffer
    // (always empty for a fresh order), nothing would ever attempt
    // QueueRecruitment, so the first RequestResource would never fire.
    std::string DiagnoseRecruitmentBlock(const Building& self, const std::string& unitDefId) const;
};

// --- DefenseCoverageComponent ---
// Static defensive footprint data. Effective values are resolved by the owner
// at query time, like the other balance-aware building components. The
// component deliberately does not cache pointers to other buildings: raid and
// coverage services derive overlaps from the live building registry.
struct DefenseCoverageComponent : IBuildingComponent
{
    Stat<double> radius{BalanceStat::ProvinceDefenseCoverage, 0.0};
    Stat<double> baseProtection{BalanceStat::ProvinceDefensePower, 0.0};
    std::string requiredState;

    BuildingCapability GetCapability() const override { return BuildingCapability::DefenseCoverage; }
    double GetEffectiveRadius(const Building& self) const;
    double GetEffectiveProtection(const Building& self) const;
};

// --- GarrisonComponent ---
// Capacity and presentation-only metadata live here. Unit IDs are not
// duplicated in this component; GarrisonService reads canonical roster
// assignments from UnitAssignmentService.
struct GarrisonComponent : IBuildingComponent
{
    Stat<int> capacity{BalanceStat::GarrisonCapacity, 0};
    std::string presentationName;

    BuildingCapability GetCapability() const override { return BuildingCapability::Garrison; }
    int GetEffectiveCapacity(const Building& self) const;
};

enum class GarrisonSupplyStatus : std::uint8_t
{
    Supplied,
    Unsupplied,
    RequestPending
};

// --- GarrisonUpkeepComponent ---
// Fixed-point debt is accumulated by GarrisonService. No fractional Resource
// is ever created: once debt reaches a whole package, the building requests or
// consumes an integer FOOD_PROVISIONS amount through its local logistics path.
struct GarrisonUpkeepComponent : IBuildingComponent
{
    static constexpr std::int64_t DebtScale = 1'000'000;
    ResourceType requiredResource{ResourceType::FOOD_PROVISIONS};
    double timer{0.0};
    // Canonical fixed-point debt. One unit equals 1 / DebtScale of a package.
    std::int64_t debtMicros{0};
    double intervalSeconds{60.0};
    double packageSize{1.0};
    int requestedAmount{0};
    GarrisonSupplyStatus supplyStatus{GarrisonSupplyStatus::Supplied};

    BuildingCapability GetCapability() const override { return BuildingCapability::GarrisonUpkeep; }
    void Update(Building& self, double dt) override;
    double GetDebtInPackages() const;
};

// --- SafetyComponent ---
// Intrinsic raid resilience and explicit target policy. Coverage and garrison
// strength are calculated externally; no pointer to a defensive building is
// stored here, which keeps save/load and deterministic raid resolution simple.
struct SafetyComponent : IBuildingComponent
{
    double intrinsicResilience{0.0};
    bool raidDestructible{true};
    bool raidStockLossTarget{true};

    BuildingCapability GetCapability() const override { return BuildingCapability::Safety; }
};

template<typename T>
constexpr BuildingCapability GetBuildingComponentCapability()
{
    return BuildingCapability::Count;
}

template<> constexpr BuildingCapability GetBuildingComponentCapability<ProductionComponent>() { return BuildingCapability::Production; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<LogisticsComponent>() { return BuildingCapability::Logistics; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<WorkerComponent>() { return BuildingCapability::Workers; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<RecipeComponent>() { return BuildingCapability::Recipes; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<ResearchComponent>() { return BuildingCapability::Research; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<StorageComponent>() { return BuildingCapability::Storage; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<LocalResourceBufferComponent>() { return BuildingCapability::LocalResourceBuffer; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<PopulationComponent>() { return BuildingCapability::Population; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<RoadComponent>() { return BuildingCapability::Road; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<RecruitmentComponent>() { return BuildingCapability::Recruitment; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<UpgradeComponent>() { return BuildingCapability::Upgrade; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<DefenseCoverageComponent>() { return BuildingCapability::DefenseCoverage; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<GarrisonComponent>() { return BuildingCapability::Garrison; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<GarrisonUpkeepComponent>() { return BuildingCapability::GarrisonUpkeep; }
template<> constexpr BuildingCapability GetBuildingComponentCapability<SafetyComponent>() { return BuildingCapability::Safety; }

#endif
