// Build, road and destroy interaction modes plus the build panel, tooltip and
// ghost-preview widgets they share.

#include "GuiInternal.h"
#include "economy/BuildingSalvage.h"

#include "ui/ControlIcons.h"
#include "scenes/Scenes.h"
#include "economy/Player.h"
#include "economy/BuildingConfig.h"
#include "economy/ProductionBuildings.h"
#include "economy/StockpileIndex.h"

#include <algorithm>
#include <cmath>

namespace
{
    int CountStoredResource(GameScene* scene, ResourceType type)
    {
        Player* player = GuiLocalPlayer(scene);
        return player != nullptr ? StockpileIndex::GetTotal(*player, type) : 0;
    }

    // Returns a compact label for terrain requirements.
    std::string TileTypeLabel(TileType type)
    {
        switch (type)
        {
            case TileType::WOOD: return "WOOD";
            case TileType::COAL: return "COAL";
            case TileType::IRON_ORE: return "IRON";
            case TileType::STONE: return "STONE";
            case TileType::COPPER_ORE: return "COPPER";
            case TileType::SAND: return "SAND";
            case TileType::CLAY: return "CLAY";
            case TileType::GRASS:
            default: return "GRASS";
        }
    }

    // Inputs/outputs of a building's default recipe, for the build tooltip's
    // icon strip. Buildings with no fixed recipe (Woodcutter/HuntersHut/Mine —
    // production depends on which tile they're built on) fall back to the
    // union of their terrain-specific outputs instead.
    struct RecipeSummary
    {
        std::vector<ResourceAmountDefinition> inputs;
        std::vector<ResourceAmountDefinition> outputs;
        std::vector<TileType> terrainTypes;
        double cycleTime{0.0};
    };

    RecipeSummary GetRecipeSummary(BuildingType type)
    {
        const auto& definition = GetBuildingDefinition(type);
        RecipeSummary summary;
        if (!definition.production.inputs.empty() || !definition.production.outputs.empty())
        {
            summary.inputs = definition.production.inputs;
            summary.outputs = definition.production.outputs;
            summary.cycleTime = definition.production.cycleTime;
            return summary;
        }

        for (const auto& terrain : definition.terrainProductions)
        {
            if (std::find(summary.terrainTypes.begin(), summary.terrainTypes.end(), terrain.tileType) ==
                summary.terrainTypes.end())
                summary.terrainTypes.push_back(terrain.tileType);
            for (const auto& output : terrain.production.outputs)
            {
                bool alreadyListed = std::any_of(summary.outputs.begin(), summary.outputs.end(),
                    [&](const ResourceAmountDefinition& existing) { return existing.type == output.type; });
                if (!alreadyListed)
                    summary.outputs.push_back(output);
            }
            if (summary.cycleTime <= 0.0)
                summary.cycleTime = terrain.production.cycleTime;
        }
        return summary;
    }

    float MeasureRecipeIconRow(const std::vector<ResourceAmountDefinition>& items,
                               int fontSize = 22)
    {
        constexpr float iconSize = 36.0f;
        constexpr float gap = 13.0f;
        float width = 0.0f;
        for (const auto& item : items)
        {
            const float amountWidth = static_cast<float>(UiText::Measure(
                "x" + std::to_string(item.amount), fontSize));
            width += iconSize + 5.0f + amountWidth + gap;
        }
        return std::max(0.0f, width - gap);
    }

    // Draws a clear, one-line material summary. Cost rows retain quantities;
    // production rows can use the same layout with product icons alone.
    float DrawRecipeIconRow(const std::vector<ResourceAmountDefinition>& items, float x, float y,
                            float maxX, int maxCount = 6, bool showAmounts = true,
                            const std::function<bool(const ResourceAmountDefinition&)>& isMissing = {})
    {
        const float iconSize = 36.0f;
        const float gap = 13.0f;
        int shown = std::min<int>(static_cast<int>(items.size()), maxCount);
        for (int i = 0; i < shown; i++)
        {
            // Alegreya Sans does not include the multiplication glyph in the
            // runtime atlas. ASCII x is deliberately used so costs cannot
            // degrade into a question mark on screen.
            std::string amount = showAmounts ? "x" + std::to_string(items[i].amount) : "";
            int amountWidth = showAmounts ? UiText::Measure(amount, 22) : 0;
            float itemWidth = iconSize + (showAmounts ? 5.0f + amountWidth : 0.0f);
            if (x + itemWidth > maxX)
                break;

            Rectangle icon{x, y, iconSize, iconSize};
            const bool missing = isMissing && isMissing(items[i]);
            GuiPanel::DrawResourceIcon(items[i].type, icon);
            if (missing)
            {
                DrawRectangleRounded(icon, 0.16f, 6, Fade(UiTheme::RustBright, 0.20f));
                DrawRectangleRoundedLines(icon, 0.16f, 6, 2.0f, UiTheme::RustBright);
            }
            if (showAmounts)
                UiText::Draw(amount, x + iconSize + 5.0f, y + 6.0f, 22,
                             missing ? UiTheme::RustBright : UiTheme::Parchment);
            x += itemWidth + gap;
        }
        return x;
    }

    int DrawRecipeFlow(const RecipeSummary& recipe, float x, float y, float maxX)
    {
        constexpr float rowHeight = 38.0f;
        constexpr float arrowWidth = 29.0f;
        const float inputWidth = MeasureRecipeIconRow(recipe.inputs);
        const float outputWidth = MeasureRecipeIconRow(recipe.outputs);
        const bool hasInputs = !recipe.inputs.empty();
        const bool hasOutputs = !recipe.outputs.empty();
        const bool oneRow = hasInputs && hasOutputs &&
            x + inputWidth + arrowWidth + outputWidth <= maxX;
        int rows = 0;

        if (oneRow)
        {
            float cursor = DrawRecipeIconRow(recipe.inputs, x, y, maxX, 6, true);
            UiText::Draw("->", cursor + 1.0f, y + 7.0f, 22, UiTheme::ParchmentDim);
            DrawRecipeIconRow(recipe.outputs, cursor + arrowWidth, y, maxX, 6, true);
            return 1;
        }

        if (!recipe.terrainTypes.empty() && !hasInputs)
        {
            std::string terrainText = "Terrain: ";
            for (size_t index = 0; index < recipe.terrainTypes.size(); ++index)
            {
                if (index > 0)
                    terrainText += "/";
                terrainText += TileTypeLabel(recipe.terrainTypes[index]);
            }
            UiText::Draw(terrainText, x, y + 8.0f, 16, UiTheme::Parchment);
            y += rowHeight;
            rows++;
        }

        if (hasInputs)
        {
            DrawRecipeIconRow(recipe.inputs, x, y, maxX, 6, true);
            y += rowHeight;
            rows++;
        }
        if (hasOutputs)
        {
            UiText::Draw("->", x, y + 7.0f, 22, UiTheme::ParchmentDim);
            DrawRecipeIconRow(recipe.outputs, x + arrowWidth, y, maxX, 6, true);
            rows++;
        }
        return std::max(1, rows);
    }

