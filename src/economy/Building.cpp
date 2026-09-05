#include "economy/Building.h"
#include "economy/BuildingConfig.h"
#include "economy/ProductionBuildings.h"
#include "economy/Player.h"
#include "core/Log.h"
#include "simulation/MapGenerator.h"
#include "simulation/RoadNetwork.h"
#include "world/ProvinceSimulation.h"
#include "warfare/GarrisonService.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    int CountIncomingResources(Building* target, ResourceType type)
    {
        if (target == nullptr || target->provinceEconomy == nullptr)
            return 0;

        // OPTIMIZATION: Iterate only tracked buildings (much smaller set than full tilemap),
        // then check if they have transportables. Avoids 1M tile scans per call.
        int incoming = 0;
        for (Building* carrier : target->provinceEconomy->dataTracker.buildings)
        {
            if (carrier == nullptr || carrier->transportables.empty())
                continue;

            for (auto* t : carrier->transportables)
            {
                auto* res = dynamic_cast<Resource*>(t);
                if (res != nullptr && res->targetBuilding == target && res->type == type)
                    incoming++;
            }
        }
        return incoming;
    }

    int GetReceiveCapacity(Building* target, ResourceType type)
    {
        if (target == nullptr || !target->CanReceiveResource(type))
            return 0;

        auto findCap = [type](const std::vector<ResourceBufferView>& views) -> int
        {
            for (const auto& v : views)
                if (v.type == type) return std::max(0, v.capacity - v.amount);
            return -1;
        };

        int free = findCap(target->GetInputBufferViews());
        if (free < 0) free = findCap(target->GetOutputBufferViews());
        if (free < 0) free = target->CanReceiveResource(type) ? 1 : 0;

        return std::max(0, free - CountIncomingResources(target, type));
    }
} // namespace

// ─── Building (base) ─────────────────────────────────────────────────────────

Building::Building()
{
    RegisterComponent(&safety);
}

Building::Building(int i) : id(i)
{
    RegisterComponent(&safety);
}

Building::~Building()
{
    // Resources in flight are raw transport pointers for compatibility with
    // the road network. Release only gameplay-owned allocations; stack-backed
    // resources used by editor/tests are deliberately left untouched.
    for (Transportable* transportable : transportables)
    {
        if (transportable == nullptr)
            continue;
        transportable->ReleaseShipment();
        if (auto* resource = dynamic_cast<Resource*>(transportable))
            Resource::DestroyOwned(resource);
    }
    transportables.clear();
}

float Building::GetEfficiency() const
{
    if (lifetime <= 0.0) return 0.0f;
    return static_cast<float>(activeTime / lifetime);
}

float Building::GetConstructionProgress() const
{
    double base = buildTime.GetBase();
    if (base <= 0.0) return 1.0f;
    return static_cast<float>(std::clamp((base - constructionRemaining) / base, 0.0, 1.0));
}

void Building::Update(double dt)
{
    double opDt = BeginOperationalUpdate(dt);
    if (opDt <= 0.0) return;

    for (auto* component : m_components)
        component->Update(*this, opDt);

    UpdateTransportables(opDt);
}

void Building::InitBuilding(TileType t)
{
    if (auto* production = GetComponent<ProductionComponent>())
        production->terrainType = t;
}

// ─── Building resource-flow facade ────────────────────────────────────────────
// Every building routes resource flow and capability queries to whichever
// component owns that behaviour. Buildings without a matching component degrade
// to safe no-ops, so callers can treat any Building* uniformly.

