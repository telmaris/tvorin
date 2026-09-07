#ifndef GAME_SESSION_H
#define GAME_SESSION_H

#include "core/GameWorld.h"
#include "core/CampaignGeneration.h"
#include "core/PersistenceLimits.h"
#include "multiplayer/SnapshotTransfer.h"

#include <cstdint>
#include <deque>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <set>
#include <string>
#include <thread>
#include <vector>

// Minimal transport contract for command/result payloads.
class IGameTransport
{
public:
    virtual ~IGameTransport() = default;
    virtual void SendClientCommand(const std::string& payload) = 0;
    virtual std::vector<std::string> ReceiveHostCommands() = 0;
    virtual void SendHostResult(const std::string& payload) = 0;
    virtual std::vector<std::string> ReceiveClientResults() = 0;
    virtual void SendHostFrame(const std::string& payload) { (void)payload; }
    virtual std::vector<std::string> ReceiveClientFrames() { return {}; }
    // Reserved for compact/chunked snapshot payloads. Do not stream full maps over TCP.
    virtual void SendHostSnapshot(const std::string& payload) { (void)payload; }
    virtual std::vector<std::string> ReceiveClientSnapshots() { return {}; }
    virtual bool IsConnected() const { return true; }
    virtual bool HasFailed() const { return false; }
    virtual std::string GetStatus() const { return {}; }
    virtual int GetPingMs() const { return -1; }
};

// In-process localhost transport used to prototype multiplayer flow before Steam/sockets.
class LocalhostGameTransport : public IGameTransport
{
public:
    void SendClientCommand(const std::string& payload) override;
    std::vector<std::string> ReceiveHostCommands() override;
    void SendHostResult(const std::string& payload) override;
    std::vector<std::string> ReceiveClientResults() override;
    void SendHostFrame(const std::string& payload) override;
    std::vector<std::string> ReceiveClientFrames() override;
    void SendHostSnapshot(const std::string& payload) override;
    std::vector<std::string> ReceiveClientSnapshots() override;

private:
    static std::vector<std::string> Drain(std::deque<std::string>& queue);

    mutable std::mutex mutex;
    std::deque<std::string> clientToHost;
    std::deque<std::string> hostToClient;
    std::deque<std::string> hostFrames;
    std::deque<std::string> hostSnapshots;
};

enum class GameSessionStartupPhase
{
    Starting,
    GeneratingWorld,
    WaitingForPeer,
    SynchronizingWorld,
    Ready,
    Recovering,
    Failed,
    Stopped
};

struct GameSessionStartupStatus
{
    GameSessionStartupPhase phase{GameSessionStartupPhase::Starting};
    float progress{0.0f};
    std::string message{"Starting session"};
    std::string error;
    bool usedFallback{false};
};

enum class HostWorldMode
{
    SinglePlayer,
    MultiplayerHost
};

struct HostSessionStartRequest
{
    std::string worldName;
    CampaignGenerationParameters params;
    HostWorldMode mode{HostWorldMode::SinglePlayer};
};

// Abstracts the authority that advances a game world.
class IGameSession
{
public:
    virtual ~IGameSession() = default;
    virtual std::uint64_t SubmitCommand(const GameCommand& command) = 0;
    virtual void Update(double dt) = 0;
    virtual GameWorld* GetWorld() = 0;
    virtual std::vector<GameCommandResult> ConsumeCommandResults() = 0;
    virtual bool ConsumeLatestSnapshot(GameSnapshot& snapshot) { (void)snapshot; return false; }
    virtual bool IsConnectionClosed() const { return false; }
    virtual std::string GetConnectionStatus() const { return {}; }
    virtual int GetPingMs() const { return -1; }
    virtual bool IsReadyForGameplay() const { return true; }
    virtual GameSessionStartupStatus GetStartupStatus() const
    {
        return {GameSessionStartupPhase::Ready, 1.0f, "Ready", {}, false};
    }
    // Releases the startup gate after the main thread has attached presentation
    // services and switched from LoadingScene to the gameplay scene.
    virtual void ActivateGameplay() {}
    virtual std::recursive_mutex* GetWorldMutex() { return nullptr; }
    // Single-player sessions stop advancing while their gameplay scene is inactive.
    // Network sessions must keep running so remote peers are never stalled by a local menu.
    virtual bool ShouldPauseWhenSceneInactive() const { return false; }
    // Suspends deterministic simulation ticks without accumulating catch-up time.
    // Sessions that cannot be paused (for example a remote client) may ignore it.
    virtual void SetPaused(bool paused) { (void)paused; }
    virtual bool IsPaused() const { return false; }
};

