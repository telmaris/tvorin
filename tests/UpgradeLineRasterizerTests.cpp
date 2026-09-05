#include "ui/UpgradeLineRasterizer.h"

#include <gtest/gtest.h>

#include <algorithm>

TEST(UpgradeLineRasterizerTests, IncludesEveryEndpointForAxisAlignedDrag)
{
    const auto tiles = RasterizeUpgradeTileLine({2, 4}, {7, 4});

    ASSERT_EQ(tiles.size(), 6u);
    EXPECT_EQ(tiles.front(), (Vec2i{2, 4}));
    EXPECT_EQ(tiles.back(), (Vec2i{7, 4}));
    for (int x = 2; x <= 7; ++x)
        EXPECT_EQ(tiles[static_cast<std::size_t>(x - 2)], (Vec2i{x, 4}));
}

TEST(UpgradeLineRasterizerTests, CoversDiagonalDragWithoutDuplicateTiles)
{
    const auto tiles = RasterizeUpgradeTileLine({1, 1}, {6, 4});

    ASSERT_FALSE(tiles.empty());
    EXPECT_EQ(tiles.front(), (Vec2i{1, 1}));
    EXPECT_EQ(tiles.back(), (Vec2i{6, 4}));
    for (std::size_t i = 1; i < tiles.size(); ++i)
    {
        EXPECT_LE(std::abs(tiles[i].x - tiles[i - 1].x), 1);
        EXPECT_LE(std::abs(tiles[i].y - tiles[i - 1].y), 1);
        EXPECT_NE(tiles[i], tiles[i - 1]);
    }
}

TEST(UpgradeLineRasterizerTests, ReverseDragProducesTheReversedTileSequence)
{
    const auto forward = RasterizeUpgradeTileLine({0, 0}, {5, 3});
    const auto backward = RasterizeUpgradeTileLine({5, 3}, {0, 0});

    ASSERT_EQ(forward.size(), backward.size());
    std::vector<Vec2i> reversed = backward;
    std::reverse(reversed.begin(), reversed.end());
    EXPECT_EQ(forward, reversed);
}
