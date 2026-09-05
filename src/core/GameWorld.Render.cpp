#include "core/GameWorldInternal.h"
#include "core/RoadTopology.h"
#include "core/VisibleTileBounds.h"
#include "economy/BuildingConfig.h"
#include "economy/StockpileIndex.h"
#include "ui/BuildingPresentation.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <vector>

using namespace GameWorldInternal;

namespace
{
    float GetRoadUtilization(const Building& building)
    {
        const auto* road = building.GetComponent<RoadComponent>();
        if (road == nullptr)
            return 0.0f;

        // Quantization prevents an EMA-changing float from dirtying every
        // road tile in every snapshot tick.
        constexpr float QuantizationSteps = 32.0f;
        const float trend = static_cast<float>(road->GetTrafficUtilizationTrend());
        return std::round(std::clamp(trend, 0.0f, 1.0f) * QuantizationSteps) / QuantizationSteps;
    }

    int GetRoadConnectionMask(const TileMap& tilemap, int x, int y)
    {
        return RoadTopology::GetCardinalMask(x, y, tilemap.params.sizeX, tilemap.params.sizeY,
            [&](int checkX, int checkY)
            {
                const auto* neighbour = tilemap.tilemap[checkY * tilemap.params.sizeX + checkX].GetBuilding();
                return neighbour != nullptr && IsRoadLike(neighbour->buildingType);
            });
    }

    bool IsRoadRecentlySaturated(const Building& building)
    {
        const auto* road = building.GetComponent<RoadComponent>();
        return road != nullptr && road->HasRecentSaturation();
    }

    bool IsBuildingRoadDisconnected(Building& building)
    {
        const auto* logistics = building.GetComponent<LogisticsComponent>();
        return logistics != nullptr && !logistics->IsConnectedToRoadNetwork(building);
    }

    void DrawRoadUtilizationOverlay(Vec2f position, float utilization,
                                    bool left, bool right, bool up, bool down)
    {
        utilization = std::clamp(utilization, 0.0f, 1.0f);
        if (utilization <= 0.01f)
            return;

        Color color{};
        if (utilization < 0.55f)
        {
            float t = utilization / 0.55f;
            color = Color{static_cast<unsigned char>(68.0f + t * 150.0f),
                          static_cast<unsigned char>(172.0f + t * 18.0f), 102, 255};
        }
        else
        {
            float t = (utilization - 0.55f) / 0.45f;
            color = Color{218, static_cast<unsigned char>(190.0f - t * 112.0f),
                          static_cast<unsigned char>(88.0f - t * 30.0f), 255};
        }
        const float top = RENDER_HEIGHT - position.y - TILE_SIZE;
        const Vector2 center{position.x + TILE_SIZE * 0.5f, top + TILE_SIZE * 0.5f};
        const float thickness = 10.0f + utilization * 8.0f;
        const float bloomScale = IsLocalLightBloomPreferenceEnabled() ? 1.25f : 1.0f;
        const Color outerGlow{color.r, color.g, color.b,
                              static_cast<unsigned char>(11.0f + utilization * 15.0f)};
        const Color innerGlow{color.r, color.g, color.b,
                              static_cast<unsigned char>(22.0f + utilization * 20.0f)};
        const Color fill{color.r, color.g, color.b,
                         static_cast<unsigned char>(58.0f + utilization * 42.0f)};
        BeginBlendMode(BLEND_ADDITIVE);
        const auto drawSegment = [&](Vector2 end)
        {
            DrawLineEx(center, end, thickness * 5.0f * bloomScale, outerGlow);
            DrawLineEx(center, end, thickness * 2.5f, innerGlow);
            DrawLineEx(center, end, thickness, fill);
        };

        if (left)  drawSegment({position.x, center.y});
        if (right) drawSegment({position.x + TILE_SIZE, center.y});
        // Map Y grows upward while framebuffer Y grows downward.
        if (up)    drawSegment({center.x, top + TILE_SIZE});
        if (down)  drawSegment({center.x, top});
        DrawCircleV(center, thickness * 2.50f * bloomScale, outerGlow);
        DrawCircleV(center, thickness * 1.25f, innerGlow);
        if (!left && !right && !up && !down)
            DrawCircleV(center, thickness * 0.55f, fill);
        else
            DrawCircleV(center, thickness * 0.50f, fill);
        EndBlendMode();
    }

