// Research tree UI shared by political focuses and technologies: the merged
// ResearchTreePanelWidget plus the Focus/Tech interaction systems.
//
// Both trees render through one layout + draw pass; the differences (data
// source, node click command) branch on ResearchTreeKind.

#include "GuiInternal.h"

#include "scenes/Scenes.h"
#include "ui/ControlIcons.h"
#include "economy/BalanceStatDisplay.h"
#include "economy/BuildingConfig.h"
#include "economy/Player.h"
#include "research/ResearchCatalog.h"
#include "research/Technology.h"
#include "warfare/UnitDefinition.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace
{
    constexpr float NodeDetailZoomThreshold = 0.70f;

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
            float width = std::min(112.0f, std::max(54.0f, static_cast<float>(UiText::Measure(label, 14) + 22)));
            Rectangle rect{x, bounds.y, width, bounds.height};
            bool selected = selectedTag == value;
            bool hover = CheckCollisionPointRec(mouse, rect);
            DrawRectangleRounded(rect, 0.20f, 6, selected ? UiTheme::SelectedFill : hover ? UiTheme::SurfaceHover : UiTheme::Inset);
            DrawRectangleRoundedLines(rect, 0.20f, 6, 1.0f, selected ? UiTheme::SageBright : UiTheme::Iron);
            UiText::DrawFit(label, Rectangle{rect.x + 8.0f, rect.y + 4.0f, rect.width - 16.0f, rect.height - 8.0f}, 14, selected ? UiTheme::Parchment : UiTheme::ParchmentDim);
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

    std::vector<std::string> WrapUiText(const std::string& text, int fontSize, float maxWidth)
    {
        std::vector<std::string> wrapped;
        std::istringstream words(text);
        std::string word;
        std::string line;
        while (words >> word)
        {
            std::string candidate = line.empty() ? word : line + " " + word;
            if (UiText::Measure(candidate, fontSize) <= maxWidth || line.empty())
            {
                line = candidate;
                continue;
            }
            wrapped.push_back(line);
            line = word;
        }
        if (!line.empty())
            wrapped.push_back(line);
        if (wrapped.empty())
            wrapped.push_back("");
        return wrapped;
    }

    void DrawUiTextWrappedCentered(const std::string& text, Rectangle bounds, int fontSize, Color color, int maxLines = 2)
    {
        std::vector<std::string> lines = WrapUiText(text, fontSize, bounds.width);
        if (static_cast<int>(lines.size()) > maxLines)
        {
            lines.resize(maxLines);
            while (!lines.back().empty() && UiText::Measure(lines.back() + "...", fontSize) > bounds.width)
                lines.back().pop_back();
            lines.back() += "...";
        }

        float lineH = static_cast<float>(fontSize) + 2.0f;
        float totalH = lineH * static_cast<float>(lines.size());
        float y = bounds.y + (bounds.height - totalH) * 0.5f;
        for (const auto& line : lines)
        {
            int measured = UiText::Measure(line, fontSize);
            UiText::Draw(line, bounds.x + (bounds.width - measured) * 0.5f, y, fontSize, color);
            y += lineH;
        }
    }

    // Finds the first idle (not researching) University owned by the local player.
    Building* FindIdleUniversity(GameScene* scene)
    {
        Player* player = GuiLocalPlayer(scene);
        if (player == nullptr)
            return nullptr;
        for (auto* building : player->GetTrackedBuildingsWithComponent<ResearchComponent>())
        {
            if (building == nullptr || building->owner != player ||
                building->buildingType != BuildingType::University ||
                building->IsUnderConstruction())
                continue;
            const auto* research = building->GetComponent<ResearchComponent>();
            if (research != nullptr && research->technologyId.empty())
                return building;
        }
        return nullptr;
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



    std::string FormatDuration(double seconds)
    {
        int total = std::max(0, static_cast<int>(std::round(seconds)));
        int minutes = total / 60;
        int remainingSeconds = total % 60;
        std::ostringstream stream;
        stream << minutes << ":";
        if (remainingSeconds < 10)
            stream << "0";
        stream << remainingSeconds;
        return stream.str();
    }

    std::string FormatModifierForFocusTooltip(const BalanceModifier& modifier)
    {
        std::ostringstream stream;
        bool lowerIsBetter = LowerValueIsBetter(modifier.stat);
        bool showAsRate = lowerIsBetter && std::abs(modifier.multiplier - 1.0) > 0.001 &&
                          (modifier.stat == BalanceStat::BuildTime ||
                           modifier.stat == BalanceStat::ProductionCycleTime ||
                           modifier.stat == BalanceStat::TransportTime ||
                           modifier.stat == BalanceStat::TransportDispatchDelay);
        const bool isUniversityResearch = modifier.stat == BalanceStat::ProductionCycleTime &&
                                          modifier.buildingType == BuildingType::University;
        stream << (isUniversityResearch ? "Research speed" :
                   (showAsRate ? ImprovedRateLabel(modifier.stat) : BalanceStatLabel(modifier.stat)));
        if (modifier.buildingType.has_value())
            stream << " for {building}" << BalanceBuildingLabel(modifier.buildingType.value()) << "{/building}";
        if (modifier.resourceType.has_value())
        {
            stream << (modifier.stat == BalanceStat::VillageSupplyConsumption
                ? " for {resource}" : " producing {resource}")
                << ResourceDisplayName(modifier.resourceType.value()) << "{/resource}";
        }
        // Surface modifier scope so filtered bonuses do not look global.
        if (modifier.unitDefId.has_value())
        {
            const UnitDefinition* unitDef = FindUnitDefinition(modifier.unitDefId.value());
            stream << " for " << (unitDef != nullptr ? unitDef->displayName : modifier.unitDefId.value());
        }
        if (modifier.resourceCategory.has_value())
            stream << " ({category}" << ResourceCategoryLabel(modifier.resourceCategory.value()) << "{/category})";
        stream << ": ";

        bool hasValue = false;
        if (std::abs(modifier.additive) > 0.001)
        {
            stream << (modifier.additive > 0.0 ? "+" : "") << modifier.additive;
            hasValue = true;
        }
        if (std::abs(modifier.multiplier - 1.0) > 0.001)
        {
            double percent = showAsRate
                ? (1.0 / modifier.multiplier - 1.0) * 100.0
                : (modifier.multiplier - 1.0) * 100.0;
            if (hasValue)
                stream << ", ";
            stream << (percent > 0.0 ? "+" : "") << static_cast<int>(std::round(percent)) << "%";
            hasValue = true;
        }
        if (!hasValue)
            stream << "No numeric modifier";
        return IsPositiveModifier(modifier) ? TooltipBonusLine(stream.str()) : TooltipPenaltyLine(stream.str());
    }

    std::string FormatResearchCosts(const std::vector<ResourceAmountDefinition>& costs)
    {
        std::ostringstream stream;
        stream << "Research cost: ";
        for (size_t index = 0; index < costs.size(); index++)
        {
            if (index > 0)
                stream << "  |  ";
            stream << "{resource}" << ResourceDisplayName(costs[index].type) << "{/resource} x" << costs[index].amount;
        }
        return stream.str();
    }

    bool RequiresTechnology(const std::vector<std::string>& requirements, const std::string& technologyId)
    {
        return std::find(requirements.begin(), requirements.end(), technologyId) != requirements.end();
    }

    std::string JoinTooltipNames(const std::vector<std::string>& names, const char* markup)
    {
        std::ostringstream stream;
        for (size_t index = 0; index < names.size(); index++)
        {
            if (index > 0)
                stream << ", ";
            stream << markup << names[index] << "{/" << (std::string(markup) == "{building}" ? "building" : "resource") << "}";
        }
        return stream.str();
    }

    std::vector<std::string> CollectTechnologyUnlockLines(const ResearchNodeView& technology)
    {
        std::vector<std::string> products;
        std::vector<std::string> units;
        for (const auto& building : GetBuildingDefinitions())
        {
            for (const auto& recipe : building.recipes)
            {
                if (!RequiresTechnology(recipe.requiredTechnologies, technology.id))
                    continue;
                // The recipe name is player-facing and already describes the
                // selected product better than its internal resource id.
                products.push_back(recipe.name);
            }
        }
        for (const auto& [id, unit] : GetUnitCatalog())
        {
            if (unit.requiredTechnology == technology.id)
                units.push_back(unit.displayName);
        }

        std::vector<std::string> lines;
        for (const auto& building : technology.unlockedBuildings)
            lines.push_back("Unlocks {building}" + building + "{/building}");
        if (!products.empty())
            lines.push_back("Unlocks product: " + JoinTooltipNames(products, "{resource}"));
        if (!units.empty())
            lines.push_back("Unlocks unit: " + JoinTooltipNames(units, "{resource}"));
        return lines;
    }

    // Display order of tree lanes; unknown lanes go last, alphabetically.
    int LaneRank(const std::string& lane)
    {
        if (lane == "Biology") return 0;
        if (lane == "Mathematics") return 1;
        if (lane == "Humanities") return 2;
        if (lane == "PRODUCTION" || lane == "ECONOMY") return 0;
        if (lane == "MILITARY" || lane == "WARFARE") return 1;
        if (lane == "SOCIAL" || lane == "LOGISTICS") return 2;
        if (lane == "POLITICS" || lane == "GOVERNANCE") return 3;
        return 10;
    }
}

// ─── ResearchTreePanelWidget ─────────────────────────────────────────────────

Rectangle ResearchTreePanelWidget::GetTreeArea(Rectangle bounds) const
{
    float top = bounds.y + 104.0f;
    float bottom = bounds.y + bounds.height - 20.0f;
    return Rectangle{bounds.x + 24.0f, top, bounds.width - 48.0f, bottom - top};
}

void ResearchTreePanelWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr)
        return;

    Player* player = GuiLocalPlayer(scene);
    if (player == nullptr)
        return;

    const bool isFocus = kind == ResearchTreeKind::Focus;
    const char* panelTitle = isFocus ? "Decisions" : "Technology Research";

    Vector2 mouse = GetMousePosition();
    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    if (!UiControlIcons::DrawPixelHudFrame(bounds))
    {
        DrawRectangleRounded(bounds, 0.025f, 8, UiTheme::Panel);
        DrawRectangleRoundedLines(bounds, 0.025f, 8, 1.0f, UiTheme::Iron);
    }
    Rectangle title{bounds.x, bounds.y, bounds.width, 52.0f};
    const float frameInset = UiControlIcons::PixelHudFrameInset(bounds);
    Rectangle titleVisual{title.x + frameInset + 2.0f, title.y + 4.0f,
                          std::max(0.0f, title.width - (frameInset + 2.0f) * 2.0f),
                          title.height - 8.0f};
    DrawCloseButton(bounds);

    bool debugMode = scene->game->GetTileMap().params.debugMode;
    if (debugMode)
    {
        Rectangle reloadBtn{bounds.x + bounds.width - 44.0f - 8.0f - 110.0f, bounds.y + 11.0f, 110.0f, 30.0f};
        bool reloadHov = CheckCollisionPointRec(mouse, reloadBtn);
        DrawRectangleRounded(reloadBtn, 0.18f, 8, reloadHov ? Color{80, 100, 60, 245} : Color{46, 60, 38, 230});
        DrawRectangleRoundedLines(reloadBtn, 0.18f, 8, 1.0f, reloadHov ? Color{160, 220, 100, 255} : Color{100, 148, 72, 230});
        UiText::DrawWithControlIcons(
            UiText::WrapWithControlIcons("[D] Reload", 17, reloadBtn.width - 16.0f, 22.0f).front(),
            reloadBtn.x + 12.0f, reloadBtn.y + 4.0f, 17, UiTheme::Parchment, 22.0f);
        if (reloadHov && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            if (isFocus)
                ReloadFocusDefinitions();
            else
                ReloadTechnologyDefinitions();
            player->ResetResearchState();
        }
        UiText::DrawTitleBar(titleVisual, std::string(panelTitle) + " [DEBUG]", PanelTitleCloseReserve(bounds) + reloadBtn.width + 8.0f);
    }
    else
    {
        UiText::DrawTitleBar(titleVisual, panelTitle, PanelTitleCloseReserve(bounds));
    }

    auto nodes = isFocus ? ResearchCatalog::BuildFocusView(*player) : ResearchCatalog::BuildView(*player);
    std::map<std::string, ResearchNodeView*> byId;
    for (auto& node : nodes)
        byId[node.id] = &node;

    auto visibleTags = CollectVisibleTags(nodes);
    Rectangle tagBar{bounds.x + 24.0f, bounds.y + 62.0f, bounds.width - 48.0f, 30.0f};
    DrawTagFilterBar(tagBar, visibleTags, selectedTagFilter);

    Rectangle treeArea = GetTreeArea(bounds);

    if (CheckCollisionPointRec(mouse, treeArea) && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
    {
        panning = true;
        lastPanMouse = {mouse.x, mouse.y};
    }
    if (panning && InputManager::IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
    {
        panOffset.x += mouse.x - lastPanMouse.x;
        panOffset.y += mouse.y - lastPanMouse.y;
        lastPanMouse = {mouse.x, mouse.y};
    }
    if (panning && InputManager::IsMouseButtonReleased(MOUSE_BUTTON_RIGHT))
        panning = false;

    // Layout: lanes -> depth rows -> per-row horizontal placement
    float nodeW = 150.0f * zoom;
    float nodeH = 118.0f * zoom;
    float colGap = 118.0f * zoom;
    float laneGap = 220.0f * zoom;
    float rowGap = 118.0f * zoom;
    float laneHeaderH = 30.0f * zoom;
    std::map<std::string, Rectangle> nodeRects;
    std::map<std::string, int> depthById;
    std::map<std::string, std::map<int, std::vector<ResearchNodeView*>>> nodesByLaneDepth;
    std::vector<std::pair<std::string, Rectangle>> laneHeaders;
    std::vector<std::string> lanes;

    auto preferredDepth = [](const ResearchNodeView& node)
    {
        return node.layoutOrder >= 1000 ? node.layoutOrder / 1000 - 1 : 0;
    };

    std::function<int(const ResearchNodeView&)> depthOf = [&](const ResearchNodeView& node)
    {
        auto cached = depthById.find(node.id);
        if (cached != depthById.end())
            return cached->second;
        int depth = preferredDepth(node);
        for (const auto& prerequisite : node.prerequisites)
        {
            auto it = byId.find(prerequisite);
            if (it != byId.end())
                depth = std::max(depth, depthOf(*it->second) + 1);
        }
        depthById[node.id] = depth;
        return depth;
    };

    for (auto& node : nodes)
    {
        int depth = depthOf(node);
        std::string lane = node.layoutLane.empty() ? node.category : node.layoutLane;
        nodesByLaneDepth[lane][depth].push_back(&node);
        if (std::find(lanes.begin(), lanes.end(), lane) == lanes.end())
            lanes.push_back(lane);
    }

    std::sort(lanes.begin(), lanes.end(), [](const std::string& a, const std::string& b)
    {
        int rankA = LaneRank(a);
        int rankB = LaneRank(b);
        if (rankA != rankB)
            return rankA < rankB;
        return a < b;
    });

    float laneX = treeArea.x + 28.0f + panOffset.x;
    for (const auto& lane : lanes)
    {
        auto& rows = nodesByLaneDepth[lane];
        size_t maxColumns = 1;
        for (const auto& [depth, rowNodes] : rows)
            maxColumns = std::max(maxColumns, rowNodes.size());

        float laneWidth = std::max(680.0f * zoom, maxColumns * nodeW + (maxColumns - 1) * colGap + 360.0f * zoom);
        Rectangle laneHeader{laneX, treeArea.y + panOffset.y - scrollOffset,
                             laneWidth, laneHeaderH};
        laneHeaders.push_back({lane, laneHeader});
        for (auto& [depth, rowNodes] : rows)
        {
            std::stable_sort(rowNodes.begin(), rowNodes.end(), [&](const ResearchNodeView* a, const ResearchNodeView* b)
            {
                auto parentOrder = [&](const ResearchNodeView* node)
                {
                    int order = node->layoutOrder;
                    for (const auto& prerequisite : node->prerequisites)
                    {
                        auto it = byId.find(prerequisite);
                        if (it != byId.end())
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

            float rowY = treeArea.y + panOffset.y + laneHeaderH + 24.0f + depth * (nodeH + rowGap) - scrollOffset;
            float usableWidth = std::max(1.0f, laneWidth - nodeW);

            // A node with no layout_order in the data file cannot be placed by
            // slot: BuildView substitutes the definition's file index for it, so
            // slots 0,1,2… would map to almost the same x and the nodes would
            // sit on top of each other at the left edge. Those pack from the
            // left with real spacing instead; only genuinely positioned nodes
            // land on their exact slot.
            std::vector<ResearchNodeView*> autoNodes;
            for (auto* rowNode : rowNodes)
            {
                bool explicitOrder = rowNode->definition != nullptr &&
                                     rowNode->definition->layoutOrder != std::numeric_limits<int>::max();
                if (!explicitOrder)
                {
                    autoNodes.push_back(rowNode);
                    continue;
                }

                int slot = ((rowNode->layoutOrder % 1000) + 1000) % 1000;
                float x = laneX + (static_cast<float>(slot) / 999.0f) * usableWidth;
                nodeRects[rowNode->id] = Rectangle{x, rowY, nodeW, nodeH};
            }

            for (size_t i = 0; i < autoNodes.size(); i++)
            {
                float x = laneX + static_cast<float>(i) * (nodeW + colGap * 0.5f);
                nodeRects[autoNodes[i]->id] = Rectangle{std::min(x, laneX + usableWidth), rowY, nodeW, nodeH};
            }
        }
        laneX += laneWidth + laneGap;
    }

    float contentBottom = treeArea.y;
    for (const auto& [id, rect] : nodeRects)
        contentBottom = std::max(contentBottom, rect.y + rect.height + scrollOffset);
    maxScrollOffset = std::max(0.0f, contentBottom - (treeArea.y + treeArea.height));
    scrollOffset = std::clamp(scrollOffset, 0.0f, maxScrollOffset);

    const ResearchNodeView* hovered = nullptr;
    const bool showNodeDetails = zoom >= NodeDetailZoomThreshold;
    for (const auto& node : nodes)
    {
        auto it = nodeRects.find(node.id);
        if (it != nodeRects.end() && CheckCollisionPointRec(mouse, it->second))
            hovered = &node;
    }

    std::set<std::string> highlightedPath;
    std::function<void(const std::string&)> collectParents = [&](const std::string& id)
    {
        if (!highlightedPath.insert(id).second)
            return;
        auto it = byId.find(id);
        if (it == byId.end())
            return;
        for (const auto& prerequisite : it->second->prerequisites)
            collectParents(prerequisite);
    };
    if (hovered != nullptr)
        collectParents(hovered->id);

    // ── Draw: lane headers, prerequisite edges, nodes ─────────────────────────
    BeginScissorMode(static_cast<int>(treeArea.x), static_cast<int>(treeArea.y), static_cast<int>(treeArea.width), static_cast<int>(treeArea.height));
    for (const auto& node : nodes)
    {
        auto child = nodeRects[node.id];
        Vector2 childAnchor{child.x + child.width * 0.5f, child.y};
        for (const auto& prerequisite : node.prerequisites)
        {
            auto parentIt = nodeRects.find(prerequisite);
            if (parentIt == nodeRects.end())
                continue;
            Rectangle parent = parentIt->second;
            Vector2 parentAnchor{parent.x + parent.width * 0.5f, parent.y + parent.height};
            bool highlighted = highlightedPath.contains(node.id) && highlightedPath.contains(prerequisite);
            Color edgeColor = highlighted ? UiTheme::Gold : Fade(UiTheme::Iron, 0.72f);
            float edgeWidth = highlighted ? 3.0f : 1.5f;
            if (std::abs(parentAnchor.x - childAnchor.x) < 1.5f)
            {
                DrawLineEx(parentAnchor, childAnchor, edgeWidth, edgeColor);
            }
            else
            {
                float midY = (parentAnchor.y + childAnchor.y) * 0.5f;
                Vector2 corner1{parentAnchor.x, midY};
                Vector2 corner2{childAnchor.x, midY};
                DrawLineEx(parentAnchor, corner1, edgeWidth, edgeColor);
                DrawLineEx(corner1, corner2, edgeWidth, edgeColor);
                DrawLineEx(corner2, childAnchor, edgeWidth, edgeColor);
            }
        }
    }

    for (const auto& node : nodes)
    {
        Rectangle rect = nodeRects[node.id];
        bool hover = CheckCollisionPointRec(mouse, rect);
        bool tagMatched = HasNodeTag(node, selectedTagFilter);
        Color fill = node.researched ? UiTheme::SelectedFill
                   : node.active ? UiTheme::SurfaceHover
                   : node.available ? UiTheme::Surface
                   : UiTheme::Inset;
        Color line = node.researched ? Color{140, 176, 96, 255}
                   : node.active ? Color{214, 178, 84, 255}
                   : node.available ? Color{176, 132, 68, 255}
                   : UiTheme::Iron;
        if (!showNodeDetails)
        {
            // At overview zoom, state is communicated by the node itself:
            // green researched, warm amber available, dark unavailable.
            fill = node.researched ? Color{56, 96, 62, 245}
                 : (node.active || node.available) ? Color{104, 82, 38, 240}
                 : Color{27, 31, 38, 225};
            line = node.researched ? Color{146, 205, 119, 255}
                 : (node.active || node.available) ? Color{220, 175, 78, 255}
                 : Color{70, 78, 88, 220};
        }
        if (!selectedTagFilter.empty() && !tagMatched)
        {
            fill.a = 110;
            line.a = 120;
        }
        Color border = tagMatched && !selectedTagFilter.empty() ? Color{150, 210, 130, 255}
                     : highlightedPath.contains(node.id) ? Color{232, 202, 104, 255}
                     : hover ? UiTheme::AmberBright
                     : line;
        UiControlIcons::DrawPixelHudWidgetFrame(rect, hover);
        Rectangle inner{rect.x + 8.0f, rect.y + 8.0f, rect.width - 16.0f, rect.height - 16.0f};
        DrawRectangleRounded(inner, 0.07f, 8, Fade(fill, 0.78f));
        DrawRectangleRoundedLines(rect, 0.08f, 8, 1.2f, border);
        DrawRectangleRounded(Rectangle{rect.x + 8.0f, rect.y + 8.0f, 3.0f, rect.height - 16.0f}, 0.5f, 4, line);
        if (showNodeDetails)
        {
            DrawUiTextWrappedCentered(node.name, Rectangle{rect.x + 14.0f * zoom, rect.y + 10.0f * zoom, rect.width - 26.0f * zoom, 46.0f * zoom}, std::max(16, static_cast<int>(22 * zoom)), UiTheme::Parchment, 2);
            UiText::DrawFit(node.stateText, Rectangle{rect.x + 14.0f * zoom, rect.y + 58.0f * zoom, rect.width - 26.0f * zoom, 20.0f * zoom}, std::max(12, static_cast<int>(16 * zoom)),
                node.researched ? Color{162, 214, 122, 255} : node.available ? UiTheme::AmberBright : Color{160, 142, 112, 255});

            float timeTextY = 82.0f;
            std::string timeText = node.active ? FormatDuration(node.remainingTime) + " left" : FormatDuration(node.researchTime);
            UiText::DrawFit(timeText, Rectangle{rect.x + 14.0f * zoom, rect.y + timeTextY * zoom, rect.width - 28.0f * zoom, 18.0f * zoom}, std::max(11, static_cast<int>(16 * zoom)), UiTheme::ParchmentDim);
        }
        if (node.active || node.researched)
        {
            Rectangle progress{rect.x + 10.0f, rect.y + rect.height - 9.0f, rect.width - 20.0f, 4.0f};
            DrawRectangleRounded(progress, 0.5f, 4, UiTheme::Ink);
            Rectangle fillBar = progress;
            fillBar.width *= static_cast<float>(std::clamp(node.progress, 0.0, 1.0));
            DrawRectangleRounded(fillBar, 0.5f, 4, node.researched ? Color{140, 176, 96, 255} : Color{214, 178, 84, 255});
        }
        if (hover && node.available && InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            if (isFocus)
            {
                scene->SubmitLocalCommand(GameCommand::StartFocus(scene->game->GetLocalPlayerId(), node.id));
            }
            else if (Building* university = FindIdleUniversity(scene))
            {
                scene->SubmitLocalCommand(GameCommand::StartTechnologyResearch(
                    scene->game->GetLocalPlayerId(), scene->game->GetLocalActiveProvinceId(),
                    node.id, university->positionId));
            }
        }
    }

    // Category separators retain the full width and world position of their
    // lane. Only the caption font is screen-space-stable, so zooming never
    // makes the name tiny while the separator still follows its category.
    for (const auto& [lane, header] : laneHeaders)
    {
        constexpr int laneFont = 23;
        const float separatorHeight = std::clamp(8.0f * zoom, 5.0f, 9.0f);
        Rectangle separator{header.x, header.y + std::max(25.0f, header.height),
                            header.width, separatorHeight};
        UiControlIcons::DrawPixelHudWidgetFrame(separator);

        const int laneWidth = UiText::Measure(lane, laneFont);
        UiText::Draw(lane,
                     header.x + (header.width - laneWidth) * 0.5f,
                     separator.y - laneFont - 4.0f,
                     laneFont, UiTheme::Parchment);
    }
    EndScissorMode();

    if (maxScrollOffset > 0.0f)
    {
        Rectangle track{treeArea.x + treeArea.width + 6.0f, treeArea.y, 5.0f, treeArea.height};
        DrawRectangleRounded(track, 0.5f, 4, UiTheme::Inset);
        float thumbH = std::max(32.0f, track.height * (track.height / (track.height + maxScrollOffset)));
        float thumbY = track.y + (track.height - thumbH) * (scrollOffset / maxScrollOffset);
        DrawRectangleRounded(Rectangle{track.x, thumbY, track.width, thumbH}, 0.5f, 4, UiTheme::Iron);
    }

    if (hovered != nullptr)
    {
        std::vector<std::string> lines{hovered->description};
        lines.push_back(TooltipSeparatorLine());
        lines.push_back("Time: " + FormatDuration(hovered->researchTime) + " | " + hovered->stateText);
        if (!isFocus && !hovered->costs.empty())
            lines.push_back(FormatResearchCosts(hovered->costs));
        if (!isFocus)
        {
            const auto unlockLines = CollectTechnologyUnlockLines(*hovered);
            lines.insert(lines.end(), unlockLines.begin(), unlockLines.end());
        }
        if (hovered->active)
            lines.push_back("Remaining: " + FormatDuration(hovered->remainingTime));
        if (!isFocus && !hovered->available && !hovered->researched && !hovered->active)
        {
            lines.push_back(TooltipSeparatorLine());
            lines.push_back("Prerequisites not met");
        }
        for (const auto& modifier : hovered->modifiers)
            lines.push_back(FormatModifierForFocusTooltip(modifier));
        Tooltip::Draw(hovered->name, lines, 460.0f, {}, 30);
    }
}

void ResearchTreePanelWidget::AdjustTreeZoom(Vec2i point, float wheel)
{
    if (wheel == 0.0f)
        return;

    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    Rectangle treeArea = GetTreeArea(bounds);
    Vector2 mouse{static_cast<float>(point.x), static_cast<float>(point.y)};
    if (!CheckCollisionPointRec(mouse, treeArea))
        return;

    float oldZoom = zoom;
    float newZoom = std::clamp(zoom + wheel * 0.08f, 0.42f, 1.15f);
    if (std::abs(newZoom - oldZoom) < 0.001f)
        return;

    float localX = mouse.x - treeArea.x;
    float localY = mouse.y - treeArea.y;
    panOffset.x = localX - (localX - panOffset.x) * (newZoom / oldZoom);
    panOffset.y = localY - (localY - panOffset.y) * (newZoom / oldZoom);
    zoom = newZoom;
}

// ─── FocusGuiSystem ──────────────────────────────────────────────────────────

FocusGuiSystem::FocusGuiSystem(GuiController* con)
    : GuiSystem(con)
{
    scene = dynamic_cast<GameScene*>(owner->scene);

    WireCommonSystemActions(*this, cameraMovement);

    focusPanel.scene = scene;
    focusPanel.ChangePositionAnchor({0.06f, 0.15f});
    focusPanel.ChangeSizeAnchor({0.88f, 0.82f});
    focusPanel.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    SetupStrategicHud(strategicHudWidget, scene);
}

void FocusGuiSystem::UpdateUiWidgets(Vec2i size)
{
    focusPanel.UpdateSize(size);
    strategicHudWidget.UpdateSize(size);
}

void FocusGuiSystem::Update(double dt)
{
    if (scene->game == nullptr)
        return;

    ApplyStrategicHudCameraPadding(scene);
    MoveCamera(scene, cameraMovement);
    owner->AddUiWidget(&focusPanel);
    owner->AddUiWidget(&strategicHudWidget);
}

void FocusGuiSystem::EscPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("default");
}

void FocusGuiSystem::BuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("build");
}

void FocusGuiSystem::RoadBuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("road_build");
}

void FocusGuiSystem::DestroyPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("destroy");
}

void FocusGuiSystem::StockpilePressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("stockpile");
}

