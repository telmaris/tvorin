#include "warfare/TaskGroup.h"

#include <limits>
#include <set>

TaskGroup* TaskGroupRegistry::Find(TaskGroupId id)
{
    const auto it = groups.find(id);
    return it == groups.end() ? nullptr : &it->second;
}

const TaskGroup* TaskGroupRegistry::Find(TaskGroupId id) const
{
    const auto it = groups.find(id);
    return it == groups.end() ? nullptr : &it->second;
}

bool TaskGroupRegistry::Create(ProvinceId provinceId, int barracksId, TaskGroupId& createdId)
{
    createdId = InvalidTaskGroupId;
    if (provinceId == InvalidProvinceId || barracksId <= 0 ||
        nextTaskGroupId == InvalidTaskGroupId || groups.size() >= MaxTaskGroupsPerPlayer)
        return false;
    const TaskGroupId id = nextTaskGroupId;
    if (!groups.emplace(id, TaskGroup{id, provinceId, barracksId}).second)
        return false;
    nextTaskGroupId = id == std::numeric_limits<TaskGroupId>::max()
        ? InvalidTaskGroupId : id + 1;
    createdId = id;
    return true;
}

bool TaskGroupRegistry::Erase(TaskGroupId id)
{
    return id != InvalidTaskGroupId && groups.erase(id) == 1;
}

bool TaskGroupRegistry::Relocate(TaskGroupId id, ProvinceId stationProvinceId,
                                 int homeBarracksBuildingId)
{
    TaskGroup* group = Find(id);
    if (group == nullptr || stationProvinceId == InvalidProvinceId || homeBarracksBuildingId <= 0)
        return false;
    group->stationProvinceId = stationProvinceId;
    group->homeBarracksBuildingId = homeBarracksBuildingId;
    return true;
}

bool TaskGroupRegistry::Restore(TaskGroupId nextId, std::map<TaskGroupId, TaskGroup> restored)
{
    if (nextId == InvalidTaskGroupId || restored.size() > MaxTaskGroupsPerPlayer ||
        (!restored.empty() && nextId <= restored.rbegin()->first))
        return false;
    for (const auto& [id, group] : restored)
        if (id == InvalidTaskGroupId || group.id != id ||
            group.stationProvinceId == InvalidProvinceId || group.homeBarracksBuildingId <= 0)
            return false;
    groups = std::move(restored);
    nextTaskGroupId = nextId;
    return true;
}

void TaskGroupRegistry::Clear()
{
    nextTaskGroupId = 1;
    groups.clear();
}

namespace
{
    bool ValidIds(const std::vector<int>& ids)
    {
        if (ids.empty() || ids.size() > TaskGroupRegistry::MaxUnitsPerTaskGroup)
            return false;
        std::set<int> unique(ids.begin(), ids.end());
        return unique.size() == ids.size() && *unique.begin() > 0;
    }

    bool EditableReserve(const BattleUnit& unit, const TaskGroup& group)
    {
        return unit.taskGroupId != InvalidTaskGroupId &&
               UnitAssignmentService::IsInReserve(unit, group.stationProvinceId) &&
               unit.assignment.buildingId == group.homeBarracksBuildingId;
    }
}

bool TaskGroupService::AddUnits(TaskGroupRegistry& registry, TaskGroupId id, PlayerId playerId,
                                const std::vector<int>& ids, UnitRoster& roster,
                                std::string& reason)
{
    const TaskGroup* group = registry.Find(id);
    if (playerId == InvalidPlayerId || group == nullptr || !ValidIds(ids))
    {
        reason = "invalid task group or unit list";
        return false;
    }
    std::size_t current = 0;
    for (const auto& [unitId, unit] : roster.units)
        if (unit.taskGroupId == id)
            ++current;
    if (current > TaskGroupRegistry::MaxUnitsPerTaskGroup - ids.size())
    {
        reason = "task group unit limit exceeded";
        return false;
    }
    std::vector<BattleUnit*> selected;
    for (const int unitId : ids)
    {
        BattleUnit* unit = roster.FindUnit(unitId);
        if (unit == nullptr || unit->ownerPlayerId != playerId ||
            unit->taskGroupId != InvalidTaskGroupId ||
            !UnitAssignmentService::IsInReserve(*unit, group->stationProvinceId) ||
            unit->assignment.buildingId != group->homeBarracksBuildingId)
        {
            reason = "all units must be unassigned in the group's Barracks reserve";
            return false;
        }
        selected.push_back(unit);
    }
    for (BattleUnit* unit : selected)
        unit->taskGroupId = id;
    return true;
}

