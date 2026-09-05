#include "core/GameCommand.h"
#include "core/GameSession.h"
#include "core/GameWorld.h"
#include "economy/StockpileIndex.h"
#include "world/GlobalMap.h"
#include "world/Trade.h"

#include <gtest/gtest.h>


#include <algorithm>
#include <limits>

namespace
{
    NeutralCityProvince* FindCity(GlobalMap& map)
    {
        for (const ProvinceId id : map.GetProvinceIds())
            if (auto* city = dynamic_cast<NeutralCityProvince*>(map.FindProvince(id)); city != nullptr)
                return city;
        return nullptr;
    }
}

TEST(TradeTests, GeneratedCityHasDataDrivenStateAndDeterministicRoute)
{
    GlobalMapGenerationParameters parameters;
    parameters.seed = 7117;
    parameters.provinceCount = 16;
    parameters.extraEdgeCount = 4;
    auto first = GlobalMapGenerator::Generate(parameters, 1);
    auto second = GlobalMapGenerator::Generate(parameters, 1);
    ASSERT_TRUE(first.success);
    ASSERT_TRUE(second.success);

    auto* firstCity = FindCity(first.map);
    auto* secondCity = FindCity(second.map);
    ASSERT_NE(firstCity, nullptr);
    ASSERT_NE(secondCity, nullptr);
    EXPECT_EQ(firstCity->GetState().stock, secondCity->GetState().stock);
    EXPECT_EQ(firstCity->GetState().sellPrices, secondCity->GetState().sellPrices);
    EXPECT_EQ(firstCity->GetState().buyPrices, secondCity->GetState().buyPrices);
    EXPECT_EQ(firstCity->GetState().barterPenaltyMultiplier, 1.25);

    const ProvinceId home = first.homeProvinceByPlayer.at(0);
    const auto route = TradeRouteService::FindRoute(
        first.map, home, firstCity->GetId(), 0);
    ASSERT_TRUE(route.has_value());
    EXPECT_EQ(route->path.size(),
              TradeRouteService::FindRoute(second.map, second.homeProvinceByPlayer.at(0),
                                            secondCity->GetId(), 0)->path.size());
    EXPECT_GT(route->etaTicks, 0u);
}

TEST(TradeTests, CoinAndBarterOrdersUseRevisionAndNeverDuplicateCargo)
{
    GlobalMapGenerationParameters parameters;
    parameters.seed = 9913;
    parameters.provinceCount = 16;
    parameters.extraEdgeCount = 4;
    auto generated = GlobalMapGenerator::Generate(parameters, 1);
    ASSERT_TRUE(generated.success);
    auto* city = FindCity(generated.map);
    ASSERT_NE(city, nullptr);
    ASSERT_TRUE(city->GetStateForAuthority().SetStock(ResourceType::WOOD, 10));
    ASSERT_TRUE(city->GetStateForAuthority().SetSellPrice(ResourceType::WOOD, 2.0));

    TradeInventory inventory;
    inventory.amounts[ResourceType::COINS] = 50;
    inventory.capacities[ResourceType::WOOD] = 0;
    const TradeRequest coinRequest{ResourceType::COINS, ResourceType::WOOD, 1, 10,
                                   TradeMode::Coin};
    const TradeQuote quote = TradePricingService::Calculate(
        *city, generated.homeProvinceByPlayer.at(0), city->GetId(), 0, coinRequest);
    ASSERT_TRUE(quote.valid) << quote.failure;
    const auto staleRevision = quote.cityRevision;
    ASSERT_TRUE(city->GetStateForAuthority().AddTradeScore(0, 1));
    TradeOrder order;
    EXPECT_FALSE(TradeService::StartOrder(*city, inventory, quote, staleRevision, 1, 0, order));

    const TradeQuote refreshed = TradePricingService::Calculate(
        *city, generated.homeProvinceByPlayer.at(0), city->GetId(), 0, coinRequest);
    ASSERT_TRUE(refreshed.valid) << refreshed.failure;
    ASSERT_TRUE(TradeService::StartOrder(*city, inventory, refreshed, refreshed.cityRevision,
                                          1, 0, order));
    EXPECT_EQ(inventory.Get(ResourceType::COINS), 50 - refreshed.requiredOfferAmount);
    EXPECT_FALSE(TradeService::CompleteOrder(*city, inventory, order));
    EXPECT_EQ(order.status, TradeOrderStatus::AwaitingUnload);
    EXPECT_EQ(inventory.Get(ResourceType::WOOD), 0);
    inventory.capacities[ResourceType::WOOD] = 2;
    ASSERT_TRUE(TradeService::CompleteOrder(*city, inventory, order));
    EXPECT_EQ(order.status, TradeOrderStatus::Succeeded);
    EXPECT_EQ(inventory.Get(ResourceType::WOOD), 1);
    EXPECT_FALSE(TradeService::CompleteOrder(*city, inventory, order));
}

