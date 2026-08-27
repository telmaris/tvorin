#include "core/GameWorld.h"
#include "core/GameSession.h"
#include "warfare/UnitMarchSystem.h"
#include "warfare/BattleUnit.h"
#include "simulation/MapGenerator.h"
#include "simulation/MilitaryRoadNetwork.h"
#include "simulation/RoadNetwork.h"
#include "economy/Player.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

// B6 (docs/work_plan_2026-07-13.md): Bridge lets a resource road cross the
// immutable military road track (which nothing else may be placed on) — a
// ring/edge can otherwise wall off part of the map from the resource-road
// network entirely. These tests cover: placement rule inversion, real
// end-to-end transport through a bridge, that destroying it never touches
// the underlying track (marching stays unaffected), and save/load.

namespace
{
    // Bare grass map with every Tile::owner left at its default (nullptr) —
    // mirrors RoadNetworkTests.cpp's FillUnownedMap: production never stamps
    // Tile::owner (the territory system was removed in the Tower Defense
    // pivot, ETAP 1), so tests must not rely on that shortcut either.
    void FillUnownedMap(TileMap& map, int width = 10, int height = 6)
    {
        map.params.sizeX = width;
        map.params.sizeY = height;
        map.tilemap.clear();
        map.tilemap.reserve(width * height);
        for (int i = 0; i < width * height; i++)
            map.tilemap.emplace_back(i);
    }

    MapParameters MakeSmallRingParams(unsigned int seed)
    {
        MapParameters params;
        params.sizeX = 81;
        params.sizeY = 81;
        params.aiOpponentCount = 1;
        params.seed = seed;
        return params;
    }

    int AddUnitToRoster(Player& player, const std::string& unitDefId)
    {
        int instanceId = player.id * 100000 + player.nextUnitInstanceId++;
        BattleUnit unit(instanceId, player.id, unitDefId);
        unit.currentHp = unit.GetEffectiveMaxHp(player);
        player.roster.AddUnit(std::move(unit));
        return instanceId;
    }
}

TEST(BridgeTests, PlacementRuleIsInvertedExactlyOnTheTrack)
{
    TileMap map;
    FillUnownedMap(map);
    Player player{0, map};

    Vec2i trackPos{4, 2};
    int trackTileId = map.GetIdFromCoords(trackPos);
    map.tilemap[trackTileId].isMilitaryRoad = true;

    // On the track: Bridge is the only type allowed. Road and a plain
    // storage building are refused, matching the pre-B6 rule for everything
    // else (TileMap::CanBuildFootprint).
    EXPECT_TRUE(map.CanPlaceBuilding(BuildingType::Bridge, trackPos, {1, 1}, &player));
    EXPECT_FALSE(map.CanPlaceBuilding(BuildingType::Road, trackPos, {1, 1}, &player));
    EXPECT_FALSE(map.CanPlaceBuilding(BuildingType::StorageBuilding, trackPos,
                                       GetBuildingDefinition(BuildingType::StorageBuilding).footprint, &player));

    // Off the track: the rule inverts back — Bridge refused, Road allowed.
    Vec2i offTrackPos{4, 3};
    ASSERT_FALSE(map.tilemap[map.GetIdFromCoords(offTrackPos)].isMilitaryRoad);
    EXPECT_FALSE(map.CanPlaceBuilding(BuildingType::Bridge, offTrackPos, {1, 1}, &player));
    EXPECT_TRUE(map.CanPlaceBuilding(BuildingType::Road, offTrackPos, {1, 1}, &player));

    Building* bridge = player.Build<Bridge>(trackTileId, false);
    ASSERT_NE(bridge, nullptr);
    EXPECT_EQ(bridge->buildingType, BuildingType::Bridge);
    const auto* upgrade = bridge->GetComponent<UpgradeComponent>();
    ASSERT_NE(upgrade, nullptr);
    EXPECT_EQ(upgrade->level, 1);
    EXPECT_EQ(upgrade->maxLevel, 4);
    EXPECT_TRUE(map.tilemap[trackTileId].isMilitaryRoad)
        << "placing a Bridge must not clear the underlying military-road flag";
}

