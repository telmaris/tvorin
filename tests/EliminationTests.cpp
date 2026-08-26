#include "core/GameWorld.h"
#include "core/GameCommand.h"
#include "core/GameSession.h"
#include "economy/ProductionBuildings.h"
#include "simulation/MapGenerator.h"
#include "simulation/PathingService.h"
#include "warfare/UnitMarchSystem.h"

#include <gtest/gtest.h>

#include <filesystem>

// TD(etap-6.3) — elimination, conquest (ConqueredEconomy), storage drain and
// multi-hop pathing through a conquered HQ. HqCombatSystem's own siege/thorns
// mechanics (which trigger GameWorld::EliminatePlayer) are covered separately
// in HqCombatSystemTests.cpp; these tests exercise EliminatePlayer and its
// supporting pieces directly/synchronously.

namespace
{
    MapParameters MakeRingParams(unsigned int seed, int aiOpponentCount)
    {
        MapParameters params;
        params.sizeX = 81;
        params.sizeY = 81;
        params.aiOpponentCount = aiOpponentCount;
        params.seed = seed;
        return params;
    }

    // AI players submit commands on the same ticks these tests do, so
    // results.back() is not reliably the command under test — match on the id
    // SubmitCommand returned instead.
    const GameCommandResult* FindResult(const std::vector<GameCommandResult>& results, std::uint64_t commandId)
    {
        for (const auto& result : results)
            if (result.commandId == commandId)
                return &result;
        return nullptr;
    }

}

TEST(EliminationTests, EliminatePlayerFlagsDefeatedAndVictoryIsDetected)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeRingParams(1, 1));

    ASSERT_EQ(world.GetVictorPlayerId(), -1);
    world.EliminatePlayer(1, 0);

    EXPECT_TRUE(world.IsPlayerDefeated(1));
    EXPECT_FALSE(world.IsPlayerDefeated(0));
    EXPECT_EQ(world.GetVictorPlayerId(), 0);
}

TEST(EliminationTests, EliminatePlayerIsIdempotent)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeRingParams(2, 1));

    world.EliminatePlayer(1, 0);
    Player* p1 = world.GetPlayerHandler().players.at(1).get();
    p1->roster.AddUnit(BattleUnit(999, 1, "militia")); // re-add something to prove a 2nd call is a no-op

    world.EliminatePlayer(1, 0); // already defeated — must not re-run cleanup
    EXPECT_EQ(p1->roster.units.size(), 1u) << "a second EliminatePlayer call must be a no-op";
}

TEST(EliminationTests, EliminationRemovesDefeatedDeployedUnitsRosterAndSpawnQueue)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeRingParams(3, 1));
    Player* p1 = world.GetPlayerHandler().players.at(1).get();

    p1->roster.AddUnit(BattleUnit(100100, 1, "militia"));
    world.GetDeployedUnits()[100101] = BattleUnit(100101, 1, "militia");
    world.GetDeployedUnits().at(100101).state = BattleUnitState::Marching;
    world.GetSpawnQueues()[{1, 0}].push_back(100101);

    // A unit belonging to the CONQUEROR (not the defeated player) must never
    // be touched, even if it's mid-duel — it self-heals back to Marching via
    // UnitCombatSystem's own next-tick opponent lookup, not EliminatePlayer.
    world.GetDeployedUnits()[100102] = BattleUnit(100102, 0, "militia");
    world.GetDeployedUnits().at(100102).state = BattleUnitState::FightingUnit;
    world.GetDeployedUnits().at(100102).routeFromPlayerId = 0;
    world.GetDeployedUnits().at(100102).routeToPlayerId = 1;

    world.EliminatePlayer(1, 0);

    EXPECT_TRUE(p1->roster.units.empty());
    EXPECT_EQ(world.GetDeployedUnits().count(100101), 0u);
    EXPECT_TRUE(world.GetSpawnQueues().find({1, 0}) == world.GetSpawnQueues().end() ||
                world.GetSpawnQueues().at({1, 0}).empty());
    ASSERT_EQ(world.GetDeployedUnits().count(100102), 1u) << "conqueror's own unit must not be touched";
}

