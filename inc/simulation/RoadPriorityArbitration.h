#ifndef ROAD_PRIORITY_ARBITRATION_H
#define ROAD_PRIORITY_ARBITRATION_H

#include "data/Resource.h"

#include <algorithm>
#include <cstdint>
#include <vector>

// Deterministic v1 arbitration primitive. The RoadNetwork spike uses this
// ordering contract before a transport phase is introduced: a matching
// product wins over a non-matching product, and equal classes are FIFO by the
// stable shipment id. No aging is hidden in this comparator.
struct RoadPriorityCandidate
{
    ResourceType resource{ResourceType::Null};
    std::uint64_t shipmentId{0};
};

inline bool RoadPriorityMatches(ResourceType priority, ResourceType resource)
{
    return priority != ResourceType::Null && priority == resource;
}

inline void SortRoadPriorityCandidates(ResourceType priority,
                                        std::vector<RoadPriorityCandidate>& candidates)
{
    std::stable_sort(candidates.begin(), candidates.end(), [priority](const auto& lhs, const auto& rhs)
    {
        const bool lhsMatches = RoadPriorityMatches(priority, lhs.resource);
        const bool rhsMatches = RoadPriorityMatches(priority, rhs.resource);
        if (lhsMatches != rhsMatches)
            return lhsMatches > rhsMatches;
        return lhs.shipmentId < rhs.shipmentId;
    });
}

#endif
