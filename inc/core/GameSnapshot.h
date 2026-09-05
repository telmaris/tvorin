#ifndef GAME_SNAPSHOT_H
#define GAME_SNAPSHOT_H

#include "core/Serialization.h"
#include "economy/BuildingConfig.h"
#include "core/Types.h"
#include "raylib.h"
#include "world/GlobalMap.h"
#include "core/PersistenceLimits.h"
#include "warfare/WarfareViews.h"
#include "warfare/TaskGroup.h"
#include "world/WorldEventSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <iomanip>
#include <sstream>
#include <set>
#include <string>
#include <vector>

// Render-only player information. A full snapshot transmits this immutable
// palette once; tile deltas refer to it through buildingOwnerId.
struct GameSnapshotPlayer
{
    int id{-1};
    Color color{WHITE};
};

inline bool operator==(const GameSnapshotPlayer& lhs, const GameSnapshotPlayer& rhs)
{
    return lhs.id == rhs.id &&
           lhs.color.r == rhs.color.r &&
           lhs.color.g == rhs.color.g &&
           lhs.color.b == rhs.color.b &&
           lhs.color.a == rhs.color.a;
}

inline void SerializeSnapshotPlayer(std::ostringstream& out, const GameSnapshotPlayer& player)
{
    out << player.id << ' '
        << static_cast<int>(player.color.r) << ' '
        << static_cast<int>(player.color.g) << ' '
        << static_cast<int>(player.color.b) << ' '
        << static_cast<int>(player.color.a) << ' ';
}

inline bool TryDeserializeSnapshotPlayer(std::istringstream& in, GameSnapshotPlayer& player)
{
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 0;
    if (!(in >> player.id >> r >> g >> b >> a))
        return false;

    player.color = Color{
        static_cast<unsigned char>(std::clamp(r, 0, 255)),
        static_cast<unsigned char>(std::clamp(g, 0, 255)),
        static_cast<unsigned char>(std::clamp(b, 0, 255)),
        static_cast<unsigned char>(std::clamp(a, 0, 255))};
    return true;
}

struct GameSnapshotTile
{
    int terrainTextureId{0};
    int resourceOverlayTextureId{-1};
    bool hasOwner{false};
    Color ownerColor{BLANK};
    bool hasBuilding{false};
    BuildingType buildingType{BuildingType::Building};
    Vec2i buildingFootprint{1, 1};
    int buildingOwnerId{-1};
    bool isBuildingOperational{false};
    bool isBuildingUpgrading{false};
    bool roadDisconnected{false};
    float roadUtilization{0.0f};
    bool roadSaturated{false};
};

inline bool operator==(const GameSnapshotTile& lhs, const GameSnapshotTile& rhs)
{
    return lhs.terrainTextureId == rhs.terrainTextureId &&
           lhs.resourceOverlayTextureId == rhs.resourceOverlayTextureId &&
           lhs.hasOwner == rhs.hasOwner &&
           lhs.ownerColor.r == rhs.ownerColor.r &&
           lhs.ownerColor.g == rhs.ownerColor.g &&
           lhs.ownerColor.b == rhs.ownerColor.b &&
           lhs.ownerColor.a == rhs.ownerColor.a &&
           lhs.hasBuilding == rhs.hasBuilding &&
           lhs.buildingType == rhs.buildingType &&
           lhs.buildingFootprint.x == rhs.buildingFootprint.x &&
           lhs.buildingFootprint.y == rhs.buildingFootprint.y &&
           lhs.buildingOwnerId == rhs.buildingOwnerId &&
           lhs.isBuildingOperational == rhs.isBuildingOperational &&
           lhs.isBuildingUpgrading == rhs.isBuildingUpgrading &&
           lhs.roadDisconnected == rhs.roadDisconnected &&
           lhs.roadUtilization == rhs.roadUtilization &&
           lhs.roadSaturated == rhs.roadSaturated;
}

inline bool operator!=(const GameSnapshotTile& lhs, const GameSnapshotTile& rhs)
{
    return !(lhs == rhs);
}

inline void SerializeSnapshotTile(std::ostringstream& out, const GameSnapshotTile& tile)
{
    out << tile.terrainTextureId << ' '
        << tile.resourceOverlayTextureId << ' '
        << (tile.hasOwner ? 1 : 0) << ' '
        << static_cast<int>(tile.ownerColor.r) << ' '
        << static_cast<int>(tile.ownerColor.g) << ' '
        << static_cast<int>(tile.ownerColor.b) << ' '
        << static_cast<int>(tile.ownerColor.a) << ' '
        << (tile.hasBuilding ? 1 : 0) << ' '
        << static_cast<int>(tile.buildingType) << ' '
        << tile.buildingFootprint.x << ' '
        << tile.buildingFootprint.y << ' '
        << tile.buildingOwnerId << ' '
        << (tile.isBuildingOperational ? 1 : 0) << ' '
        << (tile.isBuildingUpgrading ? 1 : 0) << ' '
        << (tile.roadDisconnected ? 1 : 0) << ' '
        << tile.roadUtilization << ' '
        << (tile.roadSaturated ? 1 : 0) << ' ';
}

