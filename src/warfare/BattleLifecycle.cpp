#include "warfare/BattleLifecycle.h"

#include "core/BoundedHistory.h"
#include "economy/Player.h"
#include "world/Province.h"
#include "world/ProvinceConnection.h"
#include "warfare/GarrisonService.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <set>
#include <utility>

namespace
{
    bool IsTerminal(BattleLifecycleStatus status)
    {
        return status == BattleLifecycleStatus::Resolved ||
               status == BattleLifecycleStatus::Failed ||
               status == BattleLifecycleStatus::Cancelled;
    }

    void PruneBattleHistory(std::map<BattleId, BattleInstance>& battles,
                            std::vector<BattleReport>& reports)
    {
        BoundedHistory::TrimOldestMatching(
            battles, PersistenceLimits::MaxRetainedBattles,
            [](const BattleInstance& battle) { return IsTerminal(battle.status); });
        BoundedHistory::TrimOldest(reports, PersistenceLimits::MaxBattleReports);
    }

    bool HasBattleCapacity(const std::map<BattleId, BattleInstance>& battles)
    {
        return BoundedHistory::CountMatching(
                   battles,
                   [](const BattleInstance& battle) { return !IsTerminal(battle.status); }) <
               PersistenceLimits::MaxConcurrentBattles;
    }

    constexpr PlayerId NeutralCombatantId = -2;

    std::uint64_t Mix(std::uint64_t value)
    {
        value += 0x9E3779B97F4A7C15ull;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
        return value ^ (value >> 31);
    }

    BattleRules ResolveBattleRules(const Player& player)
    {
        BattleRules rules = GetBattleRules();
        rules.baseLossFraction = std::clamp(
            player.ModifyBalance(BalanceStat::BattleCasualtyRate, rules.baseLossFraction),
            0.0, std::nextafter(1.0, 0.0));
        const int duration = player.ModifyBalanceInt(
            BalanceStat::BattleDuration,
            rules.durationTicks > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
                ? std::numeric_limits<int>::max() : static_cast<int>(rules.durationTicks),
            BuildingType::Building, ResourceType::Null, 1);
        rules.durationTicks = static_cast<std::uint64_t>(std::max(1, duration));
        return rules;
    }

    bool IsValidBattleRules(const BattleRules& rules)
    {
        return std::isfinite(rules.baseLossFraction) && rules.baseLossFraction >= 0.0 &&
               rules.baseLossFraction < 1.0 && std::isfinite(rules.casualtyCapFraction) &&
               rules.casualtyCapFraction >= 0.0 && rules.casualtyCapFraction < 1.0 &&
               std::isfinite(rules.drawBand) && rules.drawBand >= 0.0 &&
               rules.drawBand <= 1.0 && std::isfinite(rules.crushingRatio) &&
               rules.crushingRatio >= 1.0 && std::isfinite(rules.lootFraction) &&
               rules.lootFraction >= 0.0 && rules.lootFraction <= 1.0 &&
               rules.durationTicks > 0;
    }
}

bool BattleLifecycleSystem::IsValidDefenderUnit(const BattleUnit& unit,
                                                PlayerId defenderId,
                                                ProvinceId targetProvinceId) const
{
    return unit.ownerPlayerId == defenderId &&
           UnitAssignmentService::IsAvailableFromReserve(unit, targetProvinceId);
}

