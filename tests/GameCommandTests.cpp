#include "core/GameCommand.h"
#include "core/GameSession.h"
#include "core/GameWorld.h"
#include "multiplayer/FaultInjectingGameTransport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

TEST(GameCommandTests, SerializesAndDeserializesBuildCommand)
{
    GameCommand original = GameCommand::BuildBuilding(2, BuildingType::LumberMill, {12, 34});
    original.commandId = 42;
    original.targetTick = 7;

    GameCommand parsed;
    ASSERT_TRUE(GameCommand::TryDeserialize(original.Serialize(), parsed));

    EXPECT_EQ(parsed.commandId, 42u);
    EXPECT_EQ(parsed.targetTick, 7u);
    EXPECT_EQ(parsed.playerId, 2);
    EXPECT_EQ(parsed.type, GameCommandType::BuildBuilding);
    EXPECT_EQ(parsed.buildingType, BuildingType::LumberMill);
    EXPECT_EQ(parsed.tilePos.x, 12);
    EXPECT_EQ(parsed.tilePos.y, 34);
}

TEST(GameCommandTests, SerializesAndDeserializesFocusAndResearchIds)
{
    GameCommand focus = GameCommand::StartFocus(3, "tribal_council");
    GameCommand research = GameCommand::StartTechnologyResearch(3, "sawmill blades", 99);

    GameCommand parsedFocus;
    GameCommand parsedResearch;
    ASSERT_TRUE(GameCommand::TryDeserialize(focus.Serialize(), parsedFocus));
    ASSERT_TRUE(GameCommand::TryDeserialize(research.Serialize(), parsedResearch));

    EXPECT_EQ(parsedFocus.type, GameCommandType::StartFocus);
    EXPECT_EQ(parsedFocus.researchId, "tribal_council");

    EXPECT_EQ(parsedResearch.type, GameCommandType::StartTechnologyResearch);
    EXPECT_EQ(parsedResearch.researchId, "sawmill blades");
    EXPECT_EQ(parsedResearch.sourceTileId, 99);
}

TEST(GameCommandTests, SerializesAndDeserializesTowerTargetMode)
{
    GameCommand original = GameCommand::SetTowerTargetMode(
        2, 123, static_cast<int>(TowerTargetMode::StrongestUnit));

    GameCommand parsed;
    ASSERT_TRUE(GameCommand::TryDeserialize(original.Serialize(), parsed));

    EXPECT_EQ(parsed.type, GameCommandType::SetTowerTargetMode);
    EXPECT_EQ(parsed.playerId, 2);
    EXPECT_EQ(parsed.sourceTileId, 123);
    EXPECT_EQ(parsed.targetTileId, static_cast<int>(TowerTargetMode::StrongestUnit));
}

TEST(GameCommandTests, SerializesAndDeserializesProductionBlock)
{
    for (bool blocked : {false, true})
    {
        GameCommand original = GameCommand::SetProductionBlocked(2, 123, blocked);
        GameCommand parsed;
        ASSERT_TRUE(GameCommand::TryDeserialize(original.Serialize(), parsed));

        EXPECT_EQ(parsed.type, GameCommandType::SetProductionBlocked);
        EXPECT_EQ(parsed.playerId, 2);
        EXPECT_EQ(parsed.sourceTileId, 123);
        EXPECT_EQ(parsed.targetTileId, blocked ? 1 : 0);
    }
}

TEST(GameCommandTests, SerializesAndDeserializesRoadPriority)
{
    for (ResourceType resource : {ResourceType::WOOD, ResourceType::IRON_SWORD, ResourceType::Null})
    {
        GameCommand original = GameCommand::SetRoadPriority(2, 123, resource);
        GameCommand parsed;
        ASSERT_TRUE(GameCommand::TryDeserialize(original.Serialize(), parsed));
        EXPECT_EQ(parsed.type, GameCommandType::SetRoadPriority);
        EXPECT_EQ(parsed.playerId, 2);
        EXPECT_EQ(parsed.sourceTileId, 123);
        EXPECT_EQ(parsed.targetTileId, static_cast<int>(resource));
    }
}

TEST(GameCommandTests, RejectsUnknownCommandType)
{
    GameCommand parsed;
    GameCommand malformed = GameCommand::SetRoadPriority(0, 1, ResourceType::WOOD);
    malformed.type = static_cast<GameCommandType>(999);
    EXPECT_FALSE(GameCommand::TryDeserialize(malformed.Serialize(), parsed));
}