inline bool TryDeserializeSnapshotTile(std::istringstream& in, GameSnapshotTile& tile)
{
    int hasOwner = 0;
    int hasBuilding = 0;
    int r = 0;
    int g = 0;
    int b = 0;
    int a = 0;
    int buildingType = 0;
    int isBuildingOperational = 0;
    int isBuildingUpgrading = 0;
    int roadDisconnected = 0;
    int roadSaturated = 0;
    if (!(in >> tile.terrainTextureId >> tile.resourceOverlayTextureId >> hasOwner >> r >> g >> b >> a >> hasBuilding >> buildingType >> tile.buildingFootprint.x >> tile.buildingFootprint.y >> tile.buildingOwnerId >> isBuildingOperational >> isBuildingUpgrading >> roadDisconnected >> tile.roadUtilization >> roadSaturated))
        return false;
    tile.hasOwner = hasOwner != 0;
    tile.ownerColor = Color{
        static_cast<unsigned char>(std::clamp(r, 0, 255)),
        static_cast<unsigned char>(std::clamp(g, 0, 255)),
        static_cast<unsigned char>(std::clamp(b, 0, 255)),
        static_cast<unsigned char>(std::clamp(a, 0, 255))};
    tile.hasBuilding = hasBuilding != 0;
    tile.buildingType = static_cast<BuildingType>(buildingType);
    tile.isBuildingOperational = isBuildingOperational != 0;
    tile.isBuildingUpgrading = isBuildingUpgrading != 0;
    tile.roadDisconnected = roadDisconnected != 0;
    tile.roadUtilization = std::clamp(tile.roadUtilization, 0.0f, 1.0f);
    tile.roadSaturated = roadSaturated != 0;
    return true;
}

struct GameSnapshot
{
    static constexpr int MaxMapDimension = 501;
    static constexpr std::size_t MaxTileCount = 251'001;
    std::uint64_t simulationTick{0};
    int localPlayerId{0};
    ProvinceId activeProvinceId{InvalidProvinceId};
    GlobalMapView globalMapView;
    Vec2i mapSize{0, 0};
    std::vector<GameSnapshotPlayer> players;
    std::vector<GameSnapshotTile> tiles;
    // Bounded, pointer-free campaign presentation. These are summaries only;
    // correction state continues to use SerializeSimulationState().
    std::vector<JourneyStatusView> journeyStatuses;
    std::vector<BattleStatusView> battleStatuses;
    std::vector<BattleReportView> battleReports;
    std::vector<WorldEventNotificationView> eventNotifications;
    std::vector<ProvinceDefenseView> provinceDefenses;
    std::vector<TaskGroupView> taskGroups;

    bool IsValid() const
    {
        std::size_t tileCount = 0;
        return TryGetTileCount(mapSize, tileCount) && tiles.size() == tileCount &&
            journeyStatuses.size() <= 64 && battleStatuses.size() <= 64 &&
            battleReports.size() <= 64 && eventNotifications.size() <= 64 &&
            provinceDefenses.size() <= 64 && taskGroups.size() <= 64;
    }

