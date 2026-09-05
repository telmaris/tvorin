#include "core/GameSession.h"
#include "core/Log.h"

#include <array>
#include <exception>
#include <stdexcept>

// ============================================================================
// LocalhostGameTransport Implementation
// ============================================================================

std::vector<std::string> LocalhostGameTransport::Drain(std::deque<std::string>& queue)
{
    std::vector<std::string> result;
    while (!queue.empty())
    {
        result.push_back(std::move(queue.front()));
        queue.pop_front();
    }
    return result;
}

void LocalhostGameTransport::SendClientCommand(const std::string& payload)
{
    std::lock_guard<std::mutex> lock(mutex);
    clientToHost.push_back(payload);
}

std::vector<std::string> LocalhostGameTransport::ReceiveHostCommands()
{
    std::lock_guard<std::mutex> lock(mutex);
    return Drain(clientToHost);
}

void LocalhostGameTransport::SendHostResult(const std::string& payload)
{
    std::lock_guard<std::mutex> lock(mutex);
    hostToClient.push_back(payload);
}

std::vector<std::string> LocalhostGameTransport::ReceiveClientResults()
{
    std::lock_guard<std::mutex> lock(mutex);
    return Drain(hostToClient);
}

void LocalhostGameTransport::SendHostFrame(const std::string& payload)
{
    std::lock_guard<std::mutex> lock(mutex);
    hostFrames.push_back(payload);
}

std::vector<std::string> LocalhostGameTransport::ReceiveClientFrames()
{
    std::lock_guard<std::mutex> lock(mutex);
    return Drain(hostFrames);
}

void LocalhostGameTransport::SendHostSnapshot(const std::string& payload)
{
    std::lock_guard<std::mutex> lock(mutex);
    hostSnapshots.push_back(payload);
}

std::vector<std::string> LocalhostGameTransport::ReceiveClientSnapshots()
{
    std::lock_guard<std::mutex> lock(mutex);
    return Drain(hostSnapshots);
}

// ============================================================================
// HostSession Implementation - Scalona klasa dla SP i MP
// ============================================================================

// Single-player constructor
HostSession::HostSession(GameWorld& world)
    : world(&world), transport(nullptr), requireRemoteSync(false)
{
    gameplayActivated = true;
    PublishStartupStatus(GameSessionStartupPhase::Ready, 1.0f, "Ready");
    StartWorker();
}

// Multiplayer constructor
HostSession::HostSession(GameWorld& world, std::shared_ptr<IGameTransport> transport, int remotePlayerId, bool requireRemoteSync)
    : world(&world), transport(std::move(transport)), remotePlayerId(remotePlayerId), requireRemoteSync(requireRemoteSync)
{
    gameplayActivated = true;
    remoteSyncStartedAt = std::chrono::steady_clock::now();
    PublishStartupStatus(
        this->transport != nullptr && this->requireRemoteSync
            ? GameSessionStartupPhase::WaitingForPeer
            : GameSessionStartupPhase::Ready,
        this->transport != nullptr && this->requireRemoteSync ? 0.96f : 1.0f,
        this->transport != nullptr && this->requireRemoteSync
            ? "Preparing map sync"
            : "Ready");
    StartWorker();
}

HostSession::HostSession(std::unique_ptr<GameWorld> loadedWorld)
    : ownedWorld(std::move(loadedWorld)), world(ownedWorld.get()),
      transport(nullptr), requireRemoteSync(false)
{
    PublishStartupStatus(
        world != nullptr && world->IsInitialized()
            ? GameSessionStartupPhase::Ready
            : GameSessionStartupPhase::Failed,
        world != nullptr && world->IsInitialized() ? 1.0f : 0.0f,
        world != nullptr && world->IsInitialized() ? "Ready" : "Loaded world is invalid",
        world != nullptr && world->IsInitialized() ? std::string{} : "Loaded world is invalid");
    StartWorker();
}

HostSession::HostSession(HostSessionStartRequest request)
    : startRequest(std::move(request)), transport(nullptr), requireRemoteSync(false)
{
    PublishStartupStatus(GameSessionStartupPhase::Starting, 0.01f,
                         "Starting world generator");
    StartWorker();
}

HostSession::HostSession(HostSessionStartRequest request,
                         std::shared_ptr<IGameTransport> transport,
                         int remotePlayerId,
                         bool requireRemoteSync)
    : startRequest(std::move(request)), transport(std::move(transport)),
      remotePlayerId(remotePlayerId), requireRemoteSync(requireRemoteSync)
{
    PublishStartupStatus(GameSessionStartupPhase::Starting, 0.01f,
                         "Starting host session");
    StartWorker();
}

void HostSession::StartWorker()
{
    running = true;
    worker = std::thread(&HostSession::RunSimulation, this);
}

HostSession::~HostSession()
{
    Stop();
}

std::uint64_t HostSession::SubmitCommand(const GameCommand& command)
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    if (world == nullptr)
        return 0;
    GameCommand authoritative = command;
    authoritative.targetTick = world->GetSimulationTick() + inputDelayTicks;
    return world->SubmitCommand(authoritative, authoritative.targetTick);
}

void HostSession::Update(double dt)
{
    // Simulation runs on background thread - nothing to do here
    (void)dt;
}

GameWorld* HostSession::GetWorld()
{
    // Generated sessions publish this pointer before publishing Ready through
    // startupMutex. Callers must observe IsReadyForGameplay first; afterwards
    // the pointer remains stable for the entire session lifetime.
    return world;
}

std::vector<GameCommandResult> HostSession::ConsumeCommandResults()
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    std::vector<GameCommandResult> results = std::move(commandResults);
    commandResults.clear();
    return results;
}

bool HostSession::ConsumeLatestSnapshot(GameSnapshot& snapshot)
{
    std::lock_guard<std::mutex> lock(snapshotMutex);
    if (!hasSnapshot)
        return false;
    snapshot = latestSnapshot;
    hasSnapshot = false;
    return true;
}

