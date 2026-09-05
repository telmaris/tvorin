// Recruited roster panel. Deployment is a global-map concern and is not
// available while the local province view is the only playable map.

#include "GuiInternal.h"

#include "ui/ControlIcons.h"
#include "ui/UnitTypeCard.h"
#include "scenes/Scenes.h"
#include "economy/Player.h"
#include "warfare/UnitDefinition.h"

#include <algorithm>
#include <cmath>

namespace
{
    const TaskGroupView* FindTaskGroupView(const GameSnapshot& snapshot, TaskGroupId id)
    {
        for (const auto& group : snapshot.taskGroups)
            if (group.id == id)
                return &group;
        return nullptr;
    }

    const char* TaskGroupStatusLabel(TaskGroupStatus status)
    {
        switch (status)
        {
            case TaskGroupStatus::Empty: return "Empty";
            case TaskGroupStatus::Reserve: return "Reserve";
            case TaskGroupStatus::Garrison: return "Garrison";
            case TaskGroupStatus::Journey: return "Journey";
            case TaskGroupStatus::Battle: return "Battle";
            case TaskGroupStatus::Mixed: return "Mixed";
        }
        return "Unknown";
    }

    std::vector<int> CompletedBarracksIds(GameScene* scene, ProvinceId provinceId)
    {
        std::vector<int> result;
        if (scene == nullptr || scene->game == nullptr)
            return result;
        Player* player = GuiLocalPlayer(scene);
        ProvinceSimulation* simulation = player != nullptr
            ? player->GetProvinceSimulation(provinceId) : nullptr;
        if (simulation == nullptr)
            return result;

        for (Building* building : simulation->GetEconomy().dataTracker.buildings)
            if (building != nullptr && building->buildingType == BuildingType::Barracks &&
                !building->IsUnderConstruction())
                result.push_back(building->id);
        std::sort(result.begin(), result.end());
        return result;
    }
}

