#ifndef WORLD_SCOUT_EXPEDITION_SERVICE_H
#define WORLD_SCOUT_EXPEDITION_SERVICE_H

#include "warfare/BattleUnit.h"
#include "world/Expedition.h"
#include "world/GlobalMap.h"
#include "world/WorldJourney.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

const std::map<std::string, ExpeditionDefinition>& GetExpeditionCatalog();
const ExpeditionDefinition* FindExpeditionDefinition(const std::string& id);
std::map<std::string, ExpeditionDefinition> LoadExpeditionDefinitionsFromFile(const std::string& path);

class ScoutExpeditionService
{
public:
    // Creates the canonical WorldJourney and atomically moves every selected
    // scout out of reserve. This service owns no parallel expedition ledger.
    static bool Start(PlayerId playerId, ProvinceId sourceProvinceId,
                      ProvinceId targetProvinceId,
                      const std::vector<int>& unitInstanceIds,
                      UnitRoster& roster, const GlobalMap& map,
                      WorldJourneySystem& journeys,
                      std::uint64_t currentTick,
                      WorldJourneyId& createdId,
                      std::string& failureReason,
                      WorldJourneyRules journeyRules = {});

    static bool HasActiveFor(const WorldJourneySystem& journeys,
                             PlayerId playerId, ProvinceId targetProvinceId);
};

#endif
