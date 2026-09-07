#include "world/WorldEventSystem.h"

#include "core/BoundedHistory.h"
#include "economy/Player.h"
#include "economy/StockpileIndex.h"
#include "world/ProvinceDefinition.h"
#include "world/ProvinceSimulation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
    bool HasPendingRaidEffect(const WorldEventInstance& instance)
    {
        return std::any_of(instance.effects.begin(), instance.effects.end(),
                           [](const WorldEventEffect& effect)
                           {
                               const auto* raid = std::get_if<StartRaidEffect>(&effect);
                               return raid != nullptr && raid->strength > 0;
                           });
    }

    bool HasPendingJourneyEffect(const WorldEventInstance& instance)
    {
        return std::any_of(instance.effects.begin(), instance.effects.end(),
                           [](const WorldEventEffect& effect)
                           {
                              const auto* loss = std::get_if<KillJourneyUnitsEffect>(&effect);
                               return loss != nullptr &&
                                      (loss->amount > 0 ||
                                       loss->maximumFractionBasisPoints > 0);
                           });
    }

    bool IsFullyProcessed(const WorldEventInstance& instance)
    {
        return instance.expired &&
               (!HasPendingRaidEffect(instance) || instance.raidStarted) &&
               (!HasPendingJourneyEffect(instance) || instance.journeyEffectApplied);
    }

    void PruneProcessedEvents(
        std::map<WorldEventInstanceId, WorldEventInstance>& instances)
    {
        BoundedHistory::TrimOldestMatching(
            instances, PersistenceLimits::MaxRetainedWorldEventInstances,
            [](const WorldEventInstance& instance) { return IsFullyProcessed(instance); });
    }

    bool Contains(const std::vector<std::string>& values, std::string_view value)
    {
        return std::find(values.begin(), values.end(), value) != values.end();
    }

    std::uint64_t StableStringHash(std::string_view value)
    {
        std::uint64_t result = 1469598103934665603ull;
        for (const unsigned char character : value)
        {
            result ^= character;
            result *= 1099511628211ull;
        }
        return result;
    }

    std::uint64_t AddSaturating(std::uint64_t first, std::uint64_t second)
    {
        if (second > std::numeric_limits<std::uint64_t>::max() - first)
            return std::numeric_limits<std::uint64_t>::max();
        return first + second;
    }

    std::uint64_t EventDuration(const WorldEventDefinition& definition)
    {
        std::uint64_t duration = definition.durationTicks;
        for (const auto& effect : definition.effects)
        {
            if (const auto* timed = std::get_if<AddTimedModifierEffect>(&effect))
                duration = std::max(duration, timed->durationTicks);
        }
        return duration;
    }

    Player* FindProvinceOwner(GlobalMap& map, ProvinceId provinceId, PlayerId ownerId)
    {
        if (ownerId == InvalidPlayerId)
            return nullptr;
        auto* province = map.FindBuildableProvince(provinceId);
        if (province == nullptr || province->GetOwnerId() != ownerId ||
            province->GetSimulation() == nullptr)
            return nullptr;
        return province->GetSimulation()->GetOwner();
    }

    void AppendTraits(const IProvince& province, std::vector<std::string>& traits)
    {
        if (const auto* buildable = dynamic_cast<const BuildableProvince*>(&province))
        {
            traits.insert(traits.end(), buildable->GetParameters().traitIds.begin(),
                          buildable->GetParameters().traitIds.end());
            return;
        }
        if (const auto* bandit = dynamic_cast<const BanditProvince*>(&province))
        {
            traits.insert(traits.end(), bandit->GetFutureBuildableParameters().traitIds.begin(),
                          bandit->GetFutureBuildableParameters().traitIds.end());
            const ProvinceDefinition* definition = FindProvinceDefinition(bandit->GetDefinitionId());
            if (definition != nullptr)
                for (const auto& trait : definition->traits)
                    traits.push_back(trait.id);
            return;
        }

        std::string definitionId;
        if (const auto* city = dynamic_cast<const NeutralCityProvince*>(&province))
            definitionId = city->GetDefinitionId();
        else if (const auto* bandit = dynamic_cast<const BanditProvince*>(&province))
            definitionId = bandit->GetDefinitionId();

        if (definitionId.empty())
            return;
        const ProvinceDefinition* definition = FindProvinceDefinition(definitionId);
        if (definition == nullptr)
            return;
        for (const auto& trait : definition->traits)
            traits.push_back(trait.id);
    }
}

bool EventEligibilityService::IsEligible(const WorldEventDefinition& definition,
                                         WorldEventTriggerDomain trigger,
                                         const EventEligibilityContext& context)
{
    if (definition.trigger != trigger)
        return false;
    if (!definition.allowedKinds.empty() &&
        std::find(definition.allowedKinds.begin(), definition.allowedKinds.end(),
                  context.provinceKind) == definition.allowedKinds.end())
        return false;
    if (std::find(definition.excludedKinds.begin(), definition.excludedKinds.end(),
                  context.provinceKind) != definition.excludedKinds.end())
        return false;
    if (!definition.requiredTrait.empty() &&
        !Contains(context.provinceTraits, definition.requiredTrait))
        return false;
    if (!definition.requiredAdjacentTrait.empty() &&
        !Contains(context.adjacentTraits, definition.requiredAdjacentTrait))
        return false;
    if (definition.requiredProducedResource != ResourceType::Null &&
        std::find(context.producedResources.begin(), context.producedResources.end(),
                  definition.requiredProducedResource) == context.producedResources.end())
        return false;
    return context.routeLevel >= definition.minimumRouteLevel;
}

