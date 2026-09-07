#ifndef KEYBINDINGS_H
#define KEYBINDINGS_H

#include <map>
#include <string>

enum class GameAction
{
    EnterBuildMode,
    EnterDestroyMode,
    EnterRoadMode,
    OpenResearchPanel,
    OpenStatsPanel,
    OpenFocusTree,
    OpenGlobalMap,
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

    Count
};

class KeyBindingMap
{
public:
    KeyBindingMap();

    GameAction GetAction(int raylib_key) const;
    void SetKeyBinding(GameAction action, int raylib_key);
    void ResetToDefaults();
    int GetKeyForAction(GameAction action) const;

private:
    std::map<int, GameAction> keyToAction;
    std::map<GameAction, int> actionToKey;

    void SetDefault(GameAction action, int raylib_key);
};

// Shared by gameplay input and the Controls screen.
const KeyBindingMap& GetDefaultKeyBindings();

// Human-readable key name used by the Controls screen and diagnostics.
// Returns an empty string for an unbound/unknown key.
std::string GetKeyDisplayName(int raylib_key);

// Formats one action's current binding, including a stable fallback for an
// intentionally unbound action.
std::string GetBindingDisplayName(const KeyBindingMap& bindings, GameAction action);

#endif
