// Shared helpers for the GUI translation units. See GuiInternal.h.

#include "GuiInternal.h"

#include "scenes/Scenes.h"
#include "core/Log.h"
#include "economy/Player.h"
#include "economy/StockpileIndex.h"
#include "ui/Renderer.h"
#include "ui/ControlIcons.h"
#include "raymath.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

Player* GuiLocalPlayer(GameScene* scene)
{
    if (scene == nullptr || scene->game == nullptr)
        return nullptr;

    auto it = scene->game->GetPlayerHandler().players.find(scene->game->GetLocalPlayerId());
    return it != scene->game->GetPlayerHandler().players.end() ? it->second.get() : nullptr;
}

bool HasCompletedBarracks(GameScene* scene)
{
    Player* player = GuiLocalPlayer(scene);
    if (scene == nullptr || scene->game == nullptr || player == nullptr)
        return false;

    for (const ProvinceId provinceId : scene->game->GetGlobalMap().GetProvinceIds())
    {
        const ProvinceSimulation* simulation = player->GetProvinceSimulation(provinceId);
        if (simulation != nullptr && simulation->GetEconomy().dataTracker.HasBuilding(
                BuildingType::Barracks, true))
            return true;
    }
    return false;
}

bool HasUniversity(GameScene* scene)
{
    Player* player = GuiLocalPlayer(scene);
    return player != nullptr && player->HasTrackedBuilding(BuildingType::University, true);
}

Vec2i GetMapSize(GameScene* scene)
{
    return Vec2i{scene->game->GetTileMap().params.sizeX, scene->game->GetTileMap().params.sizeY};
}

// Finds the local player's headquarters building.
Building* FindLocalHeadquarters(GameScene* scene)
{
    Player* player = GuiLocalPlayer(scene);
    if (player == nullptr)
        return nullptr;

    for (auto* building : player->GetTrackedBuildings())
    {
        if (building != nullptr && building->owner == player && building->buildingType == BuildingType::Headquarters)
            return building;
    }

    return nullptr;
}

bool CenterCameraOnActiveProvinceHeadquarters(GameScene* scene)
{
    if (scene == nullptr || scene->game == nullptr)
        return false;

    Building* headquarters = FindLocalHeadquarters(scene);
    if (headquarters == nullptr)
        return false;

    TileMap& map = scene->game->GetTileMap();

    const Vec2i anchor = map.GetCoordsFromId(headquarters->positionId);
    const Vec2i footprint = headquarters->GetFootprint();
    const Vec2f center{
        static_cast<float>((anchor.x + footprint.x * 0.5f) * TILE_SIZE),
        static_cast<float>((anchor.y + footprint.y * 0.5f) * TILE_SIZE)};
    ApplyStrategicHudCameraPadding(scene);
    scene->render.CenterCameraOnWorld(center, GetMapSize(scene));
    return true;
}

namespace
{
    // Adds a debug resource package to one storage-like building.
    void GrantResourcesToStorage(StorageComponent* storage, int amount)
    {
        if (storage == nullptr || amount <= 0)
            return;

        for (ResourceType type : resourceTypes)
        {
            auto& buffer = storage->buffers[type];
            if (buffer.type == ResourceType::Null)
                buffer = ResourceBuffer{type, amount};
            buffer.bufferSize = std::max(buffer.bufferSize, static_cast<int>(buffer.buffer.size()) + amount);
            for (int i = 0; i < amount; i++)
                buffer.GenerateResource(type);
        }
    }
}

// Grants local debug resources when the current world allows debug helpers.
// Moved out of GuiController (user-directed rework, 2026-07-14): the
// controller is pure system-transition plumbing, concrete actions live in
// the systems' actionMaps.
void GrantDebugResources(GameScene* scene, int amount)
{
    if (scene == nullptr || scene->game == nullptr || !scene->game->GetTileMap().params.debugMode)
        return;

    Building* headquarters = FindLocalHeadquarters(scene);
    auto* storage = headquarters != nullptr ? headquarters->GetComponent<StorageComponent>() : nullptr;
    if (storage == nullptr)
        return;

    GrantResourcesToStorage(storage, amount);
    Log::Msg("[Debug]", "granted ", amount, " of every resource to local HQ");
}

namespace
{
    constexpr float StrategicHudScreenMargin = 4.0f;