const WorldEventDefinition* WeightedEventSelector::Select(
    const std::vector<const WorldEventDefinition*>& candidates, std::uint64_t roll)
{
    std::uint64_t totalWeight = 0;
    for (const auto* candidate : candidates)
    {
        if (candidate == nullptr || candidate->weight <= 0)
            continue;
        const auto weight = static_cast<std::uint64_t>(candidate->weight);
        if (weight > std::numeric_limits<std::uint64_t>::max() - totalWeight)
            return nullptr;
        totalWeight += weight;
    }
    if (totalWeight == 0)
        return nullptr;

    std::uint64_t selected = roll % totalWeight;
    for (const auto* candidate : candidates)
    {
        if (candidate == nullptr || candidate->weight <= 0)
            continue;
        const auto weight = static_cast<std::uint64_t>(candidate->weight);
        if (selected < weight)
            return candidate;
        selected -= weight;
    }
    return nullptr;
}

void WorldEventFeed::Push(WorldEventNotificationView notification)
{
    if (notification.instanceId == InvalidWorldEventInstanceId)
        return;
    if (knownIds.contains(notification.instanceId))
        return;
    history.push_back(std::move(notification));
    knownIds.insert(history.back().instanceId);
    Trim();
}

void WorldEventFeed::MarkExpired(WorldEventInstanceId instanceId)
{
    for (auto& notification : history)
    {
        if (notification.instanceId == instanceId)
        {
            notification.expired = true;
            return;
        }
    }
}

bool WorldEventFeed::RecordAppliedEffect(WorldEventInstanceId instanceId,
                                         AppliedWorldEventEffect effect)
{
    for (auto& notification : history)
    {
        if (notification.instanceId != instanceId)
            continue;
        notification.appliedEffects.push_back(std::move(effect));
        return true;
    }
    return false;
}

bool WorldEventFeed::IsVisibleTo(const WorldEventNotificationView& notification,
                                 PlayerId playerId) const
{
    return notification.ownerId == InvalidPlayerId || notification.ownerId == playerId;
}

std::vector<WorldEventNotificationView> WorldEventFeed::ConsumeFor(PlayerId playerId)
{
    std::vector<WorldEventNotificationView> result;
    auto& delivered = deliveredByPlayer[playerId];
    for (const auto& notification : history)
    {
        if (!IsVisibleTo(notification, playerId) || delivered.contains(notification.instanceId))
            continue;
        result.push_back(notification);
        delivered.insert(notification.instanceId);
    }
    return result;
}

bool WorldEventFeed::HasDelivered(PlayerId playerId, WorldEventInstanceId instanceId) const
{
    const auto player = deliveredByPlayer.find(playerId);
    return player != deliveredByPlayer.end() && player->second.contains(instanceId);
}

void WorldEventFeed::Trim()
{
    while (history.size() > maximumHistory)
    {
        const auto id = history.front().instanceId;
        history.pop_front();
        knownIds.erase(id);
        for (auto& [playerId, delivered] : deliveredByPlayer)
            delivered.erase(id);
    }
}

void WorldEventFeed::Clear()
{
    history.clear();
    knownIds.clear();
    deliveredByPlayer.clear();
}

ProvinceEventSystem::ProvinceEventSystem(std::uint32_t seed,
                                         const WorldEventCatalog* catalog,
                                         std::optional<PeriodicEventScheduleDefinition> schedule)
    : campaignSeed(seed), definitions(catalog != nullptr ? catalog : &GetWorldEventCatalog()),
      periodicScheduler(schedule.has_value() ? *schedule : GetPeriodicEventScheduleDefinition())
{
}

std::uint64_t ProvinceEventSystem::Mix(std::uint64_t value)
{
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31);
}

std::uint64_t ProvinceEventSystem::NextDeterministicValue(PlayerId playerId,
                                                          ProvinceId provinceId,
                                                          std::uint64_t attemptCounter,
                                                          std::uint64_t salt) const
{
    std::uint64_t value = static_cast<std::uint64_t>(campaignSeed);
    value ^= static_cast<std::uint64_t>(playerId) * 0x8CB92BA72F3D8DD7ull;
    value ^= static_cast<std::uint64_t>(provinceId) * 0xD6E8FEB86659FD93ull;
    value ^= attemptCounter * 0xA0761D6478BD642Full;
    value ^= salt * 0xE7037ED1A0B428DBull;
    return Mix(value);
}

