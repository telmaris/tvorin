#ifndef MAP_GENERATOR_H
#define MAP_GENERATOR_H

#include "core/Types.h"
#include "economy/Building.h"

#include <map>
#include <memory>
#include <optional>
#include <random>
#include <vector>

class Player;
struct ProvinceEconomy;

enum class MapSizePreset
{
    S,
    M,
    L,
    XL
};

// Parameters controlling one generated resource patch family. A patch only paints
// tiles whose biome is listed in allowedBiomes (empty = any biome). See
// docs/resource_world_design.md. Add a new deposit by appending one of these.
struct ResourcePatchParameters
{
    TileType type{TileType::WOOD};
    int patchCount{10};
    int minRadius{3};
    int maxRadius{8};
    std::vector<BiomeType> allowedBiomes{};
    float richnessScale{1.0f};   // multiplies base resourceRichness for this deposit
};

// Thresholds shaping the biome pass (all tunable; deterministic on seed).
struct BiomeParameters
{
    float mountainElevation{0.72f};  // elevation above this -> MOUNTAINS
    float hillElevation{0.55f};      // elevation above this -> HILLS
    float desertMoisture{0.30f};     // moisture below this (and warm lowland) -> DESERT
    float wetlandMoisture{0.72f};    // moisture above this (and low ground) -> WETLAND
    float forestMoisture{0.55f};     // moisture above this -> FOREST, else PLAINS
    float noiseScale{0.045f};        // lattice frequency; lower = larger regions
};

// Parameters controlling world generation and terrain distribution.
struct MapParameters
{
    MapSizePreset sizePreset{MapSizePreset::S};
    // Default follows the S preset. Keep these dimensions in sync with
    // MapGenerator::SizeFromPreset and the new-game UI labels.
    int sizeX{201}, sizeY{201};
    unsigned int seed{12345};
    float resourceDensity{0.18f};
    float resourceFieldSize{0.80f};
    int resourceRichness{180};
    int aiOpponentCount{1};
    int aiDifficulty{0};
    bool debugMode{false};
    BiomeParameters biome{};
    // Deposit families. allowedBiomes gates placement so chains stay realistic
    // (wood in forest, ores in hills/mountains, sand in desert). patchCount scales
    // with rarity — rare strategic resources get few patches.
    std::vector<ResourcePatchParameters> resourcePatches{
        // Common — widely available.
        {TileType::WOOD,        6, 7, 14, {BiomeType::FOREST},                       1.0f},
        {TileType::STONE,       2, 6, 11, {BiomeType::HILLS, BiomeType::MOUNTAINS},   1.10f},
        {TileType::COAL,        2, 5, 10, {BiomeType::HILLS, BiomeType::MOUNTAINS},   1.20f},
        {TileType::IRON_ORE,    2, 5, 10, {BiomeType::MOUNTAINS, BiomeType::HILLS},   1.20f},
        // Uncommon.
        {TileType::COPPER_ORE,  1, 3,  6, {BiomeType::HILLS, BiomeType::MOUNTAINS},   0.75f},
        {TileType::CLAY,         1, 3,  6, {BiomeType::PLAINS, BiomeType::WETLAND},    0.75f},
        // Rare — strategic, drives trade. Few, concentrated patches.
        {TileType::SAND,         1, 3,  6, {BiomeType::DESERT},                        0.75f},
    };
};

// Generates terrain, resources and starting area constraints for a tile map.
class MapGenerator
{
    public:
        MapGenerator() = default;

        // Fills a tile map according to the supplied generation parameters.
        void GenerateTileMap(TileMap&,MapParameters&);
        // Converts a size preset to square map side length.
        static int SizeFromPreset(MapSizePreset preset);
        // Deterministically places `playerCount` starting HQ anchors around
        // the local map on a (possibly irregular/concave) n-gon. No player is
        // special-cased to the map center; anchors[i] is simply the i-th
        // vertex in angular order.
        static std::vector<Vec2i> PickHeadquartersAnchors(const MapParameters& params, int playerCount);
        // Returns the fixed headquarters footprint.
        static Vec2i HeadquartersFootprint() { return {4, 4}; }
        // Returns the starting territory side length around headquarters.
        // Widened 27 -> 35 (2026-07-17, user request): the starting zone
        // scales with the 10-tile HQ build clearance + the 14-tile village +
        // the pushed-out resource patch ring (see PlaceStartingResourcePatch).
        static int HeadquartersTerritorySize() { return 35; }
        // Ensures a starting territory contains required early resources.
        static void PrepareStartingArea(TileMap&, Vec2i hqAnchor, std::mt19937&);
        static ResourceType ResourceTypeFromTileType(TileType type);
        static void FilterResourcePatchesForProfile(
            MapParameters&, const std::vector<ResourceType>& naturalResourceTypes);

