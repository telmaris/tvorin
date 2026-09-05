#ifndef KEYBINDINGS_H
#define KEYBINDINGS_H

#include <map>
#include <string>

// Game actions that can be triggered by keyboard input
enum class GameAction
{
    // Build/destroy/UI modes
    EnterBuildMode,
    EnterDestroyMode,
    EnterRoadMode,
    TogglePauseGame,

    // Camera controls
    CameraUp,
    CameraDown,
    CameraLeft,
    CameraRight,
    CameraZoomIn,
    CameraZoomOut,

    // UI panels
    OpenResearchPanel,
    OpenStatsPanel,
    OpenFocusTree,

    // Game state
    SaveGame,
    LoadGame,

    // Map views
    ToggleTerritoryView,
    ToggleRoadNetworkView,
    ToggleResourceView,
    OpenGlobalMap,

    // Actions currently handled by the gameplay input/debug seams. Keeping
    // them here lets Controls and the direct checks use the same defaults.
    OpenStockpilePanel,
    OpenRosterPanel,
    EnterUpgradeMode,
    ToggleLogisticsOverlay,
    CenterCameraOnHeadquarters,
    ToggleNightPreview,
    ToggleDayNightCycle,
    ToggleDynamicLights,
    CycleRendererDebugView,
    GrantDebugResources,
    SpawnDebugRaid,
    AdvanceTutorialStep,
    CaptureFinalFrame,

    Count  // For iteration bounds
};

// Maps keyboard keys (raylib KEY_* constants) to game actions
class KeyBindingMap
{
public:
    KeyBindingMap();

    // Get action for a key; returns GameAction::Count if not bound
    GameAction GetAction(int raylib_key) const;

    // Set custom key binding (0 = unbind)
    void SetKeyBinding(GameAction action, int raylib_key);

    // Reset to defaults
    void ResetToDefaults();

    // Get key for action (useful for UI display); returns 0 if unbound
    int GetKeyForAction(GameAction action) const;

private:
    std::map<int, GameAction> keyToAction;      // raylib_key -> GameAction
    std::map<GameAction, int> actionToKey;      // GameAction -> raylib_key

    void SetDefault(GameAction action, int raylib_key);
};

// Defaults shared by gameplay input, direct debug checks and the Controls
// screen. Callers should treat this as read-only configuration.
const KeyBindingMap& GetDefaultKeyBindings();

// Human-readable key name used by the Controls screen and diagnostics.
// Returns an empty string for an unbound/unknown key.
std::string GetKeyDisplayName(int raylib_key);

// Formats one action's current binding, including a stable fallback for an
// intentionally unbound action.
std::string GetBindingDisplayName(const KeyBindingMap& bindings, GameAction action);

#endif