bool HostSession::IsConnectionClosed() const
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    const GameSessionStartupStatus status = GetStartupStatus();
    return status.phase == GameSessionStartupPhase::Failed ||
           (transport != nullptr && hadConnection &&
            (!transport->IsConnected() || transport->HasFailed()));
}

std::string HostSession::GetConnectionStatus() const
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    const GameSessionStartupStatus startup = GetStartupStatus();
    if (startup.phase != GameSessionStartupPhase::Ready && !startup.message.empty())
        return startup.message;
    if (transport == nullptr)
        return "Single player";
    if (transport != nullptr && requireRemoteSync && !remoteInitialSnapshotReady)
        return initialSnapshotSent ? "Waiting for client map sync" : "Preparing map sync";
    return transport != nullptr ? transport->GetStatus() : std::string{};
}

int HostSession::GetPingMs() const
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    return transport != nullptr ? transport->GetPingMs() : -1;
}

bool HostSession::IsReadyForGameplay() const
{
    return GetStartupStatus().phase == GameSessionStartupPhase::Ready;
}

GameSessionStartupStatus HostSession::GetStartupStatus() const
{
    std::lock_guard<std::mutex> lock(startupMutex);
    return startupStatus;
}

void HostSession::ActivateGameplay()
{
    if (!IsReadyForGameplay())
        return;
    gameplayActivated = true;
    cv.notify_all();
}

std::recursive_mutex* HostSession::GetWorldMutex()
{
    return &worldMutex;
}

bool HostSession::ShouldPauseWhenSceneInactive() const
{
    return transport == nullptr;
}

void HostSession::SetPaused(bool shouldPause)
{
    paused.store(shouldPause);

    // When pausing, wait for an in-flight tick to leave the critical section.
    // Once this barrier returns, RunSimulationTick observes `paused` before it
    // can mutate the world again.
    if (shouldPause)
    {
        std::lock_guard<std::recursive_mutex> lock(worldMutex);
    }

    cv.notify_all();
}

bool HostSession::IsPaused() const
{
    return paused.load();
}

void HostSession::Stop()
{
    running = false;
    cv.notify_all();
    if (worker.joinable())
        worker.join();
    const GameSessionStartupStatus status = GetStartupStatus();
    if (status.phase != GameSessionStartupPhase::Failed)
        PublishStartupStatus(GameSessionStartupPhase::Stopped, status.progress,
                             "Session stopped", status.error,
                             status.usedFallback);
}

void HostSession::PublishStartupStatus(GameSessionStartupPhase phase,
                                       float progress,
                                       std::string message,
                                       std::string error,
                                       bool usedFallback)
{
    std::lock_guard<std::mutex> lock(startupMutex);
    startupStatus.phase = phase;
    startupStatus.progress = std::clamp(progress, 0.0f, 1.0f);
    startupStatus.message = std::move(message);
    startupStatus.error = std::move(error);
    startupStatus.usedFallback = usedFallback;
}