    void DrawBuildingPreviewIcon(GameScene* scene, BuildingType type, Rectangle iconBox, Color tint = WHITE)
    {
        auto textureIt = scene->render.buildingTextures.find(type);
        if (textureIt == scene->render.buildingTextures.end() || !textureIt->second.IsValid())
        {
            DrawRectangleRounded(iconBox, 0.08f, 6, UiTheme::Surface);
            return;
        }

        const Texture2D& texture = textureIt->second.Get();
        Rectangle source = scene->render.GetBuildingTextureFirstFrameSource(type);
        float sourceAspect = source.height > 0.0f ? source.width / source.height : 1.0f;
        float drawWidth = iconBox.width;
        float drawHeight = iconBox.height;
        if (sourceAspect > 1.0f)
            drawHeight = drawWidth / sourceAspect;
        else if (sourceAspect < 1.0f)
            drawWidth = drawHeight * sourceAspect;
        Rectangle destination{iconBox.x + (iconBox.width - drawWidth) * 0.5f,
                              iconBox.y + (iconBox.height - drawHeight) * 0.5f,
                              drawWidth, drawHeight};
        DrawTexturePro(texture, source, destination, {0.0f, 0.0f}, 0.0f, tint);
    }

    // Returns terrain types required by a building, if any.
    std::vector<TileType> RequiredTerrainTypes(BuildingType type)
    {
        if (type == BuildingType::Woodcutter)
            return {TileType::WOOD};

        std::vector<TileType> result;
        const auto& definition = GetBuildingDefinition(type);
        for (const auto& terrainProduction : definition.terrainProductions)
        {
            if (std::find(result.begin(), result.end(), terrainProduction.tileType) == result.end())
                result.push_back(terrainProduction.tileType);
        }
        return result;
    }

    // Returns the build panel category for a building type.
    std::string BuildCategory(BuildingType type)
    {
        switch (type)
        {
            case BuildingType::Woodcutter:
            case BuildingType::LumberMill:
            case BuildingType::Paperworks:
                return "Wood";
            case BuildingType::Mine:
            case BuildingType::Foundry:
            case BuildingType::Smith:
            case BuildingType::Copperworks:
                return "Metals";
            case BuildingType::HuntersHut:
            case BuildingType::Well:
            case BuildingType::WheatFarm:
            case BuildingType::Windmill:
            case BuildingType::Bakery:
            case BuildingType::Inn:
            case BuildingType::AnimalFarm:
            case BuildingType::Butcher:
            case BuildingType::HorseStable:
            case BuildingType::HempFarm:
                return "Food";
            case BuildingType::Tannery:
            case BuildingType::Tailor:
            case BuildingType::Kiln:
            case BuildingType::HouseholdWorkshop:
            case BuildingType::Soapworks:
            case BuildingType::Inkworks:
            case BuildingType::Scriptorium:
            case BuildingType::UrbanWorkshop:
            case BuildingType::Ropery:
            case BuildingType::Weaver:
                return "Crafts";
            case BuildingType::Barracks:
            case BuildingType::Armorer:
            case BuildingType::Bowyer:
            case BuildingType::SpearWorkshop:
            case BuildingType::SiegeWorkshop:
                return "Military";
            case BuildingType::DefenseTower:
                return "Defense";
            case BuildingType::StorageBuilding:
            case BuildingType::Village:
                return "Logistics";
            case BuildingType::University:
                return "Science";
            case BuildingType::Road:
            case BuildingType::Bridge:
                return "Roads";
            default:
                return "Other";
        }
    }

    // Explains the strategic role of a building. Production buildings derive
    // a useful summary from their recipe/terrain data; exceptional buildings
    // get their own gameplay-specific explanation instead of a misleading
    // "No material output" label.
    std::string BuildPurpose(BuildingType type, const BuildingDefinition& definition,
                             const RecipeSummary& recipe)
    {
        switch (type)
        {
            case BuildingType::StorageBuilding:
                return "Stores shared resources for your connected logistics network.";
            case BuildingType::Village:
                return "Adds " + std::to_string(definition.village.populationCap) +
                       " population capacity and generates manpower when supplied with food provisions.";
            case BuildingType::Barracks:
                return "Trains military units using manpower and delivered food, weapons and equipment.";
            case BuildingType::DefenseTower:
                return "Claims territory, protects the border and uses delivered ammunition to fight enemies.";
            case BuildingType::University:
                return "Researches technologies that improve your economy, logistics and military.";
            case BuildingType::Road:
                return "Connects buildings so resources can travel through your logistics network.";
            case BuildingType::Bridge:
                return "Extends roads across water so logistics can reach the far bank.";
            case BuildingType::Headquarters:
                return "The core of your settlement: stores shared resources and must be defended.";
            default:
                break;
        }

        if (!definition.terrainProductions.empty())
            return "Extracts resources from the terrain beneath this building.";
        if (!recipe.inputs.empty() && !recipe.outputs.empty())
            return "Processes delivered materials into the resources shown below.";
        if (!recipe.outputs.empty())
            return "Produces the resources shown below for your economy.";
        return "Supports the growth and operation of your settlement.";
    }

    // Returns category display order for build panel grouping.
    int BuildCategoryOrder(const std::string& category)
    {
        static const std::vector<std::string> order{"Wood", "Metals", "Food", "Crafts", "Logistics", "Military", "Defense", "Science", "Roads", "Other"};
        auto it = std::find(order.begin(), order.end(), category);
        return it != order.end() ? static_cast<int>(std::distance(order.begin(), it)) : static_cast<int>(order.size());
    }

    // Returns current build lock reasons for the local player.
    std::vector<std::string> BuildLockReasons(GameScene* scene, const BuildOption& option)
    {
        Player* player = GuiLocalPlayer(scene);
        if (player == nullptr)
            return {"No local player"};

        const auto& definition = GetBuildingDefinition(option.buildingType);
        return player->GetBuildRequirementFailures(definition, false);
    }

