#ifndef GUI_INTERNAL_H
#define GUI_INTERNAL_H

// Helpers shared by the GUI translation units (GuiController, GuiMapWidgets,
// GuiHudPanels, GuiResearchTree, GuiBuildModes). Implemented in GuiCommon.cpp.
// This header is internal to src/ — nothing outside the GUI code includes it.

#include "ui/GuiController.h"
#include "core/GameCommand.h"

#include <string>
#include <vector>

class GameScene;
class Player;

// ─── Player and scene lookups ────────────────────────────────────────────────

// Returns the local player controlled by this client, or nullptr.
Player* GuiLocalPlayer(GameScene* scene);

// Campaign-wide presentation gate shared by the Roster and Global Map
// systems. It checks every bound province, not only the active one.
bool HasCompletedBarracks(GameScene* scene);

// Returns true once the local player has a completed University, gating the
// Technology panel.
bool HasUniversity(GameScene* scene);

// Returns current map size in tiles for camera clamping helpers.
Vec2i GetMapSize(GameScene* scene);

// ─── Camera and coordinate helpers ───────────────────────────────────────────

// Reserves screen space under the strategic HUD and clamps the camera.
void ApplyStrategicHudCameraPadding(GameScene* scene);

// Applies the standard full-width top-bar layout to the strategic HUD.
void UpdateStrategicHudLayout(StrategicResourceHudWidget& hud, Vec2i windowSize);

// Middle/right-mouse camera drag shared by all interaction systems.
void MoveCamera(GameScene* scene, CameraMovement& cameraMovement);

// Starts a pan candidate when RMB can also trigger a click action.
void BeginCameraDrag(CameraMovement& cameraMovement);

// Ends the RMB pan candidate started by BeginCameraDrag. Returns true when
// the press+release stayed under the drag threshold (a real click — the
// caller should still run its click action), false when it was a pan
// gesture (camera already moved; no click action should fire).
bool EndCameraDragWasClick(CameraMovement& cameraMovement);

// Mouse-wheel zoom around the cursor.
void ZoomCamera(GameScene* scene);

// Converts a screen point to a map tile, or {-1,-1} when outside the map.
Vec2i ScreenToTile(GameScene* scene, Vector2 screen);

// ─── Shared panel chrome ─────────────────────────────────────────────────────

// Returns the close ("X") button area of a full-screen panel.
Rectangle PanelCloseButtonRect(Rectangle panel);

// Draws the close button for a full-screen panel.
void DrawCloseButton(Rectangle panel);

// Width (from the panel's right edge) a title bar must reserve so its text
// never overlaps DrawCloseButton's close button — pass as UiText::DrawTitleBar's
// closeButtonReserve for any panel using DrawCloseButton.
float PanelTitleCloseReserve(Rectangle panel);

// Formats a compact fixed-point HUD value with one decimal place.
std::string FormatOneDecimal(double value);
std::string FormatSimulationTimestamp(std::uint64_t ticks);
std::string FormatDurationTicks(std::uint64_t ticks);

// Tooltip body for one resource: the player-wide total followed by one line
// per warehouse holding it ("HQ #1: 42"). Shared by the strategic HUD chips
// and the stockpile panel so both always describe the stock the same way.
std::vector<std::string> StockpileTooltipLines(Player* player, ResourceType type);

// Renders the standard tooltip for a resource, including its enlarged atlas
// icon and the player-facing (title-cased) resource name.
void DrawResourceTooltip(ResourceType type, const std::vector<std::string>& lines,
                         float preferredWidth = 0.0f);

// Returns the local player's headquarters building, or nullptr.
Building* FindLocalHeadquarters(GameScene* scene);
bool CenterCameraOnActiveProvinceHeadquarters(GameScene* scene);

// Grants the local player's HQ a package of every resource — debug worlds
// only (params.debugMode). Wired to the F10 action in WireCommonSystemActions.
void GrantDebugResources(GameScene* scene, int amount);

// ─── Strategic HUD buttons ───────────────────────────────────────────────────

// Clickable mode-button areas in the strategic HUD (right-aligned row).
Rectangle StatsHudButtonRect(const StrategicResourceHudWidget& hud);
Rectangle FocusHudButtonRect(const StrategicResourceHudWidget& hud);
Rectangle TechHudButtonRect(const StrategicResourceHudWidget& hud);
Rectangle DestroyHudButtonRect(const StrategicResourceHudWidget& hud);
Rectangle RoadHudButtonRect(const StrategicResourceHudWidget& hud);
Rectangle BuildHudButtonRect(const StrategicResourceHudWidget& hud);
// Roster panel button, left of Build.
Rectangle RosterHudButtonRect(const StrategicResourceHudWidget& hud);
Rectangle GlobalMapHudButtonRect(const StrategicResourceHudWidget& hud);

