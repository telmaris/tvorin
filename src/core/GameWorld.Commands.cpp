#include "core/GameWorldInternal.h"
#include "core/Log.h"
#include "economy/StockpileIndex.h"
#include "world/ColonizationService.h"
#include "warfare/UnitDefinition.h"
#include "warfare/GarrisonService.h"
#include "warfare/TaskGroup.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace GameWorldInternal;

namespace
{
    bool HasLegalProvinceRoute(const GlobalMap& map, ProvinceId source, ProvinceId target)
    {
        return map.HasPath(source, target);
    }

    WorldJourneyRules ResolveJourneyRules(const Player& player,
                                          std::uint64_t baseLegDurationTicks)
    {
        WorldJourneyRules rules;
        rules.baseLegDurationTicks = baseLegDurationTicks;
        rules.routeTravelSpeedMultiplier =
            player.ModifyBalance(BalanceStat::RouteTravelSpeed, 1.0);
        return rules;
    }

    bool ExpandReadyTaskGroups(const Player& player, ProvinceId sourceProvinceId,
                               const std::vector<TaskGroupId>& taskGroupIds,
                               std::vector<int>& unitIds, std::string& failureReason)
    {
        if (taskGroupIds.empty() || taskGroupIds.size() > GameCommand::MaxTaskGroupIds ||
            !std::is_sorted(taskGroupIds.begin(), taskGroupIds.end()) ||
            std::adjacent_find(taskGroupIds.begin(), taskGroupIds.end()) != taskGroupIds.end())
        {
            failureReason = "task group IDs must be sorted and unique";
            return false;
        }

        unitIds.clear();
        for (const TaskGroupId taskGroupId : taskGroupIds)
        {
            const TaskGroup* group = player.taskGroups.Find(taskGroupId);
            if (group == nullptr || group->stationProvinceId != sourceProvinceId ||
                TaskGroupService::ResolveStatus(*group, player.roster) != TaskGroupStatus::Reserve)
            {
                failureReason = "task group is missing, deployed, empty or stationed elsewhere";
                return false;
            }
            for (const auto& [unitId, unit] : player.roster.units)
            {
                if (unit.taskGroupId != taskGroupId)
                    continue;
                if (!UnitAssignmentService::IsInReserve(unit, sourceProvinceId) ||
                    unit.assignment.buildingId != group->homeBarracksBuildingId)
                {
                    failureReason = "task group contains a unit outside its home Barracks reserve";
                    return false;
                }
                unitIds.push_back(unitId);
            }
        }
        if (unitIds.empty() || unitIds.size() > GameCommand::MaxUnitInstanceIds)
        {
            failureReason = "task group deployment has no units or exceeds the unit limit";
            return false;
        }
        std::sort(unitIds.begin(), unitIds.end());
        if (std::adjacent_find(unitIds.begin(), unitIds.end()) != unitIds.end())
        {
            failureReason = "task group deployment contains duplicate units";
            return false;
        }
        return true;
    }
}