struct FixedSimulationClock
{
    static constexpr std::uint64_t TicksPerSecond = 100;
    static constexpr double FixedDt = 1.0 / static_cast<double>(TicksPerSecond);
    static constexpr int MaxTicksPerUpdate = 12;

    double accumulator{0.0};

    int AddFrameTime(double dt)
    {
        accumulator += std::clamp(dt, 0.0, 0.25);
        int ticks = 0;
        while (accumulator >= FixedDt && ticks < MaxTicksPerUpdate)
        {
            accumulator -= FixedDt;
            ticks++;
        }
        if (ticks == MaxTicksPerUpdate && accumulator >= FixedDt)
            accumulator = FixedDt - 0.000001;
        return ticks;
    }
};

// Authoritative host session. Runs on background thread. Handles local, AI, and remote players.
class HostSession : public IGameSession
{
public:
    // Single-player constructor: no transport, no remote sync
    explicit HostSession(GameWorld& world);

    // Multiplayer constructor: with transport for remote players
    HostSession(GameWorld& world, std::shared_ptr<IGameTransport> transport, int remotePlayerId = 0, bool requireRemoteSync = true);

    // Owns an already initialized world (for save loading) while preserving
    // the same lifetime rules as generated sessions.
    explicit HostSession(std::unique_ptr<GameWorld> world);

    // Creates and initializes the authoritative world at the beginning of the
    // existing HostSession worker. No additional async worker is involved.
    explicit HostSession(HostSessionStartRequest request);
    HostSession(HostSessionStartRequest request,
                std::shared_ptr<IGameTransport> transport,
                int remotePlayerId = 0,
                bool requireRemoteSync = true);

    ~HostSession() override;

    std::uint64_t SubmitCommand(const GameCommand& command) override;
    void Update(double dt) override;
    GameWorld* GetWorld() override;
    std::vector<GameCommandResult> ConsumeCommandResults() override;
    bool ConsumeLatestSnapshot(GameSnapshot& snapshot) override;
    bool IsConnectionClosed() const override;
    std::string GetConnectionStatus() const override;
    int GetPingMs() const override;
    bool IsReadyForGameplay() const override;
    GameSessionStartupStatus GetStartupStatus() const override;
    void ActivateGameplay() override;
    std::recursive_mutex* GetWorldMutex() override;
    bool ShouldPauseWhenSceneInactive() const override;
    void SetPaused(bool paused) override;
    bool IsPaused() const override;

private:
    void SendInitialSnapshot();
    void SendCorrectionSnapshot();
    void RememberRemoteCommandResult(const GameCommandResult& result);
    void Stop();
    void StartWorker();
    bool InitializeGeneratedWorld();
    void PublishStartupStatus(GameSessionStartupPhase phase, float progress,
                              std::string message, std::string error = {},
                              bool usedFallback = false);
    void RunSimulation();
    // One locked iteration of the simulation loop: transport commands, remote-sync
    // gate, fixed-tick update. Returns early (no goto) once nothing more to do this
    // iteration; RunSimulation always follows it with the shared sleep/wait section.
    void RunSimulationTick();

    // Simulation state
    std::unique_ptr<GameWorld> ownedWorld;
    GameWorld* world{nullptr};
    std::optional<HostSessionStartRequest> startRequest;
    FixedSimulationClock clock;
    std::uint64_t inputDelayTicks{1};
    std::vector<GameCommandResult> commandResults;

    // Transport (optional - nullptr for single player)
    std::shared_ptr<IGameTransport> transport;
    int remotePlayerId{0};
    bool requireRemoteSync{true};
    std::atomic<bool> hadConnection{false};
    bool initialSnapshotSent{false};
    int initialSnapshotFailureCount{0};
    bool remoteInitialSnapshotReady{false};
    bool remoteStartAcknowledged{false};
    std::chrono::steady_clock::time_point remoteSyncStartedAt{};
    bool hasLastSentSnapshot{false};
    GameSnapshot lastSentSnapshot;
    double checksumTimer{0.0};
    double correctionSnapshotCooldown{0.0};
    static constexpr std::size_t MaxRememberedRemoteCommands = 2048;
    std::set<std::uint64_t> pendingRemoteCommandIds;
    std::map<std::uint64_t, GameCommandResult> completedRemoteCommandResults;
    std::deque<std::uint64_t> completedRemoteCommandOrder;

