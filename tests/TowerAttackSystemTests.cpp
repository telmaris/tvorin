#include "core/GameWorld.h"
#include "core/GameCommand.h"
#include "core/GameSession.h"
#include "warfare/TowerAttackSystem.h"
#include "warfare/UnitMarchSystem.h"
#include "economy/ProductionBuildings.h"
#include "simulation/MapGenerator.h"
#include "simulation/RoadNetwork.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <sstream>
#include <vector>

// TD(etap-7) — tower targeting/range, ammo consumption, firing without ammo,
// homing-projectile hits on a moving unit via CombatResolver, Tower*
// modifiers, and the ammo-request integration with the resource road network.

namespace
{
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

    // Places a DefenseTower whose footprint CENTER lands `offsetTiles` away
    // from a specific point on the route, offset PERPENDICULAR to the
    // route's local direction there (derived from the neighboring route
    // tiles) rather than a fixed axis. Bug found 2026-07-14: a fixed
    // along-x offset only reliably lands "beside the path" by coincidence —
    // the route can run in any local direction (SerpentineBias wiggle, B1/B2
    // n-gon HQ placement), and a since-fixed off-by-one in world-gen retry
    // validation (docs/work_plan_2026-07-13.md B5) had been masking this by
    // always exhausting every retry and silently using a different perturbed
    // seed than the one requested — once that bug was fixed, these named
    // seeds started producing their ACTUAL (unperturbed) route geometry, and
    // the fixed-axis offset stopped reliably landing in range. Bypasses the
    // normal build-cost/placement validation (PlaceLoadedBuilding) so range
    // tests get a precisely controlled distance instead of depending on
    // where world-gen happened to put things.
    Building* PlaceTowerNearRouteTile(GameWorld& world, Player* owner, const std::vector<int>& route,
                                       size_t routeIndex, int offsetTiles, int id)
    {
        TileMap& map = world.GetTileMap();
        // Sample the tangent over a WIDE span (not the immediate +/-1
        // neighbor) — the route is a 4-directional grid path, so even along
        // an overall-straight or gently curving trend it "staircases" at the
        // single-tile level (alternating individual N/E/S/W steps), which
        // makes a narrow sample noisy/unreliable. A wide span averages that
        // out and tracks the actual long-wavelength SerpentineBias trend.
        size_t span = std::min<size_t>(10, std::min(routeIndex, route.size() - 1 - routeIndex));
        span = std::max<size_t>(span, 1);
        size_t prevIndex = routeIndex - span;
        size_t nextIndex = routeIndex + span;
        Vec2i prevPos = map.GetCoordsFromId(route[prevIndex]);
        Vec2i nextPos = map.GetCoordsFromId(route[nextIndex]);

        float tangentX = static_cast<float>(nextPos.x - prevPos.x);
        float tangentY = static_cast<float>(nextPos.y - prevPos.y);
        float length = std::sqrt(tangentX * tangentX + tangentY * tangentY);
        float perpX = length > 0.0f ? -tangentY / length : 1.0f;
        float perpY = length > 0.0f ? tangentX / length : 0.0f;

        Vec2i tileAnchor = map.GetCoordsFromId(route[routeIndex]);
        Vec2i towerAnchor{
            tileAnchor.x + static_cast<int>(std::lround(perpX * offsetTiles)),
            tileAnchor.y + static_cast<int>(std::lround(perpY * offsetTiles))};
        return map.PlaceLoadedBuilding(map.GetIdFromCoords(towerAnchor), owner, std::make_unique<DefenseTower>(id));
    }

    // Same helper pattern as BuildingDomainTests.cpp.
    void FillOwnedGrass(TileMap& map, Player* owner, int width, int height)
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
            map.tilemap.push_back(std::move(tile));
        }
    }

    template <typename T>
    T* PlaceAndRegister(TileMap& map, RoadNetwork& network, Player* owner, Vec2i anchor, int id)
    {
        auto* placed = dynamic_cast<T*>(
            map.PlaceLoadedBuilding(map.GetIdFromCoords(anchor), owner, std::make_unique<T>(id)));
        if (placed == nullptr)
            return nullptr;
        for (int occupiedTileId : map.GetBuildingTileIds(placed))
            network.UpdateNavMap(occupiedTileId, placed);
        return placed;
    }
}

