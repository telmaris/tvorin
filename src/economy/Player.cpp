#include "economy/Player.h"
#include "economy/Building.h"
#include "economy/StockpileIndex.h"
#include "core/Log.h"
#include "simulation/MapGenerator.h"
#include "economy/BuildingConfig.h"
#include "research/Technology.h"

#include <algorithm>
#include <cmath>

namespace
{
    // Requirement ids are stable data keys, not player-facing text. Keep a
    // missing definition debuggable (it can happen with old mod data) while
    // normally showing the localized/catalog display name in UI feedback.
    std::string RequirementDisplayName(const std::string& id, bool isFocus)
    {
        const TechnologyDefinition* definition = isFocus
            ? FindFocusDefinition(id)
            : FindTechnologyDefinition(id);
        return definition != nullptr && !definition->name.empty() ? definition->name : id;
    }
}

void Player::ReportBuildCostFailure(const std::string& buildingName) const
{
    Log::Msg("[Player]", "Not enough resources to build ", buildingName);
}

void Player::UpdateFocus(double dt)
{
    if (tilemap->params.debugMode)
        dt *= 20.0;
    std::string completedFocus = focuses.GetActiveFocusId();
    if (focuses.UpdateActiveFocus(dt))
    {
        economyTelemetry.RecordResearchMilestone(ResearchMilestoneType::Focus, completedFocus);
        RefreshTechnologyModifiers();
    }
}

void Player::UpdateResearch(double dt)
{
    if (tilemap->params.debugMode)
        dt *= 20.0;
    for (auto* building : GetTrackedBuildingsWithComponent<ResearchComponent>())
    {
        auto* research = building != nullptr ? building->GetComponent<ResearchComponent>() : nullptr;
        if (research == nullptr || building->owner != this ||
            building->buildingType != BuildingType::University)
            continue;

        std::string completedTechnology = research->technologyId;
        if (completedTechnology.empty() || !research->Tick(dt))
            continue;

        research->technologyId.clear();
        research->remaining = 0.0;
        research->total = 0.0;
        if (technologies.UnlockTechnology(completedTechnology))
        {
            economyTelemetry.RecordResearchMilestone(ResearchMilestoneType::Technology, completedTechnology);
            RefreshTechnologyModifiers();
        }
    }
}

void Player::ResetResearchState()
{
    technologies.Clear();
    focuses.Clear();
    for (auto* building : GetTrackedBuildingsWithComponent<ResearchComponent>())
    {
        auto* research = building != nullptr ? building->GetComponent<ResearchComponent>() : nullptr;
        if (research != nullptr)
        {
            research->technologyId.clear();
            research->remaining = 0.0;
            research->total = 0.0;
        }
    }
    RefreshTechnologyModifiers();
    Log::Msg("[Debug]", "Research state reset for player ", id);
}

bool Player::IsTechnologyInProgress(const std::string& id) const
{
    for (auto* building : GetTrackedBuildingsWithComponent<ResearchComponent>())
    {
        const auto* research = building != nullptr ? building->GetComponent<ResearchComponent>() : nullptr;
        if (research != nullptr && building->owner == this &&
            building->buildingType == BuildingType::University &&
            research->technologyId == id)
            return true;
    }
    return false;
}

bool Player::HasBuildResources(const std::vector<ResourceAmountDefinition>& costs) const
{
    for (const auto& cost : costs)
        if (StockpileIndex::GetTotal(*this, cost.type) < cost.amount)
            return false;
    return true;
}

std::vector<std::string> Player::GetBuildRequirementFailures(const BuildingDefinition& definition,
                                                              bool ignoreDebugFreeBuild) const
{
    std::vector<std::string> failures;
    for (const auto& technology : definition.requiredTechnologies)
        if (!technologies.HasTechnology(technology))
            failures.push_back("Requires technology: " + RequirementDisplayName(technology, false));
    for (const auto& focus : definition.requiredFocuses)
        if (!focuses.HasFocus(focus))
            failures.push_back("Requires focus: " + RequirementDisplayName(focus, true));
    if ((!tilemap->params.debugMode || !ignoreDebugFreeBuild) && !HasBuildResources(GetEffectiveBuildCosts(definition)))
        failures.push_back("Not enough resources");
    return failures;
}