void RosterPanelWidget::Update(double dt)
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
        DrawRectangleRounded(bounds, 0.03f, 8, UiTheme::Panel);
        DrawRectangleRoundedLines(bounds, 0.03f, 8, 1.5f, UiTheme::Iron);
    }
    const float chromeInset = UiControlIcons::PixelHudFrameInset(bounds);
    Rectangle title{bounds.x + chromeInset + 2.0f, bounds.y + 4.0f,
                    bounds.width - (chromeInset + 2.0f) * 2.0f, 42.0f};
    UiText::DrawTitleBar(title, "Roster", PanelTitleCloseReserve(bounds));
    DrawCloseButton(bounds);

    const float margin = 24.0f;
    const float top = bounds.y + 64.0f;
    const float contentWidth = bounds.width - margin * 2.0f;
    const float split = std::floor(contentWidth * 0.42f);
    const float leftX = bounds.x + margin;
    const float rightX = leftX + split + 18.0f;
    const float rightWidth = contentWidth - split - 18.0f;
    const float bottom = bounds.y + bounds.height - 24.0f;
    const ProvinceId provinceId = scene->game->GetLocalActiveProvinceId();
    const std::vector<int> barracksIds = CompletedBarracksIds(scene, provinceId);
    if (std::find(barracksIds.begin(), barracksIds.end(), selectedBarracksId) == barracksIds.end())
        selectedBarracksId = barracksIds.empty() ? 0 : barracksIds.front();
    const int barracksId = selectedBarracksId;

    DrawLineEx({rightX - 9.0f, top - 4.0f}, {rightX - 9.0f, bottom}, 1.0f,
               UiTheme::Bronze);
    std::string provinceName = "active province";
    for (const auto& node : scene->latestSnapshot.globalMapView.nodes)
        if (node.id == provinceId && !node.displayName.empty())
            provinceName = node.displayName;
    UiText::Draw("Units in " + provinceName, static_cast<int>(leftX),
                 static_cast<int>(top), 20, UiTheme::AmberBright);
    UiText::Draw("Task groups", static_cast<int>(rightX),
                 static_cast<int>(top), 20, UiTheme::AmberBright);

    const float unitCardGap = 8.0f;
    constexpr int cardColumns = 4;
    const float cardSize = std::clamp(
        std::floor((split - unitCardGap * (cardColumns - 1)) / cardColumns),
        48.0f, 104.0f);
    const float cardGridWidth = cardColumns * cardSize + (cardColumns - 1) * unitCardGap;
    const float cardGridX = leftX + (split - cardGridWidth) * 0.5f;
    const float cardGridY = top + 30.0f;
    const Rectangle leftViewport{leftX, cardGridY, split, bottom - cardGridY};
    BeginScissorMode(static_cast<int>(leftViewport.x), static_cast<int>(leftViewport.y),
                     static_cast<int>(leftViewport.width),
                     static_cast<int>(leftViewport.height));

    std::vector<std::pair<std::string, const UnitDefinition*>> unitTypes;
    for (const auto& [id, definition] : GetUnitCatalog())
        if (definition.recruitBuilding == BuildingType::Barracks)
            unitTypes.emplace_back(id, &definition);
    std::stable_sort(unitTypes.begin(), unitTypes.end(), [](const auto& lhs, const auto& rhs)
    {
        return lhs.first < rhs.first;
    });

    if (unitTypes.empty())
    {
        UiText::DrawFit("No recruitable units", Rectangle{leftX, cardGridY, split, 24.0f},
                        17, UiTheme::ParchmentDim);
    }
    else
    {
        for (std::size_t index = 0; index < unitTypes.size(); ++index)
        {
            const auto& [unitId, definition] = unitTypes[index];
            int totalCount = 0;
            int selectedCount = 0;
            for (const auto& [instanceId, unit] : player->roster.units)
            {
                if (unit.unitDefId != unitId || unit.assignment.provinceId != provinceId)
                    continue;
                ++totalCount;
                if (unit.taskGroupId == selectedTaskGroupId)
                    ++selectedCount;
            }
            Rectangle card{cardGridX + static_cast<float>(index % cardColumns) * (cardSize + unitCardGap),
                           cardGridY + static_cast<float>(index / cardColumns) * (cardSize + unitCardGap),
                           cardSize, cardSize};
            const bool hovered = CheckCollisionPointRec(GetMousePosition(), card);
            DrawUnitTypeCard(card, UnitTypeCardView{unitId, definition->displayName,
                                                     totalCount, selectedCount, barracksId > 0},
                             hovered);
            if (!hovered || selectedTaskGroupId == InvalidTaskGroupId ||
                barracksId <= 0 || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                !IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
                continue;
            const TaskGroupView* selectedGroup = FindTaskGroupView(
                scene->latestSnapshot, selectedTaskGroupId);
            if (selectedGroup == nullptr || !selectedGroup->editable)
                continue;
            const bool remove = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
            const int limit = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) ? 5 : 1;
            std::vector<int> ids;
            for (const auto& [instanceId, unit] : player->roster.units)
            {
                if (unit.unitDefId != unitId || unit.assignment.provinceId != provinceId ||
                    unit.assignment.kind != UnitAssignmentKind::BarracksReserve ||
                    unit.assignment.buildingId != barracksId)
                    continue;
                if (remove ? unit.taskGroupId == selectedTaskGroupId
                           : unit.taskGroupId == InvalidTaskGroupId)
                    ids.push_back(instanceId);
                if (static_cast<int>(ids.size()) == limit)
                    break;
            }
            if (!ids.empty())
                scene->SubmitLocalCommand(remove
                    ? GameCommand::RemoveUnitsFromTaskGroup(
                        scene->game->GetLocalPlayerId(), provinceId,
                        selectedTaskGroupId, std::move(ids))
                    : GameCommand::AddUnitsToTaskGroup(
                        scene->game->GetLocalPlayerId(), provinceId,
                        selectedTaskGroupId, std::move(ids)));
        }
    }
    EndScissorMode();

    std::vector<const TaskGroupView*> groups;
    for (const auto& group : scene->latestSnapshot.taskGroups)
        if (group.stationProvinceId == provinceId && group.homeBarracksBuildingId == barracksId)
            groups.push_back(&group);
    if (std::none_of(groups.begin(), groups.end(),
                     [this](const TaskGroupView* group)
                     {
                         return group != nullptr && group->id == selectedTaskGroupId;
                     }))
        selectedTaskGroupId = InvalidTaskGroupId;

    const float cardGap = 10.0f;
    const float cardWidth = std::max(120.0f, (rightWidth - cardGap) * 0.5f);
    const float cardHeight = 66.0f;
    const float groupTop = top + (barracksIds.size() > 1 ? 58.0f : 30.0f);
    if (barracksIds.size() > 1)
    {
        UiText::DrawFit("Barracks #" + std::to_string(barracksId),
                        {rightX, top + 28.0f, rightWidth - 58.0f, 20.0f},
                        13, UiTheme::ParchmentDim);
        const Rectangle previous{rightX + rightWidth - 52.0f, top + 27.0f, 22.0f, 22.0f};
        const Rectangle next{rightX + rightWidth - 26.0f, top + 27.0f, 22.0f, 22.0f};
        const bool previousHovered = CheckCollisionPointRec(GetMousePosition(), previous);
        const bool nextHovered = CheckCollisionPointRec(GetMousePosition(), next);
        UiControlIcons::DrawPixelHudWidgetFrame(previous, previousHovered, UiTheme::Bronze);
        UiControlIcons::DrawPixelHudWidgetFrame(next, nextHovered, UiTheme::Bronze);
        UiText::Draw("<", previous.x + 6.0f, previous.y + 2.0f, 15,
                     previousHovered ? UiTheme::AmberBright : UiTheme::Parchment);
        UiText::Draw(">", next.x + 6.0f, next.y + 2.0f, 15,
                     nextHovered ? UiTheme::AmberBright : UiTheme::Parchment);
        if (previousHovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            const auto it = std::find(barracksIds.begin(), barracksIds.end(), barracksId);
            const std::size_t index = it == barracksIds.end() ? 0u
                                                               : static_cast<std::size_t>(it - barracksIds.begin());
            selectedBarracksId = barracksIds[(index + barracksIds.size() - 1) % barracksIds.size()];
        }
        if (nextHovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            const auto it = std::find(barracksIds.begin(), barracksIds.end(), barracksId);
            const std::size_t index = it == barracksIds.end() ? 0u
                                                               : static_cast<std::size_t>(it - barracksIds.begin());
            selectedBarracksId = barracksIds[(index + 1) % barracksIds.size()];
        }
    }
    const int columns = cardWidth >= 180.0f ? 2 : 1;
    const float actualCardWidth = columns == 2 ? cardWidth : rightWidth;
    const Rectangle plus{rightX, groupTop, actualCardWidth, cardHeight};
    const bool plusHovered = CheckCollisionPointRec(GetMousePosition(), plus);
    UiControlIcons::DrawPixelHudWidgetFrame(plus, plusHovered, UiTheme::SageBright);
    UiText::DrawFit(barracksId > 0 ? "+  Create empty task group" :
                                     "Build a completed Barracks first",
                    Rectangle{plus.x + 10.0f, plus.y + 20.0f, plus.width - 20.0f, 24.0f},
                    14, barracksId > 0 ? UiTheme::SageBright : UiTheme::ParchmentDim);
    if (plusHovered && barracksId > 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        scene->SubmitLocalCommand(GameCommand::CreateTaskGroup(
            scene->game->GetLocalPlayerId(), provinceId, barracksId));

    for (std::size_t index = 0; index < groups.size(); ++index)
    {
        const TaskGroupView& group = *groups[index];
        const int column = static_cast<int>(index % columns);
        const int row = static_cast<int>(index / columns) + 1;
        const Rectangle card{rightX + column * (actualCardWidth + cardGap),
                             groupTop + row * (cardHeight + cardGap),
                             actualCardWidth, cardHeight};
        const bool selected = group.id == selectedTaskGroupId;
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), card);
        UiControlIcons::DrawPixelHudWidgetFrame(card, hovered || selected,
                                                 selected ? UiTheme::Gold : WHITE);
        UiText::DrawFit("Task group #" + std::to_string(group.id),
                        Rectangle{card.x + 8.0f, card.y + 6.0f, card.width - 32.0f, 18.0f},
                        14, UiTheme::Parchment);
        UiText::DrawFit(std::to_string(group.total) + " units  " +
                            TaskGroupStatusLabel(group.status),
                        Rectangle{card.x + 8.0f, card.y + 27.0f, card.width - 16.0f, 16.0f},
                        13, group.editable ? UiTheme::SageBright : UiTheme::AmberBright);
        UiText::DrawFit("Barracks #" + std::to_string(group.homeBarracksBuildingId),
                        Rectangle{card.x + 8.0f, card.y + 45.0f, card.width - 16.0f, 15.0f},
                        11, UiTheme::ParchmentDim);

        const Rectangle disband{card.x + card.width - 25.0f, card.y + 5.0f, 20.0f, 20.0f};
        const bool disbandHovered = CheckCollisionPointRec(GetMousePosition(), disband);
        UiText::Draw("X", disband.x + 5.0f, disband.y + 1.0f, 15,
                     disbandHovered ? UiTheme::RustBright : UiTheme::ParchmentDim);
        if (disbandHovered && group.editable && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            scene->SubmitLocalCommand(GameCommand::DisbandTaskGroup(
                scene->game->GetLocalPlayerId(), provinceId, group.id));
            if (selectedTaskGroupId == group.id)
                selectedTaskGroupId = InvalidTaskGroupId;
        }
        else if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            selectedTaskGroupId = group.id;
    }

    if (selectedTaskGroupId != InvalidTaskGroupId)
        UiText::DrawFit("LMB: add unit  |  Ctrl+LMB: remove unit",
                        Rectangle{leftX, bottom - 18.0f, contentWidth, 18.0f},
                        13, UiTheme::ParchmentDim);
}

