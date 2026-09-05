#ifndef ROAD_NETWORK_H
#define ROAD_NETWORK_H

#include "economy/Building.h"
#include "simulation/ResourceShipment.h"
#include "simulation/ShipmentRenderState.h"

#include <cstdint>
#include <deque>
#include <map>
#include <tuple>
#include <vector>

class TileMap;

struct RoadTraversalCostConfig
{
    double instantaneousWeight{0.35};
    double emaWeight{0.65};
    double quadraticPenalty{1.5};
    double quarticPenalty{8.0};
    double matchingPriorityFactor{0.85};
    double otherPriorityFactor{1.05};
    double minimumCostSeconds{0.05};
};

// Pure, deterministic cost model used by weighted transport routing.
double ComputeRoadTraversalCost(const Building& road,
                                ResourceType resourceType,
                                const RoadTraversalCostConfig& config = {});

struct NavigationNode
{
    Building* node{nullptr};
    // Returns true when this navigation node contains a road-like building.
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
    std::size_t GetLiveShipmentCount() const { return activeShipments.size(); }
    std::size_t GetShipmentRecordCount() const { return activeShipments.size(); }
    bool TryGetShipmentRecord(ShipmentId id, ResourceShipment& out) const;
    // Copies pointer-free shipment records in deterministic shipment-id order
    // for read-only diagnostics and presentation projections.
    void AppendShipmentRecords(std::vector<ResourceShipment>& out) const;
    ShipmentId GetNextShipmentId() const noexcept { return nextShipmentId; }
    // Persistence-only reconstruction after every building and navigation
    // node in the province has been restored.
    bool RestoreShipment(const ResourceShipment& shipment, Building* source,
                         Building* target);
    bool RestoreNextShipmentId(ShipmentId value) noexcept;
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
    // Calculates a weighted path for a concrete resource shipment. Dynamic
    // traffic is sampled at dispatch; in-flight shipments are not rerouted.
    std::vector<int> CalculatePath(Building* src, Building* dest, ResourceType resourceType);
    // Invalidates weighted paths after a deterministic gameplay change alters
    // road speed, capacity or priority without changing map topology.
    void InvalidateRoutingCosts();

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
        struct PathCacheKey
        {
            int sourceId{0};
            int destinationId{0};
            ResourceType resourceType{ResourceType::Null};
            std::uint64_t topologyRevision{0};
            std::uint64_t trafficEpoch{0};

            bool operator<(const PathCacheKey& other) const
            {
                return std::tie(sourceId, destinationId, resourceType,
                                topologyRevision, trafficEpoch) <
                    std::tie(other.sourceId, other.destinationId, other.resourceType,
                              other.topologyRevision, other.trafficEpoch);
            }
        };

        struct PathCacheEntry
        {
            std::vector<int> path;
            std::uint64_t expiresAtTick{0};
            std::uint64_t createdAtTick{0};
        };

        static constexpr std::uint64_t PathCacheTtlTicks = 50; // 0.5 s at 100 Hz
        static constexpr std::size_t PathCacheMaxEntries = 512;

        void RefreshTrafficEpoch();
        bool TryGetCachedPath(const PathCacheKey& key, std::vector<int>& path);
        void StoreCachedPath(const PathCacheKey& key, std::vector<int> path);

        // Weighted routes are valid only for the current topology and traffic
        // bucket. The short TTL bounds the lifetime of a route when traffic
        // changes without crossing a 25% utilization bucket.
        std::map<PathCacheKey, PathCacheEntry> pathCache;
        std::uint64_t topologyRevision{1};
        std::uint64_t trafficEpoch{1};
        std::uint64_t routingTick{0};
        std::map<int, std::pair<int, int>> trafficSignature;
        std::map<ShipmentId, Transportable*> activeShipments;
        ShipmentId nextShipmentId{1};
        std::map<int, std::deque<ShipmentId>> prioritizedAdmissionGrants;
};

#endif