int Player::GetPopulationCap() const
{
    int cap = 0;
    for (const auto* building : GetTrackedBuildingsWithComponent<PopulationComponent>())
    {
        const auto* population = building != nullptr ? building->GetComponent<PopulationComponent>() : nullptr;
        if (population != nullptr && building->owner == this && !building->IsUnderConstruction())
            cap += ResolveStat(population->populationCap, building);
    }
    return cap;
}

double Player::GetFoodProductivity() const
{
    int villageCount = 0;
    double productivity = 0.0;
    for (const auto* building : GetTrackedBuildingsWithComponent<PopulationComponent>())
    {
        const auto* population = building != nullptr ? building->GetComponent<PopulationComponent>() : nullptr;
        if (population == nullptr || building->owner != this || building->IsUnderConstruction())
            continue;

        villageCount++;
        productivity += population->GetWorkerProductivity();
    }

    return villageCount > 0 ? productivity / villageCount : 1.0;
}

// T6 (docs/post_pivot_audit_2026-07-12.md): raw food supply ratio (0-100%,
// no productivity floor) averaged across every village — distinct from
// GetFoodProductivity()'s worker-productivity average (0.3 + 0.7*ratio),
// which is what actually scales production and shouldn't be shown as if it
// were the literal supply number.
double Player::GetFoodSupplyRatio() const
{
    int villageCount = 0;
    double ratio = 0.0;
    for (const auto* building : GetTrackedBuildingsWithComponent<PopulationComponent>())
    {
        const auto* population = building != nullptr ? building->GetComponent<PopulationComponent>() : nullptr;
        if (population == nullptr || building->owner != this || building->IsUnderConstruction())
            continue;

        villageCount++;
        ratio += population->GetFoodSupplyRatio();
    }

    return villageCount > 0 ? ratio / villageCount : 1.0;
}

// Ammo fill averaged per tower rather than summed across the network: five
// full towers and one empty one is a defensive hole, and a pooled
// held/capacity ratio would hide it behind the four that are fine.
double Player::GetAmmunitionSupplyRatio() const
{
    int towerCount = 0;
    double ratio = 0.0;
    for (const auto* building : GetTrackedBuildingsWithComponent<TowerCombatComponent>())
    {
        const auto* tower = building != nullptr ? building->GetComponent<TowerCombatComponent>() : nullptr;
        const auto* storage = building != nullptr ? building->GetComponent<LocalResourceBufferComponent>() : nullptr;
        if (tower == nullptr || storage == nullptr || building->owner != this || building->IsUnderConstruction())
            continue;

        towerCount++;
        auto it = storage->buffers.find(tower->ammoResource);
        if (it == storage->buffers.end() || it->second.bufferSize <= 0)
            continue;
        ratio += std::clamp(static_cast<double>(it->second.buffer.size()) / it->second.bufferSize, 0.0, 1.0);
    }

    return towerCount > 0 ? ratio / towerCount : 1.0;
}

bool Player::CanResearchTechnology(const std::string& id) const
{
    const auto* definition = FindTechnologyDefinition(id);
    return definition != nullptr &&
           technologies.CanUnlock(id) &&
           !IsTechnologyInProgress(id) &&
           (tilemap->params.debugMode || HasBuildResources(definition->costs));
}

bool Player::UnlockFocus(const std::string& id)
{
    const auto* definition = FindFocusDefinition(id);
    if (definition == nullptr || !focuses.CanUnlock(id))
        return false;

    if (!focuses.UnlockFocus(id))
        return false;

    economyTelemetry.RecordResearchMilestone(ResearchMilestoneType::Focus, id);
    RefreshTechnologyModifiers();
    return true;
}

bool Player::StartFocus(const std::string& id)
{
    const auto* definition = FindFocusDefinition(id);
    if (definition == nullptr)
        return false;

    bool wasUnlocked = focuses.HasFocus(id);
    if (!focuses.StartFocus(id))
        return false;

    if (!wasUnlocked && focuses.HasFocus(id))
    {
        economyTelemetry.RecordResearchMilestone(ResearchMilestoneType::Focus, id);
        RefreshTechnologyModifiers();
    }
    return true;
}

bool Player::UnlockTechnology(const std::string& id)
{
    const auto* definition = FindTechnologyDefinition(id);
    if (definition == nullptr || !technologies.CanUnlock(id))
        return false;

    if (!tilemap->params.debugMode && !TryPayBuildCost(definition->costs))
        return false;

    if (!technologies.UnlockTechnology(id))
        return false;

    economyTelemetry.RecordResearchMilestone(ResearchMilestoneType::Technology, id);
    RefreshTechnologyModifiers();
    return true;
}