TEST(TowerAttackSystemTests, TowerHitsMovingEnemyWithinRangeAndConsumesAmmo)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(301));
    Player* human = world.GetPlayerHandler().players.at(0).get();
    Player* ai = world.GetPlayerHandler().players.at(1).get();

    int unitId = AddUnitToRoster(*ai, "militia");
    world.SubmitCommand(GameCommand::DeployUnits(1, 0, {unitId}));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    std::vector<int> route = world.GetMilitaryRoads().GetDirectedTiles(1, 0);
    ASSERT_GT(route.size(), 20u);
    int midIndex = static_cast<int>(route.size()) / 2;

    Building* tower = PlaceTowerNearRouteTile(world, human, route, midIndex, /*offsetTiles*/ 2, 9001);
    ASSERT_NE(tower, nullptr);
    auto* combat = tower->GetComponent<TowerCombatComponent>();
    auto* storage = tower->GetComponent<LocalResourceBufferComponent>();
    ASSERT_NE(combat, nullptr);
    ASSERT_NE(storage, nullptr);
    storage->buffers[ResourceType::ARROWS].SetStoredAmount(10);
    int ammoBefore = static_cast<int>(storage->buffers[ResourceType::ARROWS].buffer.size());
    double unitHpBefore = world.GetDeployedUnits().at(unitId).currentHp;

    // Route traversal is covered by UnitMarchSystemTests. Start shortly before
    // the tower so this test still observes a genuinely moving target without
    // spending thousands of simulation ticks reaching the fixture.
    auto& movingUnit = world.GetDeployedUnits().at(unitId);
    movingUnit.tileIndex = midIndex - 2;
    movingUnit.tileProgress = 0.0;
    bool damaged = false;
    for (int i = 0; i < 300 && world.GetDeployedUnits().count(unitId) != 0 && !damaged; i++)
    {
        world.UpdateSimulation(FixedSimulationClock::FixedDt);
        auto it = world.GetDeployedUnits().find(unitId);
        if (it != world.GetDeployedUnits().end() && it->second.currentHp < unitHpBefore)
            damaged = true;
    }

    EXPECT_TRUE(damaged) << "the tower should have hit the marching unit as it passed within range";
    int ammoAfter = static_cast<int>(storage->buffers[ResourceType::ARROWS].buffer.size());
    EXPECT_LT(ammoAfter, ammoBefore) << "firing should have consumed ammo";
}

