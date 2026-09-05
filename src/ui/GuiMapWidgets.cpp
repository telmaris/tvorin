// Map-layer widgets: selection and production-warning highlights. Military map
// widgets were removed with the old local combat system; the roster remains a
// read-only resource panel until the global-map layer owns expedition controls.

#include "GuiInternal.h"

#include "scenes/Scenes.h"
#include "economy/Player.h"
#include "economy/BuildingComponents.h"
#include "ui/BuildingPresentation.h"
#include "ui/GameplayClock.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace
{
    // Returns the screen-space rectangle covering a building's footprint.
    Rectangle BuildingScreenRect(GameScene* scene, Building* building)
    {
        Vec2i anchor = scene->game->GetTileMap().GetCoordsFromId(building->positionId);
        Vec2i footprint = building->GetFootprint();
        Vec2f worldTopLeft{
            static_cast<float>(anchor.x * TILE_SIZE),
            static_cast<float>(anchor.y * TILE_SIZE)};
        Vec2f worldBottomRight{
            worldTopLeft.x + footprint.x * TILE_SIZE,
            worldTopLeft.y + footprint.y * TILE_SIZE};
        Vec2f screenTopLeft = scene->render.WorldToScreen(worldTopLeft);
        Vec2f screenBottomRight = scene->render.WorldToScreen(worldBottomRight);
        return Rectangle{
            screenTopLeft.x,
            screenTopLeft.y,
            screenBottomRight.x - screenTopLeft.x,
            screenBottomRight.y - screenTopLeft.y};
    }

    // A two-pass animated outline remains sharp above fog and lighting without
    // needing a sprite mask or a full-screen outline shader. It is screen-space
    // UI, so it never alters the cached world render targets.
    void DrawPulsingOutline(Rectangle bounds, Color color, float baseThickness)
    {
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 4.2f);
        Color outer = color;
        outer.a = static_cast<unsigned char>(55.0f + pulse * 65.0f);
        Color inner = color;
        inner.a = static_cast<unsigned char>(175.0f + pulse * 65.0f);
        DrawRectangleRoundedLines(bounds, 0.04f, 8, baseThickness + 2.0f, outer);
        DrawRectangleRoundedLines(bounds, 0.04f, 8, baseThickness, inner);
    }
}

void GameplayClockWidget::UpdateSize(Vec2i windowSize)
{
    size = {136, 34};
    pos = {std::max(0, windowSize.x - size.x - 12),
           std::max(0, windowSize.y - size.y - 12)};
}

void GameplayClockWidget::Update(double dt)
{
    (void)dt;
    if (scene == nullptr)
        return;

    UpdateSize({GetScreenWidth(), GetScreenHeight()});
    const std::uint64_t tick = scene->game != nullptr
        ? scene->game->GetSimulationTick()
        : scene->latestSnapshot.simulationTick;
    const std::uint64_t seconds = tick / FixedSimulationClock::TicksPerSecond;
    const Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y),
                           static_cast<float>(size.x), static_cast<float>(size.y)};
    DrawRectangleRounded(bounds, 0.12f, 6, Color{20, 25, 31, 225});
    DrawRectangleRoundedLines(bounds, 0.12f, 6, 1.0f, UiTheme::Iron);
    UiText::Draw(FormatGameplayDuration(seconds), pos.x + 10, pos.y + 7, 18, UiTheme::Parchment);
}

// ─── SelectedBuildingWidget ──────────────────────────────────────────────────