EventEligibilityContext ProvinceEventSystem::BuildContext(const GlobalMap& map,
                                                           ProvinceId provinceId,
                                                           int routeLevel) const
{
    EventEligibilityContext context;
    context.routeLevel = std::max(0, routeLevel);
    const IProvince* province = map.FindProvince(provinceId);
    if (province == nullptr)
        return context;
    context.provinceKind = province->GetKind();
    AppendTraits(*province, context.provinceTraits);
    if (const auto* buildable = dynamic_cast<const BuildableProvince*>(province);
        buildable != nullptr && buildable->GetSimulation() != nullptr)
    {
        for (const Building* building : buildable->GetSimulation()->GetEconomy().dataTracker.buildings)
        {
            if (building == nullptr)
                continue;
            for (const auto& output : building->GetOutputBufferViews())
                if (output.type != ResourceType::Null)
                    context.producedResources.push_back(output.type);
        }
        std::sort(context.producedResources.begin(), context.producedResources.end(),
                  [](ResourceType lhs, ResourceType rhs)
                  { return static_cast<int>(lhs) < static_cast<int>(rhs); });
        context.producedResources.erase(std::unique(context.producedResources.begin(),
                                                    context.producedResources.end()),
                                       context.producedResources.end());
    }

    std::vector<ProvinceId> neighbors = map.GetNeighbors(provinceId);
    std::sort(neighbors.begin(), neighbors.end());
    for (const ProvinceId neighborId : neighbors)
    {
        const IProvince* neighbor = map.FindProvince(neighborId);
        if (neighbor == nullptr)
            continue;
        AppendTraits(*neighbor, context.adjacentTraits);
    }
    std::sort(context.provinceTraits.begin(), context.provinceTraits.end());
    context.provinceTraits.erase(std::unique(context.provinceTraits.begin(),
                                             context.provinceTraits.end()),
                                 context.provinceTraits.end());
    std::sort(context.adjacentTraits.begin(), context.adjacentTraits.end());
    context.adjacentTraits.erase(std::unique(context.adjacentTraits.begin(),
                                             context.adjacentTraits.end()),
                                 context.adjacentTraits.end());
    return context;
}

std::vector<const WorldEventDefinition*> ProvinceEventSystem::FindCandidates(
    WorldEventTriggerDomain trigger, const EventEligibilityContext& context,
    std::string_view poolId, const std::vector<std::string>& dueDefinitionIds) const
{
    std::vector<const WorldEventDefinition*> result;
    if (definitions == nullptr)
        return result;

    auto consider = [&](const WorldEventDefinition& definition)
    {
        if (definition.trigger != trigger ||
            (!poolId.empty() && !definition.followUpPoolId.empty() &&
             definition.followUpPoolId != poolId) ||
            !EventEligibilityService::IsEligible(definition, trigger, context))
            return;
        result.push_back(&definition);
    };

    if (dueDefinitionIds.empty())
    {
        for (const auto& [id, definition] : *definitions)
            consider(definition);
    }
    else
    {
        // The IDs are normalized to catalog order so persistence or a caller
        // built from another container cannot alter the simulation result.
        std::set<std::string> due(dueDefinitionIds.begin(), dueDefinitionIds.end());
        for (const auto& [id, definition] : *definitions)
            if (due.contains(id))
                consider(definition);
    }
    return result;
}