    private:
        // Assigns a biome to every tile from elevation + moisture noise fields.
        void GenerateBiomes(TileMap&, const MapParameters&, std::mt19937&);
        // Generates every configured resource patch group.
        void GenerateResourcePatches(TileMap&, const MapParameters&, std::mt19937&);
        // Generates one organic resource patch, gated by the patch's allowed biomes.
        void GeneratePatch(TileMap&, const ResourcePatchParameters&, std::mt19937&);
        // Replaces generic fillers with directional fade tiles on the actual
        // tile contour of every generated overlay deposit.
        void RefreshResourceOverlayEdges(TileMap&, std::mt19937&);
};

// One square of terrain with optional owning player and building occupancy.
class Tile
{
    public:
        Tile() = default;
        Tile(int i) : id(i){}

        // Places an owning building anchor on this tile.
        void CreateBuilding(std::unique_ptr<Building>&& building,
                            std::optional<TileType> matchedTerrain = std::nullopt);
        // Removes the anchored building from this tile.
        void DestroyBuilding();
        // Returns true when this tile is occupied by a building anchor or footprint reference.
        bool HasBuilding() const { return building != nullptr || buildingRef != nullptr; }
        // Returns the building occupying this tile.
        Building* GetBuilding();
        // Returns the building occupying this tile.
        const Building* GetBuilding() const;
        // Returns true when this tile owns the building instance.
        bool IsBuildingAnchor() const { return building != nullptr; }

        int id;
        // Stable ownership value used by persistence/checksum. `owner` is a
        // runtime presentation cache rebuilt after load.
        PlayerId ownerId{InvalidPlayerId};
        Player* owner{nullptr};
        std::unique_ptr<Building> building{nullptr};
        Building* buildingRef{nullptr};
        TileType tileType{static_cast<TileType>(0)};
        BiomeType biome{BiomeType::PLAINS};
        int terrainTextureId{0};
        // Transparent resource sprite over the ground. -1 means no overlay.
        int resourceOverlayTextureId{-1};
        int resourceRichness{0};
};

// Weighted renderer texture candidate for a terrain type.
struct WeightedTileVariant
{
    int textureId{0};
    int weight{1};
};

// The grass-facing side of a resource deposit rim.  The overlay itself keeps
// the dense material on the opposite side and fades out towards this edge.
enum class ResourceOverlayEdgeDirection
{
    None,
    North,
    East,
    South,
    West
};

// Separate lists keep a deposit's ordinary coverage and its sparse perimeter
// visually distinct without relying on a fragile ordering convention.
struct ResourceOverlayVariantSet
{
    std::vector<WeightedTileVariant> fillers;
    std::vector<WeightedTileVariant> northEdges;
    std::vector<WeightedTileVariant> eastEdges;
    std::vector<WeightedTileVariant> southEdges;
    std::vector<WeightedTileVariant> westEdges;
};

enum class TerrainPlacementFailure
{
    None,
    OutsideMap,
    NoAllowedTerrain,
    InsufficientMatchingTerrain
};

struct TerrainPlacementEvaluation
{
    bool valid{true};
    TileType matchedTerrainType{TileType::GRASS};
    int matchingTiles{0};
    TerrainPlacementFailure failure{TerrainPlacementFailure::None};
};

// Owns all map tiles and placement/pathing helpers that depend on tile layout.
class TileMap
{
    public:
        TileMap() = default;