TEST(GameCommandTests, RoadPriorityRequiresFinishedOwnedRoadAndPhysicalResource)
{
    GameWorld world;
    MapParameters params;
    params.sizePreset = MapSizePreset::S;
    params.aiOpponentCount = 1;
    params.seed = 4242;
    ASSERT_TRUE(world.InitWorld("road-priority", nullptr, nullptr, params));

    Player* player = world.GetPlayerHandler().players.at(0).get();
    ASSERT_NE(player, nullptr);
    Road* road = nullptr;
    for (Building* building : player->GetTrackedBuildings())
        if (auto* candidate = dynamic_cast<Road*>(building); candidate != nullptr)
        {
            road = candidate;
            break;
        }
    ASSERT_NE(road, nullptr);

    const auto commandId = world.SubmitCommand(
        GameCommand::SetRoadPriority(player->id, road->positionId, ResourceType::WOOD));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);
    auto results = world.ConsumeCommandResults();
    auto resultIt = std::find_if(results.begin(), results.end(), [commandId](const GameCommandResult& result)
    {
        return result.commandId == commandId;
    });
    ASSERT_NE(resultIt, results.end());
    EXPECT_TRUE(resultIt->accepted);
    EXPECT_EQ(road->road.priorityResource, ResourceType::WOOD);

    const auto rejectedId = world.SubmitCommand(
        GameCommand::SetRoadPriority(player->id, road->positionId, static_cast<ResourceType>(23)));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);
    results = world.ConsumeCommandResults();
    resultIt = std::find_if(results.begin(), results.end(), [rejectedId](const GameCommandResult& result)
    {
        return result.commandId == rejectedId;
    });
    ASSERT_NE(resultIt, results.end());
    EXPECT_FALSE(resultIt->accepted);
    EXPECT_EQ(road->road.priorityResource, ResourceType::WOOD);
}

TEST(GameCommandTests, SerializesDebugEnemyDeployment)
{
    GameCommand original = GameCommand::DebugDeployEnemyUnits(2, 4);
    GameCommand parsed;
    ASSERT_TRUE(GameCommand::TryDeserialize(original.Serialize(), parsed));
    EXPECT_EQ(parsed.type, GameCommandType::DebugDeployEnemyUnits);
    EXPECT_EQ(parsed.playerId, 2);
    EXPECT_EQ(parsed.targetTileId, 4);
}

TEST(GameCommandTests, DebugEnemyDeploymentRequiresDebugModeAndSpawnsEnemyColumn)
{
    MapParameters params;
    params.sizeX = 81;
    params.sizeY = 81;
    params.aiOpponentCount = 1;
    params.seed = 8128;
    params.debugMode = true;

    GameWorld world;
    world.InitWorld("test", nullptr, nullptr, params);
    ASSERT_TRUE(world.GetMilitaryRoads().AreConnected(0, 1));
    const std::vector<int> route = world.GetMilitaryRoads().GetDirectedTiles(1, 0);
    ASSERT_FALSE(route.empty());
    const int expectedHeadIndex = std::min(
        std::max(0, static_cast<int>(route.size()) - 2),
        (static_cast<int>(route.size()) - 1) * 3 / 4);

    const std::uint64_t commandId = world.SubmitCommand(GameCommand::DebugDeployEnemyUnits(0, 4));
    world.UpdateSimulation(FixedSimulationClock::FixedDt);

    const auto results = world.ConsumeCommandResults();
    const auto resultIt = std::find_if(results.begin(), results.end(), [commandId](const GameCommandResult& result)
    {
        return result.commandId == commandId;
    });
    ASSERT_NE(resultIt, results.end());
    EXPECT_TRUE(resultIt->accepted);

    int spawnedEnemyUnits = 0;
    std::vector<int> spawnedTileIndices;
    for (const auto& [instanceId, unit] : world.GetDeployedUnits())
    {
        (void)instanceId;
        if (unit.ownerPlayerId == 1 && unit.routeFromPlayerId == 1 && unit.routeToPlayerId == 0)
        {
            ++spawnedEnemyUnits;
            spawnedTileIndices.push_back(unit.tileIndex);
            EXPECT_EQ(unit.unitDefId, "militia");
        }
    }
    EXPECT_EQ(spawnedEnemyUnits, 4);
    std::sort(spawnedTileIndices.begin(), spawnedTileIndices.end(), std::greater<int>());
    ASSERT_EQ(spawnedTileIndices.size(), 4u);
    for (size_t i = 0; i < spawnedTileIndices.size(); ++i)
        EXPECT_EQ(spawnedTileIndices[i], std::max(0, expectedHeadIndex - static_cast<int>(i)));
}

