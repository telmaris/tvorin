#include "economy/Player.h"
#include "economy/Building.h"
#include "economy/StockpileIndex.h"
#include "core/Log.h"
#include "simulation/MapGenerator.h"
#include "economy/BuildingConfig.h"
#include "research/Technology.h"
#include "world/ProvinceDefinition.h"

#include <algorithm>
#include <cctype>
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

Player::Player(int playerId)
    : PlayerState{}
{
    id = playerId;
}

Player::Player(int playerId, TileMap& map)
    : PlayerState{},
      compatibilityProvince(std::make_unique<ProvinceSimulation>(
          static_cast<ProvinceId>(playerId + 1), static_cast<PlayerId>(playerId)))
{
    id = playerId;
    compatibilityProvince->BindExternalTileMap(map);
    BindProvince(*compatibilityProvince);
    RefreshTechnologyModifiers();
}

Player::Player(int playerId, ProvinceSimulation& province)
    : PlayerState{}
{
    id = playerId;
    BindProvince(province);
    RefreshTechnologyModifiers();
}

void Player::BindProvince(ProvinceSimulation& province)
{
    BindProvince(province.GetProvinceId(), province);
}

void Player::BindProvince(ProvinceId provinceId, ProvinceSimulation& province)
{
    if (provinceId == InvalidProvinceId)
        provinceId = province.GetProvinceId();

    ProvinceEconomy& economy = province.GetEconomy();
    if (homeProvinceId != InvalidProvinceId && !economy.population.initialized)
    {
        if (homeProvinceId == provinceId)
        {
            economy.population.availableManpower = strategicResources.Get(
                StrategicResourceType::Manpower);
            strategicResources.Set(StrategicResourceType::Manpower, 0.0);
        }
        economy.population.initialized = true;
    }

    boundProvinces[provinceId] = &province;
    if (boundProvince == nullptr || activeProvinceId == InvalidProvinceId ||
        activeProvinceId == provinceId)
    {
        boundProvince = &province;
        activeProvinceId = provinceId;
    }
    province.BindPlayer(*this);
    RefreshProvinceTraitModifiers();
}

TileMap* Player::GetTileMap() const
{
    const ProvinceEconomy* economy = GetProvinceEconomy();
    return economy != nullptr ? economy->tilemap : nullptr;
}

TileMap* Player::GetTileMap(ProvinceId provinceId) const
{
    const ProvinceEconomy* economy = GetProvinceEconomy(provinceId);
    return economy != nullptr ? economy->tilemap : nullptr;
}

void Player::RebindTileMap(TileMap& map)
{
    if (boundProvince != nullptr)
        boundProvince->BindExternalTileMap(map);
}

void Player::RebindTileMap(ProvinceId provinceId, TileMap& map)
{
    if (auto* simulation = GetProvinceSimulation(provinceId); simulation != nullptr)
        simulation->BindExternalTileMap(map);
}

void Player::TrackAcceptedCommand(GameCommandType type)
{
    ProvinceEconomy* economy = GetCampaignEconomy();
    if (economy != nullptr)
        economy->dataTracker.TrackCommand(type);
}

void Player::TrackAcceptedCommand(GameCommandType type, ProvinceId provinceId)
{
    ProvinceEconomy* economy = GetProvinceEconomy(provinceId);
    if (economy == nullptr)
        economy = GetCampaignEconomy();
    if (economy != nullptr)
        economy->dataTracker.TrackCommand(type);
}

ProvinceEconomy* Player::GetCampaignEconomy() const
{
    if (ProvinceEconomy* home = GetProvinceEconomy(homeProvinceId); home != nullptr)
        return home;
    for (const auto& [provinceId, simulation] : boundProvinces)
    {
        (void)provinceId;
        if (simulation != nullptr)
            return &simulation->GetEconomy();
    }
    return nullptr;
}

const std::set<Building*>& Player::GetTrackedBuildings() const
{
    static const std::set<Building*> empty;
    const ProvinceEconomy* economy = GetProvinceEconomy();
    return economy != nullptr ? economy->dataTracker.buildings : empty;
}

void Player::UpdateEconomyTelemetry(double dt)
{
    ProvinceEconomy* economy = GetProvinceEconomy();
    if (economy != nullptr)
        economy->economyTelemetry.Update(*this, dt);
}

void Player::ReportBuildCostFailure(const std::string& buildingName) const
{
    Log::Msg("[Player]", "Not enough resources to build ", buildingName);
}

