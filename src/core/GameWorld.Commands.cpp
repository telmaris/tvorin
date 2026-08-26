#include "core/GameWorldInternal.h"
#include "core/Log.h"
#include "economy/BuildingSalvage.h"
#include "ui/AudioSystem.h"

#include <algorithm>

using namespace GameWorldInternal;

BuildPaymentPolicy ResolveBuildPaymentPolicy(const GameWorld& world, const Player& player)
{
    return world.GetTileMap().params.debugMode &&
                   player.controllerType == PlayerControllerType::LocalHuman
               ? BuildPaymentPolicy::FreeDebugHuman
               : BuildPaymentPolicy::ChargeAuthoritativeCost;
}

// Submits this command to the simulation.
std::uint64_t GameWorld::SubmitCommand(const GameCommand& command)
{
    return SubmitCommand(command, simulationTick + 1);
}

// Queues an intent and assigns an authoritative target tick when needed.
std::uint64_t GameWorld::SubmitCommand(const GameCommand& command, std::uint64_t minimumTargetTick)
{
    GameCommand queued = command;
    if (queued.commandId == 0)
        queued.commandId = nextCommandId++;
    else
        nextCommandId = std::max(nextCommandId, queued.commandId + 1);
    if (queued.targetTick == 0 || queued.targetTick < minimumTargetTick)
        queued.targetTick = minimumTargetTick;
    std::uint64_t commandId = queued.commandId;
    pendingCommands.push_back(std::move(queued));
    return commandId;
}

// Returns command accept/reject results emitted since the last consume.
std::vector<GameCommandResult> GameWorld::ConsumeCommandResults()
{
    std::vector<GameCommandResult> results = std::move(commandResults);
    commandResults.clear();
    return results;
}

// Applies a command received from an authoritative host to a client-side mirror.
bool GameWorld::ApplyAuthoritativeCommand(const GameCommand& command)
{
    return ExecuteCommand(command);
}

// Initializes GameWorld::AttachControllerForPlayer.
void GameWorld::AttachControllerForPlayer(Player* player)
{
    if (player == nullptr)
        return;

    switch (player->controllerType)
    {
        case PlayerControllerType::AI:
            controllers.push_back(std::make_unique<AIController>(player->id));
            break;
        case PlayerControllerType::Remote:
            controllers.push_back(std::make_unique<RemoteController>(player->id));
            break;
        case PlayerControllerType::LocalHuman:
        default:
            controllers.push_back(std::make_unique<LocalController>(player->id));
            break;
    }
}

// Advances UpdateControllers for one frame or simulation tick.
void GameWorld::UpdateControllers(double dt)
{
    for (auto& controller : controllers)
        if (controller != nullptr)
            controller->Update(*this, dt);
}

// Executes queued gameplay commands in order.
void GameWorld::ProcessCommands()
{
    std::deque<GameCommand> deferredCommands;
    while (!pendingCommands.empty())
    {
        GameCommand command = pendingCommands.front();
        pendingCommands.pop_front();
        if (command.targetTick > simulationTick)
        {
            deferredCommands.push_back(std::move(command));
            continue;
        }

        bool accepted = ExecuteCommand(command);
        commandResults.push_back(GameCommandResult{
            command.commandId,
            simulationTick,
            command.targetTick,
            command.playerId,
            command.type,
            accepted,
            accepted ? "accepted" : "rejected",
            command.Serialize()});
    }
    pendingCommands = std::move(deferredCommands);
}

