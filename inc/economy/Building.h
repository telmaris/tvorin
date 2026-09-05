#ifndef BUILDING_H
#define BUILDING_H

#include <algorithm>
#include <array>
#include <memory>

#include "core/Types.h"
#include "world/WorldIds.h"
#include "data/Resource.h"
#include "simulation/Transport.h"
#include "core/Stat.h"
#include "economy/BuildingComponents.h"

class Player;
class Tile;
struct ProvinceEconomy;

enum class BuildingType : int
{
    Building = 0,
    ProductionBuilding = 1,
    StorageBuilding = 2,
    Road = 4,
    Headquarters = 5,

    Woodcutter = 11,
    LumberMill = 12,
    Mine = 13,
    Foundry = 14,
    Village = 15,
    HuntersHut = 16,
    Windmill = 17,
    Bakery = 18,
    Inn = 19,
    Paperworks = 20,

    Barracks = 24,

    Smith = 31,
    University = 32,
    Well = 33,
    WheatFarm = 34,
    Mint = 35,
    Glassworks = 36,
    Powderworks = 37,

    AnimalFarm = 41,
    Butcher = 42,
    Tannery = 43,
    Tailor = 44,
    Armorer = 45,
    HorseStable = 46,
    Kiln = 47,
    HouseholdWorkshop = 48,
    Soapworks = 49,
    Inkworks = 50,
    Scriptorium = 51,
    Copperworks = 52,
    UrbanWorkshop = 53,
    HempFarm = 54,
    Ropery = 55,
    Weaver = 56,
    Bowyer = 57,
    ReservedBuilding59 = 59,
    SpearWorkshop = 60,
    SiegeWorkshop = 61,
    // Append-only military types for the off-screen province defense system.
    GuardTower = 62,
    Fortress = 63
};

// True for every building type the resource-road network treats as a
// traversable road node.
inline bool IsRoadLike(BuildingType type)
{
    return type == BuildingType::Road;
}

// What deposit (if any) sits on a tile — read by Mine/Woodcutter terrain_production.
enum class TileType : int
{
    GRASS = 0,
    WOOD = 1,
    COAL = 2,
    IRON_ORE = 3,
    STONE = 4,
    COPPER_ORE = 5,
    TIN_ORE = 6,
    SILVER_ORE = 7,
    GOLD_ORE = 8,
    SAND = 9,
    SULFUR = 10,
    SALTPETER = 11,
    CLAY = 12
};

// Coarse terrain region driving resource placement (and, later, ground visuals).
// See docs/resource_world_design.md.
enum class BiomeType : int
{
    PLAINS = 0,
    FOREST = 1,
    HILLS = 2,
    MOUNTAINS = 3,
    DESERT = 4,
    WETLAND = 5
};

enum class BuildCostRecordState : std::uint8_t
{
    Free = 0,
    PaidRecorded = 1,
    LegacyUnknown = 2
};

struct ResourceAmountDefinition
{
    ResourceType type{ResourceType::Null};
    int amount{0};
};

struct ResourceBufferView
{
    ResourceType type{ResourceType::Null};
    int amount{0};
    int capacity{0};
    int recipeAmount{0};
};

struct BuildingConnectionView
{
    ResourceType type{ResourceType::Null};
    Building* building{nullptr};
    bool alternative{false};
};

// Base gameplay object placed on one or more map tiles.
// A building is little more than an id, a lifecycle, and a set of capability
// components (IBuildingComponent). All resource-flow and capability queries are
// routed to the relevant component via GetComponent<T>(); concrete building
// classes only assemble the components they need in their constructor.
class Building
{
public:
    Building();
    explicit Building(int i);
    Building(const Building&) = delete;
    Building& operator=(const Building&) = delete;
    Building(Building&&) = delete;
    Building& operator=(Building&&) = delete;
    virtual ~Building();

    // Default tick: advances construction/lifetime, runs every component's
    // Update in registration order, then advances in-flight transportables.
    // Concrete buildings only need to register the right components.
    virtual void Update(double dt);
    // Default: seed the producer's terrain type when present. Buildings with no
    // production component ignore this; terrain-specialised producers override it.
    virtual void InitBuilding(TileType t);
    virtual bool CanBeManuallyDestroyed() const { return true; }