TEST(EliminationTests, EliminationTransfersProductionBuildingWithUntouchedBuffersAndStartsRamp)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeRingParams(4, 1));
    Player* p0 = world.GetPlayerHandler().players.at(0).get();
    Player* p1 = world.GetPlayerHandler().players.at(1).get();
    TileMap& map = world.GetTileMap();

    Building* woodcutter = map.PlaceLoadedBuilding(map.GetIdFromCoords({5, 5}), p1, std::make_unique<Woodcutter>(500));
    ASSERT_NE(woodcutter, nullptr);
    auto* prod = woodcutter->GetComponent<ProductionComponent>();
    ASSERT_NE(prod, nullptr);
    prod->outputBuffers[ResourceType::WOOD] = ResourceBuffer{ResourceType::WOOD, 10};
    prod->outputBuffers[ResourceType::WOOD].SetStoredAmount(4);

    world.EliminatePlayer(1, 0);

    EXPECT_EQ(woodcutter->owner, p0);
    EXPECT_EQ(prod->outputBuffers[ResourceType::WOOD].buffer.size(), 4u)
        << "a captured building's own production buffers must stay untouched";

    bool hasRamp = false;
    for (const auto& ramp : p0->conqueredEconomy.GetRamps())
        if (ramp.buildingId == woodcutter->id)
            hasRamp = true;
    EXPECT_TRUE(hasRamp) << "the conqueror should start a productivity ramp on the captured building";
}

TEST(EliminationTests, EliminationTransfersInfrastructureAndCancelsRecruitment)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeRingParams(41, 1));
    Player* p0 = world.GetPlayerHandler().players.at(0).get();
    Player* p1 = world.GetPlayerHandler().players.at(1).get();
    TileMap& map = world.GetTileMap();

    Building* road = map.PlaceLoadedBuilding(map.GetIdFromCoords({5, 5}), p1, std::make_unique<Road>(4101));
    Building* barracks = map.PlaceLoadedBuilding(map.GetIdFromCoords({8, 5}), p1, std::make_unique<Barracks>(4102));
    ASSERT_NE(road, nullptr);
    ASSERT_NE(barracks, nullptr);
    auto* recruitment = barracks->GetComponent<RecruitmentComponent>();
    ASSERT_NE(recruitment, nullptr);
    recruitment->queue.push_back({"militia", 10.0, 10.0, false});
    ASSERT_FALSE(recruitment->queue.empty());

    world.EliminatePlayer(1, 0);

    EXPECT_EQ(road->owner, p0);
    EXPECT_EQ(barracks->owner, p0);
    EXPECT_TRUE(recruitment->queue.empty());
    EXPECT_TRUE(p1->GetTrackedBuildings().count(road) == 0);
    EXPECT_TRUE(p0->GetTrackedBuildings().count(road) == 1);
}

TEST(EliminationTests, UnitsMarchingToFallenHqReturnToRosterOnArrival)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeRingParams(42, 1));
    Player* p0 = world.GetPlayerHandler().players.at(0).get();

    const int unitId = 4201;
    BattleUnit unit(unitId, p0->id, "militia");
    unit.currentHp = unit.GetEffectiveMaxHp(*p0);
    unit.state = BattleUnitState::Marching;
    unit.routeFromPlayerId = 0;
    unit.routeToPlayerId = 1;
    unit.tileIndex = 0;
    world.GetDeployedUnits()[unitId] = unit;

    world.EliminatePlayer(1, 0);
    ASSERT_TRUE(world.GetDeployedUnits().contains(unitId));

    UnitMarchSystem::Update(world, 100000.0);
    EXPECT_FALSE(world.GetDeployedUnits().contains(unitId));
    ASSERT_TRUE(p0->roster.units.contains(unitId));
    EXPECT_EQ(p0->roster.units.at(unitId).state, BattleUnitState::InRoster);
}

