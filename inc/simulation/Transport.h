#ifndef TRANSPORT_H
#define TRANSPORT_H

#include "core/Types.h"

#include <cstdint>
#include <vector>

class Building;
class Player;
class RoadNetwork;
class TileMap;

using ShipmentId = std::uint64_t;

enum class TransportUpdateResult : std::uint8_t
{
    Waiting,
    HandedOff,
    Finished
};

// Presentation-only phase of an active shipment. Loading is limited to one
// source-side shipment at a time; WaitingForRoad means loading has completed
// but admission to the next road tile is currently blocked.
enum class TransportPhase : std::uint8_t
{
    Loading,
    WaitingForRoad,
    InTransit,
    Delivered,
    Cancelled
};

struct Transportable
{
    virtual ~Transportable() = default;

    double transportTime = 0.0, elapsedTime = 0.0;

    Building* sourceBuilding = nullptr;
    Building* targetBuilding = nullptr;
    TileMap* map = nullptr;
    std::vector<int> transportPath;
    int currentPathStep = 0;
    // Owner captured once at BeginTransport, not re-read from sourceBuilding->owner
    // every tick — the road/building ownership check below must stay valid even if
    // the source building itself changes hands mid-transport (would otherwise be
    // self-referential and never detect the change).
    Player* originatingOwner = nullptr;

    // Stable per-road-network identity for diagnostics and lifecycle cleanup.
    // The transport object remains the compatibility payload for now; the
    // next AUD-02 stage can replace it with a quantity-based shipment record.
    ShipmentId shipmentId{0};
    RoadNetwork* shipmentNetwork{nullptr};

    void ReleaseShipment();
    
    TransportUpdateResult Update(double);
    void BeginTransport(Building*, Building*, TileMap*, const std::vector<int>&);
};

#endif