// Playtest follow-up (2026-07-14): two Bridges sitting edge-to-edge would
// form one longer crossing rather than the intended single-tile gap in the
// track — CanBuildFootprint must refuse a second Bridge orthogonally
// adjacent to an existing one, but still allow one that's merely diagonal
// or a tile further away.
TEST(BridgeTests, CannotPlaceTwoBridgesOrthogonallyAdjacent)
{
    TileMap map;
    FillUnownedMap(map);
    Player player{0, map};

    Vec2i firstPos{4, 2};
    Vec2i orthogonalPos{5, 2};
    Vec2i diagonalPos{5, 3};
    Vec2i farPos{6, 2};
    for (Vec2i pos : {firstPos, orthogonalPos, diagonalPos, farPos})
        map.tilemap[map.GetIdFromCoords(pos)].isMilitaryRoad = true;

    ASSERT_NE(player.Build<Bridge>(map.GetIdFromCoords(firstPos), false), nullptr);

    EXPECT_FALSE(map.CanPlaceBuilding(BuildingType::Bridge, orthogonalPos, {1, 1}, &player))
        << "orthogonally adjacent to an existing Bridge must be refused";
    EXPECT_TRUE(map.CanPlaceBuilding(BuildingType::Bridge, diagonalPos, {1, 1}, &player))
        << "diagonal-only neighbours are not blocked (matches GetAdjacentTileIds convention)";
    EXPECT_TRUE(map.CanPlaceBuilding(BuildingType::Bridge, farPos, {1, 1}, &player))
        << "a Bridge two tiles away is unaffected";
}

// Real production path (Player::Build<T>, same as GameCommand execution and
// AI) end to end: source storage -- road -- BRIDGE (on the track) -- road --
// destination storage. Asserts the resource actually crosses, not just that
// a path exists.
TEST(BridgeTests, TransportCrossesBridgeOverMilitaryRoadTrack)
{
    TileMap map;
    FillUnownedMap(map);
    Player player{0, map};

    // StorageBuilding is 3x3 (assets/data/buildings.rtsdata): source(0,1)
    // occupies x:[0,2] y:[1,3]; destination(5,1) occupies x:[5,7] y:[1,3] and
    // therefore already includes tile (5,2), which sits directly against the
    // bridge below — so a single road tile plus the bridge is enough to
    // connect them, no second road segment needed.
    Vec2i trackPos{4, 2};
    int trackTileId = map.GetIdFromCoords(trackPos);
    map.tilemap[trackTileId].isMilitaryRoad = true;

    auto* source = dynamic_cast<StorageBuilding*>(player.Build<StorageBuilding>(Vec2i{0, 1}, false));
    auto* roadA = dynamic_cast<Road*>(player.Build<Road>(Vec2i{3, 2}, false));
    auto* bridge = dynamic_cast<Bridge*>(player.Build<Bridge>(trackTileId, false));
    auto* destination = dynamic_cast<StorageBuilding*>(player.Build<StorageBuilding>(Vec2i{5, 1}, false));
    ASSERT_NE(source, nullptr);
    ASSERT_NE(roadA, nullptr);
    ASSERT_NE(bridge, nullptr);
    ASSERT_NE(destination, nullptr);
    bridge->road.SetPriorityResource(ResourceType::WOOD);
    EXPECT_EQ(bridge->road.GetPriorityResource(), ResourceType::WOOD);

    std::vector<int> path = player.roadNetwork->CalculatePath(source, destination);
    ASSERT_FALSE(path.empty());
    EXPECT_NE(std::find(path.begin(), path.end(), trackTileId), path.end())
        << "path should cross the bridge tile sitting on the military road";

    source->storage.buffers[ResourceType::WOOD].GenerateResource(ResourceType::WOOD);
    ASSERT_EQ(source->storage.buffers[ResourceType::WOOD].buffer.size(), 1u);

    // Warehouses are passive (StorageComponent has no Update) — a transfer is
    // always initiated by someone, so ask for it explicitly rather than
    // relying on an ambient push. Everything under test still runs: the
    // request goes through Player::BeginTransport -> CalculatePath and the
    // resource is then walked across the bridge by Transportable::Update.
    ASSERT_EQ(source->HandleTransport(ResourceType::WOOD, 1, destination), 1);
    for (int tick = 0; tick < 10 && destination->storage.buffers[ResourceType::WOOD].buffer.empty(); tick++)
    {
        source->Update(1.0);
        roadA->Update(1.0);
        bridge->Update(1.0);
    }

    EXPECT_EQ(destination->storage.buffers[ResourceType::WOOD].buffer.size(), 1u)
        << "resource should have crossed the bridge and arrived at destination";
    EXPECT_TRUE(source->storage.buffers[ResourceType::WOOD].buffer.empty());
}