    std::string Serialize() const
    {
        Archive ar(SerializationVersion::GameSnapshotVersion);
        ar << simulationTick << localPlayerId << mapSize.x << mapSize.y << static_cast<int>(players.size());

        std::string payload = ar.GetString();
        std::ostringstream out;
        out << payload << ' ';
        for (const auto& player : players)
            SerializeSnapshotPlayer(out, player);
        out << activeProvinceId << ' ' << (globalMapView.fogOfWarEnabled ? 1 : 0) << ' '
            << globalMapView.nodes.size() << ' '
            << globalMapView.edges.size() << ' ';
        for (const auto& node : globalMapView.nodes)
        {
            out << node.id << ' ' << static_cast<int>(node.knowledge) << ' '
                << (node.visibleKind.has_value() ? static_cast<int>(*node.visibleKind) : -1) << ' '
                << (node.visibleOwner.has_value() ? *node.visibleOwner : InvalidPlayerId) << ' '
                << node.layoutPosition.x << ' ' << node.layoutPosition.y << ' '
                << std::quoted(node.displayName) << ' ' << node.traitIds.size() << ' ';
            for (const auto& traitId : node.traitIds)
                out << std::quoted(traitId) << ' ';
            out << node.naturalResourceTypes.size() << ' ';
            for (const ResourceType type : node.naturalResourceTypes)
                out << static_cast<int>(type) << ' ';
            out
                << (node.canScout ? 1 : 0) << ' ' << (node.canTrade ? 1 : 0) << ' '
                << (node.canAttack ? 1 : 0) << ' ' << (node.canColonize ? 1 : 0) << ' ';
        }
        for (const auto& edge : globalMapView.edges)
        {
            out << edge.from << ' ' << edge.to << ' ' << edge.connectionId << ' '
                << edge.lengthUnits << ' ' << edge.level << ' '
                << edge.routeTimeBasisPoints << ' ' << edge.incidentReductionBasisPoints << ' '
                << edge.nextLevel << ' ' << edge.nextRouteTimeBasisPoints << ' '
                << edge.nextIncidentReductionBasisPoints << ' ' << edge.nextUpgradeDurationTicks << ' '
                << edge.nextUpgradeCost.size() << ' ';
            for (const auto& cost : edge.nextUpgradeCost)
                out << static_cast<int>(cost.type) << ' ' << cost.amount << ' ';
            out << edge.upgradeRemainingTicks << ' '
                << (edge.canInspect ? 1 : 0) << ' '
                << (edge.canUpgrade ? 1 : 0) << ' ';
        }
        out << journeyStatuses.size() << ' ';
        for (const auto& journey : journeyStatuses)
            out << journey.journeyId << ' ' << journey.ownerId << ' '
                << journey.originProvinceId << ' ' << journey.targetProvinceId << ' '
                << static_cast<int>(journey.status) << ' ' << journey.currentLeg << ' '
                << journey.totalLegs << ' ' << journey.startTick << ' ' << journey.etaTick << ' '
                << journey.remainingTicks << ' ' << static_cast<int>(journey.kind) << ' ';
        out << battleStatuses.size() << ' ';
        for (const auto& battle : battleStatuses)
            out << battle.battleId << ' ' << battle.attackerId << ' ' << battle.defenderId << ' '
                << battle.originProvinceId << ' ' << battle.targetProvinceId << ' '
                << battle.journeyId << ' ' << static_cast<int>(battle.status) << ' '
                << battle.startTick << ' ' << battle.endTick << ' ' << battle.remainingTicks << ' '
                << static_cast<int>(battle.attackerForceClass) << ' '
                << static_cast<int>(battle.defenderForceClass) << ' ' << (battle.isRaid ? 1 : 0) << ' ';
        out << battleReports.size() << ' ';
        for (const auto& report : battleReports)
        {
            out << report.battleId << ' ' << report.attackerId << ' ' << report.defenderId << ' '
                << report.originProvinceId << ' ' << report.targetProvinceId << ' '
                << static_cast<int>(report.winner) << ' ' << report.attackerLosses << ' '
                << report.defenderLosses << ' ' << report.lootValue << ' '
                << (report.crushingVictory ? 1 : 0) << ' ' << (report.banditTransformed ? 1 : 0) << ' '
                << (report.cityDamaged ? 1 : 0) << ' ' << (report.raid ? 1 : 0) << ' '
                << report.destroyedBuildingIds.size() << ' ';
            for (const int id : report.destroyedBuildingIds)
                out << id << ' ';
            out << report.lostResources.size() << ' ';
            for (const auto& [type, amount] : report.lostResources)
                out << static_cast<int>(type) << ' ' << amount << ' ';
        }
        out << eventNotifications.size() << ' ';
        for (const auto& event : eventNotifications)
        {
            out << event.instanceId << ' ' << event.ownerId << ' ' << event.provinceId << ' '
                << event.secondaryProvinceId << ' ' << std::quoted(event.definitionId) << ' '
                << std::quoted(event.title) << ' ' << std::quoted(event.description) << ' '
                << static_cast<int>(event.trigger) << ' ' << event.startTick << ' '
                << event.endTick << ' ' << event.outcomeRoll << ' ' << (event.expired ? 1 : 0) << ' '
                << event.appliedEffects.size() << ' ';
            for (const auto& effect : event.appliedEffects)
                out << static_cast<int>(effect.kind) << ' ' << static_cast<int>(effect.resourceType) << ' '
                    << effect.amount << ' ' << static_cast<int>(effect.stat) << ' '
                    << effect.additive << ' ' << effect.multiplier << ' ' << effect.durationTicks << ' ';
        }
        out << provinceDefenses.size() << ' ';
        for (const auto& defense : provinceDefenses)
            out << defense.provinceId << ' ' << defense.buildingId << ' '
                << static_cast<int>(defense.buildingType) << ' ' << defense.center.x << ' '
                << defense.center.y << ' ' << defense.radius << ' ' << defense.protection << ' '
                << defense.garrisonUsed << ' ' << defense.garrisonCapacity << ' '
                << defense.upkeepPerMinute << ' ' << defense.upkeepDebtPackages << ' '
                << defense.bufferedFood << ' ' << defense.incomingFood << ' ' << defense.duePackages << ' '
                << static_cast<int>(defense.supplyStatus) << ' ' << (defense.protectionActive ? 1 : 0) << ' ';
        out << taskGroups.size() << ' ';
        for (const auto& group : taskGroups)
        {
            out << group.id << ' ' << group.stationProvinceId << ' '
                << group.homeBarracksBuildingId << ' ' << static_cast<int>(group.status) << ' '
                << group.total << ' ' << group.alive << ' ' << (group.editable ? 1 : 0) << ' '
                << group.journeyId << ' ' << group.battleId << ' ' << group.garrisonBuildingId << ' '
                << group.unitCounts.size() << ' ';
            for (const auto& [unitDefId, count] : group.unitCounts)
                out << std::quoted(unitDefId) << ' ' << count << ' ';
        }
        for (const auto& tile : tiles)
            SerializeSnapshotTile(out, tile);
        return out.str();
    }

