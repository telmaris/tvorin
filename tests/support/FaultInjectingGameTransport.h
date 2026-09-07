#ifndef FAULT_INJECTING_GAME_TRANSPORT_H
#define FAULT_INJECTING_GAME_TRANSPORT_H

#include "core/GameSession.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

// Deterministic, manually-clocked transport for multiplayer tests. It models
// delay, jitter, stalls, duplicate application messages and reconnects without
// using wall-clock sleeps or sockets.
enum class TransportDirection
{
    ClientToHost,
    HostToClient
};

struct FaultInjectionProfile
{
    std::uint64_t clientToHostDelayMs{0};
    std::uint64_t hostToClientDelayMs{0};
    std::uint64_t clientToHostJitterMs{0};
    std::uint64_t hostToClientJitterMs{0};
    std::uint32_t jitterSeed{0xC0DEC0DEu};

    // A value of 0 disables duplication. Otherwise every Nth original message
    // on each logical channel is delivered a second time immediately after it.
    std::size_t duplicateEveryNthMessage{0};
};

class FaultInjectingGameTransport final : public IGameTransport
{
public:
    explicit FaultInjectingGameTransport(FaultInjectionProfile profile = {});

    void SendClientCommand(const std::string& payload) override;
    std::vector<std::string> ReceiveHostCommands() override;
    void SendHostResult(const std::string& payload) override;
    std::vector<std::string> ReceiveClientResults() override;
    void SendHostFrame(const std::string& payload) override;
    std::vector<std::string> ReceiveClientFrames() override;
    void SendHostSnapshot(const std::string& payload) override;
    std::vector<std::string> ReceiveClientSnapshots() override;

    bool IsConnected() const override { return connected.load(); }
    bool HasFailed() const override { return failed.load(); }
    std::string GetStatus() const override;
    int GetPingMs() const override;

    // Advances the deterministic network clock. Messages become receivable only
    // when their due time is reached and their direction is not stalled.
    void AdvanceTime(std::uint64_t elapsedMs);
    std::uint64_t GetTimeMs() const;

    void SetDirectionStalled(TransportDirection direction, bool stalled);
    bool IsDirectionStalled(TransportDirection direction) const;

    // A disconnect deliberately discards every in-flight message. A later
    // SetConnected(true) represents a new physical connection, so reconnect
    // logic must explicitly replay/resync what it needs.
    void SetConnected(bool shouldConnect);
    void Fail(std::string reason);

    std::size_t GetPendingMessageCount() const;

private:
    struct ScheduledMessage
    {
        std::uint64_t dueAtMs{0};
        std::string payload;
    };

    struct ChannelState
    {
        explicit ChannelState(TransportDirection direction) : direction(direction) {}

        TransportDirection direction;
        std::uint64_t lastDueAtMs{0};
        std::size_t originalMessageCount{0};
        std::deque<ScheduledMessage> scheduled;
        std::deque<std::string> ready;
    };

    void Schedule(ChannelState& channel, const std::string& payload);
    std::vector<std::string> Drain(ChannelState& channel);
    void PromoteDue(ChannelState& channel);
    void ClearChannels();
    std::uint64_t DelayFor(TransportDirection direction) const;
    std::uint64_t JitterFor(TransportDirection direction) const;
    std::int64_t NextSignedJitter(std::uint64_t maximumJitterMs);
    bool IsStalledLocked(TransportDirection direction) const;

    mutable std::mutex mutex;
    FaultInjectionProfile profile;
    std::uint64_t nowMs{0};
    std::uint32_t jitterState{0xC0DEC0DEu};
    bool clientToHostStalled{false};
    bool hostToClientStalled{false};
    ChannelState clientCommands{TransportDirection::ClientToHost};
    ChannelState clientResults{TransportDirection::HostToClient};
    ChannelState clientFrames{TransportDirection::HostToClient};
    ChannelState clientSnapshots{TransportDirection::HostToClient};
    std::atomic<bool> connected{true};
    std::atomic<bool> failed{false};
    std::string failureReason;
};

#endif
