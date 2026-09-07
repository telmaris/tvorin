#ifndef PROVINCE_POPULATION_H
#define PROVINCE_POPULATION_H

#include "world/WorldIds.h"

#include <vector>

class Building;
class Player;
struct ProvinceEconomy;

struct ProvincePopulationState
{
    double availableManpower{0.0};
    bool initialized{false};
};

struct ProvincePopulationView
{
    ProvinceId provinceId{InvalidProvinceId};
    double availableManpower{0.0};
    int assignedWorkers{0};
    double stationedSoldiers{0.0};
    double currentPopulation{0.0};
    int populationCap{0};
    double manpowerGainPerMinute{0.0};
};

// Stable, order-preserving allocation used by the simulation and presentation
// code. Residents are split proportionally to village capacity, and the
// deterministic remainder is assigned in caller-provided building order.
std::vector<double> AllocateProvinceVillageResidents(
    double population, const std::vector<double>& capacities);

ProvincePopulationView BuildProvincePopulationView(
    const ProvinceEconomy& economy, const Player& player);

#endif
