// Player-wide stockpile panel (E) + its GuiSystem.
//
// The counterpart to a building's own panel: that one shows only what the
// selected warehouse physically holds, this one shows the totals across the
// whole warehouse network, with a per-warehouse breakdown on hover (user
// request, 2026-07-25). Both read StockpileIndex, so they can never disagree.

#include "GuiInternal.h"

#include "ui/ControlIcons.h"
#include "scenes/Scenes.h"
#include "economy/Player.h"
#include "economy/StockpileIndex.h"
#include "raygui.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float PanelMargin = 24.0f;
    constexpr float HeaderHeight = 54.0f;
    constexpr float SummaryHeight = 96.0f;
    constexpr int MaximumColumns = 5;
    constexpr float CardGap = 12.0f;
    // Leave permanent space for raygui's vertical scrollbar so a long list
    // never causes a surprise horizontal scrollbar.
    constexpr float ScrollbarReserve = 24.0f;

    // One card per resource, sized so the amount stays readable at a glance
    // and the capacity bar has room underneath it.
    void DrawStockpileCard(Rectangle cell, ResourceType type, const StockpileTotals& totals, bool hovered)
    {
        if (!UiControlIcons::DrawPixelHudWidgetFrame(cell, hovered))
        {
            DrawRectangleRounded(cell, 0.10f, 8, hovered ? UiTheme::InsetHover : UiTheme::Inset);
            DrawRectangleRoundedLines(cell, 0.10f, 8, 1.0f, hovered ? UiTheme::SteelHover : UiTheme::Iron);
        }

        float iconSize = std::clamp(cell.height * 0.58f, 64.0f, 92.0f);
        float iconY = cell.y + 14.0f;
        GuiPanel::DrawResourceIcon(type, Rectangle{cell.x + 14.0f, iconY, iconSize, iconSize});

        float textX = cell.x + iconSize + 26.0f;
        float textWidth = cell.width - iconSize - 40.0f;
        UiText::DrawFit(ResourceDisplayName(type),
                        Rectangle{textX, cell.y + 15.0f, textWidth, 23.0f}, 18, UiTheme::ParchmentDim);
        UiText::DrawFit(std::to_string(totals.amount),
                        Rectangle{textX, cell.y + 42.0f, textWidth, 32.0f}, 28, UiTheme::Parchment);

        // Fill bar: how close the network is to running out of room for this
        // type — the thing that silently stalls a production chain.
        Rectangle track{cell.x + 14.0f, cell.y + cell.height - 17.0f, cell.width - 28.0f, 5.0f};
        DrawRectangleRounded(track, 0.6f, 6, UiTheme::Ink);
        if (totals.capacity > 0 && totals.amount > 0)
        {
            float fill = std::clamp(static_cast<float>(totals.amount) / totals.capacity, 0.0f, 1.0f);
            Rectangle filled{track.x, track.y, track.width * fill, track.height};
            DrawRectangleRounded(filled, 0.6f, 6, fill > 0.9f ? UiTheme::AmberBright : Color{150, 180, 98, 230});
        }

        // Split across more than one warehouse — worth surfacing on the card
        // itself so the hover tooltip is a detail view, not a discovery tool.
        if (totals.holdings.size() > 1)
        {
            std::string badge = std::to_string(totals.holdings.size()) + "x";
            int width = UiText::Measure(badge, 12);
            Rectangle chip{cell.x + cell.width - width - 16.0f, cell.y + 12.0f, static_cast<float>(width + 8), 16.0f};
            DrawRectangleRounded(chip, 0.3f, 6, UiTheme::Panel);
            UiText::Draw(badge, chip.x + 4.0f, chip.y + 2.0f, 12, UiTheme::ParchmentDim);
        }
    }
}

// ─── StockpilePanelWidget ────────────────────────────────────────────────────

Rectangle StockpilePanelWidget::GetGridRect() const
{
    return Rectangle{
        pos.x + PanelMargin,
        pos.y + HeaderHeight + SummaryHeight,
        size.x - PanelMargin * 2.0f,
        size.y - HeaderHeight - SummaryHeight - PanelMargin};
}

