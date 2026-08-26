#include "core/GameWorld.h"
#include "ai/AIActions.h"
#include "economy/Player.h"
#include "economy/BuildingComponents.h"
#include "simulation/MapGenerator.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <map>
#include <string>
#include <vector>

// AI behavior verification harness (user request after the 2026-07-16
// playtest): a repeatable, headless acceptance run that catches "wedged AI"
// regressions the unit tests can't see — like the bridge stall, whose
// signature was a stream of rejected build commands and zero progress at the
// military track. It runs a real world for several sim-minutes, samples
// progress in 30-second windows, counts every accepted/rejected AI command
// (with rejection reasons), and prints the whole behavioral table when any
// expectation fails — so a future stall is diagnosed from the test output
// alone, not by replaying the game.
//
// Budget note: the full 20 sim-minute window on a 301x301 map costs noticeable
// wall time in Debug — deliberate, this is the coarse behavioral
// gate, not a unit test.
namespace
{
    struct WindowSample
    {
        double simSeconds{0.0};
        int buildings{0};
        int roads{0};
        int roster{0};
        int deployed{0};
        int accepted{0};
        int rejected{0};
        // Producer mix (AI economy tuning plan, 2026-07-18): tracks whether
        // the AI actually diversifies its economy instead of tunnel-visioning
        // on one raw-material producer (the "no LumberMill" playtest report).
        int woodcutters{0};
        int lumberMills{0};
        int mines{0};
        int foundries{0};
        int planksRate{0};
        // Weapon/tools chain (2026-07-20, "AI must build toward attack" fix):
        // tracks whether the sword economy actually stands up, not just the
        // raw-material tier.
        int smiths{0};
        int barracksCount{0};
        int woodStored{0};
        int stoneStored{0};
        int planksStored{0};
        int ironStored{0};
        int toolsStored{0};
    };
}