    // Build tooltips follow the recruitment tooltip's compact visual grammar:
    // one large subject icon, title/costs in the right header column, then
    // the building-specific detail rows below a shared divider.
    void DrawBuildTooltip(GameScene* scene, const BuildOption& option, Vec2i hoveredTile)
    {
        const Color ok = UiTheme::SageBright;
        const Color missing = UiTheme::RustBright;
        const Color muted = UiTheme::ParchmentDim;

        auto lockReasons = BuildLockReasons(scene, option);
        // The cost row identifies the exact missing materials visually. Keep
        // only non-resource gates in the textual lock section.
        lockReasons.erase(std::remove(lockReasons.begin(), lockReasons.end(), "Not enough resources"),
                          lockReasons.end());
        auto requiredTerrain = RequiredTerrainTypes(option.buildingType);
        std::string terrainLabel;
        Color terrainColor = ok;
        if (!requiredTerrain.empty())
        {
            bool terrainOk = scene != nullptr && scene->game != nullptr && hoveredTile.x >= 0 && hoveredTile.y >= 0 &&
                scene->game->GetTileMap().HasRequiredTerrainForBuilding(option.buildingType, hoveredTile, option.footprint, 2);
            terrainLabel = "Requires: ";
            for (size_t i = 0; i < requiredTerrain.size(); i++)
            {
                if (i > 0)
                    terrainLabel += "/";
                terrainLabel += TileTypeLabel(requiredTerrain[i]);
            }
            terrainColor = terrainOk ? ok : missing;
        }

        const auto& definition = GetBuildingDefinition(option.buildingType);
        Player* player = GuiLocalPlayer(scene);
        const std::vector<ResourceAmountDefinition> effectiveBuildCosts = player != nullptr
            ? player->GetEffectiveBuildCosts(definition)
            : option.buildCosts;
        RecipeSummary recipe = GetRecipeSummary(option.buildingType);
        const std::string purpose = BuildPurpose(option.buildingType, definition, recipe);
        const float width = 520.0f;
        const float padding = 14.0f;
        const float titleIconSize = 96.0f;
        const float headerHeight = 112.0f;
        const float materialRowH = 38.0f;
        const float infoRowH = 26.0f;
        const float labelWidth = 96.0f;
        const float detailWidth = width - padding * 2.0f - labelWidth - 8.0f;
        const bool showProduction = !recipe.inputs.empty() || !recipe.outputs.empty() ||
                                    !recipe.terrainTypes.empty();
        int productionRows = 0;
        if (showProduction)
        {
            productionRows = 1;
            const bool flowNeedsWrap = !recipe.inputs.empty() && !recipe.outputs.empty() &&
                MeasureRecipeIconRow(recipe.inputs) + MeasureRecipeIconRow(recipe.outputs) +
                29.0f > detailWidth;
            const bool terrainNeedsOwnRow = recipe.inputs.empty() &&
                !recipe.terrainTypes.empty() && !recipe.outputs.empty();
            if (flowNeedsWrap || terrainNeedsOwnRow)
                productionRows = 2;
        }
        const bool showFunction = !showProduction && !purpose.empty();
        const std::vector<std::string> purposeLines = showFunction
            ? UiText::Wrap(purpose, 20, detailWidth)
            : std::vector<std::string>{};
        const float bodyHeight =
                                 (showFunction ? static_cast<float>(purposeLines.size()) * infoRowH : 0.0f) +
                                 (showProduction ? materialRowH * std::max(1, productionRows) : 0.0f) +
                                 infoRowH +
                                 (!terrainLabel.empty() ? infoRowH : 0.0f) +
                                 (lockReasons.empty() ? 0.0f :
                                     infoRowH * static_cast<float>(lockReasons.size() + 1));
        const float height = padding + headerHeight + 8.0f + bodyHeight + padding;

        Vector2 mouse = GetMousePosition();
        Rectangle box{
            mouse.x + 18.0f,
            mouse.y + 18.0f,
            width,
            height};
        box.x = std::clamp(box.x, 10.0f,
                           std::max(10.0f, static_cast<float>(GetScreenWidth()) - box.width - 10.0f));
        if (box.y + box.height > GetScreenHeight() - 10.0f)
            box.y = std::max(10.0f, mouse.y - box.height - 18.0f);

        if (!UiControlIcons::DrawPixelHudPanelFrame(box))
        {
            DrawRectangleRounded(box, 0.06f, 8, UiTheme::Inset);
            DrawRectangleRoundedLines(box, 0.06f, 8, 1.0f, UiTheme::Iron);
            Rectangle inner{box.x + 3.0f, box.y + 3.0f,
                            box.width - 6.0f, box.height - 6.0f};
            DrawRectangleRounded(inner, 0.045f, 8, Fade(UiTheme::Surface, 0.99f));
            DrawRectangleRoundedLines(inner, 0.045f, 8, 1.0f,
                                      Fade(UiTheme::Bronze, 0.68f));
        }

        Rectangle portrait{box.x + padding, box.y + padding, titleIconSize, titleIconSize};
        DrawBuildingPreviewIcon(scene, option.buildingType, portrait);
        const float detailsX = portrait.x + portrait.width + 12.0f;
        const float detailsRight = box.x + box.width - padding;
        {
            UiFontRoleScope displayRole{UiFontRole::Display};
            Rectangle titleArea{
                detailsX,
                box.y + padding,
                detailsRight - detailsX,
                34.0f};
            UiText::DrawFit(option.name, titleArea, 28, UiTheme::Parchment);
        }

        float separatorY = box.y + padding + 36.0f;
        DrawLineEx(Vector2{detailsX, separatorY}, Vector2{detailsRight, separatorY},
                   1.0f, UiTheme::Iron);
        const float costY = separatorY + 7.0f;
        UiText::Draw("Cost", detailsX, costY + 5.0f, 16, muted);
        float costX = detailsX + 39.0f;
        const float costItemWidth = std::min(
            62.0f, (detailsRight - costX) /
                       std::max(1.0f, static_cast<float>(effectiveBuildCosts.size())));
        auto drawCost = [&](const ResourceAmountDefinition& cost)
        {
            constexpr float iconSize = 31.0f;
            const bool missingCost = CountStoredResource(scene, cost.type) < cost.amount;
            Rectangle icon{costX, costY - 2.0f, iconSize, iconSize};
            GuiPanel::DrawResourceIcon(cost.type, icon);
            if (missingCost)
                DrawRectangleRec(icon, Fade(UiTheme::RustBright, 0.24f));
            UiText::Draw("x" + std::to_string(cost.amount), costX + iconSize + 2.0f,
                         costY + 5.0f, 15,
                         missingCost ? UiTheme::RustBright : UiTheme::Parchment);
            costX += costItemWidth;
        };
        if (effectiveBuildCosts.empty())
            UiText::Draw("Free", costX, costY + 5.0f, 16, ok);
        else
            for (const auto& cost : effectiveBuildCosts)
                drawCost(cost);

        separatorY = box.y + padding + headerHeight;
        DrawLineEx(Vector2{box.x + padding, separatorY},
                   Vector2{box.x + box.width - padding, separatorY},
                   1.0f, UiTheme::Iron);
        float y = separatorY + 8.0f;

        if (showFunction)
        {
            UiText::Draw("Function", box.x + padding, y + 2.0f, 18, muted);
            for (const std::string& line : purposeLines)
            {
                UiText::Draw(line, box.x + padding + labelWidth, y + 2.0f,
                             17, UiTheme::Parchment);
                y += infoRowH;
            }
        }

        if (showProduction)
        {
            UiText::Draw("Production", box.x + padding, y + 9.0f, 18, muted);
            const int drawnRows = DrawRecipeFlow(recipe,
                box.x + padding + labelWidth, y,
                box.x + box.width - padding);
            y += materialRowH * drawnRows;
        }

        std::string timings = "Build: " + FormatOneDecimal(option.buildTime) + "s";
        if (recipe.cycleTime > 0.0)
            timings += "    Production cycle: " + FormatOneDecimal(recipe.cycleTime) + "s";
        UiText::Draw(timings, box.x + padding, y + 2.0f, 17, muted);
        y += infoRowH;

        if (!terrainLabel.empty())
        {
            UiText::Draw(terrainLabel, box.x + padding, y + 2.0f, 17, terrainColor);
            y += infoRowH;
        }
        if (!lockReasons.empty())
        {
            UiText::Draw("Locked", box.x + padding, y + 2.0f, 17, missing);
            y += infoRowH;
            for (const auto& reason : lockReasons)
            {
                UiText::Draw(reason, box.x + padding + 12.0f, y + 2.0f, 17, missing);
                y += infoRowH;
            }
        }
    }

    // Creates a build option for one concrete building class.
    template <typename T>
    BuildOption MakeBuildOption(GameScene* scene, const BuildingDefinition& definition)
    {
        static_assert(std::is_base_of<Building, T>::value);

        BuildOption option;
        option.name = definition.name;
        option.costText = definition.buildCostText;
        option.buildCosts = definition.buildCosts;
        option.buildingType = definition.type;
        option.footprint = definition.footprint;
        option.buildTime = definition.buildTime;
        option.category = BuildCategory(definition.type);
        option.previewFactory = []()
        {
            return std::make_unique<T>(0);
        };
        option.buildAt = [scene, type = definition.type](Vec2i tilePos)
        {
            scene->SubmitLocalCommand(GameCommand::BuildBuilding(scene->game->GetLocalPlayerId(), type, tilePos));
        };

        return option;
    }

