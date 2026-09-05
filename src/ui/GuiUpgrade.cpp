#include "GuiInternal.h"

#include "ui/BuildingUpgradeView.h"
#include "ui/UpgradeLineRasterizer.h"
#include "economy/Player.h"
#include "scenes/Scenes.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace
{
    void Navigate(GuiController* controller, const char* system)
    {
        if (controller != nullptr)
            controller->ChangeSystem(system);
    }
}

UpgradeGuiSystem::UpgradeGuiSystem(GuiController* con)
    : GuiSystem(con)
{
    gameScene = dynamic_cast<GameScene*>(owner->scene);
    scene = gameScene;
    WireCommonSystemActions(*this, cameraMovement);
    actionMap["g"] = [this] { UpgradePressed(); };
    targetWidget.scene = gameScene;
    SetupStrategicHud(strategicHudWidget, gameScene);
}

void UpgradeGuiSystem::UpdateUiWidgets(Vec2i size)
{
    strategicHudWidget.UpdateSize(size);
}

void UpgradeGuiSystem::OnActivate()
{
    gestureActive = false;
    visitedBuildingIds.Reset();
    lastGestureTile = {-9999, -9999};
    hoveredBuilding = nullptr;
    targetWidget.building = nullptr;
}

void UpgradeGuiSystem::OnDeactivate()
{
    OnActivate();
}

Building* UpgradeGuiSystem::GetHoveredBuilding() const
{
    if (gameScene == nullptr || gameScene->game == nullptr)
        return nullptr;

    const Vec2i tile = ScreenToTile(gameScene, GetMousePosition());
    if (tile.x < 0 || tile.y < 0 || !gameScene->game->GetTileMap().IsInside(tile))
        return nullptr;
    return gameScene->game->GetTileMap().GetBuilding(tile);
}

bool UpgradeGuiSystem::CanUpgrade(Building* building) const
{
    if (building == nullptr || gameScene == nullptr || gameScene->game == nullptr)
        return false;

    const Player* player = GuiLocalPlayer(gameScene);
    if (player == nullptr || building->owner != player || building->IsUnderConstruction())
        return false;

    const BuildingUpgradeView upgrade = MakeBuildingUpgradeView(*building);
    return upgrade.CanStart() &&
           player->GetBuildUnlockRequirementFailures(GetBuildingDefinition(building->buildingType)).empty() &&
           player->HasBuildResources(upgrade.target->cost);
}

void UpgradeGuiSystem::SubmitUpgrade(Building* building)
{
    if (!CanUpgrade(building) || gameScene == nullptr || gameScene->game == nullptr)
        return;

    if (!visitedBuildingIds.InsertIfNew(building->id))
        return;
    gameScene->SubmitLocalCommand(GameCommand::UpgradeBuilding(
        gameScene->game->GetLocalPlayerId(), gameScene->game->GetLocalActiveProvinceId(),
        building->positionId));
}

void UpgradeGuiSystem::SubmitUpgradeLine(Vec2i tile)
{
    if (gameScene == nullptr || gameScene->game == nullptr ||
        !gameScene->game->GetTileMap().IsInside(tile) || tile == lastGestureTile)
        return;

    if (lastGestureTile.x < -9000 || lastGestureTile.y < -9000)
    {
        SubmitUpgrade(gameScene->game->GetTileMap().GetBuilding(tile));
        lastGestureTile = tile;
        return;
    }

    // Bresenham traversal keeps drag submission deterministic while covering
    // both axis-aligned and diagonal gestures on the tile grid.
    for (const Vec2i cursor : RasterizeUpgradeTileLine(lastGestureTile, tile))
    {
        if (cursor != lastGestureTile)
            SubmitUpgrade(gameScene->game->GetTileMap().GetBuilding(cursor));
    }

    lastGestureTile = tile;
}

