#include "support/FaultInjectingGameTransport.h"

#include <algorithm>
#include <limits>
#include <utility>

FaultInjectingGameTransport::FaultInjectingGameTransport(FaultInjectionProfile profile)
    : profile(profile), jitterState(profile.jitterSeed)
{
}

void FaultInjectingGameTransport::SendClientCommand(const std::string& payload)
{
    std::lock_guard<std::mutex> lock(mutex);
    Schedule(clientCommands, payload);
}

std::vector<std::string> FaultInjectingGameTransport::ReceiveHostCommands()
{
    std::lock_guard<std::mutex> lock(mutex);
    return Drain(clientCommands);
}

void FaultInjectingGameTransport::SendHostResult(const std::string& payload)
{
    std::lock_guard<std::mutex> lock(mutex);
    Schedule(clientResults, payload);
}

std::vector<std::string> FaultInjectingGameTransport::ReceiveClientResults()
{
    std::lock_guard<std::mutex> lock(mutex);
    return Drain(clientResults);
}

void FaultInjectingGameTransport::SendHostFrame(const std::string& payload)
{
    std::lock_guard<std::mutex> lock(mutex);
    Schedule(clientFrames, payload);
}

std::vector<std::string> FaultInjectingGameTransport::ReceiveClientFrames()
{
    std::lock_guard<std::mutex> lock(mutex);
    return Drain(clientFrames);
}

void FaultInjectingGameTransport::SendHostSnapshot(const std::string& payload)
{
    std::lock_guard<std::mutex> lock(mutex);
    Schedule(clientSnapshots, payload);
}

std::vector<std::string> FaultInjectingGameTransport::ReceiveClientSnapshots()
{
    std::lock_guard<std::mutex> lock(mutex);
    return Drain(clientSnapshots);
}

std::string FaultInjectingGameTransport::GetStatus() const
{
    std::lock_guard<std::mutex> lock(mutex);
    if (failed.load())
        return failureReason.empty() ? "Fault transport failed" : failureReason;
    if (!connected.load())
        return "Fault transport disconnected";
    if (clientToHostStalled || hostToClientStalled)
        return "Fault transport stalled";
    return "Fault transport connected";
}

int FaultInjectingGameTransport::GetPingMs() const
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!connected.load())
        return -1;

    const std::uint64_t rtt = profile.clientToHostDelayMs + profile.hostToClientDelayMs;
    return rtt > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(rtt);
}

void FaultInjectingGameTransport::AdvanceTime(std::uint64_t elapsedMs)
{
    std::lock_guard<std::mutex> lock(mutex);
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    nowMs = elapsedMs > maximum - nowMs ? maximum : nowMs + elapsedMs;
}

std::uint64_t FaultInjectingGameTransport::GetTimeMs() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return nowMs;
}

void FaultInjectingGameTransport::SetDirectionStalled(TransportDirection direction, bool stalled)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (direction == TransportDirection::ClientToHost)
        clientToHostStalled = stalled;
    else
        hostToClientStalled = stalled;
}

bool FaultInjectingGameTransport::IsDirectionStalled(TransportDirection direction) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return IsStalledLocked(direction);
}

void FaultInjectingGameTransport::SetConnected(bool shouldConnect)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!shouldConnect)
        ClearChannels();
    connected.store(shouldConnect);
    if (shouldConnect)
    {
        failed.store(false);
        failureReason.clear();
    }
}

void FaultInjectingGameTransport::Fail(std::string reason)
{
    std::lock_guard<std::mutex> lock(mutex);
    ClearChannels();
    failureReason = std::move(reason);
    failed.store(true);
    connected.store(false);
}

std::size_t FaultInjectingGameTransport::GetPendingMessageCount() const
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto count = [](const ChannelState& channel)
    {
        return channel.scheduled.size() + channel.ready.size();
    };
    return count(clientCommands) + count(clientResults) + count(clientFrames) + count(clientSnapshots);
}