// Highlights the selected building and its logistics endpoints.
void SelectedBuildingWidget::Update(double dt)
{
    (void)dt;
    if (scene == nullptr || scene->game == nullptr || building == nullptr ||
        !scene->game->GetTileMap().ContainsBuilding(building))
        return;

    struct EndpointOverlay
    {
        Building* building{nullptr};
        bool supplier{false};
        bool receiver{false};
        std::set<ResourceType> resources;
    };
    std::map<int, EndpointOverlay> endpoints;
    const auto addEndpoint = [&](const BuildingConnectionView& view, bool supplier)
    {
        if (view.building == nullptr ||
            !scene->game->GetTileMap().ContainsBuilding(view.building))
            return;
        auto& endpoint = endpoints[view.building->id];
        endpoint.building = view.building;
        endpoint.supplier = endpoint.supplier || supplier;
        endpoint.receiver = endpoint.receiver || !supplier;
        endpoint.resources.insert(view.type);
    };
    for (const auto& supplier : building->GetSupplierViews())
        addEndpoint(supplier, true);
    for (const auto& receiver : building->GetReceiverViews())
        addEndpoint(receiver, false);

    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 3.6f);
    for (const auto& [endpointId, endpoint] : endpoints)
    {
        (void)endpointId;
        if (endpoint.building == nullptr)
            continue;

        const Rectangle endpointDest = BuildingScreenRect(scene, endpoint.building);
        const bool combined = endpoint.supplier && endpoint.receiver;
        const Color fill = combined
            ? Color{210, 185, 84, static_cast<unsigned char>(28.0f + pulse * 24.0f)}
            : endpoint.supplier
                ? Color{73, 146, 236, static_cast<unsigned char>(28.0f + pulse * 24.0f)}
                : Color{236, 168, 74, static_cast<unsigned char>(28.0f + pulse * 24.0f)};
        const Color outline = combined ? Color{238, 210, 104, 210}
            : endpoint.supplier ? Color{96, 174, 255, 190} : Color{255, 190, 86, 205};
        DrawRectangleRounded(endpointDest, 0.04f, 8, fill);
        DrawPulsingOutline(endpointDest, outline, 1.0f);

        const int visibleResourceCount = std::min(3, static_cast<int>(endpoint.resources.size()));
        // Logistics endpoints are map annotations, not tiny labels. Scale
        // them with the footprint, keep a readable floor, and center the row
        // directly on the building instead of floating it above the sprite.
        const float iconSize = std::clamp(
            std::min(endpointDest.width, endpointDest.height) * 0.34f,
            28.0f, 40.0f);
        const float iconGap = 4.0f;
        const bool hasOverflow = endpoint.resources.size() > 3;
        const float overflowWidth = hasOverflow ? 30.0f : 0.0f;
        const float rowWidth = visibleResourceCount * iconSize +
                               std::max(0, visibleResourceCount - 1) * iconGap + overflowWidth;
        float iconX = endpointDest.x + (endpointDest.width - rowWidth) * 0.5f;
        iconX = std::clamp(iconX, 8.0f,
                          std::max(8.0f, static_cast<float>(GetScreenWidth()) - rowWidth - 8.0f));
        float iconY = endpointDest.y + (endpointDest.height - iconSize) * 0.5f;
        iconY = std::clamp(iconY, 8.0f,
                           std::max(8.0f, static_cast<float>(GetScreenHeight()) - iconSize - 8.0f));
        int index = 0;
        for (ResourceType type : endpoint.resources)
        {
            if (index++ >= visibleResourceCount)
                break;
            GuiPanel::DrawResourceIcon(type, {iconX, iconY, iconSize, iconSize});
            iconX += iconSize + iconGap;
        }
        if (hasOverflow)
            UiText::Draw("+" + std::to_string(endpoint.resources.size() - 3),
                         iconX, iconY + (iconSize - 18.0f) * 0.5f,
                         18, UiTheme::Parchment);
    }

    Rectangle dest = BuildingScreenRect(scene, building);
    DrawRectangleRounded(dest, 0.04f, 8,
                         Color{88, 196, 124, static_cast<unsigned char>(32.0f + pulse * 32.0f)});
    DrawPulsingOutline(dest, Color{112, 230, 150, 220}, 1.25f);

    for (const auto& defense : scene->latestSnapshot.provinceDefenses)
    {
        if (defense.buildingId != building->id || defense.radius <= 0.0)
            continue;
        const Vec2f worldCenter{defense.center.x * TILE_SIZE,
                                defense.center.y * TILE_SIZE};
        const Vec2f screenCenter = scene->render.WorldToScreen(worldCenter);
        const float screenRadius = static_cast<float>(defense.radius * TILE_SIZE *
                                                       scene->render.camera.zoom);
        const Color coverageColor = defense.protectionActive
            ? Color{150, 214, 128, 125} : Color{145, 145, 145, 105};
        DrawCircleLines(static_cast<int>(screenCenter.x), static_cast<int>(screenCenter.y),
                        screenRadius, coverageColor);
        DrawCircleLines(static_cast<int>(screenCenter.x), static_cast<int>(screenCenter.y),
                        std::max(0.0f, screenRadius - 2.0f), coverageColor);
        break;
    }

}