void Building::AddResource(Resource* res)
{
    if (res == nullptr) return;

    if (auto* prod = GetComponent<ProductionComponent>())
    {
        auto it = prod->inputBuffers.find(res->type);
        if (it == prod->inputBuffers.end() ||
            static_cast<int>(it->second.buffer.size()) >= it->second.bufferSize)
        {
            if (res->sourceBuilding != nullptr)
                res->sourceBuilding->ReturnOutgoingResource(res);
            CancelRequestedResource(res->type);
            return;
        }

        TVORIN_LOG_TRACE(tag, "resource added!");
        it->second.AddResource(res);
        if (auto* log = GetComponent<LogisticsComponent>())
        {
            auto pending = log->pendingRequests.find(res->type);
            if (pending != log->pendingRequests.end() && pending->second > 0)
                pending->second--;
        }
        return;
    }

    if (auto* local = GetComponent<LocalResourceBufferComponent>())
    {
        local->AddResource(res, *this);
        return;
    }

    if (auto* storage = GetComponent<StorageComponent>())
    {
        storage->AddResource(res, *this);
        return;
    }

    if (auto* pop = GetComponent<PopulationComponent>())
    {
        ResourceBuffer* buffer = pop->GetSupplyBuffer(res->type);
        if (buffer == nullptr || !CanReceiveResource(res->type))
        {
            if (res->sourceBuilding != nullptr)
                res->sourceBuilding->ReturnOutgoingResource(res);
            return;
        }
        buffer->AddResource(res);
        if (res->type == ResourceType::FOOD_PROVISIONS)
            pop->hasFood = true;
        return;
    }

    if (res->sourceBuilding != nullptr)
        res->sourceBuilding->ReturnOutgoingResource(res);
}

Resource Building::GetResource(ResourceType type)
{
    if (auto* prod = GetComponent<ProductionComponent>())
    {
        auto [avail, res] = prod->inputBuffers[type].GetResource();
        if (!avail)
            return Resource{};
        Resource value = *res;
        Resource::DestroyOwned(res);
        return value;
    }
    if (auto* local = GetComponent<LocalResourceBufferComponent>())
        return local->GetResource(type);
    if (auto* storage = GetComponent<StorageComponent>())
        return storage->GetResource(type);
    if (auto* pop = GetComponent<PopulationComponent>())
    {
        ResourceBuffer* buffer = pop->GetSupplyBuffer(type);
        if (buffer == nullptr) return Resource{};
        auto [avail, res] = buffer->GetResource();
        if (!avail)
            return Resource{};
        Resource value = *res;
        Resource::DestroyOwned(res);
        return value;
    }
    return Resource{};
}

void Building::ReturnOutgoingResource(Resource* res)
{
    if (res == nullptr) return;
    if (auto* prod = GetComponent<ProductionComponent>())
    {
        prod->outputBuffers[res->type].AddResource(res);
        return;
    }
    if (auto* local = GetComponent<LocalResourceBufferComponent>())
    {
        local->ReturnOutgoingResource(res);
        return;
    }
    if (auto* storage = GetComponent<StorageComponent>())
    {
        storage->ReturnOutgoingResource(res);
        return;
    }
    if (auto* pop = GetComponent<PopulationComponent>())
        if (ResourceBuffer* buffer = pop->GetSupplyBuffer(res->type))
            buffer->AddResource(res);
}

void Building::CancelRequestedResource(ResourceType type)
{
    auto* log = GetComponent<LogisticsComponent>();
    if (log == nullptr) return;
    auto it = log->pendingRequests.find(type);
    if (it != log->pendingRequests.end() && it->second > 0)
        it->second--;
}

void Building::SetSupplier(ResourceType type, Building* supplier)
{
    if (auto* log = GetComponent<LogisticsComponent>())
        log->SetSupplier(type, supplier, *this);
}

void Building::SetReceiver(ResourceType type, Building* receiver)
{
    auto* log = GetComponent<LogisticsComponent>();
    auto* prod = GetComponent<ProductionComponent>();
    if (log != nullptr && prod != nullptr)
    {
        log->SetReceiver(type, receiver, *this, *prod);
        return;
    }
    // Storage hubs do not track receivers; they register themselves as the
    // receiver's supplier instead.
    if (IsStorageLike() && receiver != nullptr)
        receiver->SetSupplier(type, this);
}

void Building::SetAlternativeReceiver(ResourceType type, Building* receiver)
{
    if (auto* log = GetComponent<LogisticsComponent>())
        log->SetAltReceiver(type, receiver, *this);
}

void Building::RemoveSupplier(ResourceType type, Building* supplier)
{
    if (auto* log = GetComponent<LogisticsComponent>())
        log->RemoveSupplier(type, supplier);
}

void Building::RemoveReceiver(ResourceType type, Building* receiver)
{
    auto* log = GetComponent<LogisticsComponent>();
    auto* prod = GetComponent<ProductionComponent>();
    if (log != nullptr && prod != nullptr)
        log->RemoveReceiver(type, receiver, *this, *prod);
}