    float StrategicHudHeightForWindow(Vec2i windowSize)
    {
        // The pilot frame reads better with a little more vertical room than
        // the original compact strip, while staying small enough not to hide
        // the map. At 1080p this resolves to roughly 123 px (+10%).
        return std::clamp(windowSize.y * 0.114f, 106.0f, 130.0f);
    }

}

void UpdateStrategicHudLayout(StrategicResourceHudWidget& hud, Vec2i windowSize)
{
    const int margin = static_cast<int>(StrategicHudScreenMargin);
    hud.ChangePosition(margin, margin);
    hud.ChangeSize(std::max(1, windowSize.x - margin * 2),
                   static_cast<int>(StrategicHudHeightForWindow(windowSize)));
}

void ApplyStrategicHudCameraPadding(GameScene* scene)
{
    if (scene == nullptr || scene->game == nullptr)
        return;

    Vec2i windowSize{GetScreenWidth(), GetScreenHeight()};
    scene->render.ClampCameraToMap(GetMapSize(scene));
}

namespace
{
    // Minimum displacement that distinguishes a pan from a click.
    constexpr float kRmbDragThresholdPixels = 4.0f;
}

void MoveCamera(GameScene* scene, CameraMovement& cameraMovement)
{
    if (!cameraMovement.isMoving)
        return;
    if (!InputManager::IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) && !InputManager::IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
    {
        cameraMovement.isMoving = false;
        return;
    }

    // Only set (via BeginCameraDrag) for systems where RMB also has a
    // competing click action — MMB-only drags never populate this, so this
    // is a no-op for them.
    if (cameraMovement.rmbPressScreenPos.x >= 0)
    {
        Vector2 mouse = GetMousePosition();
        float dx = mouse.x - static_cast<float>(cameraMovement.rmbPressScreenPos.x);
        float dy = mouse.y - static_cast<float>(cameraMovement.rmbPressScreenPos.y);
        if (dx * dx + dy * dy > kRmbDragThresholdPixels * kRmbDragThresholdPixels)
            cameraMovement.rmbDragged = true;
    }

    Vector2 delta = GetMouseDelta();
    delta.x *= -1;
    delta.x /= scene->render.camera.zoom;
    delta.y /= scene->render.camera.zoom;
    scene->render.camera.target = Vector2Add(scene->render.camera.target, delta);
    ApplyStrategicHudCameraPadding(scene);
    scene->render.ClampCameraToMap(GetMapSize(scene));
}

void BeginCameraDrag(CameraMovement& cameraMovement)
{
    cameraMovement.isMoving = true;
    Vector2 mouse = GetMousePosition();
    cameraMovement.rmbPressScreenPos = {static_cast<int>(mouse.x), static_cast<int>(mouse.y)};
    cameraMovement.rmbDragged = false;
}

bool EndCameraDragWasClick(CameraMovement& cameraMovement)
{
    bool wasClick = cameraMovement.rmbPressScreenPos.x >= 0 && !cameraMovement.rmbDragged;
    cameraMovement.isMoving = false;
    cameraMovement.rmbPressScreenPos = {-1, -1};
    cameraMovement.rmbDragged = false;
    return wasClick;
}

void ZoomCamera(GameScene* scene)
{
    float wheel = InputManager::GetMouseWheelMove();
    if (wheel == 0.0f || scene == nullptr ||
        scene->campaignSidebar.CapturesPointer(GetMousePosition()))
        return;

    ApplyStrategicHudCameraPadding(scene);
    scene->render.ZoomAtScreenPoint(GetMousePosition(), wheel, GetMapSize(scene));
}

Vec2i ScreenToTile(GameScene* scene, Vector2 screen)
{
    Vec2f world = scene->render.ScreenToWorld(screen);
    if (world.x < 0.0f || world.y < 0.0f)
        return {-1, -1};

    Vec2i tilePos{
        static_cast<int>(world.x / TILE_SIZE),
        static_cast<int>(world.y / TILE_SIZE)};
    if (!scene->game->GetTileMap().IsInside(tilePos))
        return {-1, -1};

    return tilePos;
}

Rectangle PanelCloseButtonRect(Rectangle panel)
{
    // Keep the control inside the same fixed side rail as the title visual.
    // Using the shared 9-slice inset prevents it from overlapping a corner
    // plate on narrower windows.
    return UiControlIcons::PixelHudCloseButtonRect(panel);
}