bool HostSession::InitializeGeneratedWorld()
{
    if (!startRequest.has_value())
        return world != nullptr;

    const HostSessionStartRequest request = *startRequest;
    auto normalize = [](CampaignGenerationParameters campaign)
    {
        MapParameters& params = campaign.localMap;
        const int safeMinimum = MapGenerator::SizeFromPreset(MapSizePreset::S);
        if (params.sizeX < safeMinimum || params.sizeY < safeMinimum)
        {
            params.sizePreset = MapSizePreset::S;
            params.sizeX = std::max(params.sizeX, safeMinimum);
            params.sizeY = std::max(params.sizeY, safeMinimum);
        }
        params.aiOpponentCount = std::clamp(params.aiOpponentCount, 0, 5);
        params.aiDifficulty = std::clamp(params.aiDifficulty, 0, 3);
        params.resourceDensity = std::clamp(params.resourceDensity, 0.05f, 1.0f);
        params.resourceFieldSize = std::clamp(params.resourceFieldSize, 0.05f, 1.0f);
        params.resourceRichness = std::clamp(params.resourceRichness, 1, 10000);
        if (campaign.globalMap.seed == 0)
            campaign.globalMap.seed = params.seed;
        campaign.globalMap.provinceCount = std::clamp(campaign.globalMap.provinceCount,
                                                      1,
                                                      static_cast<int>(PersistenceLimits::MaxGlobalProvinces));
        campaign.globalMap.extraEdgeCount = std::clamp(campaign.globalMap.extraEdgeCount,
                                                        0,
                                                        static_cast<int>(PersistenceLimits::MaxGlobalEdges));
        return campaign;
    };
    auto makeFallback = [&](const CampaignGenerationParameters& requested, int tier)
    {
        // Fallbacks may simplify layout policy, but never change the map or
        // participant contract selected by the user/server.
        CampaignGenerationParameters fallback = normalize(requested);
        fallback.localMap.seed = requested.localMap.seed ^
            (0xD1B54A35u * static_cast<unsigned int>(tier));
        fallback.globalMap.seed = requested.globalMap.seed ^
            (0x9E3779B9u * static_cast<unsigned int>(tier));
        return fallback;
    };

    struct Attempt
    {
        CampaignGenerationParameters params;
        float progressBegin;
        float progressSpan;
        const char* label;
        bool fallback;
    };

    const CampaignGenerationParameters requested = normalize(request.params);
    const std::array<Attempt, 3> attempts{{
        {requested, 0.02f, 0.60f, "Generating requested world", false},
        {makeFallback(requested, 1), 0.63f, 0.22f,
         "Retrying deterministic local layout", true},
        {makeFallback(requested, 2), 0.86f, 0.12f,
         "Retrying deterministic layout", true}}};

    std::string errors;
    for (const Attempt& attempt : attempts)
    {
        if (!running.load())
            return false;

        PublishStartupStatus(
            attempt.fallback ? GameSessionStartupPhase::Recovering
                             : GameSessionStartupPhase::GeneratingWorld,
            attempt.progressBegin, attempt.label, {}, attempt.fallback);
        auto candidate = std::make_unique<GameWorld>();
        bool initialized = false;
        try
        {
            const auto report = [&](float value, const std::string& message)
            {
                if (!running.load())
                    throw std::runtime_error("world generation cancelled");
                PublishStartupStatus(
                    attempt.fallback ? GameSessionStartupPhase::Recovering
                                     : GameSessionStartupPhase::GeneratingWorld,
                    attempt.progressBegin + attempt.progressSpan *
                        std::clamp(value, 0.0f, 1.0f),
                    attempt.fallback
                        ? std::string(attempt.label) + ": " + message
                        : message,
                    {}, attempt.fallback);
            };
            initialized = request.mode == HostWorldMode::SinglePlayer
                ? candidate->InitWorld(request.worldName, nullptr,
                                       attempt.params, report)
                : candidate->InitMultiplayerWorld(
                      request.worldName, nullptr, attempt.params,
                      0, true, report);
        }
        catch (const std::exception& exception)
        {
            if (!running.load())
                return false;
            errors += std::string(attempt.label) + " threw: " +
                      exception.what() + "; ";
            Log::Error("[Session]", attempt.label,
                       " raised an exception: ", exception.what());
        }
        catch (...)
        {
            errors += std::string(attempt.label) +
                      " threw an unknown exception; ";
            Log::Error("[Session]", attempt.label,
                       " raised an unknown exception");
        }

        if (initialized && candidate->IsInitialized())
        {
            {
                std::lock_guard<std::recursive_mutex> lock(worldMutex);
                ownedWorld = std::move(candidate);
                world = ownedWorld.get();
            }
            startRequest.reset();
            if (transport != nullptr && requireRemoteSync)
            {
                remoteSyncStartedAt = std::chrono::steady_clock::now();
                PublishStartupStatus(GameSessionStartupPhase::WaitingForPeer,
                                     0.96f,
                                     "Waiting for client map sync", {},
                                     attempt.fallback);
            }
            else
            {
                PublishStartupStatus(GameSessionStartupPhase::Ready, 1.0f,
                                     attempt.fallback
                                         ? "Safe fallback world ready"
                                         : "World ready",
                                     {}, attempt.fallback);
            }
            return true;
        }

        const std::string attemptError = candidate->GetInitializationError();
        if (!attemptError.empty())
        {
            errors += std::string(attempt.label) + ": " + attemptError + "; ";
            Log::Msg("[Session]", attempt.label,
                     " unavailable, continuing with fallback: ", attemptError);
        }
    }

    if (errors.empty())
        errors = "all safe generation tiers were exhausted";
    PublishStartupStatus(GameSessionStartupPhase::Failed, 1.0f,
                         "Could not create a safe world", errors, true);
    return false;
}

void HostSession::SendInitialSnapshot()
{
    if (world == nullptr || transport == nullptr)
        return;

    std::string payload = world->SerializeSimulationState();
    if (payload.empty())
    {
        ++initialSnapshotFailureCount;
        Log::Msg("[Session]", "Initial simulation-state serialization failed");
        if (initialSnapshotFailureCount >= 3)
        {
            const GameSessionStartupStatus status = GetStartupStatus();
            PublishStartupStatus(GameSessionStartupPhase::Failed, 1.0f,
                                 "Could not prepare host map data",
                                 "Simulation-state serialization failed three times",
                                 status.usedFallback);
            running = false;
            cv.notify_all();
        }
        return;
    }
    constexpr size_t ChunkSize = 12000;
    size_t totalChunks = payload.empty() ? 0 : (payload.size() + ChunkSize - 1) / ChunkSize;
    transport->SendHostSnapshot("INIT_BEGIN " + std::to_string(world->GetSimulationTick()) + " " +
                                std::to_string(payload.size()) + " " + std::to_string(totalChunks));
    for (size_t i = 0; i < totalChunks; i++)
    {
        size_t offset = i * ChunkSize;
        transport->SendHostSnapshot("INIT_CHUNK " + std::to_string(i) + " " + payload.substr(offset, ChunkSize));
    }
    transport->SendHostSnapshot("INIT_END");
    initialSnapshotSent = true;
    initialSnapshotFailureCount = 0;
    Log::Msg("[Session]", "Initial snapshot queued: bytes=", payload.size(), " chunks=", totalChunks);
}

void HostSession::SendCorrectionSnapshot()
{
    if (world == nullptr || transport == nullptr)
        return;

    // The TCP protocol limits each snapshot frame to 32 KiB. Reuse the
    // established initial-sync envelope so a corrective snapshot cannot turn
    // into one unbounded transport message on a larger map.
    std::string payload = world->SerializeSimulationState();
    if (payload.empty())
    {
        Log::Msg("[Session]", "Correction simulation-state serialization failed");
        return;
    }
    constexpr size_t ChunkSize = 12000;
    size_t totalChunks = payload.empty() ? 0 : (payload.size() + ChunkSize - 1) / ChunkSize;
    transport->SendHostSnapshot("INIT_BEGIN " + std::to_string(world->GetSimulationTick()) + " " +
                                std::to_string(payload.size()) + " " + std::to_string(totalChunks));
    for (size_t i = 0; i < totalChunks; ++i)
    {
        size_t offset = i * ChunkSize;
        transport->SendHostSnapshot("INIT_CHUNK " + std::to_string(i) + " " + payload.substr(offset, ChunkSize));
    }
    transport->SendHostSnapshot("INIT_END");
    Log::Msg("[Session]", "Correction snapshot queued: bytes=", payload.size(), " chunks=", totalChunks);
}

