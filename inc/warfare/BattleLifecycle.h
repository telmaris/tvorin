#ifndef BATTLE_LIFECYCLE_H
#define BATTLE_LIFECYCLE_H

#include "core/PersistenceLimits.h"
#include "warfare/BattleSimulator.h"
#include "warfare/BattleUnit.h"
#include "warfare/RaidDamage.h"
#include "world/WorldJourney.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

class Player;

enum class BattleLifecycleStatus : std::uint8_t
{
    InTransit,
    Active,
    Resolved,
    Failed,
    Cancelled
};

struct BattleInstance
{
    BattleId id{InvalidBattleId};
    PlayerId attackerId{InvalidPlayerId};
    PlayerId defenderId{InvalidPlayerId};
    ProvinceId sourceProvinceId{InvalidProvinceId};
    ProvinceId targetProvinceId{InvalidProvinceId};
    WorldJourneyId journeyId{InvalidWorldJourneyId};
    std::vector<int> attackerUnitIds;
    std::vector<int> defenderUnitIds;
    BattleLifecycleStatus status{BattleLifecycleStatus::InTransit};
    std::uint64_t startTick{0};
    std::uint64_t endTick{0};
    // Immutable rules captured at operation start. Balance changes after a
    // battle has begun must not alter its eventual outcome or duration.
    BattleRules rules{};
    BattleSideSnapshot attackerSnapshot;
    BattleSideSnapshot defenderSnapshot;
    std::optional<BattleOutcome> outcome;
    bool isRaid{false};
    int raidStrength{0};
};

struct BattleReport
{
    BattleId battleId{InvalidBattleId};
    PlayerId attackerId{InvalidPlayerId};
    PlayerId defenderId{InvalidPlayerId};
    ProvinceId sourceProvinceId{InvalidProvinceId};
    ProvinceId targetProvinceId{InvalidProvinceId};
    BattleOutcome outcome;
    bool banditTransformed{false};
    bool cityDamaged{false};
    bool raid{false};
    std::vector<int> destroyedBuildingIds;
    std::map<ResourceType, int> lostResources;
};

class BattleLifecycleSystem
{
public:
    static constexpr std::size_t MaxActiveBattles =
        PersistenceLimits::MaxConcurrentBattles;
    static constexpr std::size_t MaxBattleRecords =
        PersistenceLimits::MaxBattleRecords;
    static constexpr std::size_t MaxBattleReports =
        PersistenceLimits::MaxBattleReports;

    bool StartProvinceAttack(Player& attacker, ProvinceId sourceProvinceId,
                             ProvinceId targetProvinceId,
                             const std::vector<int>& unitInstanceIds,
                             GlobalMap& map, WorldJourneySystem& journeys,
                             const std::map<PlayerId, Player*>& players,
                             std::uint64_t currentTick, BattleSeed campaignSeed,
                             BattleId& createdId, std::string& failureReason,
                             ExpeditionLoadout expeditionLoadout = {},
                             JourneySpeedProfile speedProfile = {});
    bool StartRaid(Player& defender, ProvinceId targetProvinceId, int raidStrength,
                   GlobalMap& map, std::uint64_t currentTick, BattleSeed campaignSeed,
                   BattleId& createdId, std::string& failureReason);
    void Update(GlobalMap& map, WorldJourneySystem& journeys,
                const std::map<PlayerId, Player*>& players,
                std::uint64_t currentTick, BattleSeed campaignSeed,
                bool advanceJourneys = true);

    bool Cancel(BattleId battleId, const std::map<PlayerId, Player*>& players,
                WorldJourneySystem& journeys);
    const std::map<BattleId, BattleInstance>& GetBattles() const { return battles; }
    const std::vector<BattleReport>& GetReports() const { return reports; }
    const BattleInstance* FindBattle(BattleId id) const;
    std::vector<BattleReport> ConsumeReports();
    BattleId GetNextBattleId() const { return nextBattleId; }
    bool Restore(BattleId nextId, std::vector<BattleInstance> restoredBattles,
                 std::vector<BattleReport> restoredReports, const GlobalMap& map,
                 const std::map<PlayerId, Player*>& players,
                 const WorldJourneySystem& journeys, std::string& failureReason);
    void Clear();

private:
    bool IsValidDefenderUnit(const BattleUnit& unit, PlayerId defenderId,
                             ProvinceId targetProvinceId) const;
    BattleSideSnapshot SnapshotSide(const BattleInstance& battle, bool attacker,
                                    const std::map<PlayerId, Player*>& players) const;
    void ReleaseAttackerUnits(const BattleInstance& battle,
                              const std::map<PlayerId, Player*>& players,
                              ProvinceId fallbackProvinceId);
    void ResolveBattle(BattleInstance& battle, GlobalMap& map,
                       const std::map<PlayerId, Player*>& players,
                       BattleSeed campaignSeed);
    void ResolveRaid(BattleInstance& battle, GlobalMap& map,
                     const std::map<PlayerId, Player*>& players,
                     BattleSeed campaignSeed);

    BattleId nextBattleId{1};
    std::map<BattleId, BattleInstance> battles;
    std::vector<BattleReport> reports;
};

#endif