TEST(AIBehaviorHarnessTests, HardAIMakesSteadyProgressAndAttacks)
{
    MapParameters params;
    // Pinned to the pre-2026-07-17 minimum: these tests exercise AI behavior,
    // not map size, and the 401x401 default costs ~1.8x the sim time.
    params.sizeX = 301;
    params.sizeY = 301;
    params.aiOpponentCount = 1;
    params.seed = 20260716;
    params.aiDifficulty = 3;  // Hard: the production head start compresses the timeline

    GameWorld world;
    world.InitWorld("ai-behavior-harness", nullptr, nullptr, params);

    Player* ai = world.GetPlayerHandler().players.at(1).get();
    ASSERT_NE(ai, nullptr);

    int initialBuildings = static_cast<int>(ai->GetTrackedBuildings().size());
    int initialRoads = AIActions::CountOwnedBuildings(ai, BuildingType::Road);
    int initialInstanceCounter = ai->nextUnitInstanceId;

    std::vector<WindowSample> samples;
    std::map<std::string, int> rejectionReasons;
    int accepted = 0;
    int rejected = 0;
    int maxDeployedSeen = 0;
    // Weapon economy end-to-end (2026-07-20): a militia-only roster means the
    // AI never actually built toward attack, even if it technically recruited
    // and deployed something. Checked against BOTH the live roster and units
    // already marching (deployed units leave the roster) so a fast recruit-
    // then-deploy cycle can't hide a real militia-only pattern.
    bool sawNonMilitiaUnit = false;

    constexpr int TicksPerWindow = 3000;  // 30 sim-seconds at the fixed 100 Hz tick
    constexpr int Windows = 40;           // 20 sim-minutes total
    for (int window = 0; window < Windows; window++)
    {
        for (int tick = 0; tick < TicksPerWindow; tick++)
        {
            world.UpdateSimulation(0.01);
            for (const auto& result : world.ConsumeCommandResults())
            {
                if (result.playerId != ai->id)
                    continue;
                if (result.accepted)
                    accepted++;
                else
                {
                    rejected++;
                    rejectionReasons[result.reason.empty() ? "(no reason)" : result.reason]++;
                }
            }
        }

        int deployedNow = 0;
        for (const auto& [instanceId, unit] : world.GetDeployedUnits())
        {
            if (unit.ownerPlayerId != ai->id || unit.state == BattleUnitState::Dying)
                continue;
            deployedNow++;
            if (unit.unitDefId != "militia")
                sawNonMilitiaUnit = true;
        }
        maxDeployedSeen = std::max(maxDeployedSeen, deployedNow);
        for (const auto& [instanceId, unit] : ai->roster.units)
            if (unit.unitDefId != "militia")
                sawNonMilitiaUnit = true;

        WindowSample sample;
        sample.simSeconds = (window + 1) * 30.0;
        sample.buildings = static_cast<int>(ai->GetTrackedBuildings().size()) - initialBuildings;
        sample.roads = AIActions::CountOwnedBuildings(ai, BuildingType::Road) - initialRoads;
        sample.roster = static_cast<int>(ai->roster.units.size());
        sample.deployed = deployedNow;
        sample.accepted = accepted;
        sample.rejected = rejected;
        sample.woodcutters = AIActions::CountOwnedBuildings(ai, BuildingType::Woodcutter);
        sample.lumberMills = AIActions::CountOwnedBuildings(ai, BuildingType::LumberMill);
        sample.mines = AIActions::CountOwnedBuildings(ai, BuildingType::Mine);
        sample.foundries = AIActions::CountOwnedBuildings(ai, BuildingType::Foundry);
        sample.planksRate = AIActions::GetResourceRate(
            ai->economyTelemetry.current.productionRatesPerMinute, ResourceType::PLANKS);
        sample.smiths = AIActions::CountOwnedBuildings(ai, BuildingType::Smith);
        sample.barracksCount = AIActions::CountOwnedBuildings(ai, BuildingType::Barracks);
        sample.woodStored = AIActions::CountStoredResource(ai, ResourceType::WOOD);
        sample.stoneStored = AIActions::CountStoredResource(ai, ResourceType::STONE);
        sample.planksStored = AIActions::CountStoredResource(ai, ResourceType::PLANKS);
        sample.ironStored = AIActions::CountStoredResource(ai, ResourceType::IRON);
        sample.toolsStored = AIActions::CountStoredResource(ai, ResourceType::TOOLS);
        samples.push_back(sample);
    }

    auto report = [&]()
    {
        std::string out = "\nAI behavior report (deltas vs. world init):\n"
                          "  sim_s | +bldg | +road | roster | deployed | cmd_ok | cmd_rej | wcut | lmil | mine | fdry | smith | brk | planks/m | wood | stone | plank | iron | tools\n";
        char line[240];
        for (const auto& s : samples)
        {
            std::snprintf(line, sizeof(line), "  %5.0f | %5d | %5d | %6d | %8d | %6d | %7d | %4d | %4d | %4d | %4d | %5d | %3d | %8d | %4d | %5d | %5d | %4d | %5d\n",
                          s.simSeconds, s.buildings - s.roads, s.roads, s.roster,
                          s.deployed, s.accepted, s.rejected,
                          s.woodcutters, s.lumberMills, s.mines, s.foundries,
                          s.smiths, s.barracksCount, s.planksRate, s.woodStored,
                          s.stoneStored, s.planksStored, s.ironStored, s.toolsStored);
            out += line;
        }
        if (!rejectionReasons.empty())
        {
            out += "  rejection reasons:\n";
            for (const auto& [reason, count] : rejectionReasons)
            {
                std::snprintf(line, sizeof(line), "    %4d x %s\n", count, reason.c_str());
                out += line;
            }
        }
        return out;
    };

    // Wedge detector — the bridge-stall signature: the AI keeps submitting
    // commands the simulation refuses, while accepted work stagnates.
    EXPECT_LE(rejected, accepted * 2 + 20)
        << "the AI spams commands the simulation refuses" << report();

    // Economy progress: it builds, and wires what it builds.
    const WindowSample& last = samples.back();
    EXPECT_GE(last.buildings - last.roads, 4)
        << "the AI placed almost no buildings" << report();
    EXPECT_GE(last.roads, 4)
        << "the AI is not wiring its base into the road network" << report();

    // Military pipeline end-to-end: Barracks -> recruits -> units on the track.
    EXPECT_GE(AIActions::CountOwnedBuildings(ai, BuildingType::Barracks), 1)
        << "no Barracks placed" << report();
    EXPECT_GT(ai->nextUnitInstanceId, initialInstanceCounter)
        << "no unit was ever recruited" << report();
    EXPECT_GE(maxDeployedSeen, 1)
        << "no wave was ever deployed onto the military road" << report();

    // Logistics quality (playtest 2026-07-17 "cuda" report): by the end of
    // the run every COMPLETED producer must have a real road path to a
    // storage — at most one building may still be waiting on roads under
    // construction. Catches both the "orphan stubs everywhere, nothing
    // connected" wedge (the old 8-tile path cap) and future regressions.
    int unconnected = 0;
    std::string unconnectedList;
    for (const auto* building : ai->GetTrackedBuildingsWithComponent<LogisticsComponent>())
    {
        if (building == nullptr || building->owner != ai || building->IsUnderConstruction())
            continue;
        if (IsRoadLike(building->buildingType) || AIActions::IsStorageHub(building))
            continue;
        const auto* logistics = building->GetComponent<LogisticsComponent>();
        if (logistics == nullptr)
            continue;
        if (!logistics->IsConnectedToRoadNetwork(*const_cast<Building*>(building)))
        {
            unconnected++;
            unconnectedList += " " + building->name;
        }
    }
    EXPECT_LE(unconnected, 1)
        << "completed buildings without a road path to any storage:" << unconnectedList << report();

    // AI economy tuning plan (2026-07-18, Task 4): the planks chain must
    // actually stand up, not just spawn an endless forest of Woodcutters
    // (the "no LumberMill" playtest report, Tasks 1-3 above fix the root
    // causes). Check the strict ceiling at 10 minutes, while tunnel vision
    // would be visible. The late-game ceiling is wider because finite forest
    // patches legitimately require replacement over 20 sim-minutes.
    EXPECT_GE(AIActions::CountOwnedBuildings(ai, BuildingType::LumberMill), 1)
        << "no LumberMill - the planks chain never stood up" << report();
    EXPECT_GE(samples.back().woodcutters, 1) << report();
    ASSERT_GE(samples.size(), 20u);
    EXPECT_LE(samples[19].woodcutters, 4)
        << "early Woodcutter tunnel vision is back (deficit ladder not rotating)" << report();
    EXPECT_LE(samples.back().woodcutters, 8)
        << "late Woodcutter replacement became unbounded" << report();

    // Weapon economy end-to-end (2026-07-20, user report: "AI wciąż nie
    // buduje żelaza/węgla/narzędzi broni" — passive militia-only games).
    // RecruitDeploy now builds toward its own top pick's missing cost once
    // the economy has some footing (UtilityAIModel::ExecuteRecruitDeploy,
    // gated on AISituation::economyEstablished) — verified here by a real
    // Smith standing and something other than bare militia in circulation.
    //
    // NOT asserting Foundry specifically (investigated 2026-07-20):
    // producer-type vs. active-recipe selection remains a separate concern;
    // this harness must not depend on a difficulty-specific starting grant.
    EXPECT_GE(samples.back().smiths, 1)
        << "no Smith - the tools/weapon chain never stood up" << report();
    EXPECT_TRUE(sawNonMilitiaUnit)
        << "the AI never recruited or deployed anything beyond bare militia" << report()
        << "\nAI decision trace:\n" << world.GetAITrace(ai->id);
}

