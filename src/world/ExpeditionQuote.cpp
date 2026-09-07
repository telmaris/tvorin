#include "world/ExpeditionQuote.h"

#include "economy/Player.h"
#include "warfare/BattleUnit.h"
#include "warfare/UnitDefinition.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace
{
    int SafeBasisPoints(double value)
    {
        if (!std::isfinite(value) || value <= 0.0)
            return 0;
        const double scaled = value * 10000.0;
        if (scaled > static_cast<double>(std::numeric_limits<int>::max()))
            return 0;
        return static_cast<int>(std::llround(scaled));
    }

    bool IsAllowedOperationStat(BalanceStat stat)
    {
        return stat == BalanceStat::RouteTravelSpeed ||
               stat == BalanceStat::BattleAttack ||
               stat == BalanceStat::ArmySupplyConsumptionReduction;
    }

    std::vector<ProvinceConnectionId> FindPath(const GlobalMap& map,
                                               ProvinceId source,
                                               ProvinceId target)
    {
        std::vector<ProvinceConnectionId> path;
        return map.FindShortestPath(source, target, path) ? path : std::vector<ProvinceConnectionId>{};
    }

    int UnitMoveSpeedBasisPoints(const Player* player, const BattleUnit& unit)
    {
        const double speed = player == nullptr
            ? (FindUnitDefinition(unit.unitDefId) == nullptr
                ? 0.0 : FindUnitDefinition(unit.unitDefId)->moveSpeed)
            : unit.GetEffectiveMoveSpeed(*player);
        return SafeBasisPoints(speed);
    }
}

bool ExpeditionQuoteService::ValidateLoadout(const ExpeditionLoadout& loadout,
                                             std::string& reason)
{
    if (!loadout.IsSortedUnique() || loadout.resources.size() > 16 ||
        loadout.minimumResources.size() > 16)
    {
        reason = "expedition loadout must be sorted, unique and within limits";
        return false;
    }
    for (const auto& minimum : loadout.minimumResources)
    {
        if (loadout.Get(minimum.type) < minimum.amount)
        {
            reason = "expedition loadout is below a required minimum";
            return false;
        }
    }
    for (const auto& entry : loadout.resources)
        if (entry.type != ResourceType::FOOD_PROVISIONS &&
            entry.amount != loadout.GetMinimum(entry.type))
        {
            reason = "only food may exceed or undershoot its expedition minimum";
            return false;
        }
    return true;
}

int ExpeditionQuoteService::CalculateMinimumFood(const UnitRoster& roster,
                                                 const std::vector<int>& unitInstanceIds,
                                                 int physicalDistanceUnits,
                                                 int reductionBasisPoints)
{
    std::int64_t rateMicro = 0;
    for (const int unitId : unitInstanceIds)
    {
        const BattleUnit* unit = roster.FindUnit(unitId);
        const auto* definition = unit == nullptr ? nullptr : FindUnitDefinition(unit->unitDefId);
        if (definition == nullptr || !std::isfinite(definition->expeditionFoodPer100Distance) ||
            definition->expeditionFoodPer100Distance < 0.0)
            continue;
        rateMicro += static_cast<std::int64_t>(std::llround(
            definition->expeditionFoodPer100Distance * 1'000'000.0));
    }
    const std::int64_t distance = std::max(0, physicalDistanceUnits);
    const std::int64_t reduction = std::clamp(reductionBasisPoints, 0, 20000);
    const std::int64_t numerator = rateMicro * distance * reduction;
    const std::int64_t denominator = 100'000'000LL * 10000LL;
    if (numerator <= 0)
        return 0;
    return static_cast<int>((numerator + denominator - 1) / denominator);
}

int ExpeditionQuoteService::CalculateSupplyRatioBasisPoints(int selectedFood,
                                                             int minimumFood)
{
    if (minimumFood <= 0)
        return 10000;
    const int capped = std::clamp(selectedFood, minimumFood, minimumFood * 2);
    return 10000 + static_cast<int>((static_cast<std::int64_t>(capped - minimumFood) * 10000) /
                                    minimumFood);
}

int ExpeditionQuoteService::CalculateBonusBasisPoints(int selectedFood, int minimumFood)
{
    if (minimumFood <= 0)
        return 0;
    const int capped = std::clamp(selectedFood, minimumFood, minimumFood * 2);
    return static_cast<int>((static_cast<std::int64_t>(capped - minimumFood) * 2500) /
                            minimumFood);
}

