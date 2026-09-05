#include "world/WorldJourney.h"

#include "core/BoundedHistory.h"
#include "core/PersistenceLimits.h"
#include "world/ProvinceConnection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace
{
    bool IsTerminal(WorldJourneyStatus status)
    {
        return status == WorldJourneyStatus::Succeeded ||
               status == WorldJourneyStatus::Failed ||
               status == WorldJourneyStatus::Cancelled;
    }

    ProvinceId OtherEnd(const IProvinceConnection& connection, ProvinceId from)
    {
        if (connection.GetFirstProvinceId() == from)
            return connection.GetSecondProvinceId();
        if (connection.GetSecondProvinceId() == from)
            return connection.GetFirstProvinceId();
        return InvalidProvinceId;
    }

    ProvinceId ProvinceBeforeLeg(const WorldJourney& journey, const GlobalMap& map,
                                 std::size_t leg)
    {
        ProvinceId current = journey.sourceProvinceId;
        for (std::size_t index = 0; index < leg; ++index)
        {
            const auto* connection = map.FindConnection(journey.legPlan[index].connectionId);
            if (connection == nullptr)
                return InvalidProvinceId;
            current = OtherEnd(*connection, current);
            if (current == InvalidProvinceId)
                return InvalidProvinceId;
        }
        return current;
    }

    WorldJourneyKind InferJourneyKind(const WorldJourneyPayload& payload)
    {
        if (std::holds_alternative<ScoutParty>(payload))
            return WorldJourneyKind::Scout;
        if (std::holds_alternative<TradeCargo>(payload))
            return WorldJourneyKind::Trade;
        if (std::holds_alternative<ArmyParty>(payload))
            return WorldJourneyKind::Attack;
        if (std::holds_alternative<ResourceConvoy>(payload))
            return WorldJourneyKind::ResourceTransfer;
        if (std::holds_alternative<ArmyTransferParty>(payload))
            return WorldJourneyKind::ArmyTransfer;
        return WorldJourneyKind::Colonization;
    }

    bool IsPayloadCompatible(WorldJourneyKind kind, const WorldJourneyPayload& payload)
    {
        switch (kind)
        {
            case WorldJourneyKind::Scout: return std::holds_alternative<ScoutParty>(payload);
            case WorldJourneyKind::Trade: return std::holds_alternative<TradeCargo>(payload);
            case WorldJourneyKind::Attack: return std::holds_alternative<ArmyParty>(payload);
            case WorldJourneyKind::Colonization: return std::holds_alternative<Colonists>(payload);
            case WorldJourneyKind::ResourceTransfer: return std::holds_alternative<ResourceConvoy>(payload);
            case WorldJourneyKind::ArmyTransfer: return std::holds_alternative<ArmyTransferParty>(payload);
            case WorldJourneyKind::Unknown: return false;
        }
        return false;
    }

    int RouteMultiplierToBasisPoints(double multiplier)
    {
        if (!std::isfinite(multiplier) || multiplier <= 0.0 ||
            multiplier * JourneyTiming::BasisPoints > std::numeric_limits<int>::max())
            return 0;
        return static_cast<int>(std::llround(multiplier * JourneyTiming::BasisPoints));
    }
}

bool WorldJourneySystem::ValidatePath(const WorldJourney& journey, const GlobalMap& map,
                                      std::string& failureReason) const
{
    const std::size_t connectionCount = journey.legPlan.size();
    if (journey.ownerId == InvalidPlayerId || journey.sourceProvinceId == InvalidProvinceId ||
        journey.targetProvinceId == InvalidProvinceId || journey.sourceProvinceId == journey.targetProvinceId ||
        connectionCount == 0 || connectionCount > PersistenceLimits::MaxJourneyPathConnections ||
        map.FindProvince(journey.sourceProvinceId) == nullptr ||
        map.FindProvince(journey.targetProvinceId) == nullptr)
    {
        failureReason = "journey has invalid endpoints or path";
        return false;
    }
    ProvinceId current = journey.sourceProvinceId;
    for (std::size_t index = 0; index < connectionCount; ++index)
    {
        const ProvinceConnectionId connectionId = journey.legPlan[index].connectionId;
        const auto* connection = map.FindConnection(connectionId);
        const ProvinceId next = connection == nullptr ? InvalidProvinceId
                                                        : OtherEnd(*connection, current);
        if (connection == nullptr || next == InvalidProvinceId)
        {
            failureReason = "journey path contains a dangling connection";
            return false;
        }
        if (journey.legPlan[index].connectionId == InvalidProvinceConnectionId)
        {
            failureReason = "journey leg plan contains an invalid connection";
            return false;
        }
        current = next;
    }
    if (current != journey.targetProvinceId)
    {
        failureReason = "journey path does not reach target";
        return false;
    }
    return true;
}