TEST(AIBehaviorHarnessTests, PrimitiveDoesNotBrickAfterBarracks)
{
    constexpr int difficulty = 0;
        MapParameters params;
        params.sizeX = 301;
        params.sizeY = 301;
        params.aiOpponentCount = 1;
        params.seed = 20260716;
        params.aiDifficulty = difficulty;

        GameWorld world;
        world.InitWorld("ai-lower-difficulty-harness", nullptr, nullptr, params);
        Player* ai = world.GetPlayerHandler().players.at(1).get();
        ASSERT_NE(ai, nullptr);

        int maxDeployed = 0;
        int accepted = 0;
        int rejected = 0;
        int initialUnitId = ai->nextUnitInstanceId;
        int buildingsAtBarracks = -1;
        int secondsAtBarracks = -1;
        int secondsAtFirstDeploy = -1;
        constexpr int TotalTicks = 120000;
        for (int tick = 0; tick < TotalTicks; tick++)
        {
            world.UpdateSimulation(0.01);
            for (const auto& result : world.ConsumeCommandResults())
            {
                if (result.playerId != ai->id)
                    continue;
                result.accepted ? accepted++ : rejected++;
            }

            int deployed = 0;
            for (const auto& [instanceId, unit] : world.GetDeployedUnits())
                if (unit.ownerPlayerId == ai->id && unit.state != BattleUnitState::Dying)
                    deployed++;
            maxDeployed = std::max(maxDeployed, deployed);
            if (secondsAtFirstDeploy < 0 && deployed > 0)
                secondsAtFirstDeploy = tick / 100;

            if (secondsAtBarracks < 0 &&
                AIActions::CountOwnedBuildings(ai, BuildingType::Barracks) > 0)
            {
                secondsAtBarracks = tick / 100;
                buildingsAtBarracks = static_cast<int>(ai->GetTrackedBuildings().size());
            }
        }

        const int recruited = ai->nextUnitInstanceId - initialUnitId;
        SCOPED_TRACE("difficulty=" + std::to_string(difficulty) +
                     " barracks_s=" + std::to_string(secondsAtBarracks) +
                     " first_deploy_s=" + std::to_string(secondsAtFirstDeploy) +
                     " accepted=" + std::to_string(accepted) +
                     " rejected=" + std::to_string(rejected));
        ASSERT_GE(secondsAtBarracks, 0) << "the military milestone was never reached";
        EXPECT_GT(static_cast<int>(ai->GetTrackedBuildings().size()), buildingsAtBarracks)
            << "AI stopped expanding immediately after placing Barracks";
        EXPECT_GE(recruited, 1) << "Barracks never trained its primitive opening raider\n"
                                << world.GetAITrace(ai->id);
        EXPECT_GE(maxDeployed, 1) << "the opening raider never entered the military route\n"
                                   << world.GetAITrace(ai->id);
        EXPECT_GE(secondsAtFirstDeploy, 0);
        EXPECT_LE(secondsAtFirstDeploy, 600)
            << "Primitive must inherit the former Hard attack pace (first wave within 10 sim-minutes)\n"
            << world.GetAITrace(ai->id);
        EXPECT_LE(rejected, accepted / 2 + 10) << "AI is retrying refused commands instead of recovering";
}