    void DrawRoadSaturationIndicator(Vec2f position, bool saturated)
    {
        if (!saturated)
            return;
        const float top = RENDER_HEIGHT - position.y - TILE_SIZE;
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 6.0f);
        const Vector2 center{position.x + TILE_SIZE - 6.0f, top + 6.0f};
        DrawCircleV(center, 3.5f + pulse * 1.0f, Color{232, 91, 48, 255});
        DrawCircleV(center, 1.5f, Color{255, 222, 128, 255});
    }
}

// Advances authoritative gameplay state for one simulation tick.
void GameWorld::UpdateSimulation(double dt)
{
    simulationTick++;
    // Keep the compatibility visibility seam refreshed at the same fixed-tick
    // boundaries as the rest of the world update. Local province visibility
    // is intentionally unconditional in the campaign rework.
    UpdateFogOfWar();
    UpdateControllers(dt);
    for (auto& [id, player] : playerHandler.players)
        if (player != nullptr)
            player->UpdateFocus(dt);

    // Player-wide research is updated exactly once, independent of how many
    // owned buildable provinces the player controls.
    for (auto& [playerId, player] : playerHandler.players)
    {
        (void)playerId;
        if (player != nullptr)
            player->UpdateResearch(dt);
    }

    UpdateOwnedProvinceSimulations(dt);
    globalMap.UpdateConnections();
    ProcessCommands();
    eventSystem.Update(globalMap, simulationTick);
    for (const auto& request : eventSystem.ConsumePendingRaidRequests())
    {
        const auto playerIt = playerHandler.players.find(static_cast<int>(request.ownerId));
        if (playerIt == playerHandler.players.end() || playerIt->second == nullptr)
            continue;
        BattleId ignoredBattleId = InvalidBattleId;
        std::string ignoredFailure;
        if (battleSystem.StartRaid(*playerIt->second, request.provinceId, request.strength,
                                   globalMap, simulationTick,
                                   campaignGenerationParameters.globalMap.seed,
                                   ignoredBattleId, ignoredFailure))
        {
            eventSystem.RecordAppliedEffect(request.eventId,
                {AppliedWorldEventEffectKind::RaidStarted,
                 ResourceType::Null, request.strength});
            eventSystem.ConfirmRaidStarted(request.eventId);
        }
    }
    UpdateBattles();
    ProcessNonBattleJourneyEvents();
    ProcessResourceTransfers();
    ProcessArmyTransfers();
    ProcessTradeOrders();
    UpdateColonizationOperations();
    UpdateFogOfWar();
}

void GameWorld::UpdateBattles()
{
    std::map<PlayerId, Player*> players;
    for (const auto& [playerId, player] : playerHandler.players)
        if (player != nullptr)
            players.emplace(playerId, player.get());
    battleSystem.Update(globalMap, armyJourneySystem, players, simulationTick,
                        campaignGenerationParameters.globalMap.seed);
    // A crushing bandit victory first transforms the node in the warfare
    // service. Finish the automatic colonization only after the local map and
    // starting base have been generated successfully on the side.
    for (const auto& report : battleSystem.GetReports())
    {
        if (!report.banditTransformed || report.attackerId == InvalidPlayerId)
            continue;
        const auto playerIt = playerHandler.players.find(static_cast<int>(report.attackerId));
        if (playerIt != playerHandler.players.end() && playerIt->second != nullptr)
            CompleteAutomaticColonization(*playerIt->second, report.targetProvinceId);
    }
}

