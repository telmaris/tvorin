#include "simulation/RoadNetwork.h"

#include <cmath>
#include <iterator>
#include <queue>
#include <tuple>
#include <utility>
#include "simulation/MapGenerator.h"
#include "economy/Player.h"
#include "core/Log.h"
#include "simulation/RoadPriorityArbitration.h"

namespace
{
    // A path tile is still traversable for `owner` when it is the origin
    // building (step 0 of any path), the final destination building, or a
    // road owned by `owner` (every intermediate step, per
    // RoadNetwork::CalculatePath's BFS). Checked against the actual building
    // occupying the tile (source of truth), not Tile::owner — local logistics
    // no longer use a separate territory system.
    bool IsTileTraversableForOwner(TileMap* map, int tileId, Player* owner, Building* origin, Building* destination)
    {
        if (map == nullptr || tileId < 0 || tileId >= static_cast<int>(map->tilemap.size()))
            return false;

        Building* building = map->GetBuilding(tileId);
        if (building == origin || building == destination)
            return true;

        return building != nullptr && building->owner == owner &&
               IsRoadLike(building->buildingType);
    }
}

// Advances this object's state for one frame.
TransportUpdateResult Transportable::Update(double dt)
{
    auto cancelTransport = [&]()
    {
        auto* resource = dynamic_cast<Resource*>(this);
        if (resource != nullptr)
        {
            if (sourceBuilding != nullptr)
                sourceBuilding->ReturnOutgoingResource(resource);
            if (targetBuilding != nullptr)
                targetBuilding->CancelRequestedResource(resource->type);
        }
    };

    if (originatingOwner == nullptr || map == nullptr || currentPathStep < 0 || currentPathStep >= static_cast<int>(transportPath.size()))
    {
        cancelTransport();
        return TransportUpdateResult::Finished;
    }

    int currentTileId = transportPath[currentPathStep];
    if (!IsTileTraversableForOwner(map, currentTileId, originatingOwner, sourceBuilding, targetBuilding))
    {
        cancelTransport();
        return TransportUpdateResult::Finished;
    }

    elapsedTime += dt;
    if(elapsedTime >= transportTime)
    {
        if (currentPathStep + 1 >= transportPath.size())
        {
            Building* current = map->GetBuilding(currentTileId);
            if (current != nullptr && current == targetBuilding)
            {
                current->ReceptTransport(this);
            }
            else
                cancelTransport();
            return TransportUpdateResult::Finished;
        }

        int nextTileId = transportPath[currentPathStep + 1];
        if (!IsTileTraversableForOwner(map, nextTileId, originatingOwner, sourceBuilding, targetBuilding))
        {
            cancelTransport();
            return TransportUpdateResult::Finished;
        }

        Building* next = map->GetBuilding(nextTileId);
        if (next == nullptr)
        {
            cancelTransport();
            return TransportUpdateResult::Finished;
        }

        if (next->HasComponent<RoadComponent>())
        {
            auto* road = next->GetComponent<RoadComponent>();
            if (road != nullptr &&
                static_cast<int>(next->transportables.size()) >= road->GetModifiedMaxCapacity(*next))
            {
                Building* currentBuilding = map->GetBuilding(currentTileId);
                auto* currentRoad = currentBuilding != nullptr && currentBuilding->HasComponent<RoadComponent>()
                    ? currentBuilding->GetComponent<RoadComponent>()
                    : nullptr;
                if (currentRoad != nullptr && road != nullptr)
                {
                    auto currentIt = std::find(currentBuilding->transportables.begin(), currentBuilding->transportables.end(), this);
                    auto oncomingIt = std::find_if(next->transportables.begin(), next->transportables.end(),
                        [currentTileId](Transportable* other)
                        {
                            return other != nullptr &&
                                   other->currentPathStep + 1 < static_cast<int>(other->transportPath.size()) &&
                                   other->transportPath[other->currentPathStep + 1] == currentTileId &&
                                   other->elapsedTime >= other->transportTime;
                        });

                    if (currentIt != currentBuilding->transportables.end() && oncomingIt != next->transportables.end())
                    {
                        Transportable* oncoming = *oncomingIt;
                        *currentIt = oncoming;
                        *oncomingIt = this;

                        currentPathStep++;
                        elapsedTime = 0.0;
                        transportTime = next->GetModifiedTransportTime();

                        oncoming->currentPathStep++;
                        oncoming->elapsedTime = 0.0;
                        oncoming->transportTime = currentBuilding->GetModifiedTransportTime();
                    }
                }
                return TransportUpdateResult::Waiting;
            }

            if (road != nullptr && road->GetPriorityResource() != ResourceType::Null &&
                shipmentNetwork != nullptr && !shipmentNetwork->TryAdmitRoadEntry(this, next))
                return TransportUpdateResult::Waiting;
        }

        const bool reachedDestination = next == targetBuilding;
        next->ReceptTransport(this);

        return reachedDestination
            ? TransportUpdateResult::Finished
            : TransportUpdateResult::HandedOff;
    }
    return TransportUpdateResult::Waiting;
}