void FocusGuiSystem::StatsPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("stats");
}

void FocusGuiSystem::FocusPressed()
{
    EscPressed();
}

void FocusGuiSystem::TechPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("tech");
}

void FocusGuiSystem::RosterPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("roster");
}

void FocusGuiSystem::LmbPressed()
{
    if (DispatchHudButtonClick(*this, strategicHudWidget))
        return;

    Vector2 mouse = GetMousePosition();
    Rectangle panelBounds{
        static_cast<float>(focusPanel.pos.x),
        static_cast<float>(focusPanel.pos.y),
        static_cast<float>(focusPanel.size.x),
        static_cast<float>(focusPanel.size.y)};
    if (CheckCollisionPointRec(mouse, PanelCloseButtonRect(panelBounds)))
    {
        EscPressed();
        return;
    }
}

void FocusGuiSystem::LmbReleased()
{
}

void FocusGuiSystem::RmbPressed()
{
    Vector2 mouse = GetMousePosition();
    if (focusPanel.ContainsPoint(Vec2i{static_cast<int>(mouse.x), static_cast<int>(mouse.y)}))
    {
        cameraMovement.isMoving = false;
        return;
    }
    cameraMovement.isMoving = true;
}

void FocusGuiSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

void FocusGuiSystem::Scroll()
{
    Vector2 mouse = GetMousePosition();
    Vec2i point{static_cast<int>(mouse.x), static_cast<int>(mouse.y)};
    if (focusPanel.ContainsPoint(point))
    {
        if (InputManager::IsKeyDown(KEY_LEFT_CONTROL) || InputManager::IsKeyDown(KEY_RIGHT_CONTROL))
        {
            focusPanel.scrollOffset = std::clamp(focusPanel.scrollOffset - InputManager::GetMouseWheelMove() * 48.0f, 0.0f, focusPanel.maxScrollOffset);
            return;
        }
        focusPanel.AdjustTreeZoom(point, InputManager::GetMouseWheelMove());
        return;
    }
    ZoomCamera(scene);
}