    // Creates a build option for a building type from its data definition.
    BuildOption MakeBuildOption(GameScene* scene, BuildingType type)
    {
        const auto& definition = GetBuildingDefinition(type);
        switch (type)
        {
            case BuildingType::Woodcutter: return MakeBuildOption<Woodcutter>(scene, definition);
            case BuildingType::HuntersHut: return MakeBuildOption<HuntersHut>(scene, definition);
            case BuildingType::LumberMill: return MakeBuildOption<LumberMill>(scene, definition);
            case BuildingType::Mine: return MakeBuildOption<Mine>(scene, definition);
            case BuildingType::Foundry: return MakeBuildOption<Foundry>(scene, definition);
            case BuildingType::Well: return MakeBuildOption<Well>(scene, definition);
            case BuildingType::WheatFarm: return MakeBuildOption<WheatFarm>(scene, definition);
            case BuildingType::Windmill: return MakeBuildOption<Windmill>(scene, definition);
            case BuildingType::Bakery: return MakeBuildOption<Bakery>(scene, definition);
            case BuildingType::Inn: return MakeBuildOption<Inn>(scene, definition);
            case BuildingType::Paperworks: return MakeBuildOption<Paperworks>(scene, definition);
            case BuildingType::Smith: return MakeBuildOption<Smith>(scene, definition);
            case BuildingType::Mint: return MakeBuildOption<Mint>(scene, definition);
            case BuildingType::Glassworks: return MakeBuildOption<Glassworks>(scene, definition);
            case BuildingType::Powderworks: return MakeBuildOption<Powderworks>(scene, definition);
            case BuildingType::University: return MakeBuildOption<University>(scene, definition);
            case BuildingType::StorageBuilding: return MakeBuildOption<StorageBuilding>(scene, definition);
            case BuildingType::Village: return MakeBuildOption<Village>(scene, definition);
            case BuildingType::Barracks: return MakeBuildOption<Barracks>(scene, definition);
            case BuildingType::DefenseTower: return MakeBuildOption<DefenseTower>(scene, definition);
            case BuildingType::Road: return MakeBuildOption<Road>(scene, definition);
            case BuildingType::Bridge: return MakeBuildOption<Bridge>(scene, definition);
            case BuildingType::AnimalFarm:
            case BuildingType::Butcher:
            case BuildingType::Tannery:
            case BuildingType::Tailor:
            case BuildingType::Armorer:
            case BuildingType::HorseStable:
            case BuildingType::Kiln:
            case BuildingType::HouseholdWorkshop:
            case BuildingType::Soapworks:
            case BuildingType::Inkworks:
            case BuildingType::Scriptorium:
            case BuildingType::Copperworks:
            case BuildingType::UrbanWorkshop:
            case BuildingType::HempFarm:
            case BuildingType::Ropery:
            case BuildingType::Weaver:
            case BuildingType::Bowyer:
            case BuildingType::SpearWorkshop:
            case BuildingType::SiegeWorkshop:
            {
                BuildOption option;
                option.name = definition.name;
                option.costText = definition.buildCostText;
                option.buildCosts = definition.buildCosts;
                option.buildingType = type;
                option.footprint = definition.footprint;
                option.buildTime = definition.buildTime;
                option.category = BuildCategory(type);
                option.previewFactory = [type]()
                {
                    return std::make_unique<ConfiguredProductionBuilding>(0, type);
                };
                option.buildAt = [scene, type](Vec2i tilePos)
                {
                    scene->SubmitLocalCommand(GameCommand::BuildBuilding(scene->game->GetLocalPlayerId(), type, tilePos));
                };
                return option;
            }
            default: return {};
        }
    }

    // Sorts build options into stable gameplay categories.
    void SortBuildOptions(std::vector<BuildOption>& options)
    {
        std::stable_sort(options.begin(), options.end(), [](const BuildOption& lhs, const BuildOption& rhs)
        {
            int leftOrder = BuildCategoryOrder(lhs.category);
            int rightOrder = BuildCategoryOrder(rhs.category);
            if (leftOrder != rightOrder)
                return leftOrder < rightOrder;
            return lhs.name < rhs.name;
        });
    }
}

void RoadDragStabilizer::Begin(Vec2i tile, Vector2 mousePosition)
{
    active = true;
    axis = Axis::None;
    segmentAnchor = tile;
    gestureOriginMouse = mousePosition;
    lastMousePosition = mousePosition;
    stationaryTime = 0.0;
}

void RoadDragStabilizer::End()
{
    active = false;
    axis = Axis::None;
    segmentAnchor = {-9999, -9999};
    stationaryTime = 0.0;
}

Vec2i RoadDragStabilizer::Constrain(Vec2i rawTile, Vector2 mousePosition, double dt,
                                    Vec2i lastPlacedTile)
{
    if (!active)
        return rawTile;

    const float frameDx = mousePosition.x - lastMousePosition.x;
    const float frameDy = mousePosition.y - lastMousePosition.y;
    const float frameDistanceSquared = frameDx * frameDx + frameDy * frameDy;
    if (frameDistanceSquared <= StationaryPixelTolerance * StationaryPixelTolerance)
        stationaryTime += std::max(0.0, dt);
    else
        stationaryTime = 0.0;
    lastMousePosition = mousePosition;

    if (axis != Axis::None && stationaryTime >= TurnPauseSeconds)
    {
        axis = Axis::None;
        if (lastPlacedTile.x > -9000 && lastPlacedTile.y > -9000)
            segmentAnchor = lastPlacedTile;
        gestureOriginMouse = mousePosition;
        stationaryTime = 0.0;
    }

    if (axis == Axis::None)
    {
        const float dx = mousePosition.x - gestureOriginMouse.x;
        const float dy = mousePosition.y - gestureOriginMouse.y;
        if (std::max(std::abs(dx), std::abs(dy)) >= DirectionLockPixels)
            axis = std::abs(dx) > std::abs(dy) ? Axis::Horizontal : Axis::Vertical;
        else
            return segmentAnchor;
    }

    return axis == Axis::Horizontal
        ? Vec2i{rawTile.x, segmentAnchor.y}
        : Vec2i{segmentAnchor.x, rawTile.y};
}

// ─── BuildPanelWidget ────────────────────────────────────────────────────────