double ComputeRoadTraversalCost(const Building& road,
                                ResourceType resourceType,
                                const RoadTraversalCostConfig& config)
{
    const auto* component = road.GetComponent<RoadComponent>();
    if (component == nullptr)
        return config.minimumCostSeconds;

    const int capacity = std::max(1, component->GetModifiedMaxCapacity(road));
    const double instant = std::clamp(
        static_cast<double>(road.transportables.size()) / static_cast<double>(capacity),
        0.0, 1.0);
    const double ema = std::clamp(component->GetTrafficUtilizationTrend(), 0.0, 1.0);
    const double utilization = std::clamp(
        config.instantaneousWeight * instant + config.emaWeight * ema, 0.0, 1.0);
    const double penalty = config.quadraticPenalty * utilization * utilization +
        config.quarticPenalty * std::pow(utilization, 4.0);

    double priorityFactor = 1.0;
    const ResourceType priority = component->GetPriorityResource();
    if (priority != ResourceType::Null)
        priorityFactor = priority == resourceType
            ? config.matchingPriorityFactor : config.otherPriorityFactor;

    const double baseSeconds = std::max(config.minimumCostSeconds, road.GetModifiedTransportTime());
    return std::max(config.minimumCostSeconds,
        baseSeconds * (1.0 + penalty) * priorityFactor);
}

// Initializes Transportable::BeginTransport.
void Transportable::BeginTransport(Building* src,Building* target, TileMap* tmap, const std::vector<int>& path)
{
    sourceBuilding = src;
    targetBuilding = target;
    originatingOwner = src != nullptr ? src->owner : nullptr;
    map = tmap;
    transportPath = path;
    transportTime = 0.0;
    elapsedTime = 0.0;
    currentPathStep = 0;
}

// Initializes RoadNetwork::RoadNetwork.
RoadNetwork::RoadNetwork(TileMap &tmap)
{
    navMap = std::make_unique<NavigationMap>();
    navMap->map = std::vector<NavigationNode>(tmap.tilemap.size());
    tilemap = &tmap;
}

RoadNetwork::~RoadNetwork()
{
    // Buildings outlive a Player's RoadNetwork in the current ownership
    // graph. Clear back-pointers before the registry disappears so their
    // destructors never call through a dangling network pointer.
    for (auto& [id, transportable] : activeShipments)
    {
        if (transportable != nullptr && transportable->shipmentNetwork == this)
        {
            transportable->shipmentNetwork = nullptr;
            transportable->shipmentId = 0;
        }
    }
    activeShipments.clear();
    prioritizedAdmissionGrants.clear();
}

void RoadNetwork::RebindWorld(TileMap& map)
{
    tilemap = &map;
    if (navMap == nullptr)
        navMap = std::make_unique<NavigationMap>();
    if (navMap->map.size() != map.tilemap.size())
        navMap->map = std::vector<NavigationNode>(map.tilemap.size());
    navMap->sizeX = map.params.sizeX;
    navMap->sizeY = map.params.sizeY;
    ++topologyRevision;
    pathCache.clear();
    trafficSignature.clear();
    for (auto& [shipmentId, transport] : activeShipments)
    {
        (void)shipmentId;
        if (transport != nullptr)
        {
            transport->map = &map;
            transport->shipmentNetwork = this;
        }
    }
}

void Transportable::ReleaseShipment()
{
    if (shipmentNetwork != nullptr)
    {
        RoadNetwork* network = shipmentNetwork;
        network->ReleaseShipment(this);
        return;
    }
    shipmentId = 0;
}

