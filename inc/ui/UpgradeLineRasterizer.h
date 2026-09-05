#ifndef UPGRADE_LINE_RASTERIZER_H
#define UPGRADE_LINE_RASTERIZER_H

#include "core/Types.h"

#include <cmath>
#include <vector>

// Returns every tile touched by a drag segment, including both endpoints.
// Bresenham keeps the sequence integer-only and deterministic for the same
// mouse path, which is important because each visited tile can submit a
// separate authoritative UpgradeBuilding command.
inline std::vector<Vec2i> RasterizeUpgradeTileLine(Vec2i start, Vec2i end)
{
    std::vector<Vec2i> tiles;
    const int dx = std::abs(end.x - start.x);
    const int sx = start.x < end.x ? 1 : -1;
    const int dy = -std::abs(end.y - start.y);
    const int sy = start.y < end.y ? 1 : -1;
    int error = dx + dy;
    Vec2i cursor = start;

    while (true)
    {
        tiles.push_back(cursor);
        if (cursor == end)
            break;

        const int doubledError = 2 * error;
        if (doubledError >= dy)
        {
            error += dy;
            cursor.x += sx;
        }
        if (doubledError <= dx)
        {
            error += dx;
            cursor.y += sy;
        }
    }
    return tiles;
}

#endif
