#ifndef GAME_COMMAND_H
#define GAME_COMMAND_H

#include "core/Serialization.h"
#include "economy/Building.h"
#include "world/Expedition.h"
#include "world/Trade.h"
#include "warfare/TaskGroupIds.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

enum class GameCommandType
{
    // Keep historical wire slots stable. Removed military commands leave
    // holes; their numeric values must never be reused by economy commands.
    BuildBuilding = 0,
    DestroyBuilding = 1,
    SetReceiver = 2,
    StartFocus = 3,
    StartTechnologyResearch = 4,
    RecruitUnit = 5,
    UpgradeBuilding = 7,
    SetRecipe = 8,
    SetProductionBlocked = 10,
    SetRoadPriority = 12,
    ScoutProvince = 13,
    StartScoutExpedition = 14,
    UpgradeProvinceConnection = 15,
    ColonizeProvince = 16,
    StartProvinceAttack = 17,
    AssignUnitsToGarrison = 18,
    ReturnUnitsToBarracks = 19,
    StartTrade = 20,
    CreateTaskGroup = 21,
    AddUnitsToTaskGroup = 22,
    RemoveUnitsFromTaskGroup = 23,
    DisbandTaskGroup = 24,
    StartResourceTransfer = 25,
    StartArmyTransfer = 26,
    SpawnDebugRaid = 27
};

struct GameCommand
{
    static constexpr std::size_t MaxSerializedBytes = 64 * 1024;
    static constexpr std::size_t MaxResearchIdBytes = 256;
    static constexpr std::size_t MaxUnitInstanceIds = 32;
    static constexpr std::size_t MaxTaskGroupIds = 32;
    static constexpr std::size_t MaxResourceCargoTypes = 16;
    // Local commands carry provinceId explicitly. Global commands use their
    // own province and expedition fields; no ID is packed into a tile field.
    static constexpr int WireVersion = SerializationVersion::GameCommandVersion;

    static GameCommand BuildBuilding(PlayerId playerId, ProvinceId provinceId,
                                     BuildingType buildingType, Vec2i tilePos)
    {
        GameCommand command;
        command.playerId = playerId;
        command.provinceId = provinceId;
        command.type = GameCommandType::BuildBuilding;
        command.buildingType = buildingType;
        command.tilePos = tilePos;
        return command;
    }

    static GameCommand BuildBuilding(int playerId, BuildingType buildingType, Vec2i tilePos)
    {
        return BuildBuilding(playerId, InvalidProvinceId, buildingType, tilePos);
    }

    static GameCommand DestroyBuilding(PlayerId playerId, ProvinceId provinceId, int tileId)
    {
        GameCommand command;
        command.playerId = playerId;
        command.provinceId = provinceId;
        command.type = GameCommandType::DestroyBuilding;
        command.sourceTileId = tileId;
        return command;
    }

    static GameCommand DestroyBuilding(int playerId, int tileId)
    {
        return DestroyBuilding(playerId, InvalidProvinceId, tileId);
    }

    static GameCommand UpgradeBuilding(PlayerId playerId, ProvinceId provinceId, int tileId)
    {
        GameCommand command;
        command.playerId = playerId;
        command.provinceId = provinceId;
        command.type = GameCommandType::UpgradeBuilding;
        command.sourceTileId = tileId;
        return command;
    }

    static GameCommand UpgradeBuilding(int playerId, int tileId)
    {
        return UpgradeBuilding(playerId, InvalidProvinceId, tileId);
    }

    static GameCommand SetRecipe(PlayerId playerId, ProvinceId provinceId,
                                 int buildingTileId, int recipeIndex)
    {
        GameCommand command;
        command.playerId = playerId;
        command.provinceId = provinceId;
        command.type = GameCommandType::SetRecipe;
        command.sourceTileId = buildingTileId;
        command.targetTileId = recipeIndex;
        return command;
    }

    static GameCommand SetRecipe(int playerId, int buildingTileId, int recipeIndex)
    {
        return SetRecipe(playerId, InvalidProvinceId, buildingTileId, recipeIndex);
    }