// ─── TechGuiSystem ───────────────────────────────────────────────────────────

TechGuiSystem::TechGuiSystem(GuiController* con)
    : GuiSystem(con)
{
    scene = dynamic_cast<GameScene*>(owner->scene);

    WireCommonSystemActions(*this, cameraMovement);

    techPanel.scene = scene;
    techPanel.ChangePositionAnchor({0.06f, 0.15f});
    techPanel.ChangeSizeAnchor({0.88f, 0.82f});
    techPanel.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    SetupStrategicHud(strategicHudWidget, scene);
}

void TechGuiSystem::UpdateUiWidgets(Vec2i windowSize)
{
    techPanel.UpdateSize(windowSize);
    strategicHudWidget.UpdateSize(windowSize);
}

// Domain gate moved out of GuiController::ChangeSystem (user-directed rework,
// 2026-07-14): the controller stays generic, the precondition lives with the
// system it protects.
bool TechGuiSystem::CanActivate()
{
    return HasUniversity(scene);
}

void TechGuiSystem::Update(double dt)
{
    if (scene->game == nullptr)
        return;

    ApplyStrategicHudCameraPadding(scene);
    MoveCamera(scene, cameraMovement);
    owner->AddUiWidget(&techPanel);
    owner->AddUiWidget(&strategicHudWidget);
}

