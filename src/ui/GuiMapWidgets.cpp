// Map-layer widgets: selection and production-warning highlights.
// TD(etap-1): the division/army/battle widgets that used to live here
// (MilitaryOrderWidget, MilitaryDivisionBarWidget, ArmyBarWidget,
// DivisionMapWidget, MoveTargetWidget, ArmyOrderPanelWidget) were removed along
// with the old war system. The Tower Defense rework adds fresh unit/roster
// widgets starting in ETAP 3-4.

#include "GuiInternal.h"

#include "scenes/Scenes.h"
#include "economy/Player.h"
#include "economy/BuildingComponents.h"
#include "warfare/CombatPipeline.h"
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

    // T7 (docs/post_pivot_audit_2026-07-12.md): draws a semi-transparent range
    // ring for a selected DefenseTower, centered on its footprint. Screen
    // radius derived from two WorldToScreen calls (center, center+radius)
    // rather than assuming a direct zoom multiplier — same approach
    // BuildingScreenRect already uses for width/height.
    void DrawTowerRangeRing(GameScene* scene, Building* building)
    {
        const auto* combat = building->GetComponent<TowerCombatComponent>();
        if (combat == nullptr)
            return;

        Vec2f worldCenter = ComputeBuildingCenter(scene->game->GetTileMap(), *building);
        double rangePixels = combat->GetModifiedRange(*building) * TILE_SIZE;

        Vec2f screenCenter = scene->render.WorldToScreen(worldCenter);
        Vec2f screenEdge = scene->render.WorldToScreen({worldCenter.x + static_cast<float>(rangePixels), worldCenter.y});
        float screenRadius = std::abs(screenEdge.x - screenCenter.x);

        DrawCircleV({screenCenter.x, screenCenter.y}, screenRadius, Color{236, 92, 74, 28});
        DrawCircleLinesV({screenCenter.x, screenCenter.y}, screenRadius, Color{255, 120, 100, 200});
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
        const float iconSize = 20.0f;
        const float iconGap = 3.0f;
        const bool hasOverflow = endpoint.resources.size() > 3;
        const float overflowWidth = hasOverflow ? 25.0f : 0.0f;
        const float rowWidth = visibleResourceCount * iconSize +
                               std::max(0, visibleResourceCount - 1) * iconGap + overflowWidth;
        float iconX = endpointDest.x + (endpointDest.width - rowWidth) * 0.5f;
        iconX = std::clamp(iconX, 8.0f,
                          std::max(8.0f, static_cast<float>(GetScreenWidth()) - rowWidth - 8.0f));
        float iconY = endpointDest.y - iconSize - 5.0f;
        if (iconY < 8.0f)
            iconY = endpointDest.y + endpointDest.height + 5.0f;
        iconY = std::clamp(iconY, 8.0f, static_cast<float>(GetScreenHeight()) - iconSize - 8.0f);
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
                         iconX, iconY + 3.0f, 14, UiTheme::Parchment);
    }

    Rectangle dest = BuildingScreenRect(scene, building);
    DrawRectangleRounded(dest, 0.04f, 8,
                         Color{88, 196, 124, static_cast<unsigned char>(32.0f + pulse * 32.0f)});
    DrawPulsingOutline(dest, Color{112, 230, 150, 220}, 1.25f);

    DrawTowerRangeRing(scene, building);
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
    // title + action/reason + refund summary + one line per resource
    const int lineCount = 3 + static_cast<int>(preview.resources.size());
    const float width = 300.0f;
    const float height = 22.0f + lineCount * 20.0f;
    Rectangle box{mouse.x + 14.0f, mouse.y + 14.0f, width, height};
    box.x = std::clamp(box.x, 8.0f, static_cast<float>(GetScreenWidth()) - width - 8.0f);
    box.y = std::clamp(box.y, 8.0f, static_cast<float>(GetScreenHeight()) - height - 8.0f);
    DrawRectangleRounded(box, 0.06f, 8, Color{20, 22, 28, 240});
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
    y += 20;
    if (preview.allowed)
    {
        UiText::Draw(preview.unfinished ? "Cancellation refund: 100%" : "Refund: 50% base construction payment",
                     static_cast<int>(box.x + 10), y, 13, UiTheme::Parchment);
        y += 20;
        for (const auto& line : preview.resources)
        {
            std::string text = rt2s(line.type) + ": " + std::to_string(line.bufferedAmount);
            if (line.refundAmount > 0)
                text += " +" + std::to_string(line.refundAmount) + " refund";
            if (line.lostAmount > 0)
                text += " (" + std::to_string(line.lostAmount) + " lost)";
            UiText::Draw(text, static_cast<int>(box.x + 10), y, 13, UiTheme::Parchment);
            y += 20;
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