// Draws available build options grouped by category.
void BuildPanelWidget::Update(double dt)
{
    if (scene == nullptr || options == nullptr)
        return;

    Rectangle bounds{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
    Vector2 mouse = GetMousePosition();
    if (!UiControlIcons::DrawPixelHudFrame(bounds))
    {
        DrawRectangleRounded(bounds, 0.025f, 8, UiTheme::Panel);
        DrawRectangleRoundedLines(bounds, 0.025f, 8, 1.0f, UiTheme::Iron);
    }

    int margin = std::max(9, size.x / 64);
    int titleBar = std::max(58, size.y / 14);
    const float chromeInset = UiControlIcons::PixelHudFrameInset(bounds);
    Rectangle titleBounds{bounds.x + chromeInset + 2.0f, bounds.y + 8.0f,
                          bounds.width - (chromeInset + 2.0f) * 2.0f,
                          std::max(42.0f, static_cast<float>(titleBar) - 10.0f)};
    Rectangle closeButton = PanelCloseButtonRect(bounds);
    if (CheckCollisionPointRec(mouse, titleBounds) &&
        !CheckCollisionPointRec(mouse, closeButton) &&
        InputManager::IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        dragging = true;
        dragOffset = Vec2i{static_cast<int>(mouse.x) - pos.x, static_cast<int>(mouse.y) - pos.y};
    }
    if (dragging && InputManager::IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        pos.x = std::clamp(static_cast<int>(mouse.x) - dragOffset.x, 0, std::max(0, GetScreenWidth() - size.x));
        pos.y = std::clamp(static_cast<int>(mouse.y) - dragOffset.y, 0, std::max(0, GetScreenHeight() - size.y));
        bounds = Rectangle{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(size.x), static_cast<float>(size.y)};
        titleBounds = Rectangle{bounds.x + chromeInset + 2.0f, bounds.y + 8.0f,
                                bounds.width - (chromeInset + 2.0f) * 2.0f,
                                std::max(42.0f, static_cast<float>(titleBar) - 10.0f)};
        closeButton = PanelCloseButtonRect(bounds);
    }
    if (dragging && InputManager::IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        dragging = false;

    UiText::DrawTitleBar(titleBounds, title, PanelTitleCloseReserve(bounds));
    DrawCloseButton(bounds);

    int columns = 3;
    float gap = 7.0f;
    float scrollbarW = 8.0f;
    float viewportTop = bounds.y + titleBar + margin;
    const float bottomPadding = std::max(22.0f, margin * 2.0f);
    float viewportBottom = bounds.y + bounds.height - bottomPadding;
    // The scroll widget is deliberately inset from the outer panel chrome.
    // Keeping this separate from the grid's inner gutters prevents the frame
    // from looking glued to the panel edge even when a scrollbar is present.
    const float scrollBoxInset = static_cast<float>(margin) + 12.0f;
    Rectangle scrollBox{bounds.x + scrollBoxInset,
                        viewportTop - 5.0f,
                        bounds.width - scrollBoxInset * 2.0f,
                        viewportBottom - viewportTop + 10.0f};
    UiControlIcons::DrawPixelHudPanelFrame(scrollBox);
    const float gridLeftInset = 10.0f;
    const float gridRightInset = scrollbarW + 14.0f;
    const float gridX = scrollBox.x + gridLeftInset;
    float contentW = scrollBox.width - gridLeftInset - gridRightInset;
    float cardW = (contentW - gap * (columns - 1)) / columns;
    float cardH = std::max(108.0f, cardW * 0.92f);
    float headerH = 32.0f;
    float categoryGap = 5.0f;
    float startY = viewportTop - scrollOffset;
    int hoveredOption = -1;

    BeginScissorMode(static_cast<int>(gridX), static_cast<int>(viewportTop),
                     static_cast<int>(contentW),
                     static_cast<int>(viewportBottom - viewportTop));
    std::string currentCategory;
    float yCursor = startY;
    int col = 0;
    float contentBottom = viewportTop;
    for (size_t i = 0; i < options->size(); i++)
    {
        const auto& option = (*options)[i];
        if (option.category != currentCategory)
        {
            if (!currentCategory.empty() && col != 0)
                yCursor += cardH + gap;
            currentCategory = option.category;
            Rectangle header{
                gridX,
                yCursor,
                contentW,
                headerH};
            if (header.y + header.height >= viewportTop && header.y <= viewportBottom)
            {
                UiControlIcons::DrawPixelHudPanelFrame(header);
                UiFontRoleScope displayRole{UiFontRole::Display};
                int headerFont = 21;
                int headerWidth = UiText::Measure(currentCategory, headerFont);
                UiText::Draw(currentCategory,
                             header.x + (header.width - headerWidth) * 0.5f,
                             header.y + (header.height - headerFont) * 0.5f,
                             headerFont,
                             UiTheme::Parchment);
            }
            contentBottom = std::max(contentBottom, header.y + header.height + scrollOffset);
            yCursor += headerH + categoryGap;
            col = 0;
        }

        Rectangle card{
            gridX + col * (cardW + gap),
            yCursor,
            cardW,
            cardH};
        contentBottom = std::max(contentBottom, card.y + card.height + scrollOffset);
        bool visible = card.y + card.height >= viewportTop && card.y <= viewportBottom;
        if (!visible)
        {
            col++;
            if (col >= columns)
            {
                col = 0;
                yCursor += cardH + gap;
            }
            continue;
        }

        bool selected = selectedIndex < options->size() && static_cast<size_t>(i) == selectedIndex;
        auto lockReasons = BuildLockReasons(scene, option);
        bool locked = !lockReasons.empty();
        if (!UiControlIcons::DrawPixelHudWidgetFrame(card, selected && !locked,
                                                     locked ? Color{130, 120, 110, 175} : WHITE))
        {
            DrawRectangleRounded(card, 0.04f, 8, locked ? UiTheme::Ink : (selected ? UiTheme::SelectedFill : UiTheme::Inset));
            DrawRectangleRoundedLines(card, 0.04f, 8, 1.0f, locked ? UiTheme::Iron : (selected ? UiTheme::SageBright : UiTheme::Iron));
        }

        // Keep the card itself intentionally sparse: the building name is
        // legible before its artwork, while costs live in the richer hover
        // tooltip instead of competing with the icon at card scale.
        UiText::DrawFit(option.name,
        Rectangle{card.x + 10.0f, card.y + 6.0f, card.width - 20.0f, 26.0f},
                        19, locked ? UiTheme::ParchmentDim : UiTheme::Parchment);
        float icon = std::min(card.width - 36.0f, card.height - 50.0f);
        Rectangle iconBox{card.x + (card.width - icon) * 0.5f, card.y + 36.0f, icon, icon};
        DrawBuildingPreviewIcon(scene, option.buildingType, iconBox,
                                locked ? Color{110, 92, 70, 145} : WHITE);

        if (CheckCollisionPointRec(mouse, card))
            hoveredOption = static_cast<int>(i);

        col++;
        if (col >= columns)
        {
            col = 0;
            yCursor += cardH + gap;
        }
    }
    EndScissorMode();

    maxScrollOffset = std::max(0.0f, contentBottom - viewportBottom);
    scrollOffset = std::clamp(scrollOffset, 0.0f, maxScrollOffset);
    if (maxScrollOffset > 0.0f)
    {
        Rectangle track{scrollBox.x + scrollBox.width - 10.0f - scrollbarW,
                        viewportTop, scrollbarW,
                        viewportBottom - viewportTop};
        DrawRectangleRounded(track, 0.5f, 4, UiTheme::Inset);
        float thumbH = std::max(28.0f, track.height * (track.height / (track.height + maxScrollOffset)));
        float thumbY = track.y + (track.height - thumbH) * (scrollOffset / maxScrollOffset);
        Rectangle thumb{track.x, thumbY, track.width, thumbH};
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
        DrawRectangleRounded(Rectangle{track.x, thumbY, track.width, thumbH}, 0.5f, 4, UiTheme::Iron);
    }

    if (hoveredOption >= 0)
    {
        const auto& option = (*options)[hoveredOption];
        DrawBuildTooltip(scene, option, hoveredTile);
    }
}

// Scrolls this build list by mouse wheel steps.
void BuildPanelWidget::Scroll(float wheel)
{
    if (wheel == 0.0f)
        return;

    scrollOffset = std::clamp(scrollOffset - wheel * 42.0f, 0.0f, maxScrollOffset);
}

// Returns the build option index under a screen point.
int BuildPanelWidget::GetOptionAt(Vec2i point) const
{
    if (options == nullptr)
        return -1;

    int margin = std::max(9, size.x / 64);
    int titleBar = std::max(58, size.y / 14);
    float viewportTop = pos.y + titleBar + margin;
    const float bottomPadding = std::max(22.0f, margin * 2.0f);
    float viewportBottom = pos.y + size.y - bottomPadding;
    if (point.y < viewportTop || point.y > viewportBottom)
        return -1;
    int columns = 3;
    float gap = 7.0f;
    float scrollbarW = 8.0f;
    const float scrollBoxInset = static_cast<float>(margin) + 12.0f;
    const float gridLeftInset = 10.0f;
    const float gridRightInset = scrollbarW + 14.0f;
    const float gridX = static_cast<float>(pos.x) + scrollBoxInset + gridLeftInset;
    float contentW = static_cast<float>(size.x) - scrollBoxInset * 2.0f - gridLeftInset - gridRightInset;
    float cardW = (contentW - gap * (columns - 1)) / columns;
    float cardH = std::max(108.0f, cardW * 0.92f);
    float headerH = 32.0f;
    float categoryGap = 5.0f;
    float startY = pos.y + titleBar + margin - scrollOffset;

    std::string currentCategory;
    float yCursor = startY;
    int col = 0;
    for (size_t i = 0; i < options->size(); i++)
    {
        const auto& option = (*options)[i];
        if (option.category != currentCategory)
        {
            if (!currentCategory.empty() && col != 0)
                yCursor += cardH + gap;
            currentCategory = option.category;
            yCursor += headerH + categoryGap;
            col = 0;
        }

        Rectangle card{
            gridX + col * (cardW + gap),
            yCursor,
            cardW,
            cardH};
        if (CheckCollisionPointRec(Vector2{static_cast<float>(point.x), static_cast<float>(point.y)}, card))
            return static_cast<int>(i);

        col++;
        if (col >= columns)
        {
            col = 0;
            yCursor += cardH + gap;
        }
    }
    return -1;
}

// ─── BuildGhostWidget ────────────────────────────────────────────────────────

// Draws the placement preview and validity tint under the cursor.
void BuildGhostWidget::Update(double dt)
{
    if (scene == nullptr || scene->game == nullptr || selectedOption == nullptr)
        return;
    if (tilePos.x < 0 || tilePos.y < 0)
        return;

    Vec2i footprint = selectedOption->footprint;
    Vec2f worldTopLeft{
        static_cast<float>(tilePos.x * TILE_SIZE),
        static_cast<float>(tilePos.y * TILE_SIZE)};
    Vec2f worldBottomRight{
        worldTopLeft.x + footprint.x * TILE_SIZE,
        worldTopLeft.y + footprint.y * TILE_SIZE};

    Vec2f screenTopLeft = scene->render.WorldToScreen(worldTopLeft);
    Vec2f screenBottomRight = scene->render.WorldToScreen(worldBottomRight);
    Rectangle dest{
        screenTopLeft.x,
        screenTopLeft.y,
        screenBottomRight.x - screenTopLeft.x,
        screenBottomRight.y - screenTopLeft.y};

    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(GetTime()) * 5.0f);
    Color tint = canBuild ? Color{88, 196, 124, 62} : Color{220, 80, 80, 70};
    Color outline = canBuild ? Color{112, 230, 150, 180} : Color{240, 110, 110, 190};
    outline.a = static_cast<unsigned char>(150.0f + pulse * 105.0f);
    DrawRectangleRounded(dest, 0.04f, 8, tint);
    Color halo = outline;
    halo.a = static_cast<unsigned char>(45.0f + pulse * 60.0f);
    DrawRectangleRoundedLines(dest, 0.04f, 8, 3.0f, halo);
    DrawRectangleRoundedLines(dest, 0.04f, 8, 1.25f, outline);

    auto textureIt = scene->render.buildingTextures.find(selectedOption->buildingType);
    if (textureIt != scene->render.buildingTextures.end() && textureIt->second.IsValid())
    {
        const Texture2D& tex = textureIt->second.Get();
        // Match the build-card preview: animated horizontal strips show only
        // their first frame, while static building canvases stay intact.
        Rectangle src = scene->render.GetBuildingTextureFirstFrameSource(selectedOption->buildingType);
        DrawTexturePro(tex, src, dest, {0.0f, 0.0f}, 0.0f, Color{255, 255, 255, static_cast<unsigned char>(canBuild ? 170 : 120)});
    }
}

// ─── BuildGuiSystem ──────────────────────────────────────────────────────────

BuildGuiSystem::BuildGuiSystem(GuiController* con)
    : GuiSystem(con)
{
    // A4 (docs/work_plan_2026-07-13.md): shadows GuiSystem::scene (Scene*) —
    // also inherited as-is by RoadBuildSystem.
    scene = dynamic_cast<GameScene*>(owner->scene);

    WireCommonSystemActions(*this, cameraMovement);

    buildPanel.ChangePositionAnchor({0.61f, 0.13f});
    buildPanel.ChangeSizeAnchor({0.38f, 0.82f});
    buildPanel.scene = scene;
    buildPanel.options = &options;
    buildPanel.title = "Build";
    buildPanel.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    SetupStrategicHud(strategicHudWidget, scene);

    for (BuildingType type : GetBuildableBuildingTypes())
        options.push_back(MakeBuildOption(scene, type));
    SortBuildOptions(options);

    ghostWidget.scene = scene;
}

// Applies window size changes to build-mode widgets.
void BuildGuiSystem::UpdateUiWidgets(Vec2i size)
{
    buildPanel.UpdateSize(size);
    strategicHudWidget.UpdateSize(size);
}

// Updates camera drag, build panel and ghost preview.
void BuildGuiSystem::Update(double dt)
{
    if (scene->game == nullptr)
        return;

    ApplyStrategicHudCameraPadding(scene);
    MoveCamera(scene, cameraMovement);
    RefreshGhost();
    buildPanel.hoveredTile = ghostWidget.tilePos;
    owner->AddUiWidget(&ghostWidget);
    owner->AddUiWidget(&buildPanel);
    owner->AddUiWidget(&strategicHudWidget);
    owner->AddUiWidget(owner->GetGameplayClockWidget());
}

// Cancels build mode and returns to map view.
void BuildGuiSystem::EscPressed()
{
    ReturnToMapView();
}

// Toggles build mode off.
void BuildGuiSystem::BuildPressed()
{
    ReturnToMapView();
}

// Switches to road build mode.
void BuildGuiSystem::RoadBuildPressed()
{
    owner->ChangeSystem("road_build");
}

// Switches to destroy mode.
void BuildGuiSystem::DestroyPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("destroy");
}