ExpeditionQuote ExpeditionQuoteService::Quote(const Player* player,
                                              ExpeditionRole role,
                                              PlayerId playerId,
                                              ProvinceId sourceProvinceId,
                                              ProvinceId targetProvinceId,
                                              const std::vector<int>& unitInstanceIds,
                                              const UnitRoster& roster,
                                              const GlobalMap& map,
                                              ExpeditionLoadout requested,
                                              bool autoMinimum)
{
    ExpeditionQuote quote;
    const auto path = FindPath(map, sourceProvinceId, targetProvinceId);
    if (path.empty())
    {
        quote.reason = "expedition target has no legal route";
        return quote;
    }
    if (unitInstanceIds.empty() || unitInstanceIds.size() > 32)
    {
        quote.reason = "expedition requires at least one unit";
        return quote;
    }

    JourneySpeedProfile profile;
    profile.playerRouteSpeedBasisPoints = player == nullptr
        ? 10000 : SafeBasisPoints(player->ModifyBalance(BalanceStat::RouteTravelSpeed, 1.0));
    profile.moverSpeedBasisPoints = std::numeric_limits<int>::max();
    int physicalDistance = 0;
    int scoutCount = 0;
    std::map<ResourceType, int> equipment;
    for (const int unitId : unitInstanceIds)
    {
        const BattleUnit* unit = roster.FindUnit(unitId);
        const auto* definition = unit == nullptr ? nullptr : FindUnitDefinition(unit->unitDefId);
        if (definition == nullptr)
        {
            quote.reason = "expedition contains an unknown unit";
            return quote;
        }
        const int speed = UnitMoveSpeedBasisPoints(player, *unit);
        if (speed <= 0)
        {
            quote.reason = "expedition contains a unit without positive move speed";
            return quote;
        }
        profile.moverSpeedBasisPoints = std::min(profile.moverSpeedBasisPoints, speed);
        if (definition->role == UnitRole::Scout)
            ++scoutCount;
        for (const auto& cost : definition->expeditionEquipment)
            equipment[cost.type] += cost.amount;
    }
    if (profile.moverSpeedBasisPoints == std::numeric_limits<int>::max())
        profile.moverSpeedBasisPoints = 10000;
    quote.travel = JourneyTiming::QuoteForPath(map, sourceProvinceId, targetProvinceId,
                                                playerId, path, profile);
    if (!quote.travel.valid)
    {
        quote.reason = quote.travel.failureReason;
        return quote;
    }
    physicalDistance = quote.travel.physicalDistanceUnits;
    quote.scoutEscortCount = scoutCount;

    const int reduction = player == nullptr ? 10000 :
        std::clamp(SafeBasisPoints(player->ModifyBalance(
            BalanceStat::ArmySupplyConsumptionReduction, 1.0)), 1, 20000);
    const int minimumFood = CalculateMinimumFood(roster, unitInstanceIds,
                                                 physicalDistance, reduction);
    std::vector<ResourceAmount> minimums;
    if (minimumFood > 0)
        minimums.push_back({ResourceType::FOOD_PROVISIONS, minimumFood});
    for (const auto& [type, amount] : equipment)
        if (type != ResourceType::Null && amount > 0)
            minimums.push_back({type, amount});
    std::sort(minimums.begin(), minimums.end(),
              [](const ResourceAmount& lhs, const ResourceAmount& rhs)
              { return static_cast<int>(lhs.type) < static_cast<int>(rhs.type); });
    quote.loadout.minimumResources = minimums;
    if (autoMinimum || requested.resources.empty())
        requested.resources = minimums;
    requested.minimumResources = minimums;
    const int selectedFood = std::max(minimumFood,
                                      requested.Get(ResourceType::FOOD_PROVISIONS));
    if (minimumFood > 0 && selectedFood > minimumFood * 2)
    {
        quote.reason = "food loadout cannot exceed twice the expedition minimum";
        return quote;
    }
    requested.supplyRatioBasisPoints = CalculateSupplyRatioBasisPoints(selectedFood, minimumFood);
    const int bonus = CalculateBonusBasisPoints(selectedFood, minimumFood);
    profile.operationSpeedBasisPoints = 10000 + bonus;
    requested.modifiers = {
        {BalanceStat::RouteTravelSpeed, profile.operationSpeedBasisPoints,
         "expedition.food"}
    };
    quote.travel = JourneyTiming::QuoteForPath(map, sourceProvinceId, targetProvinceId,
                                                playerId, path, profile);
    if (!quote.travel.valid)
    {
        quote.reason = quote.travel.failureReason;
        return quote;
    }
    quote.loadout = std::move(requested);
    quote.speedProfile = profile;
    std::string validation;
    if (!ValidateLoadout(quote.loadout, validation))
    {
        quote.reason = validation;
        return quote;
    }

    int survivalBp = 10000;
    for (const auto& leg : quote.travel.legs)
    {
        const auto* connection = map.FindConnection(leg.connectionId);
        const int length = connection == nullptr ? leg.lengthUnits : connection->GetLengthUnits();
        const auto risk = QuoteRouteIncidentRisk({250, length, 10000,
                                                  leg.incidentReductionBasisPoints,
                                                  10000, scoutCount});
        quote.legRiskBasisPoints.push_back(risk.negativeChanceBasisPoints);
        survivalBp = static_cast<int>((static_cast<std::int64_t>(survivalBp) *
                                       (10000 - risk.negativeChanceBasisPoints) + 5000) / 10000);
    }
    quote.negativeIncidentRiskBasisPoints = std::clamp(10000 - survivalBp, 0, 10000);
    quote.allowed = true;
    return quote;
}
