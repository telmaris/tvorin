#include "warfare/RaidDamage.h"

#include "economy/BuildingComponents.h"
#include "economy/Player.h"
#include "simulation/MapGenerator.h"
#include "warfare/DefenseCoverage.h"
#include "warfare/GarrisonService.h"
#include "warfare/UnitDefinition.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace
{
    std::uint64_t Mix(std::uint64_t value)
    {
        value += 0x9E3779B97F4A7C15ull;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
        return value ^ (value >> 31);
    }

    int ClampBasisPoints(int value)
    {
        return std::clamp(value, 0, 10000);
    }

    int ReadStoredAmount(const Building& building, ResourceType type)
    {
        int result = 0;
        for (const ResourceBuffer* buffer : building.GetResourceBuffers())
        {
            if (buffer != nullptr && buffer->type == type)
                result += static_cast<int>(buffer->buffer.size());
        }
        return result;
    }

}

int RaidDamageResolver::CalculateDestructionChanceBasisPoints(
    const RaidBuildingSnapshot& building, const RaidDamageRules& rules)
{
    const int base = ClampBasisPoints(building.baseDestructionChanceBasisPoints >= 0
        ? building.baseDestructionChanceBasisPoints : rules.baseDestructionChanceBasisPoints);
    const long double readiness = static_cast<long double>(
        ClampBasisPoints(building.readinessBasisPoints)) / 10000.0L;
    const int activeCoverage = static_cast<int>(std::lround(
        ClampBasisPoints(building.coverageProtectionBasisPoints) * readiness));
    const int activeGarrison = static_cast<int>(std::lround(
        ClampBasisPoints(building.garrisonStrengthBasisPoints) * readiness));
    const long double remainingRisk =
        (1.0L - static_cast<long double>(ClampBasisPoints(building.intrinsicResilienceBasisPoints)) / 10000.0L) *
        (1.0L - static_cast<long double>(activeCoverage) / 10000.0L) *
        (1.0L - static_cast<long double>(activeGarrison) / 10000.0L);
    return ClampBasisPoints(static_cast<int>(std::lround(
        static_cast<long double>(base) * std::clamp(remainingRisk, 0.0L, 1.0L))));
}

RaidDamageOutcome RaidDamageResolver::Resolve(
    const std::vector<RaidBuildingSnapshot>& buildings, BattleId battleId,
    ProvinceId provinceId, std::uint64_t seed, const RaidDamageRules& rules)
{
    RaidDamageOutcome result;
    if (battleId == InvalidBattleId || provinceId == InvalidProvinceId)
        return result;

    std::vector<RaidBuildingSnapshot> ordered = buildings;
    std::sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs)
    {
        return lhs.buildingId < rhs.buildingId;
    });
    std::set<int> ids;
    for (const auto& building : ordered)
        if (building.buildingId <= 0 || !ids.insert(building.buildingId).second)
            return result;

    result.valid = true;
    for (const auto& building : ordered)
    {
        if (!building.owned || !building.completed || !building.raidDestructible ||
            building.buildingType == BuildingType::Headquarters ||
            building.buildingType == BuildingType::Road)
            continue;

        const std::uint64_t buildingSeed = Mix(seed ^ battleId ^
            (static_cast<std::uint64_t>(provinceId) << 32) ^
            static_cast<std::uint64_t>(building.buildingId));
        const int destructionChance = CalculateDestructionChanceBasisPoints(building, rules);
        const bool destroyed = (buildingSeed % 10000ull) < static_cast<std::uint64_t>(destructionChance);
        RaidBuildingDamage damage;
        damage.buildingId = building.buildingId;
        damage.destroyed = destroyed;

        int stockLossFraction = building.stockLossFractionBasisPoints;
        if (stockLossFraction <= 0)
            stockLossFraction = building.coveredByGarrison
                ? rules.coveredStockLossFractionBasisPoints
                : rules.uncoveredStockLossFractionBasisPoints;
        stockLossFraction = ClampBasisPoints(stockLossFraction);
        if (destroyed)
            stockLossFraction = 10000;

        // A destroyed production building loses all of its local buffers even
        // when it is not a partial-stock target. Non-destroyed buildings only
        // use this branch when their SafetyComponent opted into stock loss.
        if (building.raidStockLossTarget || destroyed)
        {
            for (const auto& resource : building.resources)
            {
                if (resource.type == ResourceType::Null || resource.amount <= 0)
                    continue;
                const std::uint64_t resourceSeed = Mix(buildingSeed ^
                    (static_cast<std::uint64_t>(static_cast<int>(resource.type)) << 16));
                // The roll is intentionally part of the plan even for a full
                // destruction, keeping every resource line independently
                // deterministic and making partial stock loss reproducible.
                const int amount = destroyed || (resourceSeed % 10000ull <
                    static_cast<std::uint64_t>(stockLossFraction))
                    ? static_cast<int>(std::floor(static_cast<long double>(resource.amount) *
                                                   stockLossFraction / 10000.0L)) : 0;
                if (amount > 0)
                {
                    damage.lostResources[resource.type] += amount;
                    result.lostResources[resource.type] += amount;
                }
            }
        }
        if (destroyed || !damage.lostResources.empty())
            result.buildings.push_back(std::move(damage));
    }
    return result;
}