    static bool TryDeserialize(const std::string& payload, GameSnapshot& snapshot)
    {
        std::istringstream in(payload);
        int version = 0;
        int playerCount = 0;
        GameSnapshot parsed;
        in >> version >> parsed.simulationTick >> parsed.localPlayerId >> parsed.mapSize.x >> parsed.mapSize.y >> playerCount;
        if (!in || version != SerializationVersion::GameSnapshotVersion)
            return false;
        constexpr int MaxSnapshotPlayers = 64;
        std::size_t tileCount = 0;
        if (!TryGetTileCount(parsed.mapSize, tileCount) || playerCount < 0 || playerCount > MaxSnapshotPlayers)
            return false;

        parsed.players.reserve(static_cast<size_t>(playerCount));
        for (int i = 0; i < playerCount; i++)
        {
            GameSnapshotPlayer player;
            if (!TryDeserializeSnapshotPlayer(in, player))
                return false;
            parsed.players.push_back(player);
        }
        std::size_t nodeCount = 0;
        std::size_t edgeCount = 0;
        int globalMapFogOfWar = 0;
        if (!(in >> parsed.activeProvinceId >> globalMapFogOfWar >> nodeCount >> edgeCount) ||
            (globalMapFogOfWar != 0 && globalMapFogOfWar != 1) ||
            nodeCount > PersistenceLimits::MaxGlobalProvinces ||
            edgeCount > PersistenceLimits::MaxGlobalEdges)
            return false;
        parsed.globalMapView.fogOfWarEnabled = globalMapFogOfWar != 0;
        parsed.globalMapView.nodes.reserve(nodeCount);
        std::set<ProvinceId> nodeIds;
        for (std::size_t i = 0; i < nodeCount; ++i)
        {
            GlobalMapNodeView node;
            int knowledge = 0;
            int visibleKind = -1;
            int visibleOwner = InvalidPlayerId;
            int canScout = 0;
            int canTrade = 0;
            int canAttack = 0;
            int canColonize = 0;
            std::size_t traitCount = 0;
            std::size_t resourceCount = 0;
            if (!(in >> node.id >> knowledge >> visibleKind >> visibleOwner >>
                  node.layoutPosition.x >> node.layoutPosition.y >> std::quoted(node.displayName) >>
                  traitCount) || traitCount > 32 || node.displayName.size() > PersistenceLimits::MaxStringBytes)
                return false;
            node.traitIds.reserve(traitCount);
            for (std::size_t traitIndex = 0; traitIndex < traitCount; ++traitIndex)
            {
                std::string traitId;
                if (!(in >> std::quoted(traitId)) || traitId.empty() ||
                    traitId.size() > PersistenceLimits::MaxStringBytes)
                    return false;
                node.traitIds.push_back(std::move(traitId));
            }
            if (!(in >> resourceCount) || resourceCount > 32)
                return false;
            int previousResource = -1;
            node.naturalResourceTypes.reserve(resourceCount);
            for (std::size_t resourceIndex = 0; resourceIndex < resourceCount; ++resourceIndex)
            {
                int resourceType = 0;
                if (!(in >> resourceType) || resourceType < 0 ||
                    resourceType > static_cast<int>(ResourceType::CATAPULT) ||
                    resourceType <= previousResource)
                    return false;
                previousResource = resourceType;
                node.naturalResourceTypes.push_back(static_cast<ResourceType>(resourceType));
            }
            if (!(in >> canScout >> canTrade >> canAttack >> canColonize) ||
                node.id == InvalidProvinceId || !nodeIds.insert(node.id).second ||
                knowledge < 0 || knowledge > static_cast<int>(ProvinceKnowledgeLevel::Owned) ||
                visibleKind < -1 || visibleKind > static_cast<int>(ProvinceKind::TreasureSite) ||
                visibleOwner < InvalidPlayerId || (canScout != 0 && canScout != 1) ||
                (canTrade != 0 && canTrade != 1) || (canAttack != 0 && canAttack != 1) ||
                (canColonize != 0 && canColonize != 1) ||
                (canScout != 0 && knowledge != static_cast<int>(ProvinceKnowledgeLevel::ReachableUnknown)) ||
                ((canTrade != 0 || canAttack != 0 || canColonize != 0) &&
                 knowledge < static_cast<int>(ProvinceKnowledgeLevel::Scouted)) ||
                (knowledge < static_cast<int>(ProvinceKnowledgeLevel::Scouted) &&
                 (visibleKind != -1 || visibleOwner != InvalidPlayerId ||
                  !node.displayName.empty() || !node.traitIds.empty() ||
                  !node.naturalResourceTypes.empty())))
                return false;
            node.knowledge = static_cast<ProvinceKnowledgeLevel>(knowledge);
            if (visibleKind >= 0)
                node.visibleKind = static_cast<ProvinceKind>(visibleKind);
            if (visibleOwner != InvalidPlayerId)
                node.visibleOwner = visibleOwner;
            node.canScout = canScout != 0;
            node.canTrade = canTrade != 0;
            node.canAttack = canAttack != 0;
            node.canColonize = canColonize != 0;
            parsed.globalMapView.nodes.push_back(node);
        }
        parsed.globalMapView.edges.reserve(edgeCount);
        std::set<std::pair<ProvinceId, ProvinceId>> edgeKeys;
        std::set<ProvinceConnectionId> connectionIds;
        for (std::size_t i = 0; i < edgeCount; ++i)
        {
            ProvinceEdgeView edge;
            int canInspect = 0;
            int canUpgrade = 0;
            std::size_t upgradeCostCount = 0;
            if (!(in >> edge.from >> edge.to >> edge.connectionId >> edge.level >>
                  edge.lengthUnits >> edge.routeTimeBasisPoints >> edge.incidentReductionBasisPoints >>
                  edge.nextLevel >> edge.nextRouteTimeBasisPoints >>
                  edge.nextIncidentReductionBasisPoints >> edge.nextUpgradeDurationTicks >>
                  upgradeCostCount) || upgradeCostCount > 16)
                return false;
            int previousCostType = -1;
            edge.nextUpgradeCost.reserve(upgradeCostCount);
            for (std::size_t costIndex = 0; costIndex < upgradeCostCount; ++costIndex)
            {
                int resourceType = 0;
                ProvinceConnectionResourceCost cost;
                if (!(in >> resourceType >> cost.amount) || resourceType < 0 ||
                    resourceType > static_cast<int>(ResourceType::CATAPULT) ||
                    resourceType <= previousCostType || cost.amount <= 0)
                    return false;
                previousCostType = resourceType;
                cost.type = static_cast<ResourceType>(resourceType);
                edge.nextUpgradeCost.push_back(cost);
            }
            if (!(in >> edge.upgradeRemainingTicks >>
                  canInspect >> canUpgrade) || edge.from == InvalidProvinceId ||
                edge.to == InvalidProvinceId || edge.from >= edge.to ||
                edge.connectionId == InvalidProvinceConnectionId || edge.level < 0 ||
                edge.lengthUnits < 0 || edge.routeTimeBasisPoints <= 0 ||
                edge.incidentReductionBasisPoints < 0 || edge.nextLevel < 0 ||
                edge.nextRouteTimeBasisPoints <= 0 || edge.nextIncidentReductionBasisPoints < 0 ||
                (canInspect != 0 && canInspect != 1) ||
                (canUpgrade != 0 && canUpgrade != 1) ||
                (canUpgrade != 0 && canInspect == 0) ||
                !nodeIds.contains(edge.from) || !nodeIds.contains(edge.to) ||
                !edgeKeys.insert({edge.from, edge.to}).second ||
                !connectionIds.insert(edge.connectionId).second)
                return false;
            edge.canInspect = canInspect != 0;
            edge.canUpgrade = canUpgrade != 0;
            if (!edge.canInspect && (edge.lengthUnits != 0 || edge.level != 0 ||
                                     edge.routeTimeBasisPoints != 10000 ||
                                     edge.incidentReductionBasisPoints != 0 ||
                                     edge.nextLevel != 0 || edge.nextRouteTimeBasisPoints != 10000 ||
                                     edge.nextIncidentReductionBasisPoints != 0 ||
                                     edge.nextUpgradeDurationTicks != 0 ||
                                     !edge.nextUpgradeCost.empty() ||
                                     edge.upgradeRemainingTicks != 0))
                return false;
            parsed.globalMapView.edges.push_back(edge);
        }
        std::size_t journeyCount = 0;
        if (!(in >> journeyCount) || journeyCount > 64)
            return false;
        parsed.journeyStatuses.reserve(journeyCount);
        std::set<WorldJourneyId> journeyIds;
        for (std::size_t i = 0; i < journeyCount; ++i)
        {
            JourneyStatusView journey;
            int status = 0;
            int kind = 0;
            if (!(in >> journey.journeyId >> journey.ownerId >> journey.originProvinceId >>
                  journey.targetProvinceId >> status >> journey.currentLeg >> journey.totalLegs >>
                  journey.startTick >> journey.etaTick >> journey.remainingTicks >> kind) ||
                journey.journeyId == InvalidWorldJourneyId || !journeyIds.insert(journey.journeyId).second ||
                status < 0 || status > static_cast<int>(WorldJourneyStatus::Cancelled) ||
                kind < 0 || kind > static_cast<int>(WorldJourneyKind::ArmyTransfer) ||
                journey.currentLeg > journey.totalLegs)
                return false;
            journey.status = static_cast<WorldJourneyStatus>(status);
            journey.kind = static_cast<WorldJourneyKind>(kind);
            parsed.journeyStatuses.push_back(journey);
        }
        std::size_t battleCount = 0;
        if (!(in >> battleCount) || battleCount > 64)
            return false;
        parsed.battleStatuses.reserve(battleCount);
        std::set<BattleId> battleIds;
        for (std::size_t i = 0; i < battleCount; ++i)
        {
            BattleStatusView battle;
            int status = 0;
            int attackerClass = 0;
            int defenderClass = 0;
            int raid = 0;
            if (!(in >> battle.battleId >> battle.attackerId >> battle.defenderId >>
                  battle.originProvinceId >> battle.targetProvinceId >> battle.journeyId >> status >>
                  battle.startTick >> battle.endTick >> battle.remainingTicks >> attackerClass >>
                  defenderClass >> raid) || battle.battleId == InvalidBattleId ||
                !battleIds.insert(battle.battleId).second || status < 0 ||
                status > static_cast<int>(BattleLifecycleStatus::Cancelled) ||
                attackerClass < 0 || attackerClass > static_cast<int>(WorldForceClass::Host) ||
                defenderClass < 0 || defenderClass > static_cast<int>(WorldForceClass::Host) ||
                (raid != 0 && raid != 1))
                return false;
            battle.status = static_cast<BattleLifecycleStatus>(status);
            battle.attackerForceClass = static_cast<WorldForceClass>(attackerClass);
            battle.defenderForceClass = static_cast<WorldForceClass>(defenderClass);
            battle.isRaid = raid != 0;
            parsed.battleStatuses.push_back(battle);
        }
        std::size_t reportCount = 0;
        if (!(in >> reportCount) || reportCount > 64)
            return false;
        parsed.battleReports.reserve(reportCount);
        for (std::size_t i = 0; i < reportCount; ++i)
        {
            BattleReportView report;
            int winner = 0;
            int crushing = 0;
            int transformed = 0;
            int cityDamaged = 0;
            int raid = 0;
            std::size_t destroyedCount = 0;
            std::size_t resourceCount = 0;
            if (!(in >> report.battleId >> report.attackerId >> report.defenderId >>
                  report.originProvinceId >> report.targetProvinceId >> winner >> report.attackerLosses >>
                  report.defenderLosses >> report.lootValue >> crushing >> transformed >> cityDamaged >> raid >>
                  destroyedCount) || report.battleId == InvalidBattleId || destroyedCount > 64 ||
                winner < 0 || winner > static_cast<int>(BattleWinner::Defender) ||
                (crushing != 0 && crushing != 1) || (transformed != 0 && transformed != 1) ||
                (cityDamaged != 0 && cityDamaged != 1) || (raid != 0 && raid != 1))
                return false;
            report.winner = static_cast<BattleWinner>(winner);
            report.crushingVictory = crushing != 0;
            report.banditTransformed = transformed != 0;
            report.cityDamaged = cityDamaged != 0;
            report.raid = raid != 0;
            report.destroyedBuildingIds.resize(destroyedCount);
            for (int& id : report.destroyedBuildingIds)
                if (!(in >> id) || id <= 0)
                    return false;
            if (!(in >> resourceCount) || resourceCount > 76)
                return false;
            for (std::size_t resourceIndex = 0; resourceIndex < resourceCount; ++resourceIndex)
            {
                int type = -1;
                int amount = 0;
                if (!(in >> type >> amount) || type < 0 || type > static_cast<int>(ResourceType::CATAPULT) || amount < 0 ||
                    !report.lostResources.emplace(static_cast<ResourceType>(type), amount).second)
                    return false;
            }
            parsed.battleReports.push_back(std::move(report));
        }
        std::size_t eventCount = 0;
        if (!(in >> eventCount) || eventCount > 64)
            return false;
        parsed.eventNotifications.reserve(eventCount);
        std::set<WorldEventInstanceId> eventIds;
        for (std::size_t i = 0; i < eventCount; ++i)
        {
            WorldEventNotificationView event;
            int trigger = 0;
            int expired = 0;
            std::size_t appliedEffectCount = 0;
            if (!(in >> event.instanceId >> event.ownerId >> event.provinceId >>
                  event.secondaryProvinceId >> std::quoted(event.definitionId) >>
                  std::quoted(event.title) >> std::quoted(event.description) >> trigger >>
                  event.startTick >> event.endTick >> event.outcomeRoll >> expired >> appliedEffectCount) ||
                event.instanceId == InvalidWorldEventInstanceId || !eventIds.insert(event.instanceId).second ||
                trigger < 0 || trigger > static_cast<int>(WorldEventTriggerDomain::Raid) ||
                (expired != 0 && expired != 1) || event.definitionId.size() > PersistenceLimits::MaxStringBytes ||
                event.title.size() > PersistenceLimits::MaxStringBytes ||
                event.description.size() > PersistenceLimits::MaxStringBytes ||
                appliedEffectCount > 32)
                return false;
            event.trigger = static_cast<WorldEventTriggerDomain>(trigger);
            event.expired = expired != 0;
            event.appliedEffects.reserve(appliedEffectCount);
            for (std::size_t effectIndex = 0; effectIndex < appliedEffectCount; ++effectIndex)
            {
                AppliedWorldEventEffect effect;
                int kind = 0;
                int resourceType = 0;
                int stat = 0;
                if (!(in >> kind >> resourceType >> effect.amount >> stat >>
                      effect.additive >> effect.multiplier >> effect.durationTicks) ||
                    kind < static_cast<int>(AppliedWorldEventEffectKind::ResourceDelta) ||
                    kind > static_cast<int>(AppliedWorldEventEffectKind::RaidStarted) ||
                    resourceType < 0 ||
                    (resourceType != static_cast<int>(ResourceType::Null) &&
                     resourceType > static_cast<int>(ResourceType::CATAPULT)) ||
                    stat < static_cast<int>(BalanceStat::BuildTime) ||
                    stat > static_cast<int>(BalanceStat::ColonizationDuration) ||
                    !std::isfinite(effect.additive) || !std::isfinite(effect.multiplier) ||
                    effect.multiplier <= 0.0)
                    return false;
                effect.kind = static_cast<AppliedWorldEventEffectKind>(kind);
                effect.resourceType = static_cast<ResourceType>(resourceType);
                effect.stat = static_cast<BalanceStat>(stat);
                event.appliedEffects.push_back(effect);
            }
            parsed.eventNotifications.push_back(std::move(event));
        }
        std::size_t defenseCount = 0;
        if (!(in >> defenseCount) || defenseCount > 64)
            return false;
        parsed.provinceDefenses.reserve(defenseCount);
        std::set<int> defenseIds;
        for (std::size_t i = 0; i < defenseCount; ++i)
        {
            ProvinceDefenseView defense;
            int buildingType = 0;
            int supplyStatus = 0;
            int protectionActive = 0;
            if (!(in >> defense.provinceId >> defense.buildingId >> buildingType >> defense.center.x >>
                  defense.center.y >> defense.radius >> defense.protection >> defense.garrisonUsed >>
                  defense.garrisonCapacity >> defense.upkeepPerMinute >> defense.upkeepDebtPackages >>
                  defense.bufferedFood >> defense.incomingFood >> defense.duePackages >> supplyStatus >>
                  protectionActive) || defense.provinceId == InvalidProvinceId || defense.buildingId <= 0 ||
                !defenseIds.insert(defense.buildingId).second ||
                buildingType < 0 || buildingType > static_cast<int>(BuildingType::Fortress) ||
                supplyStatus < 0 || supplyStatus > static_cast<int>(GarrisonSupplyStatus::RequestPending) ||
                (protectionActive != 0 && protectionActive != 1))
                return false;
            defense.buildingType = static_cast<BuildingType>(buildingType);
            defense.supplyStatus = static_cast<GarrisonSupplyStatus>(supplyStatus);
            defense.protectionActive = protectionActive != 0;
            parsed.provinceDefenses.push_back(defense);
        }
        std::size_t taskGroupCount = 0;
        if (!(in >> taskGroupCount) || taskGroupCount > 64)
            return false;
        parsed.taskGroups.reserve(taskGroupCount);
        std::set<TaskGroupId> taskGroupIds;
        for (std::size_t i = 0; i < taskGroupCount; ++i)
        {
            TaskGroupView group;
            int status = 0;
            int editable = 0;
            std::size_t unitCount = 0;
            if (!(in >> group.id >> group.stationProvinceId >> group.homeBarracksBuildingId >> status >>
                  group.total >> group.alive >> editable >> group.journeyId >> group.battleId >>
                  group.garrisonBuildingId >> unitCount) ||
                group.id == InvalidTaskGroupId || !taskGroupIds.insert(group.id).second ||
                group.stationProvinceId == InvalidProvinceId || group.homeBarracksBuildingId <= 0 ||
                status < static_cast<int>(TaskGroupStatus::Empty) ||
                status > static_cast<int>(TaskGroupStatus::Mixed) || group.total < 0 ||
                group.alive < 0 || group.alive > group.total ||
                (editable != 0 && editable != 1) || unitCount > 32)
                return false;
            group.status = static_cast<TaskGroupStatus>(status);
            group.editable = editable != 0;
            for (std::size_t unitIndex = 0; unitIndex < unitCount; ++unitIndex)
            {
                std::string unitDefId;
                int count = 0;
                if (!(in >> std::quoted(unitDefId) >> count) || unitDefId.empty() ||
                    unitDefId.size() > PersistenceLimits::MaxStringBytes || count <= 0 ||
                    !group.unitCounts.emplace(std::move(unitDefId), count).second)
                    return false;
            }
            parsed.taskGroups.push_back(std::move(group));
        }
        if (parsed.activeProvinceId != InvalidProvinceId &&
            !nodeIds.contains(parsed.activeProvinceId))
            return false;
        parsed.tiles.reserve(tileCount);
        for (std::size_t i = 0; i < tileCount; i++)
        {
            GameSnapshotTile tile;
            if (!TryDeserializeSnapshotTile(in, tile))
                return false;
            parsed.tiles.push_back(tile);
        }

        in >> std::ws;
        if (!in.eof())
            return false;

        snapshot = std::move(parsed);
        return true;
    }

