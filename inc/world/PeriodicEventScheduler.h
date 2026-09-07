#ifndef WORLD_PERIODIC_EVENT_SCHEDULER_H
#define WORLD_PERIODIC_EVENT_SCHEDULER_H

#include "core/PersistenceLimits.h"
#include "world/WorldIds.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

struct PeriodicEventScheduleDefinition
{
    std::uint64_t initialDelayTicks{6000};
    std::uint64_t checkIntervalTicks{3000};
    int occurrenceChanceBasisPoints{2500};
    std::uint64_t minimumGapTicks{6000};

    bool IsValid() const
    {
        return initialDelayTicks > 0 && checkIntervalTicks > 0 &&
               occurrenceChanceBasisPoints >= 0 && occurrenceChanceBasisPoints <= 10000 &&
               minimumGapTicks > 0;
    }
};

struct PeriodicEventSchedulerState
{
    std::uint64_t nextCheckTick{0};
    std::uint64_t nextAllowedEventTick{0};
    std::uint64_t attemptCounter{0};
    std::map<std::string, std::uint64_t> definitionCooldownUntil;
};

class PeriodicEventScheduler
{
public:
    explicit PeriodicEventScheduler(PeriodicEventScheduleDefinition schedule = {});

    const PeriodicEventScheduleDefinition& GetSchedule() const { return schedule; }
    void SetSchedule(PeriodicEventScheduleDefinition value);

    // Returns the monotonically increasing attempt counter only when the
    // player has one due attempt. Creating the state is deliberately lazy so
    // players without an owned buildable province never enter the save.
    std::optional<std::uint64_t> TakeDueAttempt(PlayerId playerId,
                                                std::uint64_t currentTick);
    bool IsDefinitionOnCooldown(PlayerId playerId, std::string_view definitionId,
                                std::uint64_t currentTick) const;
    void RecordSuccess(PlayerId playerId, std::string_view definitionId,
                       std::uint64_t repeatCooldownTicks,
                       std::uint64_t currentTick);

    const std::map<PlayerId, PeriodicEventSchedulerState>& GetStates() const
    {
        return states;
    }

    bool RestoreState(PlayerId playerId, PeriodicEventSchedulerState state);
    void Clear();

private:
    static std::uint64_t AddSaturating(std::uint64_t first, std::uint64_t second);

    PeriodicEventScheduleDefinition schedule;
    std::map<PlayerId, PeriodicEventSchedulerState> states;
};

#endif