// Advances this object's state for one frame.
void RoadNetwork::Update(double dt)
{
    (void)dt;
    ++routingTick;
    RefreshTrafficEpoch();
    // Admission grants are scoped to one fixed simulation tick. Rebuilding
    // them lets a newly-ready shipment participate in the next tick without
    // allowing a stale grant to cross a tick boundary.
    prioritizedAdmissionGrants.clear();
}

void RoadNetwork::InvalidateRoutingCosts()
{
    if (trafficEpoch == std::numeric_limits<std::uint64_t>::max())
        trafficEpoch = 1;
    else
        ++trafficEpoch;
    pathCache.clear();
    // The next fixed tick rebuilds the occupancy/priority baseline without
    // incrementing the epoch a second time for the same explicit change.
    trafficSignature.clear();
}

bool RoadNetwork::TryAdmitRoadEntry(Transportable* transportable, Building* road)
{
    if (transportable == nullptr || road == nullptr ||
        !road->HasComponent<RoadComponent>() || transportable->shipmentId == 0)
        return true;

    const auto* roadComponent = road->GetComponent<RoadComponent>();
    if (roadComponent == nullptr || roadComponent->GetPriorityResource() == ResourceType::Null)
        return true;

    const int roadTileId = road->positionId;
    auto& grants = prioritizedAdmissionGrants[roadTileId];
    auto isReadyCandidate = [this, roadTileId](ShipmentId shipmentId)
    {
        auto shipmentIt = activeShipments.find(shipmentId);
        if (shipmentIt == activeShipments.end() || shipmentIt->second == nullptr)
            return false;

        const Transportable* candidate = shipmentIt->second;
        if (candidate->currentPathStep < 0 ||
            candidate->currentPathStep + 1 >= static_cast<int>(candidate->transportPath.size()) ||
            candidate->transportPath[candidate->currentPathStep + 1] != roadTileId ||
            candidate->elapsedTime < candidate->transportTime)
            return false;

        const auto* resource = dynamic_cast<const Resource*>(candidate);
        return resource != nullptr && resource->type != ResourceType::Null;
    };

    while (!grants.empty() && !isReadyCandidate(grants.front()))
        grants.pop_front();

    if (grants.empty())
    {
        const int capacity = std::max(0, roadComponent->GetModifiedMaxCapacity(*road));
        const int occupied = static_cast<int>(road->transportables.size());
        const int available = std::max(0, capacity - occupied);
        if (available <= 0)
            return false;

        std::vector<RoadPriorityCandidate> candidates;
        candidates.reserve(activeShipments.size());
        for (const auto& [shipmentId, candidate] : activeShipments)
        {
            if (candidate == nullptr || !isReadyCandidate(shipmentId))
                continue;
            const auto* resource = dynamic_cast<const Resource*>(candidate);
            candidates.push_back({resource->type, shipmentId});
        }
        SortRoadPriorityCandidates(roadComponent->GetPriorityResource(), candidates);
        for (int index = 0; index < available && index < static_cast<int>(candidates.size()); ++index)
            grants.push_back(candidates[index].shipmentId);
    }

    if (grants.empty() || grants.front() != transportable->shipmentId)
        return false;

    grants.pop_front();
    if (grants.empty())
        prioritizedAdmissionGrants.erase(roadTileId);
    return true;
}

// Initializes RoadNetwork::BeginTransport.
bool RoadNetwork::BeginTransport(Building *src, Building *dest, Transportable* res)
{
    if (src == nullptr || dest == nullptr || res == nullptr || res->shipmentNetwork != nullptr)
        return false;
    const auto* resource = dynamic_cast<const Resource*>(res);
    auto path = CalculatePath(src, dest,
                              resource != nullptr ? resource->type : ResourceType::Null);
    if(path.empty())
    {
        // Debug-level: no spam when supply packages retry without drogi (happens constantly).
        // Only log if you really need to debug routing issues (use Logger level DEBUG to see).
        return false;
    }
    if (!CanReserveTransportPath(dest, res, path))
    {
        // Debug-level: destination full is common during supply congestion, don't spam logs.
        return false;
    }
    res->BeginTransport(src, dest, tilemap, path);
    ShipmentId shipmentId = nextShipmentId++;
    if (shipmentId == 0)
        shipmentId = nextShipmentId++;
    res->shipmentId = shipmentId;
    res->shipmentNetwork = this;
    activeShipments.emplace(shipmentId, res);
    src->ReceptTransport(res);
    // ReceptTransport normally selects the carrier's traversal time. The
    // source is a loading stage instead: hold the shipment on the source/road
    // boundary for a short, independently modifiable dispatch delay.
    res->elapsedTime = 0.0;
    res->transportTime = src->GetModifiedDispatchDelay(
        resource != nullptr ? resource->type : ResourceType::Null);

    return true;
}