void UpgradeGuiSystem::Update(double dt)
{
    if (gameScene == nullptr || gameScene->game == nullptr)
        return;

    ApplyStrategicHudCameraPadding(gameScene);
    MoveCamera(gameScene, cameraMovement);
    hoveredBuilding = GetHoveredBuilding();
    targetWidget.building = hoveredBuilding;
    targetWidget.actionable = CanUpgrade(hoveredBuilding);

    if (hoveredBuilding != nullptr)
        owner->AddUiWidget(&targetWidget);
    owner->AddUiWidget(&strategicHudWidget);
    owner->AddUiWidget(owner->GetGameplayClockWidget());

    if (hoveredBuilding != nullptr)
    {
        const BuildingUpgradeView upgrade = MakeBuildingUpgradeView(*hoveredBuilding);
        std::vector<std::string> lines;
        if (!upgrade.HasTarget())
            lines.push_back("Max level");
        else
        {
            lines.push_back("Target: level " + std::to_string(upgrade.targetLevel));
            lines.push_back("Time: " + std::to_string(static_cast<int>(std::round(upgrade.target->buildTime))) + "s");
            for (const auto& cost : upgrade.target->cost)
                lines.push_back(ResourceDisplayName(cost.type) + " x" + std::to_string(cost.amount));
            if (hoveredBuilding->IsUnderConstruction())
                lines.push_back("Under construction");
            else if (upgrade.component != nullptr && upgrade.component->isUpgrading)
                lines.push_back("Already upgrading");
            else if (gameScene != nullptr && GuiLocalPlayer(gameScene) != nullptr)
            {
                const auto requirements = GuiLocalPlayer(gameScene)->GetBuildUnlockRequirementFailures(
                    GetBuildingDefinition(hoveredBuilding->buildingType));
                if (!requirements.empty())
                    lines.push_back(requirements.front());
                else if (!CanUpgrade(hoveredBuilding))
                    lines.push_back("Missing resources");
            }
        }
        Tooltip::Draw(hoveredBuilding->name, lines, 300.0f);
    }

    if (gestureActive && InputManager::IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
        !IsAnyHudButtonHovered(strategicHudWidget))
    {
        const Vec2i tile = ScreenToTile(gameScene, GetMousePosition());
        SubmitUpgradeLine(tile);
    }
}

void UpgradeGuiSystem::EscPressed()
{
    ReturnToMapView();
}

void UpgradeGuiSystem::UpgradePressed()
{
    ReturnToMapView();
}

void UpgradeGuiSystem::LmbPressed()
{
    if (DispatchHudButtonClick(*this, strategicHudWidget))
        return;

    gestureActive = true;
    visitedBuildingIds.Reset();
    lastGestureTile = {-9999, -9999};
    SubmitUpgradeLine(ScreenToTile(gameScene, GetMousePosition()));
}

void UpgradeGuiSystem::LmbReleased()
{
    gestureActive = false;
    lastGestureTile = {-9999, -9999};
}

void UpgradeGuiSystem::RmbPressed()
{
    gestureActive = false;
    lastGestureTile = {-9999, -9999};
    visitedBuildingIds.Reset();
    ReturnToMapView();
}

void UpgradeGuiSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

void UpgradeGuiSystem::Scroll()
{
    ZoomCamera(gameScene);
}

void UpgradeGuiSystem::BuildPressed() { Navigate(owner, "build"); }
void UpgradeGuiSystem::RoadBuildPressed() { Navigate(owner, "road_build"); }
void UpgradeGuiSystem::DestroyPressed() { Navigate(owner, "destroy"); }
void UpgradeGuiSystem::StockpilePressed() { Navigate(owner, "stockpile"); }
void UpgradeGuiSystem::StatsPressed() { Navigate(owner, "stats"); }
void UpgradeGuiSystem::FocusPressed() { Navigate(owner, "focus"); }
void UpgradeGuiSystem::TechPressed() { Navigate(owner, "tech"); }
void UpgradeGuiSystem::RosterPressed() { Navigate(owner, "roster"); }

void UpgradeGuiSystem::ReturnToMapView()
{
    gestureActive = false;
    lastGestureTile = {-9999, -9999};
    visitedBuildingIds.Reset();
    hoveredBuilding = nullptr;
    targetWidget.building = nullptr;
    Navigate(owner, "default");
}