void GameWorld::ProcessNonBattleJourneyEvents()
{
    // BattleLifecycleSystem selectively consumes only army/battle journey
    // events. The remaining events belong to the shared journey payload
    // handlers and are processed in stable emission order.
    for (const auto& event : armyJourneySystem.ConsumeLegEvents())
    {
        const auto journeyIt = armyJourneySystem.GetJourneys().find(event.journeyId);
        if (journeyIt == armyJourneySystem.GetJourneys().end())
            continue;
        const WorldJourney& journey = journeyIt->second;

        // Resolve the route incident before applying the terminal scout
        // result. A final-leg ambush must be able to fail the journey before
        // it grants discovery or marks the target as scouted.
        eventSystem.TriggerRoute(globalMap, journey.ownerId, event.fromProvinceId,
                                 event.toProvinceId, [&]()
                                 {
                                     const auto* connection = globalMap.FindConnection(
                                         event.connectionId);
                                     const auto* route = dynamic_cast<const LandRouteConnection*>(
                                         connection);
                                     return route == nullptr ? 0 : route->GetLevel();
                                 }(), simulationTick, journey.id);
        for (const auto& loss : eventSystem.ConsumePendingJourneyUnitLosses())
        {
            const auto lossJourneyIt = armyJourneySystem.GetJourneys().find(loss.journeyId);
            if (lossJourneyIt == armyJourneySystem.GetJourneys().end())
                continue;
            const auto* scout = std::get_if<ScoutParty>(&lossJourneyIt->second.payload);
            if (scout == nullptr || loss.ownerId != lossJourneyIt->second.ownerId)
                continue;
            const auto lossPlayerIt = playerHandler.players.find(
                static_cast<int>(loss.ownerId));
            if (lossPlayerIt == playerHandler.players.end() || lossPlayerIt->second == nullptr)
                continue;
            const std::vector<int> casualties = armyJourneySystem.ApplyScoutUnitLoss(
                loss.journeyId, loss.amount);
            if (!casualties.empty())
            {
                eventSystem.RecordAppliedEffect(loss.eventId,
                    {AppliedWorldEventEffectKind::UnitLoss,
                     ResourceType::Null, static_cast<int>(casualties.size())});
                eventSystem.ConfirmJourneyEffectApplied(loss.eventId);
            }
            for (const int unitId : casualties)
                lossPlayerIt->second->roster.RemoveUnit(unitId);
            const auto afterLoss = armyJourneySystem.GetJourneys().find(loss.journeyId);
            if (!casualties.empty() && afterLoss != armyJourneySystem.GetJourneys().end() &&
                afterLoss->second.status == WorldJourneyStatus::Failed)
                eventSystem.PublishNotification(
                    "scout_mission_failed", loss.ownerId,
                    afterLoss->second.targetProvinceId,
                    afterLoss->second.sourceProvinceId, simulationTick,
                    "All assigned scouts were lost before province " +
                        std::to_string(afterLoss->second.targetProvinceId) +
                        " could be surveyed.");
        }

        // A route loss on any leg invalidates the complete scouting
        // operation. The failed-journey cleanup below releases survivors.
        if (journey.status == WorldJourneyStatus::Failed)
            continue;
        if (const auto* scout = std::get_if<ScoutParty>(&journey.payload))
        {
            auto playerIt = playerHandler.players.find(static_cast<int>(journey.ownerId));
            if (playerIt == playerHandler.players.end() || playerIt->second == nullptr)
                continue;
            if (event.journeySucceeded)
            {
                globalMap.SetKnowledge(journey.ownerId, journey.targetProvinceId,
                                       ProvinceKnowledgeLevel::Scouted);
                for (const ProvinceId neighbor : globalMap.GetNeighbors(journey.targetProvinceId))
                    if (const auto* province = globalMap.FindProvince(neighbor); province != nullptr &&
                        province->GetKnowledge(journey.ownerId) == ProvinceKnowledgeLevel::Hidden)
                        globalMap.SetKnowledge(journey.ownerId, neighbor,
                                               ProvinceKnowledgeLevel::ReachableUnknown);
                eventSystem.TriggerDiscovery(globalMap, journey.ownerId,
                                              journey.targetProvinceId, simulationTick);
                eventSystem.PublishNotification(
                    "scout_mission_completed", journey.ownerId,
                    journey.targetProvinceId, journey.sourceProvinceId, simulationTick,
                    "Province " + std::to_string(journey.targetProvinceId) +
                        " was surveyed successfully. " +
                        std::to_string(scout->unitInstanceIds.size()) +
                        (scout->unitInstanceIds.size() == 1
                            ? " scout returned safely."
                            : " scouts returned safely."));
                for (const int unitId : scout->unitInstanceIds)
                {
                    BattleUnit* unit = playerIt->second->roster.FindUnit(unitId);
                    if (unit != nullptr)
                        UnitAssignmentService::AssignReserve(*unit,
                                                             journey.sourceProvinceId,
                                                             unit->assignment.buildingId);
                }
            }
            else
            {
                for (const int unitId : scout->unitInstanceIds)
                {
                    BattleUnit* unit = playerIt->second->roster.FindUnit(unitId);
                    if (unit != nullptr)
                        UnitAssignmentService::AssignReserve(*unit,
                                                             journey.sourceProvinceId,
                                                             unit->assignment.buildingId);
                }
            }
        }
    }

    // A path can be failed by an authority effect without emitting a leg
    // completion event. Release those scout assignments as well.
    for (const auto& [journeyId, journey] : armyJourneySystem.GetJourneys())
    {
        if (journey.status != WorldJourneyStatus::Failed)
            continue;
        const auto* scout = std::get_if<ScoutParty>(&journey.payload);
        auto playerIt = playerHandler.players.find(static_cast<int>(journey.ownerId));
        if (scout == nullptr || playerIt == playerHandler.players.end() ||
            playerIt->second == nullptr)
            continue;
        for (const int unitId : scout->unitInstanceIds)
        {
            BattleUnit* unit = playerIt->second->roster.FindUnit(unitId);
            if (unit != nullptr && UnitAssignmentService::IsOnJourney(*unit, journeyId))
                UnitAssignmentService::AssignReserve(*unit, journey.sourceProvinceId,
                                                     unit->assignment.buildingId);
        }
    }
}

