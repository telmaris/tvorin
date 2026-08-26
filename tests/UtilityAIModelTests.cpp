#include "core/GameWorld.h"
#include "core/GameSession.h"
#include "ai/AIDifficulty.h"
#include "ai/AIActions.h"
#include "ai/AIEconomyBias.h"
#include "ai/AIModel.h"
#include "economy/Player.h"
#include "economy/BuildingComponents.h"
#include "economy/ProductionBuildings.h"
#include "simulation/MapGenerator.h"
#include "simulation/MilitaryRoadNetwork.h"
#include "warfare/BattleUnit.h"
#include "warfare/UnitDefinition.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

// AI rework etap 2 (TODO #2): the utility-based UtilityAIModel's economy +
// logistics layer, and the LogisticsComponent::IsConnectedToRoadNetwork check
// it keeps every building honest with.

namespace
{
    // Bare grass map, no Tile::owner — matches production maps post-pivot
    // (same rationale as RoadNetworkTests::FillUnownedMap).
    void FillGrassMap(TileMap& map, int width, int height)
    {
        map.params.sizeX = width;
        map.params.sizeY = height;
        map.tilemap.clear();
        map.tilemap.reserve(width * height);
        for (int i = 0; i < width * height; i++)
        {
            Tile tile{i};
            tile.tileType = TileType::GRASS;
            map.tilemap.push_back(std::move(tile));
        }
    }

    Vec2i FindFreeBuildingPosition(GameWorld& world, Player* owner, BuildingType type,
                                   Vec2i origin, int minRadius = 5, int maxRadius = 50)
    {
        const Vec2i footprint = GetBuildingDefinition(type).footprint;
        for (int radius = minRadius; radius <= maxRadius; radius += 2)
            for (int y = origin.y - radius; y <= origin.y + radius; y++)
                for (int x = origin.x - radius; x <= origin.x + radius; x++)
                {
                    const Vec2i position{x, y};
                    if (world.GetTileMap().IsInside(position) &&
                        world.GetTileMap().CanPlaceBuilding(type, position, footprint, owner))
                        return position;
                }
        return {-1, -1};
    }
}

TEST(UtilityAIModelTests, StorageHubClassificationExcludesMilitaryConsumerBuffers)
{
    Headquarters headquarters(1);
    StorageBuilding storage(2);
    Barracks barracks(3);
    DefenseTower tower(4);

    EXPECT_TRUE(AIActions::IsStorageHub(&headquarters));
    EXPECT_TRUE(AIActions::IsStorageHub(&storage));
    EXPECT_FALSE(AIActions::IsStorageHub(&barracks));
    EXPECT_FALSE(AIActions::IsStorageHub(&tower));
}

TEST(UtilityAIModelTests, StoredFoodCanBootstrapPopulationCapacityRecovery)
{
    AISituation situation;
    situation.populationCap = 140.0;
    situation.totalPopulation = 140.0;
    situation.manpower = 0.0;
    situation.foodProductionAlive = false;

    situation.foodProvisionsStored = 10;
    EXPECT_TRUE(UtilityAIModel::ManpowerEmergency(situation, 20.0));

    situation.foodProvisionsStored = 0;
    EXPECT_FALSE(UtilityAIModel::ManpowerEmergency(situation, 20.0));
}

TEST(UtilityAIModelTests, FocusScoringRecognizesUrgentMobilizationFromModifiers)
{
    AISituation situation;
    situation.enemyIncomingCount = 4;
    situation.enemyIncomingStrength = 100.0;
    situation.manpower = 0.0;
    situation.populationCap = 100.0;
    situation.totalPopulation = 100.0;
    situation.barracksCount = 1;

    TechnologyDefinition rapidRecruitment;
    rapidRecruitment.id = "rapid_recruitment";
    rapidRecruitment.researchTime = 30.0;
    BalanceModifier recruitTime;
    recruitTime.stat = BalanceStat::UnitRecruitTime;
    recruitTime.multiplier = 0.8;
    rapidRecruitment.modifiers.push_back(recruitTime);

    TechnologyDefinition constructionDrive;
    constructionDrive.id = "construction_drive";
    constructionDrive.researchTime = 10.0;
    BalanceModifier buildTime;
    buildTime.stat = BalanceStat::BuildTime;
    buildTime.multiplier = 0.8;
    constructionDrive.modifiers.push_back(buildTime);

    EXPECT_GT(UtilityAIModel::ScoreFocusChoice(rapidRecruitment, situation),
              UtilityAIModel::ScoreFocusChoice(constructionDrive, situation))
        << "an untagged recruit-time reduction must still win during emergency mobilization";
}

TEST(UtilityAIModelTests, FocusScoringUsesTagsForCurrentStrategicProblem)
{
    AISituation situation;
    situation.basicEconomyEstablished = true;
    situation.economyEstablished = true;
    situation.manpower = 100.0;
    situation.populationCap = 100.0;
    situation.totalPopulation = 50.0;
    situation.unconnectedPositionIds = {10, 20, 30};

    TechnologyDefinition logistics;
    logistics.id = "repair_supply_lines";
    logistics.tags = {"logistics"};

    TechnologyDefinition offense;
    offense.id = "offensive_spirit";
    offense.tags = {"military", "offense"};

    EXPECT_GT(UtilityAIModel::ScoreFocusChoice(logistics, situation),
              UtilityAIModel::ScoreFocusChoice(offense, situation))
        << "a broken road network should steer the current decision toward logistics";
}

TEST(UtilityAIModelTests, FocusScoringPrefersTheStrongerRelevantModifier)
{
    AISituation situation;
    situation.enemyIncomingCount = 2;
    situation.manpower = 5.0;
    situation.barracksCount = 1;

    TechnologyDefinition minorMobilization;
    minorMobilization.id = "minor_mobilization";
    BalanceModifier minorRecruitTime;
    minorRecruitTime.stat = BalanceStat::UnitRecruitTime;
    minorRecruitTime.multiplier = 0.95;
    minorMobilization.modifiers.push_back(minorRecruitTime);

    TechnologyDefinition majorMobilization = minorMobilization;
    majorMobilization.id = "major_mobilization";
    majorMobilization.modifiers.front().multiplier = 0.75;

    EXPECT_GT(UtilityAIModel::ScoreFocusChoice(majorMobilization, situation),
              UtilityAIModel::ScoreFocusChoice(minorMobilization, situation));
}