    static bool TryGetTileCount(Vec2i size, std::size_t& tileCount)
    {
        if (size.x <= 0 || size.y <= 0 || size.x > MaxMapDimension || size.y > MaxMapDimension)
            return false;

        const std::size_t width = static_cast<std::size_t>(size.x);
        const std::size_t height = static_cast<std::size_t>(size.y);
        if (width > MaxTileCount / height)
            return false;

        tileCount = width * height;
        return true;
    }
};

// A missing owner is intentionally rendered neutrally. This permits destroyed,
// neutral, and legacy buildings to keep their normal albedo while callers
// safely use the same lookup in single-player and snapshot rendering paths.
inline Color ResolveSnapshotPlayerColor(const GameSnapshot& snapshot, int playerId, Color fallback = WHITE)
{
    auto player = std::find_if(snapshot.players.begin(), snapshot.players.end(),
                               [playerId](const GameSnapshotPlayer& candidate) {
                                   return candidate.id == playerId;
                               });
    return player != snapshot.players.end() ? player->color : fallback;
}

struct GameSnapshotDeltaTile
{
    size_t index{0};
    GameSnapshotTile tile;
};

struct GameSnapshotDelta
{
    std::uint64_t simulationTick{0};
    Vec2i mapSize{0, 0};
    std::vector<GameSnapshotDeltaTile> changes;