void RoadNetwork::ReleaseShipment(Transportable* transportable)
{
    if (transportable == nullptr)
        return;

    const ShipmentId shipmentId = transportable->shipmentId;
    bool released = false;
    if (transportable->shipmentNetwork == this && transportable->shipmentId != 0)
    {
        auto it = activeShipments.find(transportable->shipmentId);
        if (it != activeShipments.end() && it->second == transportable)
        {
            activeShipments.erase(it);
            released = true;
        }
    }

    // Building destruction can discover a legacy carrier entry whose
    // back-pointer already refers to a dead road network. The current network
    // still owns the authoritative registry, so find the payload by address
    // without following that stale back-pointer.
    if (!released)
    {
        auto it = std::find_if(activeShipments.begin(), activeShipments.end(),
            [transportable](const auto& shipment) { return shipment.second == transportable; });
        if (it != activeShipments.end())
        {
            activeShipments.erase(it);
            released = true;
        }
    }

    if (released || transportable->shipmentNetwork == this)
    {
        transportable->shipmentNetwork = nullptr;
        transportable->shipmentId = 0;
    }
}

bool RoadNetwork::TryGetShipmentRecord(ShipmentId id, ResourceShipment& out) const
{
    const auto it = activeShipments.find(id);
    const auto* resource = it == activeShipments.end()
        ? nullptr : dynamic_cast<const Resource*>(it->second);
    if (resource == nullptr || resource->shipmentId != id ||
        resource->type == ResourceType::Null || resource->sourceBuilding == nullptr ||
        resource->targetBuilding == nullptr)
        return false;

    out = ResourceShipment{};
    out.id = id;
    out.type = resource->type;
    out.quantity = 1;
    out.sourceBuildingId = resource->sourceBuilding->id;
    out.targetBuildingId = resource->targetBuilding->id;
    out.pathTileIds = resource->transportPath;
    out.currentPathStep = resource->currentPathStep;
    out.elapsedTime = resource->elapsedTime;
    out.transportTime = resource->transportTime;
    out.state = ResourceShipmentState::InTransit;
    return true;
}

bool RoadNetwork::IsTrackingShipment(const Transportable* transportable) const
{
    return std::find_if(activeShipments.begin(), activeShipments.end(),
        [transportable](const auto& shipment) { return shipment.second == transportable; }) != activeShipments.end();
}

void RoadNetwork::AppendShipmentRenderStates(std::vector<ShipmentRenderState>& out) const
{
    for (const auto& [id, transportable] : activeShipments)
    {
        const auto* resource = dynamic_cast<const Resource*>(transportable);
        if (resource == nullptr || resource->originatingOwner == nullptr ||
            resource->type == ResourceType::Null || resource->shipmentId == 0)
        {
            continue;
        }

        const int step = resource->currentPathStep;
        if (step < 0 || step + 1 >= static_cast<int>(resource->transportPath.size()))
            continue;

        const int fromTileId = resource->transportPath[step];
        const int toTileId = resource->transportPath[step + 1];
        if (tilemap == nullptr || fromTileId < 0 || toTileId < 0 ||
            fromTileId >= static_cast<int>(tilemap->tilemap.size()) ||
            toTileId >= static_cast<int>(tilemap->tilemap.size()))
        {
            continue;
        }

        const bool hasDuration = resource->transportTime > 0.0;
        const double rawProgress = hasDuration
            ? resource->elapsedTime / resource->transportTime
            : 1.0;

        ShipmentRenderState view;
        view.ownerPlayerId = resource->originatingOwner->id;
        view.shipmentId = id;
        view.resourceType = resource->type;
        view.previousTileId = step > 0 ? resource->transportPath[step - 1] : -1;
        view.fromTileId = fromTileId;
        view.toTileId = toTileId;
        view.progress = static_cast<float>(std::clamp(rawProgress, 0.0, 1.0));
        view.waitingForCapacity = hasDuration && resource->elapsedTime >= resource->transportTime;
        view.phase = resource->elapsedTime < resource->transportTime
            ? TransportPhase::Loading
            : TransportPhase::WaitingForRoad;
        if (step > 0 && resource->elapsedTime < resource->transportTime)
            view.phase = TransportPhase::InTransit;
        out.push_back(view);
    }
}

