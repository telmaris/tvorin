// Strategic top HUD, the full-screen statistics panel and its interaction mode.

#include "GuiInternal.h"

#include "scenes/Scenes.h"
#include "economy/Player.h"
#include "economy/StockpileIndex.h"
#include "ui/ControlIcons.h"
#include "ui/Renderer.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
    // Player-wide strategic stats aggregated for the HUD and statistics panel.
    struct PlayerStatsSnapshot
    {
        int freeManpower{0};
        int workers{0};
        int totalPopulation{0};
        int populationCap{0};
        double manpowerGainPerMinute{0.0};
        int foodSupplyPercent{100};
        int workerProductivityPercent{100};
        double villageFoodConsumptionPerMinute{0.0};
        std::map<ResourceType, int> storedResources;
        std::map<ResourceType, int> productionRatesPerMinute;
        std::map<ResourceType, int> consumptionRatesPerMinute;
        int buildingCount{0};
        int roadCount{0};
        int totalProduced{0};
    };

    PlayerStatsSnapshot BuildPlayerStatsSnapshot(Player* player)
    {
        PlayerStatsSnapshot stats;
        if (player == nullptr)
            return stats;

        const ProvinceEconomy* economy = player->GetProvinceEconomy();
        if (economy == nullptr)
            return stats;
        if (player->homeProvinceId != InvalidProvinceId)
        {
            const ProvincePopulationView population = BuildProvincePopulationView(*economy, *player);
            stats.freeManpower = static_cast<int>(std::floor(population.availableManpower));
            stats.workers = population.assignedWorkers;
            stats.totalPopulation = static_cast<int>(std::floor(population.currentPopulation));
            stats.populationCap = population.populationCap;
            stats.manpowerGainPerMinute = population.manpowerGainPerMinute;
        }
        else
        {
            stats.freeManpower = static_cast<int>(std::floor(
                player->strategicResources.Get(StrategicResourceType::Manpower)));
            stats.workers = static_cast<int>(std::floor(
                player->strategicResources.Get(StrategicResourceType::Workers)));
            stats.totalPopulation = static_cast<int>(std::floor(player->GetTotalPopulation()));
            stats.populationCap = player->GetPopulationCap();
        }
        // Food supply and worker productivity are distinct player-facing values.
        stats.foodSupplyPercent = static_cast<int>(std::round(player->GetFoodSupplyRatio() * 100.0));
        stats.workerProductivityPercent = static_cast<int>(std::round(player->GetFoodProductivity() * 100.0));
        stats.productionRatesPerMinute = economy->economyTelemetry.current.productionRatesPerMinute;
        stats.consumptionRatesPerMinute = economy->economyTelemetry.current.consumptionRatesPerMinute;

        for (const auto* building : player->GetTrackedBuildings())
        {
            if (building == nullptr || building->owner != player)
                continue;
            if (building->buildingType == BuildingType::Road)
                stats.roadCount++;
            else
                stats.buildingCount++;
        }

        for (const auto* building : player->GetTrackedBuildingsWithComponent<PopulationComponent>())
        {
            if (building == nullptr || building->owner != player || building->IsUnderConstruction())
                continue;

            const auto* population = building->GetComponent<PopulationComponent>();
            if (player->homeProvinceId == InvalidProvinceId)
            {
                const double productivity = population->GetManpowerProductivity();
                stats.manpowerGainPerMinute += player->ResolveStat(population->manpowerRate, building) *
                    productivity * 60.0;
            }
            for (const auto& supply : population->GetSupplyConsumptionViews(*building))
                if (supply.resource == ResourceType::FOOD_PROVISIONS)
                    stats.villageFoodConsumptionPerMinute += supply.packagesPerMinute;
        }
        // Warehouse network only (see StockpileIndex): Barracks' queued unit
        // costs are that building's own consumption
        // buffer, not stock the player can spend or route anywhere else.
        for (const auto& [type, totals] : StockpileIndex::Snapshot(*player))
            stats.storedResources[type] = totals.amount;

        for (const auto* building : player->GetTrackedBuildingsWithComponent<ProductionComponent>())
        {
            if (building == nullptr || building->owner != player || building->IsUnderConstruction())
                continue;

            stats.totalProduced += building->GetTotalProduced();
        }

        return stats;
    }

    // Short resource label for endpoint chips on the flow chart.
    const char* ResourceChipShortName(ResourceType type)
    {
        switch (type)
        {
            case ResourceType::WOOD: return "Wood";
            case ResourceType::PLANKS: return "Plank";
            case ResourceType::COAL: return "Coal";
            case ResourceType::IRON_ORE: return "Ore";
            case ResourceType::IRON: return "Iron";
            case ResourceType::STONE: return "Stone";
            case ResourceType::WHEAT: return "Wheat";
            case ResourceType::FLOUR: return "Flour";
            case ResourceType::BREAD: return "Bread";
            case ResourceType::MEAT: return "Meat";
            case ResourceType::WATER: return "Water";
            case ResourceType::BEER: return "Beer";
            case ResourceType::PAPER: return "Paper";
            case ResourceType::TOOLS: return "Tools";
            case ResourceType::FOOD_PROVISIONS: return "Food";
            case ResourceType::IRON_SWORD: return "FeSw";
            case ResourceType::STEEL_SWORD: return "StSw";
            case ResourceType::BOW: return "Bow";
            case ResourceType::ARROWS: return "Arr";
            default: return "Res";
        }
    }

    // Returns a stable chart color for one resource line.
    Color ResourceChartColor(ResourceType type)
    {
        int id = static_cast<int>(type);
        return Color{
            static_cast<unsigned char>(90 + (id * 47) % 150),
            static_cast<unsigned char>(120 + (id * 83) % 120),
            static_cast<unsigned char>(110 + (id * 31) % 140),
            235};
    }

    bool HasIdleUniversity(Player* player);
    bool HasAvailableFocus(Player* player);

    struct HudTechnologyState
    {
        bool canStart{false};
        bool active{false};
        float progress{0.0f};
        std::string activeName;
    };

    HudTechnologyState BuildHudTechnologyState(Player* player)
    {
        HudTechnologyState state;
        if (player == nullptr)
            return state;

        for (const auto* building : player->GetTrackedBuildingsWithComponent<ResearchComponent>())
        {
            if (building == nullptr || building->owner != player ||
                building->buildingType != BuildingType::University || building->IsUnderConstruction())
                continue;
            const auto* research = building->GetComponent<ResearchComponent>();
            if (research == nullptr || research->technologyId.empty())
                continue;
            state.active = true;
            state.progress = static_cast<float>(research->GetProgress());
            const auto* definition = FindTechnologyDefinition(research->technologyId);
            state.activeName = definition != nullptr ? definition->name : research->technologyId;
            break;
        }

        if (!state.active && HasIdleUniversity(player))
        {
            for (const auto& definition : GetTechnologyDefinitions())
            {
                if (player->CanResearchTechnology(definition.id))
                {
                    state.canStart = true;
                    break;
                }
            }
        }
        return state;
    }

    // Draws progress counter-clockwise, starting at the bottom-left corner.
    // Every edge is one exact quarter: bottom, right, top, then left.
    void DrawHudPerimeterProgress(Rectangle rect, float progress, Color color, float thickness)
    {
        progress = std::clamp(progress, 0.0f, 1.0f);
        if (progress <= 0.0f)
            return;

        const float width = std::max(0.0f, rect.width - 4.0f);
        const float height = std::max(0.0f, rect.height - 4.0f);
        float remainingEdges = progress * 4.0f;
        Vector2 current{rect.x + 2.0f, rect.y + 2.0f + height};
        const std::array<Vector2, 4> ends{{
            {rect.x + 2.0f + width, rect.y + 2.0f + height},
            {rect.x + 2.0f + width, rect.y + 2.0f},
            {rect.x + 2.0f, rect.y + 2.0f},
            {rect.x + 2.0f, rect.y + 2.0f + height}
        }};
        for (const Vector2 edgeEnd : ends)
        {
            if (remainingEdges <= 0.0f)
                break;
            const float edgeProgress = std::min(remainingEdges, 1.0f);
            Vector2 end{current.x + (edgeEnd.x - current.x) * edgeProgress,
                        current.y + (edgeEnd.y - current.y) * edgeProgress};
            DrawLineEx(current, end, thickness, color);
            remainingEdges -= edgeProgress;
            current = edgeEnd;
        }
    }

    bool HasIdleUniversity(Player* player)
    {
        if (player == nullptr)
            return false;
        for (const auto* building : player->GetTrackedBuildingsWithComponent<ResearchComponent>())
        {
            if (building == nullptr || building->owner != player ||
                building->buildingType != BuildingType::University || building->IsUnderConstruction())
                continue;
            const auto* research = building->GetComponent<ResearchComponent>();
            if (research != nullptr && research->technologyId.empty())
                return true;
        }
        return false;
    }

    bool HasAvailableFocus(Player* player)
    {
        if (player == nullptr)
            return false;
        for (const auto& definition : GetFocusDefinitions())
            if (player->CanUnlockFocus(definition.id))
                return true;
        return false;
    }

    void DrawHudValueFit(const std::string& text, Rectangle bounds, int fontSize, Color color)
    {
        UiFontRoleScope valueFont{UiFontRole::Plain};
        int measured = UiText::Measure(text, fontSize);
        while (fontSize > 10 && measured + 1 > bounds.width)
        {
            --fontSize;
            measured = UiText::Measure(text, fontSize);
        }
        const float y = bounds.y + (bounds.height - static_cast<float>(fontSize)) * 0.5f;
        // Alegreya Sans Regular is intentionally airy. A one-pixel second pass
        // gives compact HUD numerals more body without changing prose/tooltips.
        UiText::Draw(text, bounds.x + 1.0f, y, fontSize, Fade(color, 0.90f));
        UiText::Draw(text, bounds.x, y, fontSize, color);
    }
}

