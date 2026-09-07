#ifndef BUILDING_COMPONENTS_INTERNAL_H
#define BUILDING_COMPONENTS_INTERNAL_H

#include "economy/Building.h"

// Private helpers shared by building-component translation units.

// Counts resource units already in transit toward `target` (so callers don't
// over-request capacity that's already spoken for).
int CountIncomingResources(Building* target, ResourceType type);

// Free capacity `target` can still accept for `type`, net of anything already
// incoming.
int GetReceiveCapacity(Building* target, ResourceType type);

#endif