TEST(TowerAttackSystemTests, StrongestTargetModePrefersTheMostPowerfulUnit)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(306));
    Player* human = world.GetPlayerHandler().players.at(0).get();
    Player* ai = world.GetPlayerHandler().players.at(1).get();

    std::vector<int> route = world.GetMilitaryRoads().GetDirectedTiles(1, 0);
    ASSERT_GT(route.size(), 20u);
    Building* tower = PlaceTowerNearRouteTile(world, human, route, route.size() / 2, 2, 9006);
    ASSERT_NE(tower, nullptr);
    auto* combat = tower->GetComponent<TowerCombatComponent>();
    auto* storage = tower->GetComponent<LocalResourceBufferComponent>();
    ASSERT_NE(combat, nullptr);
    ASSERT_NE(storage, nullptr);
    combat->targetMode = TowerTargetMode::StrongestUnit;
    storage->buffers[ResourceType::ARROWS].SetStoredAmount(10);

    int militiaId = AddUnitToRoster(*ai, "militia");
    int swordsmanId = AddUnitToRoster(*ai, "swordsman");
    world.SubmitCommand(GameCommand::DeployUnits(ai->id, human->id, {militiaId, swordsmanId}));

    // The swordsman is slower, so make both units simultaneously eligible for
    // this isolated target-selection check; the normal march test above
    // already covers movement into tower range.
    world.UpdateSimulation(FixedSimulationClock::FixedDt);
    ASSERT_TRUE(world.GetDeployedUnits().contains(militiaId));
    ASSERT_TRUE(world.GetDeployedUnits().contains(swordsmanId));
    for (int id : {militiaId, swordsmanId})
    {
        auto& unit = world.GetDeployedUnits().at(id);
        unit.tileIndex = static_cast<int>(route.size() / 2);
        unit.tileProgress = 0.5;
    }
    EXPECT_EQ(combat->targetMode, TowerTargetMode::StrongestUnit);
    EXPECT_GT(world.GetDeployedUnits().at(swordsmanId).GetEffectiveMaxHp(*ai),
              world.GetDeployedUnits().at(militiaId).GetEffectiveMaxHp(*ai));
    TowerAttackSystem::Update(world, FixedSimulationClock::FixedDt);

    ASSERT_FALSE(world.GetProjectiles().empty()) << "tower should fire when the group reaches its range";
    EXPECT_EQ(world.GetProjectiles().begin()->second.sourceUnitInstanceId, tower->id);
    EXPECT_EQ(world.GetProjectiles().begin()->second.targetUnitInstanceId, swordsmanId);
}

TEST(TowerAttackSystemTests, TowerDoesNotFireAtEnemyBeyondRange)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(302));
    Player* human = world.GetPlayerHandler().players.at(0).get();
    Player* ai = world.GetPlayerHandler().players.at(1).get();

    int unitId = AddUnitToRoster(*ai, "militia");
    world.SubmitCommand(GameCommand::DeployUnits(1, 0, {unitId}));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    std::vector<int> route = world.GetMilitaryRoads().GetDirectedTiles(1, 0);
    ASSERT_GT(route.size(), 20u);
    int midIndex = static_cast<int>(route.size()) / 2;

    // Default tower range is 6 tiles (buildings.rtsdata) — 40 tiles away is
    // comfortably beyond it for the unit's entire march past this point.
    Building* tower = PlaceTowerNearRouteTile(world, human, route, midIndex, /*offsetTiles*/ 40, 9002);
    ASSERT_NE(tower, nullptr);
    auto* storage = tower->GetComponent<LocalResourceBufferComponent>();
    ASSERT_NE(storage, nullptr);
    storage->buffers[ResourceType::ARROWS].SetStoredAmount(10);

    auto& distantUnit = world.GetDeployedUnits().at(unitId);
    distantUnit.tileIndex = midIndex;
    distantUnit.tileProgress = 0.0;
    for (int i = 0; i < 10 && world.GetDeployedUnits().count(unitId) != 0; i++)
        world.UpdateSimulation(FixedSimulationClock::FixedDt);

    EXPECT_EQ(storage->buffers[ResourceType::ARROWS].buffer.size(), 10u)
        << "a tower with nothing ever in range should never fire";
    EXPECT_TRUE(world.GetProjectiles().empty());
}

