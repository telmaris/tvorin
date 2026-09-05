#include "warfare/WarfareViews.h"

#include "economy/Building.h"
#include "economy/Player.h"
#include "simulation/MapGenerator.h"
#include "warfare/DefenseCoverage.h"
#include "warfare/GarrisonService.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    WorldForceClass ClassifyForce(double strength)
    {
        if (!std::isfinite(strength) || strength <= 0.0)
            return WorldForceClass::Unknown;
        if (strength < 10.0)
            return WorldForceClass::Patrol;
        if (strength < 30.0)
            return WorldForceClass::Warband;
        if (strength < 100.0)
            return WorldForceClass::Army;
        return WorldForceClass::Host;
    }

    std::uint64_t Remaining(std::uint64_t endTick, std::uint64_t currentTick)
    {
        return endTick > currentTick ? endTick - currentTick : 0;
    }

    double SnapshotStrength(const BattleSideSnapshot& snapshot)
    {
        double strength = snapshot.defensiveBonus;
        for (const auto& unit : snapshot.units)
            if (std::isfinite(unit.effectiveFieldAttack) && unit.effectiveFieldAttack > 0.0)
                strength += unit.effectiveFieldAttack;
        return strength;
    }
}

JourneyStatusView BuildJourneyStatusView(const WorldJourney& journey,
                                         std::uint64_t currentTick)
{
    JourneyStatusView view;
    view.journeyId = journey.id;
    view.ownerId = journey.ownerId;
    view.originProvinceId = journey.sourceProvinceId;
    view.targetProvinceId = journey.targetProvinceId;
    view.status = journey.status;
    view.kind = journey.kind;
    view.currentLeg = journey.currentLeg;
    view.totalLegs = journey.legPlan.size();
    view.startTick = journey.startTick;
    view.remainingTicks = Remaining(journey.legCompletionTick, currentTick);
    if (journey.status == WorldJourneyStatus::InTransit && !journey.legPlan.empty())
        for (std::size_t index = journey.currentLeg + 1; index < journey.legPlan.size(); ++index)
            view.remainingTicks += journey.legPlan[index].durationTicks;
    view.etaTick = currentTick > std::numeric_limits<std::uint64_t>::max() - view.remainingTicks
        ? std::numeric_limits<std::uint64_t>::max() : currentTick + view.remainingTicks;
    return view;
}

std::vector<JourneyStatusView> BuildJourneyStatusViews(const WorldJourneySystem& journeys,
                                                        std::uint64_t currentTick,
                                                        PlayerId viewerId,
                                                        std::size_t maximum)
{
    std::vector<JourneyStatusView> result;
    for (const auto& [journeyId, journey] : journeys.GetJourneys())
    {
        if (journey.id != journeyId ||
            (viewerId != InvalidPlayerId && journey.ownerId != viewerId))
            continue;
        result.push_back(BuildJourneyStatusView(journey, currentTick));
        if (result.size() >= maximum)
            break;
    }
    return result;
}

BattleStatusView BuildBattleStatusView(const BattleInstance& battle,
                                       std::uint64_t currentTick,
                                       PlayerId viewerId)
{
    BattleStatusView view;
    view.battleId = battle.id;
    view.attackerId = battle.attackerId;
    view.defenderId = battle.defenderId;
    view.originProvinceId = battle.sourceProvinceId;
    view.targetProvinceId = battle.targetProvinceId;
    view.journeyId = battle.journeyId;
    view.status = battle.status;
    view.startTick = battle.startTick;
    view.endTick = battle.endTick;
    view.remainingTicks = Remaining(battle.endTick, currentTick);
    view.isRaid = battle.isRaid;

    const bool revealAttacker = viewerId == InvalidPlayerId || viewerId == battle.attackerId;
    const bool revealDefender = viewerId == InvalidPlayerId || viewerId == battle.defenderId;
    view.attackerForceClass = revealAttacker
        ? ClassifyForce(SnapshotStrength(battle.attackerSnapshot))
        : WorldForceClass::Unknown;
    view.defenderForceClass = revealDefender
        ? ClassifyForce(SnapshotStrength(battle.defenderSnapshot))
        : WorldForceClass::Unknown;
    return view;
}

std::vector<BattleStatusView> BuildBattleStatusViews(const BattleLifecycleSystem& battles,
                                                     std::uint64_t currentTick,
                                                     PlayerId viewerId,
                                                     std::size_t maximum)
{
    std::vector<BattleStatusView> result;
    for (const auto& [battleId, battle] : battles.GetBattles())
    {
        if (battle.id != battleId ||
            (viewerId != InvalidPlayerId && battle.attackerId != viewerId &&
             battle.defenderId != viewerId))
            continue;
        result.push_back(BuildBattleStatusView(battle, currentTick, viewerId));
        if (result.size() >= maximum)
            break;
    }
    return result;
}