TEST(AIBehaviorHarnessTests, HardAIContinuesLaunchingWaves)
{
    MapParameters params;
    params.sizeX = 301;
    params.sizeY = 301;
    params.aiOpponentCount = 1;
    params.seed = 20260716;
    params.aiDifficulty = 3;

    GameWorld world;
    world.InitWorld("ai-hard-follow-up-wave-harness", nullptr, nullptr, params);
    Player* ai = world.GetPlayerHandler().players.at(1).get();
    ASSERT_NE(ai, nullptr);

    int deployCommands = 0;
    int accepted = 0;
    int rejected = 0;
    constexpr int TotalTicks = 120000; // 20 simulated minutes
    for (int tick = 0; tick < TotalTicks; tick++)
    {
        world.UpdateSimulation(0.01);
        for (const auto& result : world.ConsumeCommandResults())
        {
            if (result.playerId != ai->id)
                continue;
            if (result.accepted)
            {
                accepted++;
                if (result.type == GameCommandType::DeployUnits)
                    deployCommands++;
            }
            else
                rejected++;
        }
    }

    EXPECT_GE(deployCommands, 2)
        << "Hard AI launched only one wave; accepted=" << accepted
        << " rejected=" << rejected << "\nAI decision trace:\n"
        << world.GetAITrace(ai->id);
}