TEST(UtilityAIModelTests, FocusPlanningSeesStrategicPayoffThreeDecisionsAhead)
{
    AISituation situation;
    situation.enemyIncomingCount = 4;
    situation.enemyIncomingStrength = 100.0;
    situation.manpower = 0.0;
    situation.populationCap = 100.0;
    situation.totalPopulation = 100.0;
    situation.barracksCount = 1;

    TechnologyDefinition immediate;
    immediate.id = "immediate_military_bonus";
    immediate.tags = {"military"};
    immediate.researchTime = 10.0;

    TechnologyDefinition strategicRoot;
    strategicRoot.id = "strategic_root";
    strategicRoot.researchTime = 10.0;

    TechnologyDefinition preparation;
    preparation.id = "preparation";
    preparation.prerequisites = {strategicRoot.id};
    preparation.researchTime = 10.0;

    TechnologyDefinition organization;
    organization.id = "organization";
    organization.prerequisites = {preparation.id};
    organization.researchTime = 10.0;

    TechnologyDefinition massMobilization;
    massMobilization.id = "mass_mobilization";
    massMobilization.prerequisites = {organization.id};
    massMobilization.tags = {"military", "mobilization", "manpower"};
    massMobilization.researchTime = 10.0;
    BalanceModifier recruitTime;
    recruitTime.stat = BalanceStat::UnitRecruitTime;
    recruitTime.multiplier = 0.7;
    massMobilization.modifiers.push_back(recruitTime);
    BalanceModifier recruitCost;
    recruitCost.stat = BalanceStat::UnitRecruitManpowerCost;
    recruitCost.multiplier = 0.7;
    massMobilization.modifiers.push_back(recruitCost);

    const std::vector<TechnologyDefinition> definitions{
        immediate, strategicRoot, preparation, organization, massMobilization};
    const std::set<std::string> completed;

    const double immediatePlan = UtilityAIModel::ScoreFocusPlan(
        immediate, definitions, completed, situation, 3);
    const double shallowStrategicPlan = UtilityAIModel::ScoreFocusPlan(
        strategicRoot, definitions, completed, situation, 2);
    const double deepStrategicPlan = UtilityAIModel::ScoreFocusPlan(
        strategicRoot, definitions, completed, situation, 3);

    EXPECT_LT(shallowStrategicPlan, immediatePlan)
        << "the payoff is deliberately beyond a two-decision horizon";
    EXPECT_GT(deepStrategicPlan, immediatePlan)
        << "a strong mobilization payoff three decisions away should justify its setup chain";
}

TEST(UtilityAIModelTests, FocusPlanningSumsProfitFromEveryBranch)
{
    AISituation situation;
    situation.basicEconomyEstablished = true;
    situation.economyEstablished = true;
    situation.manpower = 100.0;
    situation.unconnectedPositionIds = {10, 20, 30};

    TechnologyDefinition root;
    root.id = "infrastructure_program";

    TechnologyDefinition roads;
    roads.id = "roads_branch";
    roads.prerequisites = {root.id};
    roads.tags = {"logistics"};

    TechnologyDefinition transport;
    transport.id = "transport_branch";
    transport.prerequisites = {root.id};
    transport.tags = {"logistics"};

    const std::set<std::string> completed;
    const std::vector<TechnologyDefinition> oneBranch{root, roads};
    const std::vector<TechnologyDefinition> twoBranches{root, roads, transport};

    EXPECT_GT(UtilityAIModel::ScoreFocusPlan(root, twoBranches, completed, situation, 3),
              UtilityAIModel::ScoreFocusPlan(root, oneBranch, completed, situation, 3))
        << "both profitable outgoing routes must contribute to the root decision";
}

TEST(UtilityAIModelTests, DebugMapDoesNotHideAIBuildCosts)
{
    MapParameters params;
    params.sizeX = 101;
    params.sizeY = 101;
    params.aiOpponentCount = 1;
    params.aiDifficulty = 0;
    params.seed = 20260716;

    params.debugMode = false;
    GameWorld regularWorld;
    regularWorld.InitWorld("regular-affordability", nullptr, nullptr, params);

    params.debugMode = true;
    GameWorld debugWorld;
    debugWorld.InitWorld("debug-affordability", nullptr, nullptr, params);

    Player* regularAi = regularWorld.GetPlayerHandler().players.at(1).get();
    Player* debugAi = debugWorld.GetPlayerHandler().players.at(1).get();
    ASSERT_NE(regularAi, nullptr);
    ASSERT_NE(debugAi, nullptr);

    const auto& barracks = GetBuildingDefinition(BuildingType::Barracks);
    EXPECT_EQ(AIActions::CountStoredResource(debugAi, ResourceType::TOOLS),
              AIActions::CountStoredResource(regularAi, ResourceType::TOOLS));
    EXPECT_EQ(debugAi->CanBuildDefinition(barracks), regularAi->CanBuildDefinition(barracks))
        << "debug free-build belongs to the local human and must not alter AI affordability";
}

TEST(UtilityAIModelTests, RecipeAssignmentKeepsTwoSmithsInStableRoles)
{
    MapParameters params;
    params.sizeX = 101;
    params.sizeY = 101;
    params.aiOpponentCount = 0;
    params.seed = 20260821;

    GameWorld world;
    world.InitWorld("smith-recipe-roles", nullptr, nullptr, params);
    Player* player = world.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(player, nullptr);

    Building* hq = AIActions::FindOwnedHeadquarters(player);
    ASSERT_NE(hq, nullptr);
    const Vec2i hqPosition = world.GetTileMap().GetCoordsFromId(hq->positionId);

    Vec2i firstPosition = FindFreeBuildingPosition(world, player, BuildingType::Smith, hqPosition);
    ASSERT_GE(firstPosition.x, 0);
    Building* firstSmith = player->Build<Smith>(firstPosition, false);
    ASSERT_NE(firstSmith, nullptr);

    // A generic deficit must not sacrifice the sole tools line.
    EXPECT_FALSE(AIActions::TrySwitchRecipeFor(world, player, ResourceType::IRON_SWORD));
    EXPECT_TRUE(firstSmith->GetComponent<ProductionComponent>()->products.contains(ResourceType::TOOLS));

    Vec2i secondPosition = FindFreeBuildingPosition(world, player, BuildingType::Smith, hqPosition);
    ASSERT_GE(secondPosition.x, 0);
    Building* secondSmith = player->Build<Smith>(secondPosition, false);
    ASSERT_NE(secondSmith, nullptr);

    // Policy may explicitly specialize stable ordinals.
    ASSERT_TRUE(AIActions::TryAssignRecipe(
        world, player, BuildingType::Smith, 1, ResourceType::IRON_SWORD));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    const auto* firstProduction = firstSmith->GetComponent<ProductionComponent>();
    const auto* secondProduction = secondSmith->GetComponent<ProductionComponent>();
    ASSERT_NE(firstProduction, nullptr);
    ASSERT_NE(secondProduction, nullptr);
    EXPECT_TRUE(firstProduction->products.contains(ResourceType::TOOLS));
    EXPECT_TRUE(secondProduction->products.contains(ResourceType::IRON_SWORD));

    // Once both demanded products have a line, neither generic request
    // produces churn or clears the workshops' buffers/connections.
    EXPECT_FALSE(AIActions::TrySwitchRecipeFor(world, player, ResourceType::TOOLS));
    EXPECT_FALSE(AIActions::TrySwitchRecipeFor(world, player, ResourceType::IRON_SWORD));
}

