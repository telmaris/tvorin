#ifndef WORLD_COLONIZATION_SERVICE_H
#define WORLD_COLONIZATION_SERVICE_H

#include "world/ColonizationDefinition.h"
#include "world/JourneyTiming.h"
#include "world/WorldIds.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class GlobalMap;
class Player;
struct ProvinceEconomy;

struct ColonizationQuote
{
    bool allowed{false};
    std::string reason;
    ProvinceId sourceProvinceId{InvalidProvinceId};
    ProvinceId targetProvinceId{InvalidProvinceId};
    std::vector<ColonizationCost> costs;
    JourneyTimingQuote travel;
    std::uint64_t settlementDurationTicks{0};
    std::uint64_t totalDurationTicks{0};
};

ColonizationQuote BuildColonizationQuote(
    const GlobalMap& map, const Player& player, const ProvinceEconomy& sourceEconomy,
    ProvinceId sourceProvinceId, ProvinceId targetProvinceId,
    const ColonizationDefinition& definition, bool targetHasPendingOperation,
    std::size_t activeOperationCount, std::size_t maxOperationCount);

#endif
