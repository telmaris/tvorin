#include "core/SimulationState.h"
#include "core/GameSession.h"
#include "core/GameWorld.h"

#include <gtest/gtest.h>

#include <limits>

TEST(SimulationStateTests, EncodingIsStableForIdenticalPrimitiveSequence)
{
    CanonicalStateWriter first;
    first.U64(17);
    first.I32(-4);
    first.Bool(true);
    first.FixedDouble3(1.2349);
    first.String("WOOD");

    CanonicalStateWriter second;
    second.U64(17);
    second.I32(-4);
    second.Bool(true);
    second.FixedDouble3(1.2341);
    second.String("WOOD");

    EXPECT_EQ(first.Finish(), second.Finish());
}

TEST(SimulationStateTests, EncodingIncludesStringLengthAndBytes)
{
    CanonicalStateWriter first;
    first.String("AB");
    CanonicalStateWriter second;
    second.String("A");

    EXPECT_NE(first.Finish(), second.Finish());
}

TEST(SimulationStateTests, NonFiniteDoublesAreNormalized)
{
    CanonicalStateWriter nanState;
    nanState.FixedDouble3(std::numeric_limits<double>::quiet_NaN());
    CanonicalStateWriter zeroState;
    zeroState.FixedDouble3(0.0);

    EXPECT_EQ(nanState.Finish(), zeroState.Finish());
}

TEST(SimulationStateTests, FullStateRoundTripRestoresAuthoritativeWorldForClientSlot)
{
    MapParameters params;
    params.sizeX = 101;
    params.sizeY = 101;
    params.seed = 1701;
    params.aiOpponentCount = 1;

    GameWorld host;
    host.InitMultiplayerWorld("network-state", nullptr, params, 0, true);
    for (int i = 0; i < 5; ++i)
        host.UpdateSimulation(FixedSimulationClock::FixedDt);
    const std::string payload = host.SerializeSimulationState();
    ASSERT_FALSE(payload.empty());

    GameWorld client;
    client.InitMultiplayerWorld("different-client-world", nullptr, params, 1, false);
    ASSERT_TRUE(client.RestoreSimulationState(payload, 1));

    EXPECT_EQ(client.GetSimulationTick(), host.GetSimulationTick());
    EXPECT_EQ(client.BuildChecksum(), host.BuildChecksum());
    EXPECT_EQ(client.GetLocalPlayerId(), 1);
    ASSERT_TRUE(client.GetPlayerHandler().players.contains(0));
    ASSERT_TRUE(client.GetPlayerHandler().players.contains(1));
    EXPECT_EQ(client.GetPlayerHandler().players.at(0)->controllerType, PlayerControllerType::Remote);
    EXPECT_EQ(client.GetPlayerHandler().players.at(1)->controllerType, PlayerControllerType::LocalHuman);
    EXPECT_EQ(client.GetPlayerHandler().players.size(), 2u);
}

TEST(SimulationStateTests, ClientSessionAppliesChunkedInitialAndCorrectionState)
{
    MapParameters params;
    params.sizeX = 101;
    params.sizeY = 101;
    params.seed = 8128;
    params.aiOpponentCount = 1;

    GameWorld host;
    host.InitMultiplayerWorld("authoritative", nullptr, params, 0, true);
    for (int i = 0; i < 5; ++i)
        host.UpdateSimulation(FixedSimulationClock::FixedDt);

    GameWorld client;
    client.InitMultiplayerWorld("stale-mirror", nullptr, params, 1, false);
    auto transport = std::make_shared<LocalhostGameTransport>();
    ClientSession session(&client, transport, 1);

    const auto queueState = [&transport](const GameWorld& source)
    {
        const std::string state = source.SerializeSimulationState();
        ASSERT_FALSE(state.empty());
        constexpr std::size_t ChunkSize = 12000;
        const std::size_t chunkCount = (state.size() + ChunkSize - 1) / ChunkSize;
        transport->SendHostSnapshot("INIT_BEGIN " + std::to_string(source.GetSimulationTick()) + " " +
                                    std::to_string(state.size()) + " " + std::to_string(chunkCount));
        for (std::size_t index = 0; index < chunkCount; ++index)
            transport->SendHostSnapshot("INIT_CHUNK " + std::to_string(index) + " " +
                                        state.substr(index * ChunkSize, ChunkSize));
        transport->SendHostSnapshot("INIT_END");
    };

    queueState(host);
    session.Update(0.0);
    ASSERT_TRUE(session.IsReadyForGameplay());
    EXPECT_EQ(client.GetSimulationTick(), host.GetSimulationTick());
    EXPECT_EQ(client.BuildChecksum(), host.BuildChecksum());
    EXPECT_EQ(transport->ReceiveHostCommands(), (std::vector<std::string>{"SYNC_READY"}));

    GameSnapshot initialRenderSnapshot;
    ASSERT_TRUE(session.ConsumeLatestSnapshot(initialRenderSnapshot));
    for (int i = 0; i < 2; ++i)
        host.UpdateSimulation(FixedSimulationClock::FixedDt);
    GameServerFrame ordinaryFrame;
    ordinaryFrame.tick = host.GetSimulationTick();
    ordinaryFrame.hasChecksum = true;
    ordinaryFrame.checksum = host.BuildChecksum();
    transport->SendHostFrame(ordinaryFrame.Serialize());
    session.Update(0.0);
    GameSnapshot refreshedRenderSnapshot;
    ASSERT_TRUE(session.ConsumeLatestSnapshot(refreshedRenderSnapshot));
    EXPECT_EQ(refreshedRenderSnapshot.simulationTick, host.GetSimulationTick());

    for (int i = 0; i < 3; ++i)
        host.UpdateSimulation(FixedSimulationClock::FixedDt);
    queueState(host);
    GameServerFrame correctionFrame;
    correctionFrame.tick = host.GetSimulationTick();
    correctionFrame.hasChecksum = true;
    correctionFrame.checksum = host.BuildChecksum();
    transport->SendHostFrame(correctionFrame.Serialize());
    session.Update(0.0);
    EXPECT_TRUE(session.IsReadyForGameplay());
    EXPECT_EQ(client.GetSimulationTick(), host.GetSimulationTick());
    EXPECT_EQ(client.BuildChecksum(), host.BuildChecksum());
    // The frame follows the snapshot on the host wire. If ClientSession were
    // to drain frames first it would compare against the stale mirror and
    // enqueue RESYNC_REQUEST here.
    EXPECT_EQ(transport->ReceiveHostCommands(), (std::vector<std::string>{"SYNC_READY"}));
}

TEST(SimulationStateTests, ClientSessionRejectsUnsafeOrConflictingSnapshotChunks)
{
    auto transport = std::make_shared<LocalhostGameTransport>();
    ClientSession session(nullptr, transport, 1);

    transport->SendHostSnapshot("INIT_BEGIN 0 536870913 1");
    session.Update(0.0);
    EXPECT_FALSE(session.IsReadyForGameplay());
    EXPECT_NE(session.GetConnectionStatus().find("invalid manifest"), std::string::npos);

    transport->SendHostSnapshot("INIT_BEGIN 0 3 1");
    transport->SendHostSnapshot("INIT_CHUNK 0 abc");
    transport->SendHostSnapshot("INIT_CHUNK 0 abd");
    transport->SendHostSnapshot("INIT_END");
    session.Update(0.0);
    EXPECT_FALSE(session.IsReadyForGameplay());
    EXPECT_NE(session.GetConnectionStatus().find("conflicting duplicate"), std::string::npos);
}
