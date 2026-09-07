#ifndef INPUT_H
#define INPUT_H

#include "raylib.h"
#include "ui/KeyBindings.h"

class GuiController;

// Logical input actions consumed by GUI controller systems.
enum InputMap
{
    ACTION_NULL = 0,

    CLOSE_TOP_GUI,
    OPEN_BUILD_GUI,
    OPEN_ROAD_BUILD_GUI,
    OPEN_DESTROY_GUI,
    OPEN_HEADQUARTERS_GUI,
    OPEN_STATS_GUI,
    OPEN_FOCUS_GUI,
    OPEN_TECH_GUI,
    OPEN_ROSTER_GUI,
    OPEN_UPGRADE_GUI,
    OPEN_GLOBAL_MAP_GUI,
    CENTER_CAMERA_ON_HEADQUARTERS,
    DEBUG_GRANT_RESOURCES,
    DEBUG_SPAWN_RAID,

    LEFT_BUTTON_DOWN,
    LEFT_BUTTON_UP,
    RIGHT_BUTTON_DOWN,
    RIGHT_BUTTON_UP,
    MIDDLE_BUTTON_DOWN,
    MIDDLE_BUTTON_UP,
    MOUSE_SCROLL,


    MAX_ACTION
};

// Physical key or mouse binding for one logical action.
struct ActionInput
{
    int key = -1;
    int button = -1;
};

// Polls Raylib input and exposes action-oriented queries.
struct InputProcessor
{
    // Assigns default bindings and target controller (full gameplay key set).
    inline void Init(GuiController*);
    // Assigns a minimal binding set for single-screen menu scenes: ESC + LMB
    // only (scroll needs no binding — HandleInputs polls the wheel directly).
    inline void InitMenu(GuiController*);
    // Dispatches currently active input actions to the controller.
    void HandleInputs();
    // Returns true on the frame an action becomes active.
    bool IsActionPressed(int action);
    // Returns true on the frame an action is released.
    bool IsActionReleased(int action);
    // Returns true while an action is held.
    bool IsActionDown(int action);

    void SetDebugActionsEnabled(bool enabled) { debugActionsEnabled = enabled; }


    ActionInput actionInputs[MAX_ACTION];

    GuiController* controller{nullptr};
    bool debugActionsEnabled{true};
};

inline void InputProcessor::Init(GuiController* gui)
{
    controller = gui;

    for (auto& input : actionInputs)
        input = {};

    const KeyBindingMap& bindings = GetDefaultKeyBindings();
    auto bind = [&](int action, GameAction gameAction)
    {
        actionInputs[action].key = bindings.GetKeyForAction(gameAction);
    };

    actionInputs[CLOSE_TOP_GUI].key = KEY_ESCAPE;
    bind(OPEN_BUILD_GUI, GameAction::EnterBuildMode);
    bind(OPEN_ROAD_BUILD_GUI, GameAction::EnterRoadMode);
    bind(OPEN_DESTROY_GUI, GameAction::EnterDestroyMode);
    bind(OPEN_HEADQUARTERS_GUI, GameAction::OpenStockpilePanel);
    bind(OPEN_STATS_GUI, GameAction::OpenStatsPanel);
    bind(OPEN_FOCUS_GUI, GameAction::OpenFocusTree);
    bind(OPEN_TECH_GUI, GameAction::OpenResearchPanel);
    bind(OPEN_ROSTER_GUI, GameAction::OpenRosterPanel);
    bind(OPEN_UPGRADE_GUI, GameAction::EnterUpgradeMode);
    bind(OPEN_GLOBAL_MAP_GUI, GameAction::OpenGlobalMap);
    bind(CENTER_CAMERA_ON_HEADQUARTERS, GameAction::CenterCameraOnHeadquarters);
    bind(DEBUG_GRANT_RESOURCES, GameAction::GrantDebugResources);
    bind(DEBUG_SPAWN_RAID, GameAction::SpawnDebugRaid);
    
    actionInputs[LEFT_BUTTON_DOWN].button = MOUSE_BUTTON_LEFT;
    actionInputs[LEFT_BUTTON_UP].button = MOUSE_BUTTON_LEFT;
    actionInputs[RIGHT_BUTTON_DOWN].button = MOUSE_BUTTON_RIGHT;
    actionInputs[RIGHT_BUTTON_UP].button = MOUSE_BUTTON_RIGHT;
    actionInputs[MIDDLE_BUTTON_DOWN].button = MOUSE_BUTTON_MIDDLE;
    actionInputs[MIDDLE_BUTTON_UP].button = MOUSE_BUTTON_MIDDLE;
}

inline void InputProcessor::InitMenu(GuiController* gui)
{
    controller = gui;

    for (auto& input : actionInputs)
        input = {};

    actionInputs[CLOSE_TOP_GUI].key = KEY_ESCAPE;

    actionInputs[LEFT_BUTTON_DOWN].button = MOUSE_BUTTON_LEFT;
    actionInputs[LEFT_BUTTON_UP].button = MOUSE_BUTTON_LEFT;
}





#endif