bool BattleLifecycleSystem::StartProvinceAttack(
    Player& attacker, ProvinceId sourceProvinceId, ProvinceId targetProvinceId,
    const std::vector<int>& unitInstanceIds, GlobalMap& map, WorldJourneySystem& journeys,
    const std::map<PlayerId, Player*>& players, std::uint64_t currentTick,
    BattleSeed campaignSeed, BattleId& createdId, std::string& failureReason)
{
    PruneBattleHistory(battles, reports);
    createdId = InvalidBattleId;
    if (attacker.id == InvalidPlayerId || sourceProvinceId == InvalidProvinceId ||
        targetProvinceId == InvalidProvinceId || sourceProvinceId == targetProvinceId ||
        unitInstanceIds.empty() || unitInstanceIds.size() > 32 ||
        !HasBattleCapacity(battles) || nextBattleId == InvalidBattleId)
    {
        failureReason = "invalid attack endpoints, army size or active battle limit";
        return false;
    }
    auto* source = map.FindBuildableProvince(sourceProvinceId);
    IProvince* target = map.FindProvince(targetProvinceId);
    if (source == nullptr || target == nullptr || source->GetOwnerId() != attacker.id ||
        target->GetKnowledge(attacker.id) < ProvinceKnowledgeLevel::Scouted)
    {
        failureReason = "attacker does not own source or know target";
        return false;
    }
    if (target->GetKind() == ProvinceKind::TreasureSite ||
        (target->GetKind() == ProvinceKind::Buildable &&
         dynamic_cast<BuildableProvince*>(target)->GetOwnerId() == attacker.id))
    {
        failureReason = "target does not accept an attack";
        return false;
    }

    std::vector<ProvinceConnectionId> path;
    if (!map.FindShortestPath(sourceProvinceId, targetProvinceId, path))
    {
        failureReason = "no legal province route to target";
        return false;
    }

    std::set<int> uniqueIds;
    for (const int unitId : unitInstanceIds)
    {
        if (unitId <= 0 || !uniqueIds.insert(unitId).second)
        {
            failureReason = "attacking army contains duplicate unit IDs";
            return false;
        }
        auto* unit = attacker.roster.FindUnit(unitId);
        if (unit == nullptr)
        {
            failureReason = "attacking unit does not exist";
            return false;
        }
        if (unit->ownerPlayerId != attacker.id ||
            !UnitAssignmentService::IsAvailableFromReserve(*unit, sourceProvinceId))
        {
            failureReason = "all attacking units must be in source reserve";
            return false;
        }
    }

    PlayerId defenderId = NeutralCombatantId;
    std::vector<int> defenderUnitIds;
    if (auto* buildableTarget = dynamic_cast<BuildableProvince*>(target))
    {
        defenderId = buildableTarget->GetOwnerId();
        const auto defenderIt = players.find(defenderId);
        if (defenderId == InvalidPlayerId || defenderIt == players.end() ||
            defenderIt->second == nullptr)
        {
            failureReason = "owned target has no defender state";
            return false;
        }
        for (auto& [unitId, unit] : defenderIt->second->roster.units)
        {
            if (IsValidDefenderUnit(unit, defenderId, targetProvinceId))
                defenderUnitIds.push_back(unitId);
        }
    }
    if (defenderId == attacker.id)
    {
        failureReason = "attacker cannot attack own province";
        return false;
    }

    WorldJourney journey;
    journey.ownerId = attacker.id;
    journey.sourceProvinceId = sourceProvinceId;
    journey.targetProvinceId = targetProvinceId;
    journey.kind = WorldJourneyKind::Attack;
    journey.legPlan.reserve(path.size());
    for (const ProvinceConnectionId connectionId : path)
        journey.legPlan.push_back({connectionId});
    journey.payload = ArmyParty{std::vector<int>(unitInstanceIds.begin(), unitInstanceIds.end())};
    std::sort(std::get<ArmyParty>(journey.payload).unitInstanceIds.begin(),
              std::get<ArmyParty>(journey.payload).unitInstanceIds.end());
    std::string journeyFailure;
    WorldJourneyRules journeyRules;
    journeyRules.routeTravelSpeedMultiplier =
        attacker.ModifyBalance(BalanceStat::RouteTravelSpeed, 1.0);
    double slowestMoveSpeed = std::numeric_limits<double>::max();
    for (const int unitId : unitInstanceIds)
    {
        const auto* unit = attacker.roster.FindUnit(unitId);
        if (unit == nullptr || !std::isfinite(unit->GetEffectiveMoveSpeed(attacker)) ||
            unit->GetEffectiveMoveSpeed(attacker) <= 0.0)
        {
            failureReason = "attacking army contains a unit without positive move speed";
            return false;
        }
        slowestMoveSpeed = std::min(slowestMoveSpeed,
                                    unit->GetEffectiveMoveSpeed(attacker));
    }
    journeyRules.speedProfile.moverSpeedBasisPoints = static_cast<int>(std::llround(
        slowestMoveSpeed * JourneyTiming::BasisPoints));
    const WorldJourneyStartResult start = journeys.Start(
        std::move(journey), map, currentTick, journeyRules);
    if (!start)
    {
        failureReason = start.failureReason.empty()
            ? "unable to start army journey" : start.failureReason;
        return false;
    }
    const WorldJourneyId journeyId = start.journeyId;
    BattleInstance instance;
    instance.id = nextBattleId++;
    instance.attackerId = attacker.id;
    instance.defenderId = defenderId == NeutralCombatantId ? InvalidPlayerId : defenderId;
    instance.sourceProvinceId = sourceProvinceId;
    instance.targetProvinceId = targetProvinceId;
    instance.journeyId = journeyId;
    instance.rules = ResolveBattleRules(attacker);
    instance.attackerUnitIds.assign(unitInstanceIds.begin(), unitInstanceIds.end());
    std::sort(instance.attackerUnitIds.begin(), instance.attackerUnitIds.end());
    instance.defenderUnitIds = std::move(defenderUnitIds);
    for (const int unitId : instance.attackerUnitIds)
    {
        BattleUnit* unit = attacker.roster.FindUnit(unitId);
        UnitAssignmentService::AssignJourney(*unit, sourceProvinceId, journeyId,
                                             unit->assignment.buildingId);
    }
    createdId = instance.id;
    battles.emplace(instance.id, std::move(instance));
    (void)campaignSeed;
    return true;
}