TEST(TowerAttackSystemTests, IdleTowerDoesNotAccumulateAnOpeningBurst)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(307));
    Player* human = world.GetPlayerHandler().players.at(0).get();
    Player* ai = world.GetPlayerHandler().players.at(1).get();

    std::vector<int> route = world.GetMilitaryRoads().GetDirectedTiles(1, 0);
    ASSERT_GT(route.size(), 20u);
    const int midIndex = static_cast<int>(route.size()) / 2;
    Building* tower = PlaceTowerNearRouteTile(world, human, route, midIndex, 2, 9007);
    ASSERT_NE(tower, nullptr);
    auto* combat = tower->GetComponent<TowerCombatComponent>();
    auto* storage = tower->GetComponent<LocalResourceBufferComponent>();
    ASSERT_NE(combat, nullptr);
    ASSERT_NE(storage, nullptr);
    storage->buffers[ResourceType::ARROWS].SetStoredAmount(20);

    // A long idle/full-ammo period used to drive attackTimer deeply negative.
    for (int i = 0; i < 200; ++i)
        TowerAttackSystem::Update(world, FixedSimulationClock::FixedDt);
    EXPECT_DOUBLE_EQ(combat->attackTimer, 0.0);

    const int unitId = ai->id * 100000 + ai->nextUnitInstanceId++;
    BattleUnit incoming(unitId, ai->id, "militia");
    incoming.currentHp = incoming.GetEffectiveMaxHp(*ai);
    incoming.state = BattleUnitState::Marching;
    incoming.routeFromPlayerId = ai->id;
    incoming.routeToPlayerId = human->id;
    incoming.tileIndex = midIndex;
    world.GetDeployedUnits()[unitId] = std::move(incoming);

    const size_t ammoBefore = storage->buffers[ResourceType::ARROWS].buffer.size();
    TowerAttackSystem::Update(world, FixedSimulationClock::FixedDt);
    ASSERT_EQ(world.GetProjectiles().size(), 1u);
    EXPECT_GT(combat->attackTimer, 0.0);
    EXPECT_EQ(storage->buffers[ResourceType::ARROWS].buffer.size(), ammoBefore - 1);

    TowerAttackSystem::Update(world, FixedSimulationClock::FixedDt);
    EXPECT_EQ(world.GetProjectiles().size(), 1u)
        << "the next simulation tick must not release a second backlog shot";
    EXPECT_EQ(storage->buffers[ResourceType::ARROWS].buffer.size(), ammoBefore - 1);
}

TEST(TowerAttackSystemTests, TowerStopsFiringWithoutAmmo)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(303));
    Player* human = world.GetPlayerHandler().players.at(0).get();
    Player* ai = world.GetPlayerHandler().players.at(1).get();

    int unitId = AddUnitToRoster(*ai, "militia");
    world.SubmitCommand(GameCommand::DeployUnits(1, 0, {unitId}));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    std::vector<int> route = world.GetMilitaryRoads().GetDirectedTiles(1, 0);
    ASSERT_GT(route.size(), 20u);
    int midIndex = static_cast<int>(route.size()) / 2;

    Building* tower = PlaceTowerNearRouteTile(world, human, route, midIndex, /*offsetTiles*/ 2, 9003);
    ASSERT_NE(tower, nullptr);
    auto* storage = tower->GetComponent<LocalResourceBufferComponent>();
    ASSERT_NE(storage, nullptr);
    storage->buffers[ResourceType::ARROWS].SetStoredAmount(0); // no ammo at all

    auto& unit = world.GetDeployedUnits().at(unitId);
    unit.tileIndex = midIndex;
    unit.tileProgress = 0.0;
    double unitHpBefore = world.GetDeployedUnits().at(unitId).currentHp;
    for (int i = 0; i < 100; i++)
        world.UpdateSimulation(FixedSimulationClock::FixedDt);

    ASSERT_EQ(world.GetDeployedUnits().count(unitId), 1u);
    EXPECT_DOUBLE_EQ(world.GetDeployedUnits().at(unitId).currentHp, unitHpBefore);
    EXPECT_TRUE(world.GetProjectiles().empty());
}

TEST(TowerAttackSystemTests, TowerBalanceModifiersApply)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(304));
    Player* human = world.GetPlayerHandler().players.at(0).get();

    Building* tower = world.GetTileMap().PlaceLoadedBuilding(
        world.GetTileMap().GetIdFromCoords({5, 5}), human, std::make_unique<DefenseTower>(9004));
    ASSERT_NE(tower, nullptr);
    auto* combat = tower->GetComponent<TowerCombatComponent>();
    ASSERT_NE(combat, nullptr);

    double baseDamage = combat->GetModifiedDamage(*tower);
    double baseRange = combat->GetModifiedRange(*tower);
    double baseAttackSpeed = combat->GetModifiedAttackSpeed(*tower);

    human->balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::TowerDamage, 0.0, 1.5, BalanceModifierScope::Global(), std::nullopt, std::nullopt, "test:tower"});
    human->balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::TowerRange, 2.0, 1.0, BalanceModifierScope::Global(), std::nullopt, std::nullopt, "test:tower"});
    human->balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::TowerAttackSpeed, 0.0, 2.0, BalanceModifierScope::Global(), std::nullopt, std::nullopt, "test:tower"});

    EXPECT_DOUBLE_EQ(combat->GetModifiedDamage(*tower), baseDamage * 1.5);
    EXPECT_DOUBLE_EQ(combat->GetModifiedRange(*tower), baseRange + 2.0);
    EXPECT_DOUBLE_EQ(combat->GetModifiedAttackSpeed(*tower), baseAttackSpeed * 2.0);
}