        // Returns a tile by linear id.
        Tile& GetTile(int id);
        // Replaces a tile at linear id.
        void SetTile(int id, Tile&& tile);
        // Places a new building and marks every tile in its footprint.
        void BuildOnTile(int id, Player* player, std::unique_ptr<Building>&& building);
        // Removes an anchored building and clears footprint references.
        void DestroyBuildingAt(int id);
        // Places a building restored from a save file.
        Building* PlaceLoadedBuilding(int id, Player* player, std::unique_ptr<Building>&& building);
        // Advances all anchored buildings.
        void UpdateBuildings(double dt);
        // Returns a building occupying a linear tile id.
        Building* GetBuilding(int id);
        // Returns a building occupying map coordinates.
        Building* GetBuilding(Vec2i pos);
        // Checks object identity without dereferencing the candidate. UI uses
        // this to invalidate selections after simulation-side destruction or
        // replacement.
        bool ContainsBuilding(const Building* candidate) const;
        // Returns true when coordinates are within map bounds.
        bool IsInside(Vec2i coords) const;
        // Returns true when every footprint tile is inside map bounds.
        bool IsInsideFootprint(Vec2i anchor, Vec2i footprint) const;
        // Returns true when a footprint can be placed on this local map: every
        // tile is inside the map and free of buildings. Province access is
        // validated by the campaign command router, not by tile proximity.
        // `type` is passed by callers that need type-specific placement rules.
        bool CanBuildFootprint(Vec2i anchor, Vec2i footprint, Player* player, BuildingType type = BuildingType::Building) const;
        // Returns the canonical terrain variant selected for a footprint.
        TerrainPlacementEvaluation EvaluateTerrainPlacement(BuildingType type, Vec2i anchor,
                                                            Vec2i footprint, int minimumTiles = 2) const;
        // Returns true when terrain requirements for a building type are satisfied.
        bool HasRequiredTerrainForBuilding(BuildingType type, Vec2i anchor, Vec2i footprint, int minimumTiles = 2) const;
        // Returns true when all gameplay placement rules are satisfied.
        bool CanPlaceBuilding(BuildingType type, Vec2i anchor, Vec2i footprint, Player* player) const;
        // Returns all tile ids occupied by a building footprint.
        std::vector<int> GetBuildingTileIds(const Building* building) const;
        // Returns all tile ids adjacent to a building footprint.
        std::vector<int> GetAdjacentTileIds(const Building* building) const;
        // Same as above, for a footprint that hasn't been placed yet.
        std::vector<int> GetAdjacentTileIds(Vec2i anchor, Vec2i footprint) const;
        // Returns the selected terrain texture for a terrain type.
        int GetTerrainTextureId(TileType type) const;
        // Picks a weighted terrain texture variant for generation.
        int PickTerrainTexture(TileType type, std::mt19937& rng) const;
        // Picks a transparent resource sprite. Directional edge variants fade
        // the whole outer band of a generated deposit into the terrain.
        int PickResourceOverlayTexture(TileType type, ResourceOverlayEdgeDirection edge, std::mt19937& rng) const;
        bool HasResourceOverlay(TileType type) const;
        // Computes road autotile bitmask from neighboring roads.
        int GetRoadAutotileMask(Vec2i pos) const;
        // Returns road texture id matching current road neighborhood.
        int GetRoadTextureId(Vec2i pos) const;
        // Refreshes road textures at and around a changed position.
        void RefreshRoadTilesAround(Vec2i pos);
        // Finds the closest storage building reachable from a source building.
        Building* FindNearestStorage(Building* source, Player* player);
        // Returns the automatic logistics hub: Headquarters first, with the
        // nearest ordinary warehouse used only when the HQ is unavailable.
        // Per-building buffers (including Barracks) are never default hubs.
        Building* FindDefaultStorage(Building* source, Player* player);
        // Connects a placed building to default supplier and receiver candidates.
        void AutoConnectBuilding(Building* building);
        // Makes one building send compatible outputs to another building.
        void ConnectReceiver(Building* source, Building* receiver, bool alternative = false);

        // Converts map coordinates to linear tile id.
        int GetIdFromCoords(Vec2i coords) const;
        // Converts linear tile id to map coordinates.
        Vec2i GetCoordsFromId(int id) const;
        
        Tile& operator [] (size_t idx) { return tilemap[idx];}
        Tile& operator [] (Vec2i pos) { return tilemap[GetIdFromCoords(pos)];}
        