bool ProvinceEventSystem::TryCreate(GlobalMap& map, PlayerId ownerId,
                                    ProvinceId provinceId, ProvinceId secondaryProvinceId,
                                    WorldEventTriggerDomain trigger,
                                    const EventEligibilityContext& context,
                                    std::string_view poolId, std::uint64_t currentTick,
                                    std::uint64_t attemptCounter, std::uint64_t triggerSalt,
                                    WorldJourneyId journeyId,
                                    const std::vector<std::string>& dueDefinitionIds)
{
    PruneProcessedEvents(instances);
    if (instances.size() >= PersistenceLimits::MaxWorldEventInstances)
        return false;
    const auto candidates = FindCandidates(trigger, context, poolId, dueDefinitionIds);
    if (candidates.empty())
        return false;

    std::vector<const WorldEventDefinition*> chancePassed;
    chancePassed.reserve(candidates.size());
    // Route events are attached to the destination for presentation, but the
    // player-wide balance context belongs to the owned origin province. This
    // keeps route risk modifiers active even while scouting a neutral target.
    const ProvinceId ownerProvinceId = trigger == WorldEventTriggerDomain::Route
        ? secondaryProvinceId : provinceId;
    Player* owner = FindProvinceOwner(map, ownerProvinceId, ownerId);
    constexpr std::uint64_t NegativeOccurrenceSalt = 0x4E45474154495645ull;
    constexpr std::uint64_t PositiveOccurrenceSalt = 0x504F534954495645ull;
    constexpr std::uint64_t PeriodicOccurrenceSalt = 0x4F4343555252454Eull;
    constexpr std::uint64_t PeriodicDefinitionSalt = 0x444546494E495449ull;
    constexpr std::uint64_t PeriodicOutcomeSalt = 0x4F5554434F4D45ull;
    if (trigger == WorldEventTriggerDomain::ProvincePeriodic)
    {
        int effectiveChance = periodicScheduler.GetSchedule().occurrenceChanceBasisPoints;
        if (owner != nullptr)
            effectiveChance = owner->ModifyBalanceIntAt(
                BalanceStat::ProvinceEventChance, effectiveChance, provinceId,
                BuildingType::Building, ResourceType::Null, 0);
        effectiveChance = std::clamp(effectiveChance, 0, 10000);
        const std::uint64_t occurrenceRoll = NextDeterministicValue(
            ownerId, provinceId, attemptCounter,
            triggerSalt ^ PeriodicOccurrenceSalt) % 10000ull;
        if (occurrenceRoll >= static_cast<std::uint64_t>(effectiveChance))
            return false;
        chancePassed = candidates;
    }
    else
    {
        const std::uint64_t negativeRoll = NextDeterministicValue(
            ownerId, provinceId, attemptCounter, triggerSalt ^ NegativeOccurrenceSalt) % 10000ull;
        const std::uint64_t positiveRoll = NextDeterministicValue(
            ownerId, provinceId, attemptCounter, triggerSalt ^ PositiveOccurrenceSalt) % 10000ull;
        for (const auto* candidate : candidates)
        {
            int effectiveChance = candidate->chanceBasisPoints;
            if (owner != nullptr)
                effectiveChance = owner->ModifyBalanceInt(
                    BalanceStat::ProvinceEventChance, effectiveChance,
                    BuildingType::Building, ResourceType::Null, 0);
            if (trigger == WorldEventTriggerDomain::Route && owner != nullptr)
                effectiveChance = owner->ModifyBalanceInt(
                    BalanceStat::RouteIncidentChance, effectiveChance,
                    BuildingType::Building, ResourceType::Null, 0);
            if (trigger == WorldEventTriggerDomain::Route)
            {
                const RouteIncidentRiskQuote quote = QuoteRouteIncidentRisk({
                    effectiveChance,
                    context.routeLengthUnits,
                    context.routeQualityBasisPoints,
                    context.routeIncidentChanceReductionBasisPoints,
                    10000,
                    context.scoutEscortCount,
                    100,
                    1000});
                effectiveChance = quote.negativeChanceBasisPoints;
            }
            else
                effectiveChance = std::clamp(effectiveChance, 0, 10000);
            const std::uint64_t roll = candidate->polarity == WorldEventPolarity::Positive
                ? positiveRoll : negativeRoll;
            if (roll < static_cast<std::uint64_t>(effectiveChance))
                chancePassed.push_back(candidate);
        }
    }
    if (chancePassed.empty())
        return false;

    std::uint64_t totalWeight = 0;
    std::vector<std::pair<const WorldEventDefinition*, int>> weightedCandidates;
    weightedCandidates.reserve(chancePassed.size());
    for (const auto* candidate : chancePassed)
    {
        const int weight = owner == nullptr
            ? candidate->weight
            : owner->ModifyBalanceInt(BalanceStat::ProvinceEventWeight,
                                      candidate->weight, BuildingType::Building,
                                      ResourceType::Null, 0);
        if (weight <= 0)
            continue;
        if (totalWeight > std::numeric_limits<std::uint64_t>::max() -
                              static_cast<std::uint64_t>(weight))
            return false;
        totalWeight += static_cast<std::uint64_t>(weight);
        weightedCandidates.emplace_back(candidate, weight);
    }
    if (totalWeight == 0)
        return false;
    const std::uint64_t definitionSalt = trigger == WorldEventTriggerDomain::ProvincePeriodic
        ? triggerSalt ^ PeriodicDefinitionSalt : triggerSalt;
    std::uint64_t selectedRoll = NextDeterministicValue(
        ownerId, provinceId, attemptCounter, definitionSalt) % totalWeight;
    const WorldEventDefinition* selected = nullptr;
    for (const auto& [candidate, weight] : weightedCandidates)
    {
        if (selectedRoll < static_cast<std::uint64_t>(weight))
        {
            selected = candidate;
            break;
        }
        selectedRoll -= static_cast<std::uint64_t>(weight);
    }
    if (selected == nullptr || nextInstanceId == InvalidWorldEventInstanceId)
        return false;

    WorldEventInstance instance;
    instance.id = nextInstanceId++;
    instance.ownerId = ownerId;
    instance.provinceId = provinceId;
    instance.secondaryProvinceId = secondaryProvinceId;
    instance.definitionId = selected->id;
    instance.trigger = trigger;
    instance.startTick = currentTick;
    std::uint64_t duration = EventDuration(*selected);
    if (owner != nullptr)
    {
        const int baseDuration = duration > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
            ? std::numeric_limits<int>::max() : static_cast<int>(duration);
        duration = static_cast<std::uint64_t>(owner->ModifyBalanceInt(
            BalanceStat::ProvinceEventDuration, baseDuration,
            BuildingType::Building, ResourceType::Null, 0));
    }
    instance.endTick = AddSaturating(currentTick, duration);
    instance.outcomeRoll = NextDeterministicValue(
        ownerId, provinceId, attemptCounter,
        (trigger == WorldEventTriggerDomain::ProvincePeriodic ?
             triggerSalt ^ PeriodicOutcomeSalt : triggerSalt) ^ StableStringHash(selected->id));
    instance.deterministicAttemptCounter = attemptCounter;
    instance.journeyId = journeyId;
    instance.effects = selected->effects;
    instance.expired = instance.endTick == currentTick;

    ApplyEffects(map, instance);
    instances.emplace(instance.id, instance);

    WorldEventNotificationView notification;
    notification.instanceId = instance.id;
    notification.ownerId = instance.ownerId;
    notification.provinceId = instance.provinceId;
    notification.secondaryProvinceId = instance.secondaryProvinceId;
    notification.definitionId = instance.definitionId;
    notification.title = selected->title.empty() ? selected->name : selected->title;
    notification.description = selected->description;
    notification.trigger = instance.trigger;
    notification.startTick = instance.startTick;
    notification.endTick = instance.endTick;
    notification.outcomeRoll = instance.outcomeRoll;
    notification.expired = instance.expired;
    notification.appliedEffects = instance.appliedEffects;
    feed.Push(std::move(notification));
    return true;
}