BattleReportView BuildBattleReportView(const BattleReport& report)
{
    BattleReportView view;
    view.battleId = report.battleId;
    view.attackerId = report.attackerId;
    view.defenderId = report.defenderId;
    view.originProvinceId = report.sourceProvinceId;
    view.targetProvinceId = report.targetProvinceId;
    view.winner = report.outcome.winner;
    view.attackerLosses = static_cast<int>(report.outcome.attackerLostUnitIds.size());
    view.defenderLosses = static_cast<int>(report.outcome.defenderLostUnitIds.size());
    view.lootValue = report.outcome.lootValue;
    view.crushingVictory = report.outcome.crushingVictory;
    view.banditTransformed = report.banditTransformed;
    view.cityDamaged = report.cityDamaged;
    view.raid = report.raid;
    view.destroyedBuildingIds = report.destroyedBuildingIds;
    view.lostResources = report.lostResources;
    return view;
}

ProvinceDefenseView BuildProvinceDefenseView(const TileMap& map, const Player& player,
                                             ProvinceId provinceId,
                                             const Building& defenseBuilding)
{
    ProvinceDefenseView view;
    view.provinceId = provinceId;
    view.buildingId = defenseBuilding.id;
    view.buildingType = defenseBuilding.buildingType;
    const auto* coverage = defenseBuilding.GetComponent<DefenseCoverageComponent>();
    const auto* garrison = defenseBuilding.GetComponent<GarrisonComponent>();
    const auto* upkeep = defenseBuilding.GetComponent<GarrisonUpkeepComponent>();
    if (coverage != nullptr)
    {
        const DefenseCoverageView geometry = DefenseCoverageService::GetCoverage(map, defenseBuilding);
        view.center = geometry.center;
        view.radius = geometry.radius;
        view.protection = geometry.protection;
        view.protectionActive = !defenseBuilding.IsUnderConstruction() &&
            !GarrisonService::GetGarrisonedUnitIds(player, provinceId, defenseBuilding.id).empty();
    }
    if (garrison != nullptr)
    {
        view.garrisonCapacity = garrison->GetEffectiveCapacity(defenseBuilding);
        view.garrisonUsed = static_cast<int>(GarrisonService::GetGarrisonedUnitIds(
            player, provinceId, defenseBuilding.id).size());
    }
    if (upkeep != nullptr)
    {
        const ProvinceEconomy* province = defenseBuilding.GetProvinceEconomy();
        const GarrisonSummary summary = province == nullptr
            ? GarrisonSummary{}
            : GarrisonService::BuildSummary(player, *province, defenseBuilding);
        view.upkeepPerMinute = summary.upkeepPerMinute;
        view.upkeepDebtPackages = upkeep->GetDebtInPackages();
        view.bufferedFood = summary.bufferedFood;
        view.incomingFood = summary.incomingFood;
        view.duePackages = summary.duePackages;
        view.supplyStatus = upkeep->supplyStatus;
    }
    return view;
}

const char* ToString(WorldJourneyStatus status)
{
    switch (status)
    {
        case WorldJourneyStatus::Planned: return "Planned";
        case WorldJourneyStatus::InTransit: return "In transit";
        case WorldJourneyStatus::Succeeded: return "Succeeded";
        case WorldJourneyStatus::Failed: return "Failed";
        case WorldJourneyStatus::AwaitingUnload: return "Awaiting unload";
        case WorldJourneyStatus::Cancelled: return "Cancelled";
    }
    return "Unknown";
}

const char* ToString(BattleLifecycleStatus status)
{
    switch (status)
    {
        case BattleLifecycleStatus::InTransit: return "In transit";
        case BattleLifecycleStatus::Active: return "Active";
        case BattleLifecycleStatus::Resolved: return "Resolved";
        case BattleLifecycleStatus::Failed: return "Failed";
        case BattleLifecycleStatus::Cancelled: return "Cancelled";
    }
    return "Unknown";
}

const char* ToString(BattleWinner winner)
{
    switch (winner)
    {
        case BattleWinner::Attacker: return "Attacker";
        case BattleWinner::Defender: return "Defender";
        case BattleWinner::Draw: return "Draw";
        case BattleWinner::Invalid: break;
    }
    return "Unknown";
}

const char* ToString(WorldForceClass forceClass)
{
    switch (forceClass)
    {
        case WorldForceClass::Patrol: return "Patrol";
        case WorldForceClass::Warband: return "Warband";
        case WorldForceClass::Army: return "Army";
        case WorldForceClass::Host: return "Host";
        case WorldForceClass::Unknown: break;
    }
    return "Unknown";
}

const char* ToString(GarrisonSupplyStatus status)
{
    switch (status)
    {
        case GarrisonSupplyStatus::Supplied: return "Supplied";
        case GarrisonSupplyStatus::Unsupplied: return "Unsupplied";
        case GarrisonSupplyStatus::RequestPending: return "Request pending";
    }
    return "Unknown";
}