void HostSession::RememberRemoteCommandResult(const GameCommandResult& result)
{
    if (pendingRemoteCommandIds.erase(result.commandId) == 0 &&
        !completedRemoteCommandResults.contains(result.commandId))
        return;

    if (!completedRemoteCommandResults.contains(result.commandId))
        completedRemoteCommandOrder.push_back(result.commandId);
    completedRemoteCommandResults[result.commandId] = result;

    while (completedRemoteCommandOrder.size() > MaxRememberedRemoteCommands)
    {
        const std::uint64_t expiredId = completedRemoteCommandOrder.front();
        completedRemoteCommandOrder.pop_front();
        completedRemoteCommandResults.erase(expiredId);
    }
}

void HostSession::RunSimulationTick()
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    if (world == nullptr || paused.load())
        return;

    // Handle transport commands
    if (transport != nullptr)
    {
        if (transport->HasFailed() ||
            (hadConnection && !transport->IsConnected()))
        {
            const std::string detail = transport->GetStatus();
            PublishStartupStatus(
                GameSessionStartupPhase::Failed,
                GetStartupStatus().progress,
                "Host connection failed",
                detail.empty() ? "Remote connection closed" : detail,
                GetStartupStatus().usedFallback);
            running = false;
            cv.notify_all();
            return;
        }
        if (requireRemoteSync && !remoteStartAcknowledged &&
            remoteSyncStartedAt != std::chrono::steady_clock::time_point{} &&
            std::chrono::steady_clock::now() - remoteSyncStartedAt >
                std::chrono::seconds(90))
        {
            const GameSessionStartupStatus status = GetStartupStatus();
            PublishStartupStatus(
                GameSessionStartupPhase::Failed, 1.0f,
                "Client startup synchronization timed out",
                "Remote client did not complete the startup handshake within 90 seconds",
                status.usedFallback);
            running = false;
            cv.notify_all();
            return;
        }
        hadConnection = hadConnection || transport->IsConnected();
        if (requireRemoteSync && !initialSnapshotSent && transport->IsConnected())
            SendInitialSnapshot();

        for (const auto& payload : transport->ReceiveHostCommands())
        {
            if (payload == "RESYNC_REQUEST")
            {
                if (correctionSnapshotCooldown <= 0.0)
                {
                    SendCorrectionSnapshot();
                    correctionSnapshotCooldown = 5.0;
                }
                else
                {
                    Log::Msg("[Session]", "Ignoring resync request during cooldown");
                }
                continue;
            }

            if (payload == "SYNC_READY")
            {
                remoteInitialSnapshotReady = true;
                lastSentSnapshot = GameSnapshot{};
                hasLastSentSnapshot = false;
                transport->SendHostSnapshot("START_READY");
                const GameSessionStartupStatus status = GetStartupStatus();
                PublishStartupStatus(GameSessionStartupPhase::WaitingForPeer,
                                     0.99f,
                                     "Waiting for client start acknowledgment", {},
                                     status.usedFallback);
                Log::Msg("[Session]", "Remote client confirmed map sync; start confirmation sent");
                continue;
            }

            if (payload == "START_ACK")
            {
                remoteStartAcknowledged = true;
                const GameSessionStartupStatus status = GetStartupStatus();
                PublishStartupStatus(GameSessionStartupPhase::Ready, 1.0f,
                                     "Map and start synchronized", {},
                                     status.usedFallback);
                Log::Msg("[Session]", "Remote client acknowledged synchronized start");
                continue;
            }

            GameCommand command;
            if (GameCommand::TryDeserialize(payload, command))
            {
                const std::uint64_t authoritativeTargetTick = world->GetSimulationTick() + inputDelayTicks;
                command.targetTick = authoritativeTargetTick;
                if (command.commandId == 0)
                {
                    GameCommandResult rejected{
                        command.commandId,
                        world->GetSimulationTick(),
                        authoritativeTargetTick,
                        command.playerId,
                        command.type,
                        false,
                        "rejected: missing command id",
                        command.Serialize()};
                    RememberRemoteCommandResult(rejected);
                    commandResults.push_back(rejected);
                    transport->SendHostResult(rejected.Serialize());
                    continue;
                }
                if (const auto completed = completedRemoteCommandResults.find(command.commandId);
                    completed != completedRemoteCommandResults.end())
                {
                    // Replayed input gets the original answer; the world
                    // mutation must happen at most once.
                    transport->SendHostResult(completed->second.Serialize());
                    continue;
                }
                if (pendingRemoteCommandIds.contains(command.commandId))
                    continue;
                if (command.playerId != remotePlayerId)
                {
                    GameCommandResult rejected{
                        command.commandId,
                        world->GetSimulationTick(),
                        authoritativeTargetTick,
                        command.playerId,
                        command.type,
                        false,
                        "rejected: wrong player slot",
                        command.Serialize()};
                    pendingRemoteCommandIds.insert(command.commandId);
                    RememberRemoteCommandResult(rejected);
                    commandResults.push_back(rejected);
                    transport->SendHostResult(rejected.Serialize());
                    continue;
                }
                pendingRemoteCommandIds.insert(command.commandId);
                world->SubmitCommand(command, authoritativeTargetTick);
            }
        }

        if (requireRemoteSync && !remoteStartAcknowledged)
            return;
    }

    if (!gameplayActivated.load())
        return;

    // Update simulation
    int ticks = clock.AddFrameTime(FixedSimulationClock::FixedDt);
    for (int i = 0; i < ticks; i++)
    {
        world->UpdateSimulation(FixedSimulationClock::FixedDt);
        auto results = world->ConsumeCommandResults();

        GameServerFrame frame;
        frame.tick = world->GetSimulationTick();
        checksumTimer += FixedSimulationClock::FixedDt;
        if (checksumTimer >= 1.0)
        {
            checksumTimer = 0.0;
            frame.hasChecksum = true;
            frame.checksum = world->BuildChecksum();
        }

        for (const auto& result : results)
        {
            RememberRemoteCommandResult(result);
            commandResults.push_back(result);
            frame.results.push_back(result);
        }

        if (transport != nullptr)
            transport->SendHostFrame(frame.Serialize());

        // Snapshot capture for threaded access
        if (frame.hasChecksum)
        {
            std::lock_guard<std::mutex> snapshotLock(snapshotMutex);
            latestSnapshot = world->BuildSnapshot();
            hasSnapshot = latestSnapshot.IsValid();
        }
    }
}