// ─── StrategicResourceHudWidget ──────────────────────────────────────────────

void StrategicResourceHudWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr)
        return;

    Player* player = GuiLocalPlayer(scene);
    if (player == nullptr)
        return;
    const ProvinceEconomy* economy = player->GetProvinceEconomy();
    if (economy == nullptr)
        return;

    PlayerStatsSnapshot stats = BuildPlayerStatsSnapshot(player);
    const bool disableDestroy = tutorialDisableDestroy || scene->AreTutorialDestroyLocked();
    const bool disableDecisions = tutorialDisableDecisions || scene->AreTutorialDecisionsLocked();
    const bool disableStatistics = tutorialDisableStatistics || scene->AreTutorialStatisticsLocked();
    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    Rectangle hudChrome{bounds.x, bounds.y, bounds.width, bounds.height};
    if (!UiControlIcons::DrawPixelTopHudFrame(hudChrome))
    {
        DrawRectangle(static_cast<int>(bounds.x), static_cast<int>(bounds.y),
                      static_cast<int>(bounds.width), static_cast<int>(bounds.height), UiTheme::Ink);
        DrawRectangleGradientV(static_cast<int>(bounds.x), static_cast<int>(bounds.y),
                               static_cast<int>(bounds.width), static_cast<int>(bounds.height),
                               UiTheme::Surface, UiTheme::Panel);
        DrawRectangle(static_cast<int>(bounds.x), static_cast<int>(bounds.y),
                      static_cast<int>(bounds.width), 1, Fade(UiTheme::SteelHover, 0.82f));
        DrawRectangle(static_cast<int>(bounds.x),
                      static_cast<int>(bounds.y + bounds.height - 2.0f),
                      static_cast<int>(bounds.width), 2, UiTheme::Bronze);
    }

    // Keep content on the dark centre field, past the scaled ornamental left
    // cap. The standalone heraldic crest was intentionally removed from this
    // pilot so it cannot collide with the first statistic panel.
    const float topHudCornerScale = std::min({
        1.0f,
        bounds.height / 148.0f,
        bounds.width / 256.0f});
    const float chipStartX = bounds.x + 128.0f * topHudCornerScale + 16.0f;

    // Keep every HUD panel and action button at least 20 px shorter than the
    // previous compact layout. Widths follow the same reduction so the whole
    // content row stays inside the dark field of the new frame.
    const float chipWidth = std::clamp(bounds.height * 1.55f - 20.0f,
                                       98.0f, 118.0f);
    const float populationChipWidth = std::clamp(bounds.height * 1.95f - 20.0f,
                                                 134.0f, 154.0f);
    const float chipGap = 3.0f;
    const float chipHeight = std::clamp(bounds.height - 52.0f, 44.0f, 60.0f);
    const float chipY = bounds.y + (bounds.height - chipHeight) * 0.5f;
    auto chipAt = [&](int index)
    {
        if (index == 0)
            return Rectangle{chipStartX, chipY, populationChipWidth, chipHeight};
        return Rectangle{chipStartX + populationChipWidth + chipGap +
                             static_cast<float>(index - 1) * (chipWidth + chipGap),
                         chipY, chipWidth, chipHeight};
    };
    auto chipIconRect = [](Rectangle chip)
    {
        const float iconSize = std::max(40.0f, chip.height - 16.0f);
        return Rectangle{chip.x + 10.0f, chip.y + (chip.height - iconSize) * 0.5f,
                         iconSize, iconSize};
    };

    Rectangle manpowerChip = chipAt(0);
    Rectangle foodChip = chipAt(1);
    Rectangle buildersChip = chipAt(2);
    Rectangle manpowerIcon = chipIconRect(manpowerChip);
    Rectangle foodIcon = chipIconRect(foodChip);
    Rectangle buildersIcon = chipIconRect(buildersChip);

    auto drawManpowerIcon = [&](Rectangle icon)
    {
        if (UiControlIcons::DrawPixelHudGlyph(UiControlIcons::HudIcon::Manpower, icon))
            return;
        if (UiControlIcons::DrawHudGlyph(UiControlIcons::HudIcon::Manpower, icon))
            return;
        DrawCircle(static_cast<int>(icon.x + icon.width * 0.5f), static_cast<int>(icon.y + icon.height * 0.28f), icon.width * 0.16f, Color{224, 208, 178, 255});
        DrawRectangleRounded(Rectangle{icon.x + icon.width * 0.28f, icon.y + icon.height * 0.46f, icon.width * 0.44f, icon.height * 0.38f}, 0.30f, 8, Color{224, 208, 178, 255});
    };

    auto drawStatChip = [&](Rectangle chip, Rectangle icon, const std::string& text, Color accent,
                            std::function<void(Rectangle)> drawIcon, bool pulseHighlight = false)
    {
        const float pulse = pulseHighlight
            ? 0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 4.5f)
            : 0.0f;
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), chip);
        if (!UiControlIcons::DrawPixelTopHudWidgetFrame(chip, hovered))
        {
            DrawRectangleRec(chip, UiTheme::Surface);
            DrawRectangleLinesEx(chip, 1.0f, UiTheme::Iron);
        }
        if (pulseHighlight)
            DrawRectangleLinesEx(Rectangle{chip.x + 4.0f, chip.y + 4.0f,
                                            chip.width - 8.0f, chip.height - 8.0f},
                                 1.0f + pulse * 1.2f,
                                 Color{accent.r, accent.g, accent.b,
                                       static_cast<unsigned char>(170.0f + pulse * 85.0f)});
        drawIcon(icon);
        const float textX = icon.x + icon.width + 7.0f;
        DrawHudValueFit(text, Rectangle{textX, chip.y + 6.0f,
                                        chip.x + chip.width - textX - 10.0f,
                                        chip.height - 12.0f},
                        25, UiTheme::Parchment);
    };

    auto drawResourceCell = [&](ResourceType type, Rectangle cell)
    {
        const float iconSize = std::max(34.0f, cell.height - 16.0f);
        Rectangle icon{cell.x + 5.0f, cell.y + (cell.height - iconSize) * 0.5f,
                       iconSize, iconSize};
        int amount = 0;
        auto it = stats.storedResources.find(type);
        if (it != stats.storedResources.end())
            amount = it->second;

        bool hovered = CheckCollisionPointRec(GetMousePosition(), cell);
        if (hovered)
        {
            DrawRectangleRec(Rectangle{cell.x + 2.0f, cell.y + 2.0f,
                                        cell.width - 4.0f, cell.height - 4.0f},
                             Fade(UiTheme::Cyan, 0.18f));
            DrawRectangleLinesEx(Rectangle{cell.x + 2.0f, cell.y + 2.0f,
                                            cell.width - 4.0f, cell.height - 4.0f},
                                 1.0f, Fade(UiTheme::SteelHover, 0.82f));
        }
        GuiPanel::DrawResourceIcon(type, Rectangle{icon.x + 2.0f, icon.y + 2.0f,
                                                   icon.width - 4.0f, icon.height - 4.0f});
        const float textX = icon.x + icon.width + 7.0f;
        DrawHudValueFit(std::to_string(amount),
                        Rectangle{textX, cell.y + 7.0f,
                                  cell.x + cell.width - textX - 8.0f, cell.height - 14.0f},
                        24, Color{228, 210, 180, 255});
    };

    const bool populationCapped = stats.populationCap > 0 &&
                                  stats.totalPopulation >= stats.populationCap;
    const Color populationAccent = (highlightManpower || populationCapped)
        ? UiTheme::Gold : Color{188, 150, 96, 255};
    drawStatChip(manpowerChip, manpowerIcon,
                 std::to_string(stats.freeManpower) + " | " +
                     std::to_string(stats.totalPopulation) + "/" +
                     std::to_string(stats.populationCap),
                 populationAccent, drawManpowerIcon,
                 highlightManpower || populationCapped);

    drawStatChip(foodChip, foodIcon, std::to_string(stats.foodSupplyPercent) + "%",
                 highlightFood ? UiTheme::Gold : Color{145, 198, 118, 255}, [&](Rectangle icon)
    {
        GuiPanel::DrawResourceIcon(ResourceType::FOOD_PROVISIONS,
                                   Rectangle{icon.x + 4.0f, icon.y + 4.0f,
                                             icon.width - 8.0f, icon.height - 8.0f});
    }, highlightFood);

    // Builders chip: free / total construction builders available to the player.
    int totalBuilders = economy->construction.EffectiveBuilders(*player);
    int freeBuilders = std::max(0, totalBuilders - economy->construction.ActiveCount());
    drawStatChip(buildersChip, buildersIcon,
                 std::to_string(freeBuilders) + "/" + std::to_string(totalBuilders),
                 Color{201, 174, 122, 255}, [&](Rectangle icon)
    {
        if (UiControlIcons::DrawPixelHudGlyph(UiControlIcons::HudIcon::Builders, icon))
            return;
        if (UiControlIcons::DrawHudGlyph(UiControlIcons::HudIcon::Builders, icon))
            return;
        float cx = icon.x + icon.width * 0.5f;
        DrawRectangleRounded(Rectangle{icon.x + icon.width * 0.28f, icon.y + icon.height * 0.22f, icon.width * 0.46f, icon.height * 0.20f}, 0.35f, 6, Color{224, 208, 178, 255});
        DrawRectangleRounded(Rectangle{cx - icon.width * 0.06f, icon.y + icon.height * 0.36f, icon.width * 0.12f, icon.height * 0.44f}, 0.40f, 6, Color{201, 174, 122, 255});
    });

    Rectangle statsButton = StatsHudButtonRect(*this);
    Rectangle focusButton = FocusHudButtonRect(*this);
    Rectangle techButton = TechHudButtonRect(*this);
    Rectangle destroyButton = DestroyHudButtonRect(*this);
    Rectangle roadButton = RoadHudButtonRect(*this);
    Rectangle buildButton = BuildHudButtonRect(*this);
    Rectangle rosterButton = RosterHudButtonRect(*this);
    Rectangle globalMapButton = GlobalMapHudButtonRect(*this);

    const float desiredResourcePanelWidth = std::clamp(bounds.height * 3.45f - 20.0f,
                                                       210.0f, 310.0f);
    float resourceX = buildersChip.x + buildersChip.width + 8.0f;
    const float resourceAvailableWidth = buildButton.x - 12.0f - resourceX;
    const bool showResourcePanel = resourceAvailableWidth >= 180.0f;
    std::array<Rectangle, 3> resourceCells{};
    if (showResourcePanel)
    {
        const float resourcePanelWidth = std::min(desiredResourcePanelWidth,
                                                   resourceAvailableWidth);
        Rectangle resourcePanel{resourceX, chipY, resourcePanelWidth, chipHeight};
        if (!UiControlIcons::DrawPixelTopHudWidgetFrame(resourcePanel))
        {
            DrawRectangleRec(resourcePanel, UiTheme::Surface);
            DrawRectangleLinesEx(resourcePanel, 1.0f, UiTheme::Iron);
        }

        // One cassette, three fixed cells. The outer four corners therefore
        // come from a single 9-slice instead of three frames colliding.
        constexpr float cassetteInsetX = 8.0f;
        constexpr float cassetteInsetY = 5.0f;
        const float cellWidth = (resourcePanel.width - cassetteInsetX * 2.0f) / 3.0f;
        for (size_t index = 0; index < resourceCells.size(); ++index)
        {
            resourceCells[index] = Rectangle{
                resourcePanel.x + cassetteInsetX + cellWidth * static_cast<float>(index),
                resourcePanel.y + cassetteInsetY,
                cellWidth,
                resourcePanel.height - cassetteInsetY * 2.0f};
            if (index > 0)
            {
                const float separatorX = resourceCells[index].x;
                DrawLineEx({separatorX, resourcePanel.y + 10.0f},
                           {separatorX, resourcePanel.y + resourcePanel.height - 10.0f},
                           1.0f, Fade(UiTheme::Iron, 0.90f));
            }
        }
        drawResourceCell(ResourceType::WOOD, resourceCells[0]);
        drawResourceCell(ResourceType::STONE, resourceCells[1]);
        drawResourceCell(ResourceType::PLANKS, resourceCells[2]);
    }

    bool statsHovered = IsStatsHudButtonHovered(*this);
    bool focusHovered = IsFocusHudButtonHovered(*this);
    bool techUnlocked = HasUniversity(scene);
    bool techHovered = CheckCollisionPointRec(GetMousePosition(), techButton);
    bool destroyHovered = IsDestroyHudButtonHovered(*this);
    bool roadHovered = CheckCollisionPointRec(GetMousePosition(), roadButton);
    bool buildHovered = CheckCollisionPointRec(GetMousePosition(), buildButton);
    const bool barracksUnlocked = HasCompletedBarracks(scene);
    const bool globalMapButtonHovered =
        CheckCollisionPointRec(GetMousePosition(), globalMapButton);
    bool globalMapHovered = barracksUnlocked && globalMapButtonHovered;
    const bool focusActive = !player->focuses.GetActiveFocusId().empty();
    const bool focusAvailable = !focusActive && HasAvailableFocus(player);
    float focusProgress = static_cast<float>(player->focuses.GetActiveFocusProgress());
    const HudTechnologyState technologyState = BuildHudTechnologyState(player);

    // Roster is a peaceful recruitment summary. Provincial expeditions are
    // presented by the global map layer introduced in the next stage.
    const bool rosterButtonHovered =
        CheckCollisionPointRec(GetMousePosition(), rosterButton);
    bool rosterHovered = barracksUnlocked && rosterButtonHovered;
    int rosterCount = static_cast<int>(player->roster.units.size());

    // The text labels are now deliberately in tooltips; the bar itself stays
    // compact and uses the generated icon atlas.
    auto drawHudButton = [&](Rectangle rect, UiControlIcons::HudIcon icon, bool hovered,
                             Color fallbackFill, Color outline, bool enabled = true,
                             bool attention = false)
    {
        // Availability always wins over the attention hint. A locked tutorial
        // action or research button must remain visually quiet.
        attention = attention && enabled;
        if (!UiControlIcons::DrawPixelTopHudWidgetFrame(rect, hovered && enabled))
        {
            const Color fill = hovered
                ? Color{static_cast<unsigned char>(std::min(255, fallbackFill.r + 24)),
                        static_cast<unsigned char>(std::min(255, fallbackFill.g + 24)),
                        static_cast<unsigned char>(std::min(255, fallbackFill.b + 24)),
                        fallbackFill.a}
                : fallbackFill;
            DrawRectangleRec(rect, fill);
            DrawRectangleLinesEx(rect, 1.0f, outline);
        }

        Rectangle glyph{rect.x + 7.0f, rect.y + 7.0f,
                        rect.width - 14.0f, rect.height - 14.0f};
        const float pulse = attention
            ? 0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 2.35f +
                                     static_cast<float>(static_cast<int>(icon)) * 0.37f)
            : 0.0f;
        auto scaledAroundCenter = [](Rectangle source, float scale)
        {
            const float width = source.width * scale;
            const float height = source.height * scale;
            return Rectangle{source.x + (source.width - width) * 0.5f,
                             source.y + (source.height - height) * 0.5f,
                             width, height};
        };
        if (attention)
            DrawRectangleRec(Rectangle{glyph.x + 2.0f, glyph.y + 2.0f,
                                       glyph.width - 4.0f, glyph.height - 4.0f},
                             Fade(UiTheme::Gold, 0.08f + pulse * 0.08f));
        if (attention)
        {
            const Rectangle glowGlyph = scaledAroundCenter(glyph, 1.055f + pulse * 0.025f);
            const bool shaderGlow = UiControlIcons::DrawPixelHudGlow(
                icon, glowGlyph, Color{255, 194, 74, 255}, 0.72f + pulse * 0.38f);
            if (!shaderGlow)
            {
                BeginBlendMode(BLEND_ADDITIVE);
                UiControlIcons::DrawPixelHudGlyph(
                    icon, glowGlyph,
                    Color{186, 154, 82,
                          static_cast<unsigned char>(80.0f + pulse * 55.0f)});
                EndBlendMode();
            }
        }

        const Rectangle crispGlyph = attention
            ? scaledAroundCenter(glyph, 1.0f + pulse * 0.022f)
            : glyph;
        if (!UiControlIcons::DrawPixelHudGlyph(icon, crispGlyph, WHITE))
            UiControlIcons::DrawHudGlyph(icon, crispGlyph, WHITE);
        if (attention)
        {
            DrawRectangleLinesEx(Rectangle{rect.x + 3.0f, rect.y + 3.0f,
                                            rect.width - 6.0f, rect.height - 6.0f},
                                 1.0f + pulse * 0.55f,
                                 Color{246, 205, 92,
                                       static_cast<unsigned char>(175.0f + pulse * 70.0f)});
        }

        if (!enabled)
        {
            DrawRectangleRec(rect, Color{7, 11, 18, 150});
            DrawRectangleLinesEx(rect, 1.0f, UiTheme::Iron);
        }
    };

    drawHudButton(globalMapButton, UiControlIcons::HudIcon::GlobalMap, globalMapHovered,
                  Color{24, 55, 67, 242}, UiTheme::Bronze, barracksUnlocked);

    drawHudButton(buildButton, UiControlIcons::HudIcon::Build, buildHovered,
                  Color{24, 55, 67, 242}, UiTheme::Bronze);
    drawHudButton(roadButton, UiControlIcons::HudIcon::Road, roadHovered,
                  Color{26, 43, 61, 242}, UiTheme::Bronze);
    if (disableDestroy)
    {
        drawHudButton(destroyButton, UiControlIcons::HudIcon::Destroy, false,
                      Color{76, 39, 51, 242}, UiTheme::DangerBorder, false);
    }
    else
        drawHudButton(destroyButton, UiControlIcons::HudIcon::Destroy, destroyHovered,
                      Color{76, 39, 51, 242}, UiTheme::DangerBorder);
    drawHudButton(rosterButton, UiControlIcons::HudIcon::Roster, rosterHovered,
                  Color{40, 42, 62, 242}, UiTheme::Bronze, barracksUnlocked);
    const float badgeRadius = std::max(8.0f, rosterButton.height * 0.145f);
    const Vector2 badgeCenter{rosterButton.x + rosterButton.width * 0.50f,
                              rosterButton.y + rosterButton.height * 0.65f};
    DrawCircleV(badgeCenter, badgeRadius,
                barracksUnlocked ? Color{8, 17, 30, 238} : Color{20, 24, 30, 210});
    DrawCircleLines(static_cast<int>(badgeCenter.x), static_cast<int>(badgeCenter.y),
                    badgeRadius, barracksUnlocked ? UiTheme::Iron : Color{76, 82, 90, 220});
    const std::string rosterCountText = std::to_string(rosterCount);
    const int rosterCountFont = std::max(13, static_cast<int>(badgeRadius * 1.45f));
    const int rosterCountWidth = UiText::Measure(rosterCountText, rosterCountFont);
    const float rosterCountX = badgeCenter.x - static_cast<float>(rosterCountWidth) * 0.5f;
    const float rosterCountY = badgeCenter.y - static_cast<float>(rosterCountFont) * 0.54f;
    UiText::Draw(rosterCountText, rosterCountX + 1.0f, rosterCountY + 1.0f,
                 rosterCountFont, Color{0, 0, 0, 220});
    UiText::Draw(rosterCountText, rosterCountX, rosterCountY,
                 rosterCountFont,
                 barracksUnlocked ? UiTheme::Parchment : Color{116, 124, 134, 255});
    const Color focusLine = disableDecisions ? UiTheme::Iron
        : focusAvailable ? Color{210, 170, 255, 255} : Color{158, 132, 218, 255};
    drawHudButton(focusButton, UiControlIcons::HudIcon::Decisions, focusHovered,
                  Color{66, 44, 98, 245}, focusLine, !disableDecisions,
                  focusAvailable);
    if (focusActive)
    {
        DrawRectangleLinesEx(Rectangle{focusButton.x + 2.0f, focusButton.y + 2.0f,
                                       focusButton.width - 4.0f, focusButton.height - 4.0f},
                             2.0f, Color{32, 30, 48, 230});
        DrawHudPerimeterProgress(focusButton, focusProgress,
                                 Color{196, 133, 255, 255}, 2.0f);
    }

    drawHudButton(techButton, UiControlIcons::HudIcon::Technology, techHovered,
                  Color{26, 43, 61, 242}, UiTheme::Bronze, techUnlocked,
                  technologyState.canStart);
    if (technologyState.active)
    {
        DrawRectangleLinesEx(Rectangle{techButton.x + 2.0f, techButton.y + 2.0f,
                                       techButton.width - 4.0f, techButton.height - 4.0f},
                             2.0f, Color{32, 43, 48, 230});
        DrawHudPerimeterProgress(techButton, technologyState.progress,
                                 Color{115, 219, 232, 255}, 2.0f);
    }
    drawHudButton(statsButton, UiControlIcons::HudIcon::Resources, statsHovered,
                  Color{26, 43, 61, 242}, UiTheme::Bronze, !disableStatistics);

    Vector2 mouse = GetMousePosition();
    if (CheckCollisionPointRec(mouse, manpowerChip))
    {
        Tooltip::Draw("Population", {
            "Population: " + std::to_string(stats.totalPopulation) + "/" + std::to_string(stats.populationCap),
            "Free manpower: " + std::to_string(stats.freeManpower),
            "Workers: " + std::to_string(stats.workers),
            "Gain: +" + FormatOneDecimal(stats.manpowerGainPerMinute) + " / min"
        }, 280.0f);
    }
    else if (CheckCollisionPointRec(mouse, foodChip))
    {
        Tooltip::Draw("Food Supply", {
            "Supply: " + std::to_string(stats.foodSupplyPercent) + "%",
            "Worker productivity: " + std::to_string(stats.workerProductivityPercent) + "%",
            "Village consumption: " + FormatOneDecimal(stats.villageFoodConsumptionPerMinute) + " / min"
        }, 290.0f);
    }
    else if (CheckCollisionPointRec(mouse, buildersChip))
    {
        Tooltip::Draw("Builders", {
            "Free: " + std::to_string(freeBuilders),
            "Working: " + std::to_string(economy->construction.ActiveCount()),
            "Total: " + std::to_string(totalBuilders)
        }, 250.0f);
    }
    // Warehouse chips: the total, plus which warehouse holds how much (user
    // request, 2026-07-25) — StockpileTooltipLines is shared with the
    // stockpile panel so the two always word it identically.
    else if (showResourcePanel && CheckCollisionPointRec(mouse, resourceCells[0]))
    {
        DrawResourceTooltip(ResourceType::WOOD, StockpileTooltipLines(player, ResourceType::WOOD), 280.0f);
    }
    else if (showResourcePanel && CheckCollisionPointRec(mouse, resourceCells[1]))
    {
        DrawResourceTooltip(ResourceType::STONE, StockpileTooltipLines(player, ResourceType::STONE), 280.0f);
    }
    else if (showResourcePanel && CheckCollisionPointRec(mouse, resourceCells[2]))
    {
        DrawResourceTooltip(ResourceType::PLANKS, StockpileTooltipLines(player, ResourceType::PLANKS), 280.0f);
    }
    else if (buildHovered)
    {
        Tooltip::Draw("Build", {"[Q] Open build menu"}, 210.0f);
    }
    else if (roadHovered)
    {
        Tooltip::Draw("Road", {"[R] Open road building"}, 220.0f);
    }
    else if (destroyHovered)
    {
        Tooltip::Draw("Destroy", {"[D] Open destroy mode"}, 220.0f);
    }
    else if (statsHovered)
    {
        Tooltip::Draw("Resources", {"[S] Open economy overview"}, 240.0f);
    }
    else if (focusHovered)
    {
        Tooltip::Draw("Decisions", {
            "[F] Open decisions",
            focusActive ? "Decision is already in progress" :
            focusAvailable ? "Decision available" : "No decision currently available",
            "Progress: " + std::to_string(static_cast<int>(std::round(focusProgress * 100.0f))) + "%"
        }, 240.0f);
    }
    else if (techHovered)
    {
        if (techUnlocked)
        {
            std::vector<std::string> lines{"[T] Open technology research tree"};
            if (technologyState.active)
            {
                lines.push_back("Researching: " + technologyState.activeName);
                lines.push_back("Progress: " +
                                std::to_string(static_cast<int>(std::round(technologyState.progress * 100.0f))) + "%");
            }
            else if (technologyState.canStart)
                lines.push_back("Research available");
            Tooltip::Draw("Technology", lines, 270.0f);
        }
        else
            Tooltip::Draw("Technology", {"Requires a completed University"}, 270.0f);
    }
    else if (!barracksUnlocked && rosterButtonHovered)
    {
        Tooltip::Draw("Roster", {"Requires a completed Barracks"}, 260.0f);
    }
    else if (!barracksUnlocked && globalMapButtonHovered)
    {
        Tooltip::Draw("Global map", {"Requires a completed Barracks"}, 270.0f);
    }
    else if (rosterHovered)
    {
        Tooltip::Draw("Roster", {
            "[U] Open recruited roster",
            "Recruited units: " + std::to_string(rosterCount),
            "Provincial expeditions arrive with the global map"
        }, 260.0f);
    }
    else if (globalMapHovered)
    {
        Tooltip::Draw("Global map", {
            "[M] Open/close global map",
            "Known provinces and tracks"
        }, 250.0f);
    }
}

