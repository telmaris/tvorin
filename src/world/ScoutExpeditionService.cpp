#include "world/ScoutExpeditionService.h"

#include "core/PersistenceLimits.h"
#include "core/Log.h"
#include "data/RtsDataFile.h"
#include "warfare/UnitDefinition.h"

#include <algorithm>
#include <set>

namespace
{
    constexpr const char* expeditionDataPath = "assets/data/expeditions.rtsdata";

    bool ParseRole(const std::string& value, ExpeditionRole& role)
    {
        if (value == "Scout") { role = ExpeditionRole::Scout; return true; }
        if (value == "Supply") { role = ExpeditionRole::Supply; return true; }
        if (value == "Garrison") { role = ExpeditionRole::Garrison; return true; }
        return false;
    }

    bool ParseUnitRole(const std::string& value, UnitRole& role)
    {
        if (value == "Line") { role = UnitRole::Line; return true; }
        if (value == "Scout") { role = UnitRole::Scout; return true; }
        if (value == "Supply") { role = UnitRole::Supply; return true; }
        if (value == "Garrison") { role = UnitRole::Garrison; return true; }
        return false;
    }

    bool ParseStrategicResource(const std::string& value, StrategicResourceType& resource)
    {
        if (value == "FoodPackages") { resource = StrategicResourceType::FoodPackages; return true; }
        if (value == "SupplyPackages") { resource = StrategicResourceType::SupplyPackages; return true; }
        if (value == "Weapons") { resource = StrategicResourceType::Weapons; return true; }
        return false;
    }

}

std::map<std::string, ExpeditionDefinition> LoadExpeditionDefinitionsFromFile(const std::string& path)
{
    std::map<std::string, ExpeditionDefinition> definitions;
    const RtsDataDocument document = ReadRtsDataDocument(path);
    for (const auto& diagnostic : document.diagnostics)
        Log::Msg("[ExpeditionCatalog]", diagnostic.path, diagnostic.line != 0 ? ":" : "",
                 diagnostic.line, ": ", diagnostic.message);
    if (!document.IsValid())
        return definitions;

    const auto& lines = document.lines;
    const auto sourceLine = [&document](std::size_t index)
    {
        return index < document.sourceLines.size() ? document.sourceLines[index] : index + 1;
    };
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        const auto& header = lines[index];
        if (header.empty())
            continue;
        if (header[0] != "expedition")
        {
            Log::Msg("[ExpeditionCatalog]", "unknown top-level token '", header[0], "' near line ",
                     sourceLine(index));
            continue;
        }
        if (header.size() != 2)
        {
            Log::Msg("[ExpeditionCatalog]", "invalid expedition header near line ", sourceLine(index));
            continue;
        }
        ExpeditionDefinition definition;
        definition.id = header[1];
        bool valid = true;
        bool closed = false;
        for (++index; index < lines.size(); ++index)
        {
            const auto& tokens = lines[index];
            if (tokens.empty())
                continue;
            if (tokens[0] == "end")
            {
                closed = true;
                break;
            }
            if (tokens.size() != 2)
            {
                valid = false;
                Log::Msg("[ExpeditionCatalog]", "invalid token count near line ", sourceLine(index));
                continue;
            }
            const std::string& command = tokens[0];
            const std::string& value = tokens[1];
            if (command == "role")
                valid = ParseRole(value, definition.role) && valid;
            else if (command == "required_unit_role")
                valid = ParseUnitRole(value, definition.requiredUnitRole) && valid;
            else if (command == "required_unit_count")
                definition.requiredUnitCount = RtsDataIntOr(value);
            else if (command == "supply_resource")
                valid = ParseStrategicResource(value, definition.supplyResource) && valid;
            else if (command == "supply_cost")
                definition.supplyCost = RtsDataIntOr(value);
            else if (command == "duration_ticks")
                definition.durationTicks = static_cast<std::uint64_t>(std::max(0, RtsDataIntOr(value)));
            else
            {
                valid = false;
                Log::Msg("[ExpeditionCatalog]", "unknown token '" + command + "' near line ",
                         sourceLine(index));
            }
        }
        if (!closed || !valid || !definition.IsValid())
        {
            Log::Msg("[ExpeditionCatalog]", "rejected invalid expedition definition: ", definition.id);
            continue;
        }
        if (!definitions.emplace(definition.id, definition).second)
            Log::Msg("[ExpeditionCatalog]", "duplicate expedition id ignored: ", definition.id);
    }
    return definitions;
}

const std::map<std::string, ExpeditionDefinition>& GetExpeditionCatalog()
{
    static const auto catalog = LoadExpeditionDefinitionsFromFile(expeditionDataPath);
    return catalog;
}

const ExpeditionDefinition* FindExpeditionDefinition(const std::string& id)
{
    const auto& catalog = GetExpeditionCatalog();
    const auto it = catalog.find(id);
    return it == catalog.end() ? nullptr : &it->second;
}