void FaultInjectingGameTransport::Schedule(ChannelState& channel, const std::string& payload)
{
    if (!connected.load() || failed.load())
        return;

    const std::int64_t jitter = NextSignedJitter(JitterFor(channel.direction));
    const std::uint64_t delay = DelayFor(channel.direction);
    std::uint64_t effectiveDelay = delay;
    if (jitter < 0)
    {
        const std::uint64_t reduction = static_cast<std::uint64_t>(-jitter);
        effectiveDelay = reduction >= delay ? 0 : delay - reduction;
    }
    else
    {
        const std::uint64_t addition = static_cast<std::uint64_t>(jitter);
        effectiveDelay = addition > std::numeric_limits<std::uint64_t>::max() - delay
            ? std::numeric_limits<std::uint64_t>::max()
            : delay + addition;
    }
    const std::uint64_t earliestDueAt = effectiveDelay > std::numeric_limits<std::uint64_t>::max() - nowMs
        ? std::numeric_limits<std::uint64_t>::max()
        : nowMs + effectiveDelay;
    const std::uint64_t dueAt = std::max(channel.lastDueAtMs, earliestDueAt);
    channel.lastDueAtMs = dueAt;
    channel.originalMessageCount++;
    channel.scheduled.push_back(ScheduledMessage{dueAt, payload});

    if (profile.duplicateEveryNthMessage != 0 &&
        channel.originalMessageCount % profile.duplicateEveryNthMessage == 0)
    {
        channel.scheduled.push_back(ScheduledMessage{dueAt, payload});
    }
}

std::vector<std::string> FaultInjectingGameTransport::Drain(ChannelState& channel)
{
    PromoteDue(channel);
    std::vector<std::string> result;
    result.reserve(channel.ready.size());
    while (!channel.ready.empty())
    {
        result.push_back(std::move(channel.ready.front()));
        channel.ready.pop_front();
    }
    return result;
}

void FaultInjectingGameTransport::PromoteDue(ChannelState& channel)
{
    if (!connected.load() || IsStalledLocked(channel.direction))
        return;

    while (!channel.scheduled.empty() && channel.scheduled.front().dueAtMs <= nowMs)
    {
        channel.ready.push_back(std::move(channel.scheduled.front().payload));
        channel.scheduled.pop_front();
    }
}

void FaultInjectingGameTransport::ClearChannels()
{
    const auto clear = [](ChannelState& channel)
    {
        channel.lastDueAtMs = 0;
        channel.originalMessageCount = 0;
        channel.scheduled.clear();
        channel.ready.clear();
    };
    clear(clientCommands);
    clear(clientResults);
    clear(clientFrames);
    clear(clientSnapshots);
}

std::uint64_t FaultInjectingGameTransport::DelayFor(TransportDirection direction) const
{
    return direction == TransportDirection::ClientToHost
        ? profile.clientToHostDelayMs
        : profile.hostToClientDelayMs;
}

std::uint64_t FaultInjectingGameTransport::JitterFor(TransportDirection direction) const
{
    return direction == TransportDirection::ClientToHost
        ? profile.clientToHostJitterMs
        : profile.hostToClientJitterMs;
}

std::int64_t FaultInjectingGameTransport::NextSignedJitter(std::uint64_t maximumJitterMs)
{
    if (maximumJitterMs == 0)
        return 0;

    constexpr std::uint64_t MaxSignedJitter =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() / 2);
    maximumJitterMs = std::min(maximumJitterMs, MaxSignedJitter);
    jitterState = jitterState * 1664525u + 1013904223u;
    const std::uint64_t range = maximumJitterMs * 2 + 1;
    const std::uint64_t sample = static_cast<std::uint64_t>(jitterState) % range;
    return static_cast<std::int64_t>(sample) - static_cast<std::int64_t>(maximumJitterMs);
}

bool FaultInjectingGameTransport::IsStalledLocked(TransportDirection direction) const
{
    return direction == TransportDirection::ClientToHost ? clientToHostStalled : hostToClientStalled;
}