std::vector<RaidBuildingSnapshot> BuildRaidBuildingSnapshots(
    const Player& player, const ProvinceEconomy& province,
    const std::vector<Building*>& defenseBuildings, const RaidDamageRules& rules)
{
    std::vector<Building*> defenses = defenseBuildings;
    if (defenses.empty())
    {
        for (Building* building : province.dataTracker.buildings)
            if (building != nullptr && building->owner == &player &&
                building->GetComponent<DefenseCoverageComponent>() != nullptr &&
                building->GetComponent<GarrisonComponent>() != nullptr)
                defenses.push_back(building);
    }
    std::sort(defenses.begin(), defenses.end(), [](const Building* lhs, const Building* rhs)
    {
        return lhs != nullptr && (rhs == nullptr || lhs->id < rhs->id);
    });

    std::vector<RaidBuildingSnapshot> result;
    for (Building* building : province.dataTracker.buildings)
    {
        if (building == nullptr || building->owner != &player)
            continue;
        RaidBuildingSnapshot snapshot;
        snapshot.buildingId = building->id;
        snapshot.buildingType = building->buildingType;
        snapshot.completed = !building->IsUnderConstruction();
        snapshot.owned = true;
        const auto* safety = building->GetComponent<SafetyComponent>();
        snapshot.raidDestructible = safety == nullptr || safety->raidDestructible;
        snapshot.raidStockLossTarget = (safety == nullptr || safety->raidStockLossTarget) &&
            building->GetComponent<StorageComponent>() != nullptr;
        snapshot.intrinsicResilienceBasisPoints = safety == nullptr ? 0 :
            ClampBasisPoints(static_cast<int>(std::lround(safety->intrinsicResilience * 10000.0)));
        snapshot.baseDestructionChanceBasisPoints = player.ModifyBalanceIntForBuilding(
            BalanceStat::RaidBuildingDestructionChance, rules.baseDestructionChanceBasisPoints,
            building, ResourceType::Null, 0);
        snapshot.stockLossFractionBasisPoints = player.ModifyBalanceIntForBuilding(
            BalanceStat::RaidStockLossFraction,
            snapshot.coveredByGarrison ? rules.coveredStockLossFractionBasisPoints
                                        : rules.uncoveredStockLossFractionBasisPoints,
            building, ResourceType::Null, 0);

        for (Building* defense : defenses)
        {
            if (defense == nullptr || defense == building || defense->owner != &player ||
                defense->IsUnderConstruction() || province.tilemap == nullptr ||
                !DefenseCoverageService::Covers(*province.tilemap, *defense, *building))
                continue;
            const auto ids = GarrisonService::GetGarrisonedUnitIds(
                player, province.provinceId, defense->id);
            if (ids.empty())
                continue; // an empty building never projects active defense
            const auto coverage = DefenseCoverageService::GetCoverage(
                *province.tilemap, *defense);
            snapshot.coveredByGarrison = true;
            snapshot.coverageProtectionBasisPoints = DefenseCoverageService::CombineProtectionBasisPoints({
                snapshot.coverageProtectionBasisPoints,
                DefenseCoverageService::ProtectionToBasisPoints(coverage.protection)});
            for (int unitId : ids)
            {
                const BattleUnit* unit = player.roster.FindUnit(unitId);
                const UnitDefinition* definition = unit != nullptr ? FindUnitDefinition(unit->unitDefId) : nullptr;
                if (unit != nullptr && definition != nullptr)
                {
                    const double attack = unit->GetEffectiveFieldAttack(player);
                    snapshot.garrisonStrengthBasisPoints = ClampBasisPoints(
                        snapshot.garrisonStrengthBasisPoints +
                        static_cast<int>(std::lround(std::max(0.0, attack) * 50.0)));
                }
            }
        }

        snapshot.stockLossFractionBasisPoints = player.ModifyBalanceIntForBuilding(
            BalanceStat::RaidStockLossFraction,
            snapshot.coveredByGarrison ? rules.coveredStockLossFractionBasisPoints
                                        : rules.uncoveredStockLossFractionBasisPoints,
            building, ResourceType::Null, 0);
        for (const ResourceBuffer* buffer : building->GetResourceBuffers())
        {
            if (buffer != nullptr && buffer->type != ResourceType::Null)
                snapshot.resources.push_back({buffer->type, static_cast<int>(buffer->buffer.size())});
        }
        result.push_back(std::move(snapshot));
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs)
    {
        return lhs.buildingId < rhs.buildingId;
    });
    return result;
}

