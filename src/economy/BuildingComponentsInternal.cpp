#include "BuildingComponentsInternal.h"
#include "economy/Player.h"

#include <algorithm>

int CountIncomingResources(Building* target, ResourceType type)
{
    if (target == nullptr || target->provinceEconomy == nullptr)
        return 0;

    // OPTIMIZATION: Iterate only tracked buildings (much smaller set than full tilemap),
    // then check if they have transportables. Avoids 1M tile scans per call.
    int incoming = 0;
    for (Building* carrier : target->provinceEconomy->dataTracker.buildings)
    {
        if (carrier == nullptr || carrier->transportables.empty())
            continue;

        for (auto* t : carrier->transportables)
        {
            auto* res = dynamic_cast<Resource*>(t);
            if (res != nullptr && res->targetBuilding == target && res->type == type)
                incoming++;
        }
    }
    return incoming;
}

int GetReceiveCapacity(Building* target, ResourceType type)
{
    if (target == nullptr || !target->CanReceiveResource(type))
        return 0;

    auto findCap = [type](const std::vector<ResourceBufferView>& views) -> int
    {
        for (const auto& v : views)
            if (v.type == type)
                return std::max(0, v.capacity - v.amount);
        return -1;
    };

    int free = findCap(target->GetInputBufferViews());
    if (free < 0) free = findCap(target->GetOutputBufferViews());
    if (free < 0) free = target->CanReceiveResource(type) ? 1 : 0;

    return std::max(0, free - CountIncomingResources(target, type));
}