bool ScoutExpeditionService::Start(PlayerId playerId, ProvinceId sourceProvinceId,
                                   ProvinceId targetProvinceId,
                                   const std::vector<int>& unitInstanceIds,
                                   UnitRoster& roster, const GlobalMap& map,
                                   WorldJourneySystem& journeys,
                                   std::uint64_t currentTick,
                                   WorldJourneyId& createdId,
                                   std::string& failureReason,
                                   WorldJourneyRules journeyRules)
{
    createdId = InvalidWorldJourneyId;
    const auto* definition = FindExpeditionDefinition("scout");
    if (definition == nullptr || definition->role != ExpeditionRole::Scout ||
        playerId == InvalidPlayerId || sourceProvinceId == InvalidProvinceId ||
        targetProvinceId == InvalidProvinceId || sourceProvinceId == targetProvinceId)
    {
        failureReason = "scout journey definition or endpoints are invalid";
        return false;
    }
    const auto* source = map.FindBuildableProvince(sourceProvinceId);
    const auto* target = map.FindProvince(targetProvinceId);
    if (source == nullptr || target == nullptr || source->GetOwnerId() != playerId ||
        target->GetKnowledge(playerId) != ProvinceKnowledgeLevel::ReachableUnknown)
    {
        failureReason = "scouting target is not reachable from an owned province";
        return false;
    }
    std::vector<ProvinceConnectionId> path;
    if (!map.FindShortestPath(sourceProvinceId, targetProvinceId, path))
    {
        failureReason = "scouting target has no legal route";
        return false;
    }
    if (unitInstanceIds.size() < static_cast<std::size_t>(definition->requiredUnitCount) ||
        unitInstanceIds.size() > PersistenceLimits::MaxExpeditionUnits)
    {
        failureReason = "scouting requires the configured number of units";
        return false;
    }

    std::set<int> uniqueIds;
    for (const int instanceId : unitInstanceIds)
    {
        if (instanceId <= 0 || !uniqueIds.insert(instanceId).second)
        {
            failureReason = "scout unit is duplicated or already reserved";
            return false;
        }
        BattleUnit* unit = roster.FindUnit(instanceId);
        const auto* unitDefinition = unit == nullptr ? nullptr : FindUnitDefinition(unit->unitDefId);
        if (unit == nullptr || unit->ownerPlayerId != playerId || unitDefinition == nullptr ||
            unitDefinition->role != definition->requiredUnitRole ||
            !UnitAssignmentService::IsAvailableFromReserve(*unit, sourceProvinceId))
        {
            failureReason = "all journey units must be recruited Scouts in source reserve";
            return false;
        }
    }

    WorldJourney journey;
    journey.ownerId = playerId;
    journey.sourceProvinceId = sourceProvinceId;
    journey.targetProvinceId = targetProvinceId;
    journey.legPlan.reserve(path.size());
    for (const ProvinceConnectionId connectionId : path)
        journey.legPlan.push_back({connectionId});
    journey.payload = ScoutParty{std::vector<int>(unitInstanceIds.begin(), unitInstanceIds.end())};
    auto& payload = std::get<ScoutParty>(journey.payload);
    std::sort(payload.unitInstanceIds.begin(), payload.unitInstanceIds.end());
    double slowestMoveSpeed = std::numeric_limits<double>::max();
    for (const int instanceId : unitInstanceIds)
    {
        const auto* unit = roster.FindUnit(instanceId);
        const auto* unitDefinition = unit == nullptr ? nullptr : FindUnitDefinition(unit->unitDefId);
        if (unitDefinition == nullptr || !std::isfinite(unitDefinition->moveSpeed) ||
            unitDefinition->moveSpeed <= 0.0)
        {
            failureReason = "scout unit has no positive move speed";
            return false;
        }
        slowestMoveSpeed = std::min(slowestMoveSpeed, unitDefinition->moveSpeed);
    }
    journeyRules.speedProfile.moverSpeedBasisPoints = static_cast<int>(std::llround(
        slowestMoveSpeed * JourneyTiming::BasisPoints));
    journey.kind = WorldJourneyKind::Scout;
    const WorldJourneyStartResult start = journeys.Start(
        std::move(journey), map, currentTick, journeyRules);
    if (!start)
    {
        failureReason = start.failureReason.empty()
            ? "unable to start scout journey" : start.failureReason;
        return false;
    }
    createdId = start.journeyId;
    const auto& started = journeys.GetJourneys().at(createdId);
    if (const auto* startedScout = std::get_if<ScoutParty>(&started.payload))
        for (const int instanceId : startedScout->unitInstanceIds)
        {
            BattleUnit* unit = roster.FindUnit(instanceId);
            if (unit != nullptr)
                UnitAssignmentService::AssignJourney(*unit, sourceProvinceId, createdId,
                                                      unit->assignment.buildingId);
        }
    return true;
}

bool ScoutExpeditionService::HasActiveFor(const WorldJourneySystem& journeys,
                                          PlayerId playerId,
                                          ProvinceId targetProvinceId)
{
    for (const auto& [id, journey] : journeys.GetJourneys())
    {
        (void)id;
        if (journey.ownerId == playerId && journey.targetProvinceId == targetProvinceId &&
            journey.status == WorldJourneyStatus::InTransit &&
            std::holds_alternative<ScoutParty>(journey.payload))
            return true;
    }
    return false;
}
