#ifndef TERRAIN_RENDER_GEOMETRY_H
#define TERRAIN_RENDER_GEOMETRY_H

#include "core/Types.h"

#include <cmath>

struct RenderPixelBounds
{
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};
};

// Computes both edges of a terrain tile in the fixed render-pixel grid. The
// shared edge is rounded once from the same world boundary, so neighbouring
// tiles cannot diverge by a pixel after zooming.
inline RenderPixelBounds CalculateTerrainRenderPixelBounds(
    Vec2f worldPosition, Vec2f worldSize, float zoom, float renderHeight)
{
    if (zoom <= 0.0f)
        return {};

    return {
        static_cast<int>(std::round(worldPosition.x * zoom)),
        static_cast<int>(std::round((renderHeight - worldPosition.y - worldSize.y) * zoom)),
        static_cast<int>(std::round((worldPosition.x + worldSize.x) * zoom)),
        static_cast<int>(std::round((renderHeight - worldPosition.y) * zoom))};
}

#endif
