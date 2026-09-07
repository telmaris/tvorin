#ifndef WORLD_EXPEDITION_H
#define WORLD_EXPEDITION_H

#include "world/WorldIds.h"
#include "data/StrategicResource.h"
#include "economy/BalanceStats.h"
#include "warfare/UnitDefinition.h"
#include "data/Resource.h"

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

struct OperationModifier
{
    BalanceStat stat{BalanceStat::RouteTravelSpeed};
    int multiplierBasisPoints{10000};
    std::string source;
};

struct ExpeditionLoadout
{
    std::vector<ResourceAmount> resources;
    std::vector<ResourceAmount> minimumResources;
    int supplyRatioBasisPoints{10000};
    std::vector<OperationModifier> modifiers;

    int Get(ResourceType type) const
    {
        for (const auto& entry : resources)
            if (entry.type == type)
                return entry.amount;
        return 0;
    }

    int GetMinimum(ResourceType type) const
    {
        for (const auto& entry : minimumResources)
            if (entry.type == type)
                return entry.amount;
        return 0;
    }

    bool IsSortedUnique() const
    {
        const auto valid = [](const std::vector<ResourceAmount>& entries)
        {
            for (std::size_t index = 0; index < entries.size(); ++index)
            {
                if (entries[index].type == ResourceType::Null || entries[index].amount <= 0)
                    return false;
                if (index > 0 && static_cast<int>(entries[index - 1].type) >=
                    static_cast<int>(entries[index].type))
                    return false;
            }
            return true;
        };
        return valid(resources) && valid(minimumResources) &&
               supplyRatioBasisPoints >= 10000 && supplyRatioBasisPoints <= 20000;
    }
};

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