bool Player::StartTechnologyResearch(const std::string& id, Building* university)
{
    const auto* definition = FindTechnologyDefinition(id);
    auto* research = university != nullptr ? university->GetComponent<ResearchComponent>() : nullptr;
    if (definition == nullptr || university == nullptr || university->owner != this ||
        university->buildingType != BuildingType::University || research == nullptr ||
        !research->technologyId.empty())
        return false;

    if (!CanResearchTechnology(id))
        return false;

    if (!tilemap->params.debugMode && !TryPayBuildCost(definition->costs))
        return false;

    double researchTime = ModifyBalanceForBuilding(
        BalanceStat::ProductionCycleTime,
        definition->researchTime,
        university,
        ResourceType::Null);
    if (!research->Start(id, researchTime))
        return false;

    return true;
}

void Player::RefreshTechnologyModifiers()
{
    balanceModifiers.ClearSourcePrefix("tech:");
    balanceModifiers.ClearSourcePrefix("focus:");
    technologies.CollectModifiers(balanceModifiers);
    focuses.CollectModifiers(balanceModifiers);
}

void Player::RefreshUpgradeModifiers()
{
    balanceModifiers.ClearSourcePrefix("upgrade:");
    for (Building* building : GetTrackedBuildings())
    {
        if (building == nullptr)
            continue;

        const auto* upgrade = building->GetComponent<UpgradeComponent>();
        if (upgrade != nullptr && upgrade->level > 1)
            ApplyUpgradeLevelModifiers(*building);
    }
}

void Player::ApplyUpgradeLevelModifiers(Building& building)
{
    auto* upgrade = building.GetComponent<UpgradeComponent>();
    if (upgrade == nullptr)
        return;

    std::string sourcePrefix = "upgrade:" + std::to_string(building.positionId) + ":";
    balanceModifiers.ClearSourcePrefix(sourcePrefix);

    const auto& definition = GetBuildingDefinition(building.buildingType);
    const auto* levelDefinition = FindUpgradeLevelDefinition(definition, upgrade->level);
    if (levelDefinition == nullptr)
        return;

    for (BalanceModifier modifier : levelDefinition->modifiers)
    {
        modifier.scope = BalanceModifierScope::BuildingAtPosition(building.positionId);
        modifier.source = sourcePrefix + std::to_string(upgrade->level);
        balanceModifiers.AddModifier(std::move(modifier));
    }
}

double Player::AddManpower(double amount)
{
    if (amount <= 0.0)
        return 0.0;

    double cap = static_cast<double>(GetPopulationCap());
    double room = std::max(0.0, cap - GetTotalPopulation());
    double added = std::min(amount, room);
    if (added > 0.0)
        strategicResources.Add(StrategicResourceType::Manpower, added);
    return added;
}

int Player::AutoAssignWorkers(Building* building)
{
    if (building == nullptr || building->owner != this)
        return 0;

    auto* workers = building->GetComponent<WorkerComponent>();
    if (workers == nullptr)
        return 0;

    int needed = std::max(0, building->GetWorkerCapacity() - workers->assigned);
    int available = static_cast<int>(std::floor(strategicResources.Get(StrategicResourceType::Manpower)));
    int assigned = std::min(needed, available);
    if (assigned <= 0)
        return 0;

    strategicResources.Consume(StrategicResourceType::Manpower, assigned);
    strategicResources.Add(StrategicResourceType::Workers, assigned);
    workers->assigned += assigned;
    return assigned;
}

bool Player::TryPayBuildCost(const std::vector<ResourceAmountDefinition>& costs)
{
    if (!HasBuildResources(costs))
        return false;

    // Paid out of the warehouse network only, in building-id order — see
    // StockpileIndex for why that scope (and that ordering) is the one every
    // "how much do I have" answer in the game shares.
    for (const auto& cost : costs)
        StockpileIndex::Consume(*this, cost.type, cost.amount);

    return true;
}

void Player::RefundBuildCost(const std::vector<ResourceAmountDefinition>& costs)
{
    // Pour the resources back into the network they were paid from. Buffers
    // that already hold this type have the capacity we freed at build time;
    // anything that no longer fits (production refilled it) spills.
    for (const auto& cost : costs)
        if (cost.amount > 0)
            StockpileIndex::Deposit(*this, cost.type, cost.amount);
}

// ── PlayerEconomyTelemetry ──────────────────────────────────────────────────

