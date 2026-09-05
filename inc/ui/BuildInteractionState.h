#ifndef UI_BUILD_INTERACTION_STATE_H
#define UI_BUILD_INTERACTION_STATE_H

// State transitions for the standard build mode. Keeping this pure makes the
// Browse/Placement contract testable without raylib input or a live scene.
enum class BuildInteractionState
{
    Browse,
    Placement
};

enum class BuildInteractionEvent
{
    Activate,
    SelectOption,
    PlaceSuccess,
    Reset
};

struct BuildInteractionInputPolicy
{
    bool panelInteractive{false};
    bool placementInteractive{false};
    bool rmbStartsCameraDrag{true};
};

constexpr BuildInteractionInputPolicy ResolveBuildInteractionInputPolicy(
    BuildInteractionState state)
{
    return state == BuildInteractionState::Browse
        ? BuildInteractionInputPolicy{true, false, true}
        : BuildInteractionInputPolicy{false, true, true};
}

constexpr BuildInteractionState ApplyBuildInteractionEvent(
    BuildInteractionState current, BuildInteractionEvent event)
{
    switch (event)
    {
        case BuildInteractionEvent::SelectOption:
            return BuildInteractionState::Placement;
        case BuildInteractionEvent::PlaceSuccess:
            return BuildInteractionState::Placement;
        case BuildInteractionEvent::Activate:
        case BuildInteractionEvent::Reset:
            return BuildInteractionState::Browse;
    }
    return current;
}

#endif
