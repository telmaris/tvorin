#include "warfare/DefenseCoverage.h"

#include "economy/Building.h"
#include "economy/BuildingComponents.h"
#include "simulation/MapGenerator.h"

#include <algorithm>
#include <cmath>

Vec2f DefenseCoverageService::GetBuildingCenter(const TileMap& map,
                                                 const Building& building)
{
    if (building.positionId < 0 || building.positionId >= static_cast<int>(map.tilemap.size()))
        return {};

    const Vec2i anchor = map.GetCoordsFromId(building.positionId);
    const Vec2i footprint = building.GetFootprint();
    return Vec2f{
        static_cast<float>(anchor.x) + std::max(1, footprint.x) * 0.5f,
        static_cast<float>(anchor.y) + std::max(1, footprint.y) * 0.5f};
}

DefenseCoverageView DefenseCoverageService::GetCoverage(const TileMap& map,
                                                        const Building& building)
{
    DefenseCoverageView result;
    result.center = GetBuildingCenter(map, building);
    const auto* coverage = building.GetComponent<DefenseCoverageComponent>();
    if (coverage == nullptr)
        return result;

    result.radius = std::max(0.0, coverage->GetEffectiveRadius(building));
    result.protection = std::max(0.0, coverage->GetEffectiveProtection(building));
    return result;
}

bool DefenseCoverageService::Covers(const TileMap& map, const Building& defender,
                                    const Building& target)
{
    const DefenseCoverageView view = GetCoverage(map, defender);
    if (view.radius <= 0.0 || defender.IsUnderConstruction() || target.IsUnderConstruction())
        return false;

    const Vec2f targetCenter = GetBuildingCenter(map, target);
    const double dx = static_cast<double>(targetCenter.x - view.center.x);
    const double dy = static_cast<double>(targetCenter.y - view.center.y);
    return dx * dx + dy * dy <= view.radius * view.radius;
}

int DefenseCoverageService::ProtectionToBasisPoints(double protection)
{
    return std::clamp(static_cast<int>(std::lround(std::max(0.0, protection) * 100.0)), 0, 10000);
}

int DefenseCoverageService::CombineProtectionBasisPoints(
    const std::vector<int>& protectionBasisPoints)
{
    long double remainingRisk = 1.0L;
    for (int protection : protectionBasisPoints)
    {
        const int clamped = std::clamp(protection, 0, 10000);
        remainingRisk *= 1.0L - static_cast<long double>(clamped) / 10000.0L;
    }

    const long double combined = (1.0L - remainingRisk) * 10000.0L;
    return std::clamp(static_cast<int>(std::lround(combined)), 0, 10000);
}