void DrawCloseButton(Rectangle panel)
{
    Rectangle close = PanelCloseButtonRect(panel);
    bool hover = CheckCollisionPointRec(GetMousePosition(), close);
    if (UiControlIcons::DrawPanelCloseButton(close, hover))
        return;

    DrawRectangleRounded(close, 0.18f, 8, hover ? UiTheme::SurfaceHover : UiTheme::Surface);
    DrawRectangleRoundedLines(close, 0.18f, 8, 1.0f, hover ? UiTheme::Cyan : UiTheme::Iron);
    UiText::DrawFit("X", Rectangle{close.x + 6.0f, close.y + 4.0f, close.width - 12.0f, close.height - 8.0f}, 20, UiTheme::Parchment);
}

float PanelTitleCloseReserve(Rectangle panel)
{
    Rectangle close = PanelCloseButtonRect(panel);
    return (panel.x + panel.width) - close.x + 10.0f;
}

std::string FormatOneDecimal(double value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << value;
    return stream.str();
}

std::string FormatSimulationTimestamp(std::uint64_t ticks)
{
    constexpr std::uint64_t TicksPerSecond = 100;
    constexpr std::uint64_t SecondsPerDay = 24 * 60 * 60;
    const std::uint64_t totalSeconds = ticks / TicksPerSecond;
    const std::uint64_t day = totalSeconds / SecondsPerDay;
    const std::uint64_t daySeconds = totalSeconds % SecondsPerDay;
    const std::uint64_t hours = daySeconds / 3600;
    const std::uint64_t minutes = (daySeconds / 60) % 60;
    const std::uint64_t seconds = daySeconds % 60;
    std::ostringstream stream;
    stream << '[';
    if (day > 0)
        stream << 'D' << day + 1 << ' ';
    stream << std::setfill('0') << std::setw(2) << hours << ':'
           << std::setw(2) << minutes << ':' << std::setw(2) << seconds << ']';
    return stream.str();
}

std::string FormatDurationTicks(std::uint64_t ticks)
{
    const double seconds = static_cast<double>(ticks) / 100.0;
    if (seconds < 10.0)
        return FormatOneDecimal(seconds) + " s";
    if (seconds < 60.0)
        return std::to_string(static_cast<int>(std::ceil(seconds))) + " s";
    return FormatOneDecimal(seconds / 60.0) + " min";
}

std::vector<std::string> StockpileTooltipLines(Player* player, ResourceType type)
{
    if (player == nullptr)
        return {};

    auto holdings = StockpileIndex::GetHoldings(*player, type);
    int total = 0;
    for (const auto& holding : holdings)
        total += holding.amount;

    std::vector<std::string> lines{"Total: " + std::to_string(total)};
    if (holdings.empty())
    {
        lines.push_back("Not stored anywhere");
        return lines;
    }

    lines.push_back("Stored in:");
    for (const auto& holding : holdings)
    {
        const char* label = holding.building != nullptr &&
                            holding.building->buildingType == BuildingType::Headquarters
            ? "HQ" : "Storage";
        lines.push_back("  " + std::string(label) + " #" + std::to_string(holding.buildingId) +
                        ": " + std::to_string(holding.amount) + " / " + std::to_string(holding.capacity));
    }
    return lines;
}

void DrawResourceTooltip(ResourceType type, const std::vector<std::string>& lines, float preferredWidth)
{
    Tooltip::Draw(ResourceDisplayName(type), lines, preferredWidth,
                  [type](Rectangle icon) { GuiPanel::DrawResourceIcon(type, icon); });
}

namespace
{
    constexpr float StrategicHudActionGap = 4.0f;
    constexpr int StrategicHudActionButtonCount = 8;

