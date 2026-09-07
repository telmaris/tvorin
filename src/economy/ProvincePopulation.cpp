#include "economy/ProvincePopulation.h"

#include "economy/Building.h"
#include "economy/BuildingComponents.h"
#include "economy/Player.h"
#include "warfare/UnitDefinition.h"
#include "world/ProvinceSimulation.h"

#include <algorithm>
#include <cmath>
#include <numeric>

std::vector<double> AllocateProvinceVillageResidents(
    double population, const std::vector<double>& capacities)
{
    std::vector<double> result(capacities.size(), 0.0);
    const double totalCapacity = std::accumulate(capacities.begin(), capacities.end(), 0.0,
        [](double total, double capacity) { return total + std::max(0.0, capacity); });
    if (!(population > 0.0) || !(totalCapacity > 0.0))
        return result;

    const double target = std::min(population, totalCapacity);
    double allocated = 0.0;
    for (size_t i = 0; i < capacities.size(); i++)
    {
        const double capacity = std::max(0.0, capacities[i]);
        result[i] = target * capacity / totalCapacity;
        allocated += result[i];
    }

    // Remove floating-point drift from the final slot without changing the
    // stable proportional order.
    if (!result.empty())
        result.back() = std::clamp(result.back() + target - allocated,
                                   0.0, std::max(0.0, capacities.back()));
    return result;
}

ProvincePopulationView BuildProvincePopulationView(
    const ProvinceEconomy& economy, const Player& player)
{
    ProvincePopulationView view;
    view.provinceId = economy.provinceId;
    view.availableManpower = std::max(0.0, economy.population.availableManpower);

    for (const Building* building : economy.dataTracker.buildings)
    {
        if (building == nullptr || building->owner != &player || building->IsUnderConstruction())
            continue;

        if (const auto* workers = building->GetComponent<WorkerComponent>())
            view.assignedWorkers += std::max(0, workers->assigned);

        if (const auto* population = building->GetComponent<PopulationComponent>())
        {
            view.populationCap += player.ResolveStat(population->populationCap, building);
            view.manpowerGainPerMinute += player.ResolveStat(population->manpowerRate, building) *
                population->GetManpowerProductivity() * 60.0;
        }
    }

    for (const auto& [instanceId, unit] : player.roster.units)
    {
        (void)instanceId;
        if (unit.assignment.provinceId != economy.provinceId ||
            unit.assignment.kind == UnitAssignmentKind::Journey ||
            unit.assignment.kind == UnitAssignmentKind::Battle)
            continue;
        if (const auto* definition = FindUnitDefinition(unit.unitDefId))
            view.stationedSoldiers += std::max(0.0, definition->manpowerCost);
    }

    view.currentPopulation = view.availableManpower +
        static_cast<double>(view.assignedWorkers) + view.stationedSoldiers;
    return view;
}
