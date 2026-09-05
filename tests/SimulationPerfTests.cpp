#include "core/GameWorld.h"
#include "core/GameSession.h"
#include "simulation/MapGenerator.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

// Perf regression guard (docs/post_pivot_audit_2026-07-12.md follow-up).
//
// History: after the T1 logistics fix unfroze the AI economy, every AI build
// decision ran FindBuildAnchor over the ENTIRE 301×301 tilemap with a
// per-candidate-tile full-map DistanceToNearestInfrastructure scan — O(map²),
// measured at 7.7 SECONDS per simulation tick (Debug), firing on the ~1.24 s
// AI decision cadence. In-game this froze everything (sim thread holds the
// world lock) for seconds, every few seconds. The transport path had similar
// latent full-map scans (CountIncomingToDestination per shipped unit).
//
// This test runs a realistic worst-case world (default map size, AI opponent,
// debug resources so the economy runs hot) for 15 simulated seconds and fails
// if any single tick blows past a deliberately fat threshold. The fixed code
// measures <50 ms/tick worst case in Debug; the regression this guards
// against measured 7700+ ms — three orders of magnitude of headroom, so slow
// CI machines can't flake it while a reintroduced O(map²) scan can't hide.
TEST(SimulationPerfTests, NoSimulationTickTakesCatastrophicallyLong)
{
    MapParameters params;      // defaults: S 201x201
    params.aiOpponentCount = 1;
    params.debugMode = true;   // grant resources so the AI actually builds

    GameWorld world;
    world.InitWorld("perf-guard", nullptr, params);

    const int ticks = 1500;    // 15 sim-seconds — covers many AI decision cycles
    const double dt = 0.01;
    double worstMs = 0.0;
    int worstTick = -1;

    for (int i = 0; i < ticks; i++)
    {
        auto t0 = std::chrono::steady_clock::now();
        world.UpdateSimulation(dt);
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (ms > worstMs)
        {
            worstMs = ms;
            worstTick = i;
        }
    }

    fprintf(stderr, "[PERF-GUARD] worst tick: #%d, %.1f ms\n", worstTick, worstMs);
    EXPECT_LT(worstMs, 1000.0)
        << "a single simulation tick took " << worstMs << " ms — some code path "
        << "is likely scanning the whole tilemap (or worse) inside the tick; "
        << "see the header comment of this test for the last time this happened";
}

TEST(SimulationPerfTests, MeasuresTwoActiveProvinceCampaignBudget)
{
    MapParameters parameters;
    parameters.sizeX = 101;
    parameters.sizeY = 101;
    parameters.seed = 7701;
    parameters.debugMode = true;

    GameWorld world;
    ASSERT_TRUE(world.InitMultiplayerWorld(
        "two-province-budget", nullptr, parameters, 0, true));
    ASSERT_NE(world.GetTileMapForPlayer(0), world.GetTileMapForPlayer(1));

    constexpr int tickCount = 100;
    double worstTickMs = 0.0;
    for (int tick = 0; tick < tickCount; ++tick)
    {
        const auto started = std::chrono::steady_clock::now();
        world.UpdateSimulation(FixedSimulationClock::FixedDt);
        const auto finished = std::chrono::steady_clock::now();
        worstTickMs = std::max(worstTickMs,
            std::chrono::duration<double, std::milli>(finished - started).count());
    }

    const std::string snapshot = world.BuildSnapshot().Serialize();
    const std::string fullState = world.SerializeSimulationState();
    fprintf(stderr, "[CAMPAIGN-BUDGET] two provinces: worst tick %.1f ms, snapshot %zu B, full state %zu B\n",
            worstTickMs, snapshot.size(), fullState.size());
    EXPECT_LT(worstTickMs, 1000.0);
    EXPECT_LT(fullState.size(), 64u * 1024u * 1024u);
}
