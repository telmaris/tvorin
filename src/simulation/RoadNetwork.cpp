#include "simulation/RoadNetwork.h"

#include <queue>
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
    // occupying the tile (source of truth), not Tile::owner — the old
    // territory system that populated Tile::owner was removed in the Tower
    // Defense pivot (ETAP 1) and nothing sets it anymore.
    bool IsTileTraversableForOwner(TileMap* map, int tileId, Player* owner, Building* origin, Building* destination)
    {
        if (map == nullptr || tileId < 0 || tileId >= static_cast<int>(map->tilemap.size()))
            return false;

        Building* building = map->GetBuilding(tileId);
        if (building == origin || building == destination)
            return true;

        // B6: Bridge is road-like for transport purposes (IsRoadLike, economy/Building.h).
        return building != nullptr && building->owner == owner &&
               IsRoadLike(building->buildingType);
    }
}

// Advances this object's state for one frame.
TransportUpdateResult Transportable::Update(double dt)
{
    struct ShipmentRecordRefreshGuard
    {
        Transportable& transportable;

        ~ShipmentRecordRefreshGuard()
        {
            if (transportable.shipmentNetwork != nullptr)
                transportable.shipmentNetwork->RefreshShipment(transportable);
        }
    } refreshGuard{*this};

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
                current->ReceptTransport(this);
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
    shipmentRecords.Clear();
    prioritizedAdmissionGrants.clear();
}

void RoadNetwork::RebindWorld(TileMap& map)
{
    tilemap = &map;
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
    // Admission grants are scoped to one fixed simulation tick. Rebuilding
    // them lets a newly-ready shipment participate in the next tick without
    // allowing a stale grant to cross a tick boundary.
    prioritizedAdmissionGrants.clear();
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
    auto path = CalculatePath(src, dest);
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
    const auto* resource = dynamic_cast<const Resource*>(res);
    res->elapsedTime = 0.0;
    res->transportTime = src->GetModifiedDispatchDelay(
        resource != nullptr ? resource->type : ResourceType::Null);

    // Keep a pointer-free value projection in lockstep with the legacy
    // payload. Quantity is intentionally one for this migration stage so the
    // existing economy balance and dispatch semantics remain unchanged.
    if (resource != nullptr && resource->type != ResourceType::Null)
    {
        ResourceShipment record;
        record.id = shipmentId;
        record.type = resource->type;
        record.quantity = 1;
        record.sourceBuildingId = src->id;
        record.targetBuildingId = dest->id;
        record.pathTileIds = path;
        record.currentPathStep = res->currentPathStep;
        record.elapsedTime = res->elapsedTime;
        record.transportTime = res->transportTime;
        shipmentRecords.Insert(std::move(record));
    }
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
        shipmentRecords.Erase(shipmentId);
        transportable->shipmentNetwork = nullptr;
        transportable->shipmentId = 0;
    }
}

void RoadNetwork::RefreshShipment(const Transportable& transportable)
{
    if (transportable.shipmentId == 0)
        return;

    ResourceShipment* record = shipmentRecords.Find(transportable.shipmentId);
    if (record == nullptr)
        return;

    const auto* resource = dynamic_cast<const Resource*>(&transportable);
    if (resource == nullptr)
        return;

    record->currentPathStep = transportable.currentPathStep;
    record->elapsedTime = transportable.elapsedTime;
    record->transportTime = transportable.transportTime;
    record->state = ResourceShipmentState::InTransit;
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
        out.push_back(view);
    }
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

    // Any topology change can change which paths are valid — drop every cached
    // CalculatePath result rather than risk serving a stale route.
    pathCache.clear();

    if (bld == nullptr)
    {
        navMap->map[id].node = nullptr;
        return;
    }

    Log::Msg(tag, bld->name, " added to Navigation Map at map id ", id);
    navMap->map[id].node = bld;
}

// Initializes RoadNetwork::CalculatePath.
std::vector<int> RoadNetwork::CalculatePath(Building *src, Building *dest)
{
    if (src == nullptr || dest == nullptr || src->owner == nullptr)
        return {};

    std::pair<int, int> cacheKey{src->id, dest->id};
    auto cached = pathCache.find(cacheKey);
    if (cached != pathCache.end())
        return cached->second;

    int maxColumns = tilemap->params.sizeX;
    int maxRows = tilemap->params.sizeY;
    int maxIndex = maxColumns * maxRows;
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
            // src's owner — resolved from the nav map's building, not
            // Tile::owner (removed with the territory system, ETAP 1).
            bool isDestinationTile = navMap->map[next].node == dest;
            bool isOwnedRoad = navMap->map[next].IsRoad() &&
                               navMap->map[next].node->owner == src->owner;
            if (!isDestinationTile && !isOwnedRoad)
                continue;

            visited[next] = true;
            parent[next] = current;
            q.push(next);
        }
    }

    if (reachedEnd < 0)
    {
        pathCache[cacheKey] = {};
        return {};
    }

    std::vector<int> path;
    for (int at = reachedEnd; at != -1; at = parent[at])
        path.push_back(at);

    std::reverse(path.begin(), path.end());

    pathCache[cacheKey] = path;
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
    if (dest == nullptr || dest->owner == nullptr)
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
    for (Building* carrier : dest->owner->GetTrackedBuildings())
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
