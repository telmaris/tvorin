#include "simulation/MapGenerator.h"
#include "economy/Player.h"

#include <gtest/gtest.h>

#include <array>

namespace
{
    // Creates a rectangular grass map fully owned by the supplied player.
    void FillOwnedGrassMap(TileMap& map, Player* owner, int width = 12, int height = 12)
    {
        map.params.sizeX = width;
        map.params.sizeY = height;
        map.tilemap.clear();
        map.tilemap.reserve(width * height);

        for (int i = 0; i < width * height; i++)
        {
            Tile tile{i};
            tile.owner = owner;
            tile.tileType = TileType::GRASS;
            tile.resourceRichness = 0;
            map.tilemap.push_back(std::move(tile));
        }
    }

    // Marks a footprint-sized area with resource terrain and finite richness.
    void PaintResource(TileMap& map, Vec2i anchor, Vec2i footprint, TileType type, int richness = 10)
    {
        for (int y = 0; y < footprint.y; y++)
        {
            for (int x = 0; x < footprint.x; x++)
            {
                Vec2i pos{anchor.x + x, anchor.y + y};
                Tile& tile = map.tilemap[map.GetIdFromCoords(pos)];
                tile.tileType = type;
                tile.resourceRichness = richness;
            }
        }
    }

    void PaintMineCells(TileMap& map, Vec2i anchor,
                        const std::array<TileType, 4>& types,
                        int richness = 10)
    {
        for (int index = 0; index < 4; index++)
        {
            Vec2i pos{anchor.x + index % 2, anchor.y + index / 2};
            Tile& tile = map[pos];
            tile.tileType = types[static_cast<size_t>(index)];
            tile.resourceRichness = richness;
        }
    }
}

TEST(BuildingPlacementTests, FootprintMustFitInsideMap)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrassMap(map, &player, 8, 8);

    EXPECT_TRUE(map.IsInsideFootprint({5, 5}, {3, 3}));
    EXPECT_FALSE(map.IsInsideFootprint({6, 6}, {3, 3}));
}

TEST(BuildingPlacementTests, FootprintBlockedByOccupancy)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrassMap(map, &player);

    EXPECT_TRUE(map.CanBuildFootprint({2, 2}, {2, 2}, &player));

    map.tilemap[map.GetIdFromCoords({2, 2})].buildingRef = reinterpret_cast<Building*>(0x1);
    EXPECT_FALSE(map.CanBuildFootprint({2, 2}, {2, 2}, &player));
}

TEST(BuildingPlacementTests, OwnStructuresNeverBlockPlacement)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrassMap(map, nullptr, 20, 20);

    int ownTileId = map.GetIdFromCoords({10, 10});
    map.tilemap[ownTileId].building = std::make_unique<Woodcutter>(1);
    map.tilemap[ownTileId].building->owner = &player;

    // Directly adjacent to a friendly structure — always fine, regardless of radius.
    EXPECT_TRUE(map.CanBuildFootprint({9, 10}, {1, 1}, &player));
}

TEST(BuildingPlacementTests, ResourceProducerRequiresMatchingTerrainAndRichness)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrassMap(map, &player);

    const Vec2i footprint = GetBuildingDefinition(BuildingType::Woodcutter).footprint;
    EXPECT_FALSE(map.CanPlaceBuilding(BuildingType::Woodcutter, {2, 2}, footprint, &player));

    PaintResource(map, {2, 2}, {1, 2}, TileType::WOOD);
    EXPECT_TRUE(map.CanPlaceBuilding(BuildingType::Woodcutter, {2, 2}, footprint, &player));

    map.tilemap[map.GetIdFromCoords({2, 2})].resourceRichness = 0;
    EXPECT_FALSE(map.CanPlaceBuilding(BuildingType::Woodcutter, {2, 2}, footprint, &player));
}

