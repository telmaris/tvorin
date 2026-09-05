#include "world/JourneyTiming.h"

#include <gtest/gtest.h>

namespace
{
    GlobalMap MakeTimingMap(bool useTestRoute = false)
    {
        GlobalMap map;
        map.AddProvince(std::make_unique<StaticProvince>(1, ProvinceKind::Buildable,
                                                         Vec2i{0, 0}));
        map.AddProvince(std::make_unique<StaticProvince>(2, ProvinceKind::Buildable,
                                                         Vec2i{300, 0}));
        map.AddProvince(std::make_unique<StaticProvince>(3, ProvinceKind::Buildable,
                                                         Vec2i{600, 0}));
        map.AddProvince(std::make_unique<StaticProvince>(4, ProvinceKind::Buildable,
                                                         Vec2i{900, 0}));
        EXPECT_TRUE(map.AddConnection(1, 2, 10,
                                      useTestRoute ? "timing_test_route" : "land_route"));
        EXPECT_TRUE(map.AddConnection(2, 3, 20));
        EXPECT_TRUE(map.AddConnection(3, 4, 30));
        return map;
    }
}

TEST(JourneyTimingTests, UsesCanonicalMoverAndRouteTimeInFixedTicks)
{
    const GlobalMap map = MakeTimingMap(true);
    JourneySpeedProfile profile;
    profile.moverSpeedBasisPoints = 7500;

    const JourneyTimingQuote quote = JourneyTiming::QuoteForPath(
        map, 1, 2, 0, {10}, profile);
    ASSERT_TRUE(quote.valid) << quote.failureReason;
    EXPECT_EQ(quote.physicalDistanceUnits, 300);
    EXPECT_EQ(quote.effectiveDistanceUnits, 270);
    EXPECT_EQ(quote.totalDurationTicks, 21600u);
    ASSERT_EQ(quote.legs.size(), 1u);
    EXPECT_EQ(quote.legs.front().durationTicks, 21600u);
}

TEST(JourneyTimingTests, AppliesTenPercentPenaltyPerAdditionalLeg)
{
    const GlobalMap map = MakeTimingMap();
    const JourneyTimingQuote quote = JourneyTiming::QuoteForPath(
        map, 1, 4, 0, {10, 20, 30});
    ASSERT_TRUE(quote.valid) << quote.failureReason;
    EXPECT_EQ(quote.physicalDistanceUnits, 900);
    EXPECT_EQ(quote.effectiveDistanceUnits, 1089);
    EXPECT_EQ(quote.totalDurationTicks, 65340u);
    ASSERT_EQ(quote.legs.size(), 3u);
    EXPECT_EQ(quote.legs[0].durationTicks + quote.legs[1].durationTicks +
                  quote.legs[2].durationTicks,
              quote.totalDurationTicks);
}