void StrategicResourceHudWidget::UpdateSize(Vec2i windowSize)
{
    UpdateStrategicHudLayout(*this, windowSize);
}

// ─── StatsPanelWidget ────────────────────────────────────────────────────────

// Time-window spinner in the title bar.
Rectangle StatsPanelWidget::GetWindowSpinnerRect() const
{
    const float closeReserve = 86.0f;
    return Rectangle{pos.x + size.x - closeReserve - 202.0f, pos.y + 10.0f, 190.0f, 34.0f};
}

// Production/consumption toggle in the title bar.
Rectangle StatsPanelWidget::GetFlowModeToggleRect() const
{
    const float closeReserve = 86.0f;
    return Rectangle{pos.x + size.x - closeReserve - 448.0f, pos.y + 10.0f, 220.0f, 34.0f};
}

// Chart column (right of the population/army column).
Rectangle StatsPanelWidget::GetChartRect() const
{
    float colGap = 22.0f;
    float top = pos.y + 78.0f;
    float bottom = pos.y + size.y - 22.0f;
    float leftWidth = size.x * 0.29f;
    float chartX = pos.x + 24.0f + leftWidth + colGap;
    return Rectangle{chartX, top, pos.x + size.x - chartX - 24.0f, bottom - top};
}

// Returns one resource filter slot rectangle in the production graph panel.
Rectangle StatsPanelWidget::GetFilterButtonRect(Rectangle chart, int index) const
{
    float size = 36.0f;
    float gap = 8.0f;
    int columns = std::max(1, static_cast<int>((chart.width - 116.0f) / (size + gap)));
    int col = index % columns;
    int row = index / columns;
    return Rectangle{chart.x + 104.0f + col * (size + gap), chart.y + chart.height - 70.0f + row * (size + gap), size, size};
}

