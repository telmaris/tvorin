#include "warfare/BattleUnit.h"

#include <algorithm>

UnitAssignment UnitAssignment::Reserve(ProvinceId provinceId, int buildingId)
{
    UnitAssignment result;
    result.kind = UnitAssignmentKind::BarracksReserve;
    result.provinceId = provinceId;
    result.buildingId = buildingId;
    return result;
}

UnitAssignment UnitAssignment::Garrison(ProvinceId provinceId, int buildingId)
{
    UnitAssignment result = Reserve(provinceId, buildingId);
    result.kind = UnitAssignmentKind::DefensiveGarrison;
    return result;
}

UnitAssignment UnitAssignment::Journey(ProvinceId provinceId, WorldJourneyId journeyId,
                                       int originBarracksId)
{
    UnitAssignment result;
    result.kind = UnitAssignmentKind::Journey;
    result.provinceId = provinceId;
    result.buildingId = originBarracksId;
    result.worldJourneyId = journeyId;
    return result;
}

UnitAssignment UnitAssignment::Battle(ProvinceId provinceId, BattleId battleId,
                                      int originBarracksId)
{
    UnitAssignment result;
    result.kind = UnitAssignmentKind::Battle;
    result.provinceId = provinceId;
    result.buildingId = originBarracksId;
    result.battleId = battleId;
    return result;
}

bool UnitAssignment::IsStructurallyValid() const
{
    if (kind == UnitAssignmentKind::Unassigned)
        return provinceId == InvalidProvinceId && buildingId == 0 &&
               worldJourneyId == InvalidWorldJourneyId && battleId == InvalidBattleId;
    if (provinceId == InvalidProvinceId)
        return false;
    switch (kind)
    {
        case UnitAssignmentKind::BarracksReserve:
        case UnitAssignmentKind::DefensiveGarrison:
            return buildingId > 0 && worldJourneyId == InvalidWorldJourneyId &&
                   battleId == InvalidBattleId;
        case UnitAssignmentKind::Journey:
            return worldJourneyId != InvalidWorldJourneyId &&
                   battleId == InvalidBattleId;
        case UnitAssignmentKind::Battle:
            return battleId != InvalidBattleId &&
                   worldJourneyId == InvalidWorldJourneyId;
        case UnitAssignmentKind::Unassigned:
            return false;
    }
    return false;
}

namespace
{
    bool ReplaceAssignment(BattleUnit& unit, UnitAssignment value)
    {
        if (!value.IsStructurallyValid())
            return false;
        unit.assignment = value;
        return true;
    }
}

bool UnitAssignmentService::AssignReserve(BattleUnit& unit, ProvinceId provinceId,
                                           int barracksBuildingId)
{
    return ReplaceAssignment(unit, UnitAssignment::Reserve(provinceId, barracksBuildingId));
}

bool UnitAssignmentService::AssignGarrison(BattleUnit& unit, ProvinceId provinceId,
                                           int buildingId)
{
    return ReplaceAssignment(unit, UnitAssignment::Garrison(provinceId, buildingId));
}

bool UnitAssignmentService::AssignJourney(BattleUnit& unit, ProvinceId provinceId,
                                          WorldJourneyId journeyId,
                                          int originBarracksId)
{
    return ReplaceAssignment(unit, UnitAssignment::Journey(provinceId, journeyId,
                                                           originBarracksId));
}

bool UnitAssignmentService::AssignBattle(BattleUnit& unit, ProvinceId provinceId,
                                          BattleId battleId, int originBarracksId)
{
    return ReplaceAssignment(unit, UnitAssignment::Battle(provinceId, battleId,
                                                          originBarracksId));
}

bool UnitAssignmentService::IsInReserve(const BattleUnit& unit, ProvinceId provinceId)
{
    return unit.assignment.kind == UnitAssignmentKind::BarracksReserve &&
           unit.assignment.provinceId == provinceId;
}

bool UnitAssignmentService::IsAvailableFromReserve(const BattleUnit& unit,
                                                   ProvinceId provinceId)
{
    return IsInReserve(unit, provinceId) && unit.assignment.buildingId >= 0 &&
           unit.assignment.worldJourneyId == InvalidWorldJourneyId &&
           unit.assignment.battleId == InvalidBattleId;
}

bool UnitAssignmentService::IsOnJourney(const BattleUnit& unit, WorldJourneyId journeyId)
{
    return unit.assignment.kind == UnitAssignmentKind::Journey &&
           unit.assignment.worldJourneyId == journeyId;
}

bool UnitAssignmentService::IsInBattle(const BattleUnit& unit, BattleId battleId)
{
    return unit.assignment.kind == UnitAssignmentKind::Battle &&
           unit.assignment.battleId == battleId;
}
