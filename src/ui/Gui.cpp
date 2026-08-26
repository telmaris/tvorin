#include "ui/Gui.h"
#include "ui/ControlIcons.h"
#include "ui/BuildingUpgradeView.h"
#include "ui/UiTheme.h"
#include "economy/Building.h"
#include "economy/BuildingConfig.h"
#include "economy/Player.h"
#include "economy/StockpileIndex.h"
#include "research/ResearchCatalog.h"
#include "research/Technology.h"
#include "warfare/UnitDefinition.h"
#include "core/GameCommand.h"
#include "scenes/Scenes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>

#include "raylib.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

namespace
{
    ResourceIconAtlas resourceIconAtlas;

    void DrawTextFit(const std::string& text, Rectangle bounds, int fontSize, Color color);

    struct PendingTooltip
    {
        bool visible{false};
        std::string title;
        std::vector<std::string> lines;
        float preferredWidth{0.0f};
        ResourceType resourceType{ResourceType::Null};
    };

    bool HasNodeTag(const ResearchNodeView& node, const std::string& tag)
    {
        return tag.empty() || std::find(node.tags.begin(), node.tags.end(), tag) != node.tags.end();
    }

    std::vector<std::string> CollectVisibleTags(const std::vector<ResearchNodeView>& nodes)
    {
        std::vector<std::string> tags;
        for (const auto& node : nodes)
        {
            for (const auto& tag : node.tags)
            {
                if (std::find(tags.begin(), tags.end(), tag) == tags.end())
                    tags.push_back(tag);
            }
        }
        std::sort(tags.begin(), tags.end());
        if (tags.size() > 10)
            tags.resize(10);
        return tags;
    }