TEST(EliminationTests, CapturedBuildingRejoinsConquerorsRoadNetworkAfterElimination)
{
    // T12 (docs/post_pivot_audit_2026-07-12.md): each Player owns an
    // independent RoadNetwork/NavigationMap. Reassigning Building::owner on
    // capture (proven by the test above) does nothing to either network on
    // its own — the conqueror's CalculatePath must separately learn that the
    // captured building's footprint tiles exist.
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeRingParams(10, 1));
    Player* p0 = world.GetPlayerHandler().players.at(0).get();
    Player* p1 = world.GetPlayerHandler().players.at(1).get();
    TileMap& map = world.GetTileMap();

    // Woodcutter (2x2) owned by p1, soon to be captured.
    Building* woodcutter = map.PlaceLoadedBuilding(map.GetIdFromCoords({5, 5}), p1, std::make_unique<Woodcutter>(9001));
    ASSERT_NE(woodcutter, nullptr);

    // p0's own storage (3x3) + a connecting road, right up to the woodcutter's
    // doorstep — as if p0 had already built infrastructure nearby.
    Building* road = map.PlaceLoadedBuilding(map.GetIdFromCoords({7, 5}), p0, std::make_unique<Road>(9002));
    ASSERT_NE(road, nullptr);
    Building* storage = map.PlaceLoadedBuilding(map.GetIdFromCoords({8, 5}), p0, std::make_unique<StorageBuilding>(9003));
    ASSERT_NE(storage, nullptr);
    for (int t : map.GetBuildingTileIds(road))
        p0->roadNetwork->UpdateNavMap(t, road);
    for (int t : map.GetBuildingTileIds(storage))
        p0->roadNetwork->UpdateNavMap(t, storage);

    // Before capture: p0's network has never heard of the woodcutter's tiles.
    EXPECT_TRUE(p0->roadNetwork->CalculatePath(storage, woodcutter).empty())
        << "p0 shouldn't be able to path to a building it doesn't own yet";

    world.EliminatePlayer(1, 0);
    ASSERT_EQ(woodcutter->owner, p0);

    std::vector<int> path = p0->roadNetwork->CalculatePath(storage, woodcutter);
    EXPECT_FALSE(path.empty())
        << "conqueror's road network must learn about a captured building's tiles";

    // A second elimination call is a no-op (idempotent) — re-registering must
    // not duplicate anything or throw.
    world.EliminatePlayer(1, 0);
    EXPECT_FALSE(p0->roadNetwork->CalculatePath(storage, woodcutter).empty());
}

