#include "economy/Building.h"
#include "economy/Player.h"
#include "economy/StockpileIndex.h"
#include "simulation/MapGenerator.h"
#include "BuildingComponentsInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    ProvinceEconomy* GetLocalEconomy(Building& building)
    {
        if (building.provinceEconomy != nullptr)
            return building.provinceEconomy;
        return building.owner != nullptr ? building.owner->GetProvinceEconomy() : nullptr;
    }
}

void PopulationComponent::Update(Building& self, double dt)
{
    if (self.owner == nullptr)
        return;

    for (const auto& rule : levelSupplyRules[std::clamp(settlementLevel, 1, 3)])
        RequestSupply(self, rule.resource);

    bool hasBufferedFood = !foodBuffer.buffer.empty();
    bool hasIncomingFood = CountIncomingResources(&self, ResourceType::FOOD_PROVISIONS) > 0;
    if (!hasBufferedFood && !hasIncomingFood)
    {
        const double decaySeconds = std::isfinite(foodShortageDecaySeconds) &&
                                    foodShortageDecaySeconds > 0.0
            ? foodShortageDecaySeconds : 180.0;
        foodSupplyLevel = std::max(0.0, foodSupplyLevel -
            std::max(0.0, dt) / decaySeconds);
    }

    auto consumeSupply = [&](ResourceType type, double& supplyLevel, int packageCount)
    {
        ResourceBuffer* buffer = GetSupplyBuffer(type);
        const VillageSupplyRuleDefinition* rule = FindSupplyRule(type);
        const int packageAmount = !hasAssignedResidents
            ? GetSupplyUpkeep(type)
            : rule != nullptr ? rule->packageAmount : GetSupplyUpkeep(type);
        int needed = std::max(0, packageCount * packageAmount);
        if (buffer != nullptr && static_cast<int>(buffer->buffer.size()) >= needed)
        {
            for (int i = 0; i < needed; i++)
            {
                buffer->FreeResource();
                if (ProvinceEconomy* economy = GetLocalEconomy(self); economy != nullptr)
                    economy->economyTelemetry.RecordConsumption(type);
            }
            supplyLevel = std::min(1.0, supplyLevel + 0.45 * std::max(1, packageCount));
        }
        else
        {
            if (type != ResourceType::FOOD_PROVISIONS)
                supplyLevel = std::max(0.0, supplyLevel -
                    nonFoodSupplyDropPerMissedUpkeep * std::max(1, packageCount));
        }
    };

    if (hasAssignedResidents)
    {
        auto updateDataDrivenSupply = [&](ResourceType type, double& supplyLevel)
        {
            const auto* rule = FindSupplyRule(type);
            if (rule == nullptr)
            {
                supplyDebt[type] = 0.0;
                return;
            }

            const double consumptionPerMinute = GetSupplyPackagesPerMinute(self, type);
            if (!(consumptionPerMinute > 0.0) || !std::isfinite(consumptionPerMinute))
                return;

            double& debt = supplyDebt[type];
            debt += consumptionPerMinute * std::max(0.0, dt) / 60.0;
            const int due = std::min(10000, static_cast<int>(std::floor(debt + 1e-9)));
            if (due <= 0)
                return;
            debt -= due;
            consumeSupply(type, supplyLevel, due);
        };

        updateDataDrivenSupply(ResourceType::FOOD_PROVISIONS, foodSupplyLevel);
        updateDataDrivenSupply(ResourceType::HOUSEHOLD_GOODS, householdSupplyLevel);
        updateDataDrivenSupply(ResourceType::URBAN_GOODS, urbanSupplyLevel);
    }

    auto updateSupplyUpkeep = [&](ResourceType type, double& supplyLevel, double& timer)
    {
        if (hasAssignedResidents)
            return;
        if (!RequiresSupply(type))
        {
            timer = 0.0;
            return;
        }

        const double effectiveInterval = GetEffectiveSupplyUpkeepInterval(self, type);
        if (!std::isfinite(effectiveInterval))
            return;

        timer += dt;
        // Preserve overflow instead of resetting the timer. This makes the
        // long-run average exact even when the modified interval is not an
        // integer multiple of the fixed simulation tick (e.g. 60 / 0.9).
        const double thresholdEpsilon = 1e-9 * std::max(1.0, effectiveInterval);
        while (timer + thresholdEpsilon >= effectiveInterval)
        {
            timer = std::max(0.0, timer - effectiveInterval);
            consumeSupply(type, supplyLevel, 1);
        }
    };

    updateSupplyUpkeep(ResourceType::FOOD_PROVISIONS, foodSupplyLevel, upkeepTimer);
    updateSupplyUpkeep(ResourceType::HOUSEHOLD_GOODS, householdSupplyLevel, householdUpkeepTimer);
    updateSupplyUpkeep(ResourceType::URBAN_GOODS, urbanSupplyLevel, urbanUpkeepTimer);
    hasFood = foodSupplyLevel > 0.0;

    if (settlementLevel > 1)
    {
        int effectiveLevel = GetActiveSettlementLevel();
        populationCap = levelPopulationCaps[effectiveLevel];
        manpowerRate = levelManpowerRates[effectiveLevel];
    }

    double efficiency = GetManpowerProductivity();
    double modRate = self.owner->ResolveStat(manpowerRate, &self);
    ProvinceEconomy* economy = GetLocalEconomy(self);
    if (economy != nullptr && self.owner->homeProvinceId != InvalidProvinceId)
        self.owner->AddManpower(*economy, modRate * efficiency * dt);
    else
        self.owner->AddManpower(modRate * efficiency * dt);
    self.activeTime += dt * efficiency;
}

