#include "core/GameWorldInternal.h"
#include "core/RoadTopology.h"
#include "core/VisibleTileBounds.h"
#include "economy/BuildingConfig.h"
#include "warfare/UnitMarchSystem.h"

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
    // Placeholder per-unit-type fill color (pending real sprites) — lets units
    // be told apart at a glance regardless of owner; owner identity is still
    // shown via the box outline. Unknown/future unit ids fall back to a
    // deterministic hash-derived color rather than a single flat default, so
    // adding a new unit to units.rtsdata never makes two types look identical.
    Color PlaceholderUnitColor(const std::string& unitDefId)
    {
        if (unitDefId == "militia")
            return Color{170, 170, 170, 255};
        if (unitDefId == "swordsman")
            return Color{70, 130, 220, 255};
        if (unitDefId == "knight")
            return Color{230, 190, 40, 255};
        if (unitDefId == "ram")
            return Color{150, 90, 40, 255};

        std::size_t hash = std::hash<std::string>{}(unitDefId);
        return Color{
            static_cast<unsigned char>(80 + (hash % 150)),
            static_cast<unsigned char>(80 + ((hash / 150) % 150)),
            static_cast<unsigned char>(80 + ((hash / 22500) % 150)),
            255};
    }

    // Small health bar centered above a world-space anchor (already flipped
    // to screen Y). `ratio` is clamped to [0,1]; green->red by remaining HP.
    void DrawHealthBar(int screenX, int screenTopY, int width, float ratio)
    {
        ratio = std::clamp(ratio, 0.0f, 1.0f);
        int height = 4;
        int x = screenX - width / 2;
        Color fill = Color{
            static_cast<unsigned char>(220 * (1.0f - ratio) + 30 * ratio),
            static_cast<unsigned char>(200 * ratio + 30 * (1.0f - ratio)),
            30,
            255};
        DrawRectangle(x, screenTopY, width, height, Color{20, 20, 20, 200});
        DrawRectangle(x, screenTopY, static_cast<int>(width * ratio), height, fill);
        DrawRectangleLines(x, screenTopY, width, height, Color{0, 0, 0, 200});
    }

    void DrawDamageFlashOverlay(Vec2f position, Vec2i footprint, float remainingSeconds)
    {
        if (remainingSeconds <= 0.0f)
            return;
        const float width = footprint.x * TILE_SIZE;
        const float height = footprint.y * TILE_SIZE;
        const float top = RENDER_HEIGHT - position.y - height;
        const float pulse = 0.45f + 0.55f * std::abs(std::sin(static_cast<float>(GetTime()) * 10.0f));
        const unsigned char fillAlpha = static_cast<unsigned char>(18.0f + pulse * 28.0f);
        const unsigned char lineAlpha = static_cast<unsigned char>(125.0f + pulse * 110.0f);
        DrawRectangle(static_cast<int>(position.x), static_cast<int>(top),
                      static_cast<int>(width), static_cast<int>(height), Color{255, 58, 32, fillAlpha});
        DrawRectangleLinesEx({position.x - 2.0f, top - 2.0f, width + 4.0f, height + 4.0f},
                             2.5f, Color{255, 110, 72, lineAlpha});
    }

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

    int GetMilitaryRoadConnectionMask(const TileMap& tilemap, int x, int y)
    {
        return RoadTopology::GetCardinalMask(x, y, tilemap.params.sizeX, tilemap.params.sizeY,
            [&](int checkX, int checkY)
            {
                return tilemap.tilemap[checkY * tilemap.params.sizeX + checkX].isMilitaryRoad;
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
    // Commands issued this tick must use the same deterministic, camera-free
    // visibility as the build preview. Refresh before controllers/commands,
    // then once more after movement below.
    UpdateFogOfWar();
    UpdateControllers(dt);
    for (auto& [id, player] : playerHandler.players)
        if (player != nullptr && !player->defeated)
        {
            player->UpdateFocus(dt);
            player->UpdateResearch(dt);
        }
    ProcessCommands();
    // Assign each player's builders to the front of their construction queue
    // before ticking buildings, so only funded builder slots progress this tick.
    for (auto& [id, player] : playerHandler.players)
        if (player != nullptr && !player->defeated)
            player->construction.Refresh(*player);
    // Task 12: reset per-tick road admission grants before buildings attempt
    // to dispatch or advance shipments. With no priority configured the
    // transport path does not consult this state and keeps the old order.
    for (auto& [id, player] : playerHandler.players)
        if (player != nullptr && !player->defeated && player->roadNetwork != nullptr)
            player->roadNetwork->Update(dt);
    // Update buildings by iterating through Player registries instead of tilemap scan.
    // Avoids O(1M) tilemap iteration every tick; now O(n_buildings) which is typically ~100-1000.
    //
    // Determinism fix (docs/work_plan_2026-07-13.md, found while verifying
    // B1/B2 via a flaky HqCombatSystemTests.SiegeToEliminationIsDeterministic-
    // ForSameSeed): this is THE main per-tick building update loop — every
    // ProductionComponent::Update call along the way competes for the same
    // player-wide Manpower/Workers pool via AutoAssignWorkers, and the same
    // shared road capacity/resource pool via LogisticsComponent/Storage-
    // Component dispatch. Which building's turn comes first each tick
    // therefore is simulation-visible (it decides who gets scarce workers/
    // resources this tick), so — same reasoning as every other fixed
    // instance of this bug class — iteration order must not depend on
    // Building* heap addresses, which differ across independently-
    // constructed GameWorld instances (confirmed root cause via targeted
    // instrumentation: two identically-seeded worlds' same building ended up
    // with a different GetTotalProduced() count by tick ~5700).
    std::vector<Building*> orderedBuildings;
    for (auto& [id, player] : playerHandler.players)
    {
        if (player == nullptr || player->defeated) continue;
        orderedBuildings.insert(orderedBuildings.end(), player->GetTrackedBuildings().begin(), player->GetTrackedBuildings().end());
    }
    std::sort(orderedBuildings.begin(), orderedBuildings.end(), [](Building* a, Building* b) { return a->id < b->id; });

    for (Building* building : orderedBuildings)
    {
        if (building == nullptr || building->owner == nullptr) continue;

        bool wasUnderConstruction = building->IsUnderConstruction();
        building->Update(dt);

        if (wasUnderConstruction && !building->IsUnderConstruction())
        {
            tilemap.buildingsDirty = true;
            if (building->owner->roadNetwork != nullptr)
            {
                for (int tileId : tilemap.GetBuildingTileIds(building))
                    building->owner->roadNetwork->UpdateNavMap(tileId, building);
            }
            tilemap.AutoConnectBuilding(building);
        }
    }

    for (auto& [id, player] : playerHandler.players)
        if (player != nullptr && !player->defeated)
        {
            player->UpdateEconomyTelemetry(dt);
            player->UpdateConqueredEconomy(dt);
        }

    UpdateUnits(dt);
    UpdateFogOfWar();
}

bool GameWorld::IsBuildFootprintVisibleToPlayer(int playerId, Vec2i anchor, Vec2i footprint) const
{
    auto fogIt = fogOfWarByPlayer.find(playerId);
    return fogIt != fogOfWarByPlayer.end() && fogIt->second.IsFootprintVisible(anchor, footprint);
}

void GameWorld::UpdateFogOfWar()
{
    const Vec2i mapSize{tilemap.params.sizeX, tilemap.params.sizeY};
    if (mapSize.x <= 0 || mapSize.y <= 0)
        return;

    for (const auto& [playerId, player] : playerHandler.players)
    {
        FogOfWarState& fog = fogOfWarByPlayer[playerId];
        if (!fog.IsInitializedFor(mapSize))
            fog.Initialize(mapSize);
        else
            fog.BeginVisibilityUpdate();

        if (player == nullptr || player->defeated)
            continue;

        for (const Building* building : player->GetTrackedBuildings())
        {
            if (building == nullptr)
                continue;

            const Vec2i anchor = tilemap.GetCoordsFromId(building->positionId);
            const Vec2i footprint = building->GetFootprint();
            const Vec2f center{
                static_cast<float>(anchor.x * TILE_SIZE) + footprint.x * TILE_SIZE * 0.5f,
                static_cast<float>(anchor.y * TILE_SIZE) + footprint.y * TILE_SIZE * 0.5f};
            fog.RevealWorldCircle(center, FogOfWar::BuildingRevealRadiusWorld(building->buildingType, footprint));
        }
    }

    for (const auto& [instanceId, unit] : deployedUnits)
    {
        if (unit.tileIndex < 0 || unit.state == BattleUnitState::Dying)
            continue;

        auto fogIt = fogOfWarByPlayer.find(unit.ownerPlayerId);
        if (fogIt == fogOfWarByPlayer.end())
            continue;
        fogIt->second.RevealWorldCircle(UnitMarchSystem::ComputeWorldPosition(*this, unit),
                                        FogOfWar::UnitRevealRadiusWorld);
    }
}

// Advances this object's state for one frame.
void GameWorld::Update(double dt)
{
    UpdateSimulation(dt);
    DrawMap();
}

bool GameWorld::IsPlayerDefeated(int playerId) const
{
    auto it = playerHandler.players.find(playerId);
    return it != playerHandler.players.end() && it->second != nullptr && it->second->defeated;
}

int GameWorld::GetVictorPlayerId() const
{
    int survivors = 0;
    int lastAlive = -1;
    for (const auto& [pid, player] : playerHandler.players)
    {
        if (player == nullptr) continue;
        if (!player->defeated) { survivors++; lastAlive = pid; }
    }
    // Only a decided game (started with >=2 players, one left) reports a victor.
    int total = 0;
    for (const auto& [pid, player] : playerHandler.players)
        if (player != nullptr) total++;
    return (total >= 2 && survivors == 1) ? lastAlive : -1;
}

// Captures render-safe world state for another thread.
GameSnapshot GameWorld::BuildSnapshot() const
{
    GameSnapshot snapshot;
    snapshot.simulationTick = simulationTick;
    snapshot.localPlayerId = localPlayerId;
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
            view.buildingOwnerId = tile.building->owner != nullptr ? tile.building->owner->id : -1;
            view.isBuildingOperational = !tile.building->IsUnderConstruction();
            if (const auto* hq = tile.building->GetComponent<HqComponent>(); hq != nullptr)
                view.buildingDamageIndicator = static_cast<float>(hq->recentDamageTimer);
            if (IsRoadLike(tile.building->buildingType))
            {
                view.roadUtilization = GetRoadUtilization(*tile.building);
                view.roadSaturated = IsRoadRecentlySaturated(*tile.building);
            }
        }
        view.isMilitaryRoad = tile.isMilitaryRoad;
        snapshot.tiles.push_back(view);
    }

    return snapshot;
}