    void DrawTagFilterBar(Rectangle bounds, const std::vector<std::string>& tags, std::string& selectedTag)
    {
        Vector2 mouse = GetMousePosition();
        float x = bounds.x;
        auto drawButton = [&](const std::string& label, const std::string& value)
        {
            float width = std::min(112.0f, std::max(54.0f, static_cast<float>(MeasureText(label.c_str(), 14) + 22)));
            Rectangle rect{x, bounds.y, width, bounds.height};
            bool selected = selectedTag == value;
            bool hover = CheckCollisionPointRec(mouse, rect);
            DrawRectangleRounded(rect, 0.20f, 6, selected ? UiTheme::SelectedFill : hover ? UiTheme::SurfaceHover : UiTheme::Inset);
            DrawRectangleRoundedLines(rect, 0.20f, 6, 1.0f, selected ? UiTheme::SageBright : UiTheme::Iron);
            DrawTextFit(label, Rectangle{rect.x + 8.0f, rect.y + 4.0f, rect.width - 16.0f, rect.height - 8.0f}, 14, selected ? UiTheme::Parchment : UiTheme::ParchmentDim);
            if (hover && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                selectedTag = value;
            x += width + 8.0f;
            return x < bounds.x + bounds.width - 44.0f;
        };

        if (!drawButton("All", ""))
            return;
        for (const auto& tag : tags)
            if (!drawButton(tag, tag))
                break;
    }

    PendingTooltip pendingTooltip;

    // Returns the current rectangle occupied by a UI widget.
    Rectangle WidgetBounds(const UiWidget& widget)
    {
        return Rectangle{
            static_cast<float>(widget.pos.x),
            static_cast<float>(widget.pos.y),
            static_cast<float>(widget.size.x),
            static_cast<float>(widget.size.y)};
    }

    // These four forward to the shared implementation in ui/UiText.cpp. They stay
    // as file-local names so the call sites throughout this file did not have to
    // change when the implementation moved out.
    int MeasureUiText(const std::string& text, int fontSize)
    {
        return UiText::Measure(text, fontSize);
    }

    void DrawUiText(const std::string& text, float x, float y, int fontSize, Color color)
    {
        UiText::Draw(text, x, y, fontSize, color);
    }

    // Returns a fallback swatch color for a resource type.
    Color ResourceColor(ResourceType type)
    {
        switch (type)
        {
            case ResourceType::COPPER_ORE: return Color{148, 92, 62, 255};
            case ResourceType::COPPER: return Color{191, 111, 68, 255};
            case ResourceType::WOOD: return Color{126, 87, 54, 255};
            case ResourceType::IRON_ORE: return Color{122, 126, 133, 255};
            case ResourceType::SILVER_ORE: return Color{156, 166, 174, 255};
            case ResourceType::SILVER: return Color{205, 214, 220, 255};
            case ResourceType::GOLD_ORE: return Color{160, 132, 60, 255};
            case ResourceType::GOLD: return Color{228, 181, 65, 255};
            case ResourceType::COAL: return Color{42, 43, 45, 255};
            case ResourceType::IRON: return Color{175, 176, 180, 255};
            case ResourceType::PLANKS: return Color{190, 139, 78, 255};
            case ResourceType::STONE: return Color{126, 124, 116, 255};
            case ResourceType::LEATHER: return Color{128, 75, 45, 255};
            case ResourceType::MEAT: return Color{176, 77, 69, 255};
            case ResourceType::WHEAT: return Color{214, 179, 83, 255};
            case ResourceType::FLOUR: return Color{224, 214, 184, 255};
            case ResourceType::BREAD: return Color{180, 120, 55, 255};
            case ResourceType::WATER: return Color{75, 146, 214, 255};
            case ResourceType::BEER: return Color{184, 128, 48, 255};
            case ResourceType::COINS: return Color{230, 190, 82, 255};
            case ResourceType::FOOD_PROVISIONS: return Color{113, 162, 92, 255};
            case ResourceType::PAPER: return Color{213, 211, 190, 255};
            case ResourceType::TOOLS: return Color{120, 136, 145, 255};
            case ResourceType::IRON_SWORD: return Color{153, 160, 170, 255};
            case ResourceType::STEEL_SWORD: return Color{185, 198, 205, 255};
            case ResourceType::BOW: return Color{137, 91, 48, 255};
            case ResourceType::ARROWS: return Color{174, 144, 92, 255};
            case ResourceType::HORSE: return Color{130, 91, 67, 255};
            case ResourceType::SAND: return Color{207, 194, 151, 255};
            case ResourceType::GLASS: return Color{151, 205, 211, 255};
            case ResourceType::CLAY: return Color{151, 91, 64, 255};
            case ResourceType::CATTLE: return Color{117, 82, 58, 255};
            case ResourceType::RAW_HIDE: return Color{151, 103, 70, 255};
            case ResourceType::TALLOW: return Color{213, 196, 145, 255};
            case ResourceType::CLOTHES:
            case ResourceType::CLOTH: return Color{123, 142, 170, 255};
            case ResourceType::POTTERY:
            case ResourceType::BRICKS: return Color{173, 91, 60, 255};
            case ResourceType::HOUSEHOLD_GOODS: return Color{143, 117, 84, 255};
            case ResourceType::SOAP: return Color{206, 201, 174, 255};
            case ResourceType::INK: return Color{48, 44, 51, 255};
            case ResourceType::BOOKS: return Color{125, 73, 55, 255};
            case ResourceType::COPPERWARE:
            case ResourceType::COPPER_VESSEL:
            case ResourceType::COPPER_PIPE: return Color{194, 112, 65, 255};
            case ResourceType::URBAN_GOODS: return Color{145, 94, 153, 255};
            case ResourceType::HEMP:
            case ResourceType::FIBRE:
            case ResourceType::ROPE: return Color{134, 150, 92, 255};
            case ResourceType::MECHANICAL_PARTS: return Color{134, 123, 108, 255};
            default: return Color{88, 92, 98, 255};
        }
    }

    // Returns a short fallback label for a resource icon.
    const char* ResourceShortName(ResourceType type)
    {
        switch (type)
        {
            case ResourceType::COPPER_ORE: return "CuO";
            case ResourceType::COPPER: return "Cu";
            case ResourceType::WOOD: return "W";
            case ResourceType::IRON_ORE: return "Ore";
            case ResourceType::SILVER_ORE: return "AgO";
            case ResourceType::SILVER: return "Ag";
            case ResourceType::GOLD_ORE: return "AuO";
            case ResourceType::GOLD: return "Au";
            case ResourceType::COAL: return "C";
            case ResourceType::IRON: return "Fe";
            case ResourceType::PLANKS: return "Pl";
            case ResourceType::STONE: return "St";
            case ResourceType::LEATHER: return "Le";
            case ResourceType::MEAT: return "Mt";
            case ResourceType::WHEAT: return "Wh";
            case ResourceType::FLOUR: return "Fl";
            case ResourceType::BREAD: return "Br";
            case ResourceType::WATER: return "Wa";
            case ResourceType::BEER: return "Be";
            case ResourceType::COINS: return "$";
            case ResourceType::FOOD_PROVISIONS: return "Fd";
            case ResourceType::PAPER: return "Pa";
            case ResourceType::TOOLS: return "Tl";
            case ResourceType::IRON_SWORD: return "Sw";
            case ResourceType::STEEL_SWORD: return "StS";
            case ResourceType::BOW: return "Bw";
            case ResourceType::ARROWS: return "Arr";
            case ResourceType::HORSE: return "Ho";
            case ResourceType::SAND: return "Sa";
            case ResourceType::GLASS: return "Gl";
            case ResourceType::CLAY: return "Cl";
            case ResourceType::CATTLE: return "Ct";
            case ResourceType::RAW_HIDE: return "Hd";
            case ResourceType::TALLOW: return "Ta";
            case ResourceType::CLOTHES: return "Clt";
            case ResourceType::POTTERY: return "Pot";
            case ResourceType::HOUSEHOLD_GOODS: return "HH";
            case ResourceType::SOAP: return "So";
            case ResourceType::INK: return "Ink";
            case ResourceType::BOOKS: return "Bk";
            case ResourceType::COPPERWARE: return "CuW";
            case ResourceType::URBAN_GOODS: return "UG";
            case ResourceType::HEMP: return "He";
            case ResourceType::FIBRE: return "Fi";
            case ResourceType::ROPE: return "Ro";
            case ResourceType::COPPER_VESSEL: return "Ves";
            case ResourceType::COPPER_PIPE: return "Pip";
            case ResourceType::MECHANICAL_PARTS: return "Mec";
            case ResourceType::HEAVY_BOW: return "HB";
            case ResourceType::HEAVY_ARMOR: return "HA";
            case ResourceType::BRICKS: return "Bri";
            case ResourceType::CLOTH: return "Clo";
            case ResourceType::BALLISTA: return "Bal";
            case ResourceType::BATTERING_RAM: return "Ram";
            case ResourceType::CATAPULT: return "Cat";
            default: return "?";
        }
    }

    // Draws text that shrinks until it fits inside the target rectangle.
    void DrawTextFit(const std::string& text, Rectangle bounds, int fontSize, Color color)
    {
        UiText::DrawFit(text, bounds, fontSize, color);
    }

    // Forward to the shared implementation in ui/UiText.cpp.
    void RemoveLastUtf8Codepoint(std::string& value)
    {
        Utf8::RemoveLast(value);
    }

    std::string EncodeUtf8Codepoint(int codepoint)
    {
        return Utf8::Encode(codepoint);
    }

    void SyncTextBoxBuffer(const std::string& value, char* buffer, size_t bufferSize)
    {
        if (bufferSize == 0)
            return;

        std::snprintf(buffer, bufferSize, "%s", value.c_str());
    }

    std::string TooltipBonusLine(const std::string& text)
    {
        return "{bonus}" + text;
    }

    std::string TooltipPenaltyLine(const std::string& text)
    {
        return "{penalty}" + text;
    }

    std::string TooltipSeparatorLine()
    {
        return "{separator}";
    }

    // Wraps prose to fit a fixed width while keeping the requested font size.
    std::vector<std::string> WrapText(const std::string& text, int fontSize, float maxWidth)
    {
        return UiText::Wrap(text, fontSize, maxWidth);
    }

    // Draws centered wrapped text without shrinking the requested font size.
    void DrawTextWrappedCentered(const std::string& text, Rectangle bounds, int fontSize, Color color, int maxLines = 2)
    {
        std::vector<std::string> lines = WrapText(text, fontSize, bounds.width);
        if (static_cast<int>(lines.size()) > maxLines)
        {
            lines.resize(maxLines);
            while (!lines.back().empty() && MeasureUiText(lines.back() + "...", fontSize) > bounds.width)
                lines.back().pop_back();
            lines.back() += "...";
        }

        float lineH = static_cast<float>(fontSize) + 2.0f;
        float totalH = lineH * static_cast<float>(lines.size());
        float y = bounds.y + (bounds.height - totalH) * 0.5f;
        for (const auto& line : lines)
        {
            int measured = MeasureUiText(line, fontSize);
            DrawUiText(line, bounds.x + (bounds.width - measured) * 0.5f, y, fontSize, color);
            y += lineH;
        }
    }

    // Queues a tooltip for the final overlay pass.
    void QueueTooltip(const std::string& title, std::vector<std::string> lines, float preferredWidth = 0.0f)
    {
        pendingTooltip.visible = true;
        pendingTooltip.title = title;
        pendingTooltip.lines = std::move(lines);
        pendingTooltip.preferredWidth = preferredWidth;
        pendingTooltip.resourceType = ResourceType::Null;
    }

    void QueueResourceTooltip(ResourceType type, std::vector<std::string> lines, float preferredWidth = 0.0f)
    {
        pendingTooltip.visible = true;
        pendingTooltip.title = ResourceDisplayName(type);
        pendingTooltip.lines = std::move(lines);
        pendingTooltip.preferredWidth = preferredWidth;
        pendingTooltip.resourceType = type;
    }

    // Draws the queued tooltip above all panel content.
    void DrawPendingTooltip()
    {
        if (!pendingTooltip.visible)
            return;

        if (pendingTooltip.resourceType != ResourceType::Null)
        {
            const ResourceType type = pendingTooltip.resourceType;
            Tooltip::Draw(pendingTooltip.title, pendingTooltip.lines, pendingTooltip.preferredWidth,
                          [type](Rectangle icon) { GuiPanel::DrawResourceIcon(type, icon); });
        }
        else
            Tooltip::Draw(pendingTooltip.title, pendingTooltip.lines, pendingTooltip.preferredWidth);
    }

    // Draws one resource buffer as a wide card with amount and capacity.
    void DrawResourceCard(const ResourceBufferView& view, Rectangle bounds)
    {
        DrawRectangleRounded(bounds, 0.08f, 6, UiTheme::Inset);
        DrawRectangleRoundedLines(bounds, 0.08f, 6, 1.0f, UiTheme::Iron);

        float iconSize = std::min(bounds.width * 0.38f, bounds.height * 0.64f);
        Rectangle icon{
            bounds.x + 8.0f,
            bounds.y + (bounds.height - iconSize) * 0.5f,
            iconSize,
            iconSize};

        if (resourceIconAtlas.IsLoaded())
        {
            Rectangle src = resourceIconAtlas.GetRect(view.type);
            DrawTexturePro(resourceIconAtlas.texture.Get(), src, icon, {0.0f, 0.0f}, 0.0f, WHITE);
        }
        else
        {
            DrawRectangleRounded(icon, 0.16f, 6, ResourceColor(view.type));
            DrawTextFit(ResourceShortName(view.type), {icon.x + 5.0f, icon.y + icon.height * 0.32f, icon.width - 10.0f, 20.0f}, 18, WHITE);
        }

        std::string amount = std::to_string(view.amount) + "/" + std::to_string(view.capacity);
        DrawTextFit(amount, {bounds.x + iconSize + 18.0f, bounds.y + 8.0f, bounds.width - iconSize - 26.0f, 20.0f}, 16, UiTheme::Parchment);

        if (view.recipeAmount > 0)
        {
            std::string recipe = "x" + std::to_string(view.recipeAmount);
            int fontSize = 14;
            int textWidth = UiText::Measure(recipe, fontSize);
            DrawRectangleRounded({icon.x + icon.width - textWidth - 8.0f, icon.y + icon.height - 18.0f, static_cast<float>(textWidth + 8), 18.0f}, 0.25f, 4, UiTheme::Ink);
            UiText::Draw(recipe, icon.x + icon.width - textWidth - 4.0f, icon.y + icon.height - 16.0f, fontSize, UiTheme::Parchment);
        }
    }

    // Draws one compact resource icon with an amount badge.
    void DrawResourceIcon(const ResourceBufferView& view, Rectangle bounds)
    {
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
        if (!UiControlIcons::DrawPixelHudWidgetFrame(bounds, hovered))
        {
            DrawRectangleRounded(bounds, 0.10f, 8, UiTheme::Inset);
            DrawRectangleRoundedLines(bounds, 0.10f, 8, 1.0f, UiTheme::Iron);
        }

        float padding = std::max(6.0f, std::min(bounds.width, bounds.height) * 0.10f);
        float iconSize = std::max(1.0f, std::min(bounds.width, bounds.height) - padding * 2.0f);
        Rectangle icon{
            bounds.x + (bounds.width - iconSize) * 0.5f,
            bounds.y + (bounds.height - iconSize) * 0.5f,
            iconSize,
            iconSize};

        if (resourceIconAtlas.IsLoaded())
        {
            Rectangle src = resourceIconAtlas.GetRect(view.type);
            DrawTexturePro(resourceIconAtlas.texture.Get(), src, icon, {0.0f, 0.0f}, 0.0f, WHITE);
        }
        else
        {
            DrawRectangleRounded(icon, 0.16f, 6, ResourceColor(view.type));
            DrawTextFit(ResourceShortName(view.type), {icon.x + 5.0f, icon.y + icon.height * 0.34f, icon.width - 10.0f, 20.0f}, 18, WHITE);
        }

        std::string amount = view.capacity > 0
            ? std::to_string(view.amount) + "/" + std::to_string(view.capacity)
            : std::to_string(view.amount);
        int fontSize = std::clamp(static_cast<int>(bounds.height * 0.24f), 18, 24);
        int textWidth = MeasureUiText(amount, fontSize);
        constexpr float badgePadding = 6.0f;
        Rectangle badge{
            bounds.x + bounds.width - textWidth - badgePadding * 2.0f - 1.0f,
            bounds.y + bounds.height - fontSize - badgePadding * 2.0f - 1.0f,
            static_cast<float>(textWidth) + badgePadding * 2.0f,
            static_cast<float>(fontSize) + badgePadding * 2.0f};

        DrawRectangleRounded(badge, 0.25f, 6, UiTheme::Ink);
        DrawUiText(amount, badge.x + badgePadding, badge.y + badgePadding, fontSize, UiTheme::Parchment);

        if (view.recipeAmount > 0)
        {
            std::string recipe = "x" + std::to_string(view.recipeAmount);
            int recipeFont = std::clamp(static_cast<int>(bounds.height * 0.21f), 14, 18);
            int recipeWidth = MeasureUiText(recipe, recipeFont);
            Rectangle recipeBadge{
                bounds.x + 1.0f,
                bounds.y + 1.0f,
                static_cast<float>(recipeWidth) + badgePadding * 2.0f,
                static_cast<float>(recipeFont) + badgePadding * 2.0f};
            DrawRectangleRounded(recipeBadge, 0.25f, 6, UiTheme::Panel);
            DrawUiText(recipe, recipeBadge.x + badgePadding, recipeBadge.y + badgePadding, recipeFont, UiTheme::Parchment);
        }
    }

    // Draws resource cards in a fixed-column grid.
    void DrawResourceGrid(const std::vector<ResourceBufferView>& views, Rectangle bounds, int columns)
    {
        if (views.empty())
        {
            DrawTextFit("Empty", {bounds.x, bounds.y + 6.0f, bounds.width, 20.0f}, 16, UiTheme::ParchmentDim);
            return;
        }

        columns = std::max(1, columns);
        int rows = static_cast<int>((views.size() + columns - 1) / columns);
        float gap = 6.0f;
        float cardW = (bounds.width - gap * (columns - 1)) / columns;
        float cardH = std::max(34.0f, (bounds.height - gap * (rows - 1)) / std::max(1, rows));

        for (int i = 0; i < views.size(); i++)
        {
            int col = i % columns;
            int row = i / columns;
            Rectangle card{
                bounds.x + col * (cardW + gap),
                bounds.y + row * (cardH + gap),
                cardW,
                std::min(cardH, 58.0f)};
            DrawResourceCard(views[i], card);
        }
    }

    // Draws compact resource icons in a responsive grid.  Keep each cell at
    // a readable, fixed size and scroll overflow; shrinking every cell to fit
    // every row made a full HQ/warehouse stock look like a row of pixels.
    // `panelBuilding` is only used to enrich the hover tooltip: for a warehouse
    // it adds the player-wide total, so "this panel is local stock" reads as a
    // deliberate choice rather than a number that might or might not be global.
    void DrawResourceIconGrid(const std::vector<ResourceBufferView>& views, Rectangle bounds, int columns, float* scrollOffset = nullptr, float* maxScrollOffset = nullptr, const Building* panelBuilding = nullptr, bool* scrollbarDragging = nullptr, float* scrollbarDragOffset = nullptr, float fixedCellSize = 0.0f, float fixedGap = 0.0f, float leadingPadding = 0.0f)
    {
        if (maxScrollOffset != nullptr)
            *maxScrollOffset = 0.0f;

        if (views.empty())
        {
            DrawTextFit("Empty storage", {bounds.x, bounds.y + 6.0f, bounds.width, 22.0f}, 17, UiTheme::ParchmentDim);
            return;
        }

        // Storage buffers use a std::map for deterministic simulation order;
        // that numeric enum order is not a helpful order for players.  Sort
        // only this display copy by the shared economic progression instead.
        std::vector<ResourceBufferView> orderedViews = views;
        std::stable_sort(orderedViews.begin(), orderedViews.end(),
                         [](const ResourceBufferView& left, const ResourceBufferView& right)
                         {
                             return ResourcePresentationRank(left.type) <
                                    ResourcePresentationRank(right.type);
                         });

        constexpr float defaultGap = 8.0f;
        const bool canScroll = scrollOffset != nullptr;
        const bool hasFixedCellSize = fixedCellSize > 0.0f;
        const float gap = hasFixedCellSize ? std::max(0.0f, fixedGap) : defaultGap;
        const float minimumCellWidth = canScroll ? 96.0f : 80.0f;
        const float scrollbarReserve = canScroll ? 18.0f : 0.0f;

        // Three columns are the narrow-panel baseline.  Wider panels gain a
        // fourth/fifth column, up to the caller's requested maximum, without
        // ever compromising texture legibility.
        if (hasFixedCellSize)
        {
            columns = std::max(1, columns);
        }
        else if (canScroll)
        {
            int responsiveColumns = static_cast<int>((bounds.width - scrollbarReserve + gap) /
                                                      (minimumCellWidth + gap));
            int minimumColumns = bounds.width >= 280.0f ? 3 : 1;
            columns = std::clamp(responsiveColumns, minimumColumns, std::max(minimumColumns, columns));
        }
        else
        {
            // Production panels reserve the caller's full slot count so the
            // input side can fit three ingredients and the output side can
            // fit two products at the same size. A one-product recipe must
            // not grow to fill the whole output column.
            columns = std::max(1, columns);
        }
        float contentWidth = std::max(1.0f, bounds.width - scrollbarReserve);
        int rows = static_cast<int>((orderedViews.size() + columns - 1) / columns);
        float cellW = 0.0f;
        float cellH = 0.0f;
        if (hasFixedCellSize)
        {
            cellW = fixedCellSize;
            cellH = fixedCellSize;
        }
        else if (canScroll)
        {
            cellW = (contentWidth - gap * (columns - 1)) / columns;
            cellH = std::clamp(cellW * 0.88f, 82.0f, 118.0f);
        }
        else
        {
            cellW = (contentWidth - gap * (columns - 1)) / columns;
            // The production section is clipped to its measured height. Fit
            // every row into that height instead of drawing an 82 px cell into
            // a 50–70 px viewport and cutting off the icon at the bottom.
            float availableCellHeight = (bounds.height - gap * std::max(0, rows - 1)) /
                                        std::max(1, rows);
            cellH = std::min(cellW * 0.88f, std::max(1.0f, availableCellHeight));
        }
        // Resource cards are icon slots, not stretchable banners. Keep the
        // actual card square even when a production column is wider than its
        // short vertical viewport; center it inside that column.
        if (!hasFixedCellSize)
            cellH = std::min(cellW, cellH);
        float contentHeight = rows * cellH + std::max(0, rows - 1) * gap;
        float resolvedMaxScrollOffset = canScroll
            ? std::max(0.0f, contentHeight - bounds.height)
            : 0.0f;
        if (maxScrollOffset != nullptr)
            *maxScrollOffset = resolvedMaxScrollOffset;
        float resolvedScrollOffset = scrollOffset != nullptr ? *scrollOffset : 0.0f;
        int hoveredIndex = -1;

        BeginScissorMode(static_cast<int>(bounds.x), static_cast<int>(bounds.y), static_cast<int>(bounds.width), static_cast<int>(bounds.height));
        for (int i = 0; i < orderedViews.size(); i++)
        {
            int col = i % columns;
            int row = i / columns;
            float x = hasFixedCellSize
                ? bounds.x + leadingPadding + col * (cellW + gap)
                : bounds.x + col * (cellW + gap) + (cellW - cellH) * 0.5f;
            float y = bounds.y + row * (cellH + gap) - resolvedScrollOffset;

            if (y > bounds.y + bounds.height)
                break;
            if (y + cellH < bounds.y)
                continue;

            Rectangle cell{x, y, cellH, cellH};
            DrawResourceIcon(orderedViews[i], cell);
            if (CheckCollisionPointRec(GetMousePosition(), cell))
                hoveredIndex = i;
        }
        EndScissorMode();

        // The wheel scroll is handled by GuiPanel::ScrollContent.  Reserve a
        // proper, high-contrast track for it so overflow is discoverable and
        // no resource card is hidden underneath the scrollbar.
        if (resolvedMaxScrollOffset > 0.0f)
        {
            Rectangle track{bounds.x + contentWidth + 5.0f, bounds.y, 9.0f, bounds.height};
            DrawRectangleRounded(track, 0.5f, 6, UiTheme::Inset);
            float thumbH = std::max(30.0f, track.height * (track.height / contentHeight));
            float normalizedOffset = std::clamp(resolvedScrollOffset / resolvedMaxScrollOffset, 0.0f, 1.0f);
            float thumbY = track.y + (track.height - thumbH) * normalizedOffset;
            Rectangle thumb{track.x, thumbY, track.width, thumbH};
            if (scrollbarDragging != nullptr && scrollbarDragOffset != nullptr && scrollOffset != nullptr)
            {
                Vector2 mouse = GetMousePosition();
                if (InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                    CheckCollisionPointRec(mouse, thumb))
                {
                    *scrollbarDragging = true;
                    *scrollbarDragOffset = mouse.y - thumb.y;
                }
                if (*scrollbarDragging && InputManager::IsMouseButtonDown(MOUSE_BUTTON_LEFT))
                {
                    float thumbRange = track.height - thumbH;
                    float targetThumbY = std::clamp(mouse.y - *scrollbarDragOffset,
                                                   track.y, track.y + thumbRange);
                    float normalized = thumbRange > 0.0f
                        ? (targetThumbY - track.y) / thumbRange
                        : 0.0f;
                    *scrollOffset = normalized * resolvedMaxScrollOffset;
                    resolvedScrollOffset = *scrollOffset;
                    thumbY = targetThumbY;
                }
                if (*scrollbarDragging && InputManager::IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
                    *scrollbarDragging = false;
            }
            DrawRectangleRounded(Rectangle{track.x, thumbY, track.width, thumbH}, 0.5f, 6,
                                 UiTheme::Iron);
        }

        if (hoveredIndex >= 0)
        {
            const auto& view = orderedViews[hoveredIndex];
            std::vector<std::string> lines{
                "In this building: " + std::to_string(view.amount) +
                (view.capacity > 0 ? " / " + std::to_string(view.capacity) : "")};
            // A warehouse panel is deliberately local-only (user request,
            // 2026-07-25), so name that explicitly and point at where the
            // player-wide number lives instead of leaving it ambiguous.
            if (StockpileIndex::IsWarehouse(panelBuilding) && panelBuilding->owner != nullptr)
                lines.push_back("Player-wide: " +
                    std::to_string(StockpileIndex::GetTotal(*panelBuilding->owner, view.type)) + "  [E]");
            QueueResourceTooltip(view.type, std::move(lines));
        }
    }

    // Returns remaining terrain richness under a building footprint.
    int GetLocalTerrainRichness(const Building* building)
    {
        const auto* production = building != nullptr ? building->GetComponent<ProductionComponent>() : nullptr;
        if (building == nullptr || production == nullptr || building->owner == nullptr ||
            production->terrainType == TileType::GRASS || !production->ingredients.empty())
            return -1;

            const TileMap& tilemap = *building->owner->tilemap;
        Vec2i anchor = tilemap.GetCoordsFromId(building->positionId);
        Vec2i footprint = building->GetFootprint();
        int richness = 0;
        for (int y = 0; y < footprint.y; y++)
        {
            for (int x = 0; x < footprint.x; x++)
            {
                Vec2i pos{anchor.x + x, anchor.y + y};
                if (!tilemap.IsInside(pos))
                    continue;

                const Tile& tile = tilemap.tilemap[tilemap.GetIdFromCoords(pos)];
                if (tile.tileType == production->terrainType)
                    richness += tile.resourceRichness;
            }
        }
        return richness;
    }

    // Returns a readable label for one modifier stat.
    const char* BalanceStatLabel(BalanceStat stat)
    {
        switch (stat)
        {
            case BalanceStat::BuildTime: return "Build time";
            case BalanceStat::BuildCost: return "Build cost";
            case BalanceStat::ProductionCycleTime: return "Cycle time";
            case BalanceStat::ProductionOutputAmount: return "Output";
            case BalanceStat::WorkerCapacity: return "Workers";
            case BalanceStat::TransportTime: return "Transport time";
            case BalanceStat::TransportDispatchDelay: return "Cargo dispatch delay";
            case BalanceStat::RoadCapacity: return "Road capacity";
            case BalanceStat::RoadSpeed: return "Road speed";
            case BalanceStat::ManpowerRate: return "Manpower growth";
            case BalanceStat::PopulationCap: return "Population cap";
            case BalanceStat::VillageSupplyConsumption: return "Village supply consumption";
            case BalanceStat::BuilderAmount: return "Builders";
            default: return "Effect";
        }
    }

    bool LowerValueIsBetter(BalanceStat stat)
    {
        switch (stat)
        {
            case BalanceStat::BuildTime:
            case BalanceStat::BuildCost:
            case BalanceStat::ProductionCycleTime:
            case BalanceStat::WorkerCapacity:
            case BalanceStat::TransportTime:
            case BalanceStat::TransportDispatchDelay:
            case BalanceStat::VillageSupplyConsumption:
                return true;
            default:
                return false;
        }
    }

    bool IsPositiveModifier(const BalanceModifier& modifier)
    {
        bool lowerIsBetter = LowerValueIsBetter(modifier.stat);
        if (std::abs(modifier.additive) > 0.0001)
            return lowerIsBetter ? modifier.additive < 0.0 : modifier.additive > 0.0;
        if (std::abs(modifier.multiplier - 1.0) > 0.0001)
            return lowerIsBetter ? modifier.multiplier < 1.0 : modifier.multiplier > 1.0;
        return true;
    }

    const char* ImprovedRateLabel(BalanceStat stat)
    {
        switch (stat)
        {
            case BalanceStat::BuildTime: return "Build speed";
            case BalanceStat::ProductionCycleTime: return "Production speed";
            case BalanceStat::TransportTime: return "Transport speed";
            case BalanceStat::TransportDispatchDelay: return "Cargo dispatch speed";
            default: return BalanceStatLabel(stat);
        }
    }

    // Returns a readable label for one building type.
    const char* BuildingTypeLabel(BuildingType type)
    {
        switch (type)
        {
            case BuildingType::Headquarters: return "HQ";
            case BuildingType::Village: return "Village";
            case BuildingType::StorageBuilding: return "Storage";
            case BuildingType::Woodcutter: return "Woodcutter";
            case BuildingType::HuntersHut: return "Hunter";
            case BuildingType::LumberMill: return "Lumber mill";
            case BuildingType::Mine: return "Mine";
            case BuildingType::Foundry: return "Foundry";
            case BuildingType::Well: return "Well";
            case BuildingType::WheatFarm: return "Wheat farm";
            case BuildingType::Windmill: return "Windmill";
            case BuildingType::Bakery: return "Bakery";
            case BuildingType::Inn: return "Inn";
            case BuildingType::Paperworks: return "Paperworks";
            case BuildingType::Smith: return "Smith";
            case BuildingType::University: return "University";
            case BuildingType::Barracks: return "Barracks";
            case BuildingType::Road: return "Road";
            case BuildingType::Bridge: return "Bridge";
            case BuildingType::Mint: return "Mint";
            case BuildingType::Glassworks: return "Glassworks";
            case BuildingType::Powderworks: return "Powderworks";
            case BuildingType::DefenseTower: return "Defense tower";
            case BuildingType::AnimalFarm: return "Animal farm";
            case BuildingType::Butcher: return "Butcher";
            case BuildingType::Tannery: return "Tannery";
            case BuildingType::Tailor: return "Tailor";
            case BuildingType::Armorer: return "Armorer";
            case BuildingType::HorseStable: return "Horse stable";
            case BuildingType::Kiln: return "Kiln";
            case BuildingType::HouseholdWorkshop: return "Household workshop";
            case BuildingType::Soapworks: return "Soapworks";
            case BuildingType::Inkworks: return "Inkworks";
            case BuildingType::Scriptorium: return "Scriptorium";
            case BuildingType::Copperworks: return "Copperworks";
            case BuildingType::UrbanWorkshop: return "Urban workshop";
            case BuildingType::HempFarm: return "Hemp farm";
            case BuildingType::Ropery: return "Ropery";
            case BuildingType::Weaver: return "Weaver";
            case BuildingType::Bowyer: return "Bowyer";
            case BuildingType::SpearWorkshop: return "Spear workshop";
            case BuildingType::SiegeWorkshop: return "Siege workshop";
            default: return "Building";
        }
    }

    // Formats one technology modifier for tooltip display.
    std::string FormatTechnologyEffect(const BalanceModifier& modifier)
    {
        std::string text;
        if (modifier.buildingType.has_value())
            text += "{building}" + std::string(BuildingTypeLabel(modifier.buildingType.value())) + "{/building}: ";
        if (modifier.resourceType.has_value() && modifier.resourceType.value() != ResourceType::Null)
            text += "{resource}" + ResourceDisplayName(modifier.resourceType.value()) + "{/resource} ";

        bool lowerIsBetter = LowerValueIsBetter(modifier.stat);
        bool showAsRate = lowerIsBetter && std::abs(modifier.multiplier - 1.0) > 0.0001 &&
                          (modifier.stat == BalanceStat::BuildTime ||
                           modifier.stat == BalanceStat::ProductionCycleTime ||
                           modifier.stat == BalanceStat::TransportTime ||
                           modifier.stat == BalanceStat::TransportDispatchDelay);
        text += showAsRate ? ImprovedRateLabel(modifier.stat) : BalanceStatLabel(modifier.stat);
        if (std::abs(modifier.additive) > 0.0001)
        {
            text += modifier.additive > 0.0 ? " +" : " ";
            text += std::to_string(static_cast<int>(modifier.additive));
        }
        if (std::abs(modifier.multiplier - 1.0) > 0.0001)
        {
            double displayedPercent = showAsRate
                ? (1.0 / modifier.multiplier - 1.0) * 100.0
                : (modifier.multiplier - 1.0) * 100.0;
            int deltaPercent = static_cast<int>(std::round(displayedPercent));
            text += deltaPercent > 0 ? " +" : " ";
            text += std::to_string(deltaPercent) + "%";
        }
        if (modifier.resourceCategory.has_value())
            text += std::string(" ({category}") + ResourceCategoryLabel(modifier.resourceCategory.value()) + "{/category})";
        return IsPositiveModifier(modifier) ? TooltipBonusLine(text) : TooltipPenaltyLine(text);
    }

    std::string FormatTechnologyCosts(const std::vector<ResourceAmountDefinition>& costs)
    {
        std::string text = "Research cost: ";
        for (size_t index = 0; index < costs.size(); index++)
        {
            if (index > 0)
                text += "  |  ";
            text += "{resource}" + ResourceDisplayName(costs[index].type) + "{/resource} x" + std::to_string(costs[index].amount);
        }
        return text;
    }

    // Returns display depth in the research tree based on prerequisite chains.
    int TechnologyDepth(const ResearchNodeView& technology, const std::map<std::string, const ResearchNodeView*>& nodesById, std::map<std::string, int>& cache)
    {
        auto cached = cache.find(technology.id);
        if (cached != cache.end())
            return cached->second;

        int depth = technology.layoutOrder >= 1000 ? technology.layoutOrder / 1000 - 1 : 0;
        for (const auto& prerequisite : technology.prerequisites)
        {
            auto parent = nodesById.find(prerequisite);
            if (parent != nodesById.end())
                depth = std::max(depth, TechnologyDepth(*parent->second, nodesById, cache) + 1);
        }

        cache[technology.id] = depth;
        return depth;
    }

    // Draws a compact row of resource icons representing technology cost.
    void DrawTechnologyCost(const std::vector<ResourceAmountDefinition>& costs, Rectangle bounds)
    {
        if (costs.empty())
        {
            DrawTextFit("Free", bounds, 12, UiTheme::ParchmentDim);
            return;
        }

        float iconSize = std::min(18.0f, bounds.height);
        float x = bounds.x;
        for (const auto& cost : costs)
        {
            if (x + iconSize + 22.0f > bounds.x + bounds.width)
                break;

            Rectangle icon{x, bounds.y + (bounds.height - iconSize) * 0.5f, iconSize, iconSize};
            GuiPanel::DrawResourceIcon(cost.type, icon);
            std::string amount = std::to_string(cost.amount);
            UiText::Draw(amount, x + iconSize + 3.0f, bounds.y + 2.0f, 11, Color{224, 208, 178, 255});
            x += iconSize + 25.0f;
        }
    }

    // Draws a tooltip describing one technology after the tree itself is drawn.
    void DrawTechnologyTooltip(const ResearchNodeView& technology)
    {
        std::vector<std::string> lines;
        lines.push_back(technology.description);
        lines.push_back(TooltipSeparatorLine());
        lines.push_back(std::string("Time: ") + std::to_string(static_cast<int>(technology.researchTime)) + "s | " + technology.stateText);
        if (!technology.costs.empty())
            lines.push_back(FormatTechnologyCosts(technology.costs));
        for (const auto& building : technology.unlockedBuildings)
            lines.push_back("Unlocks {building}" + building + "{/building}");
        if (technology.active)
            lines.push_back("Remaining: " + std::to_string(static_cast<int>(std::round(technology.remainingTime))) + "s");
        lines.push_back(TooltipSeparatorLine());
        for (const auto& modifier : technology.modifiers)
            lines.push_back(FormatTechnologyEffect(modifier));

        Tooltip::Draw(technology.name, lines, 330.0f, {}, 30);
    }

    constexpr std::array<const char*, 12> UnitPresentationOrder{{
        "militia", "swordsman", "armored_swordsman", "heavy_infantry",
        "archer", "heavy_archer", "spearman", "light_cavalry",
        "knight", "ballista", "ram", "catapult"
    }};

    int UnitPresentationRank(const std::string& id)
    {
        auto it = std::find_if(UnitPresentationOrder.begin(), UnitPresentationOrder.end(),
            [&id](const char* candidate) { return id == candidate; });
        return it == UnitPresentationOrder.end()
            ? static_cast<int>(UnitPresentationOrder.size())
            : static_cast<int>(std::distance(UnitPresentationOrder.begin(), it));
    }

    int AvailableRecruitmentResource(const Building& building, ResourceType type)
    {
        if (building.owner == nullptr)
            return 0;

        int available = StockpileIndex::GetTotal(*building.owner, type);
        if (const auto* localStorage = building.GetComponent<LocalResourceBufferComponent>())
        {
            auto it = localStorage->buffers.find(type);
            if (it != localStorage->buffers.end())
                available += static_cast<int>(it->second.buffer.size());
        }
        return available;
    }

    std::string FormatUnitStat(double value, int precision = 2)
    {
        if (std::abs(value - std::round(value)) < 0.0001)
            return std::to_string(static_cast<int>(std::round(value)));
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(precision) << value;
        return stream.str();
    }

    struct UnitTooltipStat
    {
        std::string label;
        std::string value;
        ResourceType resourceIcon{ResourceType::Null};
        UiControlIcons::MilitaryStatIcon customIcon{UiControlIcons::MilitaryStatIcon::HitPoints};
    };

    void DrawUnitRecruitmentTooltip(const Building& building, const std::string& unitId,
                                    const UnitDefinition& definition,
                                    double manpowerCost, double recruitTime,
                                    const std::string& blockReason)
    {
        Player* player = building.owner;
        auto modified = [player, &unitId](BalanceStat stat, double base)
        {
            return player != nullptr ? player->ModifyBalanceForUnit(stat, base, unitId) : base;
        };

        std::vector<UnitTooltipStat> stats{
            {"HP", FormatUnitStat(modified(BalanceStat::UnitHp, definition.maxHp))},
            {"Soft attack", FormatUnitStat(modified(BalanceStat::UnitRoadAttack, definition.roadAttack)), ResourceType::IRON_SWORD},
            {"Hard attack", FormatUnitStat(modified(BalanceStat::UnitSiegeAttack, definition.siegeAttack)), ResourceType::BATTERING_RAM},
            {"Armor", FormatUnitStat(modified(BalanceStat::UnitArmor, definition.armor)), ResourceType::IRON_SHIELD},
            {"Move speed", FormatUnitStat(modified(BalanceStat::UnitMoveSpeed, definition.moveSpeed)), ResourceType::Null, UiControlIcons::MilitaryStatIcon::MoveSpeed},
            // Keep weapon-shaped stats on the same authored product icon as
            // the rest of the economy UI. The pilot's bespoke crossed-swords
            // glyph was visually noisy and did not match the resource atlas.
            {"Attack speed", FormatUnitStat(modified(BalanceStat::UnitAttackSpeed, definition.attackSpeed)), ResourceType::IRON_SWORD}
        };
        if (definition.attackRange > 0.0)
            stats.push_back({"Range", FormatUnitStat(definition.attackRange), ResourceType::BOW});
        if (definition.antiCavalryMultiplier > 1.0)
            stats.push_back({"Anti-cavalry", "x" + FormatUnitStat(definition.antiCavalryMultiplier, 1), ResourceType::SPEAR});
        if (definition.areaTargets > 1)
            stats.push_back({"Area targets", std::to_string(definition.areaTargets), ResourceType::Null, UiControlIcons::MilitaryStatIcon::AreaTargets});
        if (definition.canTargetFlying)
            // The existing product bow is clearer and stylistically exact;
            // the generated upward-bow glyph is intentionally not used.
            stats.push_back({"Air targeting", "Yes", ResourceType::BOW});
        if (definition.cavalry)
            stats.push_back({"Cavalry", "Yes", ResourceType::HORSE});

        const int statRows = static_cast<int>((stats.size() + 1) / 2);
        std::string status;
        if (!definition.requiredTechnology.empty() && player != nullptr &&
            !player->technologies.HasTechnology(definition.requiredTechnology))
        {
            const TechnologyDefinition* technology = FindTechnologyDefinition(definition.requiredTechnology);
            status = "Requires technology: " +
                (technology != nullptr ? technology->name : definition.requiredTechnology);
        }
        else if (!blockReason.empty())
            status = "Missing resources or manpower";

        constexpr float width = 520.0f;
        constexpr float padding = 14.0f;
        constexpr float portraitSize = 96.0f;
        constexpr float headerHeight = 112.0f;
        constexpr float statRowHeight = 34.0f;
        const float height = padding + headerHeight + 8.0f +
                             statRows * statRowHeight +
                             (status.empty() ? 0.0f : 28.0f) + padding;
        Vector2 mouse = GetMousePosition();
        Rectangle bounds{mouse.x + 18.0f, mouse.y + 18.0f, width, height};
        bounds.x = std::clamp(bounds.x, 10.0f, std::max(10.0f, GetScreenWidth() - bounds.width - 10.0f));
        if (bounds.y + bounds.height > GetScreenHeight() - 10.0f)
            bounds.y = std::max(10.0f, mouse.y - bounds.height - 18.0f);

        if (!UiControlIcons::DrawPixelHudPanelFrame(bounds))
        {
            DrawRectangleRec(bounds, UiTheme::Panel);
            DrawRectangleLinesEx(bounds, 1.0f, UiTheme::Iron);
        }

        Rectangle portrait{bounds.x + padding, bounds.y + padding,
                           portraitSize, portraitSize};
        UiControlIcons::DrawUnitPortrait(unitId, portrait);
        const float detailsX = portrait.x + portrait.width + 12.0f;
        const float detailsRight = bounds.x + bounds.width - padding;
        Rectangle titleArea{detailsX, bounds.y + padding,
                            detailsRight - detailsX, 34.0f};
        const UiFontRole previousRole = UiText::SetRole(UiFontRole::Display);
        int titleFont = 27;
        while (titleFont > 18 && UiText::Measure(definition.displayName, titleFont) > titleArea.width)
            --titleFont;
        UiText::Draw(definition.displayName,
                     titleArea.x,
                     titleArea.y + (titleArea.height - titleFont) * 0.5f,
                     titleFont, UiTheme::Parchment);
        UiText::SetRole(previousRole);

        // The title rule belongs only to the text column: the portrait remains
        // a single uninterrupted visual anchor in the upper-left corner.
        float separatorY = titleArea.y + titleArea.height + 2.0f;
        DrawLineEx({detailsX, separatorY}, {detailsRight, separatorY},
                   1.0f, UiTheme::Iron);

        const float costY = separatorY + 7.0f;
        UiText::Draw("Cost", detailsX, costY + 5.0f, 16, UiTheme::ParchmentDim);
        float costX = detailsX + 39.0f;
        const float costItemWidth = std::min(
            62.0f,
            (detailsRight - costX) /
                std::max(1.0f, static_cast<float>(definition.cost.size() + 2)));
        auto drawCost = [&](const std::function<void(Rectangle, Color)>& drawIcon,
                            const std::string& amount, bool affordable)
        {
            constexpr float iconSize = 31.0f;
            Rectangle icon{costX, costY - 2.0f, iconSize, iconSize};
            Color tint = affordable ? WHITE : Color{224, 112, 98, 255};
            drawIcon(icon, tint);
            UiText::Draw(amount, costX + iconSize + 2.0f, costY + 5.0f, 15,
                         affordable ? UiTheme::Parchment : Color{238, 104, 92, 255});
            costX += costItemWidth;
        };

        const double manpowerAvailable = player != nullptr
            ? player->strategicResources.Get(StrategicResourceType::Manpower) : 0.0;
        drawCost([](Rectangle icon, Color tint)
            { UiControlIcons::DrawPixelHudGlyph(UiControlIcons::HudIcon::Manpower, icon, tint); },
            "x" + std::to_string(static_cast<int>(std::ceil(manpowerCost))),
            manpowerAvailable >= manpowerCost);
        for (const auto& cost : definition.cost)
        {
            const ResourceType type = cost.type;
            drawCost([type](Rectangle icon, Color tint)
                {
                    GuiPanel::DrawResourceIcon(type, icon);
                    if (tint.r < 250)
                        DrawRectangleRec(icon, Fade(tint, 0.24f));
                },
                "x" + std::to_string(cost.amount),
                AvailableRecruitmentResource(building, type) >= cost.amount);
        }
        drawCost([](Rectangle icon, Color tint)
            { UiControlIcons::DrawMilitaryStat(UiControlIcons::MilitaryStatIcon::Time, icon, tint); },
            FormatUnitStat(recruitTime, 1) + "s", true);

        // One full-width divider closes the shared portrait/title/cost header.
        separatorY = bounds.y + padding + headerHeight;
        DrawLineEx({bounds.x + padding, separatorY},
                   {bounds.x + bounds.width - padding, separatorY}, 1.0f, UiTheme::Iron);
        const float statsY = separatorY + 8.0f;
        const float columnWidth = (bounds.width - padding * 2.0f) * 0.5f;
        for (size_t index = 0; index < stats.size(); ++index)
        {
            const int column = static_cast<int>(index % 2);
            const int row = static_cast<int>(index / 2);
            Rectangle icon{bounds.x + padding + column * columnWidth,
                           statsY + row * statRowHeight, 27.0f, 27.0f};
            if (stats[index].resourceIcon != ResourceType::Null)
                GuiPanel::DrawResourceIcon(stats[index].resourceIcon, icon);
            else
                UiControlIcons::DrawMilitaryStat(stats[index].customIcon, icon);
            UiText::DrawFit(stats[index].label + "  " + stats[index].value,
                            {icon.x + 30.0f, icon.y + 2.0f,
                             columnWidth - 34.0f, 24.0f},
                            15, UiTheme::Parchment);
        }

        if (!status.empty())
        {
            const float statusY = bounds.y + bounds.height - 27.0f;
            DrawLineEx({bounds.x + padding, statusY - 4.0f},
                       {bounds.x + bounds.width - padding, statusY - 4.0f}, 1.0f, UiTheme::Iron);
            UiText::DrawFit(status, {bounds.x + padding, statusY,
                                     bounds.width - padding * 2.0f, 20.0f},
                            15, Color{232, 116, 94, 255});
        }
    }

    // Draws one categorized technology tree and returns the hovered technology, if any.
    const ResearchNodeView* DrawResearchCategory(
        Player* player,
        const std::vector<const ResearchNodeView*>& technologies,
        const std::map<std::string, const ResearchNodeView*>& nodesById,
        Rectangle bounds)
    {
        if (player == nullptr || technologies.empty())
        {
            DrawTextFit("No research", bounds, 14, UiTheme::ParchmentDim);
            return nullptr;
        }

        std::map<std::string, int> depthCache;
        std::map<int, int> rowByDepth;
        std::map<std::string, Rectangle> nodeBounds;
        int maxDepth = 0;
        for (const auto* technology : technologies)
            maxDepth = std::max(maxDepth, TechnologyDepth(*technology, nodesById, depthCache));

        float gapX = 34.0f;
        float gapY = 54.0f;
        float nodeW = 122.0f;
        float nodeH = 108.0f;
        int maxRowsAtDepth = 1;
        for (const auto* technology : technologies)
            maxRowsAtDepth = std::max(maxRowsAtDepth, ++rowByDepth[depthCache[technology->id]]);
        rowByDepth.clear();

        for (const auto* technology : technologies)
        {
            int depth = depthCache[technology->id];
            int row = rowByDepth[depth]++;
            Rectangle node{
                bounds.x + row * (nodeW + gapX),
                bounds.y + depth * (nodeH + gapY),
                nodeW,
                nodeH};
            nodeBounds[technology->id] = node;
        }

        for (const auto* technology : technologies)
        {
            auto childIt = nodeBounds.find(technology->id);
            if (childIt == nodeBounds.end())
                continue;

            Rectangle child = childIt->second;
            for (const auto& prerequisite : technology->prerequisites)
            {
                auto parentIt = nodeBounds.find(prerequisite);
                if (parentIt == nodeBounds.end())
                    continue;

                Rectangle parent = parentIt->second;
                Vector2 a{parent.x + parent.width * 0.5f, parent.y + parent.height};
                Vector2 b{child.x + child.width * 0.5f, child.y};
                float midY = (a.y + b.y) * 0.5f;
                DrawLineEx(a, Vector2{a.x, midY}, 2.0f, UiTheme::Iron);
                DrawLineEx(Vector2{a.x, midY}, Vector2{b.x, midY}, 2.0f, UiTheme::Iron);
                DrawLineEx(Vector2{b.x, midY}, b, 2.0f, UiTheme::Iron);
            }
        }

        Vector2 mouse = GetMousePosition();
        const ResearchNodeView* hoveredTechnology = nullptr;
        for (const auto* technology : technologies)
        {
            Rectangle node = nodeBounds[technology->id];
            bool hovered = CheckCollisionPointRec(mouse, node);

            Color fill = technology->researched ? UiTheme::SelectedFill
                       : technology->available ? UiTheme::Surface
                       : technology->prerequisitesMet ? UiTheme::SurfaceHover
                       : UiTheme::Inset;
            Color line = hovered ? Color{214, 178, 96, 255}
                       : technology->researched ? Color{140, 176, 96, 255}
                       : technology->available ? Color{176, 132, 68, 255}
                       : UiTheme::Iron;

            DrawRectangleRounded(node, 0.08f, 8, UiTheme::Ink);
            Rectangle inner{node.x + 2.0f, node.y + 2.0f, node.width - 4.0f, node.height - 4.0f};
            DrawRectangleRounded(inner, 0.07f, 8, fill);
            DrawRectangleRoundedLines(node, 0.08f, 8, 1.2f, line);
            DrawRectangleRounded(Rectangle{node.x + 5.0f, node.y + 5.0f, 3.0f, node.height - 10.0f}, 0.5f, 4, line);
            DrawTextFit(technology->name, Rectangle{node.x + 8.0f, node.y + 8.0f, node.width - 16.0f, 26.0f}, 17, UiTheme::Parchment);

            Color stateColor = technology->researched ? Color{162, 214, 122, 255}
                             : technology->available ? UiTheme::AmberBright
                             : technology->prerequisitesMet ? Color{238, 184, 84, 255}
                             : Color{160, 142, 112, 255};
            DrawTextFit(technology->stateText, Rectangle{node.x + 8.0f, node.y + 38.0f, node.width - 16.0f, 19.0f}, 15, stateColor);
            std::string timeText = std::to_string(static_cast<int>(technology->researchTime)) + "s";
            DrawTextFit(timeText, Rectangle{node.x + 8.0f, node.y + 62.0f, node.width - 16.0f, 18.0f}, 14, Color{190, 172, 140, 255});
            DrawTechnologyCost(technology->costs, Rectangle{node.x + 8.0f, node.y + 84.0f, node.width - 16.0f, 18.0f});

            if (hovered)
                hoveredTechnology = technology;

            if (hovered && technology->available && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                player->UnlockTechnology(technology->id);
        }

        return hoveredTechnology;
    }

    // Draws the selected university worker assignment above the research tree.
    void DrawResearchWorkerPanel(const Building* university, Rectangle bounds)
    {
        const auto* workers = university != nullptr ? university->GetComponent<WorkerComponent>() : nullptr;
        if (university == nullptr || workers == nullptr)
            return;

        if (!UiControlIcons::DrawPixelHudPanelFrame(bounds))
        {
            DrawRectangleRounded(bounds, 0.045f, 8, UiTheme::Inset);
            DrawRectangleRoundedLines(bounds, 0.045f, 8, 1.0f, UiTheme::Iron);
        }

        std::string label = "Workers: " + std::to_string(workers->assigned) + "/" + std::to_string(workers->GetModifiedCapacity(*university));
        int workerPct = static_cast<int>(std::round(workers->GetRatio() * 100.0f));
        std::string output = "Worker output: " + std::to_string(workerPct) + "%";
        float y = bounds.y + (bounds.height - 21.0f) * 0.5f;
        UiText::Draw(label, bounds.x + 14.0f, y, 20, UiTheme::Parchment);
        UiText::Draw(output, bounds.x + 220.0f, y, 20, UiTheme::AmberBright);
    }

    // Formats seconds with two decimal places for compact stat labels.
    std::string FormatSeconds(double seconds)
    {
        if (!std::isfinite(seconds))
            return "Paused";

        std::ostringstream stream;
        stream << std::fixed << std::setprecision(2) << seconds << "s";
        return stream.str();
    }

    // Formats a decimal value with one fractional digit (e.g. tower attack
    // speed, "1.2") — plain std::to_string on a double affected by
    // BalanceModifiers would otherwise show a misleadingly truncated integer.
    std::string FormatDecimal(double value, int precision = 1)
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(precision) << value;
        return stream.str();
    }

    std::string FormatUpgradeEffect(const BalanceModifier& modifier)
    {
        std::string value;
        if (std::abs(modifier.additive) > 0.0001)
        {
            value = (modifier.additive > 0.0 ? "+" : "") + FormatDecimal(modifier.additive);
        }
        else if (std::abs(modifier.multiplier - 1.0) > 0.0001)
        {
            const double percent = (modifier.multiplier - 1.0) * 100.0;
            value = (percent > 0.0 ? "+" : "") + FormatDecimal(percent, 0) + "%";
        }
        else
        {
            value = "no change";
        }
        return std::string(BalanceStatLabel(modifier.stat)) + ": " + value;
    }

    void DrawUpgradeTooltip(Rectangle anchor, int targetLevel,
                            const BuildingUpgradeLevelDefinition& levelDefinition,
                            const std::vector<std::string>& effects,
                            const Player* owner)
    {
        const int rowCount = static_cast<int>(levelDefinition.cost.size()) +
                             static_cast<int>(effects.size()) +
                             (levelDefinition.buildTime > 0.0 ? 1 : 0) + 1;
        constexpr float rowH = 22.0f;
        const float boxW = std::clamp(anchor.width, 300.0f, 380.0f);
        const float boxH = std::max(1, rowCount) * rowH + 12.0f;
        Rectangle box{anchor.x, anchor.y - boxH - 6.0f, boxW, boxH};
        box.x = std::clamp(box.x, 8.0f, static_cast<float>(GetScreenWidth()) - box.width - 8.0f);
        box.y = std::clamp(box.y, 8.0f, static_cast<float>(GetScreenHeight()) - box.height - 8.0f);
        if (!UiControlIcons::DrawPixelHudPanelFrame(box))
        {
            DrawRectangleRounded(box, 0.08f, 8, UiTheme::Inset);
            DrawRectangleRoundedLines(box, 0.08f, 8, 1.0f, UiTheme::Iron);
        }

        float rowY = box.y + 6.0f;
        for (const auto& cost : levelDefinition.cost)
        {
            Rectangle icon{box.x + 8.0f, rowY, 18.0f, 18.0f};
            GuiPanel::DrawResourceIcon(cost.type, icon);
            const bool affordable = owner == nullptr || owner->HasBuildResources({cost});
            UiText::Draw(ResourceDisplayName(cost.type) + " x" + std::to_string(cost.amount),
                box.x + 34.0f, rowY + 1.0f, 14,
                affordable ? UiTheme::Parchment : Color{238, 184, 84, 255});
            rowY += rowH;
        }
        for (const auto& effect : effects)
        {
            UiText::Draw(effect, box.x + 8.0f, rowY + 1.0f, 14, UiTheme::SageBright);
            rowY += rowH;
        }
        if (levelDefinition.buildTime > 0.0)
        {
            UiText::Draw("Upgrade time: " + FormatSeconds(levelDefinition.buildTime),
                box.x + 8.0f, rowY + 1.0f, 14, UiTheme::ParchmentDim);
            rowY += rowH;
        }
        UiText::Draw("Target level: " + std::to_string(targetLevel),
            box.x + 8.0f, rowY + 1.0f, 14, UiTheme::AmberBright);
    }

    // Draws all categorized university research trees.
    void DrawResearchTree(Player* player,
                          Building* university,
                          Rectangle bounds,
                          Vec2f panOffset,
                          float zoom,
                          std::string& selectedTagFilter,
                          const std::function<void(const std::string&, Building*)>& researchRequested)
    {
        if (player == nullptr)
        {
            DrawTextFit("No research available", bounds, 16, UiTheme::ParchmentDim);
            return;
        }

        auto nodes = ResearchCatalog::BuildView(*player);
        if (nodes.empty())
        {
            DrawTextFit("No research available", bounds, 16, UiTheme::ParchmentDim);
            return;
        }

        Rectangle manpowerPanel{bounds.x, bounds.y, bounds.width, 46.0f};
        DrawResearchWorkerPanel(university, manpowerPanel);
        auto visibleTags = CollectVisibleTags(nodes);
        Rectangle tagBar{bounds.x, bounds.y + 52.0f, bounds.width, 30.0f};
        DrawTagFilterBar(tagBar, visibleTags, selectedTagFilter);
        Rectangle treeArea{bounds.x, bounds.y + 92.0f, bounds.width, bounds.height - 92.0f};

        std::map<std::string, const ResearchNodeView*> nodesById;
        for (const auto& node : nodes)
            nodesById[node.id] = &node;

        auto laneRank = [](const std::string& lane)
        {
            if (lane == "Biology") return 0;
            if (lane == "Mathematics") return 1;
            if (lane == "Humanities") return 2;
            if (lane == "PRODUCTION" || lane == "ECONOMY") return 0;
            if (lane == "MILITARY" || lane == "WARFARE") return 1;
            if (lane == "SOCIAL" || lane == "LOGISTICS") return 2;
            if (lane == "POLITICS" || lane == "GOVERNANCE") return 3;
            return 10;
        };

        std::map<std::string, int> depthCache;
        std::map<std::string, Rectangle> nodeBounds;
        std::map<std::string, std::map<int, std::vector<const ResearchNodeView*>>> nodesByLaneDepth;
        std::vector<std::string> lanes;

        for (const auto& node : nodes)
        {
            int depth = TechnologyDepth(node, nodesById, depthCache);
            std::string lane = node.layoutLane.empty() ? node.category : node.layoutLane;
            nodesByLaneDepth[lane][depth].push_back(&node);
            if (std::find(lanes.begin(), lanes.end(), lane) == lanes.end())
                lanes.push_back(lane);
        }

        std::sort(lanes.begin(), lanes.end(), [&](const std::string& a, const std::string& b)
        {
            int rankA = laneRank(a);
            int rankB = laneRank(b);
            if (rankA != rankB)
                return rankA < rankB;
            return a < b;
        });

        float nodeW = 122.0f * zoom;
        float nodeH = 108.0f * zoom;
        float colGap = 108.0f * zoom;
        float laneGap = 230.0f * zoom;
        float rowGap = 150.0f * zoom;
        float laneHeaderH = 38.0f * zoom;
        std::vector<std::pair<std::string, Rectangle>> laneHeaders;
        float laneX = treeArea.x + 28.0f + panOffset.x;

        for (const auto& lane : lanes)
        {
            auto& rows = nodesByLaneDepth[lane];
            size_t maxColumns = 1;
            for (const auto& [depth, rowNodes] : rows)
                maxColumns = std::max(maxColumns, rowNodes.size());

            float laneWidth = std::max(640.0f * zoom, maxColumns * nodeW + (maxColumns - 1) * colGap + 330.0f * zoom);
            laneHeaders.push_back({lane, Rectangle{laneX, treeArea.y + panOffset.y, laneWidth, laneHeaderH}});

            for (auto& [depth, rowNodes] : rows)
            {
                std::stable_sort(rowNodes.begin(), rowNodes.end(), [&](const ResearchNodeView* a, const ResearchNodeView* b)
                {
                    auto parentOrder = [&](const ResearchNodeView* node)
                    {
                        int order = node->layoutOrder;
                        for (const auto& prerequisite : node->prerequisites)
                        {
                            auto it = nodesById.find(prerequisite);
                            if (it != nodesById.end())
                                order = std::min(order, it->second->layoutOrder);
                        }
                        return order;
                    };

                    int parentA = parentOrder(a);
                    int parentB = parentOrder(b);
                    if (parentA != parentB)
                        return parentA < parentB;
                    if (a->layoutOrder != b->layoutOrder)
                        return a->layoutOrder < b->layoutOrder;
                    return a->definitionIndex < b->definitionIndex;
                });

                std::vector<float> desiredX(rowNodes.size(), laneX);
                float laneCenter = laneX + laneWidth * 0.5f;
                for (size_t i = 0; i < rowNodes.size(); i++)
                {
                    float parentCenterSum = 0.0f;
                    int parentCount = 0;
                    for (const auto& prerequisite : rowNodes[i]->prerequisites)
                    {
                        auto parentIt = nodeBounds.find(prerequisite);
                        if (parentIt == nodeBounds.end())
                            continue;
                        parentCenterSum += parentIt->second.x + parentIt->second.width * 0.5f;
                        parentCount++;
                    }
                    float rowOffset = (static_cast<float>(i) - (static_cast<float>(rowNodes.size()) - 1.0f) * 0.5f) * (nodeW + colGap * 1.35f);
                    int orderWithinLayer = ((rowNodes[i]->layoutOrder % 1000) + 1000) % 1000;
                    float orderNorm = static_cast<float>(orderWithinLayer) / 999.0f;
                    float laneMargin = std::min(laneWidth * 0.28f, 210.0f * zoom);
                    float orderTarget = laneX + laneMargin + orderNorm * std::max(0.0f, laneWidth - laneMargin * 2.0f - nodeW) + nodeW * 0.5f;
                    if (parentCount > 0)
                    {
                        float parentCenter = parentCenterSum / parentCount;
                        float orderInfluence = orderWithinLayer <= 80 || orderWithinLayer >= 920 ? 0.62f : 0.42f;
                        float spreadTarget = laneCenter + rowOffset;
                        float center = parentCenter * (1.0f - orderInfluence) + orderTarget * orderInfluence;
                        center = center * 0.72f + spreadTarget * 0.28f;
                        desiredX[i] = center - nodeW * 0.5f;
                    }
                    else
                    {
                        desiredX[i] = laneCenter + rowOffset - nodeW * 0.5f;
                    }
                }

                std::vector<size_t> order(rowNodes.size());
                for (size_t i = 0; i < order.size(); i++)
                    order[i] = i;
                std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b)
                {
                    return desiredX[a] < desiredX[b];
                });

                std::vector<float> placed(order.size(), laneX);
                for (size_t orderIndex = 0; orderIndex < order.size(); orderIndex++)
                {
                    size_t i = order[orderIndex];
                    placed[orderIndex] = std::clamp(desiredX[i], laneX, laneX + laneWidth - nodeW);
                }
                float minStep = nodeW + colGap;
                for (int pass = 0; pass < 2; pass++)
                {
                    for (size_t i = 1; i < placed.size(); i++)
                        placed[i] = std::max(placed[i], placed[i - 1] + minStep);
                    for (int i = static_cast<int>(placed.size()) - 2; i >= 0; i--)
                        placed[i] = std::min(placed[i], placed[i + 1] - minStep);
                }
                if (!placed.empty())
                {
                    float rowMin = placed.front();
                    float rowMax = placed.back();
                    float rowCenter = (rowMin + rowMax + nodeW) * 0.5f;
                    float targetCenter = laneCenter;
                    for (float x : desiredX)
                        targetCenter += x + nodeW * 0.5f;
                    targetCenter /= static_cast<float>(desiredX.size() + 1);
                    float shift = std::clamp(targetCenter - rowCenter, laneX - rowMin, laneX + laneWidth - nodeW - rowMax);
                    for (auto& x : placed)
                        x += shift;
                }

                for (size_t orderIndex = 0; orderIndex < order.size(); orderIndex++)
                {
                    size_t i = order[orderIndex];
                    nodeBounds[rowNodes[i]->id] = Rectangle{
                        placed[orderIndex],
                        treeArea.y + panOffset.y + laneHeaderH + 28.0f + depth * (nodeH + rowGap),
                        nodeW,
                        nodeH};
                }
            }

            laneX += laneWidth + laneGap;
        }

        Vector2 mouse = GetMousePosition();
        const ResearchNodeView* hoveredTechnology = nullptr;
        for (const auto& technology : nodes)
        {
            auto it = nodeBounds.find(technology.id);
            if (it != nodeBounds.end() && CheckCollisionPointRec(mouse, it->second))
                hoveredTechnology = &technology;
        }

        std::set<std::string> highlightedPath;
        std::function<void(const std::string&)> collectParents = [&](const std::string& id)
        {
            if (!highlightedPath.insert(id).second)
                return;
            auto it = nodesById.find(id);
            if (it == nodesById.end())
                return;
            for (const auto& prerequisite : it->second->prerequisites)
                collectParents(prerequisite);
        };
        if (hoveredTechnology != nullptr)
            collectParents(hoveredTechnology->id);

        BeginScissorMode(static_cast<int>(treeArea.x), static_cast<int>(treeArea.y), static_cast<int>(treeArea.width), static_cast<int>(treeArea.height));
        for (const auto& [lane, header] : laneHeaders)
        {
            DrawRectangleRounded(header, 0.14f, 8, UiTheme::Surface);
            DrawRectangleRoundedLines(header, 0.14f, 8, 1.0f, UiTheme::Iron);
            DrawTextFit(lane, Rectangle{header.x + 12.0f, header.y + 4.0f, header.width - 24.0f, header.height - 8.0f}, std::max(20, static_cast<int>(27 * zoom)), UiTheme::Parchment);
        }

        for (const auto& technology : nodes)
        {
            auto childIt = nodeBounds.find(technology.id);
            if (childIt == nodeBounds.end())
                continue;

            Rectangle child = childIt->second;
            for (const auto& prerequisite : technology.prerequisites)
            {
                auto parentIt = nodeBounds.find(prerequisite);
                if (parentIt == nodeBounds.end())
                    continue;

                Rectangle parent = parentIt->second;
                Vector2 a{parent.x + parent.width * 0.5f, parent.y + parent.height};
                Vector2 b{child.x + child.width * 0.5f, child.y};
                bool highlighted = highlightedPath.contains(technology.id) && highlightedPath.contains(prerequisite);
                Color edgeColor = highlighted ? UiTheme::Gold : Fade(UiTheme::Iron, 0.60f);
                float edgeWidth = highlighted ? 4.0f : 1.5f;
                Vector2 midA{a.x, a.y + rowGap * 0.34f};
                Vector2 midB{b.x, b.y - rowGap * 0.34f};
                DrawLineEx(a, midA, edgeWidth, edgeColor);
                DrawLineEx(midA, midB, edgeWidth, edgeColor);
                DrawLineEx(midB, b, edgeWidth, edgeColor);
            }
        }

        for (const auto& technology : nodes)
        {
            Rectangle node = nodeBounds[technology.id];
            bool hovered = CheckCollisionPointRec(mouse, node);
            bool tagMatched = HasNodeTag(technology, selectedTagFilter);
            const auto* research = university != nullptr ? university->GetComponent<ResearchComponent>() : nullptr;
            bool selectedUniversityBusy = research != nullptr && !research->technologyId.empty();
            bool localAvailable = technology.available && !selectedUniversityBusy;

            Color fill = technology.researched ? UiTheme::SelectedFill
                       : technology.active ? UiTheme::SurfaceHover
                       : localAvailable ? UiTheme::Surface
                       : technology.available && selectedUniversityBusy ? UiTheme::Panel
                       : technology.prerequisitesMet ? UiTheme::SurfaceHover
                       : UiTheme::Inset;
            Color line = hovered ? Color{214, 178, 96, 255}
                       : technology.researched ? Color{140, 176, 96, 255}
                       : technology.active ? Color{214, 178, 84, 255}
                       : localAvailable ? Color{176, 132, 68, 255}
                       : UiTheme::Iron;
            if (!selectedTagFilter.empty() && !tagMatched)
            {
                fill.a = 110;
                line.a = 120;
            }

            Color border = tagMatched && !selectedTagFilter.empty() ? UiTheme::SageBright
                         : highlightedPath.contains(technology.id) ? UiTheme::Gold
                         : line;
            DrawRectangleRounded(node, 0.08f, 8, UiTheme::Ink);
            Rectangle inner{node.x + 2.0f, node.y + 2.0f, node.width - 4.0f, node.height - 4.0f};
            DrawRectangleRounded(inner, 0.07f, 8, fill);
            DrawRectangleRoundedLines(node, 0.08f, 8, 1.2f, border);
            DrawRectangleRounded(Rectangle{node.x + 5.0f, node.y + 5.0f, 3.0f, node.height - 10.0f}, 0.5f, 4, line);
            DrawTextWrappedCentered(technology.name, Rectangle{node.x + 8.0f * zoom, node.y + 7.0f * zoom, node.width - 16.0f * zoom, 39.0f * zoom}, std::max(13, static_cast<int>(20 * zoom)), UiTheme::Parchment, 2);

            Color stateColor = technology.researched ? Color{162, 214, 122, 255}
                             : technology.available ? UiTheme::AmberBright
                             : technology.prerequisitesMet ? Color{238, 184, 84, 255}
                             : Color{160, 142, 112, 255};
            std::string localState = technology.available && selectedUniversityBusy && !technology.active
                ? "University busy"
                : technology.stateText;
            DrawTextFit(localState, Rectangle{node.x + 8.0f * zoom, node.y + 48.0f * zoom, node.width - 16.0f * zoom, 19.0f * zoom}, std::max(10, static_cast<int>(15 * zoom)), stateColor);
            std::string timeText = technology.active
                ? std::to_string(static_cast<int>(std::round(technology.remainingTime))) + "s left"
                : std::to_string(static_cast<int>(technology.researchTime)) + "s";
            DrawTextFit(timeText, Rectangle{node.x + 8.0f * zoom, node.y + 70.0f * zoom, node.width - 16.0f * zoom, 18.0f * zoom}, std::max(9, static_cast<int>(14 * zoom)), Color{190, 172, 140, 255});
            DrawTechnologyCost(technology.costs, Rectangle{node.x + 8.0f * zoom, node.y + 90.0f * zoom, node.width - 16.0f * zoom, 18.0f * zoom});
            if (technology.active || technology.researched)
            {
                Rectangle progress{node.x + 10.0f, node.y + node.height - 9.0f, node.width - 20.0f, 5.0f};
                DrawRectangleRounded(progress, 0.5f, 4, UiTheme::Ink);
                Rectangle fillBar = progress;
                fillBar.width *= static_cast<float>(std::clamp(technology.progress, 0.0, 1.0));
                DrawRectangleRounded(fillBar, 0.5f, 4, technology.researched ? Color{140, 176, 96, 255} : Color{214, 178, 84, 255});
            }

            if (hovered && localAvailable && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && researchRequested)
                researchRequested(technology.id, university);
        }
        EndScissorMode();

        if (hoveredTechnology != nullptr)
            DrawTechnologyTooltip(*hoveredTechnology);
    }
}