TEST(UtilityAIModelTests, IsConnectedToRoadNetworkDetectsRoadPathToStorage)
{
    TileMap map;
    FillGrassMap(map, 10, 6);
    Player player{0, map};

    // Proven delivery layout (RoadNetworkTests): storage {0,1}, consumer
    // {5,1}, the two roads at {3,2},{4,2} complete the only path.
    auto* storage = player.Build<StorageBuilding>(Vec2i{0, 1}, false);
    auto* barracks = player.Build<Barracks>(Vec2i{5, 1}, false);
    ASSERT_NE(storage, nullptr);
    ASSERT_NE(barracks, nullptr);

    auto* logistics = barracks->GetComponent<LogisticsComponent>();
    ASSERT_NE(logistics, nullptr);

    EXPECT_FALSE(logistics->IsConnectedToRoadNetwork(*barracks))
        << "no roads yet - there is no path to any storage";

    ASSERT_NE(player.Build<Road>(Vec2i{3, 2}, false), nullptr);
    ASSERT_NE(player.Build<Road>(Vec2i{4, 2}, false), nullptr);

    EXPECT_TRUE(logistics->IsConnectedToRoadNetwork(*barracks))
        << "with the road pair placed, the path to the storage exists";
}

// AI economy tuning plan (2026-07-18, Task 2): the amortized consumption
// bias must never poison the chain-input walk - it drives the FINAL
// resource's own deficit, not a second vote against its inputs. With a real
// WOOD producer standing (production rate > 0), diagnosing PLANKS must NOT
// redirect to WOOD even under a large WOOD bias.
TEST(UtilityAIModelTests, ChainWalkDoesNotDescendPastAProducedInput)
{
    TileMap map;
    FillGrassMap(map, 8, 8);
    Player player{0, map};

    std::map<ResourceType, int> bias{{ResourceType::WOOD, 80}, {ResourceType::PLANKS, 80}};

    // Case 1: WOOD is genuinely being produced - the walk must stop at PLANKS.
    player.economyTelemetry.current.productionRatesPerMinute[ResourceType::WOOD] = 10;
    auto diag = AIActions::DiagnoseResourceNeed(&player, ResourceType::PLANKS, 0, &bias);
    EXPECT_GT(diag.urgency, 0.5) << "PLANKS itself should still read as a real deficit";
    EXPECT_EQ(std::find(diag.missingInputs.begin(), diag.missingInputs.end(), ResourceType::WOOD),
              diag.missingInputs.end())
        << "WOOD is being produced - the bias must not force a redirect to it";

    // Case 2: no WOOD production and no stock - bootstrap must still work.
    player.economyTelemetry.current.productionRatesPerMinute[ResourceType::WOOD] = 0;
    diag = AIActions::DiagnoseResourceNeed(&player, ResourceType::PLANKS, 0, &bias);
    EXPECT_NE(std::find(diag.missingInputs.begin(), diag.missingInputs.end(), ResourceType::WOOD),
              diag.missingInputs.end())
        << "with no WOOD producer at all, the chain walk must still find WOOD missing";
}

// AI economy tuning plan (2026-07-18, Task 3): a producer already ordered
// and under construction must credit its future output by capping the
// deficit's urgency - otherwise the top-of-ladder deficit keeps winning
// every decision cycle even after a producer for it was already queued,
// since an in-progress build never lowers urgency on its own (playtest
// 2026-07-18: a strong WOOD bias produced a forest of Woodcutters and
// nothing else, because the deficit never rotated to the next problem).
TEST(UtilityAIModelTests, PendingProducerCapsDeficitUrgency)
{
    TileMap map;
    FillGrassMap(map, 8, 8);
    Player player{0, map};

    std::map<ResourceType, int> bias{{ResourceType::PLANKS, 80}};

    // Case 1: no producer standing at all - a real, uncapped deficit.
    auto diag = AIActions::DiagnoseResourceNeed(&player, ResourceType::PLANKS, 0, &bias);
    EXPECT_GE(diag.urgency, 0.6) << "with no producer standing at all, PLANKS should be a hard deficit";

    // Case 2: a LumberMill is already ordered (still under construction) -
    // its future output must be credited, capping urgency so the deficit
    // ladder can move on to the next problem instead of stacking another
    // producer for the same resource.
    auto* mill = player.Build<LumberMill>(Vec2i{0, 0}, false);
    ASSERT_NE(mill, nullptr);
    mill->constructionRemaining = 10.0;
    ASSERT_TRUE(mill->IsUnderConstruction());

    diag = AIActions::DiagnoseResourceNeed(&player, ResourceType::PLANKS, 0, &bias);
    EXPECT_LE(diag.urgency, 0.3 + 1e-9)
        << "a pending LumberMill should cap the PLANKS deficit so the AI stops piling up more of it";
}

// Acceptance for etap 2: an AI player with resources available builds out
// its economy (opening plan / deficit chain) and keeps wiring it into the
// road network — real GameCommands flowing through UpdateSimulation, no
// direct state pokes.
TEST(UtilityAIModelTests, AIBuildsEconomyAndConnectsIt)
{
    MapParameters params;
    // Pinned to the pre-2026-07-17 minimum: these tests exercise AI behavior,
    // not map size, and the 401x401 default costs ~1.8x the sim time.
    params.sizeX = 301;
    params.sizeY = 301;
    params.aiOpponentCount = 1;
    params.seed = 4242;

    GameWorld world;
    world.InitWorld("utility-ai-economy", nullptr, nullptr, params);

    Player* ai = world.GetPlayerHandler().players.at(1).get();
    ASSERT_NE(ai, nullptr);

    // Stock the AI's HQ so the opening plan is affordability-gated by design,
    // not by waiting out the (slow, separately-tested) natural economy.
    Building* hq = AIActions::FindOwnedHeadquarters(ai);
    ASSERT_NE(hq, nullptr);
    auto* hqStorage = hq->GetComponent<StorageComponent>();
    ASSERT_NE(hqStorage, nullptr);
    for (ResourceType type : {ResourceType::WOOD, ResourceType::PLANKS, ResourceType::STONE,
                              ResourceType::IRON, ResourceType::TOOLS})
    {
        auto it = hqStorage->buffers.find(type);
        if (it != hqStorage->buffers.end())
            it->second.SetStoredAmount(100);
        else
        {
            hqStorage->buffers[type] = ResourceBuffer{type, 200};
            hqStorage->buffers[type].SetStoredAmount(100);
        }
    }

    int initialBuildings = static_cast<int>(ai->GetTrackedBuildings().size());
    int initialRoads = AIActions::CountOwnedBuildings(ai, BuildingType::Road);

    // 120 sim-seconds at the fixed 100 Hz tick — several decision cycles.
    auto newRoadCount = [&]()
    { return AIActions::CountOwnedBuildings(ai, BuildingType::Road) - initialRoads; };
    auto newNonRoadCount = [&]()
    {
        int newTotal = static_cast<int>(ai->GetTrackedBuildings().size()) - initialBuildings;
        return newTotal - newRoadCount();
    };
    for (int tick = 0; tick < 12000 && !(newNonRoadCount() >= 1 && newRoadCount() >= 1); tick++)
        world.UpdateSimulation(0.01);

    int newRoads = newRoadCount();
    int newBuildings = newNonRoadCount();

    EXPECT_GE(newBuildings, 1) << "the AI should have placed at least one economy building";
    EXPECT_GE(newRoads, 1) << "the AI should be wiring its base into the road network";
}