    static GameCommand SetProductionBlocked(PlayerId playerId, ProvinceId provinceId,
                                             int buildingTileId, bool blocked)
    {
        GameCommand command;
        command.playerId = playerId;
        command.provinceId = provinceId;
        command.type = GameCommandType::SetProductionBlocked;
        command.sourceTileId = buildingTileId;
        command.targetTileId = blocked ? 1 : 0;
        return command;
    }

    static GameCommand SetProductionBlocked(int playerId, int buildingTileId, bool blocked)
    {
        return SetProductionBlocked(playerId, InvalidProvinceId, buildingTileId, blocked);
    }

    static GameCommand SetRoadPriority(PlayerId playerId, ProvinceId provinceId,
                                        int roadTileId, ResourceType resource)
    {
        GameCommand command;
        command.playerId = playerId;
        command.provinceId = provinceId;
        command.type = GameCommandType::SetRoadPriority;
        command.sourceTileId = roadTileId;
        command.targetTileId = static_cast<int>(resource);
        return command;
    }

    static GameCommand SetRoadPriority(int playerId, int roadTileId, ResourceType resource)
    {
        return SetRoadPriority(playerId, InvalidProvinceId, roadTileId, resource);
    }

    static GameCommand SetReceiver(PlayerId playerId, ProvinceId provinceId,
                                   int sourceTileId, int targetTileId,
                                   bool alternativeReceiver = false)
    {
        GameCommand command;
        command.playerId = playerId;
        command.provinceId = provinceId;
        command.type = GameCommandType::SetReceiver;
        command.sourceTileId = sourceTileId;
        command.targetTileId = targetTileId;
        command.alternativeReceiver = alternativeReceiver;
        return command;
    }

    static GameCommand SetReceiver(int playerId, int sourceTileId, int targetTileId, bool alternativeReceiver = false)
    {
        return SetReceiver(playerId, InvalidProvinceId, sourceTileId, targetTileId, alternativeReceiver);
    }

