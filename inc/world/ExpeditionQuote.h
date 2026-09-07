#ifndef WORLD_EXPEDITION_QUOTE_H
#define WORLD_EXPEDITION_QUOTE_H

#include "world/Expedition.h"
#include "world/JourneyTiming.h"
#include "world/RouteIncidentRisk.h"

#include <cstdint>
#include <string>
#include <vector>

class GlobalMap;
class Player;
class UnitRoster;

struct ExpeditionQuote
{
    bool allowed{false};
    std::string reason;
    JourneyTimingQuote travel;
    JourneySpeedProfile speedProfile{};
    int negativeIncidentRiskBasisPoints{0};
    std::vector<int> legRiskBasisPoints;
    int scoutEscortCount{0};
    ExpeditionLoadout loadout;
};

class ExpeditionQuoteService
{
public:
    static ExpeditionQuote Quote(const Player* player,
                                 ExpeditionRole role,
                                 PlayerId playerId,
                                 ProvinceId sourceProvinceId,
                                 ProvinceId targetProvinceId,
                                 const std::vector<int>& unitInstanceIds,
                                 const UnitRoster& roster,
                                 const GlobalMap& map,
                                 ExpeditionLoadout requested = {},
                                 bool autoMinimum = false);

    static int CalculateMinimumFood(const UnitRoster& roster,
                                    const std::vector<int>& unitInstanceIds,
                                    int physicalDistanceUnits,
                                    int reductionBasisPoints = 10000);

    static int CalculateSupplyRatioBasisPoints(int selectedFood, int minimumFood);
    static int CalculateBonusBasisPoints(int selectedFood, int minimumFood);
    static bool ValidateLoadout(const ExpeditionLoadout& loadout, std::string& reason);
};

#endif