int Building::HandleTransport(ResourceType type, int amount, Building* receiver)
{
    if (GetComponent<ProductionComponent>() != nullptr)
    {
        auto* log = GetComponent<LogisticsComponent>();
        auto* prod = GetComponent<ProductionComponent>();
        return log != nullptr ? log->HandleTransportFrom(type, amount, receiver, *this, *prod) : 0;
    }
    if (auto* storage = GetComponent<StorageComponent>())
        return storage->HandleTransport(type, amount, receiver, *this);
    return 0;
}

bool Building::CanAcceptResource(ResourceType type) const
{
    if (auto* prod = GetComponent<ProductionComponent>())
        return prod->inputBuffers.contains(type);
    if (auto* local = GetComponent<LocalResourceBufferComponent>())
        return local->CanAccept(type);
    if (auto* storage = GetComponent<StorageComponent>())
        return storage->CanAccept(type);
    if (auto* pop = GetComponent<PopulationComponent>())
        return pop->RequiresSupply(type);
    return false;
}

bool Building::CanReceiveResource(ResourceType type) const
{
    if (auto* prod = GetComponent<ProductionComponent>())
    {
        auto it = prod->inputBuffers.find(type);
        return it != prod->inputBuffers.end() &&
               static_cast<int>(it->second.buffer.size()) < it->second.bufferSize;
    }
    if (auto* local = GetComponent<LocalResourceBufferComponent>())
        return local->CanReceive(type);
    if (auto* storage = GetComponent<StorageComponent>())
        return storage->CanReceive(type);
    if (auto* pop = GetComponent<PopulationComponent>())
    {
        const ResourceBuffer* buffer = pop->GetSupplyBuffer(type);
        return buffer != nullptr && pop->RequiresSupply(type) &&
               static_cast<int>(buffer->buffer.size()) < buffer->bufferSize;
    }
    return false;
}

std::vector<ResourceBufferView> Building::GetInputBufferViews() const
{
    if (auto* prod = GetComponent<ProductionComponent>())
        return prod->GetInputBufferViews(prod->ingredients);
    if (auto* pop = GetComponent<PopulationComponent>())
    {
        std::vector<ResourceBufferView> views;
        for (ResourceType type : {ResourceType::FOOD_PROVISIONS,
                                  ResourceType::HOUSEHOLD_GOODS,
                                  ResourceType::URBAN_GOODS})
        {
            const ResourceBuffer* buffer = pop->GetSupplyBuffer(type);
            if (buffer != nullptr && pop->RequiresSupply(type))
                views.push_back({type, static_cast<int>(buffer->buffer.size()),
                                 buffer->bufferSize, pop->GetSupplyUpkeep(type)});
        }
        return views;
    }
    // Barracks unit-cost buffers are private local entries that
    // this building needs delivered TO it, unlike a plain warehouse/HQ where
    // StorageComponent is only ever offered FROM (GetOutputBufferViews) —
    // without exposing them as inputs too, AutoConnectBuilding has no input
    // view to wire a supplier from, so ammo/unit costs never auto-connect.
    if (auto* local = GetComponent<LocalResourceBufferComponent>())
        return local->GetBufferViews();
    return {};
}

std::vector<ResourceBufferView> Building::GetOutputBufferViews() const
{
    if (auto* prod = GetComponent<ProductionComponent>())
        return prod->GetOutputBufferViews(*this);
    if (auto* storage = GetComponent<StorageComponent>())
        return storage->GetBufferViews();
    return {};
}

std::vector<BuildingConnectionView> Building::GetSupplierViews() const
{
    auto* log = GetComponent<LogisticsComponent>();
    auto* prod = GetComponent<ProductionComponent>();
    return (log != nullptr && prod != nullptr) ? log->GetSupplierViews(*prod)
                                               : std::vector<BuildingConnectionView>{};
}

std::vector<BuildingConnectionView> Building::GetReceiverViews() const
{
    auto* log = GetComponent<LogisticsComponent>();
    auto* prod = GetComponent<ProductionComponent>();
    return (log != nullptr && prod != nullptr) ? log->GetReceiverViews(*prod)
                                               : std::vector<BuildingConnectionView>{};
}