// ─── RosterGuiSystem ─────────────────────────────────────────────────────────

RosterGuiSystem::RosterGuiSystem(GuiController* con)
    : GuiSystem(con)
{
    scene = dynamic_cast<GameScene*>(owner->scene);
    WireCommonSystemActions(*this, cameraMovement);

    rosterPanel.scene = scene;
    rosterPanel.ChangePositionAnchor({0.06f, 0.15f});
    rosterPanel.ChangeSizeAnchor({0.88f, 0.82f});
    rosterPanel.UpdateSize({GetScreenWidth(), GetScreenHeight()});
    SetupStrategicHud(strategicHudWidget, scene);
}

bool RosterGuiSystem::CanActivate()
{
    return HasCompletedBarracks(scene);
}

void RosterGuiSystem::UpdateUiWidgets(Vec2i size)
{
    rosterPanel.UpdateSize(size);
    strategicHudWidget.UpdateSize(size);
}

void RosterGuiSystem::Update(double dt)
{
    if (scene->game == nullptr)
        return;

    ApplyStrategicHudCameraPadding(scene);
    MoveCamera(scene, cameraMovement);
    owner->AddUiWidget(&rosterPanel);
    owner->AddUiWidget(&strategicHudWidget);
}

void RosterGuiSystem::EscPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("default");
}