TEST(EliminationTests, EliminationDrainsDefeatedStorageAndCreditsFractionToConqueror)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeRingParams(5, 1));
    Player* p0 = world.GetPlayerHandler().players.at(0).get();
    Player* p1 = world.GetPlayerHandler().players.at(1).get();

    Building* p1Hq = nullptr;
    for (Building* b : p1->GetTrackedBuildingsWithComponent<HqComponent>())
        p1Hq = b;
    Building* p0Hq = nullptr;
    for (Building* b : p0->GetTrackedBuildingsWithComponent<HqComponent>())
        p0Hq = b;
    ASSERT_NE(p1Hq, nullptr);
    ASSERT_NE(p0Hq, nullptr);
    int p1HqPositionId = p1Hq->positionId;

    auto* p1Storage = p1Hq->GetComponent<StorageComponent>();
    auto* p0Storage = p0Hq->GetComponent<StorageComponent>();
    ASSERT_NE(p1Storage, nullptr);
    ASSERT_NE(p0Storage, nullptr);

    ASSERT_TRUE(p1Storage->buffers.count(ResourceType::WOOD) > 0);
    ASSERT_TRUE(p0Storage->buffers.count(ResourceType::WOOD) > 0);
    p1Storage->buffers[ResourceType::WOOD].SetStoredAmount(100);
    // Isolate the conquest transfer from the common fair-start package;
    // otherwise the conqueror's HQ may already be near its WOOD capacity.
    p0Storage->buffers[ResourceType::WOOD].Clear();
    int conquerorBefore = static_cast<int>(p0Storage->buffers[ResourceType::WOOD].buffer.size());

    auto* p1HqComponent = p1Hq->GetComponent<HqComponent>();
    ASSERT_NE(p1HqComponent, nullptr);
    EXPECT_DOUBLE_EQ(p1HqComponent->captureStockFraction, 0.4);

    world.EliminatePlayer(1, 0);

    Building* capturedDepot = world.GetTileMap().GetBuilding(p1HqPositionId);
    ASSERT_NE(capturedDepot, nullptr);
    EXPECT_EQ(capturedDepot->buildingType, BuildingType::StorageBuilding);
    EXPECT_EQ(capturedDepot->owner, p0);
    const auto* capturedStorage = capturedDepot->GetComponent<StorageComponent>();
    ASSERT_NE(capturedStorage, nullptr);
    EXPECT_EQ(capturedStorage->buffers.at(ResourceType::WOOD).buffer.size(), 0u)
        << "the captured depot starts empty after spoils are transferred";
    int conquerorAfter = static_cast<int>(p0Storage->buffers[ResourceType::WOOD].buffer.size());
    EXPECT_EQ(conquerorAfter - conquerorBefore, 40) << "conqueror should gain floor(100 * 0.4) = 40";
}

TEST(EliminationTests, ConquestSpoilsFractionCanBeModifiedByResearchOrFocus)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeRingParams(51, 1));
    Player* conqueror = world.GetPlayerHandler().players.at(0).get();
    Player* defeated = world.GetPlayerHandler().players.at(1).get();

    Building* defeatedHq = *defeated->GetTrackedBuildingsWithComponent<HqComponent>().begin();
    Building* conquerorHq = *conqueror->GetTrackedBuildingsWithComponent<HqComponent>().begin();
    auto* defeatedStorage = defeatedHq->GetComponent<StorageComponent>();
    auto* conquerorStorage = conquerorHq->GetComponent<StorageComponent>();
    ASSERT_NE(defeatedStorage, nullptr);
    ASSERT_NE(conquerorStorage, nullptr);
    defeatedStorage->buffers[ResourceType::WOOD].SetStoredAmount(100);
    conquerorStorage->buffers[ResourceType::WOOD].Clear();
    int before = static_cast<int>(conquerorStorage->buffers[ResourceType::WOOD].buffer.size());

    conqueror->balanceModifiers.AddModifier(BalanceModifier{
        BalanceStat::ConquestSpoilsFraction, 0.2, 1.0, BalanceModifierScope::Global(),
        std::nullopt, std::nullopt, "test:conquest_spoils"});
    world.EliminatePlayer(defeated->id, conqueror->id);

    int after = static_cast<int>(conquerorStorage->buffers[ResourceType::WOOD].buffer.size());
    EXPECT_EQ(after - before, 60) << "base 40% plus 20 percentage points should capture 60%";
}

TEST(EliminationTests, ConqueredEconomyRampRisesLinearlyThenClearsAtCompletion)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeRingParams(6, 1));
    Player* p0 = world.GetPlayerHandler().players.at(0).get();

    constexpr int buildingId = 4242;
    p0->conqueredEconomy.AddRamp(buildingId, 10.0);
    p0->conqueredEconomy.Tick(*p0, 0.0);

    auto findMultiplier = [&]() -> double
    {
        for (const auto& modifier : p0->balanceModifiers.GetModifiers())
            if (modifier.scope.type == BalanceModifierScopeType::Building &&
                modifier.scope.buildingId == buildingId)
                return modifier.multiplier;
        return -1.0;
    };

    // t=0: productivity 0.3 -> cycle multiplier 1/0.3 ~= 3.333
    EXPECT_NEAR(findMultiplier(), 1.0 / 0.3, 1e-6);

    p0->conqueredEconomy.Tick(*p0, 5.0); // halfway through the 10s ramp
    // productivity = 0.3 + 0.7*0.5 = 0.65 -> multiplier ~= 1.538
    EXPECT_NEAR(findMultiplier(), 1.0 / 0.65, 1e-6);

    p0->conqueredEconomy.Tick(*p0, 10.0); // well past completion
    EXPECT_TRUE(p0->conqueredEconomy.GetRamps().empty());
    EXPECT_DOUBLE_EQ(findMultiplier(), -1.0) << "the modifier should be removed once the ramp completes";
}

