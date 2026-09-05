#include "warfare/GarrisonService.h"

#include "core/GameCommand.h"
#include "economy/Building.h"
#include "economy/BuildingComponents.h"
#include "economy/Player.h"
#include "world/ProvinceSimulation.h"
#include "warfare/BattleUnit.h"
#include "warfare/UnitDefinition.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace
{
    int CountIncomingResources(const Building& target, ResourceType type)
    {
        if (target.provinceEconomy == nullptr)
            return 0;

        int incoming = 0;
        for (Building* carrier : target.provinceEconomy->dataTracker.buildings)
        {
            if (carrier == nullptr)
                continue;
            for (Transportable* transport : carrier->transportables)
            {
                const auto* resource = dynamic_cast<const Resource*>(transport);
                if (resource != nullptr && resource->targetBuilding == &target && resource->type == type)
                    ++incoming;
            }
        }
        return incoming;
    }

    bool ValidateUnitIds(const Player& player, const std::vector<int>& unitIds,
                         std::set<int>& unique, std::string& failureReason)
    {
        if (unitIds.empty() || unitIds.size() > GameCommand::MaxUnitInstanceIds)
        {
            failureReason = "invalid unit count";
            return false;
        }
        for (int id : unitIds)
        {
            if (id <= 0 || !unique.insert(id).second || player.roster.FindUnit(id) == nullptr)
            {
                failureReason = "unknown or duplicate unit";
                return false;
            }
        }
        return true;
    }
}

Building* GarrisonService::FindBuilding(ProvinceEconomy& province, int buildingId)
{
    for (Building* building : province.dataTracker.buildings)
        if (building != nullptr && building->id == buildingId)
            return building;
    return nullptr;
}

bool GarrisonService::AssignUnitsToGarrison(Player& player, ProvinceEconomy& province,
                                             int sourceBarracksId, int defenseBuildingId,
                                             const std::vector<int>& unitIds,
                                             std::string& failureReason)
{
    Building* source = FindBuilding(province, sourceBarracksId);
    Building* target = FindBuilding(province, defenseBuildingId);
    auto* garrison = target != nullptr ? target->GetComponent<GarrisonComponent>() : nullptr;
    if (source == nullptr || source->owner != &player || source->buildingType != BuildingType::Barracks ||
        source->IsUnderConstruction() || target == nullptr || target->owner != &player ||
        target->IsUnderConstruction() || garrison == nullptr || source == target)
    {
        failureReason = "invalid barracks or garrison";
        return false;
    }

    std::set<int> unique;
    if (!ValidateUnitIds(player, unitIds, unique, failureReason))
        return false;

    const int used = static_cast<int>(GetGarrisonedUnitIds(player, province.provinceId, target->id).size());
    if (used > garrison->GetEffectiveCapacity(*target) - static_cast<int>(unitIds.size()))
    {
        failureReason = "garrison capacity exceeded";
        return false;
    }

    for (int id : unitIds)
    {
        BattleUnit* unit = player.roster.FindUnit(id);
        if (unit == nullptr || unit->ownerPlayerId != player.id ||
            unit->assignment.provinceId != province.provinceId ||
            unit->assignment.kind != UnitAssignmentKind::BarracksReserve ||
            unit->assignment.buildingId != source->id ||
            !UnitAssignmentService::IsAvailableFromReserve(*unit, province.provinceId))
        {
            failureReason = "unit is not available in source barracks reserve";
            return false;
        }
    }

    // All validation is complete before the first assignment changes state.
    for (int id : unitIds)
        if (!UnitAssignmentService::AssignGarrison(*player.roster.FindUnit(id), province.provinceId, target->id))
        {
            failureReason = "assignment transition failed";
            return false;
        }
    return true;
}