// Advances this object's state for one frame.
void UiButton::Update(double dt)
{
    Rectangle bounds = WidgetBounds(*this);
    // Layout containers intentionally keep a generous hit box, but the new
    // frame should hug the label instead of becoming a full-width strip. Keep
    // the button centred so all existing text anchors remain unchanged.
    const float minButtonWidth = std::min(bounds.width, bounds.height * 4.0f);
    const float maxButtonWidth = std::min(bounds.width, bounds.height * 5.0f);
    Rectangle visualBounds{bounds.x + (bounds.width - maxButtonWidth) * 0.5f,
                           bounds.y, maxButtonWidth, bounds.height};

        int fontSize = std::max(16, std::min(24, size.y / 3 + 2));
    int textWidth = UiText::Measure(text, fontSize);
    constexpr float buttonTextPadding = 32.0f;
    const float textAreaWidth = std::max(20.0f, maxButtonWidth - buttonTextPadding);
    while (fontSize > 9 && textWidth > textAreaWidth)
    {
        fontSize--;
        textWidth = UiText::Measure(text, fontSize);
    }

    const float textFitWidth = static_cast<float>(textWidth + buttonTextPadding);
    visualBounds.width = std::clamp(textFitWidth, minButtonWidth, maxButtonWidth);
    visualBounds.x = bounds.x + (bounds.width - visualBounds.width) * 0.5f;
    Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, visualBounds);
    bool pressed = hovered && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    const bool frameDrawn = UiControlIcons::DrawPixelHudWidgetFrame(visualBounds, hovered);
    if (!frameDrawn)
    {
        const Color fill = hovered ? UiTheme::SurfaceHover : UiTheme::Surface;
        const Color line = hovered ? UiTheme::SteelHover : UiTheme::Iron;
        // A separate steel bezel keeps large action buttons from reading as
        // a clipped piece of the panel directly behind them.
        DrawRectangleRounded(visualBounds, 0.06f, 8, UiTheme::Ink);
        DrawRectangleRoundedLines(visualBounds, 0.06f, 8, 1.4f, line);
        const float inset = std::min(4.0f, std::max(2.0f, visualBounds.height * 0.08f));
        Rectangle face{visualBounds.x + inset, visualBounds.y + inset,
                       std::max(0.0f, visualBounds.width - inset * 2.0f),
                       std::max(0.0f, visualBounds.height - inset * 2.0f)};
        DrawRectangleRounded(face, 0.05f, 8, fill);
        DrawRectangleRoundedLines(face, 0.05f, 8, 1.0f, Fade(line, hovered ? 0.88f : 0.62f));
        DrawLineEx({face.x + 7.0f, face.y + 2.0f},
                   {face.x + face.width - 7.0f, face.y + 2.0f},
                   1.0f, Fade(UiTheme::Bronze, hovered ? 0.82f : 0.50f));
        DrawLineEx({face.x + 7.0f, face.y + face.height - 2.0f},
                   {face.x + face.width - 7.0f, face.y + face.height - 2.0f},
                   1.0f, Fade(UiTheme::Ink, 0.85f));
    }

    if (drawText)
    {
        UiText::Draw(text,
                     visualBounds.x + (visualBounds.width - textWidth) * 0.5f,
                     visualBounds.y + (visualBounds.height - fontSize) * 0.5f,
                     fontSize,
                     UiTheme::Parchment);
    }

    if (pressed)
        // Handles the UI action represented by OnClick.
        OnClick();
}

