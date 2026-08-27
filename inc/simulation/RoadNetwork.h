#ifndef ROAD_NETWORK_H
#define ROAD_NETWORK_H

#include "economy/Building.h"
#include "simulation/ResourceShipment.h"
#include "simulation/ShipmentRenderState.h"

#include <deque>

class TileMap;

struct NavigationNode
{
    Building* node{nullptr};
    // Returns true when this navigation node contains a road-like building
    // (Road or Bridge, B6 — see IsRoadLike in economy/Building.h).
    bool IsRoad()
    {
        if( node == nullptr) return false;
        return IsRoadLike(node->buildingType);
    }
};

// Linear navigation grid mirroring the tile map.
class NavigationMap
{
    public:

        std::vector<NavigationNode> map;
    int sizeX{0};
    int sizeY{0};
};

// Calculates road paths and starts resource transports between buildings.
class RoadNetwork
{
    public:

    RoadNetwork() = delete;
    RoadNetwork(TileMap&);
    ~RoadNetwork();

    // GameWorld can commit a fully parsed temporary state by moving its
    // owning graph. Rebind non-owning world pointers before publishing it.
    void RebindWorld(TileMap& map);

    // Advances road network state.
    void Update(double);
    // Grants admission to a prioritized road tile. The grant order is
    // deterministic for all ready shipments targeting the same tile; callers
    // must invoke this before handing the shipment off to that road.
    bool TryAdmitRoadEntry(Transportable* transportable, Building* road);
    // Starts a resource transport if a valid path exists.
    bool BeginTransport(Building* src, Building* dest, Transportable* res);
    // Removes a completed/cancelled transport from the world-owned registry.
    void ReleaseShipment(Transportable* transportable);
    // Updates the pointer-free value record from the legacy payload while the
    // two representations coexist during the WP-11 migration.
    void RefreshShipment(const Transportable& transportable);
    std::size_t GetLiveShipmentCount() const { return activeShipments.size(); }
    std::size_t GetShipmentRecordCount() const { return shipmentRecords.Size(); }
    const ResourceShipment* FindShipmentRecord(ShipmentId id) const { return shipmentRecords.Find(id); }
    // Pointer-safe membership check for carrier cleanup. Callers may use this
    // before dereferencing a raw pointer held by a building's legacy carrier
    // vector, because completed shipments can leave stale entries there.
    bool IsTrackingShipment(const Transportable* transportable) const;
    // Appends an immutable presentation-only view of every resource shipment.
    // Callers may retain the copied values after releasing the world lock.
    void AppendShipmentRenderStates(std::vector<ShipmentRenderState>& out) const;
    // Registers a building or road in the navigation map.
    void UpdateNavMap(int id, Building* bld);
    // Calculates a tile-id path between two building footprints.
    std::vector<int> CalculatePath(Building* src, Building* dest);

    const std::string tag{"[Road Network]"};

    std::unique_ptr<NavigationMap> navMap;
    TileMap* tilemap{nullptr};

    private: 

        // Estimates transport duration between two buildings.
        double CalculateTransportTime(Building* src, Building* dest);
        // Returns true when destination buffer and every road on the path have free capacity.
        bool CanReserveTransportPath(Building* dest, Transportable* res, const std::vector<int>& path) const;
        // Counts transports that already occupy or reserve a road tile.
        int CountReservedRoadCapacity(int roadTileId) const;
        // Counts transports already heading to a destination buffer.
        int CountIncomingToDestination(Building* dest, ResourceType type) const;

        // Perf fix (docs/post_pivot_audit_2026-07-12.md follow-up, 2026-07-12):
        // CalculatePath does a full grid BFS over the whole tilemap (allocating
        // three map-sized vectors) on every call. Before the T1 tile.owner fix
        // this cost was latent (every lookup failed instantly); with transport
        // actually working, every dispatching building calls it at least once
        // per (src, dest) pair per tick, and per RESOURCE UNIT within a
        // dispatch batch. Cache every computed (src, dest) -> path, keyed by
        // building ids (deterministic ordering), cleared on any road-network
        // topology change (UpdateNavMap — fires on every build/destroy).
        // Purely a performance memo: identical inputs, identical BFS result.
        std::map<std::pair<int, int>, std::vector<int>> pathCache;
        std::map<ShipmentId, Transportable*> activeShipments;
        ResourceShipmentIndex shipmentRecords;
        ShipmentId nextShipmentId{1};
        std::map<int, std::deque<ShipmentId>> prioritizedAdmissionGrants;
};

#endif