TEST(UtilityAIModelTests, DefenseTowerPlacementAlwaysCoversTheMilitaryTrack)
{
    MapParameters params;
    params.sizeX = 301;
    params.sizeY = 301;
    params.aiOpponentCount = 1;
    params.seed = 314159;

    GameWorld world;
    world.InitWorld("utility-ai-tower-placement", nullptr, nullptr, params);
    Player* ai = world.GetPlayerHandler().players.at(1).get();
    ASSERT_NE(ai, nullptr);
    Building* hq = AIActions::FindOwnedHeadquarters(ai);
    ASSERT_NE(hq, nullptr);

    AIActions::AIActionState state;
    Vec2i anchor = AIActions::FindBuildAnchor(world, ai, BuildingType::DefenseTower,
                                              TileType::GRASS, hq, state);
    ASSERT_GE(anchor.x, 0);
    ASSERT_GE(anchor.y, 0);
    auto* tower = ai->Build<DefenseTower>(anchor, false);
    ASSERT_NE(tower, nullptr);
    EXPECT_GT(AIActions::CountTowerTrackCoverage(world, ai, tower), 0)
        << "AI tower sites must contribute real fire coverage to a military route";
}

TEST(UtilityAIModelTests, AIOrdersDefenseTowerWhenEnemyWaveIsDeployed)
{
    MapParameters params;
    params.sizeX = 301;
    params.sizeY = 301;
    params.aiOpponentCount = 1;
    params.aiDifficulty = 3;
    params.seed = 271828;

    GameWorld world;
    world.InitWorld("utility-ai-defense-alert", nullptr, nullptr, params);
    Player* human = world.GetPlayerHandler().players.at(0).get();
    Player* ai = world.GetPlayerHandler().players.at(1).get();
    ASSERT_NE(human, nullptr);
    ASSERT_NE(ai, nullptr);

    int instanceId = human->id * 100000 + human->nextUnitInstanceId++;
    BattleUnit attacker(instanceId, human->id, "militia");
    attacker.currentHp = attacker.GetEffectiveMaxHp(*human);
    human->roster.AddUnit(std::move(attacker));
    world.SubmitCommand(GameCommand::DeployUnits(human->id, ai->id, {instanceId}));

    bool towerOrdered = false;
    for (int tick = 0; tick < 3000 && !towerOrdered; tick++)
    {
        world.UpdateSimulation(0.01);
        towerOrdered = AIActions::CountCompletedOrQueuedBuildings(
            world, ai, BuildingType::DefenseTower) > 0;
    }

    EXPECT_TRUE(towerOrdered) << "AI detected the incoming wave but never ordered a defense tower";
}

// Etap 3: composition rule, pure and world-free. Under attack the pick
// maximizes staying power per cost (assets/data/units.rtsdata: knight);
// on the offensive an empty roster starts with a siege unit (ram), then
// tops the 2:1 mix back up with the best lane-clearer (knight by
// moveSpeed x roadAttack).
TEST(UtilityAIModelTests, RosterCompositionPrefersDefensiveUnitsUnderAttack)
{
    AISituation s;
    s.enemyIncomingCount = 4;
    s.myDeployedCount = 0;

    auto ranked = UtilityAIModel::RankUnitChoices(s);
    ASSERT_FALSE(ranked.empty());
    EXPECT_GT(ranked.front()->roadAttack, ranked.front()->siegeAttack)
        << "defensive posture should prefer a lane-fighting unit";
}

TEST(UtilityAIModelTests, RosterCompositionKeepsSiegeInTheOffensiveMix)
{
    AISituation s;  // nobody incoming — offensive posture

    auto ranked = UtilityAIModel::RankUnitChoices(s);
    ASSERT_FALSE(ranked.empty());
    EXPECT_EQ(ranked.front()->id, "knight")
        << "an empty offensive roster starts with lane-fighters (siege needs an escort)";

    s.rosterByDef["militia"] = 2;
    s.rosterCount = 2;
    ranked = UtilityAIModel::RankUnitChoices(s);
    ASSERT_FALSE(ranked.empty());
    EXPECT_GT(ranked.front()->siegeAttack, ranked.front()->roadAttack)
        << "with an escort standing, top the 2:1 mix up with siege";

    s.rosterByDef["ram"] = 1;
    s.rosterCount = 3;
    ranked = UtilityAIModel::RankUnitChoices(s);
    ASSERT_FALSE(ranked.empty());
    EXPECT_EQ(ranked.front()->id, "knight") << "at 1 siege per 2 fighters, back to lane-clearers";
}

TEST(UtilityAIModelTests, FailedWaveRaisesButNeverLowersTheStrengthThreshold)
{
    EXPECT_DOUBLE_EQ(UtilityAIModel::FailedWaveStrengthThreshold(0.0, 100.0), 108.0);
    EXPECT_DOUBLE_EQ(UtilityAIModel::FailedWaveStrengthThreshold(120.0, 100.0), 120.0);
}