bool BattleLifecycleSystem::StartRaid(Player& defender, ProvinceId targetProvinceId,
                                      int raidStrength, GlobalMap& map,
                                      std::uint64_t currentTick, BattleSeed campaignSeed,
                                      BattleId& createdId, std::string& failureReason)
{
    PruneBattleHistory(battles, reports);
    createdId = InvalidBattleId;
    if (defender.id == InvalidPlayerId || targetProvinceId == InvalidProvinceId ||
        raidStrength <= 0 || !HasBattleCapacity(battles) ||
        nextBattleId == InvalidBattleId)
    {
        failureReason = "invalid raid target, strength or active battle limit";
        return false;
    }
    auto* target = map.FindBuildableProvince(targetProvinceId);
    ProvinceEconomy* province = defender.GetProvinceEconomy(targetProvinceId);
    if (target == nullptr || province == nullptr || target->GetOwnerId() != defender.id ||
        province->tilemap == nullptr)
    {
        failureReason = "raid target is not an owned buildable province";
        return false;
    }

    BattleInstance instance;
    instance.id = nextBattleId++;
    instance.attackerId = InvalidPlayerId;
    instance.defenderId = defender.id;
    instance.sourceProvinceId = targetProvinceId;
    instance.targetProvinceId = targetProvinceId;
    instance.status = BattleLifecycleStatus::Active;
    instance.startTick = currentTick;
    instance.rules = ResolveBattleRules(defender);
    if (currentTick > std::numeric_limits<std::uint64_t>::max() - instance.rules.durationTicks)
    {
        --nextBattleId;
        failureReason = "raid duration overflows simulation tick";
        return false;
    }
    instance.endTick = currentTick + instance.rules.durationTicks;
    instance.isRaid = true;
    instance.raidStrength = raidStrength;
    instance.attackerSnapshot.ownerId = -2;
    instance.attackerSnapshot.units.push_back({1, "raid", static_cast<double>(raidStrength)});
    instance.defenderSnapshot.ownerId = defender.id;

    for (Building* building : province->dataTracker.buildings)
    {
        if (building == nullptr || building->owner != &defender ||
            building->GetComponent<GarrisonComponent>() == nullptr)
            continue;
        const auto ids = GarrisonService::GetGarrisonedUnitIds(
            defender, targetProvinceId, building->id);
        if (ids.empty())
            continue;
        for (int unitId : ids)
        {
            const BattleUnit* unit = defender.roster.FindUnit(unitId);
            if (unit == nullptr)
                continue;
            const double attack = unit->GetEffectiveFieldAttack(defender) *
                defender.ModifyBalanceForUnit(BalanceStat::BattleAttack, 1.0, nullptr,
                                              unit->unitDefId);
            instance.defenderSnapshot.units.push_back({unit->instanceId, unit->unitDefId, attack});
        }
        if (const auto* coverage = building->GetComponent<DefenseCoverageComponent>())
            instance.defenderSnapshot.defensiveBonus += coverage->GetEffectiveProtection(*building);
    }
    std::sort(instance.defenderSnapshot.units.begin(), instance.defenderSnapshot.units.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.instanceId < rhs.instanceId; });

    battles.emplace(instance.id, std::move(instance));
    createdId = nextBattleId - 1;
    (void)campaignSeed;
    return true;
}