// Initializes UiButton::UiButton.
UiButton::UiButton()
{
}

// Advances this object's state for one frame.
void CheckBox::Update(double dt)
{
    (void)dt;
    const Rectangle bounds = WidgetBounds(*this);
    const float boxSize = std::clamp(bounds.height * 0.74f, 28.0f, 36.0f);
    Rectangle box{bounds.x, bounds.y + (bounds.height - boxSize) * 0.5f, boxSize, boxSize};
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
    if (hovered && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        currentState = !currentState;

    DrawRectangleRounded(box, 0.13f, 8, UiTheme::Ink);
    DrawRectangleRoundedLines(box, 0.13f, 8, hovered ? 1.6f : 1.0f,
                              hovered ? UiTheme::SteelHover : UiTheme::Iron);
    Rectangle inset{box.x + 3.0f, box.y + 3.0f, box.width - 6.0f, box.height - 6.0f};
    DrawRectangleRounded(inset, 0.10f, 8, currentState ? UiTheme::SelectedFill : UiTheme::Inset);
    if (currentState)
    {
        const Color check = UiTheme::Cyan;
        DrawLineEx({inset.x + inset.width * 0.20f, inset.y + inset.height * 0.54f},
                   {inset.x + inset.width * 0.43f, inset.y + inset.height * 0.76f},
                   2.4f, check);
        DrawLineEx({inset.x + inset.width * 0.43f, inset.y + inset.height * 0.76f},
                   {inset.x + inset.width * 0.80f, inset.y + inset.height * 0.25f},
                   2.4f, check);
    }
    const float textX = box.x + box.width + 12.0f;
    const float availableWidth = std::max(0.0f, bounds.x + bounds.width - textX);
    int fontSize = std::clamp(static_cast<int>(boxSize * 0.74f), 21, 27);
    while (fontSize > 16 && UiText::Measure(text, fontSize) > availableWidth)
        --fontSize;
    UiText::Draw(text, textX, bounds.y + (bounds.height - fontSize) * 0.5f,
                 fontSize, hovered ? UiTheme::Parchment : UiTheme::ParchmentDim);
}

// Advances this object's state for one frame.
void SliderBar::Update(double dt)
{
    (void)dt;
    const Rectangle bounds = WidgetBounds(*this);
    const float labelWidth = std::clamp(bounds.width * 0.34f, 108.0f, 190.0f);
    Rectangle label{bounds.x, bounds.y, labelWidth - 10.0f, bounds.height};
    Rectangle track{bounds.x + labelWidth, bounds.y + bounds.height * 0.35f,
                    std::max(40.0f, bounds.width - labelWidth), std::max(10.0f, bounds.height * 0.30f)};
    const float knobSize = std::clamp(bounds.height * 0.78f, 20.0f, 28.0f);
    Rectangle hitArea{track.x - knobSize * 0.5f, bounds.y, track.width + knobSize, bounds.height};
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), hitArea);
    if (hovered && (InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || InputManager::IsMouseButtonDown(MOUSE_BUTTON_LEFT)))
        currentValue = std::clamp((GetMousePosition().x - track.x) / track.width, 0.0f, 1.0f);

    UiText::DrawFit(text, label, std::clamp(static_cast<int>(bounds.height * 0.54f), 16, 21), UiTheme::ParchmentDim);
    DrawRectangleRounded(track, 0.5f, 8, UiTheme::Ink);
    DrawRectangleRoundedLines(track, 0.5f, 8, hovered ? 1.4f : 1.0f,
                              hovered ? UiTheme::SteelHover : UiTheme::Iron);
    Rectangle filled{track.x + 2.0f, track.y + 2.0f,
                     std::max(0.0f, (track.width - 4.0f) * currentValue), track.height - 4.0f};
    DrawRectangleRounded(filled, 0.5f, 8, UiTheme::SelectedFill);
    const float knobX = track.x + track.width * currentValue;
    Rectangle knob{knobX - knobSize * 0.5f, bounds.y + (bounds.height - knobSize) * 0.5f, knobSize, knobSize};
    DrawRectangleRounded(knob, 0.18f, 8, UiTheme::Ink);
    DrawRectangleRoundedLines(knob, 0.18f, 8, hovered ? 1.8f : 1.2f,
                              hovered ? UiTheme::Cyan : UiTheme::Bronze);
    Rectangle knobInset{knob.x + 3.0f, knob.y + 3.0f, knob.width - 6.0f, knob.height - 6.0f};
    DrawRectangleRounded(knobInset, 0.16f, 8, hovered ? UiTheme::SurfaceHover : UiTheme::Surface);
    const std::string percent = std::to_string(static_cast<int>(std::round(currentValue * 100.0f))) + "%";
    UiText::DrawFit(percent, Rectangle{knob.x, knob.y + 1.0f, knob.width, knob.height - 2.0f},
                    std::clamp(static_cast<int>(knob.height * 0.50f), 11, 15), UiTheme::Parchment);
}