void Player::UpdateFocus(double dt)
{
    ProvinceEconomy* campaignEconomy = GetCampaignEconomy();
    const TileMap* map = campaignEconomy != nullptr ? campaignEconomy->tilemap : nullptr;
    if (map != nullptr && map->params.debugMode)
        dt *= 20.0;
    std::string completedFocus = focuses.GetActiveFocusId();
    if (focuses.UpdateActiveFocus(dt))
    {
        if (campaignEconomy != nullptr)
            campaignEconomy->economyTelemetry.RecordResearchMilestone(ResearchMilestoneType::Focus, completedFocus);
        RefreshTechnologyModifiers();
    }
}

void Player::UpdateResearch(double dt)
{
    for (const auto& [provinceId, simulation] : boundProvinces)
    {
        (void)provinceId;
        if (simulation == nullptr)
            continue;
        ProvinceEconomy& economy = simulation->GetEconomy();
        const double provinceDt = economy.tilemap != nullptr && economy.tilemap->params.debugMode
            ? dt * 20.0 : dt;
        for (auto* building : economy.dataTracker.BuildingsWithComponent<ResearchComponent>())
        {
            auto* research = building != nullptr ? building->GetComponent<ResearchComponent>() : nullptr;
            if (research == nullptr || building->owner != this ||
                building->buildingType != BuildingType::University)
                continue;

            std::string completedTechnology = research->technologyId;
            if (completedTechnology.empty() || !research->Tick(provinceDt))
                continue;

            research->technologyId.clear();
            research->remaining = 0.0;
            research->total = 0.0;
            if (technologies.UnlockTechnology(completedTechnology))
            {
                economy.economyTelemetry.RecordResearchMilestone(
                    ResearchMilestoneType::Technology, completedTechnology);
                RefreshTechnologyModifiers();
            }
        }
    }
}

void Player::ResetResearchState()
{
    technologies.Clear();
    focuses.Clear();
    for (const auto& [provinceId, simulation] : boundProvinces)
        if (simulation != nullptr)
            for (auto* building : simulation->GetEconomy().dataTracker.BuildingsWithComponent<ResearchComponent>())
            {
                (void)provinceId;
                auto* research = building != nullptr ? building->GetComponent<ResearchComponent>() : nullptr;
                if (research == nullptr)
                    continue;
                research->technologyId.clear();
                research->remaining = 0.0;
                research->total = 0.0;
            }
    RefreshTechnologyModifiers();
    Log::Msg("[Debug]", "Research state reset for player ", id);
}