void TechGuiSystem::EscPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("default");
}

void TechGuiSystem::BuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("build");
}

void TechGuiSystem::RoadBuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("road_build");
}

void TechGuiSystem::DestroyPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("destroy");
}

void TechGuiSystem::StockpilePressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("stockpile");
}

void TechGuiSystem::StatsPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("stats");
}

void TechGuiSystem::FocusPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("focus");
}

void TechGuiSystem::TechPressed()
{
    EscPressed();
}

void TechGuiSystem::RosterPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("roster");
}

void TechGuiSystem::LmbPressed()
{
    if (DispatchHudButtonClick(*this, strategicHudWidget))
        return;

    Vector2 mouse = GetMousePosition();
    Rectangle panelBounds{
        static_cast<float>(techPanel.pos.x),
        static_cast<float>(techPanel.pos.y),
        static_cast<float>(techPanel.size.x),
        static_cast<float>(techPanel.size.y)};
    if (CheckCollisionPointRec(mouse, PanelCloseButtonRect(panelBounds)))
    {
        EscPressed();
        return;
    }
}

void TechGuiSystem::LmbReleased()
{
}

void TechGuiSystem::RmbPressed()
{
    Vector2 mouse = GetMousePosition();
    if (techPanel.ContainsPoint(Vec2i{static_cast<int>(mouse.x), static_cast<int>(mouse.y)}))
    {
        cameraMovement.isMoving = false;
        return;
    }
    cameraMovement.isMoving = true;
}

void TechGuiSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

void TechGuiSystem::Scroll()
{
    Vector2 mouse = GetMousePosition();
    Vec2i point{static_cast<int>(mouse.x), static_cast<int>(mouse.y)};
    if (techPanel.ContainsPoint(point))
    {
        if (InputManager::IsKeyDown(KEY_LEFT_CONTROL) || InputManager::IsKeyDown(KEY_RIGHT_CONTROL))
        {
            techPanel.scrollOffset = std::clamp(techPanel.scrollOffset - InputManager::GetMouseWheelMove() * 48.0f, 0.0f, techPanel.maxScrollOffset);
            return;
        }
        techPanel.AdjustTreeZoom(point, InputManager::GetMouseWheelMove());
        return;
    }
    ZoomCamera(scene);
}