// Advances this object's state for one frame.
void ProgressBar::Update(double dt)
{
    Rectangle bounds = WidgetBounds(*this);
    DrawRectangleRounded(bounds, 0.12f, 6, UiTheme::Inset);

    Rectangle fill = bounds;
    fill.width *= value;
    DrawRectangleRounded(fill, 0.12f, 6, Color{140, 176, 96, 255});
    DrawRectangleRoundedLines(bounds, 0.12f, 6, 1.0f, UiTheme::Iron);

    std::string label = text + " " + std::to_string(static_cast<int>(value * 100.0f)) + "%";
    int fontSize = std::max(12, std::min(17, static_cast<int>(bounds.height) - 3));
    int textWidth = UiText::Measure(label, fontSize);
    UiText::Draw(label,
                 bounds.x + (bounds.width - textWidth) * 0.5f,
                 bounds.y + (bounds.height - fontSize) * 0.5f,
                 fontSize,
                 UiTheme::Parchment);
}

// Advances this object's state for one frame.
void VBox::Update(double dt)
{
    for(auto& child : children)
    {
        child->Update(dt);
    }
}

// Advances this object's state for one frame.
void HBox::Update(double dt)
{
    for(auto& child : children)
    {
        child->Update(dt);
    }
}

// Advances this object's state for one frame.
void TextBox::Update(double dt)
{
    static TextBox* activeTextBox = nullptr;

    Rectangle bounds = WidgetBounds(*this);
    if (InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        if (CheckCollisionPointRec(GetMousePosition(), bounds))
            activeTextBox = this;
        else if (activeTextBox == this)
            activeTextBox = nullptr;
    }
    if (activeTextBox == this && InputManager::IsKeyPressed(KEY_ESCAPE))
        activeTextBox = nullptr;

    bool active = activeTextBox == this;
    if (active)
    {
        int key = GetCharPressed();
        while (key > 0)
        {
            std::string encoded = EncodeUtf8Codepoint(key);
            if (!encoded.empty() && text.size() + encoded.size() < sizeof(textOutput))
                text += encoded;
            key = GetCharPressed();
        }

        if (InputManager::IsKeyPressed(KEY_BACKSPACE) || InputManager::IsKeyPressedRepeat(KEY_BACKSPACE))
            RemoveLastUtf8Codepoint(text);

        SyncTextBoxBuffer(text, textOutput, sizeof(textOutput));
    }

    DrawRectangleRec(bounds, active ? UiTheme::InsetHover : UiTheme::Inset);
    DrawRectangleLinesEx(bounds, active ? 2.0f : 1.0f, active ? UiTheme::SteelHover : UiTheme::Iron);
    Rectangle textBounds{bounds.x + 10.0f, bounds.y + 7.0f, bounds.width - 20.0f, bounds.height - 14.0f};
    int textWidth = UiText::Measure(text, 22);
    float textOffset = std::max(0.0f, static_cast<float>(textWidth) - textBounds.width + 8.0f);
    BeginScissorMode(static_cast<int>(textBounds.x), static_cast<int>(textBounds.y), static_cast<int>(textBounds.width), static_cast<int>(textBounds.height));
    UiText::Draw(text, textBounds.x - textOffset, textBounds.y + (textBounds.height - 22.0f) * 0.5f, 22, text.empty() ? UiTheme::ParchmentFaint : UiTheme::Parchment);
    EndScissorMode();
    if (active && (static_cast<int>(GetTime() * 2.0) % 2 == 0))
    {
        float cursorX = std::min(textBounds.x + textBounds.width - 2.0f, textBounds.x + static_cast<float>(textWidth) - textOffset + 3.0f);
        DrawLineEx({cursorX, textBounds.y + 4.0f}, {cursorX, textBounds.y + textBounds.height - 4.0f}, 2.0f, UiTheme::Parchment);
    }
}

// Replaces the current editable value.
void TextBox::SetValue(const std::string& value)
{
    text = value;
    SyncTextBoxBuffer(text, textOutput, sizeof(textOutput));
    text = textOutput;
}

// Advances this object's state for one frame.
void UiLabel::Update(double dt)
{
    UiText::DrawFit(text, WidgetBounds(*this), fontSize, color);
}

PopupWindowWidget::PopupWindowWidget()
{
    actionButton.ChangeText("Continue");
    UpdateSize({GetScreenWidth(), GetScreenHeight()});
}

void PopupWindowWidget::Show(std::string newTitle, std::string newBody,
                             std::string actionText, std::function<void()> action)
{
    const bool wasVisible = visible;
    title = std::move(newTitle);
    body = std::move(newBody);
    actionButton.ChangeText(std::move(actionText));
    actionButton.func = std::move(action);
    visible = true;
    if (!wasVisible && modalStateCallback)
        modalStateCallback(true);
    UpdateSize({GetScreenWidth(), GetScreenHeight()});
}

void PopupWindowWidget::Hide()
{
    if (!visible)
        return;

    visible = false;
    if (modalStateCallback)
        modalStateCallback(false);
}

void PopupWindowWidget::SetModalStateCallback(std::function<void(bool)> callback)
{
    modalStateCallback = std::move(callback);
    if (visible && modalStateCallback)
        modalStateCallback(true);
}

void PopupWindowWidget::UpdateSize(Vec2i windowSize)
{
    const int panelWidth = std::clamp(static_cast<int>(windowSize.x * 0.56f), 520, 820);
    const int panelHeight = std::clamp(static_cast<int>(windowSize.y * 0.48f), 330, 500);
    size = {panelWidth, panelHeight};
    const int maxX = std::max(8, windowSize.x - panelWidth - 8);
    pos = {std::clamp((windowSize.x - panelWidth) / 2 + horizontalOffset, 8, maxX),
           (windowSize.y - panelHeight) / 2};

    const int buttonWidth = std::clamp(panelWidth / 3, 180, 260);
    actionButton.ChangeSize(buttonWidth, 52);
    actionButton.ChangePosition(pos.x + (panelWidth - buttonWidth) / 2,
                                pos.y + panelHeight - 76);
}

void PopupWindowWidget::Update(double dt)
{
    if (!visible)
        return;

    if (dimBackground)
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{4, 9, 17, 190});

    Rectangle panel{static_cast<float>(pos.x), static_cast<float>(pos.y),
                    static_cast<float>(size.x), static_cast<float>(size.y)};
    if (!UiControlIcons::DrawPixelHudFrame(panel))
    {
        DrawRectangleRounded(panel, 0.035f, 10, UiTheme::Panel);
        DrawRectangleRoundedLines(panel, 0.035f, 10, 2.0f, UiTheme::Bronze);
    }

    const float panelInset = UiControlIcons::PixelHudFrameInset(panel);
    Rectangle titleBar{panel.x + panelInset, panel.y + 5.0f,
                       panel.width - panelInset * 2.0f, 65.0f};
    UiText::DrawTitleBar(titleBar, title, 0.0f);

    constexpr int bodyFontSize = 22;
    const float textX = panel.x + 42.0f;
    float textY = titleBar.y + titleBar.height + 32.0f;
    const float textWidth = panel.width - 84.0f;
    for (const auto& line : UiText::WrapWithControlIcons(body, bodyFontSize, textWidth, 30.0f))
    {
        UiText::DrawWithControlIcons(line, textX, textY, bodyFontSize, UiTheme::Parchment, 30.0f);
        textY += bodyFontSize + 10.0f;
    }

    // The modal popup is the only widget allowed to consume input while the
    // scene is paused. Temporarily enable the shared input gate for its button
    // and restore the previous state after processing the click.
    const bool wasInputEnabled = InputManager::IsInputEnabled();
    InputManager::SetInputEnabled(true);
    actionButton.Update(dt);
    InputManager::SetInputEnabled(visible ? wasInputEnabled : true);
}

void TutorialTaskWidget::UpdateSize(Vec2i windowSize)
{
    pos = {12, std::max(52, static_cast<int>(windowSize.y * 0.075f))};
    size = {std::clamp(static_cast<int>(windowSize.x * 0.27f), 300, 390), 54 + static_cast<int>(tasks.size()) * 28};
}

void TutorialTaskWidget::Update(double dt)
{
    if (tasks.empty())
        return;

    UpdateSize({GetScreenWidth(), GetScreenHeight()});
    Rectangle panel{static_cast<float>(pos.x), static_cast<float>(pos.y),
                    static_cast<float>(size.x), static_cast<float>(size.y)};
    if (!UiControlIcons::DrawPixelHudPanelFrame(panel))
    {
        DrawRectangleRounded(panel, 0.06f, 8, Color{25, 19, 14, 224});
        DrawRectangleRoundedLines(panel, 0.06f, 8, 1.0f, UiTheme::Bronze);
    }

    UiText::Draw("Tutorial", panel.x + 14.0f, panel.y + 8.0f, 16, UiTheme::Gold);
    UiText::DrawFit(title, Rectangle{panel.x + 92.0f, panel.y + 7.0f, panel.width - 104.0f, 22.0f}, 15, UiTheme::Parchment);

    float y = panel.y + 32.0f;
    for (const auto& [task, completed] : tasks)
    {
        const Color color = completed ? Color{126, 204, 132, 255} : UiTheme::ParchmentDim;
        const Vector2 markerCenter{panel.x + 20.0f, y + 9.0f};
        DrawCircleV(markerCenter, completed ? 5.0f : 4.0f, color);
        if (completed)
            DrawCircleLines(static_cast<int>(markerCenter.x), static_cast<int>(markerCenter.y), 7.0f,
                            Color{126, 204, 132, 210});
        Rectangle textBounds{panel.x + 34.0f, y - 1.0f, panel.width - 48.0f, 22.0f};
        UiText::DrawFit(task, textBounds, 15, color);
        if (completed)
        {
            int fittedFont = 15;
            int textWidth = UiText::Measure(task, fittedFont);
            while (fittedFont > 8 && textWidth > textBounds.width)
            {
                --fittedFont;
                textWidth = UiText::Measure(task, fittedFont);
            }
            const float textX = textBounds.x + (textBounds.width - textWidth) * 0.5f;
            const float strikeY = y + 10.0f;
            DrawLineEx({textX, strikeY},
                       {textX + static_cast<float>(textWidth), strikeY},
                       1.0f, Color{126, 204, 132, 190});
        }
        y += 28.0f;
    }
}

// Loads the requested data into runtime state.
bool UiImage::LoadTextureFromFile(const std::string& path)
{
    if (!FileExists(path.c_str()))
        return false;

    tvorin::ui::TextureHandle next{LoadTexture(path.c_str())};
    if (!next)
        return false;

    SetTextureFilter(next.Get(), TEXTURE_FILTER_POINT);
    texture = std::move(next);
    hasTexture = true;
    return hasTexture;
}

UiImage::~UiImage() = default;

// Draws a decorative menu surface behind the controls.
void UiPanel::Update(double dt)
{
    (void)dt;
    const Rectangle bounds = WidgetBounds(*this);
    if (bounds.width <= 0.0f || bounds.height <= 0.0f)
        return;

    DrawRectangleRounded(
        Rectangle{bounds.x + 4.0f, bounds.y + 5.0f, bounds.width, bounds.height},
        cornerRadius, 10, Color{0, 0, 0, 120});
    DrawRectangleRounded(bounds, cornerRadius, 10, fill);
    DrawRectangleRoundedLines(bounds, cornerRadius, 10, borderThickness, border);
}

UiParallaxBackground::~UiParallaxBackground() = default;

bool UiParallaxBackground::LoadFromDirectory(const std::string& directory)
{
    ClearTextures();
    scrollOffset = 0.0;

    for (int layer = 1; layer <= 4; ++layer)
    {
        const std::string path = directory + "/layer_" + std::to_string(layer) + ".png";
        if (!FileExists(path.c_str()))
            break;

        tvorin::ui::TextureHandle texture{LoadTexture(path.c_str())};
        if (!texture)
            return !layers.empty();
        SetTextureFilter(texture.Get(), TEXTURE_FILTER_POINT);
        layers.push_back(std::move(texture));
    }

    return !layers.empty();
}

void UiParallaxBackground::ClearTextures()
{
    layers.clear();
}

void UiParallaxBackground::Update(double dt)
{
    if (layers.empty())
        return;

    const Rectangle bounds = WidgetBounds(*this);
    if (bounds.width <= 0.0f || bounds.height <= 0.0f)
        return;

    scrollOffset += std::max(0.0, dt) * scrollSpeed;

    for (std::size_t index = 0; index < layers.size(); ++index)
    {
        const Texture2D& texture = layers[index].Get();
        const float imageRatio = texture.width / static_cast<float>(texture.height);
        const float drawHeight = std::max(bounds.height, bounds.width / imageRatio);
        const float drawWidth = drawHeight * imageRatio;
        const float drawY = bounds.y + (bounds.height - drawHeight) * 0.5f;

        if (index != scrollLayerIndex)
        {
            Rectangle source{0.0f, 0.0f, static_cast<float>(texture.width),
                             static_cast<float>(texture.height)};
            if (drawWidth > bounds.width)
            {
                const float cropWidth = texture.height * bounds.width / bounds.height;
                source.x = (texture.width - cropWidth) * 0.5f;
                source.width = cropWidth;
            }
            else if (drawHeight > bounds.height)
            {
                const float cropHeight = texture.width * bounds.height / bounds.width;
                source.y = (texture.height - cropHeight) * 0.5f;
                source.height = cropHeight;
            }
            DrawTexturePro(texture, source, bounds, {0.0f, 0.0f}, 0.0f, WHITE);
            continue;
        }

        const float offset = std::fmod(static_cast<float>(scrollOffset), drawWidth);
        const float firstX = bounds.x - offset - drawWidth;
        for (float x = firstX; x < bounds.x + bounds.width; x += drawWidth)
        {
            DrawTexturePro(texture,
                           {0.0f, 0.0f, static_cast<float>(texture.width),
                            static_cast<float>(texture.height)},
                           {x, drawY, drawWidth, drawHeight},
                           {0.0f, 0.0f}, 0.0f, WHITE);
        }
    }
}

// Advances this object's state for one frame.
void UiImage::Update(double dt)
{
    Rectangle bounds = WidgetBounds(*this);
    if (hasTexture)
    {
        const Texture2D& textureValue = texture.Get();
        Rectangle src{0.0f, 0.0f, static_cast<float>(textureValue.width), static_cast<float>(textureValue.height)};
        if (cover)
        {
            float imageRatio = textureValue.width / static_cast<float>(textureValue.height);
            float targetRatio = bounds.width / bounds.height;

            if (imageRatio > targetRatio)
            {
                float cropW = textureValue.height * targetRatio;
                src.x = (textureValue.width - cropW) * 0.5f;
                src.width = cropW;
            }
            else
            {
                float cropH = textureValue.width / targetRatio;
                src.y = (textureValue.height - cropH) * 0.5f;
                src.height = cropH;
            }
        }
        DrawTexturePro(textureValue, src, bounds, {0.0f, 0.0f}, 0.0f, WHITE);
    }
    else
    {
        DrawRectangleRounded(bounds, 0.04f, 8, UiTheme::Inset);
        DrawRectangleRoundedLines(bounds, 0.04f, 8, 1.0f, UiTheme::Iron);
    }
}

UiAnimation::~UiAnimation() = default;

bool UiAnimation::AddFrameFromFile(const std::string& path)
{
    if (!FileExists(path.c_str()))
        return false;

    tvorin::ui::TextureHandle frame{LoadTexture(path.c_str())};
    if (!frame)
        return false;

    SetTextureFilter(frame.Get(), TEXTURE_FILTER_POINT);
    frames.push_back(std::move(frame));
    return true;
}

void UiAnimation::ClearFrames()
{
    frames.clear();
}

void UiAnimation::SetFrameDuration(float seconds)
{
    frameDuration = std::max(0.01f, seconds);
}

void UiAnimation::SetFloating(float amplitudePixels, float periodSeconds, float phase)
{
    floatAmplitude = std::max(0.0f, amplitudePixels);
    floatPeriod = std::max(0.1f, periodSeconds);
    floatPhase = phase;
}

void UiAnimation::Reset()
{
    elapsed = 0.0;
}

void UiAnimation::Update(double dt)
{
    if (frames.empty())
        return;

    elapsed += std::max(0.0, dt);
    const std::size_t frameIndex = static_cast<std::size_t>(
        std::floor(elapsed / static_cast<double>(frameDuration))) % frames.size();
    const Texture2D& texture = frames[frameIndex].Get();
    Rectangle bounds = WidgetBounds(*this);
    const float imageRatio = texture.width / static_cast<float>(texture.height);
    const float drawWidth = std::min(bounds.width, bounds.height * imageRatio);
    const float drawHeight = drawWidth / imageRatio;
    const float floatOffset = floatAmplitude *
        std::sin(static_cast<float>(elapsed * 2.0 * PI / floatPeriod) + floatPhase);
    Rectangle destination{
        bounds.x + (bounds.width - drawWidth) * 0.5f,
        bounds.y + (bounds.height - drawHeight) * 0.5f + floatOffset,
        drawWidth,
        drawHeight};
    DrawTexturePro(texture,
                   {0.0f, 0.0f, static_cast<float>(texture.width),
                    static_cast<float>(texture.height)},
                   destination, {0.0f, 0.0f}, 0.0f, WHITE);
}

// Loads the requested data into runtime state.
void ResourceIconAtlas::Load(const std::string& path, Vec2i iconSize)
{
    if (!FileExists(path.c_str()))
        return;

    tvorin::ui::TextureHandle next{LoadTexture(path.c_str())};
    if (!next)
        return;

    SetTextureFilter(next.Get(), TEXTURE_FILTER_POINT);
    texture = std::move(next);
    loaded = true;
    size = iconSize;
}

void ResourceIconAtlas::Unload()
{
    texture.Reset();
    loaded = false;
}

// Returns atlas source rectangle for one resource icon.
Rectangle ResourceIconAtlas::GetRect(ResourceType type) const
{
    int index = type == ResourceType::Null ? 0 : std::max(0, static_cast<int>(type));
    int columns = std::max(1, texture.Get().width / size.x);
    return Rectangle{
        static_cast<float>((index % columns) * size.x),
        static_cast<float>((index / columns) * size.y),
        static_cast<float>(size.x),
        static_cast<float>(size.y)};
}

