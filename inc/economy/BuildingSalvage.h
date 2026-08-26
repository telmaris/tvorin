#ifndef BUILDING_SALVAGE_H
#define BUILDING_SALVAGE_H

#include "data/Resource.h"

#include <string>
#include <vector>

class Building;
class Player;
class TileMap;

struct DemolitionResourceLine
{
    ResourceType type{ResourceType::Null};
    int bufferedAmount{0};
    int refundAmount{0};
    int returnedAmount{0};
    int lostAmount{0};
};

struct DemolitionPreview
{
    bool allowed{false};
    std::string reason;
    bool unfinished{false};
    std::vector<DemolitionResourceLine> resources;
};

// Both UI and the command authority use this pure, non-mutating plan builder.
DemolitionPreview BuildDemolitionPreview(const TileMap& tilemap,
                                         const Building& building,
                                         const Player& owner);

// Rebuilds and applies the plan atomically. Existing Resource* instances are
// moved into warehouse buffers; only construction refunds are newly allocated.
bool ExecuteDemolition(TileMap& tilemap, Player& owner, Building& building);

#endif