void HostSession::RunSimulation()
{
    try
    {
        if (startRequest.has_value() && !InitializeGeneratedWorld())
        {
            running = false;
            return;
        }
    }
    catch (const std::exception& exception)
    {
        PublishStartupStatus(GameSessionStartupPhase::Failed, 1.0f,
                             "Session initialization failed",
                             exception.what());
        Log::Error("[Session]", "Host startup failed: ", exception.what());
        running = false;
        return;
    }
    catch (...)
    {
        PublishStartupStatus(GameSessionStartupPhase::Failed, 1.0f,
                             "Session initialization failed",
                             "Unknown host startup exception");
        Log::Error("[Session]", "Host startup failed with unknown exception");
        running = false;
        return;
    }

    if (GetStartupStatus().phase == GameSessionStartupPhase::Failed)
    {
        running = false;
        return;
    }

    auto nextTick = std::chrono::steady_clock::now();
    while (running)
    {
        if (paused.load())
        {
            std::unique_lock<std::mutex> pauseLock(sleepMutex);
            cv.wait(pauseLock, [&]() { return !running.load() || !paused.load(); });
            nextTick = std::chrono::steady_clock::now();
            continue;
        }

        nextTick += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(FixedSimulationClock::FixedDt));

        try
        {
            RunSimulationTick();
        }
        catch (const std::exception& exception)
        {
            PublishStartupStatus(GameSessionStartupPhase::Failed, 1.0f,
                                 "Host simulation failed", exception.what());
            Log::Error("[Session]", "Host worker tick failed: ", exception.what());
            running = false;
            cv.notify_all();
            break;
        }
        catch (...)
        {
            PublishStartupStatus(GameSessionStartupPhase::Failed, 1.0f,
                                 "Host simulation failed",
                                 "Unknown host worker tick exception");
            Log::Error("[Session]", "Host worker tick failed with unknown exception");
            running = false;
            cv.notify_all();
            break;
        }

        std::unique_lock<std::mutex> sleepLock(sleepMutex);
        cv.wait_until(sleepLock, nextTick, [&]() { return !running.load() || paused.load(); });
        if (std::chrono::steady_clock::now() > nextTick + std::chrono::milliseconds(250))
            nextTick = std::chrono::steady_clock::now();
    }
}

// LocalhostHostSession is now deprecated - use HostSession instead instead

// ============================================================================
// ClientSession Implementation
// ============================================================================

ClientSession::ClientSession(GameWorld* observedWorld, std::shared_ptr<IGameTransport> transport, int assignedPlayerId)
    : observedWorld(observedWorld), transport(std::move(transport)), assignedPlayerId(assignedPlayerId)
{
    startupStartedAt = std::chrono::steady_clock::now();
    PublishStartupStatus(GameSessionStartupPhase::SynchronizingWorld, 0.02f,
                         "Waiting for host map data");
}

ClientSession::ClientSession(std::unique_ptr<GameWorld> observedWorld,
                             std::shared_ptr<IGameTransport> transport,
                             int assignedPlayerId)
    : ownedObservedWorld(std::move(observedWorld)),
      observedWorld(ownedObservedWorld.get()),
      transport(std::move(transport)), assignedPlayerId(assignedPlayerId),
      backgroundWorker(true)
{
    if (this->ownedObservedWorld == nullptr)
    {
        this->ownedObservedWorld = std::make_unique<GameWorld>();
        this->observedWorld = this->ownedObservedWorld.get();
    }
    startupStartedAt = std::chrono::steady_clock::now();
    PublishStartupStatus(GameSessionStartupPhase::SynchronizingWorld, 0.02f,
                         "Waiting for host map data");
    running = true;
    worker = std::thread(&ClientSession::RunNetwork, this);
}

ClientSession::~ClientSession()
{
    Stop();
}

void ClientSession::HandleAuthoritativeResult(const GameCommandResult& result)
{
    const auto key = std::make_pair(result.playerId, result.commandId);
    if (!observedCommandResults.insert(key).second)
        return;
    observedCommandResultOrder.push_back(key);
    while (observedCommandResultOrder.size() > MaxObservedCommandResults)
    {
        observedCommandResults.erase(observedCommandResultOrder.front());
        observedCommandResultOrder.pop_front();
    }

    if (result.accepted && observedWorld != nullptr && !result.commandPayload.empty())
    {
        GameCommand command;
        if (GameCommand::TryDeserialize(result.commandPayload, command))
        {
            if (observedWorld->GetSimulationTick() < result.simulationTick)
                observedWorld->SubmitCommand(command, command.targetTick);
            else
                observedWorld->ApplyAuthoritativeCommand(command);
        }
    }
    if (result.playerId == assignedPlayerId)
        pendingClientCommandPayloads.erase(result.commandId);
    commandResults.push_back(result);
}

std::uint64_t ClientSession::SubmitCommand(const GameCommand& command)
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    GameCommand outbound = command;
    outbound.playerId = assignedPlayerId;
    if (outbound.commandId == 0)
        outbound.commandId = nextClientCommandId++;
    const std::string payload = outbound.Serialize();
    pendingClientCommandPayloads[outbound.commandId] = payload;
    if (transport != nullptr)
    {
        transport->SendClientCommand(payload);
        wasConnected = transport->IsConnected();
    }
    return outbound.commandId;
}

void ClientSession::Update(double dt)
{
    if (backgroundWorker)
        return;
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    RunNetworkTick(dt);
}