bool TaskGroupService::RemoveUnits(TaskGroupRegistry& registry, TaskGroupId id, PlayerId playerId,
                                   const std::vector<int>& ids, UnitRoster& roster,
                                   std::string& reason)
{
    const TaskGroup* group = registry.Find(id);
    if (playerId == InvalidPlayerId || group == nullptr || !ValidIds(ids))
    {
        reason = "invalid task group or unit list";
        return false;
    }
    std::vector<BattleUnit*> selected;
    for (const int unitId : ids)
    {
        BattleUnit* unit = roster.FindUnit(unitId);
        if (unit == nullptr || unit->ownerPlayerId != playerId ||
            unit->taskGroupId != id || !EditableReserve(*unit, *group))
        {
            reason = "task group can only be edited from its home reserve";
            return false;
        }
        selected.push_back(unit);
    }
    for (BattleUnit* unit : selected)
        unit->taskGroupId = InvalidTaskGroupId;
    return true;
}

bool TaskGroupService::Disband(TaskGroupRegistry& registry, TaskGroupId id, PlayerId playerId,
                               UnitRoster& roster, std::string& reason)
{
    const TaskGroup* group = registry.Find(id);
    if (playerId == InvalidPlayerId || group == nullptr)
    {
        reason = "task group does not exist";
        return false;
    }
    for (const auto& [unitId, unit] : roster.units)
        if (unit.taskGroupId == id && !EditableReserve(unit, *group))
        {
            reason = "deployed task group cannot be disbanded";
            return false;
        }
    for (auto& [unitId, unit] : roster.units)
        if (unit.taskGroupId == id)
            unit.taskGroupId = InvalidTaskGroupId;
    return registry.Erase(id);
}

TaskGroupStatus TaskGroupService::ResolveStatus(const TaskGroup& group,
                                                const UnitRoster& roster, int* count)
{
    TaskGroupStatus result = TaskGroupStatus::Empty;
    int members = 0;
    for (const auto& [unitId, unit] : roster.units)
    {
        if (unit.taskGroupId != group.id)
            continue;
        ++members;
        TaskGroupStatus current = TaskGroupStatus::Mixed;
        switch (unit.assignment.kind)
        {
            case UnitAssignmentKind::BarracksReserve: current = TaskGroupStatus::Reserve; break;
            case UnitAssignmentKind::DefensiveGarrison: current = TaskGroupStatus::Garrison; break;
            case UnitAssignmentKind::Journey: current = TaskGroupStatus::Journey; break;
            case UnitAssignmentKind::Battle: current = TaskGroupStatus::Battle; break;
            case UnitAssignmentKind::Unassigned: break;
        }
        if (result == TaskGroupStatus::Empty)
            result = current;
        else if (result != current)
            result = TaskGroupStatus::Mixed;
    }
    if (count != nullptr)
        *count = members;
    return result;
}

std::vector<TaskGroupView> TaskGroupService::BuildViews(const TaskGroupRegistry& registry,
                                                        PlayerId playerId,
                                                        const UnitRoster& roster)
{
    (void)playerId;
    std::map<TaskGroupId, TaskGroupView> views;
    for (const auto& [id, group] : registry.GetGroups())
        views.emplace(id, TaskGroupView{id, group.stationProvinceId,
                                        group.homeBarracksBuildingId});
    for (const auto& [unitId, unit] : roster.units)
    {
        const auto it = views.find(unit.taskGroupId);
        if (it == views.end())
            continue;
        auto& view = it->second;
        ++view.total;
        ++view.alive;
        ++view.unitCounts[unit.unitDefId];
        if (unit.assignment.kind == UnitAssignmentKind::Journey)
            view.journeyId = unit.assignment.worldJourneyId;
        else if (unit.assignment.kind == UnitAssignmentKind::Battle)
            view.battleId = unit.assignment.battleId;
        else if (unit.assignment.kind == UnitAssignmentKind::DefensiveGarrison)
            view.garrisonBuildingId = unit.assignment.buildingId;
    }
    std::vector<TaskGroupView> result;
    result.reserve(views.size());
    for (auto& [id, view] : views)
    {
        const TaskGroup* group = registry.Find(id);
        view.status = ResolveStatus(*group, roster);
        view.editable = view.status == TaskGroupStatus::Empty || view.status == TaskGroupStatus::Reserve;
        for (const auto& [unitId, unit] : roster.units)
            if (unit.taskGroupId == id && !EditableReserve(unit, *group))
                view.editable = false;
        result.push_back(std::move(view));
    }
    return result;
}
