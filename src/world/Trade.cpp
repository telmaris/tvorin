#include "world/Trade.h"

#include "economy/StockpileIndex.h"
#include "world/GlobalMap.h"
#include "world/ProvinceConnection.h"

#include <algorithm>
#include <cmath>
#include <limits>

int TradeInventory::Get(ResourceType type) const
{
    const auto it = amounts.find(type);
    return it == amounts.end() ? 0 : it->second;
}

int TradeInventory::Capacity(ResourceType type) const
{
    const auto it = capacities.find(type);
    return it == capacities.end() ? std::numeric_limits<int>::max() : std::max(0, it->second);
}

bool TradeInventory::CanReceive(ResourceType type, int amount) const
{
    return type != ResourceType::Null && amount >= 0 &&
           Get(type) <= Capacity(type) && amount <= Capacity(type) - Get(type);
}

bool TradeInventory::TryConsume(ResourceType type, int amount)
{
    if (type == ResourceType::Null || amount < 0 || Get(type) < amount)
        return false;
    amounts[type] -= amount;
    return true;
}

bool TradeInventory::TryDeposit(ResourceType type, int amount)
{
    if (!CanReceive(type, amount))
        return false;
    amounts[type] += amount;
    return true;
}

int StockpileTradeInventory::Get(ResourceType type) const
{
    return StockpileIndex::GetTotal(economy, type);
}

int StockpileTradeInventory::Capacity(ResourceType type) const
{
    return StockpileIndex::GetCapacity(economy, type);
}

bool StockpileTradeInventory::CanReceive(ResourceType type, int amount) const
{
    const int stored = Get(type);
    const int capacity = Capacity(type);
    return type != ResourceType::Null && amount >= 0 && stored >= 0 &&
           stored <= capacity && amount <= capacity - stored;
}

bool StockpileTradeInventory::TryConsume(ResourceType type, int amount)
{
    if (type == ResourceType::Null || amount < 0 || Get(type) < amount)
        return false;
    const int consumed = StockpileIndex::Consume(economy, type, amount);
    if (consumed == amount)
        return true;
    if (consumed > 0)
        StockpileIndex::Deposit(economy, type, consumed);
    return false;
}

bool StockpileTradeInventory::TryDeposit(ResourceType type, int amount)
{
    if (!CanReceive(type, amount))
        return false;
    const int deposited = StockpileIndex::Deposit(economy, type, amount);
    if (deposited == amount)
        return true;
    if (deposited > 0)
        StockpileIndex::Consume(economy, type, deposited);
    return false;
}

namespace
{
    int ScorePriceModifier(int score)
    {
        return std::clamp(score, 0, 1000);
    }

    int CeilToInt(double value)
    {
        if (!std::isfinite(value) || value < 0.0 ||
            value >= static_cast<double>(std::numeric_limits<int>::max()))
            return std::numeric_limits<int>::max();
        return static_cast<int>(std::ceil(value));
    }
}

std::optional<TradeRoute> TradeRouteService::FindRoute(const GlobalMap& map,
                                                        ProvinceId originProvinceId,
                                                        ProvinceId cityProvinceId,
                                                        PlayerId playerId,
                                                        const TradeRouteRules& rules)
{
    if (originProvinceId == InvalidProvinceId || cityProvinceId == InvalidProvinceId ||
        playerId == InvalidPlayerId || rules.baseLegDurationTicks == 0 ||
        rules.baseIncidentChanceBasisPoints < 0 || rules.baseIncidentChanceBasisPoints > 10000 ||
        map.FindProvince(originProvinceId) == nullptr || map.FindProvince(cityProvinceId) == nullptr)
        return std::nullopt;

    TradeRoute result;
    if (!map.FindShortestPath(originProvinceId, cityProvinceId, result.path))
        return std::nullopt;
    if (rules.baseLegDurationTicks == 100)
    {
        const JourneyTimingQuote timing = JourneyTiming::QuoteForPath(
            map, originProvinceId, cityProvinceId, playerId, result.path,
            rules.speedProfile);
        if (!timing.valid)
            return std::nullopt;
        result.etaTicks = timing.totalDurationTicks;
        for (const auto& leg : timing.legs)
        {
            const int legRisk = std::max(
                0, rules.baseIncidentChanceBasisPoints - leg.incidentReductionBasisPoints);
            result.incidentChanceBasisPoints = std::min(
                10000, result.incidentChanceBasisPoints + legRisk);
        }
        return result;
    }
    for (const ProvinceConnectionId connectionId : result.path)
    {
        const auto* connection = map.FindConnection(connectionId);
        if (connection == nullptr)
            return std::nullopt;
        const RouteModifierContext context{originProvinceId, cityProvinceId, playerId};
        const RouteTraversalStats stats = connection->ResolveTraversalStats(context);
        if (!std::isfinite(stats.traversalTimeMultiplier) || stats.traversalTimeMultiplier <= 0.0)
            return std::nullopt;
        const double legTicks = static_cast<double>(rules.baseLegDurationTicks) *
                                stats.traversalTimeMultiplier;
        const auto roundedLegTicks = static_cast<std::uint64_t>(std::max(1.0, std::ceil(legTicks)));
        if (result.etaTicks > std::numeric_limits<std::uint64_t>::max() - roundedLegTicks)
            return std::nullopt;
        result.etaTicks += roundedLegTicks;
        const int legRisk = std::max(0, rules.baseIncidentChanceBasisPoints -
                                        stats.incidentChanceReductionBasisPoints);
        result.incidentChanceBasisPoints = std::min(
            10000, result.incidentChanceBasisPoints + legRisk);
    }
    return result;
}

