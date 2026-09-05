#include "economy/Building.h"
#include "economy/Player.h"
#include "core/Log.h"
#include "simulation/MapGenerator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace
{
    ProvinceEconomy* GetLocalEconomy(Building& building)
    {
        if (building.provinceEconomy != nullptr)
            return building.provinceEconomy;
        return building.owner != nullptr ? building.owner->GetProvinceEconomy() : nullptr;
    }
}

// ─── ProductionComponent ─────────────────────────────────────────────────────

void RoadComponent::Update(Building& self, double dt)
{
    const int capacity = std::max(1, GetModifiedMaxCapacity(self));
    const double instantUtilization = std::clamp(
        static_cast<double>(self.transportables.size()) / static_cast<double>(capacity),
        0.0, 1.0);

    // A ten-second time constant filters individual resource packages while
    // still reacting to a developing bottleneck within a few seconds.
    constexpr double TrendTimeConstantSeconds = 10.0;
    const double alpha = 1.0 - std::exp(-std::max(0.0, dt) / TrendTimeConstantSeconds);
    trafficUtilizationEma += (instantUtilization - trafficUtilizationEma) * alpha;
    trafficUtilizationEma = std::clamp(trafficUtilizationEma, 0.0, 1.0);

    if (static_cast<int>(self.transportables.size()) >= capacity)
        saturationIndicatorRemaining = 1.25;
    else
        saturationIndicatorRemaining = std::max(0.0, saturationIndicatorRemaining - dt);
}

int RoadComponent::GetModifiedMaxCapacity(const Building& self) const
{
    return self.owner != nullptr
        ? self.owner->ResolveStat(maxCapacity, &self, ResourceType::Null, 0)
        : maxCapacity.GetBase();
}

double RoadComponent::GetModifiedSpeedModifier(const Building& self) const
{
    return self.owner != nullptr
        ? self.owner->ResolveStat(speedModifier, &self)
        : speedModifier.GetBase();
}

double RoadComponent::GetTrafficUtilizationTrend() const
{
    return std::clamp(trafficUtilizationEma, 0.0, 1.0);
}

bool RoadComponent::HasRecentSaturation() const
{
    return saturationIndicatorRemaining > 0.0;
}

ProductionComponent::ProductionComponent()
    : terrainType(TileType::GRASS)
{}

void ProductionComponent::Update(Building& self, double dt)
{
    if (self.owner != nullptr)
        self.owner->AutoAssignWorkers(&self);

    auto* logistics = self.GetComponent<LogisticsComponent>();
    if (logistics != nullptr)
    {
        logistics->requestBlocked = false;
        if (ShouldRequestInputs(self))
            logistics->MaintainRequests(self, *this);
    }

    Produce(self, dt);

    if (logistics != nullptr)
        logistics->DispatchOutputs(self, *this);
}

bool ProductionComponent::ShouldRequestInputs(const Building& self) const
{
    if (self.IsUnderConstruction() || self.IsProductionBlocked() || products.empty())
        return false;

    const auto* workers = self.GetComponent<WorkerComponent>();
    if (workers == nullptr || workers->GetRatio() <= 0.0f)
        return false;

    const double effectiveCycleTime = GetModifiedCycleTime(self);
    if (!(effectiveCycleTime > 0.0) || !std::isfinite(effectiveCycleTime))
        return false;
    if (started)
        return false;

    for (const auto& [type, baseAmount] : products)
    {
        const auto it = outputBuffers.find(type);
        if (it == outputBuffers.end())
            return false;
        const int outputAmount = GetModifiedOutputAmount(self, type, baseAmount);
        if (outputAmount < 0 ||
            static_cast<int>(it->second.buffer.size()) + outputAmount > it->second.bufferSize)
            return false;
    }
    return true;
}

