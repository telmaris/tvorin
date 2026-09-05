#ifndef UI_UPGRADE_GESTURE_TARGETS_H
#define UI_UPGRADE_GESTURE_TARGETS_H

#include <cstddef>
#include <set>

// Tracks buildings already submitted by one Upgrade drag. The caller keeps
// submission order; this set makes footprint revisits and retraced segments
// no-ops.
class UpgradeGestureTargets
{
public:
    void Reset() { visitedBuildingIds.clear(); }

    bool InsertIfNew(int buildingId)
    {
        return visitedBuildingIds.insert(buildingId).second;
    }

    std::size_t Size() const { return visitedBuildingIds.size(); }

private:
    std::set<int> visitedBuildingIds;
};

#endif