namespace
{
    constexpr double FlowRateWindowSeconds = 60.0;
    constexpr double FlowHistoryRetentionSeconds = 300.0;

    void AddAmounts(std::map<ResourceType, int>& target, const std::map<ResourceType, int>& source)
    {
        for (const auto& [type, amount] : source)
            target[type] += amount;
    }
}

void PlayerEconomyTelemetry::RecordProduction(ResourceType type, int amount)
{
    if (type == ResourceType::Null || amount <= 0)
        return;
    pendingProducedAmounts[type] += amount;
}

void PlayerEconomyTelemetry::RecordConsumption(ResourceType type, int amount)
{
    if (type == ResourceType::Null || amount <= 0)
        return;
    pendingConsumedAmounts[type] += amount;
}

void PlayerEconomyTelemetry::RecordResearchMilestone(ResearchMilestoneType type,
                                                     const std::string& id)
{
    if (id.empty())
        return;

    auto key = std::make_pair(type, id);
    if (!recordedMilestones.insert(key).second)
        return;

    researchMilestones.push_back({elapsedTime, type, id});
}

void PlayerEconomyTelemetry::Update(Player&, double dt)
{
    elapsedTime += std::max(0.0, dt);
    sampleTimer += std::max(0.0, dt);

    if (sampleTimer >= 1.0 || history.empty())
    {
        ResourceFlowSnapshot sample = BuildSnapshot(elapsedTime);
        sample.producedAmounts = pendingProducedAmounts;
        sample.consumedAmounts = pendingConsumedAmounts;
        history.push_back(std::move(sample));

        pendingProducedAmounts.clear();
        pendingConsumedAmounts.clear();
        sampleTimer = 0.0;
        while (!history.empty() && elapsedTime - history.front().time > FlowHistoryRetentionSeconds)
            history.pop_front();
    }

    current = BuildSnapshot(elapsedTime);
}

ResourceFlowSnapshot PlayerEconomyTelemetry::BuildSnapshot(double time) const
{
    ResourceFlowSnapshot snapshot;
    snapshot.time = time;
    snapshot.producedAmounts = pendingProducedAmounts;
    snapshot.consumedAmounts = pendingConsumedAmounts;

    std::map<ResourceType, int> producedTotals = pendingProducedAmounts;
    std::map<ResourceType, int> consumedTotals = pendingConsumedAmounts;
    double oldestSampleTime = time;

    for (auto it = history.rbegin(); it != history.rend(); ++it)
    {
        if (time - it->time > FlowRateWindowSeconds)
            break;

        oldestSampleTime = std::min(oldestSampleTime, it->time);
        AddAmounts(producedTotals, it->producedAmounts);
        AddAmounts(consumedTotals, it->consumedAmounts);
    }

    double duration = std::clamp(time - oldestSampleTime + 1.0, 1.0, FlowRateWindowSeconds);
    for (const auto& [type, amount] : producedTotals)
        if (amount > 0)
            snapshot.productionRatesPerMinute[type] =
                static_cast<int>(std::round(amount * 60.0 / duration));
    for (const auto& [type, amount] : consumedTotals)
        if (amount > 0)
            snapshot.consumptionRatesPerMinute[type] =
                static_cast<int>(std::round(amount * 60.0 / duration));

    return snapshot;
}

ResourceFlowSnapshot PlayerEconomyTelemetry::BuildSnapshot(Player& player, double time)
{
    return player.economyTelemetry.BuildSnapshot(time);
}

// ─── ETAP 10: Strategic building registries ──────────────────────────────────

void Player::RegisterBuilding(Building* building)
{
    dataTracker.RegisterBuilding(building);
    if (building == nullptr)
        return;

    // Add to strategic registries based on components (ETAP 10).
    if (building->HasComponent<StorageComponent>())
        storages.push_back(building);

    if (building->HasComponent<PopulationComponent>())
        villages.push_back(building);

    registryGeneration++;
}

void Player::UnregisterBuilding(Building* building)
{
    dataTracker.UnregisterBuilding(building);
    if (building == nullptr)
        return;

    // Remove from strategic registries.
    auto removeFromRegistry = [building](std::vector<Building*>& registry)
    {
        auto it = std::find(registry.begin(), registry.end(), building);
        if (it != registry.end())
            registry.erase(it);
    };

    removeFromRegistry(storages);
    removeFromRegistry(villages);

    registryGeneration++;
}
