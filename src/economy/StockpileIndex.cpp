#include "economy/StockpileIndex.h"

#include "economy/Building.h"
#include "economy/Player.h"
#include "world/ProvinceSimulation.h"

#include <algorithm>

namespace
{
    // The StorageComponent of a building already known to be a warehouse.
    StorageComponent* WarehouseStorage(Building* building)
    {
        return building != nullptr ? building->GetComponent<StorageComponent>() : nullptr;
    }

    const StorageComponent* WarehouseStorage(const Building* building)
    {
        return building != nullptr ? building->GetComponent<StorageComponent>() : nullptr;
    }
}

bool StockpileIndex::IsWarehouse(const Building* building)
{
    return building != nullptr &&
           (building->buildingType == BuildingType::Headquarters ||
            building->buildingType == BuildingType::StorageBuilding);
}

std::vector<Building*> StockpileIndex::Warehouses(const Player& owner)
{
    const ProvinceEconomy* economy = owner.GetProvinceEconomy();
    return economy != nullptr ? Warehouses(*economy) : std::vector<Building*>{};
}

std::vector<Building*> StockpileIndex::Warehouses(const ProvinceEconomy& economy)
{
    std::vector<Building*> result;
    result.reserve(economy.storages.size());
    for (Building* building : economy.storages)
    {
        if (!IsWarehouse(building) || building->IsUnderConstruction())
            continue;
        result.push_back(building);
    }

    // Player::storages is append-ordered (and conquest appends captured
    // buildings out of id order), so sort explicitly — every caller below
    // drains or fills "first match wins", which is simulation-visible.
    std::sort(result.begin(), result.end(), [](Building* a, Building* b) { return a->id < b->id; });
    // The registry is a plain vector with no uniqueness guarantee. Since this
    // index is what the game answers "how much do I have" with — and what
    // build costs are actually paid from — a double-registered warehouse must
    // never inflate the total or get drained twice.
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

int StockpileIndex::GetTotal(const Player& owner, ResourceType type)
{
    const ProvinceEconomy* economy = owner.GetProvinceEconomy();
    return economy != nullptr ? GetTotal(*economy, type) : 0;
}

int StockpileIndex::GetTotal(const ProvinceEconomy& economy, ResourceType type)
{
    int total = 0;
    for (const Building* building : Warehouses(economy))
    {
        const auto* storage = WarehouseStorage(building);
        if (storage == nullptr)
            continue;

        auto it = storage->buffers.find(type);
        if (it != storage->buffers.end())
            total += static_cast<int>(it->second.buffer.size());
    }
    return total;
}

int StockpileIndex::GetCapacity(const Player& owner, ResourceType type)
{
    const ProvinceEconomy* economy = owner.GetProvinceEconomy();
    return economy != nullptr ? GetCapacity(*economy, type) : 0;
}

int StockpileIndex::GetCapacity(const ProvinceEconomy& economy, ResourceType type)
{
    int capacity = 0;
    for (const Building* building : Warehouses(economy))
    {
        const auto* storage = WarehouseStorage(building);
        if (storage == nullptr)
            continue;

        auto it = storage->buffers.find(type);
        if (it != storage->buffers.end())
            capacity += it->second.bufferSize;
    }
    return capacity;
}

std::vector<StockpileHolding> StockpileIndex::GetHoldings(const Player& owner, ResourceType type)
{
    const ProvinceEconomy* economy = owner.GetProvinceEconomy();
    return economy != nullptr ? GetHoldings(*economy, type) : std::vector<StockpileHolding>{};
}

std::vector<StockpileHolding> StockpileIndex::GetHoldings(const ProvinceEconomy& economy, ResourceType type)
{
    std::vector<StockpileHolding> holdings;
    for (Building* building : Warehouses(economy))
    {
        const auto* storage = WarehouseStorage(building);
        if (storage == nullptr)
            continue;

        auto it = storage->buffers.find(type);
        if (it == storage->buffers.end())
            continue;

        int amount = static_cast<int>(it->second.buffer.size());
        if (amount <= 0)
            continue;

        holdings.push_back({building->id, building, amount, it->second.bufferSize});
    }
    return holdings;
}

std::map<ResourceType, StockpileTotals> StockpileIndex::Snapshot(const Player& owner)
{
    const ProvinceEconomy* economy = owner.GetProvinceEconomy();
    return economy != nullptr ? Snapshot(*economy) : std::map<ResourceType, StockpileTotals>{};
}

std::map<ResourceType, StockpileTotals> StockpileIndex::Snapshot(const ProvinceEconomy& economy)
{
    std::map<ResourceType, StockpileTotals> snapshot;
    for (Building* building : Warehouses(economy))
    {
        const auto* storage = WarehouseStorage(building);
        if (storage == nullptr)
            continue;

        for (const auto& [type, buffer] : storage->buffers)
        {
            auto& totals = snapshot[type];
            int amount = static_cast<int>(buffer.buffer.size());
            totals.amount += amount;
            totals.capacity += buffer.bufferSize;
            if (amount > 0)
                totals.holdings.push_back({building->id, building, amount, buffer.bufferSize});
        }
    }
    return snapshot;
}

std::vector<Building*> StockpileIndex::RankSourcesFor(ResourceType type, Building& requester)
{
    Player* owner = requester.owner;
    ProvinceEconomy* economy = requester.provinceEconomy;
    if (owner == nullptr || economy == nullptr || economy->roadNetwork == nullptr)
        return {};

    struct Candidate
    {
        Building* warehouse{nullptr};
        size_t roadDistance{0};
    };

    std::vector<Candidate> candidates;
    for (const auto& holding : GetHoldings(*economy, type))
    {
        if (holding.building == nullptr || holding.building == &requester)
            continue;

        // A warehouse with no road path to the requester can never deliver, so
        // it is not a source at all — this is what keeps "nearest" honest
        // instead of ranking an unreachable neighbour ahead of a connected one.
        // CalculatePath memoizes per (src, dest) and drops the whole cache on
        // any road-topology change, so repeating this per tick is a map lookup.
        std::vector<int> path = economy->roadNetwork->CalculatePath(holding.building, &requester);
        if (path.empty())
            continue;

        candidates.push_back({holding.building, path.size()});
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b)
    {
        if (a.roadDistance != b.roadDistance)
            return a.roadDistance < b.roadDistance;
        return a.warehouse->id < b.warehouse->id;
    });

    std::vector<Building*> sources;
    sources.reserve(candidates.size());
    for (const auto& candidate : candidates)
        sources.push_back(candidate.warehouse);
    return sources;
}