TEST(EliminationTests, PathingRoutesThroughConqueredHq)
{
    GameWorld world;
    // A 3-player ring is a complete graph (every pair directly adjacent) —
    // a genuinely non-adjacent pair needs at least 4 players.
    world.InitWorld("test", nullptr, nullptr, MakeRingParams(7, 3));
    const auto& militaryRoads = world.GetMilitaryRoads();

    auto isEliminated = [&](int playerId)
    {
        auto it = world.GetPlayerHandler().players.find(playerId);
        return it != world.GetPlayerHandler().players.end() && it->second != nullptr && it->second->defeated;
    };

    // Find a non-adjacent pair, and any other player directly connected to
    // both of them to serve as the conquerable "middle" between them.
    int farA = -1, farB = -1, middle = -1;
    for (int a = 0; a < 4 && farA == -1; a++)
        for (int b = a + 1; b < 4 && farA == -1; b++)
            if (!militaryRoads.AreConnected(a, b))
            {
                farA = a;
                farB = b;
            }
    ASSERT_NE(farA, -1) << "expected at least one non-adjacent pair in a 4-player ring";
    for (int p = 0; p < 4; p++)
        if (p != farA && p != farB && militaryRoads.AreConnected(farA, p) && militaryRoads.AreConnected(p, farB))
            middle = p;
    ASSERT_NE(middle, -1);

    EXPECT_FALSE(PathingService::AreHqsConnected(militaryRoads, farA, farB, isEliminated))
        << "non-adjacent players shouldn't route before the player between them is eliminated";

    world.EliminatePlayer(middle, farA);

    EXPECT_TRUE(PathingService::AreHqsConnected(militaryRoads, farA, farB, isEliminated));
    MilitaryPath path = PathingService::FindMilitaryPath(militaryRoads, farA, farB, isEliminated);
    ASSERT_TRUE(path.IsValid());

    // The concatenated path must not repeat the conquered HQ's gate tile.
    std::vector<int> directA = militaryRoads.GetDirectedTiles(farA, middle);
    std::vector<int> directB = militaryRoads.GetDirectedTiles(middle, farB);
    EXPECT_EQ(path.tiles.size(), directA.size() + directB.size() - 1);
}

TEST(EliminationTests, DeployUnitsAcceptsTargetThroughConqueredHq)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeRingParams(8, 3));
    Player* p0 = world.GetPlayerHandler().players.at(0).get();

    int farTargetId = -1, middleId = -1;
    for (int candidate = 1; candidate < 4; candidate++)
    {
        if (world.GetMilitaryRoads().AreConnected(0, candidate))
            continue;
        farTargetId = candidate;
        for (int p = 1; p < 4; p++)
            if (p != candidate && world.GetMilitaryRoads().AreConnected(0, p) &&
                world.GetMilitaryRoads().AreConnected(p, candidate))
                middleId = p;
    }
    ASSERT_NE(farTargetId, -1) << "player 0 should have a non-adjacent target in a 4-player ring";
    ASSERT_NE(middleId, -1);

    int instanceId = p0->id * 100000 + p0->nextUnitInstanceId++;
    BattleUnit unit(instanceId, p0->id, "militia");
    unit.currentHp = unit.GetEffectiveMaxHp(*p0);
    p0->roster.AddUnit(std::move(unit));

    // Rejected before the middle player is eliminated — not a ring neighbor.
    std::uint64_t beforeCmd = world.SubmitCommand(GameCommand::DeployUnits(0, farTargetId, {instanceId}));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);
    auto results = world.ConsumeCommandResults();
    const GameCommandResult* before = FindResult(results, beforeCmd);
    ASSERT_NE(before, nullptr);
    EXPECT_FALSE(before->accepted);

    world.EliminatePlayer(middleId, 0);

    std::uint64_t afterCmd = world.SubmitCommand(GameCommand::DeployUnits(0, farTargetId, {instanceId}));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);
    results = world.ConsumeCommandResults();
    const GameCommandResult* after = FindResult(results, afterCmd);
    ASSERT_NE(after, nullptr);
    EXPECT_TRUE(after->accepted);
}