void ProvinceEventSystem::ApplyEffects(GlobalMap& map, WorldEventInstance& instance)
{
    IProvince* province = map.FindProvince(instance.provinceId);
    if (province == nullptr)
        return;

    auto* buildable = dynamic_cast<BuildableProvince*>(province);
    auto* city = dynamic_cast<NeutralCityProvince*>(province);
    Player* owner = nullptr;
    ProvinceEconomy* economy = nullptr;
    if (buildable != nullptr && buildable->GetSimulation() != nullptr)
    {
        owner = buildable->GetSimulation()->GetOwner();
        economy = &buildable->GetSimulation()->GetEconomy();
    }

    const std::string modifierSource = "world_event." + std::to_string(instance.id);
    for (const auto& effect : instance.effects)
    {
        if (const auto* modifyResource = std::get_if<ModifyResourceEffect>(&effect))
        {
            if (economy != nullptr)
            {
                const int applied = StockpileIndex::Deposit(
                    *economy, modifyResource->type, modifyResource->amount);
                if (applied != 0)
                    instance.appliedEffects.push_back({AppliedWorldEventEffectKind::ResourceDelta,
                        modifyResource->type, applied});
            }
        }
        else if (const auto* timed = std::get_if<AddTimedModifierEffect>(&effect))
        {
            if (owner == nullptr)
                continue;
            BalanceModifier modifier;
            modifier.stat = timed->stat;
            modifier.additive = timed->additive;
            modifier.multiplier = timed->multiplier;
            modifier.scope = BalanceModifierScope::Global();
            modifier.scope.provinceId = instance.provinceId;
            modifier.source = modifierSource;
            owner->balanceModifiers.AddModifier(std::move(modifier));
            instance.appliedEffects.push_back({AppliedWorldEventEffectKind::TimedModifier,
                ResourceType::Null, 0, timed->stat, timed->additive, timed->multiplier,
                instance.endTick >= instance.startTick ? instance.endTick - instance.startTick : 0});
        }
        else if (const auto* tradeScore = std::get_if<ModifyTradeScoreEffect>(&effect))
        {
            if (city != nullptr && instance.ownerId != InvalidPlayerId)
            {
                const int before = city->GetState().GetTradeScore(instance.ownerId);
                city->GetStateForAuthority().AddTradeScore(instance.ownerId, tradeScore->amount);
                const int applied = city->GetState().GetTradeScore(instance.ownerId) - before;
                if (applied != 0)
                    instance.appliedEffects.push_back({AppliedWorldEventEffectKind::TradeScore,
                        ResourceType::Null, applied});
            }
        }
        else if (const auto* damageCity = std::get_if<DamageCityStockEffect>(&effect))
        {
            if (city != nullptr)
            {
                const int before = city->GetState().GetStock(damageCity->type);
                const int remaining = std::max(0, before - damageCity->amount);
                city->GetStateForAuthority().SetStock(damageCity->type, remaining);
                if (before != remaining)
                    instance.appliedEffects.push_back({AppliedWorldEventEffectKind::ResourceDelta,
                        damageCity->type, remaining - before});
            }
        }
        else if (const auto* damageProvince = std::get_if<DamageProvinceStockEffect>(&effect))
        {
            if (economy != nullptr)
            {
                const int applied = StockpileIndex::Consume(
                    *economy, damageProvince->type, damageProvince->amount);
                if (applied != 0)
                    instance.appliedEffects.push_back({AppliedWorldEventEffectKind::ResourceDelta,
                        damageProvince->type, -applied});
            }
        }
        else if (const auto* destroy = std::get_if<DestroyBuildingEffect>(&effect))
        {
            if (economy == nullptr || economy->tilemap == nullptr || owner == nullptr ||
                destroy->amount <= 0)
                continue;

            std::vector<Building*> candidates;
            for (Building* building : economy->dataTracker.buildings)
                if (building != nullptr && building->owner == owner &&
                    building->CanBeManuallyDestroyed())
                    candidates.push_back(building);
            std::sort(candidates.begin(), candidates.end(),
                      [&](const Building* first, const Building* second)
                      {
                          const std::uint64_t firstRank = Mix(
                              instance.outcomeRoll ^ static_cast<std::uint64_t>(first->id));
                          const std::uint64_t secondRank = Mix(
                              instance.outcomeRoll ^ static_cast<std::uint64_t>(second->id));
                          return firstRank != secondRank ? firstRank < secondRank
                                                       : first->id < second->id;
                      });
            const std::size_t count = std::min<std::size_t>(
                candidates.size(), static_cast<std::size_t>(destroy->amount));
            std::size_t destroyed = 0;
            for (std::size_t index = 0; index < count; ++index)
            {
                economy->tilemap->DestroyBuildingAt(candidates[index]->positionId);
                if (economy->tilemap->GetBuilding(candidates[index]->positionId) == nullptr)
                    ++destroyed;
            }
            if (destroyed != 0)
                instance.appliedEffects.push_back({AppliedWorldEventEffectKind::BuildingLoss,
                    ResourceType::Null, static_cast<int>(destroyed)});
        }
        // KillJourneyUnits and StartRaid stay in the typed instance until
        // their journey/battle authority confirms a successful transition.
    }
}