TEST(TowerAttackSystemTests, TowerRequestsAmmoOverRoadNetworkFromSupplier)
{
    TileMap map;
    Player player{0, map};
    FillOwnedGrass(map, &player, 10, 4);
    auto network = std::make_unique<RoadNetwork>(map);
    RoadNetwork* networkPtr = network.get();
    player.roadNetwork = std::move(network);

    auto* warehouse = PlaceAndRegister<StorageBuilding>(map, *networkPtr, &player, {0, 0}, 1);
    auto* tower = PlaceAndRegister<DefenseTower>(map, *networkPtr, &player, {5, 0}, 2);
    ASSERT_NE(warehouse, nullptr);
    ASSERT_NE(tower, nullptr);

    for (int x = 3; x <= 4; x++)
    {
        auto* road = PlaceAndRegister<Road>(map, *networkPtr, &player, {x, 1}, 100 + x);
        ASSERT_NE(road, nullptr);
        road->road.maxCapacity.SetBase(16);
    }

    warehouse->storage.buffers[ResourceType::ARROWS].SetStoredAmount(50);
    tower->SetSupplier(ResourceType::ARROWS, warehouse);

    // TowerCombatComponent::Update is what actually issues the request
    // (mirroring ProductionComponent::MaintainRequests, minus the
    // ProductionComponent coupling that method requires).
    tower->combat.Update(*tower, 0.01);

    EXPECT_GT(warehouse->transportables.size(), 0u)
        << "the tower's ammo request should have started a real road-network transport";
}

// Regression test for T3 (docs/post_pivot_audit_2026-07-12.md): before this
// fix, Building::GetInputBufferViews() never exposed a tower's ammo buffer as
// an input, so AutoConnectBuilding had no input view to wire a supplier from
// — a tower only ever got ammo if something manually called SetSupplier (as
// the test above still does). Builds everything through the real production
// placement path (Player::Build<T>, no manual SetSupplier, no Tile::owner)
// and expects the ammo request to fire from auto-connect alone.
TEST(TowerAttackSystemTests, TowerAmmoAutoConnectsWithoutManualSupplierWiring)
{
    TileMap map;
    map.params.sizeX = 10;
    map.params.sizeY = 4;
    map.tilemap.clear();
    map.tilemap.reserve(map.params.sizeX * map.params.sizeY);
    for (int i = 0; i < map.params.sizeX * map.params.sizeY; i++)
        map.tilemap.emplace_back(i);

    Player player{0, map};

    auto* warehouse = dynamic_cast<StorageBuilding*>(player.Build<StorageBuilding>(Vec2i{0, 0}, false));
    auto* tower = dynamic_cast<DefenseTower*>(player.Build<DefenseTower>(Vec2i{5, 0}, false));
    auto* roadA = player.Build<Road>(Vec2i{3, 1}, false);
    auto* roadB = player.Build<Road>(Vec2i{4, 1}, false);
    ASSERT_NE(warehouse, nullptr);
    ASSERT_NE(tower, nullptr);
    ASSERT_NE(roadA, nullptr);
    ASSERT_NE(roadB, nullptr);

    warehouse->storage.buffers[ResourceType::ARROWS].SetStoredAmount(50);

    tower->combat.Update(*tower, 0.01);

    EXPECT_GT(warehouse->transportables.size(), 0u)
        << "auto-connect should have wired the tower as a supplied receiver for ARROWS";
}

