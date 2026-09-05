#ifndef WORLD_EVENT_SYSTEM_H
#define WORLD_EVENT_SYSTEM_H

#include "core/PersistenceLimits.h"
#include "world/GlobalMap.h"
#include "world/WorldEventDefinition.h"
#include "world/WorldIds.h"

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Input to the pure event eligibility rules. It contains values copied from
// runtime provinces, never pointers to a province or to a definition.
struct EventEligibilityContext
{
    ProvinceKind provinceKind{ProvinceKind::Buildable};
    std::vector<std::string> provinceTraits;
    std::vector<std::string> adjacentTraits;
    int routeLevel{0};
    // The physical connection supplies this value for route triggers. It is
    // kept separate from routeLevel so a route upgrade can reduce the base
    // incident roll without changing the event catalog or reroll seed.
    int routeIncidentChanceReductionBasisPoints{0};
};

class EventEligibilityService
{
public:
    static bool IsEligible(const WorldEventDefinition& definition,
                           WorldEventTriggerDomain trigger,
                           const EventEligibilityContext& context);
};

class WeightedEventSelector
{
public:
    // Candidate order is part of the deterministic contract. Callers must
    // provide it in catalog order; the selector never reorders definitions.
    static const WorldEventDefinition* Select(
        const std::vector<const WorldEventDefinition*>& candidates,
        std::uint64_t roll);
};

struct ProvinceEventCadenceState
{
    std::uint64_t nextCheckTick{0};
    std::uint64_t attemptCounter{0};
};

using ProvinceEventCadenceByDefinition = std::map<std::string, ProvinceEventCadenceState>;
using ProvinceEventCadenceStateMap = std::map<ProvinceId, ProvinceEventCadenceByDefinition>;

enum class AppliedWorldEventEffectKind : std::uint8_t
{
    ResourceDelta,
    TimedModifier,
    TradeScore,
    BuildingLoss,
    UnitLoss,
    RaidStarted
};

struct AppliedWorldEventEffect
{
    AppliedWorldEventEffectKind kind{AppliedWorldEventEffectKind::ResourceDelta};
    ResourceType resourceType{ResourceType::Null};
    int amount{0};
    BalanceStat stat{BalanceStat::BuildTime};
    double additive{0.0};
    double multiplier{1.0};
    std::uint64_t durationTicks{0};
};

struct WorldEventInstance
{
    WorldEventInstanceId id{InvalidWorldEventInstanceId};
    PlayerId ownerId{InvalidPlayerId};
    ProvinceId provinceId{InvalidProvinceId};
    ProvinceId secondaryProvinceId{InvalidProvinceId};
    std::string definitionId;
    WorldEventTriggerDomain trigger{WorldEventTriggerDomain::ProvincePeriodic};
    std::uint64_t startTick{0};
    std::uint64_t endTick{0};
    std::uint64_t outcomeRoll{0};
    std::uint64_t deterministicAttemptCounter{0};
    WorldJourneyId journeyId{InvalidWorldJourneyId};
    std::vector<WorldEventEffect> effects;
    std::vector<AppliedWorldEventEffect> appliedEffects;
    bool expired{false};
    bool raidStarted{false};
    bool journeyEffectApplied{false};
};

struct StartRaidRequest
{
    WorldEventInstanceId eventId{InvalidWorldEventInstanceId};
    PlayerId ownerId{InvalidPlayerId};
    ProvinceId provinceId{InvalidProvinceId};
    int strength{0};
};

struct JourneyUnitLossRequest
{
    WorldEventInstanceId eventId{InvalidWorldEventInstanceId};
    WorldJourneyId journeyId{InvalidWorldJourneyId};
    PlayerId ownerId{InvalidPlayerId};
    int amount{0};
};

// This is the only representation the UI/network-facing feed receives. It
// intentionally has no pointers or references into the simulation world.
struct WorldEventNotificationView
{
    WorldEventInstanceId instanceId{InvalidWorldEventInstanceId};
    PlayerId ownerId{InvalidPlayerId};
    ProvinceId provinceId{InvalidProvinceId};
    ProvinceId secondaryProvinceId{InvalidProvinceId};
    std::string definitionId;
    std::string title;
    std::string description;
    WorldEventTriggerDomain trigger{WorldEventTriggerDomain::ProvincePeriodic};
    std::uint64_t startTick{0};
    std::uint64_t endTick{0};
    std::uint64_t outcomeRoll{0};
    bool expired{false};
    std::vector<AppliedWorldEventEffect> appliedEffects;
};

class WorldEventFeed
{
public:
    explicit WorldEventFeed(
        std::size_t maximumHistory = PersistenceLimits::MaxWorldEventFeedEntries)
        : maximumHistory(maximumHistory == 0 ? 1 : maximumHistory) {}