bool Building::HasSupplier(ResourceType type) const
{
    auto* log = GetComponent<LogisticsComponent>();
    return log != nullptr && log->HasSupplier(type);
}

bool Building::HasReceiver(ResourceType type) const
{
    auto* log = GetComponent<LogisticsComponent>();
    return log != nullptr && log->HasReceiver(type);
}

bool Building::AcceptsSupplierFor(ResourceType type, const Building* supplier) const
{
    auto* log = GetComponent<LogisticsComponent>();
    return log == nullptr || log->AcceptsSupplierFor(type, supplier);
}

bool Building::IsStorageLike() const
{
    return HasComponent<StorageComponent>();
}

float Building::GetProductionProgress() const
{
    auto* prod = GetComponent<ProductionComponent>();
    return prod != nullptr ? prod->GetProgress(*this) : 0.0f;
}

float Building::GetWorkerRatio() const
{
    auto* workers = GetComponent<WorkerComponent>();
    return workers != nullptr ? workers->GetRatio() : 0.0f;
}

int Building::GetAssignedWorkers() const
{
    auto* workers = GetComponent<WorkerComponent>();
    return workers != nullptr ? workers->assigned : 0;
}

int Building::GetWorkerCapacity() const
{
    auto* workers = GetComponent<WorkerComponent>();
    return workers != nullptr ? workers->GetModifiedCapacity(*this) : 0;
}

bool Building::CanBlockProduction() const
{
    return HasComponent<ProductionComponent>();
}

std::vector<ResourceBuffer*> Building::GetResourceBuffers()
{
    std::vector<ResourceBuffer*> result;
    if (auto* production = GetComponent<ProductionComponent>())
    {
        for (auto& [type, buffer] : production->inputBuffers)
            result.push_back(&buffer);
        for (auto& [type, buffer] : production->outputBuffers)
            result.push_back(&buffer);
    }
    if (auto* storage = GetComponent<StorageComponent>())
        for (auto& [type, buffer] : storage->buffers)
            result.push_back(&buffer);
    if (auto* local = GetComponent<LocalResourceBufferComponent>())
        for (auto& [type, buffer] : local->buffers)
            result.push_back(&buffer);
    if (auto* population = GetComponent<PopulationComponent>())
    {
        result.push_back(&population->foodBuffer);
        result.push_back(&population->householdGoodsBuffer);
        result.push_back(&population->urbanGoodsBuffer);
    }
    return result;
}

std::vector<const ResourceBuffer*> Building::GetResourceBuffers() const
{
    std::vector<const ResourceBuffer*> result;
    if (const auto* production = GetComponent<ProductionComponent>())
    {
        for (const auto& [type, buffer] : production->inputBuffers)
            result.push_back(&buffer);
        for (const auto& [type, buffer] : production->outputBuffers)
            result.push_back(&buffer);
    }
    if (const auto* storage = GetComponent<StorageComponent>())
        for (const auto& [type, buffer] : storage->buffers)
            result.push_back(&buffer);
    if (const auto* local = GetComponent<LocalResourceBufferComponent>())
        for (const auto& [type, buffer] : local->buffers)
            result.push_back(&buffer);
    if (const auto* population = GetComponent<PopulationComponent>())
    {
        result.push_back(&population->foodBuffer);
        result.push_back(&population->householdGoodsBuffer);
        result.push_back(&population->urbanGoodsBuffer);
    }
    return result;
}

bool Building::IsProductionStalled() const
{
    auto* prod = GetComponent<ProductionComponent>();
    if (prod == nullptr || IsUnderConstruction() || productionBlocked)
        return false;

    for (const auto& [res, buf] : prod->outputBuffers)
        if (buf.bufferSize > 0 && static_cast<int>(buf.buffer.size()) >= buf.bufferSize)
            return true;

    auto* log = GetComponent<LogisticsComponent>();
    if (log != nullptr && log->requestBlocked) return true;

    auto* workers = GetComponent<WorkerComponent>();
    int cap = workers != nullptr ? workers->GetModifiedCapacity(*this) : 0;
    if (cap > 0 && (workers == nullptr || workers->assigned <= 0)) return true;

    for (const auto& [res, amount] : prod->ingredients)
    {
        auto it = prod->inputBuffers.find(res);
        int stored = it != prod->inputBuffers.end()
            ? static_cast<int>(it->second.buffer.size()) : 0;
        if (stored < amount && (log == nullptr || !log->HasSupplier(res)))
            return true;
    }
    return false;
}