TEST(BridgeTests, DestroyingBridgeStopsTransportButTrackTileRemainsMilitaryRoad)
{
    TileMap map;
    FillUnownedMap(map);
    Player player{0, map};

    // Same layout as TransportCrossesBridgeOverMilitaryRoadTrack above.
    Vec2i trackPos{4, 2};
    int trackTileId = map.GetIdFromCoords(trackPos);
    map.tilemap[trackTileId].isMilitaryRoad = true;

    auto* source = player.Build<StorageBuilding>(Vec2i{0, 1}, false);
    player.Build<Road>(Vec2i{3, 2}, false);
    Building* bridge = player.Build<Bridge>(trackTileId, false);
    auto* destination = player.Build<StorageBuilding>(Vec2i{5, 1}, false);
    ASSERT_NE(bridge, nullptr);

    ASSERT_FALSE(player.roadNetwork->CalculatePath(source, destination).empty());

    map.DestroyBuildingAt(trackTileId);

    EXPECT_TRUE(map.tilemap[trackTileId].isMilitaryRoad)
        << "destroying the bridge must not clear the underlying military-road flag";
    EXPECT_FALSE(map.tilemap[trackTileId].HasBuilding());
    EXPECT_TRUE(player.roadNetwork->CalculatePath(source, destination).empty())
        << "with the bridge gone, resource roads can no longer cross the track";
}

// Marching reads the ring's own precomputed tile list (UnitMarchSystem), not
// building occupancy — a bridge sitting on a track tile must not block or
// alter marching in any way. Uses a real generated world/ring (not the bare
// fixture above) so the track is the actual one units march along.
TEST(BridgeTests, UnitMarchesAcrossTrackTileWithBridgeOnIt)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(6));

    Player* human = world.GetPlayerHandler().players.at(0).get();
    const MilitaryRoute* route = world.GetMilitaryRoads().FindRoute(0, 1);
    ASSERT_NE(route, nullptr);
    ASSERT_GE(route->tiles.size(), 3u);
    int trackTileId = route->tiles[route->tiles.size() / 2];

    Building* bridge = human->Build<Bridge>(trackTileId, false);
    ASSERT_NE(bridge, nullptr);

    int unitId = AddUnitToRoster(*human, "militia");
    world.SubmitCommand(GameCommand::DeployUnits(0, 1, {unitId}));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);
    // Huge-dt march-only fast-forward (pattern from HqCombatSystemTests.cpp's
    // DeployAndRushToHqDoor) — if the bridge blocked or altered traversal of
    // its tile, the unit would stall short of the door instead of reaching it.
    UnitMarchSystem::Update(world, 100000.0);

    ASSERT_EQ(world.GetDeployedUnits().count(unitId), 1u);
    EXPECT_EQ(world.GetDeployedUnits().at(unitId).state, BattleUnitState::AttackingHq)
        << "the bridge on the track must not block or alter marching";
}

TEST(BridgeTests, SaveAndLoadPreservesBridgeAndTrackFlag)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(9));

    Player* human = world.GetPlayerHandler().players.at(0).get();
    const MilitaryRoute* route = world.GetMilitaryRoads().FindRoute(0, 1);
    ASSERT_NE(route, nullptr);
    ASSERT_GE(route->tiles.size(), 3u);
    int trackTileId = route->tiles[route->tiles.size() / 2];

    Building* bridge = human->Build<Bridge>(trackTileId, false);
    ASSERT_NE(bridge, nullptr);
    int bridgeId = bridge->id;

    const auto path = (std::filesystem::temp_directory_path() / "rts_bridge_test.save").string();
    ASSERT_TRUE(world.SaveToFile(path));

    GameWorld loaded;
    ASSERT_TRUE(loaded.LoadFromFile(path, nullptr, nullptr));

    Building* loadedBuilding = loaded.GetTileMap().GetBuilding(loaded.GetTileMap().GetCoordsFromId(trackTileId));
    ASSERT_NE(loadedBuilding, nullptr);
    EXPECT_EQ(loadedBuilding->buildingType, BuildingType::Bridge);
    EXPECT_EQ(loadedBuilding->id, bridgeId);
    EXPECT_TRUE(loaded.GetTileMap().tilemap[trackTileId].isMilitaryRoad);

    std::filesystem::remove(path);
}
