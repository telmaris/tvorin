#ifndef WORLD_EXPEDITION_H
#define WORLD_EXPEDITION_H

#include "world/WorldIds.h"
#include "data/StrategicResource.h"
#include "warfare/UnitDefinition.h"

#include <cstdint>
#include <string>

enum class ExpeditionRole : std::uint8_t
{
    Scout,
    Supply,
    Garrison
};

struct ExpeditionDefinition
{
    std::string id;
    ExpeditionRole role{ExpeditionRole::Scout};
    UnitRole requiredUnitRole{UnitRole::Scout};
    int requiredUnitCount{1};
    StrategicResourceType supplyResource{StrategicResourceType::SupplyPackages};
    int supplyCost{1};
    std::uint64_t durationTicks{1};

    bool IsValid() const
    {
        return !id.empty() && requiredUnitCount > 0 && supplyCost > 0 && durationTicks > 0;
    }
};

#endif