void StockpilePanelWidget::Update(double dt)
{
    (void)dt;
    if (scene == nullptr || scene->game == nullptr)
        return;

    Player* player = GuiLocalPlayer(scene);
    if (player == nullptr)
        return;

    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y),
                     static_cast<float>(size.x), static_cast<float>(size.y)};
    if (!UiControlIcons::DrawPixelHudFrame(bounds))
    {
        DrawRectangleRounded(bounds, 0.025f, 8, UiTheme::Panel);
        DrawRectangleRoundedLines(bounds, 0.025f, 8, 1.0f, UiTheme::Iron);
    }
    const float chromeInset = UiControlIcons::PixelHudFrameInset(bounds);
    Rectangle title{bounds.x + chromeInset + 2.0f, bounds.y + 4.0f,
                    bounds.width - (chromeInset + 2.0f) * 2.0f, HeaderHeight - 8.0f};
    UiText::DrawTitleBar(title, "Stockpile", PanelTitleCloseReserve(bounds));
    DrawCloseButton(bounds);

    auto warehouses = StockpileIndex::Warehouses(*player);
    auto snapshot = StockpileIndex::Snapshot(*player);

    int distinctTypes = 0;
    int totalUnits = 0;
    int totalCapacity = 0;
    for (const auto& [type, totals] : snapshot)
    {
        totalUnits += totals.amount;
        totalCapacity += totals.capacity;
        if (totals.amount > 0)
            distinctTypes++;
    }

    float summaryY = bounds.y + HeaderHeight + 8.0f;
    UiText::DrawFit("Warehouses: " + std::to_string(warehouses.size()) +
                        "   Resource types held: " + std::to_string(distinctTypes) +
                        "   Total units: " + std::to_string(totalUnits) +
                        " / " + std::to_string(totalCapacity),
                    Rectangle{bounds.x + PanelMargin, summaryY, bounds.width - PanelMargin * 2.0f, 24.0f},
                    20, UiTheme::Parchment);
    UiText::DrawFit("Each warehouse keeps its own stock - this is the sum. Hover a resource to see where it sits.",
                    Rectangle{bounds.x + PanelMargin, summaryY + 28.0f, bounds.width - PanelMargin * 2.0f, 20.0f},
                    16, UiTheme::ParchmentDim);

    if (warehouses.empty())
    {
        UiText::DrawFit("No warehouses", Rectangle{bounds.x + PanelMargin, bounds.y + bounds.height * 0.45f,
                                                   bounds.width - PanelMargin * 2.0f, 26.0f},
                        22, UiTheme::ParchmentDim);
        return;
    }

    // Only types the network actually holds — a grid of ~45 empty cells is
    // noise, and the capacity a warehouse "could" hold of everything is not
    // information the player acts on.
    std::vector<std::pair<ResourceType, StockpileTotals>> cards;
    for (const auto& entry : snapshot)
        if (entry.second.amount > 0)
            cards.push_back(entry);

    std::stable_sort(cards.begin(), cards.end(), [&](const auto& left, const auto& right)
    {
        return ResourcePresentationRank(left.first) < ResourcePresentationRank(right.first);
    });

    Rectangle grid = GetGridRect();
    if (cards.empty())
    {
        maxScrollOffset = 0.0f;
        scrollOffset = 0.0f;
        UiText::DrawFit("Every warehouse is empty",
                        Rectangle{grid.x, grid.y + 12.0f, grid.width, 26.0f}, 22, UiTheme::ParchmentDim);
        return;
    }

    // The full stockpile gets the same readable density as HQ/storage:
    // 3 columns on compact windows, then 4 and 5 as space permits.
    const int columns = grid.width >= 1100.0f ? MaximumColumns
                      : grid.width >= 720.0f ? 4
                                               : 3;
    const float contentWidth = std::max(120.0f, grid.width - ScrollbarReserve);
    const float cardWidth = (contentWidth - CardGap * (columns - 1)) / columns;
    const float cardHeight = std::clamp(cardWidth * 0.60f, 132.0f, 176.0f);
    int rows = static_cast<int>((cards.size() + columns - 1) / columns);
    float contentHeight = rows * cardHeight + std::max(0, rows - 1) * CardGap;
    Rectangle view{grid.x, grid.y, contentWidth, grid.height};
    maxScrollOffset = std::max(0.0f, contentHeight - view.height);
    scrollOffset = std::clamp(scrollOffset, 0.0f, maxScrollOffset);

    int hoveredIndex = -1;
    BeginScissorMode(static_cast<int>(view.x), static_cast<int>(view.y),
                     static_cast<int>(view.width), static_cast<int>(view.height));
    for (int i = 0; i < static_cast<int>(cards.size()); i++)
    {
        float x = view.x + (i % columns) * (cardWidth + CardGap);
        float y = view.y + (i / columns) * (cardHeight + CardGap) - scrollOffset;
        if (y > view.y + view.height)
            break;
        if (y + cardHeight < view.y)
            continue;

        Rectangle cell{x, y, cardWidth, cardHeight};
        bool hovered = CheckCollisionPointRec(GetMousePosition(), cell);
        DrawStockpileCard(cell, cards[i].first, cards[i].second, hovered);
        if (hovered)
            hoveredIndex = i;
    }
    EndScissorMode();

    if (maxScrollOffset > 0.0f)
    {
        Rectangle track{grid.x + contentWidth + 7.0f, grid.y, 9.0f, grid.height};
        DrawRectangleRounded(track, 0.5f, 6, UiTheme::Inset);
        float thumbH = std::max(30.0f, track.height * (track.height / (track.height + maxScrollOffset)));
        float thumbY = track.y + (track.height - thumbH) * (scrollOffset / maxScrollOffset);
        Rectangle thumb{track.x, thumbY, track.width, thumbH};
        Vector2 mouse = GetMousePosition();
        if (InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(mouse, thumb))
        {
            scrollbarDragging = true;
            scrollbarDragOffset = mouse.y - thumb.y;
        }
        if (scrollbarDragging && InputManager::IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            float thumbRange = track.height - thumbH;
            float targetThumbY = std::clamp(mouse.y - scrollbarDragOffset,
                                           track.y, track.y + thumbRange);
            float normalized = thumbRange > 0.0f
                ? (targetThumbY - track.y) / thumbRange
                : 0.0f;
            scrollOffset = normalized * maxScrollOffset;
            thumbY = targetThumbY;
        }
        if (scrollbarDragging && InputManager::IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
            scrollbarDragging = false;
        DrawRectangleRounded(Rectangle{track.x, thumbY, track.width, thumbH}, 0.5f, 6,
                             UiTheme::Iron);
    }

    // Drawn last so it is never clipped by the scroll viewport or overdrawn by
    // a neighbouring card.
    if (hoveredIndex >= 0)
        DrawResourceTooltip(cards[hoveredIndex].first,
                            StockpileTooltipLines(player, cards[hoveredIndex].first), 280.0f);
}