void GameWorld::ProcessTradeOrders()
{
    for (auto orderIt = activeTradeOrders.begin(); orderIt != activeTradeOrders.end();)
    {
        TradeOrder& order = orderIt->second;
        const auto journeyIt = armyJourneySystem.GetJourneys().find(order.journeyId);
        if (journeyIt == armyJourneySystem.GetJourneys().end())
        {
            ++orderIt;
            continue;
        }
        const WorldJourney& journey = journeyIt->second;
        const auto* cargo = std::get_if<TradeCargo>(&journey.payload);
        if (cargo == nullptr || cargo->offerType != order.request.offerType ||
            cargo->requestType != order.request.requestType ||
            cargo->amount != order.cargoAmount)
        {
            ++orderIt;
            continue;
        }

        auto playerIt = playerHandler.players.find(static_cast<int>(order.playerId));
        auto* source = globalMap.FindBuildableProvince(order.originProvinceId);
        auto* city = dynamic_cast<NeutralCityProvince*>(
            globalMap.FindProvince(order.cityProvinceId));
        auto* sourceSimulation = source != nullptr ? source->GetSimulation() : nullptr;
        if (playerIt == playerHandler.players.end() || playerIt->second == nullptr ||
            source == nullptr || sourceSimulation == nullptr || city == nullptr ||
            source->GetOwnerId() != order.playerId)
        {
            ++orderIt;
            continue;
        }

        StockpileTradeInventory inventory(sourceSimulation->GetEconomy());

        if (journey.status == WorldJourneyStatus::Failed ||
            journey.status == WorldJourneyStatus::Cancelled)
        {
            if (TradeService::RefundOrder(*city, inventory, order))
                orderIt = activeTradeOrders.erase(orderIt);
            else
            {
                // A full source warehouse can temporarily prevent a refund.
                // Keep the order visible and retry it after capacity changes;
                // no city stock is removed until the player can receive it.
                order.status = TradeOrderStatus::AwaitingUnload;
                ++orderIt;
            }
            continue;
        }
        if (journey.status != WorldJourneyStatus::Succeeded &&
            journey.status != WorldJourneyStatus::AwaitingUnload)
        {
            ++orderIt;
            continue;
        }

        const int cargoAmount = order.cargoAmount;
        if (!inventory.CanReceive(order.request.requestType, cargoAmount))
        {
            order.status = TradeOrderStatus::AwaitingUnload;
            if (journey.status == WorldJourneyStatus::Succeeded)
                armyJourneySystem.MarkAwaitingUnload(order.journeyId);
            ++orderIt;
            continue;
        }

        const int scoreGain = playerIt->second->ModifyBalanceInt(
            BalanceStat::TradeScoreGain, 1, BuildingType::Building,
            ResourceType::Null, 0);
        if (TradeService::CompleteOrder(*city, inventory, order, scoreGain))
            orderIt = activeTradeOrders.erase(orderIt);
        else
        {
            order.status = TradeOrderStatus::AwaitingUnload;
            if (journey.status == WorldJourneyStatus::Succeeded)
                armyJourneySystem.MarkAwaitingUnload(order.journeyId);
            ++orderIt;
        }
    }
}

void GameWorld::UpdateOwnedProvinceSimulations(double dt)
{
    // Every local economy is updated in stable ProvinceId order and never
    // owns a second worker/tick. The owner resolver is used only for concrete
    // buildable provinces; neutral provinces never receive a Player&.
    for (ProvinceId provinceId : globalMap.GetProvinceIds())
    {
        auto* province = globalMap.FindBuildableProvince(provinceId);
        if (province == nullptr || province->GetSimulation() == nullptr ||
            province->GetOwnerId() == InvalidPlayerId)
            continue;
        auto playerIt = playerHandler.players.find(static_cast<int>(province->GetOwnerId()));
        if (playerIt == playerHandler.players.end() || playerIt->second == nullptr)
            continue;
        province->GetSimulation()->Update(*playerIt->second, dt, simulationTick);
    }
}