    static GameCommand StartFocus(int playerId, std::string focusId)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::StartFocus;
        command.researchId = std::move(focusId);
        return command;
    }

    static GameCommand StartTechnologyResearch(PlayerId playerId, ProvinceId provinceId,
                                               std::string technologyId, int universityTileId)
    {
        GameCommand command;
        command.playerId = playerId;
        command.provinceId = provinceId;
        command.type = GameCommandType::StartTechnologyResearch;
        command.researchId = std::move(technologyId);
        command.sourceTileId = universityTileId;
        return command;
    }

    static GameCommand StartTechnologyResearch(int playerId, std::string technologyId, int universityTileId)
    {
        return StartTechnologyResearch(playerId, InvalidProvinceId,
                                       std::move(technologyId), universityTileId);
    }

    static GameCommand RecruitUnit(PlayerId playerId, ProvinceId provinceId,
                                   int buildingTileId, std::string unitDefId)
    {
        GameCommand command;
        command.playerId = playerId;
        command.provinceId = provinceId;
        command.type = GameCommandType::RecruitUnit;
        command.sourceTileId = buildingTileId;
        command.researchId = std::move(unitDefId);
        return command;
    }

    static GameCommand RecruitUnit(int playerId, int buildingTileId, std::string unitDefId)
    {
        return RecruitUnit(playerId, InvalidProvinceId, buildingTileId, std::move(unitDefId));
    }

    static GameCommand ScoutProvince(PlayerId playerId, ProvinceId sourceProvinceId,
                                     ProvinceId targetProvinceId)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::ScoutProvince;
        command.provinceId = sourceProvinceId;
        command.targetProvinceId = targetProvinceId;
        return command;
    }

    // Compatibility shortcut for old callers and serialized command fixtures.
    // New UI code should always carry the authoritative source province.
    static GameCommand ScoutProvince(PlayerId playerId, ProvinceId targetProvinceId)
    {
        return ScoutProvince(playerId, InvalidProvinceId, targetProvinceId);
    }

    static GameCommand StartScoutExpedition(PlayerId playerId, ProvinceId sourceProvinceId,
                                            ProvinceId targetProvinceId,
                                            std::vector<int> unitInstanceIds)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::StartScoutExpedition;
        command.provinceId = sourceProvinceId;
        command.targetProvinceId = targetProvinceId;
        command.expeditionRole = ExpeditionRole::Scout;
        command.unitInstanceIds = std::move(unitInstanceIds);
        return command;
    }

    static GameCommand UpgradeProvinceConnection(
        PlayerId playerId, ProvinceId sourceProvinceId,
        ProvinceConnectionId provinceConnectionId)
    {
        GameCommand command;
        command.playerId = playerId;
        command.provinceId = sourceProvinceId;
        command.type = GameCommandType::UpgradeProvinceConnection;
        command.connectionId = provinceConnectionId;
        return command;
    }

    static GameCommand ColonizeProvince(PlayerId playerId, ProvinceId sourceProvinceId,
                                        ProvinceId targetProvinceId)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::ColonizeProvince;
        command.provinceId = sourceProvinceId;
        command.targetProvinceId = targetProvinceId;
        return command;
    }

    static GameCommand StartProvinceAttack(PlayerId playerId, ProvinceId sourceProvinceId,
                                           ProvinceId targetProvinceId,
                                           std::vector<int> unitInstanceIds)
    {
        GameCommand command;
        command.playerId = playerId;
        command.provinceId = sourceProvinceId;
        command.targetProvinceId = targetProvinceId;
        command.type = GameCommandType::StartProvinceAttack;
        command.expeditionRole = ExpeditionRole::Garrison;
        command.unitInstanceIds = std::move(unitInstanceIds);
        return command;
    }

    static GameCommand StartProvinceAttackWithTaskGroups(
        PlayerId playerId, ProvinceId sourceProvinceId, ProvinceId targetProvinceId,
        std::vector<TaskGroupId> taskGroupIds)
    {
        GameCommand command;
        command.playerId = playerId;
        command.provinceId = sourceProvinceId;
        command.targetProvinceId = targetProvinceId;
        command.type = GameCommandType::StartProvinceAttack;
        command.expeditionRole = ExpeditionRole::Garrison;
        command.taskGroupIds = std::move(taskGroupIds);
        return command;
    }

    static GameCommand AssignUnitsToGarrison(PlayerId playerId, ProvinceId provinceId,
                                              int sourceBarracksId, int defenseBuildingId,
                                              std::vector<int> unitInstanceIds)
    {
        GameCommand command;
        command.playerId = playerId;
        command.provinceId = provinceId;
        command.type = GameCommandType::AssignUnitsToGarrison;
        command.sourceTileId = sourceBarracksId;
        command.targetTileId = defenseBuildingId;
        command.unitInstanceIds = std::move(unitInstanceIds);
        return command;
    }

    static GameCommand ReturnUnitsToBarracks(PlayerId playerId, ProvinceId provinceId,
                                              int defenseBuildingId, int targetBarracksId,
                                              std::vector<int> unitInstanceIds)
    {
        GameCommand command;
        command.playerId = playerId;
        command.provinceId = provinceId;
        command.type = GameCommandType::ReturnUnitsToBarracks;
        command.sourceTileId = defenseBuildingId;
        command.targetTileId = targetBarracksId;
        command.unitInstanceIds = std::move(unitInstanceIds);
        return command;
    }

    static GameCommand AssignTaskGroupToGarrison(PlayerId playerId, ProvinceId provinceId,
                                                  TaskGroupId taskGroupId,
                                                  int defenseBuildingId)
    {
        GameCommand command = AssignUnitsToGarrison(
            playerId, provinceId, 0, defenseBuildingId, {});
        command.taskGroupId = taskGroupId;
        return command;
    }

    static GameCommand ReturnTaskGroupToBarracks(PlayerId playerId, ProvinceId provinceId,
                                                  TaskGroupId taskGroupId,
                                                  int defenseBuildingId,
                                                  int targetBarracksId)
    {
        GameCommand command = ReturnUnitsToBarracks(
            playerId, provinceId, defenseBuildingId, targetBarracksId, {});
        command.taskGroupId = taskGroupId;
        return command;
    }

    static GameCommand StartResourceTransfer(PlayerId playerId, ProvinceId sourceProvinceId,
                                              ProvinceId targetProvinceId,
                                              std::vector<ResourceAmount> cargo)
    {
        GameCommand command;
        command.playerId = playerId;
        command.provinceId = sourceProvinceId;
        command.targetProvinceId = targetProvinceId;
        command.type = GameCommandType::StartResourceTransfer;
        command.resourceCargo = std::move(cargo);
        return command;
    }

    static GameCommand StartArmyTransfer(PlayerId playerId, ProvinceId sourceProvinceId,
                                         ProvinceId targetProvinceId,
                                         std::vector<TaskGroupId> taskGroupIds,
                                         int destinationBarracksId)
    {
        GameCommand command;
        command.playerId = playerId;
        command.provinceId = sourceProvinceId;
        command.targetProvinceId = targetProvinceId;
        command.type = GameCommandType::StartArmyTransfer;
        command.taskGroupIds = std::move(taskGroupIds);
        command.destinationBarracksId = destinationBarracksId;
        return command;
    }

    static GameCommand SpawnDebugRaid(PlayerId playerId, ProvinceId targetProvinceId,
                                      int strength = 20)
    {
        GameCommand command;
        command.playerId = playerId;
        command.targetProvinceId = targetProvinceId;
        command.type = GameCommandType::SpawnDebugRaid;
        command.raidStrength = strength;
        return command;
    }

    static GameCommand StartTrade(PlayerId playerId, ProvinceId originProvinceId,
                                  ProvinceId cityProvinceId, const TradeRequest& request,
                                  std::uint64_t expectedCityRevision)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::StartTrade;
        command.provinceId = originProvinceId;
        command.targetProvinceId = cityProvinceId;
        command.tradeOfferType = request.offerType;
        command.tradeRequestType = request.requestType;
        command.tradeRequestedAmount = request.requestedAmount;
        command.tradeOfferedAmount = request.offeredAmount;
        command.tradeMode = request.mode;
        command.expectedCityRevision = expectedCityRevision;
        return command;
    }

    static GameCommand CreateTaskGroup(PlayerId playerId, ProvinceId provinceId,
                                       int barracksBuildingId)
    {
        GameCommand command;
        command.playerId = playerId;
        command.provinceId = provinceId;
        command.sourceTileId = barracksBuildingId;
        command.type = GameCommandType::CreateTaskGroup;
        return command;
    }

    static GameCommand AddUnitsToTaskGroup(PlayerId playerId, ProvinceId provinceId,
                                           TaskGroupId taskGroupId,
                                           std::vector<int> unitIds)
    {
        GameCommand command;
        command.playerId = playerId;
        command.provinceId = provinceId;
        command.taskGroupId = taskGroupId;
        command.unitInstanceIds = std::move(unitIds);
        command.type = GameCommandType::AddUnitsToTaskGroup;
        return command;
    }

    static GameCommand RemoveUnitsFromTaskGroup(PlayerId playerId, ProvinceId provinceId,
                                                TaskGroupId taskGroupId,
                                                std::vector<int> unitIds)
    {
        GameCommand command = AddUnitsToTaskGroup(playerId, provinceId, taskGroupId,
                                                  std::move(unitIds));
        command.type = GameCommandType::RemoveUnitsFromTaskGroup;
        return command;
    }

    static GameCommand DisbandTaskGroup(PlayerId playerId, ProvinceId provinceId,
                                        TaskGroupId taskGroupId)
    {
        GameCommand command;
        command.playerId = playerId;
        command.provinceId = provinceId;
        command.taskGroupId = taskGroupId;
        command.type = GameCommandType::DisbandTaskGroup;
        return command;
    }

    std::string Serialize() const
    {
        Archive ar(WireVersion);
        ar << commandId
           << targetTick
           << static_cast<int>(type)
           << playerId
           << static_cast<int>(buildingType)
           << tilePos.x
           << tilePos.y
           << sourceTileId
           << targetTileId
           << (alternativeReceiver ? 1 : 0)
           << researchId
           << static_cast<std::uint64_t>(provinceId)
           << static_cast<std::uint64_t>(targetProvinceId)
           << static_cast<std::uint64_t>(connectionId)
           << static_cast<int>(expeditionRole)
           << static_cast<int>(tradeOfferType)
           << static_cast<int>(tradeRequestType)
           << tradeRequestedAmount
           << tradeOfferedAmount
           << static_cast<int>(tradeMode)
           << expectedCityRevision
           << taskGroupId
           << static_cast<int>(taskGroupIds.size());
        for (const TaskGroupId id : taskGroupIds)
            ar << id;
        ar << static_cast<int>(resourceCargo.size());
        for (const auto& cargo : resourceCargo)
            ar << static_cast<int>(cargo.type) << cargo.amount;
        ar << destinationBarracksId;
        ar << raidStrength;
        ar << static_cast<int>(unitInstanceIds.size());
        for (int unitInstanceId : unitInstanceIds)
            ar << unitInstanceId;
        return ar.GetString();
    }

    static bool TryDeserialize(const std::string& payload, GameCommand& command)
    {
        if (payload.size() > MaxSerializedBytes)
            return false;
        Archive ar(payload, WireVersion);
        if (!ar.IsValid())
            return false;

        std::uint64_t commandId = 0;
        std::uint64_t targetTick = 0;
        int type = 0;
        int playerId = 0;
        int buildingType = 0;
        int tileX = 0, tileY = 0;
        int sourceTileId = 0;
        int targetTileId = 0;
        int alternativeReceiver = 0;
        std::string researchId;
        std::uint64_t provinceId = InvalidProvinceId;
        std::uint64_t targetProvinceId = InvalidProvinceId;
        std::uint64_t connectionId = InvalidProvinceConnectionId;
        int expeditionRole = 0;
        int tradeOfferType = static_cast<int>(ResourceType::Null);
        int tradeRequestType = static_cast<int>(ResourceType::Null);
        int tradeRequestedAmount = 0;
        int tradeOfferedAmount = 0;
        int tradeMode = static_cast<int>(TradeMode::Coin);
        std::uint64_t expectedCityRevision = 0;
        std::uint64_t taskGroupId = InvalidTaskGroupId;
        int taskGroupCount = 0;
        int resourceCargoCount = 0;
        int destinationBarracksId = 0;
        int raidStrength = 0;
        int unitCount = 0;
        ar >> commandId
           >> targetTick
           >> type
           >> playerId
           >> buildingType
           >> tileX
           >> tileY
           >> sourceTileId
           >> targetTileId
           >> alternativeReceiver
           >> researchId
           >> provinceId
           >> targetProvinceId
           >> connectionId
           >> expeditionRole
           >> tradeOfferType
           >> tradeRequestType
           >> tradeRequestedAmount
           >> tradeOfferedAmount
           >> tradeMode
           >> expectedCityRevision
           >> taskGroupId
           >> taskGroupCount;

        if (!ar.IsValid() || !IsValidType(type) || researchId.size() > MaxResearchIdBytes ||
            provinceId > std::numeric_limits<ProvinceId>::max() ||
            targetProvinceId > std::numeric_limits<ProvinceId>::max() ||
            connectionId > std::numeric_limits<ProvinceConnectionId>::max() ||
            taskGroupId == InvalidTaskGroupId &&
                (static_cast<GameCommandType>(type) == GameCommandType::AddUnitsToTaskGroup ||
                 static_cast<GameCommandType>(type) == GameCommandType::RemoveUnitsFromTaskGroup ||
                 static_cast<GameCommandType>(type) == GameCommandType::DisbandTaskGroup) ||
            taskGroupCount < 0 || taskGroupCount > static_cast<int>(MaxTaskGroupIds) ||
            (static_cast<GameCommandType>(type) != GameCommandType::StartProvinceAttack &&
             static_cast<GameCommandType>(type) != GameCommandType::StartArmyTransfer &&
             taskGroupCount != 0) ||
            expeditionRole < 0 || expeditionRole > static_cast<int>(ExpeditionRole::Garrison) ||
            (tradeOfferType != static_cast<int>(ResourceType::Null) &&
             (tradeOfferType < 0 || tradeOfferType > static_cast<int>(ResourceType::CATAPULT))) ||
            (tradeRequestType != static_cast<int>(ResourceType::Null) &&
             (tradeRequestType < 0 || tradeRequestType > static_cast<int>(ResourceType::CATAPULT))) ||
            tradeRequestedAmount < 0 || tradeOfferedAmount < 0 ||
            tradeMode < static_cast<int>(TradeMode::Coin) ||
            tradeMode > static_cast<int>(TradeMode::Barter))
            return false;

        GameCommand parsed;
        parsed.commandId = commandId;
        parsed.targetTick = targetTick;
        parsed.type = static_cast<GameCommandType>(type);
        parsed.playerId = playerId;
        parsed.buildingType = static_cast<BuildingType>(buildingType);
        parsed.tilePos = {tileX, tileY};
        parsed.sourceTileId = sourceTileId;
        parsed.targetTileId = targetTileId;
        parsed.alternativeReceiver = alternativeReceiver != 0;
        parsed.researchId = std::move(researchId);
        parsed.provinceId = static_cast<ProvinceId>(provinceId);
        parsed.targetProvinceId = static_cast<ProvinceId>(targetProvinceId);
        parsed.connectionId = static_cast<ProvinceConnectionId>(connectionId);
        parsed.expeditionRole = static_cast<ExpeditionRole>(expeditionRole);
        parsed.tradeOfferType = static_cast<ResourceType>(tradeOfferType);
        parsed.tradeRequestType = static_cast<ResourceType>(tradeRequestType);
        parsed.tradeRequestedAmount = tradeRequestedAmount;
        parsed.tradeOfferedAmount = tradeOfferedAmount;
        parsed.tradeMode = static_cast<TradeMode>(tradeMode);
        parsed.expectedCityRevision = expectedCityRevision;
        parsed.taskGroupId = taskGroupId;
        parsed.taskGroupIds.resize(static_cast<std::size_t>(taskGroupCount));
        for (TaskGroupId& id : parsed.taskGroupIds)
            ar >> id;
        ar >> resourceCargoCount;
        if (!ar.IsValid() || resourceCargoCount < 0 ||
            resourceCargoCount > static_cast<int>(MaxResourceCargoTypes) ||
            (static_cast<GameCommandType>(type) != GameCommandType::StartResourceTransfer &&
             resourceCargoCount != 0))
            return false;
        parsed.resourceCargo.resize(static_cast<std::size_t>(resourceCargoCount));
        for (auto& cargo : parsed.resourceCargo)
        {
            int resourceType = 0;
            ar >> resourceType >> cargo.amount;
            if (!ar.IsValid() || resourceType < 0 ||
                resourceType > static_cast<int>(ResourceType::CATAPULT) ||
                cargo.amount <= 0)
                return false;
            cargo.type = static_cast<ResourceType>(resourceType);
        }
        ar >> destinationBarracksId;
        if (!ar.IsValid() || destinationBarracksId < 0 ||
            static_cast<GameCommandType>(type) != GameCommandType::StartArmyTransfer &&
                destinationBarracksId != 0)
            return false;
        parsed.destinationBarracksId = destinationBarracksId;
        ar >> raidStrength;
        if (!ar.IsValid() ||
            static_cast<GameCommandType>(type) != GameCommandType::SpawnDebugRaid &&
                raidStrength != 0 ||
            static_cast<GameCommandType>(type) == GameCommandType::SpawnDebugRaid &&
                raidStrength <= 0)
            return false;
        parsed.raidStrength = raidStrength;
        ar >> unitCount;
        if (!ar.IsValid() || unitCount < 0 || unitCount > static_cast<int>(MaxUnitInstanceIds) ||
            !parsed.taskGroupIds.empty() && unitCount != 0)
            return false;
        for (TaskGroupId id : parsed.taskGroupIds)
            if (id == InvalidTaskGroupId)
                return false;
        if (!std::is_sorted(parsed.taskGroupIds.begin(), parsed.taskGroupIds.end()) ||
            std::adjacent_find(parsed.taskGroupIds.begin(), parsed.taskGroupIds.end()) !=
                parsed.taskGroupIds.end())
            return false;
        for (std::size_t i = 1; i < parsed.resourceCargo.size(); ++i)
            if (static_cast<int>(parsed.resourceCargo[i - 1].type) >=
                static_cast<int>(parsed.resourceCargo[i].type))
                return false;
        parsed.unitInstanceIds.resize(static_cast<std::size_t>(unitCount));
        for (int& unitInstanceId : parsed.unitInstanceIds)
            ar >> unitInstanceId;
        if (!ar.IsValid())
            return false;
        std::vector<int> sortedUnitIds = parsed.unitInstanceIds;
        std::sort(sortedUnitIds.begin(), sortedUnitIds.end());
        if (std::adjacent_find(sortedUnitIds.begin(), sortedUnitIds.end()) != sortedUnitIds.end() ||
            std::any_of(sortedUnitIds.begin(), sortedUnitIds.end(), [](int id) { return id <= 0; }))
            return false;
        if (!ar.AtEnd())
            return false;
        command = std::move(parsed);
        return true;
    }

    std::uint64_t commandId{0};
    std::uint64_t targetTick{0};
    int playerId{0};
    GameCommandType type{GameCommandType::BuildBuilding};
    BuildingType buildingType{BuildingType::Building};
    Vec2i tilePos{0, 0};
    int sourceTileId{-1};
    int targetTileId{-1};
    bool alternativeReceiver{false};
    std::string researchId;
    ProvinceId provinceId{InvalidProvinceId};
    ProvinceId targetProvinceId{InvalidProvinceId};
    ProvinceConnectionId connectionId{InvalidProvinceConnectionId};
    ExpeditionRole expeditionRole{ExpeditionRole::Scout};
    ResourceType tradeOfferType{ResourceType::Null};
    ResourceType tradeRequestType{ResourceType::Null};
    int tradeRequestedAmount{0};
    int tradeOfferedAmount{0};
    TradeMode tradeMode{TradeMode::Coin};
    std::uint64_t expectedCityRevision{0};
    TaskGroupId taskGroupId{InvalidTaskGroupId};
    std::vector<TaskGroupId> taskGroupIds;
    std::vector<ResourceAmount> resourceCargo;
    int destinationBarracksId{0};
    int raidStrength{0};
    std::vector<int> unitInstanceIds;

    static bool IsValidType(int type)
    {
        switch (static_cast<GameCommandType>(type))
        {
            case GameCommandType::BuildBuilding:
            case GameCommandType::DestroyBuilding:
            case GameCommandType::SetReceiver:
            case GameCommandType::StartFocus:
            case GameCommandType::StartTechnologyResearch:
            case GameCommandType::RecruitUnit:
            case GameCommandType::UpgradeBuilding:
            case GameCommandType::SetRecipe:
            case GameCommandType::SetProductionBlocked:
            case GameCommandType::SetRoadPriority:
            case GameCommandType::ScoutProvince:
            case GameCommandType::StartScoutExpedition:
            case GameCommandType::UpgradeProvinceConnection:
            case GameCommandType::ColonizeProvince:
            case GameCommandType::StartProvinceAttack:
            case GameCommandType::AssignUnitsToGarrison:
            case GameCommandType::ReturnUnitsToBarracks:
            case GameCommandType::StartTrade:
            case GameCommandType::CreateTaskGroup:
            case GameCommandType::AddUnitsToTaskGroup:
            case GameCommandType::RemoveUnitsFromTaskGroup:
            case GameCommandType::DisbandTaskGroup:
            case GameCommandType::StartResourceTransfer:
            case GameCommandType::StartArmyTransfer:
            case GameCommandType::SpawnDebugRaid:
                return true;
        }
        return false;
    }
};