// Opens the player-wide stockpile panel from build mode.
void BuildGuiSystem::StockpilePressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("stockpile");
}

// Opens the statistics panel from build mode.
void BuildGuiSystem::StatsPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("stats");
}

void BuildGuiSystem::FocusPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("focus");
}

void BuildGuiSystem::TechPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("tech");
}

void BuildGuiSystem::RosterPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("roster");
}

// Selects a build option or places the selected building.
void BuildGuiSystem::LmbPressed()
{
    auto mousePos = GetMousePosition();
    Vec2i screenPos{static_cast<int>(mousePos.x), static_cast<int>(mousePos.y)};
    if (DispatchHudButtonClick(*this, strategicHudWidget))
        return;

    if (buildPanel.ContainsPoint(screenPos))
    {
        Rectangle panelBounds{
            static_cast<float>(buildPanel.pos.x),
            static_cast<float>(buildPanel.pos.y),
            static_cast<float>(buildPanel.size.x),
            static_cast<float>(buildPanel.size.y)};
        if (CheckCollisionPointRec(mousePos, PanelCloseButtonRect(panelBounds)))
        {
            ReturnToMapView();
            return;
        }

        int option = buildPanel.GetOptionAt(screenPos);
        if (option >= 0)
            SelectOption(static_cast<size_t>(option));
        return;
    }

    TryPlaceSelectedAtHovered(true);
}

