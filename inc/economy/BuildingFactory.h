#ifndef BUILDING_FACTORY_H
#define BUILDING_FACTORY_H

#include "simulation/MapGenerator.h"
#include "economy/ProductionBuildings.h"

// Small helper that creates player-owned buildings on a tile map.
struct BFactory
{
    BFactory() = default;
    BFactory(Player* p, TileMap& map, int ownerId, ProvinceId provinceId = InvalidProvinceId)
        : player(p), tilemap(&map), playerId(ownerId),
          buildingIdPrefix(ownerId * 5'000'000 +
                           (provinceId == InvalidProvinceId ? 0 : static_cast<int>(provinceId)) * 300'000) {}

    void RebindTileMap(TileMap& map) { tilemap = &map; }

    // Constructs a building type and places it on the requested tile.
    template <typename T> void Build(int tilePos)
    {
        auto bld = std::make_unique<T>(buildingIdPrefix + buildingId++);
        tilemap->BuildOnTile(tilePos, player, std::move(bld));
    }

    Player* player{nullptr};
    TileMap* tilemap{nullptr};
    int playerId = 0;
    int buildingIdPrefix = 0;
    int buildingId = 0;
};

#endif