TradeQuote TradePricingService::Calculate(const NeutralCityProvince& city,
                                          ProvinceId originProvinceId,
                                          ProvinceId cityProvinceId,
                                          PlayerId playerId,
                                          const TradeRequest& request)
{
    TradeQuote quote;
    quote.originProvinceId = originProvinceId;
    quote.cityProvinceId = cityProvinceId;
    quote.request = request;
    quote.tradeScore = ScorePriceModifier(city.GetState().GetTradeScore(playerId));
    quote.cityRevision = city.GetState().revision;
    quote.barterPenaltyMultiplier = city.GetState().barterPenaltyMultiplier;
    if (playerId == InvalidPlayerId || originProvinceId == InvalidProvinceId ||
        cityProvinceId == InvalidProvinceId || request.offerType == ResourceType::Null ||
        request.requestType == ResourceType::Null || request.offerType == request.requestType ||
        request.requestedAmount <= 0 || request.offeredAmount < 0 ||
        !std::isfinite(quote.barterPenaltyMultiplier) || quote.barterPenaltyMultiplier < 1.0)
    {
        quote.failure = "invalid trade request";
        return quote;
    }
    if (city.GetState().GetStock(request.requestType) < request.requestedAmount)
    {
        quote.failure = "city does not have enough requested stock";
        return quote;
    }

    quote.unitRequestPrice = city.GetState().GetSellPrice(request.requestType);
    quote.unitOfferPrice = city.GetState().GetBuyPrice(request.offerType);
    if (request.mode == TradeMode::Coin)
    {
        if (request.offerType != ResourceType::COINS || quote.unitRequestPrice <= 0.0)
        {
            quote.failure = "coin trade requires a priced city good";
            return quote;
        }
        const double scoreMultiplier = std::max(0.5, 1.0 - quote.tradeScore / 2000.0);
        quote.requiredOfferAmount = CeilToInt(
            request.requestedAmount * quote.unitRequestPrice * scoreMultiplier);
    }
    else
    {
        if (request.offerType == ResourceType::COINS || quote.unitOfferPrice <= 0.0 ||
            quote.unitRequestPrice <= 0.0)
        {
            quote.failure = "barter requires buy and sell prices for both goods";
            return quote;
        }
        const double scoreMultiplier = std::max(0.5, 1.0 - quote.tradeScore / 2000.0);
        quote.requiredOfferAmount = CeilToInt(
            request.requestedAmount * quote.unitRequestPrice /
            quote.unitOfferPrice * quote.barterPenaltyMultiplier * scoreMultiplier);
    }
    if (quote.requiredOfferAmount <= 0 || request.offeredAmount < quote.requiredOfferAmount)
    {
        quote.failure = "offered amount is below the quoted price";
        return quote;
    }
    quote.valid = true;
    return quote;
}

bool TradeService::StartOrder(NeutralCityProvince& city, ITradeInventory& playerInventory,
                              const TradeQuote& quote, std::uint64_t expectedCityRevision,
                              std::uint64_t orderId, PlayerId playerId, TradeOrder& outOrder)
{
    if (!quote.valid || quote.cityRevision != expectedCityRevision || orderId == 0 ||
        playerId == InvalidPlayerId || city.GetState().revision != expectedCityRevision ||
        playerInventory.Get(quote.request.offerType) < quote.requiredOfferAmount ||
        city.GetState().GetStock(quote.request.requestType) < quote.request.requestedAmount ||
        city.GetState().GetStock(quote.request.offerType) >
            std::numeric_limits<int>::max() - quote.requiredOfferAmount)
        return false;

    const int previousRequestStock = city.GetState().GetStock(quote.request.requestType);
    const int previousOfferStock = city.GetState().GetStock(quote.request.offerType);
    const std::uint64_t previousRevision = city.GetState().revision;
    if (!playerInventory.TryConsume(quote.request.offerType, quote.requiredOfferAmount))
        return false;
    auto& state = city.GetStateForAuthority();
    if (!state.SetStock(quote.request.requestType,
                        state.GetStock(quote.request.requestType) - quote.request.requestedAmount))
    {
        playerInventory.TryDeposit(quote.request.offerType, quote.requiredOfferAmount);
        state.RestoreRevision(previousRevision);
        return false;
    }
    if (!state.SetStock(quote.request.offerType,
                        state.GetStock(quote.request.offerType) + quote.requiredOfferAmount))
    {
        state.stock[quote.request.requestType] = previousRequestStock;
        state.stock[quote.request.offerType] = previousOfferStock;
        state.RestoreRevision(previousRevision);
        playerInventory.TryDeposit(quote.request.offerType, quote.requiredOfferAmount);
        return false;
    }

    outOrder.id = orderId;
    outOrder.playerId = playerId;
    outOrder.originProvinceId = quote.originProvinceId;
    outOrder.cityProvinceId = quote.cityProvinceId;
    outOrder.request = quote.request;
    outOrder.offeredAmount = quote.requiredOfferAmount;
    outOrder.cargoAmount = quote.request.requestedAmount;
    outOrder.cityRevisionAtStart = quote.cityRevision;
    outOrder.journeyId = InvalidWorldJourneyId;
    outOrder.status = TradeOrderStatus::InTransit;
    return true;
}