TEST(BuildingPlacementTests, MineSelectsTheMostAbundantTerrainFromItsWholeFootprint)
{
    const Vec2i anchor{4, 4};
    const std::array<TileType, 6> mineTerrains{
        TileType::IRON_ORE, TileType::COAL, TileType::STONE,
        TileType::COPPER_ORE, TileType::CLAY, TileType::SAND};

    for (TileType terrain : mineTerrains)
    {
        TileMap map;
        Player player{0, map};
        FillOwnedGrassMap(map, &player);
        PaintMineCells(map, anchor, {terrain, terrain, TileType::GRASS, TileType::GRASS});

        const TerrainPlacementEvaluation evaluation = map.EvaluateTerrainPlacement(
            BuildingType::Mine, anchor, {2, 2});
        EXPECT_TRUE(evaluation.valid);
        EXPECT_EQ(evaluation.matchedTerrainType, terrain);
        EXPECT_EQ(evaluation.matchingTiles, 2);
    }
}

TEST(BuildingPlacementTests, MineRequiresTwoRichTilesOfOneTerrainAndFitsInsideMap)
{
    const Vec2i anchor{4, 4};
    auto evaluate = [&](const std::array<TileType, 4>& types, int richness = 10)
    {
        TileMap map;
        Player player{0, map};
        FillOwnedGrassMap(map, &player);
        PaintMineCells(map, anchor, types, richness);
        return map.EvaluateTerrainPlacement(BuildingType::Mine, anchor, {2, 2});
    };

    EXPECT_FALSE(evaluate({TileType::GRASS, TileType::GRASS,
                           TileType::GRASS, TileType::GRASS}).valid);
    EXPECT_FALSE(evaluate({TileType::COAL, TileType::GRASS,
                           TileType::GRASS, TileType::GRASS}).valid);
    EXPECT_FALSE(evaluate({TileType::COAL, TileType::IRON_ORE,
                           TileType::GRASS, TileType::GRASS}).valid);
    EXPECT_FALSE(evaluate({TileType::COAL, TileType::COAL,
                           TileType::GRASS, TileType::GRASS}, 0).valid);

    TileMap edgeMap;
    Player edgePlayer{0, edgeMap};
    FillOwnedGrassMap(edgeMap, &edgePlayer);
    EXPECT_EQ(edgeMap.EvaluateTerrainPlacement(BuildingType::Mine, {11, 11}, {2, 2}).failure,
              TerrainPlacementFailure::OutsideMap);
}

TEST(BuildingPlacementTests, MineTieUsesDefinitionOrderAndInitializesThatVariant)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrassMap(map, &player);
    const Vec2i anchor{4, 4};
    PaintMineCells(map, anchor, {TileType::IRON_ORE, TileType::IRON_ORE,
                                 TileType::COAL, TileType::COAL});

    const TerrainPlacementEvaluation evaluation = map.EvaluateTerrainPlacement(
        BuildingType::Mine, anchor, {2, 2});
    ASSERT_TRUE(evaluation.valid);
    EXPECT_EQ(evaluation.matchedTerrainType, TileType::IRON_ORE);

    map.BuildOnTile(map.GetIdFromCoords(anchor), &player, std::make_unique<Mine>(7));
    Building* building = map.GetBuilding(anchor);
    ASSERT_NE(building, nullptr);
    const auto* production = building->GetComponent<ProductionComponent>();
    ASSERT_NE(production, nullptr);
    EXPECT_EQ(production->terrainType, TileType::IRON_ORE);
}

TEST(BuildingPlacementTests, FootprintReferencesPointBackToAnchorBuilding)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrassMap(map, &player);

    Vec2i anchor{2, 2};
    Vec2i footprint = GetBuildingDefinition(BuildingType::Woodcutter).footprint;
    PaintResource(map, anchor, footprint, TileType::WOOD);

    int anchorId = map.GetIdFromCoords(anchor);
    map.BuildOnTile(anchorId, &player, std::make_unique<Woodcutter>(42));

    Building* anchorBuilding = map.GetBuilding(anchor);
    ASSERT_NE(anchorBuilding, nullptr);
    EXPECT_EQ(map.GetBuilding({anchor.x + 1, anchor.y}), anchorBuilding);
    EXPECT_EQ(map.GetBuilding({anchor.x, anchor.y + 1}), anchorBuilding);
}