std::uint64_t WorldJourneySystem::LegDuration(const WorldJourney& journey, const GlobalMap& map,
                                              ProvinceConnectionId connectionId,
                                              const WorldJourneyRules& rules) const
{
    const auto* connection = map.FindConnection(connectionId);
    if (connection == nullptr || rules.baseLegDurationTicks == 0 ||
        !std::isfinite(rules.routeTravelSpeedMultiplier) ||
        rules.routeTravelSpeedMultiplier <= 0.0)
        return 0;
    const RouteModifierContext context{journey.sourceProvinceId, journey.targetProvinceId,
                                       journey.ownerId};
    const RouteTraversalStats stats = connection->ResolveTraversalStats(context);
    if (!std::isfinite(stats.traversalTimeMultiplier) || stats.traversalTimeMultiplier <= 0.0)
        return 0;
    const double duration = static_cast<double>(rules.baseLegDurationTicks) *
                            stats.traversalTimeMultiplier /
                            rules.routeTravelSpeedMultiplier;
    if (duration >= static_cast<double>(std::numeric_limits<std::uint64_t>::max()))
        return 0;
    return std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::ceil(duration)));
}

WorldJourneyStartResult WorldJourneySystem::Start(WorldJourney journey, const GlobalMap& map,
                                                  std::uint64_t currentTick,
                                                  const WorldJourneyRules& rules)
{
    WorldJourneyStartResult result;
    const auto reject = [&result](std::string reason)
    {
        result.failureReason = std::move(reason);
        return result;
    };
    std::string failureReason;
    BoundedHistory::TrimOldestMatching(
        journeys, PersistenceLimits::MaxRetainedWorldJourneys,
        [](const WorldJourney& existing) { return IsTerminal(existing.status); });
    WorldJourneyId assignedId = journey.id;
    const bool autoAssigned = journey.id == InvalidWorldJourneyId;
    if (!std::isfinite(rules.routeTravelSpeedMultiplier) ||
        rules.routeTravelSpeedMultiplier <= 0.0)
    {
        return reject("journey route speed multiplier is invalid");
    }
    if (journey.id == InvalidWorldJourneyId)
    {
        if (nextJourneyId == InvalidWorldJourneyId)
        {
            return reject("journey ID space exhausted");
        }
        assignedId = nextJourneyId;
        journey.id = assignedId;
    }
    else if (journeys.contains(journey.id) || journey.id > nextJourneyId)
    {
        return reject("journey ID is not monotonic or already used");
    }
    if (BoundedHistory::CountMatching(
            journeys, [](const WorldJourney& existing)
            {
                return !IsTerminal(existing.status);
            }) >= PersistenceLimits::MaxConcurrentWorldJourneys ||
        !ValidatePath(journey, map, failureReason))
        return reject(failureReason.empty() ? "journey path is invalid" : failureReason);

    if (journey.kind == WorldJourneyKind::Unknown)
        journey.kind = InferJourneyKind(journey.payload);
    if (!IsPayloadCompatible(journey.kind, journey.payload))
    {
        return reject("journey kind does not match payload");
    }

    // A non-default baseLegDurationTicks is retained only for old unit tests
    // and source-compatible tools. Runtime campaign journeys use the frozen
    // fixed-point plan below.
    if (rules.baseLegDurationTicks == 100)
    {
        JourneySpeedProfile profile = rules.speedProfile;
        profile.playerRouteSpeedBasisPoints = RouteMultiplierToBasisPoints(
            rules.routeTravelSpeedMultiplier);
        if (profile.playerRouteSpeedBasisPoints == 0)
        {
            return reject("journey route speed multiplier is invalid");
        }
        std::vector<ProvinceConnectionId> path;
        path.reserve(journey.legPlan.size());
        for (const auto& leg : journey.legPlan)
            path.push_back(leg.connectionId);
        const JourneyTimingQuote quote = JourneyTiming::QuoteForPath(
            map, journey.sourceProvinceId, journey.targetProvinceId, journey.ownerId,
            path, profile);
        if (!quote.valid)
        {
            return reject(quote.failureReason);
        }
        journey.legPlan = quote.legs;
    }
    else
    {
        for (auto& leg : journey.legPlan)
        {
            const auto* connection = map.FindConnection(leg.connectionId);
            leg.lengthUnits = connection == nullptr ? 0 : connection->GetLengthUnits();
            leg.durationTicks = LegDuration(journey, map, leg.connectionId, rules);
            if (connection == nullptr || leg.lengthUnits <= 0 || leg.durationTicks == 0)
                return reject("journey leg duration is invalid");
        }
    }

    const std::uint64_t duration = journey.legPlan.front().durationTicks;
    if (duration == 0 || currentTick > std::numeric_limits<std::uint64_t>::max() - duration)
    {
        return reject("journey leg duration is invalid");
    }
    journey.currentLeg = 0;
    journey.startTick = currentTick;
    journey.legCompletionTick = currentTick + duration;
    journey.rules = rules;
    journey.status = WorldJourneyStatus::InTransit;
    journeys.emplace(journey.id, std::move(journey));
    if (autoAssigned || assignedId == nextJourneyId)
    {
        if (assignedId == std::numeric_limits<WorldJourneyId>::max())
            nextJourneyId = InvalidWorldJourneyId;
        else
            nextJourneyId = assignedId + 1;
    }
    result.accepted = true;
    result.journeyId = assignedId;
    return result;
}

