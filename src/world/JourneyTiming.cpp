#include "world/JourneyTiming.h"

#include "core/PersistenceLimits.h"
#include "world/ProvinceConnection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace
{
    bool AddChecked(std::uint64_t left, std::uint64_t right, std::uint64_t& result)
    {
        if (right > std::numeric_limits<std::uint64_t>::max() - left)
            return false;
        result = left + right;
        return true;
    }

    bool ToBasisPoints(double multiplier, int& result)
    {
        if (!std::isfinite(multiplier) || multiplier <= 0.0)
            return false;
        const double scaled = multiplier * JourneyTiming::BasisPoints;
        if (scaled < 1.0 || scaled > std::numeric_limits<int>::max())
            return false;
        result = static_cast<int>(std::llround(scaled));
        return result > 0;
    }

    bool IsValidProfile(const JourneySpeedProfile& profile)
    {
        return profile.baseUnitsPerMinute > 0 &&
               profile.moverSpeedBasisPoints > 0 &&
               profile.playerRouteSpeedBasisPoints > 0 &&
               profile.operationSpeedBasisPoints > 0 &&
               profile.extraLegDistanceBasisPoints > 0;
    }

    bool OtherEnd(const IProvinceConnection& connection, ProvinceId current,
                  ProvinceId& next)
    {
        if (connection.GetFirstProvinceId() == current)
            next = connection.GetSecondProvinceId();
        else if (connection.GetSecondProvinceId() == current)
            next = connection.GetFirstProvinceId();
        else
            return false;
        return next != InvalidProvinceId;
    }

    bool QuoteDuration(std::uint64_t effectiveDistance,
                       const JourneySpeedProfile& profile,
                       std::uint64_t& result)
    {
        // distance * 6000 * 1'000'000'000'000 /
        // (baseUnits/minute * moverBP * playerBP * operationBP)
        std::uint64_t ticksNumerator = 0;
        if (!JourneyTiming::MulDivCeil(effectiveDistance, 6000, 1, ticksNumerator))
            return false;
        std::uint64_t denominator = static_cast<std::uint64_t>(profile.baseUnitsPerMinute);
        if (!JourneyTiming::MulDivCeil(denominator,
                                       static_cast<std::uint64_t>(profile.moverSpeedBasisPoints),
                                       1, denominator) ||
            !JourneyTiming::MulDivCeil(denominator,
                                       static_cast<std::uint64_t>(profile.playerRouteSpeedBasisPoints),
                                       1, denominator) ||
            !JourneyTiming::MulDivCeil(denominator,
                                       static_cast<std::uint64_t>(profile.operationSpeedBasisPoints),
                                       1, denominator))
            return false;
        std::uint64_t timeFactor = 1'000'000'000'000ull;
        const std::uint64_t common = std::gcd(timeFactor, denominator);
        timeFactor /= common;
        denominator /= common;
        if (!JourneyTiming::MulDivCeil(ticksNumerator, timeFactor, denominator, result))
            return false;
        return result > 0;
    }
}

bool JourneyTiming::MulDivCeil(std::uint64_t value, std::uint64_t multiplier,
                               std::uint64_t divisor, std::uint64_t& result)
{
    if (divisor == 0)
        return false;
    if (value == 0 || multiplier == 0)
    {
        result = 0;
        return true;
    }

    // Split the product before multiplying.  The remainder path is the only
    // one that can overflow for large campaign inputs, so check it explicitly.
    const std::uint64_t quotient = value / divisor;
    const std::uint64_t remainder = value % divisor;
    if (quotient > std::numeric_limits<std::uint64_t>::max() / multiplier)
        return false;
    const std::uint64_t whole = quotient * multiplier;
    if (remainder > std::numeric_limits<std::uint64_t>::max() / multiplier)
        return false;
    const std::uint64_t remainderProduct = remainder * multiplier;
    const std::uint64_t roundedRemainder =
        remainderProduct / divisor + (remainderProduct % divisor != 0 ? 1 : 0);
    if (whole > std::numeric_limits<std::uint64_t>::max() - roundedRemainder)
        return false;
    result = whole + roundedRemainder;
    return true;
}

JourneyTimingQuote JourneyTiming::Quote(const GlobalMap& map,
                                       ProvinceId sourceProvinceId,
                                       ProvinceId targetProvinceId,
                                       PlayerId playerId,
                                       const JourneySpeedProfile& profile)
{
    std::vector<ProvinceConnectionId> path;
    JourneyTimingQuote result;
    if (!map.FindShortestPath(sourceProvinceId, targetProvinceId, path))
    {
        result.failureReason = "no route between provinces";
        return result;
    }
    return QuoteForPath(map, sourceProvinceId, targetProvinceId, playerId, path, profile);
}

