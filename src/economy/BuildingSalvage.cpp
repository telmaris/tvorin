#include "economy/BuildingSalvage.h"

#include "economy/Building.h"
#include "economy/BuildingConfig.h"
#include "economy/Player.h"
#include "economy/StockpileIndex.h"
#include "simulation/MapGenerator.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace
{
struct RefundLine
{
    ResourceType type{ResourceType::Null};
    int amount{0};
};

std::vector<RefundLine> ResolveRefund(const Building& building, const Player& owner)
{
    std::vector<ResourceAmountDefinition> costs;
    if (building.buildCostRecordState == BuildCostRecordState::PaidRecorded)
        costs = building.paidBuildCosts;
    else if (building.buildCostRecordState == BuildCostRecordState::LegacyUnknown)
        costs = owner.GetEffectiveBuildCosts(GetBuildingDefinition(building.buildingType));
    else
        return {};

    const bool unfinished = building.IsUnderConstruction();
    std::vector<RefundLine> result;
    for (const auto& cost : costs)
    {
        if (cost.amount <= 0 || cost.type == ResourceType::Null)
            continue;
        const int amount = unfinished ? cost.amount : cost.amount / 2;
        if (amount <= 0)
            continue;
        result.push_back({cost.type, amount});
    }
    return result;
}

std::map<ResourceType, int> CountBuffered(const Building& building)
{
    std::map<ResourceType, int> result;
    for (const ResourceBuffer* buffer : building.GetResourceBuffers())
    {
        if (buffer == nullptr)
            continue;
        for (const Resource* resource : buffer->buffer)
            if (resource != nullptr && resource->type != ResourceType::Null)
                ++result[resource->type];
    }
    return result;
}

int Distance(const TileMap& tilemap, const Building& source, const Building& target)
{
    const Vec2i a = tilemap.GetCoordsFromId(source.positionId);
    const Vec2i b = tilemap.GetCoordsFromId(target.positionId);
    return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

std::vector<Building*> OrderedWarehouses(const Player& owner, const Building& source)
{
    auto warehouses = StockpileIndex::Warehouses(owner);
    warehouses.erase(std::remove(warehouses.begin(), warehouses.end(), &source), warehouses.end());
    struct Candidate
    {
        Building* building{nullptr};
        bool reachable{false};
        bool headquarters{false};
        int pathLength{0};
        int tileDistance{0};
    };

    std::vector<Candidate> candidates;
    candidates.reserve(warehouses.size());
    for (Building* warehouse : warehouses)
    {
        if (warehouse == nullptr)
            continue;
        std::vector<int> path;
        const RoadNetwork* roadNetwork = owner.GetRoadNetwork();
        const int expectedMapArea = owner.tilemap->params.sizeX * owner.tilemap->params.sizeY;
        if (roadNetwork != nullptr && roadNetwork->navMap != nullptr &&
            static_cast<int>(roadNetwork->navMap->map.size()) == expectedMapArea)
        {
            // RoadNetwork's path query is logically read-only but retains a
            // deterministic cache, hence its legacy non-const API.
            path = const_cast<RoadNetwork*>(roadNetwork)->CalculatePath(
                const_cast<Building*>(&source), warehouse);
        }
        candidates.push_back({warehouse,
                              !path.empty(),
                              warehouse->buildingType == BuildingType::Headquarters,
                              static_cast<int>(path.size()),
                              Distance(*owner.tilemap, source, *warehouse)});
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b)
    {
        // The HQ is always the first destination, even when another warehouse
        // has a shorter reachable road path.
        if (a.headquarters != b.headquarters)
            return a.headquarters > b.headquarters;
        if (a.reachable != b.reachable)
            return a.reachable > b.reachable;
        if (a.reachable && a.pathLength != b.pathLength)
            return a.pathLength < b.pathLength;
        if (a.tileDistance != b.tileDistance)
            return a.tileDistance < b.tileDistance;
        return a.building->id < b.building->id;
    });

    std::vector<Building*> ordered;
    ordered.reserve(candidates.size());
    for (const Candidate& candidate : candidates)
        ordered.push_back(candidate.building);
    return ordered;
}

std::map<ResourceType, int> RequiredCapacity(const Building& building, const Player& owner)
{
    std::map<ResourceType, int> required = CountBuffered(building);
    for (const auto& refund : ResolveRefund(building, owner))
        required[refund.type] += refund.amount;
    return required;
}
}