bool Player::IsTechnologyInProgress(const std::string& id) const
{
    for (const auto& [provinceId, simulation] : boundProvinces)
        if (simulation != nullptr)
            for (auto* building : simulation->GetEconomy().dataTracker.BuildingsWithComponent<ResearchComponent>())
            {
                (void)provinceId;
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
    const ProvinceEconomy* economy = GetProvinceEconomy();
    return economy != nullptr && HasBuildResources(*economy, costs);
}

bool Player::HasBuildResources(const ProvinceEconomy& economy,
                               const std::vector<ResourceAmountDefinition>& costs) const
{
    for (const auto& cost : costs)
        if (StockpileIndex::GetTotal(economy, cost.type) < cost.amount)
            return false;
    return true;
}

std::vector<std::string> Player::GetBuildUnlockRequirementFailures(const BuildingDefinition& definition) const
{
    std::vector<std::string> failures;
    for (const auto& technology : definition.requiredTechnologies)
        if (!technologies.HasTechnology(technology))
            failures.push_back("Requires technology: " + RequirementDisplayName(technology, false));
    for (const auto& focus : definition.requiredFocuses)
        if (!focuses.HasFocus(focus))
            failures.push_back("Requires focus: " + RequirementDisplayName(focus, true));
    return failures;
}

std::vector<std::string> Player::GetBuildRequirementFailures(const BuildingDefinition& definition,
                                                              bool ignoreDebugFreeBuild) const
{
    const ProvinceEconomy* economy = GetProvinceEconomy();
    if (economy == nullptr)
        return {"No province economy"};
    return GetBuildRequirementFailures(definition, *economy, ignoreDebugFreeBuild);
}

std::vector<std::string> Player::GetBuildRequirementFailures(
    const BuildingDefinition& definition, const ProvinceEconomy& economy,
    bool ignoreDebugFreeBuild) const
{
    std::vector<std::string> failures = GetBuildUnlockRequirementFailures(definition);
    const TileMap* map = economy.tilemap;
    if ((map == nullptr || !map->params.debugMode || !ignoreDebugFreeBuild) &&
        !HasBuildResources(economy, GetEffectiveBuildCosts(definition)))
        failures.push_back("Not enough resources");
    return failures;
}

int Player::GetPopulationCap() const
{
    int cap = 0;
    for (const auto& [provinceId, simulation] : boundProvinces)
        if (simulation != nullptr)
            for (const auto* building : simulation->GetEconomy().dataTracker.BuildingsWithComponent<PopulationComponent>())
            {
                (void)provinceId;
                const auto* population = building != nullptr ? building->GetComponent<PopulationComponent>() : nullptr;
                if (population != nullptr && building->owner == this && !building->IsUnderConstruction())
                    cap += ResolveStat(population->populationCap, building);
            }
    return cap;
}

double Player::GetFoodProductivity() const
{
    const ProvinceEconomy* economy = GetProvinceEconomy();
    return economy != nullptr ? GetFoodProductivity(*economy) : 1.0;
}

double Player::GetFoodProductivity(const ProvinceEconomy& economy) const
{
    int villageCount = 0;
    double productivity = 0.0;
    for (const auto* building : economy.dataTracker.BuildingsWithComponent<PopulationComponent>())
    {
        const auto* population = building != nullptr ? building->GetComponent<PopulationComponent>() : nullptr;
        if (population == nullptr || building->owner != this || building->IsUnderConstruction())
            continue;

        villageCount++;
        productivity += population->GetWorkerProductivity();
    }

    return villageCount > 0 ? productivity / villageCount : 1.0;
}

// Raw food supply ratio, without the worker-productivity floor.
double Player::GetFoodSupplyRatio() const
{
    const ProvinceEconomy* economy = GetProvinceEconomy();
    return economy != nullptr ? GetFoodSupplyRatio(*economy) : 1.0;
}

double Player::GetFoodSupplyRatio(const ProvinceEconomy& economy) const
{
    int villageCount = 0;
    double ratio = 0.0;
    for (const auto* building : economy.dataTracker.BuildingsWithComponent<PopulationComponent>())
    {
        const auto* population = building != nullptr ? building->GetComponent<PopulationComponent>() : nullptr;
        if (population == nullptr || building->owner != this || building->IsUnderConstruction())
            continue;

        villageCount++;
        ratio += population->GetFoodSupplyRatio();
    }

    return villageCount > 0 ? ratio / villageCount : 1.0;
}

bool Player::CanResearchTechnology(const std::string& id) const
{
    const ProvinceEconomy* economy = GetProvinceEconomy();
    return economy != nullptr && CanResearchTechnology(id, *economy);
}

bool Player::CanResearchTechnology(const std::string& id,
                                   const ProvinceEconomy& economy) const
{
    const auto* definition = FindTechnologyDefinition(id);
    const TileMap* map = economy.tilemap;
    return definition != nullptr &&
           technologies.CanUnlock(id) &&
           !IsTechnologyInProgress(id) &&
           (map != nullptr && map->params.debugMode || HasBuildResources(economy, definition->costs));
}

bool Player::UnlockFocus(const std::string& id)
{
    const auto* definition = FindFocusDefinition(id);
    if (definition == nullptr || !focuses.CanUnlock(id))
        return false;

    if (!focuses.UnlockFocus(id))
        return false;

    if (auto* economy = GetCampaignEconomy(); economy != nullptr)
        economy->economyTelemetry.RecordResearchMilestone(ResearchMilestoneType::Focus, id);
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
        if (auto* economy = GetCampaignEconomy(); economy != nullptr)
            economy->economyTelemetry.RecordResearchMilestone(ResearchMilestoneType::Focus, id);
        RefreshTechnologyModifiers();
    }
    return true;
}

bool Player::UnlockTechnology(const std::string& id)
{
    const auto* definition = FindTechnologyDefinition(id);
    if (definition == nullptr || !technologies.CanUnlock(id))
        return false;

    ProvinceEconomy* economy = GetCampaignEconomy();
    const TileMap* map = economy != nullptr ? economy->tilemap : nullptr;
    if (economy == nullptr ||
        ((map == nullptr || !map->params.debugMode) && !TryPayBuildCost(*economy, definition->costs)))
        return false;

    if (!technologies.UnlockTechnology(id))
        return false;

    if (economy != nullptr)
        economy->economyTelemetry.RecordResearchMilestone(ResearchMilestoneType::Technology, id);
    RefreshTechnologyModifiers();
    return true;
}