TEST(GameCommandTests, RejectsMalformedPayload)
{
    GameCommand parsed;
    EXPECT_FALSE(GameCommand::TryDeserialize("1 999 0", parsed));
    EXPECT_FALSE(GameCommand::TryDeserialize("0 0 1 2 3 4 5 6 1 0 0 -1 \"bad\"", parsed));
    EXPECT_FALSE(GameCommand::TryDeserialize("not a command", parsed));
}

TEST(GameCommandTests, RejectsPreviousWireVersionAndTrailingFields)
{
    Archive old(14);
    old << std::uint64_t{1} << std::uint64_t{1}
        << static_cast<int>(GameCommandType::BuildBuilding) << 0
        << static_cast<int>(BuildingType::LumberMill) << 2 << 3
        << -1 << -1 << 0 << 0 << std::string{} << 0;

    GameCommand parsed;
    EXPECT_FALSE(GameCommand::TryDeserialize(old.GetString(), parsed));
    EXPECT_FALSE(GameCommand::TryDeserialize(GameCommand::DestroyBuilding(0, 4).Serialize() + " 123", parsed));
}

TEST(GameCommandTests, SerializesAndDeserializesCommandResult)
{
    GameCommandResult original;
    original.commandId = 42;
    original.simulationTick = 9;
    original.targetTick = 8;
    original.playerId = 2;
    original.type = GameCommandType::BuildBuilding;
    original.accepted = true;
    original.reason = "accepted";

    GameCommandResult parsed;
    ASSERT_TRUE(GameCommandResult::TryDeserialize(original.Serialize(), parsed));

    EXPECT_EQ(parsed.commandId, 42u);
    EXPECT_EQ(parsed.simulationTick, 9u);
    EXPECT_EQ(parsed.targetTick, 8u);
    EXPECT_EQ(parsed.playerId, 2);
    EXPECT_EQ(parsed.type, GameCommandType::BuildBuilding);
    EXPECT_TRUE(parsed.accepted);
    EXPECT_EQ(parsed.reason, "accepted");
}

TEST(GameCommandTests, GameWorldPublishesCommandResultAfterSimulationTick)
{
    GameWorld world;
    std::uint64_t commandId = world.SubmitCommand(GameCommand::DestroyBuilding(999, 123));

    world.UpdateSimulation(0.016);
    auto results = world.ConsumeCommandResults();

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results.front().commandId, commandId);
    EXPECT_EQ(results.front().simulationTick, 1u);
    EXPECT_EQ(results.front().targetTick, 1u);
    EXPECT_EQ(results.front().playerId, 999);
    EXPECT_EQ(results.front().type, GameCommandType::DestroyBuilding);
    EXPECT_FALSE(results.front().accepted);
}

// DEPRECATED: LocalhostMultiplayerSession test removed in Etap 1.2
// LocalhostMultiplayerSession merged into HostSession + ClientSession architecture
// Full integration test will be added in Etap 1.4 after IGameRuntimeLoop refactor

TEST(GameCommandTests, ThreadedSessionAdvancesSimulationAndPublishesCommandResult)
{
    GameWorld world;
    HostSession session(world);

    std::uint64_t commandId = session.SubmitCommand(GameCommand::DestroyBuilding(world.GetLocalPlayerId(), 123));
    std::vector<GameCommandResult> results;
    for (int i = 0; i < 30 && results.empty(); i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        results = session.ConsumeCommandResults();
    }

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results.front().commandId, commandId);
    EXPECT_EQ(results.front().playerId, world.GetLocalPlayerId());
    EXPECT_EQ(results.front().type, GameCommandType::DestroyBuilding);
    EXPECT_FALSE(results.front().accepted);
    EXPECT_EQ(session.GetWorld(), &world);
    EXPECT_NE(session.GetWorldMutex(), nullptr);
}

TEST(GameCommandTests, HostRejectsTransportCommandForWrongPlayerSlot)
{
    GameWorld world;
    auto transport = std::make_shared<LocalhostGameTransport>();
    HostSession host(world, transport, 1);

    GameCommand command = GameCommand::DestroyBuilding(0, 123);
    command.commandId = 77;
    transport->SendClientCommand(command.Serialize());

    std::vector<GameCommandResult> results;
    for (int i = 0; i < 30 && results.empty(); i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        results = host.ConsumeCommandResults();
    }

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results.front().commandId, command.commandId);
    EXPECT_EQ(results.front().playerId, 0);
    EXPECT_FALSE(results.front().accepted);
    EXPECT_EQ(results.front().reason, "rejected: wrong player slot");
    EXPECT_LT(results.front().targetTick, 999999u);
}