void BuildGuiSystem::LmbReleased()
{
}

// Starts camera drag.
void BuildGuiSystem::RmbPressed()
{
    cameraMovement.isMoving = true;
}

// Stops camera drag.
void BuildGuiSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

// Scrolls the build panel or zooms the camera.
void BuildGuiSystem::Scroll()
{
    Vector2 mouse = GetMousePosition();
    Vec2i screenPos{static_cast<int>(mouse.x), static_cast<int>(mouse.y)};
    if (buildPanel.ContainsPoint(screenPos))
    {
        buildPanel.Scroll(InputManager::GetMouseWheelMove());
        return;
    }

    ZoomCamera(scene);
}

// Switches controller back to default map view.
void BuildGuiSystem::ReturnToMapView()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("default");
}

// Returns the tile currently targeted by the build cursor.
Vec2i BuildGuiSystem::GetHoveredTile() const
{
    auto mousePos = GetMousePosition();
    Vec2f worldPos = scene->render.ScreenToWorld(mousePos);
    if (worldPos.x < 0.0f || worldPos.y < 0.0f)
        return {-1, -1};

    Vec2i footprint{1, 1};
    if (selectedPreview != nullptr)
        footprint = selectedPreview->GetFootprint();

    return Vec2i{
        static_cast<int>(std::floor(worldPos.x / TILE_SIZE - (footprint.x - 1) * 0.5f)),
        static_cast<int>(std::floor(worldPos.y / TILE_SIZE - (footprint.y - 1) * 0.5f))};
}

// Returns true when the selected building can be placed at a tile.
bool BuildGuiSystem::CanPlaceSelected(Vec2i tilePos) const
{
    if (selectedPreview == nullptr || scene->game == nullptr)
        return false;
    if (!scene->game->GetTileMap().IsInsideFootprint(tilePos, selectedPreview->GetFootprint()))
        return false;

    Player* player = GuiLocalPlayer(scene);
    if (player == nullptr)
        return false;

    const auto& definition = GetBuildingDefinition(selectedPreview->buildingType);
    bool debugFreeBuild = scene->game->GetTileMap().params.debugMode;
    const bool fogAllowsPlacement = !IsFogOfWarPreferenceEnabled() ||
        scene->game->IsBuildFootprintVisibleToPlayer(player->id, tilePos, selectedPreview->GetFootprint());
    return fogAllowsPlacement &&
           scene->game->GetTileMap().CanPlaceBuilding(selectedPreview->buildingType, tilePos, selectedPreview->GetFootprint(), player) &&
           (debugFreeBuild || player->CanBuildDefinition(definition));
}

// Selects a build option by index and refreshes the preview.
void BuildGuiSystem::SelectOption(size_t index)
{
    if (index >= options.size())
        return;

    selectedIndex = index;
    selectedPreview = options[selectedIndex].previewFactory();
    buildPanel.selectedIndex = selectedIndex;
}

// Rebuilds the ghost preview for the selected option.
void BuildGuiSystem::RefreshGhost()
{
    ghostWidget.selectedOption = selectedIndex < options.size() ? &options[selectedIndex] : nullptr;
    ghostWidget.tilePos = GetHoveredTile();
    ghostWidget.canBuild = CanPlaceSelected(ghostWidget.tilePos);
}

// Places the selected option under the cursor when placement is valid.
bool BuildGuiSystem::TryPlaceSelectedAtHovered(bool returnAfterBuild)
{
    Vec2i tilePos = GetHoveredTile();
    if (!CanPlaceSelected(tilePos))
        return false;

    if (selectedIndex >= options.size())
        return false;

    options[selectedIndex].buildAt(tilePos);
    if (returnAfterBuild)
        ReturnToMapView();

    return true;
}

// ─── RoadBuildSystem ─────────────────────────────────────────────────────────

RoadBuildSystem::RoadBuildSystem(GuiController* con)
    : BuildGuiSystem(con)
{
    buildPanel.title = "Roads";
    options.clear();
    for (BuildingType type : GetBuildableRoadTypes())
        options.push_back(MakeBuildOption(scene, type));
    SortBuildOptions(options);

    // Initial state only — the live selection follows the cursor every frame
    // (SyncSelectionToHoveredTile), because road mode has no visible panel to
    // switch options with. Start on Road, the overwhelmingly common case.
    auto roadIt = std::find_if(options.begin(), options.end(),
        [](const BuildOption& option) { return option.buildingType == BuildingType::Road; });
    SelectOption(roadIt != options.end() ? static_cast<size_t>(std::distance(options.begin(), roadIt)) : 0);
}

// Picks Road or Bridge based on what's under the cursor — see the header
// comment: with no panel drawn in road mode, a fixed selection silently
// locks the player out of whichever type isn't selected.
void RoadBuildSystem::SyncSelectionToTile(Vec2i tilePos)
{
    if (scene == nullptr || scene->game == nullptr)
        return;

    TileMap& map = scene->game->GetTileMap();
    if (!map.IsInside(tilePos))
        return;

    BuildingType wanted = map[map.GetIdFromCoords(tilePos)].isMilitaryRoad
        ? BuildingType::Bridge
        : BuildingType::Road;
    if (selectedIndex < options.size() && options[selectedIndex].buildingType == wanted)
        return;

    for (size_t i = 0; i < options.size(); i++)
    {
        if (options[i].buildingType == wanted)
        {
            SelectOption(i);
            return;
        }
    }
}

// Updates camera drag, ghost preview and drag placement.
void RoadBuildSystem::Update(double dt)
{
    if (scene->game == nullptr)
        return;

    ApplyStrategicHudCameraPadding(scene);
    MoveCamera(scene, cameraMovement);
    Vec2i placementTile = GetRoadPlacementTile(dt);
    SyncSelectionToTile(placementTile);
    RefreshGhost();
    ghostWidget.tilePos = placementTile;
    ghostWidget.canBuild = CanPlaceSelected(placementTile);
    owner->AddUiWidget(&ghostWidget);
    owner->AddUiWidget(&strategicHudWidget);
    owner->AddUiWidget(owner->GetGameplayClockWidget());

    // Drag placement: keep painting roads while LMB is held, but never under
    // the strategic HUD buttons.
    if (InputManager::IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsAnyHudButtonHovered(strategicHudWidget))
        TryPlaceRoadTowards(placementTile);
}

// Switches back to building placement mode.
void RoadBuildSystem::BuildPressed()
{
    lastRoadDragTile = {-9999, -9999};
    dragStabilizer.End();
    cameraMovement.isMoving = false;
    owner->ChangeSystem("build");
}

// Toggles road placement mode off.
void RoadBuildSystem::RoadBuildPressed()
{
    dragStabilizer.End();
    ReturnToMapView();
}

// Handles HUD buttons or starts placing roads.
void RoadBuildSystem::LmbPressed()
{
    if (DispatchHudButtonClick(*this, strategicHudWidget))
        return;

    Vec2i tilePos = GetHoveredTile();
    dragStabilizer.Begin(tilePos, GetMousePosition());
    lastRoadDragTile = {-9999, -9999};
    TryPlaceRoadTowards(tilePos);
}