void ProductionComponent::Produce(Building& self, double dt)
{
    if (self.IsProductionBlocked()) return;

    auto* workers   = self.GetComponent<WorkerComponent>();
    auto* logistics = self.GetComponent<LogisticsComponent>();
    double ratio = workers != nullptr ? workers->GetRatio() : 0.0f;
    double workerEff = self.owner != nullptr && self.GetProvinceEconomy() != nullptr
        ? ratio * self.owner->GetFoodProductivity(*self.GetProvinceEconomy()) : ratio;
    if (workerEff <= 0.0) return;

    bool terrainBased = ingredients.empty() && terrainType != TileType::GRASS;
    double effectiveCycleTime = GetModifiedCycleTime(self);

    if (started)
    {
        if (elapsed >= effectiveCycleTime)
        {
            for (auto& [res, amount] : products)
            {
                int modAmount = GetModifiedOutputAmount(self, res, amount);
                for (int i = 0; i < modAmount; i++)
                {
                    if (terrainBased && consumesTerrain && !ConsumeTerrainRichness(self))
                        break;

                    outputBuffers[res].GenerateResource(res);
                    if (ProvinceEconomy* economy = GetLocalEconomy(self); economy != nullptr)
                        economy->economyTelemetry.RecordProduction(res);
                    self.totalProduced++;
                    totalProduced++;
                    TVORIN_LOG_TRACE(self.tag, "Created a resource: ", rt2s(res));
                    if (logistics != nullptr)
                        logistics->DispatchOutputs(self, *this);
                }
            }
            started = false;
            elapsed = 0.0;
            // Fall through to the "try to start a new cycle" check below
            // instead of waiting for the next Update() tick — otherwise a
            // fully-buffered production chain shows a visible stall/reset on
            // the progress bar for one simulation tick between 100% and the
            // next cycle actually starting, even though nothing was blocking it.
        }
        else
        {
            elapsed         += dt * workerEff;
            self.activeTime += dt * workerEff;
            return;
        }
    }

    for (auto& [res, buf] : outputBuffers)
        if (buf.buffer.size() >= static_cast<size_t>(buf.bufferSize))
            return;

    bool canStart = true;
    if (terrainBased && !HasTerrainRichness(self))
        canStart = false;

    for (auto& [res, amount] : ingredients)
        if (inputBuffers[res].buffer.size() < static_cast<size_t>(amount))
            canStart = false;

    if (canStart)
    {
        for (auto& [res, amount] : ingredients)
        {
            for (int i = 0; i < amount; i++)
            {
                inputBuffers[res].FreeResource();
                if (ProvinceEconomy* economy = GetLocalEconomy(self); economy != nullptr)
                    economy->economyTelemetry.RecordConsumption(res);
            }
            // MaintainRequests() is the single owner of inbound requests.
            // Do not create a second shipment path immediately after
            // consuming this cycle's ingredients.
        }
        started = true;
    }
}

float ProductionComponent::GetProgress(const Building& self) const
{
    if (!started || cycleTime.GetBase() <= 0.0)
        return 0.0f;
    // Bug fix (2026-07-12): this used to divide by cycleTime.GetBase() (the
    // raw, unmodified cycle time), while Produce() decides the cycle is done
    // when elapsed >= GetModifiedCycleTime(self) (tech/focus adjusted). Any
    // active modifier on ProductionCycleTime made these two thresholds
    // different: a speed-up modifier completed the cycle
    // before the bar ever visually reached 100% (it jumped from some lower
    // percentage straight to a reset 0%), while a slow-down modifier made the
    // bar hit the 100% clamp and then visibly sit there, stalled, until the
    // real (larger) modified cycle time finally elapsed — exactly the
    // reported "production freezes for a moment right at 100%".
    double effective = GetModifiedCycleTime(self);
    if (effective <= 0.0)
        return 1.0f;
    return std::clamp(static_cast<float>(elapsed / effective), 0.0f, 1.0f);
}

double ProductionComponent::GetModifiedCycleTime(const Building& self) const
{
    return self.owner != nullptr
        ? self.owner->ResolveStat(cycleTime, &self)
        : cycleTime.GetBase();
}

