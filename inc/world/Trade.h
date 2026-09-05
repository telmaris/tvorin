#ifndef WORLD_TRADE_H
#define WORLD_TRADE_H

#include "data/Resource.h"
#include "world/Province.h"
#include "world/WorldIds.h"
#include "world/WorldJourney.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

class GlobalMap;
struct ProvinceEconomy;

enum class TradeMode : std::uint8_t
{
    Coin,
    Barter
};

struct TradeRequest
{
    ResourceType offerType{ResourceType::Null};
    ResourceType requestType{ResourceType::Null};
    int requestedAmount{0};
    int offeredAmount{0};
    TradeMode mode{TradeMode::Coin};
};

struct TradeRouteRules
{
    std::uint64_t baseLegDurationTicks{100};
    int baseIncidentChanceBasisPoints{500};
    JourneySpeedProfile speedProfile{};
};

struct TradeRoute
{
    std::vector<ProvinceConnectionId> path;
    std::uint64_t etaTicks{0};
    int incidentChanceBasisPoints{0};
};

struct TradeQuote
{
    bool valid{false};
    std::string failure;
    ProvinceId originProvinceId{InvalidProvinceId};
    ProvinceId cityProvinceId{InvalidProvinceId};
    TradeRequest request;
    int tradeScore{0};
    std::uint64_t cityRevision{0};
    double unitOfferPrice{0.0};
    double unitRequestPrice{0.0};
    double barterPenaltyMultiplier{1.0};
    int requiredOfferAmount{0};
    TradeRoute route;
};

class ITradeInventory
{
public:
    virtual ~ITradeInventory() = default;
    virtual int Get(ResourceType type) const = 0;
    virtual int Capacity(ResourceType type) const = 0;
    virtual bool CanReceive(ResourceType type, int amount) const = 0;
    virtual bool TryConsume(ResourceType type, int amount) = 0;
    virtual bool TryDeposit(ResourceType type, int amount) = 0;
};

// Value-backed implementation used by pure domain tests and tooling.
struct TradeInventory final : ITradeInventory
{
    std::map<ResourceType, int> amounts;
    std::map<ResourceType, int> capacities;

    int Get(ResourceType type) const override;
    int Capacity(ResourceType type) const override;
    bool CanReceive(ResourceType type, int amount) const override;
    bool TryConsume(ResourceType type, int amount) override;
    bool TryDeposit(ResourceType type, int amount) override;
};

// Canonical runtime adapter. TradeService commits directly against the
// province warehouses instead of mutating a detached inventory copy and then
// trying to repeat the same transaction in GameWorld.
class StockpileTradeInventory final : public ITradeInventory
{
public:
    explicit StockpileTradeInventory(ProvinceEconomy& economy) : economy(economy) {}

    int Get(ResourceType type) const override;
    int Capacity(ResourceType type) const override;
    bool CanReceive(ResourceType type, int amount) const override;
    bool TryConsume(ResourceType type, int amount) override;
    bool TryDeposit(ResourceType type, int amount) override;

private:
    ProvinceEconomy& economy;
};

enum class TradeOrderStatus : std::uint8_t
{
    Planned,
    InTransit,
    Succeeded,
    AwaitingUnload,
    Cancelled
};

struct TradeOrder
{
    std::uint64_t id{0};
    PlayerId playerId{InvalidPlayerId};
    ProvinceId originProvinceId{InvalidProvinceId};
    ProvinceId cityProvinceId{InvalidProvinceId};
    TradeRequest request;
    int offeredAmount{0};
    int cargoAmount{0};
    std::uint64_t cityRevisionAtStart{0};
    WorldJourneyId journeyId{InvalidWorldJourneyId};
    TradeOrderStatus status{TradeOrderStatus::Planned};
};

class TradeRouteService
{
public:
    static std::optional<TradeRoute> FindRoute(const GlobalMap& map,
                                               ProvinceId originProvinceId,
                                               ProvinceId cityProvinceId,
                                               PlayerId playerId,
                                               const TradeRouteRules& rules = {});
};

class TradePricingService
{
public:
    static TradeQuote Calculate(const NeutralCityProvince& city,
                                ProvinceId originProvinceId,
                                ProvinceId cityProvinceId,
                                PlayerId playerId,
                                const TradeRequest& request);
};

class TradeService
{
public:
    static bool StartOrder(NeutralCityProvince& city, ITradeInventory& playerInventory,
                           const TradeQuote& quote, std::uint64_t expectedCityRevision,
                           std::uint64_t orderId, PlayerId playerId, TradeOrder& outOrder);
    static bool CompleteOrder(NeutralCityProvince& city, ITradeInventory& playerInventory,
                              TradeOrder& order, int scoreGain = 1);
    // Returns both sides of an order when its common journey fails. The
    // operation is atomic from the caller's point of view and keeps the
    // requested cargo from disappearing or being duplicated.
    static bool RefundOrder(NeutralCityProvince& city, ITradeInventory& playerInventory,
                            TradeOrder& order);
    // Converts an accepted order and its quote into the common campaign
    // journey payload. Movement, ETA and ID assignment are handled only by
    // WorldJourneySystem.
    static bool BuildJourney(const TradeOrder& order, const TradeQuote& quote,
                             WorldJourney& outJourney);
};

#endif
