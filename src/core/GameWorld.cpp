#include "core/GameWorld.h"

#include <algorithm>

// GameWorld implementation is split across GameWorld.*.cpp files by responsibility.

std::size_t GameWorld::GetLiveShipmentCount() const
{
    std::size_t count = 0;
    for (ProvinceId provinceId : globalMap.GetProvinceIds())
    {
        const auto* province = globalMap.FindBuildableProvince(provinceId);
        const auto* simulation = province != nullptr ? province->GetSimulation() : nullptr;
        const auto* network = simulation != nullptr
            ? simulation->GetEconomy().roadNetwork.get() : nullptr;
        if (network != nullptr)
            count += network->GetLiveShipmentCount();
    }
    return count;
}

int GameWorld::GetStoredResourceUnits() const
{
    int total = 0;
    for (ProvinceId provinceId : globalMap.GetProvinceIds())
    {
        const auto* province = globalMap.FindBuildableProvince(provinceId);
        const auto* simulation = province != nullptr ? province->GetSimulation() : nullptr;
        if (simulation == nullptr)
            continue;
        for (Building* building : simulation->GetEconomy().dataTracker.buildings)
        {
            if (building == nullptr)
                continue;
            for (const auto& view : building->GetInputBufferViews())
                total += std::max(0, view.amount);
            for (const auto& view : building->GetOutputBufferViews())
                total += std::max(0, view.amount);
        }
    }
    return total;
}