struct GameCommandResult
{
    static constexpr std::size_t MaxSerializedBytes = 256 * 1024;
    static constexpr std::size_t MaxReasonBytes = 1024;
    static constexpr int WireVersion = SerializationVersion::GameCommandResultVersion;

    std::uint64_t commandId{0};
    std::uint64_t simulationTick{0};
    std::uint64_t targetTick{0};
    int playerId{0};
    GameCommandType type{GameCommandType::BuildBuilding};
    bool accepted{false};
    std::string reason;
    std::string commandPayload;

    std::string Serialize() const
    {
        Archive ar(WireVersion);
        ar << commandId
           << simulationTick
           << targetTick
           << playerId
           << static_cast<int>(type)
           << (accepted ? 1 : 0)
           << reason
           << commandPayload;
        return ar.GetString();
    }

    static bool TryDeserialize(const std::string& payload, GameCommandResult& result)
    {
        if (payload.size() > MaxSerializedBytes)
            return false;
        Archive ar(payload, WireVersion);
        if (!ar.IsValid())
            return false;

        std::uint64_t commandId = 0;
        std::uint64_t simulationTick = 0;
        std::uint64_t targetTick = 0;
        int playerId = 0;
        int type = 0;
        int accepted = 0;
        std::string reason;
        std::string commandPayload;

        ar >> commandId
           >> simulationTick
           >> targetTick
           >> playerId
           >> type
           >> accepted
           >> reason
           >> commandPayload;

        if (!ar.IsValid() || !GameCommand::IsValidType(type) ||
            reason.size() > MaxReasonBytes || commandPayload.size() > GameCommand::MaxSerializedBytes)
            return false;

        GameCommandResult parsed;
        parsed.commandId = commandId;
        parsed.simulationTick = simulationTick;
        parsed.targetTick = targetTick;
        parsed.playerId = playerId;
        parsed.type = static_cast<GameCommandType>(type);
        parsed.accepted = accepted != 0;
        parsed.reason = std::move(reason);
        parsed.commandPayload = std::move(commandPayload);
        result = std::move(parsed);
        return true;
    }
};