void RoadNetwork::AppendShipmentRecords(std::vector<ResourceShipment>& out) const
{
    for (const auto& [id, transportable] : activeShipments)
    {
        (void)transportable;
        ResourceShipment record;
        if (TryGetShipmentRecord(id, record))
            out.push_back(std::move(record));
    }
}

bool RoadNetwork::RestoreShipment(const ResourceShipment& shipment, Building* source,
                                  Building* target)
{
    if (tilemap == nullptr || source == nullptr || target == nullptr || source == target ||
        shipment.id == 0 || shipment.type == ResourceType::Null || shipment.quantity != 1 ||
        shipment.sourceBuildingId != source->id || shipment.targetBuildingId != target->id ||
        shipment.state != ResourceShipmentState::InTransit ||
        shipment.currentPathStep < 0 ||
        shipment.currentPathStep >= static_cast<int>(shipment.pathTileIds.size()) ||
        shipment.pathTileIds.size() < 2 || !std::isfinite(shipment.elapsedTime) ||
        !std::isfinite(shipment.transportTime) || shipment.elapsedTime < 0.0 ||
        shipment.transportTime < 0.0 || activeShipments.contains(shipment.id) ||
        source->owner == nullptr ||
        source->owner != target->owner || source->provinceEconomy == nullptr ||
        source->provinceEconomy != target->provinceEconomy ||
        source->provinceEconomy->roadNetwork.get() != this)
        return false;

    for (int tileId : shipment.pathTileIds)
        if (tileId < 0 || tileId >= static_cast<int>(tilemap->tilemap.size()))
            return false;

    Building* carrier = tilemap->GetBuilding(
        shipment.pathTileIds[static_cast<std::size_t>(shipment.currentPathStep)]);
    if (carrier == nullptr ||
        !IsTileTraversableForOwner(tilemap, carrier->positionId, source->owner, source, target))
        return false;

    Resource* resource = Resource::CreateOwned(shipment.type);
    if (resource == nullptr)
        return false;

    resource->sourceBuilding = source;
    resource->targetBuilding = target;
    resource->originatingOwner = source->owner;
    resource->map = tilemap;
    resource->transportPath = shipment.pathTileIds;
    resource->currentPathStep = shipment.currentPathStep;
    resource->elapsedTime = shipment.elapsedTime;
    resource->transportTime = shipment.transportTime;
    resource->shipmentId = shipment.id;
    resource->shipmentNetwork = this;

    activeShipments.emplace(shipment.id, resource);
    carrier->transportables.push_back(resource);
    return true;
}

bool RoadNetwork::RestoreNextShipmentId(ShipmentId value) noexcept
{
    if (value == 0)
        return false;
    if (!activeShipments.empty() && value <= activeShipments.rbegin()->first)
        return false;
    nextShipmentId = value;
    return true;
}

// Initializes RoadNetwork::CalculateTransportTime.
double RoadNetwork::CalculateTransportTime(Building *src, Building *dest)
{
    return 3.0;
}

// Advances UpdateNavMap for one frame or simulation tick.
void RoadNetwork::UpdateNavMap(int id, Building *bld)
{
    if (id < 0 || id >= navMap->map.size())
        return;

    // Any topology change can change which paths are valid — advance the
    // revision and drop every cached result rather than serving a stale route.
    ++topologyRevision;
    pathCache.clear();
    trafficSignature.clear();

    if (bld == nullptr)
    {
        navMap->map[id].node = nullptr;
        return;
    }

    Log::Msg(tag, bld->name, " added to Navigation Map at map id ", id);
    navMap->map[id].node = bld;
}

