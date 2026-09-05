#include "world/ColonizationService.h"

#include "economy/Player.h"
#include "economy/StockpileIndex.h"
#include "world/GlobalMap.h"
#include "world/Province.h"
#include "world/ProvinceSimulation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    int ToBasisPoints(double value)
    {
        if (!std::isfinite(value) || value <= 0.0 ||
            value * JourneyTiming::BasisPoints > std::numeric_limits<int>::max())
            return 0;
        return static_cast<int>(std::llround(value * JourneyTiming::BasisPoints));
    }

    bool AddWouldOverflow(std::uint64_t left, std::uint64_t right)
    {
        return right > std::numeric_limits<std::uint64_t>::max() - left;
    }
}

ColonizationQuote BuildColonizationQuote(
    const GlobalMap& map, const Player& player, const ProvinceEconomy& sourceEconomy,
    ProvinceId sourceProvinceId, ProvinceId targetProvinceId,
    const ColonizationDefinition& definition, bool targetHasPendingOperation,
    std::size_t activeOperationCount, std::size_t maxOperationCount)
{
    ColonizationQuote quote;
    quote.sourceProvinceId = sourceProvinceId;
    quote.targetProvinceId = targetProvinceId;
    quote.costs = definition.cost;

    const auto* source = map.FindBuildableProvince(sourceProvinceId);
    const auto* target = map.FindBuildableProvince(targetProvinceId);
    if (source == nullptr || target == nullptr || sourceProvinceId == targetProvinceId)
    {
        quote.reason = "invalid colonization endpoints";
        return quote;
    }
    if (source->GetOwnerId() != player.id)
    {
        quote.reason = "source province is not owned by the player";
        return quote;
    }
    if (target->GetOwnerId() != InvalidPlayerId || target->GetSimulation() != nullptr)
    {
        quote.reason = "target province is not an empty buildable province";
        return quote;
    }
    if (target->GetKnowledge(player.id) < ProvinceKnowledgeLevel::Scouted)
    {
        quote.reason = "target province has not been scouted";
        return quote;
    }
    if (targetHasPendingOperation)
    {
        quote.reason = "target province already has a colonization operation";
        return quote;
    }
    if (activeOperationCount >= maxOperationCount)
    {
        quote.reason = "colonization operation limit reached";
        return quote;
    }
    if (definition.durationTicks == 0 || definition.cost.empty())
    {
        quote.reason = "colonization definition is invalid";
        return quote;
    }

    for (const auto& cost : definition.cost)
    {
        if (cost.type == ResourceType::Null || cost.amount <= 0 ||
            StockpileIndex::GetTotal(sourceEconomy, cost.type) < cost.amount)
        {
            quote.reason = "source province lacks colonization resources";
            return quote;
        }
    }

    std::vector<ProvinceConnectionId> path;
    if (!map.FindShortestPath(sourceProvinceId, targetProvinceId, path) || path.empty())
    {
        quote.reason = "no route to target province";
        return quote;
    }

    JourneySpeedProfile profile;
    profile.moverSpeedBasisPoints = JourneyTiming::BasisPoints;
    profile.playerRouteSpeedBasisPoints = ToBasisPoints(
        player.ModifyBalance(BalanceStat::RouteTravelSpeed, 1.0));
    if (profile.playerRouteSpeedBasisPoints == 0)
    {
        quote.reason = "player route speed modifier is invalid";
        return quote;
    }
    quote.travel = JourneyTiming::QuoteForPath(
        map, sourceProvinceId, targetProvinceId, player.id, path, profile);
    if (!quote.travel.valid)
    {
        quote.reason = quote.travel.failureReason;
        return quote;
    }

    const double modifiedSettlement = player.ModifyBalance(
        BalanceStat::ColonizationDuration,
        static_cast<double>(definition.durationTicks));
    if (!std::isfinite(modifiedSettlement) || modifiedSettlement <= 0.0 ||
        modifiedSettlement >= static_cast<double>(std::numeric_limits<std::uint64_t>::max()))
    {
        quote.reason = "colonization duration modifier is invalid";
        return quote;
    }
    quote.settlementDurationTicks = std::max<std::uint64_t>(
        1, static_cast<std::uint64_t>(std::ceil(modifiedSettlement)));
    if (AddWouldOverflow(quote.travel.totalDurationTicks, quote.settlementDurationTicks))
    {
        quote.reason = "colonization duration overflows simulation ticks";
        return quote;
    }
    quote.totalDurationTicks = quote.travel.totalDurationTicks + quote.settlementDurationTicks;
    quote.allowed = true;
    return quote;
}