// Advances this object's state for one frame.
// Draws background, title bar, drag handling and the close button; reports
// the content area below the title bar. Returns false (after already having
// called Close()) when the close button was clicked this frame — the caller
// must stop drawing content, exactly as the panel did when this was all one
// function.
bool GuiPanel::DrawChrome(double dt, Rectangle& outContentArea)
{
    Rectangle bounds = WidgetBounds(*this);
    int titleBar = std::max(34, size.y / 12);
    const float frameInset = UiControlIcons::PixelHudFrameInset(bounds);
    const int margin = std::max(10, size.x / 24);

    const bool panelDrawn = UiControlIcons::DrawPixelHudFrame(bounds);
    if (!panelDrawn)
    {
        DrawRectangleRounded(bounds, 0.02f, 8, UiTheme::Panel);
        DrawRectangleRoundedLines(bounds, 0.02f, 8, 1.0f, UiTheme::Iron);
    }

    Rectangle titleBounds{
        bounds.x,
        bounds.y,
        bounds.width,
        static_cast<float>(titleBar)};
    Vector2 mouse = GetMousePosition();
    Rectangle titleVisual{titleBounds.x + frameInset + 2.0f, titleBounds.y + 4.0f,
                          std::max(0.0f, titleBounds.width - (frameInset + 2.0f) * 2.0f),
                          std::max(0.0f, titleBounds.height - 8.0f)};
    // The main 9-slice already supplies the header rail. A second plaque
    // behind the title would visually split the panel into two skins.

    Rectangle closeBounds = UiControlIcons::PixelHudCloseButtonRect(bounds);
    const float closeWidth = closeBounds.width;
    const float closeEndGap = titleVisual.x + titleVisual.width -
                              (closeBounds.x + closeBounds.width);
    bool closeHovered = CheckCollisionPointRec(GetMousePosition(), closeBounds);
    if (!UiControlIcons::DrawPanelCloseButton(closeBounds, closeHovered))
    {
        DrawRectangleRounded(closeBounds, 0.16f, 6, closeHovered ? Color{55, 94, 128, 245} : Color{31, 46, 66, 245});
        DrawRectangleRoundedLines(closeBounds, 0.16f, 6, 1.0f, closeHovered ? UiTheme::Cyan : UiTheme::Iron);
        int xFont = std::max(13, static_cast<int>(closeBounds.height) / 2);
        int xWidth = UiText::Measure("X", xFont);
        UiText::Draw("X", closeBounds.x + (closeBounds.width - xWidth) * 0.5f,
                     closeBounds.y + (closeBounds.height - xFont) * 0.5f,
                     xFont, UiTheme::Parchment);
    }

    if (closeHovered && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        Close();
        return false;
    }

    // Drag: click+hold on title bar (outside close button) to reposition
    if (!closeHovered && CheckCollisionPointRec(mouse, titleBounds) && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        dragging = true;
        dragOffset = Vec2i{static_cast<int>(mouse.x) - pos.x, static_cast<int>(mouse.y) - pos.y};
    }
    if (dragging && InputManager::IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        pos.x = std::clamp(static_cast<int>(mouse.x) - dragOffset.x, 0, std::max(0, GetScreenWidth() - size.x));
        pos.y = std::clamp(static_cast<int>(mouse.y) - dragOffset.y, 0, std::max(0, GetScreenHeight() - size.y));
        bounds = {static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
        titleBounds = {bounds.x, bounds.y, bounds.width, static_cast<float>(titleBar)};
        titleVisual = {titleBounds.x + frameInset + 2.0f, titleBounds.y + 4.0f,
                       std::max(0.0f, titleBounds.width - (frameInset + 2.0f) * 2.0f),
                       std::max(0.0f, titleBounds.height - 8.0f)};
        closeBounds = UiControlIcons::PixelHudCloseButtonRect(bounds);
    }
    if (dragging && InputManager::IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        dragging = false;

    UiText::DrawTitleBar(titleVisual, text,
                         closeWidth + closeEndGap + 10.0f);

    outContentArea = Rectangle{
        static_cast<float>(pos.x + margin),
        static_cast<float>(pos.y + titleBar + margin),
        static_cast<float>(size.x - margin * 2),
        static_cast<float>(size.y - titleBar - margin * 2)};
    return true;
}

void GuiPanel::Update(double dt)
{
    pendingTooltip.visible = false;

    if (building == nullptr)
        return;

    Rectangle contentArea;
    if (!DrawChrome(dt, contentArea))
        return;

    int margin = std::max(10, size.x / 24);
    int y = static_cast<int>(contentArea.y);
    int contentX = static_cast<int>(contentArea.x);
    int contentW = static_cast<int>(contentArea.width);
    int bottom = static_cast<int>(contentArea.y + contentArea.height);
    auto drawDestroyButton = [&]()
    {
        if (!building->CanBeManuallyDestroyed())
            return;

        destroyButton.pos = Vec2i{contentX, bottom - destroyButton.size.y};
        destroyButton.size = Vec2i{contentW, destroyButton.size.y};
        destroyButton.ChangeText("Destroy building");
        destroyButton.Update(dt);
    };

    if (building->IsUnderConstruction())
    {
        int queuePosition = 0;
        int builderCount = 0;
        bool builderAssigned = false;
        if (building->owner != nullptr)
        {
            queuePosition = building->owner->construction.QueuePosition(building->id);
            builderCount = building->owner->construction.EffectiveBuilders(*building->owner);
            builderAssigned = building->owner->construction.IsActive(building->id);
        }

        UiText::Draw(builderAssigned ? "Under construction" : "Waiting in build queue",
                     contentX, y, 22, Color{224, 204, 168, 255});
        y += 34;
        progressBar.pos = Vec2i{contentX, y};
        progressBar.size = Vec2i{contentW, 30};
        progressBar.ChangeText("Construction");
        progressBar.SetValue(building->GetConstructionProgress());
        progressBar.Update(dt);
        y += 46;

        std::string remaining = "Remaining: " + std::to_string(static_cast<int>(std::ceil(building->constructionRemaining))) + "s";
        DrawTextFit(remaining, Rectangle{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), 20.0f}, 15, Color{224, 204, 168, 255});
        y += 24;

        if (queuePosition > 0)
        {
            std::string queueLine = "Queue position: " + std::to_string(queuePosition) +
                                    " / " + std::to_string(building->owner->construction.QueueLength()) +
                                    "   Builders: " + std::to_string(builderCount);
            DrawTextFit(queueLine, Rectangle{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), 20.0f}, 15, UiTheme::ParchmentDim);
        }

        // The Destroy path already unwinds a placed-but-unfinished building; here it
        // reads as cancelling the order. Refresh() re-numbers the rest of the queue.
        if (building->CanBeManuallyDestroyed())
        {
            destroyButton.pos = Vec2i{contentX, bottom - destroyButton.size.y};
            destroyButton.size = Vec2i{contentW, destroyButton.size.y};
            destroyButton.ChangeText("Remove from queue");
            destroyButton.Update(dt);
        }
        return;
    }

    // TD(etap-8.3): Headquarters — checked before IsStorageLike() (HqComponent
    // buildings also have StorageComponent) so it gets its own HP/defense
    // content instead of falling into the generic storage-grid branch below.
    // Shown for any player's HQ (own or enemy) per the plan's "HP HQ własnego
    // (i wroga przy kliknięciu)".
    if (auto* hq = building->GetComponent<HqComponent>())
    {
        double maxHp = hq->GetModifiedMaxHp(*building);
        double hpRatio = maxHp > 0.0 ? std::clamp(hq->currentHp / maxHp, 0.0, 1.0) : 0.0;
        progressBar.pos = Vec2i{contentX, y};
        progressBar.size = Vec2i{contentW, 30};
        progressBar.ChangeText("");
        progressBar.SetValue(static_cast<float>(hpRatio));
        progressBar.Update(dt);
        y += 34;

        const std::string hpText = "HP " + std::to_string(static_cast<int>(std::round(std::max(0.0, hq->currentHp)))) +
                                   " / " + std::to_string(static_cast<int>(std::round(maxHp))) +
                                   " (" + std::to_string(static_cast<int>(std::round(hpRatio * 100.0))) + "%)";
        UiText::Draw(hpText, static_cast<float>(contentX), static_cast<float>(y), 18, UiTheme::Parchment);
        y += 30;

        std::vector<std::string> stats{
            "Hard defense: " + std::to_string(static_cast<int>(std::round(hq->GetModifiedHardDefense(*building)))),
            "Thorns damage: " + std::to_string(static_cast<int>(std::round(hq->GetModifiedThornsDamage(*building)))),
            "Thorns interval: " + FormatDecimal(hq->thornsInterval) + "s"};
        for (const auto& stat : stats)
        {
            UiText::Draw(stat, static_cast<float>(contentX), static_cast<float>(y), 21, UiTheme::Parchment);
            y += 29;
        }
        y += margin / 2;

        UiText::Draw("Storage", contentX, y, 20, Color{224, 204, 168, 255});
        y += 26;
        Rectangle grid{
            static_cast<float>(contentX),
            static_cast<float>(y),
            static_cast<float>(contentW),
            // Headquarters cannot be manually destroyed, so it has no bottom
            // action button to reserve room for. Give that space to storage.
            static_cast<float>(bottom - y)};
        DrawResourceIconGrid(building->GetOutputBufferViews(), grid, 5, &contentScrollOffset, &maxContentScrollOffset, building, &contentScrollbarDragging, &contentScrollbarDragOffset);
        contentScrollOffset = std::clamp(contentScrollOffset, 0.0f, maxContentScrollOffset);
        drawDestroyButton(); // no-op: Headquarters::CanBeManuallyDestroyed() == false
        DrawPendingTooltip();
        return;
    }

    // TD(etap-8.2): Defense tower — same reordering reason as HQ above
    // (TowerCombatComponent buildings also have StorageComponent, used here
    // purely as the ammo buffer).
    if (auto* tower = building->GetComponent<TowerCombatComponent>())
    {
        UiText::Draw("Defense Tower", contentX, y, 22, Color{224, 204, 168, 255});
        y += 30;

        std::vector<std::string> stats{
            "Damage: " + std::to_string(static_cast<int>(std::round(tower->GetModifiedDamage(*building)))),
            "Range: " + std::to_string(static_cast<int>(std::round(tower->GetModifiedRange(*building)))) + " tiles",
            "Attack speed: " + FormatDecimal(tower->GetModifiedAttackSpeed(*building)) + "/s",
            "Crew: " + std::to_string(building->GetAssignedWorkers()) + "/" + std::to_string(building->GetWorkerCapacity())};
        for (const auto& stat : stats)
        {
            DrawTextFit(stat, Rectangle{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), 20.0f}, 15, UiTheme::Parchment);
            y += 24;
        }
        y += margin / 2;

        towerTargetButton.pos = Vec2i{contentX, y};
        towerTargetButton.size = Vec2i{contentW, std::max(30, lockButton.size.y)};
        towerTargetButton.ChangeText(tower->targetMode == TowerTargetMode::NearestToHq
            ? "Target: nearest to HQ"
            : "Target: strongest unit");
        towerTargetButton.Update(dt);
        y += towerTargetButton.size.y + margin;

        UiText::Draw("Ammunition", contentX, y, 20, Color{224, 204, 168, 255});
        y += 26;
        Rectangle grid{
            static_cast<float>(contentX),
            static_cast<float>(y),
            static_cast<float>(contentW),
            static_cast<float>(bottom - y - destroyButton.size.y - margin)};
        DrawResourceIconGrid(building->GetOutputBufferViews(), grid, 4, nullptr);
        drawDestroyButton();
        DrawPendingTooltip();
        return;
    }

    // TD(etap-8.4): recruitment building (Barracks) — same reordering reason.
    // Deliberately does not also show the generic storage grid (delivered
    // unit-cost resources): the recruit buttons below already surface each
    // unit's cost, and panel space is tight with both the queue and the
    // catalog list present.
    if (auto* recruitment = building->GetComponent<RecruitmentComponent>())
    {
        UiText::Draw("Recruitment", contentX, y, 22, Color{224, 204, 168, 255});
        y += 30;

        UiText::Draw("Available units", contentX, y, 18, UiTheme::AmberBright);
        y += 24;

        std::vector<std::pair<std::string, const UnitDefinition*>> units;
        for (const auto& [id, definition] : GetUnitCatalog())
            if (definition.recruitBuilding == building->buildingType)
                units.emplace_back(id, &definition);
        std::stable_sort(units.begin(), units.end(), [](const auto& lhs, const auto& rhs)
        {
            const int lhsRank = UnitPresentationRank(lhs.first);
            const int rhsRank = UnitPresentationRank(rhs.first);
            return lhsRank != rhsRank ? lhsRank < rhsRank : lhs.first < rhs.first;
        });

        constexpr int columns = 4;
        constexpr float gap = 8.0f;
        const float cellSize = std::clamp(
            std::floor((contentW - gap * (columns - 1)) / columns), 48.0f, 104.0f);
        const int rows = std::max(1, static_cast<int>((units.size() + columns - 1) / columns));
        const float gridWidth = columns * cellSize + (columns - 1) * gap;
        const float gridX = contentX + (contentW - gridWidth) * 0.5f;
        const float gridY = static_cast<float>(y);

        Building* self = building;
        GameScene* panelScene = scene;
        const UnitDefinition* hoveredDefinition = nullptr;
        std::string hoveredUnitId;
        std::string hoveredBlockReason;
        double hoveredManpowerCost = 0.0;
        double hoveredRecruitTime = 0.0;
        const Vector2 mouse = GetMousePosition();
        for (size_t index = 0; index < units.size(); ++index)
        {
            const std::string& id = units[index].first;
            const UnitDefinition& definition = *units[index].second;
            const int column = static_cast<int>(index % columns);
            const int row = static_cast<int>(index / columns);
            Rectangle card{
                gridX + column * (cellSize + gap),
                gridY + row * (cellSize + gap),
                cellSize,
                cellSize};
            const bool hovered = CheckCollisionPointRec(mouse, card);

            const double effectiveManpowerCost = building->owner != nullptr
                ? std::max(0.0, building->owner->ModifyBalanceForUnit(
                    BalanceStat::UnitRecruitManpowerCost, definition.manpowerCost, id))
                : definition.manpowerCost;
            const double effectiveRecruitTime = building->owner != nullptr
                ? std::max(1.0, building->owner->ModifyBalanceForUnit(
                    BalanceStat::UnitRecruitTime, definition.recruitTime, id))
                : definition.recruitTime;
            const std::string blockReason = recruitment->DiagnoseRecruitmentBlock(*building, id);
            const bool available = blockReason.empty();

            const Color frameTint = available ? WHITE : Color{126, 128, 132, 220};
            if (!UiControlIcons::DrawPixelHudWidgetFrame(card, hovered, frameTint))
            {
                DrawRectangleRec(card, UiTheme::Inset);
                DrawRectangleLinesEx(card, 1.0f, UiTheme::Iron);
            }
            // The portrait renderer performs its own aspect fit; reserve a
            // wider inset so bows, spearheads and siege wheels never touch the
            // 9-slice corners even in the narrow four-column layout.
            Rectangle portrait{card.x + 9.0f, card.y + 9.0f,
                               card.width - 18.0f, card.height - 18.0f};
            if (!UiControlIcons::DrawUnitPortrait(
                    id, portrait, available ? WHITE : Color{132, 132, 132, 210}))
            {
                DrawTextFit(definition.displayName, portrait, 14,
                            available ? UiTheme::Parchment : UiTheme::ParchmentDim);
            }
            if (!available)
                DrawRectangleRec({card.x + 4.0f, card.y + 4.0f,
                                  card.width - 8.0f, card.height - 8.0f},
                                 Fade(BLACK, 0.34f));
            if (hovered)
                DrawRectangleLinesEx({card.x + 2.0f, card.y + 2.0f,
                                      card.width - 4.0f, card.height - 4.0f},
                                     2.0f, available ? UiTheme::AmberBright
                                                     : Color{190, 104, 82, 255});

            if (hovered)
            {
                hoveredDefinition = &definition;
                hoveredUnitId = id;
                hoveredBlockReason = blockReason;
                hoveredManpowerCost = effectiveManpowerCost;
                hoveredRecruitTime = effectiveRecruitTime;
                if (available && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                    self != nullptr && panelScene != nullptr && panelScene->game != nullptr)
                {
                    panelScene->SubmitLocalCommand(GameCommand::RecruitUnit(
                        panelScene->game->GetLocalPlayerId(), self->positionId, id));
                }
            }
        }
        y += static_cast<int>(rows * cellSize + (rows - 1) * gap);

        if (!recruitment->queue.empty())
        {
            y += margin / 2;
            UiText::Draw("Queue", contentX, y, 18, UiTheme::AmberBright);
            y += 24;
            for (const auto& entry : recruitment->queue)
            {
                if (y + 34 > bottom - destroyButton.size.y - margin)
                    break;

                const UnitDefinition* def = FindUnitDefinition(entry.unitDefId);
                std::string name = def != nullptr ? def->displayName : entry.unitDefId;
                std::string label = entry.resourcesReady
                    ? name + " - " + FormatSeconds(entry.remaining) + " / " + FormatSeconds(entry.total)
                    : name + " - Waiting for resources";
                DrawTextFit(label, Rectangle{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), 18.0f},
                    14, entry.resourcesReady ? UiTheme::Parchment : Color{230, 190, 110, 255});
                y += 20;

                float progress = entry.resourcesReady && entry.total > 0.0
                    ? std::clamp(static_cast<float>(1.0 - entry.remaining / entry.total), 0.0f, 1.0f)
                    : 0.0f;
                Rectangle bar{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), 8.0f};
                DrawRectangleRounded(bar, 0.2f, 4, UiTheme::Ink);
                Rectangle fill = bar;
                fill.width *= progress;
                DrawRectangleRounded(fill, 0.2f, 4, entry.resourcesReady ? Color{140, 176, 96, 255} : Color{150, 120, 60, 255});
                y += 14;
            }
        }

        drawDestroyButton();
        if (hoveredDefinition != nullptr)
            DrawUnitRecruitmentTooltip(*building, hoveredUnitId, *hoveredDefinition,
                                       hoveredManpowerCost, hoveredRecruitTime,
                                       hoveredBlockReason);
        return;
    }

    if (building->IsStorageLike())
    {
        UiText::Draw("Storage", contentX, y, 22, Color{224, 204, 168, 255});
        y += 28;

        Rectangle grid{
            static_cast<float>(contentX),
            static_cast<float>(y),
            static_cast<float>(contentW),
            static_cast<float>(bottom - y - destroyButton.size.y - margin)};
        DrawResourceIconGrid(building->GetOutputBufferViews(), grid, 5, &contentScrollOffset, &maxContentScrollOffset, building, &contentScrollbarDragging, &contentScrollbarDragOffset);
        contentScrollOffset = std::clamp(contentScrollOffset, 0.0f, maxContentScrollOffset);
        drawDestroyButton();
        DrawPendingTooltip();
        return;
    }

    if (building->HasComponent<RoadComponent>())
    {
        auto* road = building->GetComponent<RoadComponent>();
        auto* upgrade = building->GetComponent<UpgradeComponent>();
        UiText::Draw("Transport", contentX, y, 22, Color{224, 204, 168, 255});
        y += 30;

        if (road != nullptr && upgrade != nullptr)
        {
            int used = static_cast<int>(building->transportables.size());
            std::vector<std::string> stats{
                "Capacity: " + std::to_string(used) + "/" + std::to_string(road->GetModifiedMaxCapacity(*building)),
                "Speed: x" + std::to_string(static_cast<int>(road->GetModifiedSpeedModifier(*building) * 100.0)) + "%",
                "Upgrade level: " + std::to_string(upgrade->level) + "/" + std::to_string(upgrade->maxLevel)};
            if (upgrade->isUpgrading)
                stats.push_back("Upgrading: " + std::to_string(static_cast<int>(std::ceil(upgrade->upgradeRemaining))) + "s left");

            for (const auto& stat : stats)
            {
                DrawTextFit(stat, Rectangle{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), 18.0f}, 15, UiTheme::Parchment);
                y += 22;
            }

            // Product priority is a command-backed intent. The compact picker
            // deliberately lists only the physical resource catalog; Null is
            // exposed as Clear and strategic resources never enter this UI.
            UiText::Draw("Admission priority", contentX, y, 18, UiTheme::Parchment);
            y += 24;
            Rectangle priorityButton{static_cast<float>(contentX), static_cast<float>(y),
                                     static_cast<float>(contentW), 30.0f};
            const bool priorityHovered = CheckCollisionPointRec(GetMousePosition(), priorityButton);
            DrawRectangleRounded(priorityButton, 0.12f, 6,
                                 priorityHovered ? Color{48, 76, 98, 255} : UiTheme::Inset);
            DrawRectangleRoundedLines(priorityButton, 0.12f, 6, 1.0f,
                                      priorityHovered ? UiTheme::Cyan : UiTheme::Iron);
            if (road->priorityResource == ResourceType::Null)
                DrawTextFit("None (FIFO)", {priorityButton.x + 8.0f, priorityButton.y + 7.0f,
                                             priorityButton.width - 16.0f, 17.0f}, 14, UiTheme::ParchmentDim);
            else
            {
                GuiPanel::DrawResourceIcon(road->priorityResource,
                    {priorityButton.x + 6.0f, priorityButton.y + 4.0f, 22.0f, 22.0f});
                DrawTextFit(ResourceDisplayName(road->priorityResource),
                    {priorityButton.x + 36.0f, priorityButton.y + 7.0f,
                     priorityButton.width - 44.0f, 17.0f}, 14, UiTheme::Parchment);
            }
            if (priorityHovered && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                roadPriorityPickerOpen = !roadPriorityPickerOpen;
            y += 36;

            if (roadPriorityPickerOpen)
            {
                const int columns = 8;
                const int cellWidth = std::max(1, contentW / columns);
                const int cellHeight = 30;
                const int rowCount = (static_cast<int>(std::size(resourceTypes)) + columns - 1) / columns;
                Rectangle picker{static_cast<float>(contentX), static_cast<float>(y),
                                 static_cast<float>(contentW), static_cast<float>(rowCount * cellHeight + 42)};
                DrawRectangleRounded(picker, 0.06f, 6, Color{22, 34, 49, 248});
                DrawRectangleRoundedLines(picker, 0.06f, 6, 1.0f, UiTheme::Cyan);
                DrawTextFit("Strict priority: matching goods enter first; no aging",
                    {picker.x + 6.0f, picker.y + 4.0f, picker.width - 12.0f, 16.0f}, 12, UiTheme::ParchmentDim);

                for (int index = 0; index < static_cast<int>(std::size(resourceTypes)); ++index)
                {
                    const int row = index / columns;
                    const int column = index % columns;
                    Rectangle cell{picker.x + column * cellWidth + 2.0f,
                                   picker.y + 22.0f + row * cellHeight,
                                   static_cast<float>(cellWidth - 4),
                                   static_cast<float>(cellHeight - 2)};
                    const bool hovered = CheckCollisionPointRec(GetMousePosition(), cell);
                    if (hovered)
                        DrawRectangleRounded(cell, 0.14f, 4, Color{48, 76, 98, 255});
                    GuiPanel::DrawResourceIcon(resourceTypes[index],
                        {cell.x + 4.0f, cell.y + 3.0f, 22.0f, 22.0f});
                    if (hovered && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                    {
                        if (scene != nullptr && scene->game != nullptr)
                            scene->SubmitLocalCommand(GameCommand::SetRoadPriority(
                                scene->game->GetLocalPlayerId(), building->positionId, resourceTypes[index]));
                        roadPriorityPickerOpen = false;
                    }
                }

                Rectangle clearRect{picker.x + 6.0f, picker.y + picker.height - 18.0f,
                                    picker.width - 12.0f, 14.0f};
                const bool clearHovered = CheckCollisionPointRec(GetMousePosition(), clearRect);
                DrawTextFit("Clear priority", clearRect, 12,
                            clearHovered ? UiTheme::Cyan : UiTheme::ParchmentDim);
                if (clearHovered && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                {
                    if (scene != nullptr && scene->game != nullptr)
                        scene->SubmitLocalCommand(GameCommand::SetRoadPriority(
                            scene->game->GetLocalPlayerId(), building->positionId, ResourceType::Null));
                    roadPriorityPickerOpen = false;
                }
                y += static_cast<int>(picker.height) + 6;
            }

            // Upgrade button — road keeps transporting while upgrading
            // (UpgradeComponent never touches constructionRemaining, see
            // GameWorld.Commands.cpp), so this button only disappears once
            // maxed out, never while mid-upgrade elsewhere on the road.
            const BuildingUpgradeView upgradeView = MakeBuildingUpgradeView(*building);
            if (upgradeView.CanStart())
            {
                const int targetLevel = upgradeView.targetLevel;
                const auto& levelDefinition = *upgradeView.target;
                Building* self = building;
                GameScene* panelScene = scene;
                Rectangle upgradeRect{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), 32.0f};
                UiButton upgradeButton;
                upgradeButton.pos = Vec2i{contentX, y};
                upgradeButton.size = Vec2i{contentW, 32};
                upgradeButton.ChangeText("Upgrade to level " + std::to_string(targetLevel));
                upgradeButton.func = [self, panelScene]()
                {
                    if (self == nullptr || panelScene == nullptr || panelScene->game == nullptr)
                        return;
                    panelScene->SubmitLocalCommand(GameCommand::UpgradeBuilding(
                        panelScene->game->GetLocalPlayerId(), self->positionId));
                };
                upgradeButton.Update(dt);

                // Keep the tooltip shared with every upgradeable building: the
                // button remains data-driven, while the renderer only consumes
                // the validated next-level view.
                if (CheckCollisionPointRec(GetMousePosition(), upgradeRect))
                {
                    std::vector<std::string> effects;
                    const auto& definition = GetBuildingDefinition(building->buildingType);
                    const auto* currentLevelDefinition =
                        FindUpgradeLevelDefinition(definition, upgrade->level);
                    const auto levelModifierValue = [](const BuildingUpgradeLevelDefinition* level,
                                                        BalanceStat stat, bool multiplier)
                    {
                        if (level == nullptr)
                            return multiplier ? 1.0 : 0.0;
                        for (const auto& modifier : level->modifiers)
                            if (modifier.stat == stat)
                                return multiplier ? modifier.multiplier : modifier.additive;
                        return multiplier ? 1.0 : 0.0;
                    };
                    for (const auto& modifier : levelDefinition.modifiers)
                    {
                        if (modifier.stat == BalanceStat::RoadCapacity)
                        {
                            const double currentAdd = levelModifierValue(
                                currentLevelDefinition, modifier.stat, false);
                            const double targetCapacity = road->GetModifiedMaxCapacity(*building) +
                                modifier.additive - currentAdd;
                            effects.push_back("Road capacity: " +
                                std::to_string(road->GetModifiedMaxCapacity(*building)) + " -> " +
                                std::to_string(static_cast<int>(std::round(targetCapacity))));
                        }
                        else if (modifier.stat == BalanceStat::RoadSpeed)
                        {
                            const double currentMultiplier = levelModifierValue(
                                currentLevelDefinition, modifier.stat, true);
                            const double targetSpeed = road->GetModifiedSpeedModifier(*building) *
                                modifier.multiplier / std::max(0.0001, currentMultiplier);
                            effects.push_back("Road speed: " +
                                FormatDecimal(road->GetModifiedSpeedModifier(*building) * 100.0, 0) +
                                "% -> " + FormatDecimal(targetSpeed * 100.0, 0) + "%");
                        }
                        else
                            effects.push_back(FormatUpgradeEffect(modifier));
                    }
                    DrawUpgradeTooltip(upgradeRect, targetLevel, levelDefinition, effects, building->owner);
                }
                y += 32 + 8;
            }

            y += 8;
            UiText::Draw("Resources on road", contentX, y, 20, Color{224, 204, 168, 255});
            y += 26;

            if (building->transportables.empty())
            {
                DrawTextFit("No active transports", Rectangle{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), 20.0f}, 15, UiTheme::ParchmentDim);
                drawDestroyButton();
                return;
            }

            int rowH = 42;
            for (auto* transportable : building->transportables)
            {
                if (y + rowH > bottom - destroyButton.size.y - margin)
                    break;

                auto* resource = dynamic_cast<Resource*>(transportable);
                ResourceType type = resource != nullptr ? resource->type : ResourceType::Null;
                Rectangle row{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), static_cast<float>(rowH - 6)};
                DrawRectangleRounded(row, 0.08f, 6, UiTheme::Inset);
                DrawRectangleRoundedLines(row, 0.08f, 6, 1.0f, UiTheme::Iron);

                Rectangle icon{row.x + 6.0f, row.y + 5.0f, 26.0f, 26.0f};
                if (resourceIconAtlas.IsLoaded())
                {
                    Rectangle src = resourceIconAtlas.GetRect(type);
                    DrawTexturePro(resourceIconAtlas.texture.Get(), src, icon, {0.0f, 0.0f}, 0.0f, WHITE);
                }
                else
                {
                    DrawRectangleRounded(icon, 0.16f, 6, ResourceColor(type));
                    DrawTextFit(ResourceShortName(type), {icon.x + 3.0f, icon.y + 7.0f, icon.width - 6.0f, 14.0f}, 12, WHITE);
                }

                DrawTextFit(ResourceDisplayName(type), Rectangle{row.x + 40.0f, row.y + 4.0f, row.width - 46.0f, 16.0f}, 13, UiTheme::Parchment);
                float progress = transportable->transportTime > 0.0
                    ? std::clamp(static_cast<float>(transportable->elapsedTime / transportable->transportTime), 0.0f, 1.0f)
                    : 0.0f;
                Rectangle bar{row.x + 40.0f, row.y + 23.0f, row.width - 48.0f, 8.0f};
                DrawRectangleRounded(bar, 0.2f, 4, UiTheme::Ink);
                Rectangle fill = bar;
                fill.width *= progress;
                DrawRectangleRounded(fill, 0.2f, 4, Color{140, 176, 96, 255});
                y += rowH;
            }
        }
        drawDestroyButton();
        return;
    }

    if (auto* population = building->GetComponent<PopulationComponent>())
    {
        UiText::Draw("Residential", contentX, y, 22, Color{224, 204, 168, 255});
        y += 32;

        double manpowerRate = building->owner != nullptr
            ? building->owner->ResolveStat(population->manpowerRate, building)
            : population->manpowerRate.GetBase();
        int populationCap = building->owner != nullptr
            ? building->owner->ResolveStat(population->populationCap, building)
            : population->populationCap.GetBase();
        auto upkeepPerMinute = [&](ResourceType type)
        {
            const double interval = population->GetEffectiveSupplyUpkeepInterval(*building, type);
            return std::isfinite(interval)
                ? population->GetSupplyUpkeep(type) * (60.0 / interval)
                : 0.0;
        };
        std::vector<std::string> stats{
            "Settlement: " + std::string(population->settlementLevel == 1 ? "Village" :
                                          population->settlementLevel == 2 ? "Town" : "City"),
            "Generates: Manpower",
            "Rate: " + std::to_string(static_cast<int>(manpowerRate * 60.0)) + " / min",
            "Population cap: " + std::to_string(populationCap),
            "Food upkeep: " + FormatDecimal(upkeepPerMinute(ResourceType::FOOD_PROVISIONS)) + " / min",
            "Food supply: " + std::to_string(static_cast<int>(std::round(population->GetFoodSupplyRatio() * 100.0))) + "%",
            "Worker output: " + std::to_string(static_cast<int>(std::round(population->GetWorkerProductivity() * 100.0))) + "%",
            "Lifetime: " + std::to_string(static_cast<int>(building->GetLifetime())) + "s"};
        if (population->settlementLevel >= 2)
        {
            stats.push_back("Household upkeep: " +
                FormatDecimal(upkeepPerMinute(ResourceType::HOUSEHOLD_GOODS)) + " / min");
            stats.push_back("Household supply: " +
                std::to_string(static_cast<int>(std::round(population->householdSupplyLevel * 100.0))) + "%");
        }
        if (population->settlementLevel >= 3)
        {
            stats.push_back("Urban upkeep: " +
                FormatDecimal(upkeepPerMinute(ResourceType::URBAN_GOODS)) + " / min");
            stats.push_back("Urban supply: " +
                std::to_string(static_cast<int>(std::round(population->urbanSupplyLevel * 100.0))) + "%");
        }
        if (auto* upgrade = building->GetComponent<UpgradeComponent>(); upgrade != nullptr && upgrade->isUpgrading)
            stats.push_back("Upgrading: " + std::to_string(static_cast<int>(std::ceil(upgrade->upgradeRemaining))) + "s left");

        int line = 18;
        for (const auto& stat : stats)
        {
            Color color = population->GetFoodSupplyRatio() < 1.0 && stat.find("Food supply") != std::string::npos ? Color{238, 184, 84, 255} : UiTheme::Parchment;
            DrawTextFit(stat, Rectangle{static_cast<float>(contentX), static_cast<float>(y), static_cast<float>(contentW), static_cast<float>(line)}, line - 3, color);
            y += line + 4;
        }
        const BuildingUpgradeView upgradeView = MakeBuildingUpgradeView(*building);
        if (upgradeView.CanStart())
        {
            const int targetLevel = upgradeView.targetLevel;
            const auto& levelDefinition = *upgradeView.target;
            UiButton settlementUpgradeButton;
            settlementUpgradeButton.pos = Vec2i{contentX, y};
            settlementUpgradeButton.size = Vec2i{contentW, 32};
            settlementUpgradeButton.ChangeText("Upgrade to level " + std::to_string(targetLevel));
            Building* self = building;
            GameScene* panelScene = scene;
            settlementUpgradeButton.func = [self, panelScene]()
            {
                if (self == nullptr || panelScene == nullptr || panelScene->game == nullptr)
                    return;
                panelScene->SubmitLocalCommand(GameCommand::UpgradeBuilding(
                    panelScene->game->GetLocalPlayerId(), self->positionId));
            };
            settlementUpgradeButton.Update(dt);

            Rectangle upgradeRect{static_cast<float>(contentX), static_cast<float>(y),
                                  static_cast<float>(contentW), 32.0f};
            if (CheckCollisionPointRec(GetMousePosition(), upgradeRect))
            {
                const int currentPopulationCap = populationCap;
                const double currentManpowerRate = manpowerRate;
                std::vector<std::string> effects;
                if (levelDefinition.populationCap.has_value())
                    effects.push_back("Population cap: " + std::to_string(currentPopulationCap) +
                                      " -> " + std::to_string(levelDefinition.populationCap.value()));
                if (levelDefinition.manpowerRate.has_value())
                    effects.push_back("Manpower growth: " + FormatDecimal(currentManpowerRate, 2) +
                                      " -> " + FormatDecimal(levelDefinition.manpowerRate.value(), 2));
                for (const auto& modifier : levelDefinition.modifiers)
                    effects.push_back(FormatUpgradeEffect(modifier));
                DrawUpgradeTooltip(upgradeRect, targetLevel, levelDefinition, effects, building->owner);
            }
        }
        drawDestroyButton();
        return;
    }

    auto* panelProduction = building->GetComponent<ProductionComponent>();
    auto* panelRecipes = building->GetComponent<RecipeComponent>();
    const int sectionGap = std::max(10, margin / 2);
    const int columnGap = std::max(32, margin);
    const int columnW = std::max(1, (contentW - columnGap) / 2);
    const int headerH = 24;
    const auto inputBuffers = building->GetInputBufferViews();
    const auto outputBuffers = building->GetOutputBufferViews();

    // Keep the requested 15% enlargement where the panel can afford it, but
    // reduce the slot size on narrow windows instead of letting three inputs
    // spill into the output column.
    constexpr float requestedIconSize = 114.0f; // 25% above the previous 91.2 px slot
    constexpr float productionIconGap = 3.0f;
    const int inputColumns = inputBuffers.empty()
        ? 1 : std::min(3, static_cast<int>(inputBuffers.size()));
    const int outputColumns = outputBuffers.empty()
        ? 1 : std::min(2, static_cast<int>(outputBuffers.size()));
    const float inputMaxSize = (static_cast<float>(columnW) -
                                productionIconGap * (inputColumns - 1) -
                                0.0f) / inputColumns;
    const float outputMaxSize = (static_cast<float>(columnW) -
                                 productionIconGap * (outputColumns - 1) -
                                 0.0f) / outputColumns;
    const float productionIconSize = std::min(
        requestedIconSize,
        std::max(42.0f, std::min(inputMaxSize, outputMaxSize)));
    const float inputLeadingPadding = std::max(
        0.0f, (static_cast<float>(columnW) - inputColumns * productionIconSize -
               productionIconGap * (inputColumns - 1)) * 0.5f);
    const float outputLeadingPadding = std::max(
        0.0f, (static_cast<float>(columnW) - outputColumns * productionIconSize -
               productionIconGap * (outputColumns - 1)) * 0.5f);
    const int inputRows = std::max(1, static_cast<int>((inputBuffers.size() + inputColumns - 1) / inputColumns));
    const int outputRows = std::max(1, static_cast<int>((outputBuffers.size() + outputColumns - 1) / outputColumns));
    const int iconRows = std::max(inputRows, outputRows);
    const int iconGridH = static_cast<int>(iconRows * productionIconSize +
                                           (iconRows - 1) * productionIconGap);

    UiText::Draw("Input", contentX, y, 22, Color{224, 204, 168, 255});
    UiText::Draw("Output", contentX + columnW + columnGap, y, 22, Color{224, 204, 168, 255});
    const float arrowX = static_cast<float>(contentX + columnW + columnGap / 2);
    DrawLineEx({arrowX - 7.0f, static_cast<float>(y + 11)},
               {arrowX + 7.0f, static_cast<float>(y + 11)}, 1.0f, UiTheme::Bronze);
    DrawTriangle({arrowX + 7.0f, static_cast<float>(y + 11)},
                 {arrowX + 2.0f, static_cast<float>(y + 7)},
                 {arrowX + 2.0f, static_cast<float>(y + 15)}, UiTheme::Bronze);
    y += headerH;

    Rectangle inputGrid{static_cast<float>(contentX), static_cast<float>(y),
                        static_cast<float>(columnW), static_cast<float>(iconGridH)};
    Rectangle outputGrid{static_cast<float>(contentX + columnW + columnGap),
                         static_cast<float>(y), static_cast<float>(columnW),
                         static_cast<float>(iconGridH)};
    DrawResourceIconGrid(inputBuffers, inputGrid, inputColumns, nullptr, nullptr, nullptr,
                         nullptr, nullptr, productionIconSize, productionIconGap,
                         inputLeadingPadding);
    DrawResourceIconGrid(outputBuffers, outputGrid, outputColumns, nullptr, nullptr, nullptr,
                         nullptr, nullptr, productionIconSize, productionIconGap,
                         outputLeadingPadding);
    y += iconGridH + sectionGap;

    const float productionProgress = std::clamp(building->GetProductionProgress(), 0.0f, 1.0f);
    const bool productionBlocked = building->IsProductionBlocked();
    const auto* panelLogistics = building->GetComponent<LogisticsComponent>();
    const bool roadNotConnected = panelLogistics != nullptr &&
                                   !panelLogistics->IsConnectedToRoadNetwork(*building);
    const int progressH = std::clamp(size.y / 20, 34, 42);
    Rectangle progressBounds{static_cast<float>(contentX), static_cast<float>(y),
                             static_cast<float>(contentW), static_cast<float>(progressH)};
    DrawRectangleRec(progressBounds, UiTheme::Ink);
    DrawRectangleLinesEx(progressBounds, 2.0f, UiTheme::Iron);
    Rectangle progressFill{progressBounds.x + 3.0f, progressBounds.y + 3.0f,
                           std::max(0.0f, (progressBounds.width - 6.0f) * productionProgress),
                           progressBounds.height - 6.0f};
    if (progressFill.width > 0.0f)
        DrawRectangleRec(progressFill, productionBlocked ? Color{156, 112, 70, 255}
                                                         : Color{140, 176, 96, 255});
    DrawLineEx({progressBounds.x + 4.0f, progressBounds.y + 3.0f},
               {progressBounds.x + progressBounds.width - 4.0f, progressBounds.y + 3.0f},
               1.0f, Fade(UiTheme::Bronze, 0.72f));
    const std::string progressLabel = "Production cycle - " +
        std::to_string(static_cast<int>(std::round(productionProgress * 100.0f))) + "%";
    const int progressFont = std::clamp(progressH / 2, 17, 21);
    const int progressTextW = UiText::Measure(progressLabel, progressFont);
    const float progressTextX = progressBounds.x + (progressBounds.width - progressTextW) * 0.5f;
    const float progressTextY = progressBounds.y + (progressBounds.height - progressFont) * 0.5f;
    const bool progressHovered = CheckCollisionPointRec(GetMousePosition(), progressBounds);
    if (progressHovered)
    {
        // Keep the requested black hover label readable over the dark half of
        // a partially filled bar with a restrained light keyline.
        UiText::Draw(progressLabel, progressTextX + 1.0f, progressTextY + 1.0f,
                     progressFont, Fade(WHITE, 0.72f));
    }
    UiText::Draw(progressLabel, progressTextX, progressTextY, progressFont,
                 progressHovered ? Color{12, 12, 12, 255} : UiTheme::Parchment);
    y += progressH + sectionGap;

    auto receivers = building->GetReceiverViews();
    auto suppliers = building->GetSupplierViews();
    const auto endpointIsLive = [&](const BuildingConnectionView& view)
    {
        return view.building != nullptr && scene != nullptr && scene->game != nullptr &&
               scene->game->GetTileMap().ContainsBuilding(view.building);
    };
    const auto connectionLess = [&](const BuildingConnectionView& left,
                                     const BuildingConnectionView& right)
    {
        const int leftId = endpointIsLive(left) ? left.building->id : std::numeric_limits<int>::max();
        const int rightId = endpointIsLive(right) ? right.building->id : std::numeric_limits<int>::max();
        if (leftId != rightId)
            return leftId < rightId;
        if (left.type != right.type)
            return static_cast<int>(left.type) < static_cast<int>(right.type);
        return left.alternative < right.alternative;
    };
    std::stable_sort(suppliers.begin(), suppliers.end(), connectionLess);
    std::stable_sort(receivers.begin(), receivers.end(), connectionLess);
    const int connectionLine = 23;
    const int connectionColumnW = std::max(1, (contentW - columnGap) / 2);
    const int connectionRightX = contentX + connectionColumnW + columnGap;
    UiText::Draw("Supply", contentX, y, 20, UiTheme::AmberBright);
    UiText::Draw("Delivery", connectionRightX, y, 20, UiTheme::AmberBright);
    int leftY = y + 23;
    int rightY = y + 23;
    const std::string arrow = " -> ";
    const Color missingColor{238, 184, 84, 255};
    const auto drawConnectionRow = [&](const BuildingConnectionView& view, bool supplier,
                                        int x, int& rowY)
    {
        const bool live = endpointIsLive(view);
        const std::string endpointName = live ? view.building->name : "Missing";
        const std::string resourceName = ResourceDisplayName(view.type);
        const Color color = live ? UiTheme::Parchment : missingColor;
        const float iconSize = 18.0f;
        const float iconY = static_cast<float>(rowY + 2);

        if (supplier)
        {
            const std::string prefix = endpointName + arrow;
            const float prefixWidth = std::min(
                static_cast<float>(std::max(1, connectionColumnW - 28)),
                static_cast<float>(MeasureText(prefix.c_str(), 15) + 2));
            DrawTextFit(prefix,
                        Rectangle{static_cast<float>(x), static_cast<float>(rowY),
                                  prefixWidth, 20.0f},
                        15, color);
            const float iconX = static_cast<float>(x) + prefixWidth + 3.0f;
            GuiPanel::DrawResourceIcon(view.type, {iconX, iconY, iconSize, iconSize});
            DrawTextFit(resourceName,
                        Rectangle{iconX + iconSize + 4.0f, static_cast<float>(rowY),
                                  std::max(1.0f, static_cast<float>(std::max(1, connectionColumnW)) -
                                      prefixWidth - iconSize - 7.0f),
                                  20.0f},
                        15, color);
        }
        else
        {
            GuiPanel::DrawResourceIcon(view.type,
                                       {static_cast<float>(x), iconY, iconSize, iconSize});
            std::string suffix = resourceName + arrow + endpointName;
            if (view.alternative)
                suffix += " [alt]";
            DrawTextFit(suffix,
                        Rectangle{static_cast<float>(x) + iconSize + 5.0f,
                                  static_cast<float>(rowY),
                                  static_cast<float>(std::max(1, connectionColumnW)) - iconSize - 5.0f,
                                  20.0f},
                        15, color);
        }
        rowY += connectionLine;
    };
    if (suppliers.empty())
    {
        DrawTextFit("No supplier", Rectangle{static_cast<float>(contentX), static_cast<float>(leftY),
                                     static_cast<float>(connectionColumnW), 20.0f},
                    15, missingColor);
        leftY += connectionLine;
    }
    for (const auto& supplier : suppliers)
        drawConnectionRow(supplier, true, contentX, leftY);
    if (receivers.empty() && !outputBuffers.empty())
    {
        DrawTextFit("No receiver", Rectangle{static_cast<float>(connectionRightX), static_cast<float>(rightY),
                                     static_cast<float>(connectionColumnW), 20.0f},
                    15, missingColor);
        rightY += connectionLine;
    }
    for (const auto& receiver : receivers)
        drawConnectionRow(receiver, false, connectionRightX, rightY);
    y = std::max(leftY, rightY) + sectionGap;
    DrawLineEx({static_cast<float>(contentX), static_cast<float>(y)},
               {static_cast<float>(contentX + contentW), static_cast<float>(y)},
               1.0f, Fade(UiTheme::Bronze, 0.72f));
    y += 10;

    float efficiency = building->GetEfficiency() * 100.0f;
    double foodProductivityRatio = building->owner != nullptr ? building->owner->GetFoodProductivity() : 1.0;
    int foodProductivity = static_cast<int>(std::round(foodProductivityRatio * 100.0));
    double cycleTime = 0.0;
    int workerOutput = static_cast<int>(building->GetWorkerRatio() * 100.0f);
    if (panelProduction != nullptr)
    {
        double workerEfficiency = static_cast<double>(building->GetWorkerRatio()) * foodProductivityRatio;
        cycleTime = workerEfficiency > 0.0
            ? panelProduction->GetModifiedCycleTime(*building) / workerEfficiency
            : std::numeric_limits<double>::infinity();
    }

    using StatRow = std::pair<std::string, std::string>;
    auto drawStatGroup = [&](const std::string& title, const std::vector<StatRow>& rows,
                             int x, int top, int width, Color valueColor,
                             bool compactColumns = false)
    {
        UiText::Draw(title, static_cast<float>(x), static_cast<float>(top), 20, UiTheme::AmberBright);
        int rowY = top + 25;
        const int labelWidth = compactColumns
            ? std::clamp(static_cast<int>(width * 0.40f), 150, 230)
            : static_cast<int>(width * 0.62f);
        const int valueStart = x + labelWidth + (compactColumns ? 16 : 0);
        for (const auto& [label, value] : rows)
        {
            const bool highlight = tutorialHighlight &&
                (label == "Workers" || label == "Worker output" || label == "Food productivity");
            if (highlight)
            {
                const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 4.5f);
                DrawRectangleRounded(Rectangle{static_cast<float>(x), static_cast<float>(rowY),
                                               static_cast<float>(width), 22.0f},
                                     0.12f, 6,
                                     Color{184, 132, 48, static_cast<unsigned char>(42.0f + pulse * 44.0f)});
            }
            UiText::DrawFit(label, Rectangle{static_cast<float>(x), static_cast<float>(rowY),
                                             static_cast<float>(labelWidth), 22.0f},
                            17, highlight ? UiTheme::Gold : UiTheme::ParchmentDim);
            const int valueWidth = UiText::Measure(value, 18);
            const int valueX = compactColumns
                ? valueStart
                : x + width - valueWidth;
            UiText::Draw(value, static_cast<float>(valueX),
                         static_cast<float>(rowY), 18, highlight ? UiTheme::Gold : valueColor);
            rowY += 24;
        }
        return rowY;
    };

    const int statGap = std::max(12, margin / 2);
    const int statColumnW = std::max(1, (contentW - statGap) / 2);
    const std::vector<StatRow> productionStats{
        {"Cycle time", FormatSeconds(cycleTime)},
        {"Output", std::to_string(building->GetTotalProduced())},
        {"Efficiency", std::to_string(static_cast<int>(efficiency)) + "%"}};
    const std::vector<StatRow> workforceStats{
        {"Workers", std::to_string(building->GetAssignedWorkers()) + "/" + std::to_string(building->GetWorkerCapacity())},
        {"Worker output", std::to_string(workerOutput) + "%"},
        {"Food productivity", std::to_string(foodProductivity) + "%"}};
    const int productionStatsBottom = drawStatGroup("Production", productionStats,
                                                    contentX, y, statColumnW, UiTheme::Parchment);
    const int workforceStatsBottom = drawStatGroup("Workforce", workforceStats,
                                                   contentX + statColumnW + statGap, y, statColumnW,
                                                   UiTheme::Parchment);
    y = std::max(productionStatsBottom, workforceStatsBottom) + 7;
    DrawLineEx({static_cast<float>(contentX), static_cast<float>(y)},
               {static_cast<float>(contentX + contentW), static_cast<float>(y)},
               1.0f, Fade(UiTheme::Bronze, 0.72f));
    y += sectionGap;

    std::vector<StatRow> statusStats{
        {"Status", roadNotConnected ? "Road not connected" :
                   productionBlocked ? "Blocked" : "Active"},
        {"Lifetime", std::to_string(static_cast<int>(building->GetLifetime())) + "s"}};
    const int localRichness = GetLocalTerrainRichness(building);
    if (localRichness >= 0)
        statusStats.push_back({"Local richness", std::to_string(localRichness)});
    const int statusBottom = drawStatGroup(
        "Status", statusStats, contentX, y, contentW,
        roadNotConnected || productionBlocked
            ? Color{232, 104, 92, 255}
            : Color{140, 196, 122, 255}, true);
    y = statusBottom + sectionGap;

    DrawLineEx({static_cast<float>(contentX), static_cast<float>(y)},
               {static_cast<float>(contentX + contentW), static_cast<float>(y)},
               1.0f, Fade(UiTheme::Bronze, 0.72f));
    y += sectionGap;

    const int actionGap = 10;
    const int mainActionH = std::max(44, std::min(56, size.y / 15));
    const bool hasRecipeAction = panelRecipes != nullptr && panelRecipes->HasSelectableRecipes();
    const bool hasLockAction = building->CanBlockProduction();
    const bool hasDestroyAction = building->CanBeManuallyDestroyed();
    const int actionCount = static_cast<int>(hasRecipeAction) +
                            static_cast<int>(hasLockAction) +
                            static_cast<int>(hasDestroyAction);
    if (actionCount > 0)
    {
        const int actionWidth = std::max(1, (contentW - actionGap * (actionCount - 1)) / actionCount);
        int actionIndex = 0;
        const int actionY = bottom - mainActionH;
        auto placeAction = [&](UiButton& button)
        {
            button.pos = Vec2i{contentX + actionIndex * (actionWidth + actionGap), actionY};
            button.size = Vec2i{actionWidth, mainActionH};
            ++actionIndex;
        };

        if (hasRecipeAction)
        {
            placeAction(recipeButton);
            recipeButton.ChangeText("Recipe: " + panelRecipes->GetActiveRecipeName());
            recipeButton.Update(dt);
        }
        if (hasLockAction)
        {
            placeAction(lockButton);
            lockButton.ChangeText(building->IsProductionBlocked() ? "Unlock production" : "Block production");
            lockButton.Update(dt);
        }
        if (hasDestroyAction)
        {
            placeAction(destroyButton);
            destroyButton.ChangeText("Destroy building");
            destroyButton.Update(dt);
        }
    }
    DrawPendingTooltip();
}