bool ApplyRaidDamage(TileMap& tilemap, Player& player,
                     const RaidDamageOutcome& outcome)
{
    if (!outcome.valid)
        return false;
    std::vector<RaidBuildingDamage> ordered = outcome.buildings;
    std::sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs)
    {
        return lhs.buildingId < rhs.buildingId;
    });
    for (const auto& damage : ordered)
    {
        Building* target = nullptr;
        // A raid is resolved against the target province, not against the
        // player's currently active province.  The global roster is allowed
        // to contain buildings from several provinces, so using
        // Player::GetTrackedBuildings() here could destroy a same-numbered
        // building in the wrong local map.
        if (tilemap.provinceEconomy != nullptr)
        {
            for (Building* building : tilemap.provinceEconomy->dataTracker.buildings)
                if (building != nullptr && building->id == damage.buildingId)
                {
                    target = building;
                    break;
                }
        }
        if (target == nullptr)
        {
            for (const Tile& tile : tilemap.tilemap)
                if (tile.building != nullptr && tile.building->id == damage.buildingId)
                {
                    target = tile.building.get();
                    break;
                }
        }
        if (target != nullptr && target->owner == &player)
        {
            if (damage.destroyed)
            {
                for (ResourceBuffer* buffer : target->GetResourceBuffers())
                    if (buffer != nullptr)
                        buffer->Clear();
                tilemap.DestroyBuildingAt(target->positionId);
                continue;
            }

            for (const auto& [type, amount] : damage.lostResources)
            {
                int remaining = std::max(0, amount);
                for (ResourceBuffer* buffer : target->GetResourceBuffers())
                {
                    if (buffer == nullptr || buffer->type != type)
                        continue;
                    while (remaining > 0 && !buffer->buffer.empty())
                    {
                        buffer->FreeResource();
                        --remaining;
                    }
                    if (remaining <= 0)
                        break;
                }
            }
        }
    }
    return true;
}