// Returns the "all resources" filter reset button rectangle.
Rectangle StatsPanelWidget::GetAllFilterButtonRect(Rectangle chart) const
{
    return Rectangle{chart.x + 18.0f, chart.y + chart.height - 70.0f, 72.0f, 36.0f};
}

// Draws the player-wide economy and strategic statistics panel.
void StatsPanelWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr)
        return;

    Player* player = GuiLocalPlayer(scene);
    if (player == nullptr)
        return;
    const ProvinceEconomy* economy = player->GetProvinceEconomy();
    if (economy == nullptr)
        return;

    PlayerStatsSnapshot stats = BuildPlayerStatsSnapshot(player);
    const bool showingConsumption = selectedFlowMode == 1;
    const auto& currentRates = showingConsumption ? stats.consumptionRatesPerMinute : stats.productionRatesPerMinute;
    const auto& flowHistory = economy->economyTelemetry.history;
    double historyTime = economy->economyTelemetry.elapsedTime;

    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    if (!UiControlIcons::DrawPixelHudFrame(bounds))
    {
        DrawRectangleRounded(bounds, 0.025f, 8, UiTheme::Panel);
        DrawRectangleRoundedLines(bounds, 0.025f, 8, 1.0f, UiTheme::Iron);
    }

    Rectangle title{bounds.x, bounds.y, bounds.width, 54.0f};
    const float frameInset = UiControlIcons::PixelHudFrameInset(bounds);
    Rectangle titleVisual{title.x + frameInset + 2.0f, title.y + 4.0f,
                          std::max(0.0f, title.width - (frameInset + 2.0f) * 2.0f),
                          title.height - 8.0f};
    UiText::DrawTitleBar(titleVisual, "Statistics", PanelTitleCloseReserve(bounds));
    DrawCloseButton(bounds);

    constexpr std::array<int, 3> windows{15, 60, 300};
    const char* windowLabels[] = {"15 sec", "1 min", "5 min"};
    // Right edge is at title.width - 54, giving a 10px gap before the close button (at title.width - 44).
    Rectangle spin = GetWindowSpinnerRect();
    if (!UiControlIcons::DrawPixelHudWidgetFrame(spin, CheckCollisionPointRec(GetMousePosition(), spin)))
    {
        DrawRectangleRounded(spin, 0.12f, 8, UiTheme::Inset);
        DrawRectangleRoundedLines(spin, 0.12f, 8, 1.0f, UiTheme::Iron);
    }
    UiText::DrawFit("<", Rectangle{spin.x + 8.0f, spin.y + 4.0f, 24.0f, 24.0f}, 24, UiTheme::Parchment);
    UiText::DrawFit(windowLabels[selectedWindowIndex], Rectangle{spin.x + 42.0f, spin.y + 5.0f, spin.width - 84.0f, 24.0f}, 22, UiTheme::Parchment);
    UiText::DrawFit(">", Rectangle{spin.x + spin.width - 32.0f, spin.y + 4.0f, 24.0f, 24.0f}, 24, UiTheme::Parchment);

    Rectangle modeToggle = GetFlowModeToggleRect();
    const char* modeLabels[] = {"Production", "Consumption"};
    for (int i = 0; i < 2; i++)
    {
        Rectangle half{modeToggle.x + i * modeToggle.width * 0.5f, modeToggle.y, modeToggle.width * 0.5f, modeToggle.height};
        bool selected = selectedFlowMode == i;
        if (!UiControlIcons::DrawPixelHudWidgetFrame(half, CheckCollisionPointRec(GetMousePosition(), half)))
        {
            DrawRectangleRounded(half, 0.10f, 8, selected ? UiTheme::SelectedFill : UiTheme::Inset);
            DrawRectangleRoundedLines(half, 0.10f, 8, 1.0f, selected ? UiTheme::SageBright : UiTheme::Iron);
        }
        if (selected)
            DrawRectangleRounded(Rectangle{half.x + 6.0f, half.y + half.height - 5.0f, half.width - 12.0f, 2.0f}, 0.5f, 4, UiTheme::Cyan);
        UiText::DrawFit(modeLabels[i], Rectangle{half.x + 8.0f, half.y + 6.0f, half.width - 16.0f, 22.0f}, 18, UiTheme::Parchment);
    }

    float top = bounds.y + 78.0f;
    float bottom = bounds.y + bounds.height - 22.0f;
    Rectangle left{bounds.x + 24.0f, top, bounds.width * 0.31f, bottom - top};
    Rectangle chart = GetChartRect();

    auto drawColumn = [](Rectangle col, const std::string& titleText)
    {
        if (!UiControlIcons::DrawPixelHudPanelFrame(col))
        {
            DrawRectangleRounded(col, 0.035f, 8, UiTheme::Ink);
            Rectangle inner{col.x + 3.0f, col.y + 3.0f,
                            col.width - 6.0f, col.height - 6.0f};
            DrawRectangleRounded(inner, 0.035f, 8, UiTheme::Panel);
            DrawRectangleRoundedLines(col, 0.035f, 8, 1.2f, UiTheme::Iron);
        }
        UiText::DrawFit(titleText, Rectangle{col.x + 14.0f, col.y + 12.0f, col.width - 28.0f, 26.0f}, 23, UiTheme::Parchment);
    };

    auto drawRow = [](Rectangle col, int index, const std::string& label, const std::string& value, Color valueColor = UiTheme::Parchment)
    {
        float y = col.y + 52.0f + index * 30.0f;
        UiText::DrawFit(label, Rectangle{col.x + 14.0f, y, col.width * 0.58f, 22.0f}, 20, UiTheme::ParchmentDim);
        UiText::DrawFit(value, Rectangle{col.x + col.width * 0.58f, y, col.width * 0.38f - 12.0f, 22.0f}, 20, valueColor);
    };

    drawColumn(left, "Population & economy");
    drawRow(left, 0, "Free manpower", std::to_string(stats.freeManpower));
    drawRow(left, 1, "Workers", std::to_string(stats.workers));
    drawRow(left, 2, "Total / cap", std::to_string(stats.totalPopulation) + " / " + std::to_string(stats.populationCap));
    drawRow(left, 3, "Growth", "+" + FormatOneDecimal(stats.manpowerGainPerMinute) + " / min", UiTheme::SageBright);
    drawRow(left, 4, "Food supply", std::to_string(stats.foodSupplyPercent) + "%", stats.foodSupplyPercent < 60 ? UiTheme::RustBright : UiTheme::SageBright);
    drawRow(left, 5, "Food use", FormatOneDecimal(stats.villageFoodConsumptionPerMinute) + " / min");
    drawRow(left, 6, "Worker productivity", std::to_string(stats.workerProductivityPercent) + "%");
    drawRow(left, 7, "Buildings", std::to_string(stats.buildingCount));
    drawRow(left, 8, "Roads", std::to_string(stats.roadCount));
    drawRow(left, 9, "Recruitment buildings", std::to_string(player->GetTrackedBuildingCount(BuildingType::Barracks, true)));
    drawRow(left, 10, "Build commands", std::to_string(player->GetAcceptedCommandCount(GameCommandType::BuildBuilding)));

    drawColumn(chart, showingConsumption ? "Consumption graph" : "Production graph");
    Rectangle plot{chart.x + 46.0f, chart.y + 64.0f, chart.width - 72.0f, chart.height - 142.0f};
    if (!UiControlIcons::DrawPixelHudPanelFrame(plot))
    {
        DrawRectangleRounded(plot, 0.02f, 8, UiTheme::Ink);
        DrawRectangleRounded(Rectangle{plot.x + 2.0f, plot.y + 2.0f,
                                       plot.width - 4.0f, plot.height - 4.0f},
                             0.02f, 8, UiTheme::Inset);
    }
    for (int i = 1; i <= 4; i++)
    {
        float y = plot.y + plot.height * i / 5.0f;
        DrawLineEx(Vector2{plot.x, y}, Vector2{plot.x + plot.width, y}, 1.0f, Fade(UiTheme::Iron, 0.42f));
    }

    double windowSeconds = static_cast<double>(windows[selectedWindowIndex]);
    double startTime = std::max(0.0, historyTime - windowSeconds);
    double tickSeconds = selectedWindowIndex == 0 ? 1.0 : (selectedWindowIndex == 1 ? 5.0 : 15.0);
    double firstTick = std::floor(startTime / tickSeconds) * tickSeconds;
    for (double tick = firstTick; tick <= historyTime; tick += tickSeconds)
    {
        float age = static_cast<float>((historyTime - tick) / windowSeconds);
        float x = plot.x + plot.width - std::clamp(age, 0.0f, 1.0f) * plot.width;
        Color tickColor = std::fmod(tick, tickSeconds * 5.0) < 0.001 ? Fade(UiTheme::Iron, 0.72f) : Fade(UiTheme::Iron, 0.42f);
        DrawLineEx(Vector2{x, plot.y}, Vector2{x, plot.y + plot.height}, 1.0f, tickColor);
    }

    int maxObservedRate = 1;
    std::vector<ResourceType> activeResources;
    for (const auto& [type, rate] : currentRates)
    {
        if (rate > 0)
        {
            maxObservedRate = std::max(maxObservedRate, rate);
            activeResources.push_back(type);
        }
    }
    for (ResourceType type : resourceTypes)
    {
        bool visible = false;
        for (const auto& sample : flowHistory)
        {
            if (sample.time < startTime)
                continue;
            const auto& sampleRates = showingConsumption ? sample.consumptionRatesPerMinute : sample.productionRatesPerMinute;
            auto it = sampleRates.find(type);
            if (it != sampleRates.end() && it->second > 0)
            {
                maxObservedRate = std::max(maxObservedRate, it->second);
                visible = true;
            }
        }
        if (visible && std::find(activeResources.begin(), activeResources.end(), type) == activeResources.end())
            activeResources.push_back(type);
    }
    filterResources.assign(std::begin(resourceTypes), std::end(resourceTypes));

    std::vector<ResourceType> visibleResources;
    for (ResourceType type : filterResources)
    {
        bool active = std::find(activeResources.begin(), activeResources.end(), type) != activeResources.end();
        if ((selectedResources.empty() && active) || selectedResources.contains(type))
            visibleResources.push_back(type);
    }

    int maxRate = std::max(1, static_cast<int>(std::ceil(maxObservedRate * 1.18)));
    struct SeriesEndpoint
    {
        ResourceType type{ResourceType::Null};
        Vector2 point{};
        int rate{0};
        Color color{};
    };
    std::vector<SeriesEndpoint> endpoints;
    std::vector<ResourceFlowSnapshot> chartSamples(flowHistory.begin(), flowHistory.end());
    chartSamples.push_back(economy->economyTelemetry.current);

    BeginScissorMode(static_cast<int>(plot.x), static_cast<int>(plot.y),
                     static_cast<int>(plot.width), static_cast<int>(plot.height));
    for (ResourceType type : visibleResources)
    {
        struct SeriesPoint
        {
            Vector2 position{};
            int rate{0};
        };
        std::vector<SeriesPoint> rawPoints;
        Color color = ResourceChartColor(type);
        for (const auto& sample : chartSamples)
        {
            if (sample.time < startTime)
                continue;
            int rate = 0;
            const auto& sampleRates = showingConsumption ? sample.consumptionRatesPerMinute : sample.productionRatesPerMinute;
            auto it = sampleRates.find(type);
            if (it != sampleRates.end())
                rate = it->second;
            float age = static_cast<float>((historyTime - sample.time) / windowSeconds);
            float x = plot.x + plot.width - std::clamp(age, 0.0f, 1.0f) * plot.width;
            float y = plot.y + plot.height - static_cast<float>(rate) / static_cast<float>(maxRate) * plot.height;
            rawPoints.push_back({Vector2{x, y}, rate});
        }

        if (rawPoints.empty())
            continue;

        // A centered 1-2-1 filter removes one-sample spikes without changing
        // the newest value reported by the endpoint label. Catmull-Rom then
        // rounds the joins, so the graph remains readable without looking
        // like a staircase.
        std::vector<Vector2> smoothPoints;
        smoothPoints.reserve(rawPoints.size());
        for (const auto& point : rawPoints)
            smoothPoints.push_back(point.position);
        for (size_t i = 1; i + 1 < rawPoints.size(); ++i)
        {
            smoothPoints[i].y = (rawPoints[i - 1].position.y +
                                 rawPoints[i].position.y * 2.0f +
                                 rawPoints[i + 1].position.y) * 0.25f;
        }

        if (smoothPoints.size() >= 4)
            DrawSplineCatmullRom(smoothPoints.data(), static_cast<int>(smoothPoints.size()), 2.2f, color);
        else
            for (size_t i = 1; i < smoothPoints.size(); ++i)
                DrawLineEx(smoothPoints[i - 1], smoothPoints[i], 2.2f, color);

        for (size_t i = 0; i < smoothPoints.size(); ++i)
        {
            if (i + 1 != smoothPoints.size() && i % 2 != 0)
                continue;
            DrawCircleV(smoothPoints[i], rawPoints[i].rate > 0 ? 2.4f : 1.7f,
                        rawPoints[i].rate > 0 ? color : Color{110, 92, 70, 190});
        }

        const SeriesPoint& last = rawPoints.back();
        if (last.rate > 0)
            endpoints.push_back({type, last.position, last.rate, color});
    }
    EndScissorMode();

    UiText::DrawFit("0", Rectangle{plot.x - 28.0f, plot.y + plot.height - 16.0f, 24.0f, 16.0f}, 16, Color{190, 172, 140, 255});
    UiText::DrawFit(std::to_string(maxObservedRate) + "/m", Rectangle{plot.x - 42.0f, plot.y - 4.0f, 40.0f, 18.0f}, 16, Color{190, 172, 140, 255});
    UiText::DrawFit("-" + std::string(windowLabels[selectedWindowIndex]), Rectangle{plot.x, plot.y + plot.height + 6.0f, 54.0f, 18.0f}, 16, Color{190, 172, 140, 255});
    UiText::DrawFit("now", Rectangle{plot.x + plot.width - 36.0f, plot.y + plot.height + 6.0f, 36.0f, 18.0f}, 16, Color{190, 172, 140, 255});

    std::sort(endpoints.begin(), endpoints.end(), [](const SeriesEndpoint& a, const SeriesEndpoint& b)
    {
        return a.point.y < b.point.y;
    });
    constexpr int maxEndpointLabels = 8;
    constexpr float endpointChipHeight = 24.0f;
    constexpr float endpointChipGap = 2.0f;
    const size_t endpointCount = std::min(endpoints.size(),
                                          static_cast<size_t>(maxEndpointLabels));
    std::vector<float> endpointLabelYs(endpointCount);
    const float labelTop = plot.y + 4.0f;
    const float labelBottom = plot.y + plot.height - endpointChipHeight - 4.0f;
    const float labelStep = endpointChipHeight + endpointChipGap;
    for (size_t i = 0; i < endpointCount; ++i)
    {
        endpointLabelYs[i] = std::clamp(endpoints[i].point.y - endpointChipHeight * 0.5f,
                                        labelTop, labelBottom);
        if (i > 0)
            endpointLabelYs[i] = std::max(endpointLabelYs[i], endpointLabelYs[i - 1] + labelStep);
    }
    if (!endpointLabelYs.empty() && endpointLabelYs.back() > labelBottom)
    {
        endpointLabelYs.back() = labelBottom;
        for (size_t i = endpointLabelYs.size() - 1; i > 0; --i)
            endpointLabelYs[i - 1] = std::min(endpointLabelYs[i - 1],
                                              endpointLabelYs[i] - labelStep);
    }

    for (size_t i = 0; i < endpointCount; ++i)
    {
        const auto& endpoint = endpoints[i];
        const float labelY = endpointLabelYs[i];

        std::string label = std::string(ResourceChipShortName(endpoint.type)) + " " +
            (showingConsumption ? "-" : "+") + std::to_string(endpoint.rate) + "/m";
        float labelWidth = std::min(132.0f, std::max(76.0f,
            static_cast<float>(UiText::Measure(label, 16) + 24)));
        Rectangle chip{plot.x + plot.width - labelWidth - 6.0f, labelY,
                       labelWidth, endpointChipHeight};
        DrawLineEx(endpoint.point, Vector2{chip.x, chip.y + chip.height * 0.5f}, 1.0f, Color{endpoint.color.r, endpoint.color.g, endpoint.color.b, 180});
        UiControlIcons::DrawPixelHudWidgetFrame(chip, false, endpoint.color);
        DrawRectangleRounded(Rectangle{chip.x + 6.0f, chip.y + 7.0f, 9.0f, 9.0f}, 0.35f, 4, endpoint.color);
        UiText::DrawFit(label, Rectangle{chip.x + 20.0f, chip.y + 3.0f,
                                         chip.width - 25.0f, 18.0f},
                        16, UiTheme::Parchment);
    }

    Rectangle allButton = GetAllFilterButtonRect(chart);
    bool allActive = selectedResources.empty();
    UiControlIcons::DrawPixelHudWidgetFrame(allButton, CheckCollisionPointRec(GetMousePosition(), allButton));
    if (allActive)
        DrawRectangleRounded(Rectangle{allButton.x + 6.0f, allButton.y + allButton.height - 5.0f, allButton.width - 12.0f, 2.0f}, 0.5f, 4, UiTheme::Cyan);
    UiText::DrawFit("All", Rectangle{allButton.x + 4.0f, allButton.y + 6.0f, allButton.width - 8.0f, 18.0f}, 18, UiTheme::Parchment);

    int shown = 0;
    for (ResourceType type : filterResources)
    {
        if (shown >= 18)
            break;
        Rectangle slot = GetFilterButtonRect(chart, shown);
        Color color = ResourceChartColor(type);
        bool active = std::find(activeResources.begin(), activeResources.end(), type) != activeResources.end();
        bool selected = selectedResources.empty() || selectedResources.contains(type);
        UiControlIcons::DrawPixelHudWidgetFrame(slot, CheckCollisionPointRec(GetMousePosition(), slot));
        if (selected && active)
            DrawRectangleRounded(Rectangle{slot.x + 5.0f, slot.y + slot.height - 5.0f, slot.width - 10.0f, 2.0f}, 0.5f, 4, color);
        GuiPanel::DrawResourceIcon(type, Rectangle{slot.x + 5.0f, slot.y + 5.0f, 24.0f, 24.0f});
        DrawRectangleRounded(Rectangle{slot.x + 4.0f, slot.y + slot.height - 6.0f, slot.width - 8.0f, 3.0f}, 0.4f, 4, active ? color : UiTheme::Iron);
        if (CheckCollisionPointRec(GetMousePosition(), slot))
            DrawResourceTooltip(type, {
                active ? (showingConsumption ? "Consumed in selected window" : "Produced in selected window")
                       : (showingConsumption ? "No consumption in selected window" : "No production in selected window"),
                selected ? "Visible on graph" : "Hidden from graph"
            }, 250.0f);
        shown++;
    }
    if (visibleResources.empty())
        UiText::DrawFit(showingConsumption ? "No consumption in selected window" : "No production in selected window", Rectangle{plot.x + 20.0f, plot.y + plot.height * 0.45f, plot.width - 40.0f, 24.0f}, 22, UiTheme::ParchmentDim);
}

