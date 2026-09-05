#include "../src/ui/GuiInternal.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

namespace
{
    std::array<Rectangle, 8> ActionRectangles(const StrategicResourceHudWidget& hud)
    {
        return {
            BuildHudButtonRect(hud), DestroyHudButtonRect(hud), RoadHudButtonRect(hud),
            StatsHudButtonRect(hud), RosterHudButtonRect(hud), FocusHudButtonRect(hud),
            TechHudButtonRect(hud), GlobalMapHudButtonRect(hud)
        };
    }
}

TEST(StrategicHudLayoutTests, ActionStripUsesEqualAdjacentSlotsAtMinimumWindow)
{
    StrategicResourceHudWidget hud;
    hud.ChangePosition(4, 4);
    hud.ChangeSize(1280 - 8, 106);

    const auto rects = ActionRectangles(hud);
    for (size_t index = 1; index < rects.size(); ++index)
    {
        EXPECT_FLOAT_EQ(rects[index - 1].width, rects[index].width);
        EXPECT_FLOAT_EQ(rects[index - 1].height, rects[index].height);
        EXPECT_FLOAT_EQ(rects[index - 1].x + rects[index - 1].width + 4.0f,
                        rects[index].x);
        EXPECT_FLOAT_EQ(rects[index - 1].y, rects[index].y);
    }

    EXPECT_GT(rects.back().x, rects.front().x);
    const float cornerScale = std::min(1.0f, hud.size.y / 148.0f);
    EXPECT_LE(rects.back().x + rects.back().width,
              hud.pos.x + hud.size.x - (128.0f * cornerScale + 16.0f));
}

TEST(StrategicHudLayoutTests, GlobalMapIsRightmostAndUsesActionSlotSize)
{
    StrategicResourceHudWidget hud;
    hud.ChangePosition(4, 4);
    hud.ChangeSize(1920 - 8, 130);

    const auto rects = ActionRectangles(hud);
    const Rectangle& globalMap = rects.back();
    for (size_t index = 0; index + 1 < rects.size(); ++index)
    {
        EXPECT_LT(rects[index].x, globalMap.x);
        EXPECT_FLOAT_EQ(rects[index].width, globalMap.width);
        EXPECT_FLOAT_EQ(rects[index].height, globalMap.height);
    }
}