double ProductionComponent::GetEffectiveCycleTime(const Building& self) const
{
    double modified = GetModifiedCycleTime(self);
    if (modified <= 0.0) return 0.0;

    const auto* workers = self.GetComponent<WorkerComponent>();
    double eff = workers != nullptr ? workers->GetRatio() : 0.0;
    if (self.owner != nullptr && self.GetProvinceEconomy() != nullptr)
        eff *= self.owner->GetFoodProductivity(*self.GetProvinceEconomy());
    if (eff <= 0.0) return std::numeric_limits<double>::infinity();

    return modified / eff;
}

int ProductionComponent::GetModifiedOutputAmount(const Building& self, ResourceType type,
                                                  int base) const
{
    return self.owner != nullptr
        ? self.owner->ModifyBalanceIntForBuilding(BalanceStat::ProductionOutputAmount,
                                                   base, &self, type, 0)
        : base;
}

bool ProductionComponent::HasTerrainRichness(const Building& self) const
{
    if (self.provinceEconomy == nullptr || self.provinceEconomy->tilemap == nullptr ||
        self.positionId < 0 || terrainType == TileType::GRASS)
        return false;

    TileMap* tilemap = self.provinceEconomy->tilemap;
    Vec2i anchor = tilemap->GetCoordsFromId(self.positionId);
    for (int y = 0; y < self.footprint.y; y++)
    {
        for (int x = 0; x < self.footprint.x; x++)
        {
            Vec2i pos{anchor.x + x, anchor.y + y};
            if (!tilemap->IsInside(pos))
                continue;
            const Tile& tile = tilemap->tilemap[tilemap->GetIdFromCoords(pos)];
            if (tile.tileType == terrainType && tile.resourceRichness > 0)
                return true;
        }
    }
    return false;
}

bool ProductionComponent::ConsumeTerrainRichness(Building& self)
{
    if (self.provinceEconomy == nullptr || self.provinceEconomy->tilemap == nullptr ||
        self.positionId < 0 || terrainType == TileType::GRASS)
        return false;

    TileMap* tilemap = self.provinceEconomy->tilemap;
    Vec2i anchor = tilemap->GetCoordsFromId(self.positionId);
    for (int y = 0; y < self.footprint.y; y++)
    {
        for (int x = 0; x < self.footprint.x; x++)
        {
            Vec2i pos{anchor.x + x, anchor.y + y};
            if (!tilemap->IsInside(pos))
                continue;
            Tile& tile = tilemap->tilemap[tilemap->GetIdFromCoords(pos)];
            if (tile.tileType != terrainType || tile.resourceRichness <= 0)
                continue;

            tile.resourceRichness--;
            if (tile.resourceRichness <= 0)
            {
                tile.tileType = TileType::GRASS;
                std::mt19937 rng(static_cast<unsigned int>(tile.id + tilemap->params.seed));
                tile.terrainTextureId = tilemap->PickTerrainTexture(TileType::GRASS, rng);
                tile.resourceOverlayTextureId = -1;
                tilemap->terrainDirty = true;
            }
            return true;
        }
    }
    return false;
}

std::vector<ResourceBufferView> ProductionComponent::GetInputBufferViews(
    const std::map<ResourceType, int>& recipe) const
{
    std::vector<ResourceBufferView> result;
    for (const auto& [res, buf] : inputBuffers)
    {
        int recipeAmount = 0;
        auto it = recipe.find(res);
        if (it != recipe.end())
            recipeAmount = it->second;
        result.push_back({res, static_cast<int>(buf.buffer.size()), buf.bufferSize, recipeAmount});
    }
    return result;
}

std::vector<ResourceBufferView> ProductionComponent::GetOutputBufferViews(
    const Building& self) const
{
    std::vector<ResourceBufferView> result;
    for (const auto& [res, buf] : outputBuffers)
    {
        int recipeAmount = 0;
        auto it = products.find(res);
        if (it != products.end())
            recipeAmount = GetModifiedOutputAmount(self, res, it->second);
        result.push_back({res, static_cast<int>(buf.buffer.size()), buf.bufferSize, recipeAmount});
    }
    return result;
}


bool RecipeComponent::HasSelectableRecipes() const
{
    return recipes.size() > 1;
}