void ClientSession::RunNetworkTick(double dt)
{
    if (transport == nullptr)
    {
        PublishStartupStatus(GameSessionStartupPhase::Failed, 1.0f,
                             "Client session has no transport",
                             "Missing multiplayer transport");
        return;
    }

    if (resyncRequestCooldown > 0.0)
        resyncRequestCooldown = std::max(0.0, resyncRequestCooldown - dt);

    const bool connectedNow = transport->IsConnected();
    hadConnection = hadConnection || connectedNow;
    if (transport->HasFailed() || (hadConnection && !connectedNow))
    {
        const std::string detail = transport->GetStatus();
        PublishStartupStatus(GameSessionStartupPhase::Failed, 1.0f,
                             "Connection to host failed",
                             detail.empty() ? "Host connection closed" : detail);
        running = false;
        cv.notify_all();
        return;
    }
    if (!initialSnapshotReceived &&
        std::chrono::steady_clock::now() - startupStartedAt >
            std::chrono::seconds(45))
    {
        PublishStartupStatus(GameSessionStartupPhase::Failed, 1.0f,
                             "Map synchronization timed out",
                             "Host did not provide a usable map within 45 seconds");
        running = false;
        cv.notify_all();
        return;
    }
    const GameSessionStartupStatus startup = GetStartupStatus();
    if (backgroundWorker && initialSnapshotReceived &&
        startup.phase == GameSessionStartupPhase::WaitingForPeer &&
        std::chrono::steady_clock::now() - startupStartedAt >
            std::chrono::seconds(90))
    {
        PublishStartupStatus(GameSessionStartupPhase::Failed, 1.0f,
                             "Synchronized start timed out",
                             "Host did not confirm the synchronized start within 90 seconds");
        running = false;
        cv.notify_all();
        return;
    }
    if (connectedNow && !wasConnected)
    {
        for (const auto& [commandId, payload] : pendingClientCommandPayloads)
        {
            (void)commandId;
            transport->SendClientCommand(payload);
        }
    }
    wasConnected = connectedNow;
    // TCP preserves wire order, but IGameTransport exposes snapshot and event
    // queues separately. Drain/apply the recovery state first so frames sent
    // by the host after INIT_END are applied to the restored world, never the
    // stale mirror that existed before correction.
    for (const auto& payload : transport->ReceiveClientSnapshots())
        HandleSnapshotPayload(payload);

    if (initialSnapshotFailed)
    {
        if (backgroundWorker)
        {
            RetryOrFailInitialSync();
            if (initialSnapshotFailed ||
                GetStartupStatus().phase == GameSessionStartupPhase::Failed)
                return;
        }
        else
        {
            return;
        }
    }

    for (const auto& payload : transport->ReceiveClientFrames())
    {
        GameServerFrame frame;
        if (!GameServerFrame::TryDeserialize(payload, frame))
            continue;

        for (const auto& result : frame.results)
            HandleAuthoritativeResult(result);

        if (observedWorld != nullptr && initialSnapshotReceived)
        {
            while (observedWorld->GetSimulationTick() < frame.tick)
            {
                observedWorld->UpdateSimulation(FixedSimulationClock::FixedDt);
                observedWorld->ConsumeCommandResults();
            }

            if (frame.hasChecksum)
            {
                std::uint64_t localChecksum = observedWorld->BuildChecksum();
                if (localChecksum != frame.checksum)
                {
                    syncStatus = "Desync detected, requesting snapshot";
                    if (resyncRequestCooldown <= 0.0)
                    {
                        transport->SendClientCommand("RESYNC_REQUEST");
                        resyncRequestCooldown = 30.0;
                        Log::Msg("[Session]", "Checksum mismatch: local=", localChecksum, " host=", frame.checksum, " requesting snapshot");
                    }
                    else
                    {
                        Log::Msg("[Session]", "Checksum mismatch during resync cooldown: local=", localChecksum, " host=", frame.checksum);
                    }
                }

                // The UI snapshot follows the deterministically replayed
                // client mirror. Before this refresh it stayed frozen at the
                // initial/correction snapshot despite a current live world.
                latestNetworkSnapshot = observedWorld->BuildSnapshot();
                hasNetworkSnapshot = latestNetworkSnapshot.IsValid();
            }
        }
    }

    for (const auto& payload : transport->ReceiveClientResults())
    {
        GameCommandResult result;
        if (GameCommandResult::TryDeserialize(payload, result))
            HandleAuthoritativeResult(result);
    }

    (void)dt;
}

GameWorld* ClientSession::GetWorld()
{
    return observedWorld;
}

bool ClientSession::ConsumeLatestSnapshot(GameSnapshot& snapshot)
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    if (!hasNetworkSnapshot)
        return false;
    snapshot = latestNetworkSnapshot;
    hasNetworkSnapshot = false;
    return true;
}

bool ClientSession::IsConnectionClosed() const
{
    const GameSessionStartupStatus status = GetStartupStatus();
    return status.phase == GameSessionStartupPhase::Failed ||
           (transport != nullptr && hadConnection &&
            (!transport->IsConnected() || transport->HasFailed()));
}

int ClientSession::GetPingMs() const
{
    return transport != nullptr ? transport->GetPingMs() : -1;
}

std::string ClientSession::GetConnectionStatus() const
{
    if (!backgroundWorker && !initialSnapshotReceived)
        return syncStatus;
    const GameSessionStartupStatus status = GetStartupStatus();
    if (status.phase != GameSessionStartupPhase::Ready)
        return status.message;
    if (!initialSnapshotReceived)
        return syncStatus;
    return transport != nullptr ? transport->GetStatus() : std::string{};
}

bool ClientSession::IsReadyForGameplay() const
{
    return GetStartupStatus().phase == GameSessionStartupPhase::Ready;
}

GameSessionStartupStatus ClientSession::GetStartupStatus() const
{
    std::lock_guard<std::mutex> lock(startupMutex);
    return startupStatus;
}