// Initializes GuiPanel::GuiPanel.
GuiPanel::GuiPanel()
{
    lockButton.func = [this]()
    {
        if (building != nullptr && scene != nullptr && scene->game != nullptr &&
            building->CanBlockProduction())
        {
            scene->SubmitLocalCommand(GameCommand::SetProductionBlocked(
                scene->game->GetLocalPlayerId(), building->positionId,
                !building->IsProductionBlocked()));
        }
    };
    recipeButton.func = [this]()
    {
        auto* production = building != nullptr ? building->GetComponent<ProductionComponent>() : nullptr;
        auto* logistics = building != nullptr ? building->GetComponent<LogisticsComponent>() : nullptr;
        auto* workers = building != nullptr ? building->GetComponent<WorkerComponent>() : nullptr;
        auto* recipes = building != nullptr ? building->GetComponent<RecipeComponent>() : nullptr;
        if (building != nullptr && production != nullptr && logistics != nullptr && workers != nullptr && recipes != nullptr)
            recipes->CycleRecipe(*building, *production, *logistics, *workers);
    };
    towerTargetButton.func = [this]()
    {
        auto* tower = building != nullptr ? building->GetComponent<TowerCombatComponent>() : nullptr;
        if (building == nullptr || tower == nullptr || scene == nullptr || scene->game == nullptr)
            return;
        TowerTargetMode next = tower->targetMode == TowerTargetMode::NearestToHq
            ? TowerTargetMode::StrongestUnit
            : TowerTargetMode::NearestToHq;
        scene->SubmitLocalCommand(GameCommand::SetTowerTargetMode(
            scene->game->GetLocalPlayerId(), building->positionId, static_cast<int>(next)));
    };
    destroyButton.func = [this]()
    {
        if (building != nullptr)
            destroyRequested = true;
    };
}

