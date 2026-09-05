#include "warfare/BattleUnit.h"
#include "warfare/UnitDefinition.h"
#include "economy/Player.h"

BattleUnit::BattleUnit(int instanceId, int ownerPlayerId, std::string unitDefId)
    : instanceId(instanceId), ownerPlayerId(ownerPlayerId), unitDefId(std::move(unitDefId))
{
}

double BattleUnit::GetEffectiveMaxHp(const Player& owner) const
{
    const UnitDefinition* def = FindUnitDefinition(unitDefId);
    return def != nullptr ? owner.ModifyBalanceForUnit(BalanceStat::UnitHp, def->maxHp, unitDefId) : 0.0;
}

double BattleUnit::GetEffectiveFieldAttack(const Player& owner) const
{
    const UnitDefinition* def = FindUnitDefinition(unitDefId);
    return def != nullptr ? owner.ModifyBalanceForUnit(BalanceStat::UnitFieldAttack, def->fieldAttack, unitDefId) : 0.0;
}

double BattleUnit::GetEffectiveSiegePower(const Player& owner) const
{
    const UnitDefinition* def = FindUnitDefinition(unitDefId);
    return def != nullptr ? owner.ModifyBalanceForUnit(BalanceStat::UnitSiegePower, def->siegePower, unitDefId) : 0.0;
}

double BattleUnit::GetEffectiveArmor(const Player& owner) const
{
    const UnitDefinition* def = FindUnitDefinition(unitDefId);
    return def != nullptr ? owner.ModifyBalanceForUnit(BalanceStat::UnitArmor, def->armor, unitDefId) : 0.0;
}

double BattleUnit::GetEffectiveMoveSpeed(const Player& owner) const
{
    const UnitDefinition* def = FindUnitDefinition(unitDefId);
    return def != nullptr ? owner.ModifyBalanceForUnit(BalanceStat::UnitMoveSpeed, def->moveSpeed, unitDefId) : 0.0;
}

double BattleUnit::GetEffectiveAttackSpeed(const Player& owner) const
{
    const UnitDefinition* def = FindUnitDefinition(unitDefId);
    return def != nullptr ? owner.ModifyBalanceForUnit(BalanceStat::UnitAttackSpeed, def->attackSpeed, unitDefId) : 0.0;
}

void UnitRoster::AddUnit(BattleUnit unit)
{
    units[unit.instanceId] = std::move(unit);
}

std::optional<BattleUnit> UnitRoster::RemoveUnit(int instanceId)
{
    auto it = units.find(instanceId);
    if (it == units.end())
        return std::nullopt;
    BattleUnit unit = std::move(it->second);
    units.erase(it);
    return unit;
}

BattleUnit* UnitRoster::FindUnit(int instanceId)
{
    auto it = units.find(instanceId);
    return it != units.end() ? &it->second : nullptr;
}

const BattleUnit* UnitRoster::FindUnit(int instanceId) const
{
    auto it = units.find(instanceId);
    return it != units.end() ? &it->second : nullptr;
}