bool Player::StartTechnologyResearch(const std::string& id, Building* university)
{
    ProvinceEconomy* economy = university != nullptr ? university->GetProvinceEconomy() : nullptr;
    return economy != nullptr && StartTechnologyResearch(id, university, *economy);
}

bool Player::StartTechnologyResearch(const std::string& id, Building* university,
                                     ProvinceEconomy& economy)
{
    const auto* definition = FindTechnologyDefinition(id);
    auto* research = university != nullptr ? university->GetComponent<ResearchComponent>() : nullptr;
    if (definition == nullptr || university == nullptr || university->owner != this ||
        university->buildingType != BuildingType::University || research == nullptr ||
        !research->technologyId.empty())
        return false;

    if (university->GetProvinceEconomy() != &economy || !CanResearchTechnology(id, economy))
        return false;

    const TileMap* map = economy.tilemap;
    if ((map == nullptr || !map->params.debugMode) && !TryPayBuildCost(economy, definition->costs))
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
    RefreshProvinceTraitModifiers();
    for (const auto& [provinceId, simulation] : boundProvinces)
        if (simulation != nullptr && simulation->GetEconomy().roadNetwork != nullptr)
        {
            (void)provinceId;
            simulation->GetEconomy().roadNetwork->InvalidateRoutingCosts();
        }
}

