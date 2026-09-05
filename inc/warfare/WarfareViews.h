#ifndef WARFARE_VIEWS_H
#define WARFARE_VIEWS_H

#include "core/Types.h"
#include "data/Resource.h"
#include "warfare/BattleLifecycle.h"
#include "world/WorldEventSystem.h"
#include "world/WorldJourney.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class Building;
class Player;
class TileMap;

// Coarse information deliberately used by presentation dialogs when the
// viewer is not entitled to see an immutable battle snapshot.
enum class WorldForceClass : std::uint8_t
{
    Unknown,
    Patrol,
    Warband,
    Army,
    Host
};

struct JourneyStatusView
{
    WorldJourneyId journeyId{InvalidWorldJourneyId};
    PlayerId ownerId{InvalidPlayerId};
    ProvinceId originProvinceId{InvalidProvinceId};
    ProvinceId targetProvinceId{InvalidProvinceId};
    WorldJourneyStatus status{WorldJourneyStatus::Planned};
    WorldJourneyKind kind{WorldJourneyKind::Unknown};
    std::size_t currentLeg{0};
    std::size_t totalLegs{0};
    std::uint64_t startTick{0};
    std::uint64_t etaTick{0};
    std::uint64_t remainingTicks{0};
};

struct BattleStatusView
{
    BattleId battleId{InvalidBattleId};
    PlayerId attackerId{InvalidPlayerId};
    PlayerId defenderId{InvalidPlayerId};
    ProvinceId originProvinceId{InvalidProvinceId};
    ProvinceId targetProvinceId{InvalidProvinceId};
    WorldJourneyId journeyId{InvalidWorldJourneyId};
    BattleLifecycleStatus status{BattleLifecycleStatus::InTransit};
    std::uint64_t startTick{0};
    std::uint64_t endTick{0};
    std::uint64_t remainingTicks{0};
    WorldForceClass attackerForceClass{WorldForceClass::Unknown};
    WorldForceClass defenderForceClass{WorldForceClass::Unknown};
    bool isRaid{false};
};

struct BattleReportView
{
    BattleId battleId{InvalidBattleId};
    PlayerId attackerId{InvalidPlayerId};
    PlayerId defenderId{InvalidPlayerId};
    ProvinceId originProvinceId{InvalidProvinceId};
    ProvinceId targetProvinceId{InvalidProvinceId};
    BattleWinner winner{BattleWinner::Invalid};
    int attackerLosses{0};
    int defenderLosses{0};
    double lootValue{0.0};
    bool crushingVictory{false};
    bool banditTransformed{false};
    bool cityDamaged{false};
    bool raid{false};
    std::vector<int> destroyedBuildingIds;
    std::map<ResourceType, int> lostResources;
};

struct ProvinceDefenseView
{
    ProvinceId provinceId{InvalidProvinceId};
    int buildingId{0};
    BuildingType buildingType{BuildingType::Building};
    Vec2f center{};
    double radius{0.0};
    double protection{0.0};
    int garrisonUsed{0};
    int garrisonCapacity{0};
    double upkeepPerMinute{0.0};
    double upkeepDebtPackages{0.0};
    int bufferedFood{0};
    int incomingFood{0};
    int duePackages{0};
    GarrisonSupplyStatus supplyStatus{GarrisonSupplyStatus::Supplied};
    bool protectionActive{false};
};

JourneyStatusView BuildJourneyStatusView(const WorldJourney& journey,
                                         std::uint64_t currentTick);
std::vector<JourneyStatusView> BuildJourneyStatusViews(const WorldJourneySystem& journeys,
                                                        std::uint64_t currentTick,
                                                        PlayerId viewerId = InvalidPlayerId,
                                                        std::size_t maximum = 64);
BattleStatusView BuildBattleStatusView(const BattleInstance& battle,
                                       std::uint64_t currentTick,
                                       PlayerId viewerId = InvalidPlayerId);
std::vector<BattleStatusView> BuildBattleStatusViews(const BattleLifecycleSystem& battles,
                                                     std::uint64_t currentTick,
                                                     PlayerId viewerId = InvalidPlayerId,
                                                     std::size_t maximum = 64);
BattleReportView BuildBattleReportView(const BattleReport& report);
ProvinceDefenseView BuildProvinceDefenseView(const TileMap& map, const Player& player,
                                             ProvinceId provinceId,
                                             const Building& defenseBuilding);

const char* ToString(WorldJourneyStatus status);
const char* ToString(BattleLifecycleStatus status);
const char* ToString(BattleWinner winner);
const char* ToString(WorldForceClass forceClass);
const char* ToString(GarrisonSupplyStatus status);

#endif