BattleSideSnapshot BattleLifecycleSystem::SnapshotSide(
    const BattleInstance& battle, bool attacker,
    const std::map<PlayerId, Player*>& players) const
{
    BattleSideSnapshot snapshot;
    const PlayerId playerId = attacker ? battle.attackerId : battle.defenderId;
    snapshot.ownerId = playerId == InvalidPlayerId ? NeutralCombatantId : playerId;
    const auto playerIt = players.find(playerId);
    if (playerIt != players.end() && playerIt->second != nullptr)
    {
        const Player& player = *playerIt->second;
        const auto& ids = attacker ? battle.attackerUnitIds : battle.defenderUnitIds;
        for (const int unitId : ids)
        {
            const BattleUnit* unit = player.roster.FindUnit(unitId);
            if (unit == nullptr || unit->ownerPlayerId != playerId)
                continue;
            const double attack = unit->GetEffectiveFieldAttack(player) *
                player.ModifyBalanceForUnit(BalanceStat::BattleAttack, 1.0, nullptr,
                                             unit->unitDefId);
            if (!std::isfinite(attack) || attack < 0.0)
                continue;
            snapshot.units.push_back({unit->instanceId, unit->unitDefId, attack});
        }
    }
    return snapshot;
}

void BattleLifecycleSystem::ReleaseAttackerUnits(
    const BattleInstance& battle, const std::map<PlayerId, Player*>& players,
    ProvinceId fallbackProvinceId)
{
    const auto playerIt = players.find(battle.attackerId);
    if (playerIt == players.end() || playerIt->second == nullptr)
        return;
    for (const int unitId : battle.attackerUnitIds)
    {
        BattleUnit* unit = playerIt->second->roster.FindUnit(unitId);
        if (unit == nullptr)
            continue;
        UnitAssignmentService::AssignReserve(*unit, fallbackProvinceId,
                                              unit->assignment.buildingId);
    }
}