void RosterGuiSystem::BuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("build");
}

void RosterGuiSystem::RoadBuildPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("road_build");
}

void RosterGuiSystem::DestroyPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("destroy");
}

void RosterGuiSystem::StockpilePressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("stockpile");
}

void RosterGuiSystem::StatsPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("stats");
}

void RosterGuiSystem::FocusPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("focus");
}

void RosterGuiSystem::TechPressed()
{
    cameraMovement.isMoving = false;
    owner->ChangeSystem("tech");
}

void RosterGuiSystem::RosterPressed()
{
    EscPressed();
}

void RosterGuiSystem::LmbPressed()
{
    if (DispatchHudButtonClick(*this, strategicHudWidget))
        return;

    Vector2 mouse = GetMousePosition();
    Rectangle panelBounds{static_cast<float>(rosterPanel.pos.x),
                          static_cast<float>(rosterPanel.pos.y),
                          static_cast<float>(rosterPanel.size.x),
                          static_cast<float>(rosterPanel.size.y)};
    if (CheckCollisionPointRec(mouse, PanelCloseButtonRect(panelBounds)))
        EscPressed();
}

void RosterGuiSystem::LmbReleased()
{
}

void RosterGuiSystem::RmbPressed()
{
    Vector2 mouse = GetMousePosition();
    Rectangle panelBounds{static_cast<float>(rosterPanel.pos.x),
                          static_cast<float>(rosterPanel.pos.y),
                          static_cast<float>(rosterPanel.size.x),
                          static_cast<float>(rosterPanel.size.y)};
    if (CheckCollisionPointRec(mouse, panelBounds))
    {
        cameraMovement.isMoving = false;
        return;
    }
    cameraMovement.isMoving = true;
}

void RosterGuiSystem::RmbReleased()
{
    cameraMovement.isMoving = false;
}

void RosterGuiSystem::Scroll()
{
    ZoomCamera(scene);
}
