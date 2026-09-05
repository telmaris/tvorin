#ifndef WARFARE_TASK_GROUP_H
#define WARFARE_TASK_GROUP_H

#include "core/GameCommand.h"
#include "core/PersistenceLimits.h"
#include "warfare/BattleUnit.h"
#include "warfare/TaskGroupIds.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct TaskGroup
{
    TaskGroupId id{InvalidTaskGroupId};
    ProvinceId stationProvinceId{InvalidProvinceId};
    int homeBarracksBuildingId{0};
};

enum class TaskGroupStatus : std::uint8_t
{
    Empty,
    Reserve,
    Garrison,
    Journey,
    Battle,
    Mixed
};

struct TaskGroupView
{
    TaskGroupId id{InvalidTaskGroupId};
    ProvinceId stationProvinceId{InvalidProvinceId};
    int homeBarracksBuildingId{0};
    TaskGroupStatus status{TaskGroupStatus::Empty};
    std::map<std::string, int> unitCounts;
    int total{0};
    int alive{0};
    bool editable{false};
    WorldJourneyId journeyId{InvalidWorldJourneyId};
    BattleId battleId{InvalidBattleId};
    int garrisonBuildingId{0};
};

class TaskGroupRegistry
{
public:
    static constexpr std::size_t MaxUnitsPerTaskGroup = GameCommand::MaxUnitInstanceIds;
    static constexpr std::size_t MaxTaskGroupsPerPlayer =
        PersistenceLimits::MaxGlobalProvinces * 4;

    TaskGroupId GetNextTaskGroupId() const { return nextTaskGroupId; }
    const std::map<TaskGroupId, TaskGroup>& GetGroups() const { return groups; }
    TaskGroup* Find(TaskGroupId id);
    const TaskGroup* Find(TaskGroupId id) const;
    bool Create(ProvinceId stationProvinceId, int homeBarracksBuildingId,
                TaskGroupId& createdId);
    bool Erase(TaskGroupId id);
    bool Relocate(TaskGroupId id, ProvinceId stationProvinceId, int homeBarracksBuildingId);
    bool Restore(TaskGroupId nextId, std::map<TaskGroupId, TaskGroup> restored);
    void Clear();

private:
    TaskGroupId nextTaskGroupId{1};
    std::map<TaskGroupId, TaskGroup> groups;
};

class TaskGroupService
{
public:
    static bool AddUnits(TaskGroupRegistry&, TaskGroupId, PlayerId,
                         const std::vector<int>&, UnitRoster&, std::string&);
    static bool RemoveUnits(TaskGroupRegistry&, TaskGroupId, PlayerId,
                            const std::vector<int>&, UnitRoster&, std::string&);
    static bool Disband(TaskGroupRegistry&, TaskGroupId, PlayerId,
                        UnitRoster&, std::string&);
    static std::vector<TaskGroupView> BuildViews(const TaskGroupRegistry&,
                                                  PlayerId, const UnitRoster&);
    static TaskGroupStatus ResolveStatus(const TaskGroup&, const UnitRoster&, int* memberCount = nullptr);
};

#endif
