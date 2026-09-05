#ifndef GARRISON_SERVICE_H
#define GARRISON_SERVICE_H

#include "world/WorldIds.h"

#include <map>
#include <string>
#include <vector>

class Building;
class Player;
struct ProvinceEconomy;

struct GarrisonSummary
{
    int used{0};
    int capacity{0};
    double upkeepPerMinute{0.0};
    int bufferedFood{0};
    int incomingFood{0};
    int duePackages{0};
    std::map<std::string, int> unitCounts;
};

// Authority-side local garrison transfer and upkeep service. It works against
// a province's building registry and the player's canonical UnitRoster; it
// never stores unit pointers or duplicates assignments in GarrisonComponent.
class GarrisonService
{
public:
    static bool AssignUnitsToGarrison(Player& player, ProvinceEconomy& province,
                                      int sourceBarracksId, int defenseBuildingId,
                                      const std::vector<int>& unitIds,
                                      std::string& failureReason);
    static bool ReturnUnitsToBarracks(Player& player, ProvinceEconomy& province,
                                      int defenseBuildingId, int targetBarracksId,
                                      const std::vector<int>& unitIds,
                                      std::string& failureReason);

    static std::vector<int> GetGarrisonedUnitIds(const Player& player,
                                                  ProvinceId provinceId,
                                                  int defenseBuildingId);
    static GarrisonSummary BuildSummary(const Player& player,
                                         const ProvinceEconomy& province,
                                         const Building& defenseBuilding);

    // Advances one building's upkeep. Called by GarrisonUpkeepComponent during
    // the normal province update so logistics and road routing remain unified.
    static void UpdateBuilding(Building& defenseBuilding, double dt);

private:
    static Building* FindBuilding(ProvinceEconomy& province, int buildingId);
};

#endif