void BattleLifecycleSystem::ResolveBattle(BattleInstance& battle, GlobalMap& map,
                                          const std::map<PlayerId, Player*>& players,
                                          BattleSeed campaignSeed)
{
    if (battle.isRaid)
    {
        ResolveRaid(battle, map, players, campaignSeed);
        return;
    }
    BattleSeed seed = Mix(campaignSeed ^ battle.id ^ battle.startTick);
    battle.outcome = BattleSimulator::Resolve(battle.attackerSnapshot,
                                              battle.defenderSnapshot,
                                              battle.rules, seed);
    if (!battle.outcome->valid)
    {
        battle.status = BattleLifecycleStatus::Failed;
        ReleaseAttackerUnits(battle, players, battle.sourceProvinceId);
        return;
    }

    const auto attackerIt = players.find(battle.attackerId);
    const auto defenderIt = players.find(battle.defenderId);
    if (attackerIt != players.end() && attackerIt->second != nullptr)
        for (const int unitId : battle.outcome->attackerLostUnitIds)
            attackerIt->second->roster.RemoveUnit(unitId);
    if (defenderIt != players.end() && defenderIt->second != nullptr)
        for (const int unitId : battle.outcome->defenderLostUnitIds)
            defenderIt->second->roster.RemoveUnit(unitId);

    const bool attackerWon = battle.outcome->winner == BattleWinner::Attacker;
    bool banditTransformed = false;
    bool cityDamaged = false;
    if (attackerWon)
    {
        if (auto* bandit = dynamic_cast<BanditProvince*>(map.FindProvince(battle.targetProvinceId)))
        {
            if (battle.outcome->crushingVictory)
            {
                auto replacement = std::make_unique<BuildableProvince>(
                    bandit->GetId(), bandit->GetLayoutPosition(), InvalidPlayerId);
                replacement->SetParameters(bandit->GetFutureBuildableParameters());
                banditTransformed = map.TransformProvince(std::move(replacement));
            }
            else
            {
                bandit->SetStrength(std::max(0, bandit->GetStrength() -
                    static_cast<int>(std::ceil(battle.outcome->attackerStrength * 0.5))));
            }
        }
        else if (auto* city = dynamic_cast<NeutralCityProvince*>(
                     map.FindProvince(battle.targetProvinceId)))
        {
            city->SetWealthTier(std::max(0, city->GetWealthTier() - 1));
            cityDamaged = true;
        }
    }

    ReleaseAttackerUnits(battle, players, battle.sourceProvinceId);
    if (defenderIt != players.end() && defenderIt->second != nullptr)
        for (const int unitId : battle.defenderUnitIds)
        {
            BattleUnit* unit = defenderIt->second->roster.FindUnit(unitId);
            if (unit != nullptr)
            {
                UnitAssignmentService::AssignReserve(*unit, battle.targetProvinceId,
                                                      unit->assignment.buildingId);
            }
        }
    battle.status = BattleLifecycleStatus::Resolved;
    reports.push_back({battle.id, battle.attackerId, battle.defenderId,
                       battle.sourceProvinceId, battle.targetProvinceId,
                       *battle.outcome, banditTransformed, cityDamaged});
}

void BattleLifecycleSystem::ResolveRaid(BattleInstance& battle, GlobalMap& map,
                                        const std::map<PlayerId, Player*>& players,
                                        BattleSeed campaignSeed)
{
    const BattleSeed seed = Mix(campaignSeed ^ battle.id ^ battle.startTick);
    battle.outcome = BattleSimulator::Resolve(battle.attackerSnapshot,
                                              battle.defenderSnapshot,
                                              battle.rules, seed);
    const auto defenderIt = players.find(battle.defenderId);
    if (!battle.outcome->valid || defenderIt == players.end() || defenderIt->second == nullptr)
    {
        battle.status = BattleLifecycleStatus::Failed;
        return;
    }

    Player& defender = *defenderIt->second;
    for (const int unitId : battle.outcome->defenderLostUnitIds)
        defender.roster.RemoveUnit(unitId);

    RaidDamageOutcome damage;
    if (battle.outcome->winner == BattleWinner::Attacker)
    {
        ProvinceEconomy* province = defender.GetProvinceEconomy(battle.targetProvinceId);
        TileMap* tilemap = defender.GetTileMap(battle.targetProvinceId);
        if (province != nullptr && tilemap != nullptr)
        {
            const auto snapshots = BuildRaidBuildingSnapshots(defender, *province);
            damage = RaidDamageResolver::Resolve(snapshots, battle.id,
                                                 battle.targetProvinceId, seed);
            ApplyRaidDamage(*tilemap, defender, damage);
        }
    }

    BattleReport report;
    report.battleId = battle.id;
    report.attackerId = battle.attackerId;
    report.defenderId = battle.defenderId;
    report.sourceProvinceId = battle.sourceProvinceId;
    report.targetProvinceId = battle.targetProvinceId;
    report.outcome = *battle.outcome;
    report.raid = true;
    report.lostResources = damage.lostResources;
    for (const auto& building : damage.buildings)
        if (building.destroyed)
            report.destroyedBuildingIds.push_back(building.buildingId);
    battle.status = BattleLifecycleStatus::Resolved;
    reports.push_back(std::move(report));
}