bool TradeService::CompleteOrder(NeutralCityProvince& city, ITradeInventory& playerInventory,
                                  TradeOrder& order, int scoreGain)
{
    if ((order.status != TradeOrderStatus::InTransit &&
         order.status != TradeOrderStatus::AwaitingUnload) ||
        order.cargoAmount <= 0 || scoreGain < 0 ||
        order.request.requestType == ResourceType::Null ||
        !playerInventory.CanReceive(order.request.requestType, order.cargoAmount))
    {
        if (order.status == TradeOrderStatus::InTransit)
            order.status = TradeOrderStatus::AwaitingUnload;
        return false;
    }
    if (!playerInventory.TryDeposit(order.request.requestType, order.cargoAmount))
        return false;
    auto& state = city.GetStateForAuthority();
    if (!state.AddTradeScore(order.playerId, scoreGain))
    {
        playerInventory.TryConsume(order.request.requestType, order.cargoAmount);
        return false;
    }
    order.cargoAmount = 0;
    order.status = TradeOrderStatus::Succeeded;
    return true;
}

bool TradeService::RefundOrder(NeutralCityProvince& city, ITradeInventory& playerInventory,
                               TradeOrder& order)
{
    if ((order.status != TradeOrderStatus::InTransit &&
         order.status != TradeOrderStatus::AwaitingUnload) ||
        order.offeredAmount <= 0 || order.cargoAmount <= 0 ||
        order.request.offerType == ResourceType::Null ||
        order.request.requestType == ResourceType::Null ||
        order.request.offerType == order.request.requestType ||
        !playerInventory.CanReceive(order.request.offerType, order.offeredAmount))
        return false;

    const auto& stateBefore = city.GetState();
    const int cityOfferStock = stateBefore.GetStock(order.request.offerType);
    const int cityRequestStock = stateBefore.GetStock(order.request.requestType);
    if (cityOfferStock < order.offeredAmount ||
        cityRequestStock > std::numeric_limits<int>::max() - order.cargoAmount)
        return false;

    const std::uint64_t previousRevision = stateBefore.revision;
    if (!playerInventory.TryDeposit(order.request.offerType, order.offeredAmount))
        return false;
    auto& state = city.GetStateForAuthority();
    if (!state.SetStock(order.request.offerType, cityOfferStock - order.offeredAmount) ||
        !state.SetStock(order.request.requestType, cityRequestStock + order.cargoAmount))
    {
        playerInventory.TryConsume(order.request.offerType, order.offeredAmount);
        state.stock[order.request.offerType] = cityOfferStock;
        state.stock[order.request.requestType] = cityRequestStock;
        state.RestoreRevision(previousRevision);
        return false;
    }
    order.cargoAmount = 0;
    order.status = TradeOrderStatus::Cancelled;
    return true;
}

bool TradeService::BuildJourney(const TradeOrder& order, const TradeQuote& quote,
                                WorldJourney& outJourney)
{
    if (order.id == 0 || order.journeyId != InvalidWorldJourneyId ||
        order.status != TradeOrderStatus::InTransit || order.playerId == InvalidPlayerId ||
        order.originProvinceId == InvalidProvinceId || order.cityProvinceId == InvalidProvinceId ||
        order.cargoAmount <= 0 || !quote.valid || quote.route.path.empty() ||
        quote.originProvinceId != order.originProvinceId ||
        quote.cityProvinceId != order.cityProvinceId ||
        quote.request.requestType != order.request.requestType ||
        quote.request.offerType != order.request.offerType)
        return false;
    outJourney = WorldJourney{};
    outJourney.ownerId = order.playerId;
    outJourney.sourceProvinceId = order.originProvinceId;
    outJourney.targetProvinceId = order.cityProvinceId;
    outJourney.kind = WorldJourneyKind::Trade;
    outJourney.legPlan.reserve(quote.route.path.size());
    for (const ProvinceConnectionId connectionId : quote.route.path)
        outJourney.legPlan.push_back({connectionId});
    outJourney.payload = TradeCargo{order.request.offerType, order.request.requestType,
                                    order.cargoAmount};
    return true;
}