// Handles clicks on the stats panel controls.
bool StatsPanelWidget::HandleClick(Vec2i point)
{
    Vector2 mouse{static_cast<float>(point.x), static_cast<float>(point.y)};
    Rectangle modeToggle = GetFlowModeToggleRect();
    if (CheckCollisionPointRec(mouse, modeToggle))
    {
        selectedFlowMode = point.x < modeToggle.x + modeToggle.width * 0.5f ? 0 : 1;
        return true;
    }
    Rectangle spin = GetWindowSpinnerRect();
    if (CheckCollisionPointRec(mouse, spin))
    {
        if (point.x < spin.x + spin.width * 0.5f)
            selectedWindowIndex = (selectedWindowIndex + 2) % 3;
        else
            selectedWindowIndex = (selectedWindowIndex + 1) % 3;
        return true;
    }

    Rectangle chart = GetChartRect();
    if (CheckCollisionPointRec(mouse, GetAllFilterButtonRect(chart)))
    {
        selectedResources.clear();
        return true;
    }

    int index = 0;
    for (ResourceType type : filterResources)
    {
        Rectangle slot = GetFilterButtonRect(chart, index);
        if (CheckCollisionPointRec(mouse, slot))
        {
            if (selectedResources.contains(type))
                selectedResources.erase(type);
            else
                selectedResources.insert(type);
            return true;
        }
        index++;
    }
    return false;
}