void DemolitionTargetWidget::Update(double dt)
{
    (void)dt;
    if (scene == nullptr || scene->game == nullptr || building == nullptr ||
        !scene->game->GetTileMap().ContainsBuilding(building))
        return;

    const Rectangle dest = BuildingScreenRect(scene, building);
    if (actionable)
    {
        DrawRectangleRounded(dest, 0.04f, 8, Color{125, 20, 26, 42});
        DrawPulsingOutline(dest, Color{255, 82, 76, 220}, 1.5f);
    }
    else
    {
        DrawRectangleRoundedLines(dest, 0.04f, 8, 1.0f, Color{145, 145, 145, 150});
    }
}

void DemolitionTooltipWidget::Update(double dt)
{
    (void)dt;
    if (scene == nullptr || scene->game == nullptr || building == nullptr ||
        !scene->game->GetTileMap().ContainsBuilding(building))
        return;

    const Vector2 mouse = GetMousePosition();
    // Title, action/reason, refund summary and one icon row per recovered
    // resource. The preview is the same pure quote used by authority.
    const int lineCount = preview.allowed
        ? 3 + static_cast<int>(preview.resources.size())
        : 2;
    const float width = 340.0f;
    const float height = 22.0f + lineCount * 24.0f;
    Rectangle box{mouse.x + 14.0f, mouse.y + 14.0f, width, height};
    box.x = std::clamp(box.x, 8.0f, static_cast<float>(GetScreenWidth()) - width - 8.0f);
    box.y = std::clamp(box.y, 8.0f, static_cast<float>(GetScreenHeight()) - height - 8.0f);
    DrawRectangleRounded(box, 0.06f, 8, UiTheme::TooltipBackdrop);
    DrawRectangleRoundedLines(box, 0.06f, 8, 1.0f,
                              preview.allowed ? Color{255, 82, 76, 220} : UiTheme::Iron);
    UiText::Draw(building->name, static_cast<int>(box.x + 10), static_cast<int>(box.y + 7),
                 17, UiTheme::Parchment);
    int y = static_cast<int>(box.y + 29.0f);
    bool hasLostResources = false;
    for (const auto& line : preview.resources)
        hasLostResources = hasLostResources || line.lostAmount > 0;
    const char* actionText = preview.allowed
        ? (hasLostResources ? "Click to demolish; overflow lost" : "Click to demolish")
        : preview.reason.c_str();
    UiText::Draw(actionText,
                 static_cast<int>(box.x + 10), y, 14,
                 preview.allowed ? Color{255, 120, 112, 255} : Color{225, 170, 150, 255});
    y += 24;
    if (preview.allowed)
    {
        UiText::Draw(preview.unfinished ? "Cancellation refund: 100%" : "Refund: 50% base construction payment",
                     static_cast<int>(box.x + 10), y, 13, UiTheme::Parchment);
        y += 24;
        if (preview.resources.empty())
        {
            UiText::Draw("No resources recovered", static_cast<int>(box.x + 10), y, 13,
                         UiTheme::ParchmentDim);
        }
        for (const auto& line : preview.resources)
        {
            GuiPanel::DrawResourceIcon(line.type,
                                       {box.x + 8.0f, static_cast<float>(y), 22.0f, 22.0f});
            std::string text = ResourceDisplayName(line.type) + ": " +
                               std::to_string(line.bufferedAmount) + " buffered";
            if (line.refundAmount > 0)
                text += " + " + std::to_string(line.refundAmount) + " build refund";
            text += " = " + std::to_string(line.returnedAmount) + " returned";
            UiText::DrawFit(text, {box.x + 36.0f, static_cast<float>(y),
                                   box.width - 46.0f, 18.0f}, 12,
                            line.lostAmount > 0 ? UiTheme::RustBright : UiTheme::Parchment);
            if (line.lostAmount > 0)
                UiText::DrawFit("lost " + std::to_string(line.lostAmount),
                                {box.x + 36.0f, static_cast<float>(y + 14),
                                 box.width - 46.0f, 14.0f}, 11, UiTheme::RustBright);
            y += 24;
        }
    }
}