void ProvinceEventSystem::ExpireInstances(GlobalMap& map, std::uint64_t currentTick)
{
    for (auto& [id, instance] : instances)
    {
        if (instance.expired || instance.endTick > currentTick)
            continue;
        instance.expired = true;
        feed.MarkExpired(id);
        const auto* province = map.FindProvince(instance.provinceId);
        const auto* buildable = dynamic_cast<const BuildableProvince*>(province);
        if (buildable == nullptr || buildable->GetSimulation() == nullptr)
            continue;
        Player* owner = buildable->GetSimulation()->GetOwner();
        if (owner != nullptr)
            owner->balanceModifiers.ClearSource("world_event." + std::to_string(id));
    }
}

void ProvinceEventSystem::Update(GlobalMap& map, std::uint64_t currentTick)
{
    ExpireInstances(map, currentTick);
    PruneProcessedEvents(instances);
    if (definitions == nullptr)
        return;

    std::map<PlayerId, std::vector<ProvinceId>> ownedProvinces;
    for (const ProvinceId provinceId : map.GetProvinceIds())
    {
        const auto* province = map.FindBuildableProvince(provinceId);
        if (province == nullptr || province->GetOwnerId() == InvalidPlayerId ||
            province->GetSimulation() == nullptr)
            continue;
        ownedProvinces[province->GetOwnerId()].push_back(provinceId);
    }

    constexpr std::uint64_t ProvinceSelectionSalt = 0x50524F56494E4345ull;
    constexpr std::uint64_t PeriodicTriggerSalt = 0x504552494F444943ull;
    for (const auto& [playerId, provinceIds] : ownedProvinces)
    {
        const auto attempt = periodicScheduler.TakeDueAttempt(playerId, currentTick);
        if (!attempt.has_value() || provinceIds.empty())
            continue;

        struct EligibleProvince
        {
            ProvinceId provinceId{InvalidProvinceId};
            EventEligibilityContext context;
            std::vector<std::string> definitionIds;
        };
        std::vector<EligibleProvince> eligible;
        for (const ProvinceId provinceId : provinceIds)
        {
            const auto context = BuildContext(map, provinceId, 0);
            EligibleProvince candidate{provinceId, context, {}};
            for (const auto& [definitionId, definition] : *definitions)
            {
                if (definition.trigger != WorldEventTriggerDomain::ProvincePeriodic ||
                    periodicScheduler.IsDefinitionOnCooldown(playerId, definitionId, currentTick) ||
                    !EventEligibilityService::IsEligible(
                        definition, WorldEventTriggerDomain::ProvincePeriodic, context))
                    continue;
                candidate.definitionIds.push_back(definitionId);
            }
            if (!candidate.definitionIds.empty())
                eligible.push_back(std::move(candidate));
        }
        if (eligible.empty())
            continue;

        const ProvinceId selectionAnchor = provinceIds.front();
        const std::size_t provinceIndex = static_cast<std::size_t>(
            NextDeterministicValue(playerId, selectionAnchor, *attempt,
                                   ProvinceSelectionSalt) % eligible.size());
        const EligibleProvince& selectedProvince = eligible[provinceIndex];
        const WorldEventInstanceId instanceIdBefore = nextInstanceId;
        if (TryCreate(map, playerId, selectedProvince.provinceId, InvalidProvinceId,
                      WorldEventTriggerDomain::ProvincePeriodic,
                      selectedProvince.context, {}, currentTick, *attempt,
                      PeriodicTriggerSalt, InvalidWorldJourneyId,
                      selectedProvince.definitionIds))
        {
            const auto instanceIt = instances.find(instanceIdBefore);
            if (instanceIt != instances.end())
                periodicScheduler.RecordSuccess(
                    playerId, instanceIt->second.definitionId,
                    instanceIt->second.definitionId.empty() ? 0 :
                        definitions->at(instanceIt->second.definitionId).repeatCooldownTicks,
                    currentTick);
        }
    }
}