bool Building::UpdateConstruction(double dt)
{
    if (constructionRemaining <= 0.0) return false;
    constructionRemaining = std::max(0.0, constructionRemaining - dt);
    return constructionRemaining > 0.0;
}

double Building::BeginOperationalUpdate(double dt)
{
    if (dt <= 0.0)          return 0.0;
    if (constructionRemaining <= 0.0) { lifetime += dt; return dt; }
    // Queued but no builder is free to work this one yet — hold construction.
    if (!constructionActive) return 0.0;

    double before = constructionRemaining;
    constructionRemaining = std::max(0.0, constructionRemaining - dt);
    if (constructionRemaining > 0.0) return 0.0;

    double opDt = std::max(0.0, dt - before);
    lifetime += opDt;
    return opDt;
}

double Building::GetModifiedTransportTime() const
{
    double base = transportTime.GetBase();
    if (const auto* road = GetComponent<RoadComponent>())
        base = road->GetModifiedSpeedModifier(*this) > 0.0 ? base / road->GetModifiedSpeedModifier(*this) : base;

    return owner != nullptr
        ? owner->ModifyBalanceForBuilding(transportTime.GetStatId(), base, this)
        : base;
}

double Building::GetModifiedDispatchDelay(ResourceType resourceType) const
{
    const double base = dispatchDelay.GetBase();
    return owner != nullptr
        ? owner->ModifyBalanceForBuilding(dispatchDelay.GetStatId(), base, this, resourceType)
        : base;
}

void Building::UpdateTransportables(double dt)
{
    // Dispatch from a source is a serial loading queue. Without this guard,
    // resources created during the same simulation tick would all complete
    // their delay together and enter the road as one overlapping clump.
    bool advancedDelayedDispatch = false;
    for (auto it = transportables.begin(); it != transportables.end();)
    {
        Transportable* transportable = *it;
        const bool isDelayedSourceDispatch =
            transportable->sourceBuilding == this &&
            transportable->currentPathStep == 0 &&
            transportable->transportTime > 0.0 &&
            transportable->elapsedTime < transportable->transportTime;
        if (isDelayedSourceDispatch && advancedDelayedDispatch)
        {
            ++it;
            continue;
        }
        if (isDelayedSourceDispatch)
            advancedDelayedDispatch = true;

        const TransportUpdateResult result = transportable->Update(dt);
        if (result != TransportUpdateResult::Waiting)
        {
            // A hand-off only removes the pointer from this carrier; the next
            // road tile already owns it and the RoadNetwork registry must keep
            // tracking the same shipment until delivery or cancellation.
            if (result == TransportUpdateResult::Finished)
                transportable->ReleaseShipment();
            auto* res = dynamic_cast<Resource*>(transportable);
            std::string resName = res != nullptr ? rt2s(res->type) : "Transportable";
            TVORIN_LOG_TRACE(tag, "resource ", resName, " deleted from transportables; pos: ",
                             transportable->map->GetCoordsFromId(positionId));
            it = transportables.erase(it);
            continue;
        }
        ++it;
    }
}

void Building::ReceptTransport(Transportable* trans)
{
    trans->elapsedTime   = 0.0;
    trans->transportTime = GetModifiedTransportTime();

    if (trans->sourceBuilding != this)
        trans->currentPathStep++;

    if (trans->targetBuilding == this)
    {
        auto* ptr = dynamic_cast<Resource*>(trans);
        if (ptr != nullptr)
        {
            TVORIN_LOG_TRACE(tag, "Transport of ", rt2s(ptr->type), " finished, adding resource; ID:",
                             positionId, " pos: ", trans->map->GetCoordsFromId(positionId));
            AddResource(ptr);
            trans->ReleaseShipment();
        }
    }
    else
    {
        transportables.push_back(trans);
        auto* res = dynamic_cast<Resource*>(trans);
        std::string resName = res != nullptr ? rt2s(res->type) : "Transportable";
        TVORIN_LOG_TRACE(tag, resName, " pushed into transportables; ID: ", positionId, " pos: ",
                         trans->map->GetCoordsFromId(positionId));
    }
}