void ClientSession::PublishStartupStatus(GameSessionStartupPhase phase,
                                         float progress,
                                         std::string message,
                                         std::string error)
{
    std::lock_guard<std::mutex> lock(startupMutex);
    startupStatus.phase = phase;
    startupStatus.progress = std::clamp(progress, 0.0f, 1.0f);
    startupStatus.message = std::move(message);
    startupStatus.error = std::move(error);
}

void ClientSession::ActivateGameplay()
{
    // Network receive already runs throughout LoadingScene so frames cannot
    // accumulate while the main thread fades into gameplay. Unlike the
    // authoritative host, the client has no simulation tick to release here.
}

std::recursive_mutex* ClientSession::GetWorldMutex()
{
    return &worldMutex;
}

std::vector<GameCommandResult> ClientSession::ConsumeCommandResults()
{
    std::lock_guard<std::recursive_mutex> lock(worldMutex);
    std::vector<GameCommandResult> results = std::move(commandResults);
    commandResults.clear();
    return results;
}

void ClientSession::HandleSnapshotPayload(const std::string& payload)
{
    if (payload.rfind("INIT_BEGIN ", 0) == 0)
    {
        startupStartedAt = std::chrono::steady_clock::now();
        std::istringstream in(payload.substr(11));
        std::uint64_t tick = 0;
        size_t totalBytes = 0;
        size_t totalChunks = 0;
        if (in >> tick >> totalBytes >> totalChunks)
        {
            if (totalBytes > MaxInitialSnapshotBytes || totalChunks > MaxInitialSnapshotChunks ||
                (totalBytes == 0 && totalChunks != 0) || (totalBytes != 0 && totalChunks == 0))
            {
                initialSnapshotFailed = true;
                initialSnapshotReceived = false;
                syncStatus = "Map sync failed: invalid manifest";
                Log::Msg("[Session]", "Rejected initial snapshot manifest: bytes=", totalBytes,
                         " chunks=", totalChunks);
                return;
            }
            initialSnapshotBuffer.clear();
            initialSnapshotChunks.clear();
            initialSnapshotChunks.resize(totalChunks);
            initialSnapshotChunkReceived.assign(totalChunks, false);
            expectedInitialSnapshotBytes = totalBytes;
            expectedInitialSnapshotChunks = totalChunks;
            receivedInitialSnapshotChunks = 0;
            receivedInitialSnapshotBytes = 0;
            expectedInitialSnapshotTick = tick;
            initialSnapshotReceived = false;
            initialSnapshotFailed = false;
            syncStatus = "Syncing map 0/" + std::to_string(totalChunks);
            PublishStartupStatus(GameSessionStartupPhase::SynchronizingWorld,
                                 0.08f, syncStatus);
            Log::Msg("[Session]", "Receiving initial snapshot: bytes=", totalBytes, " chunks=", totalChunks);
        }
        else
        {
            initialSnapshotFailed = true;
            initialSnapshotReceived = false;
            syncStatus = "Map sync failed: malformed manifest";
        }
        return;
    }

    if (payload.rfind("INIT_CHUNK ", 0) == 0)
    {
        size_t firstSpace = payload.find(' ', 11);
        if (firstSpace == std::string::npos)
            return;
        size_t index = 0;
        try
        {
            index = static_cast<size_t>(std::stoull(payload.substr(11, firstSpace - 11)));
        }
        catch (...)
        {
            initialSnapshotFailed = true;
            syncStatus = "Map sync failed";
            return;
        }
        if (index >= initialSnapshotChunks.size())
        {
            initialSnapshotFailed = true;
            syncStatus = "Map sync failed: chunk index out of range";
            return;
        }
        std::string chunk = payload.substr(firstSpace + 1);
        if (chunk.size() > expectedInitialSnapshotBytes ||
            (!initialSnapshotChunkReceived[index] &&
             chunk.size() > expectedInitialSnapshotBytes - receivedInitialSnapshotBytes))
        {
            initialSnapshotFailed = true;
            syncStatus = "Map sync failed: chunk exceeds manifest";
            return;
        }
        if (initialSnapshotChunkReceived[index])
        {
            if (initialSnapshotChunks[index] != chunk)
            {
                initialSnapshotFailed = true;
                syncStatus = "Map sync failed: conflicting duplicate chunk";
            }
            return;
        }
        initialSnapshotChunkReceived[index] = true;
        receivedInitialSnapshotChunks++;
        receivedInitialSnapshotBytes += chunk.size();
        initialSnapshotChunks[index] = std::move(chunk);
        syncStatus = "Syncing map " + std::to_string(receivedInitialSnapshotChunks) + "/" + std::to_string(expectedInitialSnapshotChunks);
        const float chunkRatio = expectedInitialSnapshotChunks > 0
            ? static_cast<float>(receivedInitialSnapshotChunks) /
                  static_cast<float>(expectedInitialSnapshotChunks)
            : 1.0f;
        PublishStartupStatus(GameSessionStartupPhase::SynchronizingWorld,
                             0.10f + 0.72f * chunkRatio, syncStatus);
        return;
    }

    if (payload == "INIT_END")
    {
        if (initialSnapshotFailed)
            return;
        if (receivedInitialSnapshotChunks != expectedInitialSnapshotChunks ||
            receivedInitialSnapshotBytes != expectedInitialSnapshotBytes)
        {
            syncStatus = "Waiting for map chunks";
            return;
        }
        initialSnapshotBuffer.clear();
        initialSnapshotBuffer.reserve(expectedInitialSnapshotBytes);
        for (const auto& chunk : initialSnapshotChunks)
            initialSnapshotBuffer += chunk;

        PublishStartupStatus(GameSessionStartupPhase::SynchronizingWorld,
                             0.88f, "Restoring synchronized world");

        if (initialSnapshotBuffer.size() == expectedInitialSnapshotBytes && observedWorld != nullptr &&
            observedWorld->RestoreSimulationState(initialSnapshotBuffer, assignedPlayerId) &&
            observedWorld->GetSimulationTick() == expectedInitialSnapshotTick)
        {
            latestNetworkSnapshot = observedWorld->BuildSnapshot();
            hasNetworkSnapshot = true;
            initialSnapshotReceived = true;
            initialSnapshotRetryCount = 0;
            startupStartedAt = std::chrono::steady_clock::now();
            if (backgroundWorker)
            {
                syncStatus = "Waiting for host start confirmation";
                PublishStartupStatus(GameSessionStartupPhase::WaitingForPeer,
                                     0.98f, syncStatus);
            }
            else
            {
                syncStatus = "Map synchronized";
                PublishStartupStatus(GameSessionStartupPhase::Ready, 1.0f,
                                     syncStatus);
            }
            if (transport != nullptr)
                transport->SendClientCommand("SYNC_READY");
            Log::Msg("[Session]", "Initial snapshot received");
            initialSnapshotBuffer.clear();
            initialSnapshotBuffer.shrink_to_fit();
            initialSnapshotChunks.clear();
            initialSnapshotChunks.shrink_to_fit();
            initialSnapshotChunkReceived.clear();
            initialSnapshotChunkReceived.shrink_to_fit();
            receivedInitialSnapshotBytes = 0;
        }
        else
        {
            syncStatus = "Map sync failed";
            initialSnapshotFailed = true;
            Log::Msg("[Session]", "Initial snapshot parse failed");
        }
        return;
    }

    if (payload == "START_READY")
    {
        if (initialSnapshotReceived)
        {
            syncStatus = "Map and start synchronized";
            if (transport != nullptr)
                transport->SendClientCommand("START_ACK");
            PublishStartupStatus(GameSessionStartupPhase::Ready, 1.0f,
                                 syncStatus);
            Log::Msg("[Session]", "Host confirmed synchronized start");
        }
        return;
    }

    if (payload.rfind("DELTA ", 0) == 0)
    {
        if (!initialSnapshotReceived)
            return;
        GameSnapshotDelta delta;
        if (GameSnapshotDelta::TryDeserialize(payload.substr(6), delta) &&
            delta.ApplyTo(latestNetworkSnapshot))
        {
            hasNetworkSnapshot = true;
        }
        return;
    }

    GameSnapshot snapshot;
    if (GameSnapshot::TryDeserialize(payload, snapshot))
    {
        latestNetworkSnapshot = std::move(snapshot);
        hasNetworkSnapshot = true;
    }
}

