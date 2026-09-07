#ifndef WORLD_ROUTE_INCIDENT_RISK_H
#define WORLD_ROUTE_INCIDENT_RISK_H

#include <algorithm>

struct RouteIncidentRiskInput
{
    int baseNegativeChanceBasisPoints{0};
    int lengthUnits{1};
    int routeQualityBasisPoints{10000};
    int incidentReductionBasisPoints{0};
    int maximumChanceBasisPoints{10000};
    int scoutEscortCount{1};
    int referenceLengthUnits{1};
    int lengthStepBasisPoints{1000};
};

struct RouteIncidentRiskQuote
{
    int negativeChanceBasisPoints{0};
    int qualityReductionBasisPoints{0};
    int lengthExposureBasisPoints{0};
    int escortReductionBasisPoints{0};
};

// Integer-only, deterministic route exposure. Length grows by log2 buckets,
// route quality acts as a multiplier, and road upgrades reduce the final risk
// multiplicatively. No floating-point log or subtraction can create a chance
// outside the configured [0, maximum] interval.
RouteIncidentRiskQuote QuoteRouteIncidentRisk(const RouteIncidentRiskInput& input);

#endif