// Ends road drag placement.
void RoadBuildSystem::LmbReleased()
{
    lastRoadDragTile = {-9999, -9999};
    dragStabilizer.End();
}

void RoadBuildSystem::Scroll()
{
    ZoomCamera(scene);
}

// Places a road under the cursor once per hovered tile during a drag.
Vec2i RoadBuildSystem::GetRoadPlacementTile(double dt)
{
    // Input is processed before this frame's Update — re-sync here so a
    // click can't place the type picked for the PREVIOUS frame's hover tile.
    Vec2i rawTile = GetHoveredTile();
    return dragStabilizer.IsActive()
        ? dragStabilizer.Constrain(rawTile, GetMousePosition(), dt, lastRoadDragTile)
        : rawTile;

}

bool RoadBuildSystem::TryPlaceRoadAt(Vec2i tilePos)
{
    SyncSelectionToTile(tilePos);
    if (!CanPlaceSelected(tilePos) || selectedIndex >= options.size())
        return false;
    options[selectedIndex].buildAt(tilePos);
    return true;
}

bool RoadBuildSystem::TryPlaceRoadTowards(Vec2i tilePos)
{
    if (tilePos == lastRoadDragTile)
        return false;

    if (lastRoadDragTile.x < -9000 || lastRoadDragTile.y < -9000)
    {
        if (!TryPlaceRoadAt(tilePos))
            return false;
        lastRoadDragTile = tilePos;
        return true;
    }

    Vec2i cursor = lastRoadDragTile;
    const Vec2i step{
        (tilePos.x > cursor.x) - (tilePos.x < cursor.x),
        (tilePos.y > cursor.y) - (tilePos.y < cursor.y)};
    if (step.x != 0 && step.y != 0)
        return false;

    bool placedAny = false;
    while (cursor != tilePos)
    {
        cursor.x += step.x;
        cursor.y += step.y;
        if (!TryPlaceRoadAt(cursor))
            break;
        lastRoadDragTile = cursor;
        placedAny = true;
    }
    return placedAny;
}

// ─── DestroyGuiSystem ────────────────────────────────────────────────────────

DestroyGuiSystem::DestroyGuiSystem(GuiController* con)
    : GuiSystem(con)
{
    // A4 (docs/work_plan_2026-07-13.md): shadows GuiSystem::scene (Scene*).
    scene = dynamic_cast<GameScene*>(owner->scene);

    WireCommonSystemActions(*this, cameraMovement);

    destroyTargetWidget.scene = scene;
    destroyTooltipWidget.scene = scene;
    SetupStrategicHud(strategicHudWidget, scene);
}

// Applies window size changes to destroy-mode widgets.
void DestroyGuiSystem::UpdateUiWidgets(Vec2i size)
{
    strategicHudWidget.UpdateSize(size);
}

// Tracks the hovered destroy target and updates camera drag.
void DestroyGuiSystem::Update(double dt)
{
    if (scene->game == nullptr)
        return;

    ApplyStrategicHudCameraPadding(scene);
    MoveCamera(scene, cameraMovement);

    Vec2i tilePos = ScreenToTile(scene, GetMousePosition());
    hoveredBuilding = tilePos.x >= 0 && tilePos.y >= 0
        ? scene->game->GetTileMap().GetBuilding(tilePos)
        : nullptr;
    Player* localPlayer = GuiLocalPlayer(scene);
    if (localPlayer == nullptr ||
        (hoveredBuilding != nullptr && hoveredBuilding->owner != localPlayer))
        hoveredBuilding = nullptr;

    demolitionPreview = {};
    if (hoveredBuilding != nullptr && localPlayer != nullptr)
        demolitionPreview = BuildDemolitionPreview(scene->game->GetTileMap(), *hoveredBuilding,
                                                    *localPlayer);

    destroyTargetWidget.building = hoveredBuilding;
    destroyTargetWidget.actionable = demolitionPreview.allowed;
    destroyTooltipWidget.building = hoveredBuilding;
    destroyTooltipWidget.preview = demolitionPreview;
    if (hoveredBuilding != nullptr)
        owner->AddUiWidget(&destroyTargetWidget);
    owner->AddUiWidget(&strategicHudWidget);
    if (hoveredBuilding != nullptr)
        owner->AddUiWidget(&destroyTooltipWidget);
    owner->AddUiWidget(owner->GetGameplayClockWidget());
}

// Drops the hover target before leaving destroy mode.
void DestroyGuiSystem::ClearHoverTarget()
{
    hoveredBuilding = nullptr;
    destroyTargetWidget.building = nullptr;
    destroyTooltipWidget.building = nullptr;
    destroyTooltipWidget.preview = {};
    demolitionPreview = {};
}

// Cancels destroy mode.
void DestroyGuiSystem::EscPressed()
{
    ReturnToMapView();
}

// Switches to build mode.
void DestroyGuiSystem::BuildPressed()
{
    owner->ChangeSystem("build");
}

// Switches to road build mode.
void DestroyGuiSystem::RoadBuildPressed()
{
    owner->ChangeSystem("road_build");
}

// Toggles destroy mode off.
void DestroyGuiSystem::DestroyPressed()
{
    ReturnToMapView();
}

// Opens the player-wide stockpile panel from destroy mode.
void DestroyGuiSystem::StockpilePressed()
{
    ClearHoverTarget();
    cameraMovement.isMoving = false;
    owner->ChangeSystem("stockpile");
}

// Opens the statistics panel from destroy mode.
void DestroyGuiSystem::StatsPressed()
{
    cameraMovement.isMoving = false;
    ClearHoverTarget();
    owner->ChangeSystem("stats");
}

void DestroyGuiSystem::FocusPressed()
{
    cameraMovement.isMoving = false;
    ClearHoverTarget();
    owner->ChangeSystem("focus");
}

void DestroyGuiSystem::TechPressed()
{
    cameraMovement.isMoving = false;
    ClearHoverTarget();
    owner->ChangeSystem("tech");
}

void DestroyGuiSystem::RosterPressed()
{
    cameraMovement.isMoving = false;
    ClearHoverTarget();
    owner->ChangeSystem("roster");
}

// Destroys the hovered building when allowed.
void DestroyGuiSystem::LmbPressed()
{
    if (DispatchHudButtonClick(*this, strategicHudWidget))
        return;

    if (scene->game == nullptr || hoveredBuilding == nullptr)
        return;
    const Player* localPlayer = GuiLocalPlayer(scene);
    if (localPlayer == nullptr || hoveredBuilding->owner != localPlayer)
        return;

    const DemolitionPreview currentPreview = BuildDemolitionPreview(
        scene->game->GetTileMap(), *hoveredBuilding, *localPlayer);
    if (!currentPreview.allowed)
        return;

    int positionId = hoveredBuilding->positionId;
    ClearHoverTarget();
    scene->SubmitLocalCommand(GameCommand::DestroyBuilding(scene->game->GetLocalPlayerId(), positionId));
}

void DestroyGuiSystem::LmbReleased()
{
}

// Starts camera drag.
void DestroyGuiSystem::RmbPressed()
{
    cameraMovement.isMoving = true;
}

// Stops camera drag.
void DestroyGuiSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

void DestroyGuiSystem::Scroll()
{
    ZoomCamera(scene);
}

// Returns to the default map view.
void DestroyGuiSystem::ReturnToMapView()
{
    cameraMovement.isMoving = false;
    ClearHoverTarget();
    owner->ChangeSystem("default");
}