bool IsStatsHudButtonHovered(const StrategicResourceHudWidget& hud);
bool IsFocusHudButtonHovered(const StrategicResourceHudWidget& hud);
// Hover counts only when the tech tree is unlocked (completed University).
bool IsTechHudButtonHovered(const StrategicResourceHudWidget& hud);
bool IsDestroyHudButtonHovered(const StrategicResourceHudWidget& hud);
bool IsRoadHudButtonHovered(const StrategicResourceHudWidget& hud);
bool IsBuildHudButtonHovered(const StrategicResourceHudWidget& hud);
bool IsRosterHudButtonHovered(const StrategicResourceHudWidget& hud);
bool IsGlobalMapHudButtonHovered(const StrategicResourceHudWidget& hud);

// True when the cursor is over any strategic HUD mode button.
bool IsAnyHudButtonHovered(const StrategicResourceHudWidget& hud);

// Routes an LMB press on a strategic HUD mode button to the system's matching
// keyboard action ("q"/"r"/"d"/"s"/"f"/"t"/"u"/"m"). Returns true when the click
// was consumed by a HUD button.
bool DispatchHudButtonClick(GuiSystem& system, const StrategicResourceHudWidget& hud);

// ─── System construction helpers ─────────────────────────────────────────────

// Applies the standard scene/anchor setup for a system-owned strategic HUD.
void SetupStrategicHud(StrategicResourceHudWidget& hud, GameScene* scene);

// Wires the input actions shared by every interaction system to the system's
// handler methods. Systems with extra actions add them after this call.
template <typename SystemT>
void WireCommonSystemActions(SystemT& system, CameraMovement& cameraMovement)
{
    system.actionMap["esc"]  = [&system] { system.EscPressed(); };
    system.actionMap["q"]    = [&system] { system.BuildPressed(); };
    system.actionMap["r"]    = [&system] { system.RoadBuildPressed(); };
    system.actionMap["d"]    = [&system] { system.DestroyPressed(); };
    system.actionMap["e"]    = [&system] { system.StockpilePressed(); };
    system.actionMap["s"]    = [&system] { system.StatsPressed(); };
    system.actionMap["f"]    = [&system] { system.FocusPressed(); };
    system.actionMap["t"]    = [&system] { system.TechPressed(); };
    system.actionMap["u"]    = [&system] { system.RosterPressed(); };
    system.actionMap["g"]    = [&system]
    {
        if (system.owner != nullptr)
            system.owner->ChangeSystem("upgrade");
    };
    // Click sound + dispatch — lives here (in the systems' own wiring), not
    // in GuiController::MakeAction: the controller is pure transition/
    // dispatch plumbing (user-directed rework, 2026-07-14).
    system.actionMap["lmbp"] = [&system]
    {
        if (system.scene != nullptr && system.scene->audioSystem != nullptr)
            system.scene->audioSystem->PlaySound("click", 1.0f);
        system.LmbPressed();
    };
    system.actionMap["lmbr"] = [&system] { system.LmbReleased(); };
    system.actionMap["rmbp"] = [&system] { system.RmbPressed(); };
    system.actionMap["rmbr"] = [&system] { system.RmbReleased(); };
    system.actionMap["mmbp"] = [&cameraMovement] { cameraMovement.isMoving = true; };
    system.actionMap["mmbr"] = [&cameraMovement] { cameraMovement.isMoving = false; };
    system.actionMap["debug_resources"] = [&system] { GrantDebugResources(system.scene, 50); };
    system.actionMap["debug_raid"] = [&system]
    {
        if (system.scene == nullptr || system.scene->game == nullptr ||
            !system.scene->game->GetTileMap().params.debugMode)
            return;
        const ProvinceId targetProvinceId = system.scene->game->GetLocalActiveProvinceId();
        if (targetProvinceId != InvalidProvinceId)
            system.scene->SubmitLocalCommand(GameCommand::SpawnDebugRaid(
                system.scene->game->GetLocalPlayerId(), targetProvinceId));
    };
    system.actionMap["global_map"] = [&system]
    {
        if (system.owner != nullptr && HasCompletedBarracks(system.scene))
            system.owner->ChangeSystem("global_map");
    };
    system.actionMap["scroll"] = [&system] { system.Scroll(); };
}

#endif