struct GameServerFrame
{
    static constexpr int WireVersion = SerializationVersion::GameServerFrameVersion;
    static constexpr std::size_t MaxSerializedBytes = 256 * 1024;
    static constexpr std::size_t MaxResults = 256;

    std::uint64_t tick{0};
    std::uint64_t checksum{0};
    bool hasChecksum{false};
    std::vector<GameCommandResult> results;

    std::string Serialize() const
    {
        Archive ar(SerializationVersion::GameServerFrameVersion);
        ar << tick
           << (hasChecksum ? 1 : 0)
           << checksum
           << static_cast<uint64_t>(results.size());

        std::string payload = ar.GetString();
        std::ostringstream stream;
        stream << payload;
        for (const auto& result : results)
            stream << ' ' << std::quoted(result.Serialize());
        return stream.str();
    }

    static bool TryDeserialize(const std::string& payload, GameServerFrame& frame)
    {
        if (payload.size() > MaxSerializedBytes)
            return false;
        std::istringstream stream(payload);
        int version = 0;
        int hasChecksum = 0;
        size_t resultCount = 0;
        GameServerFrame parsed;
        stream >> version >> parsed.tick >> hasChecksum >> parsed.checksum >> resultCount;
        if (!stream || version != SerializationVersion::GameServerFrameVersion || resultCount > MaxResults)
            return false;

        parsed.hasChecksum = hasChecksum != 0;
        parsed.results.reserve(resultCount);
        for (size_t i = 0; i < resultCount; i++)
        {
            std::string serializedResult;
            stream >> std::quoted(serializedResult);
            if (!stream)
                return false;

            GameCommandResult result;
            if (!GameCommandResult::TryDeserialize(serializedResult, result))
                return false;
            parsed.results.push_back(std::move(result));
        }

        frame = std::move(parsed);
        return true;
    }
};

#endif