// Etap 3 acceptance (successor of the removed AIMilitaryPipelineTests):
// given a Barracks, stocked unit costs and manpower, the AI recruits real
// units and deploys a wave — a real GameCommand::DeployUnits reaching
// UpdateSimulation. Barracks placed directly and the roster pre-seeded near
// the wave threshold, so the test exercises recruit+deploy without waiting
// out the natural economy.
TEST(UtilityAIModelTests, AIRecruitsAndDeploysAWave)
{
    MapParameters params;
    // Pinned to the pre-2026-07-17 minimum: these tests exercise AI behavior,
    // not map size, and the 401x401 default costs ~1.8x the sim time.
    params.sizeX = 301;
    params.sizeY = 301;
    params.aiOpponentCount = 1;
    params.seed = 777;
    params.aiDifficulty = 2; // Normal keeps the three-unit opening threshold

    GameWorld world;
    world.InitWorld("utility-ai-military", nullptr, nullptr, params);

    Player* ai = world.GetPlayerHandler().players.at(1).get();
    ASSERT_NE(ai, nullptr);

    Building* hq = AIActions::FindOwnedHeadquarters(ai);
    ASSERT_NE(hq, nullptr);
    Vec2i hqPos = world.GetTileMap().GetCoordsFromId(hq->positionId);

    // Free spot for a test Barracks near the AI's HQ — expanding square scan.
    Vec2i barracksPos{-1, -1};
    Vec2i footprint = GetBuildingDefinition(BuildingType::Barracks).footprint;
    for (int radius = 5; radius <= 40 && barracksPos.x < 0; radius += 2)
        for (int y = hqPos.y - radius; y <= hqPos.y + radius && barracksPos.x < 0; y++)
            for (int x = hqPos.x - radius; x <= hqPos.x + radius; x++)
            {
                Vec2i pos{x, y};
                if (world.GetTileMap().IsInside(pos) &&
                    world.GetTileMap().CanPlaceBuilding(BuildingType::Barracks, pos, footprint, ai))
                {
                    barracksPos = pos;
                    break;
                }
            }
    ASSERT_GE(barracksPos.x, 0) << "no free spot for a test Barracks near the AI's HQ";

    Building* barracks = ai->Build<Barracks>(barracksPos, false);
    ASSERT_NE(barracks, nullptr);
    ASSERT_FALSE(barracks->IsUnderConstruction());

    // Stock militia's cost locally + manpower for the remaining recruits.
    auto* storage = barracks->GetComponent<LocalResourceBufferComponent>();
    ASSERT_NE(storage, nullptr);
    auto foodIt = storage->buffers.find(ResourceType::FOOD_PROVISIONS);
    if (foodIt == storage->buffers.end())
    {
        storage->buffers[ResourceType::FOOD_PROVISIONS] = ResourceBuffer{ResourceType::FOOD_PROVISIONS, 40};
        foodIt = storage->buffers.find(ResourceType::FOOD_PROVISIONS);
    }
    foodIt->second.SetStoredAmount(40);
    ai->strategicResources.Set(StrategicResourceType::Manpower, 200);

    // Drain sword/siege-cost stocks from every AI storage so militia is the
    // only affordable pick — keeps the test's timeline on the 8 s militia
    // recruit instead of a 26 s ram plus multi-resource deliveries.
    for (Building* building : ai->GetTrackedBuildingsWithComponent<StorageComponent>())
    {
        auto* buildingStorage = building != nullptr ? building->GetComponent<StorageComponent>() : nullptr;
        if (buildingStorage == nullptr || building == barracks)
            continue;
        for (ResourceType type : {ResourceType::PLANKS, ResourceType::IRON,
                                  ResourceType::IRON_SWORD, ResourceType::STEEL_SWORD})
        {
            auto it = buildingStorage->buffers.find(type);
            if (it != buildingStorage->buffers.end())
                it->second.Clear();
        }
    }

    // The new opening raid threshold is 3: seed two so this acceptance still
    // proves both a real recruitment and the subsequent deployment.
    for (int i = 0; i < 2; i++)
    {
        int instanceId = ai->id * 100000 + ai->nextUnitInstanceId++;
        BattleUnit unit(instanceId, ai->id, "militia");
        unit.currentHp = unit.GetEffectiveMaxHp(*ai);
        ai->roster.AddUnit(std::move(unit));
    }
    int instanceCounterBefore = ai->nextUnitInstanceId;

    // 60 sim-seconds: recruit the missing unit(s), deploy, first unit spawns.
    bool deployed = false;
    for (int tick = 0; tick < 6000 && !deployed; tick++)
    {
        world.UpdateSimulation(0.01);
        for (const auto& [instanceId, unit] : world.GetDeployedUnits())
            if (unit.ownerPlayerId == ai->id)
            {
                deployed = true;
                break;
            }
    }

    EXPECT_TRUE(deployed) << "the AI never got a unit marching on the military road\n"
                          << world.GetAITrace(ai->id);
    EXPECT_GT(ai->nextUnitInstanceId, instanceCounterBefore)
        << "the AI should have recruited at least one real unit itself\n"
        << world.GetAITrace(ai->id);
}

// AI economy bias (user design 2026-07-17): amortized non-constant costs are
// loaded from a data file, scale with difficulty, and surface as virtual
// consumption in the AI's resource diagnosis — so a resource nothing consumes
// YET (like STONE for roads) still registers as a deficit to provision for.
TEST(UtilityAIModelTests, EconomyBiasLoadsFromFileAndScalesWithDifficulty)
{
    const auto path = (std::filesystem::temp_directory_path() / "rts_ai_bias_fixture.rtsdata").string();
    {
        std::ofstream file(path);
        file << "ai_economy_bias\n"
                "    difficulty_scale 0.5 0.75 1.0 2.0\n"
                "    consumption STONE 10\n"
                "    consumption BOGUS_RESOURCE 99\n"
                "    consumption PLANKS 8\n"
                "end\n";
    }

    AIEconomyBias bias = LoadAIEconomyBiasFromFile(path);
    std::filesystem::remove(path);

    EXPECT_EQ(bias.virtualConsumptionPerMinute.size(), 2u) << "unknown resource names are dropped";
    EXPECT_EQ(bias.ScaledConsumption(ResourceType::STONE, 0), 5);
    EXPECT_EQ(bias.ScaledConsumption(ResourceType::STONE, 2), 10);
    EXPECT_EQ(bias.ScaledConsumption(ResourceType::STONE, 3), 20);
    EXPECT_EQ(bias.ScaledConsumption(ResourceType::WOOD, 3), 0) << "no entry means zero bias";

    auto scaled = bias.ScaledMap(1);
    EXPECT_EQ(scaled.at(ResourceType::STONE), 7);
    EXPECT_EQ(scaled.at(ResourceType::PLANKS), 6);
}

TEST(UtilityAIModelTests, ConsumptionBiasCreatesADeficitWithoutRealConsumption)
{
    TileMap map;
    FillGrassMap(map, 8, 8);
    Player player{0, map};

    // No production, no consumption, nothing stored: without a bias STONE is
    // a non-issue; with one it must diagnose as a missing-producer deficit.
    AIActions::AIResourceDiagnosis plain = AIActions::DiagnoseResourceNeed(&player, ResourceType::STONE);
    EXPECT_LE(plain.urgency, 0.05);

    std::map<ResourceType, int> bias{{ResourceType::STONE, 10}};
    AIActions::AIResourceDiagnosis biased =
        AIActions::DiagnoseResourceNeed(&player, ResourceType::STONE, 0, &bias);
    EXPECT_GT(biased.urgency, 0.5) << "amortized consumption with zero production is a hard deficit";
    EXPECT_EQ(biased.reason, "missing producer");
}