double PopulationComponent::GetFoodSupplyRatio() const
{
    return std::clamp(foodSupplyLevel, 0.0, 1.0);
}

double PopulationComponent::GetManpowerProductivity() const
{
    return GetFoodSupplyRatio();
}

double PopulationComponent::GetWorkerProductivity() const
{
    return 0.3 + 0.7 * GetFoodSupplyRatio();
}

int PopulationComponent::RequestFoodSupply(Building& self)
{
    return RequestSupply(self, ResourceType::FOOD_PROVISIONS);
}

int PopulationComponent::RequestSupply(Building& self, ResourceType type)
{
    ResourceBuffer* buffer = GetSupplyBuffer(type);
    if (self.owner == nullptr || buffer == nullptr || !RequiresSupply(type) ||
        static_cast<int>(buffer->buffer.size()) >= buffer->bufferSize)
        return 0;

    int stored = static_cast<int>(buffer->buffer.size());
    int incoming = CountIncomingResources(&self, type);
    int missing = buffer->bufferSize - stored - incoming;
    if (missing <= 0)
        return 0;

    for (Building* warehouse : StockpileIndex::RankSourcesFor(type, self))
    {
        missing -= warehouse->HandleTransport(type, missing, &self);
        if (missing <= 0)
            break;
    }
    return std::max(0, missing);
}

int PopulationComponent::GetFoodDemand() const
{
    return std::max(0, foodBuffer.bufferSize - static_cast<int>(foodBuffer.buffer.size()));
}

void PopulationComponent::SetSettlementLevel(int level)
{
    settlementLevel = std::clamp(level, 1, 3);
    populationCap = levelPopulationCaps[settlementLevel];
    manpowerRate = levelManpowerRates[settlementLevel];

    // Keep one complete payment plus one local reserve for every active
    // data-defined stream. The physical buffers remain small and deterministic
    // even when resident counts make the rate dynamic.
    for (ResourceType type : {ResourceType::FOOD_PROVISIONS,
                              ResourceType::HOUSEHOLD_GOODS,
                              ResourceType::URBAN_GOODS})
    {
        ResourceBuffer* buffer = GetSupplyBuffer(type);
        if (buffer == nullptr)
            continue;
        // Keep the legacy physical reserve size for unassigned/old saves.
        // Campaign villages use the data-driven resident rate for actual
        // consumption; the larger reserve is harmless and preserves existing
        // save fixtures until the next allocation tick.
        const int capacity = RequiresSupply(type) ? GetSupplyUpkeep(type) + 1 : 0;
        buffer->bufferSize = std::max(0, capacity);
        while (static_cast<int>(buffer->buffer.size()) > buffer->bufferSize)
            buffer->FreeResource();
    }
}

int PopulationComponent::GetActivePopulationCap() const
{
    return levelPopulationCaps[GetActiveSettlementLevel()];
}

int PopulationComponent::GetActiveSettlementLevel() const
{
    int activeLevel = settlementLevel;
    if (activeLevel >= 2 && householdSupplyLevel < 0.5)
        activeLevel = 1;
    else if (activeLevel >= 3 && urbanSupplyLevel < 0.5)
        activeLevel = 2;
    return activeLevel;
}

bool PopulationComponent::RequiresSupply(ResourceType type) const
{
    if (!hasAssignedResidents)
    {
        if (type == ResourceType::FOOD_PROVISIONS)
            return true;
        if (type == ResourceType::HOUSEHOLD_GOODS)
            return settlementLevel >= 2;
        if (type == ResourceType::URBAN_GOODS)
            return settlementLevel >= 3;
        return false;
    }
    return FindSupplyRule(type) != nullptr;
}