int StockpileIndex::Consume(Player& owner, ResourceType type, int amount)
{
    ProvinceEconomy* economy = owner.GetProvinceEconomy();
    return economy != nullptr ? Consume(*economy, type, amount) : 0;
}

int StockpileIndex::Consume(ProvinceEconomy& economy, ResourceType type, int amount)
{
    int remaining = amount;
    for (Building* building : Warehouses(economy))
    {
        if (remaining <= 0)
            break;

        auto* storage = WarehouseStorage(building);
        if (storage == nullptr)
            continue;

        auto it = storage->buffers.find(type);
        if (it == storage->buffers.end())
            continue;

        while (remaining > 0 && !it->second.buffer.empty())
        {
            it->second.FreeResource();
            remaining--;
        }
    }
    return amount - std::max(0, remaining);
}

int StockpileIndex::Deposit(Player& owner, ResourceType type, int amount)
{
    ProvinceEconomy* economy = owner.GetProvinceEconomy();
    return economy != nullptr ? Deposit(*economy, type, amount) : 0;
}

int StockpileIndex::Deposit(ProvinceEconomy& economy, ResourceType type, int amount)
{
    int remaining = amount;
    for (Building* building : Warehouses(economy))
    {
        if (remaining <= 0)
            break;

        auto* storage = WarehouseStorage(building);
        if (storage == nullptr)
            continue;

        auto it = storage->buffers.find(type);
        if (it == storage->buffers.end())
            continue;

        while (remaining > 0 &&
               static_cast<int>(it->second.buffer.size()) < it->second.bufferSize)
        {
            const std::size_t sizeBefore = it->second.buffer.size();
            it->second.GenerateResource(type);
            if (it->second.buffer.size() == sizeBefore)
                break;
            remaining--;
        }
    }
    return amount - std::max(0, remaining);
}