void BuildingInfoPanel::UpdateSize(Vec2i windowSize)
{
    const Vec2f savedAnchor = sizeAnchor;
    const bool compactProduction = building != nullptr &&
        building->GetComponent<ProductionComponent>() != nullptr &&
        building->GetComponent<RecruitmentComponent>() == nullptr;
    if (compactProduction)
        sizeAnchor.y = 0.64f;

    GuiPanel::UpdateSize(windowSize);
    sizeAnchor = savedAnchor;
}

// Advances UpdateSize for one frame or simulation tick.
void GuiPanel::UpdateSize(Vec2i windowSize)
{
    UiWidget::UpdateSize(windowSize);

    int margin = std::max(10, size.x / 24);
    int progressH = std::max(28, size.y / 14);
    int buttonH = std::max(32, size.y / 12);

    progressBar.pos = Vec2i{pos.x + margin, pos.y + static_cast<int>(size.y * 0.58f)};
    progressBar.size = Vec2i{size.x - margin * 2, progressH};
    progressBar.ChangeText("Cycle");

    lockButton.pos = Vec2i{pos.x + margin, pos.y + size.y - buttonH - margin};
    lockButton.size = Vec2i{size.x - margin * 2, buttonH};
    recipeButton.pos = Vec2i{pos.x + margin, pos.y + size.y - buttonH * 2 - margin * 2};
    recipeButton.size = Vec2i{size.x - margin * 2, buttonH};
    towerTargetButton.pos = Vec2i{pos.x + margin, pos.y + size.y - buttonH * 2 - margin * 2};
    towerTargetButton.size = Vec2i{size.x - margin * 2, buttonH};
    destroyButton.pos = Vec2i{pos.x + margin, pos.y + size.y - buttonH - margin};
    destroyButton.size = Vec2i{size.x - margin * 2, buttonH};
}

// Updates the requested state value.
void GuiPanel::SetBuilding(Building* ptr)
{
    tutorialHighlight = false;
    building = ptr;
    destroyRequested = false;
    contentScrollOffset = 0.0f;
    maxContentScrollOffset = 0.0f;
    contentScrollbarDragging = false;
    contentScrollbarDragOffset = 0.0f;
    roadPriorityPickerOpen = false;
    ChangeText(building != nullptr ? building->name : "Gui Panel");
    UpdateSize({GetScreenWidth(), GetScreenHeight()});
}

// Scrolls overflowing generic panel content.
void GuiPanel::ScrollContent(float wheel)
{
    if (wheel == 0.0f || maxContentScrollOffset <= 0.0f)
        return;

    contentScrollOffset = std::clamp(contentScrollOffset - wheel * 42.0f, 0.0f, maxContentScrollOffset);
}

// Restricts drawing to a content rectangle so overflowing text/widgets don't
// bleed past the panel edge while scrolled. Pair with EndContentClip().
void GuiPanel::BeginContentClip(Rectangle contentArea)
{
    BeginScissorMode(static_cast<int>(contentArea.x), static_cast<int>(contentArea.y),
                      static_cast<int>(contentArea.width), static_cast<int>(contentArea.height));
}

void GuiPanel::EndContentClip()
{
    EndScissorMode();
}

// Loads the requested data into runtime state.
void GuiPanel::LoadResourceAtlas(const std::string& path, Vec2i iconSize)
{
    resourceIconAtlas.Load(path, iconSize);
}

void GuiPanel::UnloadResourceAtlas()
{
    resourceIconAtlas.Unload();
}

// Loads the shared UI font and applies it to raygui widgets. The loading itself
// lives in ui/UiText.cpp (shared with tools/); only the raygui hookup stays here,
// because RAYGUI_IMPLEMENTATION is in this translation unit.
void GuiPanel::LoadUiFont(const std::string& path)
{
    UiTextFont::Load(path);
    if (UiTextFont::IsLoaded())
    {
        GuiSetFont(UiTextFont::Get());
        GuiSetStyle(DEFAULT, TEXT_SIZE, 20);
    }
}

void GuiPanel::LoadUiPlainFont(const std::string& path, int baseSize)
{
    UiTextFont::LoadPlain(path, baseSize);
    if (UiTextFont::IsLoaded())
        GuiSetFont(UiTextFont::GetPlain());
    UiText::SetRole(UiFontRole::Plain);
}

// Draws one resource icon, using the atlas when available.
void GuiPanel::DrawResourceIcon(ResourceType type, Rectangle dest)
{
    if (resourceIconAtlas.IsLoaded())
    {
        Rectangle src = resourceIconAtlas.GetRect(type);
        DrawTexturePro(resourceIconAtlas.texture.Get(), src, dest, {0.0f, 0.0f}, 0.0f, WHITE);
        return;
    }

    DrawRectangleRounded(dest, 0.16f, 6, ResourceColor(type));
    DrawTextFit(ResourceShortName(type), {dest.x + 3.0f, dest.y + dest.height * 0.32f, dest.width - 6.0f, 14.0f}, 12, WHITE);
}

// Draws categorized research trees for the selected university owner.
void ResearchPanel::Update(double dt)
{
    if (building == nullptr)
        return;

    Rectangle bounds = WidgetBounds(*this);
    int margin = std::max(14, size.x / 54);
    int titleBar = std::max(42, size.y / 14);
    Vector2 mouse = GetMousePosition();

    if (!UiControlIcons::DrawPixelHudFrame(bounds))
    {
        DrawRectangleRounded(bounds, 0.018f, 8, UiTheme::Panel);
        DrawRectangleRoundedLines(bounds, 0.018f, 8, 1.0f, UiTheme::Iron);
    }

    Rectangle titleBounds{bounds.x, bounds.y, bounds.width, static_cast<float>(titleBar)};
    const float frameInset = UiControlIcons::PixelHudFrameInset(bounds);
    Rectangle titleVisual{titleBounds.x + frameInset + 2.0f, titleBounds.y + 4.0f,
                          std::max(0.0f, titleBounds.width - (frameInset + 2.0f) * 2.0f),
                          std::max(0.0f, titleBounds.height - 8.0f)};

    Rectangle closeBounds = UiControlIcons::PixelHudCloseButtonRect(bounds);
    const float closeWidth = closeBounds.width;
    const float closeEndGap = titleVisual.x + titleVisual.width -
                              (closeBounds.x + closeBounds.width);
    bool closeHovered = CheckCollisionPointRec(GetMousePosition(), closeBounds);
    if (!UiControlIcons::DrawPanelCloseButton(closeBounds, closeHovered))
    {
        DrawRectangleRounded(closeBounds, 0.16f, 6, closeHovered ? Color{55, 94, 128, 245} : Color{31, 46, 66, 245});
        DrawRectangleRoundedLines(closeBounds, 0.16f, 6, 1.0f, closeHovered ? UiTheme::Cyan : UiTheme::Iron);
        int xFont = std::max(14, static_cast<int>(closeBounds.height) / 2);
        int xWidth = UiText::Measure("X", xFont);
        UiText::Draw("X", closeBounds.x + (closeBounds.width - xWidth) * 0.5f,
                     closeBounds.y + (closeBounds.height - xFont) * 0.5f,
                     xFont, UiTheme::Parchment);
    }

    if (closeHovered && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        dragging = false;
        SetBuilding(nullptr);
        return;
    }

    bool titleHovered = CheckCollisionPointRec(mouse, titleBounds) && !closeHovered;
    if (titleHovered && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        dragging = true;
        dragOffset = Vec2i{
            static_cast<int>(mouse.x) - pos.x,
            static_cast<int>(mouse.y) - pos.y};
    }
    if (dragging && InputManager::IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        pos.x = std::clamp(static_cast<int>(mouse.x) - dragOffset.x, 0, std::max(0, GetScreenWidth() - size.x));
        pos.y = std::clamp(static_cast<int>(mouse.y) - dragOffset.y, 0, std::max(0, GetScreenHeight() - size.y));
        bounds = Rectangle{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
        titleBounds = Rectangle{bounds.x, bounds.y, bounds.width, static_cast<float>(titleBar)};
        titleVisual = Rectangle{titleBounds.x + frameInset + 2.0f, titleBounds.y + 4.0f,
                                std::max(0.0f, titleBounds.width - (frameInset + 2.0f) * 2.0f),
                                std::max(0.0f, titleBounds.height - 8.0f)};
        closeBounds = UiControlIcons::PixelHudCloseButtonRect(bounds);
    }
    if (dragging && InputManager::IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        dragging = false;

    const std::string title = "Research";
    UiText::DrawTitleBar(titleVisual, title,
                         closeWidth + closeEndGap + 10.0f);

    Rectangle treeBounds{
        bounds.x + margin,
        bounds.y + titleBar + margin,
        bounds.width - margin * 2.0f,
        bounds.height - titleBar - margin * 2.0f};
    if (CheckCollisionPointRec(mouse, treeBounds) && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
    {
        treePanning = true;
        lastTreePanMouse = {mouse.x, mouse.y};
    }
    if (treePanning && InputManager::IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
    {
        treePanOffset.x += mouse.x - lastTreePanMouse.x;
        treePanOffset.y += mouse.y - lastTreePanMouse.y;
        lastTreePanMouse = {mouse.x, mouse.y};
    }
    if (treePanning && InputManager::IsMouseButtonReleased(MOUSE_BUTTON_RIGHT))
        treePanning = false;

    DrawResearchTree(building->owner,
                     building,
                     treeBounds,
                     treePanOffset,
                     treeZoom,
                     selectedTagFilter,
                     researchRequested);
}

// Uses a larger modal-like layout than standard side panels.
void ResearchPanel::UpdateSize(Vec2i windowSize)
{
    pos = Vec2i{static_cast<int>(windowSize.x * 0.03f), static_cast<int>(windowSize.y * 0.04f)};
    size = Vec2i{static_cast<int>(windowSize.x * 0.94f), static_cast<int>(windowSize.y * 0.90f)};
}

void ResearchPanel::AdjustTreeZoom(Vec2i point, float wheel)
{
    if (wheel == 0.0f || building == nullptr)
        return;

    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    int margin = std::max(12, size.x / 36);
    int titleBar = std::max(38, size.y / 12);
    Rectangle treeBounds{
        bounds.x + margin,
        bounds.y + titleBar + margin,
        bounds.width - margin * 2.0f,
        bounds.height - titleBar - margin * 2.0f};
    Rectangle treeArea{treeBounds.x, treeBounds.y + 60.0f, treeBounds.width, treeBounds.height - 60.0f};
    Vector2 mouse{static_cast<float>(point.x), static_cast<float>(point.y)};
    if (!CheckCollisionPointRec(mouse, treeArea))
        return;

    float oldZoom = treeZoom;
    float newZoom = std::clamp(treeZoom + wheel * 0.08f, 0.42f, 1.15f);
    if (std::abs(newZoom - oldZoom) < 0.001f)
        return;

    float localX = mouse.x - treeArea.x;
    float localY = mouse.y - treeArea.y;
    treePanOffset.x = localX - (localX - treePanOffset.x) * (newZoom / oldZoom);
    treePanOffset.y = localY - (localY - treePanOffset.y) * (newZoom / oldZoom);
    treeZoom = newZoom;
}