void RoadNetwork::RefreshTrafficEpoch()
{
    std::map<int, std::pair<int, int>> currentSignature;
    if (navMap != nullptr)
    {
        for (int tileId = 0; tileId < static_cast<int>(navMap->map.size()); ++tileId)
        {
            Building* building = navMap->map[tileId].node;
            if (building == nullptr || !IsRoadLike(building->buildingType))
                continue;

            const auto* road = building->GetComponent<RoadComponent>();
            if (road == nullptr)
                continue;

            const int capacity = std::max(1, road->GetModifiedMaxCapacity(*building));
            const int occupied = static_cast<int>(building->transportables.size());
            const int bucket = std::clamp((occupied * 4) / capacity, 0, 4);
            currentSignature[tileId] = {
                bucket,
                static_cast<int>(road->GetPriorityResource())};
        }
    }

    if (!trafficSignature.empty() && currentSignature != trafficSignature)
    {
        if (trafficEpoch == std::numeric_limits<std::uint64_t>::max())
            trafficEpoch = 1;
        else
            ++trafficEpoch;
    }
    trafficSignature = std::move(currentSignature);
}

bool RoadNetwork::TryGetCachedPath(const PathCacheKey& key, std::vector<int>& path)
{
    const auto cached = pathCache.find(key);
    if (cached == pathCache.end())
        return false;
    if (routingTick > cached->second.expiresAtTick)
    {
        pathCache.erase(cached);
        return false;
    }

    path = cached->second.path;
    return true;
}

void RoadNetwork::StoreCachedPath(const PathCacheKey& key, std::vector<int> path)
{
    const auto existing = pathCache.find(key);
    if (existing != pathCache.end())
    {
        existing->second.path = std::move(path);
        existing->second.createdAtTick = routingTick;
        existing->second.expiresAtTick = routingTick + PathCacheTtlTicks;
        return;
    }

    if (pathCache.size() >= PathCacheMaxEntries)
    {
        auto oldest = pathCache.begin();
        for (auto candidate = std::next(pathCache.begin()); candidate != pathCache.end(); ++candidate)
        {
            if (candidate->second.createdAtTick < oldest->second.createdAtTick ||
                (candidate->second.createdAtTick == oldest->second.createdAtTick &&
                 candidate->first < oldest->first))
                oldest = candidate;
        }
        pathCache.erase(oldest);
    }

    pathCache.emplace(key, PathCacheEntry{
        std::move(path), routingTick + PathCacheTtlTicks, routingTick});
}

// Initializes RoadNetwork::CalculatePath.
std::vector<int> RoadNetwork::CalculatePath(Building *src, Building *dest)
{
    if (src == nullptr || dest == nullptr || src->owner == nullptr || tilemap == nullptr)
        return {};

    const int maxColumns = tilemap->params.sizeX;
    const int maxRows = tilemap->params.sizeY;
    const std::size_t expectedTileCount = maxColumns > 0 && maxRows > 0
        ? static_cast<std::size_t>(maxColumns) * static_cast<std::size_t>(maxRows)
        : 0u;
    if (expectedTileCount == 0 || expectedTileCount != tilemap->tilemap.size() ||
        navMap == nullptr || navMap->map.size() != expectedTileCount ||
        tilemap->GetBuilding(src->positionId) != src ||
        tilemap->GetBuilding(dest->positionId) != dest)
        return {};

    const PathCacheKey cacheKey{
        src->id, dest->id, ResourceType::Null, topologyRevision, 0};
    std::vector<int> cachedPath;
    if (TryGetCachedPath(cacheKey, cachedPath))
        return cachedPath;

    const int maxIndex = static_cast<int>(expectedTileCount);
    auto startTiles = tilemap->GetBuildingTileIds(src);
    auto endTiles = tilemap->GetBuildingTileIds(dest);

    if (startTiles.empty() || endTiles.empty())
        return {};

    std::vector<bool> isEnd(maxIndex, false);
    for (int end : endTiles)
    {
        if (end >= 0 && end < maxIndex)
            isEnd[end] = true;
    }

    const std::vector<int> directions{
        -maxColumns,
        maxColumns,
        -1,
        1
    };

    std::vector<bool> visited(maxIndex, false);
    std::vector<int> parent(maxIndex, -1);

    std::queue<int> q;
    for (int start : startTiles)
    {
        // Start tiles are src's own footprint — no ownership check needed here
        // (unlike the old territory system, a building's own tiles are always
        // "its" tiles regardless of any separate Tile::owner bookkeeping).
        if (start < 0 || start >= maxIndex)
            continue;

        q.push(start);
        visited[start] = true;
    }

    int reachedEnd = -1;

    while (!q.empty())
    {
        int current = q.front();
        q.pop();

        if (isEnd[current])
        {
            reachedEnd = current;
            break;
        }

        int currentCol = current % maxColumns;
        int currentRow = current / maxColumns;

        for (int dir = 0; dir < 4; dir++)
        {
            int next = current + directions[dir];

            if (next < 0 || next >= maxIndex)
                continue;

            int col = next % maxColumns;
            int row = next / maxColumns;

            if (abs(col - currentCol) + abs(row - currentRow) != 1)
                continue;

            if (visited[next])
                continue;

            // Traversable when it's the destination itself, or a road owned by
            // src's owner. Resolve the occupant from TileMap, not only from
            // NavigationMap: the tile map is authoritative while a building
            // finishes construction and during the same tick in which a
            // footprint is replaced. Relying solely on the mirror could make
            // a physically adjacent road look disconnected until a later
            // topology refresh.
            Building* nextBuilding = tilemap->GetBuilding(next);
            bool isDestinationTile = nextBuilding == dest;
            bool isOwnedRoad = nextBuilding != nullptr &&
                               nextBuilding->owner == src->owner &&
                               IsRoadLike(nextBuilding->buildingType);
            if (!isDestinationTile && !isOwnedRoad)
                continue;

            visited[next] = true;
            parent[next] = current;
            q.push(next);
        }
    }

    if (reachedEnd < 0)
    {
        StoreCachedPath(cacheKey, {});
        return {};
    }

    std::vector<int> path;
    for (int at = reachedEnd; at != -1; at = parent[at])
        path.push_back(at);

    std::reverse(path.begin(), path.end());

    StoreCachedPath(cacheKey, path);
    return path;
}