TEST(AIBehaviorHarnessTests, DebugMapPrimitiveDoesNotBrickAcrossSeeds)
{
    constexpr unsigned int Seeds[]{20260716u, 20260717u, 20260718u};
    for (unsigned int seed : Seeds)
    {
        MapParameters params;
        params.sizeX = 101;
        params.sizeY = 101;
        params.aiOpponentCount = 1;
        params.seed = seed;
        params.aiDifficulty = 0;
        params.debugMode = true;
        params.resourceDensity = 0.65f;
        params.resourceFieldSize = 0.45f;
        params.resourceRichness = 120;

        GameWorld world;
        world.InitWorld("ai-debug-map-diagnostic", nullptr, nullptr, params);
        Player* ai = world.GetPlayerHandler().players.at(1).get();
        ASSERT_NE(ai, nullptr);

        int accepted = 0;
        int rejected = 0;
        int lastAcceptedSecond = -1;
        int maxDeployed = 0;
        int deployCommands = 0;
        int maxRouteTile = -1;
        int initialUnitId = ai->nextUnitInstanceId;
        constexpr int TotalSeconds = 1800;
        for (int tick = 0; tick < TotalSeconds * 100; tick++)
        {
            world.UpdateSimulation(0.01);
            for (const auto& result : world.ConsumeCommandResults())
            {
                if (result.playerId != ai->id)
                    continue;
                if (result.accepted)
                {
                    accepted++;
                    lastAcceptedSecond = tick / 100;
                    if (result.type == GameCommandType::DeployUnits)
                        deployCommands++;
                }
                else
                    rejected++;
            }
            int deployed = 0;
            for (const auto& [instanceId, unit] : world.GetDeployedUnits())
                if (unit.ownerPlayerId == ai->id && unit.state != BattleUnitState::Dying)
                {
                    deployed++;
                    maxRouteTile = std::max(maxRouteTile, unit.tileIndex);
                }
            maxDeployed = std::max(maxDeployed, deployed);
        }

        std::printf("[AI-30M] seed=%u deployCommands=%d maxRouteTile=%d hqDamage=%.1f buildings=%zu roads=%d villages=%d wheat=%d windmill=%d bakery=%d well=%d hunter=%d inn=%d smith=%d barracks=%d recruited=%d manpower=%.0f food=%d wood=%d stone=%d planks=%d iron=%d tools=%d stoneRate=%d foodRate=%d\n",
                    seed, deployCommands, maxRouteTile,
                    world.GetCombatTelemetry().GetHqDamage(ai->id, 0),
                    ai->GetTrackedBuildings().size(),
                    AIActions::CountOwnedBuildings(ai, BuildingType::Road),
                    AIActions::CountOwnedBuildings(ai, BuildingType::Village),
                    AIActions::CountOwnedBuildings(ai, BuildingType::WheatFarm),
                    AIActions::CountOwnedBuildings(ai, BuildingType::Windmill),
                    AIActions::CountOwnedBuildings(ai, BuildingType::Bakery),
                    AIActions::CountOwnedBuildings(ai, BuildingType::Well),
                    AIActions::CountOwnedBuildings(ai, BuildingType::HuntersHut),
                    AIActions::CountOwnedBuildings(ai, BuildingType::Inn),
                    AIActions::CountOwnedBuildings(ai, BuildingType::Smith),
                    AIActions::CountOwnedBuildings(ai, BuildingType::Barracks),
                    ai->nextUnitInstanceId - initialUnitId,
                    ai->strategicResources.Get(StrategicResourceType::Manpower),
                    AIActions::CountStoredResource(ai, ResourceType::FOOD_PROVISIONS),
                    AIActions::CountStoredResource(ai, ResourceType::WOOD),
                    AIActions::CountStoredResource(ai, ResourceType::STONE),
                    AIActions::CountStoredResource(ai, ResourceType::PLANKS),
                    AIActions::CountStoredResource(ai, ResourceType::IRON),
                    AIActions::CountStoredResource(ai, ResourceType::TOOLS),
                    AIActions::GetResourceRate(ai->economyTelemetry.current.productionRatesPerMinute, ResourceType::STONE),
                    AIActions::GetResourceRate(ai->economyTelemetry.current.productionRatesPerMinute, ResourceType::FOOD_PROVISIONS));

        EXPECT_GE(AIActions::CountOwnedBuildings(ai, BuildingType::Barracks), 1) << "seed=" << seed;
        EXPECT_GE(ai->nextUnitInstanceId - initialUnitId, 2)
            << "Primitive did not replace its opening raider, seed=" << seed << '\n'
            << world.GetAITrace(ai->id);
        EXPECT_GE(maxDeployed, 1) << "seed=" << seed;
        EXPECT_GE(deployCommands, 2)
            << "Primitive failed to launch a follow-up raid, seed=" << seed << '\n'
            << world.GetAITrace(ai->id);
        EXPECT_GT(maxRouteTile, 0) << "accepted raid never progressed along the military lane, seed=" << seed;
        if (seed == Seeds[0])
            EXPECT_GT(world.GetCombatTelemetry().GetHqDamage(ai->id, 0), 0.0)
                << "pinned offensive wave never reached and damaged the enemy HQ, seed=" << seed;
        if (!world.IsPlayerDefeated(0))
            EXPECT_GE(lastAcceptedSecond, TotalSeconds - 180)
                << "AI had no accepted action for 3 minutes, seed=" << seed << '\n'
                << world.GetAITrace(ai->id);
        EXPECT_LE(rejected, accepted / 2 + 10) << "seed=" << seed;
    }
}
