#include "world/RouteIncidentRisk.h"

#include <cstdint>

RouteIncidentRiskQuote QuoteRouteIncidentRisk(const RouteIncidentRiskInput& input)
{
    const int maximum = std::max(0, input.maximumChanceBasisPoints);
    const int base = std::clamp(input.baseNegativeChanceBasisPoints, 0, maximum);
    int lengthBucket = 0;
    int length = std::max(1, input.lengthUnits);
    int normalizedLength = std::max(1, length / std::max(1, input.referenceLengthUnits));
    while (normalizedLength >= 2 && lengthBucket < 30)
    {
        normalizedLength >>= 1;
        ++lengthBucket;
    }

    const int lengthExposure = std::min(maximum,
        static_cast<int>((static_cast<std::int64_t>(lengthBucket) * 12500 *
                          std::max(0, input.lengthStepBasisPoints)) / 10000));
    const std::int64_t withLength = static_cast<std::int64_t>(base) + lengthExposure;
    const int quality = std::clamp(input.routeQualityBasisPoints, 2500, 20000);
    // 10000 is the neutral quality. A faster/higher-quality route has a
    // larger quality value and therefore a smaller incident chance.
    const int qualityReduction = std::clamp(10000 - quality, -10000, 7500);
    const std::int64_t qualityAdjusted = withLength *
        static_cast<std::int64_t>(10000 + qualityReduction) / 10000;
    const int reduction = std::clamp(input.incidentReductionBasisPoints, 0, 10000);
    const std::int64_t reduced = qualityAdjusted * (10000 - reduction) / 10000;
    const int escorts = std::max(1, input.scoutEscortCount);
    const int escortReduction = std::min(7500, (escorts - 1) * 1000);
    const std::int64_t escorted = reduced * (10000 - escortReduction) / 10000;

    RouteIncidentRiskQuote quote;
    quote.lengthExposureBasisPoints = lengthExposure;
    quote.qualityReductionBasisPoints = qualityReduction;
    quote.escortReductionBasisPoints = escortReduction;
    quote.negativeChanceBasisPoints = std::clamp(
        static_cast<int>(std::clamp<std::int64_t>(escorted, 0, maximum)), 0, maximum);
    return quote;
}