    // --- Resource-flow facade: routed to the building's resource components ---
    // No-ops when the building owns no component handling that resource.
    void AddResource(Resource* res);
    Resource GetResource(ResourceType type);
    void ReturnOutgoingResource(Resource* res);
    void CancelRequestedResource(ResourceType type);
    void SetSupplier(ResourceType type, Building* supplier);
    void SetReceiver(ResourceType type, Building* receiver);
    void SetAlternativeReceiver(ResourceType type, Building* receiver);
    void RemoveSupplier(ResourceType type, Building* supplier);
    void RemoveReceiver(ResourceType type, Building* receiver);
    int  HandleTransport(ResourceType type, int amount, Building* receiver);
    bool CanAcceptResource(ResourceType type) const;
    bool CanReceiveResource(ResourceType type) const;
    ProvinceEconomy* GetProvinceEconomy() const { return provinceEconomy; }

    // --- Capability queries: routed to components, empty/zero when absent ---
    std::vector<ResourceBufferView> GetInputBufferViews() const;
    std::vector<ResourceBufferView> GetOutputBufferViews() const;
    std::vector<BuildingConnectionView> GetSupplierViews() const;
    std::vector<BuildingConnectionView> GetReceiverViews() const;
    bool HasSupplier(ResourceType type) const;
    bool HasReceiver(ResourceType type) const;
    // True when `supplier` is already the explicit supplier for `type`, or
    // when the slot has not been explicitly assigned yet. This protects an
    // explicit reassignment from an unrelated automatic connection.
    bool AcceptsSupplierFor(ResourceType type, const Building* supplier) const;
    bool IsStorageLike() const;
    float GetProductionProgress() const;
    float GetWorkerRatio() const;
    int GetAssignedWorkers() const;
    int GetWorkerCapacity() const;
    bool IsProductionStalled() const;
    bool CanBlockProduction() const;
    // One canonical enumeration of every buffer owned by this building.
    // Pointers are non-owning and valid only while the building is alive.
    std::vector<ResourceBuffer*> GetResourceBuffers();
    std::vector<const ResourceBuffer*> GetResourceBuffers() const;

    bool IsProductionBlocked() const { return productionBlocked; }
    void SetProductionBlocked(bool blocked) { productionBlocked = blocked; }
    Vec2i GetFootprint() const { return footprint; }
    int GetTextureId() const { return textureId; }
    int GetTotalProduced() const { return totalProduced; }
    float GetEfficiency() const;
    double GetLifetime() const { return lifetime; }
    double GetActiveTime() const { return activeTime; }
    bool IsUnderConstruction() const { return constructionRemaining > 0.0; }
    float GetConstructionProgress() const;
    double GetModifiedTransportTime() const;
    double GetModifiedDispatchDelay(ResourceType resourceType = ResourceType::Null) const;

    // Component registry — subclass constructors call RegisterComponent for each owned component.
    template<typename T>
    bool HasComponent() const
    {
        constexpr BuildingCapability capability = GetBuildingComponentCapability<T>();
        if constexpr (capability != BuildingCapability::Count)
            return HasCapabilityFlag(capability);
        else
            return false;
    }

    template<typename T>
    T* GetComponent()
    {
        constexpr BuildingCapability capability = GetBuildingComponentCapability<T>();
        if constexpr (capability != BuildingCapability::Count)
        {
            auto index = static_cast<std::size_t>(capability);
            return HasCapabilityFlag(capability) ? static_cast<T*>(m_componentSlots[index]) : nullptr;
        }
        else
        {
            return nullptr;
        }
    }

    template<typename T>
    const T* GetComponent() const
    {
        constexpr BuildingCapability capability = GetBuildingComponentCapability<T>();
        if constexpr (capability != BuildingCapability::Count)
        {
            auto index = static_cast<std::size_t>(capability);
            return HasCapabilityFlag(capability) ? static_cast<const T*>(m_componentSlots[index]) : nullptr;
        }
        else
        {
            return nullptr;
        }
    }

    void ReceptTransport(Transportable*);
    void UpdateTransportables(double);
    bool UpdateConstruction(double dt);
    double BeginOperationalUpdate(double dt);