// Playtest regression (2026-07-16): the AI wedged at the military track edge
// forever — SubmitRoadPath always submitted plain Road, which placement
// validation refuses on track tiles (only Bridge is legal there), and its BFS
// treated existing bridges as obstacles. Crossing the track must produce a
// real Bridge order and a working road connection.
TEST(UtilityAIModelTests, SubmitRoadPathAvoidsBridgeWhenDetourIsCheaper)
{
    MapParameters params;
    params.sizeX = 81;
    params.sizeY = 81;
    params.aiOpponentCount = 1;
    params.seed = 6;  // known-good small ring (same as BridgeTests)

    GameWorld world;
    world.InitWorld("ai-bridge-crossing", nullptr, nullptr, params);

    Player* human = world.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(human, nullptr);
    const MilitaryRoute* route = world.GetMilitaryRoads().FindRoute(0, 1);
    ASSERT_NE(route, nullptr);
    ASSERT_GE(route->tiles.size(), 9u);

    TileMap& map = world.GetTileMap();

    // Stock the human so the submitted Road/Bridge commands can pay their
    // build costs (Bridge: PLANKS 6 + STONE 6 per tile).
    Building* hq = AIActions::FindOwnedHeadquarters(human);
    ASSERT_NE(hq, nullptr);
    auto* hqStorage = hq->GetComponent<StorageComponent>();
    ASSERT_NE(hqStorage, nullptr);
    for (ResourceType type : {ResourceType::PLANKS, ResourceType::STONE})
    {
        auto it = hqStorage->buffers.find(type);
        if (it == hqStorage->buffers.end())
        {
            hqStorage->buffers[type] = ResourceBuffer{type, 200};
            it = hqStorage->buffers.find(type);
        }
        it->second.SetStoredAmount(100);
    }

    // Find a straight track segment away from both HQs and drop a 1x1 road
    // fixture two tiles off the track on each side — the shortest crossing is
    // then road, BRIDGE (on the track), road.
    Building* sideA = nullptr;
    Building* sideB = nullptr;
    for (size_t i = 2; i + 2 < route->tiles.size() && sideB == nullptr; i++)
    {
        Vec2i a = map.GetCoordsFromId(route->tiles[i - 1]);
        Vec2i b = map.GetCoordsFromId(route->tiles[i]);
        Vec2i c = map.GetCoordsFromId(route->tiles[i + 1]);
        bool straight = (a.x == b.x && b.x == c.x) || (a.y == b.y && b.y == c.y);
        if (!straight)
            continue;
        Vec2i dir{c.x - a.x == 0 ? 0 : (c.x - a.x > 0 ? 1 : -1),
                  c.y - a.y == 0 ? 0 : (c.y - a.y > 0 ? 1 : -1)};
        Vec2i perp{-dir.y, dir.x};
        Vec2i posA{b.x + perp.x * 2, b.y + perp.y * 2};
        Vec2i posB{b.x - perp.x * 2, b.y - perp.y * 2};
        if (!map.IsInside(posA) || !map.IsInside(posB))
            continue;

        sideA = human->Build<Road>(posA, false);
        if (sideA == nullptr)
            continue;
        sideB = human->Build<Road>(posB, false);
        if (sideB == nullptr)
            sideA = nullptr;  // leftover fixture road is harmless — keep scanning
    }
    ASSERT_NE(sideA, nullptr) << "no usable straight track segment found for the fixture";
    ASSERT_NE(sideB, nullptr);

    AIActions::AIActionState state;
    ASSERT_TRUE(AIActions::SubmitRoadPath(world, human, sideA, sideB, state))
        << "the crossing (road, bridge, road) should be planned and submitted";

    // Commands place the crossing immediately, but roads/bridges only join
    // the navigation map once CONSTRUCTION completes (GameWorld.Commands.cpp
    // skips navmap registration for under-construction placements; the
    // completion path in GameWorld.TileMap.cpp registers them) — so give the
    // builder queue real sim time.
    bool connected = false;
    for (int tick = 0; tick < 12000 && !connected; tick++)
    {
        world.UpdateSimulation(0.01);
        state.Decay(0.01);
        // A long economical detour is intentionally issued in several small
        // batches. Wait for the reservation to expire before planning the
        // next segment, exactly as UtilityAIModel does in live play.
        if (tick > 0 && tick % 650 == 0)
            AIActions::SubmitRoadPath(world, human, sideA, sideB, state);
        if (tick % 25 == 24)
            connected = !human->roadNetwork->CalculatePath(sideA, sideB).empty();
    }

    bool bridgeOnTrack = false;
    for (const auto& tile : map.tilemap)
    {
        const Building* building = tile.building.get();
        if (building != nullptr && building->owner == human &&
            building->buildingType == BuildingType::Bridge && tile.isMilitaryRoad)
        {
            bridgeOnTrack = true;
            break;
        }
    }
    EXPECT_FALSE(bridgeOnTrack) << "the AI should prefer the cheaper road detour over a new Bridge";
    EXPECT_TRUE(connected)
        << "the two fixtures should still end up road-connected through the detour";
}