// Draws cached terrain, territory and building layers.
void GameWorld::DrawMap()
{
    if (render == nullptr || !render->HasWorldLayers())
        return;

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
        if (player == nullptr)
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
            if (!player->defeated && playerId == localPlayerId)
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
                // TD(etap-2): military road placeholder — a flat tint until a
                // dedicated texture exists; kept as its own visual type so
                // swapping in real art later doesn't touch this call site.
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

    // Military tracks are cached separately from buildings and are composed
    // below StaticObjects. Keep the track on every military-road tile,
    // including a tile occupied by a Bridge; the bridge sprite is rendered in
    // StaticObjects and therefore naturally appears one layer above it.
    if (redrawTerrain || redrawBuildings)
    {
        render->ClearLayer(WorldRenderLayer::MilitaryRoads);
        render->BeginLayer(WorldRenderLayer::MilitaryRoads);
        for (int x = minTileX; x <= maxTileX; x++)
        {
            for (int y = minTileY; y <= maxTileY; y++)
            {
                const auto& tile = tilemap.tilemap[y * tilemap.params.sizeX + x];
                if (tile.isMilitaryRoad)
                    render->DrawMilitaryRoadTexture(
                        {static_cast<float>(x * TILE_SIZE), static_cast<float>(y * TILE_SIZE)},
                        GetMilitaryRoadConnectionMask(tilemap, x, y));
            }
        }
        render->EndLayer();
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
                    const bool roadDisconnected = !IsRoadLike(tile.building->buildingType) &&
                                                  IsBuildingRoadDisconnected(*tile.building);
                    const Color tint = tile.building->IsUnderConstruction()
                        ? Color{118, 122, 132, 215}
                        : roadDisconnected
                            ? Color{224, 78, 72, 235}
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

    // TD(etap-4/5): deployed units. Always redrawn (layer 3, never cached)
    // since marching/fighting units move or change tint every tick, unlike
    // the mostly-static layers above. Placeholder shape (owner-colored
    // rectangle — no unit texture yet) pending real sprites/animation
    // (plan 4.3) — deliberately simple so swapping in art later only touches
    // this block.
    render->ClearLayer(WorldRenderLayer::DynamicObjects);
    render->BeginLayer(WorldRenderLayer::DynamicObjects);
    for (const auto& [building, position] : visibleBuildings)
    {
        if (building != nullptr)
        {
            if (const auto* hq = building->GetComponent<HqComponent>(); hq != nullptr)
                DrawDamageFlashOverlay(position, building->GetFootprint(), static_cast<float>(hq->recentDamageTimer));
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
        (void)playerId;
        if (player != nullptr && player->GetRoadNetwork() != nullptr)
            player->GetRoadNetwork()->AppendShipmentRenderStates(shipmentViews);
    }
    render->DrawShipments(shipmentViews, {tilemap.params.sizeX, tilemap.params.sizeY});
    const WorldLightingFrame dynamicLighting = render->GetCurrentWorldLightingFrame();
    const unsigned char unitShadowAlpha = static_cast<unsigned char>(std::clamp(
        45.0f + (1.0f - dynamicLighting.ambientIntensity) * 55.0f, 45.0f, 100.0f));
    const float unitDirectionalLength = dynamicLighting.shadowLength * 0.24f;
    for (const auto& [instanceId, unit] : deployedUnits)
    {
        if (unit.tileIndex < 0)
            continue; // still waiting in the spawn queue, not on the map yet

        Vec2f worldPos = UnitMarchSystem::ComputeWorldPosition(*this, unit);

        if (unit.ownerPlayerId == localPlayerId && unit.state != BattleUnitState::Dying)
            render->QueueFogReveal({{worldPos.x, worldPos.y}, FogOfWar::UnitRevealRadiusWorld});

        auto ownerIt = playerHandler.players.find(unit.ownerPlayerId);
        Player* owner = ownerIt != playerHandler.players.end() ? ownerIt->second.get() : nullptr;
        Color ownerColor = owner != nullptr ? owner->color : WHITE;

        // Placeholder fill = unit type (told apart at a glance), outline =
        // owner color (whose unit it is) — real sprites will replace both.
        Color fillColor = PlaceholderUnitColor(unit.unitDefId);
        if (unit.state == BattleUnitState::Dying)
            fillColor.a = 90;

        int screenX = static_cast<int>(worldPos.x);
        int screenY = static_cast<int>(RENDER_HEIGHT) - static_cast<int>(worldPos.y);
        int halfSize = static_cast<int>(TILE_SIZE * 0.3f);
        Rectangle box{static_cast<float>(screenX - halfSize), static_cast<float>(screenY - halfSize),
                      static_cast<float>(halfSize * 2), static_cast<float>(halfSize * 2)};
        if (render->AreContactShadowsEnabled())
        {
            // This stays in the dynamic layer so it follows marching units
            // precisely, while the selection/health overlays remain above
            // the lighting pass in regular UI rendering.
            const float baseShadowY = screenY + static_cast<float>(halfSize) * 0.62f;
            if (unitDirectionalLength > 0.5f)
            {
                const float cappedLength = std::min(unitDirectionalLength, static_cast<float>(halfSize) * 1.25f);
                const float shadowX = screenX - dynamicLighting.sunDirection.x * cappedLength * 0.60f;
                const float shadowY = baseShadowY + dynamicLighting.sunDirection.y * cappedLength * 0.60f;
                DrawEllipse(static_cast<int>(shadowX), static_cast<int>(shadowY),
                            static_cast<float>(halfSize) * 0.52f,
                            std::max(1.5f, static_cast<float>(halfSize) * 0.16f),
                            Color{0, 0, 0, static_cast<unsigned char>(unitShadowAlpha * 0.30f)});
            }
            DrawEllipse(screenX, static_cast<int>(baseShadowY),
                        static_cast<float>(halfSize) * 0.90f,
                        std::max(2.0f, static_cast<float>(halfSize) * 0.28f),
                        Color{0, 0, 0, unitShadowAlpha});
        }
        DrawRectangleRec(box, fillColor);
        DrawRectangleLinesEx(box, unit.state == BattleUnitState::FightingUnit ? 2.0f : 1.0f, ownerColor);

        if (owner != nullptr && unit.state != BattleUnitState::Dying)
        {
            double maxHp = unit.GetEffectiveMaxHp(*owner);
            float ratio = maxHp > 0.0 ? static_cast<float>(unit.currentHp / maxHp) : 0.0f;
            DrawHealthBar(screenX, screenY - halfSize - 7, halfSize * 2, ratio);
        }
    }

    // HQ health bar — shown for a few seconds after an HQ last took siege
    // damage (HqComponent::recentDamageTimer), so a defender gets an obvious
    // "under attack" cue without cluttering the view of HQs at full health
    // that aren't currently being sieged.
    for (auto& [playerId, player] : playerHandler.players)
    {
        if (player == nullptr)
            continue;
        for (Building* hqBuilding : player->GetTrackedBuildingsWithComponent<HqComponent>())
        {
            const auto* hq = hqBuilding->GetComponent<HqComponent>();
            if (hq == nullptr || hq->recentDamageTimer <= 0.0)
                continue;

            Vec2f center = ComputeBuildingCenter(tilemap, *hqBuilding);
            int screenX = static_cast<int>(center.x);
            int screenY = static_cast<int>(RENDER_HEIGHT) - static_cast<int>(center.y);
            Vec2i footprint = hqBuilding->GetFootprint();
            int barWidth = static_cast<int>(std::max(footprint.x, footprint.y) * TILE_SIZE * 0.8f);
            int barTopY = screenY - (footprint.y * TILE_SIZE) / 2 - 14;
            double modifiedMaxHp = hq->GetModifiedMaxHp(*hqBuilding);
            float ratio = modifiedMaxHp > 0.0 ? static_cast<float>(hq->currentHp / modifiedMaxHp) : 0.0f;
            DrawHealthBar(screenX, barTopY, barWidth, ratio);
        }
    }

    // TD(etap-7.2): in-flight tower projectiles. Same always-redrawn layer as
    // units (they move every tick too) — placeholder small circle pending
    // real projectile art (plan 7.2's "krótkie linie/prostokąty/okręgi").
    for (const auto& [id, projectile] : projectiles)
    {
        auto ownerIt = playerHandler.players.find(projectile.sourcePlayerId);
        Color color = ownerIt != playerHandler.players.end() && ownerIt->second != nullptr
            ? ownerIt->second->color
            : WHITE;

        int screenX = static_cast<int>(projectile.position.x);
        int screenY = static_cast<int>(RENDER_HEIGHT) - static_cast<int>(projectile.position.y);
        // Tower rounds are intentionally a visual-only high-priority light:
        // their deterministic simulation position is already available, while
        // the glow/trail never enters saves, checksums, or combat resolution.
        render->QueueDynamicLight({{projectile.position.x, projectile.position.y},
                                   Color{255, 205, 116, 255},
                                   88.0f, 0.42f, 0.58f, 0.12f, -id, 90});

        auto targetIt = deployedUnits.find(projectile.targetUnitInstanceId);
        if (targetIt != deployedUnits.end())
        {
            Vec2f targetPos = UnitMarchSystem::ComputeWorldPosition(*this, targetIt->second);
            float dx = targetPos.x - projectile.position.x;
            float dy = targetPos.y - projectile.position.y;
            float length = std::sqrt(dx * dx + dy * dy);
            if (length > 0.001f)
            {
                constexpr float TrailLength = 16.0f;
                float trailX = projectile.position.x - dx / length * TrailLength;
                float trailY = projectile.position.y - dy / length * TrailLength;
                DrawLineEx({trailX, static_cast<float>(RENDER_HEIGHT) - trailY},
                           {static_cast<float>(screenX), static_cast<float>(screenY)},
                           2.0f, Color{255, 220, 150, 185});
            }
        }
        DrawCircle(screenX, screenY, TILE_SIZE * 0.12f, color);
        DrawCircle(screenX, screenY, TILE_SIZE * 0.055f, Color{255, 244, 202, 255});
    }
    render->EndLayer();

    cachedCameraTarget = {render->camera.target.x, render->camera.target.y};
    cachedCameraZoom = render->camera.zoom;
}