    // Background simulation thread
    mutable std::recursive_mutex worldMutex;
    std::atomic<bool> running{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> gameplayActivated{false};
    std::thread worker;
    std::mutex sleepMutex;
    std::condition_variable cv;
    std::mutex snapshotMutex;
    GameSnapshot latestSnapshot;
    bool hasSnapshot{false};
    mutable std::mutex startupMutex;
    GameSessionStartupStatus startupStatus;
};

// Multiplayer client. Runtime instances own their world mirror and synchronize
// it on a session worker; the raw-pointer constructor remains for focused tests.
class ClientSession : public IGameSession
{
public:
    // Compatibility path used by focused tests and externally owned mirrors.
    // It remains foreground-driven by Update().
    ClientSession(GameWorld* observedWorld, std::shared_ptr<IGameTransport> transport, int assignedPlayerId = 0);
    // Runtime path: the session owns an empty mirror and performs network
    // receive, snapshot restore and catch-up on its own session worker.
    ClientSession(std::unique_ptr<GameWorld> observedWorld,
                  std::shared_ptr<IGameTransport> transport,
                  int assignedPlayerId = 0);
    ~ClientSession() override;

    std::uint64_t SubmitCommand(const GameCommand& command) override;
    void Update(double dt) override;
    GameWorld* GetWorld() override;
    bool ConsumeLatestSnapshot(GameSnapshot& snapshot) override;
    bool IsConnectionClosed() const override;
    int GetPingMs() const override;
    std::string GetConnectionStatus() const override;
    bool IsReadyForGameplay() const override;
    GameSessionStartupStatus GetStartupStatus() const override;
    void ActivateGameplay() override;
    std::recursive_mutex* GetWorldMutex() override;
    std::vector<GameCommandResult> ConsumeCommandResults() override;

private:
    static constexpr size_t MaxInitialSnapshotBytes =
        PersistenceLimits::MaxSerializedStateBytes;
    static constexpr size_t MaxInitialSnapshotChunks =
        (MaxInitialSnapshotBytes + SnapshotTransferLimits::DefaultChunkDataBytes - 1) /
        SnapshotTransferLimits::DefaultChunkDataBytes;
    void HandleSnapshotPayload(const std::string& payload);
    void HandleAuthoritativeResult(const GameCommandResult& result);
    void RunNetworkTick(double dt);
    void RunNetwork();
    void Stop();
    void ResetInitialSnapshotTransfer();
    void RetryOrFailInitialSync();
    void PublishStartupStatus(GameSessionStartupPhase phase, float progress,
                              std::string message, std::string error = {});

    std::unique_ptr<GameWorld> ownedObservedWorld;
    GameWorld* observedWorld{nullptr};
    std::shared_ptr<IGameTransport> transport;
    std::uint64_t nextClientCommandId{1};
    int assignedPlayerId{0};
    std::atomic<bool> hadConnection{false};
    bool wasConnected{false};
    GameSnapshot latestNetworkSnapshot;
    bool hasNetworkSnapshot{false};
    bool initialSnapshotReceived{false};
    std::string syncStatus{"Waiting for map sync"};
    size_t expectedInitialSnapshotBytes{0};
    size_t expectedInitialSnapshotChunks{0};
    size_t receivedInitialSnapshotChunks{0};
    size_t receivedInitialSnapshotBytes{0};
    std::uint64_t expectedInitialSnapshotTick{0};
    bool initialSnapshotFailed{false};
    int initialSnapshotRetryCount{0};
    std::string initialSnapshotBuffer;
    std::vector<std::string> initialSnapshotChunks;
    std::vector<bool> initialSnapshotChunkReceived;
    double resyncRequestCooldown{0.0};
    std::vector<GameCommandResult> commandResults;
    static constexpr std::size_t MaxObservedCommandResults = 4096;
    std::set<std::pair<int, std::uint64_t>> observedCommandResults;
    std::deque<std::pair<int, std::uint64_t>> observedCommandResultOrder;
    std::map<std::uint64_t, std::string> pendingClientCommandPayloads;
    bool backgroundWorker{false};
    std::atomic<bool> running{false};
    std::thread worker;
    mutable std::recursive_mutex worldMutex;
    std::mutex sleepMutex;
    std::condition_variable cv;
    std::chrono::steady_clock::time_point startupStartedAt{};
    mutable std::mutex startupMutex;
    GameSessionStartupStatus startupStatus{
        GameSessionStartupPhase::SynchronizingWorld,
        0.02f,
        "Waiting for host map data",
        {},
        false};
};

#endif