// ─── ProductionWarningWidget ─────────────────────────────────────────────────

// Highlights production buildings that cannot currently work.
void ProductionWarningWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr)
        return;

    Player* localPlayer = GuiLocalPlayer(scene);
    if (localPlayer == nullptr)
        return;

    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 3.2f);
    for (auto* building : localPlayer->GetTrackedBuildings())
    {
        if (building == nullptr || !building->IsProductionStalled())
            continue;

        Rectangle dest = BuildingScreenRect(scene, building);
        DrawRectangleRounded(dest, 0.04f, 8,
                             Color{236, 184, 62, static_cast<unsigned char>(22.0f + pulse * 30.0f)});
        DrawRectangleRoundedLines(dest, 0.04f, 8, 1.0f,
                                  Color{255, 211, 84, static_cast<unsigned char>(135.0f + pulse * 90.0f)});
    }
}

// Shows a short, delayed building status in the default map view. This widget
// deliberately resolves the building from the hovered tile every frame: the
// pointer never survives a simulation update or a building replacement.
void BuildingHoverTooltipWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr)
        return;

    const Vector2 mouse = GetMousePosition();
    const Vec2i tilePos = ScreenToTile(scene, mouse);
    Building* hovered = nullptr;
    if (tilePos.x >= 0 && tilePos.y >= 0 && scene->game->GetTileMap().IsInside(tilePos))
    {
        hovered = scene->game->GetTileMap().GetBuilding(tilePos);
        if (hovered != nullptr && !scene->game->GetTileMap().ContainsBuilding(hovered))
            hovered = nullptr;
    }

    Player* localPlayer = GuiLocalPlayer(scene);
    if (hovered != nullptr && localPlayer != nullptr)
    {
        const TileMap& tilemap = scene->game->GetTileMap();
        const Vec2i anchor = tilemap.GetCoordsFromId(hovered->positionId);
        if (!scene->game->IsBuildFootprintVisibleToPlayer(localPlayer->id,
                                                          anchor,
                                                          hovered->GetFootprint()))
            hovered = nullptr;
    }

    if (hovered == nullptr)
    {
        hoveredBuildingId = -1;
        hoverDuration = 0.0;
        connectivityBuildingId = -1;
        connectivityAge = 0.0;
        connectivityKnown = false;
        roadDisconnected = false;
        return;
    }

    // Give the hovered footprint immediate feedback. The delayed tooltip is
    // still intentional, but the footprint uses the same conversion as every
    // other building overlay so it never drifts from the sprite.
    const Rectangle hoveredBounds = BuildingScreenRect(scene, hovered);
    DrawRectangleRounded(hoveredBounds, 0.04f, 8, Color{255, 255, 255, 10});
    DrawRectangleRoundedLines(hoveredBounds, 0.04f, 8, 1.0f,
                              Color{255, 255, 255, 90});

    if (hoveredBuildingId != hovered->id)
    {
        hoveredBuildingId = hovered->id;
        hoverDuration = 0.0;
        connectivityBuildingId = -1;
        connectivityAge = 0.0;
        connectivityKnown = false;
        roadDisconnected = false;
    }
    else
        hoverDuration += std::max(0.0, dt);

    constexpr double ConnectivityRefreshSeconds = 0.50;
    const bool isOwnBuilding = hovered->owner == localPlayer;
    if (isOwnBuilding && hovered->HasComponent<LogisticsComponent>() && hoverDuration >= 0.24)
    {
        if (connectivityBuildingId != hovered->id)
        {
            connectivityBuildingId = hovered->id;
            connectivityAge = ConnectivityRefreshSeconds;
            connectivityKnown = false;
        }
        connectivityAge += std::max(0.0, dt);
        if (!connectivityKnown || connectivityAge >= ConnectivityRefreshSeconds)
        {
            const auto* logistics = hovered->GetComponent<LogisticsComponent>();
            roadDisconnected = logistics != nullptr &&
                               !logistics->IsConnectedToRoadNetwork(*hovered);
            connectivityKnown = true;
            connectivityAge = 0.0;
        }
    }

    if (hoverDuration < 0.24)
        return;

    const BuildingPresentationStatus status = BuildBuildingPresentationStatus(
        *hovered,
        isOwnBuilding && connectivityKnown && roadDisconnected,
        isOwnBuilding ? BuildingPresentationAudience::Owner
                      : BuildingPresentationAudience::Opponent);
    if (status.general.empty())
        return;

    std::vector<std::string> lines{status.general};
    if (isOwnBuilding && !status.production.empty() && status.production != status.general)
        lines.push_back(status.production);

    std::vector<ResourceType> titleResources;
    if (isOwnBuilding)
    {
        if (const auto* production = hovered->GetComponent<ProductionComponent>();
            production != nullptr && !production->products.empty())
        {
            std::string output = "Output per cycle: ";
            bool first = true;
            for (const auto& [type, amount] : production->products)
            {
                if (!first)
                    output += ", ";
                output += ResourceDisplayName(type) + " x" + std::to_string(amount);
                titleResources.push_back(type);
                first = false;
            }
            lines.push_back(std::move(output));
        }

        if (const auto* upgrade = hovered->GetComponent<UpgradeComponent>(); upgrade != nullptr)
            lines.push_back("Level: " + std::to_string(upgrade->level));

        if (hovered->GetWorkerCapacity() > 0)
            lines.push_back("Workers: " + std::to_string(hovered->GetAssignedWorkers()) +
                            " / " + std::to_string(hovered->GetWorkerCapacity()));
    }

    const int visibleTitleResources = std::min(3, static_cast<int>(titleResources.size()));
    const float titleIconSize = 34.0f;
    const float titleIconGap = 3.0f;
    const float titleIconWidth = visibleTitleResources > 0
        ? visibleTitleResources * titleIconSize +
              std::max(0, visibleTitleResources - 1) * titleIconGap
        : 0.0f;
    std::function<void(Rectangle)> drawTitleIcons;
    if (visibleTitleResources > 0)
    {
        drawTitleIcons = [resources = std::move(titleResources), visibleTitleResources,
                          titleIconSize, titleIconGap](Rectangle bounds)
        {
            float x = bounds.x;
            for (int index = 0; index < visibleTitleResources; ++index)
            {
                GuiPanel::DrawResourceIcon(resources[static_cast<size_t>(index)],
                    {x, bounds.y + (bounds.height - titleIconSize) * 0.5f,
                     titleIconSize, titleIconSize});
                x += titleIconSize + titleIconGap;
            }
        };
    }
    Tooltip::Draw(hovered->name, lines, 360.0f, drawTitleIcons, 24, titleIconWidth);
}

void UpgradeTargetWidget::Update(double dt)
{
    (void)dt;
    if (scene == nullptr || scene->game == nullptr || building == nullptr ||
        !scene->game->GetTileMap().ContainsBuilding(building))
        return;

    const Rectangle bounds = BuildingScreenRect(scene, building);
    const Color fill = actionable ? Color{74, 190, 192, 42} : Color{116, 130, 134, 28};
    const Color line = actionable ? UiTheme::Cyan : UiTheme::ParchmentDim;
    DrawRectangleRounded(bounds, 0.04f, 8, fill);
    DrawRectangleRoundedLines(bounds, 0.04f, 8, actionable ? 2.5f : 1.5f, line);
}