void BattleLifecycleSystem::Update(GlobalMap& map, WorldJourneySystem& journeys,
                                   const std::map<PlayerId, Player*>& players,
                                   std::uint64_t currentTick, BattleSeed campaignSeed)
{
    journeys.Update(map, currentTick);
    for (auto& [battleId, battle] : battles)
    {
        (void)battleId;
        if (battle.status != BattleLifecycleStatus::InTransit ||
            battle.journeyId == InvalidWorldJourneyId)
            continue;
        const auto journeyEvents = journeys.ConsumeLegEventsFor(battle.journeyId);
        for (const auto& event : journeyEvents)
        {
            if (battle.journeyId != event.journeyId ||
                battle.status != BattleLifecycleStatus::InTransit)
                continue;
            if (!event.journeySucceeded)
                continue;
            const auto attackerIt = players.find(battle.attackerId);
            if (attackerIt == players.end() || attackerIt->second == nullptr)
            {
                battle.status = BattleLifecycleStatus::Failed;
                continue;
            }
            for (const int unitId : battle.attackerUnitIds)
            {
                BattleUnit* unit = attackerIt->second->roster.FindUnit(unitId);
                if (unit != nullptr)
                    UnitAssignmentService::AssignBattle(*unit, battle.targetProvinceId,
                                                        battle.id,
                                                        unit->assignment.buildingId);
            }
            const auto defenderIt = players.find(battle.defenderId);
            if (defenderIt != players.end() && defenderIt->second != nullptr)
                for (const int unitId : battle.defenderUnitIds)
                {
                    BattleUnit* unit = defenderIt->second->roster.FindUnit(unitId);
                    if (unit != nullptr && IsValidDefenderUnit(*unit, battle.defenderId,
                                                               battle.targetProvinceId))
                        UnitAssignmentService::AssignBattle(*unit, battle.targetProvinceId,
                                                            battle.id,
                                                            unit->assignment.buildingId);
                }
            battle.attackerSnapshot = SnapshotSide(battle, true, players);
            battle.defenderSnapshot = SnapshotSide(battle, false, players);
            if (const auto* bandit = dynamic_cast<const BanditProvince*>(
                    map.FindProvince(battle.targetProvinceId)))
                battle.defenderSnapshot.defensiveBonus = bandit->GetStrength();
            else if (const auto* city = dynamic_cast<const NeutralCityProvince*>(
                         map.FindProvince(battle.targetProvinceId)))
                battle.defenderSnapshot.defensiveBonus = std::max(0, city->GetWealthTier() * 5);
            battle.status = BattleLifecycleStatus::Active;
            battle.startTick = currentTick;
            if (battle.rules.durationTicks == 0 ||
                currentTick > std::numeric_limits<std::uint64_t>::max() -
                    battle.rules.durationTicks)
            {
                battle.status = BattleLifecycleStatus::Failed;
                ReleaseAttackerUnits(battle, players, battle.sourceProvinceId);
                continue;
            }
            battle.endTick = currentTick + battle.rules.durationTicks;
        }
    }

    for (auto& [id, battle] : battles)
    {
        if (battle.status != BattleLifecycleStatus::InTransit)
            continue;
        const auto journeyIt = journeys.GetJourneys().find(battle.journeyId);
        if (journeyIt != journeys.GetJourneys().end() &&
            journeyIt->second.status == WorldJourneyStatus::Failed)
        {
            battle.status = BattleLifecycleStatus::Failed;
            ReleaseAttackerUnits(battle, players, battle.sourceProvinceId);
        }
    }
    for (auto& [id, battle] : battles)
        if (battle.status == BattleLifecycleStatus::Active && battle.endTick <= currentTick)
            ResolveBattle(battle, map, players, campaignSeed);
    PruneBattleHistory(battles, reports);
}