        MapParameters params;
        MapGenerator generator;
        // Non-owning runtime link to the province-local economy. The owning
        // ProvinceSimulation rebinds this after load/move or map attachment.
        ProvinceEconomy* provinceEconomy{nullptr};
        std::map<TileType, std::vector<WeightedTileVariant>> terrainVariants{
            // These are a single cohesive palette. Older prototype grass cells
            // remain in the atlas for save compatibility but never generate on
            // new maps, otherwise their brown/olive mismatch forms a grid.
            {TileType::GRASS, {{28, 1},{29, 1},{30, 1},{31, 1}}},
            {TileType::COAL, {{0, 1},{1, 1},{2, 1}}},
            {TileType::IRON_ORE, {{13, 1},{14, 1},{15, 1}}},
            {TileType::WOOD, {{6, 1},{7, 1},{8, 1}}},
            {TileType::STONE, {{35, 1},{36, 1},{37, 1}}},
            // Placeholder visuals (reuse existing ids until dedicated assets exist).
            {TileType::COPPER_ORE, {{0, 1},{1, 1}}},
            {TileType::TIN_ORE, {{16, 1},{17, 1}}},
            {TileType::SILVER_ORE, {{13, 1},{14, 1}}},
            {TileType::GOLD_ORE, {{14, 1},{15, 1}}},
            {TileType::SAND, {{32, 1},{33, 1},{34, 1}}},
            {TileType::CLAY, {{16, 1},{18, 1}}},
            {TileType::SULFUR, {{2, 1}}},
            {TileType::SALTPETER, {{18, 1}}}
        };

        // Atlas 41 in textures.rtsdata.  Every resource owns a contiguous
        // block of twelve 64px cells: four fillers, then two variants for each
        // grass-facing cardinal rim (N/E/S/W).
        std::map<TileType, ResourceOverlayVariantSet> resourceOverlayVariants{
            {TileType::COAL,       {{{0,1},{1,1},{2,1},{3,1}}, {{4,1},{5,1}}, {{6,1},{7,1}}, {{8,1},{9,1}}, {{10,1},{11,1}}}},
            {TileType::IRON_ORE,   {{{12,1},{13,1},{14,1},{15,1}}, {{16,1},{17,1}}, {{18,1},{19,1}}, {{20,1},{21,1}}, {{22,1},{23,1}}}},
            {TileType::COPPER_ORE, {{{24,1},{25,1},{26,1},{27,1}}, {{28,1},{29,1}}, {{30,1},{31,1}}, {{32,1},{33,1}}, {{34,1},{35,1}}}},
            {TileType::STONE,      {{{36,1},{37,1},{38,1},{39,1}}, {{40,1},{41,1}}, {{42,1},{43,1}}, {{44,1},{45,1}}, {{46,1},{47,1}}}},
            {TileType::WOOD,       {{{48,1},{49,1},{50,1},{51,1}}, {{52,1},{53,1}}, {{54,1},{55,1}}, {{56,1},{57,1}}, {{58,1},{59,1}}}},
            {TileType::TIN_ORE,    {{{60,1},{61,1},{62,1},{63,1}}, {{64,1},{65,1}}, {{66,1},{67,1}}, {{68,1},{69,1}}, {{70,1},{71,1}}}},
            {TileType::SILVER_ORE, {{{72,1},{73,1},{74,1},{75,1}}, {{76,1},{77,1}}, {{78,1},{79,1}}, {{80,1},{81,1}}, {{82,1},{83,1}}}},
            {TileType::GOLD_ORE,   {{{84,1},{85,1},{86,1},{87,1}}, {{88,1},{89,1}}, {{90,1},{91,1}}, {{92,1},{93,1}}, {{94,1},{95,1}}}},
            {TileType::SULFUR,     {{{96,1},{97,1},{98,1},{99,1}}, {{100,1},{101,1}}, {{102,1},{103,1}}, {{104,1},{105,1}}, {{106,1},{107,1}}}},
            {TileType::SALTPETER,  {{{108,1},{109,1},{110,1},{111,1}}, {{112,1},{113,1}}, {{114,1},{115,1}}, {{116,1},{117,1}}, {{118,1},{119,1}}}},
            {TileType::CLAY,       {{{120,1},{121,1},{122,1},{123,1}}, {{124,1},{125,1}}, {{126,1},{127,1}}, {{128,1},{129,1}}, {{130,1},{131,1}}}}
        };

        std::vector<Tile> tilemap;
        bool terrainDirty{true};
        bool buildingsDirty{true};
};






#endif