int PopulationComponent::GetSupplyUpkeep(ResourceType type) const
{
    if (!hasAssignedResidents)
    {
        if (type == ResourceType::FOOD_PROVISIONS)
            return settlementLevel == 1 ? 1 : settlementLevel == 2 ? 3 : 10;
        if (type == ResourceType::HOUSEHOLD_GOODS)
            return settlementLevel == 2 ? 1 : settlementLevel >= 3 ? 3 : 0;
        if (type == ResourceType::URBAN_GOODS)
            return settlementLevel >= 3 ? 1 : 0;
        return 0;
    }
    const auto* rule = FindSupplyRule(type);
    if (rule != nullptr)
        return rule->packageAmount;
    return 0;
}

double PopulationComponent::GetEffectiveSupplyUpkeepInterval(
    const Building& self, ResourceType type) const
{
    if (upkeepInterval <= 0.0)
        return std::numeric_limits<double>::infinity();

    const double consumptionMultiplier = self.owner != nullptr
        ? self.owner->ModifyBalanceForBuilding(
            BalanceStat::VillageSupplyConsumption, 1.0, &self, type)
        : 1.0;
    if (consumptionMultiplier <= 0.0)
        return std::numeric_limits<double>::infinity();

    if (hasAssignedResidents)
    {
        const double packagesPerMinute = GetSupplyPackagesPerMinute(self, type);
        return packagesPerMinute > 0.0
            ? 60.0 / packagesPerMinute
            : std::numeric_limits<double>::infinity();
    }

    // A multiplier describes consumption, not speed. Therefore a -10%
    // modifier (0.9) lengthens 60 s to 66.666... s and yields exactly
    // 0.9 package per minute over time for a one-package upkeep payment.
    return upkeepInterval / consumptionMultiplier;
}

ResourceBuffer* PopulationComponent::GetSupplyBuffer(ResourceType type)
{
    if (type == ResourceType::FOOD_PROVISIONS) return &foodBuffer;
    if (type == ResourceType::HOUSEHOLD_GOODS) return &householdGoodsBuffer;
    if (type == ResourceType::URBAN_GOODS) return &urbanGoodsBuffer;
    return nullptr;
}

const ResourceBuffer* PopulationComponent::GetSupplyBuffer(ResourceType type) const
{
    if (type == ResourceType::FOOD_PROVISIONS) return &foodBuffer;
    if (type == ResourceType::HOUSEHOLD_GOODS) return &householdGoodsBuffer;
    if (type == ResourceType::URBAN_GOODS) return &urbanGoodsBuffer;
    return nullptr;
}

const VillageSupplyRuleDefinition* PopulationComponent::FindSupplyRule(ResourceType type) const
{
    const auto& rules = levelSupplyRules[std::clamp(settlementLevel, 1, 3)];
    const auto it = std::find_if(rules.begin(), rules.end(),
        [type](const VillageSupplyRuleDefinition& rule) { return rule.resource == type; });
    return it == rules.end() ? nullptr : &*it;
}

double PopulationComponent::GetSupplyPackagesPerMinute(const Building& self, ResourceType type) const
{
    const auto* rule = FindSupplyRule(type);
    if (rule == nullptr || rule->intervalSeconds <= 0.0 || rule->residentsPerPackage <= 0.0)
        return 0.0;

    const double baseRate = assignedResidents / rule->residentsPerPackage *
                            static_cast<double>(rule->packageAmount) *
                            (60.0 / rule->intervalSeconds);
    const double multiplier = self.owner != nullptr
        ? self.owner->ModifyBalanceForBuilding(
            BalanceStat::VillageSupplyConsumption, 1.0, &self, type)
        : 1.0;
    return std::max(0.0, baseRate * multiplier);
}

std::vector<PopulationComponent::SupplyConsumptionView>
PopulationComponent::GetSupplyConsumptionViews(const Building& self) const
{
    std::vector<SupplyConsumptionView> views;
    const auto& rules = levelSupplyRules[std::clamp(settlementLevel, 1, 3)];
    for (const auto& rule : rules)
    {
        const ResourceBuffer* buffer = GetSupplyBuffer(rule.resource);
        SupplyConsumptionView view;
        view.resource = rule.resource;
        view.packageAmount = rule.packageAmount;
        view.packagesPerMinute = GetSupplyPackagesPerMinute(self, rule.resource);
        view.storedPackages = buffer == nullptr ? 0 :
            static_cast<int>(buffer->buffer.size()) / std::max(1, rule.packageAmount);
        view.supplyLevel = rule.resource == ResourceType::FOOD_PROVISIONS ? foodSupplyLevel :
            rule.resource == ResourceType::HOUSEHOLD_GOODS ? householdSupplyLevel : urbanSupplyLevel;
        views.push_back(view);
    }
    return views;
}