TEST(TradeTests, BarterRequiresBothDataDrivenPrices)
{
    NeutralCityProvince city{7, {0, 0}, "neutral_city"};
    city.GetStateForAuthority().SetStock(ResourceType::BREAD, 10);
    city.GetStateForAuthority().SetSellPrice(ResourceType::BREAD, 2.0);
    city.GetStateForAuthority().SetBuyPrice(ResourceType::WOOD, 1.0);
    const TradeRequest request{ResourceType::WOOD, ResourceType::BREAD, 2, 10,
                               TradeMode::Barter};
    const TradeQuote quote = TradePricingService::Calculate(city, 1, 7, 0, request);
    ASSERT_TRUE(quote.valid) << quote.failure;
    EXPECT_EQ(quote.requiredOfferAmount, 5);
}

TEST(TradeTests, StartOrderRejectsCityStockOverflowWithoutMutatingEitherLedger)
{
    NeutralCityProvince city{7, {0, 0}, "neutral_city"};
    ASSERT_TRUE(city.GetStateForAuthority().SetStock(ResourceType::WOOD, 10));
    ASSERT_TRUE(city.GetStateForAuthority().SetStock(
        ResourceType::COINS, std::numeric_limits<int>::max()));
    ASSERT_TRUE(city.GetStateForAuthority().SetSellPrice(ResourceType::WOOD, 2.0));
    TradeInventory inventory;
    inventory.amounts[ResourceType::COINS] = 50;
    const TradeRequest request{ResourceType::COINS, ResourceType::WOOD, 1, 10,
                               TradeMode::Coin};
    const TradeQuote quote = TradePricingService::Calculate(city, 1, 7, 0, request);
    ASSERT_TRUE(quote.valid);
    const auto cityBefore = city.GetState();

    TradeOrder order;
    EXPECT_FALSE(TradeService::StartOrder(
        city, inventory, quote, quote.cityRevision, 1, 0, order));
    EXPECT_EQ(inventory.Get(ResourceType::COINS), 50);
    EXPECT_EQ(city.GetState().stock, cityBefore.stock);
    EXPECT_EQ(city.GetState().revision, cityBefore.revision);
}

TEST(TradeTests, AcceptedOrderBuildsTheCommonTradeJourneyPayload)
{
    GlobalMapGenerationParameters parameters;
    parameters.seed = 9914;
    parameters.provinceCount = 16;
    parameters.extraEdgeCount = 4;
    auto generated = GlobalMapGenerator::Generate(parameters, 1);
    ASSERT_TRUE(generated.success);
    auto* city = FindCity(generated.map);
    ASSERT_NE(city, nullptr);
    ASSERT_TRUE(city->GetStateForAuthority().SetStock(ResourceType::WOOD, 10));
    ASSERT_TRUE(city->GetStateForAuthority().SetSellPrice(ResourceType::WOOD, 2.0));

    TradeInventory inventory;
    inventory.amounts[ResourceType::COINS] = 50;
    const ProvinceId origin = generated.homeProvinceByPlayer.at(0);
    const TradeRequest request{ResourceType::COINS, ResourceType::WOOD, 1, 10,
                               TradeMode::Coin};
    TradeQuote quote = TradePricingService::Calculate(
        *city, origin, city->GetId(), 0, request);
    ASSERT_TRUE(quote.valid) << quote.failure;
    quote.route = *TradeRouteService::FindRoute(generated.map, origin, city->GetId(), 0);

    TradeOrder order;
    ASSERT_TRUE(TradeService::StartOrder(*city, inventory, quote, quote.cityRevision,
                                         9, 0, order));
    WorldJourney journey;
    ASSERT_TRUE(TradeService::BuildJourney(order, quote, journey));
    EXPECT_EQ(journey.id, InvalidWorldJourneyId);
    EXPECT_EQ(journey.sourceProvinceId, origin);
    EXPECT_EQ(journey.targetProvinceId, city->GetId());
    ASSERT_TRUE(std::holds_alternative<TradeCargo>(journey.payload));
    EXPECT_EQ(std::get<TradeCargo>(journey.payload).amount, 1);
}

