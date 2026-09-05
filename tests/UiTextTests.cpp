#include "ui/UiText.h"

#include <gtest/gtest.h>

#include <cmath>

TEST(UiTextTests, RasterResolverMatchesRequestedPhysicalSize)
{
    EXPECT_EQ(UiText::ResolveRasterSize(20, {1.0f, 1.0f}), 20);
    EXPECT_EQ(UiText::ResolveRasterSize(20, {1.5f, 1.0f}), 30);
    EXPECT_EQ(UiText::ResolveRasterSize(14, {1.25f, 1.25f}), 18);
    EXPECT_EQ(UiText::ResolveRasterSize(40, {2.0f, 1.0f}), 80);
}

TEST(UiTextTests, ResolverNeverReturnsAnAtlasSmallerThanRequestedPixels)
{
    for (int logicalSize : {8, 12, 14, 20, 28, 40, 72})
        for (float dpi : {1.0f, 1.25f, 1.5f, 2.0f})
            EXPECT_GE(UiText::ResolveRasterSize(logicalSize, {dpi, dpi}),
                      static_cast<int>(std::ceil(logicalSize * dpi)));
}

TEST(UiTextTests, TextureFilterIsPointForEveryDpiAndFontSize)
{
    EXPECT_EQ(UiText::ResolveTextureFilter(20, {1.0f, 1.0f}), TEXTURE_FILTER_POINT);
    EXPECT_EQ(UiText::ResolveTextureFilter(16, {1.5f, 1.5f}), TEXTURE_FILTER_POINT);
    EXPECT_EQ(UiText::ResolveTextureFilter(19, {1.0f, 1.0f}), TEXTURE_FILTER_POINT);
    EXPECT_EQ(UiText::ResolveTextureFilter(20, {1.5f, 1.5f}), TEXTURE_FILTER_POINT);
}

TEST(UiTextTests, CoordinatesSnapToPhysicalPixelGrid)
{
    EXPECT_FLOAT_EQ(UiText::SnapToPhysicalPixel(10.4f, 1.0f), 10.0f);
    EXPECT_FLOAT_EQ(UiText::SnapToPhysicalPixel(10.4f, 1.25f), 10.4f);
    EXPECT_NEAR(UiText::SnapToPhysicalPixel(10.5f, 1.5f), 10.666667f, 0.00001f);
}