// ─── Road ────────────────────────────────────────────────────────────────────

Road::Road(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::Road);
    ApplyBuildingDefinition(*this, def);
    road.upgradeLevel  = def.road.upgradeLevel;
    road.maxCapacity   = def.road.maxCapacity;
    road.speedModifier = def.road.speedModifier;
    RegisterComponent(&road);

    for (const auto& levelDef : def.upgradeLevels)
        upgrade.maxLevel = std::max(upgrade.maxLevel, levelDef.level);
    RegisterComponent(&upgrade);
}

int Road::GetModifiedMaxCapacity() const
{
    return road.GetModifiedMaxCapacity(*this);
}

double Road::GetModifiedSpeedModifier() const
{
    return road.GetModifiedSpeedModifier(*this);
}

// ─── UpgradeComponent ────────────────────────────────────────────────────────

void UpgradeComponent::Update(Building& self, double dt)
{
    if (!isUpgrading || !upgradeActive)
        return;

    upgradeRemaining = std::max(0.0, upgradeRemaining - dt);
    if (upgradeRemaining > 0.0)
        return;

    level++;
    isUpgrading = false;
    if (auto* population = self.GetComponent<PopulationComponent>())
        population->SetSettlementLevel(level);
    if (self.owner != nullptr)
    {
        self.owner->ApplyUpgradeLevelModifiers(self);
        if (IsRoadLike(self.buildingType) && self.provinceEconomy != nullptr &&
            self.provinceEconomy->roadNetwork != nullptr)
            self.provinceEconomy->roadNetwork->InvalidateRoutingCosts();
    }
}

// ─── StorageBuilding ─────────────────────────────────────────────────────────

StorageBuilding::StorageBuilding(int actualId)
{
    id = actualId;
    RegisterComponent(&storage);
    const auto& def = GetBuildingDefinition(BuildingType::StorageBuilding);
    ApplyBuildingDefinition(*this, def);
    ApplyStorageDefinition(*this, def);
}

// ─── Headquarters ────────────────────────────────────────────────────────────

Headquarters::Headquarters(int actualId)
{
    id = actualId;
    RegisterComponent(&storage);
    const auto& def = GetBuildingDefinition(BuildingType::Headquarters);
    ApplyBuildingDefinition(*this, def);
    ApplyStorageDefinition(*this, def);
}

// ─── Village ─────────────────────────────────────────────────────────────────

Village::Village(int actualId)
{
    id = actualId;
    const auto& def = GetBuildingDefinition(BuildingType::Village);
    ApplyBuildingDefinition(*this, def);
    population.manpowerRate        = def.village.manpowerRate;
    population.populationCap       = def.village.populationCap;
    population.upkeepInterval      = def.village.upkeepInterval;
    population.foodPackageUpkeep   = def.village.foodPackageUpkeep;

    for (const auto& levelDef : def.upgradeLevels)
        upgrade.maxLevel = std::max(upgrade.maxLevel, levelDef.level);

    population.levelPopulationCaps.fill(def.village.populationCap);
    population.levelManpowerRates.fill(def.village.manpowerRate);
    population.levelPopulationCaps[0] = 0;
    population.levelManpowerRates[0] = 0.0;
    const int configuredMaxLevel = std::min(
        upgrade.maxLevel,
        static_cast<int>(population.levelPopulationCaps.size()) - 1);
    for (int level = 2; level <= configuredMaxLevel; level++)
    {
        population.levelPopulationCaps[level] = population.levelPopulationCaps[level - 1];
        population.levelManpowerRates[level] = population.levelManpowerRates[level - 1];

        auto levelIt = std::find_if(def.upgradeLevels.begin(), def.upgradeLevels.end(),
            [level](const BuildingUpgradeLevelDefinition& levelDef)
            {
                return levelDef.level == level;
            });
        if (levelIt == def.upgradeLevels.end())
            continue;
        if (levelIt->populationCap.has_value())
            population.levelPopulationCaps[level] = *levelIt->populationCap;
        if (levelIt->manpowerRate.has_value())
            population.levelManpowerRates[level] = *levelIt->manpowerRate;
    }

    RegisterComponent(&population);
    RegisterComponent(&upgrade);
}