bool ProvinceEventSystem::TriggerDiscovery(GlobalMap& map, PlayerId playerId,
                                           ProvinceId provinceId,
                                           std::uint64_t currentTick)
{
    if (playerId == InvalidPlayerId)
        return false;
    auto* eventProvince = dynamic_cast<EventProvince*>(map.FindProvince(provinceId));
    if (eventProvince == nullptr || eventProvince->HasResolvedFor(playerId))
        return false;
    eventProvince->RecordDiscoveryAttempt(playerId);
    const auto context = BuildContext(map, provinceId, 0);
    const std::uint64_t attempt = eventProvince->GetDiscoveryAttempts(playerId);
    const auto before = nextInstanceId;
    const bool created = TryCreate(map, playerId, provinceId, InvalidProvinceId,
                                   WorldEventTriggerDomain::Discovery, context,
                                   eventProvince->GetEventPoolId(), currentTick, attempt,
                                   0x444953434F564552ull ^ static_cast<std::uint64_t>(playerId));
    if (created && nextInstanceId != before)
        eventProvince->MarkResolvedFor(playerId);
    return created;
}

bool ProvinceEventSystem::PublishNotification(std::string_view definitionId,
                                              PlayerId ownerId,
                                              ProvinceId provinceId,
                                              ProvinceId secondaryProvinceId,
                                              std::uint64_t currentTick,
                                              std::string descriptionOverride)
{
    if (definitions == nullptr || nextInstanceId == InvalidWorldEventInstanceId ||
        ownerId == InvalidPlayerId || provinceId == InvalidProvinceId)
        return false;
    const auto definitionIt = definitions->find(std::string(definitionId));
    if (definitionIt == definitions->end())
        return false;

    const WorldEventDefinition& definition = definitionIt->second;
    WorldEventNotificationView notification;
    notification.instanceId = nextInstanceId++;
    notification.ownerId = ownerId;
    notification.provinceId = provinceId;
    notification.secondaryProvinceId = secondaryProvinceId;
    notification.definitionId = definition.id;
    notification.title = definition.title.empty() ? definition.name : definition.title;
    notification.description = descriptionOverride.empty()
        ? definition.description : std::move(descriptionOverride);
    notification.trigger = definition.trigger;
    notification.startTick = currentTick;
    notification.endTick = currentTick;
    notification.expired = true;
    feed.Push(std::move(notification));
    return true;
}

bool ProvinceEventSystem::TriggerRoute(GlobalMap& map, PlayerId playerId,
                                       ProvinceId sourceProvinceId,
                                       ProvinceId targetProvinceId, int routeLevel,
                                       std::uint64_t currentTick,
                                       WorldJourneyId journeyId,
                                       int scoutEscortCount,
                                       std::size_t completedLeg,
                                       std::uint64_t journeyAttemptCounter)
{
    const auto* connection = map.FindConnection(sourceProvinceId, targetProvinceId);
    if (playerId == InvalidPlayerId || sourceProvinceId == InvalidProvinceId ||
        targetProvinceId == InvalidProvinceId ||
        connection == nullptr)
        return false;
    const auto context = BuildContext(map, targetProvinceId, routeLevel);
    EventEligibilityContext routeContext = context;
    const RouteTraversalStats stats = connection->ResolveTraversalStats(
        {sourceProvinceId, targetProvinceId, playerId});
    routeContext.routeIncidentChanceReductionBasisPoints =
        std::clamp(stats.incidentChanceReductionBasisPoints, 0, 10000);
    routeContext.routeLengthUnits = std::max(1, connection->GetLengthUnits());
    routeContext.routeQualityBasisPoints = std::clamp(
        static_cast<int>(std::lround(10000.0 /
            std::max(0.25, stats.traversalTimeMultiplier))), 2500, 20000);
    routeContext.scoutEscortCount = std::max(1, scoutEscortCount);
    const std::uint64_t attempt = journeyAttemptCounter != 0
        ? journeyAttemptCounter : static_cast<std::uint64_t>(std::max(0, routeLevel));
    const std::uint64_t salt = 0x524F555445ull ^
        static_cast<std::uint64_t>(sourceProvinceId) ^
        Mix(static_cast<std::uint64_t>(journeyId)) ^
        Mix(static_cast<std::uint64_t>(completedLeg));
    return TryCreate(map, playerId, targetProvinceId, sourceProvinceId,
                     WorldEventTriggerDomain::Route, routeContext, {}, currentTick,
                     attempt, salt,
                     journeyId);
}

bool ProvinceEventSystem::TriggerRaid(GlobalMap& map, PlayerId playerId,
                                      ProvinceId provinceId, int routeLevel,
                                      std::uint64_t currentTick)
{
    if (playerId == InvalidPlayerId || map.FindProvince(provinceId) == nullptr)
        return false;
    const auto context = BuildContext(map, provinceId, routeLevel);
    return TryCreate(map, playerId, provinceId, InvalidProvinceId,
                     WorldEventTriggerDomain::Raid, context, {}, currentTick,
                     static_cast<std::uint64_t>(routeLevel), 0x52414944ull);
}

const WorldEventInstance* ProvinceEventSystem::FindInstance(WorldEventInstanceId id) const
{
    const auto it = instances.find(id);
    return it == instances.end() ? nullptr : &it->second;
}

