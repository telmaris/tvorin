#include "ui/KeyBindings.h"
#include "raylib.h"

KeyBindingMap::KeyBindingMap()
{
    ResetToDefaults();
}

void KeyBindingMap::ResetToDefaults()
{
    keyToAction.clear();
    actionToKey.clear();

    // Gameplay bindings are kept in sync with InputProcessor::Init and the
    // direct panel/debug checks. Unused legacy actions remain explicitly
    // unbound instead of advertising keys that do something else.
    SetDefault(GameAction::EnterBuildMode, KEY_Q);
    SetDefault(GameAction::EnterDestroyMode, KEY_D);
    SetDefault(GameAction::EnterRoadMode, KEY_R);
    SetDefault(GameAction::TogglePauseGame, 0);

    // Camera panning and zooming are mouse gestures in the current game.
    SetDefault(GameAction::CameraUp, 0);
    SetDefault(GameAction::CameraDown, 0);
    SetDefault(GameAction::CameraLeft, 0);
    SetDefault(GameAction::CameraRight, 0);
    SetDefault(GameAction::CameraZoomIn, 0);
    SetDefault(GameAction::CameraZoomOut, 0);

    // UI panels
    SetDefault(GameAction::OpenResearchPanel, KEY_T);
    SetDefault(GameAction::OpenStatsPanel, KEY_S);
    SetDefault(GameAction::OpenFocusTree, KEY_F);

    // Save/load hotkeys are not active gameplay actions. F5/F9 are reserved
    // for renderer diagnostics and Ctrl+F9 final-frame capture.
    SetDefault(GameAction::SaveGame, 0);
    SetDefault(GameAction::LoadGame, 0);

    // Map views are currently represented by the logistics overlay and the
    // global-map panel, not by the removed legacy number-key overlays.
    SetDefault(GameAction::ToggleTerritoryView, 0);
    SetDefault(GameAction::ToggleRoadNetworkView, 0);
    SetDefault(GameAction::ToggleResourceView, 0);
    SetDefault(GameAction::OpenGlobalMap, KEY_M);

    SetDefault(GameAction::OpenStockpilePanel, KEY_E);
    SetDefault(GameAction::OpenRosterPanel, KEY_U);
    SetDefault(GameAction::EnterUpgradeMode, KEY_G);
    SetDefault(GameAction::ToggleLogisticsOverlay, KEY_L);
    SetDefault(GameAction::CenterCameraOnHeadquarters, KEY_SPACE);
    SetDefault(GameAction::ToggleNightPreview, KEY_F5);
    SetDefault(GameAction::ToggleDayNightCycle, KEY_F6);
    SetDefault(GameAction::ToggleDynamicLights, KEY_F7);
    SetDefault(GameAction::CycleRendererDebugView, KEY_F8);
    SetDefault(GameAction::GrantDebugResources, KEY_F10);
    SetDefault(GameAction::SpawnDebugRaid, KEY_F2);
    SetDefault(GameAction::AdvanceTutorialStep, KEY_F11);
    SetDefault(GameAction::CaptureFinalFrame, KEY_F9);
}

GameAction KeyBindingMap::GetAction(int raylib_key) const
{
    auto it = keyToAction.find(raylib_key);
    if (it != keyToAction.end())
        return it->second;
    return GameAction::Count;
}

void KeyBindingMap::SetKeyBinding(GameAction action, int raylib_key)
{
    // Remove old binding for this action if it exists
    auto oldKey = GetKeyForAction(action);
    if (oldKey != 0)
        keyToAction.erase(oldKey);

    // Remove old binding for this key if it exists
    auto oldAction = GetAction(raylib_key);
    if (oldAction != GameAction::Count)
        actionToKey.erase(oldAction);

    // Add new binding
    if (raylib_key != 0)
    {
        keyToAction[raylib_key] = action;
        actionToKey[action] = raylib_key;
    }
    else
    {
        // Unbind
        actionToKey.erase(action);
    }
}

int KeyBindingMap::GetKeyForAction(GameAction action) const
{
    auto it = actionToKey.find(action);
    if (it != actionToKey.end())
        return it->second;
    return 0;
}

void KeyBindingMap::SetDefault(GameAction action, int raylib_key)
{
    SetKeyBinding(action, raylib_key);
}

const KeyBindingMap& GetDefaultKeyBindings()
{
    static const KeyBindingMap bindings;
    return bindings;
}

std::string GetKeyDisplayName(int raylib_key)
{
    switch (raylib_key)
    {
        case KEY_A: return "A";
        case KEY_B: return "B";
        case KEY_C: return "C";
        case KEY_D: return "D";
        case KEY_E: return "E";
        case KEY_F: return "F";
        case KEY_G: return "G";
        case KEY_H: return "H";
        case KEY_I: return "I";
        case KEY_J: return "J";
        case KEY_K: return "K";
        case KEY_L: return "L";
        case KEY_M: return "M";
        case KEY_N: return "N";
        case KEY_O: return "O";
        case KEY_P: return "P";
        case KEY_Q: return "Q";
        case KEY_R: return "R";
        case KEY_S: return "S";
        case KEY_T: return "T";
        case KEY_U: return "U";
        case KEY_V: return "V";
        case KEY_W: return "W";
        case KEY_X: return "X";
        case KEY_Y: return "Y";
        case KEY_Z: return "Z";
        case KEY_ONE: return "1";
        case KEY_TWO: return "2";
        case KEY_THREE: return "3";
        case KEY_SPACE: return "Space";
        case KEY_ESCAPE: return "ESC";
        case KEY_ENTER: return "Enter";
        case KEY_KP_ENTER: return "Numpad Enter";
        case KEY_F2: return "F2";
        case KEY_F3: return "F3";
        case KEY_F4: return "F4";
        case KEY_F5: return "F5";
        case KEY_F6: return "F6";
        case KEY_F7: return "F7";
        case KEY_F8: return "F8";
        case KEY_F9: return "F9";
        case KEY_F10: return "F10";
        case KEY_F11: return "F11";
        case KEY_LEFT_CONTROL: return "Ctrl";
        case KEY_RIGHT_CONTROL: return "Ctrl";
        case KEY_UP: return "Up";
        case KEY_DOWN: return "Down";
        case KEY_LEFT: return "Left";
        case KEY_RIGHT: return "Right";
        case KEY_BACKSPACE: return "Backspace";
        default: return {};
    }
}

std::string GetBindingDisplayName(const KeyBindingMap& bindings, GameAction action)
{
    const std::string name = GetKeyDisplayName(bindings.GetKeyForAction(action));
    return name.empty() ? "Unbound" : name;
}