bool BattleLifecycleSystem::Cancel(BattleId battleId, const std::map<PlayerId, Player*>& players,
                                   WorldJourneySystem& journeys)
{
    const auto it = battles.find(battleId);
    if (it == battles.end() || it->second.status == BattleLifecycleStatus::Resolved ||
        it->second.status == BattleLifecycleStatus::Failed ||
        it->second.status == BattleLifecycleStatus::Cancelled)
        return false;
    if (it->second.journeyId != InvalidWorldJourneyId)
        journeys.Cancel(it->second.journeyId);
    ReleaseAttackerUnits(it->second, players, it->second.sourceProvinceId);
    it->second.status = BattleLifecycleStatus::Cancelled;
    return true;
}

const BattleInstance* BattleLifecycleSystem::FindBattle(BattleId id) const
{
    const auto it = battles.find(id);
    return it == battles.end() ? nullptr : &it->second;
}

std::vector<BattleReport> BattleLifecycleSystem::ConsumeReports()
{
    std::vector<BattleReport> result = std::move(reports);
    reports.clear();
    return result;
}

bool BattleLifecycleSystem::Restore(BattleId nextId,
                                    std::vector<BattleInstance> restoredBattles,
                                    std::vector<BattleReport> restoredReports,
                                    const GlobalMap& map,
                                    const std::map<PlayerId, Player*>& players,
                                    const WorldJourneySystem& journeys,
                                    std::string& failureReason)
{
    if (nextId == InvalidBattleId || restoredBattles.size() > MaxBattleRecords ||
        restoredReports.size() > MaxBattleReports)
    {
        failureReason = "invalid battle counter or count";
        return false;
    }
    std::map<BattleId, BattleInstance> candidate;
    for (auto& battle : restoredBattles)
    {
        if (battle.id == InvalidBattleId || battle.id >= nextId || candidate.contains(battle.id) ||
            map.FindProvince(battle.targetProvinceId) == nullptr ||
            map.FindProvince(battle.sourceProvinceId) == nullptr)
        {
            failureReason = "battle identity or endpoint is invalid";
            return false;
        }
        if (!battle.isRaid && (battle.attackerId == InvalidPlayerId ||
                               !players.contains(battle.attackerId) ||
                               (battle.defenderId != InvalidPlayerId &&
                                !players.contains(battle.defenderId)) ||
                               battle.journeyId == InvalidWorldJourneyId ||
                               !journeys.GetJourneys().contains(battle.journeyId)))
        {
            failureReason = "battle references missing player or journey";
            return false;
        }
        if (battle.isRaid && (battle.defenderId == InvalidPlayerId ||
                              !players.contains(battle.defenderId) ||
                              battle.sourceProvinceId != battle.targetProvinceId))
        {
            failureReason = "raid references invalid defender or source";
            return false;
        }
        if (battle.outcome.has_value() && !battle.outcome->valid)
        {
            failureReason = "battle stores an invalid outcome";
            return false;
        }
        if (!IsValidBattleRules(battle.rules))
        {
            failureReason = "battle stores invalid immutable rules";
            return false;
        }
        candidate.emplace(battle.id, std::move(battle));
    }
    std::set<BattleId> reportIds;
    for (const auto& report : restoredReports)
    {
        if (report.battleId == InvalidBattleId || !reportIds.insert(report.battleId).second)
        {
            failureReason = "battle reports contain duplicate IDs";
            return false;
        }
    }
    battles = std::move(candidate);
    reports = std::move(restoredReports);
    nextBattleId = nextId;
    return true;
}

void BattleLifecycleSystem::Clear()
{
    nextBattleId = 1;
    battles.clear();
    reports.clear();
}
