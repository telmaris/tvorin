#include "ui/BuildInteractionState.h"
#include "ui/GuiController.h"
#include "economy/ProductionBuildings.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

TEST(BuildInteractionStateTests, ActivationStartsACompleteBrowseState)
{
    EXPECT_EQ(ApplyBuildInteractionEvent(BuildInteractionState::Placement,
                                          BuildInteractionEvent::Activate),
              BuildInteractionState::Browse);
}

TEST(BuildInteractionStateTests, SelectionAndSuccessfulPlacementStayInPlacement)
{
    const auto placement = ApplyBuildInteractionEvent(BuildInteractionState::Browse,
                                                       BuildInteractionEvent::SelectOption);
    EXPECT_EQ(placement, BuildInteractionState::Placement);
    EXPECT_EQ(ApplyBuildInteractionEvent(placement, BuildInteractionEvent::PlaceSuccess),
              BuildInteractionState::Placement);
}

TEST(BuildInteractionStateTests, ResetReturnsToBrowseForTheNextActivation)
{
    EXPECT_EQ(ApplyBuildInteractionEvent(BuildInteractionState::Placement,
                                          BuildInteractionEvent::Reset),
              BuildInteractionState::Browse);
}

TEST(BuildInteractionStateTests, PlacementHidesPanelInputButKeepsMapAndCameraInput)
{
    const auto policy = ResolveBuildInteractionInputPolicy(BuildInteractionState::Placement);
    EXPECT_FALSE(policy.panelInteractive);
    EXPECT_TRUE(policy.placementInteractive);
    EXPECT_TRUE(policy.rmbStartsCameraDrag);
}

TEST(BuildInteractionStateTests, BrowseRoutesClicksToPanelAndAlsoAllowsCameraDrag)
{
    const auto policy = ResolveBuildInteractionInputPolicy(BuildInteractionState::Browse);
    EXPECT_TRUE(policy.panelInteractive);
    EXPECT_FALSE(policy.placementInteractive);
    EXPECT_TRUE(policy.rmbStartsCameraDrag);
}

TEST(BuildInteractionStateTests, BuildPanelPersistsDirectScrollbarOffsetPerTab)
{
    BuildPanelWidget panel;
    panel.activeCategory = "Materials";
    panel.maxScrollOffset = 240.0f;

    panel.SetScrollOffset(135.0f);

    EXPECT_FLOAT_EQ(panel.scrollOffset, 135.0f);
    EXPECT_FLOAT_EQ(panel.categoryScrollOffsets.at("Materials"), 135.0f);
}

TEST(BuildInteractionStateTests, BuildPanelHitTestingUsesActiveFilteredViewAfterScroll)
{
    std::vector<BuildOption> options(7);
    for (int index = 0; index < 7; ++index)
    {
        options[index].name = "Food " + std::to_string(index);
        options[index].category = "Food";
    }

    BuildPanelWidget panel;
    panel.pos = {100, 100};
    panel.size = {600, 700};
    panel.options = &options;
    panel.activeCategory = "Food";

    const int margin = std::max(9, panel.size.x / 64);
    const int titleBar = std::max(58, panel.size.y / 14);
    const float viewportTop = panel.pos.y + titleBar + margin + 38.0f + 7.0f;
    const float scrollBoxInset = static_cast<float>(margin) + 12.0f;
    const float gridX = panel.pos.x + scrollBoxInset + 10.0f;
    const float contentWidth = panel.size.x - scrollBoxInset * 2.0f - 10.0f - 22.0f;
    const float cardWidth = (contentWidth - 14.0f) / 3.0f;
    const float cardHeight = std::max(108.0f, cardWidth * 0.92f);

    panel.scrollOffset = cardHeight + 7.0f;
    const Vec2i point{static_cast<int>(gridX + cardWidth * 0.5f),
                      static_cast<int>(viewportTop + cardHeight * 0.5f)};

    EXPECT_EQ(panel.GetOptionAt(point), 3);
}

TEST(BuildInteractionStateTests, BuildingInfoPanelGrowsForLongSupplierLists)
{
    ConfiguredProductionBuilding foundry(100, BuildingType::Foundry);
    Building supplierA(101);
    Building supplierB(102);
    Building supplierC(103);
    Building supplierD(104);
    auto* logistics = foundry.GetComponent<LogisticsComponent>();
    ASSERT_NE(logistics, nullptr);
    logistics->suppliers[ResourceType::COAL] = {&supplierA, &supplierB};
    logistics->suppliers[ResourceType::IRON_ORE] = {&supplierC, &supplierD};

    BuildingInfoPanel panel;
    panel.ChangePositionAnchor({0.66f, 0.13f});
    panel.ChangeSizeAnchor({0.31f, 0.82f});
    panel.building = &foundry;
    panel.UpdateSize({1920, 1080});

    EXPECT_GT(panel.size.y, static_cast<int>(1080 * 0.69f));
    EXPECT_LE(panel.pos.y + panel.size.y, 1080 - 8);
}