    void Push(WorldEventNotificationView notification);
    void MarkExpired(WorldEventInstanceId instanceId);
    bool RecordAppliedEffect(WorldEventInstanceId instanceId,
                             AppliedWorldEventEffect effect);
    std::vector<WorldEventNotificationView> ConsumeFor(PlayerId playerId);
    const std::deque<WorldEventNotificationView>& GetHistory() const { return history; }
    bool HasDelivered(PlayerId playerId, WorldEventInstanceId instanceId) const;
    void RestoreNotification(WorldEventNotificationView notification)
    {
        Push(std::move(notification));
    }
    void Clear();

private:
    bool IsVisibleTo(const WorldEventNotificationView& notification, PlayerId playerId) const;
    void Trim();

    std::size_t maximumHistory{PersistenceLimits::MaxWorldEventFeedEntries};
    std::deque<WorldEventNotificationView> history;
    std::set<WorldEventInstanceId> knownIds;
    std::map<PlayerId, std::set<WorldEventInstanceId>> deliveredByPlayer;
};

class ProvinceEventSystem
{
public:
    explicit ProvinceEventSystem(std::uint32_t campaignSeed = 0,
                                 const WorldEventCatalog* definitions = nullptr);

    void SetCampaignSeed(std::uint32_t seed) { campaignSeed = seed; }

    void Update(GlobalMap& map, std::uint64_t currentTick);
    bool TriggerDiscovery(GlobalMap& map, PlayerId playerId, ProvinceId provinceId,
                          std::uint64_t currentTick);
    bool TriggerRoute(GlobalMap& map, PlayerId playerId, ProvinceId sourceProvinceId,
                      ProvinceId targetProvinceId, int routeLevel,
                      std::uint64_t currentTick,
                      WorldJourneyId journeyId = InvalidWorldJourneyId);
    bool TriggerRaid(GlobalMap& map, PlayerId playerId, ProvinceId provinceId,
                     int routeLevel, std::uint64_t currentTick);
    bool PublishNotification(std::string_view definitionId, PlayerId ownerId,
                             ProvinceId provinceId, ProvinceId secondaryProvinceId,
                             std::uint64_t currentTick,
                             std::string descriptionOverride = {});

    const std::map<WorldEventInstanceId, WorldEventInstance>& GetInstances() const
    {
        return instances;
    }
    const WorldEventInstance* FindInstance(WorldEventInstanceId id) const;
    const ProvinceEventCadenceStateMap& GetCadenceStates() const { return cadenceStates; }
    WorldEventFeed& GetFeed() { return feed; }
    const WorldEventFeed& GetFeed() const { return feed; }
    std::vector<WorldEventNotificationView> ConsumeFeedFor(PlayerId playerId)
    {
        return feed.ConsumeFor(playerId);
    }

    WorldEventInstanceId GetNextInstanceId() const { return nextInstanceId; }
    void SetNextInstanceId(WorldEventInstanceId value)
    {
        nextInstanceId = value == InvalidWorldEventInstanceId ? 1 : value;
    }
    void RestoreCadenceState(ProvinceId provinceId, std::string definitionId,
                             ProvinceEventCadenceState state);
    bool RestoreInstance(WorldEventInstance instance);
    // Appends an effect that was resolved by a later authority service and
    // mirrors it into the pointer-free feed entry with the same instance ID.
    bool RecordAppliedEffect(WorldEventInstanceId eventId,
                             AppliedWorldEventEffect effect);
    // Pending authority effects stay retryable until their owning service
    // confirms a successful state transition.
    std::vector<StartRaidRequest> ConsumePendingRaidRequests();
    std::vector<JourneyUnitLossRequest> ConsumePendingJourneyUnitLosses();
    bool ConfirmRaidStarted(WorldEventInstanceId eventId);
    bool ConfirmJourneyEffectApplied(WorldEventInstanceId eventId);
    void Clear();

private:
    bool TryCreate(GlobalMap& map, PlayerId ownerId, ProvinceId provinceId,
                   ProvinceId secondaryProvinceId, WorldEventTriggerDomain trigger,
                   const EventEligibilityContext& context, std::string_view poolId,
                   std::uint64_t currentTick, std::uint64_t attemptCounter,
                   std::uint64_t triggerSalt,
                   WorldJourneyId journeyId = InvalidWorldJourneyId,
                   const std::vector<std::string>& dueDefinitionIds = {});
    EventEligibilityContext BuildContext(const GlobalMap& map, ProvinceId provinceId,
                                         int routeLevel) const;
    std::vector<const WorldEventDefinition*> FindCandidates(
        WorldEventTriggerDomain trigger, const EventEligibilityContext& context,
        std::string_view poolId, const std::vector<std::string>& dueDefinitionIds) const;
    void ApplyEffects(GlobalMap& map, WorldEventInstance& instance);
    void ExpireInstances(GlobalMap& map, std::uint64_t currentTick);
    std::uint64_t NextDeterministicValue(ProvinceId provinceId,
                                         std::uint64_t attemptCounter,
                                         std::uint64_t salt) const;
    static std::uint64_t Mix(std::uint64_t value);

    std::uint32_t campaignSeed{0};
    const WorldEventCatalog* definitions{nullptr};
    WorldEventInstanceId nextInstanceId{1};
    ProvinceEventCadenceStateMap cadenceStates;
    std::map<WorldEventInstanceId, WorldEventInstance> instances;
    WorldEventFeed feed;
};

#endif