JourneyTimingQuote JourneyTiming::QuoteForPath(
    const GlobalMap& map, ProvinceId sourceProvinceId, ProvinceId targetProvinceId,
    PlayerId playerId, const std::vector<ProvinceConnectionId>& path,
    const JourneySpeedProfile& profile)
{
    JourneyTimingQuote result;
    if (sourceProvinceId == InvalidProvinceId || targetProvinceId == InvalidProvinceId ||
        sourceProvinceId == targetProvinceId || playerId == InvalidPlayerId ||
        path.empty() || path.size() > PersistenceLimits::MaxJourneyPathConnections ||
        !IsValidProfile(profile) || map.FindProvince(sourceProvinceId) == nullptr ||
        map.FindProvince(targetProvinceId) == nullptr)
    {
        result.failureReason = "invalid journey endpoints, path or speed profile";
        return result;
    }

    ProvinceId currentProvinceId = sourceProvinceId;
    std::uint64_t physicalDistance = 0;
    std::uint64_t routeDistance = 0;
    std::vector<std::uint64_t> legDistances;
    legDistances.reserve(path.size());
    for (const ProvinceConnectionId connectionId : path)
    {
        const auto* connection = map.FindConnection(connectionId);
        ProvinceId nextProvinceId = InvalidProvinceId;
        if (connection == nullptr || !OtherEnd(*connection, currentProvinceId, nextProvinceId) ||
            connection->GetLengthUnits() <= 0)
        {
            result.failureReason = "journey path contains an invalid connection";
            return result;
        }
        const RouteModifierContext context{sourceProvinceId, targetProvinceId, playerId};
        const RouteTraversalStats traversal = connection->ResolveTraversalStats(context);
        int routeTimeBasisPoints = 0;
        if (!ToBasisPoints(traversal.traversalTimeMultiplier, routeTimeBasisPoints))
        {
            result.failureReason = "journey route has an invalid speed multiplier";
            return result;
        }
        std::uint64_t legDistance = 0;
        if (!MulDivCeil(static_cast<std::uint64_t>(connection->GetLengthUnits()),
                        static_cast<std::uint64_t>(routeTimeBasisPoints), BasisPoints,
                        legDistance) || !AddChecked(physicalDistance,
                                                     static_cast<std::uint64_t>(connection->GetLengthUnits()),
                                                     physicalDistance) ||
            !AddChecked(routeDistance, legDistance, routeDistance))
        {
            result.failureReason = "journey distance overflows";
            return result;
        }
        JourneyLegPlan leg;
        leg.connectionId = connectionId;
        leg.lengthUnits = connection->GetLengthUnits();
        leg.routeTimeBasisPoints = routeTimeBasisPoints;
        leg.routeLevelAtStart = connection->GetLevel();
        leg.incidentReductionBasisPoints = std::clamp(
            traversal.incidentChanceReductionBasisPoints, 0, BasisPoints);
        result.legs.push_back(leg);
        legDistances.push_back(legDistance);
        currentProvinceId = nextProvinceId;
    }
    if (currentProvinceId != targetProvinceId)
    {
        result.failureReason = "journey path does not reach target";
        return result;
    }

    std::uint64_t effectiveDistance = routeDistance;
    for (std::size_t index = 1; index < path.size(); ++index)
        if (!MulDivCeil(effectiveDistance,
                        static_cast<std::uint64_t>(profile.extraLegDistanceBasisPoints),
                        BasisPoints, effectiveDistance))
        {
            result.failureReason = "journey leg penalty overflows";
            return result;
        }

    std::uint64_t totalDuration = 0;
    if (!QuoteDuration(effectiveDistance, profile, totalDuration))
    {
        result.failureReason = "journey duration overflows";
        return result;
    }
    if (totalDuration < result.legs.size())
    {
        result.failureReason = "journey duration is shorter than its legs";
        return result;
    }

    std::uint64_t assignedTicks = 0;
    std::uint64_t assignedDistance = 0;
    for (std::size_t index = 0; index < result.legs.size(); ++index)
    {
        assignedDistance += legDistances[index];
        std::uint64_t deadline = totalDuration;
        if (index + 1 < result.legs.size() &&
            !MulDivCeil(totalDuration, assignedDistance, routeDistance, deadline))
        {
            result.failureReason = "journey leg schedule overflows";
            return result;
        }
        const std::uint64_t duration = index + 1 == result.legs.size()
            ? totalDuration - assignedTicks
            : deadline > assignedTicks ? deadline - assignedTicks : 0;
        if (duration == 0 || totalDuration - assignedTicks < duration)
        {
            result.failureReason = "journey leg schedule contains a zero duration";
            return result;
        }
        result.legs[index].durationTicks = duration;
        assignedTicks += duration;
    }

    result.valid = true;
    result.physicalDistanceUnits = physicalDistance > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max() : static_cast<int>(physicalDistance);
    result.effectiveDistanceUnits = effectiveDistance > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max() : static_cast<int>(effectiveDistance);
    result.totalDurationTicks = totalDuration;
    return result;
}