bool GameWorld::IsBuildFootprintVisibleToPlayer(int playerId, Vec2i anchor, Vec2i footprint) const
{
    (void)playerId;
    (void)anchor;
    (void)footprint;
    // Local province maps are completely visible in the campaign rework.
    // Keep this query as a compatibility seam for placement/UI callers, but
    // do not gate commands on a stale per-tile visibility field.
    return true;
}

void GameWorld::UpdateFogOfWar()
{
    // Deliberately empty: discovery fog belongs only to the global map. Every
    // owned province renders and accepts placement across its full local map.
}

// Advances this object's state for one frame.
void GameWorld::Update(double dt)
{
    UpdateSimulation(dt);
    DrawMap();
}

// Captures render-safe world state for another thread.
GameSnapshot GameWorld::BuildSnapshot() const
{
    const TileMap& tilemap = GetTileMap();
    GameSnapshot snapshot;
    snapshot.simulationTick = simulationTick;
    snapshot.localPlayerId = localPlayerId;
    snapshot.activeProvinceId = GetLocalActiveProvinceId();
    snapshot.globalMapView = globalMap.BuildViewFor(localPlayerId);
    snapshot.journeyStatuses = BuildJourneyStatusViews(armyJourneySystem, simulationTick,
                                                       localPlayerId, 64);
    snapshot.battleStatuses = BuildBattleStatusViews(battleSystem, simulationTick,
                                                      localPlayerId, 64);
    for (const auto& report : battleSystem.GetReports())
    {
        if (report.attackerId != localPlayerId && report.defenderId != localPlayerId)
            continue;
        snapshot.battleReports.push_back(BuildBattleReportView(report));
        if (snapshot.battleReports.size() >= 64)
            break;
    }
    for (const auto& notification : eventSystem.GetFeed().GetHistory())
    {
        if (notification.ownerId != InvalidPlayerId && notification.ownerId != localPlayerId)
            continue;
        // Public event-feed entries still carry a province reference. Do not
        // let that reference reveal an undiscovered node; owner-scoped events
        // are already private to their recipient above.
        if (notification.ownerId == InvalidPlayerId)
        {
            const auto* province = globalMap.FindProvince(notification.provinceId);
            if (province == nullptr ||
                province->GetKnowledge(localPlayerId) < ProvinceKnowledgeLevel::Scouted)
                continue;
        }
        snapshot.eventNotifications.push_back(notification);
        if (snapshot.eventNotifications.size() > 64)
            snapshot.eventNotifications.erase(snapshot.eventNotifications.begin());
    }
    const auto playerIt = playerHandler.players.find(localPlayerId);
    if (playerIt != playerHandler.players.end() && playerIt->second != nullptr)
    {
        snapshot.taskGroups = TaskGroupService::BuildViews(
            playerIt->second->taskGroups, playerIt->second->id, playerIt->second->roster);
        if (snapshot.taskGroups.size() > 64)
            snapshot.taskGroups.resize(64);
        const ProvinceId provinceId = playerIt->second->GetActiveProvinceId();
        const ProvinceEconomy* province = playerIt->second->GetProvinceEconomy(provinceId);
        const TileMap* provinceMap = playerIt->second->GetTileMap(provinceId);
        if (province != nullptr && provinceMap != nullptr)
        {
            for (Building* building : province->dataTracker.buildings)
            {
                if (building == nullptr || building->GetComponent<DefenseCoverageComponent>() == nullptr)
                    continue;
                snapshot.provinceDefenses.push_back(BuildProvinceDefenseView(
                    *provinceMap, *playerIt->second, provinceId, *building));
                if (snapshot.provinceDefenses.size() >= 64)
                    break;
            }
        }
    }
    snapshot.mapSize = {tilemap.params.sizeX, tilemap.params.sizeY};
    snapshot.players.reserve(playerHandler.players.size());
    snapshot.tiles.reserve(tilemap.tilemap.size());

    for (const auto& [playerId, player] : playerHandler.players)
    {
        if (player != nullptr)
            snapshot.players.push_back(GameSnapshotPlayer{playerId, player->color});
    }

    for (const auto& tile : tilemap.tilemap)
    {
        GameSnapshotTile view;
        view.terrainTextureId = tile.terrainTextureId;
        view.resourceOverlayTextureId = tile.resourceOverlayTextureId;
        // tile.owner is a relic of the removed territory system (ETAP 1) —
        // always nullptr in production today, so hasOwner/ownerColor are
        // effectively dead wire fields. Left in place to avoid a snapshot
        // wire-version bump for a rendering-only cleanup; see
        // docs/post_pivot_audit_2026-07-12.md T2.
        if (tile.owner != nullptr)
        {
            view.hasOwner = true;
            view.ownerColor = tile.owner->color;
        }

        if (tile.building != nullptr)
        {
            view.hasBuilding = true;
            view.buildingType = tile.building->buildingType;
            view.buildingFootprint = tile.building->GetFootprint();
            view.buildingOwnerId = tile.building->ownerId != InvalidPlayerId
                ? tile.building->ownerId
                : (tile.building->owner != nullptr ? tile.building->owner->id : -1);
            view.isBuildingOperational = !tile.building->IsUnderConstruction();
            if (const auto* upgrade = tile.building->GetComponent<UpgradeComponent>(); upgrade != nullptr)
                view.isBuildingUpgrading = upgrade->isUpgrading;
            view.roadDisconnected = !IsRoadLike(tile.building->buildingType) &&
                                    view.isBuildingOperational &&
                                    IsBuildingRoadDisconnected(*tile.building);
            if (IsRoadLike(tile.building->buildingType))
            {
                view.roadUtilization = GetRoadUtilization(*tile.building);
                view.roadSaturated = IsRoadRecentlySaturated(*tile.building);
            }
        }
        snapshot.tiles.push_back(view);
    }

    return snapshot;
}

