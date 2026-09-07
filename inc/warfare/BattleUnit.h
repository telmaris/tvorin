#ifndef BATTLE_UNIT_H
#define BATTLE_UNIT_H

#include "economy/BalanceStats.h"
#include "world/WorldIds.h"
#include "warfare/TaskGroupIds.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

class Player;

enum class UnitAssignmentKind : std::uint8_t
{
    BarracksReserve,
    DefensiveGarrison,
    Journey,
    Battle,
    Unassigned
};

struct UnitAssignment
{
    UnitAssignmentKind kind{UnitAssignmentKind::Unassigned};
    ProvinceId provinceId{InvalidProvinceId};
    int buildingId{0};
    WorldJourneyId worldJourneyId{InvalidWorldJourneyId};
    BattleId battleId{InvalidBattleId};

    static UnitAssignment Reserve(ProvinceId provinceId, int buildingId);
    static UnitAssignment Garrison(ProvinceId provinceId, int buildingId);
    static UnitAssignment Journey(ProvinceId provinceId, WorldJourneyId journeyId,
                                  int originBarracksId = 0);
    static UnitAssignment Battle(ProvinceId provinceId, BattleId battleId,
                                 int originBarracksId = 0);
    bool IsStructurallyValid() const;
};

// One recruited unit instance. Composition over inheritance: a single
// BattleUnit class plus a UnitDefinition id — no Swordsman : BattleUnit
// hierarchy. Effective stats are resolved from the definition + the owning
// player's BalanceModifierSet at query time (never cached on the instance),
// so a tech-tree/focus buff affects units already present in the roster.
class BattleUnit
{
public:
    BattleUnit() = default;
    BattleUnit(int instanceId, int ownerPlayerId, std::string unitDefId);

    double GetEffectiveMaxHp(const Player& owner) const;
    double GetEffectiveFieldAttack(const Player& owner) const;
    double GetEffectiveSiegePower(const Player& owner) const;
    double GetEffectiveArmor(const Player& owner) const;
    double GetEffectiveMoveSpeed(const Player& owner) const;
    double GetEffectiveAttackSpeed(const Player& owner) const;

    int instanceId{0};
    int ownerPlayerId{-1};
    std::string unitDefId;
    TaskGroupId taskGroupId{InvalidTaskGroupId};
    // Canonical location. It can only be changed through UnitAssignmentService.
    UnitAssignment assignment;
};

class UnitAssignmentService
{
public:
    static bool AssignReserve(BattleUnit& unit, ProvinceId provinceId, int barracksBuildingId);
    static bool AssignGarrison(BattleUnit& unit, ProvinceId provinceId, int buildingId);
    static bool AssignJourney(BattleUnit& unit, ProvinceId provinceId,
                              WorldJourneyId journeyId, int originBarracksId = 0);
    static bool AssignBattle(BattleUnit& unit, ProvinceId provinceId, BattleId battleId,
                             int originBarracksId = 0);
    static bool IsInReserve(const BattleUnit& unit, ProvinceId provinceId);
    static bool IsAvailableFromReserve(const BattleUnit& unit, ProvinceId provinceId);
    static bool IsOnJourney(const BattleUnit& unit, WorldJourneyId journeyId);
    static bool IsInBattle(const BattleUnit& unit, BattleId battleId);
};

// Per-player pool of recruited BattleUnit instances.
// Deterministic iteration order (std::map keyed by instanceId).
class UnitRoster
{
public:
    void AddUnit(BattleUnit unit);
    // Removes a unit from the roster. Returns it by value, or std::nullopt if
    // no such instance is in the roster.
    std::optional<BattleUnit> RemoveUnit(int instanceId);
    BattleUnit* FindUnit(int instanceId);
    const BattleUnit* FindUnit(int instanceId) const;

    std::map<int, BattleUnit> units;
};

#endif
