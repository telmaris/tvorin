#ifndef GAME_COMMAND_H
#define GAME_COMMAND_H

#include "core/Serialization.h"
#include "economy/Building.h"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

enum class GameCommandType
{
    BuildBuilding,
    DestroyBuilding,
    SetReceiver,
    StartFocus,
    StartTechnologyResearch,
    RecruitUnit,
    DeployUnits,
    UpgradeBuilding,
    SetRecipe,
    SetTowerTargetMode,
    SetProductionBlocked,
    DebugDeployEnemyUnits,
    SetRoadPriority
};

struct GameCommand
{
    static constexpr std::size_t MaxSerializedBytes = 64 * 1024;
    static constexpr int MaxUnitInstanceIds = 1024;
    static constexpr std::size_t MaxResearchIdBytes = 256;
    // TD(etap-1): dropped the old war system's command types (IssueMilitaryOrder,
    // MoveDivision, FormArmy, AttackTile, AssignToArmy, RecruitUnit) and their wire
    // fields (militaryOrderType, militaryUnitType, divisionId) — replacements land
    // data-driven in the Tower Defense rework (ETAP 3+).
    // TD(etap-3): RecruitUnit is back, rebuilt on the BattleUnit/UnitRoster
    // architecture — reuses sourceTileId (recruiting building) and researchId
    // (unit definition id) rather than adding new wire fields.
    // TD(etap-4): DeployUnits reuses targetTileId as the target player id
    // (deploy has no tile target of its own) and adds the one genuinely new
    // field this command needs — a variable-length unit instance id list,
    // which nothing existing could be repurposed for.
    static constexpr int WireVersion = SerializationVersion::GameCommandVersion;

    static GameCommand BuildBuilding(int playerId, BuildingType buildingType, Vec2i tilePos)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::BuildBuilding;
        command.buildingType = buildingType;
        command.tilePos = tilePos;
        return command;
    }

    static GameCommand DestroyBuilding(int playerId, int tileId)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::DestroyBuilding;
        command.sourceTileId = tileId;
        return command;
    }

    // Reuses sourceTileId (the building to upgrade), same as DestroyBuilding
    // — cost/duration for the next level are resolved deterministically
    // server-side from the building's current UpgradeComponent::level, so no
    // new wire field is needed.
    static GameCommand UpgradeBuilding(int playerId, int tileId)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::UpgradeBuilding;
        command.sourceTileId = tileId;
        return command;
    }

    // Reuses sourceTileId (the building whose recipe to switch) and targetTileId
    // (the recipe index to activate) — resolved deterministically server-side,
    // no new wire field (same trick as UpgradeBuilding).
    static GameCommand SetRecipe(int playerId, int buildingTileId, int recipeIndex)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::SetRecipe;
        command.sourceTileId = buildingTileId;
        command.targetTileId = recipeIndex;
        return command;
    }

    static GameCommand SetTowerTargetMode(int playerId, int buildingTileId, int mode)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::SetTowerTargetMode;
        command.sourceTileId = buildingTileId;
        command.targetTileId = mode;
        return command;
    }

    static GameCommand SetProductionBlocked(int playerId, int buildingTileId, bool blocked)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::SetProductionBlocked;
        command.sourceTileId = buildingTileId;
        command.targetTileId = blocked ? 1 : 0;
        return command;
    }

    // Reuses sourceTileId for the road tile and targetTileId for the physical
    // ResourceType. ResourceType::Null clears the priority.
    static GameCommand SetRoadPriority(int playerId, int roadTileId, ResourceType resource)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::SetRoadPriority;
        command.sourceTileId = roadTileId;
        command.targetTileId = static_cast<int>(resource);
        return command;
    }

    static GameCommand SetReceiver(int playerId, int sourceTileId, int targetTileId, bool alternativeReceiver = false)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::SetReceiver;
        command.sourceTileId = sourceTileId;
        command.targetTileId = targetTileId;
        command.alternativeReceiver = alternativeReceiver;
        return command;
    }

    static GameCommand StartFocus(int playerId, std::string focusId)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::StartFocus;
        command.researchId = std::move(focusId);
        return command;
    }

    static GameCommand StartTechnologyResearch(int playerId, std::string technologyId, int universityTileId)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::StartTechnologyResearch;
        command.researchId = std::move(technologyId);
        command.sourceTileId = universityTileId;
        return command;
    }

    static GameCommand RecruitUnit(int playerId, int buildingTileId, std::string unitDefId)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::RecruitUnit;
        command.sourceTileId = buildingTileId;
        command.researchId = std::move(unitDefId);
        return command;
    }

    // orderedUnitInstanceIds: column order, spearhead first (see the plan's
    // deploy decision — GUI composes the attack group, this command carries
    // the resulting order verbatim). targetPlayerId must always be explicit
    // even when a GUI auto-fills it in 1v1, so replay/determinism never
    // depends on client-side inference.
    static GameCommand DeployUnits(int playerId, int targetPlayerId, std::vector<int> orderedUnitInstanceIds)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::DeployUnits;
        command.targetTileId = targetPlayerId;
        command.unitInstanceIds = std::move(orderedUnitInstanceIds);
        return command;
    }

    // Debug-only deterministic attack injection. targetTileId carries the
    // requested unit count so the wire format needs no additional field.
    static GameCommand DebugDeployEnemyUnits(int playerId, int unitCount = 4)
    {
        GameCommand command;
        command.playerId = playerId;
        command.type = GameCommandType::DebugDeployEnemyUnits;
        command.targetTileId = unitCount;
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
           << static_cast<int>(unitInstanceIds.size());
        for (int id : unitInstanceIds)
            ar << id;
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
        int unitInstanceIdCount = 0;

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
           >> unitInstanceIdCount;

        if (!ar.IsValid() || !IsValidType(type) || unitInstanceIdCount < 0 ||
            unitInstanceIdCount > MaxUnitInstanceIds || researchId.size() > MaxResearchIdBytes)
            return false;

        std::vector<int> unitInstanceIds;
        unitInstanceIds.reserve(static_cast<size_t>(unitInstanceIdCount));
        for (int i = 0; i < unitInstanceIdCount; i++)
        {
            int id = 0;
            ar >> id;
            if (!ar.IsValid())
                return false;
            unitInstanceIds.push_back(id);
        }

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
        parsed.unitInstanceIds = std::move(unitInstanceIds);
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
            case GameCommandType::DeployUnits:
            case GameCommandType::UpgradeBuilding:
            case GameCommandType::SetRecipe:
            case GameCommandType::SetTowerTargetMode:
            case GameCommandType::SetProductionBlocked:
            case GameCommandType::DebugDeployEnemyUnits:
            case GameCommandType::SetRoadPriority:
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
