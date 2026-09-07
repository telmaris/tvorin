#ifndef BUILDING_PRESENTATION_H
#define BUILDING_PRESENTATION_H

#include "economy/Building.h"

#include <string>
#include <vector>

// Stable, short player-facing status for a building. The general status is
// intentionally separate from the optional production line so enemy hover
// can hide recipe/buffer details without changing the status hierarchy.
struct BuildingPresentationStatus
{
    std::string general;
    std::string production;
};

struct VillagePresentationView
{
    double inhabitants{0.0};
    int populationCap{0};
    double manpowerGainPerMinute{0.0};
    std::vector<PopulationComponent::SupplyConsumptionView> supplies;
};

VillagePresentationView BuildVillagePresentationView(const Building& building);

enum class BuildingPresentationAudience
{
    Owner,
    Opponent
};

enum class BuildingFootprintOverlay
{
    Disconnected,
    Upgrading
};

// Pure status formatting. Connectivity is supplied by the caller because
// checking it may walk the road network and must therefore be cached/throttled
// by a UI owner rather than recomputed for every panel/widget draw.
BuildingPresentationStatus BuildBuildingPresentationStatus(const Building& building);
BuildingPresentationStatus BuildBuildingPresentationStatus(const Building& building,
                                                            bool roadDisconnected);
BuildingPresentationStatus BuildBuildingPresentationStatus(
    const Building& building,
    bool roadDisconnected,
    BuildingPresentationAudience audience);

// Shared live/snapshot footprint marker. Both render paths pass their common
// virtual render dimensions so color, alpha and pulse cannot drift apart.
void DrawBuildingFootprintStatusOverlay(Vec2f position,
                                        Vec2i footprint,
                                        float renderHeight,
                                        float tileSize,
                                        BuildingFootprintOverlay overlay);

#endif