std::vector<int> RoadNetwork::CalculatePath(Building *src, Building *dest, ResourceType resourceType)
{
    if (src == nullptr || dest == nullptr || src->owner == nullptr || tilemap == nullptr)
        return {};

    const int maxColumns = tilemap->params.sizeX;
    const int maxRows = tilemap->params.sizeY;
    const std::size_t expectedTileCount = maxColumns > 0 && maxRows > 0
        ? static_cast<std::size_t>(maxColumns) * static_cast<std::size_t>(maxRows)
        : 0u;
    if (expectedTileCount == 0 || expectedTileCount != tilemap->tilemap.size() ||
        expectedTileCount > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        navMap == nullptr || navMap->map.size() != expectedTileCount ||
        tilemap->GetBuilding(src->positionId) != src ||
        tilemap->GetBuilding(dest->positionId) != dest)
        return {};
    const int maxIndex = static_cast<int>(expectedTileCount);

    const PathCacheKey cacheKey{
        src->id, dest->id, resourceType, topologyRevision, trafficEpoch};
    std::vector<int> cachedPath;
    if (TryGetCachedPath(cacheKey, cachedPath))
        return cachedPath;

    const std::vector<int> startTiles = tilemap->GetBuildingTileIds(src);
    const std::vector<int> endTiles = tilemap->GetBuildingTileIds(dest);
    if (startTiles.empty() || endTiles.empty())
        return {};

    std::vector<bool> isEnd(maxIndex, false);
    for (int end : endTiles)
        if (end >= 0 && end < maxIndex)
            isEnd[end] = true;

    const std::vector<int> directions{-maxColumns, maxColumns, -1, 1};
    constexpr double Epsilon = 1e-9;
    std::vector<double> distance(maxIndex, std::numeric_limits<double>::infinity());
    std::vector<int> parent(maxIndex, -1);

    struct QueueEntry
    {
        double cost;
        int tileId;
    };
    auto compare = [=](const QueueEntry& lhs, const QueueEntry& rhs)
    {
        if (std::abs(lhs.cost - rhs.cost) > Epsilon)
            return lhs.cost > rhs.cost;
        return lhs.tileId > rhs.tileId;
    };
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, decltype(compare)> queue(compare);

    for (int start : startTiles)
    {
        if (start < 0 || start >= maxIndex)
            continue;
        if (distance[start] > 0.0)
        {
            distance[start] = 0.0;
            queue.push({0.0, start});
        }
    }

    int reachedEnd = -1;
    while (!queue.empty())
    {
        const QueueEntry current = queue.top();
        queue.pop();
        if (current.cost > distance[current.tileId] + Epsilon)
            continue;
        if (isEnd[current.tileId])
        {
            reachedEnd = current.tileId;
            break;
        }

        const int currentColumn = current.tileId % maxColumns;
        const int currentRow = current.tileId / maxColumns;
        for (int direction : directions)
        {
            const int next = current.tileId + direction;
            if (next < 0 || next >= maxIndex)
                continue;
            const int nextColumn = next % maxColumns;
            const int nextRow = next / maxColumns;
            if (std::abs(nextColumn - currentColumn) + std::abs(nextRow - currentRow) != 1)
                continue;

            Building* nextBuilding = navMap->map[next].node;
            const bool isDestinationTile = nextBuilding == dest;
            const bool isOwnedRoad = nextBuilding != nullptr &&
                nextBuilding->owner == src->owner && IsRoadLike(nextBuilding->buildingType);
            if (!isDestinationTile && !isOwnedRoad)
                continue;

            const double edgeCost = isOwnedRoad
                ? ComputeRoadTraversalCost(*nextBuilding, resourceType)
                : 0.0;
            const double candidate = current.cost + edgeCost;
            const bool strictlyBetter = candidate + Epsilon < distance[next];
            const bool equalWithBetterParent = std::abs(candidate - distance[next]) <= Epsilon &&
                (parent[next] < 0 || current.tileId < parent[next]);
            if (!strictlyBetter && !equalWithBetterParent)
                continue;

            distance[next] = candidate;
            parent[next] = current.tileId;
            queue.push({candidate, next});
        }
    }

    if (reachedEnd < 0)
    {
        StoreCachedPath(cacheKey, {});
        return {};
    }

    std::vector<int> path;
    for (int at = reachedEnd; at != -1; at = parent[at])
        path.push_back(at);
    std::reverse(path.begin(), path.end());
    StoreCachedPath(cacheKey, path);
    return path;
}