    bool IsValidFor(const GameSnapshot& snapshot) const
    {
        return snapshot.IsValid() && mapSize.x == snapshot.mapSize.x && mapSize.y == snapshot.mapSize.y;
    }

    std::string Serialize() const
    {
        std::ostringstream out;
        out << simulationTick << ' ' << mapSize.x << ' ' << mapSize.y << ' ' << changes.size() << ' ';
        for (const auto& change : changes)
        {
            out << change.index << ' ';
            SerializeSnapshotTile(out, change.tile);
        }
        return out.str();
    }

    static bool TryDeserialize(const std::string& payload, GameSnapshotDelta& delta)
    {
        std::istringstream in(payload);
        GameSnapshotDelta parsed;
        size_t changeCount = 0;
        if (!(in >> parsed.simulationTick >> parsed.mapSize.x >> parsed.mapSize.y >> changeCount))
            return false;
        std::size_t tileCount = 0;
        if (!GameSnapshot::TryGetTileCount(parsed.mapSize, tileCount) || changeCount > GameSnapshot::MaxTileCount)
            return false;

        parsed.changes.reserve(changeCount);
        for (size_t i = 0; i < changeCount; i++)
        {
            GameSnapshotDeltaTile change;
            if (!(in >> change.index))
                return false;
            if (change.index >= tileCount)
                return false;
            if (!TryDeserializeSnapshotTile(in, change.tile))
                return false;
            parsed.changes.push_back(change);
        }

        delta = std::move(parsed);
        return true;
    }

    bool ApplyTo(GameSnapshot& snapshot) const
    {
        if (!IsValidFor(snapshot))
            return false;
        for (const auto& change : changes)
        {
            if (change.index >= snapshot.tiles.size())
                return false;
            snapshot.tiles[change.index] = change.tile;
        }
        snapshot.simulationTick = simulationTick;
        return true;
    }
};

#endif