void ClientSession::ResetInitialSnapshotTransfer()
{
    initialSnapshotReceived = false;
    initialSnapshotFailed = false;
    expectedInitialSnapshotBytes = 0;
    expectedInitialSnapshotChunks = 0;
    receivedInitialSnapshotChunks = 0;
    receivedInitialSnapshotBytes = 0;
    expectedInitialSnapshotTick = 0;
    initialSnapshotBuffer.clear();
    initialSnapshotChunks.clear();
    initialSnapshotChunkReceived.clear();
}

void ClientSession::RetryOrFailInitialSync()
{
    if (!initialSnapshotFailed)
        return;

    constexpr int MaxInitialSyncRetries = 3;
    if (initialSnapshotRetryCount >= MaxInitialSyncRetries || transport == nullptr)
    {
        PublishStartupStatus(
            GameSessionStartupPhase::Failed, 1.0f,
            "Map synchronization failed",
            "Host map data remained invalid after " +
                std::to_string(initialSnapshotRetryCount) + " retries");
        running = false;
        cv.notify_all();
        return;
    }

    ++initialSnapshotRetryCount;
    ResetInitialSnapshotTransfer();
    syncStatus = "Retrying map sync " +
                 std::to_string(initialSnapshotRetryCount) + "/" +
                 std::to_string(MaxInitialSyncRetries);
    PublishStartupStatus(GameSessionStartupPhase::Recovering, 0.05f,
                         syncStatus);
    transport->SendClientCommand("RESYNC_REQUEST");
    Log::Msg("[Session]", syncStatus);
}

void ClientSession::RunNetwork()
{
    auto previous = std::chrono::steady_clock::now();
    try
    {
        while (running.load())
        {
            const auto now = std::chrono::steady_clock::now();
            const double dt = std::chrono::duration<double>(now - previous).count();
            previous = now;
            {
                std::lock_guard<std::recursive_mutex> lock(worldMutex);
                RunNetworkTick(std::clamp(dt, 0.0, 0.25));
            }

            std::unique_lock<std::mutex> sleepLock(sleepMutex);
            cv.wait_for(sleepLock, std::chrono::milliseconds(5),
                        [&]() { return !running.load(); });
        }
    }
    catch (const std::exception& exception)
    {
        PublishStartupStatus(GameSessionStartupPhase::Failed, 1.0f,
                             "Client session failed", exception.what());
        Log::Error("[Session]", "Client worker failed: ", exception.what());
        running = false;
    }
    catch (...)
    {
        PublishStartupStatus(GameSessionStartupPhase::Failed, 1.0f,
                             "Client session failed",
                             "Unknown client worker exception");
        Log::Error("[Session]", "Client worker failed with unknown exception");
        running = false;
    }
}

void ClientSession::Stop()
{
    if (backgroundWorker)
    {
        running = false;
        cv.notify_all();
        if (worker.joinable())
            worker.join();
    }
    const GameSessionStartupStatus status = GetStartupStatus();
    if (status.phase != GameSessionStartupPhase::Failed)
        PublishStartupStatus(GameSessionStartupPhase::Stopped,
                             status.progress, "Session stopped",
                             status.error);
}

// LocalhostMultiplayerSession is now deprecated - use HostSession instead
// ThreadedGameSession functionality is now integrated into HostSession