// Returns whether this condition is currently true.
bool RoadNetwork::CanReserveTransportPath(Building* dest, Transportable* res, const std::vector<int>& path) const
{
    auto* resource = dynamic_cast<Resource*>(res);
    if (resource != nullptr && dest != nullptr)
    {
        auto views = dest->GetInputBufferViews();
        auto outputViews = dest->GetOutputBufferViews();
        views.insert(views.end(), outputViews.begin(), outputViews.end());

        bool hasCapacityView = false;
        for (const auto& view : views)
        {
            if (view.type != resource->type)
                continue;

            hasCapacityView = true;
            int incoming = CountIncomingToDestination(dest, resource->type);
            if (view.amount + incoming >= view.capacity)
                return false;
            break;
        }

        if (!hasCapacityView || !dest->CanReceiveResource(resource->type))
            return false;
    }

    return true;
}

// Initializes RoadNetwork::CountIncomingToDestination.
int RoadNetwork::CountIncomingToDestination(Building* dest, ResourceType type) const
{
    if (dest == nullptr || dest->provinceEconomy == nullptr)
        return 0;

    // Perf fix (2026-07-12): this ran a FULL tilemap scan (sizeX*sizeY tiles,
    // ~90k on the default map) on every call — and it's called from
    // CanReserveTransportPath once per BeginTransport, i.e. once per resource
    // unit shipped. Latent before the T1 tile.owner fix (CalculatePath failed
    // first, so this was never reached); with transport actually working it
    // froze the whole sim thread during dispatch bursts. In-flight
    // transportables are always held by a building (source or road), and every
    // building is in the owner's tracked-buildings registry — same query
    // shape as CountIncomingResources in src/economy/Building.cpp.
    int incoming = 0;
    for (Building* carrier : dest->provinceEconomy->dataTracker.buildings)
    {
        if (carrier == nullptr || carrier->transportables.empty())
            continue;

        for (auto* transportable : carrier->transportables)
        {
            auto* resource = dynamic_cast<Resource*>(transportable);
            if (resource != nullptr && resource->targetBuilding == dest && resource->type == type)
                incoming++;
        }
    }

    return incoming;
}
