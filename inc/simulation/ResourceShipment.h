#ifndef RESOURCE_SHIPMENT_H
#define RESOURCE_SHIPMENT_H

#include "data/Resource.h"

#include <cstdint>
#include <vector>

// Pointer-free persistence and presentation projection of an in-flight
// Resource. RoadNetwork owns one canonical Resource instance per shipment and
// builds this value only at subsystem boundaries.
enum class ResourceShipmentState : std::uint8_t
{
    InTransit,
    HandedOff,
    Cancelled,
    Completed
};

struct ResourceShipment
{
    ShipmentId id{0};
    ResourceType type{ResourceType::Null};
    int quantity{1};

    // Stable domain identifiers. These are intentionally not object pointers.
    int sourceBuildingId{-1};
    int targetBuildingId{-1};

    std::vector<int> pathTileIds;
    int currentPathStep{0};
    double elapsedTime{0.0};
    double transportTime{0.0};
    ResourceShipmentState state{ResourceShipmentState::InTransit};
};

#endif
