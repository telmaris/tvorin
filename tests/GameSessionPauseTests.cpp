#include "core/GameSession.h"
#include "core/GameWorld.h"
#include "multiplayer/FaultInjectingGameTransport.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

TEST(GameSessionPauseTests, HostStopsTicksUntilResumed)
{
    GameWorld world;
    HostSession session(world);
    auto readTick = [&]()
    {
        std::lock_guard<std::recursive_mutex> lock(*session.GetWorldMutex());
        return world.GetSimulationTick();
    };

    session.SetPaused(true);
    ASSERT_TRUE(session.IsPaused());
    const std::uint64_t pausedTick = readTick();

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    EXPECT_EQ(readTick(), pausedTick);

    session.SetPaused(false);
    ASSERT_FALSE(session.IsPaused());
    for (int attempt = 0; attempt < 25 && readTick() == pausedTick; ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    EXPECT_GT(readTick(), pausedTick);
}

TEST(GameSessionPauseTests, OnlySinglePlayerHostPausesWhenGameplaySceneIsInactive)
{
    GameWorld singlePlayerWorld;
    HostSession singlePlayerSession(singlePlayerWorld);
    EXPECT_TRUE(singlePlayerSession.ShouldPauseWhenSceneInactive());

    GameWorld multiplayerWorld;
    auto transport = std::make_shared<LocalhostGameTransport>();
    HostSession multiplayerSession(multiplayerWorld, transport);
    EXPECT_FALSE(multiplayerSession.ShouldPauseWhenSceneInactive());
}

TEST(GameSessionPauseTests, GeneratedHostWorldUsesSessionWorkerAndWaitsForGameplayActivation)
{
    MapParameters params;
    params.sizePreset = MapSizePreset::S;
    params.sizeX = MapGenerator::SizeFromPreset(MapSizePreset::S);
    params.sizeY = params.sizeX;
    params.seed = 24680;
    params.aiOpponentCount = 0;

    HostSession session(HostSessionStartRequest{
        "threaded-startup", params, HostWorldMode::SinglePlayer});

    for (int attempt = 0; attempt < 600 && !session.IsReadyForGameplay(); ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    ASSERT_TRUE(session.IsReadyForGameplay())
        << session.GetStartupStatus().error;
    ASSERT_NE(session.GetWorld(), nullptr);
    ASSERT_TRUE(session.GetWorld()->IsInitialized());

    const auto readTick = [&]()
    {
        std::lock_guard<std::recursive_mutex> lock(*session.GetWorldMutex());
        return session.GetWorld()->GetSimulationTick();
    };
    const std::uint64_t gatedTick = readTick();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(readTick(), gatedTick);

    session.ActivateGameplay();
    for (int attempt = 0;
         attempt < 30 && readTick() == gatedTick;
         ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_GT(readTick(), gatedTick);
}

TEST(GameSessionPauseTests, HostAndBackgroundClientSynchronizeBeforeGameplay)
{
    MapParameters params;
    params.sizePreset = MapSizePreset::S;
    params.sizeX = MapGenerator::SizeFromPreset(MapSizePreset::S);
    params.sizeY = params.sizeX;
    params.seed = 13579;
    params.aiOpponentCount = 0;

    auto transport = std::make_shared<LocalhostGameTransport>();
    HostSession host(
        HostSessionStartRequest{
            "threaded-multiplayer", params, HostWorldMode::MultiplayerHost},
        transport, 1, true);
    ClientSession client(std::make_unique<GameWorld>(), transport, 1);

    for (int attempt = 0;
         attempt < 900 &&
         (!host.IsReadyForGameplay() || !client.IsReadyForGameplay());
         ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    ASSERT_TRUE(client.IsReadyForGameplay())
        << client.GetStartupStatus().error;
    ASSERT_TRUE(host.IsReadyForGameplay())
        << host.GetStartupStatus().error;
    ASSERT_NE(client.GetWorld(), nullptr);
    EXPECT_TRUE(client.GetWorld()->IsInitialized());
    EXPECT_EQ(client.GetWorld()->GetLocalPlayerId(), 1);

    const auto readHostTick = [&]()
    {
        std::lock_guard<std::recursive_mutex> lock(*host.GetWorldMutex());
        return host.GetWorld()->GetSimulationTick();
    };
    const std::uint64_t hostTick = readHostTick();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    EXPECT_EQ(readHostTick(), hostTick);

    host.ActivateGameplay();
    client.ActivateGameplay();
    for (int attempt = 0;
         attempt < 30 && readHostTick() == hostTick;
         ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_GT(readHostTick(), hostTick);
}

TEST(GameSessionPauseTests, BackgroundClientReportsTransportFailureAndJoinsCleanly)
{
    auto transport = std::make_shared<FaultInjectingGameTransport>();
    {
        ClientSession client(std::make_unique<GameWorld>(), transport, 1);
        transport->Fail("simulated startup failure");

        for (int attempt = 0;
             attempt < 100 &&
             client.GetStartupStatus().phase != GameSessionStartupPhase::Failed;
             ++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        const GameSessionStartupStatus status = client.GetStartupStatus();
        EXPECT_EQ(status.phase, GameSessionStartupPhase::Failed);
        EXPECT_NE(status.error.find("simulated startup failure"),
                  std::string::npos);
    }
    SUCCEED();
}

TEST(GameSessionPauseTests, BackgroundClientRequestsFreshSnapshotAfterCorruptManifest)
{
    auto transport = std::make_shared<LocalhostGameTransport>();
    ClientSession client(std::make_unique<GameWorld>(), transport, 1);
    transport->SendHostSnapshot("INIT_BEGIN 0 536870913 1");

    bool requestedResync = false;
    for (int attempt = 0; attempt < 100 && !requestedResync; ++attempt)
    {
        for (const std::string& command : transport->ReceiveHostCommands())
            requestedResync = requestedResync || command == "RESYNC_REQUEST";
        if (!requestedResync)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_TRUE(requestedResync);
    EXPECT_EQ(client.GetStartupStatus().phase,
              GameSessionStartupPhase::Recovering);
}