void Player::RefreshUpgradeModifiers()
{
    balanceModifiers.ClearSourcePrefix("upgrade:");
    for (const auto& [provinceId, simulation] : boundProvinces)
        if (simulation != nullptr)
            for (Building* building : simulation->GetEconomy().dataTracker.buildings)
            {
                (void)provinceId;
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

    const ProvinceId provinceId = building.provinceId;
    std::string sourcePrefix = "upgrade:" + std::to_string(provinceId) + ":" +
                               std::to_string(building.id) + ":";
    balanceModifiers.ClearSourcePrefix(sourcePrefix);

    const auto& definition = GetBuildingDefinition(building.buildingType);
    const auto* levelDefinition = FindUpgradeLevelDefinition(definition, upgrade->level);
    if (levelDefinition == nullptr)
        return;

    for (BalanceModifier modifier : levelDefinition->modifiers)
    {
        modifier.scope = BalanceModifierScope::Building(provinceId, building.id);
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

void Player::RefreshProvinceTraitModifiers()
{
    balanceModifiers.ClearSourcePrefix("province-trait:");
    for (const auto& [provinceId, simulation] : boundProvinces)
    {
        if (simulation == nullptr)
            continue;
        for (const auto& traitId : simulation->GetEconomy().traitIds)
        {
            // Trait IDs are selected runtime data. The owning province's
            // definition is resolved by scanning the catalog, which keeps the
            // simulation independent from GlobalMap pointers.
            const ProvinceTraitDefinition* trait = nullptr;
            for (const auto& [definitionId, candidate] : GetProvinceDefinitions())
            {
                (void)definitionId;
                const auto it = std::find_if(candidate.traits.begin(), candidate.traits.end(),
                    [&traitId](const ProvinceTraitDefinition& value) { return value.id == traitId; });
                if (it != candidate.traits.end())
                {
                    trait = &*it;
                    break;
                }
            }
            if (trait == nullptr)
                continue;
            for (const auto& effect : trait->effects)
            {
                if (effect.target != ProvinceEffectTarget::SelfProvince)
                    continue;
                BalanceModifier modifier;
                modifier.stat = effect.stat;
                modifier.additive = effect.additive;
                modifier.multiplier = effect.multiplier;
                modifier.scope = BalanceModifierScope::Global();
                modifier.scope.provinceId = provinceId;
                modifier.source = "province-trait:" + std::to_string(provinceId) + ":" + traitId;
                ResourceType resource = ResourceType::Null;
                std::string condition = effect.condition;
                std::transform(condition.begin(), condition.end(), condition.begin(),
                    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                if (TryParseResourceType(condition, resource) && resource != ResourceType::Null)
                    modifier.resourceType = resource;
                balanceModifiers.AddModifier(std::move(modifier));
            }
        }
    }
}

double Player::AddManpower(ProvinceEconomy& economy, double amount)
{
    if (amount <= 0.0)
        return 0.0;
    economy.population.initialized = true;
    const ProvincePopulationView view = BuildProvincePopulationView(economy, *this);
    const double room = std::max(0.0,
        static_cast<double>(view.populationCap) - view.currentPopulation);
    const double added = std::min(amount, room);
    economy.population.availableManpower += added;
    return added;
}

bool Player::ConsumeManpower(ProvinceEconomy& economy, double amount)
{
    // Keep direct legacy callers/save fixtures compatible during the migration
    // to province-local manpower. A live campaign keeps this global pool at
    // zero; if an old caller replenishes it, absorb it exactly once here.
    if (homeProvinceId != InvalidProvinceId && economy.provinceId == homeProvinceId &&
        strategicResources.Get(StrategicResourceType::Manpower) > 0.0)
    {
        economy.population.availableManpower += strategicResources.Get(
            StrategicResourceType::Manpower);
        strategicResources.Set(StrategicResourceType::Manpower, 0.0);
    }
    if (amount < 0.0 || economy.population.availableManpower + 1e-9 < amount)
        return false;
    economy.population.availableManpower = std::max(0.0,
        economy.population.availableManpower - amount);
    return true;
}

int Player::AutoAssignWorkers(Building* building)
{
    if (building == nullptr || building->owner != this)
        return 0;

    if (homeProvinceId != InvalidProvinceId && building->GetProvinceEconomy() != nullptr)
        return AutoAssignWorkers(*building->GetProvinceEconomy(), building);

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

int Player::AutoAssignWorkers(ProvinceEconomy& economy, Building* building)
{
    if (building == nullptr || building->owner != this)
        return 0;
    auto* workers = building->GetComponent<WorkerComponent>();
    if (workers == nullptr)
        return 0;

    const int needed = std::max(0, building->GetWorkerCapacity() - workers->assigned);
    const int available = static_cast<int>(std::floor(
        std::max(0.0, economy.population.availableManpower)));
    const int assigned = std::min(needed, available);
    if (assigned <= 0)
        return 0;

    economy.population.availableManpower -= assigned;
    workers->assigned += assigned;
    return assigned;
}

ProvincePopulationView Player::GetProvincePopulationView(ProvinceId provinceId) const
{
    const ProvinceEconomy* economy = GetProvinceEconomy(provinceId);
    return economy == nullptr ? ProvincePopulationView{} :
        BuildProvincePopulationView(*economy, *this);
}

bool Player::TryPayBuildCost(const std::vector<ResourceAmountDefinition>& costs)
{
    ProvinceEconomy* economy = GetProvinceEconomy();
    return economy != nullptr && TryPayBuildCost(*economy, costs);
}

bool Player::TryPayBuildCost(ProvinceEconomy& economy,
                             const std::vector<ResourceAmountDefinition>& costs)
{
    if (!HasBuildResources(economy, costs))
        return false;

    // Paid out of the warehouse network only, in building-id order — see
    // StockpileIndex for why that scope (and that ordering) is the one every
    // "how much do I have" answer in the game shares.
    for (const auto& cost : costs)
        StockpileIndex::Consume(economy, cost.type, cost.amount);

    return true;
}

bool Player::BeginTransport(Building* src, Building* dest, Resource* res)
{
    ProvinceEconomy* economy = src != nullptr ? src->GetProvinceEconomy() : nullptr;
    if (economy == nullptr || economy->roadNetwork == nullptr ||
        src == nullptr || dest == nullptr || res == nullptr ||
        src->owner != this || dest->owner != this ||
        src->provinceEconomy != economy || dest->provinceEconomy != economy)
        return false;
    return economy->roadNetwork->BeginTransport(src, dest, res);
}

void Player::RefundBuildCost(const std::vector<ResourceAmountDefinition>& costs)
{
    ProvinceEconomy* economy = GetProvinceEconomy();
    if (economy != nullptr)
        RefundBuildCost(*economy, costs);
}

void Player::RefundBuildCost(ProvinceEconomy& economy,
                             const std::vector<ResourceAmountDefinition>& costs)
{
    // Pour the resources back into the network they were paid from. Buffers
    // that already hold this type have the capacity we freed at build time;
    // anything that no longer fits (production refilled it) spills.
    for (const auto& cost : costs)
        if (cost.amount > 0)
            StockpileIndex::Deposit(economy, cost.type, cost.amount);
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
    const ProvinceEconomy* economy = player.GetProvinceEconomy();
    return economy != nullptr ? economy->economyTelemetry.BuildSnapshot(time) : ResourceFlowSnapshot{};
}

void Player::RegisterBuilding(Building* building)
{
    ProvinceEconomy* economy = GetProvinceEconomy();
    if (economy != nullptr)
        economy->RegisterBuilding(building);
}

void Player::UnregisterBuilding(Building* building)
{
    ProvinceEconomy* economy = GetProvinceEconomy();
    if (economy != nullptr)
        economy->UnregisterBuilding(building);
}