bool ProvinceEventSystem::RestorePeriodicSchedulerState(
    PlayerId playerId, PeriodicEventSchedulerState state)
{
    if (definitions == nullptr || playerId == InvalidPlayerId)
        return false;
    for (const auto& [definitionId, cooldownUntil] : state.definitionCooldownUntil)
    {
        (void)cooldownUntil;
        const auto definitionIt = definitions->find(definitionId);
        if (definitionIt == definitions->end() ||
            definitionIt->second.trigger != WorldEventTriggerDomain::ProvincePeriodic)
            return false;
    }
    return periodicScheduler.RestoreState(playerId, std::move(state));
}

bool ProvinceEventSystem::RestoreInstance(WorldEventInstance instance)
{
    if (instance.id == InvalidWorldEventInstanceId || instance.definitionId.empty() ||
        instances.contains(instance.id) ||
        instances.size() >= PersistenceLimits::MaxWorldEventInstances)
        return false;
    if (definitions == nullptr || !definitions->contains(instance.definitionId))
        return false;
    instances.emplace(instance.id, std::move(instance));
    if (instances.rbegin()->first >= nextInstanceId)
    {
        if (instances.rbegin()->first == std::numeric_limits<WorldEventInstanceId>::max())
            nextInstanceId = 1;
        else
            nextInstanceId = instances.rbegin()->first + 1;
    }
    return true;
}

bool ProvinceEventSystem::RestoreInstance(GlobalMap& map, WorldEventInstance instance)
{
    const WorldEventInstanceId instanceId = instance.id;
    if (!RestoreInstance(std::move(instance)))
        return false;
    const auto it = instances.find(instanceId);
    if (it == instances.end() || it->second.expired)
        return true;
    const auto* province = dynamic_cast<const BuildableProvince*>(
        map.FindProvince(it->second.provinceId));
    if (province == nullptr || province->GetSimulation() == nullptr)
        return true;
    Player* owner = province->GetSimulation()->GetOwner();
    if (owner == nullptr)
        return true;
    const std::string source = "world_event." + std::to_string(instanceId);
    for (const auto& effect : it->second.appliedEffects)
        if (effect.kind == AppliedWorldEventEffectKind::TimedModifier)
        {
            BalanceModifier modifier;
            modifier.stat = effect.stat;
            modifier.additive = effect.additive;
            modifier.multiplier = effect.multiplier;
            modifier.scope = BalanceModifierScope::Global();
            modifier.scope.provinceId = it->second.provinceId;
            modifier.source = source;
            owner->balanceModifiers.AddModifier(std::move(modifier));
        }
    return true;
}

bool ProvinceEventSystem::RecordAppliedEffect(WorldEventInstanceId eventId,
                                              AppliedWorldEventEffect effect)
{
    const auto instanceIt = instances.find(eventId);
    if (instanceIt == instances.end())
        return false;
    instanceIt->second.appliedEffects.push_back(effect);
    return feed.RecordAppliedEffect(eventId, std::move(effect));
}

std::vector<StartRaidRequest> ProvinceEventSystem::ConsumePendingRaidRequests()
{
    std::vector<StartRaidRequest> result;
    for (auto& [eventId, instance] : instances)
    {
        if (instance.raidStarted)
            continue;
        for (const auto& effect : instance.effects)
        {
            const auto* raid = std::get_if<StartRaidEffect>(&effect);
            if (raid == nullptr || raid->strength <= 0)
                continue;
            result.push_back({eventId, instance.ownerId, instance.provinceId, raid->strength});
            break;
        }
    }
    return result;
}

std::vector<JourneyUnitLossRequest> ProvinceEventSystem::ConsumePendingJourneyUnitLosses()
{
    std::vector<JourneyUnitLossRequest> result;
    for (auto& [eventId, instance] : instances)
    {
        if (instance.journeyEffectApplied ||
            instance.journeyId == InvalidWorldJourneyId)
            continue;
        for (const auto& effect : instance.effects)
        {
            const auto* loss = std::get_if<KillJourneyUnitsEffect>(&effect);
            if (loss == nullptr ||
                (loss->amount <= 0 && loss->maximumFractionBasisPoints <= 0))
                continue;
            result.push_back({eventId, instance.journeyId, instance.ownerId, loss->amount,
                              loss->minimumFractionBasisPoints,
                              loss->maximumFractionBasisPoints,
                              loss->minimumUnits, loss->maximumUnits,
                              instance.outcomeRoll});
            break;
        }
    }
    return result;
}

bool ProvinceEventSystem::ConfirmRaidStarted(WorldEventInstanceId eventId)
{
    const auto it = instances.find(eventId);
    if (it == instances.end() || it->second.raidStarted ||
        !HasPendingRaidEffect(it->second))
        return false;
    it->second.raidStarted = true;
    PruneProcessedEvents(instances);
    return true;
}

bool ProvinceEventSystem::ConfirmJourneyEffectApplied(WorldEventInstanceId eventId)
{
    const auto it = instances.find(eventId);
    if (it == instances.end() || it->second.journeyEffectApplied ||
        !HasPendingJourneyEffect(it->second))
        return false;
    it->second.journeyEffectApplied = true;
    PruneProcessedEvents(instances);
    return true;
}

void ProvinceEventSystem::Clear()
{
    nextInstanceId = 1;
    periodicScheduler.Clear();
    instances.clear();
    feed.Clear();
}