// ─── StatsGuiSystem ──────────────────────────────────────────────────────────

// Initializes the full-screen statistics interaction mode.
StatsGuiSystem::StatsGuiSystem(GuiController* con)
    : GuiSystem(con)
{
    scene = dynamic_cast<GameScene*>(owner->scene);

    WireCommonSystemActions(*this, cameraMovement);

    statsPanel.scene = scene;
    statsPanel.ChangePositionAnchor({0.06f, 0.15f});
    statsPanel.ChangeSizeAnchor({0.88f, 0.82f});
    statsPanel.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    SetupStrategicHud(strategicHudWidget, scene);
}

// Rebuilds statistics layout after window size changes.
void StatsGuiSystem::UpdateUiWidgets(Vec2i size)
{
    statsPanel.UpdateSize(size);
    strategicHudWidget.UpdateSize(size);
}

// Draws the statistics overlay and top HUD.
void StatsGuiSystem::Update(double dt)
{
    if (scene->game == nullptr)
        return;

    ApplyStrategicHudCameraPadding(scene);
    MoveCamera(scene, cameraMovement);
    owner->AddUiWidget(&statsPanel);
    owner->AddUiWidget(&strategicHudWidget);
}

// Closes the statistics overlay.
void StatsGuiSystem::EscPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("default");
}