    Rectangle StrategicHudActionButtonRect(const StrategicResourceHudWidget& hud,
                                           int index)
    {
        const float height = static_cast<float>(hud.size.y);
        if (index < 0 || index >= StrategicHudActionButtonCount || hud.size.x <= 0 || hud.size.y <= 0)
            return {};

        constexpr float leftHudReserve = 450.0f;
        constexpr float buttonAspect = 1.0f;
        const float cornerScale = std::min({1.0f, height / 148.0f,
                                            static_cast<float>(hud.size.x) / 256.0f});
        const float contentRightInset = 128.0f * cornerScale + 16.0f;
        // Keep the buttons visually compact even though the frame gained some
        // vertical room. They must not grow with the taller top panel.
        const float desiredButtonHeight = std::clamp(height - 52.0f, 44.0f, 60.0f);
        const float desiredButtonWidth = desiredButtonHeight * buttonAspect;
        const float widthLimitedButtonWidth =
            (static_cast<float>(hud.size.x) - leftHudReserve - contentRightInset -
             (StrategicHudActionButtonCount - 1) * StrategicHudActionGap) /
            StrategicHudActionButtonCount;
        const float buttonWidth = std::min(desiredButtonWidth,
                                           std::max(42.0f, widthLimitedButtonWidth));
        const float buttonHeight = std::min(desiredButtonHeight, buttonWidth / buttonAspect);
        const float totalWidth = StrategicHudActionButtonCount * buttonWidth +
                                 (StrategicHudActionButtonCount - 1) * StrategicHudActionGap;
        const float contentRight = static_cast<float>(hud.pos.x + hud.size.x) -
                                   contentRightInset;
        const float left = contentRight - totalWidth;
        return Rectangle{left + index * (buttonWidth + StrategicHudActionGap),
                         static_cast<float>(hud.pos.y) + (height - buttonHeight) * 0.5f,
                         buttonWidth, buttonHeight};
    }
}

Rectangle StatsHudButtonRect(const StrategicResourceHudWidget& hud)
{
    constexpr int resourcesIndex = 3;
    return StrategicHudActionButtonRect(hud, resourcesIndex);
}

Rectangle FocusHudButtonRect(const StrategicResourceHudWidget& hud)
{
    constexpr int focusIndex = 5;
    return StrategicHudActionButtonRect(hud, focusIndex);
}

Rectangle TechHudButtonRect(const StrategicResourceHudWidget& hud)
{
    constexpr int technologyIndex = 6;
    return StrategicHudActionButtonRect(hud, technologyIndex);
}

Rectangle DestroyHudButtonRect(const StrategicResourceHudWidget& hud)
{
    constexpr int destroyIndex = 1;
    return StrategicHudActionButtonRect(hud, destroyIndex);
}

Rectangle RoadHudButtonRect(const StrategicResourceHudWidget& hud)
{
    constexpr int roadIndex = 2;
    return StrategicHudActionButtonRect(hud, roadIndex);
}

Rectangle BuildHudButtonRect(const StrategicResourceHudWidget& hud)
{
    constexpr int buildIndex = 0;
    return StrategicHudActionButtonRect(hud, buildIndex);
}

Rectangle RosterHudButtonRect(const StrategicResourceHudWidget& hud)
{
    constexpr int rosterIndex = 4;
    return StrategicHudActionButtonRect(hud, rosterIndex);
}

Rectangle GlobalMapHudButtonRect(const StrategicResourceHudWidget& hud)
{
    constexpr int globalMapIndex = 7;
    return StrategicHudActionButtonRect(hud, globalMapIndex);
}

namespace
{
    bool TutorialDestroyLocked(const StrategicResourceHudWidget& hud)
    {
        return hud.scene != nullptr && hud.scene->AreTutorialDestroyLocked();
    }

    bool TutorialDecisionsLocked(const StrategicResourceHudWidget& hud)
    {
        return hud.scene != nullptr && hud.scene->AreTutorialDecisionsLocked();
    }

    bool TutorialStatisticsLocked(const StrategicResourceHudWidget& hud)
    {
        return hud.scene != nullptr && hud.scene->AreTutorialStatisticsLocked();
    }
}

bool IsStatsHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return !hud.tutorialDisableStatistics && !TutorialStatisticsLocked(hud) &&
           CheckCollisionPointRec(GetMousePosition(), StatsHudButtonRect(hud));
}

bool IsFocusHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return !hud.tutorialDisableDecisions && !TutorialDecisionsLocked(hud) &&
           CheckCollisionPointRec(GetMousePosition(), FocusHudButtonRect(hud));
}

bool IsTechHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return CheckCollisionPointRec(GetMousePosition(), TechHudButtonRect(hud)) && HasUniversity(hud.scene);
}

bool IsDestroyHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return !hud.tutorialDisableDestroy && !TutorialDestroyLocked(hud) &&
           CheckCollisionPointRec(GetMousePosition(), DestroyHudButtonRect(hud));
}

bool IsRoadHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return CheckCollisionPointRec(GetMousePosition(), RoadHudButtonRect(hud));
}