// ─── Barracks ────────────────────────────────────────────────────────────────

Barracks::Barracks(int actualId)
{
    id = actualId;
    RegisterComponent(&storage);
    RegisterComponent(&logistics);
    RegisterComponent(&recruitment);
    const auto& def = GetBuildingDefinition(BuildingType::Barracks);
    ApplyBuildingDefinition(*this, def);
    ApplyStorageDefinition(*this, def);
}

// ─── Off-screen defense building ────────────────────────────────────────────

DefenseBuilding::DefenseBuilding(int actualId, BuildingType type)
{
    id = actualId;
    RegisterComponent(&storage);
    RegisterComponent(&logistics);
    RegisterComponent(&coverage);
    RegisterComponent(&garrison);
    RegisterComponent(&upkeep);
    const auto& def = GetBuildingDefinition(type);
    ApplyBuildingDefinition(*this, def);
    ApplyStorageDefinition(*this, def);
    ApplyDefenseDefinition(*this, def);
}

double DefenseCoverageComponent::GetEffectiveRadius(const Building& self) const
{
    return self.owner != nullptr
        ? self.owner->ResolveStat(radius, &self)
        : radius.GetBase();
}

double DefenseCoverageComponent::GetEffectiveProtection(const Building& self) const
{
    return self.owner != nullptr
        ? self.owner->ResolveStat(baseProtection, &self)
        : baseProtection.GetBase();
}

int GarrisonComponent::GetEffectiveCapacity(const Building& self) const
{
    return self.owner != nullptr
        ? self.owner->ResolveStat(capacity, &self, ResourceType::Null, 0)
        : std::max(0, capacity.GetBase());
}

void GarrisonUpkeepComponent::Update(Building& self, double dt)
{
    GarrisonService::UpdateBuilding(self, dt);
}

double GarrisonUpkeepComponent::GetDebtInPackages() const
{
    return packageSize > 0.0
        ? static_cast<double>(debtMicros) /
              (packageSize * static_cast<double>(DebtScale))
        : 0.0;
}

// ─── Concrete ProductionBuilding subclasses ───────────────────────────────────

ConfiguredProductionBuilding::ConfiguredProductionBuilding(int i, BuildingType type)
{
    id = i;
    const auto& def = GetBuildingDefinition(type);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

Woodcutter::Woodcutter(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::Woodcutter);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

HuntersHut::HuntersHut(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::HuntersHut);
    ApplyBuildingDefinition(*this, def);
    production.consumesTerrain = false;
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
}

void HuntersHut::InitBuilding(TileType tile)
{
    production.terrainType = tile;
    if (const auto* tp = FindTerrainProductionDefinition(BuildingType::HuntersHut, tile))
        ApplyProductionDefinition(*this, tp->production);
}

LumberMill::LumberMill(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::LumberMill);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

Mine::Mine(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::Mine);
    ApplyBuildingDefinition(*this, def);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
}

void Mine::InitBuilding(TileType tile)
{
    production.terrainType = tile;
    if (const auto* tp = FindTerrainProductionDefinition(BuildingType::Mine, tile))
        ApplyProductionDefinition(*this, tp->production);
}

Foundry::Foundry(int ajdi)
{
    id = ajdi;
    const auto& def = GetBuildingDefinition(BuildingType::Foundry);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

Well::Well(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::Well);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

WheatFarm::WheatFarm(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::WheatFarm);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

Windmill::Windmill(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::Windmill);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

Bakery::Bakery(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::Bakery);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

Inn::Inn(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::Inn);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

Paperworks::Paperworks(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::Paperworks);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

Smith::Smith(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::Smith);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

Mint::Mint(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::Mint);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

Glassworks::Glassworks(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::Glassworks);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

Powderworks::Powderworks(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::Powderworks);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}

University::University(int i)
{
    id = i;
    const auto& def = GetBuildingDefinition(BuildingType::University);
    RegisterComponent(&production);
    RegisterComponent(&logistics);
    RegisterComponent(&workers);
    RegisterComponent(&recipes);
    RegisterComponent(&research);
    ApplyBuildingDefinition(*this, def);
    ApplyProductionDefinition(*this, def.production);
    ApplyProductionRecipes(*this, def);
}
