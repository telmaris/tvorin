#include "world/PeriodicEventScheduler.h"

#include <algorithm>
#include <limits>

PeriodicEventScheduler::PeriodicEventScheduler(PeriodicEventScheduleDefinition value)
    : schedule(value.IsValid() ? value : PeriodicEventScheduleDefinition{})
{
}

void PeriodicEventScheduler::SetSchedule(PeriodicEventScheduleDefinition value)
{
    if (!value.IsValid())
        return;
    schedule = value;
}

std::uint64_t PeriodicEventScheduler::AddSaturating(std::uint64_t first,
                                                   std::uint64_t second)
{
    if (second > std::numeric_limits<std::uint64_t>::max() - first)
        return std::numeric_limits<std::uint64_t>::max();
    return first + second;
}

std::optional<std::uint64_t> PeriodicEventScheduler::TakeDueAttempt(
    PlayerId playerId, std::uint64_t currentTick)
{
    if (playerId == InvalidPlayerId)
        return std::nullopt;

    auto [it, inserted] = states.try_emplace(playerId);
    if (inserted)
        it->second.nextCheckTick = schedule.initialDelayTicks;

    auto& state = it->second;
    if (currentTick < state.nextCheckTick || currentTick < state.nextAllowedEventTick)
        return std::nullopt;

    if (state.attemptCounter == std::numeric_limits<std::uint64_t>::max())
        return std::nullopt;
    ++state.attemptCounter;
    state.nextCheckTick = AddSaturating(currentTick, schedule.checkIntervalTicks);
    return state.attemptCounter;
}

bool PeriodicEventScheduler::IsDefinitionOnCooldown(
    PlayerId playerId, std::string_view definitionId, std::uint64_t currentTick) const
{
    const auto stateIt = states.find(playerId);
    if (stateIt == states.end())
        return false;
    const auto cooldownIt = stateIt->second.definitionCooldownUntil.find(
        std::string(definitionId));
    return cooldownIt != stateIt->second.definitionCooldownUntil.end() &&
           currentTick < cooldownIt->second;
}

void PeriodicEventScheduler::RecordSuccess(PlayerId playerId, std::string_view definitionId,
                                            std::uint64_t repeatCooldownTicks,
                                            std::uint64_t currentTick)
{
    auto [it, inserted] = states.try_emplace(playerId);
    if (inserted)
        it->second.nextCheckTick = schedule.initialDelayTicks;

    auto& state = it->second;
    state.nextAllowedEventTick = AddSaturating(currentTick, schedule.minimumGapTicks);
    if (!definitionId.empty() && repeatCooldownTicks > 0)
        state.definitionCooldownUntil[std::string(definitionId)] =
            AddSaturating(currentTick, repeatCooldownTicks);
}

bool PeriodicEventScheduler::RestoreState(PlayerId playerId,
                                           PeriodicEventSchedulerState state)
{
    if (playerId == InvalidPlayerId || state.definitionCooldownUntil.size() >
                                      PersistenceLimits::MaxBufferEntries)
        return false;
    for (const auto& [definitionId, tick] : state.definitionCooldownUntil)
        if (definitionId.empty() || definitionId.size() > PersistenceLimits::MaxStringBytes ||
            tick < state.nextAllowedEventTick)
            return false;
    states[playerId] = std::move(state);
    return true;
}

void PeriodicEventScheduler::Clear()
{
    states.clear();
}