bool IsBuildHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return CheckCollisionPointRec(GetMousePosition(), BuildHudButtonRect(hud));
}

bool IsRosterHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return HasCompletedBarracks(hud.scene) &&
           CheckCollisionPointRec(GetMousePosition(), RosterHudButtonRect(hud));
}

bool IsGlobalMapHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    return HasCompletedBarracks(hud.scene) &&
           CheckCollisionPointRec(GetMousePosition(), GlobalMapHudButtonRect(hud));
}

bool IsAnyHudButtonHovered(const StrategicResourceHudWidget& hud)
{
    const Vector2 mouse = GetMousePosition();
    const bool disabledButtonHovered =
        ((hud.tutorialDisableDestroy || TutorialDestroyLocked(hud)) && CheckCollisionPointRec(mouse, DestroyHudButtonRect(hud))) ||
        ((hud.tutorialDisableDecisions || TutorialDecisionsLocked(hud)) && CheckCollisionPointRec(mouse, FocusHudButtonRect(hud))) ||
        ((hud.tutorialDisableStatistics || TutorialStatisticsLocked(hud)) && CheckCollisionPointRec(mouse, StatsHudButtonRect(hud)));
    const bool gatedButtonHovered =
        (!HasCompletedBarracks(hud.scene) &&
         (CheckCollisionPointRec(mouse, RosterHudButtonRect(hud)) ||
          CheckCollisionPointRec(mouse, GlobalMapHudButtonRect(hud))));
    return disabledButtonHovered || gatedButtonHovered || IsBuildHudButtonHovered(hud) || IsRoadHudButtonHovered(hud) ||
           IsDestroyHudButtonHovered(hud) || IsStatsHudButtonHovered(hud) ||
           IsFocusHudButtonHovered(hud) || IsTechHudButtonHovered(hud) ||
           IsRosterHudButtonHovered(hud) ||
           IsGlobalMapHudButtonHovered(hud);
}

bool DispatchHudButtonClick(GuiSystem& system, const StrategicResourceHudWidget& hud)
{
    // Disabled tutorial controls still consume the click so it cannot fall
    // through to map selection or camera interaction underneath the HUD.
    const Vector2 mouse = GetMousePosition();
    if (hud.scene != nullptr && hud.scene->campaignSidebar.CapturesPointer(mouse))
        return true;
    if (!HasCompletedBarracks(hud.scene) &&
        (CheckCollisionPointRec(mouse, RosterHudButtonRect(hud)) ||
         CheckCollisionPointRec(mouse, GlobalMapHudButtonRect(hud))))
        return true;
    if (((hud.tutorialDisableDestroy || TutorialDestroyLocked(hud)) && CheckCollisionPointRec(mouse, DestroyHudButtonRect(hud))) ||
        ((hud.tutorialDisableDecisions || TutorialDecisionsLocked(hud)) && CheckCollisionPointRec(mouse, FocusHudButtonRect(hud))) ||
        ((hud.tutorialDisableStatistics || TutorialStatisticsLocked(hud)) && CheckCollisionPointRec(mouse, StatsHudButtonRect(hud))))
        return true;

    if (IsGlobalMapHudButtonHovered(hud))
    {
        auto it = system.actionMap.find("global_map");
        if (it != system.actionMap.end())
            it->second();
        return true;
    }

    const char* action = nullptr;
    if (IsBuildHudButtonHovered(hud))
        action = "q";
    else if (IsRoadHudButtonHovered(hud))
        action = "r";
    else if (IsDestroyHudButtonHovered(hud))
        action = "d";
    else if (IsStatsHudButtonHovered(hud))
        action = "s";
    else if (IsFocusHudButtonHovered(hud))
        action = "f";
    else if (IsTechHudButtonHovered(hud))
        action = "t";
    else if (IsRosterHudButtonHovered(hud))
        action = "u";

    if (action == nullptr)
        return false;

    auto it = system.actionMap.find(action);
    if (it != system.actionMap.end())
        it->second();
    return true;
}

void SetupStrategicHud(StrategicResourceHudWidget& hud, GameScene* scene)
{
    hud.scene = scene;
    hud.ChangePositionAnchor({0.012f, 0.012f});
    hud.ChangeSizeAnchor({0.42f, 0.055f});
    hud.UpdateSize({GetScreenWidth(), GetScreenHeight()});
}
