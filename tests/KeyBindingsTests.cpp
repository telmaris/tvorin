#include "ui/Input.h"
#include "ui/KeyBindings.h"

#include <gtest/gtest.h>

TEST(KeyBindingsTests, DefaultsMatchActiveGameplayControls)
{
    const KeyBindingMap& bindings = GetDefaultKeyBindings();

    EXPECT_EQ(bindings.GetKeyForAction(GameAction::EnterBuildMode), KEY_Q);
    EXPECT_EQ(bindings.GetKeyForAction(GameAction::EnterRoadMode), KEY_R);
    EXPECT_EQ(bindings.GetKeyForAction(GameAction::EnterDestroyMode), KEY_D);
    EXPECT_EQ(bindings.GetKeyForAction(GameAction::OpenStatsPanel), KEY_S);
    EXPECT_EQ(bindings.GetKeyForAction(GameAction::OpenGlobalMap), KEY_M);
    EXPECT_EQ(bindings.GetKeyForAction(GameAction::ToggleNightPreview), KEY_F5);
    EXPECT_EQ(bindings.GetKeyForAction(GameAction::CaptureFinalFrame), KEY_F9);
    EXPECT_EQ(bindings.GetKeyForAction(GameAction::AdvanceTutorialStep), KEY_F11);

    EXPECT_EQ(bindings.GetKeyForAction(GameAction::SaveGame), 0);
    EXPECT_EQ(bindings.GetKeyForAction(GameAction::LoadGame), 0);
}

TEST(KeyBindingsTests, DisplayNamesAndInputProcessorUseTheSameDefaults)
{
    EXPECT_EQ(GetKeyDisplayName(KEY_M), "M");
    EXPECT_EQ(GetKeyDisplayName(KEY_F10), "F10");
    EXPECT_EQ(GetBindingDisplayName(GetDefaultKeyBindings(), GameAction::OpenGlobalMap), "M");
    EXPECT_EQ(GetBindingDisplayName(GetDefaultKeyBindings(), GameAction::SaveGame), "Unbound");

    InputProcessor processor;
    processor.Init(nullptr);
    EXPECT_EQ(processor.actionInputs[OPEN_BUILD_GUI].key, KEY_Q);
    EXPECT_EQ(processor.actionInputs[OPEN_GLOBAL_MAP_GUI].key, KEY_M);
    EXPECT_EQ(processor.actionInputs[DEBUG_GRANT_RESOURCES].key, KEY_F10);
}