// Draws cached terrain, territory and building layers.
void GameWorld::DrawMap()
{
    if (render == nullptr || !render->HasWorldLayers())
        return;

    TileMap& tilemap = GetTileMap();

    render->SetSimulationTick(simulationTick);

    bool cameraChanged =
        cachedCameraZoom != render->camera.zoom ||
        cachedCameraTarget.x != render->camera.target.x ||
        cachedCameraTarget.y != render->camera.target.y;

    Vec2f worldA = render->RenderToWorld({0.0f, 0.0f});
    Vec2f worldB = render->RenderToWorld({static_cast<float>(RENDER_WIDTH), static_cast<float>(RENDER_HEIGHT)});
    const VisibleTileBounds visibleBounds = ComputeVisibleTileBounds(
        worldA, worldB, {tilemap.params.sizeX, tilemap.params.sizeY},
        GetMaximumBuildingFootprintOverhang());
    const int minTileX = visibleBounds.minX;
    const int maxTileX = visibleBounds.maxX;
    const int minTileY = visibleBounds.minY;
    const int maxTileY = visibleBounds.maxY;

    render->ClearDynamicLights();
    render->ClearFogReveals();
    // Fog and local-light influence must not depend on whether the building
    // anchor itself is inside the camera tile rectangle. Queue every source;
    // Renderer conservatively rejects only circles that cannot touch the
    // render target. This keeps the mask stable while panning/zooming.
    for (const auto& [playerId, player] : playerHandler.players)
    {
        if (playerId != localPlayerId || player == nullptr || player->GetTileMap() != &tilemap)
            continue;

        for (Building* building : player->GetTrackedBuildings())
        {
            if (building == nullptr)
                continue;

            const Vec2i anchor = tilemap.GetCoordsFromId(building->positionId);
            const Vec2f position{static_cast<float>(anchor.x * TILE_SIZE),
                                 static_cast<float>(anchor.y * TILE_SIZE)};
            render->QueueBuildingLight(building->buildingType, building->GetFootprint(),
                                       position, building->id, !building->IsUnderConstruction());
            if (playerId == localPlayerId)
                render->QueueBuildingFogReveal(building->buildingType,
                                               building->GetFootprint(), position);
        }
    }

    std::map<Building*, Vec2f> visibleBuildings;
    for (int x = minTileX; x <= maxTileX; x++)
    {
        for (int y = minTileY; y <= maxTileY; y++)
        {
            auto& tile = tilemap.tilemap[y * tilemap.params.sizeX + x];
            const Vec2f tilePosition{static_cast<float>(x * TILE_SIZE),
                                     static_cast<float>(y * TILE_SIZE)};
            if (tile.resourceOverlayTextureId >= 0)
                render->QueueResourceLight(tile.resourceOverlayTextureId, tilePosition, tile.id);
            if (tile.building != nullptr)
            {
                visibleBuildings.emplace(tile.building.get(), tilePosition);
            }
        }
    }

    render->ClearLayer(WorldRenderLayer::WorldEffects);
    if (render->AreContactShadowsEnabled())
    {
        const WorldLightingFrame lighting = render->GetCurrentWorldLightingFrame();
        const unsigned char shadowAlpha = static_cast<unsigned char>(std::clamp(
            32.0f + (1.0f - lighting.ambientIntensity) * 42.0f, 32.0f, 74.0f));
        render->BeginLayer(WorldRenderLayer::WorldEffects);
        for (const auto& [building, position] : visibleBuildings)
        {
            // Roads are flat, contiguous tile art. Casting a separate shadow
            // from every tile turns a road into a black wall, so they receive
            // no object shadow at all.
            if (building == nullptr || IsRoadLike(building->buildingType))
                continue;

            Vec2i footprint = building->GetFootprint();
            float width = footprint.x * TILE_SIZE;
            float height = footprint.y * TILE_SIZE;
            const float baseX = position.x + width * 0.50f;
            const float baseY = RENDER_HEIGHT - position.y - height * 0.84f;
            const float directionalLength = std::min(
                lighting.shadowLength * 0.30f, std::max(width, height) * 1.10f);
            if (directionalLength > 0.5f)
            {
                // Three faint, shrinking ellipses produce a soft, tapered
                // cast shadow without the hard line/end-cap geometry.
                constexpr float samples[] = {0.32f, 0.62f, 0.90f};
                constexpr float widths[] = {0.30f, 0.24f, 0.18f};
                constexpr float alphas[] = {0.32f, 0.22f, 0.14f};
                for (int i = 0; i < 3; ++i)
                {
                    const float shadowX = baseX - lighting.sunDirection.x * directionalLength * samples[i];
                    const float shadowY = baseY + lighting.sunDirection.y * directionalLength * samples[i];
                    DrawEllipse(static_cast<int>(shadowX), static_cast<int>(shadowY),
                                width * widths[i], std::max(2.0f, height * 0.075f),
                                Color{0, 0, 0, static_cast<unsigned char>(shadowAlpha * alphas[i])});
                }
            }
            DrawEllipse(static_cast<int>(baseX), static_cast<int>(baseY),
                        static_cast<float>(width * 0.32f),
                        static_cast<float>(std::max(2.0f, height * 0.075f)),
                        Color{0, 0, 0, static_cast<unsigned char>(shadowAlpha * 0.70f)});
        }
        render->EndLayer();
    }

    // The heatmap belongs below road albedo, like a local light spilling out
    // from underneath the stones. WorldEffects is composed before
    // StaticObjects, so the road texture masks the bright core naturally.
    if (IsLogisticsOverlayPreferenceEnabled())
    {
        render->BeginLayer(WorldRenderLayer::WorldEffects);
        for (const auto& [building, position] : visibleBuildings)
        {
            if (building == nullptr || !IsRoadLike(building->buildingType))
                continue;
            const Vec2i tilePosition = tilemap.GetCoordsFromId(building->positionId);
            const int mask = GetRoadConnectionMask(tilemap, tilePosition.x, tilePosition.y);
            DrawRoadUtilizationOverlay(position, GetRoadUtilization(*building),
                                       (mask & RoadTopology::West) != 0,
                                       (mask & RoadTopology::East) != 0,
                                       (mask & RoadTopology::North) != 0,
                                       (mask & RoadTopology::South) != 0);
        }
        render->EndLayer();
    }

    bool redrawTerrain = cameraChanged || tilemap.terrainDirty;
    const bool hasVisibleBuildingAnimation = std::any_of(
        visibleBuildings.begin(), visibleBuildings.end(),
        [&](const auto& entry)
        {
            return entry.first != nullptr && render->HasBuildingAnimation(entry.first->buildingType);
        });
    bool redrawBuildings = cameraChanged || tilemap.buildingsDirty || hasVisibleBuildingAnimation;

    if (redrawTerrain)
    {
        render->ClearLayer(WorldRenderLayer::Terrain);
        render->BeginLayer(WorldRenderLayer::Terrain);
        for(int x = minTileX; x <= maxTileX; x++)
        {
            for(int y = minTileY; y <= maxTileY; y++)
            {
                auto& tile = tilemap.tilemap[y*tilemap.params.sizeX + x];

                Vec2f pos = {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)};
                render->DrawAtlasTile(0, tile.terrainTextureId, pos);
            }
        }
        // Mineral ground halos are light, not coloured paint. Alpha blending
        // a blue/red halo into olive grass can lower individual channels and
        // the retro palette then quantizes the result to a dark grey. Batch
        // these halos additively so they can only brighten the terrain.
        BeginBlendMode(BLEND_ADDITIVE);
        for (int x = minTileX; x <= maxTileX; x++)
        {
            for (int y = minTileY; y <= maxTileY; y++)
            {
                const auto& tile = tilemap.tilemap[y * tilemap.params.sizeX + x];
                if (tile.resourceOverlayTextureId >= 0)
                    render->DrawResourceGroundGlow(
                        tile.resourceOverlayTextureId,
                        {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)});
            }
        }
        EndBlendMode();
        render->EndLayer();

        render->ClearLayer(WorldRenderLayer::ResourceOverlays);
        render->BeginLayer(WorldRenderLayer::ResourceOverlays);
        for(int x = minTileX; x <= maxTileX; x++)
        {
            for(int y = minTileY; y <= maxTileY; y++)
            {
                const auto& tile = tilemap.tilemap[y * tilemap.params.sizeX + x];
                if (tile.resourceOverlayTextureId < 0)
                    continue;
                render->DrawResourceOverlay(tile.resourceOverlayTextureId,
                                            {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)});
            }
        }
        render->EndLayer();

        tilemap.terrainDirty = false;
    }

    if (redrawBuildings)
    {
        render->ClearLayer(WorldRenderLayer::StaticObjects);
        render->BeginLayer(WorldRenderLayer::StaticObjects);
        for(int x = minTileX; x <= maxTileX; x++)
        {
            for(int y = minTileY; y <= maxTileY; y++)
            {
                auto& tile = tilemap.tilemap[y*tilemap.params.sizeX + x];

                Vec2f pos = {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)};

                if(tile.building)
                {
                    const Color tint = tile.building->IsUnderConstruction()
                        ? Color{118, 122, 132, 215}
                        : WHITE;
                    if (IsRoadLike(tile.building->buildingType))
                        render->DrawRoadTexture(tile.building->buildingType, pos,
                                                GetRoadConnectionMask(tilemap, x, y), tint);
                    else
                        render->DrawBuildingTexture(tile.building.get(), pos, tint);
                }
            }
        }
        render->EndLayer();
        tilemap.buildingsDirty = false;
    }

    render->ClearLayer(WorldRenderLayer::DynamicObjects);
    render->BeginLayer(WorldRenderLayer::DynamicObjects);
    for (const auto& [building, position] : visibleBuildings)
    {
            if (building != nullptr)
        {
            if (const auto* upgrade = building->GetComponent<UpgradeComponent>();
                upgrade != nullptr && upgrade->isUpgrading)
                DrawBuildingFootprintStatusOverlay(
                    position, building->GetFootprint(), RENDER_HEIGHT, TILE_SIZE,
                    BuildingFootprintOverlay::Upgrading);
            if (!IsRoadLike(building->buildingType) && !building->IsUnderConstruction() &&
                IsBuildingRoadDisconnected(*building))
                DrawBuildingFootprintStatusOverlay(
                    position, building->GetFootprint(), RENDER_HEIGHT, TILE_SIZE,
                    BuildingFootprintOverlay::Disconnected);
            if (IsLogisticsOverlayPreferenceEnabled() && IsRoadLike(building->buildingType))
                DrawRoadSaturationIndicator(position, IsRoadRecentlySaturated(*building));
        }
    }
    // In-flight goods are physical world objects, not a diagnostics overlay.
    // Keep them visible during ordinary play; the logistics preference still
    // controls only utilization heatmaps and saturation indicators.
    std::vector<ShipmentRenderState> shipmentViews;
    shipmentViews.reserve(GetLiveShipmentCount());
    for (const auto& [playerId, player] : playerHandler.players)
    {
        if (playerId == localPlayerId && player != nullptr && player->GetRoadNetwork() != nullptr)
            player->GetRoadNetwork()->AppendShipmentRenderStates(shipmentViews);
    }
    render->DrawShipments(shipmentViews, {tilemap.params.sizeX, tilemap.params.sizeY});
    render->EndLayer();

    cachedCameraTarget = {render->camera.target.x, render->camera.target.y};
    cachedCameraZoom = render->camera.zoom;
}