bool GarrisonService::ReturnUnitsToBarracks(Player& player, ProvinceEconomy& province,
                                            int defenseBuildingId, int targetBarracksId,
                                            const std::vector<int>& unitIds,
                                            std::string& failureReason)
{
    Building* source = FindBuilding(province, defenseBuildingId);
    Building* target = FindBuilding(province, targetBarracksId);
    if (source == nullptr || source->owner != &player || source->IsUnderConstruction() ||
        source->GetComponent<GarrisonComponent>() == nullptr || target == nullptr ||
        target->owner != &player || target->buildingType != BuildingType::Barracks ||
        target->IsUnderConstruction() || source == target)
    {
        failureReason = "invalid garrison or target barracks";
        return false;
    }

    std::set<int> unique;
    if (!ValidateUnitIds(player, unitIds, unique, failureReason))
        return false;
    for (int id : unitIds)
    {
        BattleUnit* unit = player.roster.FindUnit(id);
        if (unit == nullptr || unit->ownerPlayerId != player.id ||
            unit->assignment.kind != UnitAssignmentKind::DefensiveGarrison ||
            unit->assignment.provinceId != province.provinceId ||
            unit->assignment.buildingId != source->id)
        {
            failureReason = "unit is not stationed in source garrison";
            return false;
        }
    }

    for (int id : unitIds)
        if (!UnitAssignmentService::AssignReserve(*player.roster.FindUnit(id), province.provinceId, target->id))
        {
            failureReason = "assignment transition failed";
            return false;
        }
    return true;
}

std::vector<int> GarrisonService::GetGarrisonedUnitIds(const Player& player,
                                                       ProvinceId provinceId,
                                                       int defenseBuildingId)
{
    std::vector<int> result;
    for (const auto& [id, unit] : player.roster.units)
    {
        if (unit.assignment.kind == UnitAssignmentKind::DefensiveGarrison &&
            unit.assignment.provinceId == provinceId &&
            unit.assignment.buildingId == defenseBuildingId)
            result.push_back(id);
    }
    return result;
}

GarrisonSummary GarrisonService::BuildSummary(const Player& player,
                                               const ProvinceEconomy& province,
                                               const Building& defenseBuilding)
{
    GarrisonSummary result;
    const auto* garrison = defenseBuilding.GetComponent<GarrisonComponent>();
    const auto* upkeep = defenseBuilding.GetComponent<GarrisonUpkeepComponent>();
    const auto* local = defenseBuilding.GetComponent<LocalResourceBufferComponent>();
    if (garrison == nullptr || upkeep == nullptr)
        return result;

    result.capacity = garrison->GetEffectiveCapacity(defenseBuilding);
    const auto ids = GetGarrisonedUnitIds(player, province.provinceId, defenseBuilding.id);
    result.used = static_cast<int>(ids.size());
    for (int id : ids)
    {
        const BattleUnit* unit = player.roster.FindUnit(id);
        const UnitDefinition* definition = unit != nullptr ? FindUnitDefinition(unit->unitDefId) : nullptr;
        if (definition == nullptr)
            continue;
        result.unitCounts[definition->id]++;
        result.upkeepPerMinute += player.ModifyBalanceForUnit(
            BalanceStat::GarrisonFoodUpkeep, definition->garrisonFoodUpkeepPerMinute,
            &defenseBuilding, definition->id, upkeep->requiredResource);
    }
    auto bufferIt = local != nullptr ? local->buffers.find(upkeep->requiredResource) :
                                     std::map<ResourceType, ResourceBuffer>::const_iterator{};
    result.bufferedFood = local != nullptr && bufferIt != local->buffers.end()
        ? static_cast<int>(bufferIt->second.buffer.size()) : 0;
    result.incomingFood = CountIncomingResources(defenseBuilding, upkeep->requiredResource);
    const auto* mutableUpkeep = defenseBuilding.GetComponent<GarrisonUpkeepComponent>();
    const std::int64_t packageMicros = mutableUpkeep != nullptr
        ? std::max<std::int64_t>(1, static_cast<std::int64_t>(std::llround(
            std::max(0.0, mutableUpkeep->packageSize) * GarrisonUpkeepComponent::DebtScale))) : 1;
    result.duePackages = static_cast<int>(std::min<std::int64_t>(
        std::numeric_limits<int>::max(), mutableUpkeep->debtMicros / packageMicros));
    return result;
}

