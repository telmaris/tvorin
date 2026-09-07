#include "world/RouteIncidentRisk.h"

#include <gtest/gtest.h>

TEST(RouteIncidentRiskTests, LengthExposureIsMonotonicAndSubliminary)
{
    const auto shortRoute = QuoteRouteIncidentRisk({250, 1, 10000, 0, 10000});
    const auto mediumRoute = QuoteRouteIncidentRisk({250, 4, 10000, 0, 10000});
    const auto longRoute = QuoteRouteIncidentRisk({250, 16, 10000, 0, 10000});

    EXPECT_LT(shortRoute.negativeChanceBasisPoints, mediumRoute.negativeChanceBasisPoints);
    EXPECT_LT(mediumRoute.negativeChanceBasisPoints, longRoute.negativeChanceBasisPoints);
    EXPECT_EQ(mediumRoute.lengthExposureBasisPoints, 2500);
    EXPECT_EQ(longRoute.lengthExposureBasisPoints, 5000);
    EXPECT_LE(longRoute.negativeChanceBasisPoints - mediumRoute.negativeChanceBasisPoints,
              2 * (mediumRoute.negativeChanceBasisPoints - shortRoute.negativeChanceBasisPoints));
}

TEST(RouteIncidentRiskTests, QualityAndRoadReductionAreMultiplicative)
{
    const auto neutral = QuoteRouteIncidentRisk({1000, 1, 10000, 0, 10000});
    const auto poor = QuoteRouteIncidentRisk({1000, 1, 7500, 0, 10000});
    const auto good = QuoteRouteIncidentRisk({1000, 1, 12500, 0, 10000});
    const auto upgraded = QuoteRouteIncidentRisk({1000, 1, 10000, 1000, 10000});

    EXPECT_GT(poor.negativeChanceBasisPoints, neutral.negativeChanceBasisPoints);
    EXPECT_LT(good.negativeChanceBasisPoints, neutral.negativeChanceBasisPoints);
    EXPECT_GT(upgraded.negativeChanceBasisPoints, 0);
    EXPECT_LT(upgraded.negativeChanceBasisPoints, neutral.negativeChanceBasisPoints);
}