    Player* owner{nullptr};
    // Runtime local context. It is rebuilt by TileMap placement/load and is
    // never serialized; stable ownership is carried by PlayerId/ProvinceId
    // at the campaign boundary.
    ProvinceEconomy* provinceEconomy{nullptr};
    Tile* placement{nullptr};
    int id{0};
    PlayerId ownerId{InvalidPlayerId};
    ProvinceId provinceId{InvalidProvinceId};
    int positionId{-1};
    std::string name{"Building - Generic"};
    BuildingType buildingType = BuildingType::Building;
    std::string tag;
    std::vector<Transportable*> transportables;
    Stat<double> transportTime{BalanceStat::TransportTime, 0.0};
    Stat<double> dispatchDelay{BalanceStat::TransportDispatchDelay, 0.3};
    Vec2i footprint{1, 1};
    int textureId{0};
    bool productionBlocked{false};
    // The exact construction payment is retained so demolition cannot
    // retroactively apply today's balance modifiers to yesterday's build.
    bool buildCostWasPaid{false};
    BuildCostRecordState buildCostRecordState{BuildCostRecordState::Free};
    std::vector<ResourceAmountDefinition> paidBuildCosts;
    Stat<double> buildTime{BalanceStat::BuildTime, 0.0};
    double constructionRemaining{0.0};
    // False while the building waits in the build queue with no free builder
    // assigned. Set every tick by the owner's ConstructionQueue::Refresh.
    bool constructionActive{true};
    double lifetime{0.0};
    double activeTime{0.0};
    int totalProduced{0};
    // Every placed building has an explicit raid-safety policy. Buildings
    // that are not valid raid targets are filtered by the resolver (HQ/roads/
    // construction), while specialized data can opt out further.
    SafetyComponent safety;

protected:
    void RegisterComponent(IBuildingComponent* component)
    {
        if (component == nullptr)
            return;

        m_components.push_back(component);
        BuildingCapability capability = component->GetCapability();
        if (IsValidCapability(capability))
        {
            auto index = static_cast<std::size_t>(capability);
            m_capabilities.set(index);
            m_componentSlots[index] = component;
        }
        component->OnAttached(*this);
    }

private:
    static bool IsValidCapability(BuildingCapability capability)
    {
        return static_cast<std::size_t>(capability) < BuildingCapabilityCount;
    }

    bool HasCapabilityFlag(BuildingCapability capability) const
    {
        return IsValidCapability(capability) &&
               m_capabilities.test(static_cast<std::size_t>(capability));
    }

    std::vector<IBuildingComponent*> m_components; // non-owning; owned by subclass members
    std::array<IBuildingComponent*, BuildingCapabilityCount> m_componentSlots{};
    BuildingCapabilitySet m_capabilities;
};

// Road tile that carries resource transportables between buildings.
class Road : public Building
{
public:
    Road() = default;
    Road(int i);

    RoadComponent road;
    UpgradeComponent upgrade;
    int GetModifiedMaxCapacity() const;
    double GetModifiedSpeedModifier() const;
};

// Building that stores resources and serves as a logistics hub.
class StorageBuilding : public Building
{
public:
    StorageBuilding() = default;
    StorageBuilding(int);
    virtual ~StorageBuilding() = default;

    // --- Component member ---
    StorageComponent storage;
};

// Player's starting building: a storage hub. It is never manually destroyed
// by its own owner.
class Headquarters : public Building
{
public:
    Headquarters() = default;
    Headquarters(int);

    bool CanBeManuallyDestroyed() const override { return false; }

    // --- Component members ---
    StorageComponent storage;
};

// Settlement that generates manpower and consumes food upkeep over time.
class Village : public Building
{
public:
    Village() = default;
    Village(int);

    double GetFoodSupplyRatio() const { return population.GetFoodSupplyRatio(); }
    double GetManpowerProductivity() const { return population.GetManpowerProductivity(); }
    double GetWorkerProductivity() const { return population.GetWorkerProductivity(); }
    int RequestFoodSupply() { return population.RequestFoodSupply(*this); }

    // --- Component member ---
    PopulationComponent population;
    UpgradeComponent upgrade;
};

// Recruitment factory. Holds a private local buffer for delivered unit
// costs, a LogisticsComponent that actively requests those costs over the
// road network and a RecruitmentComponent queue that spends those resources plus
// player manpower to produce BattleUnit instances into the owner's roster.
class Barracks : public Building
{
public:
    Barracks() = default;
    Barracks(int);

    // --- Component members ---
    LocalResourceBufferComponent storage;
    LogisticsComponent logistics;
    RecruitmentComponent recruitment;
};

// Off-screen defensive building assembled exclusively from the composition
// components below. It intentionally has no tower-defense update loop.
class DefenseBuilding : public Building
{
public:
    DefenseBuilding() = default;
    DefenseBuilding(int, BuildingType);

    LocalResourceBufferComponent storage;
    LogisticsComponent logistics;
    DefenseCoverageComponent coverage;
    GarrisonComponent garrison;
    GarrisonUpkeepComponent upkeep;
};

class GuardTower : public DefenseBuilding
{
public:
    GuardTower() = default;
    explicit GuardTower(int id) : DefenseBuilding(id, BuildingType::GuardTower) {}
};

class Fortress : public DefenseBuilding
{
public:
    Fortress() = default;
    explicit Fortress(int id) : DefenseBuilding(id, BuildingType::Fortress) {}
};

#endif