// AI economy tuning plan (2026-07-18, Task 1): a road-tile reservation used
// to block a tile EVEN AFTER the road physically stood there, so a second
// connection planned minutes later saw its own just-built corridor as a wall
// and dug a full parallel line beside it (playtest 2026-07-17: "carpets" of
// redundant roads). Once a tile holds a real Building, the reservation must
// be irrelevant — only emptiness should ever gate on it.
TEST(UtilityAIModelTests, SubmitRoadPathReusesJustPlacedCorridor)
{
    MapParameters params;
    params.sizeX = 81;
    params.sizeY = 81;
    // No AI opponent: this test only drives the human player directly
    // through AIActions::SubmitRoadPath and needs no military ring. A live
    // AI opponent would independently churn its own economy across many
    // resource types during every UpdateSimulation tick below — pure noise here.
    params.aiOpponentCount = 0;
    params.seed = 6;

    GameWorld world;
    world.InitWorld("ai-road-reuse", nullptr, nullptr, params);

    Player* human = world.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(human, nullptr);
    TileMap& map = world.GetTileMap();

    Building* hq = AIActions::FindOwnedHeadquarters(human);
    ASSERT_NE(hq, nullptr);
    Vec2i hqPos = map.GetCoordsFromId(hq->positionId);

    // Stock STONE so every submitted Road command can pay its build cost
    // (buildings.rtsdata: Road costs STONE 2) — TryPayBuildCost pulls from
    // any of the player's storage, so stocking the HQ is enough. Kept to
    // just above what the ~13 road tiles this test carves actually need
    // (26 STONE): SetStoredAmount allocates only the requested live instances.
    auto* hqStorage = hq->GetComponent<StorageComponent>();
    ASSERT_NE(hqStorage, nullptr);
    auto stoneIt = hqStorage->buffers.find(ResourceType::STONE);
    if (stoneIt == hqStorage->buffers.end())
    {
        hqStorage->buffers[ResourceType::STONE] = ResourceBuffer{ResourceType::STONE, 60};
        stoneIt = hqStorage->buffers.find(ResourceType::STONE);
    }
    stoneIt->second.SetStoredAmount(40);

    // Find a straight, fully open vertical run of 9 tiles (clear of the
    // military track, resource patches and the starting village) well past
    // the HQ apron — A and B anchor the run's ends, C sits 2 tiles off its
    // midpoint (the second connection's shortest legal join point).
    const Vec2i footprint{1, 1};
    constexpr int kRunLength = 8;
    Vec2i posA{-1, -1};
    Vec2i posB{-1, -1};
    Vec2i posC{-1, -1};
    for (int radius = 20; radius <= 50 && posA.x < 0; radius += 3)
    {
        for (int dx = -radius; dx <= radius && posA.x < 0; dx += 2)
        {
            Vec2i candidateA{hqPos.x + dx, hqPos.y + radius};
            Vec2i candidateB{candidateA.x, candidateA.y + kRunLength};
            if (!map.IsInside(candidateA) || !map.IsInside(candidateB))
                continue;

            bool columnClear = true;
            for (int y = candidateA.y; y <= candidateB.y && columnClear; y++)
                columnClear = map.CanPlaceBuilding(BuildingType::Road, {candidateA.x, y}, footprint, human);
            if (!columnClear)
                continue;

            Vec2i candidateC{candidateA.x + 2, candidateA.y + kRunLength / 2};
            if (!map.IsInside(candidateC) ||
                !map.CanPlaceBuilding(BuildingType::Road, candidateC, footprint, human))
                continue;

            posA = candidateA;
            posB = candidateB;
            posC = candidateC;
        }
    }
    ASSERT_GE(posA.x, 0) << "no usable open corridor found for the fixture";

    Building* fixtureA = human->Build<Road>(posA, false);
    Building* fixtureB = human->Build<Road>(posB, false);
    ASSERT_NE(fixtureA, nullptr);
    ASSERT_NE(fixtureB, nullptr);

    AIActions::AIActionState state;
    ASSERT_TRUE(AIActions::SubmitRoadPath(world, human, fixtureA, fixtureB, state))
        << "the first straight connection A->B should be planned and submitted";

    // Enough sim time for the queued commands to be processed and placed
    // (as Building instances, possibly still under construction) — the 6s
    // reservations on these exact tiles stay live in `state` the whole time,
    // since nothing here ever calls AIActionState::Decay.
    for (int tick = 0; tick < 50; tick++)
        world.UpdateSimulation(0.01);

    int roadsAfterFirstPlan = AIActions::CountOwnedBuildings(human, BuildingType::Road);
    ASSERT_GE(roadsAfterFirstPlan, kRunLength - 1)
        << "the first connection should have carved (most of) the straight run";

    Building* fixtureC = human->Build<Road>(posC, false);
    ASSERT_NE(fixtureC, nullptr);

    ASSERT_TRUE(AIActions::SubmitRoadPath(world, human, fixtureC, fixtureB, state))
        << "the second connection C->B should join the existing corridor";

    for (int tick = 0; tick < 50; tick++)
        world.UpdateSimulation(0.01);

    int roadsAfterSecondPlan = AIActions::CountOwnedBuildings(human, BuildingType::Road);
    // -1 for fixtureC itself, which SubmitRoadPath never needs to place
    // (already a Road) but which the total road count now includes.
    int newRoadsForSecondConnection = roadsAfterSecondPlan - roadsAfterFirstPlan - 1;

    // C sits 2 tiles off the corridor at its midpoint — Manhattan distance 2
    // to the nearest reusable corridor tile. Without the Task 1 fix the
    // planner sees the whole existing corridor as blocked and instead digs a
    // full second straight line from C down to B (kRunLength/2 + new tiles),
    // running parallel to the first the entire way.
    EXPECT_LE(newRoadsForSecondConnection, 4)
        << "the AI dug a parallel road instead of joining its own just-placed corridor"
        << " (roadsAfterFirstPlan=" << roadsAfterFirstPlan
        << ", roadsAfterSecondPlan=" << roadsAfterSecondPlan << ")";
}

// Etap 5: a standing idle University (or an unstarted focus) gets used — the
// Research need starts a real focus and/or technology through commands.
TEST(UtilityAIModelTests, AIUsesAnIdleUniversityOrStartsAFocus)
{
    MapParameters params;
    // Pinned to the pre-2026-07-17 minimum: these tests exercise AI behavior,
    // not map size, and the 401x401 default costs ~1.8x the sim time.
    params.sizeX = 301;
    params.sizeY = 301;
    params.aiOpponentCount = 1;
    params.seed = 9001;

    GameWorld world;
    world.InitWorld("utility-ai-research", nullptr, nullptr, params);

    Player* ai = world.GetPlayerHandler().players.at(1).get();
    ASSERT_NE(ai, nullptr);

    Building* hq = AIActions::FindOwnedHeadquarters(ai);
    ASSERT_NE(hq, nullptr);
    Vec2i hqPos = world.GetTileMap().GetCoordsFromId(hq->positionId);

    Vec2i uniPos{-1, -1};
    Vec2i footprint = GetBuildingDefinition(BuildingType::University).footprint;
    for (int radius = 5; radius <= 40 && uniPos.x < 0; radius += 2)
        for (int y = hqPos.y - radius; y <= hqPos.y + radius && uniPos.x < 0; y++)
            for (int x = hqPos.x - radius; x <= hqPos.x + radius; x++)
            {
                Vec2i pos{x, y};
                if (world.GetTileMap().IsInside(pos) &&
                    world.GetTileMap().CanPlaceBuilding(BuildingType::University, pos, footprint, ai))
                {
                    uniPos = pos;
                    break;
                }
            }
    ASSERT_GE(uniPos.x, 0);

    Building* university = ai->Build<University>(uniPos, false);
    ASSERT_NE(university, nullptr);
    ASSERT_FALSE(university->IsUnderConstruction());

    auto researchStarted = [&]()
    {
        const auto* research = university->GetComponent<ResearchComponent>();
        bool techRunning = research != nullptr && research->remaining > 0.0;
        return techRunning || !ai->focuses.GetActiveFocusId().empty();
    };

    for (int tick = 0; tick < 3000 && !researchStarted(); tick++)
        world.UpdateSimulation(0.01);

    EXPECT_TRUE(researchStarted())
        << "with an idle University standing, the AI should start a focus or a technology";
}

TEST(UtilityAIModelTests, AIStartsNextFocusAfterActionCadence)
{
    MapParameters params;
    params.sizeX = 301;
    params.sizeY = 301;
    params.aiOpponentCount = 1;
    params.aiDifficulty = 0;
    params.seed = 20260725;

    GameWorld world;
    world.InitWorld("immediate-ai-focus", nullptr, nullptr, params);
    Player* ai = world.GetPlayerHandler().players.at(1).get();
    ASSERT_NE(ai, nullptr);

    world.UpdateSimulation(FixedSimulationClock::FixedDt);
    const std::string firstFocus = ai->focuses.GetActiveFocusId();
    ASSERT_FALSE(firstFocus.empty()) << "AI should start its first focus on its first controller tick";

    ai->UpdateFocus(100000.0);
    ASSERT_TRUE(ai->focuses.GetActiveFocusId().empty());
    ASSERT_TRUE(ai->focuses.HasFocus(firstFocus));

    bool startedNextFocus = false;
    for (int tick = 0; tick < 1200 && !startedNextFocus; tick++)
    {
        world.UpdateSimulation(FixedSimulationClock::FixedDt);
        startedNextFocus = !ai->focuses.GetActiveFocusId().empty();
    }
    EXPECT_TRUE(startedNextFocus)
        << "focus selection should resume when the shared action cadence is available";
}

