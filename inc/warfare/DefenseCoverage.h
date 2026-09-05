#ifndef DEFENSE_COVERAGE_H
#define DEFENSE_COVERAGE_H

#include "core/Types.h"

#include <vector>

class Building;
class TileMap;

struct DefenseCoverageView
{
    Vec2f center{};
    double radius{0.0};
    double protection{0.0};
};

// Pure geometry/combination helpers for province raid resolution and UI. The
// live building registry remains the source of truth; this service stores no
// cache and no building pointers.
class DefenseCoverageService
{
public:
    static Vec2f GetBuildingCenter(const TileMap& map, const Building& building);
    static DefenseCoverageView GetCoverage(const TileMap& map, const Building& building);
    static bool Covers(const TileMap& map, const Building& defender,
                       const Building& target);

    // Combines overlapping protection zones as independent risk reductions:
    // remaining risk = product(1 - protection). Inputs and output are always
    // clamped to [0, 10000] basis points.
    static int CombineProtectionBasisPoints(const std::vector<int>& protectionBasisPoints);
    static int ProtectionToBasisPoints(double protection);
};

#endif
