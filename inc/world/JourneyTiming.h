#ifndef WORLD_JOURNEY_TIMING_H
#define WORLD_JOURNEY_TIMING_H

#include "world/GlobalMap.h"

#include <cstdint>
#include <string>
#include <vector>

// One fixed-point contract for all campaign journeys.  Values are basis
// points so the simulation never depends on a per-operation floating-point
// formula or on the current route upgrade state after departure.
struct JourneySpeedProfile
{
    int baseUnitsPerMinute{100};
    int moverSpeedBasisPoints{10000};
    int playerRouteSpeedBasisPoints{10000};
    int operationSpeedBasisPoints{10000};
    int extraLegDistanceBasisPoints{11000};
};

struct JourneyLegPlan
{
    ProvinceConnectionId connectionId{InvalidProvinceConnectionId};
    int lengthUnits{0};
    int routeTimeBasisPoints{10000};
    int routeLevelAtStart{0};
    int incidentReductionBasisPoints{0};
    std::uint64_t durationTicks{0};
};

struct JourneyTimingQuote
{
    bool valid{false};
    std::string failureReason;
    int physicalDistanceUnits{0};
    int effectiveDistanceUnits{0};
    std::uint64_t totalDurationTicks{0};
    std::vector<JourneyLegPlan> legs;
};

class JourneyTiming
{
public:
    static constexpr int TicksPerSecond = 100;
    static constexpr int SecondsPerMinute = 60;
    static constexpr int BasisPoints = 10000;

    static JourneyTimingQuote Quote(const GlobalMap& map,
                                    ProvinceId sourceProvinceId,
                                    ProvinceId targetProvinceId,
                                    PlayerId playerId,
                                    const JourneySpeedProfile& profile = {});

    static JourneyTimingQuote QuoteForPath(
        const GlobalMap& map,
        ProvinceId sourceProvinceId,
        ProvinceId targetProvinceId,
        PlayerId playerId,
        const std::vector<ProvinceConnectionId>& path,
        const JourneySpeedProfile& profile = {});

    // Exposed for data-driven tests and other deterministic fixed-point
    // systems which need the same overflow-safe rounding rule.
    static bool MulDivCeil(std::uint64_t value, std::uint64_t multiplier,
                           std::uint64_t divisor, std::uint64_t& result);
};

#endif