bool RecipeComponent::IsRecipeAvailable(const Building& self, int index) const
{
    if (index < 0 || index >= static_cast<int>(recipes.size()))
        return false;

    // Configuration can be applied while a building is being restored, before
    // an owner is attached.  In that narrow case the normal build/research
    // validation will still prevent play from reaching an invalid state.
    if (self.owner == nullptr)
        return true;

    const auto& recipe = recipes[index];
    for (const auto& technology : recipe.requiredTechnologies)
        if (!self.owner->technologies.HasTechnology(technology))
            return false;
    for (const auto& focus : recipe.requiredFocuses)
        if (!self.owner->focuses.HasFocus(focus))
            return false;
    return true;
}

std::string RecipeComponent::GetActiveRecipeName() const
{
    if (activeRecipeIndex < 0 || activeRecipeIndex >= static_cast<int>(recipes.size()))
        return "Default";
    return recipes[activeRecipeIndex].name.empty() ? "Default" : recipes[activeRecipeIndex].name;
}

void RecipeComponent::SetRecipes(std::vector<ProductionRecipeRuntime> newRecipes,
                                 Building& self,
                                 ProductionComponent& production,
                                 LogisticsComponent& logistics,
                                 WorkerComponent& workers)
{
    recipes = std::move(newRecipes);
    activeRecipeIndex = 0;
    for (int index = 0; index < static_cast<int>(recipes.size()); index++)
    {
        if (SetActiveRecipe(index, self, production, logistics, workers))
            return;
    }
}

bool RecipeComponent::SetActiveRecipe(int index,
                                      Building& self,
                                      ProductionComponent& production,
                                      LogisticsComponent& logistics,
                                      WorkerComponent& workers)
{
    if (index < 0 || index >= static_cast<int>(recipes.size()))
        return false;
    if (!IsRecipeAvailable(self, index))
        return false;

    for (auto& [res, buf] : production.inputBuffers)  buf.Clear();
    for (auto& [res, buf] : production.outputBuffers) buf.Clear();

    activeRecipeIndex = index;
    const auto& recipe = recipes[activeRecipeIndex];

    production.cycleTime = recipe.cycleTime;
    production.ingredients = recipe.inputs;
    production.products    = recipe.outputs;
    production.inputBuffers.clear();
    production.outputBuffers.clear();
    logistics.suppliers.clear();
    logistics.receivers.clear();
    logistics.altReceivers.clear();
    logistics.pendingRequests.clear();
    logistics.requestBlocked = false;
    production.elapsed  = 0.0;
    production.started  = false;

    for (const auto& [res, cap] : recipe.inputBufferCapacities)
        production.inputBuffers[res] = ResourceBuffer{res, cap};
    for (const auto& [res, cap] : recipe.outputBufferCapacities)
        production.outputBuffers[res] = ResourceBuffer{res, cap};

    workers.capacity = std::max(0, recipe.workerCapacity);
    workers.assigned = std::min(workers.assigned, workers.capacity.GetBase());
    return true;
}

bool RecipeComponent::CycleRecipe(Building& self,
                                  ProductionComponent& production,
                                  LogisticsComponent& logistics,
                                  WorkerComponent& workers)
{
    if (recipes.size() <= 1)
        return false;
    for (int offset = 1; offset < static_cast<int>(recipes.size()); offset++)
    {
        int next = (activeRecipeIndex + offset) % static_cast<int>(recipes.size());
        if (SetActiveRecipe(next, self, production, logistics, workers))
            return true;
    }
    return false;
}

// ─── ResearchComponent ───────────────────────────────────────────────────────

bool ResearchComponent::Start(const std::string& id, double time)
{
    if (!technologyId.empty())
        return false;
    technologyId = id;
    total = std::max(0.0, time);
    remaining = total;
    return true;
}

bool ResearchComponent::Tick(double dt)
{
    if (technologyId.empty())
        return false;
    remaining = std::max(0.0, remaining - std::max(0.0, dt));
    return remaining <= 0.0;
}

double ResearchComponent::GetProgress() const
{
    if (technologyId.empty() || total <= 0.0)
        return 0.0;
    return std::clamp(1.0 - remaining / total, 0.0, 1.0);
}