DemolitionPreview BuildDemolitionPreview(const TileMap& tilemap,
                                         const Building& building,
                                         const Player& owner)
{
    DemolitionPreview preview;
    preview.unfinished = building.IsUnderConstruction();
    if (building.owner != &owner)
    {
        preview.reason = "Building is not owned by this player";
        return preview;
    }
    if (!building.CanBeManuallyDestroyed())
    {
        preview.reason = "Headquarters cannot be manually demolished";
        return preview;
    }

    const auto buffered = CountBuffered(building);
    const auto refund = ResolveRefund(building, owner);
    const auto warehouses = OrderedWarehouses(owner, building);
    const auto required = RequiredCapacity(building, owner);

    std::map<ResourceType, int> refundByType;
    for (const auto& line : refund)
        refundByType[line.type] += line.amount;
    for (const auto& [type, amount] : required)
    {
        int free = 0;
        for (const Building* warehouse : warehouses)
        {
            const auto* storage = warehouse->GetComponent<StorageComponent>();
            if (storage == nullptr)
                continue;
            const auto it = storage->buffers.find(type);
            if (it != storage->buffers.end())
                free += std::max(0, it->second.bufferSize - static_cast<int>(it->second.buffer.size()));
        }
        const int returned = std::min(amount, free);
        preview.resources.push_back({type,
                                     buffered.contains(type) ? buffered.at(type) : 0,
                                     refundByType.contains(type) ? refundByType.at(type) : 0,
                                     returned,
                                     amount - returned});
    }
    preview.allowed = true;
    preview.reason = "ok";
    return preview;
}

bool ExecuteDemolition(TileMap& tilemap, Player& owner, Building& building)
{
    const DemolitionPreview preview = BuildDemolitionPreview(tilemap, building, owner);
    if (!preview.allowed)
        return false;

    auto warehouses = OrderedWarehouses(owner, building);
    std::map<ResourceType, std::vector<ResourceBuffer*>> targets;
    for (Building* warehouse : warehouses)
    {
        auto* storage = warehouse->GetComponent<StorageComponent>();
        if (storage == nullptr)
            continue;
        for (auto& [type, buffer] : storage->buffers)
            targets[type].push_back(&buffer);
    }

    std::map<ResourceType, int> remainingReturn;
    for (const auto& line : preview.resources)
        remainingReturn[line.type] = line.returnedAmount;

    auto place = [&targets, &remainingReturn](ResourceType type, Resource* resource) -> bool
    {
        auto remaining = remainingReturn.find(type);
        if (remaining == remainingReturn.end() || remaining->second <= 0)
            return false;
        auto it = targets.find(type);
        if (it == targets.end())
            return false;
        for (ResourceBuffer* target : it->second)
        {
            if (target == nullptr || static_cast<int>(target->buffer.size()) >= target->bufferSize)
                continue;
            resource->ReleaseShipment();
            resource->sourceBuilding = nullptr;
            resource->targetBuilding = nullptr;
            resource->map = nullptr;
            resource->originatingOwner = nullptr;
            target->AddResource(resource);
            remaining->second--;
            return true;
        }
        return false;
    };

    // The preview has already proved every insertion possible. Keep an assert
    // in debug builds so a future component cannot silently drop cargo.
    for (ResourceBuffer* source : building.GetResourceBuffers())
    {
        if (source == nullptr)
            continue;
        while (!source->buffer.empty())
        {
            auto [available, resource] = source->GetResource();
            if (!available || resource == nullptr || !place(resource->type, resource))
            {
                if (resource != nullptr)
                    Resource::DestroyOwned(resource);
            }
        }
    }

    for (const auto& line : preview.resources)
    {
        for (int i = 0; i < line.refundAmount && remainingReturn[line.type] > 0; ++i)
        {
            Resource* resource = Resource::CreateOwned(line.type);
            if (!place(line.type, resource))
                Resource::DestroyOwned(resource);
        }
    }

    tilemap.DestroyBuildingAt(building.positionId);
    return true;
}
