#ifndef WORLD_JOURNEY_H
#define WORLD_JOURNEY_H

#include "data/Resource.h"
#include "world/Expedition.h"
#include "world/GlobalMap.h"
#include "world/JourneyTiming.h"
#include "world/WorldIds.h"

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

struct ScoutParty { std::vector<int> unitInstanceIds; };
struct TradeCargo
{
    ResourceType offerType{ResourceType::Null};
    ResourceType requestType{ResourceType::Null};
    int amount{0};
};
struct ArmyParty { std::vector<int> unitInstanceIds; };
struct ResourceConvoy { std::vector<ResourceAmount> cargo; };
struct ArmyTransferParty
{
    std::vector<int> unitInstanceIds;
    int destinationBarracksBuildingId{0};
};
struct Colonists { int householdCount{0}; };
using WorldJourneyPayload = std::variant<ScoutParty, TradeCargo, ArmyParty,
                                         ResourceConvoy, ArmyTransferParty, Colonists>;

struct WorldJourneyRules
{
    std::uint64_t baseLegDurationTicks{100};
    // Snapshot of player-wide route technology/focus effects. The connection
    // still contributes its own physical level multiplier at each leg.
    double routeTravelSpeedMultiplier{1.0};
    JourneySpeedProfile speedProfile{};
};

enum class WorldJourneyKind : std::uint8_t
{
    Unknown,
    Scout,
    Trade,
    Attack,
    Colonization,
    ResourceTransfer,
    ArmyTransfer
};

enum class WorldJourneyStatus : std::uint8_t
{
    Planned, InTransit, Succeeded, Failed, AwaitingUnload, Cancelled
};

struct WorldJourney
{
    WorldJourneyId id{InvalidWorldJourneyId};
    PlayerId ownerId{InvalidPlayerId};
    ProvinceId sourceProvinceId{InvalidProvinceId};
    ProvinceId targetProvinceId{InvalidProvinceId};
    WorldJourneyKind kind{WorldJourneyKind::Unknown};
    // The frozen plan is the only route representation owned by a journey.
    // Connection IDs are used for topology, while the remaining fields keep
    // the exact timing captured at departure.
    std::vector<JourneyLegPlan> legPlan;
    std::size_t currentLeg{0};
    WorldJourneyStatus status{WorldJourneyStatus::Planned};
    std::uint64_t startTick{0};
    std::uint64_t legCompletionTick{0};
    std::uint64_t deterministicAttemptCounter{0};
    WorldJourneyRules rules{};
    ExpeditionLoadout loadout{};
    WorldJourneyPayload payload{ScoutParty{}};
};

struct WorldJourneyStartResult
{
    bool accepted{false};
    WorldJourneyId journeyId{InvalidWorldJourneyId};
    std::string failureReason;

    explicit operator bool() const { return accepted; }
};

struct JourneyLegCompleted
{
    WorldJourneyId journeyId{InvalidWorldJourneyId};
    PlayerId ownerId{InvalidPlayerId};
    ProvinceId fromProvinceId{InvalidProvinceId};
    ProvinceId toProvinceId{InvalidProvinceId};
    ProvinceConnectionId connectionId{InvalidProvinceConnectionId};
    std::size_t completedLeg{0};
    bool journeySucceeded{false};
};

class WorldJourneySystem
{
public:
    WorldJourneyStartResult Start(WorldJourney journey, const GlobalMap& map,
                                  std::uint64_t currentTick,
                                  const WorldJourneyRules& rules);
    void Update(const GlobalMap& map, std::uint64_t currentTick);
    bool FailJourney(WorldJourneyId journeyId);
    // Removes the lowest stable scout instance IDs from a journey. The
    // journey fails only when the last assigned scout is lost; sending a
    // larger party therefore provides real redundancy against route events.
    std::vector<int> ApplyScoutUnitLoss(WorldJourneyId journeyId, int amount);
    std::vector<int> ApplyJourneyUnitLoss(WorldJourneyId journeyId, int amount,
                                          std::uint64_t outcomeRoll = 0);
    bool MarkAwaitingUnload(WorldJourneyId journeyId);
    bool Cancel(WorldJourneyId journeyId);
    const std::map<WorldJourneyId, WorldJourney>& GetJourneys() const { return journeys; }
    std::map<WorldJourneyId, WorldJourney>& GetJourneysForAuthority() { return journeys; }
    std::vector<JourneyLegCompleted> ConsumeLegEvents();
    const std::vector<JourneyLegCompleted>& GetPendingLegEvents() const { return legEvents; }
    // Battle lifecycle consumes only its own journey events. Other payload
    // handlers (scouting, trade and colonization) can consume their events
    // afterwards without losing a completion notification.
    std::vector<JourneyLegCompleted> ConsumeLegEventsFor(WorldJourneyId journeyId);
    WorldJourneyId GetNextJourneyId() const { return nextJourneyId; }
    bool Restore(WorldJourneyId nextId, std::vector<WorldJourney> restored,
                 const GlobalMap& map, std::string& failureReason);
    void Clear();

private:
    bool ValidatePath(const WorldJourney& journey, const GlobalMap& map,
                      std::string& failureReason) const;
    std::uint64_t LegDuration(const WorldJourney& journey, const GlobalMap& map,
                              ProvinceConnectionId connectionId,
                              const WorldJourneyRules& rules) const;
    WorldJourneyId nextJourneyId{1};
    std::map<WorldJourneyId, WorldJourney> journeys;
    std::vector<JourneyLegCompleted> legEvents;
};

#endif
