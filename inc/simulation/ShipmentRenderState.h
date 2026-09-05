#ifndef SHIPMENT_RENDER_STATE_H
#define SHIPMENT_RENDER_STATE_H

#include "data/Resource.h"

// Immutable, pointer-free projection of one in-flight resource shipment.
// This is presentation data only: it must never feed simulation decisions,
// saves, multiplayer commands, or deterministic checksums.
struct ShipmentRenderState
{
    int ownerPlayerId{-1};
    ShipmentId shipmentId{0};
    ResourceType resourceType{ResourceType::Null};
    int previousTileId{-1};
    int fromTileId{-1};
    int toTileId{-1};
    float progress{0.0f};
    bool waitingForCapacity{false};
    TransportPhase phase{TransportPhase::InTransit};
};

#endif