// The per-player personality must be seeded, never wall-clock or unseeded.
// Difficulty itself no longer changes decision quality.
TEST(UtilityAIModelTests, TwoWorldsSameSeedAIStayInSync)
{
    MapParameters params;
    params.sizeX = 101;
    params.sizeY = 101;
    params.aiOpponentCount = 1;
    params.seed = 31337;
    params.aiDifficulty = 1;

    GameWorld worldA;
    GameWorld worldB;
    worldA.InitWorld("ai-determinism", nullptr, nullptr, params);
    worldB.InitWorld("ai-determinism", nullptr, nullptr, params);

    // Ten simulated seconds cover multiple decision cycles; map size is
    // irrelevant to the seeded personality sequence this test guards.
    for (int tick = 0; tick < 1000; tick++)
    {
        worldA.UpdateSimulation(FixedSimulationClock::FixedDt);
        worldB.UpdateSimulation(FixedSimulationClock::FixedDt);
        if (tick % 100 == 99)
            ASSERT_EQ(worldA.BuildChecksum(), worldB.BuildChecksum()) << "tick=" << tick;
    }
}

// Personality bias and wave-size jitter are active on every difficulty.
// Two same-seed Hard worlds must still stay checksum-identical.
TEST(UtilityAIModelTests, TwoWorldsSameSeedHardDifficultyWithPersonalityStayInSync)
{
    MapParameters params;
    params.sizeX = 101;
    params.sizeY = 101;
    params.aiOpponentCount = 1;
    params.seed = 31337;
    params.aiDifficulty = 3;

    GameWorld worldA;
    GameWorld worldB;
    worldA.InitWorld("personality-determinism", nullptr, nullptr, params);
    worldB.InitWorld("personality-determinism", nullptr, nullptr, params);

    for (int tick = 0; tick < 1000; tick++)
    {
        worldA.UpdateSimulation(FixedSimulationClock::FixedDt);
        worldB.UpdateSimulation(FixedSimulationClock::FixedDt);
        if (tick % 100 == 99)
            ASSERT_EQ(worldA.BuildChecksum(), worldB.BuildChecksum()) << "tick=" << tick;
    }
}

// Personality bias (2026-07-20, user request: "2 AI nie gra identycznie"):
// two players on the SAME map seed must draw DIFFERENT personality values
// (seed XOR player id) - that's the entire point. Verified against the
// private state directly through the test-seam getters.
TEST(UtilityAIModelTests, PersonalityBiasDiffersAcrossPlayersButIsStablePerSeed)
{
    MapParameters params;
    params.aiOpponentCount = 1;
    params.seed = 2026;

    GameWorld world;
    world.InitWorld("personality-diversity", nullptr, nullptr, params);

    Player* playerA = world.GetPlayerHandler().players.at(0).get();
    Player* playerB = world.GetPlayerHandler().players.at(1).get();
    ASSERT_NE(playerA, nullptr);
    ASSERT_NE(playerB, nullptr);

    UtilityAIModel modelA(playerA->id);
    UtilityAIModel modelB(playerB->id);
    modelA.Update(world, playerA, 0.01);
    modelB.Update(world, playerB, 0.01);

    bool anyNeedDiffers = false;
    for (int i = 0; i < static_cast<int>(AINeed::Count); i++)
        if (modelA.GetPersonalityNeedBias(static_cast<AINeed>(i)) !=
            modelB.GetPersonalityNeedBias(static_cast<AINeed>(i)))
            anyNeedDiffers = true;
    EXPECT_TRUE(anyNeedDiffers || modelA.GetPersonalityWaveBias() != modelB.GetPersonalityWaveBias())
        << "two different player ids on the same map seed drew an identical personality";

    // Stability: re-seeding the SAME player id against the SAME map seed must
    // reproduce the identical values - it's a pure function of (seed, id).
    UtilityAIModel modelARepeat(playerA->id);
    modelARepeat.Update(world, playerA, 0.01);
    for (int i = 0; i < static_cast<int>(AINeed::Count); i++)
        EXPECT_DOUBLE_EQ(modelA.GetPersonalityNeedBias(static_cast<AINeed>(i)),
                         modelARepeat.GetPersonalityNeedBias(static_cast<AINeed>(i)));
    EXPECT_EQ(modelA.GetPersonalityWaveBias(), modelARepeat.GetPersonalityWaveBias());
}

TEST(UtilityAIModelTests, DifficultyProfilesDoNotGrantStartingAdvantage)
{
    EXPECT_EQ(GetAIDifficultyProfile(AIDifficulty::Primitive).name, "Primitive");
    EXPECT_EQ(GetAIDifficultyProfile(AIDifficulty::Easy).name, "Easy");
    EXPECT_EQ(GetAIDifficultyProfile(AIDifficulty::Normal).name, "Normal");
    EXPECT_EQ(GetAIDifficultyProfile(AIDifficulty::Hard).name, "Hard");
    EXPECT_DOUBLE_EQ(GetAIDifficultyProfile(AIDifficulty::Primitive).actionIntervalSeconds, 10.0);
    EXPECT_DOUBLE_EQ(GetAIDifficultyProfile(AIDifficulty::Easy).actionIntervalSeconds, 6.0);
    EXPECT_DOUBLE_EQ(GetAIDifficultyProfile(AIDifficulty::Normal).actionIntervalSeconds, 3.0);
    EXPECT_DOUBLE_EQ(GetAIDifficultyProfile(AIDifficulty::Hard).actionIntervalSeconds, 1.0);

    MapParameters params;
    params.aiOpponentCount = 1;
    params.seed = 5150;

    params.aiDifficulty = 0;
    GameWorld primitiveWorld;
    primitiveWorld.InitWorld("difficulty-baseline", nullptr, nullptr, params);

    params.aiDifficulty = 3;
    GameWorld hardWorld;
    hardWorld.InitWorld("difficulty-hard", nullptr, nullptr, params);

    Player* aiPrimitive = primitiveWorld.GetPlayerHandler().players.at(1).get();
    Player* aiHard = hardWorld.GetPlayerHandler().players.at(1).get();
    Player* humanHard = hardWorld.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(aiPrimitive, nullptr);
    ASSERT_NE(aiHard, nullptr);
    ASSERT_NE(humanHard, nullptr);

    auto expectSameStartingState = [](Player* lhs, Player* rhs)
    {
        ASSERT_NE(lhs, nullptr);
        ASSERT_NE(rhs, nullptr);
        EXPECT_EQ(lhs->GetPopulationCap(), rhs->GetPopulationCap());
        EXPECT_DOUBLE_EQ(lhs->strategicResources.Get(StrategicResourceType::Manpower),
                         rhs->strategicResources.Get(StrategicResourceType::Manpower));
        for (ResourceType resource : resourceTypes)
            EXPECT_EQ(AIActions::CountStoredResource(lhs, resource),
                      AIActions::CountStoredResource(rhs, resource))
                << "different starting stock for resource " << static_cast<int>(resource);
    };

    // Difficulty must not alter the inventory or manpower before the first
    // simulation command. The AI and human start from the same economy too.
    expectSameStartingState(aiPrimitive, aiHard);
    expectSameStartingState(aiHard, humanHard);
}