void WorldJourneySystem::Update(const GlobalMap& map, std::uint64_t currentTick)
{
    for (auto& [journeyId, journey] : journeys)
    {
        if (journey.status != WorldJourneyStatus::InTransit ||
            currentTick < journey.legCompletionTick)
            continue;
        const ProvinceId from = ProvinceBeforeLeg(journey, map, journey.currentLeg);
        const ProvinceConnectionId connectionId = journey.legPlan[journey.currentLeg].connectionId;
        const auto* connection = map.FindConnection(connectionId);
        const ProvinceId to = connection == nullptr ? InvalidProvinceId : OtherEnd(*connection, from);
        if (connection == nullptr || from == InvalidProvinceId || to == InvalidProvinceId)
        {
            journey.status = WorldJourneyStatus::Failed;
            continue;
        }
        ++journey.deterministicAttemptCounter;
        ++journey.currentLeg;
        const std::size_t totalLegs = journey.legPlan.size();
        const bool succeeded = journey.currentLeg == totalLegs;
        journey.status = succeeded ? WorldJourneyStatus::Succeeded : WorldJourneyStatus::InTransit;
        legEvents.push_back({journeyId, journey.ownerId, from, to, connection->GetId(),
                             journey.currentLeg, succeeded});
        if (succeeded)
            continue;
        const std::uint64_t duration = journey.legPlan[journey.currentLeg].durationTicks;
        if (duration == 0 || currentTick > std::numeric_limits<std::uint64_t>::max() - duration)
        {
            journey.status = WorldJourneyStatus::Failed;
            continue;
        }
        journey.legCompletionTick = currentTick + duration;
    }
}

bool WorldJourneySystem::FailJourney(WorldJourneyId journeyId)
{
    const auto it = journeys.find(journeyId);
    if (it == journeys.end() || it->second.status == WorldJourneyStatus::Succeeded ||
        it->second.status == WorldJourneyStatus::Cancelled)
        return false;
    it->second.status = WorldJourneyStatus::Failed;
    return true;
}