TEST(TowerAttackSystemTests, SaveAndLoadPreservesTowerAmmoAndAttackTimer)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeSmallRingParams(305));
    Player* human = world.GetPlayerHandler().players.at(0).get();

    Building* tower = world.GetTileMap().PlaceLoadedBuilding(
        world.GetTileMap().GetIdFromCoords({5, 5}), human, std::make_unique<DefenseTower>(9005));
    ASSERT_NE(tower, nullptr);
    auto* combat = tower->GetComponent<TowerCombatComponent>();
    auto* storage = tower->GetComponent<LocalResourceBufferComponent>();
    ASSERT_NE(combat, nullptr);
    ASSERT_NE(storage, nullptr);
    combat->attackTimer = 0.42;
    combat->targetMode = TowerTargetMode::StrongestUnit;
    storage->buffers[ResourceType::ARROWS].SetStoredAmount(7);

    const auto path = (std::filesystem::temp_directory_path() / "rts_tower_test.save").string();
    ASSERT_TRUE(world.SaveToFile(path));

    GameWorld loaded;
    ASSERT_TRUE(loaded.LoadFromFile(path, nullptr, nullptr));

    Building* loadedTower = loaded.GetTileMap().GetBuilding(tower->positionId);
    ASSERT_NE(loadedTower, nullptr);
    const auto* loadedCombat = loadedTower->GetComponent<TowerCombatComponent>();
    const auto* loadedStorage = loadedTower->GetComponent<LocalResourceBufferComponent>();
    ASSERT_NE(loadedCombat, nullptr);
    ASSERT_NE(loadedStorage, nullptr);
    EXPECT_DOUBLE_EQ(loadedCombat->attackTimer, 0.42);
    EXPECT_EQ(loadedCombat->targetMode, TowerTargetMode::StrongestUnit);
    EXPECT_EQ(loadedStorage->buffers.at(ResourceType::ARROWS).buffer.size(), 7u);

    // v33 stored this exact private tower buffer under STOR. Preserve old
    // saves by routing that legacy block into LocalResourceBufferComponent.
    std::string legacyState = world.SerializeSimulationState();
    ASSERT_NE(legacyState.find("RTS_SAVE 36"), std::string::npos);
    legacyState.replace(legacyState.find("RTS_SAVE 36"), 11, "RTS_SAVE 33");
    // v35 adds the build-cost state/count after the legacy B fields. Strip
    // those two fields when constructing this v33 compatibility payload.
    {
        std::istringstream input(legacyState);
        std::ostringstream output;
        std::string line;
        while (std::getline(input, line))
        {
            if (line.rfind("B ", 0) == 0)
            {
                std::istringstream lineInput(line);
                std::vector<std::string> fields;
                std::string field;
                while (lineInput >> field)
                    fields.push_back(field);
                ASSERT_GE(fields.size(), 3u);
                fields.resize(fields.size() - 2);
                line.clear();
                for (const auto& value : fields)
                    line += (line.empty() ? "" : " ") + value;
            }
            output << line << '\n';
        }
        legacyState = output.str();
    }
    for (size_t pos = 0; (pos = legacyState.find("LOCALBUF", pos)) != std::string::npos;)
        legacyState.replace(pos, 8, "STOR");

    GameWorld legacyLoaded;
    ASSERT_TRUE(legacyLoaded.RestoreSimulationState(legacyState));
    Building* legacyTower = legacyLoaded.GetTileMap().GetBuilding(tower->positionId);
    ASSERT_NE(legacyTower, nullptr);
    const auto* legacyStorage = legacyTower->GetComponent<LocalResourceBufferComponent>();
    ASSERT_NE(legacyStorage, nullptr);
    EXPECT_EQ(legacyStorage->buffers.at(ResourceType::ARROWS).buffer.size(), 7u);

    std::filesystem::remove(path);
}
