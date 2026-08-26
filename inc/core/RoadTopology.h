#ifndef ROAD_TOPOLOGY_H
#define ROAD_TOPOLOGY_H

// Pure cardinal road-topology helpers shared by live rendering, snapshot
// rendering and domain tests. The callback supplies the visual road predicate;
// simulation state is intentionally not modified or consulted here.
namespace RoadTopology
{
    enum ConnectionBit
    {
        West = 1,
        East = 2,
        North = 4,
        South = 8
    };

    template <typename IsRoadAt>
    int GetCardinalMask(int x, int y, int width, int height, IsRoadAt&& isRoadAt)
    {
        if (width <= 0 || height <= 0 || x < 0 || y < 0 || x >= width || y >= height)
            return 0;

        int mask = 0;
        if (x > 0 && isRoadAt(x - 1, y))
            mask |= West;
        if (x + 1 < width && isRoadAt(x + 1, y))
            mask |= East;
        if (y > 0 && isRoadAt(x, y - 1))
            mask |= North;
        if (y + 1 < height && isRoadAt(x, y + 1))
            mask |= South;
        return mask;
    }
}

#endif