std::vector<int> WorldJourneySystem::ApplyScoutUnitLoss(WorldJourneyId journeyId, int amount)
{
    std::vector<int> casualties;
    const auto it = journeys.find(journeyId);
    if (it == journeys.end() || amount <= 0 ||
        (it->second.status != WorldJourneyStatus::InTransit &&
         it->second.status != WorldJourneyStatus::Succeeded))
        return casualties;

    auto* party = std::get_if<ScoutParty>(&it->second.payload);
    if (party == nullptr || party->unitInstanceIds.empty())
        return casualties;
    std::sort(party->unitInstanceIds.begin(), party->unitInstanceIds.end());
    const std::size_t count = std::min<std::size_t>(
        party->unitInstanceIds.size(), static_cast<std::size_t>(amount));
    casualties.assign(party->unitInstanceIds.begin(),
                      party->unitInstanceIds.begin() + count);
    party->unitInstanceIds.erase(party->unitInstanceIds.begin(),
                                 party->unitInstanceIds.begin() + count);
    if (party->unitInstanceIds.empty())
        it->second.status = WorldJourneyStatus::Failed;
    return casualties;
}

bool WorldJourneySystem::MarkAwaitingUnload(WorldJourneyId journeyId)
{
    const auto it = journeys.find(journeyId);
    if (it == journeys.end() || it->second.status != WorldJourneyStatus::Succeeded)
        return false;
    it->second.status = WorldJourneyStatus::AwaitingUnload;
    return true;
}

bool WorldJourneySystem::Cancel(WorldJourneyId journeyId)
{
    const auto it = journeys.find(journeyId);
    if (it == journeys.end() || it->second.status == WorldJourneyStatus::Succeeded ||
        it->second.status == WorldJourneyStatus::Failed)
        return false;
    it->second.status = WorldJourneyStatus::Cancelled;
    return true;
}

std::vector<JourneyLegCompleted> WorldJourneySystem::ConsumeLegEvents()
{
    std::vector<JourneyLegCompleted> result = std::move(legEvents);
    legEvents.clear();
    return result;
}

std::vector<JourneyLegCompleted> WorldJourneySystem::ConsumeLegEventsFor(
    WorldJourneyId journeyId)
{
    std::vector<JourneyLegCompleted> result;
    std::vector<JourneyLegCompleted> remaining;
    remaining.reserve(legEvents.size());
    for (const auto& event : legEvents)
    {
        if (event.journeyId == journeyId)
            result.push_back(event);
        else
            remaining.push_back(event);
    }
    legEvents = std::move(remaining);
    return result;
}

bool WorldJourneySystem::Restore(WorldJourneyId nextId, std::vector<WorldJourney> restored,
                                 const GlobalMap& map, std::string& failureReason)
{
    if (nextId == InvalidWorldJourneyId ||
        restored.size() > PersistenceLimits::MaxWorldJourneys)
    {
        failureReason = "invalid journey counter or count";
        return false;
    }
    std::map<WorldJourneyId, WorldJourney> candidate;
    for (auto& journey : restored)
    {
        if (journey.id == InvalidWorldJourneyId || journey.id >= nextId ||
            candidate.contains(journey.id) ||
            journey.currentLeg > journey.legPlan.size() ||
            journey.status == WorldJourneyStatus::InTransit &&
                journey.currentLeg >= journey.legPlan.size())
        {
            failureReason = "journey IDs or progress are invalid";
            return false;
        }
        if (!ValidatePath(journey, map, failureReason))
            return false;
        if (journey.kind == WorldJourneyKind::Unknown ||
            !IsPayloadCompatible(journey.kind, journey.payload))
        {
            failureReason = "journey kind does not match payload";
            return false;
        }
        for (const auto& leg : journey.legPlan)
            if (leg.lengthUnits != map.FindConnection(leg.connectionId)->GetLengthUnits() ||
                leg.durationTicks == 0)
            {
                failureReason = "journey leg plan is incomplete";
                return false;
            }
        if (journey.rules.baseLegDurationTicks == 0 ||
            !std::isfinite(journey.rules.routeTravelSpeedMultiplier) ||
            journey.rules.routeTravelSpeedMultiplier <= 0.0)
        {
            failureReason = "journey rules have zero leg duration";
            return false;
        }
        if (journey.status == WorldJourneyStatus::InTransit &&
            journey.legCompletionTick < journey.startTick)
        {
            failureReason = "journey completion tick precedes start";
            return false;
        }
        candidate.emplace(journey.id, std::move(journey));
    }
    journeys = std::move(candidate);
    legEvents.clear();
    nextJourneyId = nextId;
    return true;
}

void WorldJourneySystem::Clear()
{
    nextJourneyId = 1;
    journeys.clear();
    legEvents.clear();
}