TEST(GameCommandTests, HostReplaysCachedResultForDuplicateRemoteCommandWithoutSecondExecution)
{
    GameWorld world;
    auto transport = std::make_shared<LocalhostGameTransport>();
    HostSession host(world, transport, 1);

    GameCommand command = GameCommand::DestroyBuilding(0, 123);
    command.commandId = 901;
    transport->SendClientCommand(command.Serialize());

    std::vector<GameCommandResult> firstResults;
    for (int i = 0; i < 30 && firstResults.empty(); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        firstResults = host.ConsumeCommandResults();
    }
    ASSERT_EQ(firstResults.size(), 1u);

    std::vector<std::string> firstReplies;
    for (int i = 0; i < 30 && firstReplies.empty(); ++i)
    {
        firstReplies = transport->ReceiveClientResults();
        if (firstReplies.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_EQ(firstReplies.size(), 1u);

    transport->SendClientCommand(command.Serialize());
    std::vector<std::string> replayReplies;
    for (int i = 0; i < 30 && replayReplies.empty(); ++i)
    {
        replayReplies = transport->ReceiveClientResults();
        if (replayReplies.empty())
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_EQ(replayReplies.size(), 1u);
    EXPECT_EQ(replayReplies.front(), firstReplies.front());
    EXPECT_TRUE(host.ConsumeCommandResults().empty());
}

TEST(GameCommandTests, ClientSessionSuppressesDuplicateAuthoritativeResult)
{
    auto transport = std::make_shared<LocalhostGameTransport>();
    ClientSession client(nullptr, transport, 1);

    GameCommandResult result;
    result.commandId = 77;
    result.simulationTick = 12;
    result.targetTick = 12;
    result.playerId = 1;
    result.type = GameCommandType::DestroyBuilding;
    result.accepted = false;
    result.reason = "rejected";
    transport->SendHostResult(result.Serialize());
    transport->SendHostResult(result.Serialize());

    client.Update(0.0);
    const auto received = client.ConsumeCommandResults();
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received.front().commandId, result.commandId);
}

TEST(GameCommandTests, ClientSessionReplaysOnlyUnacknowledgedCommandsAfterReconnect)
{
    auto transport = std::make_shared<FaultInjectingGameTransport>();
    ClientSession client(nullptr, transport, 1);

    transport->SetConnected(false);
    client.Update(0.0);
    const std::uint64_t commandId = client.SubmitCommand(GameCommand::DestroyBuilding(0, 123));
    EXPECT_TRUE(transport->ReceiveHostCommands().empty());

    transport->SetConnected(true);
    client.Update(0.0);
    const auto replayed = transport->ReceiveHostCommands();
    ASSERT_EQ(replayed.size(), 1u);
    GameCommand replayedCommand;
    ASSERT_TRUE(GameCommand::TryDeserialize(replayed.front(), replayedCommand));
    EXPECT_EQ(replayedCommand.commandId, commandId);
    EXPECT_EQ(replayedCommand.playerId, 1);

    GameCommandResult acknowledgement;
    acknowledgement.commandId = commandId;
    acknowledgement.simulationTick = 10;
    acknowledgement.targetTick = 10;
    acknowledgement.playerId = 1;
    acknowledgement.type = GameCommandType::DestroyBuilding;
    acknowledgement.accepted = false;
    acknowledgement.reason = "rejected";
    transport->SendHostResult(acknowledgement.Serialize());
    client.Update(0.0);

    transport->SetConnected(false);
    client.Update(0.0);
    transport->SetConnected(true);
    client.Update(0.0);
    EXPECT_TRUE(transport->ReceiveHostCommands().empty());
}

TEST(GameCommandTests, RejectsCommandWithTooManyUnitIdsBeforeReservingMemory)
{
    GameCommand command = GameCommand::DeployUnits(1, 2, {});
    std::string payload = command.Serialize();
    ASSERT_EQ(payload.back(), '0');
    payload.back() = '1';
    payload += "025";

    GameCommand parsed;
    EXPECT_FALSE(GameCommand::TryDeserialize(payload, parsed));
}

TEST(GameCommandTests, RejectsServerFrameWithTooManyResultsBeforeReservingMemory)
{
    GameServerFrame frame;
    GameServerFrame parsed;
    EXPECT_FALSE(GameServerFrame::TryDeserialize("1 0 0 0 257", parsed));
}

TEST(GameCommandTests, LocalhostTransportPreservesLargeFrameBacklog)
{
    LocalhostGameTransport transport;
    constexpr int frameCount = 600;
    for (int i = 0; i < frameCount; ++i)
    {
        GameServerFrame frame;
        frame.tick = static_cast<std::uint64_t>(i + 1);
        GameCommandResult result;
        result.commandId = static_cast<std::uint64_t>(i + 1);
        result.simulationTick = frame.tick;
        result.playerId = 0;
        result.type = GameCommandType::DestroyBuilding;
        result.accepted = false;
        frame.results.push_back(std::move(result));
        transport.SendHostFrame(frame.Serialize());
    }

    const auto frames = transport.ReceiveClientFrames();
    ASSERT_EQ(frames.size(), static_cast<std::size_t>(frameCount));
    for (int i = 0; i < frameCount; ++i)
    {
        GameServerFrame frame;
        ASSERT_TRUE(GameServerFrame::TryDeserialize(frames[static_cast<std::size_t>(i)], frame));
        ASSERT_EQ(frame.results.size(), 1u);
        EXPECT_EQ(frame.results.front().commandId, static_cast<std::uint64_t>(i + 1));
    }
}

TEST(GameCommandTests, LocalhostTransportSupportsConcurrentSendAndDrain)
{
    LocalhostGameTransport transport;
    constexpr int payloadCount = 1000;
    std::atomic<bool> producerDone{false};
    std::vector<std::string> received;
    received.reserve(payloadCount);

    std::thread producer([&]()
    {
        for (int i = 0; i < payloadCount; ++i)
            transport.SendClientCommand(std::to_string(i));
        producerDone = true;
    });

    while (!producerDone || received.size() < static_cast<std::size_t>(payloadCount))
    {
        auto batch = transport.ReceiveHostCommands();
        received.insert(received.end(), batch.begin(), batch.end());
        if (!producerDone)
            std::this_thread::yield();
    }
    producer.join();

    ASSERT_EQ(received.size(), static_cast<std::size_t>(payloadCount));
    for (int i = 0; i < payloadCount; ++i)
        EXPECT_EQ(received[static_cast<std::size_t>(i)], std::to_string(i));
}

TEST(GameCommandTests, MultiplayerWorldAssignsStableServerSlotsAndColors)
{
    MapParameters params;
    params.aiOpponentCount = 1;
    params.seed = 27015;

    GameWorld hostWorld;
    GameWorld clientWorld;
    hostWorld.InitMultiplayerWorld("test", nullptr, nullptr, params, 0, true);
    clientWorld.InitMultiplayerWorld("test", nullptr, nullptr, params, 1, false);

    ASSERT_EQ(hostWorld.GetLocalPlayerId(), 0);
    ASSERT_EQ(clientWorld.GetLocalPlayerId(), 1);
    ASSERT_TRUE(hostWorld.GetPlayerHandlerForTesting().players.contains(0));
    ASSERT_TRUE(hostWorld.GetPlayerHandlerForTesting().players.contains(1));
    ASSERT_TRUE(clientWorld.GetPlayerHandlerForTesting().players.contains(0));
    ASSERT_TRUE(clientWorld.GetPlayerHandlerForTesting().players.contains(1));

    Color hostSlot0 = hostWorld.GetPlayerHandlerForTesting().players[0]->color;
    Color clientSlot0 = clientWorld.GetPlayerHandlerForTesting().players[0]->color;
    Color hostSlot1 = hostWorld.GetPlayerHandlerForTesting().players[1]->color;
    Color clientSlot1 = clientWorld.GetPlayerHandlerForTesting().players[1]->color;

    EXPECT_EQ(hostSlot0.r, clientSlot0.r);
    EXPECT_EQ(hostSlot0.g, clientSlot0.g);
    EXPECT_EQ(hostSlot0.b, clientSlot0.b);
    EXPECT_EQ(hostSlot1.r, clientSlot1.r);
    EXPECT_EQ(hostSlot1.g, clientSlot1.g);
    EXPECT_EQ(hostSlot1.b, clientSlot1.b);
    EXPECT_EQ(hostWorld.GetPlayerHandlerForTesting().players[1]->controllerType, PlayerControllerType::Remote);
    EXPECT_EQ(clientWorld.GetPlayerHandlerForTesting().players[1]->controllerType, PlayerControllerType::LocalHuman);
    EXPECT_EQ(clientWorld.GetPlayerHandlerForTesting().players[2]->controllerType, PlayerControllerType::Remote);
}