BuildPaymentPolicy ResolveBuildPaymentPolicy(const TileMap& map, const Player& player)
{
    return map.params.debugMode &&
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

void GameWorld::UpdateColonizationOperations()
{
    std::vector<ProvinceId> toRemove;
    for (auto& [targetProvinceId, operation] : pendingColonizations)
    {
        bool shouldRefund = false;
        bool shouldComplete = false;
        if (operation.phase == ColonizationPhase::Traveling)
        {
            const auto journeyIt = armyJourneySystem.GetJourneys().find(operation.journeyId);
            if (journeyIt == armyJourneySystem.GetJourneys().end())
                shouldRefund = true;
            else if (journeyIt->second.status == WorldJourneyStatus::Failed ||
                     journeyIt->second.status == WorldJourneyStatus::Cancelled)
                shouldRefund = true;
            else if (journeyIt->second.status == WorldJourneyStatus::Succeeded)
            {
                if (operation.settlementDurationTicks == 0 ||
                    simulationTick > std::numeric_limits<std::uint64_t>::max() -
                        operation.settlementDurationTicks)
                    shouldRefund = true;
                else
                {
                    operation.phase = ColonizationPhase::Establishing;
                    operation.phaseCompletionTick = simulationTick +
                        operation.settlementDurationTicks;
                }
            }
        }
        else if (operation.phase == ColonizationPhase::Establishing &&
                 operation.phaseCompletionTick <= simulationTick)
            shouldComplete = true;
        else if (operation.phase == ColonizationPhase::Failed ||
                 operation.phase == ColonizationPhase::Completed)
            shouldRefund = operation.phase == ColonizationPhase::Failed;

        if (shouldRefund || shouldComplete)
            toRemove.push_back(targetProvinceId);
    }

    for (const ProvinceId targetProvinceId : toRemove)
    {
        const auto it = pendingColonizations.find(targetProvinceId);
        if (it == pendingColonizations.end())
            continue;
        const ColonizationOperation operation = it->second;
        pendingColonizations.erase(it);
        if (operation.phase == ColonizationPhase::Establishing &&
            CompleteColonization(operation))
            continue;

        // Generation is prepared on a side object. If it fails, the target
        // remains neutral and the already charged resources are returned to
        // the originating province without touching any other province.
        const auto playerIt = playerHandler.players.find(
            static_cast<int>(operation.playerId));
        auto* source = globalMap.FindBuildableProvince(operation.sourceProvinceId);
        if (playerIt != playerHandler.players.end() && playerIt->second != nullptr &&
            source != nullptr && source->GetSimulation() != nullptr)
        {
            for (const auto& cost : operation.cost)
                StockpileIndex::Deposit(source->GetSimulation()->GetEconomy(),
                                        cost.type, cost.amount);
        }
    }
}

std::optional<ColonizationProgressView> GameWorld::GetColonizationProgress(
    ProvinceId targetProvinceId) const
{
    const auto operationIt = pendingColonizations.find(targetProvinceId);
    if (operationIt == pendingColonizations.end())
        return std::nullopt;

    const ColonizationOperation& operation = operationIt->second;
    ColonizationProgressView view;
    view.phase = operation.phase;
    if (operation.phase == ColonizationPhase::Traveling)
    {
        const auto journeyIt = armyJourneySystem.GetJourneys().find(operation.journeyId);
        if (journeyIt == armyJourneySystem.GetJourneys().end())
            return view;
        const JourneyStatusView journey = BuildJourneyStatusView(journeyIt->second,
                                                                  simulationTick);
        const std::uint64_t totalTicks = journey.etaTick > journey.startTick
            ? journey.etaTick - journey.startTick : 0;
        view.remainingTicks = journey.remainingTicks;
        view.progress = totalTicks == 0 ? 0.0f : std::clamp(
            static_cast<float>(totalTicks - std::min(totalTicks, view.remainingTicks)) /
                static_cast<float>(totalTicks), 0.0f, 1.0f);
    }
    else if (operation.phase == ColonizationPhase::Establishing)
    {
        view.remainingTicks = operation.phaseCompletionTick > simulationTick
            ? operation.phaseCompletionTick - simulationTick : 0;
        view.progress = operation.settlementDurationTicks == 0 ? 1.0f : std::clamp(
            static_cast<float>(operation.settlementDurationTicks -
                std::min(operation.settlementDurationTicks, view.remainingTicks)) /
                static_cast<float>(operation.settlementDurationTicks), 0.0f, 1.0f);
    }
    else
        view.progress = operation.phase == ColonizationPhase::Completed ? 1.0f : 0.0f;
    return view;
}

namespace
{
    bool DepositCargo(ProvinceEconomy& economy, std::vector<ResourceAmount>& cargo)
    {
        for (auto it = cargo.begin(); it != cargo.end();)
        {
            const int deposited = StockpileIndex::Deposit(economy, it->type, it->amount);
            if (deposited <= 0)
                return false;
            if (deposited >= it->amount)
                it = cargo.erase(it);
            else
            {
                it->amount -= deposited;
                return false;
            }
        }
        return true;
    }

    Building* FindProvinceBuilding(ProvinceEconomy& economy, int buildingId)
    {
        for (Building* building : economy.dataTracker.buildings)
            if (building != nullptr && building->id == buildingId)
                return building;
        return nullptr;
    }
}

void GameWorld::ProcessResourceTransfers()
{
    auto& journeys = armyJourneySystem.GetJourneysForAuthority();
    for (auto& [journeyId, journey] : journeys)
    {
        auto* convoy = std::get_if<ResourceConvoy>(&journey.payload);
        if (convoy == nullptr || convoy->cargo.empty() ||
            (journey.status != WorldJourneyStatus::Succeeded &&
             journey.status != WorldJourneyStatus::AwaitingUnload &&
             journey.status != WorldJourneyStatus::Failed &&
             journey.status != WorldJourneyStatus::Cancelled))
            continue;

        const auto playerIt = playerHandler.players.find(static_cast<int>(journey.ownerId));
        auto* source = globalMap.FindBuildableProvince(journey.sourceProvinceId);
        auto* target = globalMap.FindBuildableProvince(journey.targetProvinceId);
        if (playerIt == playerHandler.players.end() || playerIt->second == nullptr ||
            source == nullptr || source->GetSimulation() == nullptr ||
            source->GetOwnerId() != journey.ownerId)
            continue;

        ProvinceEconomy* destination = nullptr;
        if (journey.status == WorldJourneyStatus::Succeeded ||
            journey.status == WorldJourneyStatus::AwaitingUnload)
        {
            if (target != nullptr && target->GetSimulation() != nullptr &&
                target->GetOwnerId() == journey.ownerId)
                destination = &target->GetSimulation()->GetEconomy();
            if (destination == nullptr)
                journey.status = WorldJourneyStatus::Failed;
        }
        if (journey.status == WorldJourneyStatus::Failed ||
            journey.status == WorldJourneyStatus::Cancelled)
        {
            DepositCargo(source->GetSimulation()->GetEconomy(), convoy->cargo);
            continue;
        }
        if (destination != nullptr && DepositCargo(*destination, convoy->cargo) == false)
            journey.status = WorldJourneyStatus::AwaitingUnload;
    }
}

void GameWorld::ProcessArmyTransfers()
{
    auto& journeys = armyJourneySystem.GetJourneysForAuthority();
    for (auto& [journeyId, journey] : journeys)
    {
        auto* transfer = std::get_if<ArmyTransferParty>(&journey.payload);
        if (transfer == nullptr || transfer->unitInstanceIds.empty() ||
            (journey.status != WorldJourneyStatus::Succeeded &&
             journey.status != WorldJourneyStatus::Failed &&
             journey.status != WorldJourneyStatus::Cancelled))
            continue;
        const auto playerIt = playerHandler.players.find(static_cast<int>(journey.ownerId));
        auto* source = globalMap.FindBuildableProvince(journey.sourceProvinceId);
        auto* target = globalMap.FindBuildableProvince(journey.targetProvinceId);
        if (playerIt == playerHandler.players.end() || playerIt->second == nullptr ||
            source == nullptr || target == nullptr)
            continue;
        Player& player = *playerIt->second;
        if (journey.status == WorldJourneyStatus::Failed ||
            journey.status == WorldJourneyStatus::Cancelled)
        {
            bool restored = true;
            for (const int unitId : transfer->unitInstanceIds)
            {
                BattleUnit* unit = player.roster.FindUnit(unitId);
                const TaskGroup* group = unit == nullptr
                    ? nullptr : player.taskGroups.Find(unit->taskGroupId);
                if (unit == nullptr || group == nullptr)
                    continue;
                if (UnitAssignmentService::IsOnJourney(*unit, journeyId) &&
                    !UnitAssignmentService::AssignReserve(
                        *unit, journey.sourceProvinceId, group->homeBarracksBuildingId))
                    restored = false;
            }
            if (restored)
                transfer->unitInstanceIds.clear();
            continue;
        }

        if (target->GetSimulation() == nullptr || target->GetOwnerId() != player.id)
            continue;
        ProvinceEconomy& targetEconomy = target->GetSimulation()->GetEconomy();
        Building* barracks = FindProvinceBuilding(targetEconomy,
                                                  transfer->destinationBarracksBuildingId);
        if (barracks == nullptr || barracks->owner != &player ||
            barracks->buildingType != BuildingType::Barracks || barracks->IsUnderConstruction())
            continue;

        std::set<TaskGroupId> groups;
        bool valid = true;
        for (const int unitId : transfer->unitInstanceIds)
        {
            BattleUnit* unit = player.roster.FindUnit(unitId);
            if (unit == nullptr || !UnitAssignmentService::IsOnJourney(*unit, journeyId) ||
                unit->taskGroupId == InvalidTaskGroupId ||
                player.taskGroups.Find(unit->taskGroupId) == nullptr)
            {
                valid = false;
                break;
            }
            groups.insert(unit->taskGroupId);
        }
        if (!valid)
            continue;
        std::map<TaskGroupId, TaskGroup> originalGroups;
        for (const TaskGroupId groupId : groups)
        {
            const TaskGroup* group = player.taskGroups.Find(groupId);
            if (group == nullptr)
            {
                valid = false;
                break;
            }
            originalGroups.emplace(groupId, *group);
        }
        if (!valid)
            continue;
        const auto restoreJourneyAssignments = [&]()
        {
            for (const int unitId : transfer->unitInstanceIds)
            {
                BattleUnit* unit = player.roster.FindUnit(unitId);
                const auto originalIt = unit == nullptr
                    ? originalGroups.end() : originalGroups.find(unit->taskGroupId);
                if (unit != nullptr && originalIt != originalGroups.end())
                    UnitAssignmentService::AssignJourney(
                        *unit, journey.sourceProvinceId, journeyId,
                        originalIt->second.homeBarracksBuildingId);
            }
            for (const auto& [groupId, original] : originalGroups)
                player.taskGroups.Relocate(groupId, original.stationProvinceId,
                                            original.homeBarracksBuildingId);
        };
        std::vector<int> movedUnitIds;
        movedUnitIds.reserve(transfer->unitInstanceIds.size());
        for (const int unitId : transfer->unitInstanceIds)
        {
            BattleUnit* unit = player.roster.FindUnit(unitId);
            if (!UnitAssignmentService::AssignReserve(
                    *unit, journey.targetProvinceId, transfer->destinationBarracksBuildingId))
            {
                valid = false;
                break;
            }
            movedUnitIds.push_back(unitId);
        }
        if (!valid)
        {
            for (const int movedUnitId : movedUnitIds)
            {
                BattleUnit* unit = player.roster.FindUnit(movedUnitId);
                const auto originalIt = unit == nullptr
                    ? originalGroups.end() : originalGroups.find(unit->taskGroupId);
                if (unit != nullptr && originalIt != originalGroups.end())
                    UnitAssignmentService::AssignJourney(
                        *unit, journey.sourceProvinceId, journeyId,
                        originalIt->second.homeBarracksBuildingId);
            }
            continue;
        }
        bool relocated = true;
        for (const TaskGroupId groupId : groups)
            if (!player.taskGroups.Relocate(groupId, journey.targetProvinceId,
                                            transfer->destinationBarracksBuildingId))
            {
                relocated = false;
                break;
            }
        if (!relocated)
        {
            restoreJourneyAssignments();
            continue;
        }
        transfer->unitInstanceIds.clear();
    }
}

bool GameWorld::CompleteColonization(const ColonizationOperation& operation)
{
    auto playerIt = playerHandler.players.find(static_cast<int>(operation.playerId));
    auto* target = globalMap.FindBuildableProvince(operation.targetProvinceId);
    if (playerIt == playerHandler.players.end() || playerIt->second == nullptr ||
        target == nullptr || target->GetOwnerId() != InvalidPlayerId ||
        target->GetSimulation() != nullptr)
        return false;

    Player* player = playerIt->second.get();
    MapParameters localParams = campaignGenerationParameters.localMap;
    const auto& provinceParameters = target->GetParameters();
    localParams.sizeX = std::clamp(provinceParameters.sizeX, 1,
                                   PersistenceLimits::MaxMapDimension);
    localParams.sizeY = std::clamp(provinceParameters.sizeY, 1,
                                   PersistenceLimits::MaxMapDimension);
    localParams.seed = GlobalMapGenerator::DeriveProvinceSeed(
        campaignGenerationParameters.globalMap.seed, operation.targetProvinceId);
    localParams.aiOpponentCount = 0;
    localParams.aiDifficulty = 0;
    if (!provinceParameters.naturalResourceTypes.empty())
        MapGenerator::FilterResourcePatchesForProfile(
            localParams, provinceParameters.naturalResourceTypes);

    auto candidate = std::make_unique<ProvinceSimulation>(
        operation.targetProvinceId, operation.playerId);
    WorldLayoutResult layout = GenerateWorldLayout(candidate->GetTileMap(), localParams, 1);
    if (!layout.success)
        return false;

    // ProvinceSimulation constructs its economy before terrain generation,
    // when the owned TileMap is still empty. Rebind after GenerateWorldLayout
    // so the navigation mirror has the final dimensions before the starting
    // village can request its first supply shipment.
    candidate->GetEconomy().BindTileMap(candidate->GetTileMap());

    const ProvinceId previousActiveProvince = player->GetActiveProvinceId();
    player->BindProvince(operation.targetProvinceId, *candidate);
    if (!player->SetActiveProvince(operation.targetProvinceId))
    {
        player->UnbindProvince(operation.targetProvinceId);
        return false;
    }

    CreateStartingHq(player, layout.anchors.front(), localParams.seed,
                     candidate->GetTileMap());
    CreateStartingVillageAndResources(player, layout.anchors.front(), localParams.seed,
                     candidate->GetTileMap());
    const auto& buildings = candidate->GetEconomy().dataTracker.buildings;
    const bool hasHeadquarters = std::any_of(buildings.begin(), buildings.end(),
        [](const Building* building)
        {
            return building != nullptr && building->buildingType == BuildingType::Headquarters;
        });
    const bool hasVillage = std::any_of(buildings.begin(), buildings.end(),
        [](const Building* building)
        {
            return building != nullptr && building->buildingType == BuildingType::Village;
        });
    if (previousActiveProvince != InvalidProvinceId)
        player->SetActiveProvince(previousActiveProvince);
    else
        player->SetActiveProvince(player->homeProvinceId);
    if (!hasHeadquarters || !hasVillage)
    {
        player->UnbindProvince(operation.targetProvinceId);
        return false;
    }

    // The candidate already owns a fully generated economy and base. The
    // following two authority calls are the single commit point for ownership
    // and discovery, so no failed generation can leave an owner without a map.
    if (!target->InstallSimulation(std::move(candidate)) ||
        !globalMap.SetBuildableOwner(operation.playerId, operation.targetProvinceId))
    {
        player->UnbindProvince(operation.targetProvinceId);
        return false;
    }
    player->BindProvince(operation.targetProvinceId, *target->GetSimulation());
    globalMap.InitializeDiscovery(operation.playerId, operation.targetProvinceId);
    return true;
}

bool GameWorld::CompleteAutomaticColonization(Player& player, ProvinceId targetProvinceId)
{
    auto* target = globalMap.FindBuildableProvince(targetProvinceId);
    if (target == nullptr || target->GetOwnerId() != InvalidPlayerId ||
        target->GetSimulation() != nullptr)
        return false;

    MapParameters localParams = campaignGenerationParameters.localMap;
    const auto& provinceParameters = target->GetParameters();
    localParams.sizeX = std::clamp(provinceParameters.sizeX, 1,
                                   PersistenceLimits::MaxMapDimension);
    localParams.sizeY = std::clamp(provinceParameters.sizeY, 1,
                                   PersistenceLimits::MaxMapDimension);
    localParams.seed = GlobalMapGenerator::DeriveProvinceSeed(
        campaignGenerationParameters.globalMap.seed, targetProvinceId);
    localParams.aiOpponentCount = 0;
    localParams.aiDifficulty = 0;
    if (!provinceParameters.naturalResourceTypes.empty())
        MapGenerator::FilterResourcePatchesForProfile(
            localParams, provinceParameters.naturalResourceTypes);

    auto candidate = std::make_unique<ProvinceSimulation>(targetProvinceId, player.id);
    const WorldLayoutResult layout = GenerateWorldLayout(
        candidate->GetTileMap(), localParams, 1);
    if (!layout.success)
        return false;

    candidate->GetEconomy().BindTileMap(candidate->GetTileMap());

    const ProvinceId previousActiveProvince = player.GetActiveProvinceId();
    player.BindProvince(targetProvinceId, *candidate);
    if (!player.SetActiveProvince(targetProvinceId))
    {
        player.UnbindProvince(targetProvinceId);
        return false;
    }
    CreateStartingHq(&player, layout.anchors.front(), localParams.seed,
                    candidate->GetTileMap());
    CreateStartingVillageAndResources(&player, layout.anchors.front(), localParams.seed,
                                      candidate->GetTileMap());
    const auto& buildings = candidate->GetEconomy().dataTracker.buildings;
    const bool hasHeadquarters = std::any_of(buildings.begin(), buildings.end(),
        [](const Building* building)
        {
            return building != nullptr && building->buildingType == BuildingType::Headquarters;
        });
    const bool hasVillage = std::any_of(buildings.begin(), buildings.end(),
        [](const Building* building)
        {
            return building != nullptr && building->buildingType == BuildingType::Village;
        });
    if (previousActiveProvince != InvalidProvinceId)
        player.SetActiveProvince(previousActiveProvince);
    else
        player.SetActiveProvince(player.homeProvinceId);
    if (!hasHeadquarters || !hasVillage)
    {
        player.UnbindProvince(targetProvinceId);
        return false;
    }

    if (!target->InstallSimulation(std::move(candidate)) ||
        !globalMap.SetBuildableOwner(player.id, targetProvinceId))
    {
        player.UnbindProvince(targetProvinceId);
        return false;
    }
    player.BindProvince(targetProvinceId, *target->GetSimulation());
    globalMap.InitializeDiscovery(player.id, targetProvinceId);
    return true;
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
    const bool isLocalMapCommand = command.type == GameCommandType::BuildBuilding ||
                                   command.type == GameCommandType::DestroyBuilding ||
                                   command.type == GameCommandType::SetReceiver ||
                                   command.type == GameCommandType::StartTechnologyResearch ||
                                   command.type == GameCommandType::RecruitUnit ||
                                   command.type == GameCommandType::UpgradeBuilding ||
                                    command.type == GameCommandType::SetRecipe ||
                                    command.type == GameCommandType::SetProductionBlocked ||
                                    command.type == GameCommandType::SetRoadPriority ||
                                    command.type == GameCommandType::AssignUnitsToGarrison ||
                                    command.type == GameCommandType::ReturnUnitsToBarracks ||
                                    command.type == GameCommandType::CreateTaskGroup ||
                                    command.type == GameCommandType::AddUnitsToTaskGroup ||
                                    command.type == GameCommandType::RemoveUnitsFromTaskGroup ||
                                    command.type == GameCommandType::DisbandTaskGroup;
    ProvinceSimulation* provinceSimulation = nullptr;
    ProvinceEconomy* economy = nullptr;
    if (isLocalMapCommand)
    {
        const ProvinceId provinceId = command.provinceId == InvalidProvinceId
            ? player->homeProvinceId : command.provinceId;
        auto* province = provinceId == InvalidProvinceId
            ? nullptr : globalMap.FindBuildableProvince(provinceId);
        provinceSimulation = province != nullptr ? province->GetSimulation() : nullptr;
        economy = provinceSimulation != nullptr ? &provinceSimulation->GetEconomy() : nullptr;
        if (provinceId == InvalidProvinceId || province == nullptr ||
            province->GetOwnerId() != player->id || economy == nullptr)
            return false;
    }
    TileMap* localMap = economy != nullptr ? economy->tilemap : player->GetTileMap();
    if (localMap == nullptr)
        return false;
    TileMap& tilemap = *localMap;
    auto acceptCommand = [&]()
    {
        const ProvinceId commandProvinceId = economy != nullptr
            ? economy->provinceId
            : (command.provinceId != InvalidProvinceId
                   ? command.provinceId : player->homeProvinceId);
        player->TrackAcceptedCommand(command.type, commandProvinceId);
        return true;
    };

    if (command.type == GameCommandType::StartTrade)
    {
        if (activeTradeOrders.size() >= MaxActiveTradeOrders || nextTradeOrderId == 0 ||
            nextTradeOrderId == std::numeric_limits<std::uint64_t>::max())
            return false;

        auto* source = globalMap.FindBuildableProvince(command.provinceId);
        auto* sourceSimulation = source != nullptr ? source->GetSimulation() : nullptr;
        auto* city = dynamic_cast<NeutralCityProvince*>(
            globalMap.FindProvince(command.targetProvinceId));
        const TradeRequest request{command.tradeOfferType, command.tradeRequestType,
                                   command.tradeRequestedAmount, command.tradeOfferedAmount,
                                   command.tradeMode};
        if (source == nullptr || sourceSimulation == nullptr || city == nullptr ||
            source->GetOwnerId() != player->id || source->GetKnowledge(player->id) < ProvinceKnowledgeLevel::Scouted ||
            city->GetKnowledge(player->id) < ProvinceKnowledgeLevel::Scouted ||
            request.offerType == ResourceType::Null || request.requestType == ResourceType::Null ||
            request.offerType == request.requestType || request.requestedAmount <= 0 ||
            request.offeredAmount < 0 || request.mode < TradeMode::Coin ||
            request.mode > TradeMode::Barter || command.expectedCityRevision == 0)
            return false;

        const auto route = TradeRouteService::FindRoute(
            globalMap, source->GetId(), city->GetId(), player->id);
        if (!route.has_value() || route->path.empty())
            return false;

        TradeQuote quote = TradePricingService::Calculate(
            *city, source->GetId(), city->GetId(), player->id, request);
        if (!quote.valid)
            return false;
        quote.route = *route;
        quote.requiredOfferAmount = player->ModifyBalanceInt(
            BalanceStat::TradeExchangeRate, quote.requiredOfferAmount,
            BuildingType::Building, request.offerType, 1);
        if (request.offeredAmount < quote.requiredOfferAmount)
            return false;

        StockpileTradeInventory inventory(sourceSimulation->GetEconomy());
        if (inventory.Get(request.offerType) < quote.requiredOfferAmount)
            return false;

        TradeOrder order;
        if (!TradeService::StartOrder(*city, inventory, quote,
                                      command.expectedCityRevision, nextTradeOrderId,
                                      player->id, order))
            return false;

        WorldJourney journey;
        if (!TradeService::BuildJourney(order, quote, journey))
        {
            TradeService::RefundOrder(*city, inventory, order);
            return false;
        }
        const WorldJourneyRules journeyRules = ResolveJourneyRules(*player, 100);
        const WorldJourneyStartResult start = armyJourneySystem.Start(
            std::move(journey), globalMap, simulationTick, journeyRules);
        if (!start)
        {
            TradeService::RefundOrder(*city, inventory, order);
            return false;
        }

        order.journeyId = start.journeyId;
        const auto [orderIt, inserted] = activeTradeOrders.emplace(order.id, order);
        if (!inserted)
        {
            armyJourneySystem.Cancel(order.journeyId);
            TradeService::RefundOrder(*city, inventory, order);
            return false;
        }
        (void)orderIt;
        ++nextTradeOrderId;
        return acceptCommand();
    }

    if (command.type == GameCommandType::StartProvinceAttack)
    {
        std::vector<int> unitIds = command.unitInstanceIds;
        if (!command.taskGroupIds.empty())
        {
            std::string failureReason;
            if (!ExpandReadyTaskGroups(*player, command.provinceId, command.taskGroupIds,
                                       unitIds, failureReason))
                return false;
        }
        std::map<PlayerId, Player*> players;
        for (const auto& [playerId, candidate] : playerHandler.players)
            if (candidate != nullptr)
                players.emplace(playerId, candidate.get());
        BattleId battleId = InvalidBattleId;
        std::string failureReason;
        if (!battleSystem.StartProvinceAttack(*player, command.provinceId,
                                               command.targetProvinceId,
                                               unitIds, globalMap,
                                               armyJourneySystem, players, simulationTick,
                                               campaignGenerationParameters.globalMap.seed,
                                               battleId, failureReason))
            return false;
        return acceptCommand();
    }

    if (command.type == GameCommandType::AssignUnitsToGarrison)
    {
        std::vector<int> unitIds = command.unitInstanceIds;
        int sourceBarracksId = command.sourceTileId;
        if (command.taskGroupId != InvalidTaskGroupId)
        {
            const TaskGroup* group = player->taskGroups.Find(command.taskGroupId);
            std::string failureReason;
            if (group == nullptr ||
                !ExpandReadyTaskGroups(*player, command.provinceId,
                                       {command.taskGroupId}, unitIds, failureReason))
                return false;
            sourceBarracksId = group->homeBarracksBuildingId;
        }
        std::string failureReason;
        if (!GarrisonService::AssignUnitsToGarrison(*player, *economy,
                                                     sourceBarracksId, command.targetTileId,
                                                     unitIds, failureReason))
            return false;
        return acceptCommand();
    }

    if (command.type == GameCommandType::ReturnUnitsToBarracks)
    {
        std::vector<int> unitIds = command.unitInstanceIds;
        int targetBarracksId = command.targetTileId;
        if (command.taskGroupId != InvalidTaskGroupId)
        {
            const TaskGroup* group = player->taskGroups.Find(command.taskGroupId);
            if (group == nullptr || group->stationProvinceId != command.provinceId ||
                command.targetTileId != group->homeBarracksBuildingId)
                return false;
            for (const auto& [unitId, unit] : player->roster.units)
                if (unit.taskGroupId == command.taskGroupId &&
                    unit.assignment.kind == UnitAssignmentKind::DefensiveGarrison)
                    unitIds.push_back(unitId);
            if (unitIds.empty())
                return false;
            targetBarracksId = group->homeBarracksBuildingId;
            std::sort(unitIds.begin(), unitIds.end());
        }
        std::string failureReason;
        if (!GarrisonService::ReturnUnitsToBarracks(*player, *economy,
                                                    command.sourceTileId, targetBarracksId,
                                                    unitIds, failureReason))
            return false;
        return acceptCommand();
    }

    if (command.type == GameCommandType::CreateTaskGroup)
    {
        // Task groups persist a building id, not a tile id. Building ids are
        // intentionally independent from map positions (and include a
        // province/player prefix), so looking them up through TileMap treated
        // most valid Barracks as an out-of-range tile.
        Building* barracks = FindProvinceBuilding(*economy, command.sourceTileId);
        if (barracks == nullptr || barracks->owner != player ||
            barracks->buildingType != BuildingType::Barracks ||
            barracks->IsUnderConstruction())
            return false;

        TaskGroupId createdId = InvalidTaskGroupId;
        if (!player->taskGroups.Create(economy->provinceId, barracks->id, createdId))
            return false;
        return acceptCommand();
    }

    if (command.type == GameCommandType::AddUnitsToTaskGroup ||
        command.type == GameCommandType::RemoveUnitsFromTaskGroup ||
        command.type == GameCommandType::DisbandTaskGroup)
    {
        const TaskGroup* group = player->taskGroups.Find(command.taskGroupId);
        if (group == nullptr || group->stationProvinceId != economy->provinceId)
            return false;

        std::string failureReason;
        bool changed = false;
        if (command.type == GameCommandType::AddUnitsToTaskGroup)
        {
            changed = TaskGroupService::AddUnits(player->taskGroups, command.taskGroupId,
                                                 player->id, command.unitInstanceIds,
                                                 player->roster, failureReason);
        }
        else if (command.type == GameCommandType::RemoveUnitsFromTaskGroup)
        {
            changed = TaskGroupService::RemoveUnits(player->taskGroups, command.taskGroupId,
                                                    player->id, command.unitInstanceIds,
                                                    player->roster, failureReason);
        }
        else
        {
            changed = TaskGroupService::Disband(player->taskGroups, command.taskGroupId,
                                                player->id, player->roster, failureReason);
        }
        return changed ? acceptCommand() : false;
    }

    if (command.type == GameCommandType::SpawnDebugRaid)
    {
        auto* target = globalMap.FindBuildableProvince(command.targetProvinceId);
        if (!player->debugMode || target == nullptr ||
            target->GetOwnerId() != player->id || command.raidStrength <= 0)
            return false;
        BattleId battleId = InvalidBattleId;
        std::string failureReason;
        if (!battleSystem.StartRaid(*player, command.targetProvinceId,
                                    command.raidStrength, globalMap, simulationTick,
                                    campaignGenerationParameters.globalMap.seed,
                                    battleId, failureReason))
            return false;
        return acceptCommand();
    }

    if (command.type == GameCommandType::StartResourceTransfer)
    {
        auto* source = globalMap.FindBuildableProvince(command.provinceId);
        auto* target = globalMap.FindBuildableProvince(command.targetProvinceId);
        auto* sourceSimulation = source != nullptr ? source->GetSimulation() : nullptr;
        auto* targetSimulation = target != nullptr ? target->GetSimulation() : nullptr;
        std::vector<ProvinceConnectionId> path;
        if (source == nullptr || target == nullptr || sourceSimulation == nullptr ||
            targetSimulation == nullptr || source == target ||
            source->GetOwnerId() != player->id || target->GetOwnerId() != player->id ||
            command.resourceCargo.empty() || command.resourceCargo.size() >
                GameCommand::MaxResourceCargoTypes ||
            !globalMap.FindShortestPath(source->GetId(), target->GetId(), path) || path.empty())
            return false;
        for (std::size_t i = 0; i < command.resourceCargo.size(); ++i)
        {
            const auto& cargo = command.resourceCargo[i];
            if (cargo.type == ResourceType::Null || cargo.amount <= 0 ||
                (i > 0 && static_cast<int>(command.resourceCargo[i - 1].type) >=
                    static_cast<int>(cargo.type)) ||
                StockpileIndex::GetTotal(sourceSimulation->GetEconomy(), cargo.type) < cargo.amount)
                return false;
        }
        std::vector<ResourceAmount> consumedCargo;
        consumedCargo.reserve(command.resourceCargo.size());
        for (const auto& cargo : command.resourceCargo)
        {
            const int consumed = StockpileIndex::Consume(sourceSimulation->GetEconomy(),
                                                         cargo.type, cargo.amount);
            if (consumed != cargo.amount)
            {
                for (const auto& restored : consumedCargo)
                    StockpileIndex::Deposit(sourceSimulation->GetEconomy(), restored.type,
                                            restored.amount);
                if (consumed > 0)
                    StockpileIndex::Deposit(sourceSimulation->GetEconomy(), cargo.type,
                                            consumed);
                return false;
            }
            else
                consumedCargo.push_back(cargo);
        }
        WorldJourney journey;
        journey.ownerId = player->id;
        journey.sourceProvinceId = source->GetId();
        journey.targetProvinceId = target->GetId();
        journey.kind = WorldJourneyKind::ResourceTransfer;
        journey.legPlan.reserve(path.size());
        for (const ProvinceConnectionId connectionId : path)
            journey.legPlan.push_back({connectionId});
        journey.payload = ResourceConvoy{command.resourceCargo};
        const WorldJourneyStartResult start = armyJourneySystem.Start(
            std::move(journey), globalMap, simulationTick,
            ResolveJourneyRules(*player, 100));
        if (!start)
        {
            for (const auto& cargo : command.resourceCargo)
                StockpileIndex::Deposit(sourceSimulation->GetEconomy(), cargo.type,
                                        cargo.amount);
            return false;
        }
        return acceptCommand();
    }

    if (command.type == GameCommandType::StartArmyTransfer)
    {
        auto* source = globalMap.FindBuildableProvince(command.provinceId);
        auto* target = globalMap.FindBuildableProvince(command.targetProvinceId);
        auto* sourceSimulation = source != nullptr ? source->GetSimulation() : nullptr;
        auto* targetSimulation = target != nullptr ? target->GetSimulation() : nullptr;
        if (source == nullptr || target == nullptr || sourceSimulation == nullptr ||
            targetSimulation == nullptr || source == target ||
            source->GetOwnerId() != player->id || target->GetOwnerId() != player->id ||
            command.destinationBarracksId <= 0)
            return false;
        auto findBuilding = [](ProvinceEconomy& economy, int id) -> Building*
        {
            for (Building* building : economy.dataTracker.buildings)
                if (building != nullptr && building->id == id)
                    return building;
            return nullptr;
        };
        Building* destinationBarracks = findBuilding(targetSimulation->GetEconomy(),
                                                     command.destinationBarracksId);
        if (destinationBarracks == nullptr || destinationBarracks->owner != player ||
            destinationBarracks->buildingType != BuildingType::Barracks ||
            destinationBarracks->IsUnderConstruction())
            return false;
        std::vector<int> unitIds;
        std::string failureReason;
        if (!ExpandReadyTaskGroups(*player, command.provinceId, command.taskGroupIds,
                                   unitIds, failureReason))
            return false;
        double slowestMoveSpeed = std::numeric_limits<double>::max();
        for (const int unitId : unitIds)
        {
            const BattleUnit* unit = player->roster.FindUnit(unitId);
            if (unit == nullptr)
                return false;
            const double speed = unit->GetEffectiveMoveSpeed(*player);
            if (!std::isfinite(speed) || speed <= 0.0)
                return false;
            slowestMoveSpeed = std::min(slowestMoveSpeed, speed);
        }
        std::vector<ProvinceConnectionId> path;
        if (!globalMap.FindShortestPath(source->GetId(), target->GetId(), path) || path.empty() ||
            slowestMoveSpeed * JourneyTiming::BasisPoints > std::numeric_limits<int>::max())
            return false;
        WorldJourney journey;
        journey.ownerId = player->id;
        journey.sourceProvinceId = source->GetId();
        journey.targetProvinceId = target->GetId();
        journey.kind = WorldJourneyKind::ArmyTransfer;
        journey.legPlan.reserve(path.size());
        for (const ProvinceConnectionId connectionId : path)
            journey.legPlan.push_back({connectionId});
        const int moverSpeedBasisPoints = static_cast<int>(std::llround(
            slowestMoveSpeed * JourneyTiming::BasisPoints));
        WorldJourneyRules journeyRules = ResolveJourneyRules(*player, 100);
        journeyRules.speedProfile.moverSpeedBasisPoints = moverSpeedBasisPoints;
        journey.payload = ArmyTransferParty{unitIds, command.destinationBarracksId};
        const WorldJourneyStartResult start = armyJourneySystem.Start(
            std::move(journey), globalMap, simulationTick, journeyRules);
        if (!start)
            return false;
        const WorldJourneyId journeyId = start.journeyId;
        for (const int unitId : unitIds)
        {
            BattleUnit* unit = player->roster.FindUnit(unitId);
            const TaskGroup* group = unit == nullptr
                ? nullptr : player->taskGroups.Find(unit->taskGroupId);
            if (unit == nullptr || group == nullptr || !UnitAssignmentService::AssignJourney(
                    *unit, source->GetId(), journeyId, group->homeBarracksBuildingId))
            {
                for (const int rollbackId : unitIds)
                {
                    BattleUnit* rollback = player->roster.FindUnit(rollbackId);
                    const TaskGroup* rollbackGroup = rollback == nullptr
                        ? nullptr : player->taskGroups.Find(rollback->taskGroupId);
                    if (rollback != nullptr && rollbackGroup != nullptr)
                        UnitAssignmentService::AssignReserve(
                            *rollback, source->GetId(), rollbackGroup->homeBarracksBuildingId);
                }
                armyJourneySystem.Cancel(journeyId);
                return false;
            }
        }
        return acceptCommand();
    }

    if (command.type == GameCommandType::ColonizeProvince)
    {
        auto* source = globalMap.FindBuildableProvince(command.provinceId);
        auto* target = globalMap.FindBuildableProvince(command.targetProvinceId);
        auto* sourceSimulation = source != nullptr ? source->GetSimulation() : nullptr;
        const auto* definition = FindColonizationDefinition("frontier_settlement");
        if (source == nullptr || target == nullptr || sourceSimulation == nullptr ||
            definition == nullptr)
            return false;

        const ColonizationQuote quote = BuildColonizationQuote(
            globalMap, *player, sourceSimulation->GetEconomy(), source->GetId(), target->GetId(),
            *definition, pendingColonizations.contains(target->GetId()),
            pendingColonizations.size(), PersistenceLimits::MaxColonizationOperations);
        if (!quote.allowed)
            return false;

        std::vector<ColonizationCost> consumed;
        for (const auto& cost : quote.costs)
        {
            const int consumedAmount = StockpileIndex::Consume(
                sourceSimulation->GetEconomy(), cost.type, cost.amount);
            if (consumedAmount != cost.amount)
            {
                for (const auto& restored : consumed)
                    StockpileIndex::Deposit(sourceSimulation->GetEconomy(), restored.type,
                                            restored.amount);
                StockpileIndex::Deposit(sourceSimulation->GetEconomy(), cost.type,
                                        consumedAmount);
                return false;
            }
            consumed.push_back(cost);
        }

        WorldJourney journey;
        journey.ownerId = player->id;
        journey.sourceProvinceId = source->GetId();
        journey.targetProvinceId = target->GetId();
        journey.kind = WorldJourneyKind::Colonization;
        journey.legPlan.reserve(quote.travel.legs.size());
        for (const auto& leg : quote.travel.legs)
            journey.legPlan.push_back({leg.connectionId});
        journey.payload = Colonists{1};
        const WorldJourneyRules journeyRules = ResolveJourneyRules(*player, 100);
        const WorldJourneyStartResult start = armyJourneySystem.Start(
            std::move(journey), globalMap, simulationTick, journeyRules);
        if (!start)
        {
            for (const auto& cost : quote.costs)
                StockpileIndex::Deposit(sourceSimulation->GetEconomy(), cost.type, cost.amount);
            return false;
        }
        pendingColonizations.emplace(target->GetId(), ColonizationOperation{
            player->id, source->GetId(), target->GetId(), start.journeyId,
            ColonizationPhase::Traveling, 0, quote.settlementDurationTicks, quote.costs});
        return acceptCommand();
    }

    if (command.type == GameCommandType::UpgradeProvinceConnection)
    {
        auto* source = globalMap.FindBuildableProvince(command.provinceId);
        auto* sourceSimulation = source != nullptr ? source->GetSimulation() : nullptr;
        auto* connection = dynamic_cast<LandRouteConnection*>(
            globalMap.FindConnection(command.connectionId));
        if (source == nullptr || sourceSimulation == nullptr ||
            source->GetOwnerId() != player->id || connection == nullptr ||
            connection->GetNextLevelDefinition() == nullptr ||
            connection->IsUpgradeInProgress() ||
            (connection->GetFirstProvinceId() != source->GetId() &&
             connection->GetSecondProvinceId() != source->GetId()))
            return false;

        const ProvinceId otherProvinceId =
            connection->GetFirstProvinceId() == source->GetId()
                ? connection->GetSecondProvinceId() : connection->GetFirstProvinceId();
        const auto* otherProvince = globalMap.FindProvince(otherProvinceId);
        if (otherProvince == nullptr ||
            source->GetKnowledge(player->id) < ProvinceKnowledgeLevel::Scouted ||
            otherProvince->GetKnowledge(player->id) < ProvinceKnowledgeLevel::Scouted)
            return false;

        const auto* nextLevel = connection->GetNextLevelDefinition();
        for (const auto& cost : nextLevel->upgradeCost)
            if (cost.amount <= 0 ||
                StockpileIndex::GetTotal(sourceSimulation->GetEconomy(), cost.type) < cost.amount)
                return false;

        for (const auto& cost : nextLevel->upgradeCost)
            StockpileIndex::Consume(sourceSimulation->GetEconomy(), cost.type, cost.amount);
        if (!connection->BeginUpgrade(nextLevel->level, nextLevel->upgradeDurationTicks))
        {
            for (const auto& cost : nextLevel->upgradeCost)
                StockpileIndex::Deposit(sourceSimulation->GetEconomy(), cost.type, cost.amount);
            return false;
        }
        return acceptCommand();
    }

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
        const bool freeBuild = ResolveBuildPaymentPolicy(tilemap, *player) == BuildPaymentPolicy::FreeDebugHuman;
        const auto effectiveCosts = player->GetEffectiveBuildCosts(definition);
        if (!freeBuild)
        {
            auto failures = player->GetBuildRequirementFailures(definition, *economy);
            if (!failures.empty())
            {
                Log::Msg("[GameWorld]", "Command rejected: ", definition.name, " locked by ", failures.front());
                return false;
            }
        }
        if (!freeBuild && !player->TryPayBuildCost(*economy, effectiveCosts))
        {
            Log::Msg("[GameWorld]", "Command rejected: not enough resources to build ", definition.name);
            return false;
        }

        int tileId = tilemap.GetIdFromCoords(command.tilePos);
        const int buildingId = economy->build.buildingIdPrefix + economy->build.buildingId++;
        auto building = CreateBuildingFromType(command.buildingType, buildingId);
        if (building == nullptr)
        {
            if (!freeBuild)
                player->RefundBuildCost(*economy, effectiveCosts);
            return false;
        }

        double buildTime = player->ModifyBalanceAt(
            BalanceStat::BuildTime, definition.buildTime, economy->provinceId,
            command.buildingType, command.tilePos);
        building->buildTime = buildTime;
        building->constructionRemaining = freeBuild ? 0.0 : buildTime;
        Building* placed = provinceSimulation->PlaceBuilding(*player, tileId, std::move(building));
        if (placed == nullptr)
        {
            if (!freeBuild)
                player->RefundBuildCost(*economy, effectiveCosts);
            return false;
        }
        placed->buildCostRecordState = freeBuild ? BuildCostRecordState::Free
                                                  : BuildCostRecordState::PaidRecorded;
        placed->buildCostWasPaid = !freeBuild;
        placed->paidBuildCosts = freeBuild ? std::vector<ResourceAmountDefinition>{} : effectiveCosts;

        if (placed->IsUnderConstruction())
        {
            return acceptCommand();
        }

        if (economy->roadNetwork != nullptr)
        {
            for (int occupiedTileId : tilemap.GetBuildingTileIds(placed))
                economy->roadNetwork->UpdateNavMap(occupiedTileId, placed);
        }
        tilemap.AutoConnectBuilding(placed);
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

        if (!provinceSimulation->DestroyBuilding(*player, command.sourceTileId))
            return false;
        return acceptCommand();
    }

    if (command.type == GameCommandType::UpgradeBuilding)
    {
        Building* building = tilemap.GetBuilding(command.sourceTileId);
        if (building == nullptr || building->owner != player)
            return false;

        auto* upgrade = building->GetComponent<UpgradeComponent>();
        if (building->IsUnderConstruction() || upgrade == nullptr ||
            upgrade->isUpgrading || upgrade->level >= upgrade->maxLevel)
            return false;

        const auto& definition = GetBuildingDefinition(building->buildingType);
        int targetLevel = upgrade->level + 1;
        const auto* levelDefinition = FindUpgradeLevelDefinition(definition, targetLevel);
        if (levelDefinition == nullptr)
            return false;

        const auto unlockFailures = player->GetBuildUnlockRequirementFailures(definition);
        if (!unlockFailures.empty())
        {
            Log::Msg("[GameWorld]", "Command rejected: ", building->name,
                     " upgrade locked by ", unlockFailures.front());
            return false;
        }

        if (!player->TryPayBuildCost(*economy, levelDefinition->cost))
        {
            Log::Msg("[GameWorld]", "Command rejected: not enough resources to upgrade ", building->name);
            return false;
        }

        if (!provinceSimulation->BeginUpgrade(*player, command.sourceTileId,
                                              levelDefinition->buildTime))
        {
            player->RefundBuildCost(*economy, levelDefinition->cost);
            return false;
        }
        return acceptCommand();
    }

    if (command.type == GameCommandType::SetRecipe)
    {
        Building* building = tilemap.GetBuilding(command.sourceTileId);
        if (building == nullptr || building->owner != player || building->IsUnderConstruction())
            return false;

        if (!provinceSimulation->SetRecipe(*player, command.sourceTileId, command.targetTileId))
            return false;
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

        if (!provinceSimulation->SetProductionBlocked(*player, command.sourceTileId,
                                                      command.targetTileId == 1))
            return false;
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

        if (!provinceSimulation->SetRoadPriority(*player, command.sourceTileId, resource))
            return false;
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

        if (!provinceSimulation->ConnectReceiver(*player, command.sourceTileId,
                                                 command.targetTileId,
                                                 command.alternativeReceiver))
            return false;
        return acceptCommand();
    }

    if (command.type == GameCommandType::StartFocus)
    {
        if (command.researchId.empty() || !player->CanUnlockFocus(command.researchId))
            return false;

        if (!player->StartFocus(command.researchId))
            return false;
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

        if (command.researchId.empty() ||
            !player->CanResearchTechnology(command.researchId, *economy))
            return false;

        if (!provinceSimulation->StartTechnologyResearch(*player, command.sourceTileId,
                                                         command.researchId))
            return false;
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

        if (!provinceSimulation->RecruitUnit(*player, command.sourceTileId, command.researchId))
            return false;

        return acceptCommand();
    }

    if (command.type == GameCommandType::StartScoutExpedition ||
        command.type == GameCommandType::ScoutProvince)
    {
        ProvinceId sourceProvinceId = command.provinceId;
        ProvinceId targetProvinceId = command.targetProvinceId;
        std::vector<int> scoutIds = command.unitInstanceIds;
        if (command.type == GameCommandType::ScoutProvince)
        {
            // Current clients carry their active province explicitly. Do not
            // silently switch a current command to another province.
            const bool sourceWasExplicit = sourceProvinceId != InvalidProvinceId;
            for (auto& [instanceId, unit] : player->roster.units)
            {
                const auto* definition = FindUnitDefinition(unit.unitDefId);
                if (definition == nullptr || definition->role != UnitRole::Scout)
                    continue;

                if (!unit.assignment.IsStructurallyValid())
                    continue;
                const ProvinceId candidateSource = unit.assignment.provinceId;
                if (sourceWasExplicit && candidateSource != sourceProvinceId)
                    continue;
                const auto* source = globalMap.FindBuildableProvince(candidateSource);
                if (source == nullptr || source->GetOwnerId() != player->id ||
                    !UnitAssignmentService::IsAvailableFromReserve(unit, candidateSource))
                    continue;

                sourceProvinceId = candidateSource;
                scoutIds.push_back(instanceId);
                break;
            }
        }
        const ExpeditionDefinition* expeditionDefinition = FindExpeditionDefinition("scout");
        if (expeditionDefinition == nullptr)
            return false;
        const double strategicSupply = player->strategicResources.Get(
            expeditionDefinition->supplyResource);
        const bool useStrategicSupply = strategicSupply >= expeditionDefinition->supplyCost;
        auto* sourceProvince = globalMap.FindBuildableProvince(sourceProvinceId);
        auto* sourceSimulation = sourceProvince != nullptr
            ? sourceProvince->GetSimulation() : nullptr;
        ProvinceEconomy* sourceEconomy = sourceSimulation != nullptr
            ? &sourceSimulation->GetEconomy() : nullptr;
        // SupplyPackages are the strategic abstraction used by expedition
        // definitions. Until a dedicated packaging building exists, one
        // package is assembled deterministically from one local
        // FOOD_PROVISIONS in the source province. This closes the otherwise
        // unreachable production loop while preserving old saves/tests that
        // already hold strategic packages.
        const bool canUseLocalProvisions =
            expeditionDefinition->supplyResource == StrategicResourceType::SupplyPackages &&
            sourceEconomy != nullptr &&
            StockpileIndex::GetTotal(*sourceEconomy, ResourceType::FOOD_PROVISIONS) >=
                expeditionDefinition->supplyCost;
        if (!useStrategicSupply && !canUseLocalProvisions)
            return false;
        WorldJourneyId journeyId = InvalidWorldJourneyId;
        std::string failureReason;
        const WorldJourneyRules journeyRules = ResolveJourneyRules(*player, 100);
        if (!ScoutExpeditionService::Start(player->id, sourceProvinceId, targetProvinceId,
                                           scoutIds, player->roster, globalMap,
                                           armyJourneySystem, simulationTick,
                                           journeyId, failureReason, journeyRules))
            return false;
        if (useStrategicSupply)
        {
            if (!player->strategicResources.Consume(
                    expeditionDefinition->supplyResource,
                    static_cast<double>(expeditionDefinition->supplyCost)))
                return false;
        }
        else if (StockpileIndex::Consume(*sourceEconomy, ResourceType::FOOD_PROVISIONS,
                                         expeditionDefinition->supplyCost) !=
                 expeditionDefinition->supplyCost)
            return false;
        return acceptCommand();
    }

    return false;
}