TEST(EliminationTests, SaveAndLoadPreservesDefeatedAndCapturedStorageState)
{
    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, MakeRingParams(9, 1));
    Player* p0 = world.GetPlayerHandler().players.at(0).get();
    Player* p1 = world.GetPlayerHandler().players.at(1).get();

    Building* woodcutter = world.GetTileMap().PlaceLoadedBuilding(
        world.GetTileMap().GetIdFromCoords({5, 5}), p1, std::make_unique<Woodcutter>(500));
    ASSERT_NE(woodcutter, nullptr);

    Building* p1Hq = nullptr;
    for (Building* b : p1->GetTrackedBuildingsWithComponent<HqComponent>())
        p1Hq = b;
    ASSERT_NE(p1Hq, nullptr);
    const int p1HqPositionId = p1Hq->positionId;
    p1Hq->GetComponent<HqComponent>()->currentHp = 123.0;
    p1Hq->GetComponent<HqComponent>()->thornsTimer = 1.5;

    world.EliminatePlayer(1, 0);
    ASSERT_EQ(woodcutter->owner, p0);
    ASSERT_FALSE(p0->conqueredEconomy.GetRamps().empty());
    Building* capturedStorage = world.GetTileMap().GetBuilding(p1HqPositionId);
    ASSERT_NE(capturedStorage, nullptr);
    EXPECT_EQ(capturedStorage->buildingType, BuildingType::StorageBuilding);
    EXPECT_EQ(capturedStorage->owner, p0);

    const auto path = (std::filesystem::temp_directory_path() / "rts_elimination_test.save").string();
    ASSERT_TRUE(world.SaveToFile(path));

    GameWorld loaded;
    ASSERT_TRUE(loaded.LoadFromFile(path, nullptr, nullptr));

    EXPECT_TRUE(loaded.IsPlayerDefeated(1));
    EXPECT_EQ(loaded.GetVictorPlayerId(), 0);

    Player* loadedP0 = loaded.GetPlayerHandler().players.at(0).get();
    Player* loadedP1 = loaded.GetPlayerHandler().players.at(1).get();
    Building* loadedWoodcutter = loaded.GetTileMap().GetBuilding(woodcutter->positionId);
    ASSERT_NE(loadedWoodcutter, nullptr);
    EXPECT_EQ(loadedWoodcutter->owner, loadedP0) << "captured building's ownership must survive a round trip";

    bool hasRamp = false;
    for (const auto& ramp : loadedP0->conqueredEconomy.GetRamps())
        if (ramp.buildingId == loadedWoodcutter->id)
            hasRamp = true;
    EXPECT_TRUE(hasRamp);

    EXPECT_TRUE(loadedP1->GetTrackedBuildingsWithComponent<HqComponent>().empty());
    Building* loadedCapturedStorage = loaded.GetTileMap().GetBuilding(p1HqPositionId);
    ASSERT_NE(loadedCapturedStorage, nullptr);
    EXPECT_EQ(loadedCapturedStorage->buildingType, BuildingType::StorageBuilding);
    EXPECT_EQ(loadedCapturedStorage->owner, loadedP0);

    std::filesystem::remove(path);
}