// Switches from statistics to build mode.
void StatsGuiSystem::BuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("build");
}

// Switches from statistics to road build mode.
void StatsGuiSystem::RoadBuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("road_build");
}

// Switches from statistics to destroy mode.
void StatsGuiSystem::DestroyPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("destroy");
}

// Opens the player-wide stockpile panel from statistics mode.
void StatsGuiSystem::StockpilePressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("stockpile");
}

// Toggles the statistics overlay.
void StatsGuiSystem::StatsPressed()
{
    EscPressed();
}

void StatsGuiSystem::FocusPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("focus");
}

void StatsGuiSystem::TechPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("tech");
}

void StatsGuiSystem::RosterPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("roster");
}

// Handles clicks on the statistics overlay.
void StatsGuiSystem::LmbPressed()
{
    if (DispatchHudButtonClick(*this, strategicHudWidget))
        return;

    Vector2 mouse = GetMousePosition();
    Rectangle panelBounds{
        static_cast<float>(statsPanel.pos.x),
        static_cast<float>(statsPanel.pos.y),
        static_cast<float>(statsPanel.size.x),
        static_cast<float>(statsPanel.size.y)};
    if (CheckCollisionPointRec(mouse, PanelCloseButtonRect(panelBounds)))
    {
        EscPressed();
        return;
    }

    statsPanel.HandleClick(Vec2i{static_cast<int>(mouse.x), static_cast<int>(mouse.y)});
}

// Stops left-button interaction.
void StatsGuiSystem::LmbReleased()
{
}

// Starts camera drag behind the statistics overlay.
void StatsGuiSystem::RmbPressed()
{
    cameraMovement.isMoving = true;
}

// Stops camera drag.
void StatsGuiSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

// Zooms camera behind the statistics overlay.
void StatsGuiSystem::Scroll()
{
    ZoomCamera(scene);
}