TEST(TradeTests, StartTradeCommandUsesCommonJourneyAndRoundTripsActiveLedger)
{
    MapParameters parameters;
    parameters.sizePreset = MapSizePreset::S;
    parameters.seed = 9915;
    parameters.debugMode = true;

    GameWorld world;
    ASSERT_TRUE(world.InitWorld("authoritative-trade", nullptr, parameters));
    Player* player = world.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(player, nullptr);
    auto* source = world.GetGlobalMap().FindBuildableProvince(player->homeProvinceId);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(source->GetSimulation(), nullptr);
    NeutralCityProvince* city = FindCity(world.GetGlobalMap());
    ASSERT_NE(city, nullptr);
    ASSERT_TRUE(world.GetGlobalMap().SetKnowledge(
        player->id, city->GetId(), ProvinceKnowledgeLevel::Scouted));
    ASSERT_TRUE(city->GetStateForAuthority().SetStock(ResourceType::STONE, 10));
    ASSERT_TRUE(city->GetStateForAuthority().SetSellPrice(ResourceType::STONE, 2.0));

    const int coinsBefore = StockpileIndex::GetTotal(
        source->GetSimulation()->GetEconomy(), ResourceType::COINS);
    ASSERT_GE(coinsBefore, 10);
    ASSERT_EQ(StockpileIndex::Consume(source->GetSimulation()->GetEconomy(),
                                      ResourceType::STONE, 1), 1);
    const TradeRequest request{ResourceType::COINS, ResourceType::STONE, 1, 10,
                               TradeMode::Coin};
    const TradeQuote quote = TradePricingService::Calculate(
        *city, source->GetId(), city->GetId(), player->id, request);
    ASSERT_TRUE(quote.valid) << quote.failure;
    const auto command = GameCommand::StartTrade(
        player->id, source->GetId(), city->GetId(), request, quote.cityRevision);
    GameCommand parsed;
    ASSERT_TRUE(GameCommand::TryDeserialize(command.Serialize(), parsed));
    EXPECT_EQ(parsed.type, GameCommandType::StartTrade);
    EXPECT_EQ(parsed.provinceId, source->GetId());
    EXPECT_EQ(parsed.targetProvinceId, city->GetId());
    EXPECT_EQ(parsed.tradeOfferType, ResourceType::COINS);
    EXPECT_EQ(parsed.tradeRequestType, ResourceType::STONE);
    EXPECT_EQ(parsed.tradeRequestedAmount, 1);
    EXPECT_EQ(parsed.tradeOfferedAmount, 10);
    EXPECT_EQ(parsed.expectedCityRevision, quote.cityRevision);

    const auto commandId = world.SubmitCommand(command);
    world.UpdateSimulation(FixedSimulationClock::FixedDt);
    const auto results = world.ConsumeCommandResults();
    const auto resultIt = std::find_if(results.begin(), results.end(),
        [commandId](const GameCommandResult& result) { return result.commandId == commandId; });
    ASSERT_NE(resultIt, results.end());
    ASSERT_TRUE(resultIt->accepted);
    ASSERT_EQ(world.GetActiveTradeOrders().size(), 1u);
    EXPECT_EQ(StockpileIndex::GetTotal(source->GetSimulation()->GetEconomy(),
                                       ResourceType::COINS),
              coinsBefore - world.GetActiveTradeOrders().begin()->second.offeredAmount);

    const std::string payload = world.SerializeSimulationState();
    ASSERT_FALSE(payload.empty());
    GameWorld restored;
    ASSERT_TRUE(restored.RestoreSimulationState(payload));
    EXPECT_EQ(restored.BuildChecksum(), world.BuildChecksum());
    ASSERT_EQ(restored.GetActiveTradeOrders().size(), 1u);
    EXPECT_EQ(restored.GetActiveTradeOrders().begin()->second.request.requestType,
              ResourceType::STONE);

    // The route can be many minutes long under canonical physical-distance
    // timing; completion is covered by the fixed-tick journey tests above.
    EXPECT_FALSE(world.GetActiveTradeOrders().empty());
    EXPECT_EQ(restored.GetActiveTradeOrders().size(), 1u);
}