// Validates and applies one gameplay command.
bool GameWorld::ExecuteCommand(const GameCommand& command)
{
    auto playerIt = playerHandler.players.find(command.playerId);
    if (playerIt == playerHandler.players.end())
        return false;

    Player* player = playerIt->second.get();
    if (player == nullptr)
        return false;
    // TD(etap-6.3): an eliminated player's roster/units already vanished and
    // their AI/input is expected to stop — reject any stray command from a
    // stale client/controller rather than letting it silently re-mutate a
    // defeated player's (mostly inert) remaining state.
    if (player->defeated)
        return false;
    auto acceptCommand = [&]()
    {
        player->TrackAcceptedCommand(command.type);
        return true;
    };

    auto playFx = [this, &command](const std::string& id, float vol = 1.0f)
    {
        if (audio != nullptr && command.playerId == localPlayerId)
            audio->PlaySound(id, vol);
    };

    if (command.type == GameCommandType::BuildBuilding)
    {
        if (!tilemap.IsInside(command.tilePos))
            return false;

        auto preview = CreateBuildingFromType(command.buildingType, 0);
        if (preview == nullptr)
            return false;

        // This is gameplay state, not a sample from the screen-space FBO:
        // the exact result must be independent of camera position and renderer
        // availability. AI deliberately retains its omniscient-map bonus.
        if (IsFogOfWarPreferenceEnabled() && player->controllerType != PlayerControllerType::AI &&
            !IsBuildFootprintVisibleToPlayer(player->id, command.tilePos, preview->GetFootprint()))
            return false;

        if (!tilemap.CanPlaceBuilding(command.buildingType, command.tilePos, preview->GetFootprint(), player))
            return false;

        const auto& definition = GetBuildingDefinition(command.buildingType);
        const bool freeBuild = ResolveBuildPaymentPolicy(*this, *player) == BuildPaymentPolicy::FreeDebugHuman;
        const auto effectiveCosts = player->GetEffectiveBuildCosts(definition);
        if (!freeBuild)
        {
            auto failures = player->GetBuildRequirementFailures(definition);
            if (!failures.empty())
            {
                Log::Msg("[GameWorld]", "Command rejected: ", definition.name, " locked by ", failures.front());
                return false;
            }
        }
        if (!freeBuild && !player->TryPayBuildCost(effectiveCosts))
        {
            Log::Msg("[GameWorld]", "Command rejected: not enough resources to build ", definition.name);
            return false;
        }

        int tileId = tilemap.GetIdFromCoords(command.tilePos);
        auto building = CreateBuildingFromType(command.buildingType, player->id * 100000 + player->build.buildingId++);
        if (building == nullptr)
        {
            if (!freeBuild)
                player->RefundBuildCost(effectiveCosts);
            return false;
        }

        double buildTime = player->ModifyBalanceAt(BalanceStat::BuildTime, definition.buildTime, command.buildingType, command.tilePos);
        building->buildTime = buildTime;
        building->constructionRemaining = freeBuild ? 0.0 : buildTime;
        tilemap.BuildOnTile(tileId, player, std::move(building));

        Building* placed = tilemap.GetBuilding(tileId);
        if (placed == nullptr)
        {
            if (!freeBuild)
                player->RefundBuildCost(effectiveCosts);
            return false;
        }
        placed->buildCostRecordState = freeBuild ? BuildCostRecordState::Free
                                                  : BuildCostRecordState::PaidRecorded;
        placed->buildCostWasPaid = !freeBuild;
        placed->paidBuildCosts = freeBuild ? std::vector<ResourceAmountDefinition>{} : effectiveCosts;

        if (placed->IsUnderConstruction())
        {
            playFx("build");
            return acceptCommand();
        }

        if (player->roadNetwork != nullptr)
        {
            for (int occupiedTileId : tilemap.GetBuildingTileIds(placed))
                player->roadNetwork->UpdateNavMap(occupiedTileId, placed);
        }
        tilemap.AutoConnectBuilding(placed);
        playFx("build");
        return acceptCommand();
    }

    if (command.type == GameCommandType::DestroyBuilding)
    {
        Building* building = tilemap.GetBuilding(command.sourceTileId);
        if (building == nullptr || building->owner != player)
            return false;
        if (!building->CanBeManuallyDestroyed())
        {
            Log::Msg("[GameWorld]", "Command rejected: ", building->name, " cannot be manually destroyed");
            return false;
        }

        if (!ExecuteDemolition(tilemap, *player, *building))
            return false;
        playFx("destroy");
        return acceptCommand();
    }

    if (command.type == GameCommandType::UpgradeBuilding)
    {
        Building* building = tilemap.GetBuilding(command.sourceTileId);
        if (building == nullptr || building->owner != player)
            return false;

        auto* upgrade = building->GetComponent<UpgradeComponent>();
        if (upgrade == nullptr || upgrade->isUpgrading || upgrade->level >= upgrade->maxLevel)
            return false;

        const auto& definition = GetBuildingDefinition(building->buildingType);
        int targetLevel = upgrade->level + 1;
        const auto* levelDefinition = FindUpgradeLevelDefinition(definition, targetLevel);
        if (levelDefinition == nullptr)
            return false;

        if (!player->TryPayBuildCost(levelDefinition->cost))
        {
            Log::Msg("[GameWorld]", "Command rejected: not enough resources to upgrade ", building->name);
            return false;
        }

        upgrade->isUpgrading = true;
        upgrade->upgradeRemaining = levelDefinition->buildTime;
        return acceptCommand();
    }

    if (command.type == GameCommandType::SetRecipe)
    {
        Building* building = tilemap.GetBuilding(command.sourceTileId);
        if (building == nullptr || building->owner != player || building->IsUnderConstruction())
            return false;

        auto* recipes = building->GetComponent<RecipeComponent>();
        auto* production = building->GetComponent<ProductionComponent>();
        auto* logistics = building->GetComponent<LogisticsComponent>();
        auto* workers = building->GetComponent<WorkerComponent>();
        if (recipes == nullptr || production == nullptr || logistics == nullptr || workers == nullptr)
            return false;

        if (!recipes->SetActiveRecipe(command.targetTileId, *building, *production, *logistics, *workers))
            return false;
        return acceptCommand();
    }

    if (command.type == GameCommandType::SetTowerTargetMode)
    {
        Building* building = tilemap.GetBuilding(command.sourceTileId);
        if (building == nullptr || building->owner != player || building->IsUnderConstruction())
            return false;
        auto* tower = building->GetComponent<TowerCombatComponent>();
        if (tower == nullptr || command.targetTileId < static_cast<int>(TowerTargetMode::NearestToHq) ||
            command.targetTileId > static_cast<int>(TowerTargetMode::StrongestUnit))
            return false;
        tower->targetMode = static_cast<TowerTargetMode>(command.targetTileId);
        return acceptCommand();
    }

    if (command.type == GameCommandType::SetProductionBlocked)
    {
        Building* building = tilemap.GetBuilding(command.sourceTileId);
        if (building == nullptr || building->owner != player ||
            building->IsUnderConstruction() || !building->CanBlockProduction())
            return false;
        if (command.targetTileId != 0 && command.targetTileId != 1)
            return false;

        building->SetProductionBlocked(command.targetTileId == 1);
        return acceptCommand();
    }

    if (command.type == GameCommandType::SetRoadPriority)
    {
        Building* building = tilemap.GetBuilding(command.sourceTileId);
        if (building == nullptr || building->owner != player || building->IsUnderConstruction() ||
            !IsRoadLike(building->buildingType))
            return false;

        if (command.targetTileId < 0 || command.targetTileId > 255)
            return false;
        const ResourceType resource = static_cast<ResourceType>(command.targetTileId);
        auto* road = building->GetComponent<RoadComponent>();
        if (road == nullptr || !RoadComponent::IsValidPriorityResource(resource))
            return false;

        road->SetPriorityResource(resource);
        return acceptCommand();
    }

    if (command.type == GameCommandType::SetReceiver)
    {
        Building* source = tilemap.GetBuilding(command.sourceTileId);
        Building* target = tilemap.GetBuilding(command.targetTileId);
        if (source == nullptr || target == nullptr || source == target)
            return false;
        if (source->owner != player || target->owner != player)
            return false;
        if (source->IsUnderConstruction() || target->IsUnderConstruction())
            return false;

        tilemap.ConnectReceiver(source, target, command.alternativeReceiver);
        return acceptCommand();
    }

    if (command.type == GameCommandType::StartFocus)
    {
        if (command.researchId.empty() || !player->CanUnlockFocus(command.researchId))
            return false;

        if (!player->StartFocus(command.researchId))
            return false;
        playFx("research");
        return acceptCommand();
    }

    if (command.type == GameCommandType::StartTechnologyResearch)
    {
        Building* source = tilemap.GetBuilding(command.sourceTileId);
        if (source == nullptr || source->owner != player || source->IsUnderConstruction())
            return false;

        if (source->buildingType != BuildingType::University ||
            source->GetComponent<ResearchComponent>() == nullptr)
            return false;

        if (command.researchId.empty() || !player->CanResearchTechnology(command.researchId))
            return false;

        if (!player->StartTechnologyResearch(command.researchId, source))
            return false;
        playFx("research");
        return acceptCommand();
    }

    if (command.type == GameCommandType::RecruitUnit)
    {
        Building* source = tilemap.GetBuilding(command.sourceTileId);
        if (source == nullptr || source->owner != player || source->IsUnderConstruction())
            return false;

        auto* recruitment = source->GetComponent<RecruitmentComponent>();
        if (recruitment == nullptr || command.researchId.empty())
            return false;

        if (!recruitment->QueueRecruitment(*source, command.researchId))
            return false;

        playFx("build");
        return acceptCommand();
    }

    if (command.type == GameCommandType::DeployUnits)
    {
        int targetPlayerId = command.targetTileId;
        if (command.unitInstanceIds.empty() || targetPlayerId == player->id)
            return false;
        auto targetPlayerIt = playerHandler.players.find(targetPlayerId);
        if (targetPlayerIt == playerHandler.players.end() || targetPlayerIt->second == nullptr || targetPlayerIt->second->defeated)
            return false;
        // TD(etap-6.3): a target need not be a direct ring neighbor as long as
        // every player in between has been eliminated — the route then runs
        // through their conquered HQ.
        auto isEliminated = [&](int playerId)
        {
            auto it = playerHandler.players.find(playerId);
            return it != playerHandler.players.end() && it->second != nullptr && it->second->defeated;
        };
        if (!PathingService::AreHqsConnected(militaryRoads, player->id, targetPlayerId, isEliminated))
            return false;

        // All-or-nothing: every listed unit must be a valid, currently-rostered
        // instance before any of them are moved, so a malformed/stale command
        // never partially deploys a column.
        for (int unitInstanceId : command.unitInstanceIds)
        {
            const BattleUnit* unit = player->roster.FindUnit(unitInstanceId);
            if (unit == nullptr || unit->state != BattleUnitState::InRoster)
                return false;
        }

        auto routeKey = std::make_pair(player->id, targetPlayerId);
        for (int unitInstanceId : command.unitInstanceIds)
        {
            auto removed = player->roster.RemoveUnit(unitInstanceId);
            if (!removed.has_value())
                continue;

            BattleUnit unit = std::move(removed.value());
            unit.state = BattleUnitState::Marching;
            unit.routeFromPlayerId = player->id;
            unit.routeToPlayerId = targetPlayerId;
            unit.tileIndex = -1;
            unit.tileProgress = 0.0;
            deployedUnits[unitInstanceId] = std::move(unit);
            spawnQueues[routeKey].push_back(unitInstanceId);
        }

        playFx("build");
        return acceptCommand();
    }

    if (command.type == GameCommandType::DebugDeployEnemyUnits)
    {
        if (!tilemap.params.debugMode || command.targetTileId < 1 || command.targetTileId > 16)
            return false;

        Player* enemy = nullptr;
        for (int neighborId : militaryRoads.GetNeighbors(player->id))
        {
            auto it = playerHandler.players.find(neighborId);
            if (it != playerHandler.players.end() && it->second != nullptr && !it->second->defeated)
            {
                enemy = it->second.get();
                break;
            }
        }
        if (enemy == nullptr)
            return false;

        const std::vector<int> route = militaryRoads.GetDirectedTiles(enemy->id, player->id);
        if (route.empty())
            return false;

        // Debug attacks are meant for rapid combat iteration. Put the head of
        // the injected column three quarters of the way toward the local HQ,
        // with following units staggered behind it instead of waiting at the
        // enemy gate like a normal deployment.
        const int lastRouteIndex = static_cast<int>(route.size()) - 1;
        const int furthestMarchIndex = std::max(0, lastRouteIndex - 1);
        const int debugStartIndex = std::min(furthestMarchIndex, lastRouteIndex * 3 / 4);
        for (int i = 0; i < command.targetTileId; ++i)
        {
            const int instanceId = enemy->id * 100000 + enemy->nextUnitInstanceId++;
            BattleUnit unit(instanceId, enemy->id, "militia");
            unit.currentHp = unit.GetEffectiveMaxHp(*enemy);
            unit.state = BattleUnitState::Marching;
            unit.routeFromPlayerId = enemy->id;
            unit.routeToPlayerId = player->id;
            unit.tileIndex = std::max(0, debugStartIndex - i);
            unit.tileProgress = 0.0;
            unit.attackTimer = 0.0;
            deployedUnits[instanceId] = std::move(unit);
        }

        playFx("build");
        return acceptCommand();
    }

    return false;
}

std::string GameWorld::GetAITrace(int playerId) const
{
    for (const auto& controller : controllers)
    {
        const auto* ai = dynamic_cast<const AIController*>(controller.get());
        if (ai != nullptr && ai->playerId == playerId)
            return ai->GetDecisionTrace();
    }
    return {};
}
