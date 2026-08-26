#ifndef VISIBLE_TILE_BOUNDS_H
#define VISIBLE_TILE_BOUNDS_H

#include "core/Types.h"

#include <algorithm>
#include <cmath>

struct VisibleTileBounds
{
    int minX{0};
    int maxX{-1};
    int minY{0};
    int maxY{-1};
};

// Computes the same camera culling rectangle for live and snapshot rendering.
// `tileMargin` is the maximum anchor overhang needed by rendered footprints.
inline VisibleTileBounds ComputeVisibleTileBounds(
    Vec2f worldA, Vec2f worldB, Vec2i mapSize, int tileMargin)
{
    if (mapSize.x <= 0 || mapSize.y <= 0)
        return {};

    const float minWorldX = std::min(worldA.x, worldB.x);
    const float maxWorldX = std::max(worldA.x, worldB.x);
    const float minWorldY = std::min(worldA.y, worldB.y);
    const float maxWorldY = std::max(worldA.y, worldB.y);
    const int margin = std::max(0, tileMargin);

    return {
        std::clamp(static_cast<int>(std::floor(minWorldX / TILE_SIZE)) - margin, 0, mapSize.x - 1),
        std::clamp(static_cast<int>(std::ceil(maxWorldX / TILE_SIZE)) + margin, 0, mapSize.x - 1),
        std::clamp(static_cast<int>(std::floor(minWorldY / TILE_SIZE)) - margin, 0, mapSize.y - 1),
        std::clamp(static_cast<int>(std::ceil(maxWorldY / TILE_SIZE)) + margin, 0, mapSize.y - 1)};
}

#endif