void GarrisonService::UpdateBuilding(Building& defenseBuilding, double dt)
{
    if (dt <= 0.0 || defenseBuilding.owner == nullptr || defenseBuilding.provinceEconomy == nullptr)
        return;

    auto* garrison = defenseBuilding.GetComponent<GarrisonComponent>();
    auto* upkeep = defenseBuilding.GetComponent<GarrisonUpkeepComponent>();
    auto* local = defenseBuilding.GetComponent<LocalResourceBufferComponent>();
    auto* logistics = defenseBuilding.GetComponent<LogisticsComponent>();
    if (garrison == nullptr || upkeep == nullptr || local == nullptr || logistics == nullptr)
        return;

    const auto ids = GetGarrisonedUnitIds(*defenseBuilding.owner,
                                          defenseBuilding.provinceId, defenseBuilding.id);
    double upkeepPerMinute = 0.0;
    for (int id : ids)
    {
        const BattleUnit* unit = defenseBuilding.owner->roster.FindUnit(id);
        const UnitDefinition* definition = unit != nullptr ? FindUnitDefinition(unit->unitDefId) : nullptr;
        if (definition != nullptr)
            upkeepPerMinute += defenseBuilding.owner->ModifyBalanceForUnit(
                BalanceStat::GarrisonFoodUpkeep, definition->garrisonFoodUpkeepPerMinute,
                &defenseBuilding, definition->id, upkeep->requiredResource);
    }

    upkeep->timer += dt;
    if (upkeep->intervalSeconds > 0.0)
        upkeep->timer = std::fmod(upkeep->timer, upkeep->intervalSeconds);
    else
        upkeep->timer = 0.0;

    const double addedDebt = std::max(0.0, upkeepPerMinute) * dt / 60.0 *
                             static_cast<double>(GarrisonUpkeepComponent::DebtScale);
    if (addedDebt > 0.0 && upkeep->debtMicros <= std::numeric_limits<std::int64_t>::max() -
                                                 static_cast<std::int64_t>(std::llround(addedDebt)))
        upkeep->debtMicros += static_cast<std::int64_t>(std::llround(addedDebt));

    const std::int64_t packageMicros = std::max<std::int64_t>(
        1, static_cast<std::int64_t>(std::llround(std::max(0.0, upkeep->packageSize) *
                                                  GarrisonUpkeepComponent::DebtScale)));
    auto bufferIt = local->buffers.find(upkeep->requiredResource);
    if (bufferIt == local->buffers.end())
    {
        upkeep->supplyStatus = upkeep->debtMicros >= packageMicros
            ? GarrisonSupplyStatus::Unsupplied : GarrisonSupplyStatus::Supplied;
        upkeep->requestedAmount = 0;
        return;
    }

    // Consume only whole packages already paid by accumulated debt.
    std::int64_t duePackages = upkeep->debtMicros / packageMicros;
    while (duePackages > 0 && !bufferIt->second.buffer.empty())
    {
        bufferIt->second.FreeResource();
        upkeep->debtMicros -= packageMicros;
        --duePackages;
    }

    const int incoming = CountIncomingResources(defenseBuilding, upkeep->requiredResource);
    const int missing = duePackages > incoming ?
        static_cast<int>(std::min<std::int64_t>(std::numeric_limits<int>::max(), duePackages - incoming)) : 0;
    int sent = 0;
    if (missing > 0)
        sent = logistics->RequestResource(upkeep->requiredResource, missing, defenseBuilding);
    upkeep->requestedAmount = incoming + sent;
    upkeep->supplyStatus = upkeep->requestedAmount > 0
        ? GarrisonSupplyStatus::RequestPending
        : (duePackages > 0 ? GarrisonSupplyStatus::Unsupplied : GarrisonSupplyStatus::Supplied);
}