// ─── StockpileGuiSystem ──────────────────────────────────────────────────────

StockpileGuiSystem::StockpileGuiSystem(GuiController* con)
    : GuiSystem(con)
{
    // A4 (docs/work_plan_2026-07-13.md): shadows GuiSystem::scene (Scene*).
    scene = dynamic_cast<GameScene*>(owner->scene);

    WireCommonSystemActions(*this, cameraMovement);

    stockpilePanel.scene = scene;
    stockpilePanel.ChangePositionAnchor({0.06f, 0.15f});
    stockpilePanel.ChangeSizeAnchor({0.88f, 0.82f});
    stockpilePanel.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    SetupStrategicHud(strategicHudWidget, scene);
}

void StockpileGuiSystem::UpdateUiWidgets(Vec2i size)
{
    stockpilePanel.UpdateSize(size);
    strategicHudWidget.UpdateSize(size);
}

void StockpileGuiSystem::Update(double dt)
{
    if (scene->game == nullptr)
        return;

    ApplyStrategicHudCameraPadding(scene);
    MoveCamera(scene, cameraMovement);
    owner->AddUiWidget(&stockpilePanel);
    owner->AddUiWidget(&strategicHudWidget);
}

void StockpileGuiSystem::EscPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("default");
}

void StockpileGuiSystem::BuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("build");
}

void StockpileGuiSystem::RoadBuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("road_build");
}

void StockpileGuiSystem::DestroyPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("destroy");
}

void StockpileGuiSystem::StockpilePressed()
{
    EscPressed();
}

void StockpileGuiSystem::StatsPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("stats");
}

void StockpileGuiSystem::FocusPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("focus");
}

void StockpileGuiSystem::TechPressed()
{
    if (!HasUniversity(scene))
        return;
    cameraMovement.isMoving = false;
    owner->ChangeSystem("tech");
}

void StockpileGuiSystem::RosterPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("roster");
}

void StockpileGuiSystem::LmbPressed()
{
    if (DispatchHudButtonClick(*this, strategicHudWidget))
        return;

    Rectangle panelBounds{
        static_cast<float>(stockpilePanel.pos.x),
        static_cast<float>(stockpilePanel.pos.y),
        static_cast<float>(stockpilePanel.size.x),
        static_cast<float>(stockpilePanel.size.y)};
    if (CheckCollisionPointRec(GetMousePosition(), PanelCloseButtonRect(panelBounds)))
        EscPressed();
}

void StockpileGuiSystem::LmbReleased()
{
}

void StockpileGuiSystem::RmbPressed()
{
    Rectangle panelBounds{
        static_cast<float>(stockpilePanel.pos.x),
        static_cast<float>(stockpilePanel.pos.y),
        static_cast<float>(stockpilePanel.size.x),
        static_cast<float>(stockpilePanel.size.y)};
    if (CheckCollisionPointRec(GetMousePosition(), panelBounds))
    {
        cameraMovement.isMoving = false;
        return;
    }
    cameraMovement.isMoving = true;
}

void StockpileGuiSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

void StockpileGuiSystem::Scroll()
{
    // Scroll the stockpile only while the pointer is over its panel. Elsewhere
    // retain camera zoom.
    Rectangle panelBounds{
        static_cast<float>(stockpilePanel.pos.x),
        static_cast<float>(stockpilePanel.pos.y),
        static_cast<float>(stockpilePanel.size.x),
        static_cast<float>(stockpilePanel.size.y)};
    if (!CheckCollisionPointRec(GetMousePosition(), panelBounds))
    {
        ZoomCamera(scene);
        return;
    }
    stockpilePanel.Scroll(InputManager::GetMouseWheelMove());
}

void StockpilePanelWidget::Scroll(float wheel)
{
    if (wheel == 0.0f || maxScrollOffset <= 0.0f)
        return;
    scrollOffset = std::clamp(scrollOffset - wheel * 42.0f, 0.0f, maxScrollOffset);
}
